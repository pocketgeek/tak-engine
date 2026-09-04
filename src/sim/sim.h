#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace tak::sim {

// Cheat: when true, construction and production complete instantly and cost no
// mana (set via the takview `--cheat` flag).
extern bool gInstantBuild;

// Unit stats loaded from .fbi files. Velocities are in map pixels per
// second (FBI values are per original 30Hz tick), headings in radians
// with 0 facing +z and the model's forward axis.

// Visual family for a weapon's projectile (drives how the viewer draws it).
enum class WeaponFx { Arrow, Lightning, Fire };

struct UnitType;   // for Weapon::damageVs

struct Weapon {
    std::string name;
    float range = 0;         // px
    float reload = 1;        // seconds
    float damage = 0;        // DAMAGE.default (base, used when no category matches)
    float projVel = 0;       // px/s; 0 = instant (melee)
    bool melee = false;
    float aoe = 0;           // areaofeffect radius (px); >0 = splash
    float edge = 1;          // edgeeffectiveness: damage fraction at the aoe edge
    float aimTol = 0.1f;     // aimtolerance in radians: how close to on-target to fire
    bool ballistic = false;  // FBI weapon type = Ballistic (lobbed arc, not flat)
    float minRange = 0;      // minrange: can't hit targets closer than this
    bool noAir = false;      // noairweapon: cannot target flying units
    float manaCost = 0;      // manapershot: mana drained from the firer per shot
    // Status effect this weapon inflicts (freeze/petrify/paralyze), 0 = none.
    enum class Status { None, Frozen, Stoned, Paralyzed } status = Status::None;
    float statusDur = 0;     // seconds the inflicted status lasts
    std::string soundHit;    // soundhitclass: impact-sound class (arrow/sword/cannon..)
    std::string explosionClass;       // explosionclass: impact effect (gamedata/explosions)
    std::string waterExplosionClass;  // waterexplosionclass: impact effect over water
    // Area-effect shockwave rings emitted at this weapon's impact.
    std::string radiusArt[3];   // radiusart0..2: expanding-ring effect anims
    int   ringCount = 0;        // ringcount: how many rings
    float ringDelay = 0.2f;     // ringdelay: seconds between successive rings
    float ringDur = 1.0f;       // ringduration: seconds each ring lasts
    int   spriteCount = 24;     // spritecount: sprites arranged around each ring
    float shakeMag = 0;         // shakemagnitude: camera-shake intensity on impact
    float shakeDur = 0;         // shakeduration: seconds the shake lasts
    bool  fireStarter = false;  // firestarter: leaves ground fire at the impact
    // Per-target-category damage overrides (DAMAGE keys other than `default`),
    // keyed by lowercased category token (e.g. "monarch", "dragon", "fort").
    std::map<std::string, float> dmgVs;
    WeaponFx fx = WeaponFx::Arrow;
    // Damage this weapon deals to a unit of type `t` (category override, else base).
    float damageVs(const UnitType* t) const;
};

