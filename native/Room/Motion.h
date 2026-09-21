#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace tide::room {
constexpr int lfoCount=6;
constexpr const char* lfoFields[]={"on","target","shape","sync","rate","division","depth","phase"};
constexpr const char* motionTargets[]={"No destination","1 Wood / Left-right","1 Wood / Depth","1 Wood / Height","2 Pluck / Left-right","2 Pluck / Depth","2 Pluck / Height","3 Metal / Left-right","3 Metal / Depth","3 Metal / Height"};
constexpr const char* motionDivisions[]={"1/4 note","1/2 note","1 bar","2 bars","4 bars","8 bars","16 bars"};
constexpr double motionBeats[]={1,2,4,8,16,32,64};
constexpr float axisMin[]={-.8f,.1f,.3f},axisMax[]={.8f,.68f,2.2f};
struct LfoSettings {
    bool enabled=false; int target=0,shape=0; bool sync=false;
    float rate=.1f; int division=3; float depth=.25f,phase=0;
};
// Audio-clocked oscillators. Routing is separate from oscillation so further
// destinations can be added without teaching the oscillators about plugins.
class Motion {
public:
    using Settings=std::array<LfoSettings,lfoCount>;
    using Values=std::array<std::array<float,3>,3>;
    void prepare(double sr) { sampleRate=sr; phases.fill(0); offsets={}; signals.fill(0); }
    void restart() { phases.fill(0); } // Keep the smoothing history on transport restart.
    Values process(const Settings& settings,const Values& anchors,int count,double bpm,bool running) {
        Values target{};const double dt=count/sampleRate;
        for(size_t i=0;i<settings.size();++i) {
            const auto& s=settings[i];const double phase=wrap(phases[i]+s.phase/360.);
            const float wave=s.shape==1?(float)(1-4*std::abs(wrap(phase+.25)-.5)):(float)std::sin(phase*6.283185307179586);
            signals[i]=s.enabled?wave:0;
            if(s.enabled&&s.target>0&&s.target<=9) {
                const int dest=s.target-1,axis=dest%3;
                target[(size_t)(dest/3)][(size_t)axis]+=wave*s.depth*(axisMax[axis]-axisMin[axis])*.5f;
            }
            // Disabled oscillators still follow the transport; enabling a route
            // does not change the other oscillators' timing.
            if(running) phases[i]=wrap(phases[i]+dt*(s.sync?bpm/(60*motionBeats[std::clamp(s.division,0,6)]):s.rate));
        }
        const float follow=(float)-std::expm1(-dt/.05);Values result=anchors;
        for(size_t i=0;i<3;++i)for(size_t a=0;a<3;++a) {
            offsets[i][a]+=follow*(target[i][a]-offsets[i][a]);
            if(target[i][a]==0&&std::abs(offsets[i][a])<1.e-7f)offsets[i][a]=0;
            result[i][a]=std::clamp(anchors[i][a]+offsets[i][a],axisMin[a],axisMax[a]);
        }
        return result;
    }
    const std::array<double,lfoCount>& phaseValues()const{return phases;}
    const std::array<float,lfoCount>& signalValues()const{return signals;}
private:
    static double wrap(double p){return p-std::floor(p);}
    double sampleRate=48000;
    std::array<double,lfoCount> phases{};
    Values offsets{};
    std::array<float,lfoCount> signals{};
};
}
