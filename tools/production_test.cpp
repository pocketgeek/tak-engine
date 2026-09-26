// production_test -- what a factory does with the units it makes.
//
// Two reported bugs and one requested feature, all in tickProduction.
//
// WHERE UNITS GO. Both the spawn point and the walk-off point used to be fixed
// offsets from the building: units emerged on one tile (production waited up to 2.5s
// for it to clear, then spawned there anyway) and were ordered to one of five points
// keyed on `id % 5`. So the sixth unit out of a factory was ordered onto the spot the
// first was still standing on. Bodies are solid and the steering has no local
// avoidance -- routing around things is the router's job, and the router cannot route
// INTO an occupied cell either -- so it pushed at its own countryman for ever. The
// no-headway watchdog does not rescue it: replaceLeg resets the tracker and the router
// re-issues about once a second, so the tracker never builds up (see the KNOWN
// LIMITATION note on replaceLeg). Reported as "built units always try to go to the
// same spot, forever".
//
// Measured on this test's 24-unit run, before and after: 15 stuck / 41 overlapping
// pairs -> 0 and 0. The counts are what this file guards, because the failure is
// statistical -- one unlucky unit proves nothing, a fifth of the output stuck proves
// the mechanism.
//
// RALLY. A production building now accepts move / fight-move / patrol / attack orders
// and queues them; it cannot act on them itself, so they describe what its OUTPUT
// should do. The orders are kept in Unit::rally, deliberately NOT in `orders`: a
// structure must never enter the mover, which turns a unit's heading toward its goal
// and is how right-clicking used to spin a keep. The heading assertion below is what
// keeps that from regressing.

#include "sim/sim.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace tak::sim;

static int g_fail = 0;
static void check(bool cond, const char* what, const std::string& detail = {}) {
    std::printf("  %-62s %s%s%s\n", what, cond ? "ok" : "FAIL",
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!cond) ++g_fail;
}

static UnitType factoryType() {
    UnitType t{};
    t.name = "factory"; t.id = "factory";
    t.maxVel = tak::sim::Fixed::fromFloat(0.0f / 30.0f);                 // maxVel <= 0 => isStructure()
    t.maxHp = 5000;
    t.footX = 7; t.footZ = 12;
    t.isBuilder = true;
    t.workerTime = 1000;          // builds fast, so the test is short
    return t;
}

static UnitType soldierType() {
    UnitType t{};
    t.name = "sol"; t.id = "sol";
    t.maxVel = tak::sim::Fixed::fromFloat(60.0f / 30.0f); t.turnRate = 10000;
    t.maxHp = 100; t.canMove = true;
    t.buildTime = 1;
    t.footX = 2; t.footZ = 2;
    return t;
}

static World* makeWorld(int W, int H) {
    World* w = new World();
    w->setVisPlayer(-1);                        // headless, like the referee
    w->setTerrain(std::vector<uint8_t>(size_t(W) * size_t(H), 100), W, H, /*seaLevel=*/20);
    w->setPathService(true);                    // the real router, as in a game
    return w;
}

static void run(World& w, float seconds) {
    for (int i = 0; i < int(seconds * 30.0f); ++i) w.tick(1.0f / 30.0f);
}

