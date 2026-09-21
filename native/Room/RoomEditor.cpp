#include "RoomEditor.h"
#include "MotionPanel.h"
#include "OceanPanel.h"
#include "ImmersiveView.h"
#include "TidesPanel.h"
#include "WornTapePanel.h"
#include "AdvancedTidesPanel.h"

namespace {
const juce::Colour paper=tide::glass::background,ink=tide::glass::text,panel=tide::glass::surface;
const std::array<juce::Colour,3> colours{{juce::Colour(0xffb3c2c4),juce::Colour(0xff93b3b1),juce::Colour(0xffbdb1ae)}};
constexpr const char* names[]={"WOOD","PLUCK","METAL"};
#if TIDE_CLEAR_ROOM
constexpr const char* globalIds[]={"tempo","evolution","output","roomWidth","roomDepth","roomHeight","clearDecay","clearTail","reflections","clearDamping"};
constexpr const char* globalNames[]={"Tempo","Evolution","Output","Width","Depth","Height","Decay","Tail","Reflections","Damping"};
#elif TIDE_NATIVE_WOOD
constexpr const char* globalIds[]={"tempo","evolution","output","roomWidth","roomDepth","roomHeight","woodRegen","woodTail","reflections","woodWarmth"};
constexpr const char* globalNames[]={"Tempo","Evolution","Output","Width","Depth","Height","Sustain","Tail","Reflections","Warmth"};
#else
constexpr const char* globalIds[]={"tempo","evolution","output","roomWidth","roomDepth","roomHeight","roomDecay","tail","reflections"};
constexpr const char* globalNames[]={"Tempo","Evolution","Output","Width","Depth","Height","Decay","Tail","Reflections"};
#endif
constexpr const char* partIds[]={"level","brightness","length","density"};
constexpr const char* partNames[]={"Level","Brightness","Note decay","Density"};
void style(juce::Slider& s,const char* suffix,int decimals=1){s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,70,24);s.setTextValueSuffix(suffix);s.setNumDecimalPlacesToDisplay(decimals);}
void label(juce::Graphics& g,const juce::String& text,juce::Rectangle<int> r,float size,juce::Colour c=ink){g.setColour(c);g.setFont(juce::FontOptions(size));g.drawText(text,r,juce::Justification::centredLeft);}
void powerButton(juce::TextButton& button){
    button.setClickingTogglesState(true);button.setName("Tape power");
    button.setColour(juce::TextButton::buttonColourId,juce::Colours::transparentBlack);
    button.setColour(juce::TextButton::buttonOnColourId,ink);
    button.setColour(juce::TextButton::textColourOffId,ink);
    button.setColour(juce::TextButton::textColourOnId,ink);
    button.setTooltip("Turn the global saturation on or off. Off plays the original room at the same latency.");
}
class TapePanel final : public juce::Component, private juce::Timer {
public:
    TapePanel(RoomProcessor& p):owner(p),model(p){
        powerButton(power);addAndMakeVisible(power);powerAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"tapeOn",power);
        powerButton(motionPower);motionPower.setName("821 transport motion");motionPower.setTooltip("Enable the measured Slow transport. Off smoothly returns to perfectly steady playback.");addAndMakeVisible(motionPower);motionPowerAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters,"eightMotionOn",motionPower);
        const char* ids[]={"tapeDrive","tapeSaturation","tapeBias","tapeWarmth","tapeMix","spatialDrive","spatialSoftness","spatialTrim","studioDrive","studioCream","studioBias","studioMotion","studioNoise","studioTrim","eightDrive","eightTrim","eightWow","eightFlutter","wornDrive","wornAge","wornMotion","wornDamage","wornNoise","wornTrim","wornDips"};
        const char* help[]={"Drive into the original magnetic model.","How readily the magnetic model saturates.","Changes the hysteresis loop.","Record/replay emphasis and treble softness.","Blend with the original at the same nominal latency. Transport motion can colour a partial blend.","Push the whole room into spatial saturation.","Level-dependent softening of high harmonics. 100% is the original maximum.","Fixed output adjustment.","Recording level above the calibrated reference. Small-signal gain is compensated; saturated peaks get quieter.","0% is the SM911 reference curve. Higher values extend HF overload and soften generated upper harmonics, with little change at quiet levels.","A relative bias-colour adjustment around the reference, not bias current or a measured overbias amount.","100% is a restrained studio transport. Both channels share the same motion to preserve placement. 0% is perfectly steady.","Medium hiss and signal-dependent grain. 0% removes both; no noise gate.","Fixed output gain. No compressor or automatic gain changes.","Drive into the 821 model, with matching output compensation. 0 dB reproduces the measured reference setting.","Output gain after the 821 model.","Depth of the shared Slow transport. Even 0% retains some motion when enabled. Above 100% extends the measured range.","Speeds up the shared Slow transport and slightly changes its depth, as measured in the reference. Above 100% is an extension.","Recording level into the rounded, frequency-dependent saturation. Small-signal gain is compensated.","Progressively softens the playback bandwidth and adds low-frequency body.","Irregular shared pitch motion: slow wander, roller wow and flutter. 0% stops timing motion.","Smooth losses of tape contact: linked treble loss, level dips, uneven track wear and transport drag.","Filtered tape hiss and signal-dependent grain. 0% is completely noiseless.","Output level after the worn tape.","Depth of contact-related volume ducking. 0% removes ducking, 100% is the original depth, 200% doubles the loss in dB. Treble loss and pitch drag remain."};
        for(size_t i=0;i<sliders.size();++i){auto& c=sliders[i];addAndMakeVisible(c);const bool db=i==0||i==5||i==7||i==8||(i>=13&&i<=15)||i==18||i==23;style(c,db?" dB":"",1);c.setName(ids[i]);c.setTooltip(help[i]);attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,ids[i],c);
            if(!db){const double scale=i==6?96:1;c.textFromValueFunction=[scale](double v){return juce::String(juce::roundToInt(v/scale*100))+"%";};c.valueFromTextFunction=[scale](const juce::String& v){return v.getDoubleValue()*scale/100;};c.updateText();}
        }
        addAndMakeVisible(model);
        addAndMakeVisible(quality);quality.setName("Oversampling");quality.addItemList({"1x  Eco","2x  Balanced","4x  Reference"},1);qualityAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"tapeQuality",quality);
        addAndMakeVisible(studioQuality);studioQuality.setName("Studio oversampling");studioQuality.addItemList({"4x  Eco","8x  High","16x  Finest"},1);studioQualityAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"studioQuality",studioQuality);
        addAndMakeVisible(eightCalibration);eightCalibration.setName("821 formula and speed");eightCalibration.addItemList({"456 / 15 ips","900 / 30 ips"},1);eightCalibrationAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"eightCalibration",eightCalibration);eightCalibration.setTooltip("Independently measured tape calibrations. Switching briefly fades through dry.");
        addAndMakeVisible(eightQuality);eightQuality.setName("821 limiter quality");eightQuality.addItemList({"Native","4x / oversampled limiter","8x / oversampled limiter"},1);eightQualityAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"eightQuality",eightQuality);eightQuality.setTooltip("Oversamples the final limiter. Native uses the calibrated model at the host rate. Switching briefly fades through dry; latency stays fixed.");
        addAndMakeVisible(wornMedium);wornMedium.setName("Worn tape medium");wornMedium.addItemList({"Cassette","Home video"},1);wornMediumAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"wornMedium",wornMedium);wornMedium.setTooltip("Home video is a darker linear-track-inspired character, not a calibrated VHS Hi-Fi model.");
        quality.setTooltip("Higher settings reduce nonlinear aliasing and use more CPU. Switching fades briefly through dry.");studioQuality.setTooltip("Integrated magnetic curves plus 4x, 8x or 16x oversampling. 16x is the cleanest at heavy drive. Latency stays fixed when switching.");
        const char* presetNames[]={"Reference","Cream","Bloom"};for(int i=0;i<3;++i){addAndMakeVisible(presets[(size_t)i]);presets[(size_t)i].setButtonText(presetNames[i]);presets[(size_t)i].onClick=[this,i]{if((int)owner.get("tapeModel")==4){owner.selectWornPreset(i);return;}const float values[3][6]={{0,0,0,1,1,0},{18,1,0,1,.15f,.5f},{27,2.2f,.2f,1.8f,.25f,3.5f}};const char* keys[]={"studioDrive","studioCream","studioBias","studioMotion","studioNoise","studioTrim"};for(int j=0;j<6;++j)owner.set(keys[j],values[i][j]);owner.set("tapeMix",1);owner.set("tapeOn",1);};}
        setSize(564,604);startTimerHz(10);timerCallback();
    }
    void paint(juce::Graphics& g) override {g.fillAll(paper);label(g,"TAPE / THE WHOLE ROOM",{22,12,330,26},17);label(g,"Model",{24,52,140,28},14);
        if(mode==4){label(g,"Medium",{24,90,140,28},14);const char* names[]={"Recording drive","Age","Transport","Contact loss","Dip depth","Hiss / grain","Output trim","Mix"};for(int i=0;i<8;++i)label(g,names[i],{24,134+i*36,140,28},14);}
        else if(mode==3){label(g,"Formula / speed",{24,90,140,28},14);label(g,"Limiter quality",{24,132,140,28},14);label(g,"Transport",{24,260,140,28},14);const char* names[]={"Recording drive","Output trim","Wow","Flutter","Mix"};for(int i=0;i<5;++i)label(g,names[i],{24,176+i*42+(i>=2?42:0),140,28},14);}
        else if(mode==2){label(g,"Oversampling",{24,90,140,28},14);const char* names[]={"Recording drive","HF cream","Bias colour","Transport","Tape texture","Output trim","Mix"};for(int i=0;i<7;++i)label(g,names[i],{24,134+i*42,140,28},14);}
        else if(mode==1){label(g,"Oversampling",{24,90,140,28},14);const char* names[]={"Drive","HF softness","Output trim","Mix"};for(int i=0;i<4;++i)label(g,names[i],{24,134+i*42,140,28},14);}
        else{const char* names[]={"Drive","Saturation","Bias","Warmth","Mix"};for(int i=0;i<5;++i)label(g,names[i],{24,92+i*42,140,28},14);}
        const juce::String note=owner.get("tapeOn")<.5f?"Tape is off. Switch it on above to hear these controls.":mode==4?"Worn tape / irregular motion and uneven contact":mode==3?owner.tape821Status():mode==2?"A80-inspired studio tape / SM911 at 15 ips":mode==1?"Higher oversampling uses more CPU and reduces aliasing.":"The original magnetic engine uses fixed 32x oversampling.";
        label(g,note,{24,477,520,23},12,ink.withAlpha(.7f));
        if(mode==4)label(g,"Dip depth sets volume ducking. 100% keeps the original depth.",{24,501,520,23},12,ink.withAlpha(.7f));
        if(mode==3)label(g,"Start at 0 dB drive. Raise it for stronger colour; output is compensated.",{24,501,520,23},12,ink.withAlpha(.7f));
        if(mode==2)label(g,"Reference = calibrated curve. Cream and Bloom extend its colour.",{24,501,520,23},12,ink.withAlpha(.7f));
        label(g,"DSP: "+juce::String(juce::roundToInt(owner.dspLoad.load()*100))+"%    |    Tape latency: "+juce::String(768000./std::max(1.,owner.getSampleRate()),1)+" ms",{24,535,520,23},12,ink.withAlpha(.7f));
        label(g,"Peak DSP: "+juce::String(juce::roundToInt(owner.peakDspLoad.load()*100))+"%    |    Over-budget blocks: "+juce::String(owner.lateBlocks.load()),{24,565,520,23},12,ink.withAlpha(.7f));
    }
    void resized() override {power.setBounds(406,10,132,30);model.setBounds(174,52,364,28);quality.setBounds(174,90,364,28);studioQuality.setBounds(174,90,364,28);eightCalibration.setBounds(174,90,364,28);eightQuality.setBounds(174,132,364,28);motionPower.setBounds(174,260,164,28);wornMedium.setBounds(174,90,364,28);
        if(mode==4){const int idx[]={18,19,20,21,24,22,23,4};for(int i=0;i<8;++i)sliders[(size_t)idx[i]].setBounds(174,134+i*36,364,28);}
        else if(mode==3){const int idx[]={14,15,16,17,4};for(int i=0;i<5;++i)sliders[(size_t)idx[i]].setBounds(174,176+i*42+(i>=2?42:0),364,28);}
        else if(mode==2){const int idx[]={8,9,10,11,12,13,4};for(int i=0;i<7;++i)sliders[(size_t)idx[i]].setBounds(174,134+i*42,364,28);}
        else if(mode==1){const int idx[]={5,6,7,4};for(int i=0;i<4;++i)sliders[(size_t)idx[i]].setBounds(174,134+i*42,364,28);}else for(int i=0;i<5;++i)sliders[(size_t)i].setBounds(174,92+i*42,364,28);
        for(int i=0;i<3;++i)presets[(size_t)i].setBounds(24+i*175,434,164,29);
    }
