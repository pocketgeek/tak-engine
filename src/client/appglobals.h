#pragma once

// File-scope globals shared between main.cpp and GameView (client/gameview.h).
// Kept here (as C++17 inline variables -> one definition across TUs) so both the
// app shell and the extracted GameView translation units see the same instance.

#include <string>

// Isometric tilt factor for the world renderer (default 0.72). Read by GameView's
// draw path; written once at startup by the debug `--tilt <f>` CLI flag.
inline float gTilt = 0.72f;   // depth-sort tilt only (see kProjY/kProjZ)

// Retail's 2.5D projection, straight off the instruction sequence at icd
// 0x421dad:
//     screenX = x - cameraX
//     screenY = z - (y >> 1) - cameraY
// so screen Y takes ALL of z and HALF of y, and screen X takes no y at all.
// Note this is not a rotation -- the coefficients are 1.0 and 0.5, which are not
// the cosine and sine of any angle -- so it cannot be expressed as a tilt. We
// modelled it as one for a long time (cos/sin of gTilt = 0.75/0.66) and were
// wrong in both terms. The same halving governs the terrain lift: 0x511140
// returns a cell's height byte and 0x426820 subtracts height>>1.
inline constexpr float kProjY = 0.5f;   // screen-Y per unit of model Y
inline constexpr float kProjZ = 1.0f;   // screen-Y per unit of model Z

// The resolved retail install folder. The VFS covers everything inside the .hpi
// archives, but the movies are LOOSE files under <install>/Movies -- so anything
// that plays one (the front end, the loading screen) needs the path itself.
inline std::string gInstallRoot;
