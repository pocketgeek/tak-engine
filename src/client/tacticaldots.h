#pragma once

// TACTICAL DOTS (Options > Graphics; a non-retail extra, default off): when the
// camera is zoomed far out, every unit is drawn as a flat player-coloured dot,
// the way the minimap shows it, instead of its 3D model. Pure display -- the
// sim, the lockstep hash and the network never see it.
//
// This header is the policy, kept free of GameView so it can be tested on its
// own: when dots are on (zoom threshold + hysteresis) and how big one is.

#include <algorithm>

namespace tak::tacticaldots {

// The main threshold is expressed as SCREEN PIXELS PER FOOTPRINT CELL: one 16
// world px cell, the unit of every footprint, drawn at zoom z is 16*z screen
// pixels. That depends on the zoom alone, never on window size or UI scale, so
// the switch happens at the same apparent unit size on a 1280x960 window and on
// a 7680x2160 one. Chosen from captures of a 16000-unit army: at zoom 0.45
// (7.2 px/cell) infantry and siege engines are still told apart; at 0.30 (4.8)
// a 2x2-cell soldier is a ~10px blob whose only readable property is its team
// colour -- exactly what a dot shows, more clearly.
//
// Dots come on below kEnterCellPx and go off again only above kExitCellPx. The
// gap is the hysteresis: one mouse-wheel notch is a x1.118 zoom step, and the
// band is wider than that, so a notch back and forth across the boundary (or
// the benchmark camera's slow zoom) cannot flicker between models and dots.
inline constexpr float kEnterCellPx = 4.8f;   // zoom 0.30
inline constexpr float kExitCellPx  = 5.6f;   // zoom 0.35 (~1.17x the entry zoom)
inline constexpr float kCellWorldPx = 16.0f;

inline constexpr float enterZoom() { return kEnterCellPx / kCellWorldPx; }
inline constexpr float exitZoom() { return kExitCellPx / kCellWorldPx; }

// The second trigger: the camera at the zoom-out FLOOR. The view cannot zoom out
// past the point where the map fills the window, and that floor is window-wide:
// at 7680x2160 it is 7680/mapWidth -- 0.75 on Ulasem Arena (10240 px), 1.5 on a
// 5120 px map -- so the size threshold above could never be reached on a wide
// display. Fully zoomed out IS the "zoomed a lot out" overview, whatever the
// window, so dots also show there. Same hysteresis idea: on at the floor, still
// on one wheel notch (x1.118) in, models again from the second notch.
inline constexpr float kFloorEnter = 1.02f;   // at the floor (float slack only)
inline constexpr float kFloorExit  = 1.15f;

// Smallest dot edge, in screen pixels at UI SCALE 100%. The minimap's dots are
// 3px; the world view is far larger, so a little more keeps a lone scout visible.
inline constexpr float kMinDotPx = 4.0f;

class Switch {
public:
    // Feed the option, the frame's zoom and the window's zoom-out floor
    // (MapView::minZoom; 0 = no floor trigger); returns whether dots draw.
    bool update(bool enabled, float zoom, float floorZoom = 0.0f) {
        if (!enabled) on_ = false;
        else if (on_) on_ = zoom < exitZoom() || zoom <= floorZoom * kFloorExit;
        else on_ = zoom < enterZoom() || zoom <= floorZoom * kFloorEnter;
        return on_;
    }
    bool on() const { return on_; }

private:
    bool on_ = false;
};

// Share of the footprint a dot covers, so a tight formation still reads as
// separate dots rather than one solid block.
inline constexpr float kFootFill = 0.75f;

// Edge of a unit's square dot in screen pixels: its larger footprint side at
// this zoom (times kFootFill), so buildings come out as larger squares than
// infantry. Floored so a dot never shrinks out of sight, and capped at its size
// at the exit zoom so a floor-triggered overview on a wide window shows compact
// markers, not huge slabs. The floor and cap follow UI SCALE, the setting a
// high-DPI player already uses to make small things legible. The minimap draws
// every unit as a square too, so the shape matches it.
inline float dotSide(int footX, int footZ, float zoom, float uiScale = 1.0f) {
    const float cells = float(std::max({footX, footZ, 1}));
    const float ui = std::clamp(uiScale, 0.75f, 2.0f);
    const float lo = kMinDotPx * ui;
    const float hi = std::max(lo, cells * kCellWorldPx * exitZoom() * kFootFill * ui);
    return std::clamp(cells * kCellWorldPx * zoom * kFootFill, lo, hi);
}

}  // namespace tak::tacticaldots
