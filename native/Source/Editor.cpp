#include "Editor.h"

namespace {
const juce::Colour paper{0xffe8e5db}, ink{0xff273c39}, blue{0xff467889}, accent{0xffbb6849};
constexpr const char* ids[]={"timbre","drive","motion","attack","decay","sustain","release","space","output"};
constexpr const char* names[]={"Timbre","Drive","Motion","Attack","Decay","Sustain","Release","Amount","Output"};
constexpr const char* tips[]={"Harmonic richness follows the strength of each note.","Gentle saturation inside the voice.",
    "Slow changes in harmonic colour. The mod wheel adds more.","How gradually the note opens.","How quickly the initial energy settles.",
    "The level held while a key or sustain pedal is down.","How long the note takes to fade after release.",
    "How much of the chosen space surrounds the notes.","Final instrument level."};
constexpr float canvasHeight=TIDE_EFFECT_HOST?840.f:720.f;
}
TideLook::TideLook() {
    setColour(juce::Slider::textBoxTextColourId,ink);
    setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xfff5f2eb));
    setColour(juce::ComboBox::textColourId,ink); setColour(juce::ComboBox::outlineColourId,ink.withAlpha(.18f));
    setColour(juce::ComboBox::arrowColourId,ink);
    setColour(juce::PopupMenu::backgroundColourId,juce::Colour(0xfff5f2eb));
    setColour(juce::PopupMenu::textColourId,ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId,blue);
    setColour(juce::TextButton::buttonColourId,ink); setColour(juce::TextButton::textColourOffId,paper);
    setColour(juce::ToggleButton::textColourId,ink);setColour(juce::ToggleButton::tickColourId,ink);
    setColour(juce::ToggleButton::tickDisabledColourId,ink.withAlpha(.45f));
}
void TideLook::drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float value,float start,float finish,juce::Slider&) {
    const auto bounds=juce::Rectangle<float>((float)x,(float)y,(float)w,(float)h).reduced(13);
    const auto radius=std::min(bounds.getWidth(),bounds.getHeight())*.5f;
    const auto c=bounds.getCentre();
    const float angle=start+value*(finish-start);
    juce::Path track; track.addCentredArc(c.x,c.y,radius,radius,0,start,finish,true);
    g.setColour(ink.withAlpha(.14f)); g.strokePath(track,juce::PathStrokeType(3));
    juce::Path fill; fill.addCentredArc(c.x,c.y,radius,radius,0,start,angle,true);
    g.setColour(blue); g.strokePath(fill,juce::PathStrokeType(3,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    g.setColour(juce::Colour(0xffd6d6cc)); g.fillEllipse(c.x-radius+8,c.y-radius+10,2*radius-16,2*radius-16);
    g.setColour(juce::Colour(0xfff7f5ef)); g.fillEllipse(c.x-radius+8,c.y-radius+7,2*radius-16,2*radius-16);
    g.setColour(ink.withAlpha(.17f)); g.drawEllipse(c.x-radius+8,c.y-radius+7,2*radius-16,2*radius-16,1);
    const auto end=c.getPointOnCircumference(radius-15,angle);
    const auto inner=c.getPointOnCircumference(radius*.35f,angle);
    g.setColour(ink); g.drawLine({inner,end},3);
}
TideEditor::TideEditor(TideProcessor& p):AudioProcessorEditor(p),instrument(p),
    keyboard(p.keyboardState,juce::MidiKeyboardComponent::horizontalKeyboard) {
    setLookAndFeel(&look);
    for(size_t i=0;i<knobs.size();++i) {
        auto& k=knobs[i]; addAndMakeVisible(k);
        k.setName(names[i]); k.setTooltip(tips[i]);
        k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle(juce::Slider::TextBoxBelow,false,92,23);
        k.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
        attachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters,ids[i],k);
        if(i==3||i==4||i==6) {
            k.textFromValueFunction=[](double v){return v<1?juce::String(juce::roundToInt(v*1000))+" ms":juce::String(v,2)+" s";};
            k.valueFromTextFunction=[](const juce::String& s){return s.containsIgnoreCase("ms")?s.getDoubleValue()/1000:s.getDoubleValue();};
        } else if(i==8) {
            k.textFromValueFunction=[](double v){return juce::String(v,1)+" dB";};
        } else {
            k.textFromValueFunction=[](double v){return juce::String(juce::roundToInt(v*100))+"%";};
            k.valueFromTextFunction=[](const juce::String& s){return s.getDoubleValue()/100;};
        }
        k.updateText();
    }
    addAndMakeVisible(preset); preset.setName("Preset");
    for(int i=0;i<tide::patchCount;++i) {if(i==6)preset.addSeparator();preset.addItem(tide::patches[(size_t)i].name,i+1);}
    preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);
    preset.onChange=[this]{if(preset.getSelectedId()>0) instrument.setCurrentProgram(preset.getSelectedId()-1); keyboard.grabKeyboardFocus();};
    addAndMakeVisible(spaceMode); spaceMode.setName("Space character");
    spaceMode.addItem("Close",1); spaceMode.addItem("Bloom",2);
    spaceMode.setTooltip("Close: short reflections. Bloom: a wider, gradually building tail.");
    modeAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"spaceMode",spaceMode);
    addAndMakeVisible(articulation);articulation.setName("Voice response");
    articulation.addItemList({"Flow","Pluck","Knock","Metal"},1);
    articulation.setTooltip("Voice for new notes. Pluck, Knock and Metal ring out on their own when Sustain is zero; Decay sets their length.");
    articulationAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters,"articulation",articulation);
    addAndMakeVisible(panicButton); panicButton.setTooltip(TIDE_EFFECT_HOST?"Silence voices and effects until the next note.":"Stop all voices and clear the space.");
    panicButton.onClick=[this]{instrument.panic(); instrument.keyboardState.allNotesOff(0); keyboard.grabKeyboardFocus();};
    addAndMakeVisible(keyboard); keyboard.setAvailableRange(24,96);
    keyboard.setKeyPressBaseOctave(4); keyboard.setKeyWidth(34); keyboard.setWantsKeyboardFocus(true);
    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId,juce::Colour(0xfffaf8f1));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId,ink);
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId,blue.withAlpha(.65f));
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,blue.withAlpha(.2f));
#if TIDE_EFFECT_HOST
    for(auto* c:std::initializer_list<juce::Component*>{&effectPicker,&effectEditor,&effectBypass,&internalSpace,&refreshEffects})addAndMakeVisible(c);
    effectPicker.setName("External effect");effectPicker.setTooltip("Choose an installed stereo VST3 effect.");
    effectPicker.onChange=[this]{
        int i=effectPicker.getSelectedId();
        if(i==1)instrument.effects.unload();
        else if(i>=2&&i-2<effectFiles.size())instrument.effects.loadFile(effectFiles[i-2]);
    };
    effectEditor.onClick=[this]{instrument.effects.openEditor();};
    effectBypass.onClick=[this]{instrument.effects.setBypassed(effectBypass.getToggleState());};
    internalSpace.onClick=[this]{instrument.effects.setInternalSpace(internalSpace.getToggleState());};
    refreshEffects.onClick=[this]{refreshEffectList();};
    internalSpace.setTooltip("Include Tide's Close/Bloom space before the external effect.");
    effectBypass.setTooltip("Compare with the dry signal, compensating for the effect's reported latency.");
    effectBypass.setColour(juce::ToggleButton::textColourId,ink);internalSpace.setColour(juce::ToggleButton::textColourId,ink);
    refreshEffectList();
