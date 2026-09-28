#pragma once

#include "tnt/tnt.h"
#include "util/jpeg.h"

#include <cstdint>
#include <map>
#include <mutex>
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

    // Caller must first stop all consumers holding section references.
    void clear() { std::lock_guard<std::mutex> lk(mu_); cache_.clear(); }

    // Render the whole map at full resolution (width*16 x height*16 px).
    jpeg::Image renderMap(const tnt::Map& map);

    // Render one 32px block into `dst` (RGBA, dstW px wide) at (dx, dy).
    // Thread-safe: the decoded-tile cache is guarded, so background chunk/minimap
    // builders and the main thread may composite concurrently.
    void renderBlock(const tnt::Map& map, int bx, int by,
                     std::vector<uint8_t>& dst, int dstW, int dx, int dy);

    // Decode (or fetch the cached) section JPG for `key` and return it. The
    // returned reference stays valid until clear() or destruction (cache_ is a
    // std::map -- node-stable across inserts). Thread-safe. Throws if absent.
    // Used by the tile-atlas terrain renderer to upload each section once.
    const jpeg::Image& sectionImage(uint32_t key, bool stockTerrain = false) {
        std::lock_guard<std::mutex> lk(mu_);
        return section(key, stockTerrain);
    }

private:
    const jpeg::Image& section(uint32_t key, bool stockTerrain);

    const hpi::Vfs* vfs_ = nullptr;
    std::mutex mu_;                           // guards cache_ (see renderBlock)
    std::map<std::pair<uint32_t,bool>, jpeg::Image> cache_;   // key -> decoded section (lazy)
};

} // namespace tak::terrain
