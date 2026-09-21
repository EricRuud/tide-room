#pragma once
#include "RoomProcessor.h"
#include "GlassLook.h"

// A small physical control surface over the retained motion and ocean engines.
// Existing individual oscillators, phases, masses and routes remain in state.
class TidesPanel final : public juce::Component {
public:
    explicit TidesPanel(RoomProcessor& p){
        constexpr const char* ids[]={"tideSpeed","tideTravel","tideInertia","tideBounce","oceanGravity","oceanRate","oceanDamping","tideSound"};
        constexpr const char* names[]={"Current speed","Travel","Inertia","Bounce","Gravity","Liquid response","Viscosity","Sound coupling"};
        constexpr const char* tips[]={"Speed of the currents moving the towers. Their relative rhythms stay intact.","Distance travelled around each tower's anchor. Zero lets the towers settle.","Resistance to changes in motion. More inertia produces a slower response to the currents.","Energy retained when the solid cores collide. Zero gives a soft contact.","How strongly the other towers pull each ocean.","How quickly the water responds to a changing gravitational pull.","Damping of the liquid. Higher values settle sloshing sooner.","Overall strength of the water and impact modulation of the sound."};
        for(size_t i=0;i<sliders.size();++i){auto& s=sliders[i];addAndMakeVisible(s);s.setName(names[i]);s.setTooltip(tips[i]);s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,74,25);
            attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,ids[i],s);
            auto* parameter=p.parameters.getParameter(ids[i]);s.setDoubleClickReturnValue(true,parameter->convertFrom0to1(parameter->getDefaultValue()));
            if(i==1||i==3||i==7){s.textFromValueFunction=[](double v){return juce::String(juce::roundToInt(v*100))+"%";};s.valueFromTextFunction=[](const juce::String& t){return t.getDoubleValue()/100;};}
            else{s.textFromValueFunction=[i](double v){return juce::String(std::abs(v)<.005?0.:v,2)+(i==5?" Hz":i==0||i==2?"x":"");};s.valueFromTextFunction=[](const juce::String& t){return t.getDoubleValue();};}s.updateText();
        }
        addAndMakeVisible(sound);sound.setName("Tides sound modulation");sound.setTooltip("Let water displacement and collision impacts modulate the instruments.");
        soundAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"oceanOn",sound);
        setSize(490,516);
    }
    void resized()override{for(size_t i=0;i<sliders.size();++i)sliders[i].setBounds(166,row(i),300,28);sound.setBounds(24,466,200,28);}
    void paint(juce::Graphics& g)override{
        g.fillAll(tide::glass::background);g.setColour(tide::glass::text);g.setFont(juce::FontOptions(18.f));g.drawText("TIDES",24,16,240,32,juce::Justification::centredLeft);
        for(size_t i=0;i<sliders.size();++i){g.setFont(juce::FontOptions(13.f));g.drawText(sliders[i].getName(),24,row(i),140,28,juce::Justification::centredLeft);}
        g.setColour(tide::glass::line);g.drawLine(24,238,466,238,.7f);g.drawLine(24,394,466,394,.7f);
    }
private:
    std::array<juce::Slider,8> sliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,8> attachments;
    juce::ToggleButton sound{"Modulate sound"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soundAttachment;
    juce::TooltipWindow tips{this,650};
    static int row(size_t i){return i<4?66+(int)i*40:i<7?260+((int)i-4)*40:414;}
};
