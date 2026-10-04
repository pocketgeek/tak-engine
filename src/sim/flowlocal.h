#pragma once
#include "flowsnapshot.h"

namespace tak::sim::flow {
// Bounded temporary navigation while a shared destination/profile is unavailable.
// A lack of progress in this window is NOT a global unreachable result. The
// caller retains the mission and retries shared admission on its normal schedule.
class LocalRoute {
public:
    LocalRoute(int width,int height,int footX,int footZ,Cell start,Cell target,
               GoalRegion goal,SnapshotBuilder::Sampler sample);
    size_t step(size_t budget);
    bool done() const {return stage_==Stage::Done;}
    bool progresses() const {return done()&&route_.size()>1;}
    // Partial greedy routes are diagnostic progress only, not safe global
    // guidance: on a maze they can undo an already chosen global detour.
    bool reachesGoal() const {return progresses()&&goal_.contains(route_.back());}
    const std::vector<Cell>& route() const {return route_;}
    Cell start() const {return start_;}
    Cell origin() const {return origin_;}
    size_t bytes() const;
private:
    enum class Stage {Snapshot,Choose,Destination,Field,Trace,Done};
    Stage stage_=Stage::Snapshot;
    int width_,height_;
    Cell start_,target_,origin_,best_,at_;
    GoalRegion goal_;
    uint32_t component_=kUnreachable;
    uint64_t bestScore_=0,bestTravel_=0;
    size_t cursor_=0;
    std::unique_ptr<SnapshotBuilder> snapshot_;
    std::shared_ptr<const Topology> topology_;
    std::shared_ptr<Destination> destination_;
    std::unique_ptr<FieldBuilder> field_;
    std::vector<Cell> route_;
    uint64_t score(Cell local) const;
};
}
