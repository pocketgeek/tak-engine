// shot_range_test: a shot that dies at its range is never aimed at a building
// past that range, and an attacker using one closes to the range itself.
//
//   shot_range_test <retail-install-dir> [--legion]
//
// A Line-of-Sight bolt or lightning strike and a Guided shot are given one
// range of travel at launch, and a flame front is scanned out to the range
// (Weapon::shotEndsAtRange). Retail measures their reach centre to centre
// (530580, inclusive; the lightning, LOS, fire and Guided vtables all point
// at it). The engine padded a structure target's range by its footprint
// (8 * larger side + 24 px), so a Crusades Death Totem (zonglyph, lightning,
// range 550) took a Taros lodestone 584 px off and emptied its bolts into the
// air ~540 px out (a 0.9.12 replay), and a mobile caster parked in the same
// dead band. A Ballistic shell (its own arc test, 52bc60) and melee (footprint
// contact, 52b980) keep their reach.
//
// Checks (flat 128x128 map, shipped units):
//   - every shipped weapon of either roster: the melee, Ballistic, Dropped,
//     Remote and Wandering ones are never "ends at range";
//   - Crusades: a zonglyph 584.5 px from an enemy tarlode never targets it
//     and fires nothing; at ~538 px it targets it and its bolts hit;
//   - a mobile unit of each kind ordered to attack an enemy building closes to
//     its plain range (centre to centre) and its shots hit;
//   - an Archer (Ballistic) and a Swordsman (melee) still reach and hit one.
//
// A dev harness; not shipped in a game.

#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include "tnt/tnt.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>

using namespace tak;

namespace {
int failures = 0;
sim::PathfindingMode mode = sim::PathfindingMode::Retail;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(),
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) ++failures;
}

struct Scene {
    tnt::Map map;
    sim::World w;
};

void build(Scene& s, const hpi::Vfs& vfs, const sim::TypeRegistry& reg) {
    s.map.width = s.map.height = 128;
    s.map.seaLevel = 0;
    s.map.heights.assign(size_t(128) * 128, 62);
    s.map.features.assign(size_t(128) * 128, 0xffff);
    s.w.setPathfindingMode(mode);
    s.w.setTerrain(s.map.heights, s.map.width, s.map.height, s.map.seaLevel, &s.map.features);
    sim::registerMapFeatures(s.w, s.map, vfs, &reg);
    s.w.setPathService(true);
}

// A building that never fights back and does not die. spawn() leaves nav to
// its caller, as a placement does.
int building(sim::World& w, const sim::UnitType* t, float x, float z) {
    const int id = w.spawn(t, x, z, {}, 1);
    w.blockFoot(*t, x, z, true);
    w.setStance(id, 2);
    w.unit(id)->fireState = 0;
    w.unit(id)->hp = sim::Fixed::fromInt(30000);
    return id;
}

bool holds(const sim::Unit* u, int target) {
    if (!u) return false;
    if (!u->orders.empty() && u->orders.front().targetId == target) return true;
    for (int s : u->slotTarget) if (s == target) return true;
    return false;
}

const char* kindName(const sim::Weapon& w) {
    if (w.lightning) return "lightning";
    if (w.straight) return "line of sight";
    if (w.flameKind >= 0) return "flame";
    return "guided";
}

// Every shipped weapon: which ones end at their range, and that no melee,
// Ballistic, Dropped, Remote or Wandering weapon is among them.
void survey(const sim::TypeRegistry& reg, const char* roster) {
    std::map<std::string, std::set<std::string>> units;   // kind -> unit ids
    std::set<const sim::Weapon*> seen;
    int bad = 0, mobile = 0, structures = 0, total = 0;
    std::string badList;
    for (const auto& [id, t] : reg.types()) {
        bool any = false;
        for (const auto& w : t.weapons) {
            const bool other = w.melee || w.ballisticClass() || w.kind == sim::Weapon::Kind::Dropped ||
                               w.kind == sim::Weapon::Kind::Remote || w.kind == sim::Weapon::Kind::Wandering;
            if (!w.shotEndsAtRange()) continue;
            if (other) { ++bad; badList += " " + id + "/" + w.name; }
            if (w.structurePad(4) != 0) { ++bad; badList += " pad:" + id + "/" + w.name; }
            units[kindName(w)].insert(id);
            any = true;
        }
        for (const auto& w : t.weapons)
            if (!w.shotEndsAtRange() && w.structurePad(2) != 40) { ++bad; badList += " pad2:" + id + "/" + w.name; }
        if (any) { ++total; (t.isStructure() ? structures : mobile)++; }
    }
    std::printf("  %s roster: %d unit types fire a shot that ends at its range (%d mobile, %d structures)\n",
                roster, total, mobile, structures);
    for (const auto& [kind, ids] : units) {
        std::printf("    %-13s %2zu:", kind.c_str(), ids.size());
        for (const auto& id : ids) std::printf(" %s", id.c_str());
        std::printf("\n");
    }
    check(bad == 0, std::string(roster) + ": no melee, Ballistic, Dropped, Remote or Wandering weapon ends at its range",
          badList);
}