private:
    void timerCallback() override {motionPower.setButtonText(owner.get("eightMotionOn")>.5f?"Motion on":"Motion off");power.setButtonText(owner.get("tapeOn")>.5f?"Tape on":"Tape off");const int value=(int)owner.get("tapeModel");if(value!=mode||!initial){mode=value;initial=true;for(int i=0;i<25;++i)sliders[(size_t)i].setVisible(mode==4?(i>=18||i==4):mode==3?((i>=14&&i<18)||i==4):mode==2?((i>=8&&i<14)||i==4):mode==1?(i>=4&&i<8):(i<=4));quality.setVisible(mode==1);studioQuality.setVisible(mode==2);eightQuality.setVisible(mode==3);eightCalibration.setVisible(mode==3);motionPower.setVisible(mode==3);wornMedium.setVisible(mode==4);const char* studioNames[]={"Reference","Cream","Bloom"};const char* wornNames[]={"Old cassette","Unspooled","Home video"};for(size_t i=0;i<presets.size();++i){presets[i].setVisible(mode==2||mode==4);presets[i].setButtonText(mode==4?wornNames[i]:studioNames[i]);}resized();}repaint();}
    RoomProcessor& owner;int mode=1;bool initial=false;
    juce::TextButton power{"Tape off"},motionPower{"Motion off"};std::array<juce::TextButton,3> presets;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment,motionPowerAttachment;
    TapeModelSelector model;juce::ComboBox quality,studioQuality,eightQuality,eightCalibration,wornMedium;
    std::array<juce::Slider,25> sliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,25> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttachment,studioQualityAttachment,eightQualityAttachment,eightCalibrationAttachment,wornMediumAttachment;
    juce::TooltipWindow tips{this,700};
};
}
struct RoomEditor::TapeWindow final : juce::DocumentWindow {
    TapeWindow(RoomProcessor& p,juce::LookAndFeel& look,juce::KeyListener& keys):DocumentWindow("Tide Room - Tape",paper,closeButton),keyboard(keys){addKeyListener(&keyboard);setWantsKeyboardFocus(true);setUsingNativeTitleBar(true);setLookAndFeel(&look);auto* controls=new TapePanel(p);controls->setLookAndFeel(&look);setContentOwned(controls,true);setResizable(false,false);centreWithSize(getWidth(),getHeight());setVisible(true);}
    ~TapeWindow() override {removeKeyListener(&keyboard);clearContentComponent();setLookAndFeel(nullptr);}
    void closeButtonPressed() override {setVisible(false);}
    juce::KeyListener& keyboard;
};
struct RoomEditor::MotionWindow final : juce::DocumentWindow {
    MotionWindow(RoomProcessor& p,juce::LookAndFeel& look,juce::KeyListener& keys):DocumentWindow("Tide Room - Motion",paper,closeButton),keyboard(keys){addKeyListener(&keyboard);setWantsKeyboardFocus(true);setUsingNativeTitleBar(true);setLookAndFeel(&look);setContentOwned(new MotionPanel(p),true);setResizable(false,false);centreWithSize(getWidth(),getHeight());setVisible(true);}
    ~MotionWindow()override{removeKeyListener(&keyboard);clearContentComponent();setLookAndFeel(nullptr);}
    void closeButtonPressed()override{setVisible(false);}
    juce::KeyListener& keyboard;
};
struct RoomEditor::OceanWindow final : juce::DocumentWindow {
    OceanWindow(RoomProcessor& p,juce::LookAndFeel& look,juce::KeyListener& keys,TowerRenderer& towers):DocumentWindow("Tide Room - Tower Oceans",paper,closeButton),keyboard(keys){addKeyListener(&keyboard);setWantsKeyboardFocus(true);setUsingNativeTitleBar(true);setLookAndFeel(&look);setContentOwned(new OceanPanel(p,&towers),true);setResizable(false,false);centreWithSize(getWidth(),getHeight());setVisible(true);}
    ~OceanWindow()override{removeKeyListener(&keyboard);clearContentComponent();setLookAndFeel(nullptr);}
    void closeButtonPressed()override{setVisible(false);}
    juce::KeyListener& keyboard;
};

