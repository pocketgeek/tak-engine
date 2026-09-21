#pragma once

#include <cstdint>
#include <optional>
#include "retailmission.h"
#include "fixed.h"

namespace tak::sim {

// 405756..405809: failed MobileBuild paths may still leave the builder close
// enough to place the site. Each footprint diagonal is truncated separately.
inline bool retailBuildWithinReach(Fixed x,Fixed z,Fixed gx,Fixed gz,
        int16_t fx,int16_t fz,int16_t siteFx,int16_t siteFz,uint16_t reach) {
    const int64_t dx=std::bit_cast<int32_t>(uint32_t(x.v)-uint32_t(gx.v));
    const int64_t dz=std::bit_cast<int32_t>(uint32_t(z.v)-uint32_t(gz.v));
    const int distance=std::bit_cast<int16_t>(uint16_t(isqrt64(uint64_t(dx*dx)+uint64_t(dz*dz))>>16));
    const auto edge=[](int16_t width,int16_t height) {
        return int(isqrt64(uint64_t(int64_t(width)*width+int64_t(height)*height)*64));
    };
    return distance-edge(fx,fz)-edge(siteFx,siteFz)<=reach;
}

struct RetailConstructionResources {
    float stored=0,allocation=1,requested=0,produced=0;
    double totalProduced=0;
    float capacity=1,capacityOverride=0;
    double excess=0;

    // 51d837: income and demand are accumulated by the host in unit-slot
    // order. Retail uses this decimal tick scale, not exact 1/30.
    void beginTick(float income,float storage,float demand) {
        const double gain=double(capacityOverride>0 ? 0.f : income)*0.03333333;
        stored=float(double(stored)+gain);
        totalProduced+=gain;
        produced=float(double(produced)+gain);
        capacity=storage>0 ? storage : 1.f;
        const double required=double(demand)*0.03333333;
        allocation=required>double(stored) ? float(double(stored)/required) : 1.f;
    }

