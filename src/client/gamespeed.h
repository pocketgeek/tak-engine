#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace tak {

// Retail shows game speed as a level, never a multiplier: level d runs at
// (d+10)/10 x normal (-9 = 0.1x, NORMAL, +10 = 2x; KINGDOMS.icd 0x4999b0 prints
// "Game Speed Normal" or "Game Speed +5"). Speeds past +10 exist only in debug
// builds (TAK_FAST_SPEED) and show the multiplier instead.
inline std::string speedLevelText(int d) {
    if (d == 0) return "NORMAL";
    if (d > 10) { char b[16]; std::snprintf(b, sizeof b, "%.1fX", (d + 10) / 10.0); return b; }
    return (d > 0 ? "+" : "") + std::to_string(d);
}

// The nearest level to a measured speed multiplier (1.0 = NORMAL).
inline int speedLevelOf(float mult) { return int(std::lround(mult * 10.0f)) - 10; }

}  // namespace tak