struct RoomEditor::TidesWindow final : juce::DocumentWindow {
    TidesWindow(RoomProcessor& p,juce::LookAndFeel& look,juce::KeyListener& keys):DocumentWindow("Tide Room - Advanced Tides",paper,closeButton),keyboard(keys){addKeyListener(&keyboard);setWantsKeyboardFocus(true);setUsingNativeTitleBar(true);setLookAndFeel(&look);auto* controls=new AdvancedTidesPanel(p);controls->setLookAndFeel(&look);setContentOwned(controls,true);setResizable(false,false);centreWithSize(getWidth(),getHeight());setVisible(true);}
    ~TidesWindow()override{removeKeyListener(&keyboard);clearContentComponent();setLookAndFeel(nullptr);}
    void closeButtonPressed()override{setVisible(false);}
    juce::KeyListener& keyboard;
};

struct RoomEditor::ImmersiveWindow final : juce::DocumentWindow {
    ImmersiveWindow(RoomProcessor& p,juce::LookAndFeel& look,juce::KeyListener& keys,std::function<void()> leave)
        :DocumentWindow("Tide Room",paper,0),keyboard(keys),onExit(std::move(leave)) {
        setUsingNativeTitleBar(false);setTitleBarHeight(0);setResizable(false,false);setLookAndFeel(&look);
        addKeyListener(&keyboard);setWantsKeyboardFocus(true);
        setContentOwned(new ImmersiveView(p,onExit),true);
    }
    juce::BorderSize<int> getBorderThickness()const override{return {};}
    ~ImmersiveWindow()override{leaveFullscreen();removeKeyListener(&keyboard);clearContentComponent();setLookAndFeel(nullptr);}
    void enter(juce::Rectangle<int> parentBounds){
        auto& desktop=juce::Desktop::getInstance();
        const auto* display=desktop.getDisplays().getDisplayForRect(parentBounds);
        if(display==nullptr)return; // No window server (for example a headless host).
        setBounds(display->totalArea);
        setVisible(true);desktop.setKioskModeComponent(this,false);toFront(true);getContentComponent()->grabKeyboardFocus();
    }
    void leaveFullscreen(){auto& desktop=juce::Desktop::getInstance();if(desktop.getKioskModeComponent()==this)desktop.setKioskModeComponent(nullptr);setVisible(false);}
    bool keyPressed(const juce::KeyPress& key)override{if(key.getKeyCode()==juce::KeyPress::escapeKey){onExit();return true;}return false;}
    void closeButtonPressed()override{onExit();}
    juce::KeyListener& keyboard;
    std::function<void()> onExit;
};

