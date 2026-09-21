#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

namespace tide::room {
class HrtfBank {
public:
    static constexpr int maxTaps=640;
    using Kernel=std::array<std::array<float,maxTaps>,2>;
    void prepare(double);
    void interpolate(float azimuthRight,float elevation,Kernel&) const;
    int length() const {return taps;}
    const float* delayKernel(int phase) const {return delayTable[(size_t)phase].data();}
private:
    struct Direction {float elevation=0,azimuth=0;Kernel kernel{};};
    struct Row {float elevation=0;int start=0,count=0;};
    std::vector<Direction> directions;
    std::vector<Row> rows;
    int taps=128;
    std::array<std::array<float,12>,1025> delayTable{};
    void interpolateRow(const Row&,float,float,bool,Kernel&) const;
};

class BinauralSource {
public:
    void prepare(double,const HrtfBank&);
    void render(const float* mono,float* left,float* right,int count,float x,float y,float z,bool enabled,bool headphones=true);
    void propagation(juce::AudioBuffer<float>&,bool enabled,juce::AudioBuffer<float>* send=nullptr);
private:
    const HrtfBank* bank=nullptr;
    HrtfBank::Kernel current{},previous{},target{};
    std::array<float,HrtfBank::maxTaps*2> history{};
    static constexpr int delaySize=16384;
    std::array<std::array<float,delaySize>,4> delayed{};
    int index=0,delayIndex=0;
    double rate=48000;
    float azimuth=1000,elevation=1000;
    double delay=0,targetDelay=0,delayAim=0,delayStep=0,delaySmoothing=.001;
    bool initial=true;
    bool previousHeadphones=true;
    juce::SmoothedValue<float> gain,mix;
};
}
