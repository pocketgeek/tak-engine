#pragma once

// Random map generator. Produces a tak::tnt::Map (+ start positions) procedurally
// from a small parameter set, deterministically (integer-only) so a multiplayer
// client and the server referee build the BYTE-IDENTICAL map from the same seed --
// the generated terrain/features feed the hashed lockstep sim, so it must agree on
// every peer. The parameters ride inside the mapId string ("~gen1~<hex>"), which the
// lobby threads to every peer. Protocol 218 supports v7 open-edge maze recipes.

#include "tnt/tnt.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tak::hpi { class Vfs; }

namespace tak::mapgen {

// One of the retail worlds; also selects the terrain section art + feature palette.
// Matches matchsetup faction ids: 0=aramon 1=taros 2=veruna 3=zhon 4=creon.
enum MapType : uint8_t { Aramon = 0, Taros = 1, Veruna = 2, Zhon = 3, Creon = 4, kMapTypes };

enum Layout : uint8_t { Mainland = 0, Lakes = 1, Islands = 2, Maze, Ports, Riverlands, Jungle, Highlands, kLayouts };
const char* layoutName(uint8_t layout);
uint8_t themedLayout(uint8_t world);
bool automaticWater(uint8_t layout);

struct Params {
    uint16_t formatVer = 7;          // Older recipes retain their original terrain/features
    uint64_t seed = 1;
    std::string name;               // v5: optional ASCII map title, up to 24 characters
    uint8_t  mapType = Aramon;
    uint16_t widthCells = 256, heightCells = 256;   // multiples of 32, clamped
    uint8_t  players = 2;            // 2..8
    uint8_t  treeDensity = 128;      // 0..255 (few..lots)
    uint8_t  rockDensity = 96;       // 0..255
    uint8_t  manaDensity = 128;      // 0..255
    uint8_t  waterDensity = 96;      // mainland/lake intensity, not coverage; 0 = dry
    uint8_t  layout = Mainland;
    uint8_t  reliefDensity = 128;    // authored hill patch density; 0 = flat
};

struct Result {
    tak::tnt::Map map;
    int waterPercent = 0, reliefPatches = 0;
    // v3: one open-water harbor per island; cells suitable for naval construction.
    std::vector<std::pair<int, int>> harbors;
    std::vector<std::pair<int, int>> starts;   // start positions in 16px CELL coords (x, z)
};

// mapId carrier. isGeneratedMapId recognises the "~gen1~" magic; encode/decode pack
// the Params to/from the hex payload. friendlyLabel is what the UI shows.
bool        isGeneratedMapId(const std::string& id);
std::string encodeMapId(const Params& p);
Params      decodeMapId(const std::string& id);     // sane defaults on a malformed id
std::string friendlyLabel(const Params& p);

// The generator. Deterministic in the params + the install's prefab sections
// (coastlines are stamped from the retail Coast Sandy section TNTs read through
// the VFS -- same install => identical bytes on every peer; the MP data-hash
// agreement already pins the install).
Result generate(const Params& p, const hpi::Vfs& vfs);

// Prefabs consumed by either generator, included in multiplayer's data hash.
std::vector<std::string> assetPaths(const hpi::Vfs& vfs);

// Clamp UI inputs to section multiples, 2..8 players, and layout-specific space.
Params sanitize(Params p);

}  // namespace tak::mapgen
