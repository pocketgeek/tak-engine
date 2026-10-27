// helpbuild_test -- retail HelpBuild: shared production progress, assist
// eligibility, and a guarding builder that helps (protocol 301).
//
// Retail (KINGDOMS.icd, emulated) keeps ONE remaining fraction per unfinished
// unit (+0x108). Every worker -- the factory or conjurer making it and any
// builder helping -- takes its workertime-scaled work off it and pays mana for
// exactly that work (0x429af0), so helpers finish a factory's output sooner,
// the producer is free from that moment, and the total stays the buildcost.
// Ours kept the producer's own tick counter beside the helper's hp growth: an
// assisted output was marked finished early while the producer held its slot,
// paid the full cost again on top of the helper, kept adding hp to the
// finished unit, and on its own completion re-issued the output orders over
// whatever the player had told the unit since.
//
// HelpBuild eligibility (0x519e60): target alive and unfinished, not the
// helper, SAME owner (an ally's site does not qualify), helper a builder with a
// mover, builderlimited helpers need the type on their build menu. No repeat,
// rally or busy test. The sim enforces it, so a crafted command cannot bypass
// the client.
//
// Guard by a mobile builder (Follow_Ground 0x407f90, stage 1): repair the
// guarded unit when it is damaged, help it when it is unfinished, else help /
// repair what it is working on; the job goes ahead of the guard, which resumes
// after it; a look that finds nothing sleeps 15 + rand(15) ticks.
//
// Every scenario uses synthetic types (no game data) on a flat map.

#include "sim/sim.h"
#include "sim/matchsetup.h"
#include "net/lockstep.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace tak::sim;

static int g_fail = 0;
static void check(bool cond, const char* what, const std::string& detail = {}) {
    std::printf("  %-66s %s%s%s\n", what, cond ? "ok" : "FAIL",
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!cond) ++g_fail;
}

struct Types {
    UnitType factory, sol, builder, limited, conjurer, tower;
    Types() {
        factory.name = factory.id = "factory";
        factory.maxVel = Fixed();                       // a structure
        factory.maxHp = 5000; factory.footX = 6; factory.footZ = 6;
        factory.isBuilder = true; factory.workerTime = 10; factory.storage = 1000000;
        factory.buildMenu = {"sol"};
        factory.buildTime = 60; factory.buildCost = 600;

        sol.name = sol.id = "sol";
        sol.maxVel = Fixed::fromFloat(60.0f / 30.0f); sol.turnRate = 10000; sol.turnInPlaceRate = 10000;
        sol.canMove = true; sol.maxHp = 300; sol.footX = sol.footZ = 2;
        sol.buildTime = 60; sol.buildCost = 600;        // 180 ticks at workertime 10
        sol.healTime = 0;

        builder = sol;
        builder.name = builder.id = "builder";
        builder.isBuilder = true; builder.workerTime = 10; builder.buildDist = 60;
        builder.buildMenu = {"factory"};               // not limited: the menu is not consulted

        limited = builder;
        limited.name = limited.id = "limited";
        limited.builderLimited = true; limited.buildMenu = {"factory"};

        conjurer = builder;                             // a Zhon-style mobile producer
        conjurer.name = conjurer.id = "conjurer";
        conjurer.buildMenu = {"sol"};

        tower = factory;                                // a builder structure (never helps)
        tower.name = tower.id = "tower";
    }
};

static std::unique_ptr<World> makeWorld() {
    auto w = std::make_unique<World>();
    w->setVisPlayer(-1);
    w->setTerrain(std::vector<uint8_t>(size_t(128) * 128, 100), 128, 128, 20);
    w->setPathService(true);
    w->player(0).mana = 100000;
    w->player(1).mana = 100000;
    return w;
}

static const TypeRegistry& emptyRegistry() { static TypeRegistry reg; return reg; }

