#pragma once
#include "Processor.h"

class TideLook final : public juce::LookAndFeel_V4 {
public:
    TideLook();
    void drawRotarySlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
};
class TideEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit TideEditor(TideProcessor&);
    ~TideEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    TideProcessor& instrument;
    TideLook look;
    juce::MidiKeyboardComponent keyboard;
    juce::ComboBox preset, spaceMode,articulation;
    juce::TextButton panicButton{"Quiet"};
    std::array<juce::Slider,9> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,9> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment,articulationAttachment;
    juce::TooltipWindow tooltip{this,600};
#if TIDE_EFFECT_HOST
    juce::ComboBox effectPicker;
    juce::TextButton effectEditor{"Open editor"},refreshEffects{"Refresh"};
    juce::ToggleButton effectBypass{"Bypass"},internalSpace{"Tide space"};
    juce::Array<juce::File> effectFiles;
    void refreshEffectList();
#endif
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TideEditor)
};
