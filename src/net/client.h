#pragma once

// Client-side handler for the multiplayer protocol (docs/multiplayer-design.md).
// Owns the connection to takserver and the lobby/in-game state. The viewer polls
// it, reads the lobby view for its UI, and in game pulls one TickBundle per tick.
// This class does protocol only -- it never touches the sim; the viewer applies
// the bundle's commands and advances the World.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "net/conn.h"
#include "net/protocol.h"

namespace tak::net {

// The commands + sequenced events the server assigned to one tick.
struct Bundle {
    std::vector<Command> cmds;
    std::vector<Event> events;
};

// A game's setup as the client sees it in the lobby / at start.
struct RoomView {
    uint32_t id = 0;
    std::string name, mapId;
    GameOptions opts;
    uint32_t hostId = 0;
    SlotInfo slots[kMaxSlots];
    int mySlot = -1;
    bool valid = false;
};

class MpClient {
public:
    enum class State { Offline, Connecting, Lobby, InRoom, Starting, InGame, Done };

    // Set the local gameplay-data fingerprint (hpi::gameplayHash) BEFORE connect;
    // it goes in the Hello so the server can reject a peer whose retail gameplay
    // data (or, under Full overrides, gameplay overrides) doesn't match.
    void setDataHash(uint64_t h) { dataHash_ = h; }

    bool connect(const std::string& host, uint16_t port, const std::string& name);
    void disconnect(const std::string& reason = "bye");

    // Pump the socket: read frames, dispatch, flush writes. Call every frame.
    // Returns false once the connection is gone (see error()).
    bool poll();

    State state() const { return state_; }
    const std::string& error() const { return err_; }
    uint32_t myClientId() const { return myId_; }

    // ---- lobby actions -----------------------------------------------------
    void listGames();
    // spectate=true: create the game but DON'T seat the host -- it watches as a
    // slot-less spectator, freeing all capacity slots (e.g. for an all-AI game).
    void createGame(const std::string& name, const std::string& password,
                    const std::string& mapId, const GameOptions& opts, uint8_t capacity,
                    bool spectate = false);
    void joinGame(uint32_t id, const std::string& password);
    void leaveGame();
    void setSlot(int slot, uint8_t type, uint8_t faction, uint8_t color,
                 uint8_t team, uint8_t ready);
    void kick(int slot);
    void chat(const std::string& text);
    void startGame();

    const std::vector<GameInfo>& games() const { return games_; }
    const RoomView& room() const { return room_; }
    // Drain chat lines received since the last call (sender, text).
    std::vector<std::pair<std::string, std::string>> takeChat() { auto c = std::move(chat_); chat_.clear(); return c; }
    std::string joinError() { auto e = std::move(joinErr_); joinErr_.clear(); return e; }

    // ---- game start / play -------------------------------------------------
    // After GameStarting the viewer reads these, loads the map, then calls
    // reportLoaded(). The server begins ticking once every human has loaded.
    bool starting() const { return state_ == State::Starting; }
    const RoomView& startRoom() const { return room_; }   // final slots
    uint32_t startSeed() const { return startSeed_; }
    uint32_t gameId() const { return room_.id; }
    uint64_t resumeToken() const { return resumeToken_; }
    int myPlayer() const { return room_.mySlot; }
    void reportLoaded();
    // Reconnect to a held slot: connect() first, then rejoin() with the game id
    // and the resume token saved from the original GameStarting.
    void rejoin(uint32_t gameId, uint64_t token);
    // Watch a running game (no slot): connect() first, then spectate(). The server
    // replies with GameStarting (slot 0xFF -> mySlot -1) and the bundle log, then
    // streams live bundles. A spectator sends no commands or hashes.
    void spectate(uint32_t gameId, const std::string& password);
    bool isSpectator() const { return spectator_; }
    // True when the last GameStarting was a REJOIN (the world must be rebuilt and
    // the bundle log replayed). Cleared once consumed.
    bool isRejoin() const { return rejoin_; }
    void clearRejoin() { rejoin_ = false; }
    bool paused() const { return paused_; }

