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
    float sight = 180;        // px (FBI sightdistance)
    bool canFly = false;
    float cruiseAlt = 0;      // world units above ground when flying
    enum class Domain { Ground, Water, Hover };
    Domain domain = Domain::Ground;   // from FBI movementclass prefix
    bool canTransport = false;
    int transportCap = 0;     // units carried (FBI transportcapacity)
    std::string soundClass;   // FBI soundcategory, keys gamedata/soundclasses
    std::string corpse;       // FBI corpse feature name
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
    int targetId = 0;      // nonzero = attack (or board, if load) target
    bool load = false;     // board the friendly transport `targetId`
    bool unload = false;   // sail to (x,z) and disembark cargo
    bool attackMove = false;   // engage enemies encountered en route
    bool patrol = false;       // loop: completed orders re-queue at the back
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
    float repathLeft = 0;   // chase steering repath countdown
    float deadFor = -1;    // >= 0 once dead; counts up for death animation
    bool justFired = false;   // set for one tick when the weapon fires
    bool underConstruction = false;
    int buildSiteId = 0;   // builder: id of the building it is constructing
    int inTransport = 0;   // id of carrying transport, 0 = none
    std::vector<int> cargo;
    std::deque<Order> orders;
    // Production (buildings with a build tree).
    std::deque<const UnitType*> buildQueue;
    float buildProgress = 0;   // seconds of work done on queue front
    int justBuilt = 0;         // unit id produced this tick (viewer hook), else 0

    bool alive() const { return deadFor < 0; }
    bool embarked() const { return inTransport != 0; }
    bool moving() const { return alive() && (speed > 1.0f || !orders.empty()); }
};

struct Projectile {
    float x = 0, z = 0;
    float vx = 0, vz = 0;
    float damage = 0;
    int targetId = 0;
    int fromTeam = 0;
    float life = 0;        // seconds left before it fizzles
    float age = 0;         // seconds since launch
    float flight = 1;      // expected seconds to target (for the render arc)
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
    // Build per-domain nav grids from heights + sea level.
    void setTerrain(const std::vector<uint8_t>& heights, int w, int h, int seaLevel);
    void setNav(NavGrid grid) { nav_ = std::move(grid); }
    NavGrid& nav() { return nav_; }
    const NavGrid& navFor(const UnitType* t) const {
        if (t && t->domain == UnitType::Domain::Water) return navWater_;
        if (t && t->domain == UnitType::Domain::Hover) return navHover_;
        return nav_;
    }
    // Queue production of `typeId` at a builder building.
    void train(int builderId, const UnitType* type);
    // Mobile builder constructs a building at (x, z). Returns the new
    // building's id, or 0 if the site is invalid.
    int startBuild(int builderId, const UnitType* type, float x, float z);
    bool canPlace(const UnitType* type, float x, float z) const;
    Team& team(int i) { return teams_[size_t(i)]; }

    // Team-0 fog of war over 16px cells: 0 hidden, 1 explored, 2 visible.
    const std::vector<uint8_t>& visibility() const { return vis_; }
    int visW() const { return visW_; }
    int visH() const { return visH_; }
    bool cellVisible(float x, float z) const {
        if (vis_.empty()) return true;
        int cx = int(x) / 16, cz = int(z) / 16;
        if (cx < 0 || cz < 0 || cx >= visW_ || cz >= visH_) return false;
        return vis_[size_t(cz) * visW_ + cx] == 2;
    }
    // Move order; queue appends.
    void order(int unitId, float x, float z, bool queue);
    void attackMove(int unitId, float x, float z, bool queue);
    void patrol(int unitId, float x, float z);
    void stop(int unitId);
    // Attack order on an enemy unit.
    void attack(int unitId, int targetId, bool queue);
    // Board a friendly transport / sail to (x,z) and disembark.
    void loadInto(int unitId, int transportId);
    void unloadAt(int transportId, float x, float z);
    void tick(float dt);

    std::vector<Unit>& units() { return units_; }
    const std::vector<Unit>& units() const { return units_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    Unit* unit(int id);

private:
    void tickCombat(Unit& u, float dt);
    void fire(Unit& u, Unit& target);

    void tickProduction(Unit& u, float dt);
    void tickTransport(Unit& u, float dt);
    void tickConstruction(Unit& u, float dt);
    void updateVisibility();

    std::vector<uint8_t> vis_;
    int visW_ = 0, visH_ = 0;
    float visTimer_ = 0;
    std::vector<Unit> units_;
    std::vector<Projectile> projectiles_;
    std::vector<Team> teams_ = std::vector<Team>(4);
    NavGrid nav_, navWater_, navHover_;
    int nextId_ = 1;
};

} // namespace tak::sim
