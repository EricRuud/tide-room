#include "ClearRoom.h"
#include <array>
#include <cmath>
#include <vector>

namespace tide::room {
namespace {
constexpr size_t channels = 16, stages = 4;
using Frame = std::array<float, channels>;
struct Delay {
    std::vector<float> data;
    size_t index = 0;
    void prepare(int length) { data.assign((size_t)std::max(1, length), 0.f); index = 0; }
    float read() const { return data[index]; }
    void write(float value) { data[index] = value; if (++index == data.size()) index = 0; }
    float tick(float value) { const auto out = read(); write(value); return out; }
};
void hadamard(Frame& x) {
    for (size_t step = 1; step < channels; step *= 2)
        for (size_t start = 0; start < channels; start += 2 * step)
            for (size_t j = 0; j < step; ++j) {
                const auto a = x[start + j], b = x[start + j + step];
                x[start + j] = a + b; x[start + j + step] = a - b;
            }
    for (auto& v : x) v *= .25f; // orthonormal, no energy growth
}
bool prime(int n) { if (n < 2) return false; for (int j = 2; j * j <= n; ++j) if (n % j == 0) return false; return true; }
int nextPrime(int n) { while (!prime(n)) ++n; return n; }
uint32_t next(uint32_t& state) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
float sign(size_t i, size_t mask) {
    auto value = i & mask; unsigned parity = 0;
    while (value) { parity ^= 1; value &= value - 1; }
    return parity ? -.25f : .25f;
}
}

struct ClearRoom::Impl {
    std::array<std::array<Delay, channels>, stages> diffusion;
    std::array<std::array<size_t, channels>, stages> permutation{};
    std::array<Frame, stages> polarity{};
    std::array<Delay, channels> tank;
    Frame low{}, gain{}, highGain{}, injection{}, targetGain{}, targetHigh{}, targetInjection{};
    double rate = 48000;
    float pole = 0, follow = 0;
    float lastDecay = -1, lastDamping = -1;
    juce::SmoothedValue<float> level;
    uint64_t guards = 0;
    void coefficients(float decay, float damping, bool immediate) {
        decay = juce::jlimit(.25f, 4.f, decay);
        damping = juce::jlimit(0.f, 1.f, damping);
        if (std::abs(lastDecay - decay) < 1.e-7f && std::abs(lastDamping - damping) < 1.e-7f) return;
        lastDecay = decay; lastDamping = damping;
        // At zero damping the entire spectrum has the same nominal RT60.
        // Positive damping shortens treble decay inside each feedback path.
        const double hfRatio = std::pow(.22, damping);
        for (size_t i = 0; i < channels; ++i) {
            const double seconds = tank[i].data.size() / rate;
            targetGain[i] = (float)std::pow(.001, seconds / decay);
            targetHigh[i] = (float)std::pow(.001, seconds / (decay * hfRatio));
            // Approximate constant diffuse energy as the decay changes.
            targetInjection[i] = std::sqrt(1.f - targetGain[i] * targetGain[i]);
        }
        if (immediate) { gain = targetGain; highGain = targetHigh; injection = targetInjection; }
    }
};

ClearRoom::ClearRoom() = default;
ClearRoom::~ClearRoom() = default;
void ClearRoom::prepare(double sampleRate, float decay, float damping) {
    impl = std::make_unique<Impl>(); auto& s = *impl;
    s.rate = std::max(8000., sampleRate);
    uint32_t random = 0x13bc759d;
    // Short, feed-forward diffusion: dense onset without an allpass metallic tail.
    // These are fixed delays. No pitch modulation or sample-rate reduction.
    for (size_t stage = 0; stage < stages; ++stage) {
        const double spread = .0025 * (1u << stage);
        for (size_t i = 0; i < channels; ++i) {
            const double fraction = .15 + .7 * (next(random) / 4294967296.);
            s.diffusion[stage][i].prepare((int)std::round(s.rate * spread * (i + fraction) / channels));
            s.permutation[stage][i] = i;
            s.polarity[stage][i] = (next(random) & 1) ? -1.f : 1.f;
        }
        for (size_t i = channels - 1; i > 0; --i)
            std::swap(s.permutation[stage][i], s.permutation[stage][next(random) % (i + 1)]);
    }
    for (size_t i = 0; i < channels; ++i) {
        const double position = (i + .1 + .8 * (next(random) / 4294967296.)) / channels;
        s.tank[i].prepare(nextPrime((int)std::round(s.rate * .028 * std::pow(3., position))));
    }
    s.pole = (float)-std::expm1(-2 * juce::MathConstants<double>::pi * 3500 / s.rate);
    s.follow = (float)-std::expm1(-1 / (s.rate * .08));
    s.coefficients(decay, damping, true);
    s.level.reset(s.rate, .025); s.level.setCurrentAndTargetValue(0);
}

void ClearRoom::process(juce::AudioBuffer<float>& audio, float decay, float damping, float levelDb) {
    if (!impl) { audio.clear(); return; }
    auto& s = *impl; juce::ScopedNoDenormals noDenormals;
    s.coefficients(decay, damping, false);
    s.level.setTargetValue(juce::Decibels::decibelsToGain(levelDb));
    auto* left = audio.getWritePointer(0); auto* right = audio.getWritePointer(1);
    for (int n = 0; n < audio.getNumSamples(); ++n) {
        Frame input{}, delayed{};
        for (size_t i = 0; i < channels; ++i)
            input[i] = (i & 1 ? right[n] : left[n]) * .3535533905932738f;
        for (size_t stage = 0; stage < stages; ++stage) {
            Frame scattered{};
            for (size_t i = 0; i < channels; ++i)
                scattered[s.permutation[stage][i]] = s.diffusion[stage][i].tick(input[i]) * s.polarity[stage][i];
            hadamard(scattered); input = scattered;
        }
        float sum = 0, outL = 0, outR = 0;
        for (size_t i = 0; i < channels; ++i) {
            s.gain[i] += s.follow * (s.targetGain[i] - s.gain[i]);
            s.highGain[i] += s.follow * (s.targetHigh[i] - s.highGain[i]);
            s.injection[i] += s.follow * (s.targetInjection[i] - s.injection[i]);
            const float raw = s.tank[i].read();
            outL += raw * sign(i, 5); outR += raw * sign(i, 11);
            s.low[i] += s.pole * (raw - s.low[i]);
            delayed[i] = s.highGain[i] * raw + (s.gain[i] - s.highGain[i]) * s.low[i];
            sum += delayed[i];
        }
        // Householder reflection has unit norm; every damping path is contractive.
        for (size_t i = 0; i < channels; ++i) {
            float value = delayed[i] - sum * .125f + input[i] * s.injection[i];
            if (!std::isfinite(value)) { value = 0; s.low[i] = 0; ++s.guards; }
            s.tank[i].write(value);
        }
        const float output = s.level.getNextValue() * 2.82842712474619f;
        left[n] = outL * output; right[n] = outR * output;
    }
}
uint64_t ClearRoom::guardCount() const { return impl ? impl->guards : 0; }
}