// ---------------------------------------------------------------------------
// 1. A factory running flat out must not pile its output up on one spot.
// ---------------------------------------------------------------------------
// PRODUCTION MUST BE PAID FOR. Trivial, and it is here because it was NOT true
// twice: both times the build duration rounded to ZERO ticks, so
// `buildProgress < total` was 0 < 0 and the accumulate branch -- the only place
// mana is checked and deducted -- never ran, and positive-cost units spawned with
// an empty treasury.
//
// The assertion is "too poor to build produces nothing", not "mana went down".
// Mana going down proves little here: a player's pool is clamped to its storage
// cap every tick (max(storage, 100)), so it falls on its own with no production at
// all -- which is exactly what a first version of this test measured, happily
// passing against the bug it was written to catch.
static void productionNeedsMana() {
    std::printf("production is paid for:\n");
    World& w = *makeWorld(128, 128);
    UnitType fac = factoryType(), sol = soldierType();
    sol.buildCost = 500;                 // the default is 0, i.e. free
    fac.storage = 10000;                 // headroom, so the cap is not what limits us
    const int fid = w.spawn(&fac, 1000, 1000, 0, 0);

    w.player(0).mana = 0;                // cannot afford a single one
    w.train(fid, &sol, 4);
    run(w, 60.0f);
    int made = 0;
    for (const auto& u : w.units())
        if (u.alive() && u.type && !u.type->isStructure() && !u.underConstruction) ++made;
    check(made == 0, "a player with no mana produces nothing",
          std::to_string(made) + " unit(s) appeared");

    // EXACTLY one unit's price, and exactly one unit expected. Funding four and
    // asserting "more than none" hid an overcharge: the per-tick cost divided by the
    // UNCLAMPED duration, so a one-tick build billed a 500-cost unit about 1667 and a
    // player holding its stated price could not afford it at all.
    w.player(0).mana = 500;
    run(w, 60.0f);
    int made2 = 0;
    for (const auto& u : w.units())
        if (u.alive() && u.type && !u.type->isStructure() && !u.underConstruction) ++made2;
    check(made2 == 1, "...and a unit costs exactly its buildCost",
          std::to_string(made2) + " unit(s) for exactly 500 mana");
}

static void outputExistsDuringConstruction() {
    World& w=*makeWorld(128,128);
    UnitType fac=factoryType(),sol=soldierType();
    fac.workerTime=1; sol.buildTime=2; sol.buildCost=60;
    fac.storage=10000;
    const int fid=w.spawn(&fac,1000,1000,0,0);
    w.player(0).mana=10;
    w.train(fid,&sol,1);
    w.tick(1.0f/30.0f);
    check(w.player(0).displayResources.samples.back()[1]>0,
          "factory spending reaches HUD accounting without a mobile buildSiteId");
    const int siteId=w.unit(fid)->productionSiteId;
    check(siteId && w.unit(siteId)->underConstruction && w.unit(siteId)->buildBegun,
          "factory output exists and is unfinished while construction proceeds");
    if (!siteId) { delete &w; return; }
    const auto hash=w.stateHash();
    w.player(0).displayResources.samples[0][0]+=100;
    check(w.stateHash()==hash,"HUD resource history does not affect lockstep state");
    ++w.unit(fid)->buildProgress;
    check(w.stateHash()!=hash,"factory progress participates in the lockstep checksum");
    --w.unit(fid)->buildProgress;
    run(w,1);
    check(w.unit(fid)->productionSiteId==siteId && w.unit(siteId)->underConstruction &&
          w.unit(fid)->buildProgress==10,"mana starvation pauses work on the same unfinished unit");
    w.player(0).mana=50;
    run(w,2);
    check(!w.unit(siteId)->underConstruction && w.unit(fid)->productionSiteId==0 &&
          w.unit(fid)->buildQueue.empty() && w.player(0).mana==0,
          "completion releases the same unit for exactly its remaining cost");
    w.train(fid,&sol,1);
    w.tick(1.0f/30.0f);
    const int canceled=w.unit(fid)->productionSiteId;
    w.dequeue(fid,&sol,1);
    run(w,1);
    check(canceled && !w.unit(canceled)->alive() && w.unit(fid)->productionSiteId==0,
          "canceling production releases the unfinished site to decay");
    w.setUnitCap(3); // producer, earlier output, and one unfinished output
    w.player(0).mana=60;
    w.train(fid,&sol,1);
    w.tick(1.0f/30.0f);
    const int damaged=w.unit(fid)->productionSiteId;
    if (damaged) w.unit(damaged)->hp=Fixed();
    w.tick(1.0f/30.0f);
    check(damaged && !w.unit(damaged)->alive() && w.unit(fid)->productionSiteId==0,
          "construction cannot resurrect output killed before the death sweep");
    w.setUnitCap(2);
    w.player(0).mana=60;
    run(w,3);
    int complete=0;
    for (const auto& u:w.units())
        if (u.alive() && u.type==&sol && !u.underConstruction) ++complete;
    // One output already completed earlier in this fixture, so raise the cap
    // below to admit a replacement and verify completion at that cap.
    w.setUnitCap(3);
    run(w,3);
    int after=0;
    for (const auto& u:w.units())
        if (u.alive() && u.type==&sol && !u.underConstruction) ++after;
    check(after==complete+1,"an admitted construction site can finish at the unit cap");
    delete &w;
}

