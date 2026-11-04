// target_search_test: guards for the port of retail's automatic target
// search (4d8370 / 412620) to every automatic pick (docs/retail-engine.md,
// "Mission target scan").
//
//   target_search_test <retail-install-dir> <part> [--legion]
//
// Parts:
//   idle_coverage   Every armed shipped mobile type, idle on Fire At Will next
//                   to an enemy its player sees inside its scanning slot's
//                   reach, engages it within 60 ticks: an attack order on it
//                   (a mission's pick), a slot target on it (the sweep 40f330)
//                   or a hit on it. Written before the stride scan
//                   (tickCombat's acquireTarget) was removed, so that removal
//                   could not leave a class of idle unit deaf; since then the
//                   missions and the sweep alone engage. It also prints the
//                   audit: each type's FBI
//                   default mission, the World host that runs it, and how the
//                   unit engaged.
//
//   mission_ports   The mission hosts on the ported scan: an idle ranged unit
//                   takes an enemy in range only when its player sees it
//                   (unseen_in_range_not_picked, seen_by_own_scout_picked);
//                   one past its range is not taken
//                   (ranged_seen_out_of_range_ignored); Hold Position gets no
//                   Standby attack, and its patrol's scan a forced leashed
//                   attack with a return move (hold_position_patrol, 403600);
//                   an armed wanderer's Standby_Wander scan (4078e0) leashes
//                   its attack to its post on Maneuver and not on Roam.
//
//   attack_move     A fight-move leg is retail's MoveFight order (D1, AM-1): a
//                   Move with response mode -1 (World::markFightLeg), whose
//                   scan (4d8370) runs on every stance. The walker stops for
//                   an enemy its player sees in range with an automatic
//                   chase leashed by maneuverleashlength from where it stood
//                   (fight_move_stops_for_seen), Hold Position included; an
//                   enemy it does not see is walked past
//                   (fight_move_unseen_walked_past); after the kill the leg
//                   goes on to its goal (fight_move_continues).
//
//   shared_sight    Retail's default shared sight (4f6770 / 4c66b0, D2): a
//                   human's plane holds a same-team human ally's units' sight,
//                   so an idle archer takes an enemy only its ally's scout
//                   sees (allied_human_spots); a computer ally, a computer
//                   player and an enemy-team player share nothing
//                   (computer_never_shares, other_team_no_share); an AI
//                   takeover stops the sharing (takeover_stops_sharing); an
//                   ally's dying scout's revealer stays in both planes for its
//                   60 ticks (death_revealer_shared); and the counts always
//                   equal the sum of the footprints over their planes.
//
// The other scenarios of the plan join as their steps land.
//
// A dev harness; not shipped in a game.

#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include "tnt/tnt.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace tak;

namespace {
int failures = 0;
sim::PathfindingMode mode = sim::PathfindingMode::Retail;
constexpr float kTick = 1.f / 30;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) ++failures;
}

struct Flat {
    tnt::Map map;
    Flat() {
        map.width = map.height = 64;
        map.seaLevel = 0;
        map.heights.assign(size_t(64) * 64, 70);
        map.features.assign(size_t(64) * 64, 0xffff);
    }
    // No features, so no feature registration (registerMapFeatures would
    // read every feature GAF per world).
    void into(sim::World& w) {
        w.setPathfindingMode(mode);
        w.setTerrain(map.heights, map.width, map.height, map.seaLevel, &map.features);
        w.setPathService(true);
    }
};

// The FBI's defaultmissiontype as World reads it, and the World host that
// runs an idle unit of the type (sim.cpp standbyEligible, tickGuardNoMove,
// the wander stroll in tick).
const char* fbiMission(const sim::UnitType& t) {
    if (t.wanders) return "Standby_Wander";
    if (t.vtolStandby) return "VTOL_Standby";
    return t.canFly ? "(other flyer)" : "Standby";
}
const char* worldHost(const sim::UnitType& t) {
    if (t.isStructure()) return "Guard_NoMove";
    if (t.wanders) return "Standby_Wander";
    if (t.canFly && !t.vtolStandby) return "none";
    return t.canFly ? "VTOL_Standby" : "Standby";
}

