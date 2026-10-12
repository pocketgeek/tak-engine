#include "net/protocol.h"

#include <cmath>

namespace tak::net {

// Base command layout is unchanged; area commands append their second endpoint.
void Writer::cmd(const Command& c) {
    u8(uint8_t(c.kind));
    u8(c.player);
    u32(uint32_t(c.unitId));
    u32(uint32_t(c.targetId));
    f32(c.x);
    f32(c.z);
    u8(c.queue);
    b.insert(b.end(), c.type, c.type + 16);
    if (c.kind == Cmd::ReclaimArea || c.kind == Cmd::BuildManaArea) { f32(c.x2); f32(c.z2); }
}

Command Reader::cmd() {
    Command c;
    c.kind = Cmd(u8());
    c.player = u8();
    c.unitId = int32_t(u32());
    c.targetId = int32_t(u32());
    c.x = f32();
    c.z = f32();
    c.queue = u8();
    if (avail(16)) { std::memcpy(c.type, p, 16); p += 16; }
    else ok = false;
    c.type[15] = 0;
    if (c.kind == Cmd::ReclaimArea || c.kind == Cmd::BuildManaArea) { c.x2 = f32(); c.z2 = f32(); }
    return c;
}

void put(Writer& w, const GameOptions& o) {
    w.u8(o.crusades); w.u8(o.forfeitSelfDestruct); w.u8(o.overridePolicy);
    w.u8(o.speed); w.u8(o.speedUnlock); w.u32(o.unitCap); w.u8(o.monarchExpendable);
    w.u8(o.stressTest); w.u8(o.fogExplored); w.u8(o.benchmark); w.u8(o.randomStarts);
    w.u8(o.doubleSight); w.u8(uint8_t(o.pathfindingMode));
}

void get(Reader& r, GameOptions& o) {
    o.crusades = r.u8(); o.forfeitSelfDestruct = r.u8(); o.overridePolicy = r.u8();
    o.speed = r.u8(); o.speedUnlock = r.u8(); o.unitCap = uint16_t(r.u32()); o.monarchExpendable = r.u8();
    o.stressTest = r.u8(); o.fogExplored = r.u8(); o.benchmark = r.u8(); o.randomStarts = r.u8();
    o.doubleSight = r.u8();
    const auto pathMode = r.u8();
    if (!sim::validPathfindingMode(pathMode)) r.ok = false;
    o.pathfindingMode = sim::PathfindingMode(pathMode);
}

void put(Writer& w, const SlotInfo& s) {
    w.u8(s.type); w.u8(s.faction); w.u8(s.color); w.u8(s.team); w.u8(s.ready); w.u8(s.aiLevel);
    w.str(s.name);
}

void get(Reader& r, SlotInfo& s) {
    s.type = r.u8(); s.faction = r.u8(); s.color = r.u8(); s.team = r.u8();
    s.ready = r.u8(); s.aiLevel = r.u8(); s.name = r.str();
}

Writer crusadesRulesPreimage(uint32_t seed, const GameOptions& o, const SlotInfo* slots) {
    Writer w;
    w.str("modern-crusades-duel-v1"); w.u32(seed);
    put(w, o);
    for (int i = 0; i < kMaxSlots; ++i) {
        const SlotInfo& s = slots[i];
        w.u8(s.type); w.u8(s.faction); w.u8(s.color); w.u8(s.team); w.u8(s.aiLevel);
    }
    return w;
}

void putBundle(Writer& w, uint32_t tick, const std::vector<Command>& cmds, const std::vector<Event>& events) {
    w.u32(tick);
    w.u32(uint32_t(cmds.size()));
    for (const auto& c : cmds) w.cmd(c);
    w.u32(uint32_t(events.size()));
    for (const auto& e : events) { w.u8(uint8_t(e.kind)); w.u8(e.player); }
}

bool getBundle(Reader& r, uint32_t& tick, Bundle& out, uint32_t proto) {
    tick = r.u32();
    if (!r.ok) return false;
    // Counts are bounded by what is left before anything is reserved: a command is
    // at least 35 bytes and an event 2, so a corrupt count cannot ask for gigabytes.
    const uint32_t nc = r.u32();
    if (uint64_t(nc) * 35 > uint64_t(r.end - r.p)) return false;
    const auto maxKind = proto >= 221 ? Cmd::BuildManaArea : Cmd::ShareMana;
    for (uint32_t i = 0; i < nc && r.ok; ++i) {
        const auto c = r.cmd();
        if (uint8_t(c.kind) > uint8_t(maxKind) || c.player >= kMaxSlots ||
            !std::isfinite(c.x) || !std::isfinite(c.z) || !std::isfinite(c.x2) || !std::isfinite(c.z2)) return false;
        out.cmds.push_back(c);
    }
    const uint32_t ne = r.u32();
    if (!r.ok || uint64_t(ne) * 2 > uint64_t(r.end - r.p)) return false;
    for (uint32_t i = 0; i < ne && r.ok; ++i) {
        Event e; e.kind = Event::Kind(r.u8()); e.player = r.u8();
        if (e.player >= kMaxSlots || uint8_t(e.kind) < 1 || uint8_t(e.kind) > uint8_t(Event::Kind::CampaignForfeit)) return false;
        out.events.push_back(e);
    }
    return r.ok && r.p == r.end;   // truncated or trailing inner data
}

}  // namespace tak::net
