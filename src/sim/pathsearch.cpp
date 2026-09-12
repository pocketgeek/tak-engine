#include "sim/pathsearch.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace tak::sim {

static const bool g_pathDbg = getenv("TAK_PATHDBG") != nullptr;

int pathDist(PathCell a, PathCell b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

int pathDirFromDelta(int dx, int dz) {
    // Octant, using the rotational order of the direction tables.
    if (dx == 0 && dz == 0) return 0;
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
    w_ = mapW;
    h_ = mapH;
    // icd 0x414797: (width + height) * 20. Retail abandons a search that wanders
    // this far and leaves the unit on its straight segment, so long or tangled
    // routes legitimately fail rather than costing unbounded work.
    visitLimit = (mapW + mapH) * 20;
    flag_.assign(size_t(w_) * size_t(h_), 0);
    from_.assign(size_t(w_) * size_t(h_), 0);
    phase = Phase::Init;
    best = 0;
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
    // FIRST VISIT WINS. Overwriting the incoming direction on a revisit turns
    // the parent map into a graph with cycles, and the backtrack then walks a
    // little loop for ever instead of reaching the start -- observed as a
    // 4-cell cycle repeated to the 64-waypoint clamp. Retail's separate bitmap
    // at +0x2c is exactly this guard.
    if (flag_[i] & kSeen) return;
    from_[i] = uint8_t(d);
    flag_[i] = uint8_t(flag_[i] | kSeen | (score == 5 ? kScore5 : 0));
}

bool PathSearch::atGoal(PathCell c) const {
    if (!inside(c)) return false;
    return (flag_[size_t(c.z) * size_t(w_) + size_t(c.x)] & kGoal) != 0;
}

// Walk the breadcrumbs back from the goal and hand out the corners in travel
// order (icd 0x414450 does the same over its own 0x218-byte buffer). Only
// direction CHANGES become waypoints -- a straight run needs no intermediate
// points -- and the navigator caps the list at 64 (0x4e4ea0).
void PathSearch::buildRoute() {
    out.clear();
    if (!inside(goal)) return;
    std::vector<PathCell> rev;
    std::vector<uint8_t> walked(flag_.size(), 0);
    PathCell c = goal;
    int lastDir = -1;
    for (int guard = 0; guard < 8192; ++guard) {
        if (c.x == start.x && c.z == start.z) break;
        const size_t i = size_t(c.z) * size_t(w_) + size_t(c.x);
        if (!(flag_[i] & kSeen)) break;
        if (walked[i]) break;          // belt and braces against a cycle
        walked[i] = 1;
        const int d = from_[i] & 7;
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
    if (out.size() > 64) out.resize(64);
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
        if (s >= kCellThreshold) {
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
            phase = Phase::Failed; return Result::Failed;
        }

        switch (phase) {
        case Phase::Init: {
            if (!inside(start) || !inside(goal)) {
                phase = Phase::Failed;
                return Result::Failed;
            }
            best = pathDist(start, goal);
            cur = start;
            org = start;
            // The goal carries the terminal flag both marches test for
            // (icd 0x414b9f / 0x414d8f test bit 0x4).
            flag_[size_t(goal.z) * size_t(w_) + size_t(goal.x)] |= kGoal;
            if (best == 0) { phase = Phase::Done; buildRoute(); return Result::Arrived; }
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
            if (s < kCellThreshold) {
                giveUp = true;
            } else if (s == kCellOccupied) {
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
            if (atGoal(cur)) { phase = Phase::Done; buildRoute(); return Result::Arrived; }
            const int dist = pathDist(cur, goal);
            if (dist < best) best = dist;
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
                dirA = d; dirB = d;
                phase = Phase::Trace;
                break;
            }
            org = n;
            mark(org, d, s);
            if (atGoal(org)) { phase = Phase::Done; buildRoute(); return Result::Arrived; }
            const int dist = pathDist(org, goal);
            if (dist < best) best = dist;
            break;
        }

        // State 3 (icd 0x414c25): the twin traces. Whichever cursor regains the
        // origin-to-goal line first becomes the new march origin
        // (0x414fde / 0x414ff8 both drop back into state 2).
        case Phase::Trace: {
            work += kWorkTraceStep;
            const bool okA = traceStep(score, curA, dirA, +1);
            if (okA) {
                if (atGoal(curA)) { phase = Phase::Done; buildRoute(); return Result::Arrived; }
                const int dist = pathDist(curA, goal);
                if (dist < best) best = dist;
                if (onGoalLine(org, curA)) { org = curA; phase = Phase::CardMarch; break; }
            }
            const bool okB = traceStep(score, curB, dirB, -1);
            if (okB) {
                if (atGoal(curB)) { phase = Phase::Done; buildRoute(); return Result::Arrived; }
                const int dist = pathDist(curB, goal);
                if (dist < best) best = dist;
                if (onGoalLine(org, curB)) { org = curB; phase = Phase::CardMarch; break; }
            }
            if (!okA && !okB) {
                if (g_pathDbg) std::fprintf(stderr,
                    "  search gave up: both traces boxed in at A(%d,%d) B(%d,%d) visited=%d\n",
                    curA.x, curA.z, curB.x, curB.z, visited);
                phase = Phase::Failed; return Result::Failed;
            }
            break;
        }

        case Phase::Done: return Result::Arrived;
        case Phase::Failed: return Result::Failed;
        }
    }
}

void PathService::request(int unitId, PathCell start, PathCell goal, int mapW,
                          int mapH, float goalX, float goalZ, bool priority) {
    Entry e;
    e.search.reset(mapW, mapH);
    e.search.unitId = unitId;
    e.search.start = start;
    e.search.goal = goal;
    e.search.cur = start;
    e.search.priority = priority;
    e.goalX = goalX;
    e.goalZ = goalZ;
    e.priority = priority;
    q_[unitId] = std::move(e);
}

void PathService::cancel(int unitId) {
    q_.erase(unitId);
}

void PathService::tick(const std::function<int(int, int, int)>& score,
                       const std::function<void(int, const std::vector<PathCell>&,
                                                float, float)>& done) {
    if (q_.empty()) return;
    // Count the two classes exactly as the scheduler does, then split the
    // budget: a flagged request is worth five ordinary ones (icd 0x4164fa).
    int a = 0, b = 0;
    for (const auto& [id, e] : q_) (e.priority ? b : a) += 1;
    const int share = a + 5 * b;
    if (share <= 0) return;
    const int quantum = std::max(1, budget_ / share);

    std::vector<int> finished;
    for (auto& [id, e] : q_) {
        e.cap += quantum * (e.priority ? 5 : 1);
        auto sc = [&](int cx, int cz) { return score(id, cx, cz); };
        const PathSearch::Result r = e.search.step(sc, e.cap);
        if (r == PathSearch::Result::Arrived) {
            done(id, e.search.out, e.goalX, e.goalZ);
            finished.push_back(id);
        } else if (r == PathSearch::Result::Failed) {
            done(id, {}, e.goalX, e.goalZ);
            finished.push_back(id);
        }
        // Suspended: keep the entry, resume next tick with its state intact.
    }
    for (int id : finished) q_.erase(id);
}

}   // namespace tak::sim
