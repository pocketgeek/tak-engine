#include "tnt/mapgen.h"

#include <algorithm>
#include <array>

namespace tak::mapgen {

namespace {

// ---- deterministic integer helpers (no float -> identical on every peer) -------

uint64_t splitmix(uint64_t& s) {
    uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

// Hashed lattice value 0..255 at integer grid point (x,y) for a given seed.
uint32_t latticeVal(uint64_t seed, int x, int y) {
    uint64_t h = seed ^ 0x100000001b3ULL;
    h = (h ^ uint64_t(uint32_t(x))) * 0x9e3779b97f4a7c15ULL;
    h = (h ^ uint64_t(uint32_t(y))) * 0xc2b2ae3d27d4eb4fULL;
    h ^= h >> 29; h *= 0xbf58476d1ce4e5b9ULL; h ^= h >> 32;
    return uint32_t(h & 0xff);
}

// Bilinear value noise 0..255 at cell (cx,cz), lattice spacing L (>0).
uint32_t noise(uint64_t seed, int cx, int cz, int L) {
    int gx = (cx >= 0 ? cx : cx - L + 1) / L, gz = (cz >= 0 ? cz : cz - L + 1) / L;
    int fx = cx - gx * L, fz = cz - gz * L;                 // 0..L-1
    uint32_t v00 = latticeVal(seed, gx, gz),     v10 = latticeVal(seed, gx + 1, gz);
    uint32_t v01 = latticeVal(seed, gx, gz + 1), v11 = latticeVal(seed, gx + 1, gz + 1);
    uint32_t a = (v00 * uint32_t(L - fx) + v10 * uint32_t(fx)) / uint32_t(L);
    uint32_t b = (v01 * uint32_t(L - fx) + v11 * uint32_t(fx)) / uint32_t(L);
    return (a * uint32_t(L - fz) + b * uint32_t(fz)) / uint32_t(L);
}

// Two-octave fractal noise 0..255 (large land masses + medium detail).
uint32_t fractal(uint64_t seed, int cx, int cz, int span) {
    int L1 = std::max(8, span / 5), L2 = std::max(4, span / 11);
    uint32_t a = noise(seed, cx, cz, L1);
    uint32_t b = noise(seed ^ 0xABCDEF, cx, cz, L2);
    return (a * 7 + b * 3) / 10;   // 70% coarse + 30% fine
}

// Per-world ground + sea terrain section JPG keys (terrain/<key:08x>.jpg), verified
// present in terrain.hpi, plus each section's VALID wallpaper period in 32px tiles.
// Ground fills are seamless across their whole grid (16x16; Zhon's is 256px = 8x8).
// The sea JPGs are 512px but only PARTLY art: Aramon/Taros/Zhon carry a 5x5 block
// of flat dark-teal water in the top-left corner and pure-black padding elsewhere
// (only Veruna's dgsea fills all 16x16) -- wallpapering them %16 sampled ~90%
// padding, which is what made generated water look black.
struct WorldArt { uint32_t ground, sea; uint8_t groundK, seaK; };
constexpr std::array<WorldArt, kMapTypes> kWorldArt = {{
    {0x868a8222u, 0x0c2e64b2u, 16, 5},    // Aramon: grass / asea
    {0xca4200f8u, 0xb8524a38u, 16, 5},    // Taros:  low_ground / taros_sea
    {0xd3259f0fu, 0x9a5c5436u, 16, 16},   // Veruna: A512Lowland / dgsea (full-art sea)
    {0x9bb50b09u, 0x59f53dfdu, 8, 5},     // Zhon:   jungletile80 (256px) / H2OTILE
    // Creon (Iron Plague, IPData.hpi): green turf fill mined from the takx19-26
    // mission maps (their other big fill, 93c585b7, is city cobblestone). Every
    // shipped Creon map is DRY -- IP never used a Creon deep-sea fill -- so water
    // borrows Veruna's dgsea (full 16x16 teal). CreWave01-16 exist for shores.
    {0x814dddc1u, 0x9a5c5436u, 16, 16},   // Creon:  turf / (Veruna sea stand-in)
}};

// ---- shore-transition art ------------------------------------------------------
// Retail coastlines are hand-painted bank/beach prefab sections, not a hard
// ground/sea tile flip. Mined per world from the shipped maps' shoreline blocks
// (aramon/taros/veruna/zhon) and from the creon/shores prefab sections in
// IPSections.hpi: for each 4-bit corner case (bit0 NW, bit1 NE, bit2 SW, bit3 SE
// wet) the authored transition tile. Straight edges cycle a run of tiles along the
// coast; the prefabs paint continuous bands, so the line one step toward land is
// the bank-approach art (ring0) and one step toward water the shallow art (ring15).
// All 512px (16x16-tile) sections, identity col/row. Display only -- never hashed.
struct ShoreEdge { uint32_t key; uint8_t fixed; uint8_t runLen; uint8_t run[16]; };
struct ShoreTile { uint32_t key; uint8_t col, row; };
struct WorldShore {
    ShoreEdge n, s, w, e;            // water N (case 3), S (12), W (5), E (10)
    ShoreTile c1, c2, c4, c8;        // inner corners: water pocket NW / NE / SW / SE
    ShoreTile c7, c11, c13, c14;     // outer bends: land only SE / SW / NE / NW
};
// All entries mined from each world's designed "Coast Sandy" prefab kit in
// sections.hpi (creon/shores in IPSections.hpi): straights from the n/s/e/w
// pieces (contiguous authored runs only, so adjacent stamped tiles are adjacent
// art), outer bends from the ne/nw/se/sw pieces, inner pockets from the
// dedicated inner-corner pieces.
constexpr std::array<WorldShore, kMapTypes> kWorldShore = {{
    {   // Aramon: ascoast01a-12a_80 (01-04 straights, 05-08 outer, 09-12 inner)
        {0xa3e7d383u, 5, 3, {13, 14, 15}},
        {0xdb770551u, 13, 4, {7, 8, 9, 10}},
        {0x4a5aaee2u, 2, 3, {11, 12, 13}},
        {0x4e806e78u, 11, 4, {5, 6, 7, 8}},
        {0x4c542c48u, 3, 4}, {0xaf614593u, 12, 2}, {0x9e40bceau, 1, 11}, {0xa4669c42u, 13, 13},
        {0x13d16b0du, 2, 4}, {0x23132165u, 10, 3}, {0xb2e62e1eu, 1, 8}, {0xc66aa8d4u, 13, 13},
    },
    {   // Taros: Coast Sandy n01/s01/e01/w01 + ne/nw/se/sw + nee/nwe/see/swe
        // (runs picked so the ring lines beside them are painted -- these JPGs
        //  leave a few deep-water tiles black where maps use the sea fill)
        {0xa8b04632u, 6, 6, {10, 11, 12, 13, 14, 15}},
        {0x5b4bb9d9u, 11, 9, {0, 1, 2, 3, 4, 5, 6, 7, 8}},
        {0x922cde10u, 6, 7, {9, 10, 11, 12, 13, 14, 15}},
        {0xea4a228eu, 9, 5, {4, 5, 6, 7, 8}},
        {0xeae65890u, 4, 4}, {0x35c3afadu, 10, 5}, {0x87c7b1c9u, 3, 11}, {0xc3834599u, 11, 11},
        {0xb2f2f272u, 6, 7}, {0x702a36a0u, 7, 8}, {0xbdaf4de7u, 4, 11}, {0x80a8acc8u, 9, 9},
    },
    {   // Veruna: Coast Sandy n01/s01/e01/w01 + corners
        {0x1155cdd1u, 1, 6, {8, 9, 10, 11, 12, 13}},
        {0xd5f99723u, 13, 4, {6, 7, 8, 9}},
        {0xaf4d1117u, 2, 5, {2, 3, 4, 5, 6}},
        {0x4cd42696u, 13, 3, {6, 7, 8}},
        {0x6e306cc6u, 4, 2}, {0x0060ae02u, 13, 2}, {0xf4ec3c94u, 1, 12}, {0x7d73e12fu, 14, 12},
        {0x66b4388au, 10, 8}, {0x3a20ea34u, 5, 10}, {0x594351e3u, 6, 6}, {0xaccce484u, 13, 13},
    },
    {   // Zhon: Coast Sandy n1/s1/e1/w1 + 1xx outer / 2xx inner corners
        {0xdfcb5bf3u, 5, 3, {7, 8, 9}},
        {0x265e3cd8u, 12, 3, {0, 1, 2}},
        {0xbc123c42u, 5, 3, {13, 14, 15}},
        {0x30626406u, 11, 3, {7, 8, 9}},
        {0xe0320816u, 4, 4}, {0xdd6f3d8fu, 10, 5}, {0x8c6e085eu, 1, 13}, {0x5777bd91u, 10, 9},
        {0x650d3f3fu, 12, 7}, {0x2466d208u, 5, 8}, {0xa89c7242u, 6, 3}, {0xd9337b65u, 6, 6},
    },
    {   // Creon: sections/creon/shores/*.tnt prefabs (IPSections.hpi)
        {0x9cba08e2u, 8, 2, {7, 8}},
        {0x4987a7a5u, 12, 2, {7, 8}},
        {0x4157232du, 5, 4, {12, 13, 14, 15}},
        {0x8d17c1fcu, 10, 3, {6, 7, 8}},
        {0xb5155f1fu, 4, 4}, {0xe5afdbf5u, 11, 2}, {0xf5fded6du, 1, 13}, {0x7c36bc02u, 14, 13},
        {0x128e8cd4u, 8, 9}, {0x6c00cc44u, 5, 9}, {0x7c0ee09eu, 7, 10}, {0x7084884cu, 8, 8},
    },
}};

// Per-world doodad + mana palette (names verified in features/<world>/*.tdf).
// Trees (category=trees) and rocks (category=rocks) are reclaimable obstacles --
// we vary across ALL the art variants so no one type repeats. A mana deposit is a
// glowing Sacred Stone centre (XxxManaNN, category=Mana + animating=1 -- the
// buildable spot the sim harvests) ringed by static Standing Stones (XxxHengeNN,
// "ruins"), exactly as the shipped maps lay them out. Rocks are ordered small->big
// so placement can bias toward the little ones. The sim skips names it can't
// resolve, so an over-long list is harmless.
struct Span { const char* const* p; int n; };
struct WorldFeatures {
    Span trees, rocks, henges;
    std::array<const char*, 3> sacred;   // weak, medium, strong (sacredsite 1.0/1.5/2.0)
};

constexpr const char* kAraTree[] = {"AraTree01", "AraTree02", "AraTree03", "AraTree04", "AraTree05",
                                    "AraTree06", "AraTree07", "AraTree08", "AraTree09", "AraTree10"};
constexpr const char* kAraRock[] = {"AraRock01", "AraRock02", "AraRock03", "AraRock04",
                                    "AraRock05", "AraRock06", "AraRock07"};
constexpr const char* kAraHenge[] = {"AraHenge01", "AraHenge02", "AraHenge03", "AraHenge04", "AraHenge05",
                                     "AraHenge06", "AraHenge07", "AraHenge08", "AraHenge09"};

constexpr const char* kTarTree[] = {"TarTree01", "TarTree02", "TarTree03", "TarTree04", "TarTree05",
                                    "TarTree06", "TarTree07", "TarTree08", "TarTree09"};
constexpr const char* kTarRock[] = {"TarRock07", "TarRock06", "TarRock05", "TarRock04",
                                    "TarRock03", "TarRock02", "TarRock01"};   // small->big
constexpr const char* kTarHenge[] = {"TarHenge01", "TarHenge02", "TarHenge03", "TarHenge04", "TarHenge05",
                                     "TarHenge06", "TarHenge07", "TarHenge08", "TarHenge09", "TarHenge10",
                                     "TarHenge11", "TarHenge12", "TarHenge13", "TarHenge14"};

constexpr const char* kVerTree[] = {"VerTree01", "VerTree02", "VerTree03", "VerTree04", "VerTree05",
                                    "VerTree06", "VerTree07", "VerTree08", "VerTree09"};
constexpr const char* kVerRock[] = {"VeRock01", "VeRock02", "VeRock05", "VeRock07",
                                    "VeRock04", "VeRock03", "VeRock06"};   // small->big
constexpr const char* kVerHenge[] = {"VerHenge01", "VerHenge02", "VerHenge03", "VerHenge04", "VerHenge05",
                                     "VerHenge06", "VerHenge07", "VerHenge08", "VerHenge09", "VerHenge10",
                                     "VerHenge11"};

constexpr const char* kZonTree[] = {"ZonTree01", "ZonTree02", "ZonTree03",
                                    "ZonTree04", "ZonTree05", "ZonTree06"};
constexpr const char* kZonRock[] = {"ZonRock07", "ZonRock06", "ZonRock05", "ZonRock04",
                                    "ZonRock03", "ZonRock02", "ZonRock01"};   // small->big
constexpr const char* kZonHenge[] = {"ZonHenge01", "ZonHenge02", "ZonHenge03", "ZonHenge04", "ZonHenge05",
                                     "ZonHenge06", "ZonHenge07", "ZonHenge08", "ZonHenge09", "ZonHenge10",
                                     "ZonHenge11"};

// Creon (Iron Plague): features/creon/*.tdf in IPData.hpi.
constexpr const char* kCreTree[] = {"CreTree01", "CreTree02", "CreTree03", "CreTree04", "CreTree05",
                                    "CreTree06", "CreTree07", "CreTree08", "CreTree09"};
constexpr const char* kCreRock[] = {"CRERock12", "CRERock07", "CRERock09", "CRERock10", "CRERock11",
                                    "CRERock02", "CRERock03", "CRERock04", "CRERock06", "CRERock05",
                                    "CRERock13", "CRERock01", "CRERock08"};   // small->big
constexpr const char* kCreHenge[] = {"CREHenge01", "CREHenge02", "CREHenge03", "CREHenge04", "CREHenge05",
                                     "CREHenge06", "CREHenge07", "CREHenge08", "CREHenge09", "CREHenge10",
                                     "CREHenge11", "CREHenge12", "CREHenge13", "CREHenge14", "CREHenge15",
                                     "CREHenge16", "CREHenge17", "CREHenge18", "CREHenge19", "CREHenge20",
                                     "CREHenge21", "CREHenge22", "CREHenge23"};

constexpr std::array<WorldFeatures, kMapTypes> kWorldFeat = {{
    {{kAraTree, 10}, {kAraRock, 7}, {kAraHenge, 9},  {{"AraMana01", "AraMana02", "AraMana03"}}},
    {{kTarTree, 9},  {kTarRock, 7}, {kTarHenge, 14}, {{"TarMana01", "TarMana02", "TarMana03"}}},
    {{kVerTree, 9},  {kVerRock, 7}, {kVerHenge, 11}, {{"VerMana01", "VerMana02", "VerMana03"}}},
    {{kZonTree, 6},  {kZonRock, 7}, {kZonHenge, 11}, {{"ZonMana01", "ZonMana02", "ZonMana03"}}},
    {{kCreTree, 9},  {kCreRock, 13}, {kCreHenge, 23}, {{"CReMana01", "CREMana02", "CREMana03"}}},
}};

// Even compass directions (integer, scaled by 1000) for start-position rings.
constexpr std::array<std::pair<int, int>, 8> kCompass = {{
    {0, -1000}, {707, -707}, {1000, 0}, {707, 707},
    {0, 1000}, {-707, 707}, {-1000, 0}, {-707, -707},
}};

}  // namespace

Params sanitize(Params p) {
    if (p.mapType >= kMapTypes) p.mapType = Aramon;
    // Even cells, multiples of 32 (a 512px section unit), 128..768 cells/side.
    auto fix = [](uint16_t v) -> uint16_t {
        int u = std::clamp(int(v) / 32, 4, 24);   // 4..24 section-units
        return uint16_t(u * 32);
    };
    p.widthCells = fix(p.widthCells);
    p.heightCells = fix(p.heightCells);
    p.players = uint8_t(std::clamp<int>(p.players, 2, 8));
    return p;
}

Result generate(const Params& raw) {
    Params p = sanitize(raw);
    Result r;
    tak::tnt::Map& m = r.map;
    const int W = p.widthCells, H = p.heightCells;
    m.width = W; m.height = H;
    m.blocksX = W / 2; m.blocksY = H / 2;

    // ---- heightfield: terraced like the shipped maps -- a dominant ground plateau
    //      just above sea, one or two higher plateaus, joined by ramps. Terrain pixels
    //      render FLAT (all cliff relief in shipped maps is baked tile art we don't
    //      have); heights only LIFT units/features on screen. So relief stays gentle
    //      (no cliff faces to float over) and the ground plateau is the modal height,
    //      so flat ground lifts by zero and only raised terraces rise. ---------------
    m.seaLevel = 58;
    const int span = std::max(W, H);
    // waterDensity picks how much of the fractal range falls below the shoreline
    // (default ~96 -> ~19% water; the slider spans a few percent up to mostly ocean).
    const int seaThresh = std::clamp(40 + int(p.waterDensity) * 85 / 255, 30, 180);
    const int landRange = std::max(1, 255 - seaThresh);
    const int t1 = seaThresh + landRange * 60 / 100;   // ground plateau -> ~60% of land
    const int t2 = seaThresh + landRange * 85 / 100;   // mid plateau    -> ~25% of land
    const int kDeep = 28, kL0 = 72, kL1 = 112, kL2 = 156;   // discrete terrace heights
    m.heights.resize(size_t(W) * H);
    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x) {
            int rr = int(fractal(p.seed, x, z, span));
            m.heights[size_t(z) * W + x] =
                uint8_t(rr < seaThresh ? kDeep : rr < t1 ? kL0 : rr < t2 ? kL1 : kL2);
        }
    // Turn the hard terrace steps into walkable ramps with a few integer box-blur
    // passes: a plateau interior (4*h + 4 equal neighbours)/8 == h stays flat; only
    // the boundaries slope. Edge cells clamp to themselves.
    auto hAt = [&](int x, int z) {
        x = std::clamp(x, 0, W - 1); z = std::clamp(z, 0, H - 1);
        return int(m.heights[size_t(z) * W + x]);
    };
    std::vector<uint8_t> tmp(m.heights.size());
    for (int pass = 0; pass < 6; ++pass) {
        for (int z = 0; z < H; ++z)
            for (int x = 0; x < W; ++x)
                tmp[size_t(z) * W + x] = uint8_t((4 * hAt(x, z) + hAt(x - 1, z) + hAt(x + 1, z)
                                                  + hAt(x, z - 1) + hAt(x, z + 1)) / 8);
        m.heights.swap(tmp);
    }

