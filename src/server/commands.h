#pragma once
#include "net/protocol.h"
#include <cmath>

namespace tak::srv {
inline bool parsePlayerCommands(const std::vector<uint8_t>& payload, std::vector<tak::net::Command>& parsed) {
    tak::net::Reader reader(payload.data(),payload.size());
    const uint32_t count=reader.u32();
    if (!reader.ok || count>payload.size()/35) return false;
    const auto coordinate=[](float v) {
        // Simulation destinations use signed 16.16. Reject values that cannot
        // be converted safely, including finite but enormous coordinates.
        return std::isfinite(v) && v>=-32768.0f && v<32768.0f;
    };
    parsed.clear();
    parsed.reserve(count);
    for (uint32_t i=0;i<count && reader.ok;++i) {
        const auto c=reader.cmd();
        if (uint8_t(c.kind)>uint8_t(tak::net::Cmd::ShareMana) ||
            !coordinate(c.x) || !coordinate(c.z) ||
            !coordinate(c.x2) || !coordinate(c.z2)) return false;
        // Ctrl+Shift requests ten units, the largest client production batch.
        // A forged INT_MAX count would otherwise allocate billions of entries
        // in World::train before another network/resource check could run.
        if ((c.kind==tak::net::Cmd::Train || c.kind==tak::net::Cmd::Unqueue) &&
            (c.targetId<0 || c.targetId>10)) return false;
        parsed.push_back(c);
    }
    return reader.ok && reader.p==reader.end;
}
inline bool validPlayerCommands(const std::vector<uint8_t>& payload) {
    std::vector<tak::net::Command> parsed;
    return parsePlayerCommands(payload,parsed);
}
}
