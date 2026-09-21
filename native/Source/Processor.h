#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine.h"
#ifndef TIDE_EFFECT_HOST
#define TIDE_EFFECT_HOST 0
#endif
#if TIDE_EFFECT_HOST
#include "EffectHost.h"
#endif

class TideProcessor final : public juce::AudioProcessor {
public:
    TideProcessor();
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override {return true;}
    const juce::String getName() const override {return "Tide";}
    bool acceptsMidi() const override {return true;}
    bool producesMidi() const override {return false;}
    double getTailLengthSeconds() const override {return 32;}
    int getNumPrograms() override {return (int)tide::patches.size();}
    int getCurrentProgram() override {return program.load();}
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int i) override {return tide::patches[(size_t)juce::jlimit(0,tide::patchCount-1,i)].name;}
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    tide::Settings readSettings() const;
    bool programModified() const;
    void panic() {panicRequested.store(true);}
    juce::MidiKeyboardState keyboardState;
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float> peak{0};
    std::atomic<int> sounding{0};
    tide::Engine engine;
#if TIDE_EFFECT_HOST
    EffectHost effects{*this};
#endif
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    std::atomic<int> program{0};
    std::atomic<bool> panicRequested{false};
    std::array<std::atomic<float>*,11> values{};
#if TIDE_EFFECT_HOST
    float finalGain=1,finalGainCoefficient=.001f;
#endif
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TideProcessor)
};
