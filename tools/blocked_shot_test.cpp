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
// Zhon campfire art (ZonFireD02: indestructible, nonblocking, 'height'
// set), in Retail and Legion pathfinding. Measured: shots landing short,
// damage to the target, and where the archer ends up; plus the limits
// (Hold Position stays; an open field is untouched); the same archer
// against a Taros Castle behind a crest in front of its long face. A hovering Thirsha
// whose Lightning stops on a standing stone moves round its hover ring
// (World::hoverForShot).
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
#include <utility>
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
    int repositions = 0;     // most walks used at once against the target
    uint64_t legionMoves = 0; // Legion navigator move updates (Legion mode)
};

// A 64x64 map at height 30. Ridge: columns 17..23 climb 12 per cell to a
// 90-high crest and fall again (a 3x3 spread of 24: passable, and clear on
// the nav grid). Campfires: column 19, rows 26..38, ZonFireD02 (height 50;
// an arrow aimed at a Swordsman's drawn SweetSpot clears the 25-high
// ZonFireD01 since protocol 295).
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
        map.featureNames = {"ZonFireD02"};
        for (int z = 20; z <= 44; ++z) map.features[size_t(z) * 64 + 19] = 0;
    }
    sim::World w;
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
        out.repositions = std::max(out.repositions, int(a->repositions));   // a hit renews the budget
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
    }
    out.legionMoves = w.legionStats().moves;
    return out;
}

// A failed search writes off the spot, not the target. The archer attacks a
// Swordsman behind a full-height campfire wall too far off to walk round
// (no spot: it fires on), then the Swordsman walks west to stand behind the
// short campfire row: a spot with a clear shot is in reach again, and the
// archer must walk to it. Returns {walks, hits} after the target moved
// (walks -1 if it walked before: the wall was meant to leave no spot).
std::pair<int, int> runSticky(const hpi::Vfs& vfs, const sim::TypeRegistry& reg) {
    tnt::Map map;
    map.width = map.height = 64;
    map.seaLevel = 0;
    map.heights.assign(64 * 64, 30);
    map.features.assign(64 * 64, 0xffff);
    map.featureNames = {"ZonFireD02"};
    for (int z = 0; z < 64; ++z) map.features[size_t(z) * 64 + 34] = 0;
    for (int z = 20; z <= 44; ++z) map.features[size_t(z) * 64 + 19] = 0;
    sim::World w;
    w.setPathfindingMode(mode);
    w.setTerrain(map.heights, map.width, map.height, map.seaLevel, &map.features);
    sim::registerMapFeatures(w, map, vfs, &reg);
    w.setPathService(true);
    const sim::UnitType* bow = reg.find("arabow");
    const sim::UnitType* sword = reg.find("arasword");
    if (!bow || !sword) return {0, 0};
    const float sz = 32 * 16;
    const int archer = w.spawn(bow, 14 * 16 + 8, sz, 0, 0);
    const int target = w.spawn(sword, 44 * 16 + 8, sz, 0, 1);
    w.setStance(target, 2);
    w.unit(target)->moveState = 0;
    w.unit(target)->fireState = 0;
    w.unit(target)->hp = sim::Fixed::fromInt(30000);
    w.unit(archer)->moveState = 1;
    for (int i = 0; i < 30; ++i) w.tick(1.f / 30);
    w.attack(archer, target, false);
    int walks = 0, hits = 0;
    bool wasMove = false;
    auto step = [&](int n) {
        for (int i = 0; i < n; ++i) {
            w.tick(1.f / 30);
            const sim::Unit* a = w.unit(archer);
            for (const auto& h : w.hits())
                if (h.fromPlayer == 0 && h.victimId == target) ++hits;
            const bool m = a && !a->orders.empty() && a->orders.front().targetId == 0;
            if (m && !wasMove) ++walks;
            wasMove = m;
        }
    };
    step(900);
    const int firstWalks = walks;
    walks = hits = 0;
    w.order(target, 26 * 16 + 8, sz, false);
    step(1800);
    return {firstWalks == 0 ? walks : -1, hits};
}