    // 401270 clamps only after unit work, immediately before history rolls.
    void finishTick() {
        if (capacityOverride>0) capacity=capacityOverride;
        if (stored<0) { excess+=double(stored);stored=0; }
        if (stored>capacity) {
            excess+=double(stored)-double(capacity);stored=capacity;
        }
    }
};

struct RetailConstructionProgress {
    float remaining=1;
    uint16_t hp=0;
    uint32_t events=0,flags=0;
};

// COB BUILD_PERCENT_LEFT (50d1ab): unfinished work maps to 1..100.
inline uint32_t retailConstructionPercent(float remaining) {
    return remaining==0 ? 0u : uint32_t(1-int32_t(double(remaining)*-99.0));
}

struct RetailConstructionType {
    float inverseTime=0,cost=0;
    uint32_t maxHp=0;
};

// 4023fd..402490: GetBuilt while work remains. Completion, weapon hosts and
// unconjure accounting are separate operations; the mission owns their timing.
template<class StopWeapons,class Decay>
int retailGetBuiltWaiting(RetailMissionState& mission,uint32_t tick,uint32_t events,
                          StopWeapons stopWeapons,Decay decay) {
    switch (mission.stage) {
    case 0:
        stopWeapons();mission.sleep(tick,300);mission.waitMask|=0x10000000u;return 1;
    case 1:
        mission.sleep(tick,30);mission.waitMask|=0x10000000u;return 1;
    case 2:
        if (events&0x10000000u) mission.sleep(tick,30);
        else if (events&1) { mission.sleep(tick,1);decay(); }
        mission.waitMask|=0x10000000u;return 2;
    default:return 7;
    }
}

struct RetailConstructionSite {
    RetailConstructionProgress progress;
    RetailConstructionType type;
    std::optional<RetailMissionState> mission;
    int builder=0;
};

struct RetailConstructionJob {
    int target=0;
    bool flying=false;
    uint32_t flyingOwnerFlags=0;
    int32_t flyingSlowSpeed=0,flyingFastSpeed=0;
    uint16_t flyingBuildDistance=0;
    float workerTime=0;
    bool working=false;
    uint32_t activityDeadline=0;
    std::optional<uint16_t> turnHeading; // inserted Turn mission before build-stance wait
    RetailMissionState mission;
};

// 429990's completion eligibility and notifications. The host performs the
// returned attachment, activation and network effects in that order.
struct RetailConstructionCompletion {
    bool builderPresent=false,forced=false,sameUnit=false,allied=false;
    uint32_t builderFlags=0,builderTypeFlags=0,builderSecondaryFlags=0,siteTypeFlags=0;
    bool ownerPresent=false,attached=false;
    uint8_t ownerRole=0,deathType=0;
};
struct RetailConstructionCompletionEffects {
    bool applied=false,detach=false,activate=false,notify=false;
    uint8_t deathType=0;
};
inline RetailConstructionCompletionEffects retailCompleteConstruction(
        RetailConstructionProgress& site,const RetailConstructionCompletion& input) {
    RetailConstructionCompletionEffects effects;effects.deathType=input.deathType;
    const bool builderAllocated=input.builderPresent && (input.builderFlags&0x1000000u);
    if ((!input.forced && !builderAllocated) || !(site.flags&0x1000000u)) return effects;
    if (builderAllocated) {
        const bool constructionAllowed=(input.builderTypeFlags&0x100u) &&
            (!(input.builderSecondaryFlags&0x2000000u) || input.allied);
        if (!constructionAllowed && !(input.builderSecondaryFlags&0x20001000u) && !input.sameUnit)
            return effects;
    }
    effects.applied=true;site.remaining=0;site.flags|=0x800u;
    const bool controlled=input.ownerPresent && (input.ownerRole==1 || input.ownerRole==2);
    effects.detach=controlled && !(site.flags&0x2000000u) && input.attached;
    effects.activate=(input.siteTypeFlags&0x40000u)!=0;
    if (input.siteTypeFlags&0x1000000u) { effects.deathType=9;site.flags|=0x1000u; }
    effects.notify=controlled && !input.forced;
    return effects;
}

// 429af0: work is the builder's per-update contribution. Negative work is
// unconjuring. Requested consumption is recorded BEFORE affordability scaling.
// Completion/death callbacks remain with the host; flags and return behavior
// are independent of whether the caller chooses to display build particles.
enum class RetailConstructionResult { Idle, Worked, Completed, Removed };
inline RetailConstructionResult retailConstructionWork(
        RetailConstructionProgress& site,const RetailConstructionType& type,
        RetailConstructionResources& resources,float work) {
    if (site.remaining==0) return RetailConstructionResult::Idle;
    double amount;
    if (work>=0) {
        site.events|=0x10000000u;
        amount=double(resources.allocation)*double(work);
    } else amount=-double(work);
    if (amount<=0) return RetailConstructionResult::Idle;
    double progress=amount*double(type.inverseTime);
    float cost=float(progress*double(type.cost));
    if (work>0) {
        resources.requested=float(double(resources.requested)+
            double(type.inverseTime)*double(type.cost)*double(work));
        if (cost>resources.stored && cost>0) {
            const double fraction=resources.stored>0 ? double(resources.stored)/double(cost) : 0;
            cost=float(fraction*double(cost));
            progress*=fraction;
        }
        site.remaining=float(double(site.remaining)-progress);
        resources.stored=float(double(resources.stored)-double(cost));
        if (site.remaining<=0) {
            site.remaining=0;site.hp=uint16_t(type.maxHp);
            return RetailConstructionResult::Completed;
        }
        site.hp=uint16_t(uint32_t((1.0-double(site.remaining))*double(type.maxHp)));
        site.flags|=0x800u;
        return RetailConstructionResult::Worked;
    }
    const double remaining=double(site.remaining)+progress;
    site.remaining=float(remaining);
    if (remaining>=1) { site.hp=0;return RetailConstructionResult::Removed; }
    site.hp=uint16_t(uint32_t((1.0-remaining)*double(type.maxHp)));
    site.flags|=0x800u;
    resources.stored=float(double(resources.stored)+double(cost));
    resources.totalProduced+=double(cost);
    resources.produced=float(double(resources.produced)+double(cost));
    return RetailConstructionResult::Idle;
}

} // namespace tak::sim
