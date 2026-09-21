#pragma once
#include "SphericalWater.h"
#include <juce_graphics/juce_graphics.h>

// Soft camera glare from the note lights. No scene coordinates or colour
// channels move here; note colour separation lives in each planet's texture.
class NoteLens {
public:
    using Vec=SphericalWater::Vec;
    struct Light {juce::Point<float> centre;float radius=0,strength=0;Vec tint{1,1,1};};
    using Lights=std::array<Light,3>;
    explicit NoteLens(const Lights& lights,juce::Point<float> origin={},float scale=1) {
        for(const auto& light:lights){
            const float strength=std::clamp(light.strength,0.f,1.f);
            if(strength<=.0001f||light.radius<=0)continue;
            auto& p=pulses[(size_t)count++];p.centre=(light.centre-origin)*scale;
            p.radius=std::max(26.f,light.radius*4.3f)*scale;
            p.inverseSquare=1/(p.radius*p.radius);p.strength=strength;
            p.tint=light.tint*.65f+Vec{.35f,.35f,.35f};
        }
    }
    bool empty()const{return count==0;}
    NoteLens inRows(float first,float last)const {
        auto result=*this;result.count=0;
        for(int i=0;i<count;++i){const auto& p=pulses[(size_t)i];
            if(p.centre.y+p.radius>=first&&p.centre.y-p.radius<=last)
                result.pulses[(size_t)result.count++]=p;
        }
        return result;
    }
    Vec at(juce::Point<float> point)const {
        Vec glare;
        for(int i=0;i<count;++i){const auto& p=pulses[(size_t)i];
            const auto delta=point-p.centre;
            if(std::abs(delta.x)>=p.radius||std::abs(delta.y)>=p.radius)continue;
            const float u=(delta.x*delta.x+delta.y*delta.y)*p.inverseSquare;if(u>=1)continue;
            const float edge=1-u,falloff=edge*edge*edge,core=1/(1+u*24);
            glare=glare+p.tint*(p.strength*(12*falloff+18*core*core*falloff));
        }
        return glare;
    }
private:
    struct Pulse {juce::Point<float> centre;float radius=0,inverseSquare=0,strength=0;Vec tint;};
    std::array<Pulse,3> pulses{};int count=0;
};
