// Frozen Tide Room 0.2 engine for regression comparisons. Only namespace/include names changed.
#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace tide02 {
struct Settings {
    float timbre=.5f, drive=0, motion=.7f, attack=.006f, decay=.30f;
    float sustain=0, release=.35f, space=.25f, output=-3;
    int spaceMode=0;
    int articulation=0;
};
struct Patch { const char* name; const char* description; Settings values; };
inline constexpr int patchCount=14;
extern const std::array<Patch, patchCount> patches;

class Engine {
public:
    static constexpr int voicesCount=8, oversampling=16, latency=192;
    void prepare(double sampleRate, int maxBlock);
    void reset();
    void panic() noexcept;
    void setSettings(Settings value);
    void midi(const juce::MidiMessage&);
    void render(float* left, float* right, int count);
    int activeVoices() const noexcept;
    static float fold(float phase, float drive) noexcept;
    static float saturate(float sample, float amount) noexcept;
    float oscillatorProbe(double frequency, float drive, float saturation);
    float metalProbe(double frequency,float index,float folding,float saturation);
    static float metal(float phase,float modPhase,float index,float folding) noexcept;
    int getCloseIRSize() const { return close.getCurrentIRSize(); }
    int getBloomIRSize() const { return bloom.getCurrentIRSize(); }
private:
    struct Voice {
        bool active=false, held=false, pedalHeld=false, releasing=false, fastRelease=false;
        bool struck=false;
        int articulation=0;
        int note=0, channel=1;
        uint64_t serial=0;
        double phase=0, age=0, releaseAge=0, frequency=0, phaseIncrement=0;
        float velocity=0, env=0, releaseStart=0, driftPhase=0;
        float envStep=0, lifeSine=0, lifeStep=0;
        float stealTail=0, last=0;
        double modPhase=0;
        float lightFast=0,lightSlow=0,filterG=0,filterStep=0,low1=0,low2=0;
        float metalIndex=0,metalStep=0;
    };
    struct Smooth {
        float value=0, target=0, coefficient=.001f;
        float next() { value += coefficient * (target-value); return value; }
        void init(float x, double sampleRate, double seconds) { value=target=x; coefficient=(float)(-std::expm1(-1/(sampleRate*seconds))); }
    };
    std::array<Voice, voicesCount> voices;
    std::array<bool,16> pedals{};
    std::array<Smooth,16> bend;
    Smooth timbre, drive, motion, attack, decay, sustain, release, wet, mode, output, mod;
    Settings settings;
    double rate=48000, innerRate=384000, clock=0, probePhase=0;
    double probeModPhase=0;
    uint64_t serial=0;
    int maximum=512, ringPosition=0, controlPosition=0;
    int panicRemaining=0, panicLength=1;
    std::array<float,2*(384*oversampling+1)> ring{};
    float dc=0;
    float dcAlpha=0, stealDecay=0;
    juce::AudioBuffer<float> workClose, workBloom;
    juce::dsp::Convolution close{juce::dsp::Convolution::NonUniform{512}};
    juce::dsp::Convolution bloom{juce::dsp::Convolution::NonUniform{512}};
    void beginRelease(Voice& v);
    void push(float x);
    float down() const;
    float nextInner();
};
}
