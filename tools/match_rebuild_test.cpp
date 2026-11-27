// match_rebuild_test: a match built in a World that already held another map is the
// match the referee builds in a fresh one.
//
//   match_rebuild_test <retail-install-dir>
//
// The referee (takserver) runs setupMatch in a fresh World. A client does not: GameView's
// constructor first sets its World up on the launch map -- setTerrain + registerMapFeatures,
// and a game opened from the menu's lobby launches on the fallback map, Athri Cay -- and
// the room's map is then built on top. Whatever setup does not rebuild would carry the
// launch map into the match without showing in the state hash until something reads it
// (ta-engine desynced so on its feature ore map). GameView now calls World::resetForMatch
// before setupMatch / setupMission.
//
// A world first set up on Athri Cay and then reset builds the same Two Castles match as
// a fresh world: the state hash after setup and after a few seconds of play agree, and a
// reset leaves a fresh World's hash while keeping how the process ticks it.
//
// A dev harness; not shipped in a game.

#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include "tnt/tnt.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace tak;

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: match_rebuild_test <retail-install-dir>\n");
        return 2;
    }
    hpi::Vfs vfs = hpi::mountRetailRoot(argv[1]);
    sim::TypeRegistry reg;
    sim::setupRegistry(reg, vfs, false);
    const std::string launch = hpi::findMap(vfs, "athri cay");   // main.cpp's lobby fallback
    const std::string room = hpi::findMap(vfs, "Two Castles");
    if (launch.empty() || room.empty()) {
        std::fprintf(stderr, "match_rebuild_test: Athri Cay or Two Castles missing\n");
        return 2;
    }
    const tnt::Map launchMap = tnt::Map::load(vfs.read(launch), launch);

    sim::MatchConfig cfg;
    cfg.vfs = &vfs;
    cfg.mapPath = room;
    cfg.startSeed = 1u;
    cfg.pathfindingMode = sim::PathfindingMode::Legion;
    cfg.slots.resize(2);
    const int factions[2] = {0, 4};
    for (int i = 0; i < 2; ++i) { cfg.slots[size_t(i)].used = true; cfg.slots[size_t(i)].faction = factions[i]; cfg.slots[size_t(i)].team = i; }

    // The launch map's setup, as GameView's constructor runs it.
    const auto launchSetup = [&](sim::World& w) {
        w.setTerrain(launchMap.heights, launchMap.width, launchMap.height, launchMap.seaLevel, &launchMap.features);
        sim::registerMapFeatures(w, launchMap, vfs, &reg);
    };
    {
        const sim::World fresh;
        sim::World reset;
        launchSetup(reset);
        reset.setSerialThreads(true);
        reset.resetForMatch();
        check(reset.stateHash() == fresh.stateHash(), "resetForMatch leaves a fresh World's state hash");
    }
    sim::World referee, client;
    for (sim::World* w : {&referee, &client}) w->setSerialThreads(true);
    launchSetup(client);
    client.resetForMatch();
    sim::setupMatch(referee, reg, cfg);
    sim::setupMatch(client, reg, cfg);
    check(client.stateHash() == referee.stateHash(), "the state hash after setup matches the referee's");
    for (int t = 0; t < 30 * 10; ++t) { referee.tick(1.0f / 30.0f); client.tick(1.0f / 30.0f); }
    check(client.stateHash() == referee.stateHash(), "the state hash after ten seconds matches the referee's");

    std::printf("%s\n", failures ? "match_rebuild_test: FAILED" : "match_rebuild_test: all passed");
    return failures ? 1 : 0;
}
