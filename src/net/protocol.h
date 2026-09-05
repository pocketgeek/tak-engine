#pragma once

// Wire protocol for the client-server multiplayer (docs/multiplayer-design.md).
// A central takserver relays a server-sequenced deterministic lockstep: clients
// send commands, the server assigns them to ticks and broadcasts one TickBundle
// per tick. This header is shared by the server and the client.
//
// Framing (kept from the old 2-peer lockstep): u32 LE length prefix (payload
// size + 1) then a u8 message kind then the payload. All integers little-endian.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "net/lockstep.h"   // Command / Cmd (the 35-byte command wire format is reused)

namespace tak::net {

constexpr uint32_t kNetVersion = 6;        // bumped for the client-server protocol
constexpr uint32_t kMaxFrame = 1u << 16;   // 64 KB frame cap (hardening)
constexpr int kMaxSlots = 8;               // players per game (= max map start positions)
constexpr int kServerHz = 30;              // sim/tick rate

// Message kinds. Lobby and game messages share one stream per connection.
enum class Msg : uint8_t {
    // handshake / session
    Hello = 1,          // C->S: version, buildId, dataHash, name
    Welcome,            // S->C: sessionId, your name accepted
    Reject,             // S->C: reason string (version/data mismatch, etc.)
    Ping, Pong,         // keepalive (either direction)
    Bye,                // clean shutdown, reason string
    // lobby
    ListGames,          // C->S
    GameList,           // S->C: list of GameInfo
    CreateGame,         // C->S: name, password, mapId, options
    JoinGame,           // C->S: gameId, password
    JoinResult,         // S->C: ok+slot or error reason
    LeaveGame,          // C->S
    SlotUpdate,         // C->S: slot edit (own, or any if host)
    LobbyState,         // S->C: full slot table + settings (broadcast)
    Chat,               // both: text
    Kick,               // C->S (host): slot
    StartGame,          // C->S (host)
    GameStarting,       // S->C: final setup + seeds + resume token
    Loaded,             // C->S: map/data loaded, ready to tick
    // in-game
    PlayerCommands,     // C->S: n commands (player byte overwritten by server)
    TickBundle,         // S->C: tick, commands (sorted), events
    StateHash,          // C->S: tick, hash
    Desynced,           // S->C: tick, reason
    PlayerStatus,       // S->C: slot, status, ping (informational)
    // durability (M5)
    Rejoin,             // C->S: gameId, resume token -> GameStarting + bundle log
    Pause,              // S->C: cause, player (game paused; ticks stop)
    Resume,             // S->C: cause, player (game resumes)
    Spectate,           // C->S: gameId, password -> GameStarting (slot 0xFF) + log
};

// A slot in a game's setup. type: 0=open, 1=human, 2=ai, 3=closed.
struct SlotInfo {
    uint8_t type = 0;
    uint8_t faction = 0;   // 0=ara 1=tar 2=ver 3=zon 4=cre
    uint8_t color = 0;     // 0..9 palette slot
    uint8_t team = 0;      // 0..kMaxSlots-1
    uint8_t ready = 0;
    uint8_t isHost = 0;
    std::string name;      // player display name ("" for open/ai)
};

// A game as seen in the browser.
struct GameInfo {
    uint32_t id = 0;
    std::string name;
    std::string mapId;
    uint8_t players = 0, capacity = 0;
    uint8_t running = 0;      // 0=lobby (joinable), 1=running (view only)
    uint8_t passworded = 0;
    uint32_t uptimeSec = 0;
};

// Per-game settings chosen by the host.
struct GameOptions {
    uint8_t crusades = 0;
    uint8_t gods = 0;
    uint8_t forfeitSelfDestruct = 0;   // 0=units go inert on forfeit, 1=self-destruct
};

// A sim-affecting server decision, sequenced inside a TickBundle so every peer
// (and the referee) applies it on the same tick and the log replays identically.
struct Event {
    enum class Kind : uint8_t { Forfeit = 1, Leave = 2 /* reserved: AllianceChange=3 */ };
    Kind kind = Kind::Forfeit;
    uint8_t player = 0;
};

// ------- little-endian byte buffer writer/reader ---------------------------

struct Writer {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void f32(float v) { uint32_t x; std::memcpy(&x, &v, 4); u32(x); }
    void str(const std::string& s) {              // u16 length + bytes (capped)
        uint32_t n = uint32_t(s.size() > 4096 ? 4096 : s.size());
        u8(uint8_t(n)); u8(uint8_t(n >> 8));
        b.insert(b.end(), s.begin(), s.begin() + n);
    }
    void cmd(const Command& c);
};

struct Reader {
    const uint8_t* p; const uint8_t* end;
    bool ok = true;
    Reader(const uint8_t* d, size_t n) : p(d), end(d + n) {}
    bool avail(size_t n) const { return size_t(end - p) >= n; }
    uint8_t u8() { if (!avail(1)) { ok = false; return 0; } return *p++; }
    uint32_t u32() { if (!avail(4)) { ok = false; return 0; } uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(*p++) << (8 * i); return v; }
    uint64_t u64() { if (!avail(8)) { ok = false; return 0; } uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(*p++) << (8 * i); return v; }
    float f32() { uint32_t x = u32(); float v; std::memcpy(&v, &x, 4); return v; }
    std::string str() {
        if (!avail(2)) { ok = false; return {}; }
        uint32_t n = uint32_t(p[0]) | (uint32_t(p[1]) << 8); p += 2;
        if (n > 4096 || !avail(n)) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p), n); p += n; return s;
    }
    Command cmd();
};

}  // namespace tak::net