// The user's 0.9.1 game: a Creon Fire Wagon (crefire, a flame weapon; the
// FBI's standingunitorder=1 makes it Hold Position) on an attack-move
// auto-acquires an Aramon Lodestone 344 px off, with a Zhon standing stone
// (ZonHenge04: 1x2, indestructible, height 60) on the line of fire. The 2D
// sight walk passes beside the one-cell stone, so only the flames' impacts
// can show the shot is blocked. The geometry is the game's, moved by whole
// cells (224,480).
enum class Wagon { AttackMoveHold, AttackMoveManeuver, Hold };

struct WagonOutcome {
    int stoneFlames = 0;     // flame impacts that are not on the lodestone
    int lodestoneHits = 0;
    float damage = 0;
    int blockedShots = 0;    // most counted against the lodestone at once
    float moved = 0;
    bool attackMoveLeft = false;
};

WagonOutcome runWagon(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, Wagon variant, int ticks) {
    tnt::Map map;
    map.width = map.height = 64;
    map.seaLevel = 0;
    map.heights.assign(64 * 64, 62);
    map.features.assign(64 * 64, 0xffff);
    map.featureNames = {"ZonHenge04"};
    map.features[size_t(23) * 64 + 20] = 0;
    sim::World w;
    w.setPathfindingMode(mode);
    w.setTerrain(map.heights, map.width, map.height, map.seaLevel, &map.features);
    sim::registerMapFeatures(w, map, vfs, &reg);
    w.setPathService(true);
    const sim::UnitType* wagon = reg.find("crefire");
    const sim::UnitType* lode = reg.find("aralode");
    if (!wagon || !lode) { check(false, "shipped crefire and aralode load"); return {}; }
    const float sx = 591.3f, sz = 456.0f;
    const int lodestone = w.spawn(lode, 256, 384, 0, 1);
    for (int i = 0; i < 30; ++i) w.tick(1.f / 30);
    const int shooter = w.spawn(wagon, sx, sz, {}, 0);
    w.player(0).mana = 30000;
    sim::Unit* u = w.unit(shooter);
    if (variant == Wagon::AttackMoveManeuver) u->moveState = 1;
    WagonOutcome out;
    if (u->moveState != (variant == Wagon::AttackMoveManeuver ? 1 : 0))
        check(false, "crefire ships Hold Position (standingunitorder=1)");
    // It acquires the lodestone on its own (fire at will); the attack-move
    // the AI gave it is then behind the auto attack, as in the game.
    for (int i = 0; i < 90 && (u->orders.empty() || u->orders.front().targetId != lodestone); ++i) {
        w.tick(1.f / 30);
        u = w.unit(shooter);
    }
    if (u->orders.empty() || !u->orders.front().autoTarget) check(false, "the wagon auto-acquires the lodestone");
    if (variant != Wagon::Hold) w.attackMove(shooter, sx - 500, sz + 480, true);
    const float hp0 = w.unit(lodestone)->hp.toFloat();
    for (int i = 0; i < ticks; ++i) {
        w.tick(1.f / 30);
        const sim::Unit* a = w.unit(shooter);
        const sim::Unit* t = w.unit(lodestone);
        if (!a || !a->alive()) break;
        if (a->blockedTarget == lodestone) out.blockedShots = std::max(out.blockedShots, int(a->blockedShots));
        for (const auto& h : w.hits()) {
            if (h.fromPlayer != 0) continue;
            if (h.victimId == lodestone) ++out.lodestoneHits;
            else ++out.stoneFlames;
        }
        if (!t || !t->alive()) break;
    }
    const sim::Unit* a = w.unit(shooter);
    const sim::Unit* t = w.unit(lodestone);
    out.damage = hp0 - (t && t->alive() ? t->hp.toFloat() : 0.f);
    if (a) {
        const float dx = a->x.toFloat() - sx, dz = a->z.toFloat() - sz;
        out.moved = std::sqrt(dx * dx + dz * dz);
        out.attackMoveLeft = !a->orders.empty() && a->orders.back().attackMove;
    }
    return out;
}

// The user's 0.9.10 game (~4:28): a Thirsha (zonhunt) hover-attacks a Zhon
// Hunter that stands just past a Zhon standing stone (ZonHenge11: 5x4,
// indestructible, height 94). From its hover point 150 px north of the
// Hunter at attack altitude every Lightning bolt stops on the stone's top
// (52a4d0), and retail's hover mission (41de5d) keeps that bearing at every
// wake: in retail it fires into the stone for as long as the order lasts.
// The engine moves it round the ring to a clear bearing. The geometry is the
// game's, moved by whole cells (-2096,-768).
struct HoverOutcome {
    int stoneBolts = 0;      // impacts that are not on the Hunter
    int hunterHits = 0;
    float damage = 0;
    int repositions = 0;
    float moved = 0;
};

