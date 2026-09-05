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
    return load(d);
}

std::vector<Placement> load(const std::vector<uint8_t>& d) {
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
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    return loadTriggers(d);
}

Triggers loadTriggers(const std::vector<uint8_t>& d) {
    Triggers out;
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

    // Body: records = {ints[0..4], slots[5][64]}. A slot is valid when its
    // text is printable and the remainder of the 64 bytes is zero.
    auto slotOk = [&](size_t off) {
        if (off + 64 > d.size()) return false;
        size_t n = 0;
        while (n < 64 && d[off + n]) ++n;
        if (n == 64) return false;
        for (size_t i = 0; i < n; ++i)
            if (d[off + i] < 32 || d[off + i] >= 127) return false;
        for (size_t i = n; i < 64; ++i)
            if (d[off + i]) return false;
        return true;
    };
    size_t pos = t + 8;   // header is {version, numTriggers}
    while (pos + 320 <= defsEnd) {
        bool hit = false;
        for (int nints = 0; nints <= 4; ++nints) {
            size_t base = pos + size_t(nints) * 4;
            if (base + 320 > defsEnd) break;
            bool ok = true;
            for (int i = 0; i < 5 && ok; ++i)
                if (!slotOk(base + size_t(i) * 64)) ok = false;
            if (!ok) continue;
            TrigRecord rec;
            for (int i = 0; i < nints; ++i)
                rec.ints.push_back(int32_t(u32(d, pos + size_t(i) * 4)));
            for (int i = 0; i < 5; ++i) {
                const char* p = reinterpret_cast<const char*>(&d[base + size_t(i) * 64]);
                std::string sv(p, strnlen(p, 63));
                while (!sv.empty() && sv.back() == ' ') sv.pop_back();
                if (!sv.empty()) rec.slots.push_back(sv);
            }
            for (auto& sv : rec.slots) out.strings.push_back(sv);
            for (auto v : rec.ints)
                if (v) out.ints.push_back(v);
            out.records.push_back(std::move(rec));
            pos = base + 320;
            hit = true;
            break;
        }
        if (!hit) pos += 4;
    }
    return out;
}

} // namespace tak::crt
