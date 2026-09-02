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
    bool isBuilder = false;
    float buildCost = 0;    // mana
    float buildTime = 0;    // work units; seconds = buildTime / builder workerTime
    float workerTime = 1;
    float income = 0;       // mana/sec (mogriumincome)
    float storage = 0;      // mana cap contribution (mogriumstorage)
    int footX = 1, footZ = 1;
    std::string soundClass;   // FBI soundcategory, keys gamedata/soundclasses
    Weapon weapon;         // WEAPON1; weapon.damage == 0 means unarmed
};

class TypeRegistry {
public:
    // Parse every .fbi in a directory (extracted data/units).
    void loadDir(const std::filesystem::path& unitsDir);
    // Parse canbuild/<builder>/<buildable>.tdf into the build tree.
    void loadBuildTree(const std::filesystem::path& canbuildDir);
    const UnitType* find(const std::string& id) const;
    const std::map<std::string, UnitType>& all() const { return types_; }
    const std::vector<std::string>& buildable(const std::string& builderId) const;

private:
    std::map<std::string, UnitType> types_;
    std::map<std::string, std::vector<std::string>> buildTree_;
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
    // Production (buildings with a build tree).
    std::deque<const UnitType*> buildQueue;
    float buildProgress = 0;   // seconds of work done on queue front
    int justBuilt = 0;         // unit id produced this tick (viewer hook), else 0

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

// Walkability grid derived from TNT heights: a cell is blocked when the
// local height spread exceeds a cliff threshold.
class NavGrid {
public:
    NavGrid() = default;
    NavGrid(const std::vector<uint8_t>& heights, int w, int h, int cliff = 20);

    bool walkable(int cx, int cz) const {
        return cx >= 0 && cz >= 0 && cx < w_ && cz < h_ && cells_[size_t(cz) * w_ + cx];
    }
    bool empty() const { return cells_.empty(); }
    int width() const { return w_; }
    int height() const { return h_; }
    // Mark a rectangle of cells blocked (building footprint) or clear.
    void block(int cx, int cz, int w, int h, bool blocked);

    // A* in cell space (16px cells), with waypoint simplification.
    // Returns world-space waypoints; empty if unreachable.
    std::vector<Order> findPath(float x0, float z0, float x1, float z1) const;

private:
    bool lineClear(int x0, int z0, int x1, int z1) const;

    std::vector<uint8_t> cells_;
    int w_ = 0, h_ = 0;
};

struct Team {
    float mana = 500;
    float storage = 0;   // recomputed each tick from alive units
    float income = 0;
};

class World {
public:
    int spawn(const UnitType* type, float x, float z, float heading = 0, int team = 0);
    void setNav(NavGrid grid) { nav_ = std::move(grid); }
    NavGrid& nav() { return nav_; }
    // Queue production of `typeId` at a builder building.
    void train(int builderId, const UnitType* type);
    Team& team(int i) { return teams_[size_t(i)]; }
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

    void tickProduction(Unit& u, float dt);

    std::vector<Unit> units_;
    std::vector<Projectile> projectiles_;
    std::vector<Team> teams_ = std::vector<Team>(4);
    NavGrid nav_;
    int nextId_ = 1;
};

} // namespace tak::sim
