#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace tak::hpi { class Vfs; }

namespace tak::sim {

// Cheat: when true, construction and production complete instantly and cost no
// mana (set via the takclient `--cheat` flag).
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
    // FBI weapon type = "Line of Sight": a sustained hitscan beam (the drake's
    // Fire Breath), NOT a lobbed shot. Damage lands instantly along the sightline
    // and the flame stream is a client-side emitter driven by emitTime -- there is
    // no traveling projectile object.
    bool beam = false;
    float emitTime = 0;      // emittime (seconds): how long the flame/beam is emitted
    // The rest of the retail weapon-class model (FBI `type=`), beyond
    // melee/ballistic/line-of-sight:
    //   Guided       -- a homing projectile that steers at `turnRate` (20 weapons:
    //                   Tracking Arrow, Ball Lightning, dragon fireballs).
    //   Remote Effect-- the effect materialises AT THE TARGET POINT after
    //                   `buildUp`, then fades over `decay` (23: Earthquakes,
    //                   monarch waves, god spells, Area Mind Control).
    //   Wandering    -- a roaming storm entity that drifts for `duration`
    //                   (4: Tornado, Fire/Water Vortex, Hurricane).
    //   Dropped    -- a bomb RELEASED from a flyer: it falls to the ground under
    //                 the release point (retail looks up the terrain height right
    //                 there and drives it down under gravity), so the bomber has to
    //                 overfly its target instead of shooting from range.
    enum class Kind { Normal, Guided, Remote, Wandering, Dropped } kind = Kind::Normal;
    // Remote Effect splits further by subtype, and the split changes the damage
    // CADENCE completely (retail has a C++ subclass per variant):
    //   Plain      -- one area hit when the channel completes.
    //   Earthquake -- pulses every `shakeduration`, through buildup AND decay.
    //   Hailstorm  -- ignores buildup/decay; pulses `particlespersecond` times a
    //                 second for `duration` (the rain).
    //   MindCtl /  -- one conversion/freeze sweep at the end of the channel, and
    //   Freeze        the whole spell ABORTS if its caster dies mid-channel.
    enum class RemoteKind { Plain, Earthquake, Hailstorm, MindCtl, Freeze };
    RemoteKind remote = RemoteKind::Plain;
    float particlesPerSec = 0;   // particlespersecond: hailstorm pulse rate
    float turnRate = 0;      // guided: steering rate, radians/sec (FBI deg/s)
    float buildUp = 0;       // builduptime: channel before the effect lands
    float decay = 0;         // decaytime: fade after it lands
    float duration = 0;      // wandering: seconds the storm roams
    float maxVariation = 0;  // wandering: heading wobble per variationtime (radians)
    float variationTime = 0; // wandering: seconds between heading changes
    bool  unitsOnly = false; // unitsonly: the effect skips features (trees/props)
    // subtype=mindcontrol: converts targets to the firer's side instead of damaging
    // them, on a veterancy-scaled probability roll. The [DAMAGE] table is the
    // ACQUISITION filter -- retail zeroes the categories you may not TARGET
    // (monarch/god/dragon/fort/factory/naval/lodestone) -- while what actually
    // protects a unit caught inside an AREA charm is the commander / cantbecaptured
    // / in-transport gate at the impact itself.
    bool  mindControl = false;
    float minRange = 0;      // minrange: can't hit targets closer than this
    bool noAir = false;      // noairweapon: cannot target flying units
    float manaCost = 0;      // manapershot: mana drained from the firer per shot
    // FBI damagetype: 1 normal, 2 fire, 3 explosion (gibs -- Killed deathType 3
    // EXPLODEs every piece and leaves no corpse), 4 paralyzer (retail icd
    // 0x531bbd; "monster" and friends map to non-gib codes).
    int dmgType = 1;
    // Status effect this weapon inflicts (freeze/petrify/paralyze), 0 = none.
    enum class Status { None, Frozen, Stoned, Paralyzed } status = Status::None;
    float statusDur = 0;     // seconds the inflicted status lasts
    std::string soundHit;    // soundhitclass: impact-sound class (arrow/sword/cannon..)
    // Projectile ART (display only -- never hashed, like explosionClass below).
    // Retail draws a shot as authored art: a GAF/TAF sprite (weaponart, 60
    // weapons), or a real 3DO mesh (model, 39 -- arrows, spears, boulders), with
    // an optional glow (nimbus) and a ground shadow. Without these every shot in
    // the game is the same hand-drawn streak.
    std::string weaponArt;    // weaponart: anims/<name>_4444.taf sprite sequence
    std::string shotModel;    // model: objects3d/<name>.3do projectile mesh
    bool  nimbus = false;     // nimbus: additive glow around the shot
    bool  hasBoltColor = false;
    uint8_t inner[3] = {255, 255, 255};    // innercolor: lightning bolt core
    uint8_t middle[3] = {200, 230, 255};   // middlecolor
    uint8_t outer[3] = {120, 170, 255};    // outercolor
    float spinRate = 0;       // spinheading: shot spins as it flies (rad/sec)
    std::string shadowArt;    // shadowart: sequence in shadowgaf (always "shadows")
    int lightMap = 0;         // lightmap: ground light pool, 1 small 2 medium 3 large
    // A wandering storm's own three-part animation: the spin-up, the roaming loop,
    // and the dissipation. Without these a tornado is invisible.
    std::string wanderStart, wanderLoop, wanderEnd;
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
    bool commander = false;   // FBI commander=1: the faction's Monarch (loss condition)
    // Buildings vs mobile units: the reliable test is maxVel. The FBI `canmove`
    // flag is set on some buildings too (e.g. the Keep, or the Taros Hell), so a
    // canMove building would otherwise be mistaken for a mobile builder.
    bool isStructure() const { return maxVel <= 0.0f; }
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
    std::string stoneFeat;    // FBI stone= statue feature (death while petrified)
    std::string frozenFeat;   // FBI frozen= statue feature (death while frozen)
    int corpseAdjX = 0, corpseAdjZ = 0;   // corpseadjustx/z: wreck offset in cells
    bool canAnimate = false;              // cananimate: raises animateType from corpses
    const UnitType* animateType = nullptr;   // animatetype=<unit>, resolved post-load
    std::string animName_;                   // raw animatetype value (loadDir fixup)
    std::string shadowArt;    // FBI shadowart: shadow sprite name in shadows.gaf
    std::string veteranModel; // veteranmodel: 3DO the unit swaps to at max veterancy
    // --- extended FBI stats -------------------------------------------------
    float healTime = 0;       // healtime: seconds per HP regenerated (0 = no regen)
    float leash = 0;          // maneuverleashlength: max auto-chase distance (0 = unlimited)
    float waterMult = 1;      // watermultiplier: speed factor in shallow water
    float roadMult = 1.2f;    // roadmultiplier: on-road speed factor. Retail's FBI
                              // parser defaults it to 16.16 0x13333 (~1.2) -- icd
                              // 0x4bfc5e -- so EVERY ground unit gains on roads.
    float maxWaterDepth = 0;  // deepest water a ground unit may wade into
    float maxSlope = 255;     // steepest cell height-spread the unit may cross
    float minWaterDepth = 0;  // shallowest water a water unit needs (from MOVEINFO)
    float radar = 0;          // radardistance: fog-reveal radius (separate from sight)
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
    // [EXPLODEAS]: a weapon fired at the unit's OWN position the moment it dies
    // (Kamikaze Rat's 320-radius blast, Grenadier/Fire Demon/Balloon death pops).
    Weapon explodeAs;
    bool hasExplodeAs = false;
    // totalallowed: per-player cap on LIVE units of this type (dragons/gods/
    // juggernaut carry 1). 0 = unlimited.
    int totalAllowed = 0;
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
    void loadMoveInfo(const hpi::Vfs& vfs, const std::string& path);
    // Parse every .fbi under `prefix` in the VFS (e.g. "units"). All source
    // archives' units merge into one namespace, precedence already resolved.
    void loadDir(const hpi::Vfs& vfs, const std::string& prefix);
    // Parse <prefix>/<builder>/<buildable>.tdf into the build tree.
    void loadBuildTree(const hpi::Vfs& vfs, const std::string& prefix);
    const UnitType* find(const std::string& id) const;
    // Full type table in deterministic (name-sorted) order -- corpse interning
    // walks it so every peer builds identical FeatType indices.
    const std::map<std::string, UnitType>& types() const { return types_; }
    const std::vector<std::string>& buildable(const std::string& builderId) const;
    // Mobile, armed combat units of a faction `side` ("ARA".."CRE"), in a fixed
    // (name-sorted) order so every peer builds the same stress-test army. Excludes
    // structures, builders, the Monarch, and anything with no weapon.
    std::vector<const UnitType*> combatUnits(const std::string& side) const;
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
    float wait = 0;            // >0: hold position, counting down (SetMission "w N")
    bool waitAttack = false;   // hold until an enemy is in sight, then release (SetMission "wa")
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
    float reloads[3] = {0, 0, 0};  // per weapon slot
    int   weaponSlot = 0;          // active weapon (0=primary); player-selectable
    // True until the player picks a weapon with Ctrl+W. While set, the sim chooses
    // the best usable weapon per target the way retail's fire-at-will scan does;
    // once the player has chosen, their pick is obeyed.
    bool  weaponAuto = true;
    float repathLeft = 0;   // chase steering repath countdown
    float stuckFor = 0;     // seconds wanting to move but making no progress
    float stuckX = 0, stuckZ = 0;   // position when the stuck timer last reset
    float goalStuckT = 0;           // seconds a point-destination move has not gotten closer
    float goalStuckD = 1e30f;       // best (closest) squared distance to that goal so far
    float buildStuckT = 0;          // seconds a builder has approached its site with no progress
    float buildStuckD = 1e30f;      // best (closest) squared dist to the build site so far
    float deadFor = -1;    // >= 0 once dead; counts up for death animation
    float corpseUntil = 4;   // deadFor when the body is gone (4 = right after the
                             // death anim; corpse types extend by decomposetime)
    float overkill = 0;      // damage past the killing blow (retail severity input)
    uint8_t deathType = 1;   // damagetype of the killing blow (3 = explosion/gib)
    uint8_t severity = 0;    // retail Killed severity ((overkill% + 1s-ago HP%)/2)
    uint8_t hpPct1s = 100;   // HP% sampled every 30 ticks (previous sample -- the
    uint8_t hpPctCur = 100;  //  retail unit+0x111/+0x110 pair severity reads)
    int corpseStatue = -1;   // FeatType override chosen at death (stone/frozen), -1 = corpse=
    float corpseWork = 60;   // ordered-reclaim work left in the body (kReclaimRate/s)
    int reviveTarget = 0;    // priest: dead unit id being channelled back (0 = none)
    int8_t reviveMode = 0;   // 1 = resurrect (own corpse), 2 = animate (raise ghoul)
    float reviveLeft = 0;    // seconds of channel remaining
    float reviveTotal = 1;   // full channel length (mana drains proportionally)
    bool corpseBlocks = false;   // dead structure still occupies its nav footprint
                                 // (blocking wreck / neutral wall) until retired
    // --- extended runtime state --------------------------------------------
    float mana = 0;        // personal mana pool (casters), capped at type->maxMana
    int   xp = 0;          // accumulated experience from kills
    int   veteran = 0;     // veteran level (0..10); scales attack/armor/reload
    float atkBuff = 1;     // live attack multiplier from auras (decays to 1)
    float armBuff = 1;     // live armour multiplier from auras (decays to 1)
    float frozenFor = 0;   // >0 = frozen solid (can't act); counts down
    float stonedFor = 0;   // >0 = petrified (can't act, immune to damage while stone)
    float paralyzedFor = 0;// >0 = paralyzed (can't act, still takes damage)
    float selfDestructT = -1;// >=0 = self-destruct countdown (s) armed; -1 = not
    bool  cloaked = false; // currently invisible to enemies
    bool  cloakOn = true;  // canCloak units: player wants to cloak (gates auto-cloak)
    bool  active = true;   // onoffable units: false = powered down
    int   stance = 1;      // combat stance: 0=offensive (chase freely), 1=defensive
                           // (leashed, the default/legacy behaviour), 2=passive
                           // (hold fire: no auto-acquire, only fights when ordered)
    int8_t squad = 0;      // control squad: 0=none, +N=group N, -N=formation N (N=1..10,
                           // the digit 0 key = 10). A unit is in exactly one squad. A
                           // formation (squad<0) moves at its slowest member's speed and
                           // its stragglers rejoin. Set via Cmd::SetSquad; folded in the hash.
    int   lastHitBy = 0;   // id of the unit that last damaged this one (for kill XP)
    float captureProg = 0; // canCapture units: seconds spent charming the current target
    float homeX = 0, homeZ = 0;   // leash anchor (idle position) for auto-chase
    bool  justFired = false;   // set for one tick when the weapon fires
    bool underConstruction = false;
    bool buildBegun = false;   // construction site: true once the builder arrived
    float conjureRate = 0;     // site: hp/sec the last builder added; drives decay
    bool  beingBuilt = false;  // site: transient -- a builder worked it this tick
    int buildSiteId = 0;   // builder: id of the building it is constructing
    // These queues hold at most a handful of entries and are edited only on order
    // completion (not in a hot inner loop), so std::vector -- which allocates NOTHING
    // when empty, unlike std::deque's eager ~576-byte control block -- is both the
    // memory-lean and the faster choice; front-pops become erase(begin()). Element
    // order (all that stateHash folds in) is preserved, so lockstep is byte-identical.
    std::vector<BuildOrder> buildOrders;   // builder: queued (shift) builds
    int reclaimId = 0;                 // builder: feature being reclaimed (0 = none)
    std::vector<int> reclaimQueue;     // builder: queued area-reclaim feature ids
    int repairId = 0;                  // builder: damaged friendly being repaired (0 = none)
    int inTransport = 0;   // id of carrying transport, 0 = none
    std::vector<int> cargo;
    std::vector<Order> orders;
    // Production (buildings with a build tree).
    std::vector<const UnitType*> buildQueue;
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

// A reclaimable map feature (tree, rock, house, wreckage, …). Positions and stats
// are derived deterministically in setupMatch -- the same row-major walk runs on
// every peer -- so reclaim stays in lockstep. Not a Unit: no orders or combat,
// just reclaim "work" a builder chips away for mana. A `blocks` feature also
// occupies the nav grid, freed when it is reclaimed.
// Per-feature-type burn/footprint data the sim needs (built by setupMatch from
// the feature TDFs, identical on every peer -- indices are shared lockstep state).
struct FeatType {
    std::string name;        // lowercase TDF section name (client art lookup)
    bool flamable = false;   // TDF flamable=1: can be ignited / spread to
    bool hasBurnAnim = false;// TDF seqnameburn present (retail StartBurning requires it)
    int  spreadChance = 0;   // TDF spreadchance (percent)
    int  sparkTicks = 0;     // TDF sparktime * 30 (retail stores seconds*30)
    int  burntType = -1;     // TDF featureburnt -> index into the same table
    float energy = 0;        // reclaim yield of this stage
    int  fx = 1, fz = 1;
    bool blocking = false;
    // Corpse defs (features/corpses/*_dead.tdf): how long the body lies there
    // and whether a priest can raise it (retail decomposetime / resurrectable).
    int  decomposeTicks = 0; // TDF decomposetime * 30 (0 = never rots)
    bool resurrectable = false;
    bool reclaimable = false;
    bool isStone = false;    // TDF isstone=1 (statue: client tints it stone-gray)
    bool isFrozen = false;   // TDF isfrozen=1 (client tints it ice-blue)
    bool indestructible = false;
    float hp = 0;            // TDF damage= (weapon damage the feature absorbs)
    int  deadType = -1;      // TDF featuredead -> destroyed-replacement (placed neutral)
    std::string object;      // TDF object= (3D corpse mesh; client visual)
};

struct Feature {
    int   id = 0;          // cz*terrainWidth + cx: position-derived, peer-identical
    float x = 0, z = 0;
    int   fx = 1, fz = 1;  // footprint cells (for the nav unblock on removal)
    float manaYield = 0;   // total mana granted over a full reclaim (FBI `energy`)
    float work = 0;        // remaining reclaim work; consumed to 0
    float workFull = 1;    // initial work (for the proportional mana drip)
    bool  blocks = false;  // occupied the nav grid
    bool  alive = true;    // false once fully reclaimed (decal disappears)
    // Burning (retail mechanic, icd 0x494b40/0x495110/0x495300 -- ours runs
    // fully deterministic in the lockstep sim instead of retail's host-authority
    // event scheme). All hashed.
    int   type = -1;       // index into World's FeatType table (-1 = untyped)
    uint8_t burn = 0;      // 1 = burning
    float dmg = 0;         // accumulated weapon damage (dies at FeatType.hp)
    int   spreadIn = 0;    // ticks until the single spread event (sparktime-derived)
    int   burnLeft = 0;    // ticks until burn-out (swap to burntType / die)
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
    // Does a `foot`-cell-across unit fit CENTRED on (cx,cz)? (foot<=1 == walkable.)
    // Backed by a lazily-built clearance grid, so O(1). Footprint-aware pathing and
    // steering use this so a 4x4 unit never routes through a 1-cell gap and wedges.
    bool fits(int cx, int cz, int foot) const;
    // Force the lazy clearance grid up to date NOW. The parallel flow-field
    // prefetch calls this before handing the grid to worker threads, so they
    // only ever READ it (fits() would otherwise rebuild the mutable cache
    // concurrently from several threads).
    void ensureClearance() const { if (clearDirty_) rebuildClearance(); }
    bool empty() const { return cells_.empty(); }
    int width() const { return w_; }
    int height() const { return h_; }
    // Mark a rectangle of cells blocked (building footprint) or clear.
    void block(int cx, int cz, int w, int h, bool blocked);
    // Road preference (ground grid only): pathfinding rates non-road steps ~1.2x
    // costlier, so marches drift onto highways -- the retail steering area-rater
    // scores road cells 7 vs 6 (icd 0x508527). `roads` must outlive the grid and
    // match its dimensions (World::roads_; null = no preference).
    void setRoads(const std::vector<uint8_t>* roads) {
        roads_ = (roads && roads->size() == size_t(w_) * size_t(h_)) ? roads : nullptr;
    }
    bool roadAt(int cx, int cz) const {
        return roads_ && (*roads_)[size_t(cz) * w_ + cx] != 0;
    }
    bool hasRoads() const { return roads_ != nullptr; }

