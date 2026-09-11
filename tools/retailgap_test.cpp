// retailgap_test: verifies the retail-parity sim fixes that have no other harness.
//
//   retailgap_test <retail-install-dir>
//
// Checks, against the real shipped data:
//   1. [EXPLODEAS] parses, and a dying unit detonates it where it stands (splashing
//      nearby enemies) -- the Kamikaze Rat's whole purpose.
//   2. totalallowed parses and caps live units of that type per player (dragons).
//   3. A data-only mission (no .cob) loads, and its placed units' InitialMission=
//      order queues are applied.
//
// A dev harness; not shipped in a game.

#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"

#include <cstdio>
#include <string>

using namespace tak;

namespace {
int failures = 0;
const char* kMap = "maps/Inner Circle.tnt";   // any shipped map; we only need terrain
void check(bool ok, const char* what, const std::string& detail = "") {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) ++failures;
}
void tick(sim::World& w, float seconds) {
    const float dt = 1.0f / 30.0f;
    for (int i = 0; i < int(seconds / dt); ++i) w.tick(dt);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: retailgap_test <retail-install-dir>\n"); return 2; }
    hpi::Vfs vfs = hpi::mountRetailRoot(argv[1]);
    sim::TypeRegistry reg;
    reg.loadMoveInfo(vfs, "gamedata/moveinfo.tdf");
    reg.loadDir(vfs, "units/");

    // ---- 1. [EXPLODEAS] ----------------------------------------------------
    std::printf("[EXPLODEAS death blast]\n");
    const sim::UnitType* rat = reg.find("tarkam");        // Kamikaze Rat
    const sim::UnitType* victim = reg.find("arasword");    // a plain Aramon swordsman
    if (!rat || !victim) { std::printf("  (missing unit defs; skipped)\n"); }
    else {
        check(rat->hasExplodeAs, "tarkam parses an EXPLODEAS weapon");
        check(rat->explodeAs.aoe > 100.0f, "its blast has a real radius",
              "aoe=" + std::to_string(int(rat->explodeAs.aoe)));
        check(rat->explodeAs.damage > 1000.0f, "and real damage",
              "dmg=" + std::to_string(int(rat->explodeAs.damage)));

        sim::World w;
        sim::MatchConfig cfg;
        cfg.vfs = &vfs;
        cfg.mapPath = kMap;
        cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
        cfg.slots[0].team = 0; cfg.slots[1].team = 1;
        sim::setupMatch(w, reg, cfg);

        // A rat (player 0) with two enemy swordsmen (player 1) standing next to it.
        int ratId = w.spawn(rat, 500, 500, 0, 0);
        int aId = w.spawn(victim, 520, 500, 0, 1);
        int bId = w.spawn(victim, 500, 530, 0, 1);
        int farId = w.spawn(victim, 500, 1200, 0, 1);   // well outside the blast
        float aHp0 = w.unit(aId)->hp, farHp0 = w.unit(farId)->hp;
        check(aHp0 > 0 && farHp0 > 0, "victims spawned alive");

        w.unit(ratId)->hp = 0;    // the rat dies -> its EXPLODEAS should fire
        tick(w, 0.2f);

        const sim::Unit* a = w.unit(aId);
        const sim::Unit* b = w.unit(bId);
        const sim::Unit* far = w.unit(farId);
        check(a && (!a->alive() || a->hp < aHp0), "adjacent enemy took the blast");
        check(b && (!b->alive() || b->hp < aHp0), "second adjacent enemy took the blast");
        check(far && far->alive() && far->hp >= farHp0, "distant enemy untouched");
    }

    // ---- 2. totalallowed ---------------------------------------------------
    std::printf("[totalallowed unique cap]\n");
    const sim::UnitType* drag = reg.find("aradrag");
    if (!drag) { std::printf("  (no aradrag; skipped)\n"); }
    else {
        check(drag->totalAllowed == 1, "aradrag parses totalallowed=1",
              "got " + std::to_string(drag->totalAllowed));
        sim::World w;
        sim::MatchConfig cfg;
        cfg.vfs = &vfs;
        cfg.mapPath = kMap;
        cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
        cfg.slots[0].team = 0; cfg.slots[1].team = 1;
        sim::setupMatch(w, reg, cfg);
        check(!w.atTypeCap(0, drag), "no dragon yet -> not capped");
        int d1 = w.spawn(drag, 400, 400, 0, 0);
        check(w.atTypeCap(0, drag), "one live dragon -> player 0 is capped");
        check(!w.atTypeCap(1, drag), "the cap is PER PLAYER (player 1 free)");
        if (auto* d = w.unit(d1)) d->hp = 0;   // it dies
        tick(w, 0.2f);
        check(!w.atTypeCap(0, drag), "dragon died -> the slot frees again");
        const sim::UnitType* plain = reg.find("arasword");
        if (plain) check(!w.atTypeCap(0, plain), "an uncapped type is never capped");
    }

    // ---- 3. data-only mission + InitialMission ------------------------------
    std::printf("[data-only mission (.cob optional) + InitialMission]\n");
    {
        const std::string stem = "takmission05_dh";   // Book of Darien chapter 5: no .cob
        bool hasCob = vfs.has("missions/" + stem + ".cob");
        check(!hasCob, "takmission05_dh really ships no .cob");
        sim::World w;
        int human = 0;
        bool ok = sim::setupMission(w, reg, vfs, stem, human);
        check(ok, "the data-only mission loads (used to abort on the missing cob)");
        if (ok) {
            int alive = 0;
            for (const auto& u : w.units()) if (u.alive()) ++alive;
            check(alive > 0, "it spawned its placed units",
                  std::to_string(alive) + " units");
            // start() applies the InitialMission queues; count units holding orders.
            tick(w, 0.5f);
            int withOrders = 0;
            for (const auto& u : w.units())
                if (u.alive() && !u.orders.empty()) ++withOrders;
            check(withOrders > 0, "placed units received InitialMission order queues",
                  std::to_string(withOrders) + " units with orders");
            // And it must stay deterministic/stable for a while.
            tick(w, 3.0f);
            check(true, "ticks without crashing");
        }
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
