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

int World::spawn(const UnitType* type, float x, float z, float heading) {
    Unit u;
    u.id = nextId_++;
    u.type = type;
    u.x = x;
    u.z = z;
    u.heading = heading;
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
    if (!u || !u->type || !u->type->canMove) return;
    if (!queue) u->orders.clear();
    u->orders.push_back({x, z});
}

void World::tick(float dt) {
    for (auto& u : units_) {
        if (!u.type) continue;
        if (u.orders.empty()) {
            u.speed = std::max(0.0f, u.speed - u.type->brake * dt);
        } else {
            const Order& o = u.orders.front();
            float dx = o.x - u.x, dz = o.z - u.z;
            float dist = std::sqrt(dx * dx + dz * dz);
            if (dist < 3.0f) {
                u.orders.pop_front();
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
