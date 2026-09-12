#pragma once

// Retail's pathfinder, ported from KINGDOMS.icd by static analysis.
//
// It is NOT an A*. There is no open list, no priority queue and no cost-to-goal
// ordering anywhere in the original. It marches straight at the goal and, when
// something blocks it, traces the outline of the obstacle until the detour has
// improved on its closest approach -- a "bug" algorithm. See
// docs/retail-engine.md for the full disassembly notes; addresses in the
// comments below are the icd entry points the code mirrors.
//
// Everything here is integer arithmetic on 16px cells, and the per-frame work
// budget is an integer too (retail's default is 12000 units), so a match runs
// identically on every peer. No timers, no floats: safe for lockstep.
//
// INCOMPLETE -- NOT WIRED INTO THE SIM YET. Init and march match the original
// closely. The TRACE phase does not: retail keeps a per-cell visited bitmap
// plus flag bytes (icd 0x414d44..0x414d92) and exits the trace on a GEOMETRIC
// test against the trace origin (0x414d98 onward), where this uses the cruder
// "did the detour beat our closest approach". That difference is why a wall
// with a gap in it is not yet routed around. Finish that disassembly before
// wiring this up -- shipping the approximation under retail's name is exactly
// the mistake to avoid.

#include <cstdint>
#include <functional>
#include <vector>

namespace tak::sim {

// Cell grades from retail's passability query (icd 0x4139d0 -> 0x413c80 ->
// 0x4db640). Every call site in the original compares against 4, so a cell
// scoring below that is refused.
inline constexpr int kCellThreshold = 4;
inline constexpr int kCellImpassable = 0;   // terrain, or a body that blocks
inline constexpr int kCellOccupied = 4;     // a parked body: passable, costly
inline constexpr int kCellGround = 6;
inline constexpr int kCellRoad = 7;

// The 8 compass directions in ROTATIONAL order, lifted from the tables at
// icd 0x5f304c / 0x5f3054. Index is masked & 7 throughout, so rotating the
// index by 1 turns 45 degrees and by 2 turns 90.
inline constexpr int kDirX[8] = {0, -1, -1, -1, 0, 1, 1, 1};
inline constexpr int kDirZ[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// Work charged per step, matching the original exactly: the march costs 8
// (icd 0x414844), setting up a trace costs 7 (0x414a6a), each trace step costs
// 9 (0x414c40), and the caller charges a further 30 when a waypoint comes back
// (0x415b3f).
inline constexpr int kWorkMarch = 8;
inline constexpr int kWorkTraceInit = 7;
inline constexpr int kWorkTraceStep = 9;
inline constexpr int kWorkWaypoint = 30;

// Retail's default per-frame budget (icd 0x41617b seeds both +0x221 and
// +0x225 with 0x2ee0), before the quality percentage scales it.
inline constexpr int kPathBudgetDefault = 12000;

struct PathCell {
    int x = 0, z = 0;
};

// One resumable search. step() burns work until the quantum runs out, then
// returns Suspended with every field intact for the next tick.
struct PathSearch {
    enum class Phase : uint8_t { Init, March, TraceInit, Trace, Done, Failed };
    enum class Result : uint8_t {
        Arrived,     // icd 0: the goal was reached
        Waypoint,    // icd -1: a point was emitted, more to do
        Suspended,   // icd -2: out of quantum, resume next tick
        Failed,      // the search gave up (closed trace, or an illegal start)
    };

    // Request.
    int unitId = 0;
    PathCell start, goal;
    int foot = 1;
    int selfId = 0;
    bool priority = false;   // retail's +0x24e7 flag: five times the work share

    // Resumable state. Field comments give the icd offset each one mirrors.
    Phase phase = Phase::Init;
    PathCell cur;                 // +0xd0 / +0xd4
    PathCell traceOrigin;         // +0xf0 / +0xf4, where the march gave up
    PathCell traceStart;          // +0x100 / +0x104, closed-loop detection
    int dir = 0;                  // +0x108
    int traceStartDir = -1;       // +0x10c
    bool traced = false;          // +0x110
    int best = 0;                 // +0xcc, closest approach so far
    int nOccupied = 0;            // +0xd8
    int nGround = 0;              // +0xdc
    int nRoad = 0;                // +0xe0
    int visited = 0;              // +0xe4
    int visitLimit = 4096;        // +0xe8
    int work = 0;                 // +0x48, against the quantum in +0x165

    std::vector<PathCell> out;    // waypoints emitted so far

    // `score` answers retail's per-cell query for this unit. One call runs until
    // `quantum` work units are spent; call again next tick to continue.
    Result step(const std::function<int(int, int)>& score, int quantum);
};

// Chebyshev distance in cells. Retail's 0x413e50 returns a distance the search
// only ever compares, so the metric that matters is the 8-connected one its
// own stepping uses.
int pathDist(PathCell a, PathCell b);

// Delta -> one of the 8 directions (icd 0x415040).
int pathDirFromDelta(int dx, int dz);

}   // namespace tak::sim
