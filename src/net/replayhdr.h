#pragma once

// The .takrep header, in ONE place.
//
// It used to be written out field-by-field in two places -- Server::writeReplay and
// the client's saveReplayFile -- and read in a third, with a comment in each asking
// the next person to keep them in step. That is not a contract, it is a hope. The
// struct below is the contract: both writers fill it, the loader reads it, and a
// field that is added is added once.
//
// FORMAT 6 exists because a replay has to carry EVERY input the match started from,
// or playback silently simulates a different game. Format 5 was missing:
//   * randomStarts -- playback used fixed start positions for a shuffled match;
//   * the per-slot AI level -- an Absurd AI lost its doubled income, changing its
//     whole production curve (the recorded commands do not carry that; the income
//     multiplier is derived from the level at setup);
//   * the benchmark intensity -- playback skipped the staged spawns entirely;
//   * the mission stem -- a campaign replay was rebuilt as an ordinary skirmish,
//     with none of its placements or scripting;
//   * the seed, which was written but DISCARDED by the loader;
//   * any way to tell that playback ran the same engine and the same game data.
//
// It also carries hash checkpoints. Two identical reruns only prove the reruns
// agree; a recorded (tick, hash) trail from the original game is what lets a replay
// say WHERE it diverged from what actually happened.

#include <cstdint>
#include <string>
#include <vector>

#include "net/protocol.h"

namespace tak::net {

// Bump when the layout changes, and handle the older values in readReplayHeader.
inline constexpr uint32_t kReplayFormat = 12;   // 12: checkpoints carry posDigest; 11: independent pathfinding mode

// Simulation changes require the matching engine; do not emulate older rules.
inline bool supportedReplayProtocol(uint32_t format, uint32_t protocol) {
    if (format < 1 || format > kReplayFormat) return false;
    return protocol == kNetVersion;
}

struct ReplayHeader {
    std::string mapId;
    std::string mapDigest, overrideDigest;
    std::string mission;          // campaign mission stem ("" = skirmish)
    std::string engineVersion;    // tak::kVersion of the build that recorded it
    uint8_t crusades = 0, forfeitSelfDestruct = 0;
    uint8_t overridePolicy = 1;
    uint32_t unitCap = 0;
    uint8_t monarchExpendable = 0, stressTest = 0, randomStarts = 0;
    sim::PathfindingMode pathfindingMode = sim::PathfindingMode::Retail;
    uint8_t doubleSight = 0;
    uint8_t benchmark = 0;        // benchmark intensity (0 = off)
    uint32_t seed = 0;
    uint64_t dataHash = 0;        // hpi::gameplayHash of the data the game ran on
    uint8_t slotType[kMaxSlots] = {};
    uint8_t slotFaction[kMaxSlots] = {};
    uint8_t slotColor[kMaxSlots] = {};
    uint8_t slotTeam[kMaxSlots] = {};
    uint8_t slotAiLevel[kMaxSlots] = {};
};

// One recorded (tick, hash) pair from the ORIGINAL game.
// Format 12 adds posDigest (World::posDigest: ids, types, owners, positions, hp and the
// front order only, no bookkeeping), so a verifier can tell a hash-layout change (the
// hash diverges, the digest does not) from a behaviour change (both diverge). 0 = the
// recorder did not compute one (format < 12, or a tick it skipped). Not a net protocol
// change: the digest travels in the file only.
struct ReplayCheck {
    uint32_t tick = 0;
    uint64_t hash = 0;
    uint64_t posDigest = 0;
};

// Bytes per checkpoint record in a file of this format.
inline constexpr size_t replayCheckBytes(uint32_t fmt) { return fmt >= 12 ? 20 : 12; }

inline void writeReplayHeader(Writer& w, const ReplayHeader& h) {
    for (char ch : {'T', 'A', 'K', 'R'}) w.u8(uint8_t(ch));
    w.u32(kReplayFormat);
    w.u32(kNetVersion);
    w.str(h.mapId);
    w.u8(h.crusades);
    w.u8(h.forfeitSelfDestruct);
    w.u8(h.overridePolicy);
    w.u32(h.unitCap);
    w.u8(h.monarchExpendable);
    w.u8(h.stressTest);
    w.u32(h.seed);
    // --- format 6 ---
    w.u8(h.randomStarts);
    w.u8(h.benchmark);
    w.str(h.mission);
    w.str(h.engineVersion);
    w.u64(h.dataHash);
    w.u8(h.doubleSight);
    w.str(h.mapDigest);
    w.str(h.overrideDigest);
    w.u8(uint8_t(h.pathfindingMode));
    w.u8(uint8_t(kMaxSlots));
    for (int i = 0; i < kMaxSlots; ++i) {
        w.u8(h.slotType[i]);
        w.u8(h.slotFaction[i]);
        w.u8(h.slotColor[i]);
        w.u8(h.slotTeam[i]);
        w.u8(h.slotAiLevel[i]);   // format 6: was missing, so Absurd AI lost its income
    }
}

// Reads the header. `fmt` and `proto` come back so the caller can refuse a file it
// cannot faithfully replay -- both were recorded before and neither was ever checked.
// Returns false on a short or malformed header.
inline bool readReplayHeader(Reader& r, ReplayHeader& h, uint32_t& fmt, uint32_t& proto) {
    fmt = r.u32();
    proto = r.u32();
    if (!r.ok || fmt == 0 || fmt > kReplayFormat) return false;
    h.mapId = r.str();
    h.crusades = r.u8();
    h.forfeitSelfDestruct = r.u8();
    if (fmt >= 2) h.overridePolicy = r.u8();
    h.unitCap = 0;                       // fmt<3 ran without a unit cap
    if (fmt >= 3) h.unitCap = r.u32();
    h.monarchExpendable = 1;             // fmt<4 ran monarch-expendable
    if (fmt >= 4) h.monarchExpendable = r.u8();
    if (fmt >= 5) h.stressTest = r.u8();
    h.seed = r.u32();
    if (fmt >= 6) {
        h.randomStarts = r.u8();
        h.benchmark = r.u8();
        h.mission = r.str();
        h.engineVersion = r.str();
        h.dataHash = r.u64();
    }
    h.doubleSight = fmt >= 8 ? r.u8() : 0;
    h.mapDigest = fmt >= 9 ? r.str() : "";
    h.overrideDigest = fmt >= 10 ? r.str() : "";
    const auto pathMode = fmt >= 11 ? r.u8() : 0;
    if (!sim::validPathfindingMode(pathMode)) return false;
    h.pathfindingMode = h.mission.empty() ? sim::PathfindingMode(pathMode) : sim::PathfindingMode::Retail;
    const uint8_t nslots = r.u8();
    if (!r.ok || nslots > kMaxSlots) return false;
    for (int i = 0; i < nslots; ++i) {
        h.slotType[i] = r.u8();
        h.slotFaction[i] = r.u8();
        h.slotColor[i] = r.u8();
        h.slotTeam[i] = r.u8();
        if (fmt >= 6) h.slotAiLevel[i] = r.u8();
    }
    return r.ok;
}

}  // namespace tak::net
