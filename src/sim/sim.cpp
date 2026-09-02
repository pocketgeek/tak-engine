#include "sim/sim.h"

#include "tdf/tdf.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace tak::sim {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTick = 30.0f;               // FBI per-tick values -> per-second
constexpr float kCobAngle = 2 * kPi / 65536.0f;

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

float angleDiff(float a, float b) {
    float d = std::fmod(a - b + kPi, 2 * kPi);
    if (d < 0) d += 2 * kPi;
    return d - kPi;
}

} // namespace

void TypeRegistry::loadDir(const std::filesystem::path& unitsDir) {
    for (const auto& e : std::filesystem::directory_iterator(unitsDir)) {
        if (lower(e.path().extension().string()) != ".fbi") continue;
        try {
            auto root = tdf::parse(e.path());
            const auto* info = root.child("UNITINFO");
            if (!info) continue;
            UnitType t;
            t.id = lower(info->valueOr("objectname", e.path().stem().string()));
            t.name = info->valueOr("name", t.id);
            t.side = info->valueOr("side", "");
            t.canMove = info->numberOr("canmove", 0) != 0;
            t.maxVel = float(info->numberOr("maxvelocity", 1)) * kTick;
            t.accel = float(info->numberOr("acceleration", 0.5)) * kTick;
            t.brake = float(info->numberOr("brakerate", 0.5)) * kTick;
            // FBI turnrate is COB angle units per tick.
            float tr = float(info->numberOr("turnrate", 500));
            t.turnRate = tr * kCobAngle * kTick;
            t.maxHp = float(info->numberOr("maxdamage", 100));
            if (const auto* w = root.child("WEAPON1")) {
                t.weapon.name = w->valueOr("name", "");
                t.weapon.range = float(w->numberOr("range", 0));
                t.weapon.reload = float(w->numberOr("reloadtime", 1));
                t.weapon.projVel = float(w->numberOr("weaponvelocity", 0));
                t.weapon.melee = lower(w->valueOr("type", "")) == "melee";
                if (const auto* dmg = w->child("DAMAGE"))
                    t.weapon.damage = float(dmg->numberOr("default", 0));
            }
            types_[t.id] = std::move(t);
        } catch (const std::exception&) {
            // Skip malformed definitions rather than fail the registry.
        }
    }
}

const UnitType* TypeRegistry::find(const std::string& id) const {
    auto it = types_.find(lower(id));
    return it == types_.end() ? nullptr : &it->second;
}

int World::spawn(const UnitType* type, float x, float z, float heading, int team) {
    Unit u;
    u.id = nextId_++;
    u.team = team;
    u.type = type;
    u.x = x;
    u.z = z;
    u.heading = heading;
    u.hp = type ? type->maxHp : 100;
    units_.push_back(u);
    return u.id;
}

Unit* World::unit(int id) {
    for (auto& u : units_)
        if (u.id == id) return &u;
    return nullptr;
}

void World::order(int unitId, float x, float z, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->canMove) return;
    if (!queue) u->orders.clear();
    u->orders.push_back({x, z, 0});
}

void World::attack(int unitId, int targetId, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || u->type->weapon.damage <= 0) return;
    if (!queue) u->orders.clear();
    u->orders.push_back({0, 0, targetId});
}

void World::fire(Unit& u, Unit& target) {
    const Weapon& w = u.type->weapon;
    u.reloadLeft = w.reload;
    u.justFired = true;
    if (w.melee || w.projVel <= 0) {
        target.hp -= w.damage;
        return;
    }
    Projectile p;
    p.x = u.x;
    p.z = u.z;
    float dx = target.x - u.x, dz = target.z - u.z;
    float dist = std::max(std::sqrt(dx * dx + dz * dz), 1e-3f);
    float vel = w.projVel * kTick / 30.0f;   // weaponvelocity is already px/s-ish
    p.vx = dx / dist * vel;
    p.vz = dz / dist * vel;
    p.damage = w.damage;
    p.targetId = target.id;
    p.fromTeam = u.team;
    p.life = dist / vel + 0.5f;
    projectiles_.push_back(p);
}

