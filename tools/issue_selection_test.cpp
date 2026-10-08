// issueSelection client equivalence and the .scn format.
//
//   issue_selection_test [DATA]
//
// GameView (the SDL client) cannot be linked from tools/, so the client's
// path is transcribed below as ClientRef, statement for statement, each part
// citing the lines it copies. ClientRef keeps the client's own structure --
// an outbox drained by mpStep once per frame, a server queue closed once per
// tick, bundles that come back after a real uplink and downlink delay -- and
// issueSelection (tools/legion_issue_selection.h) folds all of that into one
// round trip R. Each case checks the two Cmd streams are equal, field for
// field, tick for tick (shifted by the uplink delay), plus the expected shape
// by hand: 64 per tick, consecutive ticks, the 512 window's gap.
//
// The second half parses a sample .scn of each map kind (ascii, flat, gen1,
// snapshot), round-trips it through the canonical writer, and builds its
// World twice: the state hashes must agree (and differ for another start
// offset). DATA (a retail install) also builds the data-gated sample.
#include "legion_issue_selection.h"
#include "legion_scn.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <set>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace tak::sim;
using tak::net::Cmd;
using tak::net::Command;
using tak::scn::CmdBatch;
using tak::scn::SelectionOrder;
using tak::scn::Verb;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", what.c_str()); ++failures; }
}

// ---- the client, transcribed ----------------------------------------------
struct ClientRef {
    const World& world_;
    int localPlayer_ = 0;
    std::vector<int> selection_;
    // gameview.h:2495-2496 and the send state mpStep keeps.
    std::vector<Command> outbox_;
    size_t outboxHead_ = 0;
    int cmdCredit_ = tak::net::kCmdCapPerTick;
    int cmdInFlight_ = 0;
    uint32_t lastSendTick_ = 0;

    explicit ClientRef(const World& w) : world_(w) {}
    const Unit* frameUnitP(int id) const {
        const Unit* u = world_.unit(id);
        return u && u->alive() ? u : nullptr;
    }
    // gameview_impl.cpp:193-197 (mp_ set: the outbox).
    void issue(Command c) {
        c.player = uint8_t(localPlayer_);
        outbox_.push_back(c);
    }
    // gameview_hud.cpp:240-270: right-click on open ground.
    void rightClickMove(float wx, float wz, bool queue) {
        const bool legion = isLegionPathfinding(world_.pathfindingMode());
        float cx = 0, cz = 0;
        int n = 0;
        for (int id : selection_)
            if (const auto* u = frameUnitP(id)) { cx += u->x.toFloat(); cz += u->z.toFloat(); ++n; }
        if (n) { cx /= float(n); cz /= float(n); }
        for (int id : selection_) {
            const auto* u = frameUnitP(id);
            if (!u) continue;
            Command c;
            c.kind = Cmd::Move;
            c.unitId = id;
            const bool shared = u->type && legion && !u->type->canFly &&
                u->type->footX >= 1 && u->type->footZ >= 1 && u->type->footX <= 8 && u->type->footZ <= 8;
            c.x = shared ? wx : wx + std::clamp(u->x.toFloat() - cx, -60.0f, 60.0f);
            c.z = shared ? wz : wz + std::clamp(u->z.toFloat() - cz, -60.0f, 60.0f);
            c.queue = queue;
            issue(c);
        }
    }
    // gameview_hud.cpp:230-237: right-click on an enemy.
    void rightClickAttack(int enemy, bool queue) {
        for (int id : selection_) {
            Command c;
            c.kind = Cmd::Attack;
            c.unitId = id;
            c.targetId = enemy;
            c.queue = queue;
            issue(c);
        }
    }
    // gameview_hud.cpp:994-1013 (armed G) and 1032-1048 (armed F / P, no enemy).
    void armedOrder(char cmd, float wx, float wz, int buddy, bool queue) {
        if (cmd == 'g') {
            for (int id : selection_) {
                if (id == buddy) continue;
                Command c;
                c.kind = Cmd::Guard;
                c.unitId = id;
                c.targetId = buddy;
                c.queue = queue ? 1 : 0;
                issue(c);
            }
            return;
        }
        for (int id : selection_) {
            Command c;
            c.kind = (cmd == 'f' || cmd == 'a') ? Cmd::AttackMove : cmd == 'p' ? Cmd::Patrol : Cmd::Move;
            c.x = wx;
            c.z = wz;
            c.unitId = id;
            c.queue = queue ? 1 : 0;
            issue(c);
        }
    }
    // gameview_impl.cpp:4707-4714 (Act::Stop).
    void stop() {
        for (int id : selection_) {
            Command c;
            c.kind = Cmd::Stop;
            c.unitId = id;
            issue(c);
        }
    }
    // gameview_impl.cpp:4748-4767 (issueSquad, assignSquad).
    void issueSquad(int unitId, int val) {
        Command c;
        c.kind = Cmd::SetSquad;
        c.unitId = unitId;
        c.targetId = val;
        issue(c);
    }
    void assignSquad(int num, int sign, bool append) {
        if (num < 1 || num > 10 || selection_.empty()) return;
        int want = sign * num;
        std::set<int> sel(selection_.begin(), selection_.end());
        for (const auto& u : world_.units())   // front().live
            if (u.alive() && u.player == localPlayer_ && std::abs(int(u.squad)) == num && sel.find(u.id) == sel.end())
                issueSquad(u.id, append ? want : 0);
        for (int id : selection_)
            if (const auto* u = frameUnitP(id); u && u->alive() && u->player == localPlayer_) issueSquad(id, want);
    }
    // gameview_net.cpp:337-368: the send half of mpStep. netTick_ is the last
    // bundle this client has simulated.
    std::vector<Command> mpStepSend(uint32_t netTick_) {
        if (netTick_ != lastSendTick_) {
            cmdCredit_ = tak::net::cmdSendCredit(cmdCredit_, netTick_ - lastSendTick_);
            lastSendTick_ = netTick_;
        }
        const int sendable = tak::net::cmdSendWindow(cmdCredit_, cmdInFlight_);
        const size_t pending = outbox_.size() - outboxHead_;
        std::vector<Command> sent;
        if (pending > 0 && sendable > 0) {
            const size_t n = std::min(pending, size_t(sendable));
            cmdCredit_ -= int(n);
            cmdInFlight_ += int(n);
            const auto first = outbox_.begin() + ptrdiff_t(outboxHead_);
            sent.assign(first, first + ptrdiff_t(n));
            outboxHead_ += n;
            if (outboxHead_ == outbox_.size()) { outbox_.clear(); outboxHead_ = 0; }
        }
        return sent;
    }
    // gameview_net.cpp:393-397: our own commands in a bundle retire in-flight credit.
    void bundleArrived(const std::vector<Command>& cmds) {
        for (const auto& c : cmds)
            if (int(c.player) == localPlayer_ && cmdInFlight_ > 0) --cmdInFlight_;
    }
};

