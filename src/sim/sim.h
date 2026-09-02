#pragma once

#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace tak::sim {

// Unit stats loaded from .fbi files. Velocities are in map pixels per
// second (FBI values are per original 30Hz tick), headings in radians
// with 0 facing +z and the model's forward axis.

struct Weapon {
    std::string name;
    float range = 0;         // px
    float reload = 1;        // seconds
    float damage = 0;
    float projVel = 0;       // px/s; 0 = instant (melee)
    bool melee = false;
};

struct UnitType {
    std::string id;        // lowercase objectname, e.g. "araarch"
    std::string name;      // display name, e.g. "Archer"
    std::string side;      // ARA/TAR/VER/ZON/CRE
    float maxVel = 30;     // px/s
    float accel = 15;      // px/s^2
    float brake = 15;      // px/s^2
    float turnRate = 6;    // rad/s
    float maxHp = 100;
    bool canMove = false;
    Weapon weapon;         // WEAPON1; weapon.damage == 0 means unarmed
};

class TypeRegistry {
public:
    // Parse every .fbi in a directory (extracted data/units).
    void loadDir(const std::filesystem::path& unitsDir);
    const UnitType* find(const std::string& id) const;
    const std::map<std::string, UnitType>& all() const { return types_; }

private:
    std::map<std::string, UnitType> types_;
};

struct Order {
    float x = 0, z = 0;
    int targetId = 0;      // nonzero = attack order on that unit
};

struct Unit {
    int id = 0;
    int team = 0;
    const UnitType* type = nullptr;
    float x = 0, z = 0;
    float heading = 0;     // radians, 0 = +z
    float speed = 0;       // px/s
    float hp = 100;
    float reloadLeft = 0;
    float deadFor = -1;    // >= 0 once dead; counts up for death animation
    bool justFired = false;   // set for one tick when the weapon fires
    std::deque<Order> orders;

    bool alive() const { return deadFor < 0; }
    bool moving() const { return alive() && (speed > 1.0f || !orders.empty()); }
};

struct Projectile {
    float x = 0, z = 0;
    float vx = 0, vz = 0;
    float damage = 0;
    int targetId = 0;
    int fromTeam = 0;
    float life = 0;        // seconds left before it fizzles
};

class World {
public:
    int spawn(const UnitType* type, float x, float z, float heading = 0, int team = 0);
    // Move order; queue appends.
    void order(int unitId, float x, float z, bool queue);
    // Attack order on an enemy unit.
    void attack(int unitId, int targetId, bool queue);
    void tick(float dt);

    std::vector<Unit>& units() { return units_; }
    const std::vector<Unit>& units() const { return units_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    Unit* unit(int id);

private:
    void tickCombat(Unit& u, float dt);
    void fire(Unit& u, Unit& target);

    std::vector<Unit> units_;
    std::vector<Projectile> projectiles_;
    int nextId_ = 1;
};

} // namespace tak::sim