    // A* in cell space (16px cells), with waypoint simplification. `foot` = the unit's
    // footprint size in cells (1 = point). Returns world-space waypoints; empty if
    // unreachable for a unit that size.
    std::vector<Order> findPath(float x0, float z0, float x1, float z1, int foot = 1) const;

    // Line of sight: no blocked cell between the two world points, ignoring cells
    // within `skip0`/`skip1` cells of each endpoint — so a shooter or target's own
    // building footprint doesn't block the shot, but a wall between them does.
    bool losBetween(float wx0, float wz0, float wx1, float wz1,
                    int skip0 = 0, int skip1 = 0) const;

private:
    bool lineClear(int x0, int z0, int x1, int z1) const;
    void rebuildClearance() const;

    std::vector<uint8_t> cells_;
    // findPath scratch, reused across calls (a big map otherwise allocates + fills
    // ~1.1MB per search). A cell's g/from are valid only when its stamp matches
    // pathGen_, so "reset" is one counter bump. mutable like clear_ -- findPath is a
    // logical-const query and the sim is single-threaded per world.
    mutable std::vector<float> pathG_;
    mutable std::vector<int> pathFrom_;
    mutable std::vector<uint32_t> pathStamp_;
    mutable uint32_t pathGen_ = 0;
    // clear_[c] = side of the largest all-walkable square whose min corner is c.
    // Lazily rebuilt (dirtied by block()); a foot-cell unit fits at corner c iff
    // clear_[c] >= foot. mutable so fits()/pathfinding can build it on demand.
    // Values saturate at 15 (the max queryable footprint), which is what lets
    // block() refresh only a bounded rect instead of dirtying the whole grid.
    void updateClearanceRect(int cx, int cz, int w, int h) const;
    mutable std::vector<uint16_t> clear_;
    mutable bool clearDirty_ = true;
    const std::vector<uint8_t>* roads_ = nullptr;   // see setRoads
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
    // Build the field over `nav` toward the goal at world (gx, gz), for a unit whose
    // footprint is `foot` cells across (so the wavefront only crosses cells the unit
    // fits in). If the goal cell is blocked it snaps to the nearest fitting cell.
    // Returns false if the grid is empty or no fitting goal cell exists.
    bool build(const NavGrid& nav, float gx, float gz, int foot = 1);
    bool ready() const { return w_ > 0; }
    // Unit direction at world (x, z); (0,0) at/near the goal, off-grid, or in an
    // unreachable pocket (caller should then steer straight at the goal).
    void dirAt(float x, float z, float& dx, float& dz) const;
    // Was the goal cell reachable from (x,z)? (finite integration distance)
    bool reachable(float x, float z) const;
    // Any reachable cell inside the CELL rect [x0,x1]x[z0,z1] (inclusive, clamped)?
    // Used by targeted flow invalidation: a nav edit whose padded rect touches no
    // reachable cell of this field cannot alter it (the change lives in a pocket
    // this field never routes through), so the field survives the edit.
    bool touchesReachable(int x0, int z0, int x1, int z1) const {
        if (w_ <= 0) return false;
        x0 = std::max(x0, 0); z0 = std::max(z0, 0);
        x1 = std::min(x1, w_ - 1); z1 = std::min(z1, h_ - 1);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                if (dist_[size_t(z) * w_ + x] != 0xFFFF) return true;
        return false;
    }

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
    // Income multiplier (1.0 = normal). Only ever != 1 for an Absurd-difficulty AI,
    // set once at match setup and applied to every mana source. Constant per game and
    // its effect lands in `mana` (which IS hashed), so it need not be hashed itself,
    // but every peer must set it identically or their mana diverges.
    float manaMult = 1.0f;
    // God economy: priests (attractsgods) channel mana into favour; once it fills
    // after the gods' appear time, the faction's god can manifest (once).
    float godFavor = 0;
    bool  godSummoned = false;
    int   kills = 0;     // enemy units this player has destroyed (F4 overlay)
    // Live unit count, refreshed every tick by updateOutcome (and bumped by spawn
    // within a tick). Derived from units_, so it is deterministic but NOT hashed;
    // drives the unit-cap check and could feed the F4 overlay.
    int   unitCount = 0;
    // Alliance layer above ownership: players sharing a team fight together and
    // share vision. Default (set at game setup) = the player's own index, i.e.
    // a free-for-all where everyone is on their own team. Immutable in v1 --
    // if diplomacy ever makes it mutable it must be a sequenced command AND
    // enter stateHash (see docs/multiplayer-design.md).
    int   team = 0;
    bool  defeated = false;   // no living units; set by the sim's win check
    // Cosmetic "disco" emote (Shift+D): seconds this player's monarchs keep
    // dancing. Set by a lockstep Cmd::Disco so every peer agrees on the timing,
    // but it drives client-side eye-candy only and is NOT folded into stateHash
    // (like vis_).
    float discoLeft = 0;
    // Cosmetic "headbang" emote (Shift+H): seconds this player's monarchs headbang to
    // heavy metal. Same deal as discoLeft -- synced by Cmd::Headbang, not hashed.
    float headbangLeft = 0;
};

// Max simultaneous players/teams (the retail map ceiling is 8 start positions).
constexpr int kMaxPlayers = 8;

class MissionScript;   // src/sim/mission.h -- optional campaign "god" script + win/lose
class ScenarioScript;  // src/sim/scenario.h -- optional .crt per-player trigger runner

// Benchmark staged-spawn plan: a set of units to spawn at a scheduled tick. Built once in
// setupMatch (deterministically -- so the client sim and the referee build the identical
// plan) and executed by World::tick. See MatchConfig::benchmark.
struct BenchSpawn { const UnitType* type = nullptr; float x = 0, z = 0; int player = 0; };
struct BenchStage { uint32_t tick = 0; std::vector<BenchSpawn> units; };

class World {
public:
    int spawn(const UnitType* type, float x, float z, float heading = 0, int player = 0);
    // Benchmark: install the staged spawn plan (see BenchStage). endTick marks when the
    // benchmark run finishes; benchmarkMode() is true while a plan is installed.
    void setBenchmarkPlan(std::vector<BenchStage> plan, uint32_t endTick) {
        benchPlan_ = std::move(plan); benchCursor_ = 0; benchEndTick_ = endTick;
    }
    bool benchmarkMode() const { return benchEndTick_ > 0; }
    uint32_t benchmarkEndTick() const { return benchEndTick_; }
    uint32_t tickCount() const { return tickCounter_; }   // ticks elapsed (benchmark timing)
    // Build per-domain nav grids from heights + sea level. `features` is the
    // map's raw per-cell feature plane if available: retail stores per-cell
    // attributes in it as special values -- 0xFFFB marks ROAD cells (the
    // roadmultiplier speed bonus + the COB walk_road gait).
    void setTerrain(const std::vector<uint8_t>& heights, int w, int h, int seaLevel,
                    const std::vector<uint16_t>* features = nullptr);
    NavGrid& nav() { return nav_; }
    const NavGrid& navFor(const UnitType* t) const {
        if (t && t->domain == UnitType::Domain::Water) return navWater_;
        if (t && t->domain == UnitType::Domain::Hover) return navHover_;
        return nav_;
    }
    // Queue production of `typeId` at a builder building.
    void train(int builderId, const UnitType* type, int count = 1);   // queue `count`
    void dequeue(int builderId, const UnitType* type, int count);     // un-queue `count`
    void setRepeat(int builderId, const UnitType* type);   // toggle infinite build
    // How many of `type` are queued at a builder (for the build-icon count).
    int queuedCount(int builderId, const UnitType* type) const;
    // Mobile builder constructs a building at (x, z). Returns the new
    // building's id, or 0 if the site is invalid.
    int startBuild(int builderId, const UnitType* type, float x, float z);
    // Build now if the builder is free, else queue it (shift-click). A
    // non-queued order replaces any pending queue.
    void queueBuild(int builderId, const UnitType* type, float x, float z, bool queue);
    // Abandon a builder's queued builds and drop any not-yet-started site.
    void cancelBuilds(int builderId);
    // Latch a mobile builder onto an existing construction site to resume/assist
    // conjuring it (e.g. reviving a decaying site). Resumes at THIS builder's
    // rate from the site's current HP. The caller checks the build tree.
    void assist(int builderId, int siteId);
    // Cosmetic emote: make `player`'s monarchs dance for 10s (Cmd::Disco). Not
    // hashed -- purely for the viewer. discoActive() gates the client animation.
    void startDisco(int player);
    bool discoActive(int player) const;
    // Cosmetic emote: make `player`'s monarchs headbang for 10s (Cmd::Headbang).
    void startHeadbang(int player);
    bool headbangActive(int player) const;
    bool canPlace(const UnitType* type, float x, float z) const;
    // Mana deposit ("Sacred Stone") spots, in world px. Lodestones (onMana)
    // can only be built on one, but only when the map actually has any.
    void setManaSpots(std::vector<std::pair<float, float>> spots) {
        manaSpots_ = std::move(spots);
    }
    bool hasManaSpots() const { return !manaSpots_.empty(); }
    const std::vector<std::pair<float, float>>& manaSpots() const { return manaSpots_; }
    // Reclaimable features (trees/rocks/houses). Populated only by setupMatch (the
    // one deterministic per-peer walk); never from the viewer. See struct Feature.
    void addFeature(int id, float x, float z, float manaYield, float work,
                    int fx, int fz, bool blocks, int type = -1);
    const std::vector<Feature>& features() const { return features_; }
    const Feature* feature(int id) const;                 // by id, nullptr if none
    const Feature* featureAt(float x, float z) const;     // by cell (viewer burn/art sync)
    void setFeatureTypes(std::vector<FeatType> t) { featTypes_ = std::move(t); }
    // Unit type -> its corpse feature def (index into featTypes_, -1 = none).
    void mapCorpse(const UnitType* t, int featType) { corpseType_[t] = featType; }
    void mapStatue(const UnitType* t, int stoneIdx, int frozenIdx) {
        if (stoneIdx >= 0) stoneType_[t] = stoneIdx;
        if (frozenIdx >= 0) frozenType_[t] = frozenIdx;
    }
    int statueTypeOf(const UnitType* t, bool frozen) const {
        const auto& m = frozen ? frozenType_ : stoneType_;
        auto it = m.find(t);
        return it == m.end() ? -1 : it->second;
    }
    int corpseTypeOf(const UnitType* t) const {
        auto it = corpseType_.find(t);
        return it == corpseType_.end() ? -1 : it->second;
    }
    // Drop all registered features (setupMatch rebuilds authoritatively -- the
    // client ctor may have pre-registered the launch map's via registerMapFeatures).
    void clearFeatures() { features_.clear(); featureIdx_.clear(); }
    const std::vector<FeatType>& featureTypes() const { return featTypes_; }
    bool featureAliveAt(float x, float z) const;          // viewer decal sync
    // Order a mobile builder to reclaim feature `featureId` (queue = append to its
    // reclaim queue, for an area drag). Grants the feature's mana as it consumes it.
    void reclaim(int builderId, int featureId, bool queue);
    // Order a mobile builder to repair damaged friendly `targetId` (restores HP at
    // the builder's work rate, draining mana proportionally). tickRepair runs it.
    void repair(int builderId, int targetId, bool queue);
    void tickRepair(Unit& b, float dt);
    // True if (x,z) lies over water (for choosing the water impact effect).
    // Retail (icd 0x509760): a unit is "on road" only when EVERY cell under its
    // footprint carries the road flag (ground units only; checked per tick into
    // a status bit that GET 34 and the speed formula read).
    bool onRoad(float x, float z, int footX = 1, int footZ = 1) const {
        if (roads_.empty()) return false;
        int cx = int(x) / 16, cz = int(z) / 16;
        int x0 = cx - footX / 2, z0 = cz - footZ / 2;
        if (x0 < 0 || z0 < 0 || x0 + footX > terW_ || z0 + footZ > terH_)
            return false;
        for (int dz = 0; dz < footZ; ++dz)
            for (int dx = 0; dx < footX; ++dx)
                if (!roads_[size_t(z0 + dz) * terW_ + size_t(x0 + dx)]) return false;
        return true;
    }
    bool isWater(float x, float z) const {
        if (depth_.empty()) return false;
        int cx = int(x) / 16, cz = int(z) / 16;
        if (cx < 0 || cz < 0 || cx >= terW_ || cz >= terH_) return false;
        return depth_[size_t(cz) * terW_ + cx] > 0;
    }
    Player& player(int i) { return players_[size_t(i)]; }
    const Player& player(int i) const { return players_[size_t(i)]; }
    int numPlayers() const { return int(players_.size()); }
    // Reset all simulation state so a match can be rebuilt (via setupMatch) and
    // replayed from tick 0 deterministically -- crucially nextId_ resets so the
    // replayed spawns get the SAME unit ids as the original run. setTerrain and
    // setPlayerCount (called by setupMatch afterwards) rebuild nav/vis/players.
    void resetForReplay() {
        units_.clear();
        projectiles_.clear();
        hits_.clear();
        flowCache_.clear();
        features_.clear();
        featureIdx_.clear();
        // Every other kind of live cross-tick sim state has to go too, or a rejoin
        // replays tick 0 with leftovers from before the drop: a storm still roaming
        // (or a spell still channelling) would deal damage the referee never dealt
        // AND advance the shared RNG on this peer alone -- an instant desync. The
        // RNG itself is re-seeded for the same reason: replaying from a mid-match
        // stream position diverges from a referee that started fresh.
        pendingEffects_.clear();
        storms_.clear();
        stormSeq_ = 0;
        deathBlasts_.clear();
        burnRng_ = 0x54414B21;
        nextId_ = 1;
        tickCounter_ = 0;
        clock_ = 0;
        winningTeam_ = -1;
    }
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

