#include "net/client.h"

#include <time.h>
#include <algorithm>
#include <cstdlib>

namespace tak::net {

namespace {
uint64_t nowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000 + uint64_t(ts.tv_nsec) / 1000000;
}
constexpr uint64_t kPingIdleMs = 5000, kTimeoutMs = 15000;
}  // namespace

bool MpClient::connect(const std::string& host, uint16_t port, const std::string& name) {
    name_ = name;
    if (!conn_.connect(host, port)) { err_ = conn_.error(); state_ = State::Done; return false; }
    state_ = State::Connecting;
    Writer w;
    w.u32(kNetVersion);
    w.str("");          // build id (version-gated only)
    w.u64(dataHash_);   // gameplay-data fingerprint (enforced by the server)
    w.str(name_);
    send(Msg::Hello, w);
    lastRecvMs_ = nowMs();
    return true;
}

void MpClient::disconnect(const std::string& reason) {
    if (conn_.ok()) { Writer w; w.str(reason); send(Msg::Bye, w); conn_.flushWrite(); }
    conn_.closeNow();
    state_ = State::Done;
}

bool MpClient::poll() {
    if (state_ == State::Offline || state_ == State::Done) return false;
    if (jitterMs_ < 0) {   // one-time init of the test link model
        const char* j = std::getenv("TAK_NET_JITTER_MS");
        const char* b = std::getenv("TAK_NET_BASE_MS");
        const char* l = std::getenv("TAK_NET_LOSS_PCT");
        jitterMs_ = j ? std::max(0, std::atoi(j)) : 0;
        baseMs_ = b ? std::max(0, std::atoi(b)) : 0;
        lossPct_ = l ? std::clamp(std::atoi(l), 0, 100) : 0;
    }
    if (!conn_.recv()) { err_ = conn_.error(); state_ = State::Done; return false; }
    Frame f;
    while (conn_.poll(f)) { onFrame(f); if (!conn_.ok()) break; }
    uint64_t now = nowMs();
    // Release any jitter-held bundles whose delay has elapsed.
    if (!jitterHeld_.empty()) {
        for (auto it = jitterHeld_.begin(); it != jitterHeld_.end();) {
            if (it->releaseMs <= now) { bundles_[it->tick] = std::move(it->bd);
                                        noteBundleArrival(now); it = jitterHeld_.erase(it); }
            else ++it;
        }
    }
    // Active RTT probe (only when a consumer enabled it): one ping/sec.
    if (measureRtt_ && !pingSentMs_ && now - lastPingMs_ > 1000) {
        send(Msg::Ping); lastPingMs_ = now; pingSentMs_ = now;
    }
    if (now - lastRecvMs_ > kPingIdleMs && now - lastPingMs_ > kPingIdleMs) {
        send(Msg::Ping); lastPingMs_ = now;
    }
    if (now - lastRecvMs_ > kTimeoutMs) { err_ = "server timeout"; state_ = State::Done; }
    if (!conn_.flushWrite()) { err_ = conn_.error(); state_ = State::Done; }
    if (!conn_.ok() && err_.empty()) { err_ = conn_.error(); state_ = State::Done; }
    return state_ != State::Done;
}

static void readSlots(Reader& r, RoomView& v) {
    v.id = r.u32();
    v.name = r.str();
    v.mapId = r.str();
    v.opts.crusades = r.u8(); v.opts.gods = r.u8(); v.opts.forfeitSelfDestruct = r.u8();
    v.opts.overridePolicy = r.u8();
    v.hostId = r.u32();
    for (int i = 0; i < kMaxSlots; ++i) {
        SlotInfo& s = v.slots[i];
        s.type = r.u8(); s.faction = r.u8(); s.color = r.u8(); s.team = r.u8();
        s.ready = r.u8(); s.isHost = r.u8(); s.name = r.str();
    }
    v.valid = r.ok;
}

