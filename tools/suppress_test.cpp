// suppress_test: armed ATTACK on the ground fires at the point (retail SUPPRESS).
//
//   suppress_test <retail-install-dir> [--legion]
//
// Retail gives an armed Attack on the ground (4de530 kind 3 with a point and
// no target) to a ground unit or a structure whose selected weapon is ranged
// as SUPPRESS (0x404d70, docs/retail-engine.md, "Fire at a point"): every
// slot aims at the point (a switcher only its selected one, a ranged unit
// not its melee ones); a point out of reach or inside the minimum range
// sends a walker toward it on rings that shrink by a third each try, firing
// at will on the way, while a tower keeps the order and looks again; after
// each shot of the main weapon the order goes to the back of the queue, so a
// lone point is fired at for ever and Shift-queued points take turns. A
// melee weapon gets the order only over a blocking, clearable feature, walks
// up to it and hits it until it is gone; an anti-air weapon gets nothing.
// A flyer gets AIRSTRIKE (a `dropped` weapon), AIRTOGROUNDHOVER (the type's
// hoverattack) or AIRTOGROUND at the point, each with no target unit.
//
// Checks (flat map, shipped units, Cmd::AttackGround through applyCommand):
//   - an Archer in reach fires at the point without moving, and keeps the
//     order; its shots carry no target unit and land on the point;
//   - out of reach it walks into reach and fires; a minimum-range unit too
//     close backs off past its minimum range and fires;
//   - a tower in reach fires; out of reach it keeps the order, stands, never
//     fires, and its slots stay automatic;
//   - two Shift-queued points take turns, one shot each;
//   - a switcher aims its selected slot only;
//   - a melee unit gets no order on bare ground; over a tree it walks up,
//     fells it, and the order ends;
//   - an anti-air weapon gets no order, on the ground or in the air;
//   - a flyer without hoverattack flies attack runs over the point and its
//     shots land there; a hoverattack flyer hovers its distance off the
//     point and fires; a `dropped` weapon's flyer strikes the point; each
//     keeps the order and carries no target;
//   - a switcher flyer whose selected weapon becomes `dropped` takes the
//     lost-target exit: VTOL_SEEKATTACK at the point;
//   - the same run twice gives the same state hash; the wire takes the
//     command from protocol 312 only.
//
// A dev harness; not shipped in a game.

#include "hpi/hpi.h"
#include "net/protocol.h"
#include "server/commands.h"
#include "sim/footprint.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include "tnt/tnt.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
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

void build(Scene& s, const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const char* feature = nullptr,
           int fx = 0, int fz = 0) {
    s.map.width = s.map.height = 96;
    s.map.seaLevel = 0;
    s.map.heights.assign(size_t(96) * 96, 70);
    s.map.features.assign(size_t(96) * 96, 0xffff);
    if (feature) {
        s.map.featureNames = {feature};
        s.map.features[size_t(fz) * 96 + size_t(fx)] = 0;
    }
    s.w.setPathfindingMode(mode);
    s.w.setTerrain(s.map.heights, s.map.width, s.map.height, s.map.seaLevel, &s.map.features);
    sim::registerMapFeatures(s.w, s.map, vfs, &reg);
    s.w.setPathService(true);
}

// Cmd::AttackGround for one unit, as the server hands it to the sim.
void attackGround(sim::World& w, const sim::TypeRegistry& reg, int id, float x, float z, bool queue = false) {
    net::Command c;
    c.kind = net::Cmd::AttackGround;
    c.player = uint8_t(w.unit(id)->player);
    c.unitId = id;
    c.x = x; c.z = z;
    c.queue = queue;
    sim::applyCommand(w, reg, c);
}

struct Run {
    int shots = 0;               // ticks with a shot released
    int firstShot = -1;          // tick of the first one
    float moved = 0;             // farthest the shooter got from where it stood
    float endDist = 0;           // shooter to the (first) point at the end
    float minDist = 1e9f;        // ... the nearest it came
    float hitDist = -1;          // mean impact distance from the nearest point
    int hits = 0;
    bool targetedShot = false;   // a projectile of its with a target unit
    bool approach = false;       // an approach leg went ahead of the order
    bool orderAtEnd = false;     // a SUPPRESS (or air point) order is still queued at the end
    uint8_t airKind = 0;         // Order::airPoint of the first order given
    bool airborne = false;       // a flyer was in the air at some tick
    double lateDist = 0;         // mean shooter-to-point distance over the last third
    size_t ordersAtEnd = 0;
    std::vector<int> sequence;   // per impact: index of the nearest point
    uint8_t slotPointMax = 0;    // every slot that held a point
    int aims = 0;                // times its slots took the point (stage 3)
    bool treeAtStart = false;    // a live blocking feature under the first point
    bool treeAtEnd = false;
    int ringGoals = 0;           // ticks a hover ring controller (4e4540(4)) was set
    bool ringWithoutFixed = false; // ... one lacking the fixed heading or altitude (0x48)
    uint64_t hash = 0;
};

struct Shooter {
    const char* id;
    float x = 300, z = 768;
    float heading = 1.5707963f;  // facing +x
    int player = 0;
};

