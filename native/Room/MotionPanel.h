#pragma once
#include "RoomProcessor.h"
class MotionPanel final : public juce::Component,private juce::Timer {
public:
    explicit MotionPanel(RoomProcessor&);
    void paint(juce::Graphics&)override;
    void resized()override;
private:
    RoomProcessor& owner;
    using SA=juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA=juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA=juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    struct Row {
        juce::TextButton on,sync{"Tempo sync"};
        juce::ComboBox target,shape,division;
        juce::Slider rate,depth,phase;
        std::unique_ptr<BA> onAttachment,syncAttachment;
        std::unique_ptr<CA> shapeAttachment,divisionAttachment;
        std::unique_ptr<juce::ParameterAttachment> targetAttachment;
        std::unique_ptr<SA> rateAttachment,depthAttachment,phaseAttachment;
    };
    std::array<Row,tide::room::lfoCount> rows;
    juce::ComboBox roomMode;
    std::unique_ptr<CA> roomModeAttachment;
    juce::TooltipWindow tips{this,500};
    void timerCallback()override;
};
