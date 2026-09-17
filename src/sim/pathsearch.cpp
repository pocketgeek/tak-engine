#include "sim/pathsearch.h"

#include <bit>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace tak::sim {

static const bool g_pathDbg = getenv("TAK_PATHDBG") != nullptr;

int pathDist(PathCell a, PathCell b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

int pathDirFromDelta(int dx, int dz) {
    // Octant, using the rotational order of the direction tables. Verified
    // against the original by emulating 0x415040 over 81 deltas: all agree.
    // The zero delta is the one case worth spelling out -- the icd answers 5,
    // not 0, and while the search should never ask for a direction to where it
    // already is, matching costs nothing.
    if (dx == 0 && dz == 0) return 5;
    if (std::abs(dx) > 2 * std::abs(dz)) return dx < 0 ? 2 : 6;
    if (std::abs(dz) > 2 * std::abs(dx)) return dz <= 0 ? 0 : 4;
    if (dx < 0) return dz <= 0 ? 1 : 3;
    return dz <= 0 ? 7 : 5;
}

// Cardinal direction toward a delta (icd 0x414ae9): x dominates, ties go north.
static int cardinalToward(int dx, int dz) {
    if (dx < 0) return 2;
    if (dx > 0) return 6;
    return dz <= 0 ? 0 : 4;
}

void PathSearch::reset(int mapW, int mapH) {
    const bool resized = (w_ != mapW || h_ != mapH);
    w_ = mapW;
    h_ = mapH;
    // icd 0x414797: (width + height) * 20. Retail abandons a search that wanders
    // this far and leaves the unit on its straight segment, so long or tangled
    // routes legitimately fail rather than costing unbounded work.
    visitLimit = (mapW + mapH) * 20;
    const size_t n = size_t(w_) * size_t(h_);
    if (resized || stamp_.size() != n) {
        stamp_.assign(n, 0);
        walkStamp_.assign(n, 0);
        flag_.assign(n, 0);
        from_.assign(n, 0);
        dStamp_.assign(n, 0);
        dDist_.assign(n, 0);
        dDir_.assign(n, 0);
        gen_ = walkGen_ = dGen_ = 0;
    }
    ++gen_;   // every cell is now stale, i.e. empty -- no clearing needed
    phase = Phase::Init;
    best = 0;
    goalCrowded = false;
    sawTraffic = false;
    visNear = false;
    visFar = false;
    visited = 0;
    work = 0;
    nOccupied = nGround = nRoad = 0;
    out.clear();
}

// Lay a breadcrumb: remember how we entered `c`, and flag it seen. Mirrors
// icd 0x414b57..0x414b9d -- the bitmap write plus the 4-byte per-cell record
// whose second byte is the incoming direction.
void PathSearch::mark(PathCell c, int d, int score) {
    if (!inside(c)) return;
    const size_t i = size_t(c.z) * size_t(w_) + size_t(c.x);
    touch(i);
    // FIRST VISIT WINS. Overwriting the incoming direction on a revisit turns
    // the parent map into a graph with cycles, and the backtrack then walks a
    // little loop for ever instead of reaching the start -- observed as a
    // 4-cell cycle repeated to the 64-waypoint clamp. Retail's separate bitmap
    // at +0x2c is exactly this guard.
    if (flag_[i] & kSeen) return;
    from_[i] = uint8_t(d);
    flag_[i] = uint8_t(flag_[i] | kSeen | (score == 5 ? kScore5 : 0));
    // RETAIL ACCUMULATES THE CROWD EVIDENCE AT VISIT TIME, right here in the
    // march (icd 0x414951-0x4149ab): the first score-5 cell within the type's
    // goal tolerance latches +0x4c, any other latches +0x50. The failure report
    // (0x414450 called with a nonzero arg from the -1 branch of the step runner,
    // 0x415b53) converts these to the navigator's outcome bits, +0x4c first --
    // emulated to confirm: {4c=1}->bit0, {50=1}->bit1, {both}->bit0, {none}->0.
    // The reconstruction walk recomputes the same pair over BREADCRUMBS for a
    // delivered route; visNear/visFar here serve the failure report.
    if (score == 5) {
        if (tolCells > 0 &&
            std::max(std::abs(c.x - goal.x), std::abs(c.z - goal.z)) < tolCells)
            visNear = true;
        else
            visFar = true;
    }
}

bool PathSearch::atGoal(PathCell c) const {
    if (!inside(c)) return false;
    const size_t i = size_t(c.z) * size_t(w_) + size_t(c.x);
    return seen(i) && (flag_[i] & kGoal) != 0;
}

// Walk the breadcrumbs back from the goal and hand out the corners in travel
// order (icd 0x414450 does the same over its own 0x218-byte buffer). Only
// direction CHANGES become waypoints -- a straight run needs no intermediate
// points -- and the navigator caps the list at 64 (0x4e4ea0).
static bool stepLegal(const std::function<int(int, int)>& score, PathCell c, int d);

void PathSearch::buildRoute() { buildRouteTo(goal); }

// Retail's phase-2 Dijkstra (icd 0x4142c0), the route producer on tracer success.
// Costs and algorithm emulated (tools/re/emuphase.py): a min-heap best-first search
// with NO heuristic, relaxing on accumulated cost. Deterministic -- integer costs, a
// binary heap tie-broken by cell index -- so every peer builds the same route.
bool PathSearch::buildDijkstraRoute(const std::function<int(int, int)>& score) {
    out.clear();
    if (!inside(start) || !inside(goal)) return false;

    // The exact per-grade cell cost from init (emuphase.py): the grade the score()
    // callback returns (retail's 0..7) maps to the entry cost. Grades below the
    // threshold are impassable and never entered.
    auto gradeCost = [](int g) -> int {
        switch (g) {
            case kCellRoad:   return 8;    // 7, +0xc4
            case kCellGround: return 24;   // 6, +0xc0
            case kCellSlope:  return 48;   // 4, +0xbc
            case kCellSameWay:return 80;   // 5, +0xc8
            default:          return -1;   // 0..3: impassable
        }
    };
    // Step cost by direction: cardinal 16, diagonal 23 (retail +0x90). Turn cost by
    // the change in heading from the parent's incoming direction, |d| in 0..4 mapping
    // to 0/80/120/160/200 (retail +0x70). Directions are the tracer's rotational
    // order (kDirX/kDirZ), so a delta of 1 is 45 degrees.
    static const int kStepCost[8] = {16, 23, 16, 23, 16, 23, 16, 23};
    static const int kTurnCost[5] = {0, 80, 120, 160, 200};

    const size_t goalI = size_t(goal.z) * size_t(w_) + size_t(goal.x);
    if (score(start.x, start.z) < kCellThreshold) return false;

    ++dGen_;
    // Binary min-heap of (cost, cell, dir): cost primary, cell index tie-break so
    // ties resolve identically on every machine.
    struct Node { int32_t cost; int32_t cell; uint8_t dir; };
    std::vector<Node> heap;
    auto less = [](const Node& a, const Node& b) {
        return a.cost != b.cost ? a.cost > b.cost : a.cell > b.cell;  // min-heap
    };
    auto relax = [&](int32_t cell, int32_t cost, uint8_t dir) {
        if (dStamp_[size_t(cell)] == dGen_ && dDist_[size_t(cell)] <= cost) return;
        dStamp_[size_t(cell)] = dGen_;
        dDist_[size_t(cell)] = cost;
        dDir_[size_t(cell)] = dir;
        heap.push_back({cost, cell, dir});
        std::push_heap(heap.begin(), heap.end(), less);
    };
    const int32_t startCell = int32_t(start.z * w_ + start.x);
    relax(startCell, 0, 0xff);   // 0xff = no incoming direction

    // Node budget: retail bounds phase 2 at map cells / 10 (icd 0x415c47). Beyond it
    // the scheduler abandons phase 2 and falls back -- for us, to the tracer route.
    const int budget = std::max(64, (w_ * h_) / 10);
    int expanded = 0;
    bool reached = false;

    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), less);
        const Node top = heap.back();
        heap.pop_back();
        // Stale heap entry (a cheaper path to this cell already popped): skip.
        if (dStamp_[size_t(top.cell)] != dGen_ || dDist_[size_t(top.cell)] != top.cost)
            continue;
        if (size_t(top.cell) == goalI) { reached = true; break; }
        if (++expanded > budget) break;

        const int cx = top.cell % w_, cz = top.cell / w_;
        for (int d = 0; d < 8; ++d) {
            const int nx = cx + kDirX[d], nz = cz + kDirZ[d];
            if (nx < 0 || nz < 0 || nx >= w_ || nz >= h_) continue;
            const int gc = gradeCost(score(nx, nz));
            if (gc < 0) continue;                       // impassable
            // NO corner-cut guard here (unlike the tracer's stepLegal): retail's cost
            // function (0x413e70) grades only the destination cell, so a diagonal may
            // round a wall corner. Validated against an OBSERVED retail route: on a
            // wall-detour the search reaches the goal at cost 1084, byte-for-byte the
            // cost of the route retail's own search produced on the same grid. The
            // mover's per-cell solidity handles the corner the diagonal rounds.
            // Turn cost from the parent's incoming heading. The start (dir 0xff) pays
            // none; otherwise the |rotational difference|, folded to 0..4.
            int turn = 0;
            if (top.dir != 0xff) {
                int diff = (d - int(top.dir)) & 7;
                if (diff > 4) diff = 8 - diff;
                turn = kTurnCost[diff];
            }
            const int32_t nc = top.cost + kStepCost[d] + turn + gc;
            relax(int32_t(nz * w_ + nx), nc, uint8_t(d));
        }
    }
    if (!reached) return false;

    // Reconstruct start->goal from the parent-direction map, emitting a waypoint only
    // at each change of direction (retail's route is direction-change corners), and
    // capping at retail's 64 waypoints (icd 0x4e4ea0).
    std::vector<PathCell> rev;
    int c = int(goalI);
    int lastDir = -1;
    for (int guard = 0; guard < w_ * h_ + 8; ++guard) {
        const int cx = c % w_, cz = c / w_;
        if (cx == start.x && cz == start.z) break;
        const int d = dDir_[size_t(c)];
        if (d == 0xff) break;
        if (d != lastDir) { rev.push_back({cx, cz}); lastDir = d; }
        c = (cz - kDirZ[d]) * w_ + (cx - kDirX[d]);
    }
    out.assign(rev.rbegin(), rev.rend());
    if (out.size() > 64) out.resize(64);
    return true;
}