static void command(World& w, tak::net::Cmd kind, int unitId, int targetId, int player = 0) {
    tak::net::Command c;
    c.kind = kind; c.player = uint8_t(player); c.unitId = unitId; c.targetId = targetId;
    applyCommand(w, emptyRegistry(), c);
}

// ---------------------------------------------------------------------------
// One producer making one `sol`, optionally helped once its output exists.
// ---------------------------------------------------------------------------
struct ProductionRun {
    int producer = 0, helper = 0, site = 0;
    int siteTick = -1;        // the tick the output was spawned
    int finishTick = -1;      // the tick it stopped being unfinished
    bool freedAtFinish = false, helperFreedAtFinish = false;
    double manaAtSite = 0, manaSpent = 0;
    float hpAfterDamage = 0, hpLater = 0;
    size_t ordersAtFinish = 0;
    float finalGoalX = 0, finalGoalZ = 0;
    bool moved = false;
    uint64_t hash = 0;
};

static ProductionRun runProduction(bool mobileProducer, bool help, bool moveAfter, bool rally = false) {
    Types ty;
    auto ownedWorld = makeWorld();
    World& w = *ownedWorld;
    ProductionRun r;
    r.producer = mobileProducer ? w.spawn(&ty.conjurer, 1000, 1000, 0.0f, 0) : w.spawn(&ty.factory, 1000, 1000, 0.0f, 0);
    if (mobileProducer) w.spawn(&ty.factory, 300, 300, 0.0f, 0);   // storage for the pool
    if (help) r.helper = w.spawn(&ty.builder, 1000, mobileProducer ? 1110 : 1150, 0.0f, 0);
    if (rally) { tak::net::Command c; c.kind = tak::net::Cmd::Move; c.unitId = r.producer; c.x = 1500; c.z = 1500; applyCommand(w, emptyRegistry(), c); }
    w.train(r.producer, &ty.sol, 1);
    for (int tick = 0; tick < 1200; ++tick) {
        w.tick(1.0f / 30.0f);
        const Unit* p = w.unit(r.producer);
        if (!r.site && p->productionSiteId) {
            r.site = p->productionSiteId; r.siteTick = tick; r.manaAtSite = w.player(0).mana;
            // Measure from the first tick of work: the spawn tick already did one.
            r.manaAtSite += 600.0 / 180.0;
            if (help) w.assist(r.helper, r.site, false);
        }
        const Unit* s = r.site ? w.unit(r.site) : nullptr;
        if (s && r.finishTick < 0 && !s->underConstruction) {
            r.finishTick = tick;
            r.manaSpent = r.manaAtSite - w.player(0).mana;
            r.freedAtFinish = w.unit(r.producer)->productionSiteId != r.site;
            r.helperFreedAtFinish = !help || w.unit(r.helper)->buildSiteId == 0;
            // The producer may finish it after the helper's turn this tick;
            // the helper's job ends on its next look.
            if (help && !r.helperFreedAtFinish) {
                w.tick(1.0f / 30.0f);
                r.helperFreedAtFinish = w.unit(r.helper)->buildSiteId == 0;
            }
            r.ordersAtFinish = s->orders.size();
            if (moveAfter) {
                tak::net::Command c; c.kind = tak::net::Cmd::Move; c.unitId = r.site; c.x = 700; c.z = 1400;
                applyCommand(w, emptyRegistry(), c);
                r.moved = true;
            }
            // Damage it: nothing may add hit points to a finished unit (healtime 0).
            w.unit(r.site)->hp = Fixed::fromInt(100);
        }
        if (r.finishTick >= 0 && tick == r.finishTick + 5) r.hpAfterDamage = w.unit(r.site)->hp.toFloat();
        if (r.finishTick >= 0 && tick >= r.finishTick + 400) break;
    }
    if (const Unit* s = r.site ? w.unit(r.site) : nullptr) {
        r.hpLater = s->hp.toFloat();
        if (!s->orders.empty()) {
            const Order& last = s->orders[World::currentLeg(s->orders)];
            r.finalGoalX = last.x.toFloat(); r.finalGoalZ = last.z.toFloat();
        }
        r.moved = r.moved && std::fabs(s->x.toFloat() - 700) < 40 && std::fabs(s->z.toFloat() - 1400) < 40;
    }
    r.hash = w.stateHash();
    return r;
}