// 519ac0: a switcher's selected slot, else the first.
int scanSlot(const sim::Unit& u) {
    if (u.type->weaponSwitching && u.weaponSlot >= 0 && u.weaponSlot < 3) return u.weaponSlot;
    return 0;
}

struct Engagement {
    int tick = -1;
    const char* how = "none";
    bool hostActive = false;   // the World's default-mission host was running at that tick
    bool seen = false;         // the player sees the foe at tick 16
    bool scanPicks = false;    // the ported mission scan (retailMissionScan) takes it at tick 16
    int fire = 0, move = 0;    // the type's standing fire and move orders
};

// One shipped type, idle, with a seen enemy inside its scanning slot's reach.
Engagement idleEngage(const sim::UnitType& type, const sim::UnitType& foeType, const sim::UnitType& spare, int ticks) {
    Flat flat;
    sim::World w;
    flat.into(w);
    const float sx = 250, sz = 400;
    const int me = w.spawn(&type, sx, sz, 1.5707963f, 0);
    sim::Unit& u = *w.unit(me);
    Engagement e;
    e.fire = u.fireState;
    e.move = u.moveState;
    u.fireState = 2;   // Fire At Will whatever the type's standing order
    const sim::Weapon& wp = type.weapons[size_t(scanSlot(u))];
    // Inside the scanning slot's range (a melee slot's too, so a unit on
    // Hold Position has it in reach) and the unit's own sight, clear of the
    // bodies and outside a minimum range; no farther than 300 px on this
    // small map.
    const float d = std::max({48.f, std::min(300.f, 0.7f * float(std::min<int32_t>(wp.range, type.sight))),
                              float(wp.minRange) + 16.f});
    const int foe = w.spawn(&foeType, sx + d, sz, -1.5707963f, 1);
    // A second unit of the player beside the foe, passive: the player sees
    // the foe whatever the tested type's own sight, and the sweep (40f330)
    // skips a player's only unit.
    const int other = w.spawn(&spare, sx + d, sz + 80, 0.f, 0);
    w.unit(other)->fireState = 0;
    w.unit(other)->moveState = 0;
    sim::Unit& f = *w.unit(foe);
    f.fireState = 0;
    f.moveState = 0;
    f.hp = sim::Fixed::fromInt(30000);
    for (int i = 0; i < ticks && e.tick < 0; ++i) {
        w.tick(kTick);
        const sim::Unit* a = w.unit(me);
        const sim::Unit* t = w.unit(foe);
        if (!a || !t) break;
        const bool host = a->standbyActive || a->guardNoMoveActive;
        const char* how = !a->orders.empty() && a->orders.front().targetId == foe ? "attack order"
            : std::find(a->slotTarget.begin(), a->slotTarget.end(), foe) != a->slotTarget.end() ? "sweep slot"
            : t->lastHitBy == me ? "hit" : nullptr;
        if (how) { e.tick = i; e.how = how; e.hostActive = host; }
    }
    // Two visible-list rebuilds (every 7 ticks) in, whatever tick it engaged
    // on: whether the player sees the foe and the ported scan takes it.
    while (w.tickCount() < 16) w.tick(kTick);
    if (sim::Unit* a = w.unit(me); a && w.unit(foe)) {
        e.seen = w.playerSees(0, *w.unit(foe));
        e.scanPicks = w.retailMissionScan(*a) == foe;
    }
    return e;
}