RoomEditor::RoomEditor(RoomProcessor& p):AudioProcessorEditor(p),scene(p),harmony(p),harmonyKeys(p),presetBar(p),minimalTides(p,[this]{openAdvancedTides();}),postProcessing(p) {
    setLookAndFeel(&look);addAndMakeVisible(minimalTides);minimalTides.setLookAndFeel(&look);addAndMakeVisible(postProcessing);postProcessing.setLookAndFeel(&look);addAndMakeVisible(presetBar);addAndMakeVisible(harmony);harmony.setLookAndFeel(&look);addKeyListener(&harmonyKeys);setWantsKeyboardFocus(true);
    for(size_t i=0;i<global.size();++i){auto& s=global[i];s.setName(globalNames[i]);if(i!=5)addAndMakeVisible(s);style(s,i==0?" bpm":i==2||i>=7?" dB":i==6?" s":i>=3?" m":"",i==6?2:1);globalAttachments[i]=std::make_unique<SliderAttachment>(p.parameters,globalIds[i],s);}
    global[1].textFromValueFunction=[](double v){return juce::String(juce::roundToInt(v*100))+"%";};global[1].valueFromTextFunction=[](const juce::String& s){return s.getDoubleValue()/100;};global[1].updateText();
    global[1].setTooltip("More changing accents, motif rotation and gradual phase drift in the supporting parts. The wooden pulse stays anchored.");
    global[6].setTooltip("Length of the shared late reverberation.");global[7].setTooltip("Level of the late tail; the direct sound and early reflections remain present.");
    global[8].setTooltip("Room field relative to the direct sound. Lower values make instruments easier to pinpoint. Available with Headphones or Moving reflections.");
#if TIDE_NATIVE_WOOD
#if TIDE_CLEAR_ROOM
    const std::initializer_list<int> percentControls{9};
#else
    const std::initializer_list<int> percentControls{6,9};
#endif
    for(int i:percentControls){global[(size_t)i].textFromValueFunction=[](double v){return juce::String(juce::roundToInt(v*100))+"%";};global[(size_t)i].valueFromTextFunction=[](const juce::String& s){return s.getDoubleValue()/100;};global[(size_t)i].setTextValueSuffix("");global[(size_t)i].updateText();}
    global[6].setTooltip("kWoodRoom regeneration. More sustain lets the wooden room ring for longer.");
    global[9].setTooltip("Smooth treble softening of the shared reverb. Direct sound and wall reflections keep their detail.");
    global[7].setTooltip("Level of the shared kWoodRoom tail. Saved independently of the previous Reverside tail.");
    for(int i:{3,4,5})global[(size_t)i].setTooltip("Dimensions of the moving reflection room. The kWoodRoom tail retains its own fixed room shape.");
    global[8].setTooltip("The whole reflected field relative to the direct sound: wall reflections and the shared tail.");
#if TIDE_CLEAR_ROOM
    global[6].setTooltip("Nominal low-frequency decay time to -60 dB. Longer decay keeps a roughly similar tail level.");
    global[7].setTooltip("Level of the shared diffuse tail. Direct sound and wall reflections are separate.");
    global[9].setTooltip("Makes treble decay faster within the room. Zero keeps the same decay across frequencies.");
    for(int i:{3,4,5})global[(size_t)i].setTooltip("Dimensions of the moving reflection room. The diffuse tail has a fixed, distributed shape.");
#endif
#endif
    for(int i=0;i<3;++i){auto& card=cards[(size_t)i];addAndMakeVisible(card.voice);card.voice.setName("Instrument "+juce::String(i+1));for(int k=0;k<tide::room::patchCount;++k)card.voice.addItem(tide::room::patches[(size_t)k].name,k+1);
        card.voice.onChange=[this,i]{const int selected=cards[(size_t)i].voice.getSelectedId()-1;if(selected>=0&&selected!=(int)scene.get(RoomProcessor::partId(i,"voice")))scene.selectVoice(i,selected);};
        addAndMakeVisible(card.mute);card.muteAttachment=std::make_unique<ButtonAttachment>(p.parameters,RoomProcessor::partId(i,"mute"),card.mute);
        for(size_t k=0;k<card.controls.size();++k){auto& s=card.controls[k];addAndMakeVisible(s);s.setName(juce::String(names[i])+" "+partNames[k]);style(s,k==0?" dB":k==2?" s":k==4?" m":"",k==2?2:1);s.setColour(juce::Slider::trackColourId,colours[(size_t)i]);card.attachments[k]=std::make_unique<SliderAttachment>(p.parameters,RoomProcessor::partId(i,partIds[k]),s);
            if(k==1||k==3){s.textFromValueFunction=[](double v){return juce::String(juce::roundToInt(v*100))+"%";};s.valueFromTextFunction=[](const juce::String& value){return value.getDoubleValue()/100;};s.updateText();}
        }
    }
    for(auto* b:{&playButton,&quietButton,&variationButton,&editorButton})addAndMakeVisible(*b);
    addAndMakeVisible(recordButton);addAndMakeVisible(recordingFileButton);addAndMakeVisible(recordingTime);
    recordButton.setName("Record output");recordingFileButton.setName("Show recording in Finder");
    recordButton.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xffac3f38));
    recordButton.setColour(juce::TextButton::textColourOnId,juce::Colours::white);
    recordingTime.setColour(juce::Label::textColourId,juce::Colour(0xffac3f38));
    recordingTime.setFont(juce::FontOptions(15.f));recordingTime.setJustificationType(juce::Justification::centredRight);
    recordButton.setTooltip("Record the complete stereo mix to a 32-bit float WAV in Music / Tide Room Recordings. Stop the music first to capture its tail, then Stop recording.");
    recordingFileButton.setTooltip("Reveal the most recent take in Finder.");
    recordButton.onClick=[this]{
        const auto state=scene.recordingState();
        if(state.recording){scene.stopRecording();timerCallback();return;}
        if(state.saving)return;
        const auto folder=juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Tide Room Recordings");
        juce::String error;
        if(folder.createDirectory().failed())error="Could not create Music / Tide Room Recordings. Check the folder permissions and disk space.";
        else {
            const auto filename="Tide Room "+juce::Time::getCurrentTime().formatted("%Y-%m-%d %H.%M.%S");
            scene.startRecording(folder.getNonexistentChildFile(filename,".wav"),error);
        }
        if(error.isNotEmpty())juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Could not record",error);
        timerCallback();
    };
    recordingFileButton.onClick=[this]{const auto file=scene.recordingState().file;if(file.existsAsFile())file.revealToUser();};
    addAndMakeVisible(placement);placement.setName("Placement");placement.addItemList({"Headphones","Stereo"},1);placementAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"placement",placement);
    powerButton(tapeOn);addChildComponent(tapeOn);addChildComponent(tapeButton);tapeAttachment=std::make_unique<ButtonAttachment>(p.parameters,"tapeOn",tapeOn);
    tapeOn.setTooltip("Global room saturation after the three instruments and their reflections. Off gives the latency-aligned original.");
    tapeButton.onClick=[this]{openTapeControls();};
    addChildComponent(oceanButton);oceanButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff2e6873));oceanButton.setTooltip("Patch each tower's water X/Y displacement into synthesis. Nearby towers pull more strongly; oceans lag and slosh.");oceanButton.onClick=[this]{if(!oceanWindow)oceanWindow=std::make_unique<OceanWindow>(scene,look,harmonyKeys,sceneRenderer.towers);oceanWindow->setVisible(true);oceanWindow->toFront(true);};
    addAndMakeVisible(immersiveButton);immersiveButton.setTooltip("View the scene fullscreen from the listener. Escape returns to the controls.");immersiveButton.onClick=[this]{openImmersiveView();};
    addChildComponent(motionButton);motionButton.setTooltip("Six LFOs for moving instruments and their reflections. Choose the reflection renderer in the Motion window.");motionButton.onClick=[this]{if(!motionWindow)motionWindow=std::make_unique<MotionWindow>(scene,look,harmonyKeys);motionWindow->setVisible(true);motionWindow->toFront(true);};
    placement.setTooltip("Headphones uses measured ear filters. Stereo uses speaker panning for the moving room, or the original Reverside placement with fixed reflections.");
    playButton.onClick=[this]{if(scene.isPlaying())scene.stop();else scene.play();};quietButton.onClick=[this]{scene.quiet();};variationButton.onClick=[this]{scene.vary();};editorButton.onClick=[this]{scene.openRoomEditor();};
    playButton.setTooltip("Play restarts the score. Stop lets the last notes and room fade away.");quietButton.setTooltip("Stop the pattern and fade all sound to silence.");
    variationButton.setTooltip("Choose another repeatable set of accents. The foundation stays the same.");editorButton.setTooltip("Edit the shared room character. This window controls the room dimensions, source positions, decay and tail level.");
