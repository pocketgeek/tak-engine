#pragma once

// Per-tick render SNAPSHOT of the sim (sim/render decouple -- see
// docs/sim-render-decouple-plan.md): UnitR / PlayerR mirror sim::Unit / sim::Player
// field names so render code reads them unchanged, and Frame is the whole triple-
// buffered snapshot the render/HUD reads instead of live world_. Extracted from
// client/main.cpp; kept at global scope so its unqualified use sites there are
// unchanged.

#include "sim/retailanimationqueries.h"
#include "sim/sim.h"   // tak::sim::UnitType / Order / Projectile / World::HitFx

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// Per-tick render SNAPSHOT of a sim Unit (sim/render decouple -- see
// docs/sim-render-decouple-plan.md). Field names + methods MIRROR sim::Unit so render code
// reads them unchanged; captured each tick by GameView::captureFrame() so the render never
// dereferences world_. Also carries the motion-interpolation prev pose (px/pz/ph); the curr
// pose is x/z/heading (mirroring Unit). Indexed by unit id in Frame::units.
struct UnitR {
    float px = 0, pz = 0, ph = 0;   // previous-tick pose (for interpolation)
    bool  seeded = false;           // has a valid prev pose to interpolate from
    uint32_t gen = 0;               // captureFrame generation this record was last written
                                    // (== frame gen means the unit is live THIS tick)
    // --- snapshot of Unit's render-read surface (same NAMES as sim::Unit, so render code
    //     that reads u.<field> works unchanged once its parameter is a UnitR) ---
    int id = 0;
    const tak::sim::UnitType* type = nullptr;
    int player = 0;
    float x = 0, z = 0, heading = 0;   // current-tick pose
    uint16_t headingWord = 0;
    uint16_t bodyPitch=0,bodyRoll=0;
    std::array<int32_t,3> aimVelocity{};
    std::array<int32_t,3> worldPosition{}; // exact, unprojected position for script queries
    int turnSpeedPercent = 0;
    int turnReqBam = 0;                // tick's requested turn (BAM) for TurnDirection anim
    float hp = 0, mana = 0;
    int veteran = 0;
    float deadFor = -1;
    int inTransport = 0;
    int8_t squad = 0;
    int stance = 1;
    uint8_t standingOrder = 3;
    int weaponSlot = 0;
    std::array<int32_t,3> weaponReloads{};
    bool underConstruction = false, buildBegun = false;
    bool cloaked = false, cloakOn = true, active = true;
    float frozenFor = 0, stonedFor = 0, paralyzedFor = 0;
    float selfDestructT = -1;   // >=0 = self-destruct countdown (seconds) armed
    int buildSiteId = 0, productionSiteId = 0, reclaimId = 0, repairId = 0;
    struct ReclaimTarget { float x, z; int fx, fz; };
    std::optional<ReclaimTarget> reclaimTarget;
    bool conjuring = false;
    bool hasConstructionEmitter = false;
    std::array<uint32_t,2> constructionEmissions{};
    std::vector<tak::sim::RetailConstructionParticle> constructionParticles;
    float buildProgress = 0;
    int constructionPercentLeft = 0;
    int scriptHealthPercent = 100;
    bool yardOpen = false;
    std::vector<const tak::sim::UnitType*> buildQueue;
    std::vector<tak::sim::Order> orders;
    // Construction still pending anywhere in the queue (builds are ordinary
    // orders now, so this is just "is one of them a build").
    bool hasQueuedBuild() const {
        for (const auto& o : orders) if (o.buildType) return true;
        return false;
    }
    // Construction OR reclaim still queued.
    bool hasQueuedWork() const {
        for (const auto& o : orders)
            if (o.buildType || o.reclaimFeat || o.repairTarget) return true;
        return false;
    }
    std::vector<int> cargo;
    const tak::sim::UnitType* repeatType = nullptr;
    bool moving_ = false, walking_ = false;   // cached u.moving()/u.walking()
    float speed = 0;                           // px/s (diagnostic use)
    int horizontalSpeedPercent = 0;
    int verticalSpeedPercent = 0;
    float flightY = 0;                        // absolute height from the sim flight controller
    uint8_t flightGroundMode = 1;
    uint16_t movementTerrainFlags = 0; // native mover flags used by GET 28/34
    uint32_t animationOccupancy = 0;
    uint32_t animationMoveRate = 0;
    uint32_t flightBeginCallbackSerial = 0;
    uint32_t flightLandingCallbackSerial = 0;
    void captureMoveRate(const tak::sim::Unit& u,int16_t turn) {
        const auto multiplier=u.groundTerrainFlags&0x800 ? u.type->roadMult :
            u.groundTerrainFlags&0x1000 ? u.type->waterMult : tak::sim::Fixed::fromInt(1);
        const auto& v=u.flightVelocity;
        const int32_t horizontal=u.type->canFly ? int32_t(tak::sim::isqrt64(
            uint64_t(int64_t(v.x)*v.x)+uint64_t(int64_t(v.z)*v.z))) : u.speed.v;
        animationMoveRate=tak::sim::retailAnimationMoveRate(u.speed.v,turn,horizontal,
            (u.type->animationMoveRate1*multiplier).v,(u.type->animationMoveRate2*multiplier).v,
            movementRefused,u.embarked());
    }
    void captureOccupancy(const tak::sim::Unit& u,int sea,uint32_t previous) {
        animationOccupancy=u.type->maxVel>tak::sim::Fixed() ?
            tak::sim::retailAnimationOccupancy(previous,u.flightGroundMode,
                int16_t((u.type->canFly ? u.flightY : u.groundY).floorInt()),
                uint8_t(sea),uint8_t(u.type->waterline),int16_t(u.type->modelTop>>16)) : 0;
    }
    bool movementRefused = false;             // retail mover's repeated-refusal bit
    void captureMovement(const tak::sim::Unit& u) {
        moving_ = u.moving(); walking_ = u.walking();
        speed = u.speed.toFloat() * 30.0f;
        bodyPitch=u.groundPitch;bodyRoll=u.groundRoll;
        worldPosition={u.x.v,(u.type->canFly ? u.flightY : u.groundY).v,u.z.v};
        flightY = u.flightY.toFloat();
        flightGroundMode = u.flightGroundMode;
        flightBeginCallbackSerial = u.flightBeginCallbackSerial;
        flightLandingCallbackSerial = u.flightLandingCallbackSerial;
        movementTerrainFlags=u.groundTerrainFlags;
        movementRefused = !u.type->canFly && u.bodyBlockStreak >= 2;
        const auto multiplier=u.groundTerrainFlags&0x800 ? u.type->roadMult :
            u.groundTerrainFlags&0x1000 ? u.type->waterMult : tak::sim::Fixed::fromInt(1);
        const auto maximum=u.baseSpeed*multiplier;
        const auto step=tak::sim::retailGroundStep(u.heading,u.speed);
        aimVelocity=u.type->canFly ? std::array<int32_t,3>{u.flightVelocity.x,u.flightVelocity.y,u.flightVelocity.z}
                                  : std::array<int32_t,3>{step.s.v,0,step.c.v};
        horizontalSpeedPercent=tak::sim::retailHorizontalAnimationPercent(
            u.type->canFly ? u.flightVelocity.x : step.s.v,
            u.type->canFly ? u.flightVelocity.z : step.c.v,
            maximum.v,movementRefused,u.embarked());
        verticalSpeedPercent = u.type->canFly ? tak::sim::retailFlightVerticalPercent(
            u.flightVelocity.y,maximum.v,false,u.embarked()) : 0;
    }
    // Split the authoritative height for the existing body/shadow/effect paths.
    // While taking off below a nearby hill's clearance datum, that datum must
    // not lift the rendered body above the actual flight position.
    std::pair<float,float> flightRenderHeight(float datum, float reference) const {
        const float height=std::max(0.0f,flightY-reference);
        const float ground=std::min(std::max(0.0f,datum),height);
        return {ground,height-ground};
    }
    // Retail GET 29 (4dc100) hides retained controller speed after repeated
    // refusal. A unit waiting behind another body must not walk in place.
    float animationSpeed() const { return movementRefused || embarked() ? 0.0f : speed; }
    int32_t animationSpeedPercent() const {
        return movementRefused || embarked() ? 0 : horizontalSpeedPercent;
    }
    bool corpsePhase = false;                  // dead, death anim done, body still lies
    uint8_t deathType = 1;                     // killing blow damagetype (3 = gib)
    uint8_t severity = 0;                      // retail Killed severity (1..100)
    int corpseFeat = -1;                       // resolved corpse/statue FeatType index
    bool corpseStatue = false;                 // petrified/frozen: the body stays UPRIGHT,
                                               // unlike a normal corpse which lies flat
    bool justFired = false;                    // one-tick: fired a weapon this tick
    uint32_t firedWeapons = 0;
    uint32_t fireAnimations = 0;
    tak::RetailWeaponAnimations weaponAnimations;
    int justBuilt = 0;                         // one-tick: unit id produced this tick, else 0
    bool disco = false, headbang = false;      // cached world_.disco/headbangActive(player)
    bool alliedToLocal = false;                // cached alliedToLocal(player)
    bool alive() const { return deadFor < 0; }
    bool embarked() const { return inTransport != 0; }
    bool moving() const { return moving_; }
    bool walking() const { return walking_ && animationSpeed() > 0.0f; }
};