    // In game: is bundle `tick` available? If so, consume it (once).
    bool haveBundle(uint32_t tick) const { return bundles_.count(tick) != 0; }
    bool takeBundle(uint32_t tick, Bundle& out);
    size_t bufferedBundles() const { return bundles_.size(); }   // backlog depth
    void sendCommands(const std::vector<Command>& cmds);
    void sendHash(uint32_t tick, uint64_t hash);

    // Set once the server flags us desynced (fatal in M3; reconnect is M5).
    bool desynced() const { return desynced_; }
    std::string desyncReason() const { return desyncReason_; }

private:
    void onFrame(const Frame& f);
    void send(Msg kind, const Writer& w) { conn_.send(kind, w); }
    void send(Msg kind) { conn_.send(kind); }

    Conn conn_;
    State state_ = State::Offline;
    std::string err_;
    std::string name_;
    uint32_t myId_ = 0;

    std::vector<GameInfo> games_;
    RoomView room_;
    std::vector<std::pair<std::string, std::string>> chat_;
    std::string joinErr_;

    uint64_t dataHash_ = 0;      // local gameplay-data fingerprint (sent in Hello)
    uint32_t startSeed_ = 0;
    uint64_t resumeToken_ = 0;
    bool rejoin_ = false;
    bool expectingRejoin_ = false;
    bool spectator_ = false;
    bool expectingSpectate_ = false;
    bool paused_ = false;
    std::map<uint32_t, Bundle> bundles_;
    bool desynced_ = false;
    std::string desyncReason_;
    uint64_t lastRecvMs_ = 0, lastPingMs_ = 0;

    // Artificial receive jitter for testing (TAK_NET_JITTER_MS): hold each bundle
    // then release it after a random 0..N ms delay, modelling uneven server->client
    // delivery. Test-only; does not touch command/sim state.
    struct HeldBundle { uint64_t releaseMs; uint32_t tick; Bundle bd; };
    std::vector<HeldBundle> jitterHeld_;
    int jitterMs_ = -1;                 // <0 = read env once; then 0 = off
    int baseMs_ = 0;                    // TAK_NET_BASE_MS: constant one-way delay
    int lossPct_ = 0;                   // TAK_NET_LOSS_PCT: retransmit-spike probability
    uint32_t rng_ = 0x2545F491u;        // xorshift (non-sim, non-determinism OK)
    uint32_t xorshift() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }
    // Modelled one-way receive delay for a message: base + uniform jitter, plus an
    // occasional ~2*base retransmit spike (TCP loss). 0 when no model is configured.
    uint64_t receiveDelayMs() {
        if (baseMs_ <= 0 && jitterMs_ <= 0 && lossPct_ <= 0) return 0;
        uint64_t d = uint64_t(baseMs_) + (jitterMs_ > 0 ? xorshift() % uint32_t(jitterMs_ + 1) : 0);
        if (lossPct_ > 0 && int(xorshift() % 100) < lossPct_)
            d += uint64_t(baseMs_) * 2 + uint64_t(jitterMs_);   // retransmit + head-of-line
        return d;
    }
    // Round-trip time, measured with active pings when a consumer asks for it
    // (the adaptive jitter buffer). Off by default -> no extra traffic.
    bool measureRtt_ = false;
    uint64_t pingSentMs_ = 0;           // outstanding RTT ping send time (0 = none)
    float rttMs_ = 0;                   // smoothed round-trip time
    // Bundle-arrival jitter: how much later than the 30 Hz schedule bundles land,
    // as a slowly-decaying recent max. This is what the jitter buffer must cover.
    uint64_t lastBundleMs_ = 0;
    float arrivalJitterMs_ = 0;
    void noteBundleArrival(uint64_t now) {
        if (lastBundleMs_) {
            float late = float(now - lastBundleMs_) - 1000.0f / 30.0f;
            arrivalJitterMs_ = std::max(std::max(0.0f, late), arrivalJitterMs_ - 0.4f);
        }
        lastBundleMs_ = now;
    }
public:
    float rttMs() const { return rttMs_; }
    float arrivalJitterMs() const { return arrivalJitterMs_; }
    void enableRttProbe() { measureRtt_ = true; }
private:
};

}  // namespace tak::net