// The wire around it: a client frame per tick, `up` ticks to the server, the
// server's closeTick drain (server.cpp:2819-2823), `down` ticks back. Returns
// the bundle of every server tick from `issueTick` until all is delivered.
struct Wire {
    ClientRef& client;
    int up = 0, down = 0;
    Wire(ClientRef& c, int u, int d) : client(c), up(u), down(d) {}
    std::map<uint32_t, std::vector<Command>> toServer;   // arrival tick -> commands
    std::deque<Command> cmdQueue;                        // Server::Client::cmdQueue
    std::map<uint32_t, std::vector<Command>> bundles;    // server tick -> bundle
    uint32_t netTick = 0;                                // last bundle the client simulated
    bool any = false;

    void frame(uint32_t t) {
        // Client frame at time t: send (mpStep's first half) ...
        if (auto sent = client.mpStepSend(netTick); !sent.empty()) {
            auto& q = toServer[t + uint32_t(up)];
            q.insert(q.end(), sent.begin(), sent.end());
        }
        // ... the server closes tick t ...
        if (auto it = toServer.find(t); it != toServer.end()) {
            cmdQueue.insert(cmdQueue.end(), it->second.begin(), it->second.end());
            toServer.erase(it);
        }
        std::vector<Command> pending;
        for (int taken = 0; taken < tak::net::kCmdCapPerTick && !cmdQueue.empty(); ++taken) {
            pending.push_back(cmdQueue.front());
            cmdQueue.pop_front();
        }
        bundles[t] = pending;
        // ... and the client drains every bundle that has reached it (mpStep's second half).
        if (t >= uint32_t(down)) {
            const uint32_t s = t - uint32_t(down);
            client.bundleArrived(bundles[s]);
            netTick = s;
        }
    }
    bool drained() const {
        return client.outbox_.size() == client.outboxHead_ && toServer.empty() && cmdQueue.empty();
    }
};

// Bundles [from, ...) until the wire drains, trimmed of trailing empties.
std::vector<CmdBatch> runWire(Wire& wire, uint32_t from, uint32_t issueTick, const std::function<void()>& click) {
    for (uint32_t t = 0; t < issueTick; ++t) wire.frame(t);   // an idle client before the click
    click();
    std::vector<CmdBatch> out;
    uint32_t t = issueTick;
    for (; !wire.drained() || t <= issueTick; ++t) wire.frame(t);
    for (uint32_t s = from; s < t; ++s) out.push_back({s, wire.bundles[s]});
    while (!out.empty() && out.back().cmds.empty()) out.pop_back();
    return out;
}