void idleCoverage(const sim::TypeRegistry& reg) {
    // The foe: land units with shootme, capturable and with no status
    // immunity; the first one the type's scanning weapon damages (a Human,
    // else an Undead, else a Siege body: Ayla's charm spares Humans).
    std::vector<const sim::UnitType*> foes;
    for (const char* id : {"araarch", "tararch", "aracan"})
        if (const auto* f = reg.find(id)) foes.push_back(f);
    const sim::UnitType* spare = reg.find("araarch");
    if (foes.size() != 3 || !spare) { check(false, "shipped araarch, tararch and aracan load"); return; }
    // Types the World leaves without an engagement today, each with its
    // reason. Empty: a type the mission hosts miss must be fixed there, not
    // listed here.
    const std::map<std::string, std::string> knownGaps = {};
    std::map<std::string, int> byHost;
    int armed = 0, engaged = 0, skipped = 0;
    std::printf("  %-10s %-15s %-20s %-9s %-13s %5s %-6s %-5s %s\n", "type", "FBI mission", "World host", "fire/move",
                "engaged by", "tick", "host", "seen", "ported scan picks");
    for (const auto& [id, t] : reg.types()) {
        if (t.isStructure() || t.weapons.empty()) continue;
        // A fresh unit scans with slot 0 (a switcher starts on it). A to-air
        // scanning slot takes flyers only; the ground foe is no case
        // for it.
        if (t.weapons[0].toAir) { ++skipped; std::printf("  %-10s skipped: its scanning slot is to-air only\n", id.c_str()); continue; }
        const sim::UnitType* foe = nullptr;
        for (const auto* f : foes)
            if (!foe && t.weapons[0].damageVs(f) > 0) foe = f;
        if (!foe) { ++skipped; std::printf("  %-10s skipped: its scanning slot damages no foe\n", id.c_str()); continue; }
        ++armed;
        const Engagement e = idleEngage(t, *foe, *spare, 60);
        const bool ok = e.tick >= 0;
        engaged += ok;
        ++byHost[worldHost(t)];
        std::printf("  %-10s %-15s %-20s %d/%-7d %-13s %5d %-6s %-5s %s\n", id.c_str(), fbiMission(t), worldHost(t),
                    e.fire, e.move, e.how, e.tick, ok ? (e.hostActive ? "yes" : "no") : "-", e.seen ? "yes" : "no",
                    e.scanPicks ? "yes" : "no");
        const auto gap = knownGaps.find(id);
        if (gap != knownGaps.end())
            check(!ok, "known gap " + id + " (" + gap->second + ") still has no engagement");
        else
            check(ok, id + " engages a seen enemy in reach within 60 ticks");
    }
    std::printf("  %d armed mobile types, %d engaged, %d skipped\n", armed, engaged, skipped);
    for (const auto& [host, n] : byHost) std::printf("    host %-20s %d types\n", host.c_str(), n);
}
// An idle unit of `type` (player 0) at (250, 400) and an Archer of player 1
// `d` px east, Hold Fire and Hold Position, run `ticks`: the first order the
// unit put in on the Archer, if any. `spotter`: a passive unit of player 0
// beside the Archer, so the player sees it.
struct Idle {
    bool attacked = false, swept = false, leashed = false, automatic = false, returnMove = false;
    sim::RetailGroundPoint origin{};
};
Idle idleNear(const sim::UnitType& type, const sim::UnitType& foeType, float d, bool spotter, int moveState,
              bool patrol, int ticks) {
    Flat flat;
    sim::World w;
    flat.into(w);
    const float sx = 250, sz = 400;
    const int me = w.spawn(&type, sx, sz, 1.5707963f, 0);
    w.unit(me)->fireState = 2;
    w.unit(me)->moveState = uint8_t(moveState);
    const int foe = w.spawn(&foeType, sx + d, sz, -1.5707963f, 1);
    w.unit(foe)->fireState = 0;
    w.unit(foe)->moveState = 0;
    w.unit(foe)->hp = sim::Fixed::fromInt(30000);
    // A second unit of the player far off (the sweep skips a player's only
    // unit), or beside the foe as its spotter.
    static sim::UnitType quiet;
    quiet = foeType; quiet.id = "quiet"; quiet.weapons.clear(); quiet.weapon = {}; quiet.sight = 200;
    const int other = spotter ? w.spawn(&quiet, sx + d, sz + 64, 0.f, 0) : w.spawn(&quiet, 900, 900, 0.f, 0);
    w.unit(other)->moveState = 0;
    if (patrol) w.patrol(me, sx, sz + 300);
    Idle out;
    for (int i = 0; i < ticks && !out.attacked; ++i) {
        w.tick(kTick);
        const sim::Unit* a = w.unit(me);
        if (!a || !w.unit(foe)) break;
        out.swept = out.swept || std::find(a->slotTarget.begin(), a->slotTarget.end(), foe) != a->slotTarget.end();
        if (!a->orders.empty() && a->orders.front().targetId == foe) {
            const auto& o = a->orders.front();
            out.attacked = true;
            out.leashed = o.chaseLeashed;
            out.automatic = o.autoTarget;
            out.origin = o.chaseOrigin;
            out.returnMove = a->orders.size() > 1 && !a->orders[1].targetId && a->orders[1].groundMission &&
                             a->orders[1].groundResponse.mode == 1;
        }
    }
    return out;
}