// Reconstruct the breadcrumb walk back from `end`. Retail reconstructs on FAILURE
// too (0x415170), from the closest-approach cell -- the best-effort route that walks
// a unit to the reachable edge of wherever it was sent, rather than leaving it to
// steer a straight line into the first wall.
void PathSearch::buildRouteTo(PathCell end) {
    out.clear();
    if (!inside(end)) return;
    std::vector<PathCell> rev;
    ++walkGen_;
    PathCell c = end;
    int lastDir = -1;
    for (int guard = 0; guard < 8192; ++guard) {
        if (c.x == start.x && c.z == start.z) break;
        const size_t i = size_t(c.z) * size_t(w_) + size_t(c.x);
        if (!seen(i) || !(flag_[i] & kSeen)) break;
        if (walkStamp_[i] == walkGen_) break;   // belt and braces against a cycle
        walkStamp_[i] = walkGen_;
        const int d = from_[i] & 7;
        // Retail's goal-crowding check runs during this walk (icd 0x414563): a cell
        // the search could only pass through as same-way traffic (kScore5), lying
        // within the type's tolerance of the goal, marks the goal area as crowded --
        // the route leans on bodies that may not part by the time the unit arrives.
        if (flag_[i] & kScore5) {
            if (tolCells > 0 &&
                std::max(std::abs(c.x - goal.x), std::abs(c.z - goal.z)) < tolCells)
                goalCrowded = true;
            else
                sawTraffic = true;
        }
        if (d != lastDir) { rev.push_back(c); lastDir = d; }
        PathCell p{c.x - kDirX[d], c.z - kDirZ[d]};
        if (!inside(p) || (p.x == c.x && p.z == c.z)) break;
        c = p;
    }
    if (rev.empty() || rev.front().x != goal.x || rev.front().z != goal.z)
        rev.insert(rev.begin(), goal);
    for (auto it = rev.rbegin(); it != rev.rend(); ++it)
        if (out.empty() || out.back().x != it->x || out.back().z != it->z)
            out.push_back(*it);
    if (out.size() > kRawRouteCap) out.resize(kRawRouteCap);
}