bool sameCmd(const Command& a, const Command& b) {
    return a.kind == b.kind && a.player == b.player && a.unitId == b.unitId && a.targetId == b.targetId &&
           std::memcmp(&a.x, &b.x, sizeof a.x) == 0 && std::memcmp(&a.z, &b.z, sizeof a.z) == 0 &&
           a.queue == b.queue && std::memcmp(a.type, b.type, sizeof a.type) == 0 && a.x2 == b.x2 && a.z2 == b.z2;
}
// The client's stream (shifted back by `shift` ticks of uplink delay) equals issueSelection's.
bool sameStream(const std::vector<CmdBatch>& mine, const std::vector<CmdBatch>& client, int shift,
                const std::string& what) {
    auto trimmed = mine;
    while (!trimmed.empty() && trimmed.back().cmds.empty()) trimmed.pop_back();
    if (trimmed.size() != client.size()) {
        std::fprintf(stderr, "  %s: %zu ticks vs client %zu\n", what.c_str(), trimmed.size(), client.size());
        return false;
    }
    for (size_t i = 0; i < trimmed.size(); ++i) {
        if (trimmed[i].tick + uint32_t(shift) != client[i].tick || trimmed[i].cmds.size() != client[i].cmds.size()) {
            std::fprintf(stderr, "  %s: batch %zu tick %u size %zu vs client tick %u size %zu\n", what.c_str(), i,
                         trimmed[i].tick, trimmed[i].cmds.size(), client[i].tick, client[i].cmds.size());
            return false;
        }
        for (size_t j = 0; j < trimmed[i].cmds.size(); ++j)
            if (!sameCmd(trimmed[i].cmds[j], client[i].cmds[j])) {
                std::fprintf(stderr, "  %s: batch %zu cmd %zu differs\n", what.c_str(), i, j);
                return false;
            }
    }
    return true;
}

// ---- fixtures --------------------------------------------------------------
UnitType ground(int foot) {
    UnitType t{};
    t.id = t.name = "sel-foot-" + std::to_string(foot);
    t.canMove = true; t.maxHp = 100; t.footX = t.footZ = foot; t.sight = 4096;
    t.maxVel = Fixed::raw(117964); t.accel = t.brake = Fixed::fromInt(10);
    t.turnRate = t.turnInPlaceRate = 2500; t.halfCellTicks = 3; t.buildTime = 1;
    return t;
}
UnitType flyer() {
    UnitType t{};
    t.id = t.name = "sel-flyer";
    t.canFly = t.canMove = true; t.maxHp = 100; t.footX = t.footZ = 2; t.sight = 4096;
    t.maxVel = Fixed::fromInt(4); t.accel = t.brake = Fixed::fromInt(1); t.turnRate = 1200; t.cruiseAlt = 80;
    t.buildTime = 1; t.vtolStandby = true;
    return t;
}
struct Field {
    World world;
    Field(PathfindingMode mode) {
        world.setGameSeed(7); world.setVisPlayer(-1); world.setSerialThreads(true); world.setPathService(true);
        world.setPathfindingMode(mode);
        world.setPlayerCount(2); world.setTeam(0, 0); world.setTeam(1, 1);
        world.setTerrain(std::vector<uint8_t>(size_t(512) * 512, 100), 512, 512, 64);
    }
    // Bodies in rows of `perRow` from cell (x, z), pitch `pitch`.
    std::vector<int> block(const UnitType& t, int n, int x, int z, int perRow, int pitch, int player = 0) {
        std::vector<int> ids;
        for (int i = 0; i < n; ++i) {
            const int id = world.spawn(&t, float((x + (i % perRow) * pitch) * 16), float((z + (i / perRow) * pitch) * 16),
                                       std::nullopt, player);
            if (id <= 0) throw std::runtime_error("spawn failed");
            ids.push_back(id);
        }
        return ids;
    }
};

constexpr uint32_t kIssueTick = 100;   // an idle client: its credit has saturated

// issueSelection vs the client for one click; every (up, down) split of R.
void compare(const World& w, const std::vector<int>& sel, const SelectionOrder& o, int roundTrip,
             const std::function<void(ClientRef&)>& click, const std::string& what,
             std::vector<CmdBatch>* keep = nullptr) {
    const auto mine = tak::scn::issueSelection(w, sel, o, kIssueTick, 0, roundTrip);
    if (keep) *keep = mine;
    // No window (roundTrip < 0) is an uplink whose acks come back before the
    // window can fill: R = 1, the client one frame behind the server.
    const int R = roundTrip < 0 ? 1 : roundTrip;
    for (int up = 0; up < R; ++up) {
        ClientRef client(w);
        client.selection_ = sel;
        Wire wire{client, up, R - 1 - up};
        const auto theirs = runWire(wire, kIssueTick + uint32_t(up), kIssueTick, [&] { click(client); });
        check(sameStream(mine, theirs, up, what), what + ": issueSelection == client HUD path (up " +
                                                       std::to_string(up) + ", down " + std::to_string(R - 1 - up) + ")");
    }
}