    // Per-player unit limit: production and new builds stall a player once it has
    // this many live units (0 = unlimited). Set at match start (from the lobby).
    // The count it tests (Player::unitCount) is deterministic, so all peers agree.
    void setUnitCap(int c) { unitCap_ = c; }
    int unitCap() const { return unitCap_; }
    bool atUnitCap(int player) const {
        return unitCap_ > 0 && player >= 0 && player < int(players_.size()) &&
               players_[size_t(player)].unitCount >= unitCap_;
    }
    // FBI `totalallowed`: a per-player cap on LIVE units of one type (the five
    // dragons, the five gods and the Aerial Juggernaut ship 1). Retail refuses to
    // finish a conjure that would exceed it, so a player fields only one. Counts
    // units under construction too, so queuing two dragons can't sneak both out.
    bool atTypeCap(int player, const UnitType* t) const {
        if (!t || t->totalAllowed <= 0) return false;
        int n = 0;
        for (const auto& u : units_)
            if (u.alive() && u.type == t && u.player == player &&
                ++n >= t->totalAllowed)
                return true;
        return false;
    }
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
    // Fog-of-war memory (client display only, never hashed): true = a seen cell stays
    // EXPLORED (dimmed) when it leaves sight; false = NOT EXPLORED -- it reverts to dark.
    void setFogExplored(bool e) { fogExplored_ = e; }
    // Monarch-expendable rule (net GameOptions): when FALSE, losing your Monarch
    // (a commander unit) loses you the game even if other units survive.
    void setMonarchExpendable(bool e) { monarchExpendable_ = e; }
    // Run flow-field prefetch on the calling thread only (no worker pool). The server
    // sets this on a referee World when it ticks several games IN PARALLEL: the
    // parallelism is already at the game level, so a per-game nested flow pool would
    // just oversubscribe. A lone game (SP, or the client's own sim) leaves it off and
    // keeps the intra-tick flow parallelism. Never affects results -- purely how the
    // (identical) flow builds are scheduled.
    void setSerialFlow(bool s) { serialFlow_ = s; }
    // Deterministic digest of sim state, for lockstep sync checking.
    uint64_t stateHash() const;