// Has `c` landed back on the straight line from `org` to the goal? Retail
// normalises the signs so the goal delta is positive, then accepts a cell that
// is either along the first leg or at the far x with z in range
// (icd 0x414dc4..0x414df4). This is the M-line re-crossing test that tells a
// bug algorithm it has finished going around.
bool PathSearch::onGoalLine(PathCell org, PathCell c) const {
    int gx = goal.x - org.x, gz = goal.z - org.z;
    int cx = c.x - org.x, cz = c.z - org.z;
    if (gx < 0) { gx = -gx; cx = -cx; }
    if (gz < 0) { gz = -gz; cz = -cz; }
    if (cz == 0 && cx > 0 && cx <= gx) return true;
    if (cx == gx && cz > 0 && cz <= gz) return true;
    return false;
}

// Is a step from `c` in direction `d` one the MOVER will actually make?
//
// For a cardinal step, the destination cell is the whole question. For a DIAGONAL, the
// body passes between the two orthogonal neighbours, and the movers refuse to cut that
// corner -- as does lineFits, which validates the shortcut afterwards. The search used to
// ask only about the destination, so it could plan a diagonal squeeze between two blocked
// cells: the mover then stalls at the corner it will not cut, and the shortcut validation
// rejects the very connection the search had just committed to. Planning a move that two
// later stages both refuse is worse than planning a longer one.
static bool stepLegal(const std::function<int(int, int)>& score, PathCell c, int d) {
    if (kDirX[d] == 0 || kDirZ[d] == 0) return true;   // cardinal: nothing to squeeze past
    return score(c.x + kDirX[d], c.z) >= kCellThreshold &&
           score(c.x, c.z + kDirZ[d]) >= kCellThreshold;
}