static void sharedProduction(bool mobile) {
    std::printf("%s output, helped vs alone:\n", mobile ? "conjurer (Zhon)" : "factory");
    const auto solo = runProduction(mobile, false, false);
    const auto helped = runProduction(mobile, true, false);
    char d[256];
    std::snprintf(d, sizeof d, "solo: site t=%d finish t=%d (%d ticks) mana %.4f hash %016llx", solo.siteTick, solo.finishTick,
                  solo.finishTick - solo.siteTick, solo.manaSpent, (unsigned long long)solo.hash);
    // 180 ticks of work: the spawn tick does the first one.
    check(solo.finishTick - solo.siteTick == 179, "alone, the output takes exactly its 180 ticks of work", d);
    check(std::fabs(solo.manaSpent - 600) < 0.01, "...and costs exactly its buildcost", d);
    check(solo.freedAtFinish, "...and the producer is freed as it finishes", d);
    std::snprintf(d, sizeof d, "helped: site t=%d finish t=%d (%d ticks) mana %.4f", helped.siteTick, helped.finishTick,
                  helped.finishTick - helped.siteTick, helped.manaSpent);
    check(helped.finishTick > 0 && helped.finishTick - helped.siteTick < 150,
          "an equal helper finishes the output well inside the producer's time", d);
    check(std::fabs(helped.manaSpent - 600) < 0.01, "...for its buildcost in total, not paid twice", d);
    check(helped.freedAtFinish, "...and the producer is freed at that very tick", d);
    check(helped.helperFreedAtFinish, "...as is the helper (by its next look)", d);
    std::snprintf(d, sizeof d, "hp 5 ticks after damage %.2f, 400 ticks after %.2f", helped.hpAfterDamage, helped.hpLater);
    check(std::fabs(helped.hpAfterDamage - 100) < 0.01 && std::fabs(helped.hpLater - 100) < 0.01,
          "no hit points are added to the finished unit", d);
}

static void outputOrdersOnce(bool mobile) {
    std::printf("%s output orders are given once:\n", mobile ? "conjurer" : "factory");
    const auto r = runProduction(mobile, true, true);
    char d[160];
    std::snprintf(d, sizeof d, "orders at finish %zu, final goal (%.0f,%.0f), reached=%d", r.ordersAtFinish,
                  r.finalGoalX, r.finalGoalZ, int(r.moved));
    check(r.ordersAtFinish >= 1, "the finished output gets its release order at completion", d);
    check(r.moved, "a Move the player gives it then is never overridden later", d);
    if (!mobile) {
        const auto rr = runProduction(false, true, false, true);
        std::snprintf(d, sizeof d, "orders at finish %zu, final goal (%.0f,%.0f)", rr.ordersAtFinish, rr.finalGoalX, rr.finalGoalZ);
        check(std::fabs(rr.finalGoalX - 1500) < 16 && std::fabs(rr.finalGoalZ - 1500) < 16,
              "a factory rally is handed over once and the output keeps it", d);
    }
}

