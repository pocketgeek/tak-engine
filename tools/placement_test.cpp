// placement_test -- no unit may start on ground it cannot stand on.
//
// This is the THIRD fix in this area, which is why it is now a test rather than a
// measurement pasted into a commit message. The earlier two were: benchmark and
// stress-fill units dropped onto a blind lattice with no terrain check at all
// (~4.5-20.5% of an army starting in a lake or on a cliff), and bodies placed inside
// each other because the overlap check reserved a centre cell instead of a footprint.
// Both were found by looking; neither left anything behind that would notice a
// recurrence.
//
// The one this test was written for: the Monarch was the single spawn in setupMatch
// that never consulted the terrain. Every other body was snapped to ground it fits;
// the Monarch's footprint was merely RESERVED so nothing else snapped onto it, and it
// was then placed at the raw start spot whatever was there. Invisible on a real map,
// whose start positions are authored -- but the benchmark ignores those and spreads
// factions around a COMPUTED ring, and any game with more slots than the map has
// starts fills the shortfall from a second computed ring. Measured on Ulasem Arena at
// 8 players: 2 of 8 Monarchs on ground they did not fit, at every intensity. A Monarch
// cannot walk out of a blocked cell, so it stayed there for the whole match.
//
// SCOPE, so the 8-unit benchmark rows are not mistaken for full coverage: a benchmark
// spawns its army from a RAMP executed during ticks, and those positions are snapped at
// PLAN time inside setupMatch. This test does not tick, so for benchmark cases it checks
// the Monarchs only. The ramp uses the same snapSpawn as the stress fill, which the
// stress cases below do exercise at scale (~3800 and ~2800 bodies) -- so the shared path
// is covered; ticking each benchmark for 1800 ticks would add minutes to buy little.
//
// Needs a retail install, so it registers only with -DTAK_TEST_DATA.

#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include "sim/footprint.h"
#include "client/renderframe.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace tak;

namespace tak::sim {
struct RetailReplayProbe {
    static const std::vector<BenchStage>& plan(const World& world) { return world.benchPlan_; }
};
}

