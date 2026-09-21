#pragma once
#include <array>
namespace tide::visual {
struct Control {const char* id;const char* name;float low,high,step,initial;};
inline constexpr std::array<Control,11> controls{{
    {"visualMix","Master mix",0,1,.001f,1},
    {"visualFractal","Fractal",0,1,.001f,0},
    {"visualMesh","Mesh",0,1,.001f,0},
    {"visualKaleidoscope","Kaleidoscope",0,1,.001f,0},
    {"visualPrism","Prism",0,1,.001f,0},
    {"visualFlow","Flow",0,1,.001f,0},
    {"visualScale","Scale",.35f,4,.01f,1},
    {"visualDetail","Detail",2,12,1,6},
    {"visualSpeed","Motion",0,2,.01f,.25f},
    {"visualHue","Color",0,1,.001f,.1f},
    {"visualReact","Audio response",0,2,.01f,.7f}
}};
}