// A stat aura from an [AdjustArmor]/[AdjustAttack]/[AdjustJoy] weapon block: the
// unit continuously scales nearby units' armour or attack (multiplier) — friends
// up (affectsenemy=0) or enemies down (affectsenemy=1) — falling to `edge` at the rim.
struct Aura {
    enum class Kind { Armor, Attack, Joy } kind = Kind::Armor;
    float amount = 1;        // multiplier (Armor/Attack); additive morale (Joy)
    bool  affectsEnemy = false;
    float radius = 0;
    float edge = 1;          // edgeeffectiveness at the radius edge
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
    float buildDist = 0;    // FBI builddistance: how far a builder reaches to build
    bool onMana = false;    // must be built on a mana deposit (yardmap 'S'), e.g. lodestones
    float buildCost = 0;    // mana
    float buildTime = 0;    // work units; seconds = buildTime / builder workerTime
    float workerTime = 1;
    float income = 0;       // mana/sec (mogriumincome)
    float storage = 0;      // mana cap contribution (mogriumstorage)
    int footX = 1, footZ = 1;
    std::string yardMap;      // footX*footZ chars; 'o' blocks, '.'/'c' passable
    // Lowercased target-category tokens (from FBI category/damagecategory/tedclass),
    // matched against a weapon's per-category damage overrides.
    std::vector<std::string> categories;
    float sight = 180;        // px (FBI sightdistance)
    bool canFly = false;
    float cruiseAlt = 0;      // world units above ground when flying
    enum class Domain { Ground, Water, Hover };
    Domain domain = Domain::Ground;   // from FBI movementclass prefix
    bool canTransport = false;
    int transportCap = 0;     // units carried (FBI transportcapacity)
    std::string soundClass;   // FBI soundcategory, keys gamedata/soundclasses
    std::string bodyType = "default";   // FBI bodytype (flesh/armor/wood/..) = hit-sound material
    std::string corpse;       // FBI corpse feature name
    std::string shadowArt;    // FBI shadowart: shadow sprite name in shadows.gaf
    std::string veteranModel; // veteranmodel: 3DO the unit swaps to at max veterancy
    // --- extended FBI stats -------------------------------------------------
    float healTime = 0;       // healtime: seconds per HP regenerated (0 = no regen)
    float leash = 0;          // maneuverleashlength: max auto-chase distance (0 = unlimited)
    float waterMult = 1;      // watermultiplier: speed factor in shallow water
    float roadMult = 1;       // roadmultiplier (parsed; no road-tile data to apply it)
    float maxWaterDepth = 0;  // deepest water a ground unit may wade into
    float maxSlope = 255;     // steepest cell height-spread the unit may cross
    float minWaterDepth = 0;  // shallowest water a water unit needs (from MOVEINFO)
    float radar = 0;          // radardistance: fog-reveal radius (separate from sight)
    int   xpValue = 0;        // experiencepoints: XP granted to whoever kills this unit,
                              // and the per-veteran-level XP divisor for THIS unit
    bool  noVeteran = false;  // noveteran: this unit can never gain veterancy
    float maxMana = 0;        // per-unit mana pool (casters); 0 = uses no personal mana
    float manaRegen = 0;      // manarechargerate: personal mana regained per second
    bool  canReclaim = false; // canreclaim: builder can reclaim corpses/features for mana
    bool  canResurrect = false;   // canresurrect: can revive nearby corpses
    bool  canCapture = false;     // cancapture: can convert an enemy unit to its player
    bool  canCloak = false;       // cancloak
    float cloakCost = 0;          // cloakcost: mana/sec while cloaked and idle
    float cloakCostMove = 0;      // cloakcostmoving: mana/sec while cloaked and moving
    float minCloakDist = 0;       // mincloakdistance: an enemy this close forces uncloak
    bool  hoverAttack = false;    // hoverattack: flyer stops to attack instead of strafing
    bool  attractsGods = false;   // attractsgods: priest channels god favour
    bool  onOffable = false;      // onoffable: can be toggled active/inactive
    bool  activateWhenBuilt = true;   // activatewhenbuilt (default on)
    bool  cantBeStoned = false, cantBeFrozen = false;
    bool  cantBeCaptured = false, cantBeTransported = false;
    int   transportSize = 1;      // transportsize: transport slots this unit occupies
    uint8_t blood[3] = {150, 30, 10};   // bloodcolor1 (r,g,b) for hit/death spray
    std::vector<Aura> auras;   // stat auras projected onto nearby units
    Weapon weapon;            // primary (WEAPON1); damage 0 = unarmed
    std::vector<Weapon> weapons;   // all slots (WEAPON1..3)
    float maxRange() const {
        float r = 0;
        for (const auto& w : weapons) r = std::max(r, w.range);
        return r;
    }
};

// A pathfinding movement class from gamedata/MOVEINFO.tdf: per-class terrain
// limits that a unit inherits via its FBI `movementclass`.
struct MoveClass {
    float maxSlope = 255;
    float maxWaterDepth = 255;
    float minWaterDepth = 0;
};