// The expected shape: per-tick sizes from `from`, every unit once in selection order.
void shape(const std::vector<CmdBatch>& b, const std::vector<int>& sizes, const std::string& what) {
    bool ok = b.size() >= sizes.size();
    for (size_t i = 0; ok && i < sizes.size(); ++i) ok = b[i].tick == kIssueTick + i && int(b[i].cmds.size()) == sizes[i];
    for (size_t i = sizes.size(); ok && i < b.size(); ++i) ok = b[i].cmds.empty();
    if (!ok) {
        std::fprintf(stderr, "  %s sizes:", what.c_str());
        for (const auto& x : b) std::fprintf(stderr, " %u:%zu", x.tick, x.cmds.size());
        std::fprintf(stderr, "\n");
    }
    check(ok, what + ": lands 64 per tick over consecutive ticks");
}
std::vector<int> split(int n) {
    std::vector<int> s;
    for (; n > 0; n -= 64) s.push_back(std::min(n, 64));
    return s;
}
std::vector<int> unitsOf(const std::vector<CmdBatch>& b) {
    std::vector<int> ids;
    for (const auto& x : b) for (const auto& c : x.cmds) ids.push_back(c.unitId);
    return ids;
}

void selections() {
    // 1, 64, 65, 200, 450 ground bodies, right-click move, both modes.
    for (PathfindingMode mode : {PathfindingMode::Legion, PathfindingMode::Retail}) {
        const char* m = mode == PathfindingMode::Legion ? "legion" : "retail";
        for (int n : {1, 64, 65, 200, 450}) {
            Field f(mode);
            const auto t = ground(2);
            const auto ids = f.block(t, n, 20, 20, 25, 3);
            SelectionOrder o;
            o.verb = Verb::Move; o.x = 400 * 16; o.z = 300 * 16;
            const std::string what = std::string(m) + " move " + std::to_string(n);
            std::vector<CmdBatch> b;
            compare(f.world, ids, o, -1, [&](ClientRef& c) { c.rightClickMove(o.x, o.z, false); }, what, &b);
            shape(b, split(n), what);
            check(unitsOf(b) == ids, what + ": every unit once, in selection order");
            bool shared = true, offset = true;
            for (const auto& x : b)
                for (const auto& c : x.cmds) {
                    shared &= c.x == o.x && c.z == o.z;
                    offset &= std::abs(c.x - o.x) <= 60 && std::abs(c.z - o.z) <= 60;
                }
            if (mode == PathfindingMode::Legion) check(shared, what + ": Legion ground shares the click");
            else check(offset && (n == 1 || !shared), what + ": Retail keeps per-axis offsets within 60 px");
            // Applied in the sim, every body takes the order.
            if (n == 200) {
                tak::sim::TypeRegistry reg;
                uint32_t tick = 0;
                for (const auto& x : b) {
                    while (tick < x.tick) { f.world.tick(1.f / 30); ++tick; }
                    for (const auto& c : x.cmds) applyCommand(f.world, reg, c);
                }
                int ordered = 0;
                for (int id : ids) ordered += !f.world.unit(id)->orders.empty();
                check(ordered == n, what + ": the sim accepts every command");
            }
        }
    }

    // Mixed: flyers first in the selection, at opposite corners more than
    // 60 px from the centroid on both axes; ground in the middle.
    for (PathfindingMode mode : {PathfindingMode::Legion, PathfindingMode::Retail}) {
        Field f(mode);
        const auto g = ground(2), a = flyer();
        auto air = f.block(a, 6, 10, 10, 3, 3);
        const auto air2 = f.block(a, 6, 90, 90, 3, 3);
        air.insert(air.end(), air2.begin(), air2.end());
        const auto foot = f.block(g, 120, 40, 40, 12, 3);
        std::vector<int> sel = air;
        sel.insert(sel.end(), foot.begin(), foot.end());
        SelectionOrder o;
        o.verb = Verb::Move; o.x = 300 * 16; o.z = 200 * 16;
        const std::string what = std::string(mode == PathfindingMode::Legion ? "legion" : "retail") + " mixed 132";
        std::vector<CmdBatch> b;
        compare(f.world, sel, o, -1, [&](ClientRef& c) { c.rightClickMove(o.x, o.z, false); }, what, &b);
        shape(b, split(int(sel.size())), what);
        bool clamped = true, sharedGround = true;
        for (const auto& x : b)
            for (const auto& c : x.cmds) {
                const bool isAir = f.world.unit(c.unitId)->type->canFly;
                if (isAir) clamped &= std::abs(c.x - o.x) == 60 && std::abs(c.z - o.z) == 60;
                else sharedGround &= c.x == o.x && c.z == o.z;
            }
        check(clamped, what + ": far flyers keep a +-60 px offset on both axes");
        if (mode == PathfindingMode::Legion) check(sharedGround, what + ": Legion ground shares the click");
    }

    // All air, over 64.
    {
        Field f(PathfindingMode::Legion);
        const auto ids = f.block(flyer(), 100, 30, 30, 10, 4);
        SelectionOrder o;
        o.verb = Verb::Move; o.x = 200 * 16; o.z = 220 * 16;
        std::vector<CmdBatch> b;
        compare(f.world, ids, o, -1, [&](ClientRef& c) { c.rightClickMove(o.x, o.z, false); }, "air 100", &b);
        shape(b, split(100), "air 100");
    }

    // The other verbs on 65 bodies, queued and not.
    {
        Field f(PathfindingMode::Legion);
        const auto ids = f.block(ground(2), 65, 20, 20, 13, 3);
        const auto enemy = f.block(ground(2), 1, 200, 200, 1, 3, 1);
        const auto buddy = f.block(ground(2), 1, 10, 10, 1, 3);
        for (bool queue : {false, true}) {
            const std::string q = queue ? " queued" : "";
            SelectionOrder o;
            o.queue = queue;
            o.verb = Verb::Attack; o.targetId = enemy[0];
            compare(f.world, ids, o, -1, [&](ClientRef& c) { c.rightClickAttack(enemy[0], queue); }, "attack 65" + q);
            o.verb = Verb::Fight; o.x = 150 * 16; o.z = 160 * 16;
            compare(f.world, ids, o, -1, [&](ClientRef& c) { c.armedOrder('f', o.x, o.z, -1, queue); }, "fight 65" + q);
            o.verb = Verb::Patrol;
            compare(f.world, ids, o, -1, [&](ClientRef& c) { c.armedOrder('p', o.x, o.z, -1, queue); }, "patrol 65" + q);
            o.verb = Verb::Guard; o.targetId = buddy[0];
            std::vector<int> withBuddy;   // a buddy in the selection is skipped
            for (size_t i = 0; i < ids.size(); ++i) {
                if (i == 30) withBuddy.push_back(buddy[0]);
                withBuddy.push_back(ids[i]);
            }
            compare(f.world, withBuddy, o, -1, [&](ClientRef& c) { c.armedOrder('g', 0, 0, buddy[0], queue); },
                    "guard 65" + q);
        }
        SelectionOrder o;
        o.verb = Verb::Stop;
        compare(f.world, ids, o, -1, [&](ClientRef& c) { c.stop(); }, "stop 65");
    }

    // Alt+N then a move: SetSquad shares the 64-per-tick budget ahead of the
    // move; members of squad N outside the selection are evicted first.
    {
        Field f(PathfindingMode::Legion);
        const auto ids = f.block(ground(2), 200, 20, 20, 20, 3);
        const auto old = f.block(ground(2), 10, 100, 100, 5, 3);
        tak::sim::TypeRegistry reg;
        for (int id : old) {
            Command c;
            c.kind = Cmd::SetSquad; c.unitId = id; c.targetId = -3;
            applyCommand(f.world, reg, c);
        }
        for (bool append : {false, true}) {
            SelectionOrder sq;
            sq.verb = Verb::Squad; sq.squad = -3; sq.append = append;
            const std::string what = std::string("alt+3 200") + (append ? " shift" : "");
            std::vector<CmdBatch> b;
            compare(f.world, ids, sq, -1, [&](ClientRef& c) { c.assignSquad(3, -1, append); }, what, &b);
            shape(b, split(210), what);
            check(b[0].cmds[0].unitId == old[0] && b[0].cmds[0].targetId == (append ? -3 : 0),
                  what + ": the old members are evicted (or retyped) first");
        }
        // The press and the click together, as one outbox.
        tak::scn::Uplink link;
        SelectionOrder sq, mv;
        sq.verb = Verb::Squad; sq.squad = -3;
        mv.verb = Verb::Move; mv.x = 300 * 16; mv.z = 300 * 16;
        link.issue(tak::scn::hudCommands(f.world, ids, sq));
        link.issue(tak::scn::hudCommands(f.world, ids, mv));
        std::vector<CmdBatch> mine;
        for (uint32_t t = kIssueTick; !link.idle(); ++t) mine.push_back({t, link.land(t)});
        ClientRef client(f.world);
        client.selection_ = ids;
        Wire wire{client, 0, 0};
        const auto theirs = runWire(wire, kIssueTick, kIssueTick, [&] {
            client.assignSquad(3, -1, false);
            client.rightClickMove(mv.x, mv.z, false);
        });
        check(sameStream(mine, theirs, 0, "alt+3 then move"), "alt+3 then move: one stream, 410 over 7 ticks");
        shape(mine, split(410), "alt+3 then move");
    }

    // The command window: 974 bodies with a 12-tick round trip land as 512
    // over 8 ticks, a 4-tick gap, then the rest -- the same for every split
    // of the round trip into uplink and downlink.
    {
        Field f(PathfindingMode::Legion);
        const auto ids = f.block(ground(1), 974, 10, 10, 60, 2);
        SelectionOrder o;
        o.verb = Verb::Move; o.x = 400 * 16; o.z = 400 * 16;
        std::vector<CmdBatch> b;
        compare(f.world, ids, o, 12, [&](ClientRef& c) { c.rightClickMove(o.x, o.z, false); }, "window 974 R12", &b);
        std::vector<int> want(8, 64);
        want.insert(want.end(), 4, 0);
        want.insert(want.end(), 7, 64);
        want.push_back(974 - 512 - 7 * 64);
        shape(b, want, "window 974 R12");
        // An 8-tick round trip is exactly the window: no gap.
        compare(f.world, ids, o, 8, [&](ClientRef& c) { c.rightClickMove(o.x, o.z, false); }, "window 974 R8", &b);
        shape(b, split(974), "window 974 R8");
        // No window: the server's drain alone.
        b = tak::scn::issueSelection(f.world, ids, o, kIssueTick);
        shape(b, split(974), "974 no window");
    }
}