static void outputDoesNotJam() {
    std::printf("a factory's output does not jam on one spot:\n");
    World& w = *makeWorld(128, 128);
    UnitType fac = factoryType(), sol = soldierType();
    const int fid = w.spawn(&fac, 1000, 1000, 0, 0);
    w.player(0).mana = 1e9f;
    w.train(fid, &sol, 24);
    int firstOverlapTick = -1;
    for (int tick = 0; tick < 180 * 30; ++tick) {
        w.tick(1.0f / 30.0f);
        if (firstOverlapTick >= 0) continue;
        const auto& units = w.units();
        for (size_t a = 0; a < units.size(); ++a) {
            if (!units[a].alive() || units[a].type->isStructure()) continue;
            for (size_t b = a + 1; b < units.size(); ++b) {
                if (!units[b].alive() || units[b].type->isStructure()) continue;
                // All output in this fixture is the same 2x2 soldier. Check
                // cell ownership throughout the run, not just a final snapshot
                // after an illegal overlap might have unwound by itself.
                const int dx = std::abs(footprintOrigin(units[a].x, sol.footX) - footprintOrigin(units[b].x, sol.footX));
                const int dz = std::abs(footprintOrigin(units[a].z, sol.footZ) - footprintOrigin(units[b].z, sol.footZ));
                if (dx < sol.footX && dz < sol.footZ) firstOverlapTick = tick;
            }
        }
    }
    check(firstOverlapTick < 0, "moving output never shares occupied footprint cells",
          firstOverlapTick < 0 ? "5400 ticks checked" : "first overlap at tick " + std::to_string(firstOverlapTick));

    // Snapshot, run on, and see who moved. A unit that still holds orders but has not
    // moved is one pushing at something it will never get past.
    std::vector<std::pair<int, std::pair<float, float>>> was;
    for (const auto& u : w.units())
        if (u.alive() && u.type && !u.type->isStructure()) was.push_back({u.id, {u.x.toFloat(), u.z.toFloat()}});
    // FORTY seconds, not ten: a pair wedged head-on unwedges when one of them
    // re-routes around the other, and the re-ask runs on the randomised deadlines
    // (up to ~(58 x halfCellTicks) ticks for the success shape). A 10s window
    // sampled inside one cadence interval and called retail's own pacing "stuck".
    run(w, 40.0f);

    int total = 0, stuck = 0, lost = 0;
    for (const auto& u : w.units()) {
        if (!u.alive() || !u.type || u.type->isStructure()) continue;
        ++total;
        if (u.orders.empty()) continue;
        for (auto& [id, p] : was)
            if (id == u.id) {
                const float dx = u.x.toFloat() - p.first, dz = u.z.toFloat() - p.second;
                if (std::sqrt(dx * dx + dz * dz) < 8.0f) {
                    ++stuck;
                    // A stationary unit with a live order is RETAIL behaviour in one
                    // place only: pressing at its own goal ring, waiting on a spot a
                    // countryman is parked on (the order stays alive; if the spot
                    // clears it walks in). Stationary anywhere ELSE is the jam this
                    // test exists to catch. The ring is retail's goal-crowding
                    // tolerance, 50 / halfCellTicks cells Chebyshev (0x414563).
                    const auto& legEnd = u.orders[World::currentLeg(u.orders)];
                    const float tolPx =
                        float((u.type->halfCellTicks > 0 ? 50 / u.type->halfCellTicks : 0) * 16 + 16);
                    const float ch = std::max(std::fabs(u.x.toFloat() - legEnd.x.toFloat()),
                                              std::fabs(u.z.toFloat() - legEnd.z.toFloat()));
                    if (ch > tolPx) ++lost;
                }
                break;
            }
    }
    check(total >= 20, "the factory actually produced its queue",
          std::to_string(total) + " units");
    check(lost == 0, "no unit is stalled anywhere but its own goal ring",
          std::to_string(lost) + " of " + std::to_string(total) + " lost (" +
              std::to_string(stuck) + " pressing at their ring, which retail does)");

    // ...and they are not standing inside one another. Bodies are solid and nothing
    // pulls an overlap apart, so a stack made at spawn time is permanent.
    int overlaps = 0;
    std::vector<const Unit*> live;
    for (const auto& u : w.units())
        if (u.alive() && u.type && !u.type->isStructure()) live.push_back(&u);
    for (size_t a = 0; a < live.size(); ++a)
        for (size_t b = a + 1; b < live.size(); ++b) {
            const float need = (float(std::max(live[a]->type->footX, live[a]->type->footZ)) +
                                float(std::max(live[b]->type->footX, live[b]->type->footZ)))
                               * 8.0f * 0.5f;
            const float dx = live[a]->x.toFloat() - live[b]->x.toFloat(), dz = live[a]->z.toFloat() - live[b]->z.toFloat();
            if (std::sqrt(dx * dx + dz * dz) < need * 0.6f) {
                ++overlaps;
                std::printf("    overlap: units %d/%d at (%.2f,%.2f)/(%.2f,%.2f)\n",
                            live[a]->id, live[b]->id, live[a]->x.toFloat(),
                            live[a]->z.toFloat(), live[b]->x.toFloat(), live[b]->z.toFloat());
            }
        }
    check(overlaps == 0, "and no two of them are standing inside each other",
          std::to_string(overlaps) + " overlapping pairs");
    delete &w;
}