    // ---- terrain tiles: WALLPAPER the world's ground/sea section (col=bx%K,row=by%K).
    //      Shipped maps tile sections this way, so a field cycles through the
    //      section's sub-tiles instead of stamping one 32px tile. K comes from each
    //      section's VALID art region (see kWorldArt: most sea JPGs are only 5x5
    //      real art + black padding). ------------------------------------------------
    const WorldArt art = kWorldArt[p.mapType];
    const int kGround = art.groundK, kSea = art.seaK;
    size_t blocks = size_t(m.blocksX) * m.blocksY;
    m.tileKeys.resize(blocks);
    m.tileCols.resize(blocks);
    m.tileRows.resize(blocks);
    // Classify every block by the SHARED corner grid (cells 2bx,2by / +2), so
    // adjacent blocks agree on their common boundary and edge tiles line up:
    // 4-bit case, bit0 NW / bit1 NE / bit2 SW / bit3 SE below sea.
    std::vector<uint8_t> bcase(blocks);
    auto wetC = [&](int x, int z) { return hAt(x, z) < m.seaLevel; };
    for (int by = 0; by < m.blocksY; ++by)
        for (int bx = 0; bx < m.blocksX; ++bx) {
            int cx = bx * 2, cz = by * 2;
            bcase[size_t(by) * m.blocksX + bx] =
                uint8_t(wetC(cx, cz) | (wetC(cx + 2, cz) << 1) |
                        (wetC(cx, cz + 2) << 2) | (wetC(cx + 2, cz + 2) << 3));
        }
    auto caseAt = [&](int bx, int by) -> int {
        if (bx < 0 || by < 0 || bx >= m.blocksX || by >= m.blocksY) return -1;
        int c = bcase[size_t(by) * m.blocksX + bx];
        return (c == 6 || c == 9) ? 15 : c;   // diagonal pinches render as open water
    };
    const WorldShore& sh = kWorldShore[p.mapType];
    auto setT = [&](size_t i, uint32_t k, int c, int r) {
        m.tileKeys[i] = k; m.tileCols[i] = uint8_t(c); m.tileRows[i] = uint8_t(r);
    };
    for (int by = 0; by < m.blocksY; ++by)
        for (int bx = 0; bx < m.blocksX; ++bx) {
            size_t i = size_t(by) * m.blocksX + bx;
            switch (caseAt(bx, by)) {
            // straight shorelines: cycle the authored bank run along the coast
            case 3:  setT(i, sh.n.key, sh.n.run[bx % sh.n.runLen], sh.n.fixed); break;
            case 12: setT(i, sh.s.key, sh.s.run[bx % sh.s.runLen], sh.s.fixed); break;
            case 5:  setT(i, sh.w.key, sh.w.fixed, sh.w.run[by % sh.w.runLen]); break;
            case 10: setT(i, sh.e.key, sh.e.fixed, sh.e.run[by % sh.e.runLen]); break;
            // corners
            case 1:  setT(i, sh.c1.key, sh.c1.col, sh.c1.row); break;
            case 2:  setT(i, sh.c2.key, sh.c2.col, sh.c2.row); break;
            case 4:  setT(i, sh.c4.key, sh.c4.col, sh.c4.row); break;
            case 8:  setT(i, sh.c8.key, sh.c8.col, sh.c8.row); break;
            case 7:  setT(i, sh.c7.key, sh.c7.col, sh.c7.row); break;
            case 11: setT(i, sh.c11.key, sh.c11.col, sh.c11.row); break;
            case 13: setT(i, sh.c13.key, sh.c13.col, sh.c13.row); break;
            case 14: setT(i, sh.c14.key, sh.c14.col, sh.c14.row); break;
            case 0:
                // pure land: bank-approach ring when hugging a straight shoreline
                if (caseAt(bx, by - 1) == 3)
                    setT(i, sh.n.key, sh.n.run[bx % sh.n.runLen], sh.n.fixed + 1);
                else if (caseAt(bx, by + 1) == 12)
                    setT(i, sh.s.key, sh.s.run[bx % sh.s.runLen], sh.s.fixed - 1);
                else if (caseAt(bx - 1, by) == 5)
                    setT(i, sh.w.key, sh.w.fixed + 1, sh.w.run[by % sh.w.runLen]);
                else if (caseAt(bx + 1, by) == 10)
                    setT(i, sh.e.key, sh.e.fixed - 1, sh.e.run[by % sh.e.runLen]);
                else
                    setT(i, art.ground, bx % kGround, by % kGround);
                break;
            default:   // 15 (and 6/9): shallow ring beside a straight, else sea fill
                if (caseAt(bx, by + 1) == 3)
                    setT(i, sh.n.key, sh.n.run[bx % sh.n.runLen], sh.n.fixed - 1);
                else if (caseAt(bx, by - 1) == 12)
                    setT(i, sh.s.key, sh.s.run[bx % sh.s.runLen], sh.s.fixed + 1);
                else if (caseAt(bx + 1, by) == 5)
                    setT(i, sh.w.key, sh.w.fixed - 1, sh.w.run[by % sh.w.runLen]);
                else if (caseAt(bx - 1, by) == 10)
                    setT(i, sh.e.key, sh.e.fixed + 1, sh.e.run[by % sh.e.runLen]);
                else
                    setT(i, art.sea, bx % kSea, by % kSea);
                break;
            }
        }

