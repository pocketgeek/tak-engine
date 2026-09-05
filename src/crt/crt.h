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
std::vector<Placement> load(const std::vector<uint8_t>& d);   // from a VFS buffer

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

// One trigger record: 1-4 leading ints then five 64-byte operand slots.
// The record's opcode is the LAST int (earlier ints are trailing params
// of the previous record). Known ops: 1 = at-time(seconds), 7 = spawn
// (type, region), 13 = score-count(type, region), 16 = count-condition
// (n, type, region), 2/17/18 = variable ops, 3 = var compare.
struct TrigRecord {
    std::vector<int32_t> ints;
    std::vector<std::string> slots;   // non-empty slots, in order
    int32_t op() const { return ints.empty() ? 0 : ints.back(); }
};

struct Triggers {
    int numTriggers = 0;
    std::vector<TrigRecord> records;
    std::vector<std::string> strings;   // slot strings in stream order
    std::vector<int32_t> ints;          // int params in stream order
    std::vector<Region> regions;
};

Triggers loadTriggers(const std::filesystem::path& path);
Triggers loadTriggers(const std::vector<uint8_t>& d);   // from a VFS buffer

} // namespace tak::crt
