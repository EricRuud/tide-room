#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace tide::room {
// Fully wet, full-rate linear FDN. Geometry and moving early paths are external.
// Design reference: Geraint Luff, "Let's Write A Reverb" (Signalsmith, 2021).
// This is our implementation, without upstream code or fixed-character voicing.
class ClearRoom {
public:
    ClearRoom();
    ~ClearRoom();
    void prepare(double sampleRate, float decaySeconds, float damping);
    void process(juce::AudioBuffer<float>&, float decaySeconds, float damping, float levelDb);
    uint64_t guardCount() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