#endif
    setResizable(true,true); setResizeLimits(884,(int)(canvasHeight*.85f),1456,(int)(canvasHeight*1.4f)); getConstrainer()->setFixedAspectRatio(1040./canvasHeight);
    setSize(1040,(int)canvasHeight); keyboard.setLowestVisibleKey(48); startTimerHz(20);timerCallback();
}
TideEditor::~TideEditor() {setLookAndFeel(nullptr);}
void TideEditor::resized() {
    const float sx=getWidth()/1040.f, sy=getHeight()/canvasHeight;
    auto rect=[&](int x,int y,int w,int h){return juce::Rectangle<int>(juce::roundToInt(x*sx),juce::roundToInt(y*sy),juce::roundToInt(w*sx),juce::roundToInt(h*sy));};
    constexpr int xs[]={52,220,388,52,220,388,556,764,824};
    for(size_t i=0;i<knobs.size();++i) knobs[i].setBounds(rect(xs[i],i<3||i==7?168:388,132,144));
    preset.setBounds(rect(644,36,250,36)); panicButton.setBounds(rect(916,36,78,36));
    spaceMode.setBounds(rect(650,149,290,31));
    articulation.setBounds(rect(334,138,210,27));
    keyboard.setBounds(rect(36,TIDE_EFFECT_HOST?700:580,968,96)); keyboard.setKeyWidth(34*sx);
#if TIDE_EFFECT_HOST
    effectPicker.setBounds(rect(52,615,340,32));effectEditor.setBounds(rect(410,615,108,32));
    effectBypass.setBounds(rect(542,615,98,32));internalSpace.setBounds(rect(658,615,132,32));refreshEffects.setBounds(rect(886,615,102,32));
#endif
}
void TideEditor::paint(juce::Graphics& g) {
    g.fillAll(paper); g.addTransform(juce::AffineTransform::scale(getWidth()/1040.f,getHeight()/canvasHeight));
    g.setColour(ink); g.setFont(juce::FontOptions(38.f,juce::Font::bold)); g.drawText("TIDE",38,25,210,48,juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(12.f)); g.drawText("AN EXPRESSIVE INSTRUMENT",40,76,320,20,juce::Justification::centredLeft);
    g.setColour(ink.withAlpha(.17f)); g.drawLine(36,112,1004,112);
    g.setColour(juce::Colour(0xfff0ede5)); g.fillRoundedRectangle(36,130,526,198,9);
    g.fillRoundedRectangle(586,130,418,198,9); g.fillRoundedRectangle(36,350,968,195,9);
    g.setColour(ink); g.setFont(juce::FontOptions(11.f,juce::Font::bold));
    g.drawText("VOICE",52,141,100,20,juce::Justification::centredLeft);
    g.drawText("SPACE",604,141,100,20,juce::Justification::centredLeft);
    g.drawText("ENVELOPE",52,361,200,20,juce::Justification::centredLeft);
    g.drawText("LEVEL",824,361,150,20,juce::Justification::centredLeft);
    constexpr int xs[]={52,220,388,52,220,388,556,764,824};
    for(size_t i=0;i<knobs.size();++i) {g.setFont(juce::FontOptions(13.f)); g.drawText(names[i],xs[i],i<3||i==7?306:526,132,19,juce::Justification::centred);}
    const bool bloom=spaceMode.getSelectedId()==2;
    g.setColour(blue.withAlpha(.65f)); juce::Path wave;
    for(int i=0;i<130;++i) {
        const float x=608.f+i, y=246.f+std::sin(i*.32f)*std::exp(-i/(bloom?90.f:28.f))*25;
        if(i==0)wave.startNewSubPath(x,y);else wave.lineTo(x,y);
    }
    g.strokePath(wave,juce::PathStrokeType(1.5f));
    g.setColour(ink.withAlpha(.65f)); g.setFont(juce::FontOptions(11.f));
    g.drawText(bloom?"A little further out.":"Keep the room close.",606,281,145,19,juce::Justification::centredLeft);
    g.setColour(ink); g.setFont(juce::FontOptions(12.f));
    const auto description=juce::String(tide::patches[(size_t)instrument.getCurrentProgram()].description)+(instrument.programModified()?"  |  Modified":"");
    g.drawText(description,38,550,750,23,juce::Justification::centredLeft);
    g.setColour(ink.withAlpha(.65f)); g.setFont(juce::FontOptions(11.f));
    const int footer=TIDE_EFFECT_HOST?803:683;
    g.drawText("MIDI keyboard or A W S E D F T G Y H U J K  |  Mod wheel: motion  |  Pitch bend: +/-2",38,footer,850,20,juce::Justification::centredLeft);
    g.drawText(TIDE_EFFECT_HOST?"TIDE + FX":"STUDY 01",903,footer,100,20,juce::Justification::centredRight);
#if TIDE_EFFECT_HOST
    g.setColour(juce::Colour(0xfff0ede5));g.fillRoundedRectangle(36,580,968,100,9);
    g.setColour(ink);g.setFont(juce::FontOptions(11.f,juce::Font::bold));g.drawText("EXTERNAL EFFECT",52,590,240,18,juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(11.f));g.setColour(ink.withAlpha(.7f));g.drawText(instrument.effects.status(),52,651,930,21,juce::Justification::centredLeft);
#endif
    const float meter=juce::jlimit(0.f,1.f,instrument.peak.load());
    g.setColour(ink.withAlpha(.12f)); g.fillRoundedRectangle(931,82,63,5,2);
    g.setColour(meter>.9f?accent:blue); g.fillRoundedRectangle(931,82,63*std::sqrt(meter),5,2);
    g.setFont(juce::FontOptions(10.f)); g.setColour(ink.withAlpha(.6f));
    g.drawText(juce::String(instrument.sounding.load())+" / 8 voices",816,78,95,15,juce::Justification::centredRight);
}
void TideEditor::timerCallback() {
    const int id=instrument.getCurrentProgram()+1;
    if(preset.getSelectedId()!=id) preset.setSelectedId(id,juce::dontSendNotification);
#if TIDE_EFFECT_HOST
    const bool busy=instrument.effects.busy();effectPicker.setEnabled(!busy);refreshEffects.setEnabled(!busy);
    effectEditor.setEnabled(instrument.effects.loaded());effectBypass.setEnabled(instrument.effects.loaded());
    effectBypass.setToggleState(instrument.effects.bypassed(),juce::dontSendNotification);
    internalSpace.setToggleState(instrument.effects.usesInternalSpace(),juce::dontSendNotification);
    spaceMode.setEnabled(instrument.effects.usesInternalSpace());knobs[7].setEnabled(instrument.effects.usesInternalSpace());
    if(!busy) {
        int selected=1;const auto path=instrument.effects.path();
        for(int i=0;i<effectFiles.size();++i)if(effectFiles[i].getFullPathName()==path)selected=i+2;
        effectPicker.setSelectedId(selected,juce::dontSendNotification);
    }
#endif
    repaint();
}
#if TIDE_EFFECT_HOST
void TideEditor::refreshEffectList() {
    effectFiles=EffectHost::installedBundles();effectPicker.clear(juce::dontSendNotification);effectPicker.addItem("No external effect",1);
    for(int i=0;i<effectFiles.size();++i)effectPicker.addItem(effectFiles[i].getFileNameWithoutExtension(),i+2);
    effectPicker.setSelectedId(1,juce::dontSendNotification);
}
#endif