// ---- .scn samples ----------------------------------------------------------
const char* kAscii = R"(scn 1
name sample-ascii
ticks 900
seed 11
players 2
team 1 1
weapons off
map ascii
................................................................
................................................................
....................#...........................................
....................#...........................................
....................#.......................,,,,,~~~~~..........
....................#.......................,,,,,~~~~~..........
....................#.......................,,,,,~~~~~..........
....................#.......................,,,,,~~~~~..........
....................#...........................................
....................#...........................................
....................#...........................................
................................................................
..........................................^^^^..................
....................#.....................^^^^..................
....................#...........................................
....................#...........................................
....................#...........................................
....................#...........................................
....................#...........................................
....................#...........................................
....................#...........................................
....................#...........................................
................................................................
................................................................
end
type inf mover 2 2500 10 1.8
type slow mover 3 400 2 1.2 turninplace=0 sight=320
type jet flyer 2 1200 1 4 cruise=96
type army roster gun=150,30,1.5
group A 0 inf 24 rect 2 1 18 12 squad=f1
group B 0 slow 6 cells 24,2 28,2 32,2 24,6 28,6 32,6
group F 0 jet 4 rect 2 15 18 20 pitch=3
group E 1 army 12 rect 26 14 62 22 pitch=4 weapons=on
gate door 20 11 20 13
line finish 40 0 40 24
region home 0 0 18 24
at 30 move F,A,B 50 8
at 60 fight A,B @E queue
at 90 attack F @E
at 120 squad B g2 append
at 150 guard E @E
at 200 patrol E 5 5
at 400 stop A,B,F
)";