#if TIDE_NATIVE_WOOD
    editorButton.setVisible(false);
    placement.setTooltip("Headphones uses measured ear filters. Stereo uses speaker panning. Both share the same room.");
    addAndMakeVisible(reverbCreditCaption);reverbCreditCaption.setText("kWoodRoom reverb credit",juce::dontSendNotification);
    reverbCreditCaption.setFont(juce::FontOptions(10.f));reverbCreditCaption.setBorderSize({});
    reverbCreditCaption.setColour(juce::Label::textColourId,tide::glass::muted);
    addAndMakeVisible(reverbCredit);reverbCredit.setFont(juce::Font(juce::FontOptions(11.f)),false,juce::Justification::centredLeft);
    reverbCredit.setColour(juce::HyperlinkButton::textColourId,tide::glass::text.withAlpha(.78f));
    reverbCredit.setTooltip("Chris Johnson / Airwindows created kWoodRoom, retained in Tide Room's WoodRoom engine under the MIT license. Visit Airwindows.");
#endif
    setSize(1200,900);startTimerHz(25);timerCallback();
}
void RoomEditor::openTapeControls(){if(!tapeWindow)tapeWindow=std::make_unique<TapeWindow>(scene,look,harmonyKeys);tapeWindow->setVisible(true);tapeWindow->toFront(true);}
void RoomEditor::openAdvancedTides(){if(!tidesWindow)tidesWindow=std::make_unique<TidesWindow>(scene,look,harmonyKeys);tidesWindow->setVisible(true);tidesWindow->toFront(true);}
RoomEditor::~RoomEditor(){stopTimer();tidesWindow.reset();immersiveWindow.reset();oceanWindow.reset();motionWindow.reset();tapeWindow.reset();removeKeyListener(&harmonyKeys);setLookAndFeel(nullptr);}
void RoomEditor::openImmersiveView(){
    if(!immersiveWindow)immersiveWindow=std::make_unique<ImmersiveWindow>(scene,look,harmonyKeys,[this]{closeImmersiveView();});
    immersiveWindow->enter(getScreenBounds());
}
void RoomEditor::closeImmersiveView(){if(immersiveWindow)immersiveWindow->leaveFullscreen();if(isShowing())grabKeyboardFocus();repaint();}
bool RoomEditor::isImmersiveViewOpen()const{return immersiveWindow&&immersiveWindow->isVisible();}
juce::Component* RoomEditor::immersiveComponent()const{return immersiveWindow?immersiveWindow->getContentComponent():nullptr;}
ListenerSpace RoomEditor::space() const {
    ListenerSpace view;view.width=scene.get("roomWidth");view.depth=scene.get("roomDepth");view.height=scene.get("roomHeight");view.bounds=tide::layout::roomBounds();view.centre=view.bounds.getCentre();return view;
}
juce::Point<float> RoomEditor::source(int i) const {const auto v=space();return v.project(v.position(scene.get(RoomProcessor::partId(i,"x")),scene.get(RoomProcessor::partId(i,"y")),scene.towerHeight()));}
juce::Point<float> RoomEditor::movingSource(int i) const {const auto v=space();const auto p=scene.movingPosition(i);return v.project(v.position(p.lateral,p.depth,p.height));}
void RoomEditor::paint(juce::Graphics& g) {
    g.fillAll(paper);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff14202a).withAlpha(.4f),600,540,juce::Colour(0xff0b1016).withAlpha(0.f),1100,730,true));g.fillAll();
    label(g,"Tide Room",{40,24,300,42},30);
    label(g,"Tempo",{40,79,62,24},11,tide::glass::muted);label(g,"Evolution",{332,79,76,24},11,tide::glass::muted);
    const auto& selectedPattern=tide::room::selectedPattern((int)scene.get("patternBank"));
    for(int i=0;i<3;++i){const int x=40+i*384;label(g,"0"+juce::String(i+1),{x,187,28,24},11,colours[(size_t)i]);label(g,names[i],{x+32,187,180,24},12);
        for(int k=0;k<4;++k)label(g,partNames[k],{x,247+k*24,87,22},11,tide::glass::muted);
        const int count=selectedPattern.parts[(size_t)i].length;for(int k=0;k<count;++k){const float w=342.f/count;g.setColour(colours[(size_t)i].withAlpha(scene.isPlaying()&&scene.lastStep[(size_t)i].load()%count==k?.85f:.12f));g.fillRoundedRectangle((float)x+w*k,346,w-3,2,1);}
    }
    g.setColour(tide::glass::line.withAlpha(.65f));g.drawLine(40,356,1160,356,.6f);
    if(!isImmersiveViewOpen())sceneRenderer.draw(g,scene,space(),dragged,getMouseXYRelative().toFloat());
    label(g,"SPACE",{975,374,185,22},12);
    for(int i=0;i<5;++i)label(g,globalNames[i<2?i+3:i+4],{975,408+i*35,68,25},11,tide::glass::muted);