    // No features yet (Phase 2 adds doodads + mana); minimap left empty (cosmetic).
    m.features.assign(size_t(W) * H, 0xFFFF);

    // ---- start positions: N spread around a ring, snapped to the nearest solid
    //      land, kept off the edges. Deterministic (integer compass table) -------
    auto isLand = [&](int cx, int cz) {
        if (cx < 0 || cz < 0 || cx >= W || cz >= H) return false;
        return int(m.heights[size_t(cz) * W + cx]) > m.seaLevel + 4;   // dry, not shallows
    };
    // Nearest land to (cx,cz) via an expanding square scan (bounded), else the point.
    auto snapLand = [&](int cx, int cz) -> std::pair<int, int> {
        for (int rad = 0; rad < std::max(W, H); ++rad) {
            for (int dz = -rad; dz <= rad; ++dz)
                for (int dx = -rad; dx <= rad; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dz)) != rad) continue;   // ring only
                    if (isLand(cx + dx, cz + dz)) return {cx + dx, cz + dz};
                }
        }
        return {cx, cz};
    };
    int ccx = W / 2, ccz = H / 2;
    int ringR = std::min(W, H) * 35 / 100;   // cells
    int margin = 8;
    for (int k = 0; k < int(p.players); ++k) {
        int ci = k * 8 / int(p.players);      // even-ish pick from the 8-way compass
        auto [dx, dz] = kCompass[size_t(ci)];
        int rx = std::clamp(ccx + dx * ringR / 1000, margin, W - 1 - margin);
        int rz = std::clamp(ccz + dz * ringR / 1000, margin, H - 1 - margin);
        r.starts.push_back(snapLand(rx, rz));
    }

    // ---- features ----------------------------------------------------------------
    // Mana deposits (each a glowing Sacred Stone centre ringed by Standing Stones)
    // and doodads (varied trees + rocks by density). Anchor cell holds the feature
    // index; a local claim-map reserves footprints so nothing overlaps. All draws
    // come from one integer PRNG in a fixed order => byte-identical on every peer.
    const WorldFeatures& wf = kWorldFeat[p.mapType];
    m.featureNames.clear();
    auto featIdx = [&](const char* nm) -> uint16_t {
        for (size_t i = 0; i < m.featureNames.size(); ++i)
            if (m.featureNames[i] == nm) return uint16_t(i);
        m.featureNames.emplace_back(nm);
        return uint16_t(m.featureNames.size() - 1);
    };
    auto nearStart = [&](int cx, int cz, int pad) {
        for (auto& [sx, sz] : r.starts) {
            int dx = cx - sx, dz = cz - sz;
            if (dx * dx + dz * dz < pad * pad) return true;
        }
        return false;
    };
    // Footprint reservation, separate from features[] (which stores only anchors):
    // fits() checks a nominal footprint is clear land; place() writes the anchor and
    // claims its footprint (centred on the anchor, matching the sim's nav blocking)
    // so later features avoid it.
    std::vector<uint8_t> claim(size_t(W) * H, 0);
    auto fits = [&](int cx, int cz, int fx, int fz) {
        for (int dz = 0; dz < fz; ++dz)
            for (int dx = 0; dx < fx; ++dx) {
                int nx = cx + dx - fx / 2, nz = cz + dz - fz / 2;
                if (!isLand(nx, nz) || claim[size_t(nz) * W + nx]) return false;
            }
        return true;
    };
    auto place = [&](int cx, int cz, const char* nm, int fx, int fz) {
        m.features[size_t(cz) * W + cx] = featIdx(nm);
        for (int dz = 0; dz < fz; ++dz)
            for (int dx = 0; dx < fx; ++dx) {
                int nx = cx + dx - fx / 2, nz = cz + dz - fz / 2;
                if (nx >= 0 && nz >= 0 && nx < W && nz < H) claim[size_t(nz) * W + nx] = 1;
            }
    };
    uint64_t frng = p.seed ^ 0x5eed1234abcdULL;

    // One mana deposit: a Sacred Stone centre (weak most common) ringed by 2..5
    // Standing Stones -- the "mana ruins" the shipped maps cluster around each spot.
    std::vector<std::pair<int, int>> depots;
    auto okDepot = [&](int cx, int cz, int minSp) {
        for (auto& [dx0, dz0] : depots) {
            int dx = cx - dx0, dz = cz - dz0;
            if (dx * dx + dz * dz < minSp * minSp) return false;
        }
        return true;
    };
    auto placeDeposit = [&](int cx, int cz) {
        uint32_t sr = uint32_t(splitmix(frng) % 100);
        int tier = sr < 60 ? 0 : (sr < 90 ? 1 : 2);   // 60% weak / 30% medium / 10% strong
        place(cx, cz, wf.sacred[size_t(tier)], 2, 2);
        depots.push_back({cx, cz});
        int nStones = 3 + int(splitmix(frng) % 3);    // 3..5 standing stones ring the spot
        for (int s = 0; s < nStones; ++s) {
            auto [ox, oz] = kCompass[splitmix(frng) % 8];
            int rad = 5 + int(splitmix(frng) % 4);     // 5..8 cells out from the centre
            int hx = cx + ox * rad / 1000, hz = cz + oz * rad / 1000;
            const char* hn = wf.henges.p[splitmix(frng) % uint64_t(wf.henges.n)];
            if (fits(hx, hz, 3, 3)) place(hx, hz, hn, 3, 3);
        }
    };
    // Try to drop a deposit within [rmin,rmax] cells of (tx,tz), on spaced land.
    auto tryDepositNear = [&](int tx, int tz, int rmin, int rmax) {
        for (int t = 0; t < 80; ++t) {
            int dx = int(splitmix(frng) % uint64_t(2 * rmax + 1)) - rmax;
            int dz = int(splitmix(frng) % uint64_t(2 * rmax + 1)) - rmax;
            int d2 = dx * dx + dz * dz;
            if (d2 < rmin * rmin || d2 > rmax * rmax) continue;
            int cx = tx + dx, cz = tz + dz;
            if (cx < 3 || cz < 3 || cx >= W - 3 || cz >= H - 3) continue;
            if (!isLand(cx, cz) || !fits(cx, cz, 2, 2) || !okDepot(cx, cz, 18)) continue;
            placeDeposit(cx, cz);
            return true;
        }
        return false;
    };

    // >=3 deposits near every start, then a density-scaled scatter across the map.
    for (auto& [sx, sz] : r.starts)
        for (int placed = 0, t = 0; placed < 3 && t < 12; ++t)
            if (tryDepositNear(sx, sz, 16, 44)) ++placed;
    int area = W * H;
    int scatter = std::clamp(area / std::max(1, 22000 - int(p.manaDensity) * 70), 0, 48);
    for (int placed = 0, tries = 0; placed < scatter && tries < scatter * 200 + 400; ++tries) {
        int cx = int(splitmix(frng) % uint64_t(W)), cz = int(splitmix(frng) % uint64_t(H));
        if (cx < 3 || cz < 3 || cx >= W - 3 || cz >= H - 3) continue;
        if (!isLand(cx, cz) || nearStart(cx, cz, 14) || !fits(cx, cz, 2, 2) || !okDepot(cx, cz, 22))
            continue;
        placeDeposit(cx, cz);
        ++placed;
    }

    // Doodads: per-land-cell probability from doodadDensity. ~1 in 7 is a rock
    // (smaller ones biased common via min-of-two draws); the rest are trees. Every
    // draw picks a fresh variant so no single tree/rock repeats across the map.
    int thresh = int(p.doodadDensity) * 5 / 4;   // out of 10000 (max ~3% of land cells)
    for (int cz = 0; cz < H; ++cz)
        for (int cx = 0; cx < W; ++cx) {
            uint64_t roll = splitmix(frng);   // one draw per cell (keeps order deterministic)
            if (!isLand(cx, cz) || claim[size_t(cz) * W + cx] || nearStart(cx, cz, 6)) continue;
            if (int(roll % 10000) >= thresh) continue;
            if ((roll >> 20) % 7 == 0) {      // rock (small-biased)
                int a = int((roll >> 24) % uint64_t(wf.rocks.n));
                int b = int((roll >> 33) % uint64_t(wf.rocks.n));
                if (fits(cx, cz, 3, 3)) place(cx, cz, wf.rocks.p[std::min(a, b)], 3, 3);
            } else {                          // tree (any variant)
                const char* nm = wf.trees.p[(roll >> 24) % uint64_t(wf.trees.n)];
                if (fits(cx, cz, 2, 2)) place(cx, cz, nm, 2, 2);
            }
        }
    return r;
}

