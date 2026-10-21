#pragma once

// Shift-drag build lines: where each site of a dragged row of buildings goes.
// Header-only and free of GameView so tools/buildline_test.cpp can check the
// geometry on its own.

#include "sim/footprint.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

namespace tak::client {

// The widest gap the mouse wheel reaches, in empty cells between neighbours.
constexpr int kBuildLineMaxGap = 12;

// World position whose footprint starts at cell (cx, cz): the footprint's centre,
// which footprintOrigin maps straight back to (cx, cz) and the Build order's own
// snap (footprintWaypoint(footprintCell(...))) leaves on the same cells.
inline std::pair<float, float> buildLineSite(int cx, int cz, int footX, int footZ) {
    return {float(cx * 16 + footX * 8), float(cz * 16 + footZ * 8)};
}

// The sites of a line dragged from (x0,z0) to (x1,z1), `gap` empty cells apart.
//
// Every site is snapped to the cell grid canPlace tests (footprintOrigin), so
// the rounding of an unaligned drag can no longer push two neighbours into one
// shared cell. The line steps in whole cells along the axis that separates
// neighbours first in the drag direction: exactly footprint + gap cells on that
// axis, with the other axis following the drag (rounded to a cell, measured from
// the first site, so a long diagonal does not drift). Neighbours therefore never
// share a cell; gap 0 is footprints touching edge to edge (corner to corner on a
// 45-degree drag). The last site is the one whose cell the cursor's own
// footprint has reached on the stepping axis, so a site appears just as the
// cursor arrives where it would go. A zero-length drag is one site.
inline std::vector<std::pair<float, float>> buildLineSites(int footX, int footZ, int gap,
                                                           float x0, float z0, float x1, float z1) {
    footX = std::max(footX, 1);
    footZ = std::max(footZ, 1);
    gap = std::clamp(gap, 0, kBuildLineMaxGap);
    const int ox = tak::sim::footprintOrigin(x0, footX), oz = tak::sim::footprintOrigin(z0, footZ);
    std::vector<std::pair<float, float>> out{buildLineSite(ox, oz, footX, footZ)};
    const float dx = x1 - x0, dz = z1 - z0;
    const float ax = std::abs(dx), az = std::abs(dz);
    if (ax < 1e-3f && az < 1e-3f) return out;
    const int stepX = footX + gap, stepZ = footZ + gap;
    // stepX/ax <= stepZ/az: moving along the drag clears stepX columns before it
    // clears stepZ rows, so x is the axis that keeps neighbours apart.
    const bool alongX = float(stepX) * az <= float(stepZ) * ax;
    const int step = alongX ? stepX : stepZ;
    const int tip = alongX ? tak::sim::footprintOrigin(x1, footX) - ox
                           : tak::sim::footprintOrigin(z1, footZ) - oz;
    const float major = alongX ? dx : dz, minor = alongX ? dz : dx;
    const int dir = major < 0 ? -1 : 1;
    const int n = std::abs(tip) / step;   // footprintOrigin is monotonic: tip has major's sign
    for (int i = 1; i <= n; ++i) {
        const int along = dir * i * step;
        const int across = int(std::lround(float(along) * minor / major));
        out.push_back(alongX ? buildLineSite(ox + along, oz + across, footX, footZ)
                             : buildLineSite(ox + across, oz + along, footX, footZ));
    }
    return out;
}

// One wheel event's change to the line gap. SDL reports a notch as 1.0 and a
// trackpad or high-resolution wheel as fractions, so fractions accumulate in
// `carry` and each event moves the gap at most one step: a trackpad flick
// (momentum deltas of many "lines" at once) cannot jump it by dozens.
inline int buildLineWheelSteps(float delta, float& carry) {
    carry += delta;
    if (carry >= 1.0f) { carry = 0; return 1; }
    if (carry <= -1.0f) { carry = 0; return -1; }
    return 0;
}

} // namespace tak::client
