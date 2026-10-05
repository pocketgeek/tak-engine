#include "retailplus.h"
#include "sim.h"
#include <algorithm>
#include <limits>

namespace tak::sim::retailplus {
namespace {
uint64_t raySamples(flow::Cell from,flow::Cell to) {
    const int64_t x=int64_t(to.x)-from.x,z=int64_t(to.z)-from.z;
    const uint64_t dx=uint64_t(x<0?-x:x),dz=uint64_t(z<0?-z:z);
    return 1+std::max(dx,dz)+2*std::min(dx,dz);
}
uint64_t weightedSamples(uint64_t samples,int footX,int footZ) {
    constexpr auto maximum=std::numeric_limits<uint64_t>::max();
    if(footX<=0||footZ<=0)return maximum;
    const uint64_t area=uint64_t(footX)*uint64_t(footZ);
    return samples>maximum/area?maximum:samples*area;
}
}
uint64_t Traffic::arrivalProofCost(Cell from,Cell slot,Cell goal,int footX,int footZ) {
    return weightedSamples(2*raySamples(from,slot)+raySamples(slot,goal),footX,footZ);
}
uint64_t Traffic::contactProofCost(Cell from,Cell peer,int footX,int footZ) {
    return weightedSamples(raySamples(from,peer),footX,footZ);
}
bool Traffic::supports(const UnitType& type,const Order& goal) {
    if(!type.canMove||type.isStructure()||type.canFly||type.domain!=UnitType::Domain::Ground||
       type.footX<1||type.footX>maxFootprint||type.footZ<1||type.footZ>maxFootprint||
       type.isBuilder||type.builderLimited||type.buildDist>0||type.canReclaim||
       type.canResurrect||type.canCapture||type.canTransport||type.transportCap>0||
       type.transportSizeCap>0||type.maxTransportSize>0)return false;
    return goal.goal&&goal.groundMission&&!goal.targetId&&!goal.attackMove&&!goal.patrol&&
        !goal.guard&&!goal.load&&!goal.unload&&!goal.buildType&&!goal.reclaimFeat&&
        !goal.reclaimArea&&!goal.manaBuildArea&&!goal.repairTarget&&!goal.patrolRepair&&
        !goal.autoTarget&&!goal.landing&&!goal.wait&&!goal.waitAttack&&!goal.productionExit&&!goal.nativeProductionExit&&
        !goal.transportPickup&&!goal.transportProductionAhead&&!goal.transportUnloadApproach&&
        !goal.transportUnloadReleasePending&&!goal.transportUnloadTransferDeferred&&
        !goal.transportTicks&&!goal.transportPassenger&&!goal.flightMoveMission&&
        !goal.park&&!goal.buildRectangle&&!goal.flightGoal&&!goal.hoverAttackRefresh;
}

bool Traffic::ordinary(const Context& c) {
    return c.id>0&&c.player>=0&&c.player<16&&c.plainMove&&!c.targetId&&
        !(c.missionKind&~1u)&&c.footX>=1&&c.footX<=maxFootprint&&c.footZ>=1&&c.footZ<=maxFootprint;
}
void Traffic::registerMove(const Context& c) {
    if(ordinary(c))local_.registerMove(c);
    else cancel(c.id);
}
Traffic::Result Traffic::update(const Context& c) {
    if(!ordinary(c)) {cancel(c.id);return {};}
    // Retail+ has no published footprint topology to authorize the optional
    // open-ground optimizations. Use the bounded local policy only; its idle
    // blocker retry delay remains valid without pretending a tile is open.
    Context local=c;local.cooperativeOpenTerrain=false;
    // Native mission acceptance must still describe a legal standing body.
    // A body refusal or a consumed Retail prefix is never goal acceptance.
    const bool rejectedNativeArrival=local.goalReached&&(!local.free||!local.free(local.position));
    if(rejectedNativeArrival)local.goalReached=false;
    auto result=local_.update(local);
    // World also checks the native goal circle after traffic. Keep ownership
    // while its standing footprint is illegal instead of allowing that second
    // geometric check to retire the mission despite this rejected proof.
    if(rejectedNativeArrival){result.arrivalApproach=true;result.settled=false;}
    result.followLeader=0;result.settledRally=false;
    return result;
}
void Traffic::cancel(int id) {local_.cancel(id);local_.cancelUnsettledArrival(id);}
void Traffic::reset() {local_=cooperative::Traffic{};}
void Traffic::setAllianceMask(int player,uint16_t mask) {local_.setAllianceMask(player,mask);}
void Traffic::prune(size_t budget,const std::function<bool(int,int,uint64_t,Cell,bool)>& valid) {
    local_.prune(std::min(budget,pruneBudget),valid);
}
bool Traffic::settled(int id,Cell position) const {return local_.settled(id,position);}
void Traffic::refreshSettled(int id,Cell position) {local_.refreshSettled(id,position);}
int Traffic::arrivalRadiusSquared(const Context& c) const {return ordinary(c)?local_.arrivalRadiusSquared(c):0;}
bool Traffic::nearArrival(const Context& c) const {return ordinary(c)&&local_.nearArrival(c);}
bool Traffic::needsArrivalNeighbors(const Context& c) const {return ordinary(c)&&local_.needsArrivalNeighbors(c);}
uint64_t Traffic::checksum() const {return local_.checksum();}
size_t Traffic::bytes() const {return sizeof(*this)-sizeof(local_)+local_.bytes();}
Traffic::Stats Traffic::stats() const {auto out=local_.stats();out.bytes=bytes();return out;}
}
