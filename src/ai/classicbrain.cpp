#include "ai/classicbrain.h"

#include "sim/retailai.h"
#include "sim/sim.h"

#ifndef NDEBUG
#include <cctype>
#include <cstdio>
#include <cstdlib>
#endif

namespace tak::ai {

ClassicBrain::ClassicBrain(int player, uint32_t roomSeed)
    : player_(player), seed_(seedFor(player, roomSeed)), profile_(catalogue_.size()),
      info_(catalogue_.size()), count_(catalogue_.size()), completed_(catalogue_.size()),
      priority_(catalogue_.size()) {}

ClassicBrain::ClassicBrain(int player, uint32_t roomSeed, const tak::sim::TypeRegistry& registry)
    : player_(player), seed_(seedFor(player, roomSeed)),
      catalogue_(ClassicCatalogue::from(registry)), profile_(catalogue_.size()),
      info_(catalogue_.size()), count_(catalogue_.size()), completed_(catalogue_.size()),
      priority_(catalogue_.size()) {
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

void ClassicBrain::tick(const tak::sim::World& world, uint32_t simTick,
                        const std::function<void(const tak::net::Command&)>&) {
    // Retail's player loop: the AI scheduler (0x40fe00 -> 0x40fd80, P3 onward),
    // then the bookkeeping (0x4121c0 -> 0x411a90).
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

}  // namespace tak::ai
