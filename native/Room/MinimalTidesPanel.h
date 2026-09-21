#pragma once
#include "RoomProcessor.h"
#include "GlassLook.h"

class MinimalTidesPanel final : public juce::Component {
public:
    MinimalTidesPanel(RoomProcessor& p,std::function<void()> openAdvanced){
        setName("Tides controls");
        constexpr const char* ids[]={"tideSpeed","tideTravel","oceanGravity","tideSound"};
        constexpr const char* names[]={"Current speed","Travel","Gravity","Sound coupling"};
        constexpr const char* help[]={"Speed of the currents moving the towers.","How far the towers move about their anchors. Zero lets them settle.","How strongly the other towers pull each ocean.","Strength of water and collision modulation when Modulate sound is enabled."};
        for(size_t i=0;i<sliders.size();++i){auto& s=sliders[i];addAndMakeVisible(s);s.setName(names[i]);s.setTooltip(help[i]);s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,58,24);
            attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,ids[i],s);
            s.textFromValueFunction=[i](double v){return i==1||i==3?juce::String(juce::roundToInt(v*100))+"%":juce::String(v,2)+(i==0?"x":"");};
            s.valueFromTextFunction=[i](const juce::String& s){return s.getDoubleValue()/(i==1||i==3?100.:1.);};s.updateText();}
        addAndMakeVisible(sound);sound.setName("Tides sound modulation");soundAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"oceanOn",sound);
        addAndMakeVisible(advanced);advanced.setName("Advanced Tides controls");advanced.setTooltip("Full physics, individual LFOs, ocean routing, masses and tower positions.");advanced.onClick=std::move(openAdvanced);
        setSize(174,330);
    }
    void paint(juce::Graphics& g)override{g.setColour(tide::glass::text);g.setFont(juce::FontOptions(12.f));g.drawText("TIDES",0,0,getWidth(),22,juce::Justification::centredLeft);
        g.setColour(tide::glass::muted);g.setFont(juce::FontOptions(11.f));for(size_t i=0;i<sliders.size();++i)g.drawText(sliders[i].getName(),0,39+(int)i*54,getWidth(),18,juce::Justification::centredLeft);}
    void resized()override{for(size_t i=0;i<sliders.size();++i)sliders[i].setBounds(0,57+(int)i*54,getWidth(),24);sound.setBounds(0,267,getWidth(),25);advanced.setBounds(0,303,getWidth(),28);}
private:
    std::array<juce::Slider,4> sliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,4> attachments;
    juce::ToggleButton sound{"Modulate sound"};juce::TextButton advanced{"Advanced..."};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soundAttachment;
};
