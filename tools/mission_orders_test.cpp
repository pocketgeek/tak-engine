// mission_orders_test -- the campaign order strings (InitialMission= and the god
// script's SetMission) read the way retail's parser 0x513fb0 reads them.
//
// Each clause is cut at a comma and scanned on its own, so no clause reads into
// the next; and the letters mean what they mean in retail:
//   o A       2 offensive / 1 defensive / 0 passive (0x5198a0); o A B the fields
//   b TYPE N  a factory builds N (BuildingBuild); b TYPE N X Y a mobile builder
//             builds TYPE at X,Y (MobileBuild) -- neither spawns anything free
//   i NAME    start aboard the transport whose Ident= is NAME (51b4f0)
//   g NAME / wa NAME / a NAME  the placed unit NAME (Ident=, else Unitname)
//   wa        hold until the watched unit is HIT (WaitForAttack 4017a0)
//   w F N     a wait that a visible enemy within N cells ends (Wait 401480)
//   v F       a fixed veterancy multiplier (unit +0xf4): rank 10, kills can't move it
//   a TYPE    AttackUType: every unit of TYPE, one after another, until none
//   s / d     no owner change / a self-destruct queued in its place
//
// Synthetic types from a scratch FBI tree, on a flat map. With a retail install
// (`mission_orders_test --data <dir>`) it checks shipped missions instead: takx04's
// Flying Builder builds its shipyard when the lodestone JIM is attacked rather than
// conjuring it at start, its keeps queue their waves, and takx07's soldiers start
// aboard their transports.

#include "sim/mission.h"
#include "sim/sim.h"
#include "sim/matchsetup.h"
#include "sim/footprint.h"
#include "hpi/hpi.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace tak;
using sim::Fixed;
using sim::Order;
using sim::ScriptedOrder;
using sim::Unit;
using sim::World;

static int g_fail = 0;
static void check(bool ok, const std::string& what, const std::string& detail = {}) {
    std::printf("  %-70s %s%s%s\n", what.c_str(), ok ? "ok" : "FAIL",
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) ++g_fail;
}

static void tickN(World& w, int n) { for (int i = 0; i < n; ++i) w.tick(1.0f / 30.0f); }

static float cell(float c) { return c * 16.0f + 8.0f; }   // MissionScript::cellToWorld

static const ScriptedOrder* scripted(const World& w, const Order& o) { return w.scriptedOrder(o); }
static bool isKind(const World& w, const Order& o, ScriptedOrder::Kind k) {
    const ScriptedOrder* s = scripted(w, o);
    return s && s->kind == k;
}

// Kill `id` as a weapon would: no hp left, the last hit from `killer`.
static void kill(World& w, int id, int killer) {
    Unit* u = w.unit(id);
    const Unit* k = w.unit(killer);
    u->hp = Fixed(); u->deathType = 1;
    u->lastHitBy = killer; u->lastHitPlayer = k ? k->player : -1;
}

// ---- the scratch data --------------------------------------------------------

