#include "client/replayfile.h"

#include "net/matchconfig.h"   // matchConfigFor / replayHeaderFor
#include "net/protocol.h"      // tak::net::Reader / getBundle

#include <cstdio>
#include <filesystem>
#include <utility>

bool loadReplayFile(const std::string& path, ReplayFile& out) {
#ifdef _WIN32
    FILE* f = _wfopen(std::filesystem::u8path(path).c_str(), L"rb");
#else
    FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0 || uint64_t(n) > tak::net::crusades::kMaxReplayBytes) {
        std::fclose(f); out.error = "replay file exceeds supported size"; return false;
    }
    std::vector<uint8_t> d(size_t(n < 0 ? 0 : n));
    if (!d.empty() && std::fread(d.data(), 1, d.size(), f) != d.size()) { std::fclose(f); return false; }
    std::fclose(f);
    if (d.size() < 4 || d[0] != 'T' || d[1] != 'A' || d[2] != 'K' || d[3] != 'R') return false;
    tak::net::Reader r(d.data() + 4, d.size() - 4);
    tak::net::ReplayHeader h;
    uint32_t fmt = 0, proto = 0;
    if (!tak::net::readReplayHeader(r, h, fmt, proto)) return false;
    // REFUSE what we cannot replay faithfully. Both of these were recorded from the
    // start and neither was ever checked, so an old file ran happily under new
    // simulation rules and quietly produced a different game.
    out.formatVersion = fmt;
    out.protocolVersion = proto;
    if (!tak::net::supportedReplayProtocol(fmt, proto)) {
        out.error = "recorded under protocol v" + std::to_string(proto) +
                    ", this build is v" + std::to_string(tak::net::kNetVersion) +
                    " -- this replay protocol is unsupported";
        return false;
    }
    out.mapId = h.mapId;
    out.mapDigest = h.mapDigest;
    out.overrideDigest = h.overrideDigest;
    out.mission = h.mission;
    out.crusades = h.crusades != 0;
    out.overridePolicy = h.overridePolicy;
    out.dataHash = h.dataHash;
    out.engineVersion = h.engineVersion;
    // The same builder the referee and the live clients use (net/matchconfig.h), fed
    // the options and START slot table the header recorded.
    tak::net::GameOptions opts;
    tak::net::SlotInfo slots[tak::net::kMaxSlots];
    tak::net::matchInputsFrom(h, opts, slots);
    out.cfg = tak::net::matchConfigFor(opts, slots, h.seed, !h.mission.empty());
    uint32_t nticks = r.u32();
    if (!r.ok) return false;
    // Bound the count by what is actually LEFT in the file before reserving. A count is
    // just four bytes a corrupt file can set to anything, and reserving on trust turns
    // a truncated 200-byte replay into a multi-gigabyte allocation that throws where
    // the caller expects a clean false. Each bundle costs at least its 4-byte length.
    if (uint64_t(nticks) * 4 > uint64_t(r.end - r.p)) return false;
    out.bundles.reserve(nticks);
    for (uint32_t t = 0; t < nticks; ++t) {
        uint32_t len = r.u32();
        if (!r.ok || !r.avail(len)) return false;
        tak::net::Reader br(r.p, len);
        r.p += len;
        // The recorded tick index proves the bundle stream is intact and in order.
        uint32_t tick = 0;
        tak::net::Bundle bd;
        if (!tak::net::getBundle(br, tick, bd, proto) || tick != t) return false;
        out.bundles.push_back(std::move(bd));
    }
    // Hash checkpoints (format 6+). Absent in an older file, which simply means
    // playback cannot be checked against the original.
    if (fmt >= 6) {
        uint32_t nchecks = r.u32();
        if (!r.ok) return false;
        if (uint64_t(nchecks) * tak::net::replayCheckBytes(fmt) > uint64_t(r.end - r.p)) return false;   // u32 + u64 (+ u64 from format 12)
        out.checks.reserve(nchecks);
        for (uint32_t i = 0; i < nchecks; ++i) {
            tak::net::ReplayCheck c;
            c.tick = r.u32();
            c.hash = r.u64();
            if (fmt >= 12) c.posDigest = r.u64();
            if (!r.ok) return false;
            out.checks.push_back(c);
        }
    }
    return r.ok && r.p == r.end;
}

std::string saveReplayFile(const std::string& dir, const tak::net::MpClient& mp,
                           uint64_t stampMs, uint64_t gameplayHash) {
    const auto& log = mp.replayLog();
    if (dir.empty() || log.empty()) return {};
    const tak::net::RoomView& room = mp.room();
    const tak::net::SlotInfo* slots = mp.startSlots();
    // Header shared with the server writer and the loader -- see net/matchconfig.h.
    tak::net::ReplayHeader h = tak::net::replayHeaderFor(room.opts, room.mapId, room.mission, slots, mp.startSeed());
    if (mp.overridePackage()) h.overrideDigest=mp.overridePackage()->digest;
    if (mp.mapPackage()) h.mapDigest = mp.mapPackage()->digest;
    // The hash of the data this GAME ran on, at its override tier -- not the
    // pure-retail fingerprint the handshake uses. Under a Full-tier game the two
    // differ, and it is the effective one a replay has to be checked against. The
    // server's writer already records that; this recorded the handshake hash.
    h.dataHash = gameplayHash;
    tak::net::Writer w;
    tak::net::writeReplayHeader(w, h);
    w.u32(uint32_t(log.size()));
    for (const auto& b : log) {
        w.u32(uint32_t(b.size()));
        w.b.insert(w.b.end(), b.begin(), b.end());
    }
    // This client's own hash trail, so a replay can say WHERE playback diverged
    // from the game as it was actually played -- not merely that two reruns agree.
    const auto& checks = mp.hashLog();
    w.u32(uint32_t(checks.size()));
    for (const auto& c : checks) { w.u32(c.tick); w.u64(c.hash); w.u64(c.posDigest); }
    // Named by the game and a timestamp, so several replays coexist and a rerun of
    // the same game does not overwrite the earlier one.
    std::string path = dir + "game-" + std::to_string(room.id) + "-" +
                       std::to_string(stampMs) + ".takrep";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return {};
    const bool ok = std::fwrite(w.b.data(), 1, w.b.size(), f) == w.b.size();
    std::fclose(f);
    return ok ? path : std::string();
}
