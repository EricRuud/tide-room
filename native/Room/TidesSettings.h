#pragma once
#include <array>
namespace tide::tides {
struct Control {const char* id;const char* name;float low,high,step,initial;};
inline constexpr std::array<Control,6> controls{{
    {"tideSpeed","Current speed",.1f,3,.01f,1},
    {"tideTravel","Travel",0,1.5f,.001f,1},
    {"tideInertia","Inertia",.25f,3,.01f,1},
    {"tideBuoyancy","Buoyancy",-.75f,.75f,.01f,0},
    {"tideBounce","Bounce",0,1,.001f,.68f},
    {"tideSound","Sound coupling",0,1,.001f,1}
}};
}
