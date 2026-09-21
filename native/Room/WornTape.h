#pragma once
#include <juce_dsp/juce_dsp.h>
#include "TapeReadout.h"

namespace tide::room {
// A creative worn-medium model, not a calibrated cassette or VHS machine.
struct WornSettings {
    float drive=14,age=.72f,motion=1.15f,damage=.58f,noise=.22f,trim=2;
    int medium=0; // cassette / linear video-tape inspired
    float dips=1; // 0 removes level ducking; 1 preserves the original depth
};
inline WornSettings wornPreset(int index){
    if(index==1)return {19,.90f,2.1f,.90f,.26f,3,0};
    if(index==2)return {12,.64f,1.35f,.76f,.30f,2,1};
    return {};
}

class WornTape {
public:
    static constexpr int latencySamples=768;
    void prepare(double sampleRate,int maximumBlock);
    void reset();
    void setSettings(WornSettings);
    void process(juce::AudioBuffer<float>&);
    uint64_t guardCount() const noexcept {return guards;}
    void setReferenceOrder(int order){referenceOrder=order;}
    double maximumDelayStep() const noexcept {return largestDelayStep;}
private:
    struct SmoothRandom {
        double a=0,b=0,position=0,length=1,lo=.1,hi=1;uint32_t seed=1;
        void reset(uint32_t s,double low,double high,double rate);
        double next(double rate);
    };
    struct Scar {
        int position=0,attack=1,hold=0,release=1,wait=1;double level=0,pan=0;uint32_t seed=1;
        void reset(double rate);
        double next(double rate,double medium);
    };
    struct Channel {
        double pre=0,postX=0,postY=0,dc=0,depth=0,last[2]{},lastRoot[2]{1,1};
        double low1=0,low2=0,bass=0,hissLP=0,hissHP=0,grain=0,ghost=0;
        uint32_t random=1;
    };
    static double random(uint32_t&) noexcept;
    static double curve(double) noexcept;
    static double lowpass(double x,double& state,double coefficient) noexcept;
    double cutoff(double frequency) const noexcept;
    WornSettings settings;
    std::array<Channel,2> channels{};
    std::array<juce::SmoothedValue<double>,7> controls;
    juce::SmoothedValue<double> dipDepth;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> modulation;
    TapeReadout readout;
    std::array<std::array<float,4096>,2> history{};
    std::vector<std::array<float,2>> ghostHistory;
    std::array<double,2049> cutoffTable{};
    SmoothRandom slow,fast,edge;Scar scar;
    double rate=48000,innerRate=192000,pad=0,prePole=0,dcPole=0,depthPole=0;
    double phase1=0,phase2=0,phase3=0,previousDelay=0,largestDelayStep=0;
    int maximum=512,write=0,ghostWrite=0,referenceOrder=0;
    uint64_t guards=0;
};
}
