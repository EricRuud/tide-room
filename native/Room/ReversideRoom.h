#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <map>
#include <optional>

namespace tide::room {
struct Position {float lateral=0,depth=.35f,height=1.3f;bool operator==(const Position& p)const{return lateral==p.lateral&&depth==p.depth&&height==p.height;}};
struct RoomSettings {float width=10,depth=8,height=4,decay=1.25f,tail=-9;std::array<Position,3> positions{{{-.48f,.30f,1.1f},{.42f,.45f,1.6f},{.08f,.62f,2.1f}}};bool headphones=true,movingReflections=false;bool operator==(const RoomSettings& s)const{return width==s.width&&depth==s.depth&&height==s.height&&decay==s.decay&&tail==s.tail&&headphones==s.headphones&&movingReflections==s.movingReflections&&positions==s.positions;}};
class ReversideRoom {
public:
    ReversideRoom();
    ~ReversideRoom();
    bool load(const juce::File&,double,int,juce::AudioPlayHead*,juce::String&);
    void prepare(double,int);
    void release();
    void reset();
    void process(int,juce::AudioBuffer<float>&);
    void configure(const RoomSettings&,bool first=false);
    void position(int,const Position&,bool headphones,bool notify=false);
    void openEditor();
    void closeEditor();
    void shareEditorChanges();
    juce::ValueTree saveState();
    void restoreState(const juce::ValueTree&);
    juce::AudioPluginInstance* instance(int i) const {return plugins[(size_t)i].get();}
    float value(int,const juce::String&) const;
    void set(int,const juce::String&,const juce::String&);
    int latency() const;
    juce::String description() const {return pluginDescription.name+" "+pluginDescription.version;}
private:
    struct Window;
    juce::VST3PluginFormat format;
    juce::PluginDescription pluginDescription;
    std::array<std::unique_ptr<juce::AudioPluginInstance>,3> plugins;
    std::array<std::map<juce::String,juce::AudioProcessorParameter*>,3> parameters;
    std::map<juce::String,float> lastMaster;
    std::optional<RoomSettings> configured;
    std::array<float,3> fixedErGain{{.25f,.25f,.25f}};
    bool smoothConfigured=false,forceNext=true,forceWrites=false;
    std::unique_ptr<Window> window;
    std::array<juce::MidiBuffer,3> noMidi;
    struct PositionControl {
        juce::AudioProcessorParameter* parameter=nullptr;
        std::array<float,1025> values{};
        float minimum=0,maximum=1;
        void prepare(juce::AudioProcessorParameter*,float,float);
        void apply(float,bool notify) const;
    };
    std::array<std::array<PositionControl,4>,3> positionControls;
    void normalized(int,const juce::String&,float);
    static bool managed(const juce::String&);
    static bool positionParameter(const juce::String&);
};
}
