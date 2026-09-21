#include "EffectHost.h"
#include "Engine.h"
#include <algorithm>

struct EffectHost::PluginWindow final : juce::DocumentWindow {
    PluginWindow(juce::AudioPluginInstance& plugin):DocumentWindow(plugin.getName(),juce::Colour(0xff273c39),closeButton) {
        setUsingNativeTitleBar(true);
        auto* ui=plugin.createEditorIfNeeded();
        if(ui==nullptr) ui=new juce::GenericAudioProcessorEditor(plugin);
        setContentOwned(ui,true);
        setResizable(ui->isResizable(),false);
        centreWithSize(getWidth(),getHeight());
        setVisible(true);toFront(true);
    }
    void closeButtonPressed() override {setVisible(false);}
};
juce::Optional<juce::AudioPlayHead::PositionInfo> EffectHost::PlayHead::getPosition() const {
    PositionInfo p;const auto n=samples.load();const auto seconds=n/rate.load();
    p.setTimeInSamples(n);p.setTimeInSeconds(seconds);p.setBpm(120.);
    p.setTimeSignature(TimeSignature{4,4});p.setPpqPosition(seconds*2);p.setIsPlaying(true);return p;
}
EffectHost::EffectHost(juce::AudioProcessor& p):owner(p) {startTimerHz(30);}
EffectHost::~EffectHost() {
    stopTimer();if(scanner.isRunning()) scanner.kill();
    if(scanResult!=juce::File{}) scanResult.deleteFile();
    editor.reset();staged.reset();active.reset();
}
void EffectHost::prepare(double sr,int block) {
    sampleRate.store(sr);maximumBlock.store(std::max(1,block));playHead.rate.store(sr);playHead.samples.store(0);
    dryDelay.setSize(2,delayCapacity);dryDelay.clear();delayPosition=0;
    wetBlend.reset(sr,.015);wetBlend.setCurrentAndTargetValue(bypass.load()?0.f:1.f);
    transition.reset(sr,.01);transition.setCurrentAndTargetValue(1);
    quietGain.reset(sr,.005);quietGain.setCurrentAndTargetValue(1);quietLatched.store(false);pendingQuietReset=false;
    if(active) {active->releaseResources();active->setRateAndBufferSizeDetails(sr,block);active->prepareToPlay(sr,block);}
    prepared.store(true);
}
void EffectHost::releaseResources() {prepared.store(false);if(active) active->releaseResources();}
void EffectHost::process(juce::AudioBuffer<float>& buffer) {
    callbacks.fetch_add(1);transition.setTargetValue(mutedForSwap.load()?0.f:1.f);
    wetBlend.setTargetValue(bypass.load()||badAudio.load()?0.f:1.f);
    if(resetRequested.exchange(false))pendingQuietReset=true;
    quietGain.setTargetValue(quietLatched.load()?0.f:1.f);
    int latencySamples=active?active->getLatencySamples():0;
    if(latencySamples<0||latencySamples>=delayCapacity-maximumBlock.load()) {badAudio.store(true);latencySamples=0;}
    reportedLatency.store(latencySamples);
    for(int start=0;start<buffer.getNumSamples();) {
        const int n=std::min(maximumBlock.load(),buffer.getNumSamples()-start);
        juce::AudioBuffer<float> block(buffer.getArrayOfWritePointers(),2,start,n);
        const int firstPosition=delayPosition;
        if(active) {
            for(int i=0;i<n;++i) {
                dryDelay.setSample(0,delayPosition,block.getSample(0,i));dryDelay.setSample(1,delayPosition,block.getSample(1,i));
                if(++delayPosition==delayCapacity) delayPosition=0;
            }
            noMidi.clear();active->processBlock(block,noMidi);
        }
        for(int i=0;i<n;++i) {
            const float blend=wetBlend.getNextValue();float gain=transition.getNextValue();
            gain*=quietGain.getNextValue();
            const int read=(firstPosition+i+delayCapacity-latencySamples)%delayCapacity;
            for(int c=0;c<2;++c) {
                float x=block.getSample(c,i);
                if(!std::isfinite(x)) {x=0;badAudio.store(true);}
                if(active) {const float dry=dryDelay.getSample(c,read);x=dry+blend*(x-dry);}
                block.setSample(c,i,x*gain);
            }
        }
        playHead.samples.fetch_add(n);start+=n;
    }
    if(pendingQuietReset&&quietGain.getCurrentValue()<1.e-6f) {
        // Some effects retain their tail on reset. Keep the output silent until
        // another note, while continuing to process so those tails can decay.
        if(active)active->reset();dryDelay.clear();delayPosition=0;pendingQuietReset=false;
    }
    silentForSwap.store(transition.getCurrentValue()<1.e-6f);
}
juce::Array<juce::File> EffectHost::installedBundles() {
    juce::Array<juce::File> result;
    for(const auto& root:{juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins/VST3"),juce::File("/Library/Audio/Plug-Ins/VST3")}) {
        for(const auto& p:root.findChildFiles(juce::File::findDirectories,true,"*.vst3"))
            if(!p.getFileNameWithoutExtension().startsWithIgnoreCase("Tide")) result.addIfNotAlreadyThere(p);
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.getFileName().compareIgnoreCase(b.getFileName())<0;});
    return result;
}
bool EffectHost::scanToFile(const juce::File& file,const juce::File& result) {
    juce::XmlElement xml("EffectScan");juce::VST3PluginFormat f;juce::OwnedArray<juce::PluginDescription> found;
    if(file.isDirectory()&&file.hasFileExtension("vst3")) f.findAllTypesForFile(found,file.getFullPathName());
    for(auto* d:found) if(!d->isInstrument&&d->numInputChannels>=2&&d->numOutputChannels>=2) xml.addChildElement(d->createXml().release());
    if(xml.getNumChildElements()==0) xml.setAttribute("error","No compatible stereo VST3 effect found.");
    return xml.writeTo(result);
}
std::unique_ptr<juce::AudioPluginInstance> EffectHost::create(const juce::PluginDescription& d,juce::String& error) {
    if(d.isInstrument) {error="This slot accepts effects, not instruments.";return {};}
    auto p=format.createInstanceFromDescription(d,sampleRate.load(),maximumBlock.load(),error);
    if(!p) return {};
    p->disableNonMainBuses();auto layout=p->getBusesLayout();
    if(layout.inputBuses.isEmpty()||layout.outputBuses.isEmpty()) {error="The effect needs stereo input and output.";return {};}
    layout.inputBuses.getReference(0)=juce::AudioChannelSet::stereo();layout.outputBuses.getReference(0)=juce::AudioChannelSet::stereo();
    if(!p->setBusesLayout(layout)||p->getTotalNumInputChannels()!=2||p->getTotalNumOutputChannels()!=2) {error="This version supports stereo effects only.";return {};}
    p->setPlayHead(&playHead);p->setRateAndBufferSizeDetails(sampleRate.load(),maximumBlock.load());
    if(restoreBytes.getSize()>0) p->setStateInformation(restoreBytes.getData(),(int)restoreBytes.getSize());
    p->prepareToPlay(sampleRate.load(),maximumBlock.load());
    if(p->getLatencySamples()>=delayCapacity-maximumBlock.load()) {error="This effect reports more latency than the slot supports.";p->releaseResources();return {};}
    return p;
}
void EffectHost::loadFile(const juce::File& file) {
    if(busy()) return;
    if(!file.isDirectory()||!file.hasFileExtension("vst3")) {fail("The selected VST3 bundle is missing.");return;}
    // JUCE's macOS tempDirectory is a Library/Caches directory. Use the OS
    // temporary directory so scanning also works in a contained console host.
    scanResult=juce::File(juce::SystemStats::getEnvironmentVariable("TMPDIR","/tmp")).getChildFile("tide-effect-scan-"+juce::Uuid().toString()+".xml");
    juce::StringArray args{juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),"--scan-effect",file.getFullPathName(),scanResult.getFullPathName()};
    if(!scanner.start(args,0)) {fail("Couldn't start the effect scanner.");return;}
    scanning=true;scanStarted=juce::Time::getMillisecondCounterHiRes();message="Loading "+file.getFileNameWithoutExtension()+"...";
}
void EffectHost::fail(const juce::String& error) {message=error;scanning=false;restoring=false;restoreBytes.reset();}
void EffectHost::stage(std::unique_ptr<juce::AudioPluginInstance> p,const juce::PluginDescription& description) {
    staged=std::move(p);stagedDescription=description;waitingToSwap=true;
    callbacksAtSwap=callbacks.load();swapStarted=juce::Time::getMillisecondCounterHiRes();
    mutedForSwap.store(true);
    if(!prepared.load()) commit();
}
void EffectHost::commit() {
    closeEditor();std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock lock(owner.getCallbackLock());
        old=std::move(active);active=std::move(staged);activeDescription=stagedDescription;
        if(active&&(std::abs(active->getSampleRate()-sampleRate.load())>.1||active->getBlockSize()!=maximumBlock.load())) {
            active->releaseResources();active->setRateAndBufferSizeDetails(sampleRate.load(),maximumBlock.load());
            active->prepareToPlay(sampleRate.load(),maximumBlock.load());
        }
        if(!restoring&&active) {internalSpace.store(false);bypass.store(false);}
        badAudio.store(false);dryDelay.clear();delayPosition=0;
        wetBlend.setCurrentAndTargetValue(bypass.load()?0.f:1.f);
        transition.setCurrentAndTargetValue(0);mutedForSwap.store(false);silentForSwap.store(false);
        reportedLatency.store(active?active->getLatencySamples():0);
    }
    if(old)old->releaseResources();old.reset();waitingToSwap=false;restoring=false;restoreBytes.reset();
    message=active?activeDescription.name+" | Use its editor to set the wet/dry balance.":"No external effect.";
    owner.setLatencySamples(tide::Engine::latency+reportedLatency.load());
}
void EffectHost::timerCallback() {
    if(scanning) {
        if(scanner.isRunning()) {
            if(juce::Time::getMillisecondCounterHiRes()-scanStarted>30000) {scanner.kill();scanResult.deleteFile();fail("Effect scan timed out. The previous effect is still available.");}
        } else {
            scanning=false;auto xml=juce::XmlDocument::parse(scanResult);scanResult.deleteFile();
            if(scanner.getExitCode()!=0) {fail("The effect stopped during scanning. The previous effect is still available.");return;}
            juce::PluginDescription d;
            if(!xml||!xml->getFirstChildElement()||!d.loadFromXml(*xml->getFirstChildElement())) {fail(xml?xml->getStringAttribute("error","Couldn't scan this effect."):"The effect scanner stopped unexpectedly.");return;}
            juce::String error;auto p=create(d,error);if(!p){fail(error);return;}stage(std::move(p),d);
        }
    }
    if(waitingToSwap) {
        const auto now=juce::Time::getMillisecondCounterHiRes();const auto count=callbacks.load();
        if(count!=callbacksAtSwap) {callbacksAtSwap=count;swapStarted=now;}
        if(silentForSwap.load()||!prepared.load()||now-swapStarted>100)commit();
    }
    if(badAudio.load()) message="Effect returned invalid audio or latency; bypass it or choose another effect.";
    const int total=tide::Engine::latency+reportedLatency.load();if(owner.getLatencySamples()!=total)owner.setLatencySamples(total);
}
void EffectHost::unload() {if(!busy()){restoring=false;stage({},{});}}
void EffectHost::openEditor() {if(active){if(!editor)editor=std::make_unique<PluginWindow>(*active);editor->setVisible(true);editor->toFront(true);}}
void EffectHost::closeEditor() {editor.reset();}
juce::ValueTree EffectHost::saveState() {
    juce::ValueTree state("ExternalEffect");state.setProperty("bypass",bypass.load(),nullptr);state.setProperty("internalSpace",internalSpace.load(),nullptr);
    if(active) {
        state.setProperty("description",activeDescription.createXml()->toString(),nullptr);
        juce::MemoryBlock data;active->getStateInformation(data);state.setProperty("state",data.toBase64Encoding(),nullptr);
    }
    return state;
}
void EffectHost::restoreState(const juce::ValueTree& state) {
    if(!state.isValid()) return;
    cancelPending();
    internalSpace.store((bool)state.getProperty("internalSpace",true));bypass.store((bool)state.getProperty("bypass",false));
    auto xml=juce::XmlDocument::parse(state.getProperty("description").toString());juce::PluginDescription d;
    if(!xml||!d.loadFromXml(*xml)) {unload();return;}
    restoreBytes.fromBase64Encoding(state.getProperty("state").toString());restoring=true;
    if(active&&activeDescription.fileOrIdentifier==d.fileOrIdentifier) {
        if(restoreBytes.getSize()>0) {
            const juce::ScopedLock lock(owner.getCallbackLock());
            active->setStateInformation(restoreBytes.getData(),(int)restoreBytes.getSize());
            dryDelay.clear();delayPosition=0;
            transition.setCurrentAndTargetValue(0);
        }
        restoreBytes.reset();restoring=false;return;
    }
    loadFile(juce::File(d.fileOrIdentifier));
}
void EffectHost::cancelPending() {
    if(scanner.isRunning())scanner.kill();if(scanResult!=juce::File{})scanResult.deleteFile();
    scanning=false;waitingToSwap=false;staged.reset();mutedForSwap.store(false);restoreBytes.reset();restoring=false;
}
bool EffectHost::loadSynchronously(const juce::File& file,juce::String& error) {
    juce::OwnedArray<juce::PluginDescription> descriptions;format.findAllTypesForFile(descriptions,file.getFullPathName());
    for(auto* d:descriptions) if(!d->isInstrument) {
        auto p=create(*d,error);if(!p)return false;staged=std::move(p);stagedDescription=*d;commit();return true;
    }
    error="No effect found";return false;
}
void EffectHost::unloadSynchronously() {staged.reset();stagedDescription={};commit();}
void EffectHost::installForTest(std::unique_ptr<juce::AudioPluginInstance> p) {
    p->setPlayHead(&playHead);p->setRateAndBufferSizeDetails(sampleRate.load(),maximumBlock.load());p->prepareToPlay(sampleRate.load(),maximumBlock.load());
    staged=std::move(p);stagedDescription={};stagedDescription.name="Test effect";commit();
}
