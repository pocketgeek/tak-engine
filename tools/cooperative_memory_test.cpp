#include "sim/navigationmemory.h"
#include <cstdio>
#include <stdexcept>
using namespace tak::sim;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool same(const flow::MemoryPlan& a,const flow::MemoryPlan& b) {
    return a.sharedBytes==b.sharedBytes&&a.profileBytes==b.profileBytes&&a.snapshotBytes==b.snapshotBytes&&
        a.cacheBytes==b.cacheBytes&&a.reservedBytes==b.reservedBytes&&a.limitBytes==b.limitBytes&&
        a.profiles==b.profiles&&a.supported==b.supported;
}
}
int main(){try {
    using Plan=flow::MemoryPlan;
    constexpr uint64_t extra=cooperative::Traffic::memoryLimit+cooperative::Passages::memoryLimit+
        cooperative::MovementBatch::memoryLimit;
    check(cooperative::Traffic::memoryLimit==16*Plan::MiB,"coordinator reservation changed without a resource-budget review");
    check(cooperative::Passages::memoryLimit==Plan::MiB/2,"passage reservation changed without a resource-budget review");
    check(cooperative::MovementBatch::memoryLimit==2*Plan::MiB,"movement reservation changed without a resource-budget review");
    check(Plan::maxRequests>=16000,"navigation cannot admit the supported 16,000-unit cohort");
    for(int width:{64,128,512,1024,2048})for(int height:{64,512,2048}) {
        const auto legacy=Plan::forMap(width,height);
        check(same(navigationMemoryPlan(PathfindingMode::Retail,width,height),legacy),"Retail navigation reservation changed");
        check(same(navigationMemoryPlan(PathfindingMode::Flowfield,width,height),legacy),"Flowfield navigation reservation changed");
        const auto cooperative=navigationMemoryPlan(PathfindingMode::Cooperative,width,height);
        auto reduced=Plan::forMap(width,height,Plan::defaultLimit-extra);
        reduced.limitBytes=Plan::defaultLimit;reduced.sharedBytes+=extra;if(reduced.supported)reduced.reservedBytes+=extra;
        check(same(cooperative,reduced),"coordinator storage was added outside the common memory cap");
        check(cooperative.supported&&cooperative.reservedBytes<=Plan::defaultLimit,"supported map exceeds the 512 MiB navigation budget");
        check(cooperative.profiles<=legacy.profiles,"extra coordinator storage increased profile admission");
    }
    for(int size:{-1,0,2049})check(!navigationMemoryPlan(PathfindingMode::Cooperative,size,128).supported,"invalid map dimensions were admitted");
    check(!navigationMemoryPlan(PathfindingMode::Cooperative,128,128,extra-1).supported,"coordinator exceeds a smaller total cap");
    const auto base=Plan::forMap(128,128);
    const uint64_t one=base.sharedBytes+base.cacheBytes+2*base.snapshotBytes+base.profileBytes+extra;
    check(!navigationMemoryPlan(PathfindingMode::Cooperative,128,128,one-1).supported,"partial profile was admitted at the budget boundary");
    const auto exact=navigationMemoryPlan(PathfindingMode::Cooperative,128,128,one);
    check(exact.supported&&exact.profiles==1&&exact.reservedBytes==one,"exact one-profile budget was rejected or overcharged");
    std::puts("PASS Cooperative resource admission, legacy reservations, and 512 MiB cap");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