// Per-tick render snapshot of a sim Player (mirrors sim::Player's read field names, like
// UnitR). Captured each tick so the HUD/scoreboard never reads live world_ players.
struct PlayerR {
    float mana = 0, storage = 0, income = 0, expenditure = 0;
    void captureEconomy(const tak::sim::Player& player) {
        mana=float(player.mana);
        storage=player.retailResources ? player.retailResources->capacity
                                      : std::max(player.storage,100.0f);
        // Retail HUD calls 401330/401350: the oldest 29 tick samples,
        // scaled to mana/second. The current sample enters next tick.
        double produced=0,used=0;
        for (size_t i=0;i<29;++i) {
            produced+=player.displayResources.samples[i][0];
            used+=player.displayResources.samples[i][1];
        }
        income=float(produced*double(1.034482717514038f));
        expenditure=float(used*double(1.034482717514038f));
    }
    int manaBulbFrame(int count) const {
        if (count<=1 || storage<=0) return 0;
        // 4b221e truncates; it does not round up to the next fill level.
        return int(std::clamp(double(mana)/double(storage),0.0,1.0)*double(count-1));
    }
    int kills = 0, unitCount = 0, team = 0;
    int built = 0, losses = 0;      // end-of-game scoreboard counters
    float defeatedAt = -1;          // world clock at elimination (-1 = still in)
    bool defeated = false, godSummoned = false;
    float discoLeft = 0, headbangLeft = 0;
};