// A shooter of player 0 at (x, z) is given Cmd::AttackGround on each point
// (the second and later Shift-queued) and watched for `ticks`.
Run fireAt(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const Shooter& sh,
           const std::vector<std::pair<float,float>>& points, int ticks,
           const std::function<void(Scene&)>& setup = {}, const char* feature = nullptr, int fx = 0, int fz = 0) {
    Scene s;
    build(s, vfs, reg, feature, fx, fz);
    if (setup) setup(s);
    Run r;
    const auto treeAt = [&](float px, float pz) {
        // A live blocking feature whose footprint covers the point's cell.
        const int cx = int(px) / 16, cz = int(pz) / 16;
        for (const auto& f : s.w.features()) {
            if (!f.alive || !f.blocks) continue;
            const int ax = sim::footprintOrigin(f.x, f.fx), az = sim::footprintOrigin(f.z, f.fz);
            if (cx >= ax && cx < ax + f.fx && cz >= az && cz < az + f.fz) return true;
        }
        return false;
    };
    r.treeAtStart = treeAt(points[0].first, points[0].second);
    const sim::UnitType* st = reg.find(sh.id);
    if (!st) { check(false, std::string("shipped ") + sh.id + " loads"); return r; }
    const int shooter = s.w.spawn(st, sh.x, sh.z, sh.heading, sh.player);
    s.w.unit(shooter)->mana = st->maxMana;
    s.w.unit(shooter)->fireState = 0;   // hold fire: only the order shoots
    for (int i = 0; i < 30; ++i) s.w.tick(kTick);
    const float x0 = s.w.unit(shooter)->x.toFloat(), z0 = s.w.unit(shooter)->z.toFloat();
    for (size_t i = 0; i < points.size(); ++i)
        attackGround(s.w, reg, shooter, points[i].first, points[i].second, i > 0);
    if (const sim::Unit* u = s.w.unit(shooter); u && !u->orders.empty()) r.airKind = u->orders.front().airPoint;
    double hitSum = 0, lateSum = 0;
    int lateN = 0;
    bool held = false;
    for (int i = 0; i < ticks; ++i) {
        const int now = int(s.w.tickCount());
        s.w.tick(kTick);
        const sim::Unit* u = s.w.unit(shooter);
        if (!u || !u->alive()) break;
        if (u->firedWeapons) { ++r.shots; if (r.firstShot < 0) r.firstShot = now; }
        r.moved = std::max(r.moved, std::hypot(u->x.toFloat() - x0, u->z.toFloat() - z0));
        const float d = std::hypot(u->x.toFloat() - points[0].first, u->z.toFloat() - points[0].second);
        r.minDist = std::min(r.minDist, d);
        r.endDist = d;
        if (i >= ticks * 2 / 3) { lateSum += d; ++lateN; }
        r.airborne = r.airborne || u->flightGroundMode == 2;
        if (!u->orders.empty() && u->orders.front().airPoint == sim::Order::kAirHover &&
            u->orders.front().flightGoal && u->orders.front().flightGoal->radius == 4) {
            ++r.ringGoals;
            if ((u->orders.front().flightGoal->flags & 0x48) != 0x48) r.ringWithoutFixed = true;
        }
        if (u->slotPoint && !held) ++r.aims;
        held = u->slotPoint != 0;
        r.slotPointMax |= u->slotPoint;
        if (!u->orders.empty() && u->orders.front().suppressApproach) r.approach = true;
        for (const auto& p : s.w.projectiles())
            if (p.fromId == shooter && p.targetId) r.targetedShot = true;
        for (const auto& h : s.w.hits()) {   // the shooter is the scene's only armed unit
            int best = 0; float bestD = 1e9f;
            for (size_t k = 0; k < points.size(); ++k) {
                const float e = std::hypot(h.x - points[k].first, h.z - points[k].second);
                if (e < bestD) { bestD = e; best = int(k); }
            }
            hitSum += bestD; ++r.hits; r.sequence.push_back(best);
        }
    }
    if (r.hits) r.hitDist = float(hitSum / r.hits);
    if (lateN) r.lateDist = lateSum / lateN;
    if (const sim::Unit* u = s.w.unit(shooter)) {
        r.ordersAtEnd = u->orders.size();
        for (const auto& o : u->orders) r.orderAtEnd = r.orderAtEnd || o.suppress || o.airPoint;
    }
    r.treeAtEnd = treeAt(points[0].first, points[0].second);
    r.hash = s.w.stateHash();
    return r;
}

std::string describe(const Run& r) {
    char b[300];
    std::snprintf(b, sizeof b, "%d shots (first %d), %d hits %.1f px off, moved %.0f, %.0f px off at the end "
                  "(nearest %.0f), approach %d, order %d, %zu orders, targeted %d",
                  r.shots, r.firstShot, r.hits, r.hitDist, r.moved, r.endDist, r.minDist, r.approach,
                  r.orderAtEnd, r.ordersAtEnd, r.targetedShot);
    return b;
}

// Is the order refused (no order queued)?
bool refused(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const char* id, float x, float z,
             const char* feature = nullptr, int fx = 0, int fz = 0) {
    Scene s;
    build(s, vfs, reg, feature, fx, fz);
    const sim::UnitType* t = reg.find(id);
    if (!t) return false;
    const int u = s.w.spawn(t, 300, 768, 1.5707963f, 0);
    s.w.tick(kTick);
    attackGround(s.w, reg, u, x, z);
    return s.w.unit(u)->orders.empty();
}

