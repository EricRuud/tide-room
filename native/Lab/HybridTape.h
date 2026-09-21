// Experimental fork of the frozen Tide Room 0.3 tape stage. Not linked into the app.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../Room/TapeMagnet.h"
#include "Brightness.h"
#include <juce_dsp/juce_dsp.h>

namespace tide::lab {
using tide::room::TapeMagnet;
struct TapeSettings {bool enabled=false;float drive=6,saturation=.5f,bias=.65f,warmth=.25f,mix=1,soften=0;BrightnessSettings brightness{};};
class HybridTape {
public:
    void prepare(double rate,int maximumBlock,int order=5);
    void setSettings(TapeSettings);
    void process(juce::AudioBuffer<float>&);
    int latency() const {return latencySamples;}
    uint64_t guardCount() const {return magnets[0].guardCount()+magnets[1].guardCount();}
    double maximumField() const {return peakField;}
private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::array<TapeMagnet,2> magnets;
    std::array<double,2> preLow{},postInput{},postOutput{},dc{},toneLow{};
    std::array<std::array<float,4096>,2> dryDelay{};
    std::array<juce::SmoothedValue<float>,5> controls;
    juce::SmoothedValue<float> blend;
    juce::AudioBuffer<float> dry,wet;
    TapeSettings settings;
    double rate=48000,innerRate=384000,prePole=0,dcPole=0;
    int maximum=512,latencySamples=0,delayIndex=0;
    bool processing=false;
    Brightness brightness;
    juce::SmoothedValue<float> soften;
    double peakField=0;
};
}