void missionPorts(const sim::TypeRegistry& reg) {
    const sim::UnitType* archer = reg.find("araarch");
    const sim::UnitType* boar = reg.find("monboar");
    if (!archer || !boar || archer->weapons.empty()) { check(false, "shipped araarch and monboar load"); return; }
    // A short-sighted gunner: the Archer's bow (range R) with sight 60, so an
    // enemy at 0.8 R is in range and out of its own sight.
    sim::UnitType gunner = *archer;
    gunner.id = "gunner";
    gunner.sight = 60;
    const float range = float(gunner.weapons[0].range);
    {
        const Idle unseen = idleNear(gunner, *archer, 0.8f * range, false, 1, false, 120);
        check(!unseen.attacked && !unseen.swept, "unseen_in_range_not_picked: Standby and the sweep leave it");
        const Idle seen = idleNear(gunner, *archer, 0.8f * range, true, 1, false, 120);
        check(seen.attacked && seen.automatic && seen.leashed && seen.returnMove,
              "seen_by_own_scout_picked: Maneuver's Standby attack, leashed, with a return move");
        const Idle roam = idleNear(gunner, *archer, 0.8f * range, true, 2, false, 120);
        check(roam.attacked && roam.automatic && !roam.leashed && !roam.returnMove,
              "Roam's Standby attack is plain (4d7de0: no leash)");
    }
    {
        // Sight 500 past the bow's range: a seen enemy beyond the range is
        // not taken by a ranged slot (52ad60).
        sim::UnitType sighted = *archer;
        sighted.id = "sighted";
        sighted.sight = int32_t(range) + 200;
        const Idle past = idleNear(sighted, *archer, range + 100.f, false, 2, false, 120);
        check(!past.attacked, "ranged_seen_out_of_range_ignored: a seen enemy past the range is left");
    }
    {
        const Idle hold = idleNear(gunner, *archer, 0.8f * range, true, 0, false, 120);
        check(!hold.attacked && hold.swept, "hold_position: no Standby attack, the sweep takes it");
        const Idle patrol = idleNear(gunner, *archer, 0.8f * range, true, 0, true, 240);
        check(patrol.attacked && patrol.leashed && patrol.returnMove,
              "hold_position_patrol: the patrol's scan puts a forced leashed attack and a return move in",
              "attacked " + std::to_string(patrol.attacked) + " leashed " + std::to_string(patrol.leashed) +
                  " return " + std::to_string(patrol.returnMove) + " auto " + std::to_string(patrol.automatic));
    }
    {
        // Standby_Wander (4078e0): Maneuver keeps a pick inside the leash of
        // the post and leashes the attack there; Roam's attack is plain.
        const float d = std::min(0.7f * float(boar->sight), 0.7f * float(std::max<int32_t>(boar->leash, 1)));
        const Idle maneuver = idleNear(*boar, *archer, std::max(d, 40.f), true, 1, false, 120);
        check(maneuver.attacked && maneuver.automatic && maneuver.leashed && !maneuver.returnMove &&
              maneuver.origin.x == 250 && maneuver.origin.z == 400,
              "Standby_Wander on Maneuver: an automatic attack leashed to the post, no return move");
        const Idle roam = idleNear(*boar, *archer, std::max(d, 40.f), true, 2, false, 120);
        check(roam.attacked && !roam.automatic && !roam.leashed, "Standby_Wander on Roam: a plain attack (mode 0)");
        const Idle hold = idleNear(*boar, *archer, std::max(d, 40.f), true, 0, false, 120);
        check(!hold.attacked, "Standby_Wander on Hold Position: no scan");
    }
}
struct Fight {
    int picked = -1;          // tick of the first attack order on the foe
    bool automatic = false, leashed = false, leashIsType = false;
    bool legBehind = false;   // the fight-move leg behind it, mode -1, origin = the attack's
    bool killed = false;
    float endFromGoal = 1e9f;
};

