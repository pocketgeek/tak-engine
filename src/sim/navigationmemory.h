#pragma once
#include "cooperative.h"
#include "cooperativemovement.h"
#include "cooperativepassages.h"
#include "flowmemory.h"
#include "pathmode.h"

namespace tak::sim {
// Cooperative's sparse coordinator shares the existing total navigation cap.
// Reserve its worst case before deciding how many terrain profiles can reside.
inline flow::MemoryPlan navigationMemoryPlan(PathfindingMode mode,int width,int height,
                                             uint64_t limit=flow::MemoryPlan::defaultLimit) {
    const uint64_t extra=mode==PathfindingMode::Cooperative?
        cooperative::Traffic::memoryLimit+cooperative::Passages::memoryLimit+
        cooperative::MovementBatch::memoryLimit:0;
    if(limit<extra)return {};
    auto plan=flow::MemoryPlan::forMap(width,height,limit-extra);
    plan.limitBytes=limit;
    plan.sharedBytes+=extra;
    if(plan.supported)plan.reservedBytes+=extra;
    return plan;
}
}
