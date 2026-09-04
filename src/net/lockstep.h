#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tak::net {

// Two-player lockstep session over TCP. Both peers run identical sims;
// only player commands cross the wire. Commands issued at tick T execute
// at T + kInputDelay on both sides. A state hash is exchanged every
// kHashInterval ticks to detect desync.

constexpr uint32_t kProtocolVersion = 1;
constexpr uint32_t kInputDelay = 4;      // ticks
constexpr uint32_t kHashInterval = 30;   // ticks

enum class Cmd : uint8_t {
    Move, Attack, AttackMove, Patrol, Stop, Train, Build, Load, Unload, Guard,
    SetWeapon,     // targetId = weapon slot (0=primary, 1, 2)
    RepeatTrain,   // ctrl+click a conjure icon: toggle infinite production of `type`
};

struct Command {
    Cmd kind = Cmd::Move;
    uint8_t player = 0;
    int32_t unitId = 0;
    int32_t targetId = 0;
    float x = 0, z = 0;
    uint8_t queue = 0;
    char type[16] = {};   // unit type id for Train/Build
};

class Session {
public:
    ~Session();

    // Host: listen and wait for one peer (blocking, with timeout seconds).
    bool host(uint16_t port, int timeoutSec);
    // Join a host.
    bool join(const std::string& addr, uint16_t port);

    bool connected() const { return fd_ >= 0; }
    // 0 = host (player 0), 1 = client (player 1).
    int localPlayer() const { return local_; }

    // Queue a local command; it is sent scheduled for currentTick + delay.
    void issue(const Command& c) { pending_.push_back(c); }

    // Advance the network for tick `tick`: sends local commands scheduled
    // for tick + kInputDelay, then blocks (up to timeoutMs) until the
    // peer's commands for `tick` are available. Returns false on timeout
    // or disconnect. On success `out` holds both sides' commands for
    // `tick` in deterministic order (host's first).
    bool exchange(uint32_t tick, std::vector<Command>& out, int timeoutMs);

    // Report our state hash for a tick; returns false on confirmed desync.
    bool checkHash(uint32_t tick, uint64_t hash);

    std::string error() const { return error_; }

private:
    bool sendMsg(uint8_t kind, const std::vector<uint8_t>& payload);
    bool pump(int timeoutMs);   // read whatever is available

    int fd_ = -1;
    int listenFd_ = -1;
    int local_ = 0;
    std::string error_;
    std::vector<Command> pending_;

    struct TickCmds {
        uint32_t tick;
        std::vector<Command> cmds;
    };
    std::vector<TickCmds> localSched_, remoteSched_;
    std::vector<std::pair<uint32_t, uint64_t>> remoteHashes_;
    std::vector<uint8_t> rxBuf_;
};

} // namespace tak::net
