// missiontool: drive a campaign mission's "god" .cob through the in-sim MissionScript
// runner, to verify the reverse-engineered scripting + win/lose model.
//
//   missiontool <retail-install-dir> [mission-stem]     (default: takmission01_mt)
//
// It builds a World (terrain via setupMatch), runs the mission Start script (spawns +
// triggers), then exercises the win and lose paths. Not shipped in a game -- a dev check.

#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/mission.h"
#include "sim/sim.h"
#include "tdf/tdf.h"

#include <cstdio>
#include <string>

using namespace tak;

namespace {

// Locate a mission bundle across the base + Iron Plague mission archives.
std::string findMission(const hpi::Vfs& vfs, const std::string& stem, const char* ext) {
    std::string p = "missions/" + stem + ext;
    return vfs.has(p) ? p : std::string();
}

// Build a fresh World with terrain from the mission map, plus the parsed .ota header.
bool build(const hpi::Vfs& vfs, const sim::TypeRegistry& reg, const std::string& stem,
           sim::World& w, tdf::Node& header, std::vector<uint8_t>& cob) {
    std::string tnt = findMission(vfs, stem, ".tnt");
    std::string ota = findMission(vfs, stem, ".ota");
    std::string cobp = findMission(vfs, stem, ".cob");
    if (tnt.empty() || ota.empty() || cobp.empty()) {
        std::fprintf(stderr, "mission '%s' not found (tnt=%d ota=%d cob=%d)\n", stem.c_str(),
                     !tnt.empty(), !ota.empty(), !cobp.empty());
        return false;
    }
    sim::MatchConfig cfg;
    cfg.vfs = &vfs;
    cfg.mapPath = tnt;
    cfg.slots = {{true, 0, 0}, {true, 1, 1}};   // human (ara/team0) + one enemy
    try { sim::setupMatch(w, reg, cfg); }
    catch (const std::exception& e) { std::fprintf(stderr, "setupMatch: %s\n", e.what()); return false; }

    auto otaBytes = vfs.read(ota);
    tdf::Node root = tdf::parseText(std::string(otaBytes.begin(), otaBytes.end()), ota);
    const tdf::Node* gh = root.child("globalheader");
    if (!gh) { std::fprintf(stderr, "%s: no [GlobalHeader]\n", ota.c_str()); return false; }
    header = *gh;
    cob = vfs.read(cobp);
    return true;
}

int countType(const sim::World& w, const std::string& typeId) {
    int n = 0;
    for (const auto& u : w.units())
        if (u.alive() && u.type && u.type->id == typeId) ++n;
    return n;
}
int findType(const sim::World& w, const std::string& typeId) {
    for (const auto& u : w.units())
        if (u.alive() && u.type && u.type->id == typeId) return u.id;
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: missiontool <retail-install-dir> [mission-stem]\n"); return 2; }
    std::string dataRoot = argv[1];
    std::string stem = argc >= 3 ? argv[2] : "takmission01_mt";

    hpi::Vfs vfs = hpi::mountRetailRoot(dataRoot);
    sim::TypeRegistry reg;
    sim::setupRegistry(reg, vfs, /*crusades=*/false);

    // ---- Start: spawns + triggers ----
    sim::World w;
    tdf::Node header;
    std::vector<uint8_t> cob;
    if (!build(vfs, reg, stem, w, header, cob)) return 1;

    size_t before = w.units().size();
    sim::MissionScript ms(cob, header, reg, /*human=*/0, stem);
    if (!ms.ok()) { std::fprintf(stderr, "mission cob failed to load\n"); return 1; }
    ms.start(w);
    for (int i = 0; i < 8; ++i) ms.step(w, 0.03f);   // run the Start thread to completion
    size_t after = w.units().size();

    std::printf("=== %s ===\n", stem.c_str());
    std::printf("units: %zu -> %zu after Start (%zd script-spawned)\n", before, after,
                ptrdiff_t(after) - ptrdiff_t(before));
    std::printf("NPCEMEN=%d ARASWORD=%d ARAARCH=%d\n",
                countType(w, "npcemen"), countType(w, "arasword"), countType(w, "araarch"));
    std::printf("outcome after Start: %d\n", ms.outcome());

    // ---- Win path: move the escorted NPC onto the objective radius ----
    int emen = findType(w, "npcemen");
    if (emen >= 0) {
        // takmission01 win = MoveUnitToRadius NPCEMEN 130 76 15; drop it on that cell.
        double wx = header.numberOr("dummy", 0);  // (header lookups sanity)
        (void)wx;
        if (auto* u = w.unit(emen)) { u->x = 130 * 16 + 8; u->z = 76 * 16 + 8; }
        ms.step(w, 0.1f);
        std::printf("WIN test  (NPCEMEN on objective): outcome=%d %s\n",
                    ms.outcome(), ms.outcome() == 1 ? "PASS" : "(no MoveUnitToRadius win?)");
    }

    // ---- Lose path: kill the escorted NPC (fresh mission) ----
    sim::World w2;
    tdf::Node header2;
    std::vector<uint8_t> cob2;
    if (build(vfs, reg, stem, w2, header2, cob2)) {
        sim::MissionScript ms2(cob2, header2, reg, 0, stem);
        ms2.start(w2);
        for (int i = 0; i < 8; ++i) ms2.step(w2, 0.03f);   // run Start (stores NPCEMEN in static0)
        int e2 = findType(w2, "npcemen");
        if (e2 >= 0) {
            if (auto* u = w2.unit(e2)) u->hp = 0;
            ms2.unitDied(w2, e2);
            for (int i = 0; i < 4; ++i) ms2.step(w2, 0.03f);   // run the UnitDestroyed thread
            std::printf("LOSE test (NPCEMEN killed):        outcome=%d %s\n",
                        ms2.outcome(), ms2.outcome() == -1 ? "PASS" : "(no scripted defeat?)");
        }
    }
    return 0;
}