// ---------------------------------------------------------------------------
// 2. A queued rally on the building is adopted by everything it makes.
// ---------------------------------------------------------------------------
static void rallyIsAdopted() {
    std::printf("a production building's rally orders:\n");
    World& w = *makeWorld(160, 160);
    UnitType fac = factoryType(), sol = soldierType();
    check(fac.producesUnits(), "a builder structure can hold a rally");
    check(!sol.producesUnits(), "an ordinary unit cannot");

    const int fid = w.spawn(&fac, 600, 600, 0, 0);
    w.player(0).mana = 1e9f;
    w.train(fid, &sol, 6);                       // queue production FIRST...
    w.order(fid, 1600, 600, /*queue=*/false);    // ...then a two-step rally
    w.attackMove(fid, 1600, 200, /*queue=*/true);

    const Unit* f = w.unit(fid);
    check(f->rally.size() == 2, "both rally steps are stored, queued",
          std::to_string(f->rally.size()));
    check(f->rally.size() == 2 && f->rally[1].attackMove,
          "and the second one kept its fight-move flag");
    // The building must NEVER take these as its own orders: the mover turns a unit's
    // heading toward its goal, so a structure in the mover is a spinning keep.
    check(f->orders.empty(), "the building's own order queue stays empty",
          std::to_string(f->orders.size()));
    check(f->heading == tak::sim::Bam(0), "and its heading is untouched",
          std::to_string(f->heading.v));
    // Setting a rally must not cancel what it is building.
    check(!f->buildQueue.empty(), "the production queue survives the rally order",
          std::to_string(f->buildQueue.size()) + " queued");

    run(w, 240.0f);
    int n = 0, arrived = 0;
    for (const auto& u : w.units()) {
        if (!u.alive() || !u.type || u.type->isStructure()) continue;
        ++n;
        const float dx = u.x.toFloat() - 1600.0f, dz = u.z.toFloat() - 200.0f;
        if (std::sqrt(dx * dx + dz * dz) < 220.0f) ++arrived;
    }
    check(n >= 5, "the units were produced", std::to_string(n));
    check(n > 0 && arrived == n, "every one of them followed the rally to its last step",
          std::to_string(arrived) + " of " + std::to_string(n));
    delete &w;
}

// ---------------------------------------------------------------------------
// 3. A rally set BEFORE anything is built still applies, and re-setting replaces.
// ---------------------------------------------------------------------------
static void rallyReplaces() {
    std::printf("re-setting a rally replaces it (unqueued):\n");
    World& w = *makeWorld(160, 160);
    UnitType fac = factoryType(), sol = soldierType();
    const int fid = w.spawn(&fac, 600, 600, 0, 0);
    w.player(0).mana = 1e9f;
    w.order(fid, 1600, 600, false);
    w.order(fid, 300, 1400, false);              // unqueued: replaces, not appends
    check(w.unit(fid)->rally.size() == 1, "an unqueued rally order replaces the plan",
          std::to_string(w.unit(fid)->rally.size()));
    w.train(fid, &sol, 3);
    run(w, 240.0f);
    int n = 0, arrived = 0;
    for (const auto& u : w.units()) {
        if (!u.alive() || !u.type || u.type->isStructure()) continue;
        ++n;
        const float dx = u.x.toFloat() - 300.0f, dz = u.z.toFloat() - 1400.0f;
        if (std::sqrt(dx * dx + dz * dz) < 220.0f) ++arrived;
    }
    check(n > 0 && arrived == n, "and the output follows the NEW plan",
          std::to_string(arrived) + " of " + std::to_string(n));
    delete &w;
}

