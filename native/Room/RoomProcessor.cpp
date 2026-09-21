#include "RoomProcessor.h"
#include "VisualSettings.h"
#include "RoomPresetAssets.h"
#include "RoomEditor.h"

namespace {
#if TIDE_NATIVE_WOOD
constexpr bool separateTowerVoices=true;
#else
constexpr bool separateTowerVoices=false;
#endif
constexpr const char* globals[]={"tempo","evolution","seed","roomWidth","roomDepth","roomHeight","roomDecay","tail","output","placement","reflections","tapeOn","tapeDrive","tapeSaturation","tapeBias","tapeWarmth","tapeMix","tapeModel","spatialDrive","spatialSoftness","spatialTrim","tapeQuality","movingReflections","studioDrive","studioCream","studioBias","studioMotion","studioNoise","studioTrim","studioQuality","eightDrive","eightTrim","eightQuality","eightCalibration","eightWow","eightFlutter","eightMotionOn","wornDrive","wornAge","wornMotion","wornDamage","wornNoise","wornTrim","wornMedium","wornDips","harmonyKey","harmonyScale","harmonyChord","patternBank","woodRegen","woodWarmth","woodTail","clearDecay","clearDamping","clearTail"};
#if TIDE_CLEAR_ROOM
constexpr size_t nativeDecay=52,nativeDamping=53,nativeTail=54;
#else
constexpr size_t nativeDecay=49,nativeDamping=50,nativeTail=51;
#endif
constexpr const char* oceanControlsIds[]={"oceanOn","oceanGravity","oceanRate","oceanDamping","oceanMass0","oceanMass1","oceanMass2"};
constexpr const char* parts[]={"voice","level","density","x","y","z","brightness","length","mute"};
}
#if TIDE_NATIVE_WOOD
struct RoomProcessor::TowerAudio {
    struct Voice {
        tide::room::BinauralSource spatial;
        tide::room::MovingReflections reflections;
        juce::SmoothedValue<float> pan;
        float elevation=0;
        uint64_t serial=0;
        int drain=0;
    };
    std::array<Voice,tide::Engine::voicesCount> voices;
    juce::AudioBuffer<float> stems,dry,direct,early,mix,send;
    std::array<float,512> fieldGain{};
    void prepare(double rate,const tide::room::HrtfBank& bank){
        stems.setSize(tide::Engine::voicesCount,512);
        for(auto* b:{&dry,&direct,&early,&mix,&send})b->setSize(2,512);
        for(auto& voice:voices){voice.spatial.prepare(rate,bank);voice.reflections.prepare(rate,bank);
            voice.pan.reset(rate,.035);voice.pan.setCurrentAndTargetValue(0);}
    }
};
#endif
struct RoomProcessor::Worker final : juce::Thread {
    Worker(RoomProcessor& p,int i):Thread("Tide source "+juce::String(i+1)),owner(p),index(i) {}
    ~Worker() override {signalThreadShouldExit();wake.signal();stopThread(-1);}
    void run() override {juce::WorkgroupToken token;for(;;){wake.wait();if(threadShouldExit())return;owner.audioGroup.join(token);owner.renderSource(index,count,*events);done.signal();}}
    void dispatch(int n,const tide::room::Events& e){count=n;events=&e;wake.signal();}
    void join(){done.wait();}
    RoomProcessor& owner;int index,count=0;const tide::room::Events* events=nullptr;
    juce::WaitableEvent wake,done;
};
juce::AudioProcessorValueTreeState::ParameterLayout RoomProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    auto add=[&](const juce::String& id,const juce::String& name,float lo,float hi,float step,float value,float skew=1.f) {
        result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,juce::NormalisableRange<float>(lo,hi,step,skew),value));
    };
    add("tempo","Tempo",60,160,.1f,108);add("evolution","Evolution",0,1,.001f,.45f);add("seed","Variation",1,999999,1,31415);
    add("roomWidth","Room width",4,18,.1f,10);add("roomDepth","Room depth",4,18,.1f,8);add("roomHeight","Room height",2.5f,8,.1f,4);
    add("roomDecay","Room decay",.3f,5,.01f,1.25f,.6f);add("tail","Tail level",-30,0,.1f,-9);add("output","Output",-36,0,.1f,-3);
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"placement",1},"Placement",juce::StringArray{"Headphones","Stereo"},0));
    add("reflections","Reflections",-24,0,.1f,0);
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"movingReflections",1},"Moving reflections",true));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"tapeOn",1},"Tape",true));
    add("tapeDrive","Tape drive",0,18,.1f,6);add("tapeSaturation","Tape saturation",0,1,.001f,.5f);
    add("tapeBias","Tape bias",.25f,.95f,.001f,.65f);add("tapeWarmth","Tape warmth",0,1,.001f,.25f);add("tapeMix","Tape mix",0,1,.001f,1);
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"tapeModel",1},"Tape model",juce::StringArray{"Magnetic (0.3)","Spatial","Studio 80 / 15 ips","821","Worn tape"},1));
    add("spatialDrive","Spatial drive",0,36,.1f,27.6f);add("spatialSoftness","HF softness",0,(float)tide::room::SpatialSolver::maximumSoftness,.1f,24);add("spatialTrim","Spatial trim",-18,12,.1f,6.8f);
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"tapeQuality",1},"Oversampling",juce::StringArray{"1x Eco","2x Balanced","4x Reference"},2));
    add("eightWow","821 wow",0,4,.001f,.1f);add("eightFlutter","821 flutter",0,4,.001f,.1f);
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"eightMotionOn",1},"821 transport motion",false));
    add("eightDrive","821 recording drive",-18,24,.1f,0);add("eightTrim","821 output",-24,12,.1f,0);
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"eightCalibration",1},"821 formula and speed",juce::StringArray{"456 / 15 ips","900 / 30 ips"},0));
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"eightQuality",1},"821 limiter quality",juce::StringArray{"Native","4x","8x"},0));
    add("studioDrive","Studio recording drive",0,30,.1f,18);add("studioCream","Studio HF cream",0,3,.01f,1);add("studioBias","Studio bias colour",-1,1,.01f,0);
    add("studioMotion","Studio transport",0,3,.01f,1);add("studioNoise","Studio tape texture",0,1,.01f,.15f);add("studioTrim","Studio output",-12,12,.1f,.5f);
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"studioQuality",1},"Studio oversampling",juce::StringArray{"4x Eco","8x High","16x Finest"},2));
    add("wornDrive","Worn recording drive",0,30,.1f,14);add("wornAge","Worn tape age",0,1,.001f,.72f);
    add("wornMotion","Worn transport",0,3,.001f,1.15f);add("wornDamage","Worn contact damage",0,1,.001f,.58f);
    add("wornNoise","Worn hiss and grain",0,1,.001f,.22f);add("wornTrim","Worn output trim",-18,12,.1f,2);
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"wornMedium",1},"Worn medium",juce::StringArray{"Cassette","Home video"},0));
    add("wornDips","Worn dip depth",0,2,.001f,1);
    const tide::room::RoomSettings initial;
    juce::StringArray names;for(const auto& patch:tide::room::patches)names.add(patch.name);
    constexpr int voices[]={13,6,10};constexpr float density[]={1,.88f,.78f};
    for(int i=0;i<3;++i) {
        const auto pos=initial.positions[(size_t)i];const auto settings=tide::room::patches[(size_t)voices[i]].values;
        result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{partId(i,"voice"),1},"Source "+juce::String(i+1),names,voices[i]));
        add(partId(i,"level"),"Level",-30,0,.1f,i==2?-7.f:-3.f);add(partId(i,"density"),"Density",0,1,.001f,density[i]);
        add(partId(i,"x"),"Left / right",-.8f,.8f,.001f,pos.lateral);add(partId(i,"y"),"Depth",.1f,.68f,.001f,pos.depth);
        add(partId(i,"z"),"Height",.3f,2.2f,.01f,pos.height);add(partId(i,"brightness"),"Brightness",0,1,.001f,settings.timbre);
        add(partId(i,"length"),"Note decay",.06f,2.5f,.001f,settings.decay,.4f);
        result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{partId(i,"mute"),1},"Mute",false));
    }
    juce::StringArray destinations,divisions;
    for(const auto* name:tide::room::motionTargets)destinations.add(name);
    for(const auto* name:tide::room::motionDivisions)divisions.add(name);
    for(int i=0;i<tide::room::lfoCount;++i) {
        const auto id=[i](const char* field){return lfoId(i,field);};
        result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id("on"),1},"LFO "+juce::String(i+1)+" on",false));
        result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id("target"),1},"Destination",destinations,1+(i/2)*3+i%2));
        result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id("shape"),1},"Shape",juce::StringArray{"Sine","Triangle"},0));
        result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id("sync"),1},"Sync",false));
        add(id("rate"),"LFO rate",.01f,4,.001f,.07f+.02f*(float)i,.35f);
        result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id("division"),1},"Cycle length",divisions,3));
        add(id("depth"),"LFO depth",0,1,.001f,.25f);add(id("phase"),"LFO phase",0,360,.1f,i%2?90.f:0.f);
    }
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"harmonyKey",1},"Key",juce::StringArray{"C","C# / Db","D","D# / Eb","E","F","F# / Gb","G","G# / Ab","A","A# / Bb","B"},2));
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"harmonyScale",1},"Scale",juce::StringArray{"Major","Minor"},1));
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"harmonyChord",1},"Chord degree",juce::StringArray{"1","2","3","4","5","6","7"},0));
    juce::StringArray patterns;for(const auto& pattern:tide::room::patternBank)patterns.add(pattern.name);
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"patternBank",1},"Global pattern",patterns,0));
    add("woodRegen","Wood room sustain",0,1,.001f,.5f);
    add("woodWarmth","Wood room warmth",0,1,.001f,.25f);
    add("woodTail","Wood room tail",-36,6,.1f,-6);
    add("clearDecay","Clear room decay",.25f,4,.01f,1.2f,.6f);
    add("clearDamping","Clear room damping",0,1,.001f,.35f);
    add("clearTail","Clear room tail",-36,6,.1f,-12);
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"oceanOn",1},"Ocean patches",false));
    add("oceanGravity","Ocean gravity",0,4,.001f,1);
    add("oceanRate","Ocean slosh rate",.05f,1.5f,.001f,.22f,.5f);
    add("oceanDamping","Ocean damping",.15f,1.5f,.001f,.38f);
    for(int i=0;i<3;++i)add("oceanMass"+juce::String(i),"Planet "+juce::String(i+1)+" mass",0,4,.001f,1);
    juce::StringArray oceanSources,oceanDestinations;
    for(const auto* name:tide::room::oceanSources)oceanSources.add(name);
    for(const auto* name:tide::room::oceanTargets)oceanDestinations.add(name);
    for(int i=0;i<tide::room::oceanRoutes;++i) {
        result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{oceanId(i,"source"),1},"Ocean source",oceanSources,i));
        result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{oceanId(i,"target"),1},"Ocean destination",oceanDestinations,0));
        add(oceanId(i,"amount"),"Ocean patch amount",-1,1,.001f,.5f);
    }
    for(const auto& c:tide::tides::controls)add(c.id,c.name,c.low,c.high,c.step,c.initial);
    for(const auto& c:tide::visual::controls)add(c.id,c.name,c.low,c.high,c.step,c.initial);
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"visualEnabled",1},"Visual effects",true));
    return result;
}
RoomProcessor::RoomProcessor(bool autoload):AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)),parameters(*this,nullptr,"TideRoomState",layout()),autoLoad(autoload) {
    for(size_t i=0;i<global.size();++i)global[i]=parameters.getRawParameterValue(globals[i]);
    for(int i=0;i<3;++i)for(size_t k=0;k<part[0].size();++k)part[(size_t)i][k]=parameters.getRawParameterValue(partId(i,parts[k]));
    for(int i=0;i<tide::room::lfoCount;++i)for(size_t k=0;k<lfo[0].size();++k)lfo[(size_t)i][k]=parameters.getRawParameterValue(lfoId(i,tide::room::lfoFields[k]));
    for(size_t i=0;i<oceanControls.size();++i)oceanControls[i]=parameters.getRawParameterValue(oceanControlsIds[i]);
    for(int i=0;i<tide::room::oceanRoutes;++i)for(size_t k=0;k<3;++k)oceanRoutes[(size_t)i][k]=parameters.getRawParameterValue(oceanId(i,tide::room::oceanFields[k]));
    for(size_t i=0;i<tidesControls.size();++i)tidesControls[i]=parameters.getRawParameterValue(tide::tides::controls[i].id);
    motion.prepare(rate);ocean.prepare(rate);planetMotion.prepare(rate);updateMotion(0,false);
#if TIDE_NATIVE_WOOD
    loaded.store(true);
#if TIDE_CLEAR_ROOM
    statusText="Clear room / one shared space";
#else
    statusText="Airwindows kWoodRoom / one shared room";
#endif
#endif
    if(autoLoad){
        if(auto xml=juce::parseXML(juce::String::fromUTF8(RoomPresetAssets::SlowTides_xml,RoomPresetAssets::SlowTides_xmlSize))){juce::MemoryBlock state;copyXmlToBinary(*xml,state);setStateInformation(state.getData(),(int)state.getSize());}
        useSimpleInterface();startTimerHz(20);
    }
}
RoomProcessor::~RoomProcessor(){stopTimer();recording.reset();for(auto& w:workers)w.reset();room.reset();}
bool RoomProcessor::startRecording(const juce::File& file,juce::String& error) {
    double sampleRate=0;
    {const juce::ScopedLock lock(getCallbackLock());
        if(!prepared||!loaded.load()){error="Start the audio device before recording.";return false;}
        if(recording&&!recording->finished()){error="The previous take is still recording or saving.";return false;}
        sampleRate=rate;
    }
    auto next=OutputRecorder::create(file,sampleRate,error);if(!next)return false;
    {const juce::ScopedLock lock(getCallbackLock());
        if(!prepared||rate!=sampleRate||(recording&&!recording->finished())){error="Audio settings changed. Please try recording again.";return false;}
        recording.swap(next);
    } // Any previous take is destroyed after releasing the audio callback lock.
    return true;
}
void RoomProcessor::stopRecording(bool deviceStopped){const juce::ScopedLock lock(getCallbackLock());if(recording)recording->requestStop(deviceStopped);}
OutputRecorder::State RoomProcessor::recordingState() const {const juce::ScopedLock lock(getCallbackLock());return recording?recording->state():OutputRecorder::State{};}
juce::Optional<juce::AudioPlayHead::PositionInfo> RoomProcessor::Head::getPosition() const {
    PositionInfo p;p.setBpm(bpm.load());p.setTimeSignature(TimeSignature{4,4});p.setTimeInSamples(samples.load());p.setTimeInSeconds(seconds.load());p.setPpqPosition(ppq.load());p.setIsPlaying(playing.load());return p;
}
void RoomProcessor::prepareToPlay(double sr,int) {
    {const juce::ScopedLock lock(getCallbackLock());prepared=false;stopRecording(true);}
    for(auto& w:workers)w.reset();
    rate=sr;dspLoad.store(0);peakDspLoad.store(0);lateBlocks.store(0);pattern.reset(sr);dormant=false;silentSamples=0;elapsed=0;beatPosition=0;wasPlaying=false;
    for(auto& light:noteLights)light.store(0);for(auto& voice:pitchLights)for(auto& light:voice)light.store(0);
    motion.prepare(sr);ocean.prepare(sr);planetMotion.prepare(sr);for(auto& signal:oceanSignals)signal.store(0);resetMotion.store(false);updateMotion(0,false);
    updateTape();tape.prepare(sr,chunkSize);
#if TIDE_NATIVE_WOOD
    wood.prepare(sr,global[nativeDecay]->load(),global[nativeDamping]->load());woodBus.setSize(2,chunkSize);
    for(size_t i=0;i<3;++i){woodSources[i].setSize(2,chunkSize);sendPan[i].reset(sr,.035);sendPan[i].setCurrentAndTargetValue(0);}
    headphones.store(global[9]->load()<.5f);movingRoom.store(global[22]->load()>.5f);
#endif
    hrtf.prepare(sr);for(size_t i=0;i<3;++i){spatial[i].prepare(sr,hrtf);direct[i].setSize(2,chunkSize);field[i].reset(sr,.025);field[i].setCurrentAndTargetValue(global[9]->load()<.5f?juce::Decibels::decibelsToGain(global[10]->load()):1.f);}
    for(size_t i=0;i<3;++i){reflections[i].prepare(sr,hrtf);early[i].setSize(2,chunkSize);}
    for(size_t i=0;i<3;++i){auto s=tide::room::patches[(size_t)(int)part[i][0]->load()].values;s.space=0;s.output=0;engines[i].setSettings(s);engines[i].prepare(sr,chunkSize,false,separateTowerVoices);work[i].setSize(2,chunkSize);gains[i].reset(sr,.02);gains[i].setCurrentAndTargetValue(juce::Decibels::decibelsToGain(part[i][1]->load()));}
#if TIDE_NATIVE_WOOD
    for(auto& source:towerAudio){source=std::make_unique<TowerAudio>();source->prepare(sr,hrtf);}
    for(auto& source:pitchHeights)for(auto& height:source)height.store(0);
#endif
    master.reset(sr,.025);master.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(global[8]->load()));gate.reset(sr,.005);gate.setCurrentAndTargetValue(1);
    muteBus.reset(sr,.02);muteBus.setCurrentAndTargetValue(part[0][8]->load()>.5f&&part[1][8]->load()>.5f&&part[2][8]->load()>.5f?0.f:1.f);
    if(room)room->prepare(sr,chunkSize);setLatencySamples(tide::Engine::latency+tape.latency()+(room?room->latency():0));
    for(int i=0;i<2;++i){auto worker=std::make_unique<Worker>(*this,i+1);const double period=chunkSize/sr*1000;
        if(worker->startRealtimeThread(juce::Thread::RealtimeOptions{}.withProcessingTimeMs(period*.5).withMaximumProcessingTimeMs(period).withPeriodMs(period))||worker->startThread(juce::Thread::Priority::high))workers[(size_t)i]=std::move(worker);
    }
    prepared=true;
}
void RoomProcessor::releaseResources(){prepared=false;stopRecording(true);for(auto& w:workers)w.reset();for(auto& light:noteLights)light.store(0);for(auto& voice:pitchLights)for(auto& light:voice)light.store(0);tape.release();if(room)room->release();}
void RoomProcessor::audioWorkgroupContextChanged(const juce::AudioWorkgroup& group){const juce::ScopedLock lock(getCallbackLock());audioGroup=group;tape.workgroupChanged(group);}
void RoomProcessor::updateTape(){
    const tide::room::TapeSettings a{global[11]->load()>.5f,global[12]->load(),global[13]->load(),global[14]->load(),global[15]->load(),global[16]->load()};
    const tide::room::SpatialSettings s{a.enabled,global[18]->load(),global[19]->load(),global[20]->load(),a.mix,(int)global[21]->load()};const tide::room::StudioSettings m{global[23]->load(),global[24]->load(),global[25]->load(),global[26]->load(),global[27]->load(),global[28]->load(),(int)global[29]->load()};const tide::room::EightTwentyOneSettings e{global[30]->load(),global[31]->load(),(int)global[32]->load(),(int)global[33]->load(),global[34]->load(),global[35]->load(),global[36]->load()>.5f};const tide::room::WornSettings w{global[37]->load(),global[38]->load(),global[39]->load(),global[40]->load(),global[41]->load(),global[42]->load(),(int)global[43]->load(),global[44]->load()};tape.setSettings(a,s,m,e,w,(int)global[17]->load());
}
bool RoomProcessor::loadRoom(const juce::File& file) {
#if TIDE_NATIVE_WOOD
    juce::ignoreUnused(file);return false; // This edition never loads an external reverb.
#else
    attempted=true;playing.store(false);juce::String error;
    auto candidate=std::make_unique<tide::room::ReversideRoom>();
    if(!candidate->load(file,rate,chunkSize,&head,error)){statusText=error;return false;}
    for(int i=1;i<3;++i)if(candidate->instance(i)->getLatencySamples()!=candidate->instance(0)->getLatencySamples()){statusText="Reverside source latencies do not match.";return false;}
    if(pendingRoom.isValid())candidate->restoreState(pendingRoom);
    candidate->configure(roomSettings());headphones.store(roomSettings().headphones);movingRoom.store(roomSettings().movingReflections);
    {const juce::ScopedLock lock(getCallbackLock());room=std::move(candidate);loaded.store(true);setLatencySamples(tide::Engine::latency+tape.latency()+room->latency());}
    statusText="Three sources / "+room->description();return true;
#endif
}
void RoomProcessor::timerCallback() {
#if !TIDE_NATIVE_WOOD
    if(autoLoad&&prepared&&!attempted)loadRoom(juce::File("/Library/Audio/Plug-Ins/VST3/DPA_Reverside.vst3"));
#endif
    updateRoom();
    if(audition821&&loaded.load()){audition821=false;play();}
    if(loaded.load()&&captureFolder!=juce::File{}&&!capture){
        auto next=std::make_unique<RoomCapture>(captureFolder,rate);
        openRoomEditor();
        auto snapshot=parameters.copyState();snapshot.removeChild(snapshot.getChildWithName("Reverside"),nullptr);
        next->parametersStart=snapshot.toXmlString();
        peakDspLoad.store(0);lateBlocks.store(0);
        {const juce::ScopedLock lock(getCallbackLock());capture=std::move(next);}
        play();
    }
    if(capture&&!captureSaved&&capture->complete.load()){
        captureSaved=true;
        auto snapshot=parameters.copyState();snapshot.removeChild(snapshot.getChildWithName("Reverside"),nullptr);
        capture->parametersEnd=snapshot.toXmlString();
        capture->performance="{\"peak_callback_load\":"+juce::String(peakDspLoad.load(),9)+",\"late_callbacks\":"+juce::String((juce::int64)lateBlocks.load())+"}";
        try{capture->save();statusText="Diagnostic recording saved";}catch(const std::exception& e){statusText=e.what();}
    }
}
void RoomProcessor::updateRoom(){
#if TIDE_NATIVE_WOOD
    headphones.store(global[9]->load()<.5f);movingRoom.store(global[22]->load()>.5f);
#endif
    if(room){const auto settings=roomSettings();
    // Hosted geometry follows only the manually placed anchors. Continuous
    // LFO motion is rendered by our direct path and image-source reflections.
    if(settings.headphones!=headphones.load()||settings.movingReflections!=movingRoom.load()){const juce::ScopedLock lock(getCallbackLock());room->shareEditorChanges();room->configure(settings);headphones.store(settings.headphones);movingRoom.store(settings.movingReflections);}else{room->shareEditorChanges();room->configure(settings);}}}
