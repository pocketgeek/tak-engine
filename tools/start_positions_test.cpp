// Fixed start locations follow retail: the SEATED players take the map's starts
// in slot order, and an empty seat takes none. A human in slot 1 and an AI in
// slot 8 therefore start at StartPos1 and StartPos2, not 1 and 8.
//
// Established in KINGDOMS.icd, statically and by emulation (docs/retail-engine.md,
// "Start positions"): the skirmish spawn at 0x4d2e70 and the multiplayer host's
// start table at 0x4ea950 both walk the ten player records (one per battle-room
// row) and give the k-th occupied one the k-th start. This pins that rule so a
// report like "slot 8 should start at 8" is not "fixed" into a non-retail game.
#include "hpi/hpi.h"
#include "net/matchconfig.h"
#include "sim/matchsetup.h"
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <string>
using namespace tak;

static void check(bool ok, const std::string& text) { if (!ok) throw std::runtime_error(text); }

static std::vector<uint8_t> mapBytes() {
    tnt::Map map; map.width = map.height = 128; map.blocksX = map.blocksY = 64;
    map.heights.assign(size_t(128) * 128, 100); map.features.assign(size_t(128) * 128, 0xffff);
    const auto blocks = size_t(map.blocksX) * map.blocksY;
    map.tileKeys.resize(blocks); map.tileCols.resize(blocks); map.tileRows.resize(blocks);
    map.minimapW = map.minimapH = map.overviewW = map.overviewH = 1;
    map.minimap = {0}; map.overview = {0}; return map.save();
}

// Eight starts, numbered down the west edge (1-4) and up the east edge (5-8), as
// a "4 vs 4" map lays out its teams. Stored out of order: the number decides.
static const int kStart[8][2] = {{8, 8}, {8, 40}, {8, 72}, {8, 104},
                                 {120, 104}, {120, 72}, {120, 40}, {120, 8}};

static std::string ota() {
    std::string s = "[GlobalHeader]\n{\nmissionname=Starts;\nnumplayers=8;\n[Map Data]\n{\n[specials]\n{\n";
    for (int n : {5, 1, 8, 2, 7, 3, 6, 4})
        s += "[special" + std::to_string(n) + "]\n{\nspecialwhat=StartPos" + std::to_string(n) +
             ";\nXPos=" + std::to_string(kStart[n - 1][0]) + ";\nZPos=" + std::to_string(kStart[n - 1][1]) + ";\n}\n";
    return s + "}\n}\n}\n";
}

// Which StartPos (1-based) the Monarch of each seated slot got, in slot order.
static std::vector<int> startsFor(const hpi::Vfs& vfs, std::initializer_list<std::pair<int, uint8_t>> seats,
                                  bool random = false, uint32_t seed = 1) {
    net::SlotInfo slots[net::kMaxSlots];
    for (auto [slot, type] : seats) {
        slots[slot].type = type;                  // 1 human, 2 AI
        slots[slot].faction = uint8_t(slot % 5);
        slots[slot].team = uint8_t(slot);
    }
    net::GameOptions o; o.randomStarts = random ? 1 : 0;
    // The builder every peer, the referee and replay playback use.
    auto cfg = net::matchConfigFor(o, slots, seed);
    cfg.vfs = &vfs; cfg.mapPath = "maps/starts.tnt";
    sim::TypeRegistry reg; sim::World world;
    const auto spots = sim::setupMatch(world, reg, cfg);
    std::vector<int> got;
    for (const auto& [x, z] : spots) {
        int n = 0;
        for (int k = 0; k < 8; ++k)
            if (x == float(kStart[k][0] * 16) && z == float(kStart[k][1] * 16)) n = k + 1;
        got.push_back(n);
    }
    return got;
}

static std::string show(const std::vector<int>& v) {
    std::string s;
    for (int n : v) s += (s.empty() ? "" : ",") + std::to_string(n);
    return s;
}

int main() try {
    auto files = std::make_shared<hpi::Vfs::Files>();
    (*files)["maps/starts.tnt"] = mapBytes();
    const auto text = ota(); (*files)["maps/starts.ota"] = {text.begin(), text.end()};
    hpi::Vfs vfs; vfs.setMapFiles(files);
    const auto parsed = sim::parseStartPositions(vfs, "maps/starts.tnt");
    check(parsed.size() == 8 && parsed[0] == std::make_pair(128.f, 128.f) &&
          parsed[7] == std::make_pair(1920.f, 128.f), "the starts are not read in StartPos order");

    // The reported game: a human in slot 1 and an AI in slot 8, slots 2-7 open.
    // Retail's 0x4d2e70 spawns them at StartPos1 and StartPos2.
    auto got = startsFor(vfs, {{0, 1}, {7, 2}});
    check(got == std::vector<int>{1, 2}, "human slot 1 + AI slot 8 should take starts 1,2 as retail does, got " + show(got));
    // Gaps anywhere collapse the same way: the k-th seated slot takes start k.
    got = startsFor(vfs, {{0, 1}, {2, 2}, {5, 2}, {7, 2}});
    check(got == std::vector<int>{1, 2, 3, 4}, "seats 1,3,6,8 should take starts 1-4, got " + show(got));
    // An empty first seat does not reserve StartPos1 either.
    got = startsFor(vfs, {{3, 2}, {6, 1}});
    check(got == std::vector<int>{1, 2}, "seats 4,7 should take starts 1,2, got " + show(got));
    // With every seat taken, slot N is at StartPos N.
    got = startsFor(vfs, {{0, 1}, {1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}});
    check(got == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}, "a full table should be slot N at start N, got " + show(got));
    // Fixed starts do not depend on the match seed.
    check(startsFor(vfs, {{0, 1}, {7, 2}}, false, 99) == std::vector<int>{1, 2}, "fixed starts moved with the seed");
    // Random starts still give each seated player a distinct real start.
    got = startsFor(vfs, {{0, 1}, {7, 2}}, true, 12345);
    check(got.size() == 2 && got[0] && got[1] && got[0] != got[1], "random starts lost a start, got " + show(got));
    std::puts("PASS fixed starts: seated slots take StartPos1..N in slot order (retail 0x4d2e70 / 0x4ea950)");
    return 0;
} catch (const std::exception& e) { std::fprintf(stderr, "FAIL %s\n", e.what()); return 1; }
