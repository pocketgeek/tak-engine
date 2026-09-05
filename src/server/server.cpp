// takserver: the central multiplayer server (docs/multiplayer-design.md, M3).
//
// A single-threaded poll loop hosts a lobby and any number of games. Each game
// is a server-sequenced deterministic lockstep: clients send commands, the
// server buckets them per tick, closes one tick every 1/30 s, and broadcasts a
// TickBundle to every client in the game. Clients advance their sims in step.
//
// With --data the server runs a REFEREE sim (a headless World built by the same
// setupMatch the clients use) that also hosts the AI players: each tick the AI
// controllers append their orders to the bundle, and the referee's own state
// hash is the canonical one clients are checked against (with a suspicion rule
// that blames the referee if every client agrees against it). Without --data the
// server is a pure relay and clients cross-check hashes among themselves (M3).

#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai/ai.h"
#include "hpi/hpi.h"
#include "net/conn.h"
#include "net/protocol.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"

using namespace tak::net;

namespace {

uint64_t nowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000 + uint64_t(ts.tv_nsec) / 1000000;
}

constexpr uint64_t kPingIdleMs = 5000;    // ping a quiet client after this
constexpr uint64_t kTimeoutMs = 15000;    // drop a silent client after this
constexpr int kCmdCapPerTick = 64;        // per-client command cap per tick
uint64_t kGraceMs = 300000;     // hold a dropped slot this long (5 min)
uint64_t kPauseBudgetMs = 120000;  // total auto-pause a player may cause

// A resume token (real entropy; not sim state so no determinism concern).
uint64_t randToken() {
    uint64_t v = 0;
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (f) { if (std::fread(&v, sizeof v, 1, f) != 1) v = 0; std::fclose(f); }
    if (v == 0) v = nowMs() * 6364136223846793005ULL + 1442695040888963407ULL;
    return v ? v : 1;
}

struct Client {
    Conn conn;
    uint32_t id = 0;
    std::string name;
    enum State { Handshake, Lobby, InGame } state = Handshake;
    uint32_t roomId = 0;
    int slot = -1;
    uint64_t lastRecvMs = 0;
    uint64_t lastPingMs = 0;
    bool loaded = false;
};

struct Room {
    uint32_t id = 0;
    std::string name, password, mapId;
    GameOptions opts;
    uint32_t hostId = 0;
    SlotInfo slots[kMaxSlots];
    int slotClient[kMaxSlots];      // client id in each slot, -1 = none/ai/open
    std::vector<uint32_t> spectators;   // watching, not seated (no slot, no hash)
    bool running = false;
    int cap = kMaxSlots;            // map capacity (from the host's CreateGame)
    uint64_t createdMs = 0;
    // running state
    uint32_t tick = 0;                          // next tick to close
    std::vector<Command> pending;               // commands for the next tick
    std::map<uint32_t, std::vector<Command>> pendingAt;  // server input-delay: cmds bucketed by future tick
    std::vector<Event> pendingEvents;           // sequenced events for the next tick
    uint64_t nextTickMs = 0;                    // wall deadline for the next tick
    // hash cross-check: tick -> (clientId -> hash)
    std::map<uint32_t, std::map<uint32_t, uint64_t>> hashes;
    std::map<uint32_t, bool> desyncFlagged;     // clientId -> already told
    // referee sim (server-side, drives server-hosted AI + a canonical hash)
    std::unique_ptr<tak::sim::World> ref;
    const tak::sim::TypeRegistry* reg = nullptr;   // the balance this game uses
    std::vector<tak::ai::Controller> ai;        // one per AI slot
    std::map<uint32_t, uint64_t> refHash;       // tick -> referee hash (bounded ring)
    bool refSuspect = false;                    // referee itself suspected desynced
    // durability (M5): the full bundle log for reconnect/replay, per-slot resume
    // tokens, and drop-hold / auto-pause state.
    std::vector<std::vector<uint8_t>> log;      // serialized TickBundle payload per tick
    SlotInfo startSlots[kMaxSlots];             // slot config at game start (for the replay)
    uint64_t slotToken[kMaxSlots] = {};         // resume token per human slot (0 = none)
    bool slotDropped[kMaxSlots] = {};           // slot held after a mid-game disconnect
    uint64_t graceDeadline[kMaxSlots] = {};     // forfeit time for a dropped slot
    bool paused = false;                        // a drop paused the game (ticks stop)
    int pausePlayer = -1;                       // which dropped player paused it
    uint64_t pauseStartMs = 0;                  // when the current pause began
    uint64_t pauseBudgetMs[kMaxSlots] = {};     // remaining pause budget per player
    Room() { for (int i = 0; i < kMaxSlots; ++i) slotClient[i] = -1; }
    bool anyDropped() const {
        for (int i = 0; i < kMaxSlots; ++i) if (slotDropped[i]) return true;
        return false;
    }
    int capacity() const { return cap; }
    int humanCount() const {
        int n = 0;
        for (int i = 0; i < kMaxSlots; ++i) if (slots[i].type == 1) ++n;
        return n;
    }
    int usedSlots() const {
        int n = 0;
        for (int i = 0; i < kMaxSlots; ++i) if (slots[i].type == 1 || slots[i].type == 2) ++n;
        return n;
    }
};

