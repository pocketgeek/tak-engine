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

struct UnitType {
    std::string id;        // lowercase objectname, e.g. "araarch"
    std::string name;      // display name, e.g. "Archer"
    std::string side;      // ARA/TAR/VER/ZON/CRE
    float maxVel = 30;     // px/s
    float accel = 15;      // px/s^2
    float brake = 15;      // px/s^2
    float turnRate = 6;    // rad/s
    bool canMove = false;
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
};

struct Unit {
    int id = 0;
    const UnitType* type = nullptr;
    float x = 0, z = 0;
    float heading = 0;     // radians, 0 = +z
    float speed = 0;       // px/s
    std::deque<Order> orders;

    bool moving() const { return speed > 1.0f || !orders.empty(); }
};

class World {
public:
    int spawn(const UnitType* type, float x, float z, float heading = 0);
    // Replace (or queue) a move order.
    void order(int unitId, float x, float z, bool queue);
    void tick(float dt);

    std::vector<Unit>& units() { return units_; }
    const std::vector<Unit>& units() const { return units_; }
    Unit* unit(int id);

private:
    std::vector<Unit> units_;
    int nextId_ = 1;
};

} // namespace tak::sim
