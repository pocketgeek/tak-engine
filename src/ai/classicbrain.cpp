#include "ai/classicbrain.h"

#include "sim/retailai.h"
#include "sim/sim.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_set>

#ifndef NDEBUG
#include <cctype>
#include <cstdio>
#endif

namespace tak::ai {

ClassicBrain::ClassicBrain(int player, uint32_t roomSeed)
    : player_(player), seed_(seedFor(player, roomSeed)), profile_(catalogue_.size()),
      info_(catalogue_.size()), count_(catalogue_.size()), completed_(catalogue_.size()),
      priority_(catalogue_.size()) {
    classicConstructSquads(squads_, 0, [&](int n) { return rand(n); });
}

ClassicBrain::ClassicBrain(int player, uint32_t roomSeed, const tak::sim::TypeRegistry& registry,
                           int32_t largestEnemyPopulation)
    : player_(player), seed_(seedFor(player, roomSeed)),
      catalogue_(ClassicCatalogue::from(registry)), profile_(catalogue_.size()),
      info_(catalogue_.size()), count_(catalogue_.size()), completed_(catalogue_.size()),
      priority_(catalogue_.size()) {
    // 0x4f6660: the AI manager (0x40fed0) before the build manager (0x4121e0).
    classicConstructSquads(squads_, largestEnemyPopulation, [&](int n) { return rand(n); });
    for (size_t t = 1; t < catalogue_.size(); ++t) {
        info_[t] = ClassicTypeInfo::from(*catalogue_.types[t], &registry);
        index_.emplace(catalogue_.types[t], uint16_t(t));
    }
    creationPass();
}

void ClassicBrain::creationPass() {
    // The build manager computes its priorities once as it is created at game
    // start (0x4102f0 -> 0x410810), before the profile file is read and before
    // the player has units or a store: every limit is still -1, every count and
    // the population 0, the store empty (fill 0) with an empty income history,
    // which reads as a shortfall. Reproduced exactly against a captured retail
    // PrintWeights table (tools/re/check_ai_printweights.py, Taros at tick 16).
    const tak::sim::RetailResourceHistory none;
    const bool shortfall = none.shortfall(0.0f);
    for (size_t t = 1; t < catalogue_.size(); ++t)
        priority_[t] = tak::sim::retailBuildPriority(info_[t].priorityInputs(0, 0, -1), 0, storeFill(0, 0),
                                                     shortfall, [&](int n) { return rand(n); });
    ++refreshes_;
}

void ClassicBrain::useProfile(const ClassicProfileScript& script) {
    profile_.reset(catalogue_.size());   // as retail's ReloadAIProfiles does first
    profile_.execute(script.text, catalogue_);
    profile_.path = script.path;
}

int ClassicBrain::buildWeight(size_t t) const {
    if (t == 0 || t >= priority_.size()) return 0;
    // An AI seat always has its limits on (retail player+0xe3 = 1 in skirmish).
    return tak::sim::retailAiBuildWeight(true, profile_.limit[t], count_[t], profile_.pref[t],
                                         int8_t(priority_[t]));
}

void ClassicBrain::recount(const tak::sim::World& world) {
    std::fill(count_.begin(), count_.end(), int16_t(0));
    std::fill(completed_.begin(), completed_.end(), int16_t(0));
    for (const auto& u : world.units()) {
        if (u.player != player_ || !u.alive() || !u.type) continue;
        const auto it = index_.find(u.type);
        if (it == index_.end()) continue;
        ++count_[it->second];
        if (!u.underConstruction) ++completed_[it->second];
    }
}

void ClassicBrain::recomputePriorities(const tak::sim::World& world) {
    // The owner's live unit count (player+0xe8) and its store: fill = stored /
    // capacity (0 under 0.01 capacity), shortfall from the 29-of-30-tick income
    // and spend history the World keeps for every player (displayResources).
    unsigned population = 0;
    for (const auto& u : world.units())
        if (u.player == player_ && u.alive() && u.type) ++population;
    const auto& me = world.player(player_);
    const float stored = float(me.mana);
    const float capacity = me.manaCapacity();
    const float ratio = storeFill(stored, capacity);
    const bool shortfall = me.displayResources.shortfall(stored);
    for (size_t t = 1; t < catalogue_.size(); ++t) {
        const auto in = info_[t].priorityInputs(count_[t], completed_[t], profile_.limit[t]);
        priority_[t] = tak::sim::retailBuildPriority(in, uint16_t(population), ratio, shortfall,
                                                     [&](int n) { return rand(n); });
    }
    ++refreshes_;
}

void ClassicBrain::bookkeeping(const tak::sim::World& world, uint32_t simTick) {
    if (tak::sim::retailTickPlayerCache(clock_, simTick, [&](int n) { return rand(n); }))
        recomputePriorities(world);
    recount(world);
}

void ClassicBrain::setStart(int index, float x, float z) {
    if (index < 0) return;
    // 0x4e0810: the start's 16.16 position shifted down to its 16-pixel cell.
    auto cell = [](float v) { return int16_t(uint32_t(int32_t(std::lround(double(v) * 65536.0))) >> 20); };
    if (squads_.starts.size() <= size_t(index)) squads_.starts.resize(size_t(index) + 1);
    squads_.starts[size_t(index)] = ClassicCell{cell(x), cell(z)};
    startIndex_ = index;
}

std::vector<ClassicSquadUnit> ClassicBrain::squadUnits(const tak::sim::World& world) {
    std::vector<ClassicSquadUnit> units;
    std::unordered_set<int> own;
    for (const auto& u : world.units()) {
        if (u.player != player_ || !u.type) continue;
        own.insert(u.id);
        const auto* t = u.type;
        const auto it = index_.find(t);
        const ClassicTypeInfo* info = it == index_.end() ? nullptr : &info_[it->second];
        ClassicSquadUnit s;
        s.id = u.id;
        const bool structure = t->isStructure();
        const bool armed = info ? info->armed : !t->weapons.empty();
        // The port's readings of the unit bits (docs/classic-ai-plan.md U6): a unit
        // in the World is a live slot, a dead one still listed is dying; a finished
        // unit is selectable (retail sets that once it has left its factory).
        s.flags = ClassicSquadUnit::kLive | (u.alive() ? 0u : ClassicSquadUnit::kDying) |
                  (u.underConstruction ? 0u : ClassicSquadUnit::kSelectable) |
                  (structure ? ClassicSquadUnit::kStructure : 0u) | (armed ? ClassicSquadUnit::kArmed : 0u);
        s.commander = info ? info->commander : t->commander;
        s.sacredSite = info && info->manaSite;
        s.builder = info && info->builder;
        s.finished = !u.underConstruction;
        s.carried = u.embarked();
        s.mover = !structure;
        s.menu = info && info->menu ? 1 : 0;
        s.x = u.x.v; s.z = u.z.v;
        s.footX = int16_t(t->footX); s.footZ = int16_t(t->footZ);
        s.squad = squadOf(u.id);
        units.push_back(s);
    }
    // A member that is no longer the player's unit (removed from the World, or
    // given away) leaves its squad the way set-squad removes one.
    for (auto& squad : squads_.squads)
        for (size_t k = 0; k < squad.members.size();)
            if (!own.count(squad.members[k])) { squad.members[k] = squad.members.back(); squad.members.pop_back(); }
            else ++k;
    std::erase_if(squadOf_, [&](const auto& e) { return !own.count(e.first); });
    return units;
}

void ClassicBrain::scheduler(const tak::sim::World& world, uint32_t simTick,
                             const std::function<void(const tak::net::Command&)>& sink) {
    std::vector<ClassicSquadUnit> units;
    bool snapshot = false;
    auto random = [&](int n) { return rand(n); };
    auto command = [&](tak::net::Cmd kind, int unitId) {
        tak::net::Command c;
        c.kind = kind; c.player = uint8_t(player_); c.unitId = unitId; c.targetId = 0;
        sink(c);
    };
    tak::sim::retailTickAiSchedule(squads_.schedule, simTick, random,
        [&] {   // 0x40fe80
            units = squadUnits(world); snapshot = true;
            classicDropDying(squads_, units);
        },
        [&] {   // 0x40eeb0
            const ClassicAssignContext ctx{simTick, scenarioMode_, startIndex_};
            classicAssign(squads_, units, ctx,
                [&](size_t i) { command(tak::net::Cmd::Stance, units[i].id); },   // 0x5198a0(u, 2)
                [&](size_t i) { command(tak::net::Cmd::Stop, units[i].id); });    // 0x4d6a50(u, 0)
        },
        [&](unsigned slot, auto& clock) {
            // Each kind's think reschedules itself (and an empty strike or VTOL
            // squad goes inactive). Strike recruitment (0x40abd0) is P6, and the
            // occupied squads' plans come in P4-P7.
            tak::sim::retailTickAiSquad(squads_.squads[slot], clock, simTick, random,
                                        [](auto&) {}, [](auto&) {});
        });
    if (!snapshot) return;
    for (const auto& u : units) {
        if (u.squad) squadOf_[u.id] = u.squad;
        else squadOf_.erase(u.id);
    }
}

void ClassicBrain::tick(const tak::sim::World& world, uint32_t simTick,
                        const std::function<void(const tak::net::Command&)>& sink) {
    // Retail's player loop: the AI scheduler (0x40fe00 -> 0x40fd80), then the
    // bookkeeping (0x4121c0 -> 0x411a90).
    scheduler(world, simTick, sink);
    bookkeeping(world, simTick);
#ifndef NDEBUG
    debugDump(simTick);
#endif
}

#ifndef NDEBUG
// Debug-only harness hook, for comparing with retail's PrintWeights dumps
// (tools/re/retail_ai_harness.py --printweights): TAK_CLASSIC_WEIGHTS names an
// output path and TAK_CLASSIC_WEIGHTS_AT a sim tick (default 600). At the first
// tick at or past it, each Classic brain writes <path>.p<player> once, one row
// per catalogue type in retail's column layout:
//     <limit, or n/a> - <priority> = <preference> - '<unit>\t\t:<name>'
// A release takserver compiles none of this and reads neither variable. It only
// writes a file, so --mpai hashes are the same with it on.
void ClassicBrain::debugDump(uint32_t simTick) {
    static const char* path = std::getenv("TAK_CLASSIC_WEIGHTS");
    static const uint32_t at = [] {
        const char* e = std::getenv("TAK_CLASSIC_WEIGHTS_AT");
        const long v = e ? std::strtol(e, nullptr, 10) : 600;
        return uint32_t(v > 0 ? v : 600);
    }();
    if (dumped_ || !path || !*path || simTick < at) return;
    dumped_ = true;
    const std::string out = std::string(path) + ".p" + std::to_string(player_);
    std::FILE* f = std::fopen(out.c_str(), "w");
    if (!f) { std::fprintf(stderr, "TAK_CLASSIC_WEIGHTS: cannot write %s\n", out.c_str()); return; }
    const unsigned secs = simTick / 30;
    std::fprintf(f, "gametime: '%02u:%02u:%02u'\nplayer:   'player %d' num: %d\ncontroller: AI (Classic)\n"
                    "profile:    '%s'\nsim tick: %u, priority refreshes: %u, units: %d\n"
                    "------------------------------------------------\n"
                    "<limit> - <dynamic priority> = <profile preference> - <unit>\n",
                 secs / 3600, secs / 60 % 60, secs % 60, player_, player_, profile_.path.c_str(), simTick,
                 refreshes_, [&] { int n = 0; for (auto c : count_) n += c; return n; }());
    for (size_t t = 1; t < catalogue_.size(); ++t) {
        char limit[16];
        if (profile_.limit[t] == -1) std::snprintf(limit, sizeof limit, "n/a ");
        else std::snprintf(limit, sizeof limit, "%4d", profile_.limit[t]);
        std::string name = catalogue_.names[t];
        for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
        std::fprintf(f, "%s - %3d = %3d - '%s\t\t:%s'\n", limit, int(int8_t(priority_[t])),
                     int(profile_.pref[t]), name.c_str(),
                     catalogue_.types[t] ? catalogue_.types[t]->name.c_str() : "");
    }
    std::fclose(f);
}
#endif

uint32_t classicMassAttackTicks(const std::string& line) {
    // strstr(line, "massattack"), then sscanf(at, "massattack %i"): the format's
    // space takes any whitespace, %i a sign and a 0x / 0 prefix.
    const size_t at = line.find("massattack");
    if (at == std::string::npos) return 0;
    const char* p = line.c_str() + at + 10;
    char* end = nullptr;
    const long v = std::strtol(p, &end, 0);
    if (end == p) return 0;
    return uint32_t(v) * 30u;   // the stored dword: n x 30, wrapping
}

}  // namespace tak::ai