class Server {
public:
    void setReplayDir(const std::string& d) { replayDir_ = d; }
    Server(uint16_t port, const std::string& dataRoot) : port_(port), dataRoot_(dataRoot) {
        if (!dataRoot_.empty()) {
            // The referee reads the retail install directly. Full overrides so a
            // shared-install test agrees with clients; Phase 3 negotiates the policy.
            vfs_ = tak::hpi::mountRetailRoot(dataRoot_, tak::hpi::OverridePolicy::Full);
            tak::sim::setupRegistry(registry_, vfs_, false);
            if (!vfs_.list("unitscb").empty()) {
                tak::sim::setupRegistry(registryCb_, vfs_, true);
                haveCb_ = true;
            }
            aiProfile_ = tak::ai::loadProfile(vfs_);
            dataHash_ = tak::hpi::gameplayHash(vfs_);
            haveData_ = true;
            std::fprintf(stderr, "takserver: loaded game data from %s (referee sim + AI enabled%s), "
                         "gameplay hash %016llx\n",
                         dataRoot_.c_str(), haveCb_ ? ", +Crusades" : "",
                         (unsigned long long)dataHash_);
        }
    }
    int run();

private:
    uint16_t port_;
    std::string dataRoot_;
    tak::hpi::Vfs vfs_;
    uint64_t dataHash_ = 0;             // referee's gameplay-data fingerprint (--data)
    uint64_t relayHash_ = 0;            // pure-relay: the first client's hash (peers must match)
    bool relayHashSet_ = false;
    bool haveData_ = false, haveCb_ = false;
    tak::sim::TypeRegistry registry_, registryCb_;
    tak::ai::Profile aiProfile_;
    const tak::sim::TypeRegistry& registryFor(bool crusades) const {
        return (crusades && haveCb_) ? registryCb_ : registry_;
    }
    std::string replayDir_;
    int listenFd_ = -1;
    uint32_t nextClientId_ = 1, nextRoomId_ = 1;
    std::unordered_map<uint32_t, std::unique_ptr<Client>> clients_;
    std::map<uint32_t, Room> rooms_;

    void onFrame(Client& c, const Frame& f);
    void handshake(Client& c, const Frame& f);
    void lobbyMsg(Client& c, const Frame& f);
    void gameMsg(Client& c, const Frame& f);

    void sendReject(Client& c, const std::string& why);
    void sendGameList(Client& c);
    void broadcastLobby(Room& r);
    void broadcastRoom(Room& r, Msg kind, const Writer& w, uint32_t exceptClient = 0);
    Room* roomOf(Client& c) { auto it = rooms_.find(c.roomId); return it == rooms_.end() ? nullptr : &it->second; }
    void leaveRoom(Client& c, const char* reason);
    void tryStart(Client& c);
    void closeTick(Room& r);
    void checkHashes(Room& r, uint32_t tick);
    void dropClient(uint32_t id, const char* reason);
    void writeSlots(Writer& w, Room& r);
    void writeReplay(Room& r);
};

// A running game is abandoned once no human slot has a live or held (dropped-
// within-grace) client -- then it can be torn down and its replay written.
static bool roomActive(const Room& r) {
    for (int i = 0; i < kMaxSlots; ++i)
        if (r.slots[i].type == 1 && (r.slotClient[i] >= 0 || r.slotDropped[i])) return true;
    return false;
}

void Server::writeReplay(Room& r) {
    if (replayDir_.empty() || r.log.empty()) return;
    // Self-contained replay: header (format, map, options, final slot table,
    // seed) + every tick bundle. A viewer can rebuild the world and play it back.
    Writer w;
    for (char ch : {'T', 'A', 'K', 'R'}) w.u8(uint8_t(ch));
    w.u32(1);                 // replay format version
    w.u32(kNetVersion);
    w.str(r.mapId);
    w.u8(r.opts.crusades); w.u8(r.opts.gods); w.u8(r.opts.forfeitSelfDestruct);
    w.u32(0x7a6b0000u + r.id);
    w.u8(uint8_t(kMaxSlots));
    for (int i = 0; i < kMaxSlots; ++i) {
        const SlotInfo& s = r.startSlots[i];   // start config, not the forfeited end state
        w.u8(s.type); w.u8(s.faction); w.u8(s.color); w.u8(s.team);
    }
    w.u32(uint32_t(r.log.size()));
    for (const auto& b : r.log) { w.u32(uint32_t(b.size())); w.b.insert(w.b.end(), b.begin(), b.end()); }
    std::string path = replayDir_ + "/game-" + std::to_string(r.id) + "-" +
                       std::to_string(r.createdMs) + ".takrep";
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(w.b.data(), 1, w.b.size(), f);
        std::fclose(f);
        std::fprintf(stderr, "game %u: wrote replay %s (%zu ticks, %zu bytes)\n",
                     r.id, path.c_str(), r.log.size(), w.b.size());
    }
}

void Server::sendReject(Client& c, const std::string& why) {
    Writer w; w.str(why);
    c.conn.send(Msg::Reject, w);
    std::fprintf(stderr, "reject client %u: %s\n", c.id, why.c_str());
}

void Server::handshake(Client& c, const Frame& f) {
    if (f.kind != Msg::Hello) { sendReject(c, "expected Hello"); c.conn.fail("no hello"); return; }
    Reader r(f.payload.data(), f.payload.size());
    uint32_t ver = r.u32();
    std::string build = r.str();
    uint64_t dataHash = r.u64();
    std::string name = r.str();
    (void)build; (void)dataHash;   // logged; clients are gated by version here
    if (!r.ok) { sendReject(c, "malformed hello"); c.conn.fail("bad hello"); return; }
    if (ver != kNetVersion) {
        sendReject(c, "protocol version mismatch (server " + std::to_string(kNetVersion) +
                       ", client " + std::to_string(ver) + ") -- update your build");
        c.conn.fail("version");
        return;
    }
    // Gameplay-data agreement: every peer must feed its sim byte-identical gameplay
    // data (verifies the retail files are unmodified, and that Full-override players
    // share the same overrides). With --data we hold the canonical referee hash;
    // as a pure relay we adopt the first client's and hold the rest to it.
    uint64_t want = haveData_ ? dataHash_ : (relayHashSet_ ? relayHash_ : dataHash);
    if (dataHash != want) {
        char msg[128];
        std::snprintf(msg, sizeof msg,
                      "game data mismatch (server %016llx, you %016llx) -- your retail "
                      "data or overrides differ", (unsigned long long)want,
                      (unsigned long long)dataHash);
        sendReject(c, msg);
        c.conn.fail("datahash");
        std::fprintf(stderr, "client %u rejected: data hash %016llx != %016llx\n",
                     c.id, (unsigned long long)dataHash, (unsigned long long)want);
        return;
    }
    if (!haveData_ && !relayHashSet_) { relayHash_ = dataHash; relayHashSet_ = true; }
    c.name = name.empty() ? ("player" + std::to_string(c.id)) : name;
    c.state = Client::Lobby;
    Writer w; w.u32(c.id); w.str(c.name);
    c.conn.send(Msg::Welcome, w);
    std::fprintf(stderr, "client %u '%s' joined lobby\n", c.id, c.name.c_str());
}

