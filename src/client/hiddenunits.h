#pragma once

// Painter layers of the world pass (client/gameview_render.cpp), kept free of
// GameView so they can be tested on their own.

#include <cstdint>

namespace tak::hiddenunits {

// ---- Painter layers ---------------------------------------------------------
// Retail's world pass (KINGDOMS.icd 0x4fc648..0x4fcc46) scans the map cells and
// draws every feature whose definition height (feature TDF `height`, def +0x138,
// parsed at 0x4939ef/0x493a0e) is below 10 at once, before any unit; taller
// features are drawn with the ground units in depth order, and units that are not
// on the ground go last. So a stump, a crop row, a mana deposit or a smudge never
// paints over a unit's feet, whichever side of it the unit stands on.
inline constexpr int kLowFeatureHeight = 10;
inline constexpr int kLayerLowFeature = -1;   // flat scenery: under every unit
inline constexpr int kLayerGround = 0;        // tall features + ground units, by depth
inline constexpr int kLayerAir = 1;           // airborne flyers, after everything
inline constexpr bool lowFeature(int tdfHeight) { return tdfHeight < kLowFeatureHeight; }
inline constexpr int featureLayer(int tdfHeight) {
    return lowFeature(tdfHeight) ? kLayerLowFeature : kLayerGround;
}

}  // namespace tak::hiddenunits
