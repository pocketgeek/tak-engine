#pragma once

// The skirmish AI, extracted from the SDL viewer so it can also run headless on
// the multiplayer server. A Controller reads a const World and EMITS net::Commands
// through a sink instead of mutating the world directly -- so the same AI drives
// a player whether the commands are applied in-process (single-player) or fed into
// the server's command sequencer (server-hosted AI). See docs/multiplayer-design.md.
//
// The AI runs ONLY on the server (the referee), never on clients: its *decisions*
// are server-local, and only the net::Commands it emits are relayed and applied by
// every peer -- so lockstep never depends on the AI being reproducible. We still
// keep it RNG-driven so a --mpai run is repeatable for testing.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "net/lockstep.h"
#include "sim/sim.h"

namespace tak::ai {

class ClassicBrain;   // ai/classicbrain.h: the Classic level's brain
struct ClassicProfileScript;   // ai/classicprofile.h

// Retail AI profile weights and policy limits. Normal/Easy retain scaled limits;
// sustained-growth modes raise them to their force/production targets. A zero
// limit remains disabled; -1 is unlimited. Actual UnitType limits always apply.
// Missing weight => the AI never builds that unit.
struct Profile {
    std::unordered_map<std::string, int> weight, limit;
};

// Parse a game's ai/<name>.txt (else ai/default.txt) from `vfs` in the
// builtinprofile.h line format. One file covers every faction. Never throws; a
// missing file yields an empty profile (the AI then builds nothing). Our levels
// never read these files -- they play their built-in tables (builtinProfile) --
// so this serves the campaign missions' named profiles and the check that the
// built-in tables still reproduce the shipped default profile.
Profile loadProfile(const tak::hpi::Vfs& vfs, const std::string& name = "default");

// Opponent skill, loosely modelled on retail's easy/normal/hard. It scales HOW the
// AI plays (economy pace, army size before it commits, aggression, reaction rate)
// rather than cheating its economy -- so all three respect the same rules the human
// does. See paramsFor().
// The value is the wire aiLevel (SlotInfo.aiLevel); 0-4 run weakest -> strongest.
// Passive (displayed as Defensive) builds a home force and intercepts nearby
// threats, without scouting or attacking enemy bases, and keeps building in step
// with its income with no AI-imposed limit on how much. Absurd = Hard with a
// mana-income cheat (see incomeMultFor). AbsurdlyDefensive (5, added under protocol
// 273) = Defensive with Absurd's cheat. Classic (6, added under protocol 290) is a
// port of the retail game's own AI (docs/classic-ai-plan.md): a separate brain
// (ClassicBrain) with no difficulty knobs and no income cheat, so the sim gives its
// seat exactly a Normal seat's config. Renumbering is versioned by kNetVersion.
enum class Difficulty : uint8_t {
    Passive = 0, Easy = 1, Normal = 2, Hard = 3, Absurd = 4, AbsurdlyDefensive = 5, Classic = 6
};
inline constexpr uint8_t kAiLevels = 7;

// Map a wire aiLevel to a Difficulty, clamping anything out of range to Normal.
inline Difficulty difficultyFromLevel(uint8_t lvl) {
    return lvl < kAiLevels ? Difficulty(lvl) : Difficulty::Normal;
}

// The defensive levels: no offensive waves, a non-pursuing stance from birth
// (MatchSlot::defensiveAi), and production that follows income without caps.
inline constexpr bool defensiveLevel(Difficulty d) {
    return d == Difficulty::Passive || d == Difficulty::AbsurdlyDefensive;
}

// Per-player mana-income multiplier for a difficulty. This is the ONE economy cheat:
// Absurd and Absurdly Defensive earn double from every income source. It scales
// HASHED sim state (mana), so every peer must apply the same factor to the same
// player -- it is derived from the broadcast aiLevel and set on the sim player at
// match setup, never AI-local.
inline constexpr float incomeMultFor(Difficulty d) {
    return d == Difficulty::Absurd || d == Difficulty::AbsurdlyDefensive ? 2.0f : 1.0f;
}

// Behaviour knobs derived from Difficulty (paramsFor).
struct DiffParams {
    int  thinkPeriod;      // sim ticks between decisions (30 = 1 Hz); lower = faster reactions
    int  waveSize;         // base army size to gather before an attack; the actual "big
                           // push" threshold scales UP with mana income (a rich AI masses
                           // a larger army, a poor one strikes with what it has)
    int  producersPerThink;// how many idle producers act each think (economy/APM pace)
    int  limitScale;       // percent applied to the profile's unit limits (100 = as shipped)
    bool scout;            // send an early lone scout toward an enemy start
    bool attack;           // commit offensive waves (Defensive only intercepts local threats)
    int  raidSize;         // fighters peeled off for a small harassing raid while the main
                           // army musters (0 = no raiding; gated on `scout` difficulties)
    bool unbounded;        // no AI-imposed caps: the planner's targets scale with income
                           // without ceilings, profile limits do not apply, and spending
                           // is paced by mana alone (the defensive levels)
};
DiffParams paramsFor(Difficulty d);

// What a buildable unit is FOR, derived from its UnitType (faction-agnostic) so the
// planner can balance an army instead of drawing types blindly. See Controller::categoryOf.
enum class BuildCat { Economy, Factory, Builder, Army, Defense };

// The empire's current shape, assessed once per think. The planner compares these to
// simple targets to decide which category a producer should build next -- so the AI
// bootstraps an economy, adds factories to spend its income, keeps only a few builders,
// and then pours the rest into army, rather than letting a weighted-random draw spiral
// into all-economy / all-builders / no-soldiers (the old seed-fragile failure).
struct Needs {
    float income = 0;          // actual spendable income, including difficulty bonus
    // Own live units per type, filled in the same assessNeeds pass -- weightedPick's
    // limit checks read this instead of re-scanning all units per menu entry.
    std::unordered_map<const tak::sim::UnitType*, int> counts;
    std::unordered_map<const tak::sim::UnitType*, int> unfinished; // of counts, still under construction
    int   economyProjects = 0; // producers committed to income/storage construction
    int   lodestoneProjects = 0; // builders whose job is a lodestone (onMana), placed or queued
    int   population = 0;      // includes construction and this think's planned units
    int   economy = 0;         // count: income/storage structures
    int   factories = 0;       // count: static or mobile production units
    int   navalFactories = 0;  // dedicated shipyards, including construction
    int   navalProducers = 0;  // shipyards or mobile conjurers with a water-unit menu
    int   navalArmy = 0;
    int   builders = 0;        // count: mobile builders (incl. the Monarch)
    int   defenses = 0;
    int   desiredArmy = 0;
    int   desiredDefenses = 1;
    int   army = 0;            // count: mobile combatants
    int   builderCap = 2;      // stop making builders past this, the Monarch included
    int   desiredFactories = 1;// how many factories the current income wants to feed;
                               // a side with no factory buildings adds its builder allowance
    int   mobileBuilders = 0;  // every builder=1 mobile unit of either category, the
                               // Monarch, those unfinished and those queued or ordered
    int   mobileBuilderCap = 2;// builderCap; a side with no factory buildings adds its factories
    bool  storeFull = false;   // mana at 90% of capacity or more: income goes unspent
    float committed = 0;       // unbounded levels: build cost ordered this think, held
                               // against mana by the affordability gate
};

// Sink for the commands a Controller decides to issue this tick. Offline this
// applies them immediately; on the server it queues them into the tick sequencer.
using CommandSink = std::function<void(const tak::net::Command&)>;

// One AI brain, driving a single player.
class Controller {
public:
    // enemyStarts: the start positions of the players this AI is NOT allied with, so
    // it can march on a base under fog before it has actually spotted enemy units.
    Controller(int player, const tak::sim::TypeRegistry& registry,
               const Profile& profile, uint32_t seed,
               Difficulty difficulty = Difficulty::Normal,
               std::vector<std::pair<float, float>> enemyStarts = {});
    Controller(Controller&&) noexcept;
    ~Controller();