struct Static {
    int held = 0;       // ticks the shooter held the target
    int shots = 0;      // ticks with a shot of its in the air aimed at the target
    int hits = 0;       // impacts on the target
};

// A static shooter (player 0, fire at will) and an enemy building at
// (dx, dz) from it.
Static staticShooter(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const char* shooterId,
                     const char* targetId, float dx, float dz, int ticks) {
    Scene s;
    build(s, vfs, reg);
    const sim::UnitType* st = reg.find(shooterId);
    const sim::UnitType* tt = reg.find(targetId);
    if (!st || !tt) { check(false, std::string("shipped ") + shooterId + " and " + targetId + " load"); return {}; }
    const float sx = 1000, sz = 600;
    const int shooter = s.w.spawn(st, sx, sz, {}, 0);
    s.w.blockFoot(*st, sx, sz, true);
    s.w.player(0).mana = 30000;
    const int target = building(s.w, tt, sx + dx, sz + dz);
    Static out;
    for (int i = 0; i < ticks; ++i) {
        s.w.tick(1.f / 30);
        if (holds(s.w.unit(shooter), target)) ++out.held;
        for (const auto& p : s.w.projectiles())
            if (p.fromId == shooter && p.targetId == target) { ++out.shots; break; }
        for (const auto& h : s.w.hits())
            if (h.fromPlayer == 0 && h.victimId == target) ++out.hits;
    }
    return out;
}

// The weapon an attack approaches on (World::approachSlot): the selected one
// for a switcher (a fresh unit's first), the longest when they all fire.
const sim::Weapon* reachWeapon(const sim::UnitType& t) {
    if (t.weapons.empty()) return nullptr;
    int s = 0;
    if (!t.weaponSwitching && t.weapons.size() > 1)
        for (int i = 0; i < int(t.weapons.size()); ++i)
            if (t.weapons[size_t(i)].range > t.weapons[size_t(s)].range) s = i;
    return &t.weapons[size_t(s)];
}

struct Approach {
    float dist = 0;     // centre distance where it stood at the end
    int hits = 0;       // impacts on the target
    int range = 0;      // the weapon it approached on
    bool ok = false;
};

// A mobile shooter 1100 px from an enemy building, ordered to attack it.
Approach attackBuilding(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const std::string& shooterId,
                        const char* targetId, int ticks) {
    Scene s;
    build(s, vfs, reg);
    const sim::UnitType* st = reg.find(shooterId);
    const sim::UnitType* tt = reg.find(targetId);
    if (!st || !tt) { check(false, "shipped " + shooterId + " and " + targetId + " load"); return {}; }
    const float tx = 1600, tz = 1024;
    const int shooter = s.w.spawn(st, tx - 1100, tz, {}, 0);
    s.w.player(0).mana = 30000;
    const int target = building(s.w, tt, tx, tz);
    for (int i = 0; i < 30; ++i) s.w.tick(1.f / 30);
    s.w.attack(shooter, target, false);
    Approach out;
    for (int i = 0; i < ticks; ++i) {
        s.w.tick(1.f / 30);
        for (const auto& h : s.w.hits())
            if (h.fromPlayer == 0 && h.victimId == target) ++out.hits;
    }
    const sim::Unit* u = s.w.unit(shooter);
    const sim::Unit* t = s.w.unit(target);
    if (!u || !t) return out;
    out.dist = std::hypot((t->x - u->x).toFloat(), (t->z - u->z).toFloat());
    out.range = reachWeapon(*u->type) ? reachWeapon(*u->type)->range : 0;
    out.ok = true;
    return out;
}