class TypeRegistry {
public:
    // Parse gamedata/MOVEINFO.tdf (movement classes). Call BEFORE loadDir so
    // units can inherit their class's slope / water limits.
    void loadMoveInfo(const std::filesystem::path& tdf);
    // Parse every .fbi in a directory (extracted data/units).
    void loadDir(const std::filesystem::path& unitsDir);
    // Parse canbuild/<builder>/<buildable>.tdf into the build tree.
    void loadBuildTree(const std::filesystem::path& canbuildDir);
    const UnitType* find(const std::string& id) const;
    const std::map<std::string, UnitType>& all() const { return types_; }
    const std::vector<std::string>& buildable(const std::string& builderId) const;
    // Largest build menu of any builder (drives the minimum window width so the
    // whole icon row always fits at full size -- some Crusades menus reach 13).
    std::size_t maxBuildMenu() const {
        std::size_t m = 0;
        for (const auto& [id, list] : buildTree_) m = std::max(m, list.size());
        return m;
    }

private:
    std::map<std::string, UnitType> types_;
    std::set<std::string> canonicalTypes_;   // ids whose defining .fbi filename == objectname
    std::map<std::string, std::vector<std::string>> buildTree_;
    std::map<std::string, MoveClass> moveClasses_;   // lowercased name -> limits
};

struct Order {
    float x = 0, z = 0;
    int targetId = 0;      // nonzero = attack (or board, if load) target
    bool load = false;     // board the friendly transport `targetId`
    bool unload = false;   // sail to (x,z) and disembark cargo
    bool attackMove = false;   // engage enemies encountered en route
    bool patrol = false;       // loop: completed orders re-queue at the back
    bool guard = false;        // follow friendly `targetId`, engage threats
    bool flow = false;         // steer by the shared flow field toward (x,z)
};

// A queued construction: build `type` at (x, z) when the builder gets to it.
struct BuildOrder {
    const UnitType* type = nullptr;
    float x = 0, z = 0;
};

struct Unit {
    int id = 0;
    int player = 0;
    const UnitType* type = nullptr;
    float x = 0, z = 0;
    float heading = 0;     // radians, 0 = +z
    float speed = 0;       // px/s
    float hp = 100;
    float reloadLeft = 0;          // primary slot (kept for anim hooks)
    float reloads[3] = {0, 0, 0};  // per weapon slot
    int   weaponSlot = 0;          // active weapon (0=primary); player-selectable
    float repathLeft = 0;   // chase steering repath countdown
    float stuckFor = 0;     // seconds wanting to move but making no progress
    float stuckX = 0, stuckZ = 0;   // position when the stuck timer last reset
    float deadFor = -1;    // >= 0 once dead; counts up for death animation
    // --- extended runtime state --------------------------------------------
    float mana = 0;        // personal mana pool (casters), capped at type->maxMana
    int   xp = 0;          // accumulated experience from kills
    int   veteran = 0;     // veteran level (0..10); scales attack/armor/reload
    float atkBuff = 1;     // live attack multiplier from auras (decays to 1)
    float armBuff = 1;     // live armour multiplier from auras (decays to 1)
    float frozenFor = 0;   // >0 = frozen solid (can't act); counts down
    float stonedFor = 0;   // >0 = petrified (can't act, immune to damage while stone)
    float paralyzedFor = 0;// >0 = paralyzed (can't act, still takes damage)
    bool  cloaked = false; // currently invisible to enemies
    bool  active = true;   // onoffable units: false = powered down
    int   lastHitBy = 0;   // id of the unit that last damaged this one (for kill XP)
    float captureProg = 0; // canCapture units: seconds spent charming the current target
    float homeX = 0, homeZ = 0;   // leash anchor (idle position) for auto-chase
    bool  justFired = false;   // set for one tick when the weapon fires
    bool underConstruction = false;
    bool buildBegun = false;   // construction site: true once the builder arrived
    int buildSiteId = 0;   // builder: id of the building it is constructing
    std::deque<BuildOrder> buildOrders;   // builder: queued (shift) builds
    int inTransport = 0;   // id of carrying transport, 0 = none
    std::vector<int> cargo;
    std::deque<Order> orders;
    // Production (buildings with a build tree).
    std::deque<const UnitType*> buildQueue;
    float buildProgress = 0;   // seconds of work done on queue front
    int justBuilt = 0;         // unit id produced this tick (viewer hook), else 0
    const UnitType* repeatType = nullptr;   // infinite production: re-queue when idle