void World::tickCombat(Unit& u, float dt) {
    if (u.reloadLeft > 0) u.reloadLeft -= dt;
    if (u.orders.empty() || u.orders.front().targetId == 0) return;

    Unit* target = unit(u.orders.front().targetId);
    if (!target || !target->alive()) {
        u.orders.pop_front();
        return;
    }
    float dx = target->x - u.x, dz = target->z - u.z;
    float dist = std::sqrt(dx * dx + dz * dz);
    const Weapon& w = u.type->weapon;
    if (dist > w.range * 0.95f) {
        // Advance toward the target: rewrite the head order's move point.
        u.orders.front().x = target->x;
        u.orders.front().z = target->z;
        return;   // movement handled by the normal move logic
    }
    // In range: stop and face the target.
    u.speed = std::max(0.0f, u.speed - u.type->brake * dt);
    float want = std::atan2(dx, dz);
    float diff = angleDiff(want, u.heading);
    float maxTurn = u.type->turnRate * dt;
    u.heading += std::clamp(diff, -maxTurn, maxTurn);
    if (std::abs(diff) < 0.2f && u.reloadLeft <= 0) fire(u, *target);
}

void World::tick(float dt) {
    for (auto& u : units_) u.justFired = false;

    // Projectiles.
    for (auto& p : projectiles_) {
        p.x += p.vx * dt;
        p.z += p.vz * dt;
        p.life -= dt;
        Unit* t = unit(p.targetId);
        if (t && t->alive()) {
            float dx = t->x - p.x, dz = t->z - p.z;
            if (dx * dx + dz * dz < 8 * 8) {
                t->hp -= p.damage;
                p.life = -1;
            }
        }
    }
    std::erase_if(projectiles_, [](const Projectile& p) { return p.life <= 0; });

    for (auto& u : units_) {
        if (!u.type) continue;
        if (!u.alive()) { u.deadFor += dt; continue; }
        if (u.hp <= 0) { u.deadFor = 0; u.orders.clear(); u.speed = 0; continue; }

        tickCombat(u, dt);

        bool combatHold =
            !u.orders.empty() && u.orders.front().targetId != 0 && [&] {
                Unit* t = unit(u.orders.front().targetId);
                if (!t) return false;
                float dx = t->x - u.x, dz = t->z - u.z;
                return std::sqrt(dx * dx + dz * dz) <= u.type->weapon.range * 0.95f;
            }();
        if (combatHold) continue;

        if (u.orders.empty()) {
            u.speed = std::max(0.0f, u.speed - u.type->brake * dt);
        } else {
            const Order& o = u.orders.front();
            float dx = o.x - u.x, dz = o.z - u.z;
            float dist = std::sqrt(dx * dx + dz * dz);
            if (dist < 3.0f) {
                if (o.targetId == 0) u.orders.pop_front();
                continue;
            }
            float want = std::atan2(dx, dz);
            float diff = angleDiff(want, u.heading);
            float maxTurn = u.type->turnRate * dt;
            u.heading += std::clamp(diff, -maxTurn, maxTurn);

            // Brake into the waypoint if it's the last one; slow for big turns.
            float target = u.type->maxVel;
            if (std::abs(diff) > 0.8f) target *= 0.3f;
            bool last = u.orders.size() == 1;
            if (last) {
                float stopDist = u.speed * u.speed / (2 * u.type->brake);
                if (dist < stopDist) target = 0;
            }
            if (u.speed < target)
                u.speed = std::min(u.speed + u.type->accel * dt, target);
            else
                u.speed = std::max(u.speed - u.type->brake * dt, target);

            u.x += std::sin(u.heading) * u.speed * dt;
            u.z += std::cos(u.heading) * u.speed * dt;
        }
    }
}

} // namespace tak::sim