std::string describe(const Approach& a) {
    char b[160];
    std::snprintf(b, sizeof b, "stood %.1f px off (range %d), %d hits", a.dist, a.range, a.hits);
    return b;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: shot_range_test <retail-install-dir> [--legion]\n"); return 2; }
    for (int i = 2; i < argc; ++i)
        if (std::string(argv[i]) == "--legion") mode = sim::PathfindingMode::Legion;
    hpi::Vfs vfs = hpi::mountRetailRoot(argv[1]);
    sim::TypeRegistry standard, crusades;
    sim::setupRegistry(standard, vfs, false);
    sim::setupRegistry(crusades, vfs, true);
    std::printf("[%s pathfinding]\n", mode == sim::PathfindingMode::Legion ? "Legion" : "Retail");

    survey(standard, "standard");
    survey(crusades, "Crusades");

    // The 0.9.12 replay's geometry: totem at (2600, 2952), lodestone at
    // (2304, 3456), 584.49 px apart. Retail's 530580 at range 550: out.
    {
        const Static far = staticShooter(vfs, crusades, "zonglyph", "tarlode", -296, 504, 900);
        std::printf("  Death Totem, lodestone 584.5 px off: held %d ticks, %d shots, %d hits\n", far.held, far.shots,
                    far.hits);
        check(far.held == 0 && far.shots == 0,
              "a Crusades Death Totem (range 550) never targets a lodestone 584.5 px off");
        const Static near = staticShooter(vfs, crusades, "zonglyph", "tarlode", -272, 464, 900);
        std::printf("  Death Totem, lodestone 537.8 px off: held %d ticks, %d shots, %d hits\n", near.held, near.shots,
                    near.hits);
        check(near.held > 0 && near.hits > 0, "at 537.8 px it targets the lodestone and its bolts hit");
    }

    // Mobile units of each kind attack an enemy building (an Aramon Barracks,
    // 7x12: the old pad was 120 px, its half width 56). Each must stand within
    // its plain range and hit; before, each parked up to the pad beyond and
    // its shots died short.
    const char* target = "arakeep";
    {
        struct Case { const char* unit; const char* what; };
        const Case cases[] = {
            {"verarch", "a line-of-sight shooter (Veruna Crossbowman)"},
            {"zonsham", "a lightning caster (Zhon Shaman)"},
            {"tarfire", "a Guided caster (Taros Fire Demon)"},
            {"crefire", "a flame thrower (Creon Fire Wagon)"},
            {"zondrake", "a flying flame breather (Zhon Drake)"},
        };
        for (const auto& c : cases) {
            const sim::UnitType* t = standard.find(c.unit);
            if (!t) { check(false, std::string("shipped ") + c.unit + " loads"); continue; }
            const sim::Weapon* w = reachWeapon(*t);
            if (!w || !w->shotEndsAtRange()) {
                check(false, std::string(c.unit) + "'s weapon ends at its range", "fixture");
                continue;
            }
            const Approach a = attackBuilding(vfs, standard, c.unit, target, 1800);
            std::printf("  %s vs %s: %s\n", c.unit, target, describe(a).c_str());
            check(a.ok && a.dist <= float(a.range) + 0.5f && a.hits > 0,
                  std::string(c.what) + " ordered to attack a building closes to its plain range and hits",
                  describe(a));
        }
    }

    // A Ballistic archer and a melee swordsman keep their reach.
    {
        const Approach bow = attackBuilding(vfs, standard, "araarch", target, 1800);
        std::printf("  araarch vs %s: %s\n", target, describe(bow).c_str());
        check(bow.ok && bow.hits > 0, "a Ballistic archer still reaches and hits a building", describe(bow));
        const Approach sword = attackBuilding(vfs, standard, "arasword", target, 1800);
        std::printf("  arasword vs %s: %s\n", target, describe(sword).c_str());
        check(sword.ok && sword.hits > 0, "a melee swordsman still reaches and hits a building", describe(sword));
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
