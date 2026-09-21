#pragma once
#include <juce_graphics/juce_graphics.h>
#include <memory>

// UI-only Metal compute passes. The audio thread never calls this interface.
// Float4 keeps the C++ / Metal constant-buffer layouts explicit and identical.
namespace tide::gpu {
struct alignas(16) Float4 {float x=0,y=0,z=0,w=0;};
struct PlanetParameters {
    Float4 shape, dipole, axis, inverseAxes, waves[3], phases, amplitudes;
    Float4 originRadius, right, up, back, room, settings;
};
struct TowerParameters {
    Float4 originRadius, right, up, back, room, settings, flow, plates[16];
};
struct GlassParameters {
    Float4 sizeScale, bounds, centreFocal, eye, forward, right, up, room;
};
struct Lights {Float4 geometry[3], tint[3];};
struct Effects {Float4 layers, controls, animation;};
struct Statistics {uint64_t planets=0, glasses=0, effects=0, failures=0;};
bool available();
juce::String description();
Statistics statistics();

class PlanetPass {
public:
    PlanetPass();
    ~PlanetPass();
    bool render(const PlanetParameters&,juce::Image& plain,juce::Image& split);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
class TowerPass {
public:
    TowerPass();
    ~TowerPass();
    bool render(const TowerParameters&,juce::Image& image);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
class GlassPass {
public:
    GlassPass();
    ~GlassPass();
    bool prepare(const GlassParameters&);
    bool render(const juce::Image& source,juce::Image& target,const Lights&,const Effects& = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