void Server::sendGameList(Client& c) {
    Writer w;
    w.u32(uint32_t(rooms_.size()));
    uint64_t t = nowMs();
    for (auto& [id, r] : rooms_) {
        w.u32(r.id);
        w.str(r.name);
        w.str(r.mapId);
        w.u8(uint8_t(r.usedSlots()));
        w.u8(uint8_t(r.capacity()));
        w.u8(r.running ? 1 : 0);
        w.u8(r.password.empty() ? 0 : 1);
        w.u32(uint32_t((t - r.createdMs) / 1000));
    }
    c.conn.send(Msg::GameList, w);
}

void Server::writeSlots(Writer& w, Room& r) {
    w.u32(r.id);
    w.str(r.name);
    w.str(r.mapId);
    w.u8(r.opts.crusades); w.u8(r.opts.gods); w.u8(r.opts.forfeitSelfDestruct);
    w.u32(r.hostId);
    for (int i = 0; i < kMaxSlots; ++i) {
        const SlotInfo& s = r.slots[i];
        w.u8(s.type); w.u8(s.faction); w.u8(s.color); w.u8(s.team); w.u8(s.ready);
        w.u8(uint8_t(r.hostId != 0 && r.slotClient[i] >= 0 &&
                     uint32_t(r.slotClient[i]) == r.hostId ? 1 : 0));
        w.str(s.name);
    }
}

void Server::broadcastLobby(Room& r) {
    Writer w; writeSlots(w, r);
    for (int i = 0; i < kMaxSlots; ++i) {
        if (r.slotClient[i] < 0) continue;
        auto it = clients_.find(uint32_t(r.slotClient[i]));
        if (it != clients_.end()) it->second->conn.send(Msg::LobbyState, w);
    }
    // Lobby spectators (e.g. a host who created the game to watch) see the room
    // fill too -- and this is how they learn hostId, so they can start it.
    for (uint32_t sid : r.spectators) {
        auto it = clients_.find(sid);
        if (it != clients_.end()) it->second->conn.send(Msg::LobbyState, w);
    }
}

void Server::broadcastRoom(Room& r, Msg kind, const Writer& w, uint32_t exceptClient) {
    for (int i = 0; i < kMaxSlots; ++i) {
        if (r.slotClient[i] < 0) continue;
        uint32_t cid = uint32_t(r.slotClient[i]);
        if (cid == exceptClient) continue;
        auto it = clients_.find(cid);
        if (it != clients_.end()) it->second->conn.send(kind, w);
    }
    // Spectators receive the same stream (bundles, chat, pause/resume, status).
    for (uint32_t cid : r.spectators) {
        if (cid == exceptClient) continue;
        auto it = clients_.find(cid);
        if (it != clients_.end()) it->second->conn.send(kind, w);
    }
}

