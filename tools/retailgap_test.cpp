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

    // ---- 1b. weapon-class parse ------------------------------------------
    std::printf("[weapon class parse]\n");
    {
        struct C { const char* unit; int slot; sim::Weapon::Kind kind; const char* label; };
        const C cases[] = {
            {"arabow",   1, sim::Weapon::Kind::Guided,    "arabow W2 Tracking Arrow = Guided"},
            {"aradrag",  2, sim::Weapon::Kind::Remote,    "aradrag W3 Earthquake = Remote Effect"},
            {"tarwitch", 0, sim::Weapon::Kind::Wandering, "tarwitch W1 Tornado = Wandering"},
        };
        for (const C& c : cases) {
            const sim::UnitType* t = reg.find(c.unit);
            bool ok = t && int(t->weapons.size()) > c.slot && t->weapons[size_t(c.slot)].kind == c.kind;
            check(ok, c.label);
        }
        if (const sim::UnitType* t = reg.find("tarwitch"); t && !t->weapons.empty()) {
            const sim::Weapon& w = t->weapons[0];
            check(w.duration > 8.0f, "tornado duration parsed", std::to_string(w.duration));
            check(w.buildUp > 2.0f && w.decay > 2.0f, "buildup/decay parsed");
            check(w.variationTime > 0 && w.maxVariation > 0, "wander variation parsed");
        }
        if (const sim::UnitType* t = reg.find("tarmind"); t && !t->weapons.empty()) {
            check(t->weapons[0].mindControl, "tarmind W1 flagged mindControl");
            check(t->weapons[0].unitsOnly, "tarmind W1 unitsonly");
            // The DAMAGE table is the eligibility filter.
            const sim::UnitType* monarch = reg.find("araking");
            const sim::UnitType* sword = reg.find("arasword");
            if (monarch && sword) {
                check(t->weapons[0].damageVs(monarch) <= 0.0f, "monarch is mind-control IMMUNE (damage 0)");
                check(t->weapons[0].damageVs(sword) > 0.0f, "a swordsman is convertible (damage > 0)");
            }
        }
        if (const sim::UnitType* t = reg.find("arabow"); t && t->weapons.size() > 1)
            check(t->weapons[1].turnRate > 3.0f, "guided turnrate in radians/s",
                  std::to_string(t->weapons[1].turnRate));
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

    // ---- 2b. guided homing --------------------------------------------------
    std::printf("[guided homing]\n");
    {
        // tarfire (Fire Demon) has a GUIDED primary: Fire Ball of Gloom, range 900,
        // velocity 250 (slow -- long flight), turnrate 180, reload 3.2s. The slow
        // shot + long reload make the dodge unambiguous: one shot is in the air, and
        // only homing can bring it back onto a target that jumps aside mid-flight.
        const sim::UnitType* demon = reg.find("tarfire");
        const sim::UnitType* prey = reg.find("arasword");
        if (!demon || !prey || demon->weapons.empty()) std::printf("  (missing defs; skipped)\n");
        else {
            check(demon->weapons[0].kind == sim::Weapon::Kind::Guided &&
                  demon->weapons[0].turnRate > 0, "Fire Ball of Gloom is guided");
            sim::World w;
            sim::MatchConfig cfg;
            cfg.vfs = &vfs; cfg.mapPath = kMap;
            cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
            cfg.slots[0].team = 0; cfg.slots[1].team = 1;
            sim::setupMatch(w, reg, cfg);
            int shooter = w.spawn(demon, 1000, 1000, 0, 0);
            int mover = w.spawn(prey, 1000, 1600, 0, 1);   // 600px away, in range
            if (auto* su = w.unit(shooter)) su->stance = 0;
            w.attack(shooter, mover, false);
            // Tick until a shot is in the air.
            int guard = 0;
            while (w.projectiles().empty() && guard++ < 400) w.tick(1.0f / 30.0f);
            check(!w.projectiles().empty(), "the demon launched a guided shot");
            if (!w.projectiles().empty()) {
                // Jump the target 300px sideways: a straight shot now misses by far
                // more than the hit radius.
                if (auto* m = w.unit(mover)) { m->x = 1300; m->orders.clear(); }
                float hp0 = w.unit(mover)->hp;
                for (int i = 0; i < 90; ++i) w.tick(1.0f / 30.0f);   // 3s < reload
                const sim::Unit* m = w.unit(mover);
                bool hurt = m && (!m->alive() || m->hp < hp0);
                check(hurt, "the shot curved onto the dodging target",
                      hurt ? "" : "untouched -- homing did not engage");
            }
        }
    }

    // ---- 2c. remote effect / wandering / mind control ------------------------
    std::printf("[remote effect + wandering + mind control]\n");
    {
        const sim::UnitType* prey = reg.find("arasword");
        auto freshWorld = [&](sim::World& w) {
            sim::MatchConfig cfg;
            cfg.vfs = &vfs; cfg.mapPath = kMap;
            cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
            cfg.slots[0].team = 0; cfg.slots[1].team = 1;
            sim::setupMatch(w, reg, cfg);
        };

        // Remote Effect: the Acolyte's Earthquake lands at the AIMED GROUND POINT
        // after builduptime -- stepping aside must not dodge it (aoe 200).
        const sim::UnitType* acolyte = reg.find("arapries");
        if (acolyte && prey) {
            int quake = -1;
            for (size_t i = 0; i < acolyte->weapons.size(); ++i)
                if (acolyte->weapons[i].kind == sim::Weapon::Kind::Remote) { quake = int(i); break; }
            check(quake >= 0, "arapries has a Remote Effect weapon");
            if (quake >= 0) {
                sim::World w; freshWorld(w);
                int caster = w.spawn(acolyte, 1000, 1000, 0, 0);
                int victim = w.spawn(prey, 1000, 1150, 0, 1);
                if (auto* c = w.unit(caster)) c->mana = c->type->maxMana;
                w.attack(caster, victim, false);
                float hp0 = w.unit(victim)->hp;
                for (int i = 0; i < 600; ++i) {
                    w.tick(1.0f / 30.0f);
                    if (const sim::Unit* v = w.unit(victim); v && (!v->alive() || v->hp < hp0)) break;
                }
                const sim::Unit* v = w.unit(victim);
                check(v && (!v->alive() || v->hp < hp0), "the remote effect landed on its target");
            }
        }

        // Wandering: the Weather Witch's Tornado must roam and grind over time,
        // not deal one tap. Check it damages across multiple separate bites.
        const sim::UnitType* witch = reg.find("tarwitch");
        if (witch && prey && !witch->weapons.empty() &&
            witch->weapons[0].kind == sim::Weapon::Kind::Wandering) {
            sim::World w; freshWorld(w);
            int caster = w.spawn(witch, 1000, 1000, 0, 0);
            int victim = w.spawn(prey, 1000, 1100, 0, 1);
            if (auto* c = w.unit(caster)) c->mana = c->type->maxMana;
            w.attack(caster, victim, false);
            float hp0 = w.unit(victim)->hp;
            int hits = 0; float last = hp0;
            for (int i = 0; i < 900; ++i) {
                w.tick(1.0f / 30.0f);
                const sim::Unit* v = w.unit(victim);
                if (!v || !v->alive()) { ++hits; break; }
                if (v->hp < last - 0.01f) { ++hits; last = v->hp; }
            }
            check(hits > 0, "the tornado damaged its victim");
            check(hits > 1, "and it GRINDS (multiple bites, not one tap)",
                  std::to_string(hits) + " bites");
        }

        // Mind control: the Mind Mage converts an eligible enemy; a Monarch is immune.
        const sim::UnitType* mage = reg.find("tarmind");
        if (mage && prey && !mage->weapons.empty()) {
            sim::World w; freshWorld(w);
            int caster = w.spawn(mage, 1000, 1000, 0, 0);
            int victim = w.spawn(prey, 1000, 1200, 0, 1);
            if (auto* c = w.unit(caster)) c->mana = c->type->maxMana;
            w.attack(caster, victim, false);
            bool converted = false;
            for (int i = 0; i < 900; ++i) {
                w.tick(1.0f / 30.0f);
                const sim::Unit* v = w.unit(victim);
                if (v && v->alive() && v->player == 0) { converted = true; break; }
            }
            check(converted, "the Mind Mage CONVERTED an enemy swordsman to its side");
        }

        // A Monarch must be IMMUNE (its DAMAGE category is zeroed) -- and must not
        // be silently damaged by the 1-point nominal damage either.
        const sim::UnitType* king = reg.find("araking");
        if (mage && king) {
            sim::World w; freshWorld(w);
            int caster = w.spawn(mage, 1000, 1000, 0, 0);
            int royal = w.spawn(king, 1000, 1200, 0, 1);
            if (auto* c = w.unit(caster)) c->mana = c->type->maxMana;
            w.attack(caster, royal, false);
            bool stolen = false;
            for (int i = 0; i < 900; ++i) {
                w.tick(1.0f / 30.0f);
                const sim::Unit* v = w.unit(royal);
                if (v && v->alive() && v->player == 0) { stolen = true; break; }
            }
            check(!stolen, "a Monarch resists mind control (damage-table immunity)");
        }

        // Area Mind Control (Remote Effect + mindcontrol, aoe 250) should convert
        // EVERY eligible enemy in the radius, not just one.
        if (mage && prey && mage->weapons.size() > 1 &&
            mage->weapons[1].kind == sim::Weapon::Kind::Remote &&
            mage->weapons[1].mindControl) {
            sim::World w; freshWorld(w);
            int caster = w.spawn(mage, 1000, 1000, 0, 0);
            int v1 = w.spawn(prey, 1000, 1150, 0, 1);
            int v2 = w.spawn(prey, 1060, 1160, 0, 1);
            int v3 = w.spawn(prey, 940, 1140, 0, 1);
            if (auto* c = w.unit(caster)) c->mana = c->type->maxMana;
            // A player selects the area spell (Ctrl+W cycles weapons); the sim fires
            // whichever slot is selected, so pick slot 1 as a player would.
            w.setWeapon(caster, 1);
            w.attack(caster, v1, false);
            int flipped = 0;
            for (int i = 0; i < 1200; ++i) {
                w.tick(1.0f / 30.0f);
                flipped = 0;
                for (int id : {v1, v2, v3})
                    if (const sim::Unit* v = w.unit(id); v && v->alive() && v->player == 0) ++flipped;
                if (flipped >= 2) break;
            }
            check(flipped >= 2, "Area Mind Control converts a GROUP",
                  std::to_string(flipped) + " of 3 flipped");
        }
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