    // Campaign mission: an optional in-sim "god" script + win/lose rules
    // (src/sim/mission.h). Ticked inside tick(), and folded into stateHash().
    World();                                                    // out-of-line (mission_ member)
    ~World();                                                   // out-of-line (mission_ dtor)
    void setMission(std::unique_ptr<MissionScript> m);
    MissionScript* mission() { return mission_.get(); }
    int missionOutcome() const;   // 0 none/running / +1 victory / -1 defeat

    // Scenario (.crt) trigger runner: an optional in-sim per-player rule engine
    // (src/sim/scenario.h). Ticked inside tick() and folded into stateHash().
    void setScenario(std::unique_ptr<ScenarioScript> s);
    ScenarioScript* scenario() { return scenario_.get(); }
    // Force a player to count as defeated regardless of its unit count (a
    // scenario Victory/Defeat action); respected by updateOutcome, hashed.
    void forceDefeat(int player);
    // Fog of war for the local player over 16px cells: 0 hidden, 1 explored, 2 visible.
    const std::vector<uint8_t>& visibility() const { return vis_; }
    uint32_t visGeneration() const { return visGen_; }   // bumps on each fog recompute
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
    bool pathExists(const UnitType* type, float gx, float gz, float fx, float fz) const;
    void patrol(int unitId, float x, float z);
    // Queue a patrol waypoint (SetMission "p X Y"): like a move but the completed
    // order re-queues at the back, so a chain of these loops the unit through them.
    void patrolTo(int unitId, float x, float z, bool queue);
    // Queue a timed hold (SetMission "w N"): the unit stands still for `seconds`
    // before the next queued order runs.
    void orderWait(int unitId, float seconds, bool queue);
    // Queue an ambush hold (SetMission "wa"): the unit stands still until a non-allied
    // unit comes within its sight, then the next queued order runs.
    void orderWaitAttack(int unitId, bool queue);
    void guard(int unitId, int targetId, bool queue);
    void stop(int unitId);
    // Self-destruct a living unit (Ctrl+D via the command path; no kill credit).
    void destroy(int unitId);
    void setWeapon(int unitId, int slot);   // choose the active weapon (0=primary)
    void setStance(int unitId, int stance); // combat stance 0=offensive/1=defensive/2=passive
    void setCloak(int unitId, bool on);     // canCloak unit: enable/disable cloaking
    void setActive(int unitId, bool on);    // onOffable unit: power on/off
    void setSquad(int unitId, int squad);   // control squad: 0 none, +N group N, -N formation N
    // Attack order on an enemy unit.
    void attack(int unitId, int targetId, bool queue);
    // Board a friendly transport / sail to (x,z) and disembark.
    void loadInto(int unitId, int transportId);
    void unloadAt(int transportId, float x, float z);
    void tick(float dt);

