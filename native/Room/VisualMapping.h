#pragma once
#include "GpuEffects.h"
#include <cmath>

namespace tide::visual {
// Map picking through the same kaleidoscope/flow field as the shader. With a
// crossfade, the brighter of the clean and transformed images owns the hit.
inline juce::Point<float> sourcePoint(juce::Point<float> point,juce::Rectangle<float> bounds,const gpu::Effects& fx){
    if(fx.controls.y<.5f)return point;
    const float unit=std::min(bounds.getWidth(),bounds.getHeight())*.5f;
    if(unit<=0)return point;
    const auto uv=(point-bounds.getCentre())/unit;auto at=uv;
    const float time=fx.animation.x,react=fx.animation.z*fx.animation.w;
    if(fx.layers.z>0){
        const float sector=juce::MathConstants<float>::twoPi/std::round(3+fx.controls.w);
        const float angle=std::abs(std::fmod(std::atan2(uv.y,uv.x)+time*.08f+react*.12f+juce::MathConstants<float>::twoPi*2,sector)-sector*.5f);
        const auto folded=juce::Point<float>{std::cos(angle),std::sin(angle)}*(uv.getDistanceFromOrigin()/(1+.06f*react));
        at+=(folded-at)*fx.layers.z;
    }
    if(fx.controls.x>0){
        const float zoom=std::max(.25f,fx.controls.z);
        at+=juce::Point<float>{std::sin(at.y*5/zoom+time*.7f),std::cos(at.x*4/zoom-time*.6f)}*(fx.controls.x*(.055f+.07f*react));
    }
    const auto result=at*unit+bounds.getCentre();
    return {std::clamp(result.x,bounds.getX(),bounds.getRight()-.001f),std::clamp(result.y,bounds.getY(),bounds.getBottom()-.001f)};
}
}
