#include "crt/crt.h"

#include <cmath>
#include <cstring>
#include <fstream>

namespace tak::crt {

namespace {
uint32_t u32(const std::vector<uint8_t>& d, size_t off) {
    return d[off] | (d[off + 1] << 8) | (d[off + 2] << 16) | (uint32_t(d[off + 3]) << 24);
}
uint16_t u16(const std::vector<uint8_t>& d, size_t off) {
    return uint16_t(d[off] | (d[off + 1] << 8));
}
} // namespace

std::vector<Placement> load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    if (d.size() < 16) return {};

    float version;
    std::memcpy(&version, d.data(), 4);
    uint32_t flag = u32(d, 4);
    uint32_t count = u32(d, 8);
    if (std::abs(version - 1.0f) > 0.01f || flag != 0) return {};
    constexpr size_t kRec = 568;
    if (count > 2000 || 12 + size_t(count) * kRec > d.size()) return {};

    std::vector<Placement> out;
    for (uint32_t i = 0; i < count; ++i) {
        size_t off = 12 + size_t(i) * kRec;
        const char* nm = reinterpret_cast<const char*>(&d[off]);
        Placement p;
        p.name.assign(nm, strnlen(nm, 32));
        p.player = d[off + 352];
        p.x = float(u16(d, off + 504)) * 2;
        p.z = float(u16(d, off + 506)) * 2;
        if (p.name.empty() || (p.x == 0 && p.z == 0)) continue;
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace tak::crt