    // Evaluate the AI for sim tick `simTick`. Does nothing off its think cadence.
    // A Classic controller hands every tick to its ClassicBrain instead.
    // Reads `world` (never mutates it) and emits any orders through `sink`.
    void tick(const tak::sim::World& world, uint32_t simTick, const CommandSink& sink);

    // Difficulty::Classic only (a no-op otherwise): the AI profile script the
    // ClassicBrain bakes its per-type preferences and limits from -- the map's
    // aiprofile file, chosen by the server (ai/classicprofile.h). Without one the
    // brain keeps retail's defaults (every type 100 / unlimited).
    void setClassicProfile(const ClassicProfileScript& script);
    // Difficulty::Classic only (a no-op otherwise): this seat's start location,
    // start number `index` at (x, z) pixels, which the brain anchors its home
    // base on (ClassicBrain::setStart).
    void setClassicStart(int index, float x, float z);
    // Difficulty::Classic only: every start location of the map, in start order
    // (ClassicBrain::setStartTable); call it before setClassicStart.
    void setClassicStarts(const std::vector<std::pair<float, float>>& positions);
    // Difficulty::Classic only: this player is a campaign mission's computer
    // player (retail's scenario mode 1: the first assignment pass's pre-placed
    // rules, base 1 not anchored on a start); `otaPlayer` is its .ota Player<N>
    // value, whose `massattack <n>` keyword (if any) starts the mass attack at
    // n x 30 ticks (classicMassAttackTicks).
    void setClassicCampaign(const std::string& otaPlayer);
    const ClassicBrain* classicBrain() const { return classic_.get(); }

