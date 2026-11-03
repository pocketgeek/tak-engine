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
//                   or a hit on it. Written against the World as it stands
//                   before the stride scan (tickCombat's acquireTarget) is
//                   removed, so that removal cannot leave a class of idle
//                   unit deaf. It also prints the audit: each type's FBI
//                   default mission, the World host that runs it, and how the
//                   unit engaged.
//
// The other scenarios of the plan (unseen_in_range_not_picked, ...) join as
// the mission hosts move to the ported scan.
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
    if (t.wanders) return "stroll (no mission)";
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
    // reason. Empty while the stride scan stands; a type the mission hosts
    // miss once it goes must be fixed there, not listed here.
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
    else { std::fprintf(stderr, "unknown part %s\n", part.c_str()); return 2; }
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
