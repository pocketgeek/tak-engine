#include "terrain/terrain.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace tak::terrain {

namespace {
constexpr int kBlock = 32;
}

Compositor::Compositor(const std::filesystem::path& terrainDir) : dir_(terrainDir) {
    // Index JPGs by their hex-key filename (case-insensitive).
    for (const auto& e : std::filesystem::directory_iterator(dir_)) {
        if (!e.is_regular_file()) continue;
        std::string stem = e.path().stem().string();
        if (stem.size() != 8) continue;
        char* end = nullptr;
        unsigned long key = std::strtoul(stem.c_str(), &end, 16);
        if (end != stem.c_str() + 8) continue;
        index_[uint32_t(key)] = e.path();
    }
    if (index_.empty())
        throw std::runtime_error("no terrain JPGs found in " + dir_.string());
}

const jpeg::Image& Compositor::section(uint32_t key) {
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    auto pi = index_.find(key);
    if (pi == index_.end()) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%08x", key);
        throw std::runtime_error(std::string("terrain JPG not found: ") + buf);
    }
    return cache_.emplace(key, jpeg::load(pi->second)).first->second;
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