    // Read-only identity, for the debug roster dump (server/rosterdump.h).
    int player() const { return player_; }
    Difficulty difficulty() const { return diff_; }

private:
    friend struct PlacementProbe;
    std::vector<std::pair<float,float>> plannedManaSites_; // reservations until emitted orders reach the world
    // Unbounded levels: building sites already spoken for -- ordered this think
    // (plannedSites_), or ordered earlier and refused when the builder got there
    // (refusedSites_, held for kRefusedSiteTicks; issuedBuilds_ watches each order).
    struct PlannedSite { const tak::sim::UnitType* type; float x, z; };
    std::vector<PlannedSite> plannedSites_;
    struct IssuedBuild { int unit; const tak::sim::UnitType* type; float x, z; uint32_t tick; };
    std::vector<IssuedBuild> issuedBuilds_;
    struct RefusedSite { float x, z; uint32_t until; };
    std::vector<RefusedSite> refusedSites_;
    void updateIssuedBuilds(const tak::sim::World&, uint32_t simTick);
    // --- deterministic RNG (retail-style LCG) --------------------------------
    int rand(int n) {
        rng_ = rng_ * 1103515245u + 12345u;
        return n > 0 ? int((rng_ >> 16) % uint32_t(n)) : 0;
    }

    // --- decision helpers (all read-only over the world) ---------------------
    // Needs-based build planner: assess the empire, score each category against its
    // target, and let a producer build the most-needed thing its menu offers.
    Needs assessNeeds(const tak::sim::World&) const;
    BuildCat categoryOf(const tak::sim::UnitType*) const;
    // True for a side with no factory buildings (Zhon): its mobile builders do the
    // production other sides' factories do as well as the construction.
    bool mobileProduction(const std::string& side) const;
    mutable std::unordered_map<std::string,bool> mobileProduction_;
    bool navalFactory(const tak::sim::UnitType*) const;
    bool navalProducer(const tak::sim::UnitType*) const;
    const std::vector<std::pair<float,float>>& navalLaunchOffsets(const tak::sim::UnitType*) const;
    bool navalLaunchClear(const tak::sim::World&,const tak::sim::UnitType* factory,
                          const tak::sim::UnitType* ship,float x,float z,float dx,float dz) const;
    int   desire(BuildCat, const Needs&) const;
    // Retail's lodestone trigger: short of mana, with no lodestone under way.
    bool  manaShort(const tak::sim::World&, const Needs&) const;
    // Retail's base radius around home for that lodestone's deposit (icd 0x40cf30).
    float retailLodestoneRadius(const tak::sim::World&, const tak::sim::Unit& builder) const;
    // `excludeCats` is a bitmask of 1<<int(BuildCat): categories already tried and
    // found unproducible this think, so the pick falls through to the next best.
    // `lodestoneOnly` limits the pick to the lodestone manaShort() asks for.
    // `siteLimit`, when given, receives the radius from home the pick's site must
    // lie within (retail's base radius for the trigger's lodestone), else 0.
    const tak::sim::UnitType* weightedPick(const tak::sim::World&,
                                           const tak::sim::Unit& producer, const Needs&,
                                           int excludeCats = 0, bool lodestoneOnly = false,
                                           float* siteLimit = nullptr);
    // True if an order was actually emitted. False means the pick could not be
    // acted on (no build site, nowhere to conjure) -- the caller then retries in
    // another category rather than burning the producer's turn on it.
    bool produce(const tak::sim::World&, const tak::sim::Unit& producer,
                 const tak::sim::UnitType* pick, const CommandSink&, float siteLimit = 0);
    // `limit` > 0 narrows the radius from home a site may lie within.
    bool placeSite(const tak::sim::World&, const tak::sim::UnitType*, const tak::sim::Unit& builder, float nx, float nz,
                   float& outX, float& outZ, float limit = 0) const;
    // Fog of war for the AI: an enemy is targeted only when one of the AI's own units
    // is within its sight/radar. For direction (before anything is spotted) the AI
    // falls back to the known enemy start positions.
    bool nearestVisibleEnemy(const tak::sim::World&, float cx, float cz,
                             const tak::sim::UnitType* atype, float& tx, float& tz) const;
    bool nearestEnemyStart(float cx, float cz, float& tx, float& tz) const;
    // (x,z) lies nearer some enemy start than home (hx,hz): the enemy's side of the map.
    bool enemySide(float x, float z, float hx, float hz) const;
    bool reachablePoint(const tak::sim::World&, const tak::sim::UnitType*,
                        float fx, float fz, float tx, float tz, float& x, float& z) const;
    int terrainWeight(const tak::sim::World&, const tak::sim::Unit&,
                      const tak::sim::UnitType*, int weight) const;
    void sendWaves(const tak::sim::World&, uint32_t simTick, const CommandSink&);
    void sendDefenders(const tak::sim::World&, const CommandSink&);
    // Unbounded levels: walk idle fighters out of the base to a ring around it.
    void stationArmy(const tak::sim::World&, std::vector<int>& assigned, const CommandSink&);
    // The initial base anchor stays fixed as expansion adds remote buildings.
    // The Monarch and defensive AI use it to remain near home.
    std::pair<float, float> homeOf(const tak::sim::World&) const;

