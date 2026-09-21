#include "Engine.h"
#include "Voices.h"
#include "Binaural.h"
#include "MovingReflections.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <iostream>
#include <stdexcept>

// Offline diagnostic only. No changes to the instrument or live settings.
namespace {
void save(const juce::File& file,const juce::AudioBuffer<float>& b,double sr=48000){
    juce::WavAudioFormat format;auto stream=file.createOutputStream();
    if(!stream||!stream->setPosition(0)||stream->truncate().failed())throw std::runtime_error("WAV truncate failed");
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),sr,(unsigned)b.getNumChannels(),32,{},0));
    if(!writer||!writer->writeFromAudioSampleBuffer(b,0,b.getNumSamples()))throw std::runtime_error("WAV write failed");
}
tide::Settings reeds(){auto s=tide::room::patches[18].values;s.space=0;s.output=0;return s;}
void notes(const juce::File& out){
    for(const auto attack:{.002f,.005f,.012f}){
        auto s=reeds();s.attack=attack;tide::Engine engine;engine.setSettings(s);engine.prepare(48000,512,false);
        juce::AudioBuffer<float> audio(2,144000);audio.clear();
        int pos=0;const auto to=[&](int end){engine.render(audio.getWritePointer(0)+pos,audio.getWritePointer(1)+pos,end-pos);pos=end;};
        to(4800);engine.midi(juce::MidiMessage::noteOn(1,74,.523153603f));to(8389);engine.midi(juce::MidiMessage::noteOff(1,74));to(audio.getNumSamples());
        save(out.getChildFile("reeds-attack-"+juce::String(attack*1000,0)+"ms.wav"),audio);
    }
    // Current envelope with/without AM, retaining the carrier/folder recipe.
    for(const float depth:{0.f,1.f}){
        auto s=reeds();s.modDepth=depth;tide::Engine e;e.setSettings(s);e.prepare(48000,512,false);
        juce::AudioBuffer<float> audio(2,96000);audio.clear();e.render(audio.getWritePointer(0),audio.getWritePointer(1),4800);e.midi(juce::MidiMessage::noteOn(1,74,.523153603f));
        e.render(audio.getWritePointer(0)+4800,audio.getWritePointer(1)+4800,91200);save(out.getChildFile(depth==0?"reeds-no-am.wav":"reeds-current-am.wav"),audio);
    }
}
void replay(const juce::File& capture,const juce::File& out){
    auto xml=juce::XmlDocument::parse(capture.getChildFile("parameters-start.xml"));if(!xml)throw std::runtime_error("Missing parameters");
    const auto get=[&](juce::String id){for(auto* p:xml->getChildIterator())if(p->getStringAttribute("id")==id)return(float)p->getDoubleAttribute("value");throw std::runtime_error("Missing parameter");};
    const auto lines=juce::StringArray::fromLines(capture.getChildFile("notes.csv").loadFileAsString());
    const auto motionRows=juce::StringArray::fromLines(capture.getChildFile("positions.csv").loadFileAsString());
    const auto first=juce::StringArray::fromTokens(motionRows[1],",","");
    for(int part=1;part<3;++part){
        auto initial=tide::room::patches[(size_t)(int)get("p"+juce::String(part)+"_voice")].values;initial.space=0;initial.output=0;
        auto target=initial;target.timbre=get("p"+juce::String(part)+"_brightness");target.decay=get("p"+juce::String(part)+"_length");
        for(int variant=0;variant<(part==2?2:1);++variant){
            if(variant)initial.attack=target.attack=.002f;
            tide::Engine e;e.setSettings(initial);e.prepare(48000,512,false);e.setSettings(target);
            if(first.size()>=23){int warm=(int)std::llround((first[20+part].getDoubleValue()-512./48000.)*48000.);juce::AudioBuffer<float> silent(2,512);while(warm>0){const int n=std::min(512,warm);e.render(silent.getWritePointer(0),silent.getWritePointer(1),n);warm-=n;}}
            juce::AudioBuffer<float> audio(2,576000);audio.clear();int pos=0,maxVoices=0;
            auto to=[&](int end){e.render(audio.getWritePointer(0)+pos,audio.getWritePointer(1)+pos,end-pos);pos=end;maxVoices=std::max(maxVoices,e.activeVoices());};
            for(int k=1;k<lines.size();++k){const auto f=juce::StringArray::fromTokens(lines[k],",","");if(f.size()<5||f[1].getIntValue()!=part)continue;const int sample=f[0].getIntValue();if(sample>=audio.getNumSamples())break;to(sample);e.midi(f[4].getIntValue()?juce::MidiMessage::noteOn(1,f[2].getIntValue(),f[3].getFloatValue()):juce::MidiMessage::noteOff(1,f[2].getIntValue()));}
            to(audio.getNumSamples());save(out.getChildFile("replay-source-"+juce::String(part)+(variant?"-attack2":"-current")+".wav"),audio);std::cout<<"replay part="<<part<<" variant="<<variant<<" max_active_voices="<<maxVoices<<"\n";
        }
    }
}
void tones(const juce::File& out){
    // Coherent 1 Hz fundamental grid. AM sidebands are legitimate spectrum;
    // Python compares with a double precision Fourier reference, not a harmonic mask.
    for(const double f:{590.,8000.}){
        tide::Engine e;e.prepare(48000,512,false);juce::AudioBuffer<float> audio(1,96000);
        for(int n=0;n<audio.getNumSamples();++n)audio.setSample(0,n,e.ringProbe(f,1.4,1.f,2.5f,.12f,5));
        save(out.getChildFile("am-tone-"+juce::String((int)f)+".wav"),audio);
    }
}
void kernels(const juce::File& out){
    tide::room::HrtfBank bank;bank.prepare(48000);
    juce::AudioBuffer<float> all(2,101*bank.length());all.clear();
    for(int i=0;i<=100;++i){tide::room::HrtfBank::Kernel k;bank.interpolate(-50.f+i*.5f,-2.5f,k);for(int c=0;c<2;++c)all.copyFrom(c,i*bank.length(),k[(size_t)c].data(),bank.length());}
    save(out.getChildFile("hrtf-sweep-kernels.wav"),all);std::cout<<"HRTF length="<<bank.length()<<"\n";
}
void propagation(const juce::File& capture,const juce::File& out){
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(capture.getChildFile("live.wav")));
    if(!reader||reader->numChannels!=36)throw std::runtime_error("Expected 36-channel live capture");
    const int length=(int)reader->lengthInSamples;juce::AudioBuffer<float> live(36,length);
    reader->read(live.getArrayOfWritePointers(),36,0,length);
    tide::room::HrtfBank bank;bank.prepare(48000);
    juce::AudioBuffer<float> result(6,length);result.clear();
    const auto rows=juce::StringArray::fromLines(capture.getChildFile("positions.csv").loadFileAsString());
    float delay=0;const float follow=(float)-std::expm1(-1/(48000.*.05));
    for(int row=1;row<rows.size();++row){const auto f=juce::StringArray::fromTokens(rows[row],",","");if(f.size()<20)continue;
        const int offset=f[0].getIntValue(),count=std::min(512,length-offset);
        const float x=f[8].getFloatValue()*10.60000038146973f*.5f,y=f[9].getFloatValue()*11.10000038146973f,z=f[10].getFloatValue()-1.5f;
        const float horizontal=std::sqrt(x*x+y*y),distance=std::sqrt(horizontal*horizontal+z*z);
        const float target=(float)(distance/343.*48000.);if(offset==0)delay=target;
        for(int n=offset;n<offset+count;++n){delay+=follow*(target-delay);const int whole=(int)delay;const float fraction=delay-whole,phase=fraction*1024;const int lower=std::min(1023,(int)phase);const float blend=phase-lower;
            const auto* first=bank.delayKernel(lower);const auto* second=bank.delayKernel(lower+1);
            std::array<double,64> kernel{};double norm=0;
            for(int k=0;k<64;++k){const double d=k-31-fraction,a=juce::MathConstants<double>::pi*d;const double v=(std::abs(d)<1.e-12?1.:std::sin(a)/a)*(.42+.5*std::cos(a/32)+.08*std::cos(a/16));kernel[(size_t)k]=v;norm+=v;}
            for(int c=0;c<2;++c){auto sample=[&](int i){return i>=0&&i<length?live.getSample(24+c,i)+live.getSample(12+c,i)+live.getSample(18+c,i):0.f;};
                float native=0;for(int k=0;k<12;++k)native+=((1-blend)*first[k]+blend*second[k])*sample(n-whole+5-k);
                double reference=0;for(int k=0;k<64;++k)reference+=kernel[(size_t)k]/norm*sample(n-whole+31-k);
                result.setSample(c,n,native);result.setSample(c+2,n,(float)reference);result.setSample(c+4,n,delay);
            }
        }
    }
    save(out.getChildFile("propagation-native-and-64tap.wav"),result);
}
void motionTone(const juce::File& out){
    tide::room::HrtfBank bank;bank.prepare(48000);tide::room::BinauralSource source;source.prepare(48000,bank);
    tide::room::MovingReflections early;early.prepare(48000,bank);
    constexpr int total=48000*40;juce::AudioBuffer<float> audio(2,total),reflected(2,total),discard(2,512);std::array<float,512> mono{};
    for(int offset=0;offset<total;offset+=512){const int count=std::min(512,total-offset);
        const float desired=790.f+212.5f*(float)std::sin(2*juce::MathConstants<double>::pi*.4*offset/48000.);
        for(int n=0;n<count;++n)mono[(size_t)n]=.08f*(float)std::sin(2*juce::MathConstants<double>::pi*600*(offset+n)/48000.);
        source.render(mono.data(),discard.getWritePointer(0),discard.getWritePointer(1),count,0,desired*343.f/48000.f,0,true,false);
        early.render(mono.data(),reflected.getWritePointer(0)+offset,reflected.getWritePointer(1)+offset,count,0,desired*343.f/48000.f,0,10.6f,11.1f,5.1f,true,true);
        juce::AudioBuffer<float> b(audio.getArrayOfWritePointers(),2,offset,count);
        for(int c=0;c<2;++c)for(int n=0;n<count;++n)b.setSample(c,n,.08f*(float)std::sin(2*juce::MathConstants<double>::pi*600*(offset+n)/48000.));
        source.propagation(b,true);
    }
    juce::AudioBuffer<float> steady(audio.getArrayOfWritePointers(),2,48000*20,48000*20);save(out.getChildFile("motion-tone-actual.wav"),steady);
    juce::AudioBuffer<float> tail(reflected.getArrayOfWritePointers(),2,48000*20,48000*20);save(out.getChildFile("reflection-tone-actual.wav"),tail);
}
void refinedScene(const juce::File& capture,const juce::File& out){
    auto xml=juce::XmlDocument::parse(capture.getChildFile("parameters-start.xml"));if(!xml)throw std::runtime_error("Missing parameters");
    const auto get=[&](juce::String id){for(auto* p:xml->getChildIterator())if(p->getStringAttribute("id")==id)return(float)p->getDoubleAttribute("value");throw std::runtime_error("Missing parameter");};
    if(get("tapeOn")>.5f||get("movingReflections")<.5f||get("placement")>.5f)throw std::runtime_error("This comparison requires tape off / moving reflections / headphones");
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(capture.getChildFile("live.wav")));
    if(!reader||reader->numChannels!=36||reader->sampleRate!=48000.)throw std::runtime_error("Expected 36-channel 48 kHz capture");
    const int length=(int)reader->lengthInSamples;juce::AudioBuffer<float> live(36,length),result(2,length),discard(2,512),earlyAudio(2,512),combined(2,512);result.clear();reader->read(live.getArrayOfWritePointers(),36,0,length);
    tide::room::HrtfBank bank;bank.prepare(48000);std::array<tide::room::BinauralSource,3> spatial;std::array<tide::room::MovingReflections,3> early;
    for(int i=0;i<3;++i){spatial[(size_t)i].prepare(48000,bank);early[(size_t)i].prepare(48000,bank);}
    const auto rows=juce::StringArray::fromLines(capture.getChildFile("positions.csv").loadFileAsString());
    const float width=get("roomWidth"),depth=get("roomDepth"),height=get("roomHeight"),field=juce::Decibels::decibelsToGain(get("reflections")),master=juce::Decibels::decibelsToGain(get("output"));
    for(int row=1;row<rows.size();++row){const auto f=juce::StringArray::fromTokens(rows[row],",","");if(f.size()<20)continue;const int offset=f[0].getIntValue(),count=std::min(512,length-offset);
        for(int i=0;i<3;++i){const auto p=(size_t)i;const float x=f[2+3*i].getFloatValue()*width*.5f,y=f[3+3*i].getFloatValue()*depth,z=f[4+3*i].getFloatValue()-1.5f;const auto* input=live.getReadPointer(2+4*i)+offset;
            spatial[p].render(input,discard.getWritePointer(0),discard.getWritePointer(1),count,x,y,z,true,true);
            early[p].render(input,earlyAudio.getWritePointer(0),earlyAudio.getWritePointer(1),count,x,y,z,width,depth,height,true,true);
            juce::AudioBuffer<float> block(combined.getArrayOfWritePointers(),2,count);
            for(int c=0;c<2;++c)for(int n=0;n<count;++n)block.setSample(c,n,live.getSample(20+2*i+c,offset+n)+field*(live.getSample(4+4*i+c,offset+n)+earlyAudio.getSample(c,n)));
            spatial[p].propagation(block,true);
            const float gain=get("p"+juce::String(i)+"_mute")>.5f?0:master*juce::Decibels::decibelsToGain(get("p"+juce::String(i)+"_level"));
            for(int c=0;c<2;++c)for(int n=0;n<count&&offset+n+768<length;++n)result.addSample(c,offset+n+768,gain*block.getSample(c,n));
        }
    }
    save(out.getChildFile("06-current-scene-refined-motion.wav"),result);
}
}
int main(int argc,char** argv){try{
    if(argc==4&&juce::String(argv[1])=="--refine-capture"){juce::ScopedJuceInitialiser_GUI init;const juce::File out(argv[3]);out.createDirectory();refinedScene(juce::File(argv[2]),out);std::cout<<"PASS captured scene reconstruction\n";return 0;}
    if(argc!=3)throw std::runtime_error("Usage: TideQualityAudit CAPTURE_DIR OUTPUT_DIR");
    juce::ScopedJuceInitialiser_GUI init;const juce::File capture(argv[1]),out(argv[2]);out.createDirectory();
    if(juce::String(argv[1])=="--motion-tone"){motionTone(out);std::cout<<"PASS actual motion tone render\n";return 0;}
    replay(capture,out);notes(out);tones(out);kernels(out);propagation(capture,out);std::cout<<"PASS quality audit renders\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
