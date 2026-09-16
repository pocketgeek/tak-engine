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

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace tak;

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
    sim::TypeRegistry reg;
    reg.loadMoveInfo(vfs, "gamedata/moveinfo.tdf");
    sim::setupRegistry(reg, vfs, false);
    for (const auto& c : kCases) runCase(vfs, reg, c);
    std::printf(failures ? "placement_test: FAILURES (%d case(s))\n"
                         : "placement_test: all passed\n", failures);
    return failures ? 1 : 0;
}