const char* kFlat = R"(scn 1
name sample-flat
ticks 600
players 1
uplink 12
map flat 160 120
wall 60 0 4 50
wall 60 56 4 64
height 100 80 10 10 160
type inf mover 1 2500 10 1.8
type keel boat 3 900 4 2 depth=10
type fort structure 4 4
group A 0 inf 974 rect 1 1 58 118 pitch=2
group K 0 fort 2 cells 120,20 130,20
at 0 move A 140.5 60.25
)";

std::string gen1Text() {
    return std::string("scn 1\nname sample-gen1\nticks 300\nplayers 2\nmap gen1 ") +
           tak::mapgen::encodeMapId([] {
               tak::mapgen::Params p;
               p.seed = 0x5eed;
               p.widthCells = p.heightCells = 256;
               p.waterDensity = 0;
               p.reliefDensity = 0;
               return p;
           }()) +
           "\ntype inf mover 2 2500 10 1.8\ngroup A 0 inf 48 rect 40 40 70 70\ngroup B 1 inf 48 rect 180 180 210 210\n"
           "at 0 move A 190 190\nat 0 move B 50 50\n";
}

const char* kSnapshot = R"(scn 1
name sample-snapshot
players 2
map snapshot sample.tnt
type inf mover 2 2500 10 1.8
group A 0 inf 16 rect 20 20 40 40
at 5 move A 100 100
)";

