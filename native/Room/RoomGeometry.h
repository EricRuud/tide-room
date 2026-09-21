#pragma once
namespace tide::room {
inline constexpr float towerRadius=.72f;
inline constexpr float towerEnvelopeRadius=.98f;
inline constexpr float towerHeightRatio=.38196601125f;
// The listener sits just above the tallest plate, revealing the water surfaces.
inline constexpr float listenerHeight(float roomHeight){return roomHeight*towerHeightRatio+1.10f;}
}