const sim::UnitType* pick(const sim::TypeRegistry& reg, const std::vector<const char*>& prefer,
                          const std::function<bool(const sim::UnitType&)>& ok) {
    for (const char* id : prefer)
        if (const auto* t = reg.find(id); t && ok(*t)) return t;
    for (const auto& [id, t] : reg.types())
        if (ok(t)) return &t;
    return nullptr;
}

// A flyer given Cmd::AttackGround on a point 600 px off: the mission it
// gets, its shots and where it flies.
Run flyAt(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const sim::UnitType* t, int ticks) {
    return fireAt(vfs, reg, {t->id.c_str()}, {{900, 768}}, ticks);
}

std::string describeAir(const Run& r) {
    char b[120];
    std::snprintf(b, sizeof b, ", kind %u, airborne %d, late distance %.0f", unsigned(r.airKind), r.airborne,
                  r.lateDist);
    return describe(r) + b;
}

// A Run for a type the registry does not hold (a stand-in copy).
Run flyCopy(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const sim::UnitType& copy, int ticks,
            const std::function<void(Scene&, int)>& during = {}, int at = -1, bool* seek = nullptr) {
    Scene s;
    build(s, vfs, reg);
    Run r;
    const int id = s.w.spawn(&copy, 300, 768, 1.5707963f, 0);
    s.w.unit(id)->mana = copy.maxMana;
    s.w.unit(id)->fireState = 0;
    for (int i = 0; i < 30; ++i) s.w.tick(kTick);
    attackGround(s.w, reg, id, 900, 768);
    if (const sim::Unit* u = s.w.unit(id); u && !u->orders.empty()) r.airKind = u->orders.front().airPoint;
    double hitSum = 0;
    for (int i = 0; i < ticks; ++i) {
        if (i == at && during) during(s, id);
        s.w.tick(kTick);
        const sim::Unit* u = s.w.unit(id);
        if (!u || !u->alive()) break;
        if (u->firedWeapons) { ++r.shots; if (r.firstShot < 0) r.firstShot = int(s.w.tickCount()); }
        r.airborne = r.airborne || u->flightGroundMode == 2;
        for (const auto& p : s.w.projectiles())
            if (p.fromId == id && p.targetId) r.targetedShot = true;
        for (const auto& h : s.w.hits()) { hitSum += std::hypot(h.x - 900, h.z - 768); ++r.hits; }
        if (seek && !u->orders.empty() && u->orders.front().seekAttack &&
            std::abs(u->orders.front().x.toFloat() - 900) < 1 && std::abs(u->orders.front().z.toFloat() - 768) < 1)
            *seek = true;
    }
    if (r.hits) r.hitDist = float(hitSum / r.hits);
    if (const sim::Unit* u = s.w.unit(id)) {
        r.ordersAtEnd = u->orders.size();
        for (const auto& o : u->orders) r.orderAtEnd = r.orderAtEnd || o.airPoint;
    }
    r.hash = s.w.stateHash();
    return r;
}

void flyerGroups(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const sim::UnitType& runner,
                 const sim::UnitType& hover);