#if TIDE_NATIVE_WOOD
    label(g,globalNames[9],{975,583,68,25},11,tide::glass::muted);
#endif
    g.setColour(tide::glass::line);g.drawLine(40,739,1160,739,.65f);g.drawLine(1011,762,1011,873,.65f);
    const auto signalArrow=[&](juce::Point<float> from,juce::Point<float> to){
        auto direction=to-from;direction/=direction.getDistanceFromOrigin();const auto side=juce::Point<float>{-direction.y,direction.x};
        g.setColour(tide::glass::text.withAlpha(.52f));g.drawLine({from,to},.85f);juce::Path tip;tip.startNewSubPath(to-direction*4+side*3);tip.lineTo(to);tip.lineTo(to-direction*4-side*3);g.strokePath(tip,juce::PathStrokeType(.85f));
        if(scene.isPlaying()){const float phase=(float)std::fmod(juce::Time::getMillisecondCounterHiRes()*.00065,1.);const auto dot=from+(to-from)*phase;g.setColour(tide::glass::text.withAlpha(.8f));g.fillEllipse(dot.x-1.5f,dot.y-1.5f,3,3);}
    };
    signalArrow({600,351},{600,372});signalArrow({600,730},{600,761});signalArrow({988,818},{1030,818});
    label(g,"POST PROCESSING",{40,745,300,20},10,tide::glass::muted);label(g,"MASTER",{1040,754,113,20},11);
    const float level=std::clamp((juce::Decibels::gainToDecibels(scene.peak.load(),-60.f)+60.f)/60.f,0.f,1.f);
    g.setColour(tide::glass::line);g.fillRoundedRectangle(1139,784,4,65,2);g.setColour(scene.peak.load()>.98f?juce::Colour(0xffd8a18d):juce::Colour(0xffb7ceca));g.fillRoundedRectangle(1139,849-65*level,4,65*level,2);
    label(g,recordingStatus.isNotEmpty()?recordingStatus:"A-G key   /   1-7 chord   /   Q W R T Y U I O pattern",{40,882,780,16},10,tide::glass::muted);
    label(g,"DSP "+juce::String(juce::roundToInt(scene.dspLoad.load()*100))+"%    /    Variation "+juce::String((int)scene.get("seed")),{944,882,220,16},10,tide::glass::muted);
}
void RoomEditor::resized(){
    presetBar.setBounds(340,15,326,52);harmony.setBounds(40,114,1120,66);
    global[0].setBounds(100,79,200,24);global[1].setBounds(408,79,232,24);
    global[2].setName("Master volume");global[2].setTooltip("Final output volume after the room and tape processing.");global[2].setSliderStyle(juce::Slider::LinearVertical);global[2].setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,24);global[2].setBounds(1040,780,80,96);
    playButton.setBounds(808,31,88,33);quietButton.setBounds(904,31,76,33);variationButton.setBounds(988,31,172,33);
    recordButton.setBounds(688,31,112,33);recordingFileButton.setBounds(1074,77,86,25);recordingTime.setBounds(901,77,162,25);
    minimalTides.setBounds(40,374,174,334);postProcessing.setBounds(40,772,946,108);
    placement.setBounds(975,625,185,28);
    reverbCreditCaption.setBounds(975,684,185,18);reverbCredit.setBounds(974,703,186,20);
    const auto room=tide::layout::roomBounds().toNearestInt();immersiveButton.setBounds(room.getRight()-190,room.getY()+12,178,29);
    for(int i=0;i<5;++i){auto& control=global[(size_t)(i<2?i+3:i+4)];control.setBounds(1041,408+i*35,119,25);control.setTextBoxStyle(juce::Slider::TextBoxRight,false,53,25);}
    editorButton.setBounds(975,698,185,25);
