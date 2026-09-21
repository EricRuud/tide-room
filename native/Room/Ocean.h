#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace tide::room {
constexpr int oceanRoutes=6, oceanSourceCount=9;
constexpr const char* oceanFields[]={"source","target","amount"};
constexpr const char* oceanSources[]={"1 Wood / Ocean X","1 Wood / Ocean Y","2 Pluck / Ocean X","2 Pluck / Ocean Y","3 Metal / Ocean X","3 Metal / Ocean Y","1 Wood / Impact","2 Pluck / Impact","3 Metal / Impact"};
constexpr const char* oceanTargets[]={"Unpatched","1 Wood / Brightness + fold","1 Wood / Ring depth","1 Wood / Modulator ratio","1 Wood / Decay","2 Pluck / Brightness + fold","2 Pluck / Ring depth","2 Pluck / Modulator ratio","2 Pluck / Decay","3 Metal / Brightness + fold","3 Metal / Ring depth","3 Metal / Modulator ratio","3 Metal / Decay"};
struct OceanSettings {
    float gravity=1,rate=.22f,damping=.38f;
    std::array<float,3> mass{{1,1,1}};
};
struct OceanRoute {int source=0,target=0;float amount=0;};

// A musical slosh model, not an equilibrium-tide simulation: a softened
// attraction displaces a damped ocean relative to its anchored planet.
// Real differential tides also have opposing bulges, not represented here.
// Positions are metres, +X right and +Y away from the listener. Heights
// contribute to distance, but only the planar displacement becomes CV.
class Ocean {
public:
    using Positions=std::array<std::array<float,3>,3>;
    using Signals=std::array<float,6>;
    void prepare(double sampleRate) {rate=sampleRate;tick=0;position={};velocity={};signals={};}
    const Signals& process(const Positions& planets,const OceanSettings& settings,int samples) {
        // Fixed 240 Hz integration clock, carried across callback boundaries.
        tick+=std::max(0,samples)*240.;
        if(tick<rate)return signals;
        std::array<std::array<double,2>,3> target{};
        for(size_t i=0;i<3;++i) {
            double x=0,y=0;
            for(size_t j=0;j<3;++j)if(i!=j) {
                const double dx=planets[j][0]-planets[i][0],dy=planets[j][1]-planets[i][1],dz=planets[j][2]-planets[i][2];
                const double distance2=dx*dx+dy*dy+dz*dz+1.; // 1 m softening: safe at overlap.
                const double pull=6.*std::clamp(settings.gravity,0.f,4.f)*std::clamp(settings.mass[j],0.f,4.f)/(distance2*std::sqrt(distance2));
                x+=dx*pull;y+=dy*pull;
            }
            const double scale=.7/(1+std::hypot(x,y));target[i]={x*scale,y*scale};
        }
        const double omega=6.283185307179586*std::clamp(settings.rate,.05f,1.5f);
        const double drag=std::exp(-2*std::clamp(settings.damping,.15f,1.5f)*omega/240.);
        while(tick>=rate) {
            tick-=rate;
            for(size_t i=0;i<3;++i)for(size_t a=0;a<2;++a) {
                auto& v=velocity[i][a];auto& x=position[i][a];
                v=drag*v+omega*omega*(target[i][a]-x)/240.;
                x+=v/240.;
                signals[i*2+a]=(float)std::tanh(1.5*x);
            }
        }
        return signals;
    }
    const Signals& values()const{return signals;}
private:
    double rate=48000,tick=0;
    std::array<std::array<double,2>,3> position{},velocity{};
    Signals signals{};
};

using OceanModulation=std::array<std::array<float,4>,3>;
inline OceanModulation oceanModulation(const Ocean::Signals& signals,const std::array<OceanRoute,oceanRoutes>& routes,bool enabled,const std::array<float,3>& impacts={}) {
    OceanModulation result{};
    if(enabled)for(const auto& route:routes)if(route.source>=0&&route.source<oceanSourceCount&&route.target>0&&route.target<=12) {
        const int target=route.target-1;
        result[(size_t)(target/4)][(size_t)(target%4)]+=(route.source<6?signals[(size_t)route.source]:impacts[(size_t)route.source-6])*route.amount;
    }
    // Amounts add, including cancellation, before bounded destination mapping.
    for(auto& planet:result)for(auto& value:planet)value=std::clamp(value,-1.f,1.f);
    return result;
}
}
