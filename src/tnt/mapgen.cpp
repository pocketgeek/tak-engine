#include "tnt/mapgen.h"
#include "tnt/mapgen_internal.h"

#include "hpi/hpi.h"   // coast prefab sections are read through the VFS

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <string>

namespace tak::mapgen {

using namespace detail;

Result generateBalanced(const Params& p, const hpi::Vfs& vfs);

std::vector<std::string> assetPaths(const hpi::Vfs& vfs) {
    std::vector<std::string> paths;
    for (const auto& kit : kShoreKit)
        for (const auto* name : kit.name)
            for (const auto* variant : kit.var) {
                std::string path = std::string(kit.dir) + name + variant + kit.ext;
                if (vfs.has(path)) paths.push_back(std::move(path));
            }
    for (const auto* prefix : kReliefDir)
        for (const auto& path : vfs.list(prefix))
            if (hpi::MountSet::key(path).ends_with(".tnt")) paths.push_back(path);
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

Params sanitize(Params p) {
    if (p.mapType >= kMapTypes) p.mapType = Aramon;
    // Section multiples: new recipes support 64x64; legacy seeds keep their cap.
    auto fix = [p](uint16_t v) -> uint16_t {
        int u = std::clamp(int(v) / 32, 4, p.formatVer >= 4 ? 64 : 24);
        return uint16_t(u * 32);
    };
    p.widthCells = fix(p.widthCells);
    p.heightCells = fix(p.heightCells);
    p.players = uint8_t(std::clamp<int>(p.players, 2, 8));
    if (p.formatVer >= 3) {
        p.layout = std::min<uint8_t>(p.layout, Islands);
        const int minimum = p.layout == Lakes ? (p.players > 4 ? 512 : 384) : (p.players > 4 ? 384 : 256);
        int minW = minimum, minH = minimum;
        if (p.layout == Islands) {
            const int cols = p.players <= 4 ? 2 : 3;
            const int rows = (p.players + cols - 1) / cols;
            minW = std::max(minW, cols * 256);
            minH = std::max(minH, rows * 256);
        }
        p.widthCells = uint16_t(std::max<int>(p.widthCells, minW));
        p.heightCells = uint16_t(std::max<int>(p.heightCells, minH));
    }
    return p;
}

Result generate(const Params& raw, const tak::hpi::Vfs& input) {
    tak::hpi::Vfs vfs(&input, true);
    Params p = sanitize(raw);
    if (p.formatVer >= 3) return generateBalanced(p, vfs);
    Result r;
    tak::tnt::Map& m = r.map;
    const int W = p.widthCells, H = p.heightCells;
    m.width = W; m.height = H;
    m.blocksX = W / 2; m.blocksY = H / 2;
    const int SW = W / 32, SH = H / 32;   // map size in 512px section units

    // ---- coarse land/water mask on the SECTION-CORNER grid ------------------------
    // The coastline is decided at 512px granularity so whole coast prefabs can be
    // stamped -- this is what gives retail's sweeping curves instead of a 32px
    // block staircase. Adjacent sections share corners, so the marching cases mesh.
    const WorldLevels lv = kLevels[p.mapType];
    m.seaLevel = lv.sea;
    const int land = lv.land;
    const int span = std::max(W, H);
    // waterDensity picks how much of the fractal range falls below the shoreline.
    const int seaThresh = std::clamp(40 + int(p.waterDensity) * 85 / 255, 30, 180);
    std::vector<uint8_t> wet(size_t(SW + 1) * (SH + 1));
    for (int j = 0; j <= SH; ++j)
        for (int i = 0; i <= SW; ++i)
            wet[size_t(j) * (SW + 1) + i] =
                uint8_t(int(fractal(p.seed, i * 32, j * 32, span)) < seaThresh);
    auto wetAt = [&](int i, int j) {
        i = std::clamp(i, 0, SW); j = std::clamp(j, 0, SH);
        return int(wet[size_t(j) * (SW + 1) + i]);
    };
    auto scase = [&](int sx, int sy) {
        return wetAt(sx, sy) | (wetAt(sx + 1, sy) << 1) |
               (wetAt(sx, sy + 1) << 2) | (wetAt(sx + 1, sy + 1) << 3);
    };
    // No prefab depicts a diagonal pinch (cases 6/9 -- retail never authors them):
    // dry the offending corner until the mask is clean. Deterministic scan order.
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (int sy = 0; sy < SH; ++sy)
            for (int sx = 0; sx < SW; ++sx) {
                int c = scase(sx, sy);
                if (c == 6) { wet[size_t(sy) * (SW + 1) + sx + 1] = 0; changed = true; }
                if (c == 9) { wet[size_t(sy) * (SW + 1) + sx] = 0; changed = true; }
            }
        if (!changed) break;
    }

    // ---- heights -------------------------------------------------------------------
    // Interior land sections get the procedural terraces (base plateau = the
    // world's authored flat level, so it meets the prefab land edges exactly);
    // water sections sit at 0 (retail deep-water floor); shoreline sections get
    // the prefab's authored bank heights when stamped below.
    const int landRange = std::max(1, 255 - seaThresh);
    // reliefDensity scales how much of the interior rises into plateaus: 0 = dead
    // flat, 128 ~= the previous default (~40% of land raised), 255 = mostly mesa.
    const int raised = int(p.reliefDensity) * 79 / 255;            // % of land raised
    const int t1 = seaThresh + landRange * (100 - raised) / 100;   // base plateau
    const int t2 = t1 + (255 - t1) * 5 / 8;                        // mid vs high split
    const int kL1 = std::min(250, land + 42), kL2 = std::min(250, land + 92);
    // Raised terraces only where the whole 3x3 section neighbourhood is land, so
    // the blur can never bleed a hill into a stamped prefab's edge rows.
    std::vector<uint8_t> interior(size_t(SW) * SH, 0);
    for (int sy = 0; sy < SH; ++sy)
        for (int sx = 0; sx < SW; ++sx) {
            bool ok = true;
            for (int dy = -1; dy <= 1 && ok; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (scase(std::clamp(sx + dx, 0, SW - 1),
                              std::clamp(sy + dy, 0, SH - 1)) != 0) { ok = false; break; }
            interior[size_t(sy) * SW + sx] = uint8_t(ok);
        }
    m.heights.resize(size_t(W) * H);
    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x) {
            int c = scase(x >> 5, z >> 5);
            uint8_t h;
            if (c == 15) h = 0;
            else if (c != 0 || !interior[size_t(z >> 5) * SW + (x >> 5)]) h = uint8_t(land);
            else {
                int rr = int(fractal(p.seed, x, z, span));
                h = uint8_t(rr < t1 ? land : rr < t2 ? kL1 : kL2);
            }
            m.heights[size_t(z) * W + x] = h;
        }
    // Box-blur the terrace steps into walkable ramps (interior only by
    // construction; shoreline sections are overwritten by the prefab stamp).
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

    // ---- terrain tiles: fills, then whole coast prefabs -----------------------------
    // Land/water sections WALLPAPER the world's ground/sea fill (col=bx%K,row=by%K;
    // K = the fill's VALID art region, see kWorldArt). Shoreline sections are then
    // stamped whole from the world's Coast Sandy prefab kit: tiles, the authored
    // bank HEIGHTS (overwriting the placeholder -- this is what puts the sim
    // waterline exactly on the painted shoreline), and any beach features the
    // prefab carries.
    const WorldArt art = kWorldArt[p.mapType];
    const int kGround = art.groundK, kSea = art.seaK;
    size_t blocks = size_t(m.blocksX) * m.blocksY;
    m.tileKeys.resize(blocks);
    m.tileCols.resize(blocks);
    m.tileRows.resize(blocks);
    for (int by = 0; by < m.blocksY; ++by)
        for (int bx = 0; bx < m.blocksX; ++bx) {
            size_t i = size_t(by) * m.blocksX + bx;
            int c = scase(bx >> 4, by >> 4);
            bool water = (c == 15);   // shore blocks get stamped; fill land-ish meanwhile
            m.tileKeys[i] = water ? art.sea : art.ground;
            int K = water ? kSea : kGround;
            m.tileCols[i] = uint8_t(bx % K);
            m.tileRows[i] = uint8_t(by % K);
        }
    m.features.assign(size_t(W) * H, 0xFFFF);

    // Prefab loader: cached per path, tolerant of a missing variant (falls back to
    // the next; a fully missing piece leaves the fill -- same files on every peer,
    // so the fallback is byte-identical too).
    const ShoreKit& kit = kShoreKit[p.mapType];
    std::map<std::string, std::unique_ptr<tak::tnt::Map>> pieceCache;
    auto loadPiece = [&](int role, uint64_t vh) -> const tak::tnt::Map* {
        for (int attempt = 0; attempt < 3; ++attempt) {
            int v = int((vh + uint64_t(attempt)) % 3);
            std::string path = std::string(kit.dir) + kit.name[role] + kit.var[v] + kit.ext;
            auto it = pieceCache.find(path);
            if (it == pieceCache.end()) {
                std::unique_ptr<tak::tnt::Map> pm;
                try {
                    auto d = vfs.read(path);
                    auto loaded = tak::tnt::Map::load(d, path);
                    if (loaded.width == 32 && loaded.height == 32)
                        pm = std::make_unique<tak::tnt::Map>(std::move(loaded));
                } catch (const std::exception&) {}
                it = pieceCache.emplace(std::move(path), std::move(pm)).first;
            }
            if (it->second) return it->second.get();
        }
        return nullptr;
    };
    struct PendingFeat { int cx, cz; std::string name; };
    std::vector<PendingFeat> prefabFeats;
    for (int sy = 0; sy < SH; ++sy)
        for (int sx = 0; sx < SW; ++sx) {
            int role = kCaseRole[scase(sx, sy)];
            if (role < 0) continue;
            uint64_t vh = p.seed ^ (uint64_t(sy) * 1000003u + uint64_t(sx) * 7919u);
            const tak::tnt::Map* pc = loadPiece(role, splitmix(vh));   // vh is advanced in place
            if (!pc) continue;
            // heights + features: 32x32 cells at (sx*32, sy*32)
            for (int z = 0; z < 32; ++z)
                for (int x = 0; x < 32; ++x) {
                    int cx = sx * 32 + x, cz = sy * 32 + z;
                    m.heights[size_t(cz) * W + cx] = pc->heights[size_t(z) * 32 + x];
                    uint16_t fi = pc->features[size_t(z) * 32 + x];
                    if (fi != 0xFFFF && fi < pc->featureNames.size())
                        prefabFeats.push_back({cx, cz, pc->featureNames[fi]});
                }
            // tiles: 16x16 blocks at (sx*16, sy*16)
            for (int bz = 0; bz < 16; ++bz)
                for (int bxl = 0; bxl < 16; ++bxl) {
                    size_t bi = size_t(sy * 16 + bz) * m.blocksX + (sx * 16 + bxl);
                    size_t pi = size_t(bz) * 16 + bxl;
                    m.tileKeys[bi] = pc->tileKeys[pi];
                    m.tileCols[bi] = pc->tileCols[pi];
                    m.tileRows[bi] = pc->tileRows[pi];
                }
        }

    // ---- start positions: N spread around a ring, snapped to the nearest solid
    //      land, kept off the edges. Deterministic (integer compass table) -------
    auto isLand = [&](int cx, int cz) {
        if (cx < 0 || cz < 0 || cx >= W || cz >= H) return false;
        // Dry (not shallows) AND on an interior land section -- keeps starts,
        // mana and doodads off the stamped prefab beaches.
        return int(m.heights[size_t(cz) * W + cx]) > m.seaLevel + 3 &&
               scase(cx >> 5, cz >> 5) == 0;
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
    auto featIdx = [&](const std::string& nm) -> uint16_t {
        for (size_t i = 0; i < m.featureNames.size(); ++i)
            if (m.featureNames[i] == nm) return uint16_t(i);
        m.featureNames.emplace_back(nm);
        return uint16_t(m.featureNames.size() - 1);
    };
    // The stamped coast prefabs' own features (beach rocks etc.) go in first, with
    // their names remapped into this map's table; their cells are claimed below so
    // the procedural scatter avoids them.
    for (const auto& pf : prefabFeats)
        m.features[size_t(pf.cz) * W + pf.cx] = featIdx(pf.name);
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
    for (const auto& pf : prefabFeats)   // prefab features block the scatter
        claim[size_t(pf.cz) * W + pf.cx] = 1;
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
        // Sacred stone tier at the shipped 01:02:03 mix (223:446:537 of 1206).
        uint32_t sr = uint32_t(splitmix(frng) % 1206);
        int tier = sr < 223 ? 0 : (sr < 669 ? 1 : 2);
        place(cx, cz, wf.sacred[size_t(tier)], 2, 2);
        depots.push_back({cx, cz});
        // Ring the stone the retail way (see kRing): draw the ring size from the
        // world's shipped distribution, walk the slot-preference order (skipping
        // a slot ~12% of the time for variety when spares remain), and place each
        // slot's arc-segment variant at its canonical offset + jitter. A henge
        // whose spot doesn't fit is simply omitted, as retail maps do.
        const RingSpec& rs = kRing[p.mapType];
        int roll = int(splitmix(frng) % 100), want = 5;
        for (int k = 0; k < 6; ++k) if (roll < rs.cnt[k]) { want = k; break; }
        uint64_t skipBits = splitmix(frng);
        int placedH = 0, remaining = 0;
        bool slotHas[8] = {};
        for (int i = 0; i < rs.n; ++i) slotHas[rs.p[i].slot] = true;
        for (int oi = 0; oi < 8; ++oi) if (slotHas[rs.order[oi]]) ++remaining;
        for (int oi = 0; oi < 8 && placedH < want; ++oi) {
            int slot = rs.order[oi];
            if (!slotHas[slot]) continue;
            --remaining;
            if (remaining >= want - placedH && ((skipBits >> oi) & 7) == 0)
                continue;   // variety: occasionally pass over a canonical slot
            int total = 0;
            for (int i = 0; i < rs.n; ++i) if (rs.p[i].slot == slot) total += rs.p[i].w;
            int pickW = int(splitmix(frng) % uint64_t(total));
            const RingPiece* pc = nullptr;
            for (int i = 0; i < rs.n; ++i)
                if (rs.p[i].slot == slot) { if ((pickW -= rs.p[i].w) < 0) { pc = &rs.p[i]; break; } }
            int jr = rs.jitter;
            int hx = cx + pc->dx + int(splitmix(frng) % uint64_t(2 * jr + 1)) - jr;
            int hz = cz + pc->dz + int(splitmix(frng) % uint64_t(2 * jr + 1)) - jr;
            ++placedH;   // the slot is consumed even if the piece doesn't fit
            if (fits(hx, hz, 3, 3)) place(hx, hz, pc->name, 3, 3);
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

    // Doodads: independent per-cell probabilities per TYPE (each has its own
    // slider). Every placement picks a fresh variant so nothing repeats; rocks are
    // small-biased via min-of-two draws. One PRNG draw per cell, fixed order =>
    // byte-identical on every peer.
    const int treeThresh = int(p.treeDensity) * 5 / 4;   // /10000 (max ~3% of land)
    const int rockThresh = int(p.rockDensity) * 5 / 8;   // /10000 (max ~1.6%)
    for (int cz = 0; cz < H; ++cz)
        for (int cx = 0; cx < W; ++cx) {
            uint64_t roll = splitmix(frng);   // one draw per cell (keeps order deterministic)
            if (!isLand(cx, cz) || claim[size_t(cz) * W + cx] || nearStart(cx, cz, 6)) continue;
            if (int(roll % 10000) < treeThresh) {          // tree (any variant)
                const char* nm = wf.trees.p[(roll >> 24) % uint64_t(wf.trees.n)];
                if (fits(cx, cz, 2, 2)) place(cx, cz, nm, 2, 2);
            } else if (int((roll >> 13) % 10000) < rockThresh) {   // rock (small-biased)
                int a = int((roll >> 24) % uint64_t(wf.rocks.n));
                int b = int((roll >> 33) % uint64_t(wf.rocks.n));
                if (fits(cx, cz, 3, 3)) place(cx, cz, wf.rocks.p[std::min(a, b)], 3, 3);
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
    b.push_back(char(p.treeDensity));
    b.push_back(char(p.rockDensity));
    b.push_back(char(p.manaDensity));
    b.push_back(char(p.waterDensity));
    b.push_back(char(p.reliefDensity));
    if (p.formatVer >= 3) b.push_back(char(p.layout));
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
        if (p.formatVer > 4 || (p.formatVer >= 3 && raw.size() != 22)) return Params{};
        if (p.formatVer >= 3) p.layout = u8(21);
        uint64_t s = 0; for (int i = 0; i < 8; ++i) s |= uint64_t(u8(2 + i)) << (8 * i);
        p.seed = s;
        p.mapType = u8(10);
        p.widthCells = u16(11); p.heightCells = u16(13);
        p.players = u8(15);
        if (p.formatVer >= 2 && raw.size() >= 21) {
            p.treeDensity = u8(16);
            p.rockDensity = u8(17);
            p.manaDensity = u8(18);
            p.waterDensity = u8(19);
            p.reliefDensity = u8(20);
        } else {   // v1 ids: one doodad slider drove both, no relief control
            p.treeDensity = u8(16);
            p.rockDensity = uint8_t(std::min(255, int(u8(16)) * 3 / 4));
            p.manaDensity = u8(17);
            p.waterDensity = u8(18);
            p.reliefDensity = 128;
        }
    }
    return sanitize(p);
}

std::string friendlyLabel(const Params& pin) {
    Params p = sanitize(pin);
    static const char* kNames[kMapTypes] = {"Aramon", "Taros", "Veruna", "Zhon", "Creon"};
    int u = p.widthCells / 32, v = p.heightCells / 32;
    // ASCII only: the lobby draws this with the 5x7 block font, which has no glyph
    // for a middot and would render each byte of one as a blank.
    return "Random " + std::to_string(u) + "x" + std::to_string(v) + " " +
           std::to_string(int(p.players)) + "P " + kNames[p.mapType];
}

}  // namespace tak::mapgen
