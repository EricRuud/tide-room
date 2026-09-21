#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "RoomGeometry.h"

namespace tide::room {
// Audio-clocked, tethered equal-mass bodies. Anchors and LFOs provide targets;
// bounded velocity and 480 Hz integration prevent a fast drag tunnelling through
// another body. Collision impulses and contact latching are independent of UI.
class PlanetMotion {
public:
    using Point=std::array<float,3>;
    using Positions=std::array<Point,3>;
    static constexpr float waterRadius=towerRadius,solidRadius=.64f,stepSeconds=1.f/480;
    static constexpr std::array<std::array<int,2>,3> pairs{{{{0,1}},{{0,2}},{{1,2}}}};
    void prepare(double sr){sampleRate=sr;clock=0;ready=false;position={};velocity={};impact={};joins={};latched={};kicks={};events=0;}
    const Positions& process(const Positions& target,int samples,float width,float depth,float height=4,float bounce=.68f,float inertia=1.f,float elevation=-1.f) {
        desired=target;
        restitution=std::clamp(bounce,0.f,1.f);bodyInertia=std::clamp(inertia,.25f,3.f);
        const float boundaryRadius=elevation>=0?towerEnvelopeRadius:waterRadius;
        const float xLimit=std::min(width*.4f,width*.5f-boundaryRadius);
        Point low{{-xLimit,std::max(depth*.1f,boundaryRadius),waterRadius}},high{{xLimit,std::min(depth*.68f,depth-boundaryRadius),std::min(2.2f,height-waterRadius)}};
        if(elevation>=0){low[2]=high[2]=elevation;for(size_t i=0;i<3;++i){position[i][2]=elevation;velocity[i][2]=0;kicks[i][2]=0;}constrain(low,high);}
        if(!ready){position=target;ready=true;
            // A perfectly collinear spawn in a narrow room cannot separate
            // sideways. Seed depth separation before solving that degenerate case.
            if(elevation>=0&&high[0]-low[0]<4*solidRadius&&std::abs(position[0][1]-position[1][1])<.001f&&std::abs(position[0][1]-position[2][1])<.001f)
                for(size_t i=0;i<3;++i)position[i][1]+=.12f*((float)i-1);
            for(int pass=0;pass<80;++pass){constrain(low,high);solve(false);}constrain(low,high);}
        clock+=(double)std::max(0,samples)*480.;
        while(clock+1.e-8>=sampleRate){clock-=sampleRate;step(target,low,high);}
        return position;
    }
    const Positions& positions()const{return position;}
    const Positions& velocities()const{return velocity;}
    const std::array<float,3>& impacts()const{return impact;}
    const std::array<float,3>& connections()const{return joins;}
    const Positions& waterKicks()const{return kicks;}
    unsigned impactCount()const{return events;}
private:
    double sampleRate=48000,clock=0;bool ready=false;
    Positions position{},velocity{},kicks{},desired{};
    std::array<float,3> impact{},joins{};std::array<bool,3> latched{};unsigned events=0;
    float restitution=.68f,bodyInertia=1;
    static float dot(Point a,Point b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
    static Point difference(Point a,Point b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
    void constrain(Point low,Point high) {
        for(size_t i=0;i<3;++i)for(size_t a=0;a<3;++a){if(position[i][a]<low[a]){position[i][a]=low[a];velocity[i][a]=std::max(0.f,velocity[i][a]);}
            if(position[i][a]>high[a]){position[i][a]=high[a];velocity[i][a]=std::min(0.f,velocity[i][a]);}}
    }
    void solve(bool trigger) {
        for(size_t pair=0;pair<3;++pair){const size_t a=(size_t)pairs[pair][0],b=(size_t)pairs[pair][1];auto delta=difference(position[b],position[a]);const float distance=std::sqrt(dot(delta,delta));
            if(distance>2*solidRadius+.055f&&dot(difference(desired[b],desired[a]),delta)>0)latched[pair]=false;
            if(distance>=2*solidRadius)continue;
            Point normal=distance>1.e-7f?Point{{delta[0]/distance,delta[1]/distance,delta[2]/distance}}:Point{{pair==1?0.f:1.f,pair==1?1.f:0.f,0}};
            const float closing=-dot(difference(velocity[b],velocity[a]),normal);
            const float rebound=closing>.12f?restitution:0.f;
            const float impulse=std::max(0.f,closing)*(1+rebound)*.5f;
            const float correction=(2*solidRadius-distance+.00001f)*.5f;
            for(size_t axis=0;axis<3;++axis){position[a][axis]-=normal[axis]*correction;position[b][axis]+=normal[axis]*correction;
                velocity[a][axis]-=normal[axis]*impulse;velocity[b][axis]+=normal[axis]*impulse;}
            if(trigger&&!latched[pair]&&closing>.055f){const float strength=std::clamp(closing*.55f,.045f,1.f);
                impact[a]=std::max(impact[a],strength);impact[b]=std::max(impact[b],strength);joins[pair]=std::max(joins[pair],std::sqrt(strength));
                for(size_t axis=0;axis<3;++axis){kicks[a][axis]=std::clamp(kicks[a][axis]-normal[axis]*strength,-1.f,1.f);kicks[b][axis]=std::clamp(kicks[b][axis]+normal[axis]*strength,-1.f,1.f);}
                ++events;latched[pair]=true;
            }
        }
    }
    void step(const Positions& target,Point low,Point high) {
        constexpr float dt=stepSeconds;
        for(size_t i=0;i<3;++i){impact[i]*=.9917013f;joins[i]*=.990574f;
            for(size_t a=0;a<3;++a){kicks[i][a]*=.987f;const float destination=std::clamp(target[i][a],low[a],high[a]);
                velocity[i][a]+=dt*(180.f/bodyInertia*(destination-position[i][a])-19.f/std::sqrt(bodyInertia)*velocity[i][a]);}
            const float speed=std::sqrt(dot(velocity[i],velocity[i]));if(speed>8)for(float& v:velocity[i])v*=8/speed;
            for(size_t a=0;a<3;++a)position[i][a]+=velocity[i][a]*dt;
        }
        // A few positional passes also resolve triple overlaps at a wall.
        for(int pass=0;pass<40;++pass){constrain(low,high);solve(pass==0);}constrain(low,high);
        for(size_t i=0;i<3;++i){const auto d=difference(position[(size_t)pairs[i][0]],position[(size_t)pairs[i][1]]);
            if(dot(d,d)>std::pow(waterRadius*2.9f,2.f))joins[i]=0;
        }
    }
};
}
