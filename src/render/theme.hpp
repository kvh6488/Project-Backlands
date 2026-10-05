#pragma once
// ==== theme ====
// Presentation colour roles, each a step of the master palette (core/palette.hpp).
// UI code names a role - never a ramp step, never a raylib named colour - so
// a palette change is one edit to assets/palette.json plus a header regen,
// and a role change is one line here. Lives in render/ because both the UI
// and the renderers draw from it, and ui/ may include render/, not the
// reverse. Alpha is applied at the draw site with Fade(); the roles are opaque.
//
// Only fills and text go through roles. A WHITE passed to DrawTexture is a
// tint multiplier, not a colour, and stays WHITE.

#include "core/palette.hpp"

namespace theme {

inline constexpr Color ground = pal::neutral[0];     // panels, dimming overlays
inline constexpr Color groundCool = pal::grey[0];    // map void, off-map
inline constexpr Color border = pal::neutral[3];     // slot outlines
inline constexpr Color inkDim = pal::neutral[5];     // secondary text
inline constexpr Color ink = pal::neutral[7];        // primary text
inline constexpr Color highlight = pal::yellow[12];  // selected slot, tooltip title
inline constexpr Color good = pal::accent[5];        // enough ingredients, craftable, popups
inline constexpr Color bad = pal::accent[3];         // missing ingredients, uncraftable
inline constexpr Color mapWall = pal::neutral[3];
inline constexpr Color mapZone = pal::accent[3];        // shifting-zone cells on the map
inline constexpr Color mapPlayer = pal::accent[4];      // lavender dot, ringed in ground
inline constexpr Color radiationGlow = pal::accent[5];  // barrel halos, additive

// Overworld flat fills: the surface draws these until its tile sheets land,
// and the island minimap keeps using them afterwards.
inline constexpr Color ocean = pal::blue[0];
inline constexpr Color lake = pal::blue[3];
inline constexpr Color river = pal::blue[4];
inline constexpr Color beach = pal::yellow[9];
inline constexpr Color grassland = pal::green[9];
inline constexpr Color forest = pal::green[5];
inline constexpr Color wetland = pal::green[3];
inline constexpr Color mountain = pal::grey[6];
inline constexpr Color snow = pal::grey[11];
// Placeholder prop shapes.
inline constexpr Color trunk = pal::brown[2];
inline constexpr Color canopy = pal::green[3];
inline constexpr Color pine = pal::green[1];
inline constexpr Color bush = pal::green[7];
inline constexpr Color rock = pal::grey[7];
inline constexpr Color reeds = pal::yellow[6];

}  // namespace theme
