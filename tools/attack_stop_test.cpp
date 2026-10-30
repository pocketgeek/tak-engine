// attack_stop_test: an attack order aims only once the walk to the target has
// stopped.
//
//   attack_stop_test <retail-install-dir> [--legion]
//
// Retail's ground attack mission (Attack_Chase, 403bb0) gives the selected
// weapon slot its target in its stage 1, which runs at the order's first
// dispatch when it starts in reach and two ticks after the arrival stopped the
// body when it had to walk. The weapon update starts AimWeapon the tick after
// (it runs before the missions), the unit's COB pass runs it, and only then
// does the mover pivot the body to the facing request it posted. In World,
// whose COB pass comes before the weapons, that is: aim on the stage 1 tick,
// face from the tick after (tickCombat).
//
// Kirenna (VERMAGE) is why this matters: her AimWeapon waits for static 7,
// which her Create-started walk controller sets only when it finds her
// stopped (restore_x) and not yet aiming, or moving (walk_legs) while aiming.
// World aimed on the tick she stopped,
// the controller then saw her aiming and still (restore_legs), and static 7
// stayed 0: ordered onto a target 664 px off she parked 399 px from it and
// never fired. Retail can stall the same way when its walk controller looks
// late (docs/retail-engine.md, "Attack_Chase: the stop comes before the aim").
//
// Checks (flat map, shipped units, a passive Swordsman target):
//   - Kirenna ordered from 664 px walks in, aims two ticks after she stops,
//     turns no earlier, and fires (before: 0 shots in a minute);
//   - an Aramon Archer ordered from 600 px aims two ticks after it stops too;
//   - Kirenna ordered in reach while facing away aims at once and pivots
//     from the next tick, and fires.
//
// A dev harness; not shipped in a game.

#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include "tnt/tnt.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace tak;

namespace {
int failures = 0;
sim::PathfindingMode mode = sim::PathfindingMode::Retail;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) ++failures;
}

constexpr float kTick = 1.f / 30;

struct Scene {
    tnt::Map map;
    sim::World w;
};

void build(Scene& s, const hpi::Vfs& vfs, const sim::TypeRegistry& reg) {
    s.map.width = s.map.height = 96;
    s.map.seaLevel = 0;
    s.map.heights.assign(size_t(96) * 96, 70);
    s.map.features.assign(size_t(96) * 96, 0xffff);
    s.w.setPathfindingMode(mode);
    s.w.setTerrain(s.map.heights, s.map.width, s.map.height, s.map.seaLevel, &s.map.features);
    sim::registerMapFeatures(s.w, s.map, vfs, &reg);
    s.w.setPathService(true);
}

struct Run {
    int order = -1;         // tick the attack was given
    int stop = -1;          // first tick it stood still after walking
    int aim = -1;           // first tick its weapon started aiming
    int turned = -1;        // first tick its heading changed after the order
    int fired = -1;         // first tick it fired
    float dist = 0;         // centre distance at the end
};

std::string describe(const Run& r) {
    char b[200];
    std::snprintf(b, sizeof b, "order %d, stop %d, aim %d, turned %d, fired %d, %.1f px off", r.order, r.stop, r.aim,
                  r.turned, r.fired, r.dist);
    return b;
}

