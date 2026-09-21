#pragma once
#include "RoomSceneRenderer.h"

class ImmersiveView final : public juce::Component,private juce::Timer {
public:
    ImmersiveView(RoomProcessor& processor,std::function<void()> leave):scene(processor),onExit(std::move(leave)) {
        setOpaque(true);setWantsKeyboardFocus(true);
        addAndMakeVisible(exitButton);exitButton.setName("Exit fullscreen");
        exitButton.setTooltip("Return to the room controls. Escape also exits fullscreen.");
        exitButton.onClick=[this]{onExit();};
        setSize(1280,800);startTimerHz(25);
    }
    void paint(juce::Graphics& g)override{renderer.draw(g,scene,space());}
    void resized()override{exitButton.setBounds(std::max(12,getWidth()-192),20,172,32);}
    bool keyPressed(const juce::KeyPress& key)override{
        if(key.getKeyCode()==juce::KeyPress::escapeKey){onExit();return true;}return false;
    }
    ListenerSpace space()const{
        ListenerSpace view;view.fromListener=true;view.width=scene.get("roomWidth");view.depth=scene.get("roomDepth");view.height=scene.get("roomHeight");
        view.bounds=getLocalBounds().toFloat();view.centre=view.bounds.getCentre();return view;
    }
    void advancePreview(double seconds){renderer.advancePreview(scene,space(),seconds);}
private:
    RoomProcessor& scene;
    std::function<void()> onExit;
    RoomSceneRenderer renderer;
    juce::TextButton exitButton{"Exit fullscreen  /  Esc"};
    void timerCallback()override{if(isShowing())repaint();}
};