const char* kData = R"(scn 1
name sample-data
players 2
map flat 96 96
type hunter fbi zonter
type knight fbi arasword
group H 0 hunter 8 rect 10 10 30 30
group K 1 knight 8 rect 60 60 80 80 weapons=on
at 0 fight H @K
)";

// Build, feed orders for `ticks` ticks, return the hash.
uint64_t runHash(const tak::scn::Scenario& s, const tak::scn::BuildOptions& opt, uint32_t ticks,
                 uint64_t* initial = nullptr) {
    auto b = tak::scn::build(s, opt);
    if (!b->skipped.empty()) throw std::runtime_error("unexpectedly skipped: " + b->skipped);
    if (initial) *initial = b->world->stateHash();
    tak::scn::OrderFeed feed(s, *b);
    for (uint32_t t = 0; t < ticks; ++t) {
        feed.apply(t);
        b->world->tick(1.f / 30);
    }
    return b->world->stateHash();
}

void roundTrip(const std::string& text, const std::string& what, const std::string& dir = {}) {
    const auto a = tak::scn::parse(text, what, dir);
    const auto canon = tak::scn::format(a);
    const auto b = tak::scn::parse(canon, what + " (canonical)", dir);
    check(tak::scn::format(b) == canon, what + ": parse(format(parse(file))) formats identically");
}

void determinism(const tak::scn::Scenario& s, const std::string& what, uint32_t ticks, const char* data = nullptr,
                 const tak::hpi::Vfs* install = nullptr) {
    for (auto mode : {PathfindingMode::Legion, PathfindingMode::Retail}) {
        const std::string m = what + (mode == PathfindingMode::Legion ? " legion" : " retail");
        tak::scn::BuildOptions opt;
        opt.mode = mode;
        opt.data = data;
        opt.install = install;
        uint64_t i0 = 0, i1 = 0, ip = 0, im = 0;
        const uint64_t h0 = runHash(s, opt, ticks, &i0), h1 = runHash(s, opt, ticks, &i1);
        check(i0 == i1, m + ": two builds of the file hash the same");
        check(h0 == h1, m + ": two runs of the file hash the same");
        opt.offset = 1;
        const uint64_t hp = runHash(s, opt, 0, &ip);
        opt.offset = -1;
        const uint64_t hm = runHash(s, opt, 0, &im);
        (void)hp; (void)hm;
        check(ip != i0 && im != i0 && ip != im, m + ": start offsets 0/+1/-1 build different worlds");
        opt.offset = 0;
        opt.serial = false;
        check(runHash(s, opt, ticks) == h0, m + ": serial == workers");
        std::printf("%s: initial=%016llx after %u ticks=%016llx\n", m.c_str(), (unsigned long long)i0, ticks,
                    (unsigned long long)h0);
    }
}