// A shooter of player 0 `dist` px from a passive Swordsman of player 1 along
// +x, heading `heading` (radians; pi/2 faces the target), is ordered to attack
// it and watched for `ticks`. `fireState` 2 is fire at will; 0 (hold fire)
// keeps it from turning to a target in reach before the order.
Run attack(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const char* shooterId, float dist, float heading,
           int ticks, int fireState = 2) {
    Scene s;
    build(s, vfs, reg);
    Run r;
    const sim::UnitType* st = reg.find(shooterId);
    const sim::UnitType* tt = reg.find("arasword");
    if (!st || !tt) { check(false, std::string("shipped ") + shooterId + " and arasword load"); return r; }
    const float sx = 300, sz = 768;
    const int target = s.w.spawn(tt, sx + dist, sz, 0.f, 1);
    s.w.setStance(target, 2);
    s.w.unit(target)->moveState = 0;
    s.w.unit(target)->fireState = 0;
    s.w.unit(target)->hp = sim::Fixed::fromInt(30000);
    const int shooter = s.w.spawn(st, sx, sz, heading, 0);
    s.w.unit(shooter)->fireState = fireState;
    s.w.unit(shooter)->mana = st->maxMana;
    for (int i = 0; i < 30; ++i) s.w.tick(kTick);   // Create's controllers settle
    r.order = int(s.w.tickCount());
    s.w.attack(shooter, target, false);
    const auto start = s.w.unit(shooter)->heading;
    bool walked = false;
    for (int i = 0; i < ticks; ++i) {
        const int now = int(s.w.tickCount());
        s.w.tick(kTick);
        const sim::Unit* u = s.w.unit(shooter);
        const sim::Unit* t = s.w.unit(target);
        if (!u || !t) break;
        if (u->speed.v) walked = true;
        else if (walked && r.stop < 0) r.stop = now;
        const int slot = u->type->weaponSwitching ? u->weaponSlot : 0;
        if (r.aim < 0 && (u->weaponAim[size_t(slot)].flags & 0xe0)) r.aim = now;
        if (r.turned < 0 && u->heading != start) r.turned = now;
        if (r.fired < 0 && u->firedWeapons) r.fired = now;
        r.dist = std::hypot((t->x - u->x).toFloat(), (t->z - u->z).toFloat());
    }
    return r;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: attack_stop_test <retail-install-dir> [--legion]\n"); return 2; }
    for (int i = 2; i < argc; ++i)
        if (std::string(argv[i]) == "--legion") mode = sim::PathfindingMode::Legion;
    hpi::Vfs vfs = hpi::mountRetailRoot(argv[1]);
    sim::TypeRegistry reg;
    sim::setupRegistry(reg, vfs, false);
    std::printf("[%s pathfinding]\n", mode == sim::PathfindingMode::Legion ? "Legion" : "Retail");
    constexpr float kFacing = 1.5707963f;

    // Kirenna (Water Ball, range 400) walks 264 px in, stops, aims two ticks
    // later and fires: her walk controller sees her stop before she aims.
    {
        const Run r = attack(vfs, reg, "vermage", 664, kFacing, 600);
        std::printf("  vermage from 664 px: %s\n", describe(r).c_str());
        check(r.stop > r.order && r.aim == r.stop + 2, "Kirenna aims two ticks after her walk stops", describe(r));
        check(r.turned < 0 || r.turned > r.aim, "she turns to her facing request no earlier than the tick after",
              describe(r));
        check(r.fired > r.aim && r.fired <= r.order + 300 && r.dist <= 400.5f,
              "she fires within 10 s from where she stopped (before: never, parked 399 px off)", describe(r));
    }

    // The stop comes before the aim for every walker, not only Kirenna.
    {
        const Run r = attack(vfs, reg, "araarch", 600, kFacing, 600);
        std::printf("  araarch from 600 px: %s\n", describe(r).c_str());
        check(r.stop > r.order && r.aim == r.stop + 2 && r.fired > r.aim,
              "an Archer also aims two ticks after its walk stops, and fires", describe(r));
    }

    // In reach from the start, facing away: stage 1 runs at once, so she aims
    // on the order's tick and pivots from the next.
    {
        const Run r = attack(vfs, reg, "vermage", 300, -kFacing, 300, 0);
        std::printf("  vermage in reach, facing away: %s\n", describe(r).c_str());
        check(r.aim == r.order && r.turned == r.order + 1, "ordered in reach she aims at once and turns the tick after",
              describe(r));
        check(r.fired > r.aim, "and she fires", describe(r));
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