// One step of a boundary trace. `rot` is +1 for the cursor sweeping one way
// round the obstacle and -1 for its twin: retail runs BOTH at once (icd
// 0x414c52 rotates by -2/-3, 0x414e23 by +2/+3) and takes whichever regains
// the goal line first, which is what lets it round a wall from either end.
bool PathSearch::traceStep(const std::function<int(int, int)>& score,
                           PathCell& c, int& d, int rot) {
    const int limit = (d + 3 * rot) & 7;
    int probe = (d + 2 * rot) & 7;
    for (;;) {
        const PathCell n{c.x + kDirX[probe], c.z + kDirZ[probe]};
        const int s = score(n.x, n.z);
        ++visited;
        if (s >= kCellThreshold && stepLegal(score, c, probe)) {
            c = n;
            d = probe;
            mark(c, probe, s);
            return true;
        }
        probe = (probe - rot) & 7;
        if (probe == limit) return false;   // swept the circle: boxed in
    }
}



PathSearch::Result PathSearch::step(const std::function<int(int, int)>& score,
                                    int quantum) {
    if (w_ <= 0) return Result::Failed;     // reset() was never called
    for (;;) {
        if (work >= quantum) return Result::Suspended;   // icd 0x415028, -2
        if (visited > visitLimit) {
            if (g_pathDbg) std::fprintf(stderr,
                "  search gave up: visitLimit, phase=%d visited=%d work=%d best=%d cur=(%d,%d)\n",
                int(phase), visited, work, best, cur.x, cur.z);
            phase = Phase::Failed;
            buildRouteTo(bestCell);
            return Result::Failed;
        }

        switch (phase) {
case Phase::Init: {
            if (!inside(start) || !inside(goal)) {
                phase = Phase::Failed;
                return Result::Failed;
            }
            best = pathDist(start, goal);
            bestCell = start;
            cur = start;
            org = start;
            // The goal carries the terminal flag both marches test for
            // (icd 0x414b9f / 0x414d8f test bit 0x4).
            {
                const size_t gi = size_t(goal.z) * size_t(w_) + size_t(goal.x);
                touch(gi);
                flag_[gi] |= kGoal;
            }
if (best == 0) { phase = Phase::Done;
                if (!buildDijkstraRoute(score)) buildRoute();
                return Result::Arrived; }
            if (score(start.x, start.z) < kCellThreshold) {
                phase = Phase::Failed;
                return Result::Failed;
            }
            phase = Phase::March;
            break;
        }

        // State 1 (icd 0x414833): the OCTANT march, run once from the true
        // start. It counts what it crosses and gives up on the third occupied
        // cell; ordinary ground is counted but only capped once the road run
        // has also reached 3 (0x4148d7..0x4148fa).
        case Phase::March: {
            work += kWorkMarch;
            const int d = pathDirFromDelta(goal.x - cur.x, goal.z - cur.z);
            const PathCell n{cur.x + kDirX[d], cur.z + kDirZ[d]};
            ++visited;
            const int s = score(n.x, n.z);
            bool giveUp = false;
            if (s < kCellThreshold || !stepLegal(score, cur, d)) {
                giveUp = true;
            } else if (s == kCellSlope) {
                if (nOccupied >= 3) giveUp = true; else ++nOccupied;
            } else if (s == kCellRoad) {
                if (nRoad >= 3) { if (nGround >= 3) giveUp = true; }
                else ++nRoad;
            } else {
                ++nGround;
            }
            if (giveUp) { org = cur; phase = Phase::CardMarch; break; }
            cur = n;
            mark(cur, d, s);
            if (atGoal(cur)) { phase = Phase::Done;
                if (!buildDijkstraRoute(score)) buildRoute();
                return Result::Arrived; }
            const int dist = pathDist(cur, goal);
            if (dist < best) { best = dist; bestCell = cur; }
            break;
        }

        // State 2 (icd 0x414a6a): the CARDINAL march from the current origin,
        // laying breadcrumbs, until something blocks it.
        case Phase::CardMarch: {
            work += kWorkTraceInit;
            const int d = cardinalToward(goal.x - org.x, goal.z - org.z);
            const PathCell n{org.x + kDirX[d], org.z + kDirZ[d]};
            const int s = score(n.x, n.z);
            ++visited;
            if (s < kCellThreshold) {          // blocked -> start both traces
                curA = org; curB = org;
                // Seed so each cursor's FIRST probe lands on the direction that
                // was just refused, then its own rule takes over. Emulating the
                // original over this exact case shows both cursors probing the
                // blocked heading first (6, then 7,0 one way and 5 the other);
                // seeding both with the blocked direction instead makes them
                // open at blocked+-2 and skip straight past it, which is how a
                // two-step detour turned into a long wander.
                dirA = (d - 2) & 7;            // probes from (dirA+2) == d
                dirB = (d + 2) & 7;            // probes from (dirB-2) == d
                started = false;
                phase = Phase::Trace;
                break;
            }
            org = n;
            mark(org, d, s);
            if (atGoal(org)) { phase = Phase::Done;
                if (!buildDijkstraRoute(score)) buildRoute();
                return Result::Arrived; }
            const int dist = pathDist(org, goal);
            if (dist < best) { best = dist; bestCell = org; }
            break;
        }

        // State 3 (icd 0x414c25): the twin traces. Whichever cursor regains the
        // origin-to-goal line first becomes the new march origin
        // (0x414fde / 0x414ff8 both drop back into state 2).
        case Phase::Trace: {
            work += kWorkTraceStep;
            const bool okA = traceStep(score, curA, dirA, +1);
            if (okA) {
                if (atGoal(curA)) { phase = Phase::Done;
                    if (!buildDijkstraRoute(score)) buildRoute();
                    return Result::Arrived; }
                const int dist = pathDist(curA, goal);
                if (dist < best) { best = dist; bestCell = curA; }
                if (onGoalLine(org, curA)) { org = curA; phase = Phase::CardMarch; break; }
            }
            // The traces give up when they MEET (icd 0x414ec7 compares cursor B's
            // cell and direction against cursor A's): two cursors going opposite
            // ways round the same outline can only coincide if the outline is
            // closed, so there is no way through. Comparing each cursor with its
            // own start instead -- which is what I had -- lets a trace wander most
            // of the map before it notices, which is what burned the visit limit.
            if (started && curA.x == curB.x && curA.z == curB.z && dirA == dirB) {
                phase = Phase::Failed;
                buildRouteTo(bestCell);
                return Result::Failed;
            }
            const bool okB = traceStep(score, curB, dirB, -1);
            if (okB) {
                if (atGoal(curB)) { phase = Phase::Done;
                    if (!buildDijkstraRoute(score)) buildRoute();
                    return Result::Arrived; }
                const int dist = pathDist(curB, goal);
                if (dist < best) { best = dist; bestCell = curB; }
                if (onGoalLine(org, curB)) { org = curB; phase = Phase::CardMarch; break; }
            }
            started = true;
            if (!okA && !okB) {
                if (g_pathDbg) std::fprintf(stderr,
                    "  search gave up: both traces boxed in at A(%d,%d) B(%d,%d) visited=%d\n",
                    curA.x, curA.z, curB.x, curB.z, visited);
                phase = Phase::Failed;
                buildRouteTo(bestCell);
                return Result::Failed;
            }
            break;
        }

        case Phase::Done: return Result::Arrived;
        case Phase::Failed: return Result::Failed;
        }
    }
}

