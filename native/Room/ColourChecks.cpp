#include "RoomProcessor.h"
#include "../Tests/legacy/Engine02.h"
#include <iostream>
#include <stdexcept>
#include <chrono>

namespace {
void check(bool x,const char* message){if(!x)throw std::runtime_error(message);}
void wav(const juce::File& file,const juce::AudioBuffer<float>& audio,double sr){juce::WavAudioFormat f;auto stream=file.createOutputStream();check(stream!=nullptr,"Cannot open WAV");std::unique_ptr<juce::AudioFormatWriter> writer(f.createWriterFor(stream.release(),sr,(unsigned)audio.getNumChannels(),32,{},0));check(writer&&writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()),"Cannot write WAV");}
void finite(const juce::AudioBuffer<float>& b){for(int c=0;c<b.getNumChannels();++c)for(int i=0;i<b.getNumSamples();++i)check(std::isfinite(b.getSample(c,i)),"Non-finite output");}
juce::AudioBuffer<float> taped(const juce::AudioBuffer<float>& input,double sr,tide::room::TapeSettings s,int order,int block,uint64_t& guards,int& latency){
    tide::room::Tape tape;tape.setSettings(s);tape.prepare(sr,512,order);juce::AudioBuffer<float> out;out.makeCopyOf(input);latency=tape.latency();
    for(int offset=0;offset<out.getNumSamples();offset+=block){juce::AudioBuffer<float> part(out.getArrayOfWritePointers(),2,offset,std::min(block,out.getNumSamples()-offset));tape.process(part);}finite(out);guards=tape.guardCount();return out;
}
void tapeChecks(const juce::File& out){
    for(double sr:{44100.,48000.,96000.}){
        juce::AudioBuffer<float> input(2,24000);for(int n=0;n<input.getNumSamples();++n){float v=.18f*(float)(std::sin(n*.053)+.25*std::sin(n*.491));input.setSample(0,n,v);input.setSample(1,n,v);}
        tide::room::TapeSettings settings;settings.enabled=true;uint64_t guards=0;int latency=0;auto base=taped(input,sr,settings,5,512,guards,latency);check(guards==0,"Default tape required numerical fallback");
        for(int block:{17,127,511,2048}){uint64_t g=0;int l=0;auto other=taped(input,sr,settings,5,block,g,l);float error=0;for(int n=0;n<input.getNumSamples();++n){error=std::max(error,std::abs(base.getSample(0,n)-other.getSample(0,n)));check(base.getSample(0,n)==base.getSample(1,n),"Tape moved a centered source");}check(error<2.e-6f&&g==0,"Tape depends on buffer partition");}
        settings.enabled=false;auto dry=taped(input,sr,settings,5,127,guards,latency);for(int n=latency;n<input.getNumSamples();++n)check(dry.getSample(0,n)==input.getSample(0,n-latency),"Tape bypass changes dry audio");
        settings.enabled=true;settings.mix=0;dry=taped(input,sr,settings,5,127,guards,latency);for(int n=latency;n<input.getNumSamples();++n)check(dry.getSample(0,n)==input.getSample(0,n-latency),"Zero tape mix changes dry audio");
        settings.mix=1;float peak=0;
        for(float drive:{0.f,18.f})for(float sat:{0.f,1.f})for(float bias:{.25f,.95f}){settings.drive=drive;settings.saturation=sat;settings.bias=bias;auto b=taped(input,sr,settings,5,127,guards,latency);check(guards==0,"Tape extreme controls required fallback");peak=std::max(peak,b.getMagnitude(0,b.getNumSamples()));}
        check(peak<1,"Tape stress clipped");std::cout<<"PASS tape "<<sr<<" latency="<<latency<<" partition/bypass/mono/extremes peak="<<peak<<"\n";
    }
    tide::room::Tape moving;tide::room::TapeSettings automation;automation.enabled=true;moving.setSettings(automation);moving.prepare(48000,512);juce::AudioBuffer<float> sweep(2,127);float movingPeak=0;
    for(int k=0;k<1800;++k){if(k%100==0){automation.enabled=(k/100)%3!=0;automation.drive=(k/100)%2?18.f:0.f;automation.bias=(k/100)%2?.25f:.95f;automation.saturation=(k/100)%2?1.f:0.f;automation.warmth=(k/100)%2?1.f:0.f;moving.setSettings(automation);}
        for(int n=0;n<127;++n){const double t=(k*127+n)/48000.;sweep.setSample(0,n,.3f*(float)(std::sin(t*2731)+.3*std::sin(t*18531)));sweep.setSample(1,n,0);}
        moving.process(sweep);finite(sweep);check(sweep.getMagnitude(1,0,127)==0,"Tape leaks between channels");movingPeak=std::max(movingPeak,sweep.getMagnitude(0,127));}
    check(movingPeak<1&&moving.guardCount()==0,"Live tape controls exceeded bounds");std::cout<<"PASS tape automation, repeated bypass and channel isolation peak="<<movingPeak<<"\n";
    RoomProcessor p(false);p.set("tapeDrive",13);p.set("tapeBias",.42f);p.set("tapeOn",1);p.selectVoice(1,19);juce::MemoryBlock data;p.getStateInformation(data);p.set("tapeDrive",0);p.selectVoice(1,6);p.setStateInformation(data.getData(),(int)data.getSize());check(std::abs(p.get("tapeDrive")-13)<.01f&&std::abs(p.get("tapeBias")-.42f)<.001f&&p.get("tapeOn")==1&&p.get("p1_voice")==19,"Tape/new voice state failed");
    auto old=p.parameters.copyState();for(const char* id:{"tapeOn","tapeDrive","tapeSaturation","tapeBias","tapeWarmth","tapeMix"})old.removeChild(old.getChildWithProperty("id",id),nullptr);juce::AudioProcessor::copyXmlToBinary(*old.createXml(),data);p.setStateInformation(data.getData(),(int)data.getSize());check(p.get("tapeOn")==0&&p.get("tapeDrive")==6,"Legacy scene tape migration failed");
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());auto shot=editor->createComponentSnapshot(editor->getLocalBounds());juce::PNGImageFormat png;auto stream=out.getChildFile("room.png").createOutputStream();png.writeImageToStream(shot,*stream);
    std::cout<<"PASS tape and new-voice state, old-scene migration\n";
}
void voices(const juce::File& out){
    for(int patch=0;patch<14;++patch){auto s=tide::room::patches[(size_t)patch].values;s.space=0;s.output=0;auto old=tide02::patches[(size_t)patch].values;old.space=0;old.output=0;tide::Engine current;tide02::Engine previous;current.setSettings(s);previous.setSettings(old);current.prepare(48000,127);previous.prepare(48000,127);juce::AudioBuffer<float> a(2,127),b(2,127);float error=0;
        for(int k=0;k<600;++k){if(k%80==0){auto note=juce::MidiMessage::noteOn(1,48+(k/80)%12,.85f);current.midi(note);previous.midi(note);}if(k==450){current.midi(juce::MidiMessage::allNotesOff(1));previous.midi(juce::MidiMessage::allNotesOff(1));}
            current.render(a.getWritePointer(0),a.getWritePointer(1),127);previous.render(b.getWritePointer(0),b.getWritePointer(1),127);for(int n=0;n<127;++n)error=std::max(error,std::abs(a.getSample(0,n)-b.getSample(0,n)));}
        check(error<1.e-7f,"Original voice changed");std::cout<<"LEGACY "<<patch<<" max_error="<<error<<"\n";
    }
    for(int patch=0;patch<tide::room::patchCount;++patch){auto s=tide::room::patches[(size_t)patch].values;s.space=0;s.output=0;
        tide::Engine e;e.setSettings(s);e.prepare(48000,512);juce::AudioBuffer<float> b(2,48000*5);b.clear();
        constexpr int notes[]={48,55,60,51,58,48};constexpr float velocities[]={.5f,.8f,1.f,.6f,.9f,.7f};
        for(int pos=0;pos<b.getNumSamples();){const int step=pos/24000;if(pos%24000==0&&step<6)e.midi(juce::MidiMessage::noteOn(1,notes[step],velocities[step]));const int n=std::min({512,b.getNumSamples()-pos,24000-pos%24000});e.render(b.getWritePointer(0)+pos,b.getWritePointer(1)+pos,n);pos+=n;}
        finite(b);check(b.getMagnitude(0,b.getNumSamples())>.01f&&b.getMagnitude(0,b.getNumSamples())<.9f,"New voice silent or clips");wav(out.getChildFile(juce::String(patch).paddedLeft('0',2)+"-"+juce::String(tide::room::patches[(size_t)patch].name).replaceCharacter(' ','-')+".wav"),b,48000);
        std::cout<<"VOICE "<<patch<<" "<<tide::room::patches[(size_t)patch].name<<" peak="<<b.getMagnitude(0,b.getNumSamples())<<"\n";
    }
    for(int mode:{4,5,6})for(double sr:{44100.,48000.,96000.})for(int note:{24,60,96,127}){auto s=tide::room::patches[19].values;s.articulation=mode;s.timbre=1;s.drive=1;s.modRatio=6;s.modDepth=1;s.output=0;s.space=0;s.sustain=1;tide::Engine e;e.setSettings(s);e.prepare(sr,127);for(int i=0;i<8;++i)e.midi(juce::MidiMessage::noteOn(1,note,1.f));juce::AudioBuffer<float> b(2,127);for(int k=0;k<120;++k){e.render(b.getWritePointer(0),b.getWritePointer(1),127);finite(b);check(b.getMagnitude(0,127)<1,"Eight-voice ring stress clipped");}}
    std::cout<<"PASS new-voice extreme sample-rate/pitch/8-note checks\n";
}
void probes(const juce::File& out){
    for(int sr:{44100,48000})for(int bin:{3,17,56,177,317,563})for(int mode:{4,5,6}){const double f=(double)sr*bin*5/8192;tide::Engine e;e.prepare(sr,512);for(int n=0;n<4096;++n)e.ringProbe(f,2.4,.96f,6.4f,1,mode,2.4f);juce::AudioBuffer<float> b(1,8192);for(int n=0;n<8192;++n)b.setSample(0,n,e.ringProbe(f,2.4,.96f,6.4f,1,mode,2.4f));wav(out.getChildFile("ring-"+juce::String(sr)+"-"+juce::String(bin)+"-"+juce::String(mode)+".wav"),b,sr);}
    std::cout<<"PASS ring probes rendered\n";
}
void tapeProbes(const juce::File& out){
    for(int sr:{44100,48000})for(int bin:{17,177,563,1403,3001})for(float drive:{0.f,9.f,18.f})for(int order:{3,5}){
        const int size=8192,warm=32768;const double frequency=(double)sr*bin/size;juce::AudioBuffer<float> input(2,warm+size);for(int n=0;n<input.getNumSamples();++n)for(int c=0;c<2;++c)input.setSample(c,n,.3f*(float)std::sin(2*juce::MathConstants<double>::pi*frequency*n/sr));
        tide::room::TapeSettings s;s.enabled=true;s.drive=drive;s.warmth=0;uint64_t guards=0;int latency=0;auto b=taped(input,sr,s,order,512,guards,latency);juce::AudioBuffer<float> tail(b.getArrayOfWritePointers(),2,warm,size);
        wav(out.getChildFile("tape-"+juce::String(sr)+"-"+juce::String(bin)+"-"+juce::String((int)drive)+"-"+juce::String(order)+".wav"),tail,sr);std::cout<<"TAPE PROBE "<<sr<<" "<<bin<<" "<<drive<<" OS="<<(1<<order)<<" latency="<<latency<<" guards="<<guards<<"\n";check(guards==0,"Spectral probe used numerical fallback");
    }
}
void tapeFile(const juce::File& file,const juce::File& out){
    juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));check(reader&&reader->numChannels==2,"Expected stereo scene WAV");const int length=(int)reader->lengthInSamples;
    juce::AudioBuffer<float> input(2,length+512);input.clear();reader->read(&input,0,length,0,true,true);input.applyGain(juce::Decibels::decibelsToGain(3.f));
    for(int drive:{-1,6,12}){tide::room::TapeSettings s;s.enabled=drive>=0;s.drive=(float)std::max(0,drive);uint64_t guards=0;int latency=0;auto processed=taped(input,reader->sampleRate,s,5,512,guards,latency);check(guards==0,"Listening render needed numerical recovery");juce::AudioBuffer<float> aligned(processed.getArrayOfWritePointers(),2,latency,length);aligned.applyGain(juce::Decibels::decibelsToGain(-3.f));wav(out.getChildFile(drive<0?"original.wav":"tape-"+juce::String(drive)+"dB.wav"),aligned,reader->sampleRate);}
    std::cout<<"PASS identical-source tape comparison rendered\n";
}
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;std::cout<<std::unitbuf;try{check(argc==3||argc==4,"Use --tape/--voices/--probes/--tape-probes/--tape-file and a new directory");juce::File out=juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);check(!out.exists(),"Use a new output directory");out.createDirectory();const juce::String mode=argv[1];if(mode=="--tape")tapeChecks(out);else if(mode=="--voices")voices(out);else if(mode=="--probes")probes(out);else if(mode=="--tape-probes")tapeProbes(out);else if(mode=="--tape-file"&&argc==4)tapeFile(juce::File::getCurrentWorkingDirectory().getChildFile(argv[3]),out);else check(false,"Unknown mode");return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
