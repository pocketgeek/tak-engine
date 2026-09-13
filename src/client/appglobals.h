#pragma once

// File-scope globals shared between main.cpp and GameView (client/gameview.h).
// Kept here (as C++17 inline variables -> one definition across TUs) so both the
// app shell and the extracted GameView translation units see the same instance.

#include <string>

// Triangle depth-sort weights INSIDE one model. We draw through
// SDL_RenderGeometry with no depth buffer, so overlapping pieces of a model need
// an order.
//
// DERIVED FROM THE PROJECTION, not from a camera angle. screenY = z - y/2 means
// points differing along (y,z) = (2,1) land on the same pixel -- that direction
// IS the view ray -- so depth along it is proportional to 2y + z. Larger y is
// higher and nearer the camera; larger z is further down-screen and also nearer.
// Both terms therefore carry the SAME sign, and the sort (descending, farthest
// first) wants the negation.
//
// The previous weights were the cosine and sine of a 0.72 rad "tilt" the
// projection turns out not to have, and they gave y and z OPPOSITE signs -- so
// every triangle pair that differed mainly in z was ordered backwards. That is
// what made capes and other layered pieces tear.
//
// Retail does not sort at all: it z-buffers (Glide grDepthBuffer*, and the
// DirectDraw path's "no hardware support for zbuffer blting" complaint; the
// piece walk at icd 0x4eea20 has no depth compare in it). So this remains an
// approximation of a depth buffer -- correct ordering between triangles, still
// unable to resolve two that interpenetrate.
inline constexpr float kSortY = 2.0f;   // weight on model Y in the view-ray depth
inline constexpr float kSortZ = 1.0f;   // weight on model Z

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
