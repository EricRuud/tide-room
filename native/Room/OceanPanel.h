#pragma once
#include "RoomProcessor.h"
#include "TowerRenderer.h"

class OceanPanel final : public juce::Component,private juce::Timer {
public:
    explicit OceanPanel(RoomProcessor&,TowerRenderer* sharedTowers=nullptr);
    void paint(juce::Graphics&)override;
    void resized()override;
private:
    using SA=juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA=juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA=juce::AudioProcessorValueTreeState::ButtonAttachment;
    RoomProcessor& owner;
    std::unique_ptr<TowerRenderer> ownedTowers;
    TowerRenderer& towers;
    juce::TextButton power{"Patches off"},audition{"Try orbit patch"},collision{"Collision patch"};
    std::unique_ptr<BA> powerAttachment;
    std::array<juce::Slider,6> controls;
    std::array<std::unique_ptr<SA>,6> attachments;
    struct Row {juce::ComboBox source,target;juce::Slider amount;std::unique_ptr<CA> sourceAttachment,targetAttachment;std::unique_ptr<SA> amountAttachment;};
    std::array<Row,tide::room::oceanRoutes> rows;
    juce::TooltipWindow tips{this,500};
    void timerCallback()override;
};
