#include "ai/ai.h"

#include "hpi/hpi.h"
#include "sim/detmath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

namespace tak::ai {

Profile loadProfile(const tak::hpi::Vfs& vfs) {
    Profile prof;
    if (!vfs.has("ai/default.txt")) return prof;
    auto b = vfs.read("ai/default.txt");
    std::istringstream f(std::string(b.begin(), b.end()));
    std::string kw, unit;
    int v;
    for (std::string line; std::getline(f, line);) {
        if (line.size() < 2 || line[0] == '/') continue;
        std::istringstream ss(line);
        if (!(ss >> kw >> unit >> v)) continue;
        std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
        if (kw == "weight") prof.weight[unit] = v;
        else if (kw == "limit") prof.limit[unit] = v;
    }
    return prof;
}

DiffParams paramsFor(Difficulty d) {
    switch (d) {
        // Turtle: Easy's build-up, but never sends an attack wave -- it only defends
        // (idle units still auto-fire on anything that walks into range).
        case Difficulty::Passive: return {60, 4, 1, 70,  false, false};
        // Sluggish: reacts slowly, builds up slowly, and only commits once it has
        // gathered a sizeable group -- so it's passive and beatable.
        case Difficulty::Easy:   return {60, 4, 1, 70,  false, true};
        // Fast, army-heavy, and aggressive: reacts often, attacks with small groups,
        // and pushes bigger unit limits.
        case Difficulty::Hard:   return {20, 2, 8, 150, true,  true};
        // Hard's behaviour, plus a 2x income cheat applied to the sim (incomeMultFor).
        case Difficulty::Absurd: return {20, 2, 8, 150, true,  true};
        case Difficulty::Normal:
        default:                 return {30, 3, 3, 100, true,  true};
    }
}

Controller::Controller(int player, const tak::sim::TypeRegistry& registry,
                       const Profile& profile, uint32_t seed, Difficulty difficulty,
                       std::vector<std::pair<float, float>> enemyStarts)
    : player_(player), registry_(registry), profile_(profile),
      // The seed is taken as-is: the CALLER hands distinct seeds to distinct players
      // when it wants them to diverge from tick one (the lobby does, per game+player).
      // Same-seed controllers still diverge within a few ticks, because every RNG draw
      // is gated on that player's own unit counts / income / producers, which differ
      // by map position. The AI runs server-side only, so this RNG exists just to make
      // a --mpai run repeatable, not for lockstep.
      rng_(seed), diff_(difficulty), dp_(paramsFor(difficulty)),
      enemyStarts_(std::move(enemyStarts)) {}

void Controller::emit(const CommandSink& sink, tak::net::Cmd kind, int unitId,
                      const std::string& type, float x, float z) const {
    tak::net::Command c;
    c.kind = kind;
    c.player = uint8_t(player_);
    c.unitId = unitId;
    c.x = x;
    c.z = z;
    std::snprintf(c.type, sizeof c.type, "%s", type.c_str());
    sink(c);
}

int Controller::countOf(const tak::sim::World& world, const std::string& id) const {
    int n = 0;
    for (auto& u : world.units())
        if (u.player == player_ && u.type && u.alive() && u.type->id == id) ++n;
    return n;
}

// What is this unit FOR? Derived purely from its stats, so it works for every faction:
//   Economy  - a structure that makes/holds mana (lodestone, mana storage)
//   Factory  - a structure that trains units (keep, castle, hell)
//   Defense  - any other structure (towers, walls)
//   Builder  - a mobile unit that builds (Monarch, Dark Mason, priest)
//   Army     - any other mobile unit (the combatants)
BuildCat Controller::categoryOf(const tak::sim::UnitType* t) const {
    if (t->isStructure()) {
        if (!registry_.buildable(t->id).empty()) return BuildCat::Factory;
        if (t->income > 0 || t->storage > 0) return BuildCat::Economy;
        return BuildCat::Defense;
    }
    return t->isBuilder ? BuildCat::Builder : BuildCat::Army;
}

// Count the empire by category and set the targets the planner steers toward.
Needs Controller::assessNeeds(const tak::sim::World& world) const {
    Needs n;
    const auto& me = world.player(player_);
    n.income = me.income / std::max(me.manaMult, 1.0f);   // ignore an Absurd AI's cheat
    for (const auto& u : world.units()) {
        if (!u.alive() || u.player != player_ || !u.type) continue;
        switch (categoryOf(u.type)) {
            case BuildCat::Economy:  ++n.economy;   break;
            case BuildCat::Factory:  ++n.factories; break;
            case BuildCat::Builder:  ++n.builders;  break;
            case BuildCat::Army:     ++n.army;      break;
            case BuildCat::Defense:  break;
        }
    }
    // One factory per ~40 income so production can actually spend what we earn; a couple
    // of mobile builders is plenty (more just spiral the economy). Hard/Absurd run hotter.
    n.desiredFactories = std::clamp(int(n.income / 40.0f) + 1, 1, 8);
    n.builderCap = (diff_ == Difficulty::Hard || diff_ == Difficulty::Absurd) ? 3 : 2;
    return n;
}

// Priority of building one more of a category, given the empire's needs. Higher wins;
// 0 means "have enough, don't". The ladder: guarantee production, floor the economy,
// keep a few builders, grow economy/factories to match income, then army as the sink.
int Controller::desire(BuildCat c, const Needs& n) const {
    switch (c) {
        case BuildCat::Economy:
            // Bootstrap income BEFORE the pricey first factory -- building a 1700-mana
            // keep out of the opening treasury with no income starves everything after.
            if (n.income < 20.0f) return 95;
            return n.income < 25.0f + 20.0f * n.factories ? 60 : 0; // sustain the factories
        case BuildCat::Factory:
            if (n.factories == 0) return 90;                        // then: some production
            return n.factories < n.desiredFactories ? 70 : 0;       // scale with income
        case BuildCat::Builder:
            return n.builders < n.builderCap ? 65 : 0;              // a handful, then stop
        case BuildCat::Army:
            return 50;                                              // the default sink
        case BuildCat::Defense:
            return 0;                                               // (profile walls weight 0)
    }
    return 0;
}

// Pick what a producer should build: the most-needed category its menu can supply
// (desire()), then a weighted-random draw WITHIN that category (profile weights, for
// variety + retail flavour). A candidate must be affordable to finish and under its
// difficulty-scaled limit. Returns nullptr if nothing worth building is affordable now.
const tak::sim::UnitType* Controller::weightedPick(const tak::sim::World& world,
                                                   const tak::sim::Unit& producer,
                                                   const Needs& needs) {
    const auto& menu = registry_.buildable(producer.type->id);
    const auto& me = world.player(player_);
    float income = me.income / std::max(me.manaMult, 1.0f);   // plan against base income
    // A menu entry the AI may build right now: has a positive weight, is under its
    // limit, and savings + income over its build time cover the cost (so a builder
    // never traps itself on a site the mana runs dry beneath).
    auto usable = [&](const tak::sim::UnitType* ut) -> int {
        if (!ut) return 0;
        auto wi = profile_.weight.find(ut->id);
        int w = wi == profile_.weight.end() ? 0 : wi->second;
        if (w <= 0) return 0;
        auto li = profile_.limit.find(ut->id);
        int lim = li == profile_.limit.end() ? -1 : li->second;
        if (lim >= 0 && countOf(world, ut->id) >= std::max(1, lim * dp_.limitScale / 100))
            return 0;
        if (ut->buildTime > 0) {
            float secs = ut->buildTime / std::max(producer.type->workerTime, 1.0f);
            if (me.mana + income * secs < ut->buildCost) return 0;
        }
        return w;
    };
    // Pass 1: the highest desire among categories this producer can actually build now.
    int best = 0;
    for (const auto& id : menu) {
        const auto* ut = registry_.find(id);
        if (usable(ut) <= 0) continue;
        best = std::max(best, desire(categoryOf(ut), needs));
    }
    if (best <= 0) return nullptr;   // nothing needed is affordable -> wait (no spiral)
    // Pass 2: weighted-random among the usable entries in that top category.
    const tak::sim::UnitType* chosen = nullptr;
    int total = 0;
    for (const auto& id : menu) {
        const auto* ut = registry_.find(id);
        int w = usable(ut);
        if (w <= 0 || desire(categoryOf(ut), needs) != best) continue;
        total += w;
        if (rand(total) < w) chosen = ut;   // reservoir sample
    }
    return chosen;
}

// Turn a pick into a command: a factory (keep/castle) trains a mobile unit; a
// mobile builder places a structure or conjures a mobile unit near itself. The
// placement spot is probed against the (const) world, then issued as a Build.
void Controller::produce(const tak::sim::World& world, const tak::sim::Unit& p,
                         const tak::sim::UnitType* pick, const CommandSink& sink) {
    if (pick->isStructure()) {                  // structure
        if (!p.type->isStructure() && p.type->isBuilder) {
            float x, z;
            if (placeSite(world, pick, p.x, p.z, x, z))
                emit(sink, tak::net::Cmd::Build, p.id, pick->id, x, z);
        }
    } else if (p.type->isStructure()) {         // factory trains mobile
        emit(sink, tak::net::Cmd::Train, p.id, pick->id, 0, 0);
    } else if (p.type->isBuilder) {             // mobile builder conjures mobile
        for (float r = 40; r < 170; r += 20)
            for (float a = 0; a < 6.28f; a += 0.6f) {
                float x = p.x + detmath::cos(a) * r, z = p.z + detmath::sin(a) * r;
                if (world.canPlace(pick, x, z)) {
                    emit(sink, tak::net::Cmd::Build, p.id, pick->id, x, z);
                    return;
                }
            }
    }
}

// Find a build site for the AI: lodestones go on the nearest free mana deposit
// (when the map has any), everything else probes outward from the builder.
// Returns true and the chosen (outX,outZ) if a spot was found.
bool Controller::placeSite(const tak::sim::World& world, const tak::sim::UnitType* t,
                           float nx, float nz, float& outX, float& outZ) const {
    if (!t) return false;
    if (t->onMana && world.hasManaSpots()) {
        float bestD = 1e18f;
        bool found = false;
        for (const auto& [sx, sz] : world.manaSpots()) {
            if (!world.canPlace(t, sx, sz)) continue;   // taken or blocked
            float dx = sx - nx, dz = sz - nz, d = dx * dx + dz * dz;
            if (d < bestD) { bestD = d; outX = sx; outZ = sz; found = true; }
        }
        return found;
    }
    for (float r = 70; r < 340; r += 30)
        for (float a = 0; a < 6.28f; a += 0.5f) {
            float x = nx + detmath::cos(a) * r, z = nz + detmath::sin(a) * r;
            if (world.canPlace(t, x, z)) { outX = x; outZ = z; return true; }
        }
    return false;
}

// Nearest enemy the group at (cx,cz) can SEE (some AI unit within sight/radar of it)
// AND can actually REACH (flow connectivity), scanning closest-first. Fog: the AI
// never targets a unit it hasn't spotted; reachability keeps it off walled-in foes.
bool Controller::nearestVisibleEnemy(const tak::sim::World& world, float cx, float cz,
                                     const tak::sim::UnitType* atype,
                                     float& tx, float& tz) const {
    // My eyes: each own unit reveals a radius of max(sight, radar) around itself.
    struct Eye { float x, z, r2; };
    std::vector<Eye> eyes;
    for (auto& u : world.units())
        if (u.alive() && u.player == player_ && u.type && !u.embarked()) {
            float s = std::max(u.type->sight, u.type->radar);
            eyes.push_back({u.x, u.z, s * s});
        }
    if (eyes.empty()) return false;
    std::vector<std::pair<float, std::pair<float, float>>> vis;
    for (auto& e : world.units()) {
        if (!e.alive() || e.embarked() || world.allied(e.player, player_) || !e.type)
            continue;
        bool seen = false;
        for (const Eye& eye : eyes) {
            float dx = e.x - eye.x, dz = e.z - eye.z;
            if (dx * dx + dz * dz <= eye.r2) { seen = true; break; }
        }
        if (!seen) continue;   // fogged: we haven't spotted this one
        float dx = e.x - cx, dz = e.z - cz;
        vis.push_back({dx * dx + dz * dz, {e.x, e.z}});
    }
    if (vis.empty()) return false;
    std::sort(vis.begin(), vis.end());
    int checked = 0;
    for (auto& e : vis) {
        if (++checked > 16) break;   // bound the reachability probes (flow builds)
        if (!atype || world.pathExists(atype, e.second.first, e.second.second, cx, cz)) {
            tx = e.second.first; tz = e.second.second;
            return true;
        }
    }
    return false;
}

// The nearest KNOWN enemy start position -- where the AI marches when fog hides the
// enemy, so the army pushes into a base (and spots its defenders) instead of idling.
bool Controller::nearestEnemyStart(float cx, float cz, float& tx, float& tz) const {
    float best = 1e18f;
    bool found = false;
    for (const auto& [x, z] : enemyStarts_) {
        float dx = x - cx, dz = z - cz, d = dx * dx + dz * dz;
        if (d < best) { best = d; tx = x; tz = z; found = true; }
    }
    return found;
}

// Pool idle (non-builder) fighters; once a strike force has gathered (waveSize, per
// difficulty), attack-move the whole group at one target so they share a flow field:
// the nearest enemy it can SEE, else the nearest enemy start (marching on the base).
// Also sends one early scout so the AI reveals + commits rather than turtling forever.
void Controller::sendWaves(const tak::sim::World& world, const CommandSink& sink) {
    if (!dp_.attack) return;   // Passive: never marches out; units defend in place.
    std::vector<int> idle;
    double sx = 0, sz = 0;
    const tak::sim::UnitType* atype = nullptr;
    for (auto& u : world.units())
        if (u.alive() && u.player == player_ && u.type && !u.type->isStructure() &&
            !u.type->isBuilder && waveFree(u)) {
            idle.push_back(u.id);
            sx += u.x; sz += u.z;
            if (!atype && !u.type->canFly) atype = u.type;
        }
    if (idle.empty()) return;
    float cx = float(sx / idle.size()), cz = float(sz / idle.size());

    // Objective: the nearest enemy we can actually SEE, else march on the nearest
    // known enemy base (which draws us forward and into its defenders).
    float tx = 0, tz = 0;
    if (!nearestVisibleEnemy(world, cx, cz, atype, tx, tz) &&
        !nearestEnemyStart(cx, cz, tx, tz))
        return;   // nothing seen and no known base to march on -> hold

    // Commit the whole force once it's big enough, once we've already committed (keep
    // the pressure on / reinforce), OR once the economy is tapped out -- so a mana-poor
    // position attacks with what it has instead of turtling forever.
    const auto& me = world.player(player_);
    bool tapped = me.mana < 200.0f && me.income < 40.0f;
    bool commit = int(idle.size()) >= dp_.waveSize || committed_ || tapped;
    if (!commit) {
        if (dp_.scout && !scouted_) {   // send one early scout to reveal + draw forward
            emit(sink, tak::net::Cmd::AttackMove, idle.front(), "", tx, tz);
            scouted_ = true;
        }
        return;
    }
    committed_ = true;
    for (int id : idle) emit(sink, tak::net::Cmd::AttackMove, id, "", tx, tz);
}

void Controller::tick(const tak::sim::World& world, uint32_t simTick,
                      const CommandSink& sink) {
    // Think cadence (per difficulty), staggered by player so several AIs don't all
    // fire on the same tick. Sim-tick driven so a --mpai run is repeatable. Hard
    // reacts more often (thinkPeriod=20) than Easy (60).
    if ((simTick % uint32_t(dp_.thinkPeriod)) != uint32_t(player_ % dp_.thinkPeriod)) return;
    if (world.player(player_).defeated) return;

    const Needs needs = assessNeeds(world);   // one empire assessment drives every producer
    // Snapshot the idle producers, then act on up to producersPerThink of them -- the
    // per-think cap is what paces the economy across difficulties (Easy builds one
    // thing per think, Hard many).
    std::vector<int> producers;
    for (auto& u : world.units()) {
        if (!u.alive() || u.player != player_ || !u.type) continue;
        // A building can carry canMove=1 in its FBI (the Keep, the Taros Hell), so
        // classify by isStructure() (maxVel), not canMove -- otherwise a canMove
        // building is mistaken for a mobile builder and issued Build orders it can't
        // honour. And never drive a producer that is itself still under construction.
        if (!u.type->isStructure() && u.type->isBuilder && !u.underConstruction &&
            u.orders.empty() && u.buildSiteId == 0)
            producers.push_back(u.id);                        // idle mobile builder
        else if (u.type->isStructure() && !u.underConstruction && u.buildQueue.empty() &&
                 !registry_.buildable(u.type->id).empty())
            producers.push_back(u.id);                        // idle factory
    }
    // Round-robin who acts when the per-think cap is smaller than the producer count
    // (Easy caps at 1): otherwise the lowest-id producer -- the Monarch -- takes the
    // only slot every think, so it keeps building economy and the factories never get
    // to train an army. Rotating by the think index gives each producer its turn.
    if (!producers.empty() && dp_.producersPerThink < int(producers.size())) {
        uint32_t rr = simTick / uint32_t(std::max(dp_.thinkPeriod, 1));
        std::rotate(producers.begin(),
                    producers.begin() + rr % uint32_t(producers.size()), producers.end());
    }
    // Over-commit throttle: while broke with construction already in progress, don't
    // start ANOTHER build. Concurrent builds share the one mana pool, so piling on more
    // while the treasury is empty starves them all and leaves half-built units that
    // never finish -- the "produces units then gets stuck" failure. Let the in-flight
    // ones finish (income flows into them) before starting the next.
    bool throttle = false;
    if (world.player(player_).mana < 50.0f)
        for (const auto& u : world.units())
            if (u.player == player_ && u.alive() && u.underConstruction) { throttle = true; break; }
    int acted = 0;
    if (!throttle)
        for (int pid : producers) {
            if (acted >= dp_.producersPerThink) break;
            const auto* p = world.unit(pid);
            if (!p || !p->alive()) continue;
            if (const auto* pick = weightedPick(world, *p, needs)) {
                produce(world, *p, pick, sink);
                ++acted;
            }
        }
    sendWaves(world, sink);
}

}  // namespace tak::ai