    std::vector<Unit>& units() { return units_; }
    const std::vector<Unit>& units() const { return units_; }
    // A wandering storm (FBI type=wandering): a roaming hazard the VIEWER must
    // draw, so its state is public like a projectile's. `id` is stable for the
    // storm's life so the viewer can play its spin-up, loop and dissipation art.
    struct Storm {
        const Weapon* w = nullptr;
        float x = 0, z = 0;
        float dirX = 0, dirZ = 1;   // FIXED launch direction: a storm never re-aims
        float jitX = 0, jitZ = 0;   // current per-tick wander offset (px/tick)
        int player = 0, fromId = 0;
        int id = 0;
        float arm = 0;       // builduptime: it drifts but does not bite yet
        float left = 0;      // seconds of roaming left (duration)
        float nextVary = 0;  // seconds until the next wander re-roll
    };
    const std::vector<Storm>& storms() const { return storms_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    Unit* unit(int id);
    const Unit* unit(int id) const {
        return const_cast<World*>(this)->unit(id);
    }

    // Weapon impacts this tick (view-side: impact sound + effect). Carries the
    // source weapon (soundhitclass / hweffect) and the unit struck (body material
    // for the material-specific hit sound). Cleared at the start of each tick.
    struct HitFx { float x = 0, z = 0; const Weapon* weapon = nullptr;
                   const UnitType* target = nullptr;
                   int victimId = 0;            // primary struck unit (0 = ground hit)
                   float fromX = 0, fromZ = 0;  // attacker pos (viewer flinch direction)
                   float damage = 0; };         // pre-armour damage vs the victim
    const std::vector<HitFx>& hits() const { return hits_; }
    void clearHits() { hits_.clear(); }

private:
    void tickCombat(Unit& u, float dt);
    void fire(Unit& u, Unit& target, int slot);
    // Convert a unit to another player (contact charm + mind-control weapons).
    void captureUnit(Unit& t, int newPlayer);
    // Apply a weapon's damage at (hx,hz): the direct hit on `primary` plus, if
    // the weapon has areaofeffect, splash on other enemies of `fromPlayer` scaled
    // from full at the centre to `edge` at the rim. Per-category damage per victim.
    void applyHit(const Weapon& w, float hx, float hz, int fromPlayer, int fromId, Unit* primary);

    void tickProduction(Unit& u, float dt);
    void tickTransport(Unit& u, float dt);
    void tickConstruction(Unit& u, float dt);
    // Orphaned conjure (no builder worked it this tick): bleed HP at its build
    // rate, then vanish with no corpse.
    void decayConstruction(Unit& u, float dt);
    // A builder chips reclaim work off its target feature, drips mana, then removes
    // the feature and advances its reclaim queue.
    void tickReclaim(Unit& b, float dt);
    void tickAbilities(float dt);   // reclaim / resurrect on nearby corpses
    void tickAuras(float dt);       // AdjustArmor/Attack stat auras
    void updateVisibility();

    // Return a flow field toward world (gx,gz) over `type`'s nav grid, built and
    // cached on first use (keyed by goal cell + domain). Cleared when the nav
    // grid changes (a building is placed or removed). nullptr if unbuildable.
    // const: the flow field is a PURE MEMO of (nav grid, goal, domain) -- content
    // never depends on when/whether it was built -- so building a cache entry the
    // AI needs (on the server, where clients don't) cannot perturb sim state. The
    // cache is therefore mutable and this is a logical-const query. Single-threaded
    // per world (not safe to call off the sim thread). See docs/multiplayer-design.md.
    const FlowField* flowFor(const UnitType* type, float gx, float gz) const;
    // The flow memo's cache key + build inputs, shared by flowFor and the tick-top
    // parallel prefetch so both derive the identical pure function of
    // (domain, foot, quantized goal block). False if the domain grid is empty.
    struct FlowKey { long long key; const NavGrid* grid; float bx, bz; int foot; };
    bool flowKeyFor(const UnitType* type, float gx, float gz, FlowKey& out) const;
    void prefetchFlows();   // batch-build this tick's missing fields on threads
    // Targeted flow invalidation after a GROUND nav edit in the cell rect
    // (cx, cz, w, h): water/hover grids never change post-setup so their fields
    // always survive, and a ground field survives when the rect -- padded by its
    // footprint reach -- touches none of its reachable cells. Replaces the old
    // clear-everything (which forced a burst of full-map Dijkstra rebuilds on
    // every building placed, cancelled, reclaimed, or decayed).
    void invalidateFlows(int cx, int cz, int w, int h);
    mutable std::map<long long, FlowField> flowCache_;

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
    // As forEachNear, but stops after `maxVisits` candidates. Deterministic (the grid
    // iteration order is fixed), so a lockstep-safe density cap: in an overcrowded cell
    // a unit only needs to interact with a bounded number of the nearest others (e.g.
    // the separation push is dominated by the closest neighbours). maxVisits <= 0 = all.
    template <class F>
    void forEachNearCapped(float x, float z, float radius, int maxVisits, F&& fn) const {
        if (gW_ <= 0) return;
        if (maxVisits <= 0) { forEachNear(x, z, radius, std::forward<F>(fn)); return; }
        int r = int(radius / gCell_) + 1;
        int cx = int((x - gOx_) / gCell_), cz = int((z - gOz_) / gCell_);
        int seen = 0;
        for (int dz = -r; dz <= r; ++dz) {
            int gz = cz + dz;
            if (gz < 0 || gz >= gH_) continue;
            for (int dx = -r; dx <= r; ++dx) {
                int gx = cx + dx;
                if (gx < 0 || gx >= gW_) continue;
                for (int i = gHead_[size_t(gz) * gW_ + gx]; i >= 0; i = gNext_[size_t(i)]) {
                    fn(i);
                    if (++seen >= maxVisits) return;
                }
            }
        }
    }
    std::vector<int> gHead_, gNext_;
    int gW_ = 0, gH_ = 0;
    float gCell_ = 32.0f, gOx_ = 0, gOz_ = 0;

    std::vector<uint8_t> vis_;
    int visPlayer_ = 0;
    bool fogExplored_ = true;   // keep seen cells dimmed (vs. reverting to dark); display-only
    int visW_ = 0, visH_ = 0;
    // Client-local fog acceleration: the LoS-tested cell set for a (sight, radar,
    // cell) combination is a pure function of the IMMUTABLE heightmap, so it is
    // computed once and re-stamped while a unit stands on that cell -- the O(r^3)
    // ray-march that used to recompute every 0.25s made 300-600-unit armies cost
    // 11-66ms per fog pass. Display-only: never hashed, and the headless referee
    // (visPlayer_ < 0) skips the fog pass entirely.
    std::unordered_map<uint64_t, std::vector<uint32_t>> visMaskCache_;
    // Bumped after every fog recompute so the renderer can skip re-uploading an
    // unchanged fog texture (vis_ changes at 4Hz; frames render far faster).
    uint32_t visGen_ = 0;
    float visTimer_ = 0;
    std::vector<uint8_t> heights_;   // raw TNT heightmap, for fog line-of-sight
    int hW_ = 0, hH_ = 0;
    // True if the ground at cell (tx,tz) is visible from a unit at cell (ux,uz):
    // no intervening terrain rises above the eye->target sight line. Local display
    // only (fog is not in stateHash), so plain float math is fine.
    bool sightClear(int ux, int uz, float eyeH, int tx, int tz) const;
    std::vector<Unit> units_;
    std::unique_ptr<MissionScript> mission_;   // optional campaign mission runner
    std::unique_ptr<ScenarioScript> scenario_; // optional .crt trigger runner
    std::vector<uint8_t> forcedDefeat_;        // scenario Victory/Defeat: forced-defeated slots
    std::vector<int> justDied_;                // unit ids that died this tick (mission/scenario hook)
    std::vector<std::pair<float, float>> manaSpots_;
    std::vector<Feature> features_;             // reclaimable map features
    std::vector<FeatType> featTypes_;           // per-type burn data (setup-time, static)
    std::unordered_map<const UnitType*, int> corpseType_;   // unit -> corpse FeatType
    std::unordered_map<const UnitType*, int> stoneType_, frozenType_;   // statue defs
    // Burn RNG: retail rolls spread on its game LCG (icd 0x535cc0, Lehmer 16807);
    // ours is identical on every peer -- draws happen only in deterministic sim
    // paths, and the state is folded into stateHash.
    uint32_t burnRng_ = 0x54414B21;
    int burnRand(int n) {
        burnRng_ = uint32_t((uint64_t(burnRng_) * 16807ULL) % 0x7FFFFFFFULL);
        return n > 0 ? int(burnRng_ % uint32_t(n)) : 0;
    }
    void igniteFeature(Feature& f);
    void swapFeature(Feature& f, int newType);   // chain stage swap (burnt/dead)
    void tickBurning();
    std::unordered_map<int, size_t> featureIdx_;   // feature id -> index in features_
    std::vector<Projectile> projectiles_;
    std::vector<HitFx> hits_;
    // [EXPLODEAS] blasts queued during the death sweep and applied just after it
    // (applyHit mutates units_, which the sweep is walking). Transient within one
    // tick -- always empty at tick end, so it needs no hashing.
    struct DeathBlast { const Weapon* w; float x, z; int player, fromId; };
    std::vector<DeathBlast> deathBlasts_;
    // Remote Effect (FBI type=Remote Effect): the effect materialises at the AIMED
    // GROUND POINT after `builduptime`, then applies its damage/status/conversion
    // once over areaofeffect. Earthquakes, monarch waves, god spells, Area Mind
    // Control. Lives across ticks, so it IS hashed.
    struct PendingEffect {
        const Weapon* w = nullptr;
        float x = 0, z = 0;
        int player = 0, fromId = 0;
        float at = 0;        // seconds until the next pulse
        float endAt = 0;     // seconds until the effect expires
        float period = 0;    // seconds between pulses (<=0 = a single pulse)
        bool casterGated = false;   // dies with its caster (mind control / freeze)
    };
    std::vector<PendingEffect> pendingEffects_;
    // Wandering (FBI type=wandering): a storm entity that roams for `duration`,
    // drifting at weaponvelocity and wobbling its heading every `variationtime`.
    // Damages everything it passes on a fixed cadence. Hashed.
    std::vector<Storm> storms_;
    int stormSeq_ = 0;        // id source, so the viewer can track a storm's life
    std::vector<Player> players_ = []{
        std::vector<Player> v(4);
        for (int i = 0; i < 4; ++i) v[size_t(i)].team = i;
        return v;
    }();
    int winningTeam_ = -1;
    bool monarchExpendable_ = true;      // default: Monarch is just a unit (net option overrides)
    bool serialFlow_ = false;            // true = flow prefetch stays on this thread (server parallel-games mode)
    std::vector<uint8_t> hadMonarch_;   // per-player: ever fielded a Monarch (for the loss rule)
    bool godsEnabled_ = false;
    int unitCap_ = 0;                 // per-player live-unit limit (0 = unlimited)
    float godAppearTime_ = 1e9f, clock_ = 0;
    uint32_t tickCounter_ = 0;   // ticks elapsed; staggers per-unit auto-acquisition
    std::vector<BenchStage> benchPlan_;   // benchmark staged spawns (executed in tick)
    size_t benchCursor_ = 0;              // next unexecuted stage
    uint32_t benchEndTick_ = 0;           // 0 = not a benchmark run
    uint32_t acqStride_ = 4;     // auto-acquire re-scan period, widened with crowd size
                                 // (deterministic: derived from the live-unit count)
    uint32_t flowQuantShift_ = 1;// flow-goal block = 2^shift cells; coarsens with the
                                 // crowd so a huge battle shares fewer distinct fields
    int pathBudget_ = 0;         // A* repaths still allowed this tick (crowd throttle)
    NavGrid nav_, navWater_, navHover_;
    // Per-cell terrain metrics (16px cells) for per-unit passability limits.
    std::vector<uint8_t> slope_;   // local height spread
    std::vector<uint8_t> depth_;   // water depth (sea level - height), 0 on land
    std::vector<uint8_t> roads_;   // 1 = road cell (feature-plane 0xFFFB); may be empty
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