// ---------------------------------------------------------------------------
// Eligibility (0x519e60), through applyCommand (what a client can send).
// ---------------------------------------------------------------------------
static void eligibility() {
    std::printf("HelpBuild eligibility (sim-enforced):\n");
    Types ty;
    auto ownedWorld = makeWorld();
    World& w = *ownedWorld;
    w.setTeam(1, 0);                                     // player 1 is player 0's ally
    const int fac = w.spawn(&ty.factory, 1000, 1000, 0.0f, 0);
    w.train(fac, &ty.sol, 5);
    const int allyFac = w.spawn(&ty.factory, 1400, 1000, 0.0f, 1);
    w.train(allyFac, &ty.sol, 5);
    const int enemyFac = w.spawn(&ty.factory, 600, 1000, 0.0f, 2);
    w.player(2).mana = 100000;
    w.train(enemyFac, &ty.sol, 5);
    for (int i = 0; i < 10; ++i) w.tick(1.0f / 30.0f);
    const int own = w.unit(fac)->productionSiteId, ally = w.unit(allyFac)->productionSiteId,
              enemy = w.unit(enemyFac)->productionSiteId;
    check(own && ally && enemy, "three factories have outputs under way");
    const int b = w.spawn(&ty.builder, 1000, 1200, 0.0f, 0);
    const int lim = w.spawn(&ty.limited, 1050, 1200, 0.0f, 0);
    const int sol = w.spawn(&ty.sol, 950, 1200, 0.0f, 0);
    const int tower = w.spawn(&ty.tower, 300, 300, 0.0f, 0);
    const int finished = w.spawn(&ty.sol, 900, 1200, 0.0f, 0);
    const auto accepted = [&](int helper, int target) {
        if (Unit* u = w.unit(helper)) u->buildSiteId = 0;
        command(w, tak::net::Cmd::Assist, helper, target);
        return w.unit(helper)->buildSiteId == target;
    };
    check(accepted(b, own), "a mobile builder helps its own factory's output");
    check(!accepted(b, ally), "...but not an ally's");
    check(!accepted(b, enemy), "...nor an enemy's");
    check(!accepted(sol, own), "a non-builder does not help");
    check(!accepted(tower, own), "a builder structure does not help");
    check(!accepted(b, finished), "a finished unit is not helped");
    check(!accepted(b, b), "a builder does not help itself");
    check(!accepted(lim, own), "a builderlimited builder needs the type on its menu");
    // A crafted client could call around the command checks; World::assist
    // applies the same rules.
    w.unit(lim)->buildSiteId = 0;
    w.assist(lim, own, false);
    check(w.unit(lim)->buildSiteId != own, "...and World::assist refuses it too");
    w.assist(b, ally, false);
    check(w.unit(b)->buildSiteId != ally, "World::assist refuses an ally's site");
    UnitType onMenu = ty.limited; onMenu.id = "onmenu"; onMenu.buildMenu = {"sol"};
    const int lim2 = w.spawn(&onMenu, 1100, 1200, 0.0f, 0);
    check(accepted(lim2, own), "a builderlimited builder with the type on its menu helps");
}

// A conjurer on a repeat queue: an Assist is an Assist, a Move is a rally.
static void repeatConjurer() {
    std::printf("a repeating conjurer:\n");
    Types ty;
    auto ownedWorld = makeWorld();
    World& w = *ownedWorld;
    w.spawn(&ty.factory, 300, 300, 0.0f, 0);
    const int fac = w.spawn(&ty.factory, 1000, 1000, 0.0f, 0);
    w.train(fac, &ty.sol, 3);
    const int conj = w.spawn(&ty.conjurer, 1000, 1300, 0.0f, 0);
    w.setRepeat(conj, &ty.sol);
    for (int i = 0; i < 10; ++i) w.tick(1.0f / 30.0f);
    const int own = w.unit(fac)->productionSiteId;
    const int mine = w.unit(conj)->productionSiteId;
    tak::net::Command mv; mv.kind = tak::net::Cmd::Move; mv.unitId = conj; mv.x = 200; mv.z = 1800;
    applyCommand(w, emptyRegistry(), mv);
    check(w.unit(conj)->orders.empty() && !w.unit(conj)->rally.empty(), "its Move is still a rally for its output");
    command(w, tak::net::Cmd::Assist, conj, own);
    check(w.unit(conj)->buildSiteId == own, "an Assist on an unfinished own unit is accepted");
    check(w.unit(conj)->repeatType == &ty.sol, "...keeping its repeat queue");
    const int32_t before = w.unit(conj)->buildProgress;
    for (int i = 0; i < 30; ++i) w.tick(1.0f / 30.0f);
    check(w.unit(conj)->buildProgress == before && w.unit(conj)->productionSiteId == mine,
          "...and its own output waits while it helps");
    for (int i = 0; i < 600 && w.unit(conj)->buildSiteId; ++i) w.tick(1.0f / 30.0f);
    for (int i = 0; i < 30; ++i) w.tick(1.0f / 30.0f);
    check(w.unit(conj)->buildProgress > before || w.unit(conj)->productionSiteId != mine,
          "...and resumes when the help ends");
}

