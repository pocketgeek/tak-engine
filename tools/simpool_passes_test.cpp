// WE E4.3: the tick passes that run on the SimPool (the spatial rebuild, the
// turn reset and economy walk, the producer walk, the ability lists, the aura
// relax and gathers, the turn-delta walk) give the serial result. One world
// above every pool threshold (kSpatialMinParallel, kPrologueMinParallel,
// kPostMinParallel: 2048 units; more than 8 aura emitters) is ticked serially
// and at 1, 3 and 7 workers; the state hashes must agree every tick. A debug
// build also sets TAK_VERIFY_POOL, which redoes each pooled pass serially and
// aborts on any difference in its lists, sums, chains or stamps.
#include "sim/sim.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

int main() {
#ifndef NDEBUG
#ifdef _WIN32
    _putenv_s("TAK_VERIFY_POOL", "1");
#else
    setenv("TAK_VERIFY_POOL", "1", 1);
#endif
#endif
    using namespace tak::sim;
    UnitType soldier{}, big{}, shaman{}, lode{}, reclaimer{}, flyer{};
    auto mobile = [](UnitType& t, const char* id, int foot) {
        t.name = t.id = id; t.maxHp = 100; t.maxVel = Fixed::fromInt(1); t.canMove = true;
        t.footX = t.footZ = foot; t.sight = 160;
    };
    mobile(soldier, "soldier", 1);
    mobile(big, "big", 2);
    mobile(shaman, "shaman", 1);
    shaman.auras = {Aura{Aura::Kind::Armor, 1.25f, false, 96, 0.5f}, Aura{Aura::Kind::Attack, 0.8f, true, 80, 1.0f}};
    mobile(reclaimer, "reclaimer", 1);
    reclaimer.canReclaim = true;
    mobile(flyer, "flyer", 1);
    flyer.canFly = true;
    lode.name = lode.id = "lode"; lode.maxHp = 400; lode.maxVel = Fixed(); lode.footX = lode.footZ = 2;
    lode.income = 1.5f; lode.storage = 200;
    const UnitType* types[] = {&soldier, &soldier, &soldier, &big, &shaman, &reclaimer, &flyer, &lode};
    std::vector<uint8_t> terrain(160 * 160);
    for (int z = 0; z < 160; ++z) for (int x = 0; x < 160; ++x) terrain[size_t(z) * 160 + x] = uint8_t((x * 5 + z * 3) % 40);
    const int widths[] = {-1, 1, 3, 7};   // -1: serial (setSerialThreads)
    std::unique_ptr<World> worlds[4];
    for (int w = 0; w < 4; ++w) {
        auto& world = worlds[w];
        world = std::make_unique<World>();
        world->setVisPlayer(-1); world->setPlayerCount(4); world->setTerrain(terrain, 160, 160, 30);
        world->setTeam(1, 0);
        if (widths[w] < 0) world->setSerialThreads(true); else world->setSimWorkers(widths[w]);
        for (int i = 0; i < 2600; ++i) {
            const UnitType* t = types[size_t(i) % 8];
            const int id = world->spawn(t, float(64 + (i * 37) % 2300), float(64 + (i * 53) % 2300), 0.0f, i % 4);
            if (Unit* u = world->unit(id); u && t == &lode && i % 3 == 0) u->underConstruction = true;
        }
    }
    for (int step = 0; step < 120; ++step) {
        uint64_t first = 0;
        for (int w = 0; w < 4; ++w) {
            World& world = *worlds[w];
            if (step % 20 == 0)
                for (size_t i = 0; i < world.units().size(); i += 3) {
                    const Unit& u = world.units()[i];
                    if (u.alive() && u.type && !u.type->isStructure())
                        world.order(u.id, float(64 + (i * 91 + size_t(step) * 13) % 2300),
                                    float(64 + (i * 29 + size_t(step) * 7) % 2300), false);
                }
            world.tick(1.f / 30);
            const uint64_t h = world.stateHash();
            if (w == 0) first = h;
            else if (h != first) {
                std::fprintf(stderr, "workers=%d differs from serial at step %d\n", widths[w], step);
                return 1;
            }
        }
    }
    std::printf("pooled passes match serial at 1/3/7 workers (hash %016llx)\n",
                (unsigned long long)worlds[0]->stateHash());
}
