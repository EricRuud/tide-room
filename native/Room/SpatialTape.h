#pragma once
#include "SpatialSolver.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>

namespace tide::room {
struct SpatialSettings {bool enabled=true;float drive=27.6f,softness=24,trim=6.8f,mix=1;int quality=2;};
class SpatialTape {
public:
    SpatialTape();
    ~SpatialTape();
    void prepare(double rate);
    void release();
    void setSettings(SpatialSettings);
    void process(juce::AudioBuffer<float>&);
    void suspend(){blend.setCurrentAndTargetValue(0);qualityBlend.setCurrentAndTargetValue(0);}
    void workgroupChanged(const juce::AudioWorkgroup& g){group=g;}
    static constexpr int frameSize=1024,hop=512,latencySamples=768;
    int latency() const {return latencySamples;}
    uint64_t guardCount() const {return guards;}
    uint64_t limitedFrames() const {return limited;}
    double maximumResidual() const {return maxResidual;}
    void setParallelForTest(bool value){parallel=value;}
private:
    struct Worker;
    struct Channel {
        std::array<SpatialSolver,3> solvers;
        std::array<double,frameSize> history{},input{},result{};
        std::array<float,hop> queued{};
        SpatialSolver::Result info;
    };
    std::array<Channel,2> channels;
    std::array<double,frameSize> window{},driveHistory{};
    std::array<std::array<float,latencySamples+1>,2> dry{};
    std::unique_ptr<Worker> worker;
    juce::AudioWorkgroup group;
    juce::SmoothedValue<double> drive,softness,trim,blend,qualityBlend;
    SpatialSettings settings;
    int write=0,phase=0,dryWrite=0,currentQuality=2;
    double rate=48000,frameSoftness=24,maxResidual=0;
    uint64_t guards=0,limited=0;
    bool prepared=false,parallel=true;
    void renderFrame();
    void renderChannel(int);
};
}