void Server::lobbyMsg(Client& c, const Frame& f) {
    switch (f.kind) {
        case Msg::ListGames: sendGameList(c); break;
        case Msg::CreateGame: {
            Reader r(f.payload.data(), f.payload.size());
            std::string name = r.str(), pass = r.str(), mapId = r.str();
            GameOptions o; o.crusades = r.u8(); o.gods = r.u8(); o.forfeitSelfDestruct = r.u8();
            int cap = int(r.u8());
            uint8_t spectate = r.u8();   // host watches, taking no slot (all-AI game)
            if (!r.ok) return;
            if (cap < 2 || cap > kMaxSlots) cap = kMaxSlots;   // sane default
            Room& room = rooms_[nextRoomId_];
            room.id = nextRoomId_++;
            room.name = name.empty() ? ("game" + std::to_string(room.id)) : name;
            room.password = pass;
            room.mapId = mapId;
            room.opts = o;
            room.cap = cap;
            room.hostId = c.id;
            room.createdMs = nowMs();
            // Open slots up to the map capacity, close the rest (the map has no
            // start position for them).
            for (int i = 0; i < kMaxSlots; ++i) {
                room.slots[i].type = i < cap ? 0 : 3;
                room.slots[i].color = uint8_t(i);
                room.slots[i].team = uint8_t(i);
            }
            if (spectate) {
                // Host watches without a slot -- every capacity slot stays open (fill
                // with AIs). The host still owns the room (add AIs, start).
                room.spectators.push_back(c.id);
                c.state = Client::InGame; c.roomId = room.id; c.slot = -1;
                Writer jr; jr.u8(1); jr.u8(0xFF); jr.str("");   // ok, spectator
                c.conn.send(Msg::JoinResult, jr);
            } else {
                // Host takes slot 0 (human).
                room.slots[0].type = 1; room.slots[0].faction = 0; room.slots[0].color = 0;
                room.slots[0].team = 0; room.slots[0].name = c.name;
                room.slotClient[0] = int(c.id);
                c.state = Client::InGame; c.roomId = room.id; c.slot = 0;
                Writer jr; jr.u8(1); jr.u8(0); jr.str("");   // ok, slot 0
                c.conn.send(Msg::JoinResult, jr);
            }
            broadcastLobby(room);
            std::fprintf(stderr, "client %u created game %u '%s'\n", c.id, room.id, room.name.c_str());
            break;
        }
        case Msg::JoinGame: {
            Reader r(f.payload.data(), f.payload.size());
            uint32_t gid = r.u32(); std::string pass = r.str();
            if (!r.ok) return;
            auto it = rooms_.find(gid);
            auto reject = [&](const std::string& why) {
                Writer jr; jr.u8(0); jr.u8(0); jr.str(why); c.conn.send(Msg::JoinResult, jr);
            };
            if (it == rooms_.end()) { reject("no such game"); break; }
            Room& room = it->second;
            if (room.running) { reject("game already started"); break; }
            if (!room.password.empty() && room.password != pass) { reject("wrong password"); break; }
            int freeSlot = -1;
            for (int i = 0; i < kMaxSlots; ++i)
                if (room.slots[i].type == 0) { freeSlot = i; break; }
            if (freeSlot < 0) { reject("game is full"); break; }
            room.slots[freeSlot].type = 1;
            room.slots[freeSlot].name = c.name;
            room.slotClient[freeSlot] = int(c.id);
            c.state = Client::InGame; c.roomId = room.id; c.slot = freeSlot;
            Writer jr; jr.u8(1); jr.u8(uint8_t(freeSlot)); jr.str("");
            c.conn.send(Msg::JoinResult, jr);
            broadcastLobby(room);
            std::fprintf(stderr, "client %u joined game %u at slot %d\n", c.id, room.id, freeSlot);
            break;
        }
        case Msg::Rejoin: {
            Reader r(f.payload.data(), f.payload.size());
            uint32_t gid = r.u32(); uint64_t token = r.u64();
            if (!r.ok) return;
            auto it = rooms_.find(gid);
            auto reject = [&](const std::string& why) {
                Writer jr; jr.u8(0); jr.u8(0); jr.str(why); c.conn.send(Msg::JoinResult, jr);
            };
            if (it == rooms_.end() || !it->second.running) { reject("game not found or ended"); break; }
            Room& room = it->second;
            int slot = -1;
            for (int i = 0; i < kMaxSlots; ++i)
                if (room.slotDropped[i] && token != 0 && room.slotToken[i] == token) { slot = i; break; }
            if (slot < 0) { reject("invalid or expired resume token"); break; }
            // Re-seat the client and rotate the token (single use).
            room.slotClient[slot] = int(c.id);
            room.slotDropped[slot] = false;
            room.slotToken[slot] = randToken();
            room.desyncFlagged.clear();   // fresh sim; old desync flags are stale
            c.state = Client::InGame; c.roomId = gid; c.slot = slot; c.loaded = true;
            // GameStarting rebuilds the client's world; then the whole bundle log
            // replays it up to now, after which it receives live bundles.
            Writer w; writeSlots(w, room);
            w.u8(uint8_t(slot)); w.u32(0x7a6b0000u + room.id); w.u64(room.slotToken[slot]);
            c.conn.send(Msg::GameStarting, w);
            for (const auto& bundle : room.log) c.conn.send(Msg::TickBundle, bundle);
            // Unpause if this was the player we were waiting on.
            if (room.paused && room.pausePlayer == slot) {
                room.pauseBudgetMs[slot] -= std::min(room.pauseBudgetMs[slot], nowMs() - room.pauseStartMs);
                room.paused = false; room.pausePlayer = -1;
                room.nextTickMs = nowMs();   // rebase the tick clock (no burst)
                Writer rw; rw.u8(0); rw.u8(uint8_t(slot));
                broadcastRoom(room, Msg::Resume, rw);
            }
            Writer ps; ps.u8(uint8_t(slot)); ps.u8(0 /*connected*/); ps.u32(0);
            broadcastRoom(room, Msg::PlayerStatus, ps, c.id);
            std::fprintf(stderr, "game %u: client %u REJOINED slot %d (replaying %zu ticks)\n",
                         room.id, c.id, slot, room.log.size());
            break;
        }
        case Msg::Spectate: {
            Reader r(f.payload.data(), f.payload.size());
            uint32_t gid = r.u32(); std::string pass = r.str();
            if (!r.ok) return;
            auto it = rooms_.find(gid);
            auto reject = [&](const std::string& why) {
                Writer jr; jr.u8(0); jr.u8(0); jr.str(why); c.conn.send(Msg::JoinResult, jr);
            };
            if (it == rooms_.end() || !it->second.running) { reject("game not found or ended"); break; }
            Room& room = it->second;
            if (!room.password.empty() && room.password != pass) { reject("wrong password"); break; }
            // Seat nothing: a spectator has no slot, sends no commands or hashes,
            // and holds nothing on disconnect. It just receives the stream.
            if (std::find(room.spectators.begin(), room.spectators.end(), c.id) == room.spectators.end())
                room.spectators.push_back(c.id);
            c.state = Client::InGame; c.roomId = gid; c.slot = -1; c.loaded = true;
            // GameStarting (slot 0xFF = spectator) rebuilds the world; the whole
            // bundle log replays it to now, then live bundles stream via broadcast.
            Writer w; writeSlots(w, room);
            w.u8(0xFF); w.u32(0x7a6b0000u + room.id); w.u64(0);
            c.conn.send(Msg::GameStarting, w);
            for (const auto& bundle : room.log) c.conn.send(Msg::TickBundle, bundle);
            std::fprintf(stderr, "game %u: client %u SPECTATING (replaying %zu ticks)\n",
                         room.id, c.id, room.log.size());
            break;
        }
        default: break;   // ignore other messages while in the lobby
    }
}

