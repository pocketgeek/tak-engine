#pragma once

// A parsed .takrep replay: enough to rebuild the world and replay it.
// Extracted from client/main.cpp; kept at global scope so its existing
// unqualified use sites there are unchanged.

#include <cstdint>
#include <string>
#include <vector>

#include "net/client.h"        // tak::net::Bundle (+ transitively net/protocol.h)
#include "net/replayhdr.h"     // the shared .takrep header + ReplayCheck
#include "sim/matchsetup.h"    // tak::sim::MatchConfig

struct ReplayFile {
    std::string mapId, mapDigest, overrideDigest;
    std::string mission;          // campaign mission stem ("" = skirmish)
    std::string engineVersion;    // build that recorded it
    std::string error;            // why a load was refused (shown to the user)
    bool crusades = false;
    uint8_t overridePolicy = 1;   // override tier the recorded game ran under
    uint32_t formatVersion = 0, protocolVersion = 0;
    uint64_t dataHash = 0;        // gameplay data the recording ran on
    tak::sim::MatchConfig cfg;
    std::vector<tak::net::Bundle> bundles;
    // (tick, hash) from the ORIGINAL game, so playback can be checked against what
    // actually happened rather than against another rerun of itself.
    std::vector<tak::net::ReplayCheck> checks;
};

// Compares playback against the recorded checkpoints and tells a hash-LAYOUT change from a
// BEHAVIOUR change. stateHash folds Legion bookkeeping, scripts and nav, so it diverges the
// moment a field is added to it; posDigest folds only what a player sees (types, owners,
// positions, hp, the front order). Divergence of the first with the second still matching
// means the layout moved; both diverging means the game itself played differently.
// Feed it every checkpoint in order; `pos` is only evaluated when a digest was recorded.
class ReplayCheckTracker {
public:
    // Returns true the first time the state hash diverges at a checkpoint.
    template <class PosFn>
    bool observe(const tak::net::ReplayCheck& ck, uint64_t mine, PosFn&& posNow) {
        bool first = false;
        if (mine != ck.hash && !stateDiverged_) {
            stateDiverged_ = true; stateTick_ = ck.tick; stateRecorded_ = ck.hash; stateMine_ = mine; first = true;
        }
        if (ck.posDigest != 0) {
            haveDigest_ = true;
            if (!posDiverged_) {
                if (posNow() != ck.posDigest) { posDiverged_ = true; posTick_ = ck.tick; }
                else posLastGood_ = ck.tick;
            }
        }
        return first;
    }
    bool stateDiverged() const { return stateDiverged_; }
    bool posDiverged() const { return posDiverged_; }
    bool haveDigest() const { return haveDigest_; }
    uint32_t stateTick() const { return stateTick_; }
    uint32_t posTick() const { return posTick_; }
    uint64_t stateRecorded() const { return stateRecorded_; }
    uint64_t stateMine() const { return stateMine_; }
    // "" when playback matched everywhere it was checked.
    std::string summary() const {
        if (!stateDiverged_) return posDiverged_
            ? "positions/hp/orders diverged at tick " + std::to_string(posTick_) + " (state hash matched)" : "";
        std::string s = "state diverged at tick " + std::to_string(stateTick_);
        if (!haveDigest_) return s + "; no position digest was recorded (format < 12)";
        if (posDiverged_) {
            if (posTick_ == stateTick_) return s + "; positions/hp/orders diverge at the same tick (a behaviour change)";
            return s + "; positions/hp/orders match until tick " + std::to_string(posTick_) + " (hash layout changed first, behaviour later)";
        }
        return s + "; positions/hp/orders match through every checkpoint (a hash layout change only)";
    }
private:
    bool stateDiverged_ = false, posDiverged_ = false, haveDigest_ = false;
    uint32_t stateTick_ = 0, posTick_ = 0, posLastGood_ = 0;
    uint64_t stateRecorded_ = 0, stateMine_ = 0;
};

// Load a .takrep (header + tick bundles). Returns false on a malformed file.
bool loadReplayFile(const std::string& path, ReplayFile& out);

// ---- writing ---------------------------------------------------------------
//
// Save the replay the CLIENT recorded (MpClient::replayLog) as a .takrep, in the
// same format and the same byte layout the server's --replaydir writes: the header
// fields below are mirrored from Server::writeReplay, and the bundles are the raw
// payloads the server broadcast, so the two writers cannot drift.
//
// Every human player records its own copy, which is what makes a replay survive a
// server that keeps none -- and a client only ever has the game it was in, so there
// is nothing here that a player could not already see.
//
// `dir` is the user's config directory (settingsPath()'s folder), so replays live
// beside settings.ini rather than in whatever the working directory happened to be.
// Returns the written path, or empty on failure.
// `gameplayHash` is the fingerprint of the data the GAME ran on (the client's
// gameDataHash(), what it reports at Loaded) -- NOT the pure-retail handshake hash,
// which differs under a Full-tier override.
std::string saveReplayFile(const std::string& dir, const tak::net::MpClient& mp,
                           uint64_t stampMs, uint64_t gameplayHash);