// Point a pool slot at this request and start its search there.
void PathService::admit(int unitId, Entry& e, int slot) {
    e.slot = slot;
    e.cap = 0;
    slotOwner_[size_t(slot)] = unitId;
    PathSearch& ps = pool_[size_t(slot)];
    // reset() only reallocates when the map size changed, so a slot that has
    // already run a search on this map recycles its arrays for free.
    ps.reset(e.mapW, e.mapH);
    ps.unitId = unitId;
    ps.start = e.start;
    ps.goal = e.goal;
    ps.tolCells = e.tolCells;
    ps.cur = e.start;
    ps.priority = e.priority;
}

void PathService::release(Entry& e) {
    if (e.slot >= 0) slotOwner_[size_t(e.slot)] = -1;
    e.slot = -1;
}

void PathService::request(int unitId, PathCell start, PathCell goal, int mapW,
                          int mapH, Fixed goalX, Fixed goalZ, int player, bool priority,
                          int tolCells) {
    // A RE-REQUEST FOR THE SAME SEARCH LETS IT RUN. Everything below restarts the
    // search from scratch (cap = 0), which is right when the question changed and
    // ruinous when it did not: the sim re-asks every kPathRetryTicks (120 ticks, 4s)
    // for any unit not making headway, while a saturated queue takes far longer than
    // that to reach it. Measured at 3300 units: 1445 pending against 12 slots, average
    // wait 490 ticks (~16s), so a starved unit reset its own search about four times
    // before it could ever have finished -- 19,985 requests yielded 996 completions.
    // It never got a route, and with no local obstacle avoidance in the steering,
    // waiting for a route means walking straight at the goal into whatever is between.
    //
    // "The same search" is the same GOAL from ~the same PLACE. The start is allowed to
    // drift a cell because a unit shuffling on the spot has not invalidated anything;
    // move further than that and the old answer really is for a different question, so
    // it restarts as before. A unit that is actually travelling is making headway and
    // never reaches the retry path at all.
    if (auto it = q_.find(unitId); it != q_.end()) {
        Entry& ex = it->second;
        const int dx = ex.start.x - start.x, dz = ex.start.z - start.z;
        if (ex.goal.x == goal.x && ex.goal.z == goal.z &&
            dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1) {
            // Keep the search's PROGRESS (cap, slot) but take the new request's exact
            // destination. goalX/goalZ never enter the search -- it works in cells --
            // they are carried through to done(), which installs them as the order's
            // point. Holding the old pair meant a fresh move order to a different spot
            // inside the SAME 16px cell silently walked to where the previous order
            // had pointed.
            ex.goalX = goalX;
            ex.goalZ = goalZ;
            if (priority) ex.priority = true;   // may still be promoted
            return;
        }
    }
    Entry& e = q_[unitId];
    const int slot = e.slot;      // keep the slot if this unit already holds one
    e.start = start;
    e.goal = goal;
    e.mapW = mapW;
    e.mapH = mapH;
    e.goalX = goalX;
    e.goalZ = goalZ;
    e.player = player;
    e.tolCells = tolCells;
    e.priority = priority;
    e.cap = 0;
    e.slot = -1;
    // A re-request for a unit that is already searching restarts it in place,
    // exactly as it did when every request owned its own search.
    if (slot >= 0) admit(unitId, e, slot);
}

