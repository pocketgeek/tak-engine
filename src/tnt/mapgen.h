#pragma once

// Random map generator. Produces a tak::tnt::Map (+ start positions) procedurally
// from a small parameter set, deterministically (integer-only) so a multiplayer
// client and the server referee build the BYTE-IDENTICAL map from the same seed --
// the generated terrain/features feed the hashed lockstep sim, so it must agree on
// every peer. The parameters ride inside the mapId string ("~gen1~<hex>"), which the
// lobby threads to every peer. Version 10 adds the retail cliff/plateau, mountain
// and town kits; older recipes are refused (their saved .kmp maps stay playable).

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

// v9: neutral mana spots scattered over open ground, as an area density. Max is
// the density of a home economy holding six spots (6 per 87x87-cell reserved home
// square, ~1 spot per 1262 cells); each lower level halves it.
enum ManaScatter : uint8_t { ScatterNone = 0, ScatterMinimum, ScatterMedium, ScatterHigh, ScatterMax, kScatterLevels };
const char* scatterName(uint8_t level);
// Jittered-grid pitch in cells for a level (0 = none): one candidate per pitch^2.
int scatterPitch(uint8_t level);

// v10 retail terrain kits a world can assemble (some worlds lack one; see kitAvailable).
enum Kit : uint8_t { KitPlateaus, KitMountains, KitTowns, kKits };
bool kitAvailable(uint8_t world, uint8_t kit);
// Layouts that supply their own high ground (Maze walls, Creon highlands) ignore the
// plateau amount; mountains and towns still apply.
bool automaticPlateaus(uint8_t layout);

struct Params {
    uint16_t formatVer = 10;
    uint64_t seed = 1;
    std::string name;               // v5: optional ASCII map title, up to 24 characters
    uint8_t  mapType = Aramon;
    uint16_t widthCells = 256, heightCells = 256;   // multiples of 32, clamped
    uint8_t  players = 2;            // 2..8
    uint8_t  treeDensity = 128;      // 0..255 (few..lots)
    uint8_t  rockDensity = 96;       // 0..255
    uint8_t  manaDensity = 128;      // 0..255 requests 0..6 extra spots per player
    uint8_t  waterDensity = 96;      // mainland/lake intensity, not coverage; 0 = dry
    uint8_t  layout = Mainland;
    uint8_t  reliefDensity = 128;    // authored hill patch density; 0 = flat
    uint8_t  manaScatter = ScatterNone;   // v9: ManaScatter level for neutral spots
    // v10 retail kits, 0 = none. All three at 0 reproduce the v9 terrain exactly.
    uint8_t  plateauDensity = 72;    // cliff-edged plateaus (with walkable ramps)
    uint8_t  mountainDensity = 56;   // mountain ranges (raised on their own plateau)
    uint8_t  townDensity = 64;       // walled settlements / Zhon hut villages
};

struct Result {
    tak::tnt::Map map;
    int waterPercent = 0, reliefPatches = 0;
    int scatteredMana = 0;   // v9 neutral spots placed (not part of any player's rounds)
    int plateaus = 0, mountains = 0, towns = 0;   // v10 kit placements (components/ranges/towns)
    // v10: 0 = kits as requested; 1 = halved, 2 = dropped, because a fair map did not fit.
    int kitFallback = 0;
    // v3: one open-water harbor per island; cells suitable for naval construction.
    std::vector<std::pair<int, int>> harbors;
    std::vector<std::pair<int, int>> starts;   // start positions in 16px CELL coords (x, z)
};

// mapId carrier. isGeneratedMapId recognises the "~gen1~" magic; encode/decode pack
// the Params to/from the hex payload. friendlyLabel is what the UI shows.
bool        isGeneratedMapId(const std::string& id);
std::string encodeMapId(const Params& p);
// Sane defaults on a malformed id. An id from an older generator keeps its version,
// which generate() refuses ("generate a new map"); the menus upgrade it on edit.
Params      decodeMapId(const std::string& id);
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

// Complete, balanced expansion rounds; independent of map area/player count.
inline int extraManaRounds(uint8_t density) { return (int(density)*6+127)/255; }

}  // namespace tak::mapgen