void RoomProcessor::openRoomEditor(){if(room)room->openEditor();}
float RoomProcessor::get(const juce::String& id) const {return parameters.getRawParameterValue(id)->load();}
void RoomProcessor::set(const juce::String& id,float value){if(auto* p=parameters.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(value));p->endChangeGesture();}}
void RoomProcessor::useSimpleInterface(){
    simpleInterface=true;set("tapeModel",4);
    // A neutral travel setting preserves an old scene with all motion off,
    // while making the simplified Travel control useful when it is raised.
    bool moving=false;for(int i=0;i<tide::room::lfoCount;++i)moving|=get(lfoId(i,"on"))>.5f;
    if(!moving){set("tideTravel",0);for(int i=0;i<tide::room::lfoCount;++i)set(lfoId(i,"on"),1);}
    bool patched=false;for(int i=0;i<tide::room::oceanRoutes;++i)patched|=get(oceanId(i,"target"))>0;
    if(!patched)for(int i=0;i<3;++i){set(oceanId(i*2,"source"),(float)(i*2));set(oceanId(i*2,"target"),(float)(i*4+1));set(oceanId(i*2,"amount"),.25f);
        set(oceanId(i*2+1,"source"),(float)(6+i));set(oceanId(i*2+1,"target"),(float)(i*4+2));set(oceanId(i*2+1,"amount"),.35f);}
}
void RoomProcessor::selectWornPreset(int index){
    const auto s=tide::room::wornPreset(index);const char* ids[]={"wornDrive","wornAge","wornMotion","wornDamage","wornNoise","wornTrim","wornMedium","wornDips"};
    const float values[]={s.drive,s.age,s.motion,s.damage,s.noise,s.trim,(float)s.medium,s.dips};
    for(int i=0;i<8;++i)set(ids[i],values[i]);set("tapeModel",4);set("tapeMix",1);set("tapeOn",1);
}
tide::room::RoomSettings RoomProcessor::roomSettings() const {
    tide::room::RoomSettings s;s.width=global[3]->load();s.depth=global[4]->load();s.height=global[5]->load();s.decay=global[6]->load();s.tail=global[7]->load();
    for(size_t i=0;i<3;++i)s.positions[i]={part[i][3]->load(),part[i][4]->load(),s.height*tide::room::towerHeightRatio};s.headphones=global[9]->load()<.5f;s.movingReflections=global[22]->load()>.5f;return s;
}
void RoomProcessor::play(){if(loaded.load()){restart.store(true);playing.store(true);}}
void RoomProcessor::stop(){playing.store(false);}
void RoomProcessor::quiet(){playing.store(false);silence.store(true);}
void RoomProcessor::vary(){set("seed",(float)(1+((uint32_t)get("seed")*1664525u+1013904223u)%999999u));if(playing.load())restart.store(true);}
void RoomProcessor::selectVoice(int i,int index){index=juce::jlimit(0,tide::room::patchCount-1,index);set(partId(i,"voice"),(float)index);set(partId(i,"brightness"),tide::room::patches[(size_t)index].values.timbre);set(partId(i,"length"),tide::room::patches[(size_t)index].values.decay);}
void RoomProcessor::updateMotion(int count,bool active) {
    if(resetMotion.exchange(false)){motion.prepare(rate);ocean.prepare(rate);planetMotion.prepare(rate);for(auto& signal:oceanSignals)signal.store(0);}
    tide::room::Motion::Settings settings;
    for(size_t i=0;i<settings.size();++i){const auto& p=lfo[i];settings[i]={p[0]->load()>.5f&&((int)p[1]->load()%3!=0),(int)p[1]->load(),(int)p[2]->load(),p[3]->load()>.5f,p[4]->load()*tidesControls[0]->load(),(int)p[5]->load(),p[6]->load()*tidesControls[1]->load(),p[7]->load()};}
    tide::room::Motion::Values anchors;
    for(size_t i=0;i<3;++i)anchors[i]={part[i][3]->load(),part[i][4]->load(),towerHeight()};
    const auto targets=motion.process(settings,anchors,count,global[0]->load()*tidesControls[0]->load(),active);
    tide::room::PlanetMotion::Positions physical=targets;
    const float width=global[3]->load(),depth=global[4]->load();
    for(auto& p:physical){p[0]*=width*.5f;p[1]*=depth;p[2]=towerHeight();}
    const auto& resolved=planetMotion.process(physical,count,width,depth,global[5]->load(),tidesControls[4]->load(),tidesControls[2]->load(),towerHeight());
    for(size_t i=0;i<3;++i){positions[i]={resolved[i][0]*2/width,resolved[i][1]/depth,resolved[i][2]};
        impactSignals[i].store(planetMotion.impacts()[i]);waterConnections[i].store(planetMotion.connections()[i]);
        for(size_t a=0;a<3;++a)waterKicks[i][a].store(planetMotion.waterKicks()[i][a]);}
    positionVersion.fetch_add(1);
    for(size_t i=0;i<3;++i)for(size_t a=0;a<3;++a)displayedPositions[i][a].store(positions[i][a]);
    positionVersion.fetch_add(1);
    for(size_t i=0;i<settings.size();++i)lfoSignals[i].store(motion.signalValues()[i]);
}
tide::room::Position RoomProcessor::movingPosition(int i)const {const auto& p=displayedPositions[(size_t)i];for(;;){const auto version=positionVersion.load();if(version&1u)continue;const tide::room::Position value{p[0].load(),p[1].load(),p[2].load()};if(positionVersion.load()==version)return value;}}
void RoomProcessor::updateOcean(int count) {
    tide::room::OceanSettings settings;
    settings.gravity=oceanControls[1]->load();settings.rate=oceanControls[2]->load();settings.damping=oceanControls[3]->load();
    tide::room::Ocean::Positions physical=positions;
    for(size_t i=0;i<3;++i){settings.mass[i]=oceanControls[i+4]->load();physical[i][0]*=global[3]->load()*.5f;physical[i][1]*=global[4]->load();}
    const auto& values=ocean.process(physical,settings,count);
    for(size_t i=0;i<values.size();++i)oceanSignals[i].store(values[i]);
    std::array<tide::room::OceanRoute,tide::room::oceanRoutes> routes;
    for(size_t i=0;i<routes.size();++i)routes[i]={(int)oceanRoutes[i][0]->load(),(int)oceanRoutes[i][1]->load(),oceanRoutes[i][2]->load()};
    auto modulation=tide::room::oceanModulation(values,routes,oceanControls[0]->load()>.5f,planetMotion.impacts());
    for(auto& source:modulation)for(auto& value:source)value*=tidesControls[5]->load();
    for(size_t i=0;i<3;++i) {
        auto s=tide::room::patches[(size_t)(int)part[i][0]->load()].values;s.space=0;s.output=0;
        s.timbre=std::clamp(part[i][6]->load()+modulation[i][0],0.f,1.f);
        s.modDepth=std::clamp(s.modDepth+modulation[i][1],0.f,1.f);
        s.modRatio=std::clamp(s.modRatio*std::exp2(2*modulation[i][2]),.125f,6.f);
        s.decay=std::clamp(part[i][7]->load()*std::exp2(2*modulation[i][3]),.06f,2.5f);
        // Engine controls have existing per-sample smoothing. Base parameters
        // remain untouched, so disconnecting returns to the saved sound.
        engines[i].setSettings(s);
    }
}
void RoomProcessor::oceanAudition() {
    selectVoice(0,14);selectVoice(1,6);selectVoice(2,17);
    for(int i=0;i<tide::room::lfoCount;++i)set(lfoId(i,"on"),0);
    for(int i=0;i<2;++i){set(lfoId(i,"target"),(float)(i+1));set(lfoId(i,"rate"),.11f);set(lfoId(i,"depth"),.42f);set(lfoId(i,"phase"),i?90.f:0.f);set(lfoId(i,"shape"),0);set(lfoId(i,"sync"),0);set(lfoId(i,"on"),1);}
    for(int i=0;i<tide::room::oceanRoutes;++i){set(oceanId(i,"target"),0);set(oceanId(i,"source"),(float)i);set(oceanId(i,"amount"),.5f);}
    set(oceanId(0,"target"),5);set(oceanId(0,"amount"),-.8f);
    set(oceanId(1,"source"),3);set(oceanId(1,"target"),11);set(oceanId(1,"amount"),.7f);
    set(oceanId(2,"source"),4);set(oceanId(2,"target"),2);set(oceanId(2,"amount"),-1);
    set("oceanGravity",1.5f);set("oceanRate",.22f);set("oceanDamping",.3f);
    for(int i=0;i<3;++i)set("oceanMass"+juce::String(i),1);
    set("oceanOn",1);
}
void RoomProcessor::collisionAudition() {
    oceanAudition();selectVoice(0,14);selectVoice(1,14);
    for(int i=0;i<tide::room::lfoCount;++i)set(lfoId(i,"on"),0);
    set(partId(0,"x"),-.11f);set(partId(1,"x"),.11f);set(partId(2,"x"),.52f);
    for(int i=0;i<2;++i){set(partId(i,"y"),.32f);set(partId(i,"z"),1.45f);}
    set(partId(2,"y"),.62f);set(partId(2,"z"),1.9f);
    set(lfoId(0,"target"),1);set(lfoId(0,"rate"),.28f);set(lfoId(0,"depth"),.28f);set(lfoId(0,"phase"),0);set(lfoId(0,"on"),1);
    for(int i=0;i<tide::room::oceanRoutes;++i)set(oceanId(i,"target"),0);
    set(oceanId(0,"source"),6);set(oceanId(0,"target"),2);set(oceanId(0,"amount"),-.55f);
    set(oceanId(1,"source"),7);set(oceanId(1,"target"),7);set(oceanId(1,"amount"),.65f);
    resetMotion.store(true);
}
tide::room::TowerNotes RoomProcessor::towerNotes(int partIndex)const {
    tide::room::PatternSettings settings;settings.bank=(int)get("patternBank");settings.evolution=get("evolution");
    settings.harmony={(int)get("harmonyKey"),(int)get("harmonyScale"),(int)get("harmonyChord")};
    return tide::room::TowerNotes::forPattern(settings,partIndex);
}
void RoomProcessor::renderSource(int i,int count,const tide::room::Events& events) {
#if TIDE_NATIVE_WOOD
    renderTowerSource(i,count,events);return;
#endif
    juce::ScopedNoDenormals noDenormals;
    const auto p=(size_t)i;juce::AudioBuffer<float> b(work[p].getArrayOfWritePointers(),2,count);b.clear();int position=0;
    float light=noteLights[p].load(std::memory_order_relaxed);
    std::array<float,128> pitches{};
    for(size_t pitch=0;pitch<pitches.size();++pitch)pitches[pitch]=pitchLights[p][pitch].load(std::memory_order_relaxed);
    const auto decayLights=[&](int frames){const float decay=(float)std::exp(-frames/(rate*.18));light*=decay;for(auto& value:pitches)value*=decay;};
    for(int n=0;n<events.size;++n){const auto& event=events.data[(size_t)n];if(event.part!=i)continue;engines[p].render(b.getWritePointer(0)+position,b.getWritePointer(1)+position,event.offset-position);
        decayLights(event.offset-position);position=event.offset;
        if(event.on){if(part[p][8]->load()<.5f){engines[p].midi(juce::MidiMessage::noteOn(1,event.note,event.velocity));light=std::sqrt(std::clamp(event.velocity,0.f,1.f));pitches[(size_t)std::clamp(event.note,0,127)]=light;}lastStep[p].store(event.step);}
        else engines[p].midi(juce::MidiMessage::noteOff(1,event.note));
    }
    engines[p].render(b.getWritePointer(0)+position,b.getWritePointer(1)+position,count-position);
    decayLights(count-position);
    for(size_t pitch=0;pitch<pitches.size();++pitch)pitchLights[p][pitch].store(part[p][8]->load()>.5f||pitches[pitch]<.0001f?0.f:pitches[pitch],std::memory_order_relaxed);
    noteLights[p].store(part[p][8]->load()>.5f||light<.0001f?0.f:light,std::memory_order_relaxed);
    // One direct path: measured ear filters in Headphones mode, Reverside's
    // original direct path in Original mode. Reverside renders the room field.
    for(int n=0;n<count;++n){const float x=.5f*(b.getSample(0,n)+b.getSample(1,n));b.setSample(0,n,x);b.setSample(1,n,x);}
    const bool focus=headphones.load(),smooth=movingRoom.load(),ownDirect=
#if TIDE_NATIVE_WOOD
        true;
#else
        focus||smooth;
#endif
    const float x=positions[p][0]*global[3]->load()*.5f,y=positions[p][1]*global[4]->load(),z=positions[p][2]-listenerHeight();
    spatial[p].render(b.getReadPointer(0),direct[p].getWritePointer(0),direct[p].getWritePointer(1),count,x,y,z,ownDirect,focus);
    if(capture&&!capture->complete.load())capture->tap(20+2*i,direct[p],count);
    reflections[p].render(b.getReadPointer(0),early[p].getWritePointer(0),early[p].getWritePointer(1),count,x,y,z,global[3]->load(),global[4]->load(),global[5]->load(),smooth,focus,listenerHeight());
    if(capture&&!capture->complete.load())capture->source(i,false,b);
#if TIDE_NATIVE_WOOD
    juce::AudioBuffer<float> send(woodSources[p].getArrayOfWritePointers(),2,count);
    // The diffuse send has a soft stereo direction, without an ear filter or
    // direct-path 1/r attenuation. Its propagation delay follows the source.
    // Distance therefore increases the reflected/direct balance naturally.
    sendPan[p].setTargetValue(.55f*x/std::max(.01f,std::sqrt(x*x+y*y+z*z)));
    for(int n=0;n<count;++n){const float pan=sendPan[p].getNextValue(),mono=b.getSample(0,n);
        send.setSample(0,n,mono*std::sqrt(.5f*(1-pan)));send.setSample(1,n,mono*std::sqrt(.5f*(1+pan)));}
    b.clear();
#else
    if(room)room->process(i,b);else b.clear();
#endif
#if !TIDE_NATIVE_WOOD
    if(capture&&!capture->complete.load())capture->source(i,true,b);
#endif
    for(int c=0;c<2;++c)b.addFrom(c,0,early[p],c,0,count);
    if(capture&&!capture->complete.load())capture->early(i,early[p],count);
    field[p].setTargetValue(ownDirect?juce::Decibels::decibelsToGain(global[10]->load()):1.f);
    for(int n=0;n<count;++n){const float gain=field[p].getNextValue();for(int c=0;c<2;++c)b.setSample(c,n,b.getSample(c,n)*gain);}
    for(int c=0;c<2;++c)b.addFrom(c,0,direct[p],c,0,count);
#if TIDE_NATIVE_WOOD
    spatial[p].propagation(b,true,&send);
    if(capture&&!capture->complete.load())capture->source(i,true,send);
#else
    spatial[p].propagation(b,ownDirect);
#endif
    if(capture&&!capture->complete.load())capture->tap(26+2*i,b,count);
}
#if TIDE_NATIVE_WOOD
void RoomProcessor::renderTowerSource(int i,int count,const tide::room::Events& events) {
    juce::ScopedNoDenormals noDenormals;
    const auto p=(size_t)i;auto& audio=*towerAudio[p];auto& engine=engines[p];
    for(auto* buffer:{&work[p],&direct[p],&early[p],&woodSources[p],&audio.dry})buffer->clear();
    const float width=global[3]->load(),depth=global[4]->load(),height=global[5]->load();
    const float listener=tide::room::listenerHeight(height),x=positions[p][0]*width*.5f,y=positions[p][1]*depth;
    const bool focus=headphones.load(),smooth=movingRoom.load();
    tide::room::PatternSettings settings;settings.bank=(int)global[48]->load();settings.evolution=global[1]->load();
    settings.harmony={(int)global[45]->load(),(int)global[46]->load(),(int)global[47]->load()};
    const auto notes=tide::room::TowerNotes::forPattern(settings,i);
    field[p].setTargetValue(juce::Decibels::decibelsToGain(global[10]->load()));
    for(int n=0;n<count;++n)audio.fieldGain[(size_t)n]=field[p].getNextValue();
    float light=noteLights[p].load(std::memory_order_relaxed);std::array<float,128> pitches{};
    for(size_t pitch=0;pitch<pitches.size();++pitch)pitches[pitch]=pitchLights[p][pitch].load(std::memory_order_relaxed);
    const auto render=[&](int offset,int frames){
        if(frames<=0)return;
        std::array<float*,tide::Engine::voicesCount> pointers{};
        for(int v=0;v<tide::Engine::voicesCount;++v){auto& voice=audio.voices[(size_t)v];pointers[(size_t)v]=audio.stems.getWritePointer(v);
            const auto serial=engine.voiceSerial(v);const int pitch=engine.voiceNote(v),plate=notes.index(pitch);
            if(serial!=voice.serial){voice.serial=serial;voice.elevation=plate>=0?notes.worldElevation(plate,height):height*tide::room::towerHeightRatio;}
            else if(plate>=0)voice.elevation=notes.worldElevation(plate,height);
            // Each ringing oscillator retains its own source, even after another
            // pitch starts. Retired pitches keep their last physical elevation.
            if(engine.voiceActive(v)){voice.drain=(int)(rate*1.0);pitchHeights[p][(size_t)pitch].store(voice.elevation,std::memory_order_relaxed);}
        }
        engine.renderVoices(pointers,frames);
        for(int v=0;v<tide::Engine::voicesCount;++v){auto& voice=audio.voices[(size_t)v];
            if(voice.drain<=0)continue;voice.drain=std::max(0,voice.drain-frames);
            const float* mono=pointers[(size_t)v];const float z=voice.elevation-listener;
            voice.spatial.render(mono,audio.direct.getWritePointer(0),audio.direct.getWritePointer(1),frames,x,y,z,true,focus);
            voice.reflections.render(mono,audio.early.getWritePointer(0),audio.early.getWritePointer(1),frames,x,y,z,width,depth,height,smooth,focus,listener);
            voice.pan.setTargetValue(.55f*x/std::max(.01f,std::sqrt(x*x+y*y+z*z)));
            for(int n=0;n<frames;++n){const float pan=voice.pan.getNextValue();
                for(int c=0;c<2;++c){const float d=audio.direct.getSample(c,n),e=audio.early.getSample(c,n);
                    audio.mix.setSample(c,n,d+e*audio.fieldGain[(size_t)(offset+n)]);
                    audio.send.setSample(c,n,mono[n]*std::sqrt(.5f*(1+(c?pan:-pan))));
                    audio.dry.addSample(c,offset+n,mono[n]);direct[p].addSample(c,offset+n,d);early[p].addSample(c,offset+n,e);}
            }
            juce::AudioBuffer<float> mixed(audio.mix.getArrayOfWritePointers(),2,frames),send(audio.send.getArrayOfWritePointers(),2,frames);
            voice.spatial.propagation(mixed,true,&send);
            for(int c=0;c<2;++c){work[p].addFrom(c,offset,mixed,c,0,frames);woodSources[p].addFrom(c,offset,send,c,0,frames);}
        }
        const float decay=(float)std::exp(-frames/(rate*.18));light*=decay;for(auto& value:pitches)value*=decay;
    };
    int position=0;
    for(int n=0;n<events.size;++n){const auto& event=events.data[(size_t)n];if(event.part!=i)continue;
        render(position,event.offset-position);position=event.offset;
        if(event.on){if(part[p][8]->load()<.5f){engine.midi(juce::MidiMessage::noteOn(1,event.note,event.velocity));
            light=std::sqrt(std::clamp(event.velocity,0.f,1.f));pitches[(size_t)std::clamp(event.note,0,127)]=light;}lastStep[p].store(event.step);}
        else engine.midi(juce::MidiMessage::noteOff(1,event.note));
    }
    render(position,count-position);
    for(size_t pitch=0;pitch<pitches.size();++pitch)pitchLights[p][pitch].store(part[p][8]->load()>.5f||pitches[pitch]<.0001f?0.f:pitches[pitch],std::memory_order_relaxed);
    noteLights[p].store(part[p][8]->load()>.5f||light<.0001f?0.f:light,std::memory_order_relaxed);
    if(capture&&!capture->complete.load()){
        juce::AudioBuffer<float> dry(audio.dry.getArrayOfWritePointers(),2,count),send(woodSources[p].getArrayOfWritePointers(),2,count);
        capture->source(i,false,dry);capture->source(i,true,send);
        capture->tap(20+2*i,direct[p],count);capture->early(i,early[p],count);capture->tap(26+2*i,work[p],count);
    }
}
#endif
void RoomProcessor::processBlock(juce::AudioBuffer<float>& output,juce::MidiBuffer& midi) {
    const auto started=juce::Time::getHighResolutionTicks();
    juce::ScopedNoDenormals noDenormals;output.clear();midi.clear();
    if(output.getNumChannels()<2||!prepared||!loaded.load()){if(recording)recording->push(output);return;}
    // Drain real tails before sleeping; Play resumes the prepared graph.
    if(dormant&&!playing.load()){updateMotion(output.getNumSamples(),false);updateOcean(output.getNumSamples());if(silence.exchange(false))gate.setCurrentAndTargetValue(0);if(recording)recording->push(output);dspLoad.store(0);peak.store(0);for(auto& meter:meters)meter.store(0);return;}
    dormant=false;
    if(silence.exchange(false)){gate.setTargetValue(0);for(auto& e:engines)e.panic();for(auto& light:noteLights)light.store(0);for(auto& voice:pitchLights)for(auto& light:voice)light.store(0);}
    const bool active=playing.load()&&loaded.load();
    if(active&&(!wasPlaying||restart.exchange(false))) {
        restart.store(false);pattern.reset(rate);elapsed=0;beatPosition=0;gate.setTargetValue(1);for(auto& s:lastStep)s.store(-1);
        motion.restart();
        for(auto& e:engines)e.midi(juce::MidiMessage::allNotesOff(1));
    }
    if(wasPlaying&&!active)for(auto& e:engines)e.midi(juce::MidiMessage::allNotesOff(1));
    wasPlaying=active;
    tide::room::PatternSettings ps;ps.bpm=global[0]->load();ps.evolution=global[1]->load();ps.seed=(uint32_t)global[2]->load();ps.harmony={(int)global[45]->load(),(int)global[46]->load(),(int)global[47]->load()};ps.bank=(int)global[48]->load();
    for(size_t i=0;i<3;++i)ps.density[i]=part[i][2]->load();
    master.setTargetValue(juce::Decibels::decibelsToGain(global[8]->load()));
    muteBus.setTargetValue(part[0][8]->load()>.5f&&part[1][8]->load()>.5f&&part[2][8]->load()>.5f?0.f:1.f);
    updateTape();
    float tailActivity=0;
    for(int offset=0;offset<output.getNumSamples();offset+=chunkSize) {
        const int count=std::min(chunkSize,output.getNumSamples()-offset);
        updateMotion(count,active);updateOcean(count);
#if TIDE_NATIVE_WOOD
        woodBus.clear();
#endif
        for(size_t i=0;i<3;++i){const float x=positions[i][0]*global[3]->load()*.5f,y=positions[i][1]*global[4]->load(),z=positions[i][2]-listenerHeight();const float distance=std::sqrt(x*x+y*y+z*z);gains[i].setTargetValue(part[i][8]->load()>.5f?0.f:((headphones.load()||movingRoom.load()
#if TIDE_NATIVE_WOOD
            ||true
#endif
            )?1.f:std::sqrt(3.f/std::max(3.f,distance)))*juce::Decibels::decibelsToGain(part[i][1]->load()));}
        head.bpm.store(ps.bpm);head.playing.store(active);head.samples.store((int64_t)elapsed);head.seconds.store((double)elapsed/rate);head.ppq.store(beatPosition);
        const auto events=active?pattern.advance(count,ps):tide::room::Events{};
        if(capture&&!capture->complete.load())for(int n=0;n<events.size;++n){const auto& e=events.data[(size_t)n];if(!e.on||part[(size_t)e.part][8]->load()<.5f)capture->note(e.offset,e.part,e.note,e.velocity,e.on);}
        // Two persistent workers and the device callback process independent
        // sources concurrently. All three finish before their outputs are mixed.
        for(auto& w:workers)if(w)w->dispatch(count,events);
        renderSource(0,count,events);
        for(int i=0;i<2;++i){if(workers[(size_t)i])workers[(size_t)i]->join();else renderSource(i+1,count,events);}
        for(int i=0;i<3;++i) {
            const auto p=(size_t)i;juce::AudioBuffer<float> b(work[p].getArrayOfWritePointers(),2,count);
            float measured=0;
            if(!active)tailActivity=std::max(tailActivity,b.getMagnitude(0,count));
            for(int n=0;n<count;++n){const float gain=gains[p].getNextValue();for(int c=0;c<2;++c){const float x=b.getSample(c,n)*gain;measured=std::max(measured,std::abs(x));output.addSample(c,offset+n,x);
#if TIDE_NATIVE_WOOD
                woodBus.addSample(c,n,woodSources[p].getSample(c,n)*gain);
#endif
            }}
            meters[p].store(std::max(measured,meters[p].load()*(float)std::exp(-count/(rate*.18))));
        }
        juce::AudioBuffer<float> mix(output.getArrayOfWritePointers(),2,offset,count);
#if TIDE_NATIVE_WOOD
        juce::AudioBuffer<float> tail(woodBus.getArrayOfWritePointers(),2,count);
        wood.process(tail,global[nativeDecay]->load(),global[nativeDamping]->load(),global[nativeTail]->load()+global[10]->load());
        if(!active)tailActivity=std::max(tailActivity,tail.getMagnitude(0,count));
        for(int c=0;c<2;++c)mix.addFrom(c,0,tail,c,0,count);
#endif
        if(capture&&!capture->complete.load())capture->tap(32,mix,count);
        tape.process(mix);
        if(capture&&!capture->complete.load())capture->tap(34,mix,count);
        // Studio and worn tape have intentional continuous hiss. Its upstream room tails
        // determine activity; the one-second drain covers its 768-sample delay
        // and 4 Hz / 14 Hz coupling states before sleep.
        if(!active&&(int)global[17]->load()!=2&&(int)global[17]->load()!=4)tailActivity=std::max(tailActivity,mix.getMagnitude(0,count));
        for(int n=0;n<count;++n){const float gain=master.getNextValue()*gate.getNextValue()*muteBus.getNextValue();for(int c=0;c<2;++c)output.setSample(c,offset+n,output.getSample(c,offset+n)*gain);}
        if(capture&&!capture->complete.load()){
            RoomCapture::Frame frame;frame.load=dspLoad.load();
            for(size_t i=0;i<3;++i)frame.engineTime[i]=engines[i].renderTimeForDiagnostics();
            const char* names[]={"GeomPan","Distance","Src Z (m)"};
            for(size_t i=0;i<3;++i)for(size_t a=0;a<3;++a){frame.target[i*3+a]=positions[i][a];frame.hosted[i*3+a]=room?room->value((int)i,names[a]):0.f;}
            capture->output(mix,frame);
        }
        if(active){elapsed+=(uint64_t)count;beatPosition+=count/rate*ps.bpm/60;}
    }
    bool voicesActive=false;for(const auto& engine:engines)voicesActive|=engine.activeVoices()>0;
    // kWoodRoom injects tiny denormal-prevention noise even at zero input.
    // This threshold applies only to stopped transport, never to playing audio.
#if TIDE_NATIVE_WOOD && !TIDE_CLEAR_ROOM
    constexpr float stoppedFloor=1.e-7f;
#else
    constexpr float stoppedFloor=1.e-8f;
#endif
    if(!active&&!voicesActive&&tailActivity<stoppedFloor)silentSamples+=(uint64_t)output.getNumSamples();else silentSamples=0;
    if(!active&&((int)global[17]->load()==2||(int)global[17]->load()==4
#if TIDE_NATIVE_WOOD
        ||true
#endif
        )&&silentSamples>=(uint64_t)(rate*.75))gate.setTargetValue(0);
    dormant=silentSamples>=(uint64_t)rate;
    peak.store(std::max(output.getMagnitude(0,output.getNumSamples()),peak.load()*.9f));
    if(recording)recording->push(output);
    if(output.getNumSamples()>0){const float load=(float)(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-started)*rate/output.getNumSamples());dspLoad.store(.95f*dspLoad.load()+.05f*load);peakDspLoad.store(std::max(peakDspLoad.load(),load));if(load>1)lateBlocks.fetch_add(1);}
}
void RoomProcessor::getStateInformation(juce::MemoryBlock& data){auto state=parameters.copyState();state.removeChild(state.getChildWithName("Reverside"),nullptr);const juce::ScopedLock lock(getCallbackLock());if(room)state.addChild(room->saveState(),-1,nullptr);else if(pendingRoom.isValid())state.addChild(pendingRoom.createCopy(),-1,nullptr);if(auto xml=state.createXml())copyXmlToBinary(*xml,data);}
void RoomProcessor::setStateInformation(const void* data,int size){if(auto xml=getXmlFromBinary(data,size))if(xml->hasTagName("TideRoomState")){auto tree=juce::ValueTree::fromXml(*xml);for(const char* id:{"placement","reflections"})if(!tree.getChildWithProperty("id",id).isValid()){juce::ValueTree value("PARAM");value.setProperty("id",id,nullptr);value.setProperty("value",0,nullptr);tree.addChild(value,-1,nullptr);}// Old scenes retain their untaped balance until Tape is enabled.
const std::pair<const char*,float> tapeDefaults[]={{"clearDecay",1.2f},{"clearDamping",.35f},{"clearTail",-12},{"woodTail",-6},{"woodRegen",.5f},{"woodWarmth",.25f},{"patternBank",0},{"harmonyKey",2},{"harmonyScale",1},{"harmonyChord",0},{"wornDips",1},{"wornDrive",14},{"wornAge",.72f},{"wornMotion",1.15f},{"wornDamage",.58f},{"wornNoise",.22f},{"wornTrim",2},{"wornMedium",0},{"eightMotionOn",0},{"eightWow",.1f},{"eightFlutter",.1f},{"eightCalibration",0},{"eightDrive",0},{"eightTrim",0},{"eightQuality",0},{"studioDrive",18},{"studioCream",1},{"studioBias",0},{"studioMotion",1},{"studioNoise",.15f},{"studioTrim",.5f},{"studioQuality",2},{"movingReflections",1},{"tapeOn",0},{"tapeDrive",6},{"tapeSaturation",.5f},{"tapeBias",.65f},{"tapeWarmth",.25f},{"tapeMix",1},{"tapeModel",0},{"spatialDrive",27.6f},{"spatialSoftness",24},{"spatialTrim",6.8f},{"tapeQuality",2}};for(const auto& item:tapeDefaults)if(!tree.getChildWithProperty("id",item.first).isValid()){juce::ValueTree value("PARAM");value.setProperty("id",item.first,nullptr);value.setProperty("value",item.second,nullptr);tree.addChild(value,-1,nullptr);}for(int i=0;i<tide::room::lfoCount;++i)for(const auto* field:tide::room::lfoFields){const auto id=lfoId(i,field);if(!tree.getChildWithProperty("id",id).isValid()){auto* parameter=parameters.getParameter(id);juce::ValueTree value("PARAM");value.setProperty("id",id,nullptr);value.setProperty("value",parameter->convertFrom0to1(parameter->getDefaultValue()),nullptr);tree.addChild(value,-1,nullptr);}}// Missing ocean controls must use defaults even when an old scene is loaded
// over an already patched session.
juce::StringArray oceanIds;for(const auto* id:oceanControlsIds)oceanIds.add(id);
for(int i=0;i<tide::room::oceanRoutes;++i)for(const auto* field:tide::room::oceanFields)oceanIds.add(oceanId(i,field));
for(const auto& c:tide::tides::controls)oceanIds.add(c.id);
for(const auto& c:tide::visual::controls)oceanIds.add(c.id);oceanIds.add("visualEnabled");
for(const auto& id:oceanIds)if(!tree.getChildWithProperty("id",id).isValid()){auto* parameter=parameters.getParameter(id);juce::ValueTree value("PARAM");value.setProperty("id",id,nullptr);value.setProperty("value",parameter->convertFrom0to1(parameter->getDefaultValue()),nullptr);tree.addChild(value,-1,nullptr);}
parameters.replaceState(tree);if(simpleInterface)useSimpleInterface();resetMotion.store(true);pendingRoom=tree.getChildWithName("Reverside").createCopy();playing.store(false);if(room){const juce::ScopedLock lock(getCallbackLock());room->restoreState(pendingRoom);room->configure(roomSettings());headphones.store(roomSettings().headphones);movingRoom.store(roomSettings().movingReflections);}}}
juce::AudioProcessorEditor* RoomProcessor::createEditor(){return new RoomEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new RoomProcessor();}