// ---- mapId carrier: "~gen1~" + hex of the packed Params -----------------------

namespace {
constexpr const char* kMagic = "~gen1~";

void put16(std::string& b, uint16_t v) { b.push_back(char(v)); b.push_back(char(v >> 8)); }
void put64(std::string& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(char(v >> (8 * i))); }
std::string toHex(const std::string& raw) {
    static const char* H = "0123456789abcdef";
    std::string s; s.reserve(raw.size() * 2);
    for (unsigned char c : raw) { s.push_back(H[c >> 4]); s.push_back(H[c & 15]); }
    return s;
}
int hexNib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}  // namespace

bool isGeneratedMapId(const std::string& id) {
    return id.size() >= 6 && id.compare(0, 6, kMagic) == 0;
}

std::string encodeMapId(const Params& pin) {
    Params p = sanitize(pin);
    std::string b;
    put16(b, p.formatVer);
    put64(b, p.seed);
    b.push_back(char(p.mapType));
    put16(b, p.widthCells); put16(b, p.heightCells);
    b.push_back(char(p.players));
    b.push_back(char(p.doodadDensity));
    b.push_back(char(p.manaDensity));
    b.push_back(char(p.waterDensity));
    return std::string(kMagic) + toHex(b);
}

Params decodeMapId(const std::string& id) {
    Params p;
    if (!isGeneratedMapId(id)) return p;
    // De-hex the payload.
    std::string raw;
    for (size_t i = 6; i + 1 < id.size(); i += 2) {
        int hi = hexNib(id[i]), lo = hexNib(id[i + 1]);
        if (hi < 0 || lo < 0) return sanitize(p);
        raw.push_back(char((hi << 4) | lo));
    }
    auto u8 = [&](size_t o) -> uint8_t { return o < raw.size() ? uint8_t(raw[o]) : 0; };
    auto u16 = [&](size_t o) -> uint16_t { return uint16_t(u8(o) | (u8(o + 1) << 8)); };
    if (raw.size() >= 19) {
        p.formatVer = u16(0);
        uint64_t s = 0; for (int i = 0; i < 8; ++i) s |= uint64_t(u8(2 + i)) << (8 * i);
        p.seed = s;
        p.mapType = u8(10);
        p.widthCells = u16(11); p.heightCells = u16(13);
        p.players = u8(15);
        p.doodadDensity = u8(16);
        p.manaDensity = u8(17);
        p.waterDensity = u8(18);
    }
    return sanitize(p);
}

std::string friendlyLabel(const Params& pin) {
    Params p = sanitize(pin);
    static const char* kNames[kMapTypes] = {"Aramon", "Taros", "Veruna", "Zhon", "Creon"};
    int u = p.widthCells / 32, v = p.heightCells / 32;
    return "Random " + std::to_string(u) + "x" + std::to_string(v) + " \xC2\xB7 " +
           std::to_string(int(p.players)) + "P \xC2\xB7 " + kNames[p.mapType];
}

}  // namespace tak::mapgen
