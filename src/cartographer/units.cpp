#include "cartographer/units.h"

#include "crt/crt.h"
#include "hpi/hpi.h"

#include <algorithm>
#include <filesystem>

namespace cart {

std::vector<PlacedUnit> loadUnits(const tak::hpi::Vfs& vfs, const std::string& crtPath) {
    std::vector<PlacedUnit> out;
    std::vector<uint8_t> d;
    try { d = vfs.read(crtPath); } catch (const std::exception&) { return out; }
    for (const auto& p : tak::crt::load(d)) {
        PlacedUnit u;
        u.type = p.name;
        u.player = p.player;
        u.x = p.x; u.z = p.z;
        out.push_back(std::move(u));
    }
    return out;
}

std::vector<std::string> unitTypeNames(const tak::hpi::Vfs& vfs) {
    std::vector<std::string> names;
    for (const std::string& p : vfs.list("units")) {
        std::filesystem::path fp(p);
        std::string ext = fp.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".fbi") continue;
        std::string stem = fp.stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(), ::toupper);
        names.push_back(stem);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

} // namespace cart
