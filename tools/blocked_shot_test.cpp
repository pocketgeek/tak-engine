// blocked_shot_test: an attacker whose shots keep stopping short of its target
// on something it cannot destroy walks to a spot with a clear shot.
//
//   blocked_shot_test <retail-install-dir> [--legion]
//
// Retail admits a shot on range alone (52ae90/52fe30) and its projectile
// collision (52a4d0) stops the shot at the first terrain or feature cell it
// meets, so a retail archer behind a hill crest empties its quiver into the
// hill. The engine's 2D sight test sees only cells the nav grid blocks; a
// passable crest, water and tall scenery that does not block movement are
// invisible to it. World::noteShotImpact counts those impacts and
// World::repositionForShot moves the attacker (see sim.cpp).
//
// Shipped Aramon Archers (arabow, a ballistic Standard Arrow) against a
// Swordsman standing behind a passable ridge, and behind a row of shipped
// Zhon campfire art (ZonFireD01: indestructible, nonblocking, 'height'
// set), in Retail and Legion pathfinding. Measured: shots landing short,
// damage to the target, and where the archer ends up; plus the limits
// (Hold Position stays; an open field is untouched).
//
// A dev harness; not shipped in a game.

#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include "tnt/tnt.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace tak;

namespace {
int failures = 0;
sim::PathfindingMode mode = sim::PathfindingMode::Retail;

void check(bool ok, const char* what, const std::string& detail = "") {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) ++failures;
}

enum class Obstacle { None, Ridge, Campfires };

struct Outcome {
    int shortShots = 0;      // ground/scenery impacts well short of the target
    int targetHits = 0;      // impacts on the target
    float damage = 0;        // target HP lost
    float moved = 0;         // archer's distance from where it started
    int repositions = 0;
    uint64_t legionMoves = 0; // Legion navigator move updates (Legion mode)
};

// A 64x64 map at height 30. Ridge: columns 17..23 climb 12 per cell to a
// 90-high crest and fall again (a 3x3 spread of 24: passable, and clear on
// the nav grid). Campfires: column 19, rows 26..38, ZonFireD01.
Outcome run(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, Obstacle obstacle,
            int moveState, int ticks) {
    tnt::Map map;
    map.width = map.height = 64;
    map.seaLevel = 0;
    map.heights.assign(64 * 64, 30);
    map.features.assign(64 * 64, 0xffff);
    if (obstacle == Obstacle::Ridge) {
        const int profile[] = {42, 54, 66, 78, 90, 78, 66, 54, 42};
        for (int z = 0; z < 64; ++z)
            for (int i = 0; i < 9; ++i) map.heights[size_t(z) * 64 + size_t(16 + i)] = uint8_t(profile[i]);
    } else if (obstacle == Obstacle::Campfires) {
        map.featureNames = {"ZonFireD01"};
        for (int z = 20; z <= 44; ++z) map.features[size_t(z) * 64 + 19] = 0;
    }
    sim::World w;
    w.setVisPlayer(-1);
    w.setPathfindingMode(mode);
    w.setTerrain(map.heights, map.width, map.height, map.seaLevel, &map.features);
    sim::registerMapFeatures(w, map, vfs, &reg);
    w.setPathService(true);
    const sim::UnitType* bow = reg.find("arabow");
    const sim::UnitType* sword = reg.find("arasword");
    if (!bow || !sword) { check(false, "shipped arabow and arasword load"); return {}; }
    const float sx = 14 * 16 + 8, sz = 32 * 16, tx = 26 * 16 + 8, tz = 32 * 16;
    const int archer = w.spawn(bow, sx, sz, 0, 0);
    const int target = w.spawn(sword, tx, tz, 0, 1);
    // The target stands still and never fights back.
    w.setStance(target, 2);
    w.unit(target)->moveState = 0;
    w.unit(target)->fireState = 0;
    w.unit(archer)->moveState = moveState;
    for (int i = 0; i < 30; ++i) w.tick(1.f / 30);
    const float hp0 = w.unit(target)->hp.toFloat();
    w.attack(archer, target, false);
    Outcome out;
    for (int i = 0; i < ticks; ++i) {
        w.tick(1.f / 30);
        const sim::Unit* a = w.unit(archer);
        const sim::Unit* t = w.unit(target);
        if (!a || !t || !t->alive()) break;
        for (const auto& h : w.hits()) {
            if (h.fromPlayer != 0) continue;
            if (h.victimId == target) { ++out.targetHits; continue; }
            const float dx = h.x - t->x.toFloat(), dz = h.z - t->z.toFloat();
            if (dx * dx + dz * dz > 48.f * 48.f) ++out.shortShots;
        }
    }
    const sim::Unit* a = w.unit(archer);
    const sim::Unit* t = w.unit(target);
    out.damage = hp0 - (t && t->alive() ? t->hp.toFloat() : 0.f);
    if (a) {
        const float dx = a->x.toFloat() - sx, dz = a->z.toFloat() - sz;
        out.moved = std::sqrt(dx * dx + dz * dz);
        out.repositions = a->repositions;
    }
    out.legionMoves = w.legionStats().moves;
    return out;
}

std::string describe(const Outcome& o) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "short=%d hits=%d damage=%.0f moved=%.0fpx repositions=%d",
                  o.shortShots, o.targetHits, o.damage, o.moved, o.repositions);
    return buf;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: blocked_shot_test <retail-install-dir> [--legion]\n"); return 2; }
    for (int i = 2; i < argc; ++i)
        if (std::string(argv[i]) == "--legion") mode = sim::PathfindingMode::Legion;
    hpi::Vfs vfs = hpi::mountRetailRoot(argv[1]);
    sim::TypeRegistry reg;
    reg.loadMoveInfo(vfs, "gamedata/moveinfo.tdf");
    reg.loadDir(vfs, "units/");
    std::printf("[%s pathfinding]\n", mode == sim::PathfindingMode::Legion ? "Legion" : "Retail");
    const int kTicks = 900;   // 30 s: about eleven Standard Arrows

    const Outcome open = run(vfs, reg, Obstacle::None, 1, kTicks);
    std::printf("  open field:  %s\n", describe(open).c_str());
    check(open.targetHits > 0 && open.repositions == 0 && open.moved < 8,
          "an open shot connects from where the archer stands, no reposition");

    for (const Obstacle obstacle : {Obstacle::Ridge, Obstacle::Campfires}) {
        const char* name = obstacle == Obstacle::Ridge ? "ridge" : "campfires";
        const Outcome held = run(vfs, reg, obstacle, 0, kTicks);
        const Outcome free = run(vfs, reg, obstacle, 1, kTicks);
        std::printf("  %-10s hold position: %s\n", name, describe(held).c_str());
        std::printf("  %-10s maneuver:      %s\n", name, describe(free).c_str());
        check(held.shortShots >= 3 && held.damage == 0 && held.moved < 8,
              obstacle == Obstacle::Ridge ? "ridge: the shot is blocked, and Hold Position stays and fires on"
                                          : "campfires: the shot is blocked, and Hold Position stays and fires on");
        check(free.repositions >= 1 && free.damage > 0 && free.shortShots <= 4,
              obstacle == Obstacle::Ridge ? "ridge: the archer walks to a clear shot and hurts the target"
                                          : "campfires: the archer walks to a clear shot and hurts the target");
        if (mode == sim::PathfindingMode::Legion)
            check(free.legionMoves > held.legionMoves, "the walk is Legion's", std::to_string(free.legionMoves));
    }
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