void PathService::cancel(int unitId) {
    auto it = q_.find(unitId);
    if (it == q_.end()) return;
    release(it->second);
    q_.erase(it);
}

void PathService::tick(const std::function<int(int, int, int)>& score,
                       const std::function<void(int, const std::vector<PathCell>&,
                                                Fixed, Fixed, bool, bool, bool)>& done) {
    if (q_.empty()) return;
    if (pool_.empty()) {
        pool_.resize(kMaxActiveSearches);
        slotOwner_.assign(kMaxActiveSearches, -1);
    }

    ++tickNo_;

    // ADMIT, RUN, THEN REFILL WHILE BUDGET REMAINS.
    //
    // This used to admit once, run the admitted set, and release finished slots at the
    // end -- so a slot freed by a search that completed early sat idle for the rest of
    // the tick no matter how much work budget was left. Throughput was pinned at
    // kMaxActiveSearches per tick regardless of how cheap the searches were. Measured on
    // trivial requests: one costs 72 work, so 12 slots spend 864 of the 12000 budget --
    // 7.2% used -- while 48 requests still took four ticks and 96 took eight.
    //
    // (The comment above kMaxActiveSearches argued throughput was unaffected by bounding
    // the pool. That holds when searches are expensive enough to consume their slice; it
    // does not hold for cheap ones, which finish far inside their quantum and leave the
    // rest of the budget unusable until the next tick.)
    //
    // Now a completion frees its slot immediately and the freed slot is refilled from the
    // queue while integer budget remains. Two properties are preserved deliberately:
    //
    //   * ADMISSION ORDER. Still the rotating cursor over unit-id order, so every peer
    //     admits the same requests in the same sequence on the same tick.
    //   * ONE SLICE PER TICK. A search that suspends is NOT run again this tick -- only
    //     newly admitted requests run in a refill round. Re-running suspended searches
    //     would change their pacing, which is hashed state.
    int spent = 0;
    for (int round = 0; round < kMaxActiveSearches + 1; ++round) {
        // Fill every free slot from the queue, starting after the last unit id admitted
        // and wrapping, so a low unit id that re-requests every tick cannot hold the pool
        // against a higher one.
        int free = 0;
        for (int owner : slotOwner_) if (owner < 0) ++free;
        if (free > 0) {
            for (int pass = 0; pass < 2 && free > 0; ++pass) {
                auto it = pass == 0 ? q_.upper_bound(admitCursor_) : q_.begin();
                const auto stop = pass == 0 ? q_.end() : q_.upper_bound(admitCursor_);
                for (; it != stop && free > 0; ++it) {
                    Entry& e = it->second;
                    if (e.slot >= 0) continue;
                    int slot = -1;
                    for (size_t i = 0; i < slotOwner_.size(); ++i)
                        if (slotOwner_[i] < 0) { slot = int(i); break; }
                    if (slot < 0) break;
                    admit(it->first, e, slot);
                    ++requests_;
                    admitCursor_ = it->first;
                    --free;
                }
            }
        }

        // Count PLAYERS, not requests -- emulated to settle it (docs/pathfinding-port
        // .md): two players with one pending unit each split the budget 500/500, and so
        // do two players with fifty each, and so does 1-pending against 99-pending.
        // Retail's 0x634674 table holds a per-player pending count that the scheduler
        // (0x4164fa) only ever tests > 0; the bucket increments once per player, and a
        // flagged player is worth five ordinary ones. Splitting per REQUEST -- which is
        // what stood here -- let one player with 500 units starve another, which retail
        // cannot do. Only entries that have not yet had a slice this tick count.
        uint32_t normals = 0, specials = 0;   // bitmasks over player slots
        for (const auto& [id, e] : q_) {
            if (e.slot < 0 || e.ranAt == tickNo_) continue;
            const uint32_t bit = 1u << (unsigned(e.player) & 31);
            if (e.priority) specials |= bit; else normals |= bit;
        }
        normals &= ~specials;                  // a player is one class, not both
        const int a = std::popcount(normals), b = std::popcount(specials);
        const int share = a + 5 * b;
        if (share <= 0) break;                 // nothing left to run
        const int remaining = budget_ - spent;
        if (remaining <= 0) break;             // the budget is a real cap
        const int quantum = std::max(1, remaining / share);

        // THE CALLBACK IS NOT INVOKED WHILE THE QUEUE IS BEING WALKED.
        //
        // `done` installs the route, and installing a route can ask for another one --
        // a route that no longer connects to its unit requests a repair. That reaches
        // back into request()/cancel() and mutates q_ underneath this very loop, which
        // segfaulted the moment a crowd made repairs common (a crowd is exactly when it
        // matters). Collect the finished searches, finish walking the queue, release the
        // slots, and only then hand the routes over -- so a callback is free to queue
        // whatever it likes.
        struct Finished { int id; std::vector<PathCell> route; Fixed gx, gz;
                          bool failed; bool crowded; bool traffic; };
        std::vector<Finished> finished;
        for (auto& [id, e] : q_) {
            if (e.slot < 0 || e.ranAt == tickNo_) continue;
            // THE BUDGET IS CHECKED HERE, not only between rounds. The quantum is
            // computed once per round from what was left at the START of it, and
            // completions are charged as they happen -- so the searches later in the same
            // round were still handed a full slice from a budget already spent. Measured:
            // 12 open-ground requests at 952 search + 540 completion work each charged
            // 17,904 against a 12,000 budget. A cap that can be overrun by half again is
            // not a cap, and the whole point of bounding this is that a big order becomes
            // a short queue rather than a frame spike.
            //
            // Leaving the entry unmarked means it simply waits: next tick it is first in
            // line with its state intact.
            if (spent >= budget_) break;
            e.ranAt = tickNo_;
            e.cap += quantum * (e.priority ? 5 : 1);
            auto sc = [&](int cx, int cz) { return score(id, cx, cz); };
            PathSearch& ps = pool_[size_t(e.slot)];
            const int workBefore = ps.work;
            const PathSearch::Result r = ps.step(sc, e.cap);
            spent += ps.work - workBefore;
            workSpent_ += uint64_t(ps.work - workBefore);
            if (r == PathSearch::Result::Arrived) {
                // Charge the completion BEFORE handing the route over: the callback
                // smooths it, and that cost belongs to this tick's budget.
                spent += kWorkCompleteBase + kWorkPerCorner * int(ps.out.size());
                ++completions_;
                finished.push_back({id, ps.out, e.goalX, e.goalZ,
                                    false, ps.goalCrowded, ps.sawTraffic});
            } else if (r == PathSearch::Result::Failed) {
                spent += kWorkCompleteBase;
                ++failures_;
                // A failed search still delivers its BEST-EFFORT route -- the walk to
                // its closest approach (icd 0x415170 reconstructs on failure too). An
                // empty route here meant a unit sent at a crowded or walled goal got
                // NOTHING and steered a straight line instead; retail's walks to the
                // reachable edge. The route may legitimately be empty (start boxed
                // in, nothing visited): the installer treats that as the old failure.
                // The FAILURE REPORT's flags come from the visit-time accumulators,
                // bit 0 taking priority over bit 1 exactly as 0x414450's report
                // branch does (emulated: both set -> bit0 alone). A failure that
                // saw no score-5 cells at all reports NOTHING -- and that is not a
                // rest: with no bits set, the service worker's PLAIN branch governs
                // (elapsed >= 120 and a 1-in-120 roll per frame), which is how a
                // long haul that failed at its visit limit chains route by route,
                // and how a unit against a sealed wall keeps gently probing it.
                finished.push_back({id, ps.out, e.goalX, e.goalZ,
                                    true, ps.visNear,
                                    !ps.visNear && ps.visFar});
            }
            // Suspended: keep the entry, resume next tick with its state intact.
        }
        // Hand the slots back NOW rather than at the end of the tick, so the next round
        // can use them. This is the whole point of the loop.
        for (const Finished& f : finished) {
            auto it = q_.find(f.id);
            if (it == q_.end()) continue;
            release(it->second);
            q_.erase(it);
        }
        // Queue walked, slots free: now it is safe for a callback to re-enter.
        for (const Finished& f : finished)
            done(f.id, f.route, f.gx, f.gz, f.failed, f.crowded, f.traffic);
        if (finished.empty()) break;   // no slot freed -> a refill round would do nothing
    }
}

}   // namespace tak::sim
