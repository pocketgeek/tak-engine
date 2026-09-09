#pragma once

// Per-tick render SNAPSHOT of the sim (sim/render decouple -- see
// docs/sim-render-decouple-plan.md): UnitR / PlayerR mirror sim::Unit / sim::Player
// field names so render code reads them unchanged, and Frame is the whole triple-
// buffered snapshot the render/HUD reads instead of live world_. Extracted from
// client/main.cpp; kept at global scope so its unqualified use sites there are
// unchanged.

#include "sim/sim.h"   // tak::sim::UnitType / Order / BuildOrder / Projectile / World::HitFx

#include <array>
#include <cstdint>
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
    float hp = 0, mana = 0;
    int veteran = 0;
    float deadFor = -1;
    int inTransport = 0;
    int8_t squad = 0;
    int stance = 1;
    int weaponSlot = 0;
    bool underConstruction = false, buildBegun = false;
    bool cloaked = false, cloakOn = true, active = true;
    float frozenFor = 0, stonedFor = 0, paralyzedFor = 0;
    int buildSiteId = 0, reclaimId = 0, repairId = 0;
    float buildProgress = 0;
    std::vector<const tak::sim::UnitType*> buildQueue;
    std::vector<tak::sim::Order> orders;
    std::vector<tak::sim::BuildOrder> buildOrders;
    std::vector<int> cargo;
    std::vector<int> reclaimQueue;   // builder's queued area-reclaim feature ids
    const tak::sim::UnitType* repeatType = nullptr;
    bool moving_ = false, walking_ = false;   // cached u.moving()/u.walking()
    float speed = 0;                           // px/s (diagnostic use)
    bool justFired = false;                    // one-tick: fired a weapon this tick
    int justBuilt = 0;                         // one-tick: unit id produced this tick, else 0
    bool disco = false, headbang = false;      // cached world_.disco/headbangActive(player)
    bool alliedToLocal = false;                // cached alliedToLocal(player)
    bool alive() const { return deadFor < 0; }
    bool embarked() const { return inTransport != 0; }
    bool moving() const { return moving_; }
    bool walking() const { return walking_; }
};

// Per-tick render snapshot of a sim Player (mirrors sim::Player's read field names, like
// UnitR). Captured each tick so the HUD/scoreboard never reads live world_ players.
struct PlayerR {
    float mana = 0, storage = 0, income = 0, godFavor = 0;
    int kills = 0, unitCount = 0, team = 0;
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
    std::vector<tak::sim::Projectile> projectiles;
    std::vector<tak::sim::World::HitFx> hits;   // weapon impacts this tick (cosmeticStep FX)
    int winningTeam = -1;                // world_.winningTeam() (victory overlay)
    uint32_t gameTick = 0;               // world_.tickCount() (benchmark timing)
    uint64_t tickMs = 0;                 // wall-clock of this tick (for interpolation)
    float tickDurMs = 1000.0f / 30.0f;
    uint32_t gen = 0;                    // capture generation (UnitR.gen == this => live this tick)
};
