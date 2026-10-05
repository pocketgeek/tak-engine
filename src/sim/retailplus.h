#pragma once
#include "cooperative.h"

namespace tak::sim {
struct UnitType;
struct Order;
namespace retailplus {
// Local coordination over the existing Retail route. This policy owns no
// terrain field, passage finder, corridor sampler or deferred movement batch.
class Traffic {
public:
    using Context=flow::Traffic::Context;
    using Result=flow::Traffic::Result;
    using Cell=flow::Cell;
    using Stats=cooperative::Traffic::Stats;
    // The local coordinator has a 16 MiB cap. Reserve the inherited arrival
    // records/slots conservatively at the same 32 MiB as shared-navigation
    // requests, even though this adapter owns no field requests.
    static constexpr size_t memoryLimit=48*1024*1024;
    static constexpr size_t pruneBudget=64;
    // Shipped ground movers fit within this bound. Larger modified bodies use
    // native routing rather than repeatedly exhausting a small local search.
    static constexpr int maxFootprint=4;

    // Call on the issued goal (World::currentLeg), never a path waypoint.
    // Special transient front orders must also remain in the Retail handler.
    static bool supports(const UnitType&,const Order&);
    // Conservative whole-footprint query counts for complete supercover rays.
    // Unaffordable proofs are unavailable, rather than deferred every tick.
    static uint64_t arrivalProofCost(Cell from,Cell slot,Cell goal,int footX,int footZ);
    static uint64_t contactProofCost(Cell from,Cell peer,int footX,int footZ);
    void registerMove(const Context&);
    Result update(const Context&);
    bool updateUnblocked(const Context&);
    // Immediate local claim and uncompleted slot release. Verified settled
    // records remain contact anchors until the prune callback rejects them.
    void cancel(int id);
    void reset();
    void setAllianceMask(int player,uint16_t mask);
    void prune(size_t budget,const std::function<bool(int,int,uint64_t,Cell,bool)>& valid);
    bool settled(int id,Cell position) const;
    void refreshSettled(int id,Cell position);
    int arrivalRadiusSquared(const Context&) const;
    bool nearArrival(const Context&) const;
    bool needsArrivalNeighbors(const Context&) const;
    uint64_t checksum() const;
    size_t bytes() const;
    Stats stats() const;
private:
    static bool ordinary(const Context&);
    cooperative::Traffic local_;
};
}
}
