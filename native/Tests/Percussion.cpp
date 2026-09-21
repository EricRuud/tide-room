#include "Processor.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool b,const char* text){if(!b)throw std::runtime_error(text);}
void write(const juce::File& path,const juce::AudioBuffer<float>& b,double sr) {
    auto stream=path.createOutputStream();check(stream!=nullptr,"WAV output failed");juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),sr,(unsigned)b.getNumChannels(),24,{},0));
    check(writer&&writer->writeFromAudioSampleBuffer(b,0,b.getNumSamples()),"WAV write failed");
}
juce::AudioBuffer<float> hit(tide::Settings s,double sr,int note,float velocity,int block,int frames,bool shortGate=false,int count=1) {
    tide::Engine e;e.setSettings(s);e.prepare(sr,256);
    for(int i=0;i<count;++i)e.midi(juce::MidiMessage::noteOn(1,note,velocity));
    juce::AudioBuffer<float> b(2,frames);bool released=false;
    const int off=(int)(sr*.01);
    for(int pos=0;pos<frames;) {
        if(shortGate&&pos==off&&!released){for(int i=0;i<count;++i)e.midi(juce::MidiMessage::noteOff(1,note));released=true;}
        int n=std::min(block,frames-pos);if(shortGate&&!released)n=std::min(n,off-pos);
        e.render(b.getWritePointer(0)+pos,b.getWritePointer(1)+pos,n);pos+=n;
    }
    for(int c=0;c<2;++c)for(int i=0;i<frames;++i)check(std::isfinite(b.getSample(c,i)),"Non-finite percussion audio");
    return b;
}
double energy(const juce::AudioBuffer<float>& b) {double sum=0;for(int i=0;i<b.getNumSamples();++i){double x=b.getSample(0,i);sum+=x*x;}return sum;}
void stateChecks() {
    TideProcessor p;check(p.getNumPrograms()==tide::patchCount,"Factory bank count mismatch");
    for(int patch=0;patch<tide::patchCount;++patch) {
        p.setCurrentProgram(patch);check(!p.programModified(),"Preset doesn't match parameters");
        juce::MemoryBlock data;p.getStateInformation(data);p.setCurrentProgram(0);p.setStateInformation(data.getData(),(int)data.getSize());
        check(p.getCurrentProgram()==patch&&!p.programModified(),"New voice/preset did not round-trip");
    }
    p.setCurrentProgram(0);auto old=p.parameters.copyState();old.removeChild(old.getChildWithProperty("id","articulation"),nullptr);old.setProperty("program",0,nullptr);
    juce::MemoryBlock legacy;juce::AudioProcessor::copyXmlToBinary(*old.createXml(),legacy);
    p.setCurrentProgram(11);p.setStateInformation(legacy.getData(),(int)legacy.getSize());
    check(p.readSettings().articulation==0&&!p.programModified(),"Legacy state adopted percussion voice");
    std::cout<<"PASS all 14 preset states and legacy voice migration\n";
}
void checks(const juce::File& out) {
    stateChecks();float worst=0;double longest=0;
    for(int mode=1;mode<=3;++mode) {
        auto s=tide::patches[mode==1?6:mode==2?7:10].values;s.space=0;
        const auto held=hit(s,48000,57,.8f,256,48000);
        for(int block:{17,127,511}) {
            auto tap=hit(s,48000,57,.8f,block,48000,true);float difference=0;
            for(int i=0;i<48000;++i)difference=std::max(difference,std::abs(tap.getSample(0,i)-held.getSample(0,i)));
            check(difference<2.e-6f,"Strike changed with key length or host block size");
        }
        const auto soft=hit(s,48000,57,.35f,256,24000),hard=hit(s,48000,57,.9f,256,24000);
        check(energy(hard)>energy(soft)*1.6,"Hard strikes must have more energy");
        tide::Engine e;e.setSettings(s);e.prepare(48000,256);e.midi(juce::MidiMessage::noteOn(1,57,1.f));
        juce::AudioBuffer<float> b(2,256);for(int i=0;i<20;++i)e.render(b.getWritePointer(0),b.getWritePointer(1),256);
        e.midi(juce::MidiMessage::allSoundOff(1));for(int i=0;i<30;++i)e.render(b.getWritePointer(0),b.getWritePointer(1),256);
        check(e.activeVoices()==0,"All sound off left a struck voice active");
        // The shared 2 Hz DC blocker can retain a subsonic settling tail.
        for(int i=0;i<90;++i)e.render(b.getWritePointer(0),b.getWritePointer(1),256);
        check(b.getMagnitude(0,256)<1.e-5f,"All sound off did not settle");
        e.midi(juce::MidiMessage::noteOn(1,57,1.f));for(int i=0;i<20;++i)e.render(b.getWritePointer(0),b.getWritePointer(1),256);
        e.panic();for(int i=0;i<5;++i)e.render(b.getWritePointer(0),b.getWritePointer(1),256);
        check(b.getMagnitude(0,256)==0,"Quiet did not clear percussion filters");
        s.decay=.06f;s.sustain=0;e.setSettings(s);e.prepare(48000,256);e.midi(juce::MidiMessage::noteOn(1,57,1.f));
        for(int i=0;i<10;++i)e.render(b.getWritePointer(0),b.getWritePointer(1),256);
        e.midi(juce::MidiMessage::noteOff(1,57));s.sustain=1;e.setSettings(s);
        for(int i=0;i<400;++i)e.render(b.getWritePointer(0),b.getWritePointer(1),256);
        check(e.activeVoices()==0,"Sustain edit latched an already struck note");
    }
    std::cout<<"PASS strike length, block invariance, velocity, all-sound-off and Quiet\n";
    for(double sr:{44100.,48000.,96000.})for(int mode=1;mode<=3;++mode)for(int note:{24,60,96,127}) {
        auto s=tide::patches[6].values;s.articulation=mode;s.output=0;s.timbre=1;s.drive=1;s.space=1;s.sustain=1;
        auto start=std::chrono::steady_clock::now();const auto b=hit(s,sr,note,1,127,(int)(sr*.7),true,8);
        longest=std::max(longest,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()/.7);
        worst=std::max(worst,b.getMagnitude(0,b.getNumSamples()));check(worst<.98f,"New modes clip under eight-note stress");
    }
    std::cout<<"PASS 36 eight-voice extreme/rate cases peak="<<worst<<" worst_render_fraction="<<longest<<"\n";
    for(int patch=6;patch<tide::patchCount;++patch) {
        auto s=tide::patches[(size_t)patch].values;
        const auto name=juce::String(patch-5).paddedLeft('0',2)+"-"+juce::String(tide::patches[(size_t)patch].name).toLowerCase().replaceCharacter(' ','-');
        tide::Engine e;e.setSettings(s);e.prepare(48000,256);
        juce::AudioBuffer<float> phrase(2,48000*8);phrase.clear();
        constexpr int notes[]={48,55,60,51,58,48,60,55};constexpr float velocities[]={.45f,.72f,1.f,.6f,.85f,.5f,.95f,.7f};
        for(int pos=0;pos<phrase.getNumSamples();) {
            const int step=pos/24000;if(pos%24000==0&&step<8)e.midi(juce::MidiMessage::noteOn(1,notes[step],velocities[step]));
            const int n=std::min({256,phrase.getNumSamples()-pos,24000-pos%24000});
            e.render(phrase.getWritePointer(0)+pos,phrase.getWritePointer(1)+pos,n);pos+=n;
        }
        check(phrase.getMagnitude(0,phrase.getNumSamples())>.01f,"New patch is silent");write(out.getChildFile(name+".wav"),phrase,48000);
        s.space=0;const auto dry=hit(s,48000,57,.85f,256,48000*3);write(out.getChildFile(name+"-dry.wav"),dry,48000);
    }
    juce::DynamicObject::Ptr report=new juce::DynamicObject;report->setProperty("passed",true);report->setProperty("stressPeak",worst);report->setProperty("worstRenderTimeFraction",longest);
    out.getChildFile("checks.json").replaceWithText(juce::JSON::toString(juce::var(report.get()),true));
    std::cout<<"PASS rendered eight percussion presets and dry hits\n";
}
void editorSnapshot(const juce::File& out) {
    TideProcessor p;p.setCurrentProgram(6);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    juce::Thread::sleep(80);juce::Timer::callPendingTimersSynchronously();
    const auto snapshot=editor->createComponentSnapshot(editor->getLocalBounds());
    juce::PNGImageFormat format;auto stream=out.getChildFile("editor.png").createOutputStream();
    check(stream&&format.writeImageToStream(snapshot,*stream),"Editor snapshot failed");
}
void benchmark(const juce::File& out) {
    juce::Array<juce::var> rows;
    for(double sr:{48000.,96000.})for(int mode=0;mode<4;++mode) {
        TideProcessor p;auto* parameter=p.parameters.getParameter("articulation");parameter->setValueNotifyingHost(parameter->convertTo0to1((float)mode));
        p.parameters.getParameter("sustain")->setValueNotifyingHost(1);p.prepareToPlay(sr,256);
        juce::AudioBuffer<float> b(2,256);juce::MidiBuffer midi;for(int n=0;n<8;++n)midi.addEvent(juce::MidiMessage::noteOn(1,48+n*3,1.f),0);
        for(int i=0;i<30;++i)p.processBlock(b,midi);
        double seconds=0;for(int i=0;i<250;++i) {
            const auto start=std::chrono::steady_clock::now();p.processBlock(b,midi);
            seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        }
        const double fraction=seconds/(250*256/sr);juce::DynamicObject::Ptr row=new juce::DynamicObject;
        row->setProperty("rate",sr);row->setProperty("voice",mode);row->setProperty("averageCallbackTimeFraction",fraction);rows.add(juce::var(row.get()));
        std::cout<<"BENCH "<<sr<<" Hz mode="<<mode<<" eight voices average_callback_fraction="<<fraction<<"\n";
    }
    out.getChildFile("benchmark.json").replaceWithText(juce::JSON::toString(rows,true));
}
void probes(const juce::File& out) {
    for(int sr:{44100,48000})for(int bin:{3,17,56,177,317,482,563})for(float index:{0.f,.8f,2.5f})for(float fold:{3.f,6.4f})for(float drive:{0.f,1.f}) {
        tide::Engine e;e.prepare(sr,256);const double f=(double)sr*bin*5/8192;
        for(int i=0;i<4096;++i)e.metalProbe(f,index,fold,drive);
        juce::AudioBuffer<float> b(1,8192);for(int i=0;i<8192;++i)b.setSample(0,i,e.metalProbe(f,index,fold,drive));
        write(out.getChildFile(juce::String(sr)+"-"+juce::String(bin)+"-"+juce::String(index,1)+"-"+juce::String(fold,1)+"-"+juce::String(drive,1)+".wav"),b,sr);
    }
}
}
int main(int argc,char* argv[]) {
    juce::ScopedJuceInitialiser_GUI init;
    try {
        check(argc==3,"Use --checks or --probes followed by a new output directory");const juce::File out=juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
        check(!out.exists(),"Choose a new output directory");check(out.createDirectory().wasOk(),"Cannot make output directory");
        if(juce::String(argv[1])=="--checks")checks(out);else if(juce::String(argv[1])=="--probes")probes(out);else if(juce::String(argv[1])=="--ui")editorSnapshot(out);else if(juce::String(argv[1])=="--benchmark")benchmark(out);else throw std::runtime_error("Unknown mode");
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