// A walker of `type` (player 0) fight-moves from (150, 300) to (900, 300)
// past a foe at (500, 300 + off) of player 1 that never fights back; with
// `spotter`, a passive unit of player 0 stands beside the foe.
Fight fightMove(const sim::UnitType& type, const sim::UnitType& foeType, float off, int stance, bool spotter,
                bool killable, int ticks) {
    Flat flat;
    sim::World w;
    flat.into(w);
    Fight r;
    const int me = w.spawn(&type, 150, 300, 1.5707963f, 0);
    w.unit(me)->moveState = stance;
    const int foe = w.spawn(&foeType, 500, 300 + off, -1.5707963f, 1);
    sim::Unit& f = *w.unit(foe);
    f.fireState = 0;
    f.moveState = 0;
    if (!killable) f.hp = sim::Fixed::fromInt(30000);
    const int other = w.spawn(&foeType, spotter ? 500 : 150, spotter ? 300 + off + 60 : 900, 0.f, 0);
    w.unit(other)->fireState = 0;
    w.unit(other)->moveState = 0;
    for (int i = 0; i < 30; ++i) w.tick(kTick);
    w.attackMove(me, 900, 300, false);
    for (int i = 0; i < ticks; ++i) {
        w.tick(kTick);
        const sim::Unit* a = w.unit(me);
        if (!a) break;
        const sim::Unit* t = w.unit(foe);
        if (!t || !t->alive()) r.killed = true;
        if (r.picked < 0 && !a->orders.empty() && a->orders.front().targetId == foe) {
            const sim::Order& o = a->orders.front();
            r.picked = i;
            r.automatic = o.autoTarget;
            r.leashed = o.chaseLeashed;
            r.leashIsType = o.chaseLeash == uint16_t(std::clamp(type.leash, 0, 65535));
            for (size_t k = 1; k < a->orders.size(); ++k) {
                const sim::Order& leg = a->orders[k];
                if (!leg.groundMission || !leg.goal) continue;
                r.legBehind = leg.attackMove && leg.groundResponse.mode == -1 &&
                              leg.groundResponse.origin.x == o.chaseOrigin.x &&
                              leg.groundResponse.origin.z == o.chaseOrigin.z;
                break;
            }
        }
        r.endFromGoal = std::hypot(a->x.toFloat() - 900, a->z.toFloat() - 300);
    }
    return r;
}

void attackMoves(const sim::TypeRegistry& reg) {
    const sim::UnitType* archer = reg.find("araarch");
    if (!archer || archer->weapons.empty()) { check(false, "shipped araarch loads"); return; }
    const float range = float(archer->weapons[0].range);
    auto describe = [](const Fight& f) {
        return "picked " + std::to_string(f.picked) + " auto " + std::to_string(f.automatic) + " leashed " +
               std::to_string(f.leashed) + " leash " + std::to_string(f.leashIsType) + " leg " +
               std::to_string(f.legBehind) + " killed " + std::to_string(f.killed) + " from goal " +
               std::to_string(int(f.endFromGoal));
    };
    // Sight 60: it never sees the foe, which stays inside the bow's range
    // as it walks past 0.6 R off the path.
    sim::UnitType blind = *archer;
    blind.id = "blind";
    blind.sight = 60;
    for (int stance : {1, 2, 0}) {
        const Fight seen = fightMove(blind, *archer, 0.6f * range, stance, true, false, 600);
        check(seen.picked >= 0 && seen.automatic && seen.leashed && seen.leashIsType && seen.legBehind,
              "fight_move_stops_for_seen (stance " + std::to_string(stance) +
                  "): an automatic chase leashed by maneuverleashlength, the mode -1 leg behind it with its origin",
              describe(seen));
    }
    const Fight unseen = fightMove(blind, *archer, 0.6f * range, 1, false, false, 900);
    check(unseen.picked < 0 && unseen.endFromGoal < 64.f,
          "fight_move_unseen_walked_past: no attack on an enemy its player cannot see; it reaches the goal",
          describe(unseen));
    const Fight kill = fightMove(*archer, *archer, 0.6f * range, 1, true, true, 3600);
    check(kill.picked >= 0 && kill.killed && kill.endFromGoal < 64.f,
          "fight_move_continues: after the kill the leg goes on to its goal", describe(kill));
}
// The view counters equal a from-scratch sum of every active footprint over
// the planes it is counted in. A death revealer's cells are not in it, so it
// is checked only while none is pending.
bool countsMatchFootprints(const sim::World& w) {
    const int width = w.viewPlaneW(), height = w.viewPlaneH();
    const size_t cells = size_t(width) * height;
    static const auto heights = sim::retailExplorationHeights(64, 64, 0, [](int, int) { return 70; });
    if (heights.size() != cells) return false;
    std::vector<uint16_t> expect(w.viewCounts().size(), 0);
    for (const auto& u : w.units()) {
        if (!u.viewSight.active) continue;
        if (!u.viewPlanes) return false;
        sim::RetailSightFootprint sight = u.viewSight;
        sight.active = false;
        sim::retailSightFootprint(sight, true, false, width, height, uint8_t(u.viewPlayer),
                                  [&](int x, int z) { return heights[size_t(z) * width + x]; },
                                  [&](int x, int z, int d, uint16_t) {
                                      for (int p = 0; p < 16; ++p)
                                          if (u.viewPlanes >> p & 1) expect[size_t(p) * cells + size_t(z) * width + x] += uint16_t(d);
                                  });
    }
    return expect == w.viewCounts();
}

