// sweetspot_test: a shooter aims at its target's drawn SweetSpot.
//
//   sweetspot_test <retail-install-dir>
//
// Retail's SweetSpot (4dd4f0 -> 4dd2a0) bounds the piece's vertex buffer that
// the draw path rewrites in place (4ee620 -> 4ee920/4eea20: piece chain plus
// body pitch/heading/roll), so a drawn Zhon Hunter's SweetSpot sits at its
// torso's middle, about 19 px up. The simulation once bounded the authored
// (local) torso vertices instead, which put the aim point 1.24 px below the
// feet: Hunters lobbed every spear into the ground in front of each other
// (a 0.9.0 game: 727 Hunter-on-Hunter shots, 0 hits).
//
// Checks, in both balances, on shipped zonter:
// - the rest-pose SweetSpot offset against four drawn Hunters emulated from a
//   live retail capture (4dd4f0 on next-device-state-01, tick 11134);
// - two Hunters on flat ground in that game's geometry (dx -272, dz +192,
//   333 px): the shooter's spears hit its standing target.
//
// A dev harness; not shipped in a game.

#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace tak;

namespace {
int failures = 0;

void check(bool ok, const char* what, const std::string& detail = "") {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) ++failures;
}

void flatGround(sim::World& w) {
    w.setVisPlayer(-1);
    w.setTerrain(std::vector<uint8_t>(64 * 64, 62), 64, 64, 0);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: sweetspot_test <retail-install-dir>\n"); return 2; }
    const hpi::Vfs vfs = hpi::mountRetailRoot(argv[1]);
    for (const bool crusades : {false, true}) {
        std::printf("[%s balance]\n", crusades ? "Crusades" : "Original");
        sim::TypeRegistry reg;
        sim::setupRegistry(reg, vfs, crusades);
        const sim::UnitType* hunter = reg.find("zonter");
        if (!hunter) { check(false, "shipped zonter loads"); continue; }

        // Retail heading -> SweetSpot offset (px) of four drawn Hunters at rest.
        struct Captured { uint16_t heading; float x, y, z; };
        const Captured captured[] = {{8192, 1.54f, 18.99f, -1.55f}, {40960, -1.54f, 18.99f, 1.55f},
                                     {57508, -1.55f, 18.99f, -1.53f}, {42005, -1.51f, 18.99f, 1.56f}};
        {
            sim::World w;
            flatGround(w);
            const int id = w.spawn(hunter, 512, 512, 0, 0);
            w.tick(1.f / 30);
            for (const auto& c : captured) {
                sim::Unit* u = w.unit(id);
                u->heading = sim::Bam(uint16_t(c.heading + 32768));   // portHeadingToRetail inverse
                const auto point = w.queryUnitScriptPoint(id, true);
                const float x = float(point[0] - u->x.v) / 65536.f, y = float(point[1] - u->groundY.v) / 65536.f,
                            z = float(point[2] - u->z.v) / 65536.f;
                char buf[160];
                std::snprintf(buf, sizeof buf, "heading %u: (%.2f, %.2f, %.2f), retail (%.2f, %.2f, %.2f)",
                              unsigned(c.heading), x, y, z, c.x, c.y, c.z);
                check(std::fabs(x - c.x) < 0.03f && std::fabs(y - c.y) < 0.03f && std::fabs(z - c.z) < 0.03f,
                      "zonter SweetSpot is the drawn torso bounds centre", buf);
            }
        }

        // The 0.9.0 pair: shooter 213 at rest, target 162 at dx -272, dz +192.
        {
            sim::World w;
            flatGround(w);
            const float sx = 700, sz = 300;
            const int shooter = w.spawn(hunter, sx, sz, 0, 0);
            const int target = w.spawn(hunter, sx - 272, sz + 192, 0, 1);
            w.setStance(target, 2);
            w.unit(target)->moveState = 0;
            w.unit(target)->fireState = 0;
            w.unit(target)->hp = sim::Fixed::fromInt(30000);
            w.unit(shooter)->moveState = 0;
            for (int i = 0; i < 30; ++i) w.tick(1.f / 30);
            w.attack(shooter, target, false);
            int shots = 0, hits = 0;
            for (int i = 0; i < 45 * 30; ++i) {
                w.tick(1.f / 30);
                for (const auto& h : w.hits()) {
                    if (h.fromPlayer != 0) continue;
                    ++shots;
                    hits += h.victimId == target;
                }
            }
            const auto* t = w.unit(target);
            const auto* s = w.unit(shooter);
            const float moved = s ? std::hypot(s->x.toFloat() - sx, s->z.toFloat() - sz) : -1.f;
            char buf[160];
            std::snprintf(buf, sizeof buf, "impacts=%d hits=%d damage=%.0f shooter moved %.0f px", shots, hits,
                          t ? 30000.f - t->hp.toFloat() : -1.f, moved);
            check(shots >= 8 && hits * 10 >= shots * 8, "a Hunter's spears hit a standing Hunter at 333 px", buf);
        }
    }
    std::printf(failures ? "FAIL: %d checks\n" : "PASS: drawn SweetSpot aim\n", failures);
    return failures ? 1 : 0;
}
