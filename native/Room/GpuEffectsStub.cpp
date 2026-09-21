#include "GpuEffects.h"
namespace tide::gpu {
struct PlanetPass::Impl {};
struct TowerPass::Impl {};
TowerPass::TowerPass()=default;
TowerPass::~TowerPass()=default;
bool TowerPass::render(const TowerParameters&,juce::Image&){return false;}
struct GlassPass::Impl {};
PlanetPass::PlanetPass()=default;
PlanetPass::~PlanetPass()=default;
GlassPass::GlassPass()=default;
GlassPass::~GlassPass()=default;
bool available(){return false;}
juce::String description(){return "CPU renderer (Metal unavailable on this platform)";}
Statistics statistics(){return {};}
bool PlanetPass::render(const PlanetParameters&,juce::Image&,juce::Image&){return false;}
bool GlassPass::prepare(const GlassParameters&){return false;}
bool GlassPass::render(const juce::Image&,juce::Image&,const Lights&,const Effects&){return false;}
}
