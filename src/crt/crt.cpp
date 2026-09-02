#include "crt/crt.h"

#include <cctype>
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

Triggers loadTriggers(const std::filesystem::path& path) {
    Triggers out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    if (d.size() < 16) return out;
    float version;
    std::memcpy(&version, d.data(), 4);
    uint32_t flag = u32(d, 4);
    uint32_t count = u32(d, 8);
    if (std::abs(version - 1.0f) > 0.01f || flag != 0) return out;
    size_t t = 12 + size_t(count) * 568;
    if (t + 16 > d.size()) return out;
    if (u32(d, t) != 9) return out;   // trigger-format version
    out.numTriggers = int(u32(d, t + 4));

    // Trailer: numDefs + 272-byte defs, anchored at file end.
    size_t defsEnd = d.size();
    for (int n = int((defsEnd - t) / 272); n >= 0; --n) {
        size_t defsStart = defsEnd - size_t(n) * 272;
        if (defsStart < t + 20) continue;
        if (u32(d, defsStart - 4) == uint32_t(n)) {
            if (n > 0) {   // sanity: first def name must be printable
                const char* nm = reinterpret_cast<const char*>(&d[defsStart]);
                size_t len = strnlen(nm, 63);
                bool okName = len > 0;
                for (size_t i = 0; i < len && okName; ++i)
                    if (uint8_t(nm[i]) < 32 || uint8_t(nm[i]) >= 127) okName = false;
                if (!okName) continue;
            }
            for (int i = 0; i < n; ++i) {
                size_t off = defsStart + size_t(i) * 272;
                const char* nm = reinterpret_cast<const char*>(&d[off]);
                Region r;
                r.name.assign(nm, strnlen(nm, 63));
                while (!r.name.empty() && r.name.back() == ' ') r.name.pop_back();
                r.x1 = int32_t(u32(d, off + 256));
                r.z1 = int32_t(u32(d, off + 260));
                r.x2 = int32_t(u32(d, off + 264));
                r.z2 = int32_t(u32(d, off + 268));
                out.regions.push_back(std::move(r));
            }
            defsEnd = defsStart - 4;
            break;
        }
    }

    // Body: token walk — 64-byte printable slots or 4-byte ints (zeros skipped).
    size_t pos = t + 16;
    while (pos + 4 <= defsEnd) {
        if (pos + 64 <= defsEnd) {
            const char* p = reinterpret_cast<const char*>(&d[pos]);
            size_t len = strnlen(p, 63);
            bool printable = len > 0;
            for (size_t i = 0; i < len && printable; ++i)
                if (uint8_t(p[i]) < 32 || uint8_t(p[i]) >= 127) printable = false;
            if (printable && (len > 2 || (std::isdigit(uint8_t(p[0]))))) {
                std::string sv(p, len);
                while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                out.strings.push_back(std::move(sv));
                pos += 64;
                continue;
            }
        }
        int32_t v = int32_t(u32(d, pos));
        if (v != 0) out.ints.push_back(v);
        pos += 4;
    }
    return out;
}

} // namespace tak::crt