namespace {
int failures = 0;

// Every configuration that places bodies without an authored position behind it.
struct Case { const char* name; const char* map; int players; int benchmark; bool stress; };

const Case kCases[] = {
    {"benchmark Low",         "Ulasem Arena", 8, 1, false},
    {"benchmark High",        "Ulasem Arena", 8, 3, false},
    {"benchmark Extra Absurd","Ulasem Arena", 8, 6, false},
    {"benchmark cramped",     "Inner Circle", 8, 4, false},
    {"stress Ulasem",         "Ulasem Arena", 8, 0, true},
    {"stress cramped",        "Inner Circle", 8, 0, true},
    // More slots than the map has start positions: the shortfall comes from a computed
    // ring too, and is just as unchecked as the benchmark's.
    {"overfilled starts",     "Two Castles",  8, 0, false},
};

// A body stands legally when the nav grid for ITS movement class admits its footprint
// at its cell -- the same question the mover asks, so a unit that fails here is one
// that can never move off the spot it started on.
bool standsLegally(const sim::World& w, const sim::Unit& u) {
    if (!u.type || u.type->canFly) return true;         // flyers are over it all
    const sim::NavGrid& g = w.navFor(u.type);
    if (g.empty()) return true;
    const int foot = std::clamp(std::max(u.type->footX, u.type->footZ), 1, 15);
    return g.fits(int(u.x.toFloat()) / 16, int(u.z.toFloat()) / 16, foot);
}

void runCase(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const Case& c) {
    sim::World world;
    world.setVisPlayer(-1);
    sim::MatchConfig cfg;
    cfg.vfs = &vfs;
    cfg.mapPath = std::string("maps/") + c.map + ".tnt";
    cfg.benchmark = c.benchmark;
    cfg.stressTest = c.stress;
    cfg.unitCap = 500;                                   // keep the stress fills quick
    for (int i = 0; i < c.players; ++i) {
        sim::MatchSlot s; s.used = true; s.faction = i % 5; s.team = i;
        cfg.slots.push_back(s);
    }
    sim::setupMatch(world, reg, cfg);

    std::map<std::pair<int,const sim::UnitType*>,int> counts;
    for (const auto& u:world.units()) if (u.alive() && u.type) {
        const int n=++counts[{u.player,u.type}];
        if (u.type->totalAllowed>0 && n>u.type->totalAllowed) {
            std::printf("      player %d exceeds %s totalallowed=%d\n",u.player,
                        u.type->id.c_str(),u.type->totalAllowed);
            ++failures;
        }
    }
    for (const auto& stage:sim::RetailReplayProbe::plan(world))
        for (const auto& spawn:stage.units) {
            const int n=++counts[{spawn.player,spawn.type}];
            if (spawn.type->totalAllowed>0 && n>spawn.type->totalAllowed) {
                std::printf("      benchmark plan exceeds %s totalallowed=%d for player %d\n",
                            spawn.type->id.c_str(),spawn.type->totalAllowed,spawn.player);
                ++failures;
            }
        }

    int total = 0, bad = 0, badMonarch = 0;
    for (const auto& u : world.units()) {
        if (!u.alive() || !u.type) continue;
        ++total;
        if (standsLegally(world, u)) continue;
        ++bad;
        if (u.type->id.find("king") != std::string::npos ||
            u.type->id.find("necro") != std::string::npos ||
            u.type->id.find("mage") != std::string::npos ||
            u.type->id.find("hunt") != std::string::npos ||
            u.type->id.find("sage") != std::string::npos)
            ++badMonarch;
        if (bad <= 3)
            std::printf("      %s at %.0f,%.0f does not fit its cell\n",
                        u.type->id.c_str(), u.x.toFloat(), u.z.toFloat());
    }
    std::printf("  %-24s %4d units, %d unplaceable%s\n", c.name, total, bad,
                badMonarch ? "  (INCLUDING A MONARCH)" : "");
    if (bad) ++failures;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: placement_test <retail-install>\n"); return 2; }
    std::printf("placement_test -- no unit starts on ground it cannot stand on\n");
    auto vfs = hpi::mountRetailRoot(argv[1], hpi::OverridePolicy::None);
    for (bool crusades:{false,true}) {
        sim::TypeRegistry reg;
        sim::setupRegistry(reg, vfs, crusades);
        std::printf("balance: %s\n",crusades ? "Crusades" : "standard");
        for (const auto& c : kCases) runCase(vfs, reg, c);
        for (const char* id:{"aralode","tarlode","verlode","zonlode","crelode"}) {
            const auto* type=reg.find(id);
            if (!type) { ++failures;continue; }
            sim::World world;world.setVisPlayer(-1);
            sim::MatchConfig cfg;cfg.vfs=&vfs;cfg.mapPath=hpi::findMap(vfs,"ulasem arena");
            cfg.slots={{true,0,0,1}};
            sim::setupMatch(world,reg,cfg);
            int accepted=0,unproductive=0;
            float px=0,pz=0,multiplier=0;
            for (const auto& [x,z]:world.manaSpots())
                for (int dx=-16;dx<=16;dx+=8) for (int dz=-16;dz<=16;dz+=8) {
                    if (!world.canPlace(type,x+dx,z+dz)) continue;
                    ++accepted;
                    multiplier=world.sacredIncomeMultiplier(
                        sim::footprintOrigin(x+dx,type->footX),sim::footprintOrigin(z+dz,type->footZ),
                        type->footX,type->footZ);
                    if (multiplier<=0) ++unproductive;
                    px=x+dx;pz=z+dz;
                }
            if (!accepted || unproductive) ++failures;
            world.player(0).mana=0;
            for (int i=0;i<31;++i) world.tick(1.f/30.f);
            PlayerR before;before.captureEconomy(world.player(0));
            const int lode=world.spawn(type,px,pz,0,0);
            world.unit(lode)->underConstruction=true;
            for (int i=0;i<31;++i) world.tick(1.f/30.f);
            PlayerR unfinished;unfinished.captureEconomy(world.player(0));
            if (std::abs(unfinished.income-before.income)>0.001 ||
                unfinished.storage!=before.storage) ++failures;
            world.unit(lode)->underConstruction=false;
            const double stored=world.player(0).mana;
            for (int i=0;i<60;++i) world.tick(1.f/30.f);
            PlayerR after;after.captureEconomy(world.player(0));
            const float expected=before.income+type->income*multiplier;
            if (std::abs(after.income-expected)>0.001 ||
                std::abs(after.mana-(stored+expected*2))>0.01 ||
                after.storage!=before.storage+type->storage) ++failures;
            std::printf("  %s: %d productive placements, %d zero-income placements; HUD income %.1f -> %.1f, capacity %.0f -> %.0f\n",
                        id,accepted,unproductive,before.income,after.income,before.storage,after.storage);
        }

    }
    std::printf(failures ? "placement_test: FAILURES (%d case(s))\n"
                         : "placement_test: all passed\n", failures);
    return failures ? 1 : 0;
}
