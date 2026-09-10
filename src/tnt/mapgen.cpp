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
// present in terrain.hpi. col=row=0 of each is a clean fill piece.
struct WorldArt { uint32_t ground, sea; };
constexpr std::array<WorldArt, kMapTypes> kWorldArt = {{
    {0x868a8222u, 0x0c2e64b2u},   // Aramon: grass / asea
    {0xca4200f8u, 0xb8524a38u},   // Taros:  low_ground / taros_sea
    {0xd3259f0fu, 0x9a5c5436u},   // Veruna: A512Lowland / dgsea
    {0x9bb50b09u, 0x59f53dfdu},   // Zhon:   jungletile80 / H2OTILE
    {0x868a8222u, 0x0c2e64b2u},   // Creon:  (fall back to Aramon art until IP tiles wired)
}};

// Per-world doodad + mana feature palette (names from features/<world>/*.tdf --
// trees/rocks are category=trees/rocks 1x1-ish reclaimable obstacles; the Henge is
// a category=mana deposit). The sim skips any name it can't resolve, so this is safe.
struct WorldFeatures {
    std::array<const char*, 3> trees;
    std::array<const char*, 3> rocks;
    const char* mana;
};
constexpr std::array<WorldFeatures, kMapTypes> kWorldFeat = {{
    {{"AraTree01", "AraTree02", "AraTree03"}, {"AraRock01", "AraRock02", "AraRock03"}, "AraHenge01"},
    {{"TarTree01", "TarTree02", "TarTree03"}, {"TarRock01", "TarRock02", "TarRock03"}, "TarHenge01"},
    {{"VerTree01", "VerTree02", "VerTree03"}, {"VerRock01", "VerRock02", "VerRock03"}, "VerHenge03"},
    {{"ZonTree01", "ZonTree02", "ZonTree03"}, {"ZonRock01", "ZonRock02", "ZonRock03"}, "ZonHenge01"},
    {{"AraTree01", "AraTree02", "AraTree03"}, {"AraRock01", "AraRock02", "AraRock03"}, "AraHenge01"},
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

    // Sea level from waterDensity: value noise clusters near 128, so a threshold near
    // that fraction submerges roughly that share of the map.
    m.seaLevel = std::clamp(40 + int(p.waterDensity) * 90 / 255, 40, 150);
    const int span = std::max(W, H);

    // ---- heightfield: smooth fractal noise (small neighbour deltas => no cliff
    //      occlusion; shallow fringe + deep centres give natural shorelines) -------
    m.heights.resize(size_t(W) * H);
    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x)
            m.heights[size_t(z) * W + x] = uint8_t(fractal(p.seed, x, z, span));

    // ---- terrain tiles: sea section where the block's centre cell is below the
    //      water line, else the world's ground section (col=row=0 clean fill) -------
    const WorldArt art = kWorldArt[p.mapType];
    size_t blocks = size_t(m.blocksX) * m.blocksY;
    m.tileKeys.resize(blocks);
    m.tileCols.assign(blocks, 0);
    m.tileRows.assign(blocks, 0);
    for (int by = 0; by < m.blocksY; ++by)
        for (int bx = 0; bx < m.blocksX; ++bx) {
            int cx = bx * 2, cz = by * 2;   // top-left cell of the 32px block
            int hgt = m.heights[size_t(cz) * W + cx];
            m.tileKeys[size_t(by) * m.blocksX + bx] = (hgt < m.seaLevel) ? art.sea : art.ground;
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

    // ---- features: mana deposits (spread on land, spaced) + doodads (trees/rocks by
    //      density). Anchor cell holds the feature index; footprints block nav. Same
    //      integer PRNG => byte-identical on every peer -------------------------------
    const WorldFeatures& wf = kWorldFeat[p.mapType];
    m.featureNames.clear();
    auto featIdx = [&](const char* nm) -> uint16_t {
        for (size_t i = 0; i < m.featureNames.size(); ++i)
            if (m.featureNames[i] == nm) return uint16_t(i);
        m.featureNames.emplace_back(nm);
        return uint16_t(m.featureNames.size() - 1);
    };
    auto occupied = [&](int cx, int cz) { return m.features[size_t(cz) * W + cx] != 0xFFFF; };
    auto nearStart = [&](int cx, int cz, int pad) {
        for (auto& [sx, sz] : r.starts) {
            int dx = cx - sx, dz = cz - sz;
            if (dx * dx + dz * dz < pad * pad) return true;
        }
        return false;
    };
    uint64_t frng = p.seed ^ 0x5eed1234abcdULL;

    // Mana: count scales with density + area; placed on land, off start pads, spaced.
    int area = W * H;
    int manaCount = std::clamp(int(p.players) + area / std::max(1, 16000 - int(p.manaDensity) * 45),
                               int(p.players), 64);
    const int minSpace = 24;   // cells between mana spots
    uint16_t manaFi = featIdx(wf.mana);
    for (int placed = 0, tries = 0; placed < manaCount && tries < manaCount * 300; ++tries) {
        int cx = int(splitmix(frng) % uint64_t(W));
        int cz = int(splitmix(frng) % uint64_t(H));
        if (!isLand(cx, cz) || occupied(cx, cz) || nearStart(cx, cz, 6)) continue;
        bool ok = true;
        for (int dz = -minSpace; dz <= minSpace && ok; ++dz)
            for (int dx = -minSpace; dx <= minSpace && ok; ++dx) {
                int nx = cx + dx, nz = cz + dz;
                if (nx >= 0 && nz >= 0 && nx < W && nz < H &&
                    m.features[size_t(nz) * W + nx] == manaFi) ok = false;
            }
        if (!ok) continue;
        m.features[size_t(cz) * W + cx] = manaFi;
        ++placed;
    }

    // Doodads: per-land-cell probability from doodadDensity (trees/rocks mixed).
    int thresh = int(p.doodadDensity) * 5 / 4;   // out of 10000 (max ~3% of land cells)
    for (int cz = 0; cz < H; ++cz)
        for (int cx = 0; cx < W; ++cx) {
            uint64_t roll = splitmix(frng);   // one draw per cell (keeps order deterministic)
            if (!isLand(cx, cz) || occupied(cx, cz) || nearStart(cx, cz, 5)) continue;
            if (int(roll % 10000) >= thresh) continue;
            bool rock = (roll >> 20) & 1;
            const char* nm = rock ? wf.rocks[(roll >> 24) % 3] : wf.trees[(roll >> 24) % 3];
            m.features[size_t(cz) * W + cx] = featIdx(nm);
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