void scenarios(const char* data, const std::filesystem::path& scratch) {
    // Ascii: every directive in one file.
    roundTrip(kAscii, "ascii");
    const auto ascii = tak::scn::parse(kAscii, "ascii");
    check(ascii.map.width == 64 && ascii.map.height == 24, "ascii: size from the rows");
    check(ascii.groups.size() == 4 && ascii.orders.size() == 7 && ascii.shapes.size() == 3, "ascii: counts");
    check(ascii.group("A")->squad == -1 && ascii.group("E")->weapons == 1, "ascii: group options");
    check(ascii.orders[0].selection == std::vector<std::string>{"F", "A", "B"}, "ascii: selection order kept");
    check(ascii.orders[1].queue && ascii.orders[1].target == "E", "ascii: fight @E queue");
    check(ascii.needsData().empty(), "ascii: asset-free");
    {
        auto b = tak::scn::build(ascii, {});
        check(b->groups["A"].size() == 24 && b->groups["E"].size() == 12, "ascii: groups spawned");
        check(b->world->unit(b->groups["E"][0])->type->weapons.size() == 1 &&
                  b->world->unit(b->groups["A"][0])->type->weapons.empty(),
              "ascii: weapons on/off per group");
        // The F,A,B click at tick 30 lands 34 commands in one tick, flyers first.
        tak::scn::OrderFeed feed(ascii, *b);
        size_t at0 = 0, at30 = 0;
        for (uint32_t t = 0; t <= 30; ++t) {
            const auto cmds = feed.commandsFor(t);
            if (t == 0) at0 = cmds.size();
            if (t == 30) {
                at30 = cmds.size();
                check(!cmds.empty() && b->world->unit(cmds[0].unitId)->type->canFly, "ascii: flyers first");
            }
        }
        check(at0 == 24 && at30 == 34, "ascii: tick-0 Alt+1 (24) and the tick-30 click (34)");
    }
    determinism(ascii, "ascii", 300);

    // Flat with overlays and the command window.
    roundTrip(kFlat, "flat");
    const auto flat = tak::scn::parse(kFlat, "flat");
    check(flat.roundTrip == 12 && flat.map.overlays.size() == 3, "flat: uplink and overlays");
    {
        auto b = tak::scn::build(flat, {});
        tak::scn::OrderFeed feed(flat, *b);
        std::vector<size_t> sizes;
        for (uint32_t t = 0; t < 25; ++t) sizes.push_back(feed.commandsFor(t).size());
        const std::vector<size_t> want{64, 64, 64, 64, 64, 64, 64, 64, 0, 0, 0, 0, 64, 64, 64, 64, 64, 64, 64, 14, 0, 0, 0, 0, 0};
        check(sizes == want, "flat: a 974-body click at tick 0 lands through the 512 window with R=12");
        check(feed.done(), "flat: the feed drains");
    }
    determinism(flat, "flat", 60);

    // gen1 and snapshot maps take their features from an install: without
    // one they report a skip; with mapgen_test's synthetic Aramon features
    // they build asset-free (no coast or relief prefabs).
    std::filesystem::create_directories(scratch / "features" / "aramon");
    {
        std::ofstream out(scratch / "features" / "aramon" / "fixture.tdf");
        for (const auto& [kind, count] : std::array<std::pair<const char*, int>, 4>{
                 {{"Tree", 10}, {"Rock", 7}, {"Henge", 9}, {"Mana", 3}}})
            for (int i = 1; i <= count; ++i) {
                char name[32];
                std::snprintf(name, sizeof name, "Ara%s%02d", kind, i);
                const bool mana = std::string(kind) == "Mana";
                out << "[" << name << "] {\nfootprintx=" << (mana ? 2 : 3) << ";\nfootprintz=" << (mana ? 2 : 4)
                    << ";\nblocking=" << (!mana) << "; }\n";
            }
    }
    tak::hpi::Vfs synthetic;
    synthetic.addLayer(tak::hpi::MountSet(scratch));
    const auto genText = gen1Text();
    roundTrip(genText, "gen1");
    const auto gen = tak::scn::parse(genText, "gen1");
    check(tak::scn::build(gen, {})->skipped == "needs --data: gen1 map", "gen1: skipped without an install");
    determinism(gen, "gen1", 200, nullptr, &synthetic);

    // Snapshot: a .tnt beside the file.
    {
        const auto g = tak::mapgen::generate(tak::mapgen::decodeMapId(gen.map.recipe), synthetic);
        const auto bytes = g.map.save();
        std::ofstream(scratch / "sample.tnt", std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                                     std::streamsize(bytes.size()));
        roundTrip(kSnapshot, "snapshot", scratch.string());
        const auto snap = tak::scn::parse(kSnapshot, "snapshot", scratch.string());
        check(tak::scn::build(snap, {})->skipped == "needs --data: snapshot sample.tnt",
              "snapshot: skipped without an install");
        determinism(snap, "snapshot", 100, nullptr, &synthetic);
    }

    // Data-gated: skipped with a status without an install, built with one.
    roundTrip(kData, "data");
    const auto gated = tak::scn::parse(kData, "data");
    {
        auto b = tak::scn::build(gated, {});
        check(b->skipped == "needs --data: fbi type 'zonter'", "data: skipped without --data ('" + b->skipped + "')");
        std::printf("data sample without --data: %s\n", b->skipped.c_str());
    }
    if (data) {
        determinism(gated, "data", 100, data);
        // The user's map (movement_orders_test's recipe family) with the real install.
        const auto user = tak::scn::parse(
            "scn 1\nname user-gen1\nplayers 2\nmap gen1 ~gen1~080001000000000000000100030003020000ff608003\n"
            "type inf mover 2 2500 10 1.8\ngroup A 0 inf 32 rect 100 100 124 124\nat 0 move A 160 160\n",
            "user-gen1");
        roundTrip(tak::scn::format(user), "user-gen1");
        determinism(user, "user-gen1", 100, data);
    }

    // Errors carry the file and line.
    bool threw = false;
    try { tak::scn::parse("scn 1\nmap flat 64 64\ngroup A 0 nope 3 rect 0 0 9 9\n", "bad.scn"); }
    catch (const std::runtime_error& e) { threw = std::string(e.what()).rfind("bad.scn:3:", 0) == 0; }
    check(threw, "a bad line is reported as file:line");
}
}  // namespace

int main(int argc, char** argv) {
    try {
        selections();
        scenarios(argc > 1 ? argv[1] : nullptr,
                  std::filesystem::temp_directory_path() / "issue_selection_test-scn");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL %s\n", e.what());
        return 1;
    }
    if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    std::printf("issue_selection_test: all passed\n");
    return 0;
}
