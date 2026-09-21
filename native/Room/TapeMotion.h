#pragma once
#include <array>
#include <cmath>
#include <algorithm>

namespace tide::room {
// Dominant shared trajectory measured from P821 1.5.0, 900/30, Slow mode.
// Wow changes excursion; Flutter changes its rate and slightly its excursion.
// Above 100% is our continuous extension, not a measured reference setting.
class TapeMotion {
public:
    void prepare(double sampleRate) noexcept {rate=sampleRate;reset();}
    void reset() noexcept {phase=0;current={0,1.88};target=current;step.fill(0);remaining.fill(0);}
    void set(bool enabled,double wow,double flutter) noexcept {
        wow=std::clamp(wow,0.,4.);flutter=std::clamp(flutter,0.,4.);
        const double depth=lookup(wow,{1.9949350722,2.4853423349,3.0966688518,3.8604620108});
        const double scale=lookup(flutter,{1.,1.9696872761/1.9949350722,1.9323897182/1.9949350722,1.8811151696/1.9949350722});
        const std::array<double,2> values{enabled?depth*scale:0.,lookup(flutter,{1.88,2.1608,2.5352,3.0032})};
        for(size_t i=0;i<2;++i)if(values[i]!=target[i]){target[i]=values[i];remaining[i]=std::max(1,(int)(rate*.2));step[i]=(target[i]-current[i])/remaining[i];}
    }
    double next() noexcept {
        for(size_t i=0;i<2;++i)if(remaining[i]>0){current[i]+=step[i];if(--remaining[i]==0)current[i]=target[i];}
        const double result=current[0]==0?0:current[0]*std::sin(phase);
        phase+=2*pi*current[1]/rate;if(phase>=2*pi)phase-=2*pi;
        return result;
    }
private:
    static double lookup(double x,const std::array<double,4>& values) noexcept {
        constexpr std::array<double,4> positions{0,.1,.5,1};const int i=x<.1?0:x<.5?1:2;
        const double fraction=(x-positions[(size_t)i])/(positions[(size_t)i+1]-positions[(size_t)i]);
        return values[(size_t)i]+fraction*(values[(size_t)i+1]-values[(size_t)i]);
    }
    static constexpr double pi=3.14159265358979323846;
    double rate=48000,phase=0;
    std::array<double,2> current{},target{},step{};
    std::array<int,2> remaining{};
};
}
