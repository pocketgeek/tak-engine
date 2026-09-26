#include "net/protocol.h"

namespace tak::net {

// Base command layout is unchanged; area reclaim appends its second endpoint.
void Writer::cmd(const Command& c) {
    u8(uint8_t(c.kind));
    u8(c.player);
    u32(uint32_t(c.unitId));
    u32(uint32_t(c.targetId));
    f32(c.x);
    f32(c.z);
    u8(c.queue);
    b.insert(b.end(), c.type, c.type + 16);
    if (c.kind == Cmd::ReclaimArea) { f32(c.x2); f32(c.z2); }
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
    if (c.kind == Cmd::ReclaimArea) { c.x2 = f32(); c.z2 = f32(); }
    return c;
}

}  // namespace tak::net
