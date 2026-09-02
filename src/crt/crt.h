#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tak::crt {

// .crt scenario files: initial unit placements for a map, plus a trigger
// section (not yet parsed). Reverse-engineered layout (version 1.0, flag 0):
//   f32 version; u32 flag; u32 count;
//   count x 568-byte records: char name[32] @0, u8 player @352,
//   u16 x @504, u16 z @506 (both in 2px units).

struct Placement {
    std::string name;   // unit type (objectname)
    float x = 0, z = 0; // map pixels
    int player = 0;
};

// Returns placements; empty if the file is empty or an unsupported variant.
std::vector<Placement> load(const std::filesystem::path& path);

} // namespace tak::crt
