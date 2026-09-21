#include "MotionPanel.h"
namespace {
const juce::Colour paper{0xff0b1016},ink{0xffd7e2e8},panel{0xff111b23};
void text(juce::Graphics& g,const juce::String& s,juce::Rectangle<int> r,float size=12){g.setColour(ink);g.setFont(juce::FontOptions(size));g.drawText(s,r,juce::Justification::centredLeft);}
void toggle(juce::TextButton& b){b.setClickingTogglesState(true);b.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff17222b));b.setColour(juce::TextButton::buttonOnColourId,ink);b.setColour(juce::TextButton::textColourOffId,ink);b.setColour(juce::TextButton::textColourOnId,ink);}
void slider(juce::Slider& s){s.setSliderStyle(juce::Slider::LinearHorizontal);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,76,25);}
}
MotionPanel::MotionPanel(RoomProcessor& p):owner(p) {
    addAndMakeVisible(roomMode);roomMode.addItem("Reverside fixed",1);roomMode.addItem("Moving reflections",2);
    roomMode.setName("Reflection renderer");roomMode.setTooltip("Moving reflections: smooth wall, floor and ceiling paths, with Reverside supplying the late tail. Reverside fixed: its original reflection character stays at each source's centre while headphone direct sound can move.");
#if TIDE_NATIVE_WOOD
    roomMode.changeItemText(1,"Tail only");
    roomMode.setTooltip("Moving reflections adds the moving wall, floor and ceiling paths. Tail only keeps the shared room tail and direct placement.");
#endif
    roomModeAttachment=std::make_unique<CA>(p.parameters,"movingReflections",roomMode);
    for(int i=0;i<tide::room::lfoCount;++i){auto& r=rows[(size_t)i];const auto id=[i](const char* field){return RoomProcessor::lfoId(i,field);};const auto name="LFO "+juce::String(i+1)+" ";
        for(juce::Component* c:std::array<juce::Component*,8>{&r.on,&r.sync,&r.target,&r.shape,&r.division,&r.rate,&r.depth,&r.phase})addAndMakeVisible(c);
        toggle(r.on);toggle(r.sync);r.on.setName(name+"power");r.sync.setName(name+"tempo sync");
        r.onAttachment=std::make_unique<BA>(p.parameters,id("on"),r.on);r.syncAttachment=std::make_unique<BA>(p.parameters,id("sync"),r.sync);
        for(int k=0;k<10;++k)if(k==0||k%3!=0)r.target.addItem(tide::room::motionTargets[k],k+1);
        r.shape.addItemList({"Sine","Triangle"},1);for(int k=0;k<7;++k)r.division.addItem(tide::room::motionDivisions[k],k+1);
        r.target.setName(name+"destination");r.shape.setName(name+"shape");r.division.setName(name+"cycle length");
        r.targetAttachment=std::make_unique<juce::ParameterAttachment>(*p.parameters.getParameter(id("target")),[&r](float value){const int destination=(int)value;r.target.setSelectedId(destination%3==0?1:destination+1,juce::dontSendNotification);},nullptr);
        r.target.onChange=[&r]{r.targetAttachment->setValueAsCompleteGesture((float)(r.target.getSelectedId()-1));};r.targetAttachment->sendInitialUpdate();r.shapeAttachment=std::make_unique<CA>(p.parameters,id("shape"),r.shape);r.divisionAttachment=std::make_unique<CA>(p.parameters,id("division"),r.division);
        slider(r.rate);slider(r.depth);slider(r.phase);r.rate.setName(name+"rate");r.depth.setName(name+"depth");r.phase.setName(name+"phase");
        r.rateAttachment=std::make_unique<SA>(p.parameters,id("rate"),r.rate);r.depthAttachment=std::make_unique<SA>(p.parameters,id("depth"),r.depth);r.phaseAttachment=std::make_unique<SA>(p.parameters,id("phase"),r.phase);
        r.rate.setTextValueSuffix(" Hz");r.rate.setNumDecimalPlacesToDisplay(3);r.phase.setTextValueSuffix(" deg");r.phase.setNumDecimalPlacesToDisplay(1);
        r.depth.textFromValueFunction=[](double v){return juce::String(v*100,1)+"%";};r.depth.valueFromTextFunction=[](const juce::String& v){return v.getDoubleValue()/100.;};r.depth.updateText();
        r.on.setTooltip("Enable this route. Switching off smoothly returns its contribution to zero.");r.sync.setTooltip("Use the scene tempo for one complete LFO cycle. Switch off for a free rate in Hz.");
        r.target.setTooltip("Move one instrument's direct sound and, in Moving reflections mode, its wall reflections. Reverside's geometry stays at the centre. Multiple LFOs on the same destination add together.");
#if TIDE_NATIVE_WOOD
        r.target.setTooltip("Move an instrument and its wall reflections. The source also moves within the input to the shared room tail. Routes on the same destination add together.");
#endif
        r.depth.setTooltip("Peak excursion as a percentage of half the axis range. Motion is limited at the room-map boundaries; lower depth if it lingers there.");
        r.phase.setTooltip("Starting phase. A 90-degree difference between left/right and depth gives a looping path with sine LFOs at the same rate.");
        r.rate.setTooltip("Cycles per second, from a 100-second drift to 4 Hz. Fast or deep distance movement can bend pitch.");
    }
    setSize(904,722);startTimerHz(25);timerCallback();
}
void MotionPanel::resized(){roomMode.setBounds(650,17,230,28);for(size_t i=0;i<rows.size();++i){auto& r=rows[i];const int y=84+(int)i*94;
    r.on.setBounds(28,y+10,100,27);r.target.setBounds(142,y+10,268,27);r.shape.setBounds(424,y+10,112,27);r.sync.setBounds(550,y+10,110,27);
    r.rate.setBounds(70,y+47,222,27);r.division.setBounds(78,y+47,206,27);r.depth.setBounds(358,y+47,198,27);r.phase.setBounds(626,y+47,247,27);
}}
void MotionPanel::paint(juce::Graphics& g){g.fillAll(paper);text(g,"MOTION / POSITION LFOs",{24,13,550,30},21);
    text(g,"Choose a destination, then switch an LFO on. Drag a source in the room map to move its centre.",{24,48,864,24},13);
    for(size_t i=0;i<rows.size();++i){const int y=84+(int)i*94;g.setColour(panel);g.fillRoundedRectangle(16.f,(float)y,872.f,86.f,9.f);
        text(g,owner.get(RoomProcessor::lfoId((int)i,"sync"))>.5f?"Cycle":"Rate",{28,y+48,46,25});text(g,"Depth",{310,y+48,48,25});text(g,"Phase",{580,y+48,46,25});
        const float value=owner.lfoSignals[i].load();g.setColour(ink.withAlpha(.18f));g.drawLine(686.f,(float)y+24,862.f,(float)y+24,2);g.drawVerticalLine(774,(float)y+17,(float)y+31);
        g.setColour(ink.withAlpha(owner.get(RoomProcessor::lfoId((int)i,"on"))>.5f?1.f:.25f));g.fillEllipse(770.f+value*88,(float)y+20,8,8);
    }
    text(g,"Play restarts the phases. Stop holds them. Routes add together; positions stay inside the map.",{24,654,864,21},12);
    text(g,"Deep or fast motion can create Doppler pitch bends. Depth also changes distance.",{24,675,864,19},12);
#if TIDE_NATIVE_WOOD
    text(g,owner.get("movingReflections")>.5f?"Moving wall reflections + one shared room tail.":"Direct placement + one shared room tail.",{24,695,864,19},12);
#else
    text(g,owner.get("movingReflections")>.5f?"Moving reflections + Reverside late tail. Reverside's geometry stays fixed during LFO motion.":"Reverside reflections stay fixed. Direct motion remains available in Headphones placement.",{24,695,864,19},12);
#endif
}
void MotionPanel::timerCallback(){for(size_t i=0;i<rows.size();++i){auto& r=rows[i];r.on.setButtonText("LFO "+juce::String((int)i+1)+(owner.get(RoomProcessor::lfoId((int)i,"on"))>.5f?" on":" off"));const bool sync=owner.get(RoomProcessor::lfoId((int)i,"sync"))>.5f;r.sync.setButtonText(sync?"Sync on":"Sync off");r.rate.setVisible(!sync);r.division.setVisible(sync);}repaint();}
