#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace tak {

enum class RetailDeathSfxFamily : uint8_t {
    Smoke,
    DamageFlame,
    Detached
};

// KINGDOMS.icd's extended EMIT_SFX dispatcher: these codes all have distinct
// creation paths. Code 259 is deliberately absent; its native handler is a no-op.
constexpr std::optional<RetailDeathSfxFamily> retailDeathSfxFamily(int32_t code) {
    if(code==257 || code==258 || code==265)return RetailDeathSfxFamily::Smoke;
    if(code>=260 && code<=262)return RetailDeathSfxFamily::DamageFlame;
    if(code==263 || code==264)return RetailDeathSfxFamily::Detached;
    return std::nullopt;
}

// Unit-owned effect lists remain associated with the rendered unit through its
// death animation. The port hands off the body at kCorpseAnimTicks; a statue
// takes that path immediately and never runs Killed/Dying.
constexpr bool retailAttachedSfxOwnerRemoved(bool alive,int deadForTicks,
                                              int corpseStatue,int deathAnimTicks) {
    return !alive && (corpseStatue>=0 || deadForTicks>=deathAnimTicks);
}

// RenderFrame stores only the already-resolved statue bit, not the simulation's
// -1-or-feature-id field. Keep that representation boundary explicit so false
// cannot be mistaken for feature id 0.
constexpr bool retailAttachedSfxOwnerRemovedSnapshot(bool alive,int deadForTicks,
                                                       bool corpseStatue,int deathAnimTicks) {
    return !alive && (corpseStatue || deadForTicks>=deathAnimTicks);
}

constexpr uint32_t retailAttachedSfxRetirementTick(uint32_t eventTick,
                                                    int deadForTicks,
                                                    int deathAnimTicks) {
    return eventTick+uint32_t(std::max(0,deathAnimTicks-deadForTicks));
}

constexpr bool retailTickAtOrAfter(uint32_t tick,uint32_t deadline) {
    return int32_t(tick-deadline)>=0;
}

} // namespace tak
