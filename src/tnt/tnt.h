#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tak::tnt {

// TAK TNT map format (version 0x4000), reverse-engineered from the GOG data.
// A map is W x H cells of 16px. Terrain graphics come from *content-addressed*
// JPG section images in terrain.hpi (terrain/<hexkey>.jpg): the map carries,
// per 32px block (W/2 x H/2 grid), a u32 JPG key plus column/row bytes
// selecting the 32px piece of that JPG.
//
// Header (u32 little-endian):
//   0: version (0x4000)      1: width in cells      2: height in cells
//   3: unknown               4: -> heights (1 B/cell)
//   5: -> features (u16/cell, 0xFFFF = none)   6,7: unknown
//   8: -> jpg keys (u32/block)   9: -> columns (1 B/block)
//  10: -> rows (1 B/block)      11: -> minimap {u32 w, u32 h, w*h bytes}
//  12: -> overview image {u32 w, u32 h, w*h bytes}

struct Map {
    int width = 0, height = 0;       // in 16px cells
    int seaLevel = 0;                // heights below this are water
    int blocksX = 0, blocksY = 0;    // in 32px blocks (width/2, height/2)

    std::vector<uint8_t> heights;    // width*height
    std::vector<uint16_t> features;  // width*height
    std::vector<uint32_t> tileKeys;  // blocksX*blocksY
    std::vector<uint8_t> tileCols;   // blocksX*blocksY
    std::vector<uint8_t> tileRows;   // blocksX*blocksY

    int minimapW = 0, minimapH = 0;
    std::vector<uint8_t> minimap;    // 8-bit indexed
    std::vector<std::string> featureNames;   // indexed by feature-layer values

    static Map load(const std::filesystem::path& file);
    // Parse from an in-memory buffer (a VFS-resolved archive entry). `origin`
    // names the source in error messages.
    static Map load(const std::vector<uint8_t>& d, const std::string& origin = "<memory>");

    // "deadbeef" style lowercase hex name for a block's terrain JPG.
    std::string tileKeyHex(int bx, int by) const;
};

} // namespace tak::tnt
