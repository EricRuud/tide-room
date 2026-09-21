#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <array>
#include "TapeMotion.h"
#include "TapeReadout.h"

namespace tide::room {
struct EightTwentyOneSettings {float drive=0,trim=0;int quality=0,calibration=0;float wow=.1f,flutter=.1f;bool motion=false;};

// Two independently measured 48 kHz calibrations, with host controls outside DSP.
// Construction/file access happens in prepare; mode changes only reset state.
class EightTwentyOneTape {
public:
    EightTwentyOneTape();
    ~EightTwentyOneTape();
    void setSettings(EightTwentyOneSettings s) noexcept {settings=s;}
    void prepare(double rate,int maximumBlock,const juce::File& resources={});
    void reset(int quality) noexcept;
    void process(juce::AudioBuffer<float>&) noexcept;
    bool ready(int calibration=-1) const noexcept {return available[(size_t)juce::jlimit(0,1,calibration<0?settings.calibration:calibration)];}
    const juce::String& status(int calibration=0) const noexcept {return messages[(size_t)juce::jlimit(0,1,calibration)];}
    uint64_t guardCount() const noexcept {return guards;}
    static constexpr int latencySamples=768;
private:
    struct Impl;
    std::array<std::unique_ptr<Impl>,2> models;
    Impl* impl=nullptr;
    EightTwentyOneSettings settings;
    juce::SmoothedValue<float> drive,trim;
    TapeMotion motion;
    TapeReadout readout;
    juce::AudioBuffer<float> work;
    std::array<std::array<float,latencySamples+1>,2> alignment{};
    static constexpr int gainLength=2048;
    std::array<std::array<float,gainLength>,2> gainDelay{};
    int position=0,gainPosition=0,extra=512,maximum=512,mode=0,activeCalibration=0;
    std::array<bool,2> available{};
    uint64_t guards=0;
    std::array<juce::String,2> messages{{"821 model has not been prepared.","821 model has not been prepared."}};
};
}