void Server::leaveRoom(Client& c, const char* reason) {
    Room* r = roomOf(c);
    if (!r) return;
    // A spectator just detaches from the stream -- no slot, nothing to forfeit.
    {
        auto& sp = r->spectators;
        auto it = std::find(sp.begin(), sp.end(), c.id);
        if (it != sp.end()) {
            sp.erase(it);
            c.state = Client::Lobby; c.roomId = 0; c.slot = -1; c.loaded = false;
            std::fprintf(stderr, "client %u stopped spectating game %u (%s)\n", c.id, r->id, reason);
            return;
        }
    }
    if (c.slot >= 0 && c.slot < kMaxSlots) {
        r->slotClient[c.slot] = -1;
        if (!r->running) { r->slots[c.slot].type = 0; r->slots[c.slot].name.clear(); }
        else {
            // Voluntary leave mid-game = immediate forfeit (sequenced event so the
            // sim disposes the units in lockstep).
            r->slots[c.slot].type = 3;
            r->pendingEvents.push_back({tak::net::Event::Kind::Leave, uint8_t(c.slot)});
        }
    }
    uint32_t rid = r->id;
    bool wasHost = (r->hostId == c.id);
    c.state = Client::Lobby; c.roomId = 0; c.slot = -1; c.loaded = false;
    // If the host left in the lobby, pass host to the next human (or dissolve).
    if (!r->running && wasHost) {
        int next = -1;
        for (int i = 0; i < kMaxSlots; ++i)
            if (r->slotClient[i] >= 0) { next = r->slotClient[i]; break; }
        if (next >= 0) r->hostId = uint32_t(next);
        else { rooms_.erase(rid); std::fprintf(stderr, "game %u dissolved\n", rid); return; }
    }
    if (r->slotClient) broadcastLobby(*r);
    std::fprintf(stderr, "client %u left game %u (%s)\n", c.id, rid, reason);
}

void Server::tryStart(Client& c) {
    Room* r = roomOf(c);
    if (!r || r->hostId != c.id || r->running) return;
    // Validate: >=2 used slots, every human ready, unique colors among used slots.
    if (r->usedSlots() < 2) return;
    bool usedColor[10] = {};
    for (int i = 0; i < kMaxSlots; ++i) {
        const SlotInfo& s = r->slots[i];
        if (s.type != 1 && s.type != 2) continue;
        if (s.type == 1 && !s.ready) return;             // a human isn't ready
        if (s.color < 10) { if (usedColor[s.color]) return; usedColor[s.color] = true; }
    }
    // AI slots require the server to have game data (referee sim).
    bool anyAi = false;
    for (int i = 0; i < kMaxSlots; ++i) if (r->slots[i].type == 2) anyAi = true;
    if (anyAi && !haveData_) return;   // can't host AI without --data
    r->running = true;
    r->tick = 0;
    r->nextTickMs = nowMs();
    for (int i = 0; i < kMaxSlots; ++i) r->startSlots[i] = r->slots[i];   // for the replay
    // Build the referee sim (and AI controllers) if we have game data. The world
    // is built by the SAME setupMatch the clients use, so its hash is canonical.
    std::string mapPath = haveData_ ? tak::hpi::findMap(vfs_, r->mapId) : std::string();
    if (haveData_ && mapPath.empty())
        std::fprintf(stderr, "takserver: map '%s' not found in data; no referee sim\n",
                     r->mapId.c_str());
    if (haveData_ && !mapPath.empty()) {
        int maxSlot = 0;
        for (int i = 0; i < kMaxSlots; ++i)
            if (r->slots[i].type == 1 || r->slots[i].type == 2) maxSlot = i;
        tak::sim::MatchConfig cfg;
        cfg.vfs = &vfs_;
        cfg.mapPath = mapPath;
        cfg.gods = r->opts.gods != 0;
        cfg.slots.resize(size_t(maxSlot + 1));
        for (int i = 0; i <= maxSlot; ++i) {
            const auto& s = r->slots[i];
            cfg.slots[size_t(i)] = {s.type == 1 || s.type == 2, s.faction % 5, s.team};
        }
        r->reg = &registryFor(r->opts.crusades != 0);
        r->ref = std::make_unique<tak::sim::World>();
        r->ref->setVisPlayer(-1);   // headless referee: no fog pass
        tak::sim::setupMatch(*r->ref, *r->reg, cfg);
        r->ai.reserve(size_t(maxSlot + 1));
        for (int i = 0; i <= maxSlot; ++i)
            if (r->slots[i].type == 2)
                r->ai.emplace_back(i, *r->reg, aiProfile_, 0x7a6b0000u + r->id);
    }
    // GameStarting: final slot table + options + seed + a per-slot resume token
    // (used to rejoin the held slot after a disconnect).
    for (int i = 0; i < kMaxSlots; ++i) {
        r->pauseBudgetMs[i] = kPauseBudgetMs;
        if (r->slotClient[i] < 0) continue;
        r->slotToken[i] = randToken();
        auto it = clients_.find(uint32_t(r->slotClient[i]));
        if (it == clients_.end()) continue;
        Writer w; writeSlots(w, *r);
        w.u8(uint8_t(i));                 // your slot
        w.u32(0x7a6b0000u + r->id);       // per-game RNG seed base
        w.u64(r->slotToken[i]);           // resume token
        it->second->conn.send(Msg::GameStarting, w);
        it->second->loaded = false;
    }
    // Spectators (incl. a host who created the game to watch) get a slot-less
    // GameStarting; they don't gate the first tick, so mark them loaded.
    for (uint32_t sid : r->spectators) {
        auto it = clients_.find(sid);
        if (it == clients_.end()) continue;
        Writer w; writeSlots(w, *r);
        w.u8(0xFF); w.u32(0x7a6b0000u + r->id); w.u64(0);
        it->second->conn.send(Msg::GameStarting, w);
        it->second->loaded = true;
    }
    std::fprintf(stderr, "game %u starting with %d players\n", r->id, r->usedSlots());
}