// ---------------------------------------------------------------------------
// Guard (Follow_Ground stage 1).
// ---------------------------------------------------------------------------
static int guardLatch(World& w, int guard, int ticks, int want, bool repair) {
    for (int i = 0; i < ticks; ++i) {
        w.tick(1.0f / 30.0f);
        const Unit* g = w.unit(guard);
        if (repair ? g->repairId == want : g->buildSiteId == want) return i;
    }
    return -1;
}

static bool guardUnder(const World& w, int guard) {
    const Unit* g = w.unit(guard);
    return !g->orders.empty() && g->orders.back().guard;
}

static void guardHelps() {
    std::printf("a guarding builder:\n");
    Types ty;
    char d[160];
    {   // guards a producing factory -> helps its output, guard resumes after
        auto ownedWorld = makeWorld();
        World& w = *ownedWorld;
        const int fac = w.spawn(&ty.factory, 1000, 1000, 0.0f, 0);
        w.train(fac, &ty.sol, 2);
        const int g = w.spawn(&ty.builder, 1060, 1150, 0.0f, 0);
        w.tick(1.0f / 30.0f);
        const int site = w.unit(fac)->productionSiteId;
        command(w, tak::net::Cmd::Guard, g, fac);
        const int at = guardLatch(w, g, 5, site, false);
        std::snprintf(d, sizeof d, "latched after %d ticks", at);
        check(at >= 0, "guarding a busy factory, it helps the factory's output at once", d);
        check(guardUnder(w, g), "...with its guard order kept under the help");
        int finish = -1;
        for (int i = 0; i < 400 && finish < 0; ++i) {
            w.tick(1.0f / 30.0f);
            if (const Unit* s = w.unit(site); s && !s->underConstruction) finish = i;
        }
        check(finish >= 0 && finish < 150, "...which finishes sooner than the factory alone",
              "finished " + std::to_string(finish) + " ticks after latching");
        // The next output starts once the finished one has cleared the pad;
        // the guard's next look (15..29 ticks apart while idle) picks it up.
        int next = 0;
        for (int i = 0; i < 300 && !(next = w.unit(fac)->productionSiteId); ++i) w.tick(1.0f / 30.0f);
        const int again = guardLatch(w, g, 31, next, false);
        check(next && next != site && again >= 0, "...and it helps the next output from its next look",
              "next site " + std::to_string(next) + " latched after " + std::to_string(again));
        for (int i = 0; i < 400 && w.unit(g)->buildSiteId; ++i) w.tick(1.0f / 30.0f);
        for (int i = 0; i < 3; ++i) w.tick(1.0f / 30.0f);
        check(!w.unit(g)->orders.empty() && w.unit(g)->orders.front().guard &&
              w.unit(g)->orders.front().targetId == fac, "with the queue done, the guard order is in front again");
    }
    {   // guards a builder that is helping a site -> helps that site; repair of an ally
        auto ownedWorld = makeWorld();
        World& w = *ownedWorld;
        w.setTeam(1, 0);
        const int fac = w.spawn(&ty.factory, 1000, 1000, 0.0f, 0);
        w.train(fac, &ty.sol, 1);
        const int worker = w.spawn(&ty.builder, 1000, 1150, 0.0f, 0);
        const int g = w.spawn(&ty.builder, 1200, 1300, 0.0f, 0);
        w.tick(1.0f / 30.0f);
        const int site = w.unit(fac)->productionSiteId;
        w.assist(worker, site, false);
        for (int i = 0; i < 20; ++i) w.tick(1.0f / 30.0f);
        command(w, tak::net::Cmd::Guard, g, worker);
        check(guardLatch(w, g, 3, site, false) >= 0, "guarding a helping builder, it helps that same output");
        // ally repair through the guarded unit's RepairUnit
        const int allyUnit = w.spawn(&ty.sol, 700, 1300, 0.0f, 1);
        w.unit(allyUnit)->hp = Fixed::fromInt(100);
        const int g2 = w.spawn(&ty.builder, 600, 1500, 0.0f, 0);
        const int repairer = w.spawn(&ty.builder, 650, 1350, 0.0f, 0);
        w.repair(repairer, allyUnit, false);
        for (int i = 0; i < 3; ++i) w.tick(1.0f / 30.0f);
        command(w, tak::net::Cmd::Guard, g2, repairer);
        check(guardLatch(w, g2, 3, allyUnit, true) >= 0, "guarding a repairer, it repairs the ally being repaired");
        check(guardUnder(w, g2), "...guard order kept under the repair");
        // the guarded unit itself damaged -> repair it
        const int hurt = w.spawn(&ty.sol, 1600, 1600, 0.0f, 0);
        w.unit(hurt)->hp = Fixed::fromInt(50);
        const int g3 = w.spawn(&ty.builder, 1650, 1650, 0.0f, 0);
        command(w, tak::net::Cmd::Guard, g3, hurt);
        check(guardLatch(w, g3, 3, hurt, true) >= 0, "a damaged guarded unit is repaired");
        for (int i = 0; i < 900 && w.unit(g3)->repairId; ++i) w.tick(1.0f / 30.0f);
        check(w.unit(hurt)->hp.toFloat() >= 299.9f && !w.unit(g3)->orders.empty() && w.unit(g3)->orders.front().guard,
              "...to full, then the guard resumes");
    }
    {   // negative cases: idle guarded unit, off-menu limited guard, enemy-free
        auto ownedWorld = makeWorld();
        World& w = *ownedWorld;
        const int idle = w.spawn(&ty.sol, 1000, 1000, 0.0f, 0);
        const int g = w.spawn(&ty.builder, 1100, 1100, 0.0f, 0);
        command(w, tak::net::Cmd::Guard, g, idle);
        w.tick(1.0f / 30.0f);
        const Unit* gu = w.unit(g);
        const uint32_t wait = gu->guardHelpTick - (w.tickCount() - 1);
        std::snprintf(d, sizeof d, "next look in %u ticks", wait);
        check(!gu->buildSiteId && !gu->repairId && gu->orders.size() == 1 && gu->orders.front().guard,
              "guarding an idle unit, it only escorts", d);
        check(wait >= 15 && wait < 30, "...and looks again after 15 + rand(15) ticks", d);
        bool spread = false;
        uint32_t prev = gu->guardHelpTick;
        for (int i = 0; i < 200; ++i) {
            w.tick(1.0f / 30.0f);
            if (w.unit(g)->guardHelpTick != prev) {
                const uint32_t gap = w.unit(g)->guardHelpTick - prev;
                if (gap < 15 || gap >= 30) { spread = false; break; }
                if (gap != wait) spread = true;
                prev = w.unit(g)->guardHelpTick;
            }
        }
        check(spread, "...each look re-arming 15..29 ticks out, drawn afresh");
        const int fac = w.spawn(&ty.factory, 400, 400, 0.0f, 0);
        w.train(fac, &ty.sol, 1);
        const int lim = w.spawn(&ty.limited, 500, 600, 0.0f, 0);
        w.tick(1.0f / 30.0f);
        command(w, tak::net::Cmd::Guard, lim, fac);
        check(guardLatch(w, lim, 40, w.unit(fac)->productionSiteId, false) < 0,
              "a builderlimited guard does not help an output off its menu");
        // A non-builder guard escorts only.
        const int s2 = w.spawn(&ty.sol, 500, 650, 0.0f, 0);
        command(w, tak::net::Cmd::Guard, s2, fac);
        for (int i = 0; i < 40; ++i) w.tick(1.0f / 30.0f);
        check(!w.unit(s2)->buildSiteId, "a non-builder guard only escorts");
    }
    {   // a builder walking to a placed site is just followed until the site is begun
        auto ownedWorld = makeWorld();
        World& w = *ownedWorld;
        w.spawn(&ty.factory, 300, 1800, 0.0f, 0);         // storage for the pool
        const int walker = w.spawn(&ty.builder, 400, 400, 0.0f, 0);
        const int g = w.spawn(&ty.builder, 330, 480, 0.0f, 0);
        w.queueBuild(walker, &ty.factory, 1600, 1600, false);
        command(w, tak::net::Cmd::Guard, g, walker);
        int site = 0, begunTick = -1, latchTick = -1;
        for (int i = 0; i < 1500 && latchTick < 0; ++i) {
            w.tick(1.0f / 30.0f);
            if (!site) site = w.unit(walker)->buildSiteId;
            const Unit* s = site ? w.unit(site) : nullptr;
            if (s && s->buildBegun && begunTick < 0) begunTick = i;
            if (site && w.unit(g)->buildSiteId == site) latchTick = i;
        }
        std::snprintf(d, sizeof d, "begun t=%d, guard latched t=%d", begunTick, latchTick);
        check(begunTick >= 0 && latchTick >= begunTick, "a builder still walking to its site is followed, then helped", d);
        for (int i = 0; i < 1500 && w.unit(walker)->buildSiteId; ++i) w.tick(1.0f / 30.0f);
        const Unit* s = w.unit(site);
        check(s && !s->underConstruction && !w.unit(g)->buildSiteId && !w.unit(g)->orders.empty() &&
              w.unit(g)->orders.front().guard, "...the site is finished and the guard order resumes");
    }
}