HoverOutcome runHover(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, int ticks) {
    tnt::Map map;
    map.width = map.height = 64;
    map.seaLevel = 0;
    map.heights.assign(64 * 64, 62);
    map.features.assign(64 * 64, 0xffff);
    map.featureNames = {"ZonHenge11"};
    map.features[size_t(20) * 64 + 20] = 0;   // anchor: cells 20..24 x 20..23
    sim::World w;
    w.setPathfindingMode(mode);
    w.setTerrain(map.heights, map.width, map.height, map.seaLevel, &map.features);
    sim::registerMapFeatures(w, map, vfs, &reg);
    w.setPathService(true);
    const sim::UnitType* thirsha = reg.find("zonhunt");
    const sim::UnitType* hunter = reg.find("zonter");
    if (!thirsha || !hunter) { check(false, "shipped zonhunt and zonter load"); return {}; }
    const float sx = 391, sz = 288;
    const int target = w.spawn(hunter, 384, 432, 0, 1);
    w.setStance(target, 2);
    w.unit(target)->moveState = 0;
    w.unit(target)->fireState = 0;
    w.unit(target)->hp = sim::Fixed::fromInt(30000);
    const int shooter = w.spawn(thirsha, sx, sz, 0, 0);
    for (int i = 0; i < 30; ++i) w.tick(1.f / 30);
    const float hp0 = w.unit(target)->hp.toFloat();
    w.attack(shooter, target, false);
    HoverOutcome out;
    for (int i = 0; i < ticks; ++i) {
        w.tick(1.f / 30);
        const sim::Unit* a = w.unit(shooter);
        if (!a || !a->alive()) break;
        out.repositions = std::max(out.repositions, int(a->repositions));
        for (const auto& h : w.hits()) {
            if (h.fromPlayer != 0) continue;
            if (h.victimId == target) ++out.hunterHits;
            else ++out.stoneBolts;
        }
    }
    const sim::Unit* a = w.unit(shooter);
    const sim::Unit* t = w.unit(target);
    out.damage = hp0 - (t && t->alive() ? t->hp.toFloat() : 0.f);
    if (a) out.moved = std::hypot(a->x.toFloat() - sx, a->z.toFloat() - sz);
    return out;
}

// A long building behind a crest. Retail's collision reaches a unit through
// the cells it owns (tools/re/probe_long_footprint_blocked.py), so a shot
// that lands on a ridge in front of a building's long face is short of it,
// however long the building is the other way. The Taros Castle (tarcastl,
// 8x20 cells) stands with its long face to the archer, and a short passable
// ridge (columns 32..38, rows 30..33, a 90-high crest at x 568) lies 16..128
// px in front of that face, the crest 136 px from the castle's centre: inside
// a circle of its 160 px long half-extent, outside its footprint. Returns
// the same Outcome as run().
Outcome runCastle(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, int moveState, int ticks) {
    tnt::Map map;
    map.width = map.height = 64;
    map.seaLevel = 0;
    map.heights.assign(64 * 64, 30);
    map.features.assign(64 * 64, 0xffff);
    const int profile[] = {45, 60, 75, 90, 75, 60, 45};
    for (int z = 30; z <= 33; ++z)
        for (int i = 0; i < 7; ++i) map.heights[size_t(z) * 64 + size_t(32 + i)] = uint8_t(profile[i]);
    sim::World w;
    w.setPathfindingMode(mode);
    w.setTerrain(map.heights, map.width, map.height, map.seaLevel, &map.features);
    sim::registerMapFeatures(w, map, vfs, &reg);
    w.setPathService(true);
    const sim::UnitType* bow = reg.find("arabow");
    const sim::UnitType* castle = reg.find("tarcastl");
    if (!bow || !castle) { check(false, "shipped arabow and tarcastl load"); return {}; }
    if (castle->footX != 8 || castle->footZ != 20) check(false, "tarcastl is 8x20");
    // Footprint columns 40..47, rows 22..41: centre (704,512), long face x=640.
    const float sx = 23 * 16 + 8, sz = 32 * 16, tx = 704, tz = 512;
    const int archer = w.spawn(bow, sx, sz, 0, 0);
    const int target = w.spawn(castle, tx, tz, 0, 1);
    w.setStance(target, 2);
    w.unit(target)->fireState = 0;
    w.unit(target)->hp = sim::Fixed::fromInt(30000);
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
        out.repositions = std::max(out.repositions, int(a->repositions));
        for (const auto& h : w.hits()) {
            if (h.fromPlayer != 0) continue;
            if (h.victimId == target) { ++out.targetHits; continue; }
            if (h.x < 640) ++out.shortShots;   // short of the long face
        }
    }
    const sim::Unit* a = w.unit(archer);
    const sim::Unit* t = w.unit(target);
    out.damage = hp0 - (t && t->alive() ? t->hp.toFloat() : 0.f);
    if (a) out.moved = std::hypot(a->x.toFloat() - sx, a->z.toFloat() - sz);
    out.legionMoves = w.legionStats().moves;
    return out;
}