#if TIDE_NATIVE_WOOD
    global[9].setBounds(1041,583,119,25);global[9].setTextBoxStyle(juce::Slider::TextBoxRight,false,53,25);
#endif
    for(int i=0;i<3;++i){const int x=40+i*384;auto& card=cards[(size_t)i];card.mute.setBounds(x+276,187,67,24);card.voice.setBounds(x,214,344,27);
        for(int k=0;k<4;++k)card.controls[(size_t)k].setBounds(x+89,247+k*24,255,22);}
}
void RoomEditor::timerCallback(){tapeOn.setButtonText(scene.get("tapeOn")>.5f?"Tape on":"Tape off");playButton.setEnabled(scene.ready());editorButton.setEnabled(scene.ready());global[8].setEnabled(
#if TIDE_NATIVE_WOOD
    true
#else
    scene.get("placement")<.5f||scene.get("movingReflections")>.5f
#endif
    );playButton.setButtonText(scene.isPlaying()?"Stop":"Play");for(int i=0;i<3;++i)cards[(size_t)i].voice.setSelectedId(1+(int)scene.get(RoomProcessor::partId(i,"voice")),juce::dontSendNotification);
    const auto take=scene.recordingState();
    recordButton.setButtonText(take.recording?"Stop recording":take.saving?"Saving...":"Record");
    recordButton.setToggleState(take.recording,juce::dontSendNotification);recordButton.setEnabled(!take.saving&&(scene.ready()||take.recording));
    recordingFileButton.setEnabled(take.finished);
    const auto seconds=(int64_t)take.seconds;
    const auto elapsed=juce::String(seconds/3600).paddedLeft('0',2)+":"+juce::String(seconds/60%60).paddedLeft('0',2)+":"+juce::String(seconds%60).paddedLeft('0',2);
    recordingTime.setText(take.file==juce::File{}?juce::String{}:(take.recording?"REC ":"")+elapsed,juce::dontSendNotification);
    if(take.error.isNotEmpty())recordingStatus=take.error;
    else if(take.recording)recordingStatus="Recording stereo WAV / "+take.file.getFileName();
    else if(take.saving)recordingStatus="Finishing recording...";
    else if(take.finished)recordingStatus=(take.deviceStopped?"Saved (audio device changed) / ":"Saved / ")+take.file.getFileName();
    else recordingStatus.clear();
    if(!isImmersiveViewOpen())repaint();}
