#pragma once

#include <cstdint>
#include <stdexcept>

namespace tak::sim {

// Primary order dispatcher (4d8450), independent of individual order handlers.
// The host owns the queue and controller lifetimes. This is not the campaign
// MissionScript runner and does not implement ground/air/build/combat handlers.
struct RetailMissionState {
    uint8_t stage = 0;
    uint32_t waitMask = 0;
    uint32_t deadline = 0xffffffffu;
    uint32_t pending = 0;
    uint32_t flags = 0;

    // 4d6a10: unsigned tick arithmetic, with no wrap-aware comparison at dispatch.
    void sleep(uint32_t tick, uint32_t delay) {
        waitMask |= 1;
        deadline = tick + delay;
    }
};

// 401bb0: Guard_NoMove's attack scan and repeat timer. Weapon firing is a
// separate per-tick update; an unarmed structure sleeps for 150 ticks.
template<class Random, class Scan>
int retailGuardNoMove(RetailMissionState& m, uint32_t tick, bool enabled,
                      bool armed, Random random, Scan scan) {
    if (!enabled) return 8;
    if (armed) scan();
    m.sleep(tick, armed ? random(15)+7 : 150);
    m.stage=0;
    return 2;
}

// 40781c..4078c6: Standby after the host's diversion check. The host owns
// navigator/weapon initialization and deciding whether a new order is available.
template<class Random, class Initialize, class TryOrder>
int retailStandby(RetailMissionState& m, uint32_t tick, bool hasMover,
                  Random random, Initialize initialize, TryOrder tryOrder) {
    switch (m.stage) {
    case 0:
        if (!hasMover) return 7;
        initialize();
        m.waitMask |= 0x20;
        m.sleep(tick, 1);
        return 1;
    case 1:
        if (tryOrder()) return 5;
        m.waitMask |= 0x20;
        m.sleep(tick, random(7) + 7);
        return 2;
    default:
        return 7;
    }
}

// 4175c8..417741: ordinary VTOL_Standby after boundary recovery/diversion.
// Landing and target-order installation remain explicit host operations.
template<class Random,class Initialize,class TryOrder,class TryLand>
int retailVtolStandby(RetailMissionState& m,uint32_t tick,bool hasMover,bool canFly,
                      bool airborne,Random random,Initialize initialize,TryOrder tryOrder,TryLand tryLand) {
    switch (m.stage) {
    case 0:
        if (!hasMover || !canFly) return 7;
        initialize(); m.waitMask|=0x20; m.sleep(tick,1); return 1;
    case 1:
        if (tryOrder()) { m.waitMask=0; m.stage=0; return 3; }
        return 1;
    case 2:
        if (canFly && airborne && tryLand()) return 5;
        m.waitMask|=0x20; m.sleep(tick,random(7)+7); m.stage=1; return 2;
    default: return 7;
    }
}

struct RetailGroundPoint { int16_t x=0, z=0; };
struct RetailGroundResponse {
    int32_t mode=0;
    RetailGroundPoint origin, goal;
};

// The executable truncates these integer-coordinate distances after sqrt.
// Use an integer root so the leash boundary does not depend on FP rounding.
inline uint32_t retailGroundDistance(RetailGroundPoint a,RetailGroundPoint b) {
    const int64_t x=int32_t(a.x)-int32_t(b.x), z=int32_t(a.z)-int32_t(b.z);
    uint64_t value=uint64_t(x*x+z*z), root=0, bit=uint64_t(1)<<62;
    while (bit>value) bit>>=2;
    while (bit) {
        if (value>=root+bit) { value-=root+bit; root=(root>>1)+bit; }
        else root>>=1;
        bit>>=2;
    }
    return uint32_t(root);
}

inline bool retailGroundReturnNeeded(RetailGroundPoint position,const RetailGroundResponse& response,uint16_t leash) {
    return retailGroundDistance(position,response.goal)>
           retailGroundDistance(response.origin,response.goal)+uint32_t(leash)/3;
}

// Ordinary Move_Ground after the host's two diversion checks. Response owns
// target selection/retaliation and auxiliary attack installation; Resume owns
// any return order needed after that attack. Neither callback may invalidate m
// until this function returns (the World host defers queue insertion).
// ResetGoal owns controller replacement, including cancellation and clearing
// controller events; the existing navigator route must survive replacement.
template<class Random, class ResetGoal, class Respond, class Resume>
int retailGroundMove(RetailMissionState& m, uint32_t& radius,
                          uint32_t tick, uint32_t events, int16_t footX,
                          bool attachmentBlocked, int32_t responseMode, uint8_t moveOrder,
                          Random random, ResetGoal resetGoal, Respond respond, Resume resume) {
    if ((m.flags & 0x8000000u) && random(10) == 0) m.flags &= ~0x8000000u;
    switch (m.stage) {
    case 0:
        if (attachmentBlocked) return 7;
        resetGoal(radius + 4);
        return 1;
    case 1:
        m.waitMask = 0x2700;
        m.sleep(tick, random(5) + 5);
        return 1;
    case 2:
        if (events & 0x100) return 5;
        if (events & 0x2700) {
            radius += uint32_t(int32_t(footX) * 32);
            return 0;
        }
        if ((responseMode<0 || (responseMode>0 && moveOrder)) && respond()) {
            m.stage=responseMode<0 ? 3 : 0;
            return 4;
        }
        m.stage = 1;
        return 4;
    case 3:
        if (!resume()) return 7;
        m.stage=0;
        return 4;
    default:
        return 7;
    }
}

template<class Random,class ResetGoal>
int retailPlainGroundMove(RetailMissionState& m,uint32_t& radius,uint32_t tick,uint32_t events,
                          int16_t footX,bool attachmentBlocked,Random random,ResetGoal resetGoal) {
    if (m.stage==3) throw std::invalid_argument("plain ground move has no auxiliary return state");
    return retailGroundMove(m,radius,tick,events,footX,attachmentBlocked,0,0,
                            random,resetGoal,[]{return false;},[]{return false;});
}

// 40388d..403b91: Patrol after the host's diversion checks. Initialize owns
// the return waypoint; ResetGoal owns the controller and weapon reset. Action
// checks combat then assistance and returns 0 (none), 3 (inserted), or 8 (failed
// order construction). Queue mutation must be deferred until this returns.
template<class Random,class Initialize,class ResetGoal,class Action>
int retailGroundPatrol(RetailMissionState& m,uint32_t& radius,uint32_t tick,
        uint32_t events,int16_t footX,bool hasMover,Random random,
        Initialize initialize,ResetGoal resetGoal,Action action) {
    if (!hasMover) return 7;
    if ((m.flags&0x8000000u) && random(10)==0) m.flags&=~0x8000000u;
    switch (m.stage) {
    case 0:
        initialize();m.sleep(tick,1);return 1;
    case 1:
        radius=0;return 1;
    case 2:
        resetGoal(radius+4);return 1;
    case 3:
        if (events&0x700u) { m.stage=1;return 6; }
        if (events&0x2000u) {
            radius+=uint32_t(int32_t(footX)*32);
            m.stage=2;m.sleep(tick,1);return 4;
        }
        if (const int result=action()) {
            if (result==3) { m.waitMask=0;m.stage=1; }
            return result;
        }
        m.waitMask=0x2700;m.sleep(tick,random(10)+5);return 2;
    default:return 7;
    }
}

// Host: enabled(), head(), hasNext(state), handle(state, events), random(bound),
// remove(state), rotate(state), clear(), idle(). Re-fetch the head and enabled
// state after every handler: handlers can replace the queue or disable a unit.
template<class Host>
void retailDispatchMissions(uint32_t tick, uint32_t& unitEvents, Host& host) {
    unsigned calls = 0;
    while (host.enabled()) {
        auto* mission = host.head();
        if (!mission) { host.idle(); return; }
        auto& m = *mission;
        if (tick >= m.deadline) {
            m.deadline = 0xffffffffu;
            m.pending |= 1;
        }
        const uint32_t events = (unitEvents | m.pending) & m.waitMask;
        if (m.waitMask && !events) return;
        unitEvents &= ~events;
        m.pending &= ~events;
        m.waitMask = 0;
        // Retail consumes events and clears the mask on the 101st attempt,
        // then clears the queue without invoking another handler.
        if (++calls > 100) { host.clear(); return; }
        switch (host.handle(m, events)) {
        case 0: m.stage = 0; break;
        case 1: ++m.stage; break;
        case 2: case 4: break;
        case 3: m.sleep(tick, host.random(30) + 15); break;
        case 5: case 8: host.remove(m); break;
        case 6: host.rotate(m); break;
        case 9:
            m.flags |= 0x400000;
            if (host.hasNext(m)) host.remove(m);
            else { m.stage = 0; m.sleep(tick, host.random(30) + 15); }
            break;
        default: host.clear(); return;
        }
    }
}

} // namespace tak::sim
