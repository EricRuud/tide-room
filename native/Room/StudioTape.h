#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace tide::room {
// Grey-box, SM911/15 ips recording model. See STUDIO-80.md for calibration and
// limits. This is not a component-exact A80 or an explicit ultrasonic bias model.
struct StudioSettings {
    float drive=18,cream=1,bias=0,motion=1,noise=.15f,trim=.5f;
    int quality=1; // 4/8/16x
};
class StudioTape {
public:
    static constexpr int latencySamples=768;
    void prepare(double,int);
    void reset();
    void setSettings(StudioSettings);
    void process(juce::AudioBuffer<float>&);
    uint64_t guardCount() const {return guards;}
    int quality() const {return activeQuality;}
    // Offline convergence only; production quality is bounded to 4/8/16x.
    void setReferenceOrder(int order){referenceOrder=order;}
private:
    struct Biquad {double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;double tick(double x){const auto y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;}void clear(){z1=z2=0;}};
    struct Channel {double pre=0,postX=0,postY=0,dc=0,noiseLP=0,grain=0;std::array<double,3> depth{},lastField{},lastIntegral{},olderField{},lastDivided{};Biquad bump,highpass,air;uint32_t random=1;};
    static double random(uint32_t&);
    double firstIntegral(double) const;
    static double remanence(double);
    double tailC1=0,tailC2=0;
    double secondIntegral(double) const;
    double divided(double,double,double,double) const;
    std::array<std::array<double,3>,8193> integrals{};
    void coefficients();
    float read(int,double) const;
    double rate=48000,innerRate=384000,pole=0,dcPole=0,noisePole=0,grainPole=0;
    double preGain=1,postB0=1,postB1=0,recordScale=1.41777209;
    std::array<double,3> dwell{};
    std::array<Channel,2> channels{};
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>,3> oversamplers;
    juce::dsp::Oversampling<float>* oversampler=nullptr;
    std::array<juce::SmoothedValue<double>,6> controls;
    std::array<std::array<float,2048>,2> history{};
    std::array<std::array<float,96>,1025> sinc{};
    StudioSettings settings;
    int activeQuality=1,maximum=512,write=0,referenceOrder=0;
    double pad=0;
    juce::AudioBuffer<float> modulation;
    uint32_t transportRandom=0x3416743;
    double phase1=0,phase2=.7,phase3=1.3,wander=0,wander2=0;
    uint64_t guards=0;
};
}