// A complete per-tick render snapshot -- everything the render/HUD reads from the sim.
// Triple-buffered (see frameBuf_): the render reads front(), the writer fills a spare and
// publishes it. `live` points into THIS Frame's `units`, so it swaps consistently.
struct Frame {
    std::vector<UnitR> units;            // indexed by unit id
    std::vector<const UnitR*> live;      // compact list of units live this tick (points into units)
    std::array<PlayerR, 8> players{};
    int numPlayers = 0;
    std::vector<uint8_t> vis;            // fog (empty for a noFog_ spectator)
    int visW = 0, visH = 0;
    uint32_t visGen = 0;
    std::vector<uint8_t> effectVisibility;
    int effectVisW = 0, effectVisH = 0;

    std::vector<tak::sim::Projectile> projectiles;
    std::vector<tak::sim::FlameShot> flames;
    std::vector<tak::sim::World::Storm> storms;   // roaming wandering-weapon hazards
    std::vector<tak::sim::World::HitFx> hits;   // weapon impacts this tick (cosmeticStep FX)
    int winningTeam = -1;                // world_.winningTeam() (victory overlay)
    tak::sim::RetailWind wind;
    uint32_t gameTick = 0;               // world_.tickCount() (benchmark timing)
    uint64_t tickMs = 0;                 // wall-clock of this tick (for interpolation)
    float tickDurMs = 1000.0f / 30.0f;
    uint32_t gen = 0;                    // capture generation (UnitR.gen == this => live this tick)
    // Mission-script one-shots, SNAPSHOTTED rather than read live. The render
    // thread used to reach into world_.shakeRequest()/soundRequest() directly,
    // which races the worker -- and soundRequest carries a std::string, so that
    // is a torn read of a heap pointer, not just a stale float.
    tak::sim::World::ShakeReq shakeReq;
    tak::sim::World::SoundReq soundReq;
};
