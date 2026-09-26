#pragma once
#include <cstdint>
namespace tak::sim {
// Native campaign conditions (523670..524950). Victory conditions are combined
// with AND; defeat conditions with OR by the condition manager.
struct RetailCampaignConditionState {
    enum Kind { MoveUnitToRadius, KillEnemyCommander, DestroyAllUnits, KillAllMobileUnits,
        KillAllOfType, KillUnitType, VictoryTimerRunsOut, UnitTypePassesX, UnitTypePassesZ,
        CommanderKilled, AllUnitsKilled, AllUnitsKilledOfType, UnitTypeKilled,
        DeathTimerRunsOut, AnyUnitPassesX, AnyUnitPassesZ, BuildUnitType, CaptureUnitType };
    bool met=false;
    int32_t remaining=0;
};
inline bool retailCampaignAxis(int cell,int line) {
    const int64_t delta=int64_t(cell)-line;
    return delta>=-2 && delta<=2;
}
inline bool retailCampaignRadius(int32_t x,int32_t z,int32_t centerX,int32_t centerZ,int32_t radius) {
    const int64_t dx=int32_t(uint32_t(x)-uint32_t(centerX));
    const int64_t dz=int32_t(uint32_t(z)-uint32_t(centerZ));
    const int32_t squared=int32_t(uint32_t((dx*dx)>>32)+uint32_t((dz*dz)>>32));
    return squared<=int32_t((int64_t(radius)*radius)>>32);
}
inline void retailCampaignDeath(RetailCampaignConditionState::Kind kind,
        RetailCampaignConditionState& state,int owner,bool matching,bool commander,
        bool mobile,bool survivor) {
    using C=RetailCampaignConditionState;
    switch(kind) {
    case C::KillEnemyCommander: if(owner==1 && commander) state.met=true;break;
    case C::CommanderKilled: if(owner==0 && commander) state.met=true;break;
    case C::KillUnitType:
        if(owner==1 && matching && state.remaining>0 && --state.remaining<=0) state.met=true;
        break;
    case C::UnitTypeKilled:
        if(matching) {state.remaining=int32_t(uint32_t(state.remaining)-1);if(state.remaining<=0)state.met=true;}
        break;
    case C::KillAllMobileUnits: if(owner==1 && mobile && !survivor)state.met=true;break;
    case C::KillAllOfType: if(owner==1 && matching && !survivor)state.met=true;break;
    case C::AllUnitsKilledOfType: if(matching && !survivor)state.met=true;break;
    default:break;
    }
}
} // namespace tak::sim
