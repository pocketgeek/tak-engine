#include "ai/ai.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace tak::ai {

Profile loadProfile(const std::string& dataRoot, const std::string& ipRoot) {
    Profile prof;
    for (std::string path : {dataRoot + "/ai/default.txt",
                             ipRoot.empty() ? std::string() : ipRoot + "/ai/default.txt"}) {
        if (path.empty()) continue;
        std::ifstream f(path);
        if (!f) continue;
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
        break;
    }
    return prof;
}

Controller::Controller(int player, const tak::sim::TypeRegistry& registry,
                       const Profile& profile, uint32_t seed)
    : player_(player), registry_(registry), profile_(profile),
      // The seed is taken as-is: the CALLER is responsible for handing distinct
      // seeds to distinct players when it wants them to diverge from tick one
      // (the multiplayer lobby will, per game + player). Same-seed controllers
      // still diverge in practice within a few ticks anyway, because every RNG
      // draw is gated on that player's own unit counts / income / producers, which
      // differ by map position. Keeping it un-mangled means a single AI opponent
      // reproduces the pre-extraction behaviour exactly under the same seed.
      rng_(seed) {}

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

float Controller::manaRatio(const tak::sim::World& world) const {
    const auto& tm = world.player(player_);
    return tm.mana / std::max(tm.storage, 100.0f);
}

// Weighted-random pick over a producer's build menu (retail 0x412d00): a unit's
// weight is its probability share; anything at its limit is excluded; army units
// are scaled by econFactor (rich economy => more army). Skips structures the
// economy can't yet fund so a builder never traps itself on a stalled site.
const tak::sim::UnitType* Controller::weightedPick(const tak::sim::World& world,
                                                   const tak::sim::Unit& producer,
                                                   int econFactor) {
    const auto& menu = registry_.buildable(producer.type->id);
    const tak::sim::UnitType* chosen = nullptr;
    int total = 0;
    float income = world.player(player_).income;
    for (const auto& id : menu) {
        const auto* ut = registry_.find(id);
        if (!ut) continue;
        auto wi = profile_.weight.find(id);
        int w = wi == profile_.weight.end() ? 0 : wi->second;
        if (w <= 0) continue;
        auto li = profile_.limit.find(id);
        int lim = li == profile_.limit.end() ? -1 : li->second;
        if (lim >= 0 && countOf(world, id) >= lim) continue;
        bool economy = ut->income > 0 || ut->onMana;
        bool structure = !ut->canMove;
        // Don't start a non-economy building the economy can't yet drive: its
        // mogrium draw is buildCost*workerTime/buildTime (as the sim charges it).
        // This economy-first gate stands in for the tech progression our engine
        // lacks a formal tree for (lodestones raise income, unlocking keeps, then
        // the pricier castle).
        if (structure && !economy && ut->buildTime > 0) {
            float wt = std::max(producer.type->workerTime, 1.0f);
            float drain = ut->buildCost * wt / ut->buildTime;
            if (income < drain) continue;
        }
        if (ut->canMove && !ut->isBuilder) w *= econFactor;   // army: economy tweak
        total += w;
        if (rand(total) < w) chosen = ut;                     // reservoir sample
    }
    return chosen;
}

