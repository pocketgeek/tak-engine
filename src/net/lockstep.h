#pragma once

#include <cstdint>

namespace tak::net {

// Player command wire format, shared by the client and server and reused by the
// sim's applyCommand(). This was originally the payload of a 2-peer TCP lockstep
// session -- hence the file name -- but that Session is gone; the transport is
// now the server-sequenced client-server protocol in net/protocol.h.

enum class Cmd : uint8_t {
    Move, Attack, AttackMove, Patrol, Stop, Train, Build, Load, Unload, Guard,
    SetWeapon,     // targetId = weapon slot (0=primary, 1, 2)
    RepeatTrain,   // ctrl+click a conjure icon: toggle infinite production of `type`
    Destroy,       // self-destruct unitId (Ctrl+D) -- via the command path so
                   // networked sims stay in lockstep
    Unqueue,       // remove `targetId` copies of `type` from unitId's build queue
                   // (Train also carries a count in targetId; 0 => 1)
    Assist,        // unitId (a mobile builder) resumes/assists conjuring the
                   // existing construction site targetId (revives a decaying one)
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

} // namespace tak::net