void Server::gameMsg(Client& c, const Frame& f) {
    Room* r = roomOf(c);
    if (!r) return;
    switch (f.kind) {
        case Msg::SlotUpdate: {
            Reader rd(f.payload.data(), f.payload.size());
            int slot = int(rd.u8());
            uint8_t type = rd.u8(), faction = rd.u8(), color = rd.u8(), team = rd.u8(), ready = rd.u8();
            if (!rd.ok || slot < 0 || slot >= kMaxSlots || r->running) return;
            bool isHost = (r->hostId == c.id);
            // A player edits only their own slot; the host may edit any.
            if (!isHost && slot != c.slot) return;
            SlotInfo& s = r->slots[slot];
            // Non-host can only touch faction/color/team/ready on their own slot.
            // Slots past the map capacity stay closed (no start position for them).
            if (isHost) { if (type <= 3 && !(slot >= r->cap && type != 3)) s.type = type; }
            s.faction = faction % 5;
            s.color = color % 10;
            s.team = uint8_t(team % kMaxSlots);
            s.ready = ready ? 1 : 0;
            broadcastLobby(*r);
            break;
        }
        case Msg::Kick: {
            if (r->hostId != c.id || r->running) return;
            Reader rd(f.payload.data(), f.payload.size());
            int slot = int(rd.u8());
            if (!rd.ok || slot < 0 || slot >= kMaxSlots) return;
            int cid = r->slotClient[slot];
            if (cid >= 0 && uint32_t(cid) != c.id) {
                auto it = clients_.find(uint32_t(cid));
                if (it != clients_.end()) { leaveRoom(*it->second, "kicked"); it->second->conn.send(Msg::Bye, Writer{}); }
            }
            break;
        }
        case Msg::StartGame: tryStart(c); break;
        case Msg::Loaded:
            c.loaded = true;
            break;
        case Msg::LeaveGame: leaveRoom(c, "left"); break;
        case Msg::Chat: {
            Reader rd(f.payload.data(), f.payload.size());
            std::string text = rd.str();
            if (!rd.ok || text.size() > 512) return;
            Writer w; w.str(c.name); w.str(text);
            broadcastRoom(*r, Msg::Chat, w);
            break;
        }
        case Msg::PlayerCommands: {
            if (!r->running) return;
            Reader rd(f.payload.data(), f.payload.size());
            uint32_t n = rd.u32();
            if (n > uint32_t(kCmdCapPerTick)) n = kCmdCapPerTick;   // drop the excess
            // Server-side input delay (TAK_SRV_DELAY=K, default 0): bucket incoming
            // commands K ticks into the future instead of the very next tick, so a
            // command never "just misses" a tick boundary. Costs K ticks of latency.
            static const int srvDelay = [] {
                const char* e = std::getenv("TAK_SRV_DELAY"); return e ? std::max(0, std::atoi(e)) : 0;
            }();
            for (uint32_t i = 0; i < n && rd.ok; ++i) {
                Command cmd = rd.cmd();
                if (!rd.ok) break;
                cmd.player = uint8_t(c.slot);   // server stamps ownership
                if (srvDelay > 0) r->pendingAt[r->tick + uint32_t(srvDelay)].push_back(cmd);
                else r->pending.push_back(cmd);
            }
            break;
        }
        case Msg::StateHash: {
            if (!r->running) return;
            Reader rd(f.payload.data(), f.payload.size());
            uint32_t tk = rd.u32(); uint64_t h = rd.u64();
            if (!rd.ok) return;
            r->hashes[tk][c.id] = h;
            checkHashes(*r, tk);
            break;
        }
        default: break;
    }
}

void Server::checkHashes(Room& r, uint32_t tick) {
    auto it = r.hashes.find(tick);
    if (it == r.hashes.end()) return;
    // Wait until every live human client in the room has reported this tick.
    int live = 0;
    for (int i = 0; i < kMaxSlots; ++i)
        if (r.slots[i].type == 1 && r.slotClient[i] >= 0) ++live;
    if (int(it->second.size()) < live || live == 0) return;

    // The canonical hash: the referee's for this tick if we ran a referee sim,
    // else the clients' majority (relay-only mode).
    bool haveRef = r.ref && r.refHash.count(tick);
    uint64_t canon;
    if (haveRef) {
        canon = r.refHash[tick];
        // Referee suspicion needs a client CONSENSUS to appeal against the server
        // sim: with >=2 clients all agreeing with each other but NOT the referee,
        // the server sim is the odd one out (a server bug) -- don't punish the
        // clients. With a single client there is no consensus, so the referee is
        // authoritative and a lone disagreeing client is simply desynced.
        if (live >= 2) {
            std::map<uint64_t, int> ctally;
            for (auto& [cid, h] : it->second) ++ctally[h];
            if (ctally.size() == 1 && it->second.begin()->second != canon && !r.refSuspect) {
                r.refSuspect = true;
                std::fprintf(stderr, "game %u: REFEREE SUSPECT at tick %u -- all %d clients agree "
                                     "with each other but disagree with the server sim; not dropping.\n",
                             r.id, tick, live);
            }
        }
        if (r.refSuspect) { r.hashes.erase(r.hashes.begin(), std::next(it)); return; }
    } else {
        std::map<uint64_t, int> tally;
        for (auto& [cid, h] : it->second) ++tally[h];
        int best = -1;
        for (auto& [h, cnt] : tally) if (cnt > best) { best = cnt; canon = h; }
    }
    for (auto& [cid, h] : it->second) {
        if (h != canon && !r.desyncFlagged[cid]) {
            r.desyncFlagged[cid] = true;
            auto ci = clients_.find(cid);
            if (ci != clients_.end()) {
                Writer w; w.u32(tick); w.str("desync detected (state diverged from the game)");
                ci->second->conn.send(Msg::Desynced, w);
                std::fprintf(stderr, "game %u: client %u DESYNCED at tick %u\n", r.id, cid, tick);
            }
        }
    }
    r.hashes.erase(r.hashes.begin(), std::next(it));   // drop this and older
}

