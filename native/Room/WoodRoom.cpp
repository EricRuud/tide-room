#include "WoodRoom.h"
#include <array>
#include "../third_party/airwindows/kwoodroom/kWoodRoom.h"

namespace tide::room {
struct WoodRoom::Impl {
    kWoodRoom core{nullptr};
    juce::SmoothedValue<float> sustain,level;
    double rate=48000,pole=1,dcPole=0;
    double low[2]{},low2[2]{},dc[2]{};
    uint64_t guards=0;
    // Double DSP avoids adding another floating-point dither stage to the bus.
    std::array<std::array<double,32>,2> block{};
};
WoodRoom::WoodRoom()=default;
WoodRoom::~WoodRoom()=default;
void WoodRoom::prepare(double rate,float sustain,float warmth) {
    impl=std::make_unique<Impl>();auto& s=*impl;s.rate=rate;s.core.setSampleRate(rate);
    s.core.setParameter(kParamA,juce::jlimit(0.f,1.f,sustain));
    s.core.setParameter(kParamB,.5f); // Upstream full-resolution centre; fixed.
    s.core.setParameter(kParamC,.25f); // Original wood-room voicing; fixed.
    s.core.setParameter(kParamD,0); // Our geometry supplies the audible early field.
    s.core.setParameter(kParamE,.75f); // Keep the upstream input diffuser unchanged.
    s.core.setParameter(kParamF,1); // Shared send, wet only.
    s.sustain.reset(rate,.08);s.sustain.setCurrentAndTargetValue(juce::jlimit(0.f,1.f,sustain));
    s.level.reset(rate,.025);s.level.setCurrentAndTargetValue(0);
    s.pole=-std::expm1(-2*juce::MathConstants<double>::pi*std::min(rate*.4,16000*std::pow(.125,juce::jlimit(0.f,1.f,warmth)))/rate);
    s.dcPole=-std::expm1(-2*juce::MathConstants<double>::pi*12/rate);
}
void WoodRoom::process(juce::AudioBuffer<float>& audio,float sustain,float warmth,float levelDb) {
    if(!impl){audio.clear();return;}
    auto& s=*impl;juce::ScopedNoDenormals denormals;
    s.sustain.setTargetValue(juce::jlimit(0.f,1.f,sustain));
    s.level.setTargetValue(juce::Decibels::decibelsToGain(levelDb));
    const double target=-std::expm1(-2*juce::MathConstants<double>::pi*std::min(s.rate*.4,16000*std::pow(.125,juce::jlimit(0.f,1.f,warmth)))/s.rate);
    const double follow=-std::expm1(-1/(s.rate*.035));
    for(int offset=0;offset<audio.getNumSamples();offset+=32){
        const int count=std::min(32,audio.getNumSamples()-offset);
        s.core.setParameter(kParamA,s.sustain.skip(count));
        for(int c=0;c<2;++c)for(int n=0;n<count;++n)s.block[(size_t)c][(size_t)n]=audio.getSample(c,offset+n);
        double* data[]={s.block[0].data(),s.block[1].data()};s.core.processDoubleReplacing(data,data,count);
        for(int n=0;n<count;++n){s.pole+=follow*(target-s.pole);const float gain=s.level.getNextValue();
            for(int c=0;c<2;++c){double value=s.block[(size_t)c][(size_t)n];
                if(!std::isfinite(value)||std::abs(value)>32){value=0;++s.guards;}
                s.dc[c]+=s.dcPole*(value-s.dc[c]);value-=s.dc[c];
                s.low[c]+=s.pole*(value-s.low[c]);s.low2[c]+=s.pole*(s.low[c]-s.low2[c]);
                audio.setSample(c,offset+n,(float)s.low2[c]*gain);
            }
        }
    }
}
uint64_t WoodRoom::guardCount() const {return impl?impl->guards:0;}
}
