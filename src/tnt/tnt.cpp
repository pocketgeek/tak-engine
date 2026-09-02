#include "tnt/tnt.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace tak::tnt {

namespace {

uint32_t u32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
}

void need(const std::vector<uint8_t>& d, uint64_t off, uint64_t n, const char* what) {
    if (off + n > d.size()) throw std::runtime_error(std::string(what) + " out of range");
}

} // namespace

Map Map::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + file.string());
    std::vector<uint8_t> d(std::filesystem::file_size(file));
    in.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(d.size()));

    need(d, 0, 52, "TNT header");
    if (u32(&d[0]) != 0x4000)
        throw std::runtime_error(file.string() + ": not a TAK TNT (version != 0x4000)");

    Map m;
    m.width = int(u32(&d[4]));
    m.height = int(u32(&d[8]));
    m.seaLevel = int(u32(&d[12]));
    m.blocksX = m.width / 2;
    m.blocksY = m.height / 2;

    uint32_t pHeights = u32(&d[16]);
    uint32_t pFeatures = u32(&d[20]);
    uint32_t pKeys = u32(&d[32]);
    uint32_t pCols = u32(&d[36]);
    uint32_t pRows = u32(&d[40]);
    uint32_t pMinimap = u32(&d[44]);

    size_t cells = size_t(m.width) * m.height;
    size_t blocks = size_t(m.blocksX) * m.blocksY;

    need(d, pHeights, cells, "heights");
    m.heights.assign(d.begin() + pHeights, d.begin() + pHeights + cells);

    need(d, pFeatures, cells * 2, "features");
    m.features.resize(cells);
    for (size_t i = 0; i < cells; ++i)
        m.features[i] = uint16_t(d[pFeatures + i * 2] | (d[pFeatures + i * 2 + 1] << 8));

    need(d, pKeys, blocks * 4, "tile keys");
    m.tileKeys.resize(blocks);
    for (size_t i = 0; i < blocks; ++i) m.tileKeys[i] = u32(&d[pKeys + i * 4]);

    need(d, pCols, blocks, "tile columns");
    m.tileCols.assign(d.begin() + pCols, d.begin() + pCols + blocks);
    need(d, pRows, blocks, "tile rows");
    m.tileRows.assign(d.begin() + pRows, d.begin() + pRows + blocks);

    // Feature-name table: header words 6/7 = pointer + count;
    // 132-byte records with the name at offset +4.
    uint32_t pFeatNames = u32(&d[24]);
    uint32_t featCount = u32(&d[28]);
    if (pFeatNames && featCount && featCount < 4096 &&
        uint64_t(pFeatNames) + uint64_t(featCount) * 132 <= d.size()) {
        for (uint32_t i = 0; i < featCount; ++i) {
            const char* nm = reinterpret_cast<const char*>(&d[pFeatNames + i * 132 + 4]);
            m.featureNames.emplace_back(nm, strnlen(nm, 64));
        }
    }

    if (pMinimap && pMinimap + 8 <= d.size()) {
        m.minimapW = int(u32(&d[pMinimap]));
        m.minimapH = int(u32(&d[pMinimap + 4]));
        size_t n = size_t(m.minimapW) * m.minimapH;
        need(d, pMinimap + 8, n, "minimap");
        m.minimap.assign(d.begin() + pMinimap + 8, d.begin() + pMinimap + 8 + n);
    }
    return m;
}

std::string Map::tileKeyHex(int bx, int by) const {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08x", tileKeys[size_t(by) * blocksX + bx]);
    return buf;
}

} // namespace tak::tnt
