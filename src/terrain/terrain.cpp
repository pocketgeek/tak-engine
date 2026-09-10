#include "terrain/terrain.h"

#include "hpi/hpi.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace tak::terrain {

namespace {
constexpr int kBlock = 32;
}

Compositor::Compositor(const hpi::Vfs& vfs) : vfs_(&vfs) {}

const jpeg::Image& Compositor::section(uint32_t key) {
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    // Terrain tiles are content-addressed by the map's u32 tile key: terrain/<8hex>.jpg.
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08x", key);
    std::string path = std::string("terrain/") + buf + ".jpg";
    if (!vfs_->has(path))
        throw std::runtime_error(std::string("terrain JPG not found: ") + buf);
    return cache_.emplace(key, jpeg::load(vfs_->read(path))).first->second;
}

void Compositor::renderBlock(const tnt::Map& map, int bx, int by,
                             std::vector<uint8_t>& dst, int dstW, int dx, int dy) {
    // One coarse lock for lookup+decode+copy: `section` returns a reference into
    // cache_, so the copy below must not race a rehash from another thread. The
    // JPEG decode dominates the hold time and only ever runs once per tile.
    std::lock_guard<std::mutex> lk(mu_);
    size_t b = size_t(by) * map.blocksX + bx;
    const jpeg::Image& img = section(map.tileKeys[b]);
    int sx = (map.tileCols[b] * kBlock) % std::max(img.width, 1);
    int sy = (map.tileRows[b] * kBlock) % std::max(img.height, 1);
    // The per-world "sea" sections are near-black placeholders (retail draws an
    // animated water surface over them, which we don't). Tint any below-sea cell a
    // blue depth-gradient so water reads as water instead of black squares -- keyed
    // on the map heightfield, not the tile, so it fixes both generated maps and
    // shipped ocean maps. Display only (the terrain texture is never hashed).
    const int sea = map.seaLevel;
    const bool haveH = map.width > 0 && map.heights.size() >= size_t(map.width) * map.height;
    for (int y = 0; y < kBlock; ++y) {
        if (sy + y >= img.height) break;
        const uint8_t* srow = &img.rgba[(size_t(sy + y) * img.width + sx) * 4];
        uint8_t* drow = &dst[(size_t(dy + y) * dstW + dx) * 4];
        int copyW = std::min(kBlock, img.width - sx);
        std::memcpy(drow, srow, size_t(copyW) * 4);
        if (!haveH) continue;
        int cz = by * 2 + (y >> 4);                 // 32px block spans 2 cells (16px each)
        if (cz >= map.height) continue;
        for (int x = 0; x < copyW; ++x) {
            int cx = bx * 2 + (x >> 4);
            if (cx >= map.width) break;
            int h = map.heights[size_t(cz) * map.width + cx];
            if (h >= sea) continue;                 // land
            int depth = std::clamp(sea - h, 0, 70);
            int wr = 58 - depth * 38 / 70, wg = 120 - depth * 70 / 70, wb = 165 - depth * 65 / 70;
            uint8_t* p = drow + x * 4;               // blend 78% water + 22% tile (faint ripple)
            p[0] = uint8_t((wr * 78 + p[0] * 22) / 100);
            p[1] = uint8_t((wg * 78 + p[1] * 22) / 100);
            p[2] = uint8_t((wb * 78 + p[2] * 22) / 100);
        }
    }
}

jpeg::Image Compositor::renderMap(const tnt::Map& map) {
    jpeg::Image out;
    out.width = map.blocksX * kBlock;
    out.height = map.blocksY * kBlock;
    out.rgba.assign(size_t(out.width) * out.height * 4, 0);
    for (int by = 0; by < map.blocksY; ++by)
        for (int bx = 0; bx < map.blocksX; ++bx)
            renderBlock(map, bx, by, out.rgba, out.width, bx * kBlock, by * kBlock);
    return out;
}

} // namespace tak::terrain
