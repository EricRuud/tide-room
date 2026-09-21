#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace tide::room {
// A single shared, fully wet kWoodRoom. Source propagation and geometric early
// reflections live outside this network. No source coordinates resize its delays.
class WoodRoom {
public:
    WoodRoom();
    ~WoodRoom();
    void prepare(double sampleRate,float sustain,float warmth);
    void process(juce::AudioBuffer<float>&,float sustain,float warmth,float levelDb);
    uint64_t guardCount() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
