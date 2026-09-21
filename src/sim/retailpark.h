#pragma once
#include "fixed.h"
#include "retailmission.h"
#include "retailgoal.h"
#include <optional>
#include <bit>

namespace tak::sim {

struct RetailParkState {
    int target=0;
    uint32_t padding=0,attempts=0;
    int32_t permanent=1;
    std::optional<RetailRingGoal> ring;
};

// Ground branch of 407ca0. The host resolves target lifetime and whether a
// following order can run, and installs the requested annular navigator goal.
template<class Random,class ClearTarget,class ResetGoal>
int retailGroundPark(RetailMissionState& mission,uint32_t events,
        bool hasMover,bool targetPresent,bool targetValid,bool nextReady,
        int16_t footX,int16_t targetFootX,int16_t targetFootZ,
        uint32_t padding,int32_t permanent,uint32_t& attempts,
        Random random,ClearTarget clearTarget,ResetGoal resetGoal) {
    if ((events&8) || (targetPresent && !targetValid)) {
        clearTarget();targetPresent=false;
    }
    if (mission.stage==0) {
        if (!hasMover) return 7;
        attempts=0;return 1;
    }
    if (mission.stage!=1) return 7;
    if ((events&0x100) || nextReady) return 5;
    if (!permanent) {
        const uint32_t first=random(4),second=random(4),third=random(4);
        if (std::bit_cast<int32_t>(attempts)>int32_t(first+second+third)) return 8;
    }
    ++attempts;
    uint32_t radius=uint32_t(int32_t(footX)*16)+padding;
    if (targetPresent) {
        const uint64_t squared=uint64_t(int64_t(targetFootX)*targetFootX+
                                         int64_t(targetFootZ)*targetFootZ);
        radius+=uint32_t(isqrt64(squared*256)/2);
    }
    resetGoal(targetPresent,std::bit_cast<int32_t>(radius*3),std::bit_cast<int32_t>(radius));
    mission.waitMask=0x2708;return 2;
}

} // namespace tak::sim