// ---------------------------------------------------------------------------
// Determinism: the same mixed scenario twice gives the same hash.
// ---------------------------------------------------------------------------
static uint64_t mixedRun() {
    Types ty;
    auto ownedWorld = makeWorld();
    World& w = *ownedWorld;
    const int fac = w.spawn(&ty.factory, 1000, 1000, 0.0f, 0);
    w.train(fac, &ty.sol, 4);
    const int conj = w.spawn(&ty.conjurer, 1500, 1000, 0.0f, 0);
    w.train(conj, &ty.sol, 3);
    std::vector<int> guards;
    for (int i = 0; i < 3; ++i) guards.push_back(w.spawn(&ty.builder, 1100.0f + 60 * i, 1400, 0.0f, 0));
    const int helper = w.spawn(&ty.builder, 1500, 1200, 0.0f, 0);
    command(w, tak::net::Cmd::Guard, guards[0], fac);
    command(w, tak::net::Cmd::Guard, guards[1], conj);
    command(w, tak::net::Cmd::Guard, guards[2], guards[0]);
    for (int i = 0; i < 1500; ++i) {
        w.tick(1.0f / 30.0f);
        if (i % 97 == 0 && w.unit(conj)->productionSiteId) w.assist(helper, w.unit(conj)->productionSiteId, false);
    }
    return w.stateHash();
}

static void determinism() {
    std::printf("determinism:\n");
    const uint64_t a = mixedRun(), b = mixedRun();
    char d[64];
    std::snprintf(d, sizeof d, "%016llx / %016llx", (unsigned long long)a, (unsigned long long)b);
    check(a == b, "guard-help, assist and shared production hash the same twice", d);
}

int main() {
    sharedProduction(false);
    sharedProduction(true);
    outputOrdersOnce(false);
    outputOrdersOnce(true);
    eligibility();
    repeatConjurer();
    guardHelps();
    determinism();
    std::printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
