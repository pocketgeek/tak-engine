#pragma once
#include <cstdint>

// Retail deaths no weapon causes (KINGDOMS.icd); tools/re/check_kill_credit.py
// runs them natively.
namespace tak::sim {

// Deadly water, the unit update's 51e0d5..51e14c: once a second (the game
// tick a multiple of 30), when the map has waterdoesdamage (+0xd41) and a
// nonzero waterdamage (+0xd45), a unit whose height's whole part (+0x6e, a
// signed word of the 16.16 +0x6c) is at or below sea level (the byte at game
// +0x19ef8) and whose type is not canhover (UnitDef +0x260 & 0x1000) takes
// the whole waterdamage through 51a140, with no hitter and damage kind 13.
// Flight, a ship's hull or an amphibian's depth exempt nothing: only the
// height counts. 51a1d4 hands the amount on as a word.
inline uint16_t retailDeadlyWaterDamage(uint32_t tick, int32_t height, int seaLevel, bool canHover,
                                        bool enabled, int32_t amount) {
    if (!enabled || amount == 0 || tick % 30 != 0) return 0;
    if (int16_t(uint32_t(height) >> 16) > int16_t(uint8_t(seaLevel)) || canHover) return 0;
    return uint16_t(amount);
}

// A corpse on land, as the feature update 4959c0 (495aaf..495af1) runs it
// after 513027..513034 loaded its decomposition +0x3c (the feature type's
// +0x130: decomposetime x 30, truncated, low 16 bits): each update counts
// +0x3c down; the update that empties it starts +0x40 at 1 and every update
// from that one on adds 1 to it (the body sinks into the ground meanwhile),
// and the one that takes it past 60 removes the feature (496380). The
// placement's own tick runs the first update, so a corpse with a
// decomposition D goes D + 58 updates after it; with none (0) it rests for
// good (494a80). (E: tools/re/check_corpse_decay.py.)
inline int32_t retailCorpseDecayLife(int32_t decompose) {
    return decompose > 0 ? decompose + 58 : -1;
}

// A reclaim wake on a decaying corpse (406615 / VTOL 420ba4) reloads +0x3c
// with the decomposition D and leaves +0x40 alone. `remaining` is the updates
// left until the removal, counted as retailCorpseDecayLife does: 59 or more
// while +0x3c still counts (it holds remaining - 58), fewer once +0x40 runs
// (the removal comes when it passes 60 whatever +0x3c holds). The reload
// starts the count again unless +0x40 runs out first: when +0x3c empties
// before or on the removal update, +0x40 starts again at 1. Returns the
// updates left after the wake. (E: tools/re/check_corpse_decay.py.)
inline int32_t retailCorpseDecayHold(int32_t remaining, int32_t decompose) {
    if (decompose <= 0) return remaining >= 59 ? -1 : remaining;
    if (remaining >= 59 || decompose - 1 <= remaining) return decompose + 58;
    return remaining;
}

// A water grave's feature, as the feature update 4959c0 (495b81..495c8d)
// moves it after 512ee0 placed it with velocity (0, -0x2ccc, 0) at the dying
// body's 16.16 height `y`. Each update, in order: a fading feature loses its
// fade step from its alpha and is removed (496380) once the alpha is at or
// below zero; a feature at rest leaves the update list (494a80) for good;
// otherwise the velocity is added, and a feature at or below its bed (511260:
// the cell's (highest + lowest corner) / 2, -1 off the map) comes to rest
// there, one under sea level sinks at 0x2ccc a tick and, on a bed of exactly
// 0, starts fading (alpha 1, step 0.17498779 / height in pixels), and one at
// or above sea level gains gravity (game +0x19ecc). The placement's own tick
// runs the first update. Returns the updates after that one until the
// removal, or -1 for a feature that comes to rest: a water grave carries no
// decomposition (only the land branch loads +0x3c), so it stays. The floats
// are the x87 code's: the step is a correctly rounded single quotient and
// each alpha a correctly rounded single difference, so every comparison
// agrees. (E: tools/re/check_water_grave.py.)
inline int32_t retailWaterGraveLife(int32_t y, int bed, int seaLevel, int32_t gravity = 0x1fdb) {
    int32_t vy = -0x2ccc;
    bool fading = false;
    float alpha = 0.0f, step = 0.0f;
    const auto sea = int32_t(uint32_t(uint8_t(seaLevel)) << 16);
    const auto rest = int32_t(uint32_t(bed) << 16);
    for (int32_t update = 1; update < (1 << 20); ++update) {
        if (fading) {
            alpha = alpha - step;
            if (alpha <= 0.0f) return update - 1;
        }
        if (vy == 0) return -1;
        y = int32_t(uint32_t(y) + uint32_t(vy));
        if (y <= rest) { vy = 0; continue; }
        if (y < sea) {
            vy = -0x2ccc;
            if (!fading && bed == 0) {
                fading = true;
                alpha = 1.0f;
                step = float(0.17498779296875 / (double(y) * (1.0 / 65536.0)));
            }
        } else {
            vy -= gravity;
        }
    }
    return -1;
}

} // namespace tak::sim
