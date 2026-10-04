#include "flowlocal.h"
#include <algorithm>
#include <stdexcept>

namespace tak::sim::flow {
namespace {
uint64_t squareDistance(Cell a,Cell b) {
    const int64_t x=int64_t(a.x)-b.x,z=int64_t(a.z)-b.z;
    return uint64_t(x*x+z*z);
}
}
LocalRoute::LocalRoute(int width,int height,int footX,int footZ,Cell start,Cell target,
                       GoalRegion goal,SnapshotBuilder::Sampler sample)
    :width_(std::min(width,kTileSize)),height_(std::min(height,kTileSize)),start_(start),target_(target),goal_(goal) {
    // Gameplay endpoints are bounded by the map. Keep distance arithmetic
    // bounded too, even for a rejected/imported order outside the world.
    if(width<=0||height<=0||width>2048||height>2048||start.x<0||start.z<0||start.x>=width||start.z>=height)
        throw std::invalid_argument("local flow dimensions/start");
    target_.x=std::clamp(target_.x,0,width-1);target_.z=std::clamp(target_.z,0,height-1);
    origin_={std::clamp(start.x-width_/2,0,width-width_),std::clamp(start.z-height_/2,0,height-height_)};
    at_=best_={start.x-origin_.x,start.z-origin_.z};
    bestScore_=score(best_);
    snapshot_=std::make_unique<SnapshotBuilder>(width_,height_,footX,footZ,
        [origin=origin_,sample=std::move(sample)](int x,int z){return sample(x+origin.x,z+origin.z);},
        nullptr,std::vector<bool>{},Limits{1,4096,0});
}
uint64_t LocalRoute::score(Cell local) const {
    const Cell world{local.x+origin_.x,local.z+origin_.z};
    return goal_.contains(world)?0:squareDistance(world,target_)+1;
}
size_t LocalRoute::step(size_t budget) {
    size_t work=0;
    while(work<budget&&!done()) {
        if(stage_==Stage::Snapshot) {
            work+=snapshot_->step(budget-work);
            if(snapshot_->failed()) {stage_=Stage::Done;continue;}
            if(!snapshot_->done())continue;
            topology_=snapshot_->finish();snapshot_.reset();component_=topology_->componentAt(at_);
            stage_=component_==kUnreachable?Stage::Done:Stage::Choose;
        } else if(stage_==Stage::Choose) {
            ++work;
            if(cursor_==size_t(width_)*height_) {
                if(best_==at_) {stage_=Stage::Done;continue;}
                destination_=std::make_shared<Destination>(topology_,std::vector<Cell>{best_});
                stage_=Stage::Destination;continue;
            }
            const Cell candidate{int(cursor_%width_),int(cursor_/width_)};++cursor_;
            if(topology_->componentAt(candidate)!=component_)continue;
            const auto value=score(candidate),travel=squareDistance(candidate,at_);
            if(value<bestScore_||(value==bestScore_&&travel<bestTravel_)) {
                bestScore_=value;bestTravel_=travel;best_=candidate;
            }
        } else if(stage_==Stage::Destination) {
            work+=destination_->step(budget-work);
            if(destination_->done()) {field_=std::make_unique<FieldBuilder>(destination_,0);stage_=Stage::Field;}
        } else if(stage_==Stage::Field) {
            work+=field_->step(budget-work);
            if(field_->done()) {route_.push_back(start_);stage_=Stage::Trace;}
        } else if(stage_==Stage::Trace) {
            ++work;Cell next;
            if(at_==best_||route_.size()==64) {stage_=Stage::Done;continue;}
            if(!field_->field().next(at_,next)) {route_.clear();stage_=Stage::Done;continue;}
            at_=next;route_.push_back({at_.x+origin_.x,at_.z+origin_.z});
        }
    }
    return work;
}
size_t LocalRoute::bytes() const {
    size_t result=sizeof(*this)+route_.capacity()*sizeof(Cell);
    if(snapshot_)result+=snapshot_->bytes();
    if(topology_)result+=topology_->bytes();
    if(destination_)result+=destination_->bytes();
    if(field_)result+=field_->bytes();
    return result;
}
}
