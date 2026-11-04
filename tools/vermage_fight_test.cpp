// vermage_fight_test: Kirenna (VERMAGE) on an attack-move with Maneuver
// fires (kirenna_attack_move_fires). Until the retail target search
// (fx-tsearch step 6) she dealt no damage (0.9.3 notes, Known limits;
// docs/retail-engine.md, "Kirenna on an attack-move"):
//
//   vermage_fight_test <retail-install-dir> [--legion]
//
// The attack-move used World's own acquisition (findTarget): an enemy
// within the longest weapon's range + 90 px whether or not the player saw
// it, so she chased an Archer she could not see, stopped in reach unseen,
// the sweep never aimed for her and her AimWeapon waited on her walk
// controller's static 7 for good. Now a fight-move leg is retail's MoveFight
// order, a Move with response mode -1 (World::markFightLeg): its scan
// (4d8370) takes only an enemy her player sees, within her selected slot's
// range, so she stops on sight and the attack fires.
//
// Checks (flat map, shipped units, a hold-position Archer of player 1):
//   - on the attack-move she takes the Archer only once her player sees it;
//   - she fires and the Archer loses hit points;
//   - on a plain move past it she fires (the sweep aims her automatic slot
//     as she walks, as in retail).
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
constexpr float kTick = 1.f / 30;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) ++failures;
}

struct Run {
    int acquired = -1;      // first tick the front order was an auto attack on the Archer
    bool seenAtAcquire = false;
    int shots = 0;          // ticks Kirenna fired
    long dealt = 0;         // hit points the Archer lost
    int hits = 0;           // ticks she lost hit points
    float dist = 0;         // centre distance at the end
    bool standing = false;  // speed 0 at the end
    bool aiming = false;    // her selected slot's aim started (flags 0xe0) at the end
};

std::string describe(const Run& r) {
    char b[200];
    std::snprintf(b, sizeof b, "acquired %d (seen %d), shots %d, dealt %ld, hits taken %d, %.1f px off, standing %d, aiming %d",
                  r.acquired, int(r.seenAtAcquire), r.shots, r.dealt, r.hits, r.dist, int(r.standing), int(r.aiming));
    return b;
}

Run fight(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, bool attackMove) {
    tnt::Map map;
    map.width = map.height = 128;
    map.seaLevel = 0;
    map.heights.assign(size_t(128) * 128, 70);
    map.features.assign(size_t(128) * 128, 0xffff);
    sim::World w;
    w.setPathfindingMode(mode);
    w.setTerrain(map.heights, map.width, map.height, map.seaLevel, &map.features);
    sim::registerMapFeatures(w, map, vfs, &reg);
    w.setPathService(true);
    Run r;
    const sim::UnitType* kirenna = reg.find("vermage");
    const sim::UnitType* archer = reg.find("araarch");
    const sim::UnitType* peasant = reg.find("verman");
    if (!kirenna || !archer || !peasant) { check(false, "shipped vermage, araarch and verman load"); return r; }
    const float sx = 300, sz = 1000;
    const int foe = w.spawn(archer, sx + 1100, sz, -1.5707963f, 1);
    w.unit(foe)->moveState = 0;
    w.unit(foe)->hp = sim::Fixed::fromInt(30000);
    const int me = w.spawn(kirenna, sx, sz, 1.5707963f, 0);
    w.unit(me)->moveState = 1;   // Maneuver
    // A second unit of hers, far off and passive: the sweep (40f330) skips
    // a player's only unit.
    const int other = w.spawn(peasant, 150, 1900, 0.f, 0);
    w.unit(other)->fireState = 0;
    w.unit(other)->moveState = 0;
    for (int i = 0; i < 30; ++i) w.tick(kTick);
    if (attackMove) w.attackMove(me, sx + 1800, sz, false);
    else w.order(me, sx + 1800, sz, false);
    auto hp = w.unit(me)->hp;
    for (int i = 0; i < 1800; ++i) {
        w.tick(kTick);
        const sim::Unit* u = w.unit(me);
        const sim::Unit* t = w.unit(foe);
        if (!u || !t) break;
        if (r.acquired < 0 && !u->orders.empty() && u->orders.front().autoTarget &&
            u->orders.front().targetId == foe) {
            r.acquired = int(w.tickCount());
            r.seenAtAcquire = w.playerSees(0, *t);
        }
        r.shots += u->firedWeapons != 0;
        r.hits += u->hp < hp;
        hp = u->hp;
        r.dealt += 30000 - t->hp.floorInt();
        w.unit(foe)->hp = sim::Fixed::fromInt(30000);
        r.dist = std::hypot((t->x - u->x).toFloat(), (t->z - u->z).toFloat());
        r.standing = u->speed.v == 0;
        r.aiming = (u->weaponAim[size_t(u->weaponSlot)].flags & 0xe0) != 0;
    }
    return r;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: vermage_fight_test <retail-install-dir> [--legion]\n"); return 2; }
    for (int i = 2; i < argc; ++i)
        if (std::string(argv[i]) == "--legion") mode = sim::PathfindingMode::Legion;
    hpi::Vfs vfs = hpi::mountRetailRoot(argv[1]);
    sim::TypeRegistry reg;
    sim::setupRegistry(reg, vfs, false);
    std::printf("[%s pathfinding]\n", mode == sim::PathfindingMode::Legion ? "Legion" : "Retail");

    const Run am = fight(vfs, reg, true);
    std::printf("  attack-move: %s\n", describe(am).c_str());
    check(am.acquired >= 0 && am.seenAtAcquire, "the attack-move takes the Archer only once her player sees it",
          describe(am));
    check(am.shots > 0 && am.dealt > 0, "kirenna_attack_move_fires: she fires and the Archer loses hit points",
          describe(am));

    const Run mv = fight(vfs, reg, false);
    std::printf("  plain move: %s\n", describe(mv).c_str());
    check(mv.shots > 0, "on a plain move past it the sweep aims her automatic slot as she walks, and she fires",
          describe(mv));

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