    bool alive() const { return deadFor < 0; }
    bool embarked() const { return inTransport != 0; }
    // Frozen/petrified/paralyzed units can't move, turn, or fire.
    bool incapacitated() const { return frozenFor > 0 || stonedFor > 0 || paralyzedFor > 0; }
    // Veterancy stat multiplier (retail: base 1.0, +10% per level, level capped
    // at 10 => up to 2.0x). Scales attack up, armor up (less damage taken), and
    // reload down (faster). Verified against KINGDOMS.icd. See retail-engine-internals.
    float vetMul() const { return 1.0f + 0.10f * float(veteran); }
    bool moving() const { return alive() && (speed > 1.0f || !orders.empty()); }
    // Actually translating (for the walk animation), vs standing with an
    // attack/queued order.
    bool walking() const { return alive() && speed > 3.0f; }
};

struct Projectile {
    float x = 0, z = 0;
    float vx = 0, vz = 0;
    float damage = 0;
    int targetId = 0;
    int fromPlayer = 0;
    float life = 0;        // seconds left before it fizzles
    float age = 0;         // seconds since launch
    float flight = 1;      // expected seconds to target (for the render arc)
    int fromId = 0;        // firing unit id (for kill attribution / veterancy)
    const Weapon* wsrc = nullptr;   // source weapon (splash + per-category damage)
    WeaponFx fx = WeaponFx::Arrow;   // how the viewer draws it
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

    // Line of sight: no blocked cell between the two world points, ignoring cells
    // within `skip0`/`skip1` cells of each endpoint — so a shooter or target's own
    // building footprint doesn't block the shot, but a wall between them does.
    bool losBetween(float wx0, float wz0, float wx1, float wz1,
                    int skip0 = 0, int skip1 = 0) const;

private:
    bool lineClear(int x0, int z0, int x1, int z1) const;

    std::vector<uint8_t> cells_;
    int w_ = 0, h_ = 0;
};

// Block/unblock a building's yardmap-aware footprint on a nav grid.
void blockFootprint(NavGrid& nav, const UnitType& t, float x, float z, bool blocked);

// A flow field: a per-cell direction pointing along the shortest walkable path
// toward a single goal, built by a wavefront (Dijkstra) out from the goal. Many
// units heading to the same goal share one field, so a crowd spreads and flows
// around obstacles instead of single-filing into a corner. Deterministic (built
// purely from the nav grid), so it is safe under lockstep. Cells are 16px, same
// as the NavGrid it is built from.
class FlowField {
public:
    // Build the field over `nav` toward the goal at world (gx, gz). If that cell
    // is blocked, the goal snaps to the nearest walkable cell. Returns false if
    // the grid is empty or no walkable goal cell exists.
    bool build(const NavGrid& nav, float gx, float gz);
    bool ready() const { return w_ > 0; }
    int goalCell() const { return goal_; }
    // Unit direction at world (x, z); (0,0) at/near the goal, off-grid, or in an
    // unreachable pocket (caller should then steer straight at the goal).
    void dirAt(float x, float z, float& dx, float& dz) const;
    // Was the goal cell reachable from (x,z)? (finite integration distance)
    bool reachable(float x, float z) const;

    uint32_t used = 0;   // tick of last use, for the flow-cache LRU eviction

private:
    int w_ = 0, h_ = 0, goal_ = -1;
    std::vector<int8_t> dir_;      // per cell: best neighbour index 0-7, -1 = none
                                   // (1 byte, not two floats -- keeps the LRU cache
                                   // of fields small enough to hold every live goal)
    std::vector<uint16_t> dist_;   // integration field (0xFFFF = unreachable)
};

