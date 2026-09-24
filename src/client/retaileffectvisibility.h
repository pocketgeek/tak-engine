#pragma once

#include "sim/retailexploration.h"
#include <array>
#include <span>
#include <unordered_map>

namespace tak {

// Display-owned current sight. The terrain plane and footprint admission are
// shared with navigation, but these byte counts expire instead of accumulating.
class RetailEffectVisibility {
public:
    RetailEffectVisibility(int width,int height,
            std::vector<sim::RetailExplorationHeight> heights)
        : width_(width),height_(height),heights_(std::move(heights)),
          counts_(size_t(width)*height,0) {}

    void set(sim::RetailSightFootprint& sight,bool active) {
        sim::retailSightFootprint(sight,active,false,width_,height_,0,
            [&](int x,int z) {return heights_[size_t(z)*width_+x];},
            [&](int x,int z,int delta,uint16_t) {
                auto& count=counts_[size_t(z)*width_+x];
                count=uint8_t(int(count)+delta);
            });
    }

    // Feed every simulation unit, including newly dead/embarked records.
    // Navigation supplies the cached geometry; display owns stamp lifetime.
    void track(int id,int owner,int localPlayer,bool eligible,bool alive,
            const sim::RetailSightFootprint& next,uint32_t now) {
        const auto found=revealers_.find(id);
        if (!eligible) {
            if (found!=revealers_.end()) {
                if (!alive && owner==localPlayer) retain(found->second.sight,now,60);
                else set(found->second.sight,false);
                revealers_.erase(found);
            }
            return;
        }
        auto& tracked=revealers_[id];
        auto& old=tracked.sight;
        if (tracked.owner==owner && old.x==next.x && old.z==next.z &&
            old.eyeHeight==next.eyeHeight && old.distance==next.distance &&
            old.sightHeight==next.sightHeight && old.active==next.active) return;
        set(old,false);
        tracked.owner=owner;old=next;old.active=false;
        set(old,next.active);
    }

    // 4c6750 transfers ownership of the stamp without changing its counts.
    // The bounded overflow path removes it immediately.
    void retain(sim::RetailSightFootprint& sight,uint32_t now,uint32_t delay) {
        if (retainedCount_==retained_.size()) {set(sight,false);return;}
        retained_[retainedCount_++]={sight,now+delay};
        sight.active=false;
    }

    // 4c6b30 uses unsigned comparison and retains the deadline tick itself.
    void expire(uint32_t now) {
        size_t kept=0;
        for (size_t i=0;i<retainedCount_;++i) {
            if (retained_[i].deadline<now) set(retained_[i].sight,false);
            else retained_[kept++]=retained_[i];
        }
        retainedCount_=kept;
    }

    // 421d00 tests the projected whole-coordinate cell, including Y lift.
    static bool visible(std::span<const uint8_t> counts,int width,int height,
            const std::array<int32_t,3>& position) {
        const int x=(position[0]>>16)>>5;
        const int z=((position[2]>>16)-((position[1]>>16)>>1))>>5;
        return uint32_t(x)<uint32_t(width) && uint32_t(z)<uint32_t(height) &&
               counts[size_t(z)*width+x]!=0;
    }
    bool visible(const std::array<int32_t,3>& position) const {
        return visible(counts_,width_,height_,position);
    }
    std::span<const uint8_t> counts() const {return counts_;}
    size_t retainedCount() const {return retainedCount_;}

private:
    int width_,height_;
    std::vector<sim::RetailExplorationHeight> heights_;
    std::vector<uint8_t> counts_;
    struct Revealer {sim::RetailSightFootprint sight;int owner=-1;};
    std::unordered_map<int,Revealer> revealers_;
    struct Retained {sim::RetailSightFootprint sight;uint32_t deadline;};
    std::array<Retained,20> retained_{};
    size_t retainedCount_=0;
};

} // namespace tak
