// missiontool: load a full campaign mission (terrain + placed units + the in-sim
// MissionScript) via setupMission, run it through World::tick, and check the win/lose
// paths. Verifies the reverse-engineered scripting + loader + tick integration.
//
//   missiontool <retail-install-dir> [mission-stem]     (default: takmission01_mt)
//
// A dev harness; not shipped in a game.

#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"

#include <cstdio>
#include <string>

using namespace tak;

namespace {
int countType(const sim::World& w, const std::string& id) {
    int n = 0;
    for (const auto& u : w.units()) if (u.alive() && u.type && u.type->id == id) ++n;
    return n;
}
int findType(const sim::World& w, const std::string& id) {
    for (const auto& u : w.units()) if (u.alive() && u.type && u.type->id == id) return u.id;
    return -1;
}
void tick(sim::World& w, float seconds) {
    const float dt = 1.0f / 30.0f;
    for (int i = 0; i < int(seconds / dt); ++i) { w.tick(dt); if (w.missionOutcome()) break; }
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: missiontool <retail-install-dir> [mission-stem]\n"); return 2; }
    std::string dataRoot = argv[1];
    std::string stem = argc >= 3 ? argv[2] : "takmission01_mt";

    hpi::Vfs vfs = hpi::mountRetailRoot(dataRoot);
    sim::TypeRegistry reg;
    sim::setupRegistry(reg, vfs, /*crusades=*/false);

    // ---- full load via setupMission, then run through World::tick ----
    sim::World w;
    int human = 0;
    if (!sim::setupMission(w, reg, vfs, stem, human)) return 1;
    std::printf("=== %s ===\n", stem.c_str());
    std::printf("placed+monarchs: %zu units at load\n", w.units().size());
    tick(w, 2.0f);   // run Start + settle
    std::printf("after 2s: units=%zu  NPCEMEN=%d ARASWORD=%d  outcome=%d\n",
                w.units().size(), countType(w, "npcemen"), countType(w, "arasword"), w.missionOutcome());

    // ---- WIN: drop the escorted NPC onto the objective radius ----
    int emen = findType(w, "npcemen");
    if (emen >= 0 && w.missionOutcome() == 0) {
        if (auto* u = w.unit(emen)) { u->x = 130 * 16 + 8; u->z = 76 * 16 + 8; }
        w.tick(1.0f / 30.0f);
        std::printf("WIN test  (NPCEMEN on objective): outcome=%d %s\n",
                    w.missionOutcome(), w.missionOutcome() == 1 ? "PASS" : "FAIL");
    }

    // ---- LOSE: kill the escorted NPC in a fresh run ----
    sim::World w2;
    int h2 = 0;
    if (sim::setupMission(w2, reg, vfs, stem, h2)) {
        tick(w2, 2.0f);
        int e2 = findType(w2, "npcemen");
        if (e2 >= 0 && w2.missionOutcome() == 0) {
            if (auto* u = w2.unit(e2)) u->hp = 0;   // die -> World::tick fires UnitDestroyed
            tick(w2, 1.0f);
            std::printf("LOSE test (NPCEMEN killed):       outcome=%d %s\n",
                        w2.missionOutcome(), w2.missionOutcome() == -1 ? "PASS" : "FAIL");
        }
    }
    return 0;
}
