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
    size_t b = size_t(by) * map.blocksX + bx;
    const jpeg::Image& img = section(map.tileKeys[b]);
    int sx = (map.tileCols[b] * kBlock) % std::max(img.width, 1);
    int sy = (map.tileRows[b] * kBlock) % std::max(img.height, 1);
    for (int y = 0; y < kBlock; ++y) {
        if (sy + y >= img.height) break;
        const uint8_t* srow = &img.rgba[(size_t(sy + y) * img.width + sx) * 4];
        uint8_t* drow = &dst[(size_t(dy + y) * dstW + dx) * 4];
        std::memcpy(drow, srow, size_t(std::min(kBlock, img.width - sx)) * 4);
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
