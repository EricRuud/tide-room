#include <juce_audio_utils/juce_audio_utils.h>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace {
void require(bool b, const juce::String& s) { if (!b) throw std::runtime_error(s.toStdString()); }
struct BenchHead final : juce::AudioPlayHead {
    double rate = 48000; int64_t sample = 0;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setIsPlaying(true); p.setTimeInSamples(sample);
        p.setTimeInSeconds(sample/rate); p.setBpm(100); p.setPpqPosition(sample/rate*100/60); return p;
    }
};
struct Window final : juce::DocumentWindow {
    Window(juce::AudioProcessorEditor* e, bool hidden) : DocumentWindow("P821 measurement host — no speaker output", juce::Colours::black, allButtons) {
        setUsingNativeTitleBar(true); setContentOwned(e,true); centreWithSize(getWidth(),getHeight()); setVisible(!hidden);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};
class App final : public juce::JUCEApplication, private juce::Timer {
public:
    const juce::String getApplicationName() override { return "Tide Plugin Bench"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void anotherInstanceStarted(const juce::String&) override { if(window) window->toFront(true); }
    void initialise(const juce::String& command) override {
        try {
            auto args = juce::StringArray::fromTokens(command,true);
            require(args.size()==2 || (args.size()==3 && args[2]=="--hidden"),"Expected plugin path and workspace directory, optionally --hidden");
            const bool hidden=args.size()==3;
            root=juce::File(args[1].unquoted()); root.createDirectory();
            juce::OwnedArray<juce::PluginDescription> descriptions;
            format.findAllTypesForFile(descriptions,args[0].unquoted());
            require(!descriptions.isEmpty(),"Plugin scan returned no descriptions"); description=*descriptions[0];
            juce::String error; plugin=format.createInstanceFromDescription(description,48000,256,error);
            require(plugin!=nullptr,error); plugin->setPlayHead(&head);
            auto layout=plugin->getBusesLayout();
            require(!layout.inputBuses.isEmpty()&&!layout.outputBuses.isEmpty(),"Expected effect input and output buses");
            layout.inputBuses.set(0,juce::AudioChannelSet::stereo()); layout.outputBuses.set(0,juce::AudioChannelSet::stereo());
            for(int i=1;i<layout.inputBuses.size();++i) layout.inputBuses.set(i,juce::AudioChannelSet::disabled());
            for(int i=1;i<layout.outputBuses.size();++i) layout.outputBuses.set(i,juce::AudioChannelSet::disabled());
            require(plugin->setBusesLayout(layout),"Cannot configure stereo effect");
            plugin->setRateAndBufferSizeDetails(48000,256); plugin->prepareToPlay(48000,256);
            require(plugin->hasEditor(),"Plugin has no editor"); window=std::make_unique<Window>(plugin->createEditorIfNeeded(),hidden);
            root.getChildFile("parameters-live.json").replaceWithText(juce::JSON::toString(metadata(),true));
            startTimer(200);
        } catch(const std::exception& e) { juce::Logger::writeToLog(e.what()); setApplicationReturnValue(1); quit(); }
    }
    void systemRequestedQuit() override { quit(); }
    void shutdown() override { stopTimer(); if(worker.joinable())worker.join(); window.reset(); if(plugin)plugin->releaseResources(); plugin.reset(); }
private:
    juce::var metadata() {
        juce::DynamicObject::Ptr o=new juce::DynamicObject;
        o->setProperty("name",description.name); o->setProperty("version",description.version);
        o->setProperty("inputs",plugin->getTotalNumInputChannels()); o->setProperty("outputs",plugin->getTotalNumOutputChannels());
        o->setProperty("latency",plugin->getLatencySamples());
        juce::Array<juce::var> parameters;
        for(auto* p:plugin->getParameters()) {
            juce::DynamicObject::Ptr row=new juce::DynamicObject;
            row->setProperty("name",p->getName(100)); row->setProperty("value",p->getValue()); row->setProperty("text",p->getText(p->getValue(),100));
            parameters.add(juce::var(row.get()));
        }
        o->setProperty("parameters",parameters); return juce::var(o.get());
    }
    void set(const juce::String& name, const juce::var& value) {
        for(auto* p:plugin->getParameters()) if(p->getName(100)==name) {
            const auto v=value.isString()?p->getValueForText(value.toString()):(float)value;
            require(v>=0&&v<=1,"Parameter outside normalized range: "+name);
            p->setValueNotifyingHost(v); return;
        }
        throw std::runtime_error("Unknown parameter: "+name.toStdString());
    }
    void timerCallback() override {
        if(worker.joinable()) {
            if(!finished.load()) return;
            worker.join();
            pendingResult->setProperty("metadata",metadata());
            root.getChildFile(pendingJob["id"].toString()+".json").replaceWithText(juce::JSON::toString(juce::var(pendingResult.get()),true));
            pendingResult=nullptr; return;
        }
        if(queued) {
            queued=false; finished.store(false);
            worker=std::thread([this]{
                try { run(pendingJob,*pendingResult); pendingResult->setProperty("ok",true); }
                catch(const std::exception& e) {pendingResult->setProperty("ok",false);pendingResult->setProperty("error",e.what());}
                finished.store(true);
            });
            return;
        }
        const auto command=root.getChildFile("command.json");
        if(!command.existsAsFile())return;
        const auto job=juce::JSON::parse(command);
        // Commands are immutable by ID. Leave the mailbox in place: deleting a
        // file from the GUI thread can block behind filesystem services.
        const auto id=job.getProperty("id","").toString();
        if(id.isEmpty() || id==lastCommandId) return;
        lastCommandId=id;
        if(root.getChildFile(id+".json").existsAsFile()) return;
        juce::DynamicObject::Ptr result=new juce::DynamicObject;
        result->setProperty("id",id);
        try {
            if(job.getProperty("describe",false)) { result->setProperty("metadata",metadata()); }
            else {
                // VST3 controllers and plugins can use main-loop callbacks when
                // parameters change. Let those run before starting audio work,
                // and keep the message thread alive throughout processing.
                juce::AudioFormatManager fm;fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(juce::File(job["input"].toString())));
                require(r!=nullptr,"Cannot read input audio");
                const int block=(int)job.getProperty("block",256);
                require(block>0&&block<=8192,"Invalid block size");
                plugin->releaseResources(); plugin->setNonRealtime((bool)job.getProperty("offline",false));
                plugin->setRateAndBufferSizeDetails(r->sampleRate,block);plugin->prepareToPlay(r->sampleRate,block);plugin->reset();
                if(auto* obj=job["parameters"].getDynamicObject())
                    for(const auto& p:obj->getProperties())set(p.name.toString(),p.value);
                // Flush JUCE's controller dispatcher through the standard state API.
                juce::MemoryBlock scratch;plugin->getStateInformation(scratch);
                pendingJob=job;pendingResult=result;queued=true;return;
            }
            result->setProperty("ok",true);
        } catch(const std::exception& e) { result->setProperty("ok",false); result->setProperty("error",e.what()); }
        root.getChildFile(id+".json").replaceWithText(juce::JSON::toString(juce::var(result.get()),true));
    }
    void run(const juce::var& job,juce::DynamicObject& result) {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(juce::File(job["input"].toString())));
        require(reader!=nullptr,"Cannot read input audio");
        require(reader->numChannels==2,"Input must be stereo");
        const int block=(int)job.getProperty("block",256); require(block>0&&block<=8192,"Invalid block size");
        head.rate=reader->sampleRate; head.sample=0;
        require(plugin->getTotalNumInputChannels()==2&&plugin->getTotalNumOutputChannels()==2,"Stereo layout changed");
        juce::AudioBuffer<float> b(2,block); juce::MidiBuffer midi;
        const int warm=(int)((double)job.getProperty("warmup",2.0)*head.rate/block);
        const auto warmStart=std::chrono::steady_clock::now();
        for(int i=0;i<warm;++i) { b.clear();
            if((bool)job.getProperty("warmupInput",false))
                reader->read(&b,0,block,(int64_t(i)*block)%std::max<int64_t>(block,reader->lengthInSamples-block),true,true);
            plugin->processBlock(b,midi); midi.clear(); head.sample+=block;
            if((bool)job.getProperty("realtimeWarmup",false))
                std::this_thread::sleep_until(warmStart+std::chrono::duration<double>((i+1)*block/head.rate));
        }
        const juce::File output(job["output"].toString()); require(!output.exists(),"Refusing to overwrite audio output");
        auto stream=output.createOutputStream(); require(stream!=nullptr,"Cannot create audio output");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.release(),head.rate,2,32,{},0));
        require(writer!=nullptr,"Cannot create WAV writer");
        double cpu=0,worst=0,energy=0,difference=0;float peak=0;int64_t exact=0,count=0;
        juce::AudioBuffer<float> dry(2,block);
        // Optional public parameter observations for offline identification.
        // These are host-visible values, sampled immediately after each block;
        // a plugin may publish meter updates less often than audio processing.
        juce::Array<juce::AudioProcessorParameter*> traceHandles;
        juce::Array<juce::String> traceNames;
        juce::Array<juce::var> traceValues;
        if (const auto* requested=job["traceParameters"].getArray())
            for (const auto& name:*requested) {
                juce::AudioProcessorParameter* found=nullptr;
                for (auto* parameter:plugin->getParameters())
                    if (parameter->getName(256)==name.toString()) {found=parameter;break;}
                require(found!=nullptr,"Unknown trace parameter");
                traceHandles.add(found);traceNames.add(name.toString());traceValues.add(juce::Array<juce::var>{});
            }
        const auto renderStart=std::chrono::steady_clock::now();
        for(int64_t pos=0;pos<reader->lengthInSamples;pos+=block) {
            const int n=(int)std::min<int64_t>(block,reader->lengthInSamples-pos);
            b.clear(); require(reader->read(&b,0,n,pos,true,true),"Input read failed"); dry.makeCopyOf(b);
            const auto begin=std::chrono::steady_clock::now(); plugin->processBlock(b,midi);
            const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
            cpu+=elapsed; worst=std::max(worst,elapsed); midi.clear(); head.sample+=block;
            for (int j=0;j<traceHandles.size();++j)
                traceValues.getReference(j).getArray()->add(traceHandles[j]->getValue());
            for(int c=0;c<2;++c)for(int i=0;i<n;++i){ const float x=b.getSample(c,i); require(std::isfinite(x),"Non-finite plugin output");
                peak=std::max(peak,std::abs(x)); energy+=(double)x*x;
                const double d=x-dry.getSample(c,i); difference+=d*d; exact+=(d==0); ++count; }
            require(writer->writeFromAudioSampleBuffer(b,0,n),"Audio output write failed");
            if((bool)job.getProperty("realtimeRender",false))
                std::this_thread::sleep_until(renderStart+std::chrono::duration<double>((pos+n)/head.rate));
        }
        writer.reset(); result.setProperty("sampleRate",head.rate); result.setProperty("samples",reader->lengthInSamples);
        result.setProperty("block",block); result.setProperty("outputPeak",peak); result.setProperty("outputRMS",std::sqrt(energy/count));
        result.setProperty("differenceRMS",std::sqrt(difference/count)); result.setProperty("exactInputFraction",(double)exact/count);
        result.setProperty("cpuPercent",cpu/(reader->lengthInSamples/head.rate)*100); result.setProperty("worstCallbackMs",worst*1000);
        result.setProperty("job",job);
        if (!traceHandles.isEmpty()) {
            auto* traces=new juce::DynamicObject();
            for(int j=0;j<traceHandles.size();++j)traces->setProperty(traceNames[j],traceValues[j]);
            result.setProperty("parameterTraces",juce::var(traces));
            result.setProperty("parameterTraceTiming","Host-visible values after each audio block; plugin publication may be asynchronous");
        }
    }
    juce::File root; juce::VST3PluginFormat format; juce::PluginDescription description;
    BenchHead head; std::unique_ptr<juce::AudioPluginInstance> plugin; std::unique_ptr<Window> window;
    std::thread worker; std::atomic<bool> finished{false}; bool queued=false;
    juce::var pendingJob; juce::DynamicObject::Ptr pendingResult;
    juce::String lastCommandId;
};
}
START_JUCE_APPLICATION(App)
