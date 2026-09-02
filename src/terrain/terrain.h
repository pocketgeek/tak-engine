#pragma once

#include "tnt/tnt.h"
#include "util/jpeg.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>

namespace tak::terrain {

// Composites TNT map terrain from the content-addressed section JPGs in
// an extracted terrain directory (terrain/<hexkey>.jpg). Each 32px map
// block crops (col*32, row*32, 32, 32) out of its keyed JPG.

class Compositor {
public:
    explicit Compositor(const std::filesystem::path& terrainDir);

    // Render the whole map at full resolution (width*16 x height*16 px).
    jpeg::Image renderMap(const tnt::Map& map);

    // Render one 32px block into `dst` (RGBA, dstW px wide) at (dx, dy).
    void renderBlock(const tnt::Map& map, int bx, int by,
                     std::vector<uint8_t>& dst, int dstW, int dx, int dy);

private:
    const jpeg::Image& section(uint32_t key);

    std::filesystem::path dir_;
    std::map<uint32_t, jpeg::Image> cache_;
    std::map<uint32_t, std::filesystem::path> index_;  // key -> jpg path
};

} // namespace tak::terrain