struct Fixture {
    std::filesystem::path root;
    hpi::Vfs vfs;
    sim::TypeRegistry reg;
    Fixture() {
        root = std::filesystem::temp_directory_path() /
               ("tak-mission-orders-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "units");
        std::filesystem::create_directories(root / "canbuild" / "builder");
        auto fbi = [&](const char* name, const std::string& body, const std::string& weapon = {}) {
            std::ofstream f(root / "units" / (std::string(name) + ".fbi"));
            f << "[UNITINFO]\n{\nunitname=" << name << ";\nobjectname=" << name << ";\n" << body << "}\n" << weapon;
        };
        const std::string mobile = "canmove=1;\nmaxvelocity=1.5;\nturnrate=2000;\nturninplacerate=2000;\n"
                                   "footprintx=2;\nfootprintz=2;\nsightdistance=200;\nmaxdamage=500;\n";
        const std::string melee = "[WEAPON1]\n{\nname=Blade;\ntype=Melee;\nrange=30;\nreloadtime=1;\n"
                                  "[DAMAGE]\n{\ndefault=100;\n}\n}\n";
        fbi("soldier", mobile + "buildtime=60;\nbuildcost=10;\n", melee);
        fbi("target", mobile);
        fbi("stubborn", mobile + "unitstandorders=0;\n", melee);
        fbi("plain", mobile + "noveteran=1;\n", melee);
        fbi("builder", mobile + "builder=1;\nworkertime=10;\nbuilddistance=60;\n");
        fbi("wagon", "canmove=1;\nmaxvelocity=2;\nturnrate=4000;\nturninplacerate=4000;\nfootprintx=3;\n"
                     "footprintz=3;\nmaxdamage=800;\ncantransport=1;\ntransportcapacity=4;\ntransportsize=4;\n"
                     "transportsizecapacity=16;\ntransportdistance=150;\n");
        fbi("keep", "footprintx=4;\nfootprintz=4;\nmaxdamage=5000;\nbuilder=1;\nworkertime=10;\n");
        fbi("hut", "footprintx=2;\nfootprintz=2;\nmaxdamage=1000;\nbuildtime=60;\nbuildcost=10;\n");
        std::ofstream(root / "canbuild" / "builder" / "hut.tdf") << "[Menu]\n{\nPriority=1;\n}\n";
        vfs.addLayer(hpi::MountSet(root));
        reg.loadDir(vfs, "units");
        reg.loadBuildTree(vfs, "canbuild");
    }
    ~Fixture() { std::error_code ec; std::filesystem::remove_all(root, ec); }
    const sim::UnitType* t(const char* name) const { return reg.find(name); }
};

static std::unique_ptr<World> makeWorld() {
    auto w = std::make_unique<World>();
    w->setTerrain(std::vector<uint8_t>(size_t(128) * 128, 100), 128, 128, 20);
    w->setPathService(true);
    w->player(0).team = 0; w->player(1).team = 1;
    w->player(0).mana = 1e6; w->player(1).mana = 1e6;
    return w;
}

// One placed unit of a mission: its type, cell, owner, Ident= and InitialMission=.
struct Placed { const char* type; int cx, cz, player; const char* ident; const char* orders; int id = 0; };

// Spawn `placed` in file order and start a mission whose only script is their
// InitialMission strings.
static std::unique_ptr<sim::MissionScript> startMission(Fixture& f, World& w, std::vector<Placed>& placed) {
    std::vector<sim::MissionScript::Placement> table;
    std::vector<std::pair<int, std::string>> orders;
    for (auto& p : placed) {
        p.id = w.spawn(f.t(p.type), cell(float(p.cx)), cell(float(p.cz)), 0.0f, p.player);
        table.push_back({p.ident, p.type, p.id});
        if (p.orders && *p.orders) orders.push_back({p.id, p.orders});
    }
    auto m = std::make_unique<sim::MissionScript>(std::vector<uint8_t>{}, tdf::Node{}, f.reg, 0, "orders fixture");
    m->setInitialOrders(std::move(orders));
    m->setPlacements(std::move(table));
    m->start(w);
    return m;
}

// A god script whose Start runs `SetMission <orders>` on `unitId` (MAP_COMMAND 0, one argument).
static std::vector<uint8_t> setMissionCob(int unitId, const std::string& orders) {
    const std::string name = "SetMission " + orders;
    const std::vector<uint32_t> code = {0x10021001, uint32_t(unitId), 0x10073000, 0, 1, 0x10024000,
                                        0x10021001, 0, 0x10065000};
    std::vector<uint8_t> cob(size_t(160 + name.size() + 1 + code.size() * 4));
    auto put = [&](size_t offset, uint32_t value) { std::memcpy(cob.data() + offset, &value, 4); };
    const size_t codeAt = 160 + ((name.size() + 4) & ~size_t(3));
    cob.resize(codeAt + code.size() * 4);
    put(0, 6); put(4, 1); put(12, uint32_t(code.size())); put(24, 52); put(28, 56);
    put(36, uint32_t(codeAt)); put(44, 60); put(48, 1); put(52, 0); put(56, 72); put(60, 80);
    std::memcpy(cob.data() + 72, "Start", 6);
    std::memcpy(cob.data() + 80, name.c_str(), name.size() + 1);
    for (size_t i = 0; i < code.size(); ++i) put(codeAt + i * 4, code[i]);
    return cob;
}

// ---- 1. clauses end at commas -------------------------------------------------

static void clauses(Fixture& f) {
    std::printf("a clause ends at its comma:\n");
    auto w = makeWorld();
    std::vector<Placed> p = {
        {"soldier", 20, 20, 0, "", "ma, a target"},
        {"keep", 40, 20, 0, "", "w 900 40 b soldier 1"},
        {"target", 90, 90, 1, "", ""},
    };
    auto m = startMission(f, *w, p);
    const Unit* s = w->unit(p[0].id);
    check(s->orders.size() == 1 && isKind(*w, s->orders.front(), ScriptedOrder::Kind::AttackType),
          "\"ma\" with no point does nothing; \"a target\" still reads",
          std::to_string(s->orders.size()) + " order(s)");
    const Unit* k = w->unit(p[1].id);
    const ScriptedOrder* wait = k->orders.size() == 1 ? scripted(*w, k->orders.front()) : nullptr;
    check(wait && wait->kind == ScriptedOrder::Kind::WaitNear && wait->radius == 640 && wait->count == 27000,
          "\"w 900 40 b soldier 1\" is one wait of 900 s within 40 cells",
          std::to_string(k->orders.size()) + " order(s)");
    const size_t before = w->units().size();
    tickN(*w, 90);
    check(w->units().size() == before && w->unit(p[1].id)->buildQueue.empty(),
          "and the \"b soldier 1\" in its tail neither spawns nor builds");
}

// ---- 2. o: the standing orders ------------------------------------------------

static void standing(Fixture& f) {
    std::printf("o A / o A B:\n");
    auto w = makeWorld();
    std::vector<Placed> p = {
        {"soldier", 10, 10, 0, "", "o 2"},
        {"soldier", 14, 10, 0, "", "o 1"},
        {"soldier", 18, 10, 0, "", "o 0"},
        {"soldier", 22, 10, 0, "", "o 1 2"},
        {"stubborn", 26, 10, 0, "", "o 0"},
        {"soldier", 30, 10, 0, "", "o 3"},
    };
    auto m = startMission(f, *w, p);
    auto fields = [&](int i) {
        const Unit* u = w->unit(p[size_t(i)].id);
        return std::to_string(u->standingOrder) + "/" + std::to_string(u->moveState) + "/" + std::to_string(u->fireState);
    };
    check(fields(0) == "2/1/2", "o 2 is Offensive: maneuver, fire at will (it was Passive)", fields(0));
    check(fields(1) == "1/0/2", "o 1 is Defensive: hold position, fire at will", fields(1));
    check(fields(2) == "0/0/0", "o 0 is Passive: hold position, hold fire (it was Offensive)", fields(2));
    const std::string spawned = fields(5);
    check(fields(3) == spawned.substr(0, 1) + "/1/2", "o 1 2 sets the move and fire fields, not the shown order",
          fields(3));
    check(fields(4) == "0/0/0", "a type without stance buttons still takes a script's o", fields(4));
    check(w->unit(p[5].id)->standingOrder == 3, "o 3 is out of range and does nothing", fields(5));
}

// ---- 3. b: real build orders ----------------------------------------------------

static void builds(Fixture& f) {
    std::printf("b TYPE N (a factory) / b TYPE N X Y (a builder):\n");
    auto w = makeWorld();
    std::vector<Placed> p = {
        {"keep", 30, 30, 0, "", "b soldier 2, w 1, b soldier 1"},
        {"builder", 50, 30, 0, "", "b hut 0 60 30"},
        {"builder", 50, 40, 0, "", "b soldier 0 60 40, b hut 0 60 44"},
    };
    auto m = startMission(f, *w, p);
    const Unit* b = w->unit(p[1].id);
    const Order* build = b->orders.empty() ? nullptr : &b->orders.front();
    check(build && build->buildType == f.t("hut"), "the builder gets a build order, not a hut out of nowhere");
    if (build)
        check(sim::footprintOrigin(build->buildX, 2) == 59 && sim::footprintOrigin(build->buildZ, 2) == 29,
              "at retail's site: X*16 snapped to the 2x2 footprint (origin 59,29)",
              std::to_string(sim::footprintOrigin(build->buildX, 2)) + "," +
              std::to_string(sim::footprintOrigin(build->buildZ, 2)));
    const Unit* c = w->unit(p[2].id);
    check(c->orders.size() == 1 && c->orders.front().buildType == f.t("hut"),
          "a type off the builder's menu is refused (519850); the next clause still reads");
    const Unit* keep = w->unit(p[0].id);
    check(keep->orders.size() == 3 && isKind(*w, keep->orders[0], ScriptedOrder::Kind::Train) &&
          keep->orders[1].wait > 0 && isKind(*w, keep->orders[2], ScriptedOrder::Kind::Train),
          "the keep queues build 2, wait, build 1", std::to_string(keep->orders.size()) + " order(s)");
    tickN(*w, 2);
    check(w->unit(p[0].id)->buildQueue.size() == 2, "the first build puts 2 soldiers in the queue, not 3",
          std::to_string(w->unit(p[0].id)->buildQueue.size()));
    auto soldiers = [&] {
        int n = 0;
        for (const auto& u : w->units()) if (u.alive() && u.type == f.t("soldier") && !u.underConstruction) ++n;
        return n;
    };
    check(soldiers() == 0, "nobody arrives free at the start");
    int first = -1, third = -1;
    for (int t = 0; t < 3000 && third < 0; ++t) {
        tickN(*w, 1);
        if (first < 0 && soldiers() == 2) first = t;
        if (soldiers() == 3) third = t;
    }
    check(first > 0 && third > first + 30, "it builds two, waits, then builds the third",
          "two at tick " + std::to_string(first) + ", three at " + std::to_string(third));
    check(w->unit(p[0].id)->orders.empty() && w->unit(p[0].id)->buildQueue.empty(), "and its script is spent");
    bool hut = false;
    for (const auto& u : w->units()) hut = hut || (u.type == f.t("hut") && u.alive());
    check(hut, "the builder walks there and builds the hut");
}

// ---- 4. i / u: troops start aboard ------------------------------------------------

static void aboard(Fixture& f) {
    std::printf("i NAME / u X Y:\n");
    auto w = makeWorld();
    std::vector<Placed> p = {
        {"wagon", 20, 20, 0, "beta", ""},
        {"wagon", 20, 30, 0, "alpha", "u 45 30"},
        {"soldier", 60, 60, 0, "", "i alpha, m 50 24"},
        {"soldier", 64, 60, 0, "", "i wagon"},
        {"soldier", 68, 60, 0, "", "i nobody, m 70 70"},
    };
    auto m = startMission(f, *w, p);
    const Unit* a = w->unit(p[2].id);
    check(a->inTransport == p[1].id, "i alpha boards the wagon whose Ident= is alpha");
    check(a->orders.size() == 1 && a->orders.front().goal, "and its later move waits aboard");
    check(w->unit(p[3].id)->inTransport == p[0].id, "i wagon falls back to the first record named wagon");
    check(!w->unit(p[4].id)->embarked() && w->unit(p[4].id)->orders.size() == 1,
          "a NAME nothing answers to does nothing");
    check(!w->unit(p[1].id)->orders.empty() && w->unit(p[1].id)->orders.back().unload,
          "the wagon's unload is queued though it was empty when it was read");
    bool out = false;
    for (int t = 0; t < 4000 && !out; ++t) { tickN(*w, 1); out = !w->unit(p[2].id)->embarked(); }
    check(out, "the wagon unloads it");
    for (int t = 0; t < 2000; ++t) {
        tickN(*w, 1);
        const Unit* u = w->unit(p[2].id);
        if (std::hypot(u->x.toFloat() - cell(50), u->z.toFloat() - cell(24)) < 40) break;
    }
    const Unit* u = w->unit(p[2].id);
    check(std::hypot(u->x.toFloat() - cell(50), u->z.toFloat() - cell(24)) < 40,
          "then its own move runs", std::to_string(int(u->x.toFloat())) + "," + std::to_string(int(u->z.toFloat())));
}

// ---- 5. g / wa NAME / wa: names and the WaitForAttack -------------------------------

static void names(Fixture& f) {
    std::printf("g NAME / wa NAME / wa:\n");
    auto w = makeWorld();
    std::vector<Placed> p = {
        {"soldier", 20, 20, 0, "boss", ""},
        {"soldier", 24, 20, 0, "", "i boss"},
        {"soldier", 28, 20, 0, "", "g boss"},
        {"target", 30, 40, 0, "bait", ""},
        {"target", 40, 40, 0, "", "wa bait, m 40 50"},   // unarmed: they never pick a fight
        {"target", 44, 40, 0, "", "wa, m 44 50"},
        {"target", 48, 40, 0, "", "wa nobody, m 48 50"},
        {"target", 52, 40, 0, "", "w 1, wa, m 52 50"},
        {"target", 46, 44, 1, "", ""},           // an enemy in sight that never strikes
    };
    auto m = startMission(f, *w, p);
    const Unit* g = w->unit(p[2].id);
    check(!g->orders.empty() && g->orders.front().guard && g->orders.front().targetId == p[0].id,
          "g boss guards the unit whose Ident= is boss, not one an i clause named");
    check(!w->unit(p[6].id)->orders.empty() && !w->unit(p[6].id)->orders.front().waitAttack,
          "wa with a NAME nothing answers to is dropped");
    w->unit(p[7].id)->attackedTick = w->tickCount() + 1;   // hit during its first wait
    tickN(*w, 20);
    auto waiting = [&](int i) { const Unit* u = w->unit(p[size_t(i)].id); return !u->orders.empty() && u->orders.front().waitAttack; };
    check(waiting(4) && waiting(5), "an enemy in sight does not end a wa (it used to)");
    tickN(*w, 30);
    check(!waiting(7), "a hit that came before the wa's turn ends it when it comes up");
    // Strike the bait: only the wa watching it ends.
    const int striker = w->spawn(f.t("soldier"), cell(31), cell(41), 0.0f, 1);
    w->attack(striker, p[3].id, false);
    bool released = false;
    for (int t = 0; t < 600 && !released; ++t) { tickN(*w, 1); released = !waiting(4); }
    check(released && waiting(5), "wa bait ends when bait is hit; wa (itself) holds on");
    // The watched unit dying ends it too.
    std::vector<Placed> q = {
        {"target", 80, 80, 0, "ward", ""},
        {"target", 84, 80, 0, "", "wa ward, m 84 90"},
    };
    auto w2 = makeWorld();
    auto m2 = startMission(f, *w2, q);
    tickN(*w2, 10);
    check(!w2->unit(q[1].id)->orders.empty() && w2->unit(q[1].id)->orders.front().waitAttack, "wa ward holds");
    kill(*w2, q[0].id, 0);
    tickN(*w2, 3);
    check(!w2->unit(q[1].id)->orders.empty() && !w2->unit(q[1].id)->orders.front().waitAttack,
          "and ends when ward dies");
}

// ---- 6. w F N -------------------------------------------------------------------------

static void waitNear(Fixture& f) {
    std::printf("w F N:\n");
    auto w = makeWorld();
    std::vector<Placed> p = {
        {"target", 30, 30, 0, "", "w 100 5, m 30 40"},
        {"target", 60, 30, 0, "", "w 100 1, m 60 40"},
        {"target", 33, 30, 1, "", ""},            // 48 px away: inside 5 cells
        {"target", 63, 30, 1, "", ""},            // 48 px away: outside 1 cell
    };
    auto m = startMission(f, *w, p);
    tickN(*w, 60);
    auto waiting = [&](int i) { const Unit* u = w->unit(p[size_t(i)].id); return !u->orders.empty() && u->orders.front().wait > 0; };
    check(!waiting(0), "a visible enemy within N cells ends the wait early (N was ignored)");
    check(waiting(1), "one beyond N cells does not");
}

// ---- 7. v F ----------------------------------------------------------------------------

static void veterancy(Fixture& f) {
    std::printf("v F:\n");
    auto w = makeWorld();
    std::vector<Placed> p = {
        {"soldier", 20, 20, 0, "", "v 2.5"},
        {"plain", 24, 20, 0, "", "v 3"},
        {"target", 21, 20, 1, "", ""},
    };
    auto m = startMission(f, *w, p);
    const Unit* v = w->unit(p[0].id);
    check(std::fabs(v->vetMul() - 2.5f) < 1e-6f && v->veteran == 10, "v 2.5: multiplier 2.5, rank 10",
          std::to_string(v->vetMul()) + ", rank " + std::to_string(v->veteran));
    check(w->unit(p[1].id)->vetMul() == 1.0f && w->unit(p[1].id)->veteran == 0, "a noveteran type ignores it");
    kill(*w, p[2].id, p[0].id);
    tickN(*w, 2);
    check(w->unit(p[0].id)->veteran == 10 && w->unit(p[0].id)->vetMul() == 2.5f, "a kill moves neither");
}

// ---- 8. a TYPE / a NAME / a X Y ---------------------------------------------------------

static void attacks(Fixture& f) {
    std::printf("a TYPE / a NAME / a X Y:\n");
    auto w = makeWorld();
    std::vector<Placed> p = {
        {"soldier", 60, 60, 0, "", "o 0, a target"},   // holds fire: only the order strikes
        {"target", 62, 60, 0, "", ""},            // its own player's: never a mark
        {"target", 70, 60, 1, "", ""},
        {"target", 60, 95, 1, "", ""},
        {"target", 20, 60, 1, "chief", ""},
        {"soldier", 90, 90, 0, "", "a chief"},
        {"soldier", 100, 20, 0, "", "a 110 20"},
    };
    auto m = startMission(f, *w, p);
    const Unit* chief = w->unit(p[5].id);
    check(!chief->orders.empty() && chief->orders.front().targetId == p[4].id,
          "a chief attacks the unit whose Ident= is chief (it was read as a type)");
    const Unit* ground = w->unit(p[6].id);
    check(ground->orders.size() == 1 && !ground->orders.back().attackMove && ground->orders.back().missionRadius > 0,
          "a X Y goes until the point is in reach (it was a fight-move to it)");
    std::vector<int> struck;
    for (int t = 0; t < 4000 && struck.size() < 3; ++t) {
        tickN(*w, 1);
        const Unit* s = w->unit(p[0].id);
        if (!s->orders.empty() && s->orders.front().targetId && !s->orders.front().autoTarget) {
            const int mark = s->orders.front().targetId;
            struck.push_back(mark);
            kill(*w, mark, p[0].id);
            tickN(*w, 2);
        }
    }
    check(struck.size() == 3 && struck[0] == p[2].id, "a target attacks the nearest enemy target first",
          std::to_string(struck.size()) + " struck");
    check(std::find(struck.begin(), struck.end(), p[1].id) == struck.end(),
          "never one its own player owns");
    tickN(*w, 120);
    check(w->unit(p[0].id)->orders.empty() && w->unit(p[1].id)->alive(), "and stops when none is left");
}

// ---- 9. s and d ----------------------------------------------------------------------------

static void selectAndDestruct(Fixture& f) {
    std::printf("s / d:\n");
    auto w = makeWorld();
    std::vector<Placed> p = {
        {"soldier", 20, 20, 1, "", "s, m 20 30"},
        {"soldier", 30, 20, 0, "", "w 1, d"},
    };
    auto m = startMission(f, *w, p);
    check(w->unit(p[0].id)->player == 1, "s leaves an enemy unit its owner's (it was handed to the player)");
    tickN(*w, 15);
    check(w->unit(p[1].id)->alive(), "d waits its turn behind w 1 (it used to kill at once)");
    tickN(*w, 40);
    const Unit* d = w->unit(p[1].id);
    check(!d->alive() && d->deathType == Unit::kDeathSelfDestruct, "then self-destructs");
}

// ---- 10. SetMission ---------------------------------------------------------------------------

static void setMission(Fixture& f) {
    std::printf("SetMission:\n");
    auto w = makeWorld();
    const int unit = w->spawn(f.t("soldier"), cell(20), cell(20), 0.0f, 0);
    w->spawn(f.t("soldier"), cell(24), cell(20), 0.0f, 0);
    w->orderWait(unit, 100, false);
    sim::MissionScript m(setMissionCob(unit, "a target, wa bait"), tdf::Node{}, f.reg, 0, "setmission fixture");
    m.start(*w);
    m.step(*w, 1.0f / 30.0f);
    const Unit* u = w->unit(unit);
    check(u->orders.size() == 2 && u->orders[0].wait > 0 && isKind(*w, u->orders[1], ScriptedOrder::Kind::AttackType),
          "it queues behind what the unit has; its NAMEs resolve to nothing",
          std::to_string(u->orders.size()) + " order(s)");
}

// ---- shipped missions -----------------------------------------------------------------------------

static void shipped(const char* data) {
    hpi::Vfs vfs = hpi::mountRetailRoot(data);
    sim::TypeRegistry reg;
    sim::setupRegistry(reg, vfs, false);
    const auto* arafly = reg.find("arafly"), *arassh = reg.find("arassh"), *aralode = reg.find("aralode");
    const auto* arakeep = reg.find("arakeep"), *araat = reg.find("araat");
    std::printf("takx04 (Flying Builder, keeps, towers):\n");
    {
        World w;
        int human = 0;
        if (!sim::setupMission(w, reg, vfs, "takx04_ph", human)) { check(false, "takx04_ph loads"); return; }
        tickN(w, 2);
        int flyer = 0, jim = 0, keep = 0;
        std::vector<int> towers;
        for (const auto& u : w.units()) {
            if (u.type == arafly) flyer = u.id;
            if (u.type == aralode && u.x.toFloat() == cell(231) && u.z.toFloat() == cell(222)) jim = u.id;
            if (u.type == arakeep) keep = u.id;
            if (u.type == araat) towers.push_back(u.id);
        }
        check(flyer && jim && keep && towers.size() >= 2, "finds the flyer, the lodestone JIM, the keep and the towers");
        if (!flyer || !jim || !keep) return;
        const Unit* fl = w.unit(flyer);
        const ScriptedOrder* wa = fl->orders.empty() ? nullptr : w.scriptedOrder(fl->orders.front());
        check(wa && wa->kind == ScriptedOrder::Kind::WaitAttack && wa->watched == jim,
              "the Flying Builder waits on JIM (wa JIM)");
        tickN(w, 300);
        int shipyards = 0;   // (two more are placed elsewhere: JOE and ZAP)
        for (const auto& u : w.units())
            if (u.type == arassh && u.alive() &&
                std::hypot(u.x.toFloat() - 370 * 16.0f, u.z.toFloat() - 225 * 16.0f) < 160) ++shipyards;
        check(shipyards == 0, "no shipyard appears at 370,225 at the start (it was conjured free)");
        check(!w.unit(flyer)->orders.empty() && w.unit(flyer)->orders.front().waitAttack, "it is still waiting");
        w.unit(jim)->attackedTick = w.tickCount() + 1;     // JIM is attacked
        tickN(w, 3);
        fl = w.unit(flyer);
        const Order* build = fl->orders.empty() ? nullptr : &fl->orders.front();
        check(build && build->buildType == arassh, "once JIM is attacked it sets off to build the shipyard");
        if (build && arassh)
            check(sim::footprintOrigin(build->buildX, arassh->footX) == 370 - arassh->footX / 2 &&
                  sim::footprintOrigin(build->buildZ, arassh->footZ) == 225 - arassh->footZ / 2,
                  "at 370,225", std::to_string(sim::footprintOrigin(build->buildX, arassh->footX)) + "," +
                                std::to_string(sim::footprintOrigin(build->buildZ, arassh->footZ)));
        const Unit* k = w.unit(keep);
        const ScriptedOrder* first = k->orders.size() == 3 ? w.scriptedOrder(k->orders[0]) : nullptr;
        check(first && first->kind == ScriptedOrder::Kind::WaitNear && first->radius == 80 &&
              w.scriptedOrder(k->orders[1]) && w.scriptedOrder(k->orders[1])->kind == ScriptedOrder::Kind::Train &&
              w.scriptedOrder(k->orders[1])->count == 15 && k->buildQueue.empty(),
              "the keep waits (5 cells) before it builds 15 swordsmen, then 20 archers",
              std::to_string(k->orders.size()) + " order(s)");
        const Unit* tower = w.unit(towers.front());
        check(tower->standingOrder == 0 && tower->fireState == 0, "the towers' o 0 is Passive",
              std::to_string(tower->standingOrder) + "/" + std::to_string(tower->fireState));
    }
    std::printf("takx07 (troops aboard):\n");
    {
        World w;
        int human = 0;
        if (!sim::setupMission(w, reg, vfs, "takx07_ph", human)) { check(false, "takx07_ph loads"); return; }
        tickN(w, 2);
        int aboard = 0, carriers = 0;
        for (const auto& u : w.units()) {
            if (u.embarked()) ++aboard;
            if (u.type && u.type->canTransport && !u.cargo.empty()) ++carriers;
        }
        check(aboard >= 95 && carriers >= 2, "the i clauses put the troops in their transports",
              std::to_string(aboard) + " aboard " + std::to_string(carriers) + " carriers");
        tickN(w, 300);
        check(true, "and the mission runs on");
    }
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--data") {
        std::printf("mission_orders_test (shipped missions)\n");
        shipped(argv[2]);
    } else {
        std::printf("mission_orders_test\n");
        Fixture f;
        if (!f.t("soldier") || !f.t("keep") || !f.t("builder") || !f.t("wagon")) {
            std::printf("fixture types failed to load\n");
            return 2;
        }
        clauses(f);
        standing(f);
        builds(f);
        aboard(f);
        names(f);
        waitNear(f);
        veterancy(f);
        attacks(f);
        selectAndDestruct(f);
        setMission(f);
    }
    std::printf(g_fail ? "mission_orders_test: %d FAILED\n" : "mission_orders_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
