#pragma once

#include "sim/retailflight.h"
#include "sim/retailmission.h"
#include "sim/retailmotion.h"
#include <bit>
#include <cmath>
#include <cstdint>

namespace tak::sim {

// AirToGround (mission registry row 5ebd9b, handler 41bf80): the attack
// mission 4de530 picks for a flyer without hoverattack whose target cannot
// fly. A flying target gets AirToAir (41ca50), hoverattack or not; a
// hoverattack flyer whose target cannot fly gets AirToGroundHover (41d820);
// and a selected weapon with `dropped` AirStrike
// (41b5e0); no shipped weapon sets `dropped`. The flyer does not stop to
// shoot: it flies attack runs over the target and its weapons fire whenever
// the weapon update (52ae90) finds the target in range.
//
//   stage 3: a point controller (4e40e0, flags 0x30) at the target, radius
//            attackrunlength/2, or half the selected weapon's range when the
//            unit has none; it is re-aimed at the target every 16 ticks.
//   stage 5: on arrival, the run: a point beyond the target, along the line
//            from the flyer through it, attackrunlength + 128 + rand(128) px
//            past it (3 x range without one), radius 128 + rand(128). Until
//            the flyer has once been within range/2 of the target it re-aims
//            the run every 15 ticks.
//   stage 6: on arrival, the turn: rand(2) picks 90 degrees left or right of
//            the body's heading, rand(128) + attackrunlength/2 px out (range
//            without one), radius 64 + rand(128).
//   stage 7: on arrival, back to stage 2 and the approach. Off the map it
//            adds one to mission +0x4e (1 once a run has come within
//            range/2, reset to 1 by a wake on the map); once that reaches 3
//            it first flies 800 px back toward the map, to 192 px inside the
//            playable edge.
//
// Every draw is 535cc0, the shared game random number generator. The host
// has made the head's checks (a live target that cannot fly, the leash);
// the group check (51d1e0) is not ported, so the 0x8000000 flag it sets is
// never set here. Stage 4's air-traffic step-out (unit +0x12a >= 5 pushes
// VTOL_StepOut) is not ported either: World keeps no congestion counter.
struct RetailAirRun {
    RetailFlightVector position;   // unit +0x68
    RetailFlightVector target;     // mission +0x22, the target's position at this wake
    uint16_t heading = 0;          // unit +0x7e
    int32_t range = 0;             // the selected slot's WeaponType +0x90
    uint16_t runLength = 0;        // UnitType +0x234, attackrunlength
    bool onMap = true;             // unit +0xa4 is a sector, not the off-map cell (game +0x19f30)
    int32_t width = 0, height = 0; // game +0x19e90 / +0x19e94: the playable map, in pixels
};

// 41c5b1..41c5c9 and its relatives: `distance` px from `from` along -heading.
inline RetailFlightVector retailAirRunPoint(RetailFlightVector from, int32_t y, uint16_t heading, uint32_t distance) {
    const int32_t magnitude=std::bit_cast<int32_t>(distance<<16);
    return {std::bit_cast<int32_t>(uint32_t(from.x)-uint32_t(retailScaledSine(heading,magnitude))),y,
            std::bit_cast<int32_t>(uint32_t(from.z)-uint32_t(retailScaledCosine(heading,magnitude)))};
}

// Host: random(bound); beginFlight() (416c50); slotsAuto() (519b10(3));
// aimSlot() (519b70 and 51a700 on the selected slot); install(goal) (4d4d40,
// which also clears the mission's pending controller events 0x3700).
template<class Host>
int retailAirToGround(RetailMissionState& m, int32_t& count, uint32_t tick, uint32_t events,
                      const RetailAirRun& run, Host& host) {
    if (events&0x2a) return 5;                               // 41c00c -> 41c8f6
    if (events&0x40) { m.sleep(tick,1); return 0; }          // 41c122
    if ((m.flags&0x8000000u) && host.random(25)==0) m.flags&=~0x8000000u;   // 41c29f
    auto install=[&](RetailFlightVector point,int radius) {
        host.install(RetailFlightGoal{point,0x30,0,int16_t(radius)});
    };
    switch (m.stage) {
    case 0:                                                  // 41c2cf
        host.beginFlight();
        return 1;
    case 1:                                                  // 41c308
        host.slotsAuto();
        return 1;
    case 2:                                                  // 41c31f
        host.aimSlot();
        return 1;
    case 3:                                                  // 41c37a
        install(run.target,run.runLength ? run.runLength>>1 : run.range/2);
        m.waitMask=0x76a;
        m.sleep(tick,15);
        return 1;
    case 4:                                                  // 41c3eb
        if (events&0x700) return 1;
        m.sleep(tick,1);
        m.stage=3;
        return 4;
    case 5: {                                                // 41c531
        const int32_t dx=std::bit_cast<int32_t>(uint32_t(run.position.x)-uint32_t(run.target.x));
        const int32_t dz=std::bit_cast<int32_t>(uint32_t(run.position.z)-uint32_t(run.target.z));
        const uint16_t heading=uint16_t(retailDirection(Fixed::raw(dx),Fixed::raw(dz)).v);
        const int radius=int(host.random(128))+128;
        const uint32_t distance=run.runLength ? host.random(128)+run.runLength+128u : uint32_t(run.range)*3u;
        install(retailAirRunPoint(run.target,run.target.y,heading,distance),radius);
        m.waitMask=0x76a;
        if (count>0) return 1;
        // x87 at double precision: dx*dx + dz*dz, fsqrt, _ftol.
        const int32_t reach=truncSat32(std::sqrt(double(dx)*dx+double(dz)*dz));
        if (reach>std::bit_cast<int32_t>(uint32_t(run.range/2)<<16)) { m.sleep(tick,15); return 2; }
        count=1;
        return 1;
    }
    case 6: {                                                // 41c6a7
        const uint16_t heading=uint16_t(run.heading+(host.random(2) ? 0x4000 : -0x4000));
        const uint32_t distance=run.runLength ? host.random(128)+(run.runLength>>1) : uint32_t(run.range);
        const auto point=retailAirRunPoint(run.position,run.position.y,heading,distance);
        install(point,int(host.random(128))+64);
        m.waitMask=0x76a;
        return 1;
    }
    case 7: {                                                // 41c7ae
        if (run.onMap) { count=1; m.stage=2; return 2; }
        if (++count<3) { m.stage=2; return 2; }
        RetailFlightVector inside=run.position;
        auto clamp=[](int32_t& v,int32_t size) {
            if (v<0xc00000) v=0xc00000;
            else if (const int32_t edge=std::bit_cast<int32_t>(uint32_t(size+0xff40)<<16);v>edge) v=edge;
        };
        clamp(inside.x,run.width);clamp(inside.z,run.height);
        // 51b340(unit, point): the heading of the unit as seen from the point.
        const uint16_t heading=uint16_t(retailDirection(
            Fixed::raw(std::bit_cast<int32_t>(uint32_t(run.position.x)-uint32_t(inside.x))),
            Fixed::raw(std::bit_cast<int32_t>(uint32_t(run.position.z)-uint32_t(inside.z)))).v);
        install(retailAirRunPoint(run.position,run.position.y,heading,800),128);
        m.waitMask|=0x700;
        return 2;
    }
    default:
        return 7;
    }
}

} // namespace tak::sim
