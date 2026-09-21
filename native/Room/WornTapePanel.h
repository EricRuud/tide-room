#pragma once
#include "RoomProcessor.h"
#include "GlassLook.h"

class WornTapePanel final : public juce::Component {
public:
    explicit WornTapePanel(RoomProcessor& p):owner(p){
        addAndMakeVisible(power);powerAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"tapeOn",power);
        addAndMakeVisible(medium);medium.setName("Worn tape medium");medium.addItemList({"Cassette","Home video"},1);
        mediumAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"wornMedium",medium);
        constexpr const char* ids[]={"wornDrive","wornAge","wornMotion","wornDamage","wornDips","wornNoise","wornTrim","tapeMix"};
        constexpr const char* names[]={"Drive","Age","Transport","Contact loss","Dip depth","Hiss / grain","Output","Mix"};
        for(size_t i=0;i<sliders.size();++i){auto& s=sliders[i];addAndMakeVisible(s);s.setName(names[i]);s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,74,25);
            attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,ids[i],s);
            if(i==0||i==6){s.setNumDecimalPlacesToDisplay(1);s.setTextValueSuffix(" dB");}
            else{s.textFromValueFunction=[](double v){return juce::String(juce::roundToInt(v*100))+"%";};s.valueFromTextFunction=[](const juce::String& t){return t.getDoubleValue()/100;};}s.updateText();}
        for(size_t i=0;i<presets.size();++i){addAndMakeVisible(presets[i]);presets[i].onClick=[this,i]{owner.selectWornPreset((int)i);};}
        setSize(490,484);
    }
    void resized()override{power.setBounds(362,22,102,25);medium.setBounds(162,65,302,28);for(size_t i=0;i<sliders.size();++i)sliders[i].setBounds(162,113+(int)i*39,302,28);for(size_t i=0;i<presets.size();++i)presets[i].setBounds(24+(int)i*151,439,139,27);}
    void paint(juce::Graphics& g)override{g.fillAll(tide::glass::background);g.setColour(tide::glass::text);g.setFont(juce::FontOptions(18.f));g.drawText("WORN TAPE",24,16,280,32,juce::Justification::centredLeft);g.setFont(juce::FontOptions(13.f));g.drawText("Medium",24,65,126,28,juce::Justification::centredLeft);for(size_t i=0;i<sliders.size();++i)g.drawText(sliders[i].getName(),24,113+(int)i*39,126,28,juce::Justification::centredLeft);}
private:
    RoomProcessor& owner;
    juce::ToggleButton power{"Enabled"};juce::ComboBox medium;
    std::array<juce::Slider,8> sliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,8> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mediumAttachment;
    std::array<juce::TextButton,3> presets{{juce::TextButton{"Old cassette"},juce::TextButton{"Unspooled"},juce::TextButton{"Home video"}}};
};
