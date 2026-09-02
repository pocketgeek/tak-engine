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

// Trigger section (after the placement records):
//   header { i32 version(9), i32 numTriggers, ... } (16 bytes)
//   trigger records: { i32 params[1..3], char slots[5][64] } — slots hold
//     unit type names, region names, player names, ASCII numbers
//   trailer { i32 numDefs, numDefs x 272-byte defs:
//     { char name[64], uninitialized[192], i32 x1,z1,x2,z2 } } — region
//     definitions in 16px cells (includes per-player start zones)
struct Region {
    std::string name;
    int x1 = 0, z1 = 0, x2 = 0, z2 = 0;   // cells
};

struct Triggers {
    int numTriggers = 0;
    std::vector<std::string> strings;   // slot strings in stream order
    std::vector<int32_t> ints;          // int params in stream order
    std::vector<Region> regions;
};

Triggers loadTriggers(const std::filesystem::path& path);

} // namespace tak::crt
