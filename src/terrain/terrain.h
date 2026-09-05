#pragma once

#include "tnt/tnt.h"
#include "util/jpeg.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tak::hpi { class Vfs; }

namespace tak::terrain {

// Composites TNT map terrain from the content-addressed section JPGs the VFS
// resolves as terrain/<hexkey>.jpg (from terrain.hpi). Each 32px map block
// crops (col*32, row*32, 32, 32) out of its keyed JPG.

class Compositor {
public:
    explicit Compositor(const hpi::Vfs& vfs);

    // Render the whole map at full resolution (width*16 x height*16 px).
    jpeg::Image renderMap(const tnt::Map& map);

    // Render one 32px block into `dst` (RGBA, dstW px wide) at (dx, dy).
    void renderBlock(const tnt::Map& map, int bx, int by,
                     std::vector<uint8_t>& dst, int dstW, int dx, int dy);

private:
    const jpeg::Image& section(uint32_t key);

    const hpi::Vfs* vfs_ = nullptr;
    std::map<uint32_t, jpeg::Image> cache_;   // key -> decoded section (lazy)
};

} // namespace tak::terrain
