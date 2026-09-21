#pragma once
#include "TowerNotes.h"
#include "Engine.h"
#include "Pattern.h"
#include "ReversideRoom.h"
#include "Binaural.h"
#include "RoomTape.h"
#include "Voices.h"
#include "Motion.h"
#include "Ocean.h"
#include "PlanetMotion.h"
#include "TidesSettings.h"
#include "RoomCapture.h"
#include "OutputRecorder.h"
#include "MovingReflections.h"
#if TIDE_NATIVE_WOOD
#include "WoodRoom.h"
#if TIDE_CLEAR_ROOM
#include "ClearRoom.h"
#endif
#endif

class RoomProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    explicit RoomProcessor(bool autoload=true);
    ~RoomProcessor() override;
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void audioWorkgroupContextChanged(const juce::AudioWorkgroup&) override;
    bool isBusesLayoutSupported(const BusesLayout& l) const override {return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainInputChannelSet().isDisabled();}
    const juce::String getName() const override {return "Tide Room";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override {return 20;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Three in a room";}
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    bool loadRoom(const juce::File&);
    void updateRoom();
    void requestDiagnosticCapture(const juce::File& f){captureFolder=f;}
    bool startRecording(const juce::File&,juce::String& error);
    void stopRecording(bool deviceStopped=false);
    OutputRecorder::State recordingState() const;
    void openRoomEditor();
    void request821Audition(){audition821=true;}
    juce::String tape821Status() const {return tape.eightStatus((int)get("eightCalibration"));}
    void play();
    void stop();
    void quiet();
    void vary();
    void selectVoice(int part,int index);
    void selectWornPreset(int index);
    void useSimpleInterface();
    void set(const juce::String&,float);
    float get(const juce::String&) const;
    tide::room::RoomSettings roomSettings() const;
    bool isPlaying() const {return playing.load();}
    bool ready() const {return loaded.load();}
    juce::String status() const {return statusText;}
    tide::room::ReversideRoom* roomForTest() {return room.get();}
    bool sleepingForTest() const {return dormant;}
    uint64_t tapeGuards() const {return tape.guardCount();}
#if TIDE_NATIVE_WOOD
    uint64_t roomGuards() const {return wood.guardCount();}
#endif
    static juce::String partId(int i,const char* name) {return "p"+juce::String(i)+"_"+name;}
    static juce::String lfoId(int i,const char* name) {return "lfo"+juce::String(i)+"_"+name;}
    static juce::String oceanId(int i,const char* name) {return "ocean"+juce::String(i)+"_"+name;}
    void oceanAudition();
    void collisionAudition();
    tide::room::Position movingPosition(int i)const;
    std::array<std::atomic<float>,6> oceanSignals{};
    std::array<std::atomic<float>,3> impactSignals{},waterConnections{};
    std::array<std::array<std::atomic<float>,3>,3> waterKicks{};
    std::array<std::atomic<float>,tide::room::lfoCount> lfoSignals{};
    juce::AudioProcessorValueTreeState parameters;
    std::array<std::atomic<float>,3> meters{};
    std::array<std::atomic<float>,3> noteLights{};
    std::array<std::array<std::atomic<float>,128>,3> pitchLights{};
    std::array<std::array<std::atomic<float>,128>,3> pitchHeights{};
    float towerHeight()const{return get("roomHeight")*tide::room::towerHeightRatio;}
    float listenerHeight()const{return tide::room::listenerHeight(get("roomHeight"));}
    tide::room::TowerNotes towerNotes(int part)const;
    std::array<std::atomic<int>,3> lastStep{{-1,-1,-1}};
    std::atomic<float> peak{0};
    std::atomic<float> dspLoad{0},peakDspLoad{0};
    std::atomic<uint64_t> lateBlocks{0};
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    struct Head final : juce::AudioPlayHead {
        juce::Optional<PositionInfo> getPosition() const override;
        std::atomic<double> bpm{108},seconds{0},ppq{0};
        std::atomic<int64_t> samples{0};
        std::atomic<bool> playing{false};
    } head;
    std::unique_ptr<tide::room::ReversideRoom> room;
    juce::File captureFolder;
    std::unique_ptr<RoomCapture> capture;
    std::unique_ptr<OutputRecorder> recording;
    bool captureSaved=false;
    std::array<tide::Engine,3> engines;
    std::array<juce::AudioBuffer<float>,3> work;
    std::array<juce::AudioBuffer<float>,3> direct;
    std::array<juce::AudioBuffer<float>,3> early;
    std::array<tide::room::MovingReflections,3> reflections;
#if TIDE_NATIVE_WOOD
#if TIDE_CLEAR_ROOM
    tide::room::ClearRoom wood;
#else
    tide::room::WoodRoom wood;
#endif
    struct TowerAudio;
    std::array<std::unique_ptr<TowerAudio>,3> towerAudio;
    void renderTowerSource(int,int,const tide::room::Events&);
    std::array<juce::AudioBuffer<float>,3> woodSources;
    juce::AudioBuffer<float> woodBus;
    std::array<juce::SmoothedValue<float>,3> sendPan;
#endif
    tide::room::HrtfBank hrtf;
    tide::room::RoomTape tape;
    std::array<tide::room::BinauralSource,3> spatial;
    std::array<juce::SmoothedValue<float>,3> field;
    struct Worker;
    std::array<std::unique_ptr<Worker>,2> workers;
    juce::AudioWorkgroup audioGroup;
    tide::room::Pattern pattern;
    std::array<std::atomic<float>*,55> global{};
    std::array<std::array<std::atomic<float>*,9>,3> part{};
    std::array<std::array<std::atomic<float>*,8>,tide::room::lfoCount> lfo{};
    tide::room::Motion motion;
    tide::room::Ocean ocean;
    tide::room::PlanetMotion planetMotion;
    std::array<std::atomic<float>*,7> oceanControls{};
    std::array<std::atomic<float>*,tide::tides::controls.size()> tidesControls{};
    bool simpleInterface=false;
    std::array<std::array<std::atomic<float>*,3>,tide::room::oceanRoutes> oceanRoutes{};
    void updateOcean(int);
    tide::room::Motion::Values positions{};
    std::array<std::array<std::atomic<float>,3>,3> displayedPositions{};
    std::atomic<unsigned> positionVersion{0};
    std::atomic<bool> resetMotion{false};
    void updateMotion(int,bool);
    std::array<juce::SmoothedValue<float>,3> gains;
    juce::SmoothedValue<float> master,gate,muteBus;
    juce::ValueTree pendingRoom;
    juce::String statusText="Preparing Reverside...";
    std::atomic<bool> loaded{false},playing{false},restart{false},silence{false};
    std::atomic<bool> headphones{true};
    std::atomic<bool> movingRoom{true};
    std::atomic<bool> prepared{false};
    bool audition821=false;
    bool autoLoad=true,attempted=false,wasPlaying=false,dormant=false;
    uint64_t silentSamples=0;
    double rate=48000,beatPosition=0;
    uint64_t elapsed=0;
    static constexpr int chunkSize=512;
    void timerCallback() override;
    void renderSource(int,int,const tide::room::Events&);
    void updateTape();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoomProcessor)
};
