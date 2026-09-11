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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

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

        // Remote Effect SUBCLASSES: the cadence differs per subtype.
        // arapries: W1 Earthquake (pulses every shakeduration=1s through its
        // buildup 3 + decay 3), W2 Hail Shower (rains particlespersecond=5 times a
        // second for duration=2.5), W3 Turn To Stone.
        if (acolyte && prey && acolyte->weapons.size() >= 3) {
            check(acolyte->weapons[0].remote == sim::Weapon::RemoteKind::Earthquake,
                  "arapries W1 is the Earthquake subclass");
            check(acolyte->weapons[1].remote == sim::Weapon::RemoteKind::Hailstorm,
                  "arapries W2 is the Hailstorm subclass");
            // THE BUG FIX: "Hail Shower" used to trip a name-substring freeze
            // heuristic, and our freeze is an instant statue death -- so a 70-damage
            // shower silently annihilated everything in a 200-wide radius.
            check(acolyte->weapons[1].status == sim::Weapon::Status::None,
                  "Hail Shower is NOT a freeze weapon (it used to instant-kill)");
            check(acolyte->weapons[2].status == sim::Weapon::Status::Stoned,
                  "Turn To Stone still petrifies (subtype=turntostone)");

            // Drive each and count how many distinct ticks dealt damage: a plain
            // one-shot would show exactly 1, these must pulse.
            auto pulseCount = [&](int slot, float seconds) {
                sim::World w; freshWorld(w);
                int caster = w.spawn(acolyte, 1000, 1000, 0, 0);
                int victim = w.spawn(prey, 1000, 1040, 0, 1);   // inside aoe/2 = 100
                if (auto* c = w.unit(caster)) c->mana = c->type->maxMana;
                w.setWeapon(caster, slot);
                w.attack(caster, victim, false);
                int pulses = 0; float last = w.unit(victim)->hp; bool died = false;
                for (int i = 0; i < int(seconds * 30); ++i) {
                    w.tick(1.0f / 30.0f);
                    const sim::Unit* v = w.unit(victim);
                    if (!v || !v->alive()) { died = true; break; }
                    if (v->hp < last - 0.01f) { ++pulses; last = v->hp; }
                }
                return std::pair<int, bool>{pulses, died};
            };
            // shotart: this Earthquake is DELIVERED -- a visible shot flies to the
            // aim point first and only then does the 3s channel start, so the first
            // damage lands later than a conjured-in-place spell would.
            check(!acolyte->weapons[0].shotArt.empty(), "arapries W1 carries shotart");
            {
                sim::World w; freshWorld(w);
                int caster = w.spawn(acolyte, 1000, 1000, 0, 0);
                int victim = w.spawn(prey, 1000, 1200, 0, 1);   // 200px: a real flight
                if (auto* c = w.unit(caster)) c->mana = c->type->maxMana;
                w.setWeapon(caster, 0);
                w.attack(caster, victim, false);
                bool sawShot = false;
                for (int i = 0; i < 240; ++i) {
                    w.tick(1.0f / 30.0f);
                    if (!w.projectiles().empty()) { sawShot = true; break; }
                }
                check(sawShot, "it launches a visible delivery shot");
            }
            auto [quakePulses, quakeDied] = pulseCount(0, 8.0f);
            check(quakePulses > 1 || quakeDied, "the Earthquake PULSES (not one tap)",
                  std::to_string(quakePulses) + " damage ticks");
            auto [hailPulses, hailDied] = pulseCount(1, 8.0f);
            check(hailPulses > 3, "the Hail Shower RAINS many small hits",
                  std::to_string(hailPulses) + " damage ticks");
            check(!hailDied, "and its victim survives it (70 dmg x ~13 < 2500 HP)");
        }

        // Wandering: the Tornado is a MOVING HAZARD, not a targeted strike -- it is
        // born in front of the caster, drifts along the launch heading and weaves
        // hard sideways (maxvariation is px/tick and dwarfs the drift), grinding
        // whatever it touches every tick. So seed the area it will cross and check
        // it chews through units over time rather than dealing one tap.
        const sim::UnitType* witch = reg.find("tarwitch");
        if (witch && prey && !witch->weapons.empty() &&
            witch->weapons[0].kind == sim::Weapon::Kind::Wandering) {
            sim::World w; freshWorld(w);
            int caster = w.spawn(witch, 1000, 1000, 0, 0);
            if (auto* c = w.unit(caster)) c->mana = c->type->maxMana;
            // A dense field across the whole region a 9-second tornado can reach
            // (it drifts ~400px forward and wanders hundreds of px sideways), so the
            // check does not depend on the exact RNG path.
            std::vector<int> crowd;
            for (int gz = 0; gz < 11; ++gz)
                for (int gx = 0; gx < 11; ++gx)
                    crowd.push_back(w.spawn(prey, 900.0f + gx * 45.0f,
                                            1100.0f + gz * 45.0f, 0, 1));
            float total0 = 0;
            for (int id : crowd) if (auto* u = w.unit(id)) total0 += u->hp;
            w.attack(caster, crowd[12], false);
            int ticksWithDamage = 0;
            float last = total0;
            for (int i = 0; i < 600; ++i) {
                w.tick(1.0f / 30.0f);
                float now = 0;
                for (int id : crowd) if (const sim::Unit* u = w.unit(id); u && u->alive()) now += u->hp;
                if (now < last - 0.01f) ++ticksWithDamage;
                last = now;
            }
            check(last < total0, "the tornado chewed through the crowd it crossed",
                  "lost " + std::to_string(int(total0 - last)) + " HP");
            check(ticksWithDamage > 5, "and it GRINDS continuously (many damage ticks)",
                  std::to_string(ticksWithDamage) + " ticks dealt damage");
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
            check(!stolen, "a Monarch resists mind control (commander gate)");
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

    // ---- 2d. dropped bombs ---------------------------------------------------
    std::printf("[dropped bombs]\n");
    {
        // tarbeak's Egg Bomb is subtype=Dropped: released from the flyer and falls
        // onto the ground beneath, so the bomber must OVERFLY its target rather
        // than shooting from its nominal 200px range.
        const sim::UnitType* bomber = reg.find("tarbeak");
        const sim::UnitType* prey = reg.find("arasword");
        if (!bomber || !prey || bomber->weapons.empty()) std::printf("  (missing defs; skipped)\n");
        else {
            int slot = -1;
            for (size_t i = 0; i < bomber->weapons.size(); ++i)
                if (bomber->weapons[i].kind == sim::Weapon::Kind::Dropped) { slot = int(i); break; }
            check(slot >= 0, "tarbeak carries a Dropped weapon");
            sim::World w;
            sim::MatchConfig cfg;
            cfg.vfs = &vfs; cfg.mapPath = kMap;
            cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
            cfg.slots[0].team = 0; cfg.slots[1].team = 1;
            sim::setupMatch(w, reg, cfg);
            int b = w.spawn(bomber, 1000, 1000, 0, 0);
            int v = w.spawn(prey, 1000, 1500, 0, 1);   // 500px away: well beyond a drop
            if (auto* bu = w.unit(b)) bu->mana = bomber->maxMana;
            w.attack(b, v, false);
            float hp0 = w.unit(v)->hp;
            bool hurt = false; float closest = 1e9f;
            for (int i = 0; i < 1800; ++i) {
                w.tick(1.0f / 30.0f);
                const sim::Unit* bb = w.unit(b);
                const sim::Unit* vv = w.unit(v);
                if (!vv) break;
                if (bb && vv) {
                    float d = std::sqrt((bb->x - vv->x) * (bb->x - vv->x) +
                                        (bb->z - vv->z) * (bb->z - vv->z));
                    closest = std::min(closest, d);
                }
                if (!vv->alive() || vv->hp < hp0) { hurt = true; break; }
            }
            // Retail does NOT require an overflight: Dropped reverts to the plain
            // 2D range test and the bomb's velocity is solved so it arrives over
            // the aim point as it lands. So it releases inside its FBI range (200)
            // and the bomb glides in.
            check(closest < 220.0f, "the bomber closed to within its release range",
                  "closest " + std::to_string(int(closest)) + "px");
            check(hurt, "and its dropped bomb glided onto the target");
        }
    }

    // ---- 2e. automatic weapon selection --------------------------------------
    std::printf("[auto weapon selection]\n");
    {
        // Elsin carries Lightning (250px, free, 950), Meteor (400px, 200 mana,
        // 2000) and Earthen Wave (120px, 900 mana, 5000). The sim used to fire slot
        // 0 forever, so he cast Lightning all game and never spent a point of mana.
        const sim::UnitType* king = reg.find("araking");
        const sim::UnitType* prey = reg.find("arasword");
        if (!king || !prey || king->weapons.size() < 3) std::printf("  (missing defs; skipped)\n");
        else {
            auto runFight = [&](bool manualSlot0) {
                sim::World w;
                sim::MatchConfig cfg;
                cfg.vfs = &vfs; cfg.mapPath = kMap;
                cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
                cfg.slots[0].team = 0; cfg.slots[1].team = 1;
                sim::setupMatch(w, reg, cfg);
                int c = w.spawn(king, 1000, 1000, 0, 0);
                if (auto* u = w.unit(c)) u->mana = king->maxMana;
                if (manualSlot0) w.setWeapon(c, 0);      // player takes control
                for (int i = 0; i < 40; ++i)
                    w.spawn(prey, 1000.0f + (i % 8) * 30, 1150.0f + (i / 8) * 30, 0, 1);
                bool used[3] = {false, false, false};
                float prev[3] = {0, 0, 0};
                for (int t = 0; t < 900; ++t) {
                    w.tick(1.0f / 30.0f);
                    const sim::Unit* u = w.unit(c);
                    if (!u) break;
                    for (int sl = 0; sl < 3; ++sl) {
                        if (u->reloads[sl] > prev[sl] + 0.01f) used[sl] = true;
                        prev[sl] = u->reloads[sl];
                    }
                }
                int n = 0; for (bool b : used) if (b) ++n;
                float manaLeft = w.unit(c) ? w.unit(c)->mana : -1;
                return std::pair<int, float>{n, manaLeft};
            };
            auto [autoN, autoMana] = runFight(false);
            // Elsin carries weaponswitching, so retail holds him on ONE weapon --
            // the one the player picked (WEAPON1 by default). An earlier pass here
            // had the sim auto-pick the biggest usable weapon, which made every
            // Monarch, dragon and caster markedly stronger than retail.
            check(king->weaponSwitching, "araking carries weaponswitching");
            check(autoN == 1, "Elsin holds ONE weapon (the player's pick), like retail",
                  std::to_string(autoN) + " of 3 used");
            (void)autoMana;
            auto [manualN, manualMana] = runFight(true);
            check(manualN == 1, "and a Ctrl+W pick is still obeyed",
                  std::to_string(manualN) + " weapon used");
            (void)manualMana;
        }

        // A multi-weapon unit WITHOUT weaponswitching fires every weapon
        // independently -- retail's AA/ground turrets really do work both barrels.
        const sim::UnitType* tower = reg.find("araat");
        if (tower && prey && tower->weapons.size() > 1) {
            check(!tower->weaponSwitching, "araat has NO weaponswitching (fires all)");
            sim::World w;
            sim::MatchConfig cfg;
            cfg.vfs = &vfs; cfg.mapPath = kMap;
            cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
            cfg.slots[0].team = 0; cfg.slots[1].team = 1;
            sim::setupMatch(w, reg, cfg);
            int t = w.spawn(tower, 1000, 1000, 0, 0);
            if (auto* tu = w.unit(t)) tu->mana = tower->maxMana;
            for (int i = 0; i < 20; ++i) w.spawn(prey, 1000.0f + (i % 5) * 25, 1120.0f + (i / 5) * 25, 0, 1);
            bool used[3] = {false, false, false};
            float prev[3] = {0, 0, 0};
            for (int i = 0; i < 900; ++i) {
                w.tick(1.0f / 30.0f);
                const sim::Unit* tu = w.unit(t);
                if (!tu) break;
                for (size_t sl = 0; sl < tower->weapons.size() && sl < 3; ++sl) {
                    if (tu->reloads[sl] > prev[sl] + 0.01f) used[sl] = true;
                    prev[sl] = tu->reloads[sl];
                }
            }
            int n = 0; for (bool b2 : used) if (b2) ++n;
            check(n > 1, "araat fires BOTH of its weapons independently",
                  std::to_string(n) + " weapons fired");
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

    // ---- 4. campaign conditions + the mission GET table ----------------------
    std::printf("[campaign win/lose conditions]\n");
    {
        auto runMission = [&](const char* stem, sim::World& w) {
            int human = 0;
            return sim::setupMission(w, reg, vfs, stem, human);
        };
        auto tickM = [&](sim::World& w, float sec) {
            for (int i = 0; i < int(sec * 30); ++i) {
                w.tick(1.0f / 30.0f);
                if (w.missionOutcome()) break;
            }
        };
        // takmission28_dh is an escort: NPCDERN must cross Z=11 (victory) and losing
        // every Dern is defeat. Neither condition was parsed, so it could not be
        // won OR lost. (It is also one of the 11 missions that ship no .cob.)
        const sim::UnitType* dern = reg.find("npcdern");
        {
            sim::World w;
            if (runMission("takmission28_dh", w) && dern) {
                tickM(w, 2.0f);
                check(w.missionOutcome() == 0, "escort mission does not resolve at the start");
                for (auto& u : w.units())
                    if (u.type == dern) if (auto* p = w.unit(u.id)) p->hp = 0;
                tickM(w, 3.0f);
                check(w.missionOutcome() < 0, "losing every escorted unit is a DEFEAT");
            }
        }
        {
            sim::World w;
            if (runMission("takmission28_dh", w) && dern) {
                tickM(w, 2.0f);
                int moved = 0;
                for (auto& u : w.units())
                    if (u.alive() && u.type == dern)
                        if (auto* p = w.unit(u.id)) { p->z = 5 * 16.0f; ++moved; }
                tickM(w, 2.0f);
                check(moved > 0 && w.missionOutcome() > 0,
                      "getting it across the line is a VICTORY");
            }
        }
        // takmission11_dh: the human plays Taros and must keep TARPRIE2 alive.
        {
            sim::World w;
            const sim::UnitType* pr = reg.find("tarprie2");
            if (runMission("takmission11_dh", w) && pr) {
                tickM(w, 2.0f);   // let the condition arm (it must not fire at t=0)
                for (auto& u : w.units())
                    if (u.type == pr) if (auto* p = w.unit(u.id)) p->hp = 0;
                tickM(w, 3.0f);
                check(w.missionOutcome() < 0, "AllUnitsKilledOfType protects YOUR units");
            }
        }
        // takmission04_ph wins on GET(5, player1) < 5 -- a living-unit count. While
        // the GET table returned 0 for everything, that read as 0 < 5 and the
        // mission declared victory almost immediately.
        {
            sim::World w;
            if (runMission("takmission04_ph", w)) {
                int alive = 0;
                for (const auto& u : w.units()) if (u.alive()) ++alive;
                tickM(w, 5.0f);
                check(alive > 5 && w.missionOutcome() <= 0,
                      "a unit-count victory no longer fires instantly",
                      std::to_string(alive) + " units alive, outcome=" +
                          std::to_string(w.missionOutcome()));
            }
        }
    }

    // --- Random Start Locations -------------------------------------------
    // Retail's room option. Two requirements: the same seed must reproduce the
    // same arrangement on every peer (lockstep), and a different seed must
    // actually move somebody (otherwise the option does nothing).
    std::printf("[random start locations]\n");
    {
        auto startsFor = [&](bool rnd, uint32_t seed) {
            sim::World w;
            sim::MatchConfig cfg;
            cfg.vfs = &vfs;
            cfg.mapPath = kMap;
            cfg.randomStarts = rnd;
            cfg.startSeed = seed;
            cfg.slots.resize(4);
            for (int i = 0; i < 4; ++i) { cfg.slots[size_t(i)].used = true;
                                          cfg.slots[size_t(i)].faction = i % 5;
                                          cfg.slots[size_t(i)].team = uint8_t(i); }
            return sim::setupMatch(w, reg, cfg);
        };
        auto fixedA = startsFor(false, 111), fixedB = startsFor(false, 222);
        check(fixedA == fixedB, "fixed starts ignore the seed");
        auto randA = startsFor(true, 111), randA2 = startsFor(true, 111);
        check(randA == randA2, "the same seed reproduces the same arrangement");
        auto randB = startsFor(true, 999);
        bool moved = randA != fixedA || randB != fixedA;
        check(moved, "a shuffled match doesn't just hand back the fixed order");
        bool sameSet = true;
        {
            auto a = fixedA, b = randA;
            std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
            sameSet = a == b;
        }
        check(sameSet, "the shuffle permutes the map's starts, it doesn't invent them");
    }

    // --- Tier-4 "cosmetic lows" ------------------------------------------
    // Several of these are display-only, but they all start with an FBI field that
    // must actually reach UnitType/Weapon -- which is the part that silently rots.
    std::printf("[cosmetic lows: FBI fields reach the registry]\n");
    {
        auto ut = [&](const char* id) { return reg.find(id); };
        if (const auto* wall = ut("arawall"))
            check(wall->noShadow, "arawall carries noshadow");
        if (const auto* harp = ut("verharp"))
            check(harp->floater, "verharp is a floater (retail draws it no shadow)");
        if (const auto* god = ut("aragod")) {
            check(god->canHover, "aragod wades (canhover)");
            check(god->waterline == 40, "and sinks to its waterline",
                  "waterline=" + std::to_string(god->waterline));
        }
        if (const auto* gg = ut("cregod"))
            check(gg->ghost, "the Ghost of Garacaius is a ghost (translucent)");
        // Shadow suppression is the render rule those flags feed.
        if (const auto* wall = ut("arawall"))
            check(!(wall->maxVel > 0) || !wall->noShadow, "a wall is a structure anyway");
        if (const auto* trans = ut("aratrans"))
            check(int(trans->transportDist) == 419,
                  "aratrans loads from its own transportdistance, not a constant 70",
                  std::to_string(int(trans->transportDist)) + "px");
        // turninplacerate: present on most movers, and our default falls back to
        // turnrate rather than retail's 0 (which would freeze a pivot entirely).
        if (const auto* sw = ut("arasword"))
            check(sw->turnInPlaceRate > 0, "a swordsman can pivot on the spot",
                  std::to_string(sw->turnInPlaceRate));
        // AdjustJoy is a repair aura; the Acolyte is the headline carrier.
        if (const auto* pr = ut("arapries")) {
            int joy = 0;
            for (const auto& a : pr->auras)
                if (a.kind == sim::Aura::Kind::Joy && a.radius > 0) ++joy;
            check(joy == 1, "the Acolyte carries one AdjustJoy aura");
            for (const auto& a : pr->auras)
                if (a.kind == sim::Aura::Kind::Joy) {
                    // Retail's falloff is weakest at the emitter, full at the rim.
                    check(a.falloff(0.0f) < a.falloff(a.radius),
                          "the aura is weaker at the centre than at the rim",
                          std::to_string(a.falloff(0.0f)) + " -> " +
                              std::to_string(a.falloff(a.radius)));
                }
        }
    }
    // The repair aura, end to end: a damaged unit next to an Acolyte heals, and the
    // Acolyte's owner pays for it.
    std::printf("[AdjustJoy repair aura]\n");
    {
        const sim::UnitType* pri = reg.find("arapries");
        const sim::UnitType* vic = reg.find("arasword");
        if (pri && vic) {
            sim::World w;
            sim::MatchConfig cfg;
            cfg.vfs = &vfs;
            cfg.mapPath = kMap;
            cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
            sim::setupMatch(w, reg, cfg);
            int pid = w.spawn(pri, 600, 600, 0, 0);
            int vid = w.spawn(vic, 660, 600, 0, 0);   // well inside radius 200
            (void)pid;
            sim::Unit* v = w.unit(vid);
            v->hp = v->type->maxHp * 0.25f;
            float hp0 = v->hp;
            float mana0 = w.player(0).mana;
            for (int i = 0; i < 60; ++i) w.tick(1.0f / 30.0f);   // 2s = two 1Hz pulses
            float hp1 = w.unit(vid)->hp;
            check(hp1 > hp0, "a damaged ally next to an Acolyte is repaired",
                  std::to_string(int(hp0)) + " -> " + std::to_string(int(hp1)));
            check(w.player(0).mana < mana0, "and the Acolyte's owner pays the mana",
                  std::to_string(int(mana0)) + " -> " + std::to_string(int(w.player(0).mana)));
            // An undamaged unit must not be touched (and must still count toward N).
            int fid = w.spawn(vic, 640, 620, 0, 0);
            float full0 = w.unit(fid)->hp;
            for (int i = 0; i < 60; ++i) w.tick(1.0f / 30.0f);
            check(w.unit(fid)->hp <= full0 + 0.01f, "an undamaged ally is left alone");
        }
    }

    // The turn/brake coupling replaced a flat "big turn -> 30% speed" cliff with
    // retail's arc-distance rule. The risk is a unit that brakes forever and never
    // arrives, so test arrival on a straight run AND on a right-angle dogleg.
    std::printf("[turn/brake coupling]\n");
    {
        const sim::UnitType* sw = reg.find("arasword");
        if (sw) {
            sim::World w;
            sim::MatchConfig cfg;
            cfg.vfs = &vfs;
            cfg.mapPath = kMap;
            cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
            sim::setupMatch(w, reg, cfg);
            // heading 0 faces +Z and pi/2 faces +X, so ordering a unit due +X gives
            // an aligned start or a 90-degree departure depending on which we spawn
            // with. Same corridor for both, so terrain can't explain a difference.
            auto runTo = [&](float sz, float head, float secs) {
                int id = w.spawn(sw, 600, sz, head, 0);
                w.order(id, 900, sz, false);
                for (int i = 0; i < int(secs * 30); ++i) w.tick(1.0f / 30.0f);
                const sim::Unit* u = w.unit(id);
                float dx = u->x - 900.0f, dz = u->z - sz;
                return std::sqrt(dx * dx + dz * dz);
            };
            float aligned = runTo(600, 1.5707963f, 20.0f);
            check(aligned < 48.0f, "a 300px run with an aligned start arrives",
                  std::to_string(int(aligned)) + "px short");
            float turning = runTo(640, 0.0f, 20.0f);
            check(turning < 48.0f, "and so does a 90-degree departure",
                  std::to_string(int(turning)) + "px short");
        }
    }

    // --- missed shots land; lobbers shoot over walls ----------------------
    std::printf("[ballistic: fields + lobbing]\n");
    {
        auto wpn = [&](const char* id, int slot) -> const sim::Weapon* {
            const sim::UnitType* t = reg.find(id);
            if (!t || int(t->weapons.size()) <= slot) return nullptr;
            return &t->weapons[size_t(slot)];
        };
        if (const sim::Weapon* w = wpn("arapult", 0)) {
            check(w->lobPreferred, "the Aramon catapult lobs");
            check(std::abs(w->gravityAdj - 4.25f) < 0.01f,
                  "and carries its own gravityadjustment",
                  std::to_string(w->gravityAdj));
            check(w->aoe > 0, "its cannonball has a blast radius");
        }
        if (const sim::Weapon* w = wpn("arabow", 0))
            check(!w->lobPreferred, "an archer does not lob");
        if (const sim::UnitType* t = reg.find("vermort"))
            check(t->lobs(), "the Veruna mortar lobs (exempt from the LoS gate)");
        if (const sim::UnitType* t = reg.find("arabow"))
            check(!t->lobs(), "an archer is not");
        // dontleadtargets ships only on Dropped bombs, whose weaponvelocity is a
        // fall parameter -- leading on it would throw the aim point off the map.
        int noLead = 0;
        for (const auto& [id, t] : reg.types())
            for (const auto& w : t.weapons)
                if (w.noLead) ++noLead;
        check(noLead > 0, "dontleadtargets reaches the registry",
              std::to_string(noLead) + " weapon blocks");
    }
    // A missed splash shell must crater. Fire a catapult, wait for the shell to be
    // in the air, then move the target out from under it -- the shell must still
    // land where it was aimed and splash whatever is standing there.
    // Every faction's wall must actually block. Two of the four declare canmove=1
    // with no velocity, so a canMove gate left them blocking nothing at all.
    std::printf("[walls block, all four factions]\n");
    {
        for (const char* id : {"arawall", "crewall", "verwall", "tarwall"}) {
            const sim::UnitType* t = reg.find(id);
            if (!t) continue;
            sim::World w;
            sim::MatchConfig cfg;
            cfg.vfs = &vfs;
            cfg.mapPath = kMap;
            cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
            sim::setupMatch(w, reg, cfg);
            // Find open ground rather than assuming a cell: Inner Circle is mostly
            // blocked terrain and a hardcoded cell tests nothing.
            int cx = -1, cz = -1;
            for (int j = 30; j < 70 && cx < 0; ++j)
                for (int i = 30; i < 70; ++i)
                    if (w.nav().walkable(i, j) && w.nav().walkable(i + 1, j) &&
                        w.nav().walkable(i, j + 1) && w.nav().walkable(i + 1, j + 1)) {
                        cx = i; cz = j; break;
                    }
            if (cx < 0) { check(false, "found open ground to build a wall on"); continue; }
            float wx = float(cx) * 16 + 8 + 16, wz = float(cz) * 16 + 8 + 16;
            bool before = w.nav().walkable(cx, cz);
            w.spawn(t, wx, wz, 0, 0);
            sim::blockFootprint(w.nav(), *t, wx, wz, true);
            bool after = w.nav().walkable(cx, cz);
            check(before && !after, "the wall blocks its footprint",
                  std::string(id) + " (canmove=" + (t->canMove ? "1" : "0") + ")");
        }
    }
    std::printf("[a missed shell craters]\n");
    {
        const sim::UnitType* pult = reg.find("arapult");
        const sim::UnitType* vic = reg.find("arasword");
        if (pult && vic) {
            sim::World w;
            sim::MatchConfig cfg;
            cfg.vfs = &vfs;
            cfg.mapPath = kMap;
            cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
            cfg.slots[0].team = 0; cfg.slots[1].team = 1;
            sim::setupMatch(w, reg, cfg);
            int gun = w.spawn(pult, 600, 900, 0, 0);
            int mark = w.spawn(vic, 900, 900, 0, 1);
            int bystander = w.spawn(vic, 916, 900, 0, 1);   // beside the aim point
            float by0 = w.unit(bystander)->hp;
            w.attack(gun, mark, false);
            bool flew = false, moved = false;
            for (int i = 0; i < 30 * 20; ++i) {
                w.tick(1.0f / 30.0f);
                if (!moved && !w.projectiles().empty()) {
                    flew = true;
                    // Yank the target well clear, so the shell cannot connect.
                    sim::Unit* m = w.unit(mark);
                    m->x = 600; m->z = 1400;
                    moved = true;
                }
            }
            check(flew, "the catapult actually fired");
            float by1 = w.unit(bystander)->hp;
            check(by1 < by0, "the shell still lands and splashes the aim point",
                  std::to_string(int(by0)) + " -> " + std::to_string(int(by1)));
        }
    }
    // ...but a shot that CONNECTS must not also crater on the same tick (the
    // life<=0 expiry and the direct hit can coincide -- that is what `spent` guards).
    std::printf("[a hit does not double-dip]\n");
    {
        const sim::UnitType* pult = reg.find("arapult");
        const sim::UnitType* vic = reg.find("araking");   // tough enough to survive
        if (pult && vic) {
            sim::World w;
            sim::MatchConfig cfg;
            cfg.vfs = &vfs;
            cfg.mapPath = kMap;
            cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
            cfg.slots[0].team = 0; cfg.slots[1].team = 1;
            sim::setupMatch(w, reg, cfg);
            int gun = w.spawn(pult, 600, 1100, 0, 0);
            int sitting = w.spawn(vic, 900, 1100, 0, 1);   // stationary: no lead, direct hit
            float hp0 = w.unit(sitting)->hp;
            w.attack(gun, sitting, false);
            // Long enough for exactly one shell to be fired and to land, but well
            // inside the 6.5s reload so a second can't confuse the total.
            for (int i = 0; i < 30 * 6; ++i) w.tick(1.0f / 30.0f);
            float lost = hp0 - w.unit(sitting)->hp;
            const sim::Weapon& cw = pult->weapons[0];
            float once = cw.damageVs(vic);
            check(lost <= once * 1.60f,
                  "one shell deals about one shell's damage, not two",
                  std::to_string(int(lost)) + " vs " + std::to_string(int(once)));
        }
    }

    // --- units are solid --------------------------------------------------
    // The dangerous change in this area is not "does blocking work" but "does an
    // army still move". Test both, and test the corridor case that deadlocks first.
    std::printf("[units are solid]\n");
    {
        const sim::UnitType* sw = reg.find("arasword");
        if (sw) {
            // (a) A parked body blocks: walk one unit into a stationary one and
            //     assert it cannot pass through.
            {
                sim::World w;
                sim::MatchConfig cfg;
                cfg.vfs = &vfs; cfg.mapPath = kMap;
                cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
                sim::setupMatch(w, reg, cfg);
                int blocker = w.spawn(sw, 700, 600, 0, 0);
                int walker = w.spawn(sw, 640, 600, 0, 0);
                for (int i = 0; i < 30 * 10; ++i) w.tick(1.0f / 30.0f);   // settle
                w.order(walker, 800, 600, false);                          // straight through
                for (int i = 0; i < 30 * 10; ++i) w.tick(1.0f / 30.0f);
                const sim::Unit* b = w.unit(blocker);
                const sim::Unit* m = w.unit(walker);
                // The discriminating assertion: the walker was sent to x=800, PAST
                // the blocker at 700. With only separation it shoulders through and
                // arrives; solid, it is still on the near side or squeezing round.
                check(m->x < 800.0f - 40.0f,
                      "a parked body actually stops a walker (not just spaces it)",
                      "walker x=" + std::to_string(int(m->x)) +
                          " blocker x=" + std::to_string(int(b->x)));
            }
            // (b) An army still moves. 24 units ordered across open ground must
            //     nearly all arrive -- this is the regression that matters.
            {
                sim::World w;
                sim::MatchConfig cfg;
                cfg.vfs = &vfs; cfg.mapPath = kMap;
                cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
                sim::setupMatch(w, reg, cfg);
                std::vector<int> army;
                for (int j = 0; j < 4; ++j)
                    for (int i = 0; i < 6; ++i)
                        army.push_back(w.spawn(sw, 600.0f + float(i) * 20.0f,
                                               600.0f + float(j) * 20.0f, 0, 0));
                for (int id : army) w.order(id, 900, 700, false);
                for (int i = 0; i < 30 * 40; ++i) w.tick(1.0f / 30.0f);
                int arrived = 0;
                for (int id : army) {
                    const sim::Unit* u = w.unit(id);
                    float dx = u->x - 900.0f, dz = u->z - 700.0f;
                    if (std::sqrt(dx * dx + dz * dz) < 120.0f) ++arrived;
                }
                check(arrived >= int(army.size()) * 3 / 4,
                      "an army of 24 still reaches its destination",
                      std::to_string(arrived) + "/" + std::to_string(army.size()));
            }
            // (c) Two columns marching THROUGH each other must not lock. This is
            //     deadlock mode #1, and the reason occupancy records only parked
            //     bodies -- two moving units can never block one another.
            {
                sim::World w;
                sim::MatchConfig cfg;
                cfg.vfs = &vfs; cfg.mapPath = kMap;
                cfg.slots = {sim::MatchSlot{}, sim::MatchSlot{}};
                sim::setupMatch(w, reg, cfg);
                std::vector<int> east, west;
                for (int i = 0; i < 6; ++i) {
                    east.push_back(w.spawn(sw, 620.0f + float(i) * 18.0f, 660, 0, 0));
                    west.push_back(w.spawn(sw, 880.0f - float(i) * 18.0f, 660, 0, 0));
                }
                for (int id : east) w.order(id, 900, 660, false);
                for (int id : west) w.order(id, 600, 660, false);
                for (int i = 0; i < 30 * 40; ++i) w.tick(1.0f / 30.0f);
                int through = 0;
                for (int id : east) if (w.unit(id)->x > 820.0f) ++through;
                for (int id : west) if (w.unit(id)->x < 680.0f) ++through;
                check(through >= 8, "two columns pass through each other",
                      std::to_string(through) + "/12 got past");
            }
        }
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
