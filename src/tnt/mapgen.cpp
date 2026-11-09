#include "tnt/mapgen.h"
#include "tnt/mapgen_internal.h"

#include "hpi/hpi.h"   // coast prefab sections are read through the VFS

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace tak::mapgen {

using namespace detail;

Result generateBalanced(const Params& p, const hpi::Vfs& vfs);

std::vector<std::string> assetPaths(const hpi::Vfs& vfs) {
    std::vector<std::string> paths;
    for (const auto& kit : kShoreKit)
        for (const auto* name : kit.name)
            for (const auto* variant : kit.var) {
                std::string path = std::string(kit.dir) + name + variant + kit.ext;
                if (vfs.has(path)) paths.push_back(std::move(path));
            }
    for (const auto* prefix : kReliefDir)
        for (const auto& path : vfs.list(prefix))
            if (hpi::MountSet::key(path).ends_with(".tnt")) paths.push_back(path);
    for (const auto& prefix : kitDirectories())
        for (const auto& path : vfs.list(prefix))
            if (hpi::MountSet::key(path).ends_with(".tnt")) paths.push_back(path);
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

Params sanitize(Params p) {
    std::string name;
    for (unsigned char c : p.name) {
        if (name.size() == 24) break;
        // Safe both as an archive path and as an unquoted OTA value.
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_') name += char(c);
    }
    while (!name.empty() && name.back() == ' ') name.pop_back();
    const auto first = name.find_first_not_of(' ');
    p.name = first == std::string::npos ? std::string{} : name.substr(first);
    if (p.mapType >= kMapTypes) p.mapType = Aramon;
    // Section multiples, 4..64 sections per side.
    auto fix = [](uint16_t v) -> uint16_t { return uint16_t(std::clamp(int(v) / 32, 4, 64) * 32); };
    p.widthCells = fix(p.widthCells);
    p.heightCells = fix(p.heightCells);
    p.players = uint8_t(std::clamp<int>(p.players, 2, 8));
    p.manaScatter = std::min<uint8_t>(p.manaScatter, kScatterLevels - 1);
    p.layout = std::min<uint8_t>(p.layout, kLayouts-1);
    if (p.layout >= Maze) for (uint8_t world=0;world<kMapTypes;++world)
        if (themedLayout(world)==p.layout) p.mapType=world;
    const int minimum = p.layout == Lakes ? (p.players > 4 ? 512 : 384) : (p.players > 4 ? 384 : 256);
    int minW = minimum, minH = minimum;
    if (p.layout >= Maze) minW = minH = p.players > 4 ? 768 : 512;
    if (p.layout == Islands) {
        const int cols = p.players <= 4 ? 2 : 3;
        const int rows = (p.players + cols - 1) / cols;
        minW = std::max(minW, cols * 256);
        minH = std::max(minH, rows * 256);
    }
    p.widthCells = uint16_t(std::max<int>(p.widthCells, minW));
    p.heightCells = uint16_t(std::max<int>(p.heightCells, minH));
    return p;
}

Result generate(const Params& raw, const tak::hpi::Vfs& input) {
    // v10 added the retail terrain kits. Older recipes are not regenerated: their
    // saved maps remain ordinary maps, and the menus upgrade a remembered recipe.
    if(raw.formatVer!=Params{}.formatVer)
        throw std::runtime_error("Random-map recipe version "+std::to_string(raw.formatVer)+
            " is no longer supported (this build makes version "+std::to_string(Params{}.formatVer)+"); generate a new map");
    tak::hpi::Vfs vfs(&input, true);
    return generateBalanced(sanitize(raw), vfs);
}

// ---- mapId carrier: "~gen1~" + hex of the packed Params -----------------------

namespace {
constexpr const char* kMagic = "~gen1~";

void put16(std::string& b, uint16_t v) { b.push_back(char(v)); b.push_back(char(v >> 8)); }
void put64(std::string& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(char(v >> (8 * i))); }
std::string toHex(const std::string& raw) {
    static const char* H = "0123456789abcdef";
    std::string s; s.reserve(raw.size() * 2);
    for (unsigned char c : raw) { s.push_back(H[c >> 4]); s.push_back(H[c & 15]); }
    return s;
}
int hexNib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}  // namespace

bool isGeneratedMapId(const std::string& id) {
    return id.size() >= 6 && id.compare(0, 6, kMagic) == 0;
}

std::string encodeMapId(const Params& pin) {
    Params p = sanitize(pin);
    std::string b;
    put16(b, Params{}.formatVer);   // only the current layout is written
    put64(b, p.seed);
    b.push_back(char(p.mapType));
    put16(b, p.widthCells); put16(b, p.heightCells);
    b.push_back(char(p.players));
    b.push_back(char(p.treeDensity));
    b.push_back(char(p.rockDensity));
    b.push_back(char(p.manaDensity));
    b.push_back(char(p.waterDensity));
    b.push_back(char(p.reliefDensity));
    b.push_back(char(p.layout));
    b.push_back(char(p.manaScatter));
    b.push_back(char(p.plateauDensity));   // v10 kits, before the variable-length name
    b.push_back(char(p.mountainDensity));
    b.push_back(char(p.townDensity));
    b += p.name;
    return std::string(kMagic) + toHex(b);
}

Params decodeMapId(const std::string& id) {
    Params p;
    if (!isGeneratedMapId(id) || id.size() > 6 + 2 * (26 + 24) || (id.size() - 6) % 2) return p;
    // De-hex the payload.
    std::string raw;
    for (size_t i = 6; i + 1 < id.size(); i += 2) {
        int hi = hexNib(id[i]), lo = hexNib(id[i + 1]);
        if (hi < 0 || lo < 0) return sanitize(p);
        raw.push_back(char((hi << 4) | lo));
    }
    auto u8 = [&](size_t o) -> uint8_t { return o < raw.size() ? uint8_t(raw[o]) : 0; };
    auto u16 = [&](size_t o) -> uint16_t { return uint16_t(u8(o) | (u8(o + 1) << 8)); };
    // v8 (22 bytes + name), v9 (+ scattered mana) and v10 (+ three kit amounts) share
    // their leading fields; decoding an older one keeps its version so generate()
    // refuses it clearly, while a menu that edits it upgrades the remembered settings.
    const uint16_t version = u16(0);
    if (raw.size() >= 2 && version != 0 && (version < 8 || version > 10)) { p.formatVer = version; return p; }
    if (raw.size() < 22) return sanitize(p);
    const size_t fixed = version >= 10 ? 26 : version == 9 ? 23 : 22;
    if (version < 8 || raw.size() < fixed || raw.size() > fixed + 24) return Params{};
    p.formatVer = version;
    uint64_t s = 0; for (int i = 0; i < 8; ++i) s |= uint64_t(u8(2 + i)) << (8 * i);
    p.seed = s;
    p.mapType = u8(10);
    p.widthCells = u16(11); p.heightCells = u16(13);
    p.players = u8(15);
    p.treeDensity = u8(16);
    p.rockDensity = u8(17);
    p.manaDensity = u8(18);
    p.waterDensity = u8(19);
    p.reliefDensity = u8(20);
    p.layout = u8(21);
    if (version >= 9) p.manaScatter = u8(22);
    if (version >= 10) { p.plateauDensity = u8(23); p.mountainDensity = u8(24); p.townDensity = u8(25); }
    p.name = raw.substr(fixed);
    return sanitize(p);
}

std::string friendlyLabel(const Params& pin) {
    Params p = sanitize(pin);
    if (!p.name.empty()) return p.name;
    static const char* kNames[kMapTypes] = {"Aramon", "Taros", "Veruna", "Zhon", "Creon"};
    int u = p.widthCells / 32, v = p.heightCells / 32;
    // ASCII only: the lobby draws this with the 5x7 block font, which has no glyph
    // for a middot and would render each byte of one as a blank.
    return "Random " + std::to_string(u) + "x" + std::to_string(v) + " " +
           std::to_string(int(p.players)) + "P " + kNames[p.mapType];
}

}  // namespace tak::mapgen
