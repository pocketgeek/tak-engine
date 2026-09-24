#pragma once
#include <cstdint>
#include <algorithm>
#include <bit>
#include "fixed.h"
#include "retailmission.h"
#include "retailflight.h"

namespace tak::sim {
// 4034ec..4035e7: Move_Seek_Pickup after attachment, eligibility and
// carrier mission-chain checks. The host copies the carrier position and owns
// controller installation/removal; the dispatcher interprets the return code.
template<class Approach,class Detach>
int retailPassengerPickup(RetailMissionState& m,uint32_t& retries,uint32_t now,
        uint32_t events,bool surface,Approach approach,Detach detach) {
    switch(m.stage) {
    case 0:
        if(surface) { approach();m.waitMask=0x700; }
        else detach();
        m.waitMask|=0x88;m.sleep(now,30);return 1;
    case 1:
        if(events&0x80) { detach();retries=0;return 1; }
        if(events&0x700) { detach();m.waitMask=0x88;m.sleep(now,30);return 2; }
        m.stage=0;m.sleep(now,5);return 4;
    case 2:
        ++retries;
        if(std::bit_cast<int32_t>(retries)>5)return 8;
        m.sleep(now,30);return 2;
    default:return 7;
    }
}

// 408992/41a9f8 (pickup), 408df5/41b0cc (unload): compare the sum of the HIGH words of the two fixed-point
// squares. Flooring each term is observable at the edge of the beam radius.
inline bool retailTransportInRange(int32_t dx,int32_t dz,uint16_t range) {
    const uint32_t squared=uint32_t((uint64_t(int64_t(dx)*dx)>>32)+
                                    (uint64_t(int64_t(dz)*dz)>>32));
    return std::bit_cast<int32_t>(squared)<=std::bit_cast<int32_t>(uint32_t(range)*range);
}

// 41aa4b..41aab7 / 41b12f..41b19e: once in pickup/unload transfer range, an air carrier flies away along
// its current heading while unloading. This is a controller goal, not a
// different passenger landing position.
inline RetailFlightGoal retailUnloadStepOut(RetailFlightVector position,uint16_t heading,uint16_t range) {
    const int32_t distance=std::bit_cast<int32_t>((uint32_t(range)+400u)*131072u);
    position.x=std::bit_cast<int32_t>(uint32_t(position.x)-uint32_t(retailScaledSine(heading,distance)));
    position.z=std::bit_cast<int32_t>(uint32_t(position.z)-uint32_t(retailScaledCosine(heading,distance)));
    return {position,0x30,0,16};
}

// 4e3f70/4e4540/4e41d0: pickup's persistent pursuit follows the
// non-flying passenger, including its altitude, with a strict horizontal radius.
inline RetailFlightGoal retailPickupPursuitGoal(RetailFlightVector target,uint16_t range) {
    target.y=std::min(target.y,511*65536);
    return {target,0x11,0,int16_t(uint16_t(range-1))};
}

// 408bdd..408c21 / 41ac91..41acf3: approach polls while the navigator
// continues independently. Controller arrival/failure can wake the mission early.
template<class Random>
void retailPickupApproachWait(RetailMissionState& m,uint32_t now,bool flying,Random random) {
    m.waitMask=flying ? 0x728u : 0x708u;
    m.sleep(now,flying ? random(6)+6u : 15u);
}

// 408d50 stage 1 / 408e3f..408ea1: a surface unload succeeds once the
// carrier is in transport range. Outside range, a failed navigator or missing
// mover aborts before installing the 0x700 event mask; otherwise refresh the
// exact-point circle controller and poll in 15 ticks.
template<class Approach>
int retailTransportUnloadApproach(RetailMissionState& m,uint32_t& attempts,
        uint32_t now,uint32_t events,bool inRange,bool hasMover,Approach approach) {
    if(m.stage!=1)return 7;
    if(inRange) { ++attempts;m.sleep(now,1);return 1; }
    if((events&0x200u) || !hasMover)return 8;
    m.waitMask=0x700u;
    approach();
    m.sleep(now,15);
    return 2;
}

// 4089fa..408a19: a surface pickup outside transfer range aborts when
// its controller failed (or there is no mover) and the passenger is stationary.
// A moving passenger may still close the gap. VTOL_PICKUP has no such gate.
constexpr bool retailPickupApproachAborted(bool flying,bool hasMover,uint32_t events,int32_t passengerSpeed) {
    return !flying && (!hasMover || (events&0x200u)) && passengerSpeed==0;
}

// GROUND_PICKUP / VTOL_PICKUP transfer stage (408c3b / 41ad12).
// A moving passenger restarts the beam delay, with a bounded six-tick retry.
template<class Effects>
int retailPickupTransfer(RetailMissionState& m,int32_t& ticks,uint32_t& attempts,
        uint32_t now,bool flying,int32_t passengerSpeed,Effects effects) {
    if(std::bit_cast<int32_t>(attempts)>=(flying ? 4:10))return 8;
    if(passengerSpeed>0) {
        ++attempts;ticks=0;m.sleep(now,6);return 2;
    }
    if(ticks>=15)return 1;
    if(!ticks)effects();
    ++ticks;m.sleep(now,1);return 2;
}

// Shared transfer/retry stages of Unload (408ee1/408fe6) and VTOL_Unload
// (41b22b/41b362). Approach and attachment remain host operations.
template<class Placement,class Effects,class Blocked>
int retailUnloadTransfer(RetailMissionState& m,int32_t& ticks,uint32_t attempts,
        uint32_t now,bool passengerValid,Placement placement,Effects effects,Blocked blocked) {
    if (m.stage==2) {
        if (!passengerValid) { m.sleep(now,1);return 0; }
        if (placement(false)) {
            if (ticks>=15) { m.stage=4;return 4; }
            if (!ticks) effects();
            ++ticks;m.sleep(now,1);return 2;
        }
        if (placement(true)) { m.sleep(now,1);return 1; }
        blocked();return 8;
    }
    if (m.stage==3) {
        if (std::bit_cast<int32_t>(attempts)>=6) return 9;
        ticks=0;m.sleep(now,10);m.stage=1;return 4;
    }
    return 7;
}

// 4090b7/41b42c: after unloading, PARK disperses the passenger so
// the next one can use the same drop point. Two random(3) draws precede
// the footprint allowance for up to six passengers still aboard.
inline uint32_t retailUnloadParkPadding(uint32_t first,uint32_t second,
        uint32_t remaining,int16_t nextFootX,int16_t nextFootZ) {
    const uint64_t squared=uint64_t(int64_t(nextFootX)*nextFootX+
                                    int64_t(nextFootZ)*nextFootZ);
    return (first+second+1)*16+std::min(remaining,6u)*uint32_t(isqrt64(squared*256)/2);
}

// KINGDOMS.icd 0x51a085..0x51a12b: three independent limits. The
// passenger cost is transportedsize (default footprint area), not the
// carrier's transportsize (largest individual passenger it admits).
constexpr bool retailTransportCapacity(uint16_t passengerSize,uint16_t maxPassenger,
        uint32_t passengers,uint16_t countLimit,uint32_t usedSize,uint16_t sizeLimit) {
    return passengerSize<=maxPassenger && passengers<countLimit &&
           usedSize+uint32_t(passengerSize)<=sizeLimit;
}
}
