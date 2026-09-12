#include "sim/pathsearch.h"

#include <algorithm>
#include <cstdlib>

namespace tak::sim {

int pathDist(PathCell a, PathCell b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

int pathDirFromDelta(int dx, int dz) {
    // The tables are in rotational order, so the direction is just the octant.
    // Ties resolve the way retail's own cardinal pick does (icd 0x414ae9): x
    // dominates, and a pure-z delta chooses north for <= 0.
    if (dx == 0 && dz == 0) return 0;
    if (std::abs(dx) > 2 * std::abs(dz)) return dx < 0 ? 2 : 6;
    if (std::abs(dz) > 2 * std::abs(dx)) return dz <= 0 ? 0 : 4;
    if (dx < 0) return dz <= 0 ? 1 : 3;
    return dz <= 0 ? 7 : 5;
}

PathSearch::Result PathSearch::step(const std::function<int(int, int)>& score,
                                    int quantum) {
    for (;;) {
        // Out of quantum: suspend with state intact (icd 0x415028 returns -2,
        // which the caller records as "still pending" rather than a failure).
        if (work >= quantum) return Result::Suspended;
        if (visited > visitLimit) return Result::Failed;   // +0xe4 vs +0xe8

        switch (phase) {
        case Phase::Init: {
            best = pathDist(start, goal);
            cur = start;
            if (best == 0) { phase = Phase::Done; return Result::Arrived; }
            // Retail refuses to search out of an illegal cell (icd 0x414733).
            if (score(start.x, start.z) < kCellThreshold) {
                phase = Phase::Failed;
                return Result::Failed;
            }
            phase = Phase::March;
            break;
        }

        case Phase::March: {
            work += kWorkMarch;
            int d = pathDirFromDelta(goal.x - cur.x, goal.z - cur.z);
            PathCell n{cur.x + kDirX[d], cur.z + kDirZ[d]};
            ++visited;
            const int s = score(n.x, n.z);
            // Below the threshold the way is shut; at exactly 4 a body is
            // standing there -- passable, but retail only tolerates three such
            // cells before it stops bulling through and goes around. Ordinary
            // ground is counted the same way; road (7) is free.
            // Mirrors icd 0x4148b2..0x414900 exactly. Note that ORDINARY GROUND
            // is only counted, never capped -- the cap on it applies solely once
            // the road run has also reached 3. Capping ordinary ground (which I
            // did first) makes the march give up after three steps of open
            // field, and nothing arrives anywhere.
            bool giveUp = false;
            if (s < kCellThreshold) {
                giveUp = true;                          // 0x4148b2
            } else if (s == kCellOccupied) {            // 0x4148ba
                if (nOccupied >= 3) giveUp = true; else ++nOccupied;
            } else if (s == kCellRoad) {                // 0x4148d7
                if (nRoad >= 3) { if (nGround >= 3) giveUp = true; }
                else ++nRoad;
            } else {
                ++nGround;                              // 0x4148fa
            }
            if (giveUp) {
                traceOrigin = cur;          // +0xf0 / +0xf4
                phase = Phase::TraceInit;
                break;
            }
            cur = n;
            const int dist = pathDist(cur, goal);
            if (dist < best) best = dist;
            if (best == 0) {                // icd 0x4149f4
                out.push_back(cur);
                phase = Phase::Done;
                return Result::Arrived;
            }
            break;
        }

        case Phase::TraceInit: {
            work += kWorkTraceInit;
            // The trace starts on the cardinal direction that points at the
            // goal (icd 0x414ae9): x dominates, else north/south.
            const int dx = goal.x - traceOrigin.x;
            const int dz = goal.z - traceOrigin.z;
            dir = dx < 0 ? 2 : dx > 0 ? 6 : (dz <= 0 ? 0 : 4);
            cur = traceOrigin;
            traceStart = traceOrigin;
            traceStartDir = dir;
            traced = false;
            phase = Phase::Trace;
            break;
        }

        case Phase::Trace: {
            work += kWorkTraceStep;
            // Sweep from 90 degrees left of the current heading, rotating one
            // step at a time, and take the first cell that clears the
            // threshold (icd 0x414c52..0x414cee). That is a left-hand wall
            // follow; the sweep ending where it began means we are boxed in.
            const int limit = (dir - 3) & 7;
            int d = (dir - 2) & 7;
            bool moved = false;
            for (;;) {
                PathCell n{cur.x + kDirX[d], cur.z + kDirZ[d]};
                if (score(n.x, n.z) >= kCellThreshold) {
                    // Closed loop: back at the start cell facing the start way.
                    if (traced && n.x == traceStart.x && n.z == traceStart.z &&
                        d == traceStartDir) {
                        phase = Phase::Failed;
                        return Result::Failed;
                    }
                    cur = n;
                    dir = d;
                    traced = true;
                    ++visited;
                    moved = true;
                    break;
                }
                d = (d + 1) & 7;
                if (d == limit) break;      // swept the full circle: no way out
            }
            if (!moved) { phase = Phase::Failed; return Result::Failed; }
            // APPROXIMATION, NOT RETAIL: leave the obstacle as soon as the
            // detour beats the march's closest approach. The original marks
            // every traced cell in a bitmap with flag bytes and applies a
            // geometric test against the trace origin (icd 0x414d44 onward).
            // This is the one piece still to port, and it is why a wall with a
            // gap in it is not yet routed around.
            const int dist = pathDist(cur, goal);
            if (dist < best) {
                best = dist;
                out.push_back(cur);
                if (best == 0) { phase = Phase::Done; return Result::Arrived; }
                phase = Phase::March;
                return Result::Waypoint;
            }
            break;
        }

        case Phase::Done: return Result::Arrived;
        case Phase::Failed: return Result::Failed;
        }
    }
}

}   // namespace tak::sim
