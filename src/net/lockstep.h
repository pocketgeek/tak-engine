#pragma once

#include <cstdint>
#include <memory>
#include <vector>

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
    Disco,         // cosmetic emote: unitId dances for 10s (Shift+D); 0 = living monarch.
                   // queue=0 starts a real-time emote; queue=1 appends selection targets.
    Reclaim,       // mobile builder unitId reclaims feature targetId for mana
    Stance,        // set unitId's combat stance to targetId (0=offensive/1=defensive/
                   // 2=passive) -- gates auto-acquire and chase leash
    Cloak,         // canCloak unitId: targetId != 0 enables cloaking, 0 disables
    SetActive,     // onOffable unitId: targetId != 0 powers on, 0 powers down
    Repair,        // mobile builder unitId repairs damaged friendly targetId (restores
                   // HP at its build rate, draining mana)
    Headbang,      // cosmetic emote: unitId headbangs 10s (Shift+H); 0 = living monarch.
    SetSquad,      // assign unitId to a control squad: targetId = 0 none, +N group N,
                   // -N formation N (N=1..10). A unit is in exactly one squad; formations
                   // move at their slowest member's speed and their stragglers rejoin.
    ReclaimArea,   // persistent area clear: x/z through x2/z2, chooses each next target in sim
    GiveUnit,      // unitId = owned eligible unit, targetId = allied recipient player
    ShareMana,     // targetId = allied recipient, queue = enabled; no unit required
    BuildManaArea, // mobile builder: lodestones in x/z through x2/z2, exploring as needed
    // An explicit selection order (protocol 267, Legion 10,000 stage 3): one
    // player intent over a whole selection, `order` holds it. Never sent in a
    // PlayerCommands message: the uplink carries it as an OrderBegin /
    // OrderMembers / OrderCommit transaction (net/protocol.h) and the server
    // puts the committed order into ONE tick bundle, its activation tick.
    Order,
};

struct SelectionOrder;

struct Command {
    Cmd kind = Cmd::Move;
    uint8_t player = 0;
    int32_t unitId = 0;
    int32_t targetId = 0; // target unit; for Unload, destination Y in signed 16.16
    float x = 0, z = 0;
    uint8_t queue = 0;
    char type[16] = {};   // unit type id for Train/Build
    float x2 = 0, z2 = 0; // ReclaimArea upper rectangle endpoint
    // Cmd::Order only: the order (immutable once built; shared by the copies a
    // command makes between the wire, the server's queue and the sim).
    std::shared_ptr<const SelectionOrder> order;
};

// How an order's members get their goal (Move only uses Offsets):
//   Shared   every member is given the template's command as it stands;
//   Offsets  the right-click move rule (client/gameview_hud.cpp): a member
//            Legion plans on a shared point (legionSharedClick, Legion mode
//            only) takes the click itself, every other member the click plus
//            its offset from the centroid of the order's live owned members,
//            clamped to 60 px per axis. The sim computes the offsets at
//            activation from hashed positions (integer Fixed arithmetic), so
//            no per-member coordinates travel.
enum class OrderShape : uint8_t { Shared = 0, Offsets = 1 };

// One explicit selection order: the player's intent over a selection, applied
// to every member in ONE tick (the activation tick the server gives it). The
// identity is (the issuing player, serial): serials are client-assigned,
// strictly increasing per player per game, 1..kOrderMaxSerial.
struct SelectionOrder {
    uint32_t serial = 0;
    Command tmpl;                    // the verb and its arguments; unitId and player unused
    OrderShape shape = OrderShape::Shared;
    std::vector<int32_t> members;    // unit ids, strictly increasing (no duplicates by construction)
    // The bundle record's size (orderRecordBytes), kept by whoever builds the
    // order (packClick, the decoder, the server's commit) so the send window and
    // the tick budget do not walk the members again; 0 = not known, computed.
    uint32_t bytes = 0;
};

} // namespace tak::net