void RoomEditor::mouseDown(const juce::MouseEvent& e){const auto view=space();if(!view.bounds.contains(e.position))return;
    const auto hitPosition=sceneRenderer.hitPosition(view,e.position);
    float nearestDepth=100;for(int i=0;i<3;++i){const auto p=scene.movingPosition(i);const auto physical=view.position(p.lateral,p.depth,p.height);
        if(TowerRenderer::bounds(scene,i,physical,view).contains(hitPosition)&&view.cameraDepth(physical)<nearestDepth){nearestDepth=view.cameraDepth(physical);dragged=i;}}
    if(dragged>=0){dragOffset=source(dragged)-hitPosition;
        for(const auto* id:{"x","y"})scene.parameters.getParameter(RoomProcessor::partId(dragged,id))->beginChangeGesture();}}
void RoomEditor::moveSource(juce::Point<float> point){if(dragged<0)return;const auto view=space();
    const auto set=[&](const char* key,float value){auto* p=scene.parameters.getParameter(RoomProcessor::partId(dragged,key));p->setValueNotifyingHost(p->convertTo0to1(value));};
    const auto world=view.unprojectAtHeight(point,scene.towerHeight());
    set("x",juce::jlimit(-.8f,.8f,world.x*2/view.width));set("y",juce::jlimit(.1f,.68f,world.y/view.depth));repaint();}
void RoomEditor::mouseDrag(const juce::MouseEvent& e){moveSource(sceneRenderer.hitPosition(space(),e.position)+dragOffset);}
void RoomEditor::mouseUp(const juce::MouseEvent&){if(dragged>=0)for(const auto* id:{"x","y"})scene.parameters.getParameter(RoomProcessor::partId(dragged,id))->endChangeGesture();dragged=-1;}
void RoomEditor::mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& wheel){const auto view=space();if(!view.bounds.contains(e.position))return;
    const auto hitPosition=sceneRenderer.hitPosition(view,e.position);
    int hit=-1;float nearest=100;for(int i=0;i<3;++i){const auto p=scene.movingPosition(i);const auto v=view.position(p.lateral,p.depth,p.height);
        if(TowerRenderer::bounds(scene,i,v,view).contains(hitPosition)&&view.cameraDepth(v)<nearest){hit=i;nearest=view.cameraDepth(v);}}
    if(hit<0)return;auto* parameter=scene.parameters.getParameter(RoomProcessor::partId(hit,"y"));parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(juce::jlimit(.1f,.68f,scene.get(RoomProcessor::partId(hit,"y"))+wheel.deltaY*.15f)));parameter->endChangeGesture();repaint();}

juce::Image RoomEditor::tapePanelSnapshot(){TapePanel controls(scene);controls.setLookAndFeel(&look);auto image=controls.createComponentSnapshot(controls.getLocalBounds());controls.setLookAndFeel(nullptr);return image;}
juce::Image RoomEditor::motionPanelSnapshot(){MotionPanel controls(scene);controls.setLookAndFeel(&look);auto image=controls.createComponentSnapshot(controls.getLocalBounds());controls.setLookAndFeel(nullptr);return image;}

juce::Image RoomEditor::oceanPanelSnapshot(){OceanPanel controls(scene,&sceneRenderer.towers);controls.setLookAndFeel(&look);auto image=controls.createComponentSnapshot(controls.getLocalBounds());controls.setLookAndFeel(nullptr);return image;}

void RoomEditor::advanceOceanPreview(double seconds) {timerCallback();sceneRenderer.advancePreview(scene,space(),seconds);}

juce::Image RoomEditor::tidesPanelSnapshot(){TidesPanel controls(scene);controls.setLookAndFeel(&look);return controls.createComponentSnapshot(controls.getLocalBounds());}
