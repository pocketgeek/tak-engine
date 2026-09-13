#pragma once

// File-scope globals shared between main.cpp and GameView (client/gameview.h).
// Kept here (as C++17 inline variables -> one definition across TUs) so both the
// app shell and the extracted GameView translation units see the same instance.

#include <string>

// Triangle depth-sort weights INSIDE one model. Not a camera tilt, and not
// retail's: we draw with SDL_RenderGeometry and no depth buffer, so overlapping
// pieces of a model need an order, and these are the weights that order has
// always used (they were cos/sin of a 0.72 rad "tilt" that the projection turned
// out not to have -- see kProjY/kProjZ).
//
// Retail does not sort primitives at all: it Z-BUFFERS them. The binary imports
// the Glide depth API (grDepthBufferMode/Function/Mask/Range/BiasLevel) and the
// DirectDraw path complains about missing "zbuffer blting" support, while the
// piece-tree walk at icd 0x4eea20 contains no depth comparison of any kind.
//
// So these are a stand-in for a depth buffer, not for a sort, and no values can
// make them exact -- one key per triangle cannot do what a per-pixel test does
// for interpenetrating geometry. Closing that gap means rendering models through
// something with a depth buffer; it does not mean tuning these two numbers.
inline constexpr float kSortZ = 0.7518f;
inline constexpr float kSortY = 0.6594f;

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