static void scriptControlsReadinessAndPosition() {
    std::printf("factory script readiness and exact piece position:\n");
    World& w=*makeWorld(160,160);
    UnitType fac=factoryType(),sol=soldierType();
    sol.buildTime=100000;
    auto script=std::make_shared<tak::cob::File>();
    script->pieces={"emit","root"};
    script->scripts={{"Create",0},{"QueryBuildInfo",14}};
    script->code={0x10021001,5,0x10021001,0,0x10082000,
                  0x10021001,100,0x10013000,
                  0x10021001,5,0x10021001,1,0x10082000,0x10065000,
                  0x10021001,0,0x10023002,0,0x10065000};
    fac.productionScript=script;
    fac.productionModel={{{0,0,0},-1,1},{{17,1024,64*65536+37},0,0}};
    const int fid=w.spawn(&fac,600,600,3.14159265f,0);
    w.player(0).mana=100000;
    w.train(fid,&sol,1);
    for (int i=0;i<3;++i) w.tick(1.0f/30.0f);
    check(w.unit(fid)->productionSiteId==0,"sleeping factory script prevents early creation");
    w.tick(1.0f/30.0f);
    const Unit* site=w.unit(w.unit(fid)->productionSiteId);
    check(site && site->underConstruction,"ready signal creates the output on that update");
    check(site && site->x.v==600*65536-17 && site->z.v==664*65536+37 &&
          site->flightY.v==100*65536+1024,"QueryBuildInfo retains all raw coordinate bits");
    delete &w;
}

static void mobileScriptRandomRunsInWorld() {
    World& w=*makeWorld(64,64);
    UnitType mobile=soldierType();
    auto script=std::make_shared<tak::cob::File>();
    script->scripts={{"Create",0}};
    script->code={0x10021001,100,0x10013000,0x10021001,0,0x10021001,9,
                  0x10041000,0x10024000,0x10065000};
    mobile.simulationScript=script;
    const int id=w.spawn(&mobile,256,256);
    std::vector<World::RngObservation> draws;
    w.setRngObserver([&](const auto& draw){if (draw.bound==10) draws.push_back(draw);});
    for (int i=0;i<3;++i) w.tick(1.0f/30.0f);
    check(draws.empty(),"mobile COB sleep waits on simulation ticks");
    w.tick(1.0f/30.0f);
    check(draws.size()==1 && draws[0].bound==10 && draws[0].tick==4,
          "mobile COB random shares gameplay RNG without a factory script");
    w.unit(id)->hp=Fixed();w.tick(1.0f/30.0f);
    check(draws.size()==1,"dead mobile script stops consuming random state");
    delete &w;
}

static void mobileProducerFacesSite() {
    std::printf("mobile conjurer faces its production site:\n");
    for (bool flying : {false,true}) {
        World& w=*makeWorld(128,128);
        UnitType conjurer=soldierType(), output=soldierType();
        conjurer.isBuilder=true;conjurer.canFly=flying;conjurer.workerTime=1;
        conjurer.turnInPlaceRate=1000;conjurer.turnRate=400;conjurer.buildDist=100;
        output.buildTime=10000;
        const int id=w.spawn(&conjurer,1000,1000,1.57079632679f,0);
        w.train(id,&output,1);
        run(w,3.0f);
        const auto* producer=w.unit(id);
        const auto* site=w.unit(producer->productionSiteId);
        check(site && site->underConstruction,"conjure site remains active during facing check");
        if (site) {
            const auto want=retailHeadingToPort(uint16_t(retailDirection(producer->x-site->x,producer->z-site->z).v));
            check(flying ? std::abs(bamDiff(want,producer->heading))<8192 : bamDiff(want,producer->heading)==0,flying ? "flying conjurer faces queued output" : "ground conjurer faces queued output",
                  "heading error="+std::to_string(bamDiff(want,producer->heading)));
        }
        delete &w;
    }
}

