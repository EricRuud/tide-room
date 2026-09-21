#pragma once
#include "TidesPanel.h"
#include "MotionPanel.h"
#include "OceanPanel.h"

class PlanetPositionsPanel final : public juce::Component {
public:
    explicit PlanetPositionsPanel(RoomProcessor& p){
        constexpr const char* fields[]={"x","y"};constexpr const char* axes[]={"Left / right","Depth"};
        for(size_t i=0;i<sliders.size();++i){auto& s=sliders[i];addAndMakeVisible(s);s.setName("Tower "+juce::String((int)i/2+1)+" "+axes[i%2]);s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,68,24);
            attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,RoomProcessor::partId((int)i/2,fields[i%2]),s);
            s.textFromValueFunction=[](double v){return juce::String(v,2);};s.updateText();}setSize(904,360);
    }
    void paint(juce::Graphics& g)override{g.fillAll(tide::glass::background);g.setColour(tide::glass::text);g.setFont(juce::FontOptions(18.f));g.drawText("TOWER ANCHORS",24,16,500,30,juce::Justification::centredLeft);
        constexpr const char* names[]={"WOOD","PLUCK","METAL"};constexpr const char* axes[]={"Left / right","Depth"};
        for(int i=0;i<3;++i){g.setColour(tide::glass::text);g.setFont(juce::FontOptions(13.f));g.drawText(names[i],24+i*300,70,265,25,juce::Justification::centredLeft);g.setColour(tide::glass::muted);g.setFont(juce::FontOptions(11.f));for(int a=0;a<2;++a)g.drawText(axes[a],24+i*300,111+a*62,265,20,juce::Justification::centredLeft);}}
    void resized()override{for(size_t i=0;i<sliders.size();++i)sliders[i].setBounds(24+(int)(i/2)*300,134+(int)(i%2)*62,265,25);}
private:
    std::array<juce::Slider,6> sliders;std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,6> attachments;
};

class AdvancedTidesPanel final : public juce::Component {
public:
    explicit AdvancedTidesPanel(RoomProcessor& p):tabs(juce::TabbedButtonBar::TabsAtTop){
        setName("Advanced Tides");tabs.setName("Tides sections");addAndMakeVisible(tabs);tabs.setTabBarDepth(34);tabs.setOutline(0);tabs.setColour(juce::TabbedComponent::backgroundColourId,tide::glass::background);
        add("Physics",new TidesPanel(p));add("Motion / LFOs",new MotionPanel(p));add("Oceans / routing",new OceanPanel(p));add("Positions",new PlanetPositionsPanel(p));setSize(944,788);
    }
    void paint(juce::Graphics& g)override{g.fillAll(tide::glass::background);}
    void resized()override{tabs.setBounds(getLocalBounds().reduced(12));}
private:
    struct Page final : juce::Viewport {
        explicit Page(juce::Component* content){setViewedComponent(content,true);setScrollBarsShown(true,true);}
        void paint(juce::Graphics& g)override{g.fillAll(tide::glass::background);}
    };
    juce::TabbedComponent tabs;
    void add(const juce::String& title,juce::Component* content){tabs.addTab(title,tide::glass::background,new Page(content),true);}
};