// Players 0 and 1 on `team1` (0 = allied with 0), player 2 the enemy. Player
// 0's archer (sight 40) at (250, 400) on Fire At Will, with a passive second
// unit far off; player 1's scout (sight 200) beside player 2's Archer, which
// stands in the bow's range. ai0/ai1: that player is a computer.
struct Share {
    bool sees0 = false, sees1 = false, engaged = false, consistent = true;
    bool takeoverSees = true, revealerHeld = false, revealerGone = false;
    bool explored0 = false, lockedOwnSees = false;
};
Share sharedSight(const sim::UnitType& archer, int team1, bool ai0, bool ai1, bool takeover, bool revealer) {
    Flat flat;
    sim::World w;
    flat.into(w);
    w.setPlayerCount(3);
    w.setTeam(1, team1);
    w.player(0).automaticGates = ai0;
    w.player(1).automaticGates = ai1;
    static sim::UnitType blind, scout;
    blind = archer; blind.id = "blind"; blind.sight = 40;
    scout = archer; scout.id = "scout"; scout.weapons.clear(); scout.weapon = {}; scout.sight = 200;
    const float sx = 250, sz = 400, d = 0.7f * float(archer.weapons[0].range);
    const int me = w.spawn(&blind, sx, sz, 1.5707963f, 0);
    w.unit(me)->fireState = 2;
    w.unit(me)->moveState = 0;
    const int other = w.spawn(&scout, 900, 900, 0.f, 0);
    w.unit(other)->moveState = 0;
    const int spot = w.spawn(&scout, sx + d, sz + 64, 0.f, 1);
    w.unit(spot)->moveState = 0;
    const int foe = w.spawn(&archer, sx + d, sz, -1.5707963f, 2);
    w.unit(foe)->fireState = 0;
    w.unit(foe)->moveState = 0;
    w.unit(foe)->hp = sim::Fixed::fromInt(30000);
    Share out;
    for (int i = 0; i < 60; ++i) {
        w.tick(kTick);
        out.consistent = out.consistent && countsMatchFootprints(w);
        const sim::Unit* a = w.unit(me);
        if (!a || !w.unit(foe)) break;
        out.engaged = out.engaged ||
                      std::find(a->slotTarget.begin(), a->slotTarget.end(), foe) != a->slotTarget.end() ||
                      (!a->orders.empty() && a->orders.front().targetId == foe);
    }
    if (const sim::Unit* f = w.unit(foe)) {
        out.sees0 = w.playerSees(0, *f);
        out.sees1 = w.playerSees(1, *f);
        out.explored0 = w.exploredAt(0, f->x.toFloat(), f->z.toFloat());
    }
    if (takeover) {
        // The scout is frozen through the takeover: its footprint stays where
        // it stands and moves to its owner's own plane alone.
        w.unit(spot)->frozenFor = 200;
        w.player(1).automaticGates = true;
        w.tick(kTick);
        out.consistent = out.consistent && countsMatchFootprints(w);
        out.takeoverSees = w.unit(foe) && w.playerSees(0, *w.unit(foe));
        out.lockedOwnSees = w.unit(foe) && w.playerSees(1, *w.unit(foe));
    }
    if (revealer) {
        w.unit(me)->fireState = 0;
        w.destroy(spot);
        int deadAt = -1;
        for (int i = 0; i < 300; ++i) {
            w.tick(kTick);
            const sim::Unit* s = w.unit(spot);
            if (deadAt < 0 && (!s || !s->alive())) deadAt = i;
            const sim::Unit* f = w.unit(foe);
            if (!f || deadAt < 0) continue;
            if (i == deadAt + 30) out.revealerHeld = w.playerSees(0, *f) && w.playerSees(1, *f);
            if (i == deadAt + 70) {
                out.revealerGone = !w.playerSees(0, *f) && !w.playerSees(1, *f);
                out.consistent = out.consistent && countsMatchFootprints(w);
                break;
            }
        }
    }
    return out;
}

