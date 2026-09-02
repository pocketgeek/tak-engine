#include "net/lockstep.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace tak::net {

namespace {

enum MsgKind : uint8_t { kHello = 1, kCommands = 2, kHash = 3 };

void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 24));
}
void put64(std::vector<uint8_t>& v, uint64_t x) {
    put32(v, uint32_t(x));
    put32(v, uint32_t(x >> 32));
}
uint32_t get32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
}
uint64_t get64(const uint8_t* p) {
    return uint64_t(get32(p)) | (uint64_t(get32(p + 4)) << 32);
}

void putCommand(std::vector<uint8_t>& v, const Command& c) {
    v.push_back(uint8_t(c.kind));
    v.push_back(c.player);
    put32(v, uint32_t(c.unitId));
    put32(v, uint32_t(c.targetId));
    uint32_t xb, zb;
    std::memcpy(&xb, &c.x, 4);
    std::memcpy(&zb, &c.z, 4);
    put32(v, xb);
    put32(v, zb);
    v.push_back(c.queue);
    v.insert(v.end(), c.type, c.type + 16);
}

constexpr size_t kCmdWire = 2 + 4 + 4 + 4 + 4 + 1 + 16;

Command getCommand(const uint8_t* p) {
    Command c;
    c.kind = Cmd(p[0]);
    c.player = p[1];
    c.unitId = int32_t(get32(p + 2));
    c.targetId = int32_t(get32(p + 6));
    uint32_t xb = get32(p + 10), zb = get32(p + 14);
    std::memcpy(&c.x, &xb, 4);
    std::memcpy(&c.z, &zb, 4);
    c.queue = p[18];
    std::memcpy(c.type, p + 19, 16);
    c.type[15] = 0;
    return c;
}

} // namespace

Session::~Session() {
    if (fd_ >= 0) close(fd_);
    if (listenFd_ >= 0) close(listenFd_);
}

bool Session::host(uint16_t port, int timeoutSec) {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) { error_ = "socket failed"; return false; }
    int one = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 ||
        listen(listenFd_, 1) != 0) {
        error_ = "bind/listen failed";
        return false;
    }
    pollfd pf{listenFd_, POLLIN, 0};
    if (poll(&pf, 1, timeoutSec * 1000) <= 0) {
        error_ = "no peer connected";
        return false;
    }
    fd_ = accept(listenFd_, nullptr, nullptr);
    if (fd_ < 0) { error_ = "accept failed"; return false; }
    int nd = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof nd);
    local_ = 0;
    std::vector<uint8_t> hello;
    put32(hello, kProtocolVersion);
    return sendMsg(kHello, hello);
}

bool Session::join(const std::string& addrStr, uint16_t port) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) { error_ = "socket failed"; return false; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, addrStr.c_str(), &addr.sin_addr) != 1) {
        error_ = "bad address";
        return false;
    }
    if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        error_ = "connect failed";
        return false;
    }
    int nd = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof nd);
    local_ = 1;
    std::vector<uint8_t> hello;
    put32(hello, kProtocolVersion);
    return sendMsg(kHello, hello);
}

bool Session::sendMsg(uint8_t kind, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame;
    put32(frame, uint32_t(payload.size() + 1));
    frame.push_back(kind);
    frame.insert(frame.end(), payload.begin(), payload.end());
    size_t off = 0;
    while (off < frame.size()) {
        ssize_t n = send(fd_, frame.data() + off, frame.size() - off, MSG_NOSIGNAL);
        if (n <= 0) { error_ = "send failed"; return false; }
        off += size_t(n);
    }
    return true;
}

bool Session::pump(int timeoutMs) {
    pollfd pf{fd_, POLLIN, 0};
    int r = poll(&pf, 1, timeoutMs);
    if (r < 0) { error_ = "poll failed"; return false; }
    if (r > 0) {
        uint8_t buf[65536];
        ssize_t n = recv(fd_, buf, sizeof buf, 0);
        if (n <= 0) { error_ = "peer disconnected"; return false; }
        rxBuf_.insert(rxBuf_.end(), buf, buf + n);
    }
    // Parse complete frames.
    size_t pos = 0;
    while (rxBuf_.size() - pos >= 4) {
        uint32_t len = get32(&rxBuf_[pos]);
        if (rxBuf_.size() - pos - 4 < len) break;
        const uint8_t* p = &rxBuf_[pos + 4];
        uint8_t kind = p[0];
        const uint8_t* body = p + 1;
        if (kind == kCommands && len >= 1 + 8) {
            uint32_t tick = get32(body);
            uint32_t n = get32(body + 4);
            TickCmds tc{tick, {}};
            const uint8_t* c = body + 8;
            for (uint32_t i = 0; i < n && (size_t(c - body) + kCmdWire) <= len - 1; ++i) {
                tc.cmds.push_back(getCommand(c));
                c += kCmdWire;
            }
            remoteSched_.push_back(std::move(tc));
        } else if (kind == kHash && len >= 1 + 12) {
            remoteHashes_.push_back({get32(body), get64(body + 4)});
        }
        pos += 4 + len;
    }
    rxBuf_.erase(rxBuf_.begin(), rxBuf_.begin() + long(pos));
    return true;
}

bool Session::exchange(uint32_t tick, std::vector<Command>& out, int timeoutMs) {
    // Send local commands scheduled for the future tick.
    {
        TickCmds tc{tick + kInputDelay, pending_};
        pending_.clear();
        std::vector<uint8_t> payload;
        put32(payload, tc.tick);
        put32(payload, uint32_t(tc.cmds.size()));
        for (const auto& c : tc.cmds) putCommand(payload, c);
        if (!sendMsg(kCommands, payload)) return false;
        localSched_.push_back(std::move(tc));
    }

    // Wait for the peer's schedule for this tick.
    auto haveRemote = [&] {
        if (tick < kInputDelay) return true;   // pre-delay ticks are empty
        for (const auto& tc : remoteSched_)
            if (tc.tick == tick) return true;
        return false;
    };
    int waited = 0;
    while (!haveRemote()) {
        if (!pump(20)) return false;
        waited += 20;
        if (waited > timeoutMs) { error_ = "lockstep timeout"; return false; }
    }

    out.clear();
    auto collect = [&](std::vector<TickCmds>& sched, bool erase) {
        for (auto it = sched.begin(); it != sched.end();) {
            if (it->tick == tick) {
                out.insert(out.end(), it->cmds.begin(), it->cmds.end());
                if (erase) { it = sched.erase(it); continue; }
            }
            ++it;
        }
    };
    // Deterministic order: host commands first.
    if (local_ == 0) {
        collect(localSched_, true);
        collect(remoteSched_, true);
    } else {
        collect(remoteSched_, true);
        collect(localSched_, true);
    }
    return true;
}

bool Session::checkHash(uint32_t tick, uint64_t hash) {
    std::vector<uint8_t> payload;
    put32(payload, tick);
    put64(payload, hash);
    if (!sendMsg(kHash, payload)) return false;
    pump(0);
    for (auto& [t, h] : remoteHashes_)
        if (t == tick && h != hash) {
            error_ = "DESYNC at tick " + std::to_string(t);
            return false;
        }
    std::erase_if(remoteHashes_, [&](auto& th) { return th.first <= tick; });
    return true;
}

} // namespace tak::net
