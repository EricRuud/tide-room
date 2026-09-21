#pragma once
#include "RoomProcessor.h"

class HarmonyKeys final : public juce::KeyListener {
public:
    explicit HarmonyKeys(RoomProcessor& p):owner(p){}
    bool keyPressed(const juce::KeyPress& key,juce::Component* origin) override {
        auto* focus=juce::Component::getCurrentlyFocusedComponent();
        return handle(key,focus?focus:origin);
    }
    bool handle(const juce::KeyPress& key,juce::Component* focus) {
        // Editing a number or name must never change the music. Native menus
        // and the hosted plugin keep their own keyboard handling.
        if(focus&&(dynamic_cast<juce::TextEditor*>(focus)||focus->findParentComponentOfClass<juce::TextEditor>()))return false;
        const auto modifiers=key.getModifiers();
        if(modifiers.isCommandDown()||modifiers.isCtrlDown()||modifiers.isAltDown())return false;
        const int code=key.getKeyCode();
        if(!modifiers.isShiftDown()){
            const int pads[]={juce::KeyPress::numberPad1,juce::KeyPress::numberPad2,juce::KeyPress::numberPad3,juce::KeyPress::numberPad4,juce::KeyPress::numberPad5,juce::KeyPress::numberPad6,juce::KeyPress::numberPad7};
            for(int i=0;i<7;++i)if(code=='1'+i||code==pads[i]){select("harmonyChord",i);return true;}
        }
        const auto letter=juce::CharacterFunctions::toUpperCase((juce::juce_wchar)code);
        if(!modifiers.isShiftDown())for(size_t i=0;i<tide::room::patternBank.size();++i)
            if(letter==(juce::juce_wchar)tide::room::patternBank[i].shortcut){select("patternBank",(int)i);return true;}
        if(letter>='A'&&letter<='G'){
            constexpr int natural[]={9,11,0,2,4,5,7};
            select("harmonyKey",(natural[letter-'A']+(modifiers.isShiftDown()?1:0))%12);return true;
        }
        return false;
    }
private:
    void select(const char* id,int value){if((int)owner.get(id)!=value)owner.set(id,(float)value);}
    RoomProcessor& owner;
};

class HarmonyControls final : public juce::Component,private juce::Timer {
public:
    explicit HarmonyControls(RoomProcessor& p):owner(p){
        addAndMakeVisible(key);addAndMakeVisible(scale);key.setName("Global key");scale.setName("Global scale");
        key.addItemList({"C","C# / Db","D","D# / Eb","E","F","F# / Gb","G","G# / Ab","A","A# / Bb","B"},1);
        scale.addItemList({"Major","Minor"},1);
        key.setTooltip("A-G select the matching key. Shift + letter selects its sharp. Existing notes finish naturally.");
        scale.setTooltip("Major or natural minor. The chord buttons follow this scale.");
        keyAttachment=std::make_unique<Attachment>(owner.parameters,"harmonyKey",key);
        scaleAttachment=std::make_unique<Attachment>(owner.parameters,"harmonyScale",scale);
        for(size_t i=0;i<chords.size();++i){auto& b=chords[i];addAndMakeVisible(b);b.setClickingTogglesState(true);b.setRadioGroupId(1);b.setColour(juce::TextButton::buttonColourId,juce::Colours::transparentBlack);b.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xffd7e2e8));b.setColour(juce::TextButton::textColourOffId,juce::Colour(0xffd7e2e8));b.setColour(juce::TextButton::textColourOnId,juce::Colour(0xffd7e2e8));b.onClick=[this,i]{owner.set("harmonyChord",(float)i);};}
        for(size_t i=0;i<patterns.size();++i){auto& b=patterns[i];const auto& definition=tide::room::patternBank[i];addAndMakeVisible(b);b.setClickingTogglesState(true);b.setRadioGroupId(2);b.setColour(juce::TextButton::buttonColourId,juce::Colours::transparentBlack);b.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xff28353e));b.setColour(juce::TextButton::textColourOffId,juce::Colour(0xffd7e2e8));b.setColour(juce::TextButton::textColourOnId,juce::Colour(0xffd7e2e8));const auto shortcut=juce::String::charToString(definition.shortcut);b.setButtonText(shortcut+"  "+definition.name);b.setName("Pattern "+juce::String(definition.name)+", "+shortcut);b.setTooltip(juce::String(definition.description)+" Shortcut: "+shortcut+". Existing notes and tails continue.");b.onClick=[this,i]{owner.set("patternBank",(float)i);};}
        refresh();startTimerHz(25);
    }
    ~HarmonyControls() override {stopTimer();setLookAndFeel(nullptr);}
    static juce::String chordName(tide::room::Harmony h){
        constexpr const char* sharps[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        constexpr const char* flats[]={"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};
        const bool flatKey=h.minor?(h.key==0||h.key==2||h.key==3||h.key==5||h.key==7||h.key==10):(h.key==1||h.key==3||h.key==5||h.key==8||h.key==10);
        const juce::String root=(flatKey?flats:sharps)[h.chordRoot()];
        return root+(h.fifth()==6?"dim":h.third()==3?"m":"");
    }
    void refresh(){
        const int bank=(int)owner.get("patternBank");if(bank!=lastPattern){lastPattern=bank;for(size_t i=0;i<patterns.size();++i)patterns[i].setToggleState((int)i==bank,juce::dontSendNotification);}
        const int root=(int)owner.get("harmonyKey"),minor=(int)owner.get("harmonyScale"),selected=(int)owner.get("harmonyChord");
        if(root==lastKey&&minor==lastScale&&selected==lastChord)return;
        lastKey=root;lastScale=minor;lastChord=selected;
        for(int i=0;i<7;++i){auto& b=chords[(size_t)i];const auto name=chordName({root,minor,i});b.setButtonText(juce::String(i+1)+"  "+name);b.setName("Chord "+juce::String(i+1)+", "+name);b.setTooltip("Press "+juce::String(i+1)+" for "+name+". Patterns change on their next notes; tails continue.");b.setToggleState(i==selected,juce::dontSendNotification);}
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(0xff84949f));g.setFont(juce::FontOptions(11.f));g.drawText("Key",0,0,35,26,juce::Justification::centredLeft);
        g.drawText("Pattern",0,37,64,26,juce::Justification::centredLeft);
    }
    void resized() override {key.setBounds(37,0,87,26);scale.setBounds(138,0,112,26);
        const float chordWidth=(float)(getWidth()-278)/7;for(int i=0;i<7;++i)chords[(size_t)i].setBounds(278+(int)(i*chordWidth),0,(int)chordWidth-7,26);
        const float patternWidth=(float)(getWidth()-80)/8;for(int i=0;i<8;++i)patterns[(size_t)i].setBounds(80+(int)(i*patternWidth),37,(int)patternWidth-7,26);
    }

private:
    void timerCallback() override {refresh();}
    RoomProcessor& owner;
    juce::ComboBox key,scale;
    std::array<juce::TextButton,7> chords;
    std::array<juce::TextButton,8> patterns;
    using Attachment=juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<Attachment> keyAttachment,scaleAttachment;
    int lastKey=-1,lastScale=-1,lastChord=-1,lastPattern=-1;
};