void flyers(const hpi::Vfs& vfs, const sim::TypeRegistry& reg) {
    using sim::Order;
    const auto armed = [](const sim::UnitType& t) {
        if (!t.canFly || t.isStructure() || !t.canMove || t.weapons.empty() || t.weaponSwitching) return false;
        const auto& w = t.weapons[0];
        return !w.toAir && !w.melee && !w.commandFire && !w.cursorAirstrike && w.range > 0 && w.damage > 0;
    };
    const sim::UnitType* runner = pick(reg, {}, [&](const sim::UnitType& t) { return armed(t) && !t.hoverAttack; });
    const sim::UnitType* hover = pick(reg, {}, [&](const sim::UnitType& t) { return armed(t) && t.hoverAttack; });
    std::printf("  flyers: attack runs %s, hover %s\n", runner ? runner->id.c_str() : "-",
                hover ? hover->id.c_str() : "-");
    if (!runner || !hover) { check(false, "the shipped data has a flyer of each kind"); return; }
    flyerGroups(vfs, reg, *runner, *hover);

    // AIRTOGROUND: attack runs over the point.
    {
        const Run r = flyAt(vfs, reg, runner, 1500);
        std::printf("  %s at a point 600 px off: %s\n", runner->id.c_str(), describeAir(r).c_str());
        check(r.airKind == Order::kAirRun, "a flyer without hoverattack gets AIRTOGROUND at the point", describeAir(r));
        check(r.airborne && r.shots >= 2 && r.hits >= 2 && r.hitDist < 64 && !r.targetedShot,
              "it flies over the point and its shots land there, with no target", describeAir(r));
        check(r.orderAtEnd && r.ordersAtEnd == 1, "and keeps the order", describeAir(r));
        check(flyAt(vfs, reg, runner, 1500).hash == r.hash, "the same run gives the same state hash");
    }
    // AIRTOGROUNDHOVER: hovers its distance off the point and fires.
    {
        const Run r = flyAt(vfs, reg, hover, 1500);
        const int d = hover->weapons[0].hoverAttackDistance ? hover->weapons[0].hoverAttackDistance
                                                            : hover->hoverAttackDistance;
        std::printf("  %s (hoverattack, distance %d) at a point 600 px off: %s\n", hover->id.c_str(), d,
                    describeAir(r).c_str());
        check(r.airKind == Order::kAirHover, "a hoverattack flyer gets AIRTOGROUNDHOVER at the point", describeAir(r));
        check(r.airborne && std::abs(r.lateDist - d) < 48, "it hovers its hoverattack distance off the point",
              describeAir(r));
        check(r.shots >= 2 && r.hits >= 2 && r.hitDist < 64 && !r.targetedShot,
              "and its shots land on the point, with no target", describeAir(r));
        check(r.orderAtEnd && r.ordersAtEnd == 1, "and keeps the order", describeAir(r));
        // 41df8f..41dfd3 / 41e225..41e269: every ring controller without a
        // target is 4e40e0 + 4e4520 (0x40) + 4e4540 (0x10) + 4e44c0 (0x8).
        check(r.ringGoals > 0 && !r.ringWithoutFixed,
              "each hover ring point has a fixed heading and the attack altitude (0x48)", describeAir(r));
    }
    // AIRSTRIKE: no shipped weapon has `dropped`; the attack-run flyer with
    // its weapon given the key stands in.
    {
        sim::UnitType striker = *runner;
        striker.weapons[0].cursorAirstrike = true;
        const Run r = flyCopy(vfs, reg, striker, 1500);
        std::printf("  %s with a dropped weapon at a point 600 px off: %s\n", runner->id.c_str(), describeAir(r).c_str());
        check(r.airKind == Order::kAirStrike, "a `dropped` weapon gets AIRSTRIKE at the point", describeAir(r));
        check(r.airborne && r.shots >= 1 && r.hits >= 1 && r.hitDist < 96 && !r.targetedShot,
              "it strikes the point, with no target", describeAir(r));
        check(r.orderAtEnd, "and keeps the order", describeAir(r));
    }
    // A selected weapon that has `dropped` when AIRTOGROUND wakes: the lost
    // exit, VTOL_SEEKATTACK at the point (41c8f6). The order is given on the
    // plain type; the key appears on the unit's weapon mid-mission, as a
    // switch to such a slot would give it.
    {
        sim::UnitType plain = *runner;
        sim::UnitType keyed = *runner;
        keyed.weapons[0].cursorAirstrike = true;
        bool seek = false;
        const Run r = flyCopy(vfs, reg, plain, 300, [&](Scene& s, int id) {
            s.w.unit(id)->type = &keyed;
            s.w.unit(id)->missionEvents |= 0x40;   // the switch's event wakes the mission
        }, 120, &seek);
        std::printf("  %s whose weapon turns dropped: %s, seek %d\n", runner->id.c_str(), describeAir(r).c_str(), seek);
        // Hold Fire skips the seek (41c91c); on Fire At Will it goes in.
        check(!r.orderAtEnd, "a selected `dropped` weapon ends AIRTOGROUND (Hold Fire: no seek)", describeAir(r));
        bool seekFree = false;
        sim::UnitType plain2 = *runner;
        flyCopy(vfs, reg, plain2, 300, [&](Scene& s, int id) {
            s.w.unit(id)->fireState = 2;
            s.w.unit(id)->type = &keyed;
            s.w.unit(id)->missionEvents |= 0x40;
        }, 120, &seekFree);
        check(!seek && seekFree, "on Fire At Will it seeks at the point (VTOL_SEEKATTACK, no target)");
    }
    // An anti-air weapon in the air: no order.
    {
        sim::UnitType aa = *runner;
        aa.weapons[0].toAir = true;
        Scene s;
        build(s, vfs, reg);
        const int u = s.w.spawn(&aa, 300, 768, 1.5707963f, 0);
        s.w.tick(kTick);
        attackGround(s.w, reg, u, 500, 768);
        check(s.w.unit(u)->orders.empty(), "a flyer's anti-air weapon gets no order");
    }
}

// A Ctrl group (squad 1) in Retail: the outlier is given SUPPRESS on a point
// beside the group's line of march while it stands in its slot; the rest of
// the group then walks off. SUPPRESS's group check (404e12, level 4) finds
// the outlier out of its slot at a wake and puts Move_Ground_Formation
// (level -3) ahead of the order: it walks back to the group with its slot
// still aimed at the point, and fires on the way if its weapon does not need
// the body's facing (52ae90 reads no mission). Back in its slot the
// formation retires, every slot goes automatic, and SUPPRESS starts again at
// stage 0 and fires from there. Legion has no group record: nothing of this.
struct GroupRun {
    int formations = 0;          // formation legs SUPPRESS put ahead of itself
    int walkTicks = 0;           // ticks one led
    int walkShots = 0;           // ... with a shot released on the move (standing, before its
                                 // route comes, it still faces the point)
    int walkHeld = 0;            // ... with the slot still holding the point
    int afterShots = 0;          // shots once the last formation had retired
    int dropped = -1;            // tick the SUPPRESS order went (the leave, 8)
    int shotsSince = 0;          // shots since the first formation retired
    bool retired = false;        // a formation retired with SUPPRESS behind it
    bool orderAtEnd = false;     // the outlier's SUPPRESS order is still queued
    bool otherLegs = false;      // a formation leg with anything else behind it
    float hitDist = -1;          // mean impact distance from the point
    int hits = 0;
    uint64_t hash = 0;
};

