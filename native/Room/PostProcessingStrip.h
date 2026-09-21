#pragma once
#include "RoomProcessor.h"
#include "GlassLook.h"
#include "TapeModelSelector.h"

class PostProcessingStrip final : public juce::Component, private juce::Timer {
public:
    explicit PostProcessingStrip(RoomProcessor& p):scene(p),model(p){
        setName("Tape post processing");addAndMakeVisible(model);addAndMakeVisible(power);powerAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"tapeOn",power);
        addAndMakeVisible(medium);medium.setName("Worn tape medium");medium.addItemList({"Cassette","Home video"},1);mediumAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"wornMedium",medium);
        addAndMakeVisible(presets);presets.setName("Worn tape presets");presets.setTextWhenNothingSelected("Tape presets...");presets.addItemList({"Old cassette","Unspooled","Home video"},1);
        presets.onChange=[this]{if(presets.getSelectedId()>0){scene.selectWornPreset(presets.getSelectedId()-1);presets.setSelectedId(0,juce::dontSendNotification);}};
        constexpr const char* ids[]={"wornDrive","wornAge","wornMotion","wornDamage","wornDips","wornNoise","wornTrim","tapeMix"};
        constexpr const char* names[]={"Drive","Age","Transport","Contact loss","Dip depth","Hiss / grain","Tape output","Mix"};
        for(size_t i=0;i<sliders.size();++i){auto& s=sliders[i];addAndMakeVisible(s);s.setName(names[i]);s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,60,24);
            attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,ids[i],s);
            s.textFromValueFunction=[i](double v){return i==0||i==6?juce::String(v,1)+" dB":juce::String(juce::roundToInt(v*100))+"%";};
            s.valueFromTextFunction=[i](const juce::String& s){return s.getDoubleValue()/(i==0||i==6?1.:100.);};s.updateText();}
        addChildComponent(calibration);calibration.setName("821 formula and speed");calibration.addItemList({"456 / 15 ips","900 / 30 ips"},1);
        calibration.setTooltip("Measured 821 calibrations. Switching briefly fades through dry.");calibrationAttachment=std::make_unique<CA>(p.parameters,"eightCalibration",calibration);
        addChildComponent(quality);quality.setName("821 limiter quality");quality.addItemList({"Native","4x limiter","8x limiter"},1);
        quality.setTooltip("Oversampling for the final limiter. Native is the calibrated path; latency stays fixed.");qualityAttachment=std::make_unique<CA>(p.parameters,"eightQuality",quality);
        addChildComponent(motion);motion.setName("821 transport motion");motion.setTooltip("Shared stereo transport. Off returns to steady playback; Wow and Flutter keep their settings.");motionAttachment=std::make_unique<BA>(p.parameters,"eightMotionOn",motion);
        constexpr const char* eightIds[]={"eightDrive","eightWow","eightFlutter","eightTrim"};
        constexpr const char* eightNames[]={"821 drive","821 wow","821 flutter","821 output"};
        constexpr const char* help[]={"Drive into the 821 model with matching output compensation. Start at 0 dB.","Depth of the shared transport. Above 100% extends the measured range.","Rate and depth of the shared transport. Above 100% extends the measured range.","Output gain after the 821 model."};
        for(size_t i=0;i<eightSliders.size();++i){auto& s=eightSliders[i];addChildComponent(s);s.setName(eightNames[i]);s.setTooltip(help[i]);s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,60,24);
            eightAttachments[i]=std::make_unique<SA>(p.parameters,eightIds[i],s);
            s.textFromValueFunction=[i](double v){return i==0||i==3?juce::String(v,1)+" dB":juce::String(juce::roundToInt(v*100))+"%";};
            s.valueFromTextFunction=[i](const juce::String& text){return text.getDoubleValue()/(i==0||i==3?1.:100.);};s.updateText();}
        addChildComponent(status);status.setName("821 status");status.setFont(juce::FontOptions(11.f));status.setColour(juce::Label::textColourId,tide::glass::muted);status.setBorderSize({});
        model.onModelChange=[this](int mode){showModel(mode);};showModel((int)p.get("tapeModel"));startTimerHz(4);
        setSize(946,116);
    }
    void paint(juce::Graphics& g)override{
        g.setColour(tide::glass::muted);g.setFont(juce::FontOptions(11.f));
        if(is821){constexpr const char* names[]={"Drive","Wow","Flutter","Tape output","Mix","Transport"};for(int i=0;i<6;++i)g.drawText(names[i],216+(i%4)*182,(i/4)*58,172,18,juce::Justification::centredLeft);}
        else for(size_t i=0;i<sliders.size();++i)g.drawText(sliders[i].getName(),216+(int)(i%4)*182,(int)(i/4)*58,172,18,juce::Justification::centredLeft);}
    void resized()override{model.setBounds(0,0,112,26);power.setBounds(118,0,80,23);medium.setBounds(0,34,184,26);presets.setBounds(0,77,184,26);calibration.setBounds(medium.getBounds());quality.setBounds(presets.getBounds());
        for(size_t i=0;i<sliders.size();++i)sliders[i].setBounds(216+(int)(i%4)*182,20+(int)(i/4)*58,172,25);
        for(size_t i=0;i<eightSliders.size();++i)eightSliders[i].setBounds(216+(int)i*182,20,172,25);
        if(is821)sliders[7].setBounds(216,78,172,25);motion.setBounds(398,78,172,25);status.setBounds(580,58,354,47);}
private:
    using SA=juce::AudioProcessorValueTreeState::SliderAttachment;using CA=juce::AudioProcessorValueTreeState::ComboBoxAttachment;using BA=juce::AudioProcessorValueTreeState::ButtonAttachment;
    void showModel(int mode){is821=mode==3;medium.setVisible(!is821);presets.setVisible(!is821);calibration.setVisible(is821);quality.setVisible(is821);motion.setVisible(is821);status.setVisible(is821);
        for(size_t i=0;i<sliders.size();++i)sliders[i].setVisible(!is821||i==7);for(auto& s:eightSliders)s.setVisible(is821);resized();timerCallback();repaint();}
    void timerCallback()override{if(is821){const auto message=scene.tape821Status();status.setText(message,juce::dontSendNotification);status.setTooltip(message);}}
    RoomProcessor& scene;TapeModelSelector model;bool is821=false;juce::ToggleButton power{"Enabled"},motion{"Motion on"};juce::ComboBox medium,presets,calibration,quality;juce::Label status;
    std::array<juce::Slider,4> eightSliders;std::array<std::unique_ptr<SA>,4> eightAttachments;
    std::unique_ptr<CA> calibrationAttachment,qualityAttachment;std::unique_ptr<BA> motionAttachment;
    std::array<juce::Slider,8> sliders;std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,8> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mediumAttachment;
};