// Turn a pick into a command: a factory (keep/castle) trains a mobile unit; a
// mobile builder places a structure or conjures a mobile unit near itself. The
// placement spot is probed against the (const) world, then issued as a Build.
void Controller::produce(const tak::sim::World& world, const tak::sim::Unit& p,
                         const tak::sim::UnitType* pick, const CommandSink& sink) {
    if (!pick->canMove) {                       // structure
        if (p.type->canMove && p.type->isBuilder) {
            float x, z;
            if (placeSite(world, pick, p.x, p.z, x, z))
                emit(sink, tak::net::Cmd::Build, p.id, pick->id, x, z);
        }
    } else if (!p.type->canMove) {              // factory trains mobile
        emit(sink, tak::net::Cmd::Train, p.id, pick->id, 0, 0);
    } else if (p.type->isBuilder) {             // mobile builder conjures mobile
        for (float r = 40; r < 170; r += 20)
            for (float a = 0; a < 6.28f; a += 0.6f) {
                float x = p.x + std::cos(a) * r, z = p.z + std::sin(a) * r;
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
            float x = nx + std::cos(a) * r, z = nz + std::sin(a) * r;
            if (world.canPlace(t, x, z)) { outX = x; outZ = z; return true; }
        }
    return false;
}

// Nearest enemy of an un-allied player the group at (cx,cz) can actually REACH
// (flow connectivity), scanning closest-first. Picking merely the straight-line
// nearest foe on a maze sends the army at a walled-off target it can't get to.
bool Controller::nearestReachableEnemy(const tak::sim::World& world, float cx, float cz,
                                       const tak::sim::UnitType* atype,
                                       float& tx, float& tz) const {
    std::vector<std::pair<float, std::pair<float, float>>> es;
    for (auto& e : world.units()) {
        if (!e.alive() || e.embarked() || world.allied(e.player, player_) || !e.type)
            continue;
        float dx = e.x - cx, dz = e.z - cz;
        es.push_back({dx * dx + dz * dz, {e.x, e.z}});
    }
    if (es.empty()) return false;
    std::sort(es.begin(), es.end());
    int checked = 0;
    for (auto& e : es) {
        if (++checked > 16) break;   // bound the reachability probes (flow builds)
        if (!atype || world.pathExists(atype, e.second.first, e.second.second, cx, cz)) {
            tx = e.second.first; tz = e.second.second;
            return true;
        }
    }
    return false;
}

// Pool idle (non-builder) fighters and, once a strike force has gathered,
// attack-move the whole group at one reachable enemy so they share a flow field.
void Controller::sendWaves(const tak::sim::World& world, const CommandSink& sink) {
    std::vector<int> idle;
    double sx = 0, sz = 0;
    const tak::sim::UnitType* atype = nullptr;
    for (auto& u : world.units())
        if (u.alive() && u.player == player_ && u.type && u.type->canMove &&
            !u.type->isBuilder && waveFree(u)) {
            idle.push_back(u.id);
            sx += u.x; sz += u.z;
            if (!atype && !u.type->canFly) atype = u.type;
        }
    if (idle.size() < 4) return;
    float cx = float(sx / idle.size()), cz = float(sz / idle.size());
    float tx = 0, tz = 0;
    if (!nearestReachableEnemy(world, cx, cz, atype, tx, tz)) return;
    for (int id : idle) emit(sink, tak::net::Cmd::AttackMove, id, "", tx, tz);
}

void Controller::tick(const tak::sim::World& world, uint32_t simTick,
                      const CommandSink& sink) {
    // ~1 Hz (every 30 sim ticks), staggered by player so eight AIs don't all
    // think on the same tick. Sim-tick driven (not wall clock) so the decision
    // cadence is deterministic and replay-safe. The offset is the player index:
    // player 1 fires on ticks 1,31,61..., matching the pre-extraction AI exactly
    // (it fired once mana... aiTimer reset each ~30 ticks starting from tick 1).
    if ((simTick % 30) != uint32_t(player_ % 30)) return;
    if (world.player(player_).defeated) return;

    int econFactor = manaRatio(world) >= 0.5f ? 2 : 1;
    // Snapshot producer ids first. (We only read the world here, but keeping the
    // same two-pass shape as before preserves the exact RNG call order.)
    std::vector<int> producers;
    for (auto& u : world.units()) {
        if (!u.alive() || u.player != player_ || !u.type) continue;
        if (u.type->canMove && u.type->isBuilder && u.orders.empty() &&
            u.buildSiteId == 0)
            producers.push_back(u.id);                        // idle mobile builder
        else if (!u.type->canMove && !u.underConstruction && u.buildQueue.empty() &&
                 !registry_.buildable(u.type->id).empty())
            producers.push_back(u.id);                        // idle factory
    }
    for (int pid : producers) {
        const auto* p = world.unit(pid);
        if (!p || !p->alive()) continue;
        if (const auto* pick = weightedPick(world, *p, econFactor))
            produce(world, *p, pick, sink);
    }
    sendWaves(world, sink);
}

}  // namespace tak::ai