GroupRun groupFire(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const sim::UnitType& outlier,
                   const sim::UnitType& member, int ticks, float march = 0.9f,
                   const std::function<void(Scene&, int, float, float)>& onWalk = {}) {
    Scene s;
    build(s, vfs, reg);
    GroupRun r;
    const float range = float(outlier.weapons[0].range);
    std::vector<int> group;
    for (int i = 0; i < 4; ++i)
        group.push_back(s.w.spawn(&member, 600 + float(i % 2) * 48 - 24, 640 + float(i / 2) * 48, 3.1415926f, 0));
    const int id = s.w.spawn(&outlier, 600, 800, 3.1415926f, 0);   // facing -z, the group's way
    for (int g : group) { s.w.setSquad(g, 1); s.w.unit(g)->fireState = 0; }
    s.w.setSquad(id, 1);
    s.w.unit(id)->fireState = 0;
    for (int i = 0; i < 30; ++i) s.w.tick(kTick);
    const float px = 600 + range * 0.3f, pz = 800 - range * 0.5f;
    attackGround(s.w, reg, id, px, pz);
    for (int i = 0; i < 60; ++i) s.w.tick(kTick);
    for (int g : group) {
        const float gx = s.w.unit(g)->x.toFloat(), gz = s.w.unit(g)->z.toFloat();
        // march < 0: a patrol across the map and back, so the group keeps
        // leaving the outlier behind.
        if (march < 0) s.w.patrol(g, gx + 800, gz);
        else s.w.order(g, gx, gz - range * march, false);
    }
    bool walking = false;
    double hitSum = 0;
    for (int i = 0; i < ticks; ++i) {
        s.w.tick(kTick);
        const sim::Unit* u = s.w.unit(id);
        if (!u || !u->alive()) break;
        const auto& o = u->orders;
        if (r.dropped < 0 && std::none_of(o.begin(), o.end(), [](const sim::Order& q) { return q.suppress; }))
            r.dropped = i;
        const size_t leg = sim::World::currentLeg(o);
        const bool lead = !o.empty() && o[leg].formationLevel && leg + 1 < o.size() && o[leg + 1].suppress;
        if (!o.empty() && o[leg].formationLevel && !lead) r.otherLegs = true;
        const bool pushed = lead && !walking;
        if (pushed) ++r.formations;
        if (pushed && onWalk) { onWalk(s, id, px, pz); return r; }
        if (!lead && walking && !o.empty() && o[leg].suppress) { r.retired = true; r.afterShots = 0; }
        walking = lead;
        if (lead) {
            ++r.walkTicks;
            if (u->firedWeapons && !pushed && u->speed > sim::Fixed()) ++r.walkShots;
            if (u->slotPoint) ++r.walkHeld;
        } else if (r.retired && u->firedWeapons) ++r.afterShots;
        for (const auto& h : s.w.hits()) { hitSum += std::hypot(h.x - px, h.z - pz); ++r.hits; }
    }
    if (r.hits) r.hitDist = float(hitSum / r.hits);
    if (const sim::Unit* u = s.w.unit(id))
        for (const auto& o : u->orders) r.orderAtEnd = r.orderAtEnd || o.suppress;
    r.hash = s.w.stateHash();
    return r;
}

std::string describe(const GroupRun& r) {
    char b[300];
    std::snprintf(b, sizeof b, "%d formations, %d walk ticks (%d shots, %d holding the point), retired %d, "
                  "%d shots after, order %d (gone at %d), %d hits %.1f px off", r.formations, r.walkTicks,
                  r.walkShots, r.walkHeld, r.retired, r.afterShots, r.orderAtEnd, r.dropped, r.hits, r.hitDist);
    return b;
}

// An all-flyer Ctrl group at a point: the outlier starts far from the rest.
// The flyer attacks' group check (51d1e0(unit, 1, 5): AirToGround 41c19a,
// AirStrike 41b7a4, AirToGroundHover 41dafa) puts VTOL_Move_Formation (level
// -4) ahead of its attack; once back it attacks again from stage 0.
GroupRun flyGroup(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const sim::UnitType& t, int ticks) {
    Scene s;
    build(s, vfs, reg);
    GroupRun r;
    std::vector<int> ids;
    for (int i = 0; i < 3; ++i) ids.push_back(s.w.spawn(&t, 160, 160 + float(i) * 96, 1.5707963f, 0));
    const int id = s.w.spawn(&t, 1400, 1400, 1.5707963f, 0);
    ids.push_back(id);
    for (int g : ids) { s.w.setSquad(g, 1); s.w.unit(g)->fireState = 0; s.w.unit(g)->mana = t.maxMana; }
    for (int i = 0; i < 30; ++i) s.w.tick(kTick);
    for (int g : ids) attackGround(s.w, reg, g, 900, 700);
    bool walking = false;
    for (int i = 0; i < ticks; ++i) {
        s.w.tick(kTick);
        const sim::Unit* u = s.w.unit(id);
        if (!u || !u->alive()) break;
        const auto& o = u->orders;
        const bool lead = o.size() > 1 && o[0].formationLevel && o[1].airPoint;
        if (!o.empty() && o[0].formationLevel && !lead) r.otherLegs = true;
        if (lead && !walking) ++r.formations;
        if (!lead && walking && !o.empty() && o[0].airPoint) { r.retired = true; r.afterShots = 0; }
        walking = lead;
        if (lead) { ++r.walkTicks; if (u->slotPoint) ++r.walkHeld; }
        else if (r.retired && u->firedWeapons) ++r.afterShots;
        if (r.retired && u->firedWeapons) ++r.shotsSince;
    }
    if (const sim::Unit* u = s.w.unit(id))
        for (const auto& o : u->orders) r.orderAtEnd = r.orderAtEnd || o.airPoint;
    r.hash = s.w.stateHash();
    return r;
}

