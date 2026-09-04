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
    void createGame(const std::string& name, const std::string& password,
                    const std::string& mapId, const GameOptions& opts, uint8_t capacity);
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

    uint32_t startSeed_ = 0;
    uint64_t resumeToken_ = 0;
    bool rejoin_ = false;
    bool expectingRejoin_ = false;
    bool paused_ = false;
    std::map<uint32_t, Bundle> bundles_;
    bool desynced_ = false;
    std::string desyncReason_;
    uint64_t lastRecvMs_ = 0, lastPingMs_ = 0;
};

}  // namespace tak::net
