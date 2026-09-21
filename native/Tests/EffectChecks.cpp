#include "Processor.h"
#include <iostream>
#include <stdexcept>
#include <CoreFoundation/CoreFoundation.h>

namespace {
void check(bool b,const char* text){if(!b)throw std::runtime_error(text);}
void writeWav(const juce::File& file,const juce::AudioBuffer<float>& audio,double rate) {
    auto stream=file.createOutputStream();check(stream!=nullptr,"Cannot write test audio");juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(),rate,2,24,{},0));
    check(writer&&writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()),"WAV write failed");
}
double energy(const juce::AudioBuffer<float>& b,int start) {
    double sum=0;for(int c=0;c<2;++c)for(int i=start;i<b.getNumSamples();++i){double x=b.getSample(c,i);sum+=x*x;}
    return sum;
}
class DelayEffect final : public juce::AudioPluginInstance {
public:
    DelayEffect():AudioPluginInstance(BusesProperties().withInput("In",juce::AudioChannelSet::stereo()).withOutput("Out",juce::AudioChannelSet::stereo())){}
    void fillInPluginDescription(juce::PluginDescription& d) const override {d.name="Delay test";d.numInputChannels=d.numOutputChannels=2;}
    const juce::String getName() const override{return "Delay test";}
    void prepareToPlay(double,int) override {reset();setLatencySamples(127);}
    void releaseResources() override {}
    void reset() override {for(auto& r:ring)r.fill(0);position=0;}
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override {
        for(int i=0;i<b.getNumSamples();++i){for(int c=0;c<2;++c){auto& r=ring[(size_t)c];float out=r[(size_t)position];r[(size_t)position]=b.getSample(c,i);b.setSample(c,i,out*.5f);}if(++position==127)position=0;}
    }
    bool acceptsMidi() const override{return false;}bool producesMidi() const override{return false;}
    double getTailLengthSeconds() const override{return 0;}
    int getNumPrograms() override{return 1;}int getCurrentProgram() override{return 0;}
    void setCurrentProgram(int) override{}const juce::String getProgramName(int) override{return {};}
    void changeProgramName(int,const juce::String&) override{}
    bool hasEditor() const override{return false;}juce::AudioProcessorEditor* createEditor() override{return nullptr;}
    void getStateInformation(juce::MemoryBlock&) override{}void setStateInformation(const void*,int) override{}
private:
    std::array<std::array<float,127>,2> ring{};int position=0;
};
void bypassChecks() {
    TideProcessor owner;owner.prepareToPlay(48000,256);auto& host=owner.effects;
    host.installForTest(std::make_unique<DelayEffect>());
    juce::AudioBuffer<float> block(2,256);
    for(bool bypass:{false,true}) {
        host.setBypassed(bypass);
        for(int i=0;i<20;++i){block.clear();host.process(block);}
        block.clear();block.setSample(0,0,1);block.setSample(1,0,1);host.process(block);
        check(block.getMagnitude(0,127)<1.e-7f,"Bypass did not preserve effect latency");
        check(std::abs(block.getSample(0,127)-(bypass?1.f:.5f))<1.e-6f,"Bypass amplitude mismatch");
    }
    host.unloadSynchronously();
    for(int i=0;i<20;++i){block.clear();host.process(block);}
    block.clear();block.setSample(0,0,.25f);host.process(block);
    check(std::abs(block.getSample(0,0)-.25f)<1.e-7f,"Empty insert changed dry audio");
    std::cout<<"PASS latency-compensated bypass and empty insert transparency\n";
}
void emptySynthChecks() {
    for(int patch=0;patch<6;++patch) {
        TideProcessor processor;processor.setCurrentProgram(patch);processor.prepareToPlay(48000,256);
        tide::Engine reference;reference.setSettings(processor.readSettings());reference.prepare(48000,256);
        const auto note=juce::MidiMessage::noteOn(1,57,.8f);reference.midi(note);
        juce::AudioBuffer<float> a(2,256),b(2,256);juce::MidiBuffer midi;midi.addEvent(note,0);
        for(int block=0;block<30;++block) {
            processor.processBlock(a,midi);reference.render(b.getWritePointer(0),b.getWritePointer(1),256);
            for(int c=0;c<2;++c)for(int i=0;i<256;++i)check(std::abs(a.getSample(c,i)-b.getSample(c,i))<2.e-6f,"Empty FX synth differs from original engine");
        }
    }
    std::cout<<"PASS empty-slot sound matches original engine across six presets\n";
}
void finishLoad(EffectHost& host) {
    const auto deadline=juce::Time::getMillisecondCounterHiRes()+35000;
    while(host.busy()&&juce::Time::getMillisecondCounterHiRes()<deadline) {
        juce::Timer::callPendingTimersSynchronously();juce::Thread::sleep(10);
    }
    check(!host.busy(),"Asynchronous load did not finish");
}
}
int runEffectChecks(const juce::File& plugin,const juce::File& output) {
    try {
        check(!output.exists(),"Choose a new test output directory");check(output.createDirectory().wasOk(),"Cannot create test output directory");
        bypassChecks();emptySynthChecks();
        juce::DynamicObject::Ptr report=new juce::DynamicObject;report->setProperty("plugin",plugin.getFullPathName());
        juce::Array<juce::var> cases;
        for(double sr:{44100.,48000.,96000.}) {
            TideProcessor processor;
            processor.parameters.getParameter("space")->setValueNotifyingHost(0);
            processor.prepareToPlay(sr,256);juce::String error;
            if(sr==44100) {
                processor.effects.loadFile(plugin);finishLoad(processor.effects);
                check(processor.effects.loaded(),processor.effects.status().toRawUTF8());
                const auto bad=output.getChildFile("Invalid.vst3");check(bad.createDirectory().wasOk(),"Cannot create invalid scan fixture");
                processor.effects.loadFile(bad);finishLoad(processor.effects);
                check(processor.effects.path()==plugin.getFullPathName(),"Failed scan lost the previous effect");bad.deleteRecursively();
                std::cout<<"PASS child-process scan, staged load and failed-scan recovery\n";
            }else check(processor.effects.loadSynchronously(plugin,error),error.toRawUTF8());
            auto* instance=processor.effects.instanceForTest();
            check(instance!=nullptr,"Missing hosted instance");
            juce::AudioBuffer<float> buffer(2,256);juce::MidiBuffer midi;
            for(int i=0;i<30;++i){buffer.clear();processor.processBlock(buffer,midi);}
            if(sr==48000) {
                juce::AudioProcessorParameter* selected=nullptr;
                for(auto* p:instance->getParameters())if(p->getName(100)=="Mix Wet Dry Bal"){selected=p;break;}
                bool restored=false;
                check(selected!=nullptr,"Cannot find wet/dry parameter for state check");
                if(selected) {
                    const float initial=selected->getValue();selected->setValueNotifyingHost(.61f);
                    for(int i=0;i<20;++i){buffer.clear();processor.processBlock(buffer,midi);}
                    auto state=processor.effects.saveState();
                    juce::MemoryBlock before,changed,after;instance->getStateInformation(before);
                    const float original=selected->getValue();selected->setValueNotifyingHost(juce::jlimit(0.f,1.f,original+.17f));
                    for(int i=0;i<4;++i){buffer.clear();processor.processBlock(buffer,midi);}
                    instance->getStateInformation(changed);
                    processor.effects.restoreState(state);
                    for(int i=0;i<25;++i){buffer.clear();processor.processBlock(buffer,midi);CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);}
                    instance->getStateInformation(after);
                    juce::MemoryBlock decoded;decoded.fromBase64Encoding(state.getProperty("state").toString());
                    check(decoded==before,"Host state encoding changed the plugin bytes");
                    report->setProperty("hostStateBytesRoundTrip",true);
                    report->setProperty("pluginStateBlobRestored",before==after);
                    check(before!=changed,"State probe did not change plugin state");
                    instance->setStateInformation(before.getData(),(int)before.getSize());
                    report->setProperty("directJuceParameterRestore",std::abs(selected->getValue()-original)<.001f);
                    restored=std::abs(selected->getValue()-original)<.001f;
                    processor.effects.unloadSynchronously();processor.effects.restoreState(state);finishLoad(processor.effects);
                    check(processor.effects.loaded(),"Saved effect selection did not load");instance=processor.effects.instanceForTest();selected=nullptr;
                    for(auto* p:instance->getParameters())if(p->getName(100)=="Mix Wet Dry Bal"){selected=p;break;}
                    check(selected!=nullptr,"Wet/dry parameter missing after reload");
                    for(int i=0;i<25;++i){buffer.clear();processor.processBlock(buffer,midi);CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);}
                    const bool freshRestored=std::abs(selected->getValue()-original)<.001f;
                    std::cout<<"Fresh instance saved state="<<selected->getValue()<<" expected="<<original<<"\n";
                    report->setProperty("pluginStateFreshInstance",freshRestored);
                    // Return the transient test instance to its original control setting.
                    selected->setValueNotifyingHost(initial);
                }
                report->setProperty("pluginStateRoundTrip",restored);
                std::cout<<"Reverside parameter state round-trip: "<<(restored?"PASS":"NOT RESTORED by the plugin in this run")<<"\n";
                processor.effects.setBypassed(true);processor.effects.setInternalSpace(true);
                juce::MemoryBlock saved;processor.getStateInformation(saved);
                processor.effects.setBypassed(false);processor.effects.setInternalSpace(false);
                processor.setStateInformation(saved.getData(),(int)saved.getSize());
                check(processor.effects.bypassed()&&processor.effects.usesInternalSpace(),"Insert flags did not round-trip");
                processor.effects.setBypassed(false);processor.effects.setInternalSpace(false);
                std::cout<<"PASS effect selection and bypass/space session state\n";
            }
            const int total=(int)(sr*10);juce::AudioBuffer<float> audio(2,total);audio.clear();
            for(int start=0;start<total;) {
                const int n=std::min(256,total-start);juce::AudioBuffer<float> b(audio.getArrayOfWritePointers(),2,start,n);midi.clear();
                if(start==0)midi.addEvent(juce::MidiMessage::noteOn(1,57,.8f),0);
                if(start<sr*.6&&start+n>=sr*.6)midi.addEvent(juce::MidiMessage::noteOff(1,57),(int)(sr*.6)-start);
                processor.processBlock(b,midi);start+=n;
            }
            float peak=0;for(int c=0;c<2;++c)for(int i=0;i<total;++i){float x=audio.getSample(c,i);check(std::isfinite(x),"Non-finite hosted audio");peak=std::max(peak,std::abs(x));}
            const double tailEnergy=energy(audio,(int)(sr*2));
            check(peak>.001f&&peak<.99f,"Hosted Tide render is silent or clipped");check(tailEnergy>1.e-6,"No external reverb tail found");
            midi.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);
            for(int i=0;i<30;++i){buffer.clear();processor.processBlock(buffer,midi);midi.clear();}
            check(buffer.getMagnitude(0,256)>.001f,"Quiet test needs audible input");
            processor.panic();
            for(int i=0;i<20;++i){buffer.clear();midi.clear();processor.processBlock(buffer,midi);}
            const float quietPeak=buffer.getMagnitude(0,256);
            check(quietPeak<1.e-5f,"Quiet did not silence the hosted tail");
            midi.addEvent(juce::MidiMessage::noteOn(1,64,1.f),0);
            for(int i=0;i<30;++i){buffer.clear();processor.processBlock(buffer,midi);midi.clear();}
            check(buffer.getMagnitude(0,256)>.001f,"Next note did not resume after Quiet");
            std::cout<<"PASS Tide -> Reverside "<<sr<<" Hz peak="<<peak<<" tail_energy="<<tailEnergy<<" quiet_peak="<<quietPeak<<"\n";
            if(sr==48000)writeWav(output.getChildFile("tide-through-reverside.wav"),audio,sr);
            juce::DynamicObject::Ptr item=new juce::DynamicObject;item->setProperty("rate",sr);item->setProperty("peak",peak);item->setProperty("tailEnergy",tailEnergy);item->setProperty("quietPeak",quietPeak);cases.add(juce::var(item.get()));
            processor.effects.unloadSynchronously();check(!processor.effects.loaded(),"Effect did not unload");processor.releaseResources();
        }
        report->setProperty("cases",cases);report->setProperty("audioAndHostChecksPassed",true);
        report->setProperty("allChecksPassed",(bool)report->getProperty("pluginStateRoundTrip")&&(bool)report->getProperty("pluginStateFreshInstance"));
        output.getChildFile("report.json").replaceWithText(juce::JSON::toString(juce::var(report.get()),true));
        std::cout<<"PASS stereo hosting, rates, finite output, reverb tails, Quiet, unload\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
