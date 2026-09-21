#pragma once
#include "RoomProcessor.h"
#include <atomic>

// Whole-scene presets: voice/room/tape/tides/visual parameters, not device setup.
class RoomPresets final : private juce::AudioProcessorValueTreeState::Listener {
public:
    struct Entry {juce::String id,name;juce::File file;bool factory=false;};
    explicit RoomPresets(RoomProcessor&,juce::File directory={});
    ~RoomPresets() override;
    static juce::ValueTree slowTides();
    std::vector<Entry> entries() const;
    juce::Result load(const Entry&);
    juce::Result saveAs(juce::String name);
    juce::Result importFile(const juce::File&);
    juce::String currentName()const;
    juce::String currentId()const;
    bool modified()const{return dirty.load();}
    void persistMetadata();
    const juce::File& directory()const{return folder;}
private:
    RoomProcessor& processor;juce::File folder;
    std::atomic<bool> dirty{false},loading{false};
    void parameterChanged(const juce::String&,float) override {if(!loading.load())dirty=true;}
    juce::Result read(const juce::File&,juce::ValueTree&)const;
    juce::Result validate(const juce::ValueTree&)const;
    juce::Result apply(juce::ValueTree,const Entry&);
    bool matchesFactory()const;
};
