#pragma once
#include "SphericalWater.h"

// Neutral satin material shared by the room floor and rays through the water.
namespace tide::surface {
inline SphericalWater::Vec floor(float x,float y,float width,float depth) {
    const float u=x/std::max(1.f,width)+.5f,v=y/std::max(1.f,depth);
    const float key=std::exp(-((u-.30f)*(u-.30f)*7.f+(v-.62f)*(v-.62f)*3.2f));
    const float fill=std::exp(-((u-.84f)*(u-.84f)*18.f+(v-.75f)*(v-.75f)*7.f));
    return SphericalWater::Vec{.049f,.058f,.063f}+SphericalWater::Vec{.088f,.081f,.068f}*key+SphericalWater::Vec{.017f,.020f,.023f}*fill;
}
}
