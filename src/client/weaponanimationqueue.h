#pragma once

#include "sim/retailweapon.h"
#include <deque>

namespace tak {

// Simulation-to-display handoff. The caller owns synchronization; packets wait
// until the render snapshot has reached their tick, and are consumed only once.
class WeaponAnimationQueue {
    struct Packet { uint32_t tick; int unit; RetailWeaponAnimations callbacks; };
    std::deque<Packet> packets_;
public:
    void push(uint32_t tick,int unit,const RetailWeaponAnimations& callbacks) {
        if (!callbacks.count) return;
        // Bound memory if rendering stalls for a long time. Retain recent poses.
        if (packets_.size()>=65536) packets_.pop_front();
        packets_.push_back({tick,unit,callbacks});
    }
    template<class Consume> void drain(uint32_t throughTick,Consume&& consume) {
        while (!packets_.empty() && int32_t(throughTick-packets_.front().tick)>=0) {
            const auto& packet=packets_.front();
            for(unsigned i=0;i<packet.callbacks.count;++i)
                consume(packet.unit,packet.callbacks.events[i]);
            packets_.pop_front();
        }
    }
};

} // namespace tak