struct Player {
    float mana = 500;
    float storage = 0;   // recomputed each tick from alive units
    float income = 0;
    // God economy: priests (attractsgods) channel mana into favour; once it fills
    // after the gods' appear time, the faction's god can manifest (once).
    float godFavor = 0;
    bool  godSummoned = false;
    int   kills = 0;     // enemy units this player has destroyed (F4 overlay)
    // Alliance layer above ownership: players sharing a team fight together and
    // share vision. Default (set at game setup) = the player's own index, i.e.
    // a free-for-all where everyone is on their own team. Immutable in v1 --
    // if diplomacy ever makes it mutable it must be a sequenced command AND
    // enter stateHash (see docs/multiplayer-design.md).
    int   team = 0;
    bool  defeated = false;   // no living units; set by the sim's win check
};

// Max simultaneous players/teams (the retail map ceiling is 8 start positions).
constexpr int kMaxPlayers = 8;

class World {
public:
    int spawn(const UnitType* type, float x, float z, float heading = 0, int player = 0);
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
    void setRepeat(int builderId, const UnitType* type);   // toggle infinite build
    // Mobile builder constructs a building at (x, z). Returns the new
    // building's id, or 0 if the site is invalid.
    int startBuild(int builderId, const UnitType* type, float x, float z);
    // Build now if the builder is free, else queue it (shift-click). A
    // non-queued order replaces any pending queue.
    void queueBuild(int builderId, const UnitType* type, float x, float z, bool queue);
    // Abandon a builder's queued builds and drop any not-yet-started site.
    void cancelBuilds(int builderId);
    bool canPlace(const UnitType* type, float x, float z) const;
    // Mana deposit ("Sacred Stone") spots, in world px. Lodestones (onMana)
    // can only be built on one, but only when the map actually has any.
    void setManaSpots(std::vector<std::pair<float, float>> spots) {
        manaSpots_ = std::move(spots);
    }
    bool hasManaSpots() const { return !manaSpots_.empty(); }
    bool onManaSpot(float x, float z) const;
    // True if (x,z) lies over water (for choosing the water impact effect).
    bool isWater(float x, float z) const {
        if (depth_.empty()) return false;
        int cx = int(x) / 16, cz = int(z) / 16;
        if (cx < 0 || cz < 0 || cx >= terW_ || cz >= terH_) return false;
        return depth_[size_t(cz) * terW_ + cx] > 0;
    }
    Player& player(int i) { return players_[size_t(i)]; }
    const Player& player(int i) const { return players_[size_t(i)]; }
    int numPlayers() const { return int(players_.size()); }
    // Size the player table for a match (default 4 for single-player/scenarios).
    // Call BEFORE spawning any units -- shrinking it after would leave units with
    // an out-of-range owner index (dereferenced unchecked in the economy loop).
    void setPlayerCount(int n) {
        players_.assign(size_t(std::clamp(n, 1, kMaxPlayers)), Player{});
        for (int i = 0; i < numPlayers(); ++i) players_[size_t(i)].team = i;
    }
    // Assign a player's team, clamped to a valid team id [0, numPlayers). Teams
    // are 0..n-1 by construction; keeping them in range is the invariant that the
    // outcome check and the viewer's per-team arrays rely on -- so the sim owns
    // it rather than trusting setup (a lobby, or the TAK_FFA dev harness).
    void setTeam(int player, int team) {
        if (player < 0 || player >= numPlayers()) return;
        players_[size_t(player)].team = std::clamp(team, 0, numPlayers() - 1);
    }
    // Two players are allied if they share a team (a player is allied to itself).
    bool allied(int a, int b) const {
        if (a == b) return true;
        if (a < 0 || b < 0 || a >= numPlayers() || b >= numPlayers()) return false;
        return players_[size_t(a)].team == players_[size_t(b)].team;
    }

