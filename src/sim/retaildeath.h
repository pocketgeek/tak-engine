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

} // namespace tak::sim