void flyerGroups(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const sim::UnitType& runner,
                 const sim::UnitType& hover) {
    sim::UnitType striker = runner;
    striker.weapons[0].cursorAirstrike = true;
    const std::pair<const char*, const sim::UnitType*> kinds[] = {
        {"AIRTOGROUND", &runner}, {"AIRTOGROUNDHOVER", &hover}, {"AIRSTRIKE", &striker}};
    for (const auto& [name, t] : kinds) {
        const GroupRun r = flyGroup(vfs, reg, *t, 2400);
        std::printf("  %s outlier of an all-flyer group: %s\n", name, describe(r).c_str());
        // A strike's passes reach far: it may be sent back again before its
        // next release, so its shots are counted from the first return.
        if (mode == sim::PathfindingMode::Retail)
            check(r.formations >= 1 && !r.otherLegs && r.retired && r.orderAtEnd && r.shotsSince >= 1,
                  std::string(name) + ": out of its slot it flies back on VTOL_Move_Formation, then attacks again",
                  describe(r) + ", " + std::to_string(r.shotsSince) + " shots since the first return");
        else
            check(r.formations == 0 && r.orderAtEnd, std::string(name) + ": Legion has no group check", describe(r));
    }
}

void groups(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const sim::UnitType& archer) {
    // A turret type (turninplacerate 0: no facing request) and one that has
    // to face the point to fire (turninplacerate > 0, max(aimtol, 512)).
    sim::UnitType turret = archer, facing = archer;
    turret.turnInPlaceRate = 0;
    facing.turnInPlaceRate = std::max(archer.turnInPlaceRate, 400);
    {
        const GroupRun r = groupFire(vfs, reg, turret, archer, 1500);
        std::printf("  turret outlier of a Ctrl group: %s\n", describe(r).c_str());
        if (mode == sim::PathfindingMode::Retail) {
            check(r.formations >= 1 && !r.otherLegs, "out of its slot it walks back on a formation ahead of SUPPRESS",
                  describe(r));
            check(r.walkHeld == r.walkTicks && r.walkShots >= 1,
                  "its slot keeps the point and fires on the way", describe(r));
            check(r.retired && r.afterShots >= 1 && r.orderAtEnd,
                  "back in its slot SUPPRESS starts again and fires", describe(r));
        } else {
            check(r.formations == 0 && r.orderAtEnd, "Legion: no group check, the order stands", describe(r));
        }
        check(r.hits >= 1 && r.hitDist < 48, "its shots land on the point", describe(r));
        check(groupFire(vfs, reg, turret, archer, 1500).hash == r.hash, "the same run gives the same state hash");
    }
    {
        const GroupRun r = groupFire(vfs, reg, facing, archer, 1500);
        std::printf("  facing outlier of a Ctrl group: %s\n", describe(r).c_str());
        if (mode == sim::PathfindingMode::Retail) {
            check(r.formations >= 1 && r.walkHeld == r.walkTicks && r.walkShots == 0,
                  "one that must face the point holds fire on the walk (it does not turn to it)", describe(r));
            check(r.retired && r.afterShots >= 1 && r.orderAtEnd,
                  "back in its slot it turns, and fires", describe(r));
        } else {
            check(r.formations == 0 && r.orderAtEnd, "Legion: no group check, the order stands", describe(r));
        }
    }
    // Shift on the point while the formation leads: SUPPRESS goes, and the
    // formation it put ahead of itself (not an issued leg) with it. Ctrl
    // ahead of it: the formation goes, SUPPRESS waits behind the new order
    // from stage 0.
    if (mode == sim::PathfindingMode::Retail) {
        bool shift = false, ctrl = false;
        std::string seen;
        groupFire(vfs, reg, turret, archer, 1500, 0.9f, [&](Scene& s, int id, float px, float pz) {
            net::Command c;
            c.kind = net::Cmd::AttackGround; c.player = 0; c.unitId = id; c.x = px; c.z = pz;
            c.queue = net::kQueueAppend | net::kQueueToggle;
            sim::applyCommand(s.w, reg, c);
            shift = s.w.unit(id)->orders.empty();
        });
        groupFire(vfs, reg, turret, archer, 1500, 0.9f, [&](Scene& s, int id, float, float) {
            net::Command c;
            c.kind = net::Cmd::Move; c.player = 0; c.unitId = id; c.x = 300; c.z = 300;
            c.queue = net::kQueueFront;
            sim::applyCommand(s.w, reg, c);
            const auto& o = s.w.unit(id)->orders;
            for (const auto& q : o) seen += q.formationLevel ? 'F' : q.suppress ? 'S' : 'M';
            ctrl = !o.empty() && o.back().suppress && o.back().mission.stage == 0 &&
                   std::none_of(o.begin(), o.end(), [](const sim::Order& q) { return q.formationLevel != 0; });
        });
        check(shift, "Shift on the point takes SUPPRESS and its formation back");
        check(ctrl, "Ctrl puts the new order ahead: the formation goes, SUPPRESS waits from stage 0", seen);
    }
    // The group patrols there and back: out of its slot again once flagged
    // (0x8000000), the outlier leaves the order on rand(3) == 0 (8).
    {
        const GroupRun r = groupFire(vfs, reg, turret, archer, 6000, -1);
        std::printf("  outlier of a Ctrl group on patrol: %s\n", describe(r).c_str());
        if (mode == sim::PathfindingMode::Retail)
            check(r.formations >= 2 && !r.orderAtEnd && r.dropped > 0,
                  "out of its slot again, flagged, it leaves the order", describe(r));
        else
            check(r.formations == 0 && r.orderAtEnd, "Legion: the order stands", describe(r));
    }
}

