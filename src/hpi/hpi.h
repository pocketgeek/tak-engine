#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace tak::hpi {

// TAK ships its data in HPI archives, a format inherited from Total
// Annihilation but revised for Kingdoms (version 2: larger offsets, zlib
// compression, no XOR key obfuscation in most files). We only interpret
// fields we have verified against real archives; everything else is
// reported raw by `inspect`.

struct HeaderInfo {
    std::string magic;      // expected "HAPI"
    uint32_t version = 0;   // 0x00010000 = classic TA, "BANK" = savegame,
                            // 0x00020000 = TAK (to be verified)
    uint32_t word2 = 0;     // meaning differs by version
    uint32_t word3 = 0;
    uint32_t word4 = 0;
    uint64_t fileSize = 0;
};

HeaderInfo inspect(const std::filesystem::path& archive);

std::string describe(const HeaderInfo& info);

} // namespace tak::hpi
