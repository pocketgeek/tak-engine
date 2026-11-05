#pragma once
// The information bar's status line for one selected unit (GameView's
// ActionText), split out of the GameView so a test can read it.
#include "client/renderframe.h"

namespace tak::client {

// Retail's own mission labels, from translate/unitmissions.tdf -- the table
// keyed by the UNITMISSIONCODE_* strings the engine's command table carries.
// Several of these used to be reasonable-sounding paraphrases that retail
// simply does not use: it says "Clearing" and not RECLAIMING, "Loading" and
// not BOARDING, and a half-built unit is an "Intangible Mass".
inline const char* unitStatusLabel(const UnitR& u) {
    if (!u.type) return "";
    // UNITMISSIONCODE_SELFDESTRUCT. First, because it is what the unit is
    // doing -- it has stopped taking orders and is walking out on you.
    if (u.selfDestructT >= 0) return "LEAVING YOUR COMMAND";
    if (u.stonedFor > 0) return "PETRIFIED";
    if (u.frozenFor > 0) return "FROZEN";
    if (u.paralyzedFor > 0) return "PARALYZED";
    if (u.underConstruction) return "INTANGIBLE MASS";
    // Only a unit that can be switched off is ever "off": every type without
    // activatewhenbuilt is created with its activation bit clear (4c033e,
    // default 0), the archer as much as the gate.
    if (u.type->onOffable && !u.active) return "INACTIVE";
    if (u.repairId != 0) return "REPAIRING";
    // UNITMISSIONCODE_RESURRECT: the walk to the body (its route's
    // waypoints lead the leg) and the channel.
    for (const auto& o : u.orders)
        if (o.goal) { if (o.revive) return "RAISING DEAD"; break; }
    if (u.reclaimId != 0) return "CLEARING";
    if (u.buildSiteId != 0 || !u.buildQueue.empty()) return "CONJURING";
    if (!u.orders.empty()) {
        const auto& o = u.orders.front();
        if (o.guard) return "GUARDING";
        if (o.patrol) return "PATROLLING";
        if (o.load) return "LOADING";
        if (o.unload) return "UNLOADING";
        if (o.reclaimFeat || o.reclaimArea) return "CLEARING AREA";
        if (o.targetId != 0) return "ATTACKING";
        if (o.attackMove) return "SEEKING TO ATTACK";
        return "MOVING";
    }
    if (u.cloaked) return "CLOAKING";
    return "STANDBY";   // retail's idle label
}

} // namespace tak::client