void wire() {
    std::printf("Cmd::AttackGround on the wire:\n");
    net::Command c; c.kind = net::Cmd::AttackGround; c.unitId = 7; c.x = 100; c.z = 200;
    net::Writer w; net::putBundle(w, 3, {c}, {});
    auto read = [&](uint32_t proto) {
        net::Reader r(w.b.data(), w.b.size()); uint32_t tick = 0; net::Bundle b;
        return net::getBundle(r, tick, b, true, proto) && b.cmds.size() == 1 &&
               b.cmds[0].kind == net::Cmd::AttackGround && b.cmds[0].x == 100 && b.cmds[0].z == 200;
    };
    check(read(net::kAttackGroundProtocol) && !read(net::kAttackGroundProtocol - 1),
          "a strict read takes it from protocol 312 only");
    net::Writer batch; batch.u32(1); batch.cmd(c);
    std::vector<net::Command> parsed;
    check(srv::parsePlayerCommands(batch.b, parsed) && parsed.size() == 1, "the server accepts it from a client");
    check(net::orderVerb(net::Cmd::AttackGround), "a selection order may carry it");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: suppress_test <retail-install-dir> [--legion]\n"); return 2; }
    for (int i = 2; i < argc; ++i)
        if (std::string(argv[i]) == "--legion") mode = sim::PathfindingMode::Legion;
    hpi::Vfs vfs = hpi::mountRetailRoot(argv[1]);
    sim::TypeRegistry reg;
    sim::setupRegistry(reg, vfs, false);
    std::printf("[%s pathfinding]\n", mode == sim::PathfindingMode::Legion ? "Legion" : "Retail");

    const auto ground = [](const sim::UnitType& t) { return !t.canFly && !t.isStructure() && t.canMove; };
    const auto ranged = [](const sim::Weapon& w) { return !w.melee && !w.toAir && w.range > 0 && !w.commandFire; };
    const sim::UnitType* archer = pick(reg, {"araarch"}, [&](const sim::UnitType& t) {
        return ground(t) && !t.weaponSwitching && t.weapons.size() == 1 && ranged(t.weapons[0]) && !t.weapons[0].minRange;
    });
    const sim::UnitType* siege = pick(reg, {"aracat", "aratreb", "veriron"}, [&](const sim::UnitType& t) {
        return ground(t) && !t.weaponSwitching && t.weapons.size() == 1 && ranged(t.weapons[0]) &&
               t.weapons[0].minRange >= 64 && t.weapons[0].range >= t.weapons[0].minRange + 150;
    });
    const sim::UnitType* tower = pick(reg, {"aratower", "aragtwr"}, [&](const sim::UnitType& t) {
        return t.isStructure() && !t.weapons.empty() && !t.weaponSwitching && ranged(t.weapons[0]) &&
               !t.weapons[0].minRange && !t.producesUnits();
    });
    const sim::UnitType* switcher = pick(reg, {}, [&](const sim::UnitType& t) {
        return ground(t) && t.weaponSwitching && t.weapons.size() >= 2 && ranged(t.weapons[0]) && !t.weapons[0].minRange;
    });
    const sim::UnitType* melee = pick(reg, {"arasword"}, [&](const sim::UnitType& t) {
        return ground(t) && !t.weaponSwitching && t.weapons.size() == 1 && t.weapons[0].melee;
    });
    std::printf("  types: archer %s, siege %s, tower %s, switcher %s, melee %s\n",
                archer ? archer->id.c_str() : "-", siege ? siege->id.c_str() : "-", tower ? tower->id.c_str() : "-",
                switcher ? switcher->id.c_str() : "-", melee ? melee->id.c_str() : "-");
    if (!archer || !siege || !tower || !switcher || !melee) {
        check(false, "the shipped data has every kind of shooter");
        return 1;
    }
    // The shipped data has no ground unit with an anti-air weapon: an Archer
    // whose bow is made a toairweapon stands in.
    sim::UnitType antiAirType = *archer;
    antiAirType.weapons[0].toAir = true;
    const sim::UnitType* antiAir = &antiAirType;

    // An Archer in reach: fires where it stands, keeps the order.
    {
        const float range = float(archer->weapons[0].range);
        const Run r = fireAt(vfs, reg, {archer->id.c_str()}, {{300 + range * 0.6f, 768}}, 450);
        std::printf("  %s, point %.0f px off: %s\n", archer->id.c_str(), range * 0.6f, describe(r).c_str());
        check(r.shots >= 3 && r.moved < 4 && !r.approach, "an Archer in reach fires at the point where it stands",
              describe(r));
        check(r.orderAtEnd && r.ordersAtEnd == 1, "a lone point is fired at for ever (the order stays)", describe(r));
        check(!r.targetedShot && r.hits >= 2 && r.hitDist < 48, "its shots carry no target and land on the point",
              describe(r));
        const Run again = fireAt(vfs, reg, {archer->id.c_str()}, {{300 + range * 0.6f, 768}}, 450);
        check(again.hash == r.hash && r.hash, "the same run gives the same state hash");
    }

    // Out of reach: walks in on the shrinking rings, then fires.
    {
        const float range = float(archer->weapons[0].range);
        const Run r = fireAt(vfs, reg, {archer->id.c_str()}, {{300 + range + 300, 768}}, 900);
        std::printf("  %s, point %.0f px off: %s\n", archer->id.c_str(), range + 300, describe(r).c_str());
        check(r.approach && r.moved > 250 && r.endDist <= range + 1, "out of reach it walks into reach", describe(r));
        check(r.shots >= 2 && r.orderAtEnd, "and fires from there", describe(r));
    }

    // Too close for a minimum-range weapon: backs off onto the ring and fires.
    {
        const auto& w = siege->weapons[0];
        const Run r = fireAt(vfs, reg, {siege->id.c_str()}, {{300 + float(w.minRange) * 0.4f, 768}}, 1500);
        std::printf("  %s (minrange %d, range %d), point %.0f px off: %s\n", siege->id.c_str(), w.minRange, w.range,
                    float(w.minRange) * 0.4f, describe(r).c_str());
        check(r.approach && r.endDist >= float(w.minRange) && r.endDist <= float(w.range) + 1,
              "inside its minimum range it backs off onto the ring", describe(r));
        check(r.shots >= 1, "and fires from there", describe(r));
    }

    // A tower: in reach it fires; out of reach it keeps the order and waits.
    {
        const float range = float(tower->weapons[0].range);
        const Run in = fireAt(vfs, reg, {tower->id.c_str()}, {{300 + range * 0.5f, 768}}, 450);
        std::printf("  %s, point %.0f px off: %s\n", tower->id.c_str(), range * 0.5f, describe(in).c_str());
        check(in.shots >= 2 && in.orderAtEnd, "a tower fires at a point in reach", describe(in));
        const Run out = fireAt(vfs, reg, {tower->id.c_str()}, {{300 + range + 200, 768}}, 450);
        std::printf("  %s, point %.0f px off: %s\n", tower->id.c_str(), range + 200, describe(out).c_str());
        check(out.shots == 0 && out.moved < 1 && out.orderAtEnd && !out.approach && out.slotPointMax,
              "out of reach it keeps the order and looks again, never firing", describe(out));
        // 9 with no order behind it: stage 0 again after rand(30) + 15 ticks.
        check(out.aims >= 450 / 46 && out.aims <= 450 / 15 + 1, "it looks again every 15 to 44 ticks",
              std::to_string(out.aims) + " looks in 450 ticks");
    }

    // Two Shift-queued points take turns, one shot each.
    {
        const float range = float(archer->weapons[0].range);
        const Run r = fireAt(vfs, reg, {archer->id.c_str()},
                             {{300 + range * 0.6f, 768 - 120}, {300 + range * 0.6f, 768 + 120}}, 600);
        int switches = 0;
        for (size_t i = 1; i < r.sequence.size(); ++i) switches += r.sequence[i] != r.sequence[i - 1];
        std::string seq;
        for (int k : r.sequence) seq += char('A' + k);
        std::printf("  %s, two queued points: %s, impacts %s\n", archer->id.c_str(), describe(r).c_str(), seq.c_str());
        check(r.sequence.size() >= 4 && switches + 1 == int(r.sequence.size()) && r.ordersAtEnd == 2,
              "two queued points take turns, one shot each", seq);
    }

    // A switcher aims its selected slot only.
    {
        const float range = float(switcher->weapons[0].range);
        const Run r = fireAt(vfs, reg, {switcher->id.c_str()}, {{300 + range * 0.5f, 768}}, 300);
        std::printf("  %s (switcher), point %.0f px off: %s, slots %u\n", switcher->id.c_str(), range * 0.5f,
                    describe(r).c_str(), unsigned(r.slotPointMax));
        check(r.slotPointMax == 1 && r.shots >= 1, "a switcher aims and fires its selected slot only", describe(r));
    }

    // Melee: nothing on bare ground; a tree is walked up to and felled.
    {
        check(refused(vfs, reg, melee->id.c_str(), 500, 768), "a melee weapon gets no order on bare ground");
        // ZonTree201 is blocking and clearable: its cell is (40, 48).
        const float tx = 40 * 16 + 8, tz = 48 * 16 + 8;
        const Run r = fireAt(vfs, reg, {melee->id.c_str()}, {{tx, tz}}, 1800, {}, "ZonTree201", 40, 48);
        std::printf("  %s at a tree: %s, tree %d -> %d\n", melee->id.c_str(), describe(r).c_str(), r.treeAtStart,
                    r.treeAtEnd);
        check(r.treeAtStart, "the test tree is on the map");
        check(r.moved > 100 && r.hits >= 1 && !r.treeAtEnd && !r.orderAtEnd,
              "over a tree it walks up, fells it, and the order ends", describe(r));
    }

    // An anti-air weapon gets no order, flyer or not (4de673).
    {
        Scene s;
        build(s, vfs, reg);
        const int u = s.w.spawn(antiAir, 300, 768, 1.5707963f, 0);
        s.w.tick(kTick);
        attackGround(s.w, reg, u, 500, 768);
        check(s.w.unit(u)->orders.empty(), "an anti-air weapon gets no order");
    }

    flyers(vfs, reg);

    groups(vfs, reg, *archer);

    wire();

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
