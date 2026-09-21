#pragma once
#include "RoomProcessor.h"
#include "VisualSettings.h"
#include "GpuEffects.h"
#include "GlassLook.h"

class VisualEffectsPanel final : public juce::Component,private juce::Timer {
public:
    explicit VisualEffectsPanel(RoomProcessor& p):scene(p){
        addAndMakeVisible(enabled);enabled.setName("Enable visual effects");
        enableAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"visualEnabled",enabled);
        constexpr const char* tips[]={"Blend all visual layers with the clean room.","Animated Julia fractal filaments.","A flowing triangular interference mesh.","Fold the view into radial symmetry.","Separate the image into red and blue refractions.","Bend the image with a flowing wave field.","Change the size of the fractal and mesh patterns.","Fractal iterations, mesh density and kaleidoscope segments.","Pattern animation speed. Zero holds the animation phase.","Shift the fractal and mesh palette.","How strongly note hits and instrument levels move the patterns."};
        for(size_t i=0;i<sliders.size();++i){auto& slider=sliders[i];const auto& spec=tide::visual::controls[i];
            slider.setName(spec.name);slider.setTooltip(tips[i]);slider.setSliderStyle(juce::Slider::LinearHorizontal);slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,64,24);
            slider.setDoubleClickReturnValue(true,spec.initial);slider.setNumDecimalPlacesToDisplay(i==7?0:2);addAndMakeVisible(slider);
            attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,spec.id,slider);
            if(i<6||i>=9){slider.textFromValueFunction=[](double v){return juce::String(juce::roundToInt(v*100))+"%";};slider.valueFromTextFunction=[](const juce::String& s){return s.getDoubleValue()/100.;};slider.updateText();}
        }
        for(size_t i=0;i<presets.size();++i){addAndMakeVisible(presets[i]);presets[i].onClick=[this,i]{preset((int)i);};}
        setSize(480,638);startTimerHz(4);timerCallback();
    }
    void paint(juce::Graphics& g)override{
        g.fillAll(tide::glass::background);g.setColour(tide::glass::text);g.setFont(juce::FontOptions(18.f));g.drawText("VISUAL EFFECTS",24,18,290,30,juce::Justification::centredLeft);
        for(size_t i=0;i<sliders.size();++i){g.setFont(juce::FontOptions(13.f));g.setColour(tide::glass::text.withAlpha(isAvailable?1.f:.4f));g.drawText(tide::visual::controls[i].name,24,row(i),122,28,juce::Justification::centredLeft);}
        g.setColour(tide::glass::line);g.drawLine(24,368,456,368,.7f);
        if(!isAvailable){g.setColour(tide::glass::muted);g.setFont(juce::FontOptions(12.f));g.drawText("Visual effects unavailable. Room rendering is still active.",24,599,432,24,juce::Justification::centredLeft);}
    }
    void resized()override{enabled.setBounds(352,21,104,25);for(size_t i=0;i<sliders.size();++i)sliders[i].setBounds(150,row(i),306,28);
        for(size_t i=0;i<presets.size();++i)presets[i].setBounds(24+(int)i*110,64,102,27);}
private:
    RoomProcessor& scene;
    juce::ToggleButton enabled{"Enabled"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment;
    std::array<juce::Slider,11> sliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,11> attachments;
    std::array<juce::TextButton,4> presets{{juce::TextButton{"Clean"},juce::TextButton{"Filaments"},juce::TextButton{"Topology"},juce::TextButton{"Prismatic"}}};
    bool isAvailable=true;
    static int row(size_t i){return 112+(int)i*40+(i>=6?24:0);}
    void timerCallback()override{const bool next=tide::gpu::available();if(next!=isAvailable){isAvailable=next;for(auto& s:sliders)s.setEnabled(next);for(auto& b:presets)b.setEnabled(next);enabled.setEnabled(next);repaint();}}
    void preset(int index){
        for(const auto& c:tide::visual::controls)scene.set(c.id,c.initial);scene.set("visualEnabled",1);
        if(index==1){scene.set("visualFractal",.7f);scene.set("visualPrism",.2f);scene.set("visualFlow",.15f);scene.set("visualScale",1.4f);scene.set("visualHue",.58f);}
        if(index==2){scene.set("visualMesh",.8f);scene.set("visualFlow",.4f);scene.set("visualDetail",5);scene.set("visualHue",.36f);scene.set("visualReact",1.2f);}
        if(index==3){scene.set("visualKaleidoscope",.75f);scene.set("visualPrism",.7f);scene.set("visualFractal",.25f);scene.set("visualHue",.12f);scene.set("visualDetail",4);}
    }
};

class VisualEffectsWindow final : public juce::DocumentWindow {
public:
    VisualEffectsWindow(RoomProcessor& p,juce::LookAndFeel& look):DocumentWindow("Tide Room — Visual effects",tide::glass::background,closeButton){
        setUsingNativeTitleBar(true);setLookAndFeel(&look);setContentOwned(new VisualEffectsPanel(p),true);setResizable(false,false);setAlwaysOnTop(true);centreWithSize(getWidth(),getHeight());setVisible(true);
    }
    ~VisualEffectsWindow()override{clearContentComponent();setLookAndFeel(nullptr);}
    void closeButtonPressed()override{setVisible(false);}
};
