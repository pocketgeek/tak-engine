#pragma once

#include "sim/sim.h"

namespace tak::client {

// What a selected unit does when the player right-clicks a FINISHED friendly
// unit (an unfinished one is a help-build site; see GameView::canAssistSite).
enum class FriendlyOrder { None, Repair, Guard };

// Retail's default-order resolver (0x4de530, action mode 1, the right-click
// interface: UseLeftClickIface clear) decides PER SELECTED UNIT: HelpBuild when
// it may help build the target, else RepairUnit when it may repair it, else
// Load, else FOLLOW_GROUND -- Guard -- when its type has `canguard`
// (UnitDef+0x264 bit 0x40, the bit the FBI parser at 0x4c0685 sets). The guard
// test does not look at weapons, so a builder guards a healthy unit just as a
// soldier does. (With nothing applicable retail falls through to a move to the
// point; we keep the click a no-op for that unit.)
//
// `repeating`: a mobile builder repeating production holds a rally plan and the
// sim refuses its Guard (matchsetup.cpp); it does nothing here, as it already
// did not repair.
inline FriendlyOrder friendlyUnitOrder(const sim::UnitType& unit, bool unitFinished,
                                       bool repeating, bool sameUnit, bool targetDamaged) {
    if (sameUnit || !unitFinished) return FriendlyOrder::None;
    const bool mobileBuilder=unit.isBuilder && !unit.isStructure();
    if (mobileBuilder && repeating) return FriendlyOrder::None;
    if (mobileBuilder && targetDamaged) return FriendlyOrder::Repair;
    return unit.canGuard ? FriendlyOrder::Guard : FriendlyOrder::None;
}

} // namespace tak::client
