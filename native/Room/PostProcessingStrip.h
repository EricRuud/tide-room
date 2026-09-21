#pragma once
#include "RoomProcessor.h"
#include "GlassLook.h"

class PostProcessingStrip final : public juce::Component {
public:
    explicit PostProcessingStrip(RoomProcessor& p):scene(p){
        setName("Worn tape post processing");addAndMakeVisible(power);powerAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"tapeOn",power);
        addAndMakeVisible(medium);medium.setName("Worn tape medium");medium.addItemList({"Cassette","Home video"},1);mediumAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"wornMedium",medium);
        addAndMakeVisible(presets);presets.setName("Worn tape presets");presets.setTextWhenNothingSelected("Tape presets...");presets.addItemList({"Old cassette","Unspooled","Home video"},1);
        presets.onChange=[this]{if(presets.getSelectedId()>0){scene.selectWornPreset(presets.getSelectedId()-1);presets.setSelectedId(0,juce::dontSendNotification);}};
        constexpr const char* ids[]={"wornDrive","wornAge","wornMotion","wornDamage","wornDips","wornNoise","wornTrim","tapeMix"};
        constexpr const char* names[]={"Drive","Age","Transport","Contact loss","Dip depth","Hiss / grain","Tape output","Mix"};
        for(size_t i=0;i<sliders.size();++i){auto& s=sliders[i];addAndMakeVisible(s);s.setName(names[i]);s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,60,24);
            attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,ids[i],s);
            s.textFromValueFunction=[i](double v){return i==0||i==6?juce::String(v,1)+" dB":juce::String(juce::roundToInt(v*100))+"%";};
            s.valueFromTextFunction=[i](const juce::String& s){return s.getDoubleValue()/(i==0||i==6?1.:100.);};s.updateText();}
        setSize(946,116);
    }
    void paint(juce::Graphics& g)override{g.setColour(tide::glass::text);g.setFont(juce::FontOptions(12.f));g.drawText("WORN TAPE",0,0,113,23,juce::Justification::centredLeft);
        g.setColour(tide::glass::muted);g.setFont(juce::FontOptions(11.f));for(size_t i=0;i<sliders.size();++i)g.drawText(sliders[i].getName(),216+(int)(i%4)*182,(int)(i/4)*58,172,18,juce::Justification::centredLeft);}
    void resized()override{power.setBounds(118,0,80,23);medium.setBounds(0,34,184,26);presets.setBounds(0,77,184,26);
        for(size_t i=0;i<sliders.size();++i)sliders[i].setBounds(216+(int)(i%4)*182,20+(int)(i/4)*58,172,25);}
private:
    RoomProcessor& scene;juce::ToggleButton power{"Enabled"};juce::ComboBox medium,presets;
    std::array<juce::Slider,8> sliders;std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,8> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mediumAttachment;
};