std::string describe(const HoverOutcome& o) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "bolts off target=%d hits=%d damage=%.0f repositions=%d moved=%.0fpx",
                  o.stoneBolts, o.hunterHits, o.damage, o.repositions, o.moved);
    return buf;
}

std::string describe(const WagonOutcome& o) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "flames off target=%d hits=%d damage=%.0f blocked=%d moved=%.0fpx",
                  o.stoneFlames, o.lodestoneHits, o.damage, o.blockedShots, o.moved);
    return buf;
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
    // A long building: the crest in front of its long face blocks the shot.
    {
        const Outcome held = runCastle(vfs, reg, 0, kTicks);
        const Outcome free = runCastle(vfs, reg, 1, kTicks);
        std::printf("  castle     hold position: %s\n", describe(held).c_str());
        std::printf("  castle     maneuver:      %s\n", describe(free).c_str());
        check(held.shortShots >= 3 && held.damage == 0 && held.moved < 8,
              "castle: a crest in front of its long face blocks the shot, and Hold Position stays and fires on");
        check(free.repositions >= 1 && free.damage > 0 && free.shortShots <= 4,
              "castle: the archer counts those shots blocked, walks to a clear shot and hurts the castle");
        if (mode == sim::PathfindingMode::Legion)
            check(free.legionMoves > held.legionMoves, "the walk is Legion's", std::to_string(free.legionMoves));
    }
    // Fire Wagon flames against a lodestone behind a standing stone.
    const WagonOutcome hold = runWagon(vfs, reg, Wagon::Hold, kTicks);
    const WagonOutcome onMove = runWagon(vfs, reg, Wagon::AttackMoveHold, 1800);
    const WagonOutcome maneuver = runWagon(vfs, reg, Wagon::AttackMoveManeuver, 1800);
    std::printf("  fire wagon hold, idle:        %s\n", describe(hold).c_str());
    std::printf("  fire wagon hold, attack-move: %s\n", describe(onMove).c_str());
    std::printf("  fire wagon maneuver, a-move:  %s\n", describe(maneuver).c_str());
    check(hold.stoneFlames >= 3 && hold.blockedShots >= 3,
          "flame impacts on the stone count as blocked shots against the lodestone");
    check(hold.damage == 0 && hold.moved < 8, "Hold Position with nothing queued stays and fires on");
    check(onMove.damage > 0 && onMove.stoneFlames <= 6,
          "Hold Position on an attack-move leaves the blocked spot and burns the lodestone");
    check(maneuver.damage > 0 && maneuver.stoneFlames <= 6,
          "Maneuver on an attack-move walks to a clear shot and burns the lodestone");
    // A Thirsha hovering behind a standing stone moves round its ring.
    const HoverOutcome hover = runHover(vfs, reg, 1800);
    std::printf("  thirsha hover, stone:        %s\n", describe(hover).c_str());
    check(hover.stoneBolts >= 3 && hover.stoneBolts <= 6,
          "the Thirsha's first bolts stop on the standing stone, then it stops firing into it");
    check(hover.repositions >= 1 && hover.hunterHits > 0 && hover.damage > 0,
          "the Thirsha moves round its hover ring to a clear bearing and hits the Hunter");
    const auto [walks, hits] = runSticky(vfs, reg);
    check(walks >= 1 && hits > 0, "a failed search does not give up on the target once a clear spot is in reach",
          "walks=" + std::to_string(walks) + " hits=" + std::to_string(hits));
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
