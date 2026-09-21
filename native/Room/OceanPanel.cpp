#include "OceanPanel.h"
namespace {
const juce::Colour paper{0xff0b1016},ink{0xffd7e2e8},panel{0xff111b23};
void text(juce::Graphics& g,const juce::String& value,juce::Rectangle<int> r,float size=13){g.setColour(ink);g.setFont(juce::FontOptions(size));g.drawText(value,r,juce::Justification::centredLeft);}
void slider(juce::Slider& s){s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,72,25);s.setNumDecimalPlacesToDisplay(2);}
}
OceanPanel::OceanPanel(RoomProcessor& p,TowerRenderer* sharedTowers):owner(p),ownedTowers(sharedTowers?nullptr:std::make_unique<TowerRenderer>()),towers(sharedTowers?*sharedTowers:*ownedTowers) {
    addAndMakeVisible(power);power.setClickingTogglesState(true);power.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xff467e8e));
    power.setTooltip("Enable all ocean patches. Off returns to the base sound; the oceans continue to respond to the towers.");
    powerAttachment=std::make_unique<BA>(p.parameters,"oceanOn",power);
    addAndMakeVisible(audition);audition.onClick=[this]{owner.oceanAudition();};
    audition.setTooltip("Replace the three voices, six position LFOs and ocean routes with an orbiting ring/fold patch. Press Play to hear it. Positions, rhythm, tape and room stay as set.");
    addAndMakeVisible(collision);collision.onClick=[this]{owner.collisionAudition();};
    collision.setTooltip("Replace the voices, positions, position LFOs and routes with a collision patch. Press Play. Impacts modulate ring depth and modulator ratio.");
    const char* ids[]={"oceanGravity","oceanRate","oceanDamping","oceanMass0","oceanMass1","oceanMass2"};
    const char* names[]={"Gravity","Slosh rate","Damping","Tower 1 mass","Tower 2 mass","Tower 3 mass"};
    for(size_t i=0;i<controls.size();++i){auto& s=controls[i];slider(s);s.setName(names[i]);addAndMakeVisible(s);attachments[i]=std::make_unique<SA>(p.parameters,ids[i],s);}
    controls[1].setTextValueSuffix(" Hz");
    controls[0].setTooltip("Strength of the attraction. Zero lets the water settle back to the tower centre.");
    controls[1].setTooltip("Natural rate of the ocean's slosh. Moving towers keep exciting it; fixed towers eventually settle.");
    controls[2].setTooltip("Low damping lets the water overshoot and ring. Higher damping settles more directly.");
    for(int i=3;i<6;++i)controls[(size_t)i].setTooltip("How strongly this tower pulls the other oceans. Zero removes its pull. Muting a voice does not remove its gravity.");
    for(int i=0;i<tide::room::oceanRoutes;++i){auto& row=rows[(size_t)i];
        addAndMakeVisible(row.source);addAndMakeVisible(row.target);addAndMakeVisible(row.amount);
        for(int j=0;j<tide::room::oceanSourceCount;++j)row.source.addItem(tide::room::oceanSources[j],j+1);
        for(int j=0;j<13;++j)row.target.addItem(tide::room::oceanTargets[j],j+1);
        const auto prefix="Ocean patch "+juce::String(i+1)+" ";row.source.setName(prefix+"source");row.target.setName(prefix+"destination");row.amount.setName(prefix+"amount");slider(row.amount);
        row.sourceAttachment=std::make_unique<CA>(p.parameters,RoomProcessor::oceanId(i,"source"),row.source);
        row.targetAttachment=std::make_unique<CA>(p.parameters,RoomProcessor::oceanId(i,"target"),row.target);
        row.amountAttachment=std::make_unique<SA>(p.parameters,RoomProcessor::oceanId(i,"amount"),row.amount);
        row.amount.textFromValueFunction=[](double v){return (v>0?"+":juce::String{})+juce::String(v*100,0)+"%";};
        row.amount.valueFromTextFunction=[](const juce::String& v){return v.getDoubleValue()/100.;};row.amount.updateText();
        row.source.setTooltip("Ocean X/Y is bipolar displacement. Impact is a 0-1 collision pulse with a 250 ms decay. Any source can control any voice.");
        row.target.setTooltip("Brightness changes folding and gate brightness. Ring depth and modulator ratio affect Ring, AM and Complex voices. Decay changes note length. Routes add before the destination is limited.");
        row.amount.setTooltip("Signed modulation around the base sound. Full amount spans one brightness/ring-depth unit, or two octaves of ratio/decay. Negative amounts reverse the movement.");
    }
    setSize(904,676);startTimerHz(25);timerCallback();
}
void OceanPanel::resized() {
    collision.setBounds(24,52,153,29);
    power.setBounds(582,18,126,30);audition.setBounds(722,18,158,30);
    for(int i=0;i<3;++i){controls[(size_t)i].setBounds(24+i*292,111,272,28);controls[(size_t)i+3].setBounds(34+i*292,235,160,28);}
    for(size_t i=0;i<rows.size();++i){const int y=307+(int)i*43;auto& r=rows[i];r.source.setBounds(24,y,244,29);r.target.setBounds(282,y,304,29);r.amount.setBounds(610,y,270,29);}
}
void OceanPanel::paint(juce::Graphics& g) {
    g.fillAll(paper);text(g,"TIDE ROOM / OCEANS",{24,16,540,32},21);

    const char* labels[]={"Gravity","Slosh rate","Damping"};
    for(int i=0;i<3;++i){const int x=24+i*292;text(g,labels[i],{x,88,272,22});
        g.setColour(panel);g.fillRoundedRectangle((float)x,154,272,120,8);
        g.setColour(juce::Colour(0xff111b23));g.fillRoundedRectangle((float)x+168,159,99,110,7);
        text(g,"Tower "+juce::String(i+1)+" / mass",{x+12,162,200,22});
        const float sx=owner.oceanSignals[(size_t)i*2].load(),sy=owner.oceanSignals[(size_t)i*2+1].load();
        text(g,"X "+juce::String(sx,2)+"     Y "+juce::String(sy,2),{x+12,190,158,23},13);
        text(g,"Impact "+juce::String(owner.impactSignals[(size_t)i].load(),2),{x+12,213,150,20},11);
        const auto pos=owner.movingPosition(i);const SphericalWater::Vec physical{pos.lateral*owner.get("roomWidth")*.5f,pos.depth*owner.get("roomDepth"),pos.height};
        ListenerSpace view;view.width=owner.get("roomWidth");view.depth=owner.get("roomDepth");view.height=owner.get("roomHeight");
        towers.draw(g,owner,i,physical,view,{(float)x+218,214},36,.04);


    }
    text(g,"OCEAN OUTPUT",{24,279,244,23},12);text(g,"PATCH TO",{282,279,304,23},12);text(g,"AMOUNT",{610,279,270,23},12);
    text(g,"Ring depth and ratio work on Ring, AM and Complex voices. Brightness and decay work on every voice.",{24,578,864,23},12);
    text(g,"X = right / Y = away. Fixed towers settle to steady voltages; position LFOs keep the oceans moving.",{24,604,864,23},12);
    text(g,"Try orbit patch replaces the voices, position LFOs and ocean patches. Press Play to start the motion.",{24,630,864,23},12);
}
void OceanPanel::timerCallback(){power.setButtonText(owner.get("oceanOn")>.5f?"Patches on":"Patches off");repaint();}