void Server::closeTick(Room& r) {
    // All humans must have loaded before the first tick.
    if (r.tick == 0) {
        for (int i = 0; i < kMaxSlots; ++i)
            if (r.slots[i].type == 1 && r.slotClient[i] >= 0) {
                auto it = clients_.find(uint32_t(r.slotClient[i]));
                if (it == clients_.end() || !it->second->loaded) { r.nextTickMs = nowMs() + 100; return; }
            }
    }
    // Server-hosted AI: each controller observes the referee world (state after
    // tick-1) and appends its orders to this tick's bundle, exactly like a client.
    if (r.ref)
        for (auto& ctl : r.ai)
            ctl.tick(*r.ref, r.tick, [&r](const Command& c) { r.pending.push_back(c); });

    // Server input delay: client commands scheduled for THIS tick (received
    // srvDelay ticks ago) join the bundle now.
    if (auto it = r.pendingAt.find(r.tick); it != r.pendingAt.end()) {
        for (auto& c : it->second) r.pending.push_back(c);
        r.pendingAt.erase(it);
    }
    Writer w;
    w.u32(r.tick);
    // Deterministic order: sort by player, stable within a player (arrival order).
    std::stable_sort(r.pending.begin(), r.pending.end(),
                     [](const Command& a, const Command& b) { return a.player < b.player; });
    w.u32(uint32_t(r.pending.size()));
    for (const auto& cmd : r.pending) w.cmd(cmd);
    w.u32(uint32_t(r.pendingEvents.size()));
    for (const auto& e : r.pendingEvents) { w.u8(uint8_t(e.kind)); w.u8(e.player); }
    broadcastRoom(r, Msg::TickBundle, w);
    r.log.push_back(w.b);   // keep the full bundle log for reconnect/replay

    // Advance the referee sim by this same bundle, then record its canonical hash.
    if (r.ref) {
        for (const auto& cmd : r.pending) tak::sim::applyCommand(*r.ref, *r.reg, cmd);
        for (const auto& e : r.pendingEvents) tak::sim::applyEvent(*r.ref, e);
        r.ref->tick(1.0f / kServerHz);
        r.refHash[r.tick] = r.ref->stateHash();
        // bound the ring
        while (r.refHash.size() > 300) r.refHash.erase(r.refHash.begin());
    }
    r.pending.clear();
    r.pendingEvents.clear();
    r.tick++;
    r.nextTickMs += 1000 / kServerHz;
}

void Server::dropClient(uint32_t id, const char* reason) {
    auto it = clients_.find(id);
    if (it == clients_.end()) return;
    Client& c = *it->second;
    Room* r = c.roomId ? roomOf(c) : nullptr;
    if (r && r->running && c.slot >= 0 && c.slot < kMaxSlots && r->slots[c.slot].type == 1) {
        // A disconnect from a running game HOLDS the slot: the player may rejoin
        // with their resume token within the grace window. Auto-pause (budget
        // permitting) so nobody is fighting a frozen empire meanwhile.
        int s = c.slot;
        r->slotDropped[s] = true;
        r->slotClient[s] = -1;
        r->graceDeadline[s] = nowMs() + kGraceMs;
        if (r->pauseBudgetMs[s] > 0 && !r->paused) {
            r->paused = true; r->pausePlayer = s; r->pauseStartMs = nowMs();
            Writer pw; pw.u8(1 /*drop-grace*/); pw.u8(uint8_t(s));
            broadcastRoom(*r, Msg::Pause, pw);
        }
        Writer w; w.u8(uint8_t(s)); w.u8(1 /*dropped*/); w.u32(0);
        broadcastRoom(*r, Msg::PlayerStatus, w);
        std::fprintf(stderr, "game %u: client %u dropped -- slot %d held (grace %llus)%s\n",
                     r->id, id, s, (unsigned long long)(kGraceMs / 1000),
                     r->paused ? ", paused" : "");
    } else if (r) {
        leaveRoom(c, reason);
    }
    std::fprintf(stderr, "client %u dropped (%s)\n", id, reason);
    clients_.erase(it);
}

void Server::onFrame(Client& c, const Frame& f) {
    c.lastRecvMs = nowMs();
    if (f.kind == Msg::Ping) { c.conn.send(Msg::Pong); return; }
    if (f.kind == Msg::Pong) return;
    if (f.kind == Msg::Bye) { c.conn.fail("bye"); return; }
    switch (c.state) {
        case Client::Handshake: handshake(c, f); break;
        case Client::Lobby: lobbyMsg(c, f); break;
        case Client::InGame: gameMsg(c, f); break;
    }
}

