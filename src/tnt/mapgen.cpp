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
    {0x868a8222u, 0x0c2e64b2u, 16, 5},    // Creon:  (fall back to Aramon art until IP tiles wired)
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

constexpr std::array<WorldFeatures, kMapTypes> kWorldFeat = {{
    {{kAraTree, 10}, {kAraRock, 7}, {kAraHenge, 9},  {{"AraMana01", "AraMana02", "AraMana03"}}},
    {{kTarTree, 9},  {kTarRock, 7}, {kTarHenge, 14}, {{"TarMana01", "TarMana02", "TarMana03"}}},
    {{kVerTree, 9},  {kVerRock, 7}, {kVerHenge, 11}, {{"VerMana01", "VerMana02", "VerMana03"}}},
    {{kZonTree, 6},  {kZonRock, 7}, {kZonHenge, 11}, {{"ZonMana01", "ZonMana02", "ZonMana03"}}},
    {{kAraTree, 10}, {kAraRock, 7}, {kAraHenge, 9},  {{"AraMana01", "AraMana02", "AraMana03"}}},  // Creon->Aramon
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
    for (int by = 0; by < m.blocksY; ++by)
        for (int bx = 0; bx < m.blocksX; ++bx) {
            int cx = bx * 2, cz = by * 2;   // 2x2 cells under the 32px block
            // Majority rule: a block that is mostly underwater gets the sea tile,
            // so the drawn waterline rounds to the nearest block edge. (Shipped
            // maps refine this with hand-painted shore-transition sections --
            // possible follow-up via the sections.hpi berm pieces.)
            int below = (hAt(cx, cz) < m.seaLevel) + (hAt(cx + 1, cz) < m.seaLevel) +
                        (hAt(cx, cz + 1) < m.seaLevel) + (hAt(cx + 1, cz + 1) < m.seaLevel);
            bool water = below >= 2;
            size_t i = size_t(by) * m.blocksX + bx;
            m.tileKeys[i] = water ? art.sea : art.ground;
            int K = water ? kSea : kGround;
            m.tileCols[i] = uint8_t(bx % K);
            m.tileRows[i] = uint8_t(by % K);
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
