#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

// One stereo insert. UI/lifetime changes occur on the message thread;
// the owning processor's callback lock protects the short instance swap.
class EffectHost final : private juce::Timer {
public:
    explicit EffectHost(juce::AudioProcessor&);
    ~EffectHost() override;
    void prepare(double,int);
    void releaseResources();
    void process(juce::AudioBuffer<float>&);
    void quiet() {quietLatched.store(true);resetRequested.store(true);}
    void resumeOnNote() {quietLatched.store(false);}
    void loadFile(const juce::File&);
    void unload();
    void openEditor();
    void closeEditor();
    bool busy() const {return scanning||waitingToSwap;}
    bool loaded() const {return active!=nullptr;}
    juce::String path() const {return activeDescription.fileOrIdentifier;}
    juce::String name() const {return activeDescription.name;}
    juce::String status() const {return message;}
    bool bypassed() const {return bypass.load();}
    void setBypassed(bool b) {bypass.store(b);}
    bool usesInternalSpace() const {return internalSpace.load();}
    void setInternalSpace(bool b) {internalSpace.store(b);}
    int latency() const {return reportedLatency.load();}
    juce::ValueTree saveState();
    void restoreState(const juce::ValueTree&);
    static juce::Array<juce::File> installedBundles();
    static bool scanToFile(const juce::File& plugin,const juce::File& result);
    // Deterministic test entry points; no audio device or UI required.
    bool loadSynchronously(const juce::File&,juce::String& error);
    juce::AudioPluginInstance* instanceForTest() {return active.get();}
    void unloadSynchronously();
    void installForTest(std::unique_ptr<juce::AudioPluginInstance>);
private:
    struct PlayHead final : juce::AudioPlayHead {
        std::atomic<double> rate{48000};
        std::atomic<juce::int64> samples{0};
        juce::Optional<PositionInfo> getPosition() const override;
    } playHead;
    struct PluginWindow;
    juce::AudioProcessor& owner;
    juce::VST3PluginFormat format;
    std::unique_ptr<juce::AudioPluginInstance> active,staged;
    std::unique_ptr<PluginWindow> editor;
    juce::PluginDescription activeDescription,stagedDescription;
    juce::ChildProcess scanner;
    juce::File scanResult;
    juce::MemoryBlock restoreBytes;
    juce::String message="Choose an installed VST3 effect.";
    std::atomic<double> sampleRate{48000};
    std::atomic<int> maximumBlock{256},reportedLatency{0};
    std::atomic<bool> prepared{false},bypass{false},internalSpace{true};
    std::atomic<bool> mutedForSwap{false},silentForSwap{false},resetRequested{false},badAudio{false};
    std::atomic<bool> quietLatched{false};
    std::atomic<juce::uint64> callbacks{0};
    juce::uint64 callbacksAtSwap=0;
    double scanStarted=0,swapStarted=0;
    bool scanning=false,waitingToSwap=false,restoring=false;
    juce::AudioBuffer<float> dryDelay;
    int delayPosition=0;
    bool pendingQuietReset=false;
    static constexpr int delayCapacity=262144;
    juce::SmoothedValue<float> wetBlend,transition,quietGain;
    juce::MidiBuffer noMidi;
    std::unique_ptr<juce::AudioPluginInstance> create(const juce::PluginDescription&,juce::String& error);
    void stage(std::unique_ptr<juce::AudioPluginInstance>,const juce::PluginDescription&);
    void commit();
    void timerCallback() override;
    void fail(const juce::String&);
    void cancelPending();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectHost)
};
