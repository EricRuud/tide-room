#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace tide::glass {
inline const juce::Colour background{0xff0b1016},text{0xffd7e2e8},muted{0xff84949f},line{0xff28353e},surface{0xff111b23};
}
class GlassLook final : public juce::LookAndFeel_V4 {
public:
    GlassLook() {
        using namespace tide::glass;
        setColour(juce::Slider::thumbColourId,text);setColour(juce::Slider::trackColourId,text.withAlpha(.65f));setColour(juce::Slider::backgroundColourId,line);
        setColour(juce::Slider::textBoxTextColourId,text);setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);
        setColour(juce::ComboBox::backgroundColourId,background);setColour(juce::ComboBox::textColourId,text);setColour(juce::ComboBox::arrowColourId,muted);setColour(juce::ComboBox::outlineColourId,line);
        setColour(juce::PopupMenu::backgroundColourId,surface);setColour(juce::PopupMenu::textColourId,text);setColour(juce::PopupMenu::highlightedBackgroundColourId,line);setColour(juce::PopupMenu::highlightedTextColourId,juce::Colours::white);
        setColour(juce::TextButton::buttonColourId,juce::Colours::transparentBlack);setColour(juce::TextButton::buttonOnColourId,line);setColour(juce::TextButton::textColourOffId,text);setColour(juce::TextButton::textColourOnId,juce::Colours::white);
        setColour(juce::ToggleButton::textColourId,muted);setColour(juce::ToggleButton::tickColourId,text);setColour(juce::ToggleButton::tickDisabledColourId,line);
        setColour(juce::Label::textColourId,text);setColour(juce::TextEditor::backgroundColourId,surface);setColour(juce::TextEditor::textColourId,text);
        setColour(juce::TooltipWindow::backgroundColourId,surface);setColour(juce::TooltipWindow::textColourId,text);setColour(juce::TooltipWindow::outlineColourId,line);
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool over,bool down)override {
        const auto r=b.getLocalBounds().toFloat().reduced(.5f);
        if(b.getToggleState()||over||down){g.setColour(tide::glass::text.withAlpha(down?.16f:b.getToggleState()?.09f:.04f));g.fillRoundedRectangle(r,4);}
        g.setColour(tide::glass::text.withAlpha(b.isEnabled()?(over?.35f:.14f):.05f));g.drawRoundedRectangle(r,4,.6f);
        if(b.getToggleState()){g.setColour(tide::glass::text.withAlpha(.8f));g.drawLine(r.getX()+8,r.getBottom()-1,r.getRight()-8,r.getBottom()-1,1);}
    }
    juce::Font getTextButtonFont(juce::TextButton&,int height)override{return juce::Font(juce::FontOptions(std::min(13.f,(float)height*.46f)));}
    void drawComboBox(juce::Graphics& g,int width,int height,bool,int,int,int,int,juce::ComboBox&)override {
        g.setColour(tide::glass::line);g.drawLine(0,(float)height-1,(float)width,(float)height-1,.7f);
        juce::Path arrow;arrow.startNewSubPath((float)width-16,(float)height*.45f);arrow.lineTo((float)width-12,(float)height*.58f);arrow.lineTo((float)width-8,(float)height*.45f);
        g.setColour(tide::glass::muted);g.strokePath(arrow,juce::PathStrokeType(.9f));
    }
    juce::Font getComboBoxFont(juce::ComboBox&)override{return juce::Font(juce::FontOptions(13.f));}
    void drawTabButton(juce::TabBarButton& button,juce::Graphics& g,bool over,bool down)override{
        const auto area=button.getActiveArea().toFloat().reduced(2,0);const bool selected=button.isFrontTab();
        if(selected||over){g.setColour(tide::glass::text.withAlpha(down?.12f:selected?.07f:.035f));g.fillRoundedRectangle(area,3);}
        g.setColour(selected?tide::glass::text:tide::glass::muted);g.setFont(juce::FontOptions(12.f));g.drawText(button.getButtonText(),area,juce::Justification::centred);
        if(selected){g.setColour(tide::glass::text.withAlpha(.65f));g.drawLine(area.getX()+8,area.getBottom()-1,area.getRight()-8,area.getBottom()-1,1);}
    }
    void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,float pos,float,float,juce::Slider::SliderStyle style,juce::Slider& slider)override {
        if(style==juce::Slider::LinearVertical){const float xx=(float)x+(float)width*.5f;g.setColour(tide::glass::line);g.drawLine(xx,(float)y,xx,(float)(y+height),2);
            g.setColour(tide::glass::text.withAlpha(.6f));g.drawLine(xx,pos,xx,(float)(y+height),2);g.setColour(tide::glass::text);g.fillRoundedRectangle(xx-7,pos-2,14,4,1);return;}
        const float yy=(float)y+(float)height*.5f;g.setColour(tide::glass::line);g.drawLine((float)x,yy,(float)(x+width),yy,1);
        g.setColour(slider.findColour(juce::Slider::trackColourId).withMultipliedAlpha(slider.isEnabled()?.75f:.3f));g.drawLine((float)x,yy,pos,yy,1);
        g.setColour(tide::glass::text.withAlpha(slider.isEnabled()?.9f:.25f));g.fillEllipse(pos-2.5f,yy-2.5f,5,5);
    }
    void drawToggleButton(juce::Graphics& g,juce::ToggleButton& b,bool over,bool)override {
        const float y=(float)b.getHeight()*.5f;g.setColour(tide::glass::muted.withAlpha(over?1.f:.65f));g.drawEllipse(2,y-3,6,6,.7f);
        if(b.getToggleState()){g.setColour(tide::glass::text);g.fillEllipse(3,y-2,4,4);}
        g.setFont(juce::FontOptions(12.f));g.setColour(tide::glass::text.withAlpha(b.getToggleState()?1.f:.7f));g.drawText(b.getButtonText(),14,0,b.getWidth()-14,b.getHeight(),juce::Justification::centredLeft);
    }
};