    // God economy (gamedata/Gods.tdf). Enable it, then a player whose god favour
    // fills after `appearSec` may manifest its god — the viewer polls godReady().
    void enableGods(float appearSec) { godsEnabled_ = true; godAppearTime_ = appearSec; }
    bool godsEnabled() const { return godsEnabled_; }
    float clock() const { return clock_; }
    static constexpr float kGodFavorNeeded = 3000.0f;
    bool godReady(int t) const {
        return godsEnabled_ && !players_[size_t(t)].godSummoned &&
               clock_ >= godAppearTime_ && players_[size_t(t)].godFavor >= kGodFavorNeeded;
    }

    // Win/defeat, computed sim-side so every lockstep peer agrees on the same
    // tick. A player is defeated when it has no living units; a team is out when
    // all its players are. Returns the surviving team id once exactly one team
    // remains (the winner), or -1 while >1 team still has units. Updates each
    // Player::defeated as a side effect. Idempotent; call once per tick.
    int updateOutcome();
    int winningTeam() const { return winningTeam_; }

    // Which player the fog-of-war grid tracks (default 0 = local player).
    void setVisPlayer(int t) { visPlayer_ = t; }
    int visPlayer() const { return visPlayer_; }
    // Deterministic digest of sim state, for lockstep sync checking.
    uint64_t stateHash() const;
    // Fog of war for the local player over 16px cells: 0 hidden, 1 explored, 2 visible.
    const std::vector<uint8_t>& visibility() const { return vis_; }
    int visW() const { return visW_; }
    int visH() const { return visH_; }
    bool cellVisible(float x, float z) const {
        if (vis_.empty()) return true;
        int cx = int(x) / 16, cz = int(z) / 16;
        if (cx < 0 || cz < 0 || cx >= visW_ || cz >= visH_) return false;
        return vis_[size_t(cz) * visW_ + cx] == 2;
    }
    // Move order; queue appends. Ground units steer by a shared flow field
    // toward (x,z) (crowd-friendly); flyers and unreachable goals fall back to
    // A* waypoints.
    void order(int unitId, float x, float z, bool queue);
    void attackMove(int unitId, float x, float z, bool queue);
    // Can a unit of `type` at (fx,fz) actually reach goal (gx,gz)? (flow-field
    // connectivity). Lets the AI pick a REACHABLE target instead of one that's
    // merely nearest in a straight line but walled off (army would stall/pile).
    bool pathExists(const UnitType* type, float gx, float gz, float fx, float fz);
    void patrol(int unitId, float x, float z);
    void guard(int unitId, int targetId, bool queue);
    void stop(int unitId);
    // Self-destruct a living unit (Ctrl+D via the command path; no kill credit).
    void destroy(int unitId);
    void setWeapon(int unitId, int slot);   // choose the active weapon (0=primary)
    // Toggle an onoffable unit's active state (gates, sacred fire, etc.).
    void setActive(int unitId, bool on) {
        Unit* u = unit(unitId);
        if (u && u->type && u->type->onOffable) u->active = on;
    }
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

    // Weapon impacts this tick (view-side: impact sound + effect). Carries the
    // source weapon (soundhitclass / hweffect) and the unit struck (body material
    // for the material-specific hit sound). Cleared at the start of each tick.
    struct HitFx { float x = 0, z = 0; const Weapon* weapon = nullptr;
                   const UnitType* target = nullptr; };
    const std::vector<HitFx>& hits() const { return hits_; }
    void clearHits() { hits_.clear(); }

private:
    void tickCombat(Unit& u, float dt);
    void fire(Unit& u, Unit& target, int slot);
    // Apply a weapon's damage at (hx,hz): the direct hit on `primary` plus, if
    // the weapon has areaofeffect, splash on other enemies of `fromPlayer` scaled
    // from full at the centre to `edge` at the rim. Per-category damage per victim.
    void applyHit(const Weapon& w, float hx, float hz, int fromPlayer, int fromId, Unit* primary);