void sharedSights(const sim::TypeRegistry& reg) {
    const sim::UnitType* archer = reg.find("araarch");
    if (!archer || archer->weapons.empty()) { check(false, "shipped araarch loads"); return; }
    auto describe = [](const Share& s) {
        return "sees0 " + std::to_string(s.sees0) + " sees1 " + std::to_string(s.sees1) + " engaged " +
               std::to_string(s.engaged) + " consistent " + std::to_string(s.consistent);
    };
    const Share human = sharedSight(*archer, 0, false, false, true, false);
    check(human.sees0 && human.sees1 && human.engaged && human.consistent,
          "allied_human_spots: a same-team human ally's scout puts the enemy in player 0's plane; the archer takes it",
          describe(human));
    check(!human.takeoverSees, "takeover_stops_sharing: once the ally is a computer its sight leaves player 0's plane");
    check(human.lockedOwnSees && human.consistent,
          "takeover_frozen_scout: a frozen scout's footprint stays in its owner's plane through the takeover");
    check(human.explored0, "explore_shared: the human ally's scout explores for player 0 too");
    const Share aiAlly = sharedSight(*archer, 0, false, true, false, false);
    check(!aiAlly.sees0 && aiAlly.sees1 && !aiAlly.engaged && aiAlly.consistent,
          "computer_never_shares: a computer ally's sight stays its own", describe(aiAlly));
    check(!aiAlly.explored0, "computer_never_explores_for_ally: a computer ally's scout explores for itself alone");
    const Share aiMe = sharedSight(*archer, 0, true, false, false, false);
    check(!aiMe.sees0 && aiMe.sees1 && !aiMe.engaged && aiMe.consistent,
          "computer_never_shares: a computer player gets no human ally's sight", describe(aiMe));
    check(!aiMe.explored0, "computer_never_explores_for_ally: a computer player gets no human ally's explored cells");
    const Share enemy = sharedSight(*archer, 1, false, false, false, false);
    check(!enemy.sees0 && enemy.sees1 && !enemy.engaged && enemy.consistent,
          "other_team_no_share: a player on another team shares nothing", describe(enemy));
    const Share dies = sharedSight(*archer, 0, false, false, false, true);
    check(dies.revealerHeld && dies.revealerGone && dies.consistent,
          "death_revealer_shared: the dying scout's revealer holds in both planes, then goes after 60 ticks",
          "held " + std::to_string(dies.revealerHeld) + " gone " + std::to_string(dies.revealerGone) +
              " consistent " + std::to_string(dies.consistent));
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: target_search_test <retail-install-dir> <part> [--legion]\n");
        return 2;
    }
    const std::string part = argv[2];
    for (int i = 3; i < argc; ++i)
        if (std::string(argv[i]) == "--legion") mode = sim::PathfindingMode::Legion;
    hpi::Vfs vfs = hpi::mountRetailRoot(argv[1]);
    sim::TypeRegistry reg;
    sim::setupRegistry(reg, vfs, false);
    std::printf("[%s pathfinding] %s\n", mode == sim::PathfindingMode::Legion ? "Legion" : "Retail", part.c_str());
    if (part == "idle_coverage") idleCoverage(reg);
    else if (part == "mission_ports") missionPorts(reg);
    else if (part == "attack_move") attackMoves(reg);
    else if (part == "shared_sight") sharedSights(reg);
    else { std::fprintf(stderr, "unknown part %s\n", part.c_str()); return 2; }
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