int Server::run() {
    std::string err;
    listenFd_ = listenOn(port_, err);
    if (listenFd_ < 0) { std::fprintf(stderr, "takserver: %s on port %u\n", err.c_str(), port_); return 1; }
    std::fprintf(stderr, "takserver listening on port %u (protocol v%u)\n", port_, kNetVersion);

    for (;;) {
        // Build the pollfd set: listen + every client (POLLOUT when it has pending writes).
        std::vector<pollfd> pfds;
        std::vector<uint32_t> ids;
        pfds.push_back({listenFd_, POLLIN, 0});
        ids.push_back(0);
        for (auto& [id, c] : clients_) {
            short ev = POLLIN;
            if (c->conn.wantWrite()) ev |= POLLOUT;
            pfds.push_back({c->conn.fd(), ev, 0});
            ids.push_back(id);
        }
        // Timeout = time until the soonest running room's next tick (or 1s idle).
        uint64_t now = nowMs();
        uint64_t soonest = now + 1000;
        for (auto& [rid, r] : rooms_)
            if (r.running && r.nextTickMs < soonest) soonest = r.nextTickMs;
        int timeout = int(soonest > now ? soonest - now : 0);

        int n = ::poll(pfds.data(), pfds.size(), timeout);
        if (n < 0) { if (errno == EINTR) continue; break; }

        // Accept new connections.
        if (pfds[0].revents & POLLIN) {
            for (;;) {
                int fd = accept(listenFd_, nullptr, nullptr);
                if (fd < 0) break;
                setupSocket(fd);
                auto c = std::make_unique<Client>();
                c->id = nextClientId_++;
                c->conn = Conn(fd);
                c->lastRecvMs = nowMs();
                clients_[c->id] = std::move(c);
            }
        }
        // Service clients.
        std::vector<uint32_t> dead;
        for (size_t i = 1; i < pfds.size(); ++i) {
            uint32_t id = ids[i];
            auto it = clients_.find(id);
            if (it == clients_.end()) continue;
            Client& c = *it->second;
            if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (!c.conn.recv()) { dead.push_back(id); continue; }
                Frame fr;
                while (c.conn.poll(fr)) { onFrame(c, fr); if (!c.conn.ok()) break; }
            }
            if (!c.conn.ok()) { dead.push_back(id); continue; }
            if (pfds[i].revents & POLLOUT) c.conn.flushWrite();
        }
        // Grace / pause-budget expiry: a held slot that isn't reclaimed in time,
        // or a pause that outlasts its budget, forfeits the player. The forfeit is
        // a SEQUENCED event so every sim (and the replay) disposes their units on
        // the same tick.
        now = nowMs();
        for (auto& [rid, r] : rooms_) {
            if (!r.running) continue;
            bool budgetOut = r.paused && r.pausePlayer >= 0 &&
                             now - r.pauseStartMs >= r.pauseBudgetMs[r.pausePlayer];
            for (int i = 0; i < kMaxSlots; ++i) {
                if (!r.slotDropped[i]) continue;
                bool forfeit = now >= r.graceDeadline[i] ||
                               (budgetOut && i == r.pausePlayer);
                if (!forfeit) continue;
                r.slotDropped[i] = false;
                r.slots[i].type = 3;   // slot closed; the player is out
                r.pendingEvents.push_back({tak::net::Event::Kind::Forfeit, uint8_t(i)});
                std::fprintf(stderr, "game %u: player %d FORFEIT (grace/budget expired)\n", rid, i);
                if (r.paused && r.pausePlayer == i) {
                    r.paused = false; r.pausePlayer = -1; r.nextTickMs = now;
                    Writer rw; rw.u8(0); rw.u8(uint8_t(i));
                    broadcastRoom(r, Msg::Resume, rw);
                }
            }
        }
        // Tear down abandoned running games (everyone left/forfeited): write the
        // replay, then erase.
        std::vector<uint32_t> doneRooms;
        for (auto& [rid, r] : rooms_)
            if (r.running && !roomActive(r)) doneRooms.push_back(rid);
        for (uint32_t rid : doneRooms) {
            Room& r = rooms_.at(rid);
            writeReplay(r);
            // Detach any lingering spectators before the room vanishes: reset their
            // server-side state to Lobby (their client already shows the game's end
            // from the sim, and can browse/leave on its own).
            for (uint32_t sid : r.spectators) {
                auto it = clients_.find(sid);
                if (it == clients_.end()) continue;
                it->second->state = Client::Lobby;
                it->second->roomId = 0; it->second->slot = -1;
            }
            std::fprintf(stderr, "game %u ended (all players gone)\n", rid);
            rooms_.erase(rid);
        }
        // Close ticks for running, unpaused rooms whose deadline passed.
        now = nowMs();
        for (auto& [rid, r] : rooms_)
            while (r.running && !r.paused && r.nextTickMs <= now) closeTick(r);
        // Flush all pending writes (bundles just queued) + keepalive + timeouts.
        now = nowMs();
        for (auto& [id, c] : clients_) {
            if (!c->conn.flushWrite()) { dead.push_back(id); continue; }
            if (c->state != Client::Handshake && now - c->lastRecvMs > kPingIdleMs &&
                now - c->lastPingMs > kPingIdleMs) {
                c->conn.send(Msg::Ping); c->lastPingMs = now; c->conn.flushWrite();
            }
            if (now - c->lastRecvMs > kTimeoutMs) dead.push_back(id);
        }
        for (uint32_t id : dead) dropClient(id, "disconnected");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // Test overrides for the reconnect timers (seconds).
    if (const char* g = std::getenv("TAK_GRACE_MS")) kGraceMs = uint64_t(std::atoll(g));
    if (const char* b = std::getenv("TAK_PAUSE_BUDGET_MS")) kPauseBudgetMs = uint64_t(std::atoll(b));
    uint16_t port = 7677;
    std::string dataRoot, replayDir;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--port") && i + 1 < argc) port = uint16_t(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) dataRoot = argv[++i];
        else if (!std::strcmp(argv[i], "--replaydir") && i + 1 < argc) replayDir = argv[++i];
        else if (!std::strcmp(argv[i], "--help")) {
            std::printf("usage: takserver [--port N] [--data <retail-install-dir>] "
                        "[--replaydir <dir>]\n"
                        "  --data enables the referee sim + server-hosted AI; without it the\n"
                        "  server is a pure relay (clients cross-check hashes among themselves).\n"
                        "  --replaydir writes a .takrep replay file per finished game.\n");
            return 0;
        }
    }
    Server s(port, dataRoot);
    if (!replayDir.empty()) s.setReplayDir(replayDir);
    return s.run();
}
