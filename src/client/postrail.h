#pragma once
// TAK_POSTRAIL=<path> / TAK_POSTRAIL_REF=<path> -- debug-only replay hooks (src/client/dev.h).
//
// A format-12 replay carries World::posDigest beside every state hash, so playback can say
// "the hash layout moved" apart from "the game played differently". Older recordings (the
// user's legion-r9 and legion-r10) carry only the state hash, and they play back exactly only
// on the build that recorded them. TAK_POSTRAIL, run on that recording build, writes the digest
// the recording had at every checkpoint tick; TAK_POSTRAIL_REF, run on the head, feeds that file
// in as the missing digests, so the head reports both divergence ticks for a recording that
// predates the format.
//
// Header-only and self-contained so it can be copied into the old trees: digest() below is the
// same fold as World::posDigest (replay_file_test holds the two equal).
#include "sim/sim.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

namespace tak::postrail {

inline uint64_t digest(const tak::sim::World& w) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFF; h *= 1099511628211ULL; }
    };
    for (const auto& u : w.units()) {
        mix(uint64_t(uint32_t(u.id)));
        uint64_t t = 1469598103934665603ULL;
        if (u.type) for (unsigned char c : u.type->name) { t ^= c; t *= 1099511628211ULL; }
        mix(t);
        mix(uint64_t(uint32_t(u.player)));
        mix(uint64_t(uint32_t(u.x.v)));
        mix(uint64_t(uint32_t(u.z.v)));
        mix(uint64_t(uint32_t(u.hp.v)));
        if (u.orders.empty()) { mix(0); continue; }
        const auto& o = u.orders.front();
        mix(1 + (o.load ? 2u : 0u) + (o.unload ? 4u : 0u) + (o.attackMove ? 8u : 0u) + (o.patrol ? 16u : 0u) +
            (o.guard ? 32u : 0u) + (o.waitAttack ? 64u : 0u) + (o.buildType ? 128u : 0u) + (o.wait > 0 ? 256u : 0u));
        mix(uint64_t(uint32_t(o.targetId)));
        mix(uint64_t(uint32_t(o.x.v)));
        mix(uint64_t(uint32_t(o.z.v)));
    }
    return h;
}

inline constexpr uint32_t kPeriod = 30;   // the clients record a checkpoint every kHashPeriod ticks

class Writer {
public:
    void open(const char* path) { if (path && *path) f_ = std::fopen(path, "w"); }
    bool active() const { return f_ != nullptr; }
    // Call with the world after tick `tick` has run.
    void record(uint32_t tick, const tak::sim::World& w) {
        if (f_ && tick % kPeriod == 0) std::fprintf(f_, "%u %016llx\n", tick, (unsigned long long)digest(w));
    }
    ~Writer() { if (f_) std::fclose(f_); }
private:
    std::FILE* f_ = nullptr;
};

inline std::map<uint32_t, uint64_t> load(const std::string& path) {
    std::map<uint32_t, uint64_t> out;
    if (std::FILE* f = std::fopen(path.c_str(), "r")) {
        unsigned tick; unsigned long long d;
        while (std::fscanf(f, "%u %llx", &tick, &d) == 2) out[tick] = d;
        std::fclose(f);
    }
    return out;
}

}  // namespace tak::postrail