void MpClient::onFrame(const Frame& f) {
    lastRecvMs_ = nowMs();
    Reader r(f.payload.data(), f.payload.size());
    switch (f.kind) {
        case Msg::Welcome:
            myId_ = r.u32();
            name_ = r.str();
            state_ = State::Lobby;
            break;
        case Msg::Reject:
            err_ = r.str();
            state_ = State::Done;
            break;
        case Msg::Ping: send(Msg::Pong); break;
        case Msg::Pong:
            if (pingSentMs_) {
                // Add the modelled return-path delay so RTT reflects the test link.
                float sample = float(nowMs() - pingSentMs_) + float(receiveDelayMs());
                rttMs_ = rttMs_ > 0 ? 0.7f * rttMs_ + 0.3f * sample : sample;
                pingSentMs_ = 0;
            }
            break;
        case Msg::Bye: err_ = r.str(); state_ = State::Done; break;
        case Msg::GameList: {
            games_.clear();
            uint32_t n = r.u32();
            for (uint32_t i = 0; i < n && r.ok; ++i) {
                GameInfo g;
                g.id = r.u32(); g.name = r.str(); g.mapId = r.str();
                g.players = r.u8(); g.capacity = r.u8(); g.running = r.u8();
                g.passworded = r.u8(); g.uptimeSec = r.u32();
                games_.push_back(g);
            }
            break;
        }
        case Msg::JoinResult: {
            uint8_t ok = r.u8(); uint8_t slot = r.u8(); std::string why = r.str();
            if (ok) {
                // 0xFF = the host created the game as a slot-less spectator.
                room_.mySlot = (slot == 0xFF) ? -1 : int(slot);
                spectator_ = (slot == 0xFF);
                state_ = State::InRoom;
            } else joinErr_ = why.empty() ? "join failed" : why;
            break;
        }
        case Msg::LobbyState: {
            int keep = room_.mySlot;
            readSlots(r, room_);
            room_.mySlot = keep;
            if (state_ == State::Lobby) state_ = State::InRoom;
            break;
        }
        case Msg::Chat: {
            std::string who = r.str(), text = r.str();
            if (r.ok) chat_.push_back({who, text});
            break;
        }
        case Msg::GameStarting: {
            int keep = room_.mySlot;
            readSlots(r, room_);
            uint8_t mySlot = r.u8();
            startSeed_ = r.u32();
            resumeToken_ = r.u64();
            // 0xFF marks a spectator (no slot); map it to -1.
            room_.mySlot = !r.ok ? keep : (mySlot == 0xFF ? -1 : int(mySlot));
            // Any 0xFF start is a spectator (a create-as-spectator host, or spectate()).
            spectator_ = expectingSpectate_ || (r.ok && mySlot == 0xFF);
            // A rejoin OR a spectate of a RUNNING game replays the bundle log; a
            // host-spectator whose game is just starting has no log to replay.
            rejoin_ = expectingRejoin_ || expectingSpectate_;
            expectingRejoin_ = expectingSpectate_ = false;
            state_ = State::Starting;
            break;
        }
        case Msg::Pause: paused_ = true; break;
        case Msg::Resume: paused_ = false; break;
        case Msg::TickBundle: {
            uint32_t tk = r.u32();
            Bundle bd;
            uint32_t nc = r.u32();
            for (uint32_t i = 0; i < nc && r.ok; ++i) bd.cmds.push_back(r.cmd());
            uint32_t ne = r.u32();
            for (uint32_t i = 0; i < ne && r.ok; ++i) {
                Event e; e.kind = Event::Kind(r.u8()); e.player = r.u8();
                bd.events.push_back(e);
            }
            if (r.ok) {
                uint64_t delay = receiveDelayMs();
                if (delay > 0) {   // model the link: hold, then release later
                    jitterHeld_.push_back({nowMs() + delay, tk, std::move(bd)});
                } else {
                    bundles_[tk] = std::move(bd);
                    noteBundleArrival(nowMs());
                }
                if (state_ == State::Starting) state_ = State::InGame;
            }
            break;
        }
        case Msg::Desynced: {
            uint32_t tk = r.u32(); std::string why = r.str();
            desynced_ = true;
            desyncReason_ = why + " (tick " + std::to_string(tk) + ")";
            break;
        }
        default: break;
    }
}

// ---- lobby actions --------------------------------------------------------

void MpClient::listGames() { send(Msg::ListGames); }

void MpClient::createGame(const std::string& name, const std::string& password,
                          const std::string& mapId, const GameOptions& o, uint8_t capacity,
                          bool spectate, bool priv) {
    Writer w; w.str(name); w.str(password); w.str(mapId);
    w.u8(o.crusades); w.u8(o.gods); w.u8(o.forfeitSelfDestruct); w.u8(o.overridePolicy);
    w.u8(capacity);   // map's start-position count (the server has no map data)
    w.u8(spectate ? 1 : 0);   // host watches, taking no slot
    w.u8(priv ? 1 : 0);       // private (single-player): not in the public game list
    send(Msg::CreateGame, w);
}

void MpClient::joinGame(uint32_t id, const std::string& password) {
    Writer w; w.u32(id); w.str(password);
    send(Msg::JoinGame, w);
}

void MpClient::leaveGame() {
    send(Msg::LeaveGame);
    room_ = RoomView{};
    state_ = State::Lobby;
}

void MpClient::setSlot(int slot, uint8_t type, uint8_t faction, uint8_t color,
                       uint8_t team, uint8_t ready) {
    Writer w; w.u8(uint8_t(slot)); w.u8(type); w.u8(faction); w.u8(color); w.u8(team); w.u8(ready);
    send(Msg::SlotUpdate, w);
}

void MpClient::kick(int slot) { Writer w; w.u8(uint8_t(slot)); send(Msg::Kick, w); }

void MpClient::chat(const std::string& text) { Writer w; w.str(text); send(Msg::Chat, w); }

void MpClient::startGame() { send(Msg::StartGame); }

// ---- game play ------------------------------------------------------------

void MpClient::reportLoaded(uint64_t dataHash) { Writer w; w.u64(dataHash); send(Msg::Loaded, w); }

void MpClient::rejoin(uint32_t gameId, uint64_t token) {
    expectingRejoin_ = true;
    Writer w; w.u32(gameId); w.u64(token);
    send(Msg::Rejoin, w);
}

void MpClient::spectate(uint32_t gameId, const std::string& password) {
    expectingSpectate_ = true;
    Writer w; w.u32(gameId); w.str(password);
    send(Msg::Spectate, w);
}

bool MpClient::takeBundle(uint32_t tick, Bundle& out) {
    auto it = bundles_.find(tick);
    if (it == bundles_.end()) return false;
    out = std::move(it->second);
    bundles_.erase(it);
    return true;
}

void MpClient::sendCommands(const std::vector<Command>& cmds) {
    if (cmds.empty()) return;
    Writer w; w.u32(uint32_t(cmds.size()));
    for (const auto& c : cmds) w.cmd(c);
    send(Msg::PlayerCommands, w);
}

void MpClient::sendHash(uint32_t tick, uint64_t hash) {
    Writer w; w.u32(tick); w.u64(hash);
    send(Msg::StateHash, w);
}

}  // namespace tak::net
