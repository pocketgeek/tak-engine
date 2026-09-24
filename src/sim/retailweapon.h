#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>

namespace tak {
// Ordered display callbacks emitted by the authoritative weapon update. At most
// three clears, three aim starts and three fires occur in one unit update.
struct RetailWeaponAnimation {
    enum Kind : uint8_t { Aim, Fire, Clear };
    Kind kind=Clear;
    uint8_t slot=0;
    uint16_t heading=0,pitch=0;
};
struct RetailWeaponAnimations {
    std::array<RetailWeaponAnimation,9> events{};
    uint8_t count=0;
    void clear() { count=0; }
    void add(RetailWeaponAnimation::Kind kind,int slot,uint16_t heading=0,uint16_t pitch=0) {
        // Native display packets retain the high byte of each aiming angle.
        events.at(count++)={kind,uint8_t(slot),uint16_t(heading&0xff00),uint16_t(pitch&0xff00)};
    }
};

// Native weapon record's aim handshake (50d450, 52fe30, 52fff0).
// The countdown advances only when the requested angles leave tolerance.
struct RetailAimState {
    uint16_t heading=0,pitch=0,flags=0;
    void set(int opcode) {
        if(opcode==21)flags&=0xff07;
        else if(opcode==22)flags|=8;
        else if(opcode==23)flags|=16;
    }
    void projectileCreated() { flags&=0xff0f; }
    bool start(uint16_t nextHeading,uint16_t nextPitch) {
        if(flags&0xe0)return false;
        heading=nextHeading;pitch=nextPitch;
        flags=uint16_t((flags&~8u)|0xe0u);
        return true;
    }
    bool ready(uint16_t nextHeading,uint16_t nextPitch,uint16_t tolerance,
               bool available,bool moverAligned) {
        if(!available)return false;
        const auto distance=[](uint16_t a,uint16_t b) {
            return std::abs(int(std::bit_cast<int16_t>(uint16_t(a-b))));
        };
        if(distance(nextHeading,heading)>tolerance || distance(nextPitch,pitch)>tolerance/2) {
            if(flags&0xe0)flags=uint16_t((flags&~0xe0u)|((flags&0xe0u)-0x20u));
            return false;
        }
        if(!(flags&8) || !moverAligned)return false;
        heading=nextHeading;pitch=nextPitch;
        return true;
    }
};

} // namespace tak

namespace tak::sim {
// 530140: FireWeapon starts a nominal +/-20% reload using one CRT draw.
// The reload field is a word; large nominal values wrap after the adjustment.
constexpr uint16_t retailWeaponReload(uint16_t nominal,uint16_t crtRoll) {
    const uint32_t spread=nominal/5u;
    return uint16_t(uint32_t(nominal)+(uint32_t(crtRoll)*2u*spread)/32768u-spread);
}

// 53018b..5301e9: event posted before the one-argument FireWeapon callback.
constexpr uint32_t retailWeaponFireEvent(uint32_t weaponFlags,uint32_t typeFlags,
        uint32_t unitFlags,uint16_t aimFlags) {
    if(weaponFlags&0x20000u)return 0x40000;
    if(((typeFlags&0x10000u) && (unitFlags&0xc0000000u)!=0xc0000000u) || !(aimFlags&3u))
        return 0x10000;
    return 0x20000;
}

// 52ae90: per-slot update after host weapon selection/presence checks.
// Fire starts the script and reload; SET 23 may arrive on a later update.
// Create owns allocation, projectileCreated(), mana and slot fallback handling.
template<class Admit,class Ready,class Fire,class Create>
void retailWeaponUpdate(uint16_t& reload,tak::RetailAimState& aim,uint32_t now,
        uint32_t& combatUntil,Admit admit,Ready ready,Fire fire,Create create) {
    if(reload)--reload;
    if(!admit())return;
    if(ready())fire();
    if(aim.flags&16) {
        combatUntil=std::max(combatUntil,now+600u);
        create();
    }
}
} // namespace tak::sim
