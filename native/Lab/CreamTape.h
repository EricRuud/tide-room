// SPDX-License-Identifier: GPL-3.0-only
// Offline saturation voicings. Fixed settings are selected at prepare time.
// No added envelope compressor, limiter, automatic gain or dry blend.
#pragma once
#include "HybridTape.h"

namespace tide::lab {
struct CreamVoicing {
    const char* name;
    double driveDb, preDarkenDb, postPoleHz;
    float saturation;
};
inline constexpr std::array<CreamVoicing,3> creamVoicings{{
    {"smooth",30,3,5200,.5f},
    {"cream",30,5,3500,.5f},
    {"dense",33,7,2500,.62f}
}};

class CreamTape {
public:
    void prepare(double rate,int block,const CreamVoicing& v) {
        preLow.fill(0);post1.fill(0);post2.fill(0);
        prePole=std::exp(-2*juce::MathConstants<double>::pi*1800/rate);
        postPole=std::exp(-2*juce::MathConstants<double>::pi*v.postPoleHz/rate);
        highGain=std::pow(10.,-v.preDarkenDb/20.);
        extraGain=std::pow(10.,(v.driveDb-12)/20.);
        TapeSettings s;s.enabled=true;s.drive=12;s.saturation=v.saturation;s.soften=1;
        // Offline quality experiment: 32x left a small high-order folded tone
        // after aggressive treble shaping. Validate 64x before auditioning.
        tape.setSettings(s);tape.prepare(rate,block,6);
    }
    void process(juce::AudioBuffer<float>& b) {
        juce::ScopedNoDenormals noDenormals;
        for(int c=0;c<2;++c)for(int n=0;n<b.getNumSamples();++n){
            const double x=b.getSample(c,n);
            preLow[c]+=(1-prePole)*(x-preLow[c]);
            b.setSample(c,n,(float)(extraGain*(preLow[c]+highGain*(x-preLow[c]))));
        }
        tape.process(b);
        for(int c=0;c<2;++c)for(int n=0;n<b.getNumSamples();++n){
            const double x=b.getSample(c,n)/extraGain;
            // Two non-resonant one-poles: a broad, smooth treble roll-off.
            // The specified pole frequency is approximately -6 dB combined,
            // not a -3 dB Butterworth cutoff. Operates after tape decimation.
            post1[c]+=(1-postPole)*(x-post1[c]);
            post2[c]+=(1-postPole)*(post1[c]-post2[c]);
            b.setSample(c,n,(float)post2[c]);
        }
    }
    int latency() const {return tape.latency();}
    uint64_t guardCount() const {return tape.guardCount();}
    double maximumField() const {return tape.maximumField();}
private:
    HybridTape tape;
    std::array<double,2> preLow{},post1{},post2{};
    double prePole=0,postPole=0,highGain=1,extraGain=1;
};
}