static void repairParticles() {
    World w;w.setVisPlayer(-1);
    w.setTerrain(std::vector<uint8_t>(64*64,100),64,64,20);
    UnitType builder;builder.id="repair-worker";builder.maxHp=100;
    builder.isBuilder=builder.canMove=true;builder.buildDist=200;builder.workerTime=10;
    builder.modelTop=32*65536;
    UnitType target;target.id="repair-target";target.maxHp=1000;
    target.buildTime=100;target.buildCost=100;target.modelTop=32*65536;
    const int bid=w.spawn(&builder,200,200),tid=w.spawn(&target,250,200);
    w.unit(tid)->hp=Fixed::fromInt(100);w.player(0).mana=0;
    w.repair(bid,tid,false);
    for(int i=0;i<10;++i)w.tick(1.f/30);
    check(w.unit(bid)->constructionEmissions[0]==0 &&
          w.unit(tid)->constructionEmissions[1]==0,
          "unfunded repair emits no worker or target particles");
    w.player(0).mana=1000;
    for(int i=0;i<10;++i)w.tick(1.f/30);
    const auto* b=w.unit(bid);const auto* t=w.unit(tid);
    check(t->hp>Fixed::fromInt(100) && b->constructionEmissions[0]>0 &&
          b->constructionEmissions[0]==t->constructionEmissions[1] &&
          b->cosmeticConstructionEmitter && t->cosmeticConstructionEmitter &&
          !b->cosmeticConstructionEmitter->particles.empty() &&
          !t->cosmeticConstructionEmitter->particles.empty(),
          "successful repair emits particles at both worker and target");
    const auto emitted=b->constructionEmissions[0];
    w.unit(tid)->hp=Fixed::fromInt(1000);
    for(int i=0;i<180;++i)w.tick(1.f/30);
    check(b->constructionEmissions[0]==emitted &&
          t->constructionEmissions[1]==emitted &&
          (!b->cosmeticConstructionEmitter || b->cosmeticConstructionEmitter->particles.empty()) &&
          (!t->cosmeticConstructionEmitter || t->cosmeticConstructionEmitter->particles.empty()),
          "finished repair stops emission and lets existing particles drain");
}

int main() {
    std::printf("production_test\n");
    {
        World world;world.setVisPlayer(-1);world.setPlayerCount(2);
        UnitType limited=soldierType();limited.totalAllowed=1;
        UnitType ordinary=soldierType();ordinary.id="ordinary";
        const int existing=world.spawn(&limited,100,100,0,0);
        world.unit(existing)->underConstruction=true;
        world.setBenchmarkPlan({{1,{{&limited,200,100,0},{&limited,300,100,1},
                                    {&limited,400,100,1},{&ordinary,500,100,0}}},
                                {2,{{&limited,200,200,0},{&limited,300,200,0}}}},3);
        world.tick(1.0f/30);
        auto count=[&](int player,const UnitType* type) {
            int n=0;for (const auto& u:world.units())
                n+=u.alive() && u.player==player && u.type==type;
            return n;
        };
        check(count(0,&limited)==1 && count(1,&limited)==1 && count(0,&ordinary)==1,
              "benchmark caps are per player, count construction, and preserve ordinary spawns");
        world.unit(existing)->deadFor=0;
        world.tick(1.0f/30);
        check(count(0,&limited)==1 && count(1,&limited)==1,
              "benchmark can replace a dead limited unit without exceeding its cap");
    }
    repairParticles();
    mobileProducerFacesSite();
    productionNeedsMana();
    outputExistsDuringConstruction();
    scriptControlsReadinessAndPosition();
    mobileScriptRandomRunsInWorld();
    outputDoesNotJam();
    rallyIsAdopted();
    rallyReplaces();
    std::printf(g_fail ? "production_test: %d FAILURE(S)\n" : "production_test: all passed\n",
                g_fail);
    return g_fail ? 1 : 0;
}