    // A fighter is free to be committed to a wave when it's idle or only doing a plain
    // move -- NOT while it's already fight-moving or attacking (so re-commanding it each
    // think doesn't reset its march and thrash it in place).
    static bool waveFree(const tak::sim::Unit& u) {
        return u.orders.empty() ||
               (u.orders.front().targetId == 0 && !u.orders.front().attackMove && !u.orders.front().buildType &&
                !u.orders.front().load && !u.orders.front().unload && !u.orders.front().guard);
    }
    void emit(const CommandSink& sink, tak::net::Cmd kind, int unitId,
              const std::string& type, float x, float z) const;

    int player_;
    const tak::sim::TypeRegistry& registry_;
    const Profile& profile_;
    uint32_t rng_;
    Difficulty diff_;
    DiffParams dp_;
    std::vector<std::pair<float, float>> enemyStarts_;
    std::optional<std::pair<float,float>> home_;
    // Allied players' home anchors, fixed at the first think like home_ (homeOf's
    // rule applied to each ally). Expansion leaves a living ally's home deposits
    // to that ally while it can still claim them, then to the nearest other ally
    // (updateAllyClaims).
    struct AllyHome { int player; float x, z; };
    std::vector<AllyHome> allyHomes_;
    bool allyHomesKnown_ = false;
    // Per mana deposit (World::manaSpots() index): the last think tick it was
    // claimed -- a lodestone on it or a builder bound for it -- and whether this
    // think leaves it to an ally.
    std::vector<uint32_t> depositClaimedAt_;
    std::vector<uint8_t> allyHeldDeposits_;
    void updateAllyClaims(const tak::sim::World&, uint32_t simTick);
    // Each allied builder whose orders hold a deposit, by unit id: the point of
    // the task it is on now (its build site, or its leg's goal), the nearest it
    // has come to it, and the think tick it last came 64 px nearer than that or
    // was at work. `stalled` once it has not for a minute: it then holds no
    // deposit, and this AI stops its own (updateBuilderProgress) and never sends
    // that builder back to them.
    struct BuilderProgress { float siteX, siteZ, best; uint32_t gainedAt; bool stalled; };
    std::unordered_map<int, BuilderProgress> builderProgress_;
    struct StalledSite { int unit; float x, z; };
    std::vector<StalledSite> stalledSites_;
    void updateBuilderProgress(const tak::sim::World&, uint32_t simTick, const CommandSink&);
    bool stalledBuilder(int unitId) const;
    bool buildsLodestone(const tak::sim::UnitType*) const;
    mutable std::unordered_map<const tak::sim::UnitType*,bool> lodestoneBuilders_;
    struct DefenseResponse {
        float homeX, homeZ, goalX, goalZ;
        bool returning = false;
        int targetId = 0;   // melee defenders chase one attacker with a direct Attack
        bool ownAttacker = false;   // targetId is the unit that was hitting this one
    };
    // Server-local assignments, not simulation state. Commands still use the
    // ordinary sequencer and movement controllers on every peer.
    std::unordered_map<int, DefenseResponse> defenders_;
    mutable std::unordered_map<const tak::sim::UnitType*,std::vector<std::pair<float,float>>> navalOffsets_;
    // Unbounded levels: a factory whose queue head has waited kQueueStallTicks
    // without a site (its output pad blocked, say) has it unqueued, and that factory
    // trains anything but that type for kRefusedTicks (refused_).
    struct QueueWait { const tak::sim::UnitType* type; uint32_t since; };
    std::unordered_map<int, QueueWait> queueWait_;
    std::unordered_map<int, QueueWait> refused_;   // since = the tick it may try again
    void unqueueStalled(const tak::sim::World&, uint32_t simTick, const CommandSink&);
    // Difficulty::Classic only: the retail-AI port that drives this player in place
    // of everything above (which then stays idle).
    std::unique_ptr<ClassicBrain> classic_;
    uint32_t now_ = 0;
    int raidersSincePush_ = 0;
    bool scouted_ = false;        // one-shot early scout sent
    uint32_t lastRaidTick_ = 0;   // last tick a harassing raid was sent (raid cooldown)
};

}  // namespace tak::ai
