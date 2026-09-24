#pragma once

#include "sim/retailaim.h"
#include "sim/sim.h"

#include <array>
#include <cstdint>

namespace tak::client {

enum class CursorWeaponRange { InRange, OutOfRange, Unknown };

// The retail cursor selector resolves damage against UnitDef+0x9e, populated
// only from FBI `damagecategory`. Simulation damage uses the wider category
// list, so do not use Weapon::damageVs here.
inline float cursorDamageVs(const sim::Weapon& weapon, const sim::UnitType& target) {
    const auto it = weapon.dmgVs.find(target.damageCategory);
    return it == weapon.dmgVs.end() ? weapon.damage : weapon.damage * it->second;
}

// Retail's minrange and ordinary range checks square each 16.16 x/z delta, take
// that product's high dword, then add the two whole-pixel squared terms. Do not
// add the raw squares before shifting: the carry from the two fractional parts
// is discarded by the native code. Keeping the per-axis shifts also avoids a
// 65-bit sum at the signed-coordinate limits.
inline uint64_t cursorDistanceSquaredPixels(const std::array<int32_t, 3>& a,
                                           const std::array<int32_t, 3>& b) {
    const auto squareDelta = [](int32_t av, int32_t bv) {
        const int64_t delta = int64_t(av) - int64_t(bv);
        const uint64_t magnitude = uint64_t(delta < 0 ? -delta : delta);
        return magnitude * magnitude;
    };
    const uint64_t x2 = squareDelta(a[0], b[0]);
    const uint64_t z2 = squareDelta(a[2], b[2]);
    return (x2 >> 32) + (z2 >> 32);
}

// Native WeaponType inRange result for the classes represented by our UnitType
// data. Remote effects and dropped weapons have separate retail classes whose
// cursor callbacks have not been mapped; leave those inconclusive so they never
// produce a false TooFar glyph.
inline CursorWeaponRange cursorWeaponRange(const sim::Weapon& weapon,
        const sim::UnitType& attacker, const std::array<int32_t, 3>& attackerPosition,
        const sim::UnitType& target, const std::array<int32_t, 3>& targetPosition,
        uint8_t targetFlightGroundMode) {
    const uint64_t distance2 = cursorDistanceSquaredPixels(attackerPosition, targetPosition);
    if (weapon.minRange > 0 &&
        distance2 < uint64_t(weapon.minRange) * uint64_t(weapon.minRange))
        return CursorWeaponRange::OutOfRange;

    if (weapon.kind == sim::Weapon::Kind::Remote ||
        weapon.kind == sim::Weapon::Kind::Dropped)
        return CursorWeaponRange::Unknown;

    if (weapon.melee) {
        // MeleeWeapon::inRange ignores the authored `range`; its two footprint
        // boxes must be closer than half a 16px cell on both axes. A melee unit
        // cannot connect while the target is in retail's airborne mode 2.
        if (targetFlightGroundMode == 2) return CursorWeaponRange::OutOfRange;
        const auto absDelta = [](int32_t av, int32_t bv) {
            const int64_t d = int64_t(av) - int64_t(bv);
            return uint64_t(d < 0 ? -d : d);
        };
        constexpr uint64_t halfCell = 0x80000; // 8 px in 16.16
        const uint64_t xReach = uint64_t(attacker.footX + target.footX) * halfCell + halfCell;
        const uint64_t zReach = uint64_t(attacker.footZ + target.footZ) * halfCell + halfCell;
        return absDelta(attackerPosition[0], targetPosition[0]) < xReach &&
               absDelta(attackerPosition[2], targetPosition[2]) < zReach
                   ? CursorWeaponRange::InRange : CursorWeaponRange::OutOfRange;
    }

    if (weapon.ballistic) {
        const int32_t speed = retailAimSpeed(weapon.projVel);
        const uint16_t pitch = retailBallisticPitch(
            float(int64_t(targetPosition[0]) - attackerPosition[0]) / 65536.0f,
            float(int64_t(targetPosition[1]) - attackerPosition[1]) / 65536.0f,
            float(int64_t(targetPosition[2]) - attackerPosition[2]) / 65536.0f,
            float(speed) / 65536.0f, weapon.gravityAdj, weapon.lobPreferred);
        if (pitch == 0x8000) return CursorWeaponRange::OutOfRange;
    }

    // Base WeaponType::inRange is true for range=0 and otherwise compares the
    // 2D center distance inclusively. Guided, line-of-sight, and wandering
    // classes inherit this native check.
    if (weapon.range <= 0 ||
        distance2 <= uint64_t(weapon.range) * uint64_t(weapon.range))
        return CursorWeaponRange::InRange;
    return CursorWeaponRange::OutOfRange;
}

} // namespace tak::client