    void tickProduction(Unit& u, float dt);
    void tickTransport(Unit& u, float dt);
    void tickConstruction(Unit& u, float dt);
    void tickAbilities(float dt);   // reclaim / resurrect on nearby corpses
    void tickAuras(float dt);       // AdjustArmor/Attack stat auras
    void updateVisibility();

    // Return a flow field toward world (gx,gz) over `type`'s nav grid, built and
    // cached on first use (keyed by goal cell + domain). Cleared when the nav
    // grid changes (a building is placed or removed). nullptr if unbuildable.
    const FlowField* flowFor(const UnitType* type, float gx, float gz);
    std::map<long long, FlowField> flowCache_;

    // Uniform spatial hash over mobile units, rebuilt each tick, so the
    // separation and combat-acquisition passes are O(n) instead of O(n^2).
    void rebuildGrid();
    // Call fn(int unitIndex) for every mobile unit whose cell lies within
    // `radius` of (x,z). Iterates cells in a fixed order, so it is deterministic.
    template <class F>
    void forEachNear(float x, float z, float radius, F&& fn) const {
        if (gW_ <= 0) return;
        int r = int(radius / gCell_) + 1;
        int cx = int((x - gOx_) / gCell_), cz = int((z - gOz_) / gCell_);
        for (int dz = -r; dz <= r; ++dz) {
            int gz = cz + dz;
            if (gz < 0 || gz >= gH_) continue;
            for (int dx = -r; dx <= r; ++dx) {
                int gx = cx + dx;
                if (gx < 0 || gx >= gW_) continue;
                for (int i = gHead_[size_t(gz) * gW_ + gx]; i >= 0; i = gNext_[size_t(i)])
                    fn(i);
            }
        }
    }
    std::vector<int> gHead_, gNext_;
    int gW_ = 0, gH_ = 0;
    float gCell_ = 32.0f, gOx_ = 0, gOz_ = 0;

    std::vector<uint8_t> vis_;
    int visPlayer_ = 0;
    int visW_ = 0, visH_ = 0;
    float visTimer_ = 0;
    std::vector<Unit> units_;
    std::vector<std::pair<float, float>> manaSpots_;
    std::vector<Projectile> projectiles_;
    std::vector<HitFx> hits_;
    std::vector<Player> players_ = []{
        std::vector<Player> v(4);
        for (int i = 0; i < 4; ++i) v[size_t(i)].team = i;
        return v;
    }();
    int winningTeam_ = -1;
    bool godsEnabled_ = false;
    float godAppearTime_ = 1e9f, clock_ = 0;
    uint32_t tickCounter_ = 0;   // ticks elapsed; staggers per-unit auto-acquisition
    int pathBudget_ = 0;         // A* repaths still allowed this tick (crowd throttle)
    NavGrid nav_, navWater_, navHover_;
    // Per-cell terrain metrics (16px cells) for per-unit passability limits.
    std::vector<uint8_t> slope_;   // local height spread
    std::vector<uint8_t> depth_;   // water depth (sea level - height), 0 on land
    int terW_ = 0, terH_ = 0;
    // A cell passable for `t`, honouring its maxSlope / maxWaterDepth on top of
    // the shared domain nav grid.
    bool passable(const UnitType* t, int cx, int cz) const {
        if (!navFor(t).walkable(cx, cz)) return false;
        if (!t || slope_.empty()) return true;
        if (cx < 0 || cz < 0 || cx >= terW_ || cz >= terH_) return false;
        size_t i = size_t(cz) * terW_ + cx;
        if (t->domain == UnitType::Domain::Ground) {
            if (slope_[i] > t->maxSlope) return false;                 // too steep
            if (t->maxWaterDepth > 0 && depth_[i] > t->maxWaterDepth)  // too deep
                return false;
        } else if (t->domain == UnitType::Domain::Water) {
            if (t->minWaterDepth > 0 && depth_[i] < t->minWaterDepth)  // too shallow
                return false;
        }
        return true;
    }
    int nextId_ = 1;
};

} // namespace tak::sim
