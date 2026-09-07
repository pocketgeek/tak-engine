// aitool: run the skirmish AI headless (one AI player vs an idle opponent) and report
// how its economy + army develop and whether it marches on the enemy. A dev harness
// for tuning the AI -- not shipped in a game.
//
//   aitool <retail-install-dir> [map] [easy|normal|hard] [seconds]

#include "ai/ai.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

using namespace tak;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: aitool <install> [map] [easy|normal|hard] [seconds]\n"); return 2; }
    std::string dataRoot = argv[1];
    std::string map = argc >= 3 ? argv[2] : "Inner Circle";
    std::string dstr = argc >= 4 ? argv[3] : "normal";
    int seconds = argc >= 5 ? std::atoi(argv[4]) : 180;
    ai::Difficulty diff = dstr == "easy" ? ai::Difficulty::Easy
                        : dstr == "hard" ? ai::Difficulty::Hard : ai::Difficulty::Normal;

    hpi::Vfs vfs = hpi::mountRetailRoot(dataRoot);
    sim::TypeRegistry reg;
    sim::setupRegistry(reg, vfs, false);
    ai::Profile profile = ai::loadProfile(vfs);

    // 2-player 1v1: slot 0 = idle human (Aramon), slot 1 = the AI under test (Taros).
    sim::World w;
    w.setVisPlayer(-1);
    sim::MatchConfig cfg;
    cfg.vfs = &vfs;
    cfg.mapPath = hpi::findMap(vfs, map);
    if (cfg.mapPath.empty()) { std::fprintf(stderr, "aitool: map '%s' not found\n", map.c_str()); return 1; }
    cfg.slots = {{true, 0, 0}, {true, 1, 1}};
    auto spots = sim::setupMatch(w, reg, cfg);
    std::vector<std::pair<float, float>> enemyStarts;
    if (!spots.empty()) enemyStarts.push_back(spots[0]);   // the human's start
    float ex = enemyStarts.empty() ? 0 : enemyStarts[0].first;
    float ez = enemyStarts.empty() ? 0 : enemyStarts[0].second;

    // How many mana deposits exist, and how many are near the AI's start (a rough
    // proxy for how much economy it can build).
    int nearAi = 0;
    for (const auto& [sx, sz] : w.manaSpots()) {
        float dx = sx - spots[1].first, dz = sz - spots[1].second;
        if (dx * dx + dz * dz < 900.f * 900.f) ++nearAi;
    }
    std::printf("map mana spots: %zu total, %d within 900px of the AI start\n",
                w.manaSpots().size(), nearAi);
    // Is the enemy base reachable by ground from the AI base? (a common attack blocker)
    const sim::UnitType* ground = reg.find("tarknigh");
    if (ground)
        std::printf("enemy base reachable by ground (tarknigh): %s  (AI start %.0f,%.0f -> enemy %.0f,%.0f)\n",
                    w.pathExists(ground, ex, ez, spots[1].first, spots[1].second) ? "YES" : "NO",
                    spots[1].first, spots[1].second, ex, ez);

    // Controlled path test: spawn a knight at the AI base, order it straight at the
    // enemy base, and watch it travel -- isolates long-range nav from the AI logic.
    if (std::getenv("TAK_PATHTEST")) {
        const sim::UnitType* kt = reg.find("tarknigh");
        int id = w.spawn(kt, spots[1].first, spots[1].second, 0, 1);
        bool am = std::getenv("TAK_PATHTEST_MOVE") == nullptr;   // default: attackMove (what the AI uses)
        if (am) w.attackMove(id, ex, ez, false); else w.order(id, ex, ez, false);
        std::printf("PATHTEST (%s): knight %d from (%.0f,%.0f) -> (%.0f,%.0f)\n",
                    am ? "attackMove" : "move", id, spots[1].first, spots[1].second, ex, ez);
        for (int t = 0; t < 90 * 30; ++t) {
            w.tick(1.0f / 30.0f);
            if (t % (10 * 30) == 0) {
                const sim::Unit* u = w.unit(id);
                if (u) std::printf("  t=%2ds pos=(%.0f,%.0f) dist=%.0f orders=%zu\n",
                                   t / 30, u->x, u->z,
                                   std::sqrt((u->x - ex) * (u->x - ex) + (u->z - ez) * (u->z - ez)),
                                   u->orders.size());
            }
        }
        return 0;
    }

    ai::Controller ctl(1, reg, profile, 0x1234, diff, enemyStarts);
    std::map<int, int> cmdCount;   // Cmd kind -> count
    auto sink = [&](const net::Command& c) {
        cmdCount[int(c.kind)]++;
        sim::applyCommand(w, reg, c);
    };

    auto report = [&](int t) {
        std::map<std::string, int> comp;
        int army = 0; float mana = 0, income = 0;
        float minDist = 1e9f;   // closest army unit to the enemy base (shows advances)
        for (const auto& u : w.units())
            if (u.alive() && u.player == 1 && u.type) {
                comp[u.type->id]++;
                if (u.type->canMove && !u.type->isBuilder) {
                    ++army;
                    float d = std::sqrt((u.x - ex) * (u.x - ex) + (u.z - ez) * (u.z - ez));
                    minDist = std::min(minDist, d);
                }
            }
        mana = w.player(1).mana; income = w.player(1).income;
        std::string s;
        for (const auto& [id, n] : comp) { s += id + ":" + std::to_string(n) + " "; }
        std::printf("t=%3ds  income=%.0f mana=%.0f  army=%d closest-to-enemy=%.0f  | %s\n",
                    t, income, mana, army, army ? minDist : -1, s.c_str());
        if (std::getenv("TAK_AI_UNITS"))
            for (const auto& u : w.units())
                if (u.alive() && u.player == 1 && u.type && u.type->canMove && !u.type->isBuilder) {
                    float d = std::sqrt((u.x - ex) * (u.x - ex) + (u.z - ez) * (u.z - ez));
                    bool reach = w.pathExists(u.type, ex, ez, u.x, u.z);
                    std::printf("      %s#%d pos=(%.0f,%.0f) dist=%.0f spd=%.1f reach-enemy=%s orders=%zu%s\n",
                                u.type->id.c_str(), u.id, u.x, u.z, d, u.speed, reach ? "Y" : "N", u.orders.size(),
                                u.orders.empty() ? "" : (u.orders.front().attackMove ? " [attackMove]" :
                                                         u.orders.front().targetId ? " [attack]" : " [move]"));
                }
    };

    const float dt = 1.0f / 30.0f;
    int totalTicks = seconds * 30;
    std::printf("=== AI %s on '%s' (enemy start %.0f,%.0f) ===\n", dstr.c_str(), map.c_str(), ex, ez);
    for (int t = 0; t < totalTicks; ++t) {
        ctl.tick(w, uint32_t(t), sink);
        w.tick(dt);
        if (t % (30 * 30) == 0) report(t / 30);   // every 30s
    }
    report(seconds);
    std::printf("commands issued:");
    for (const auto& [k, n] : cmdCount)
        std::printf(" kind%d=%d", k, n);
    std::printf("  (Build=%d Train=%d Move=%d AttackMove=%d Attack=%d Stop=%d)\n",
                int(net::Cmd::Build), int(net::Cmd::Train), int(net::Cmd::Move),
                int(net::Cmd::AttackMove), int(net::Cmd::Attack), int(net::Cmd::Stop));
    return 0;
}
