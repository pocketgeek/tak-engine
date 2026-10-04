#include "sim/flowmemory.h"
#include <cstdio>
#include <stdexcept>
using tak::sim::flow::MemoryPlan;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try {
    uint64_t previous=0;
    for(int dim:{1,63,64,65,128,512,1024,2048}) {
        const auto plan=MemoryPlan::forMap(dim,dim);
        check(plan.supported&&plan.profiles>0&&plan.profiles<=MemoryPlan::maxProfiles,"supported map loses all profiles");
        check(plan.reservedBytes<=plan.limitBytes,"reservation exceeds cap");
        check(plan.profileBytes>=previous,"larger map reserves less per profile");previous=plan.profileBytes;
        const auto exact=MemoryPlan::forMap(dim,dim,plan.sharedBytes+plan.cacheBytes+MemoryPlan::maxSnapshots*plan.snapshotBytes+plan.profileBytes);
        check(exact.supported&&exact.profiles==1,"exact one-profile boundary rejected");
        check(!MemoryPlan::forMap(dim,dim,plan.sharedBytes+plan.cacheBytes+MemoryPlan::maxSnapshots*plan.snapshotBytes+plan.profileBytes-1).supported,"undersized profile reservation accepted");
        const auto two=MemoryPlan::forMap(dim,dim,plan.sharedBytes+plan.cacheBytes+MemoryPlan::maxSnapshots*plan.snapshotBytes+2*plan.profileBytes);
        check(two.profiles==2&&two.reservedBytes==two.limitBytes,"two-profile admission differs");
    }
    for(auto dimensions:{std::pair{0,64},std::pair{-1,64},std::pair{2049,64},std::pair{64,2049},std::pair{2147483647,2147483647}})
        check(!MemoryPlan::forMap(dimensions.first,dimensions.second).supported,"unsupported dimensions accepted");
    check(!MemoryPlan::forMap(64,64,0).supported,"zero cap accepted");
    const auto max=MemoryPlan::forMap(2048,2048);
    check(max.profiles==8,"largest supported map lost the eight-profile reservation");
    check(MemoryPlan::maxSnapshots==2&&max.snapshotBytes>max.profileBytes,"scratch is not separately bounded");
    check(max.cacheBytes>=MemoryPlan::maxCachedTiles*MemoryPlan::tileBytes,"cache tiles are unreserved");
    check(MemoryPlan::requestBytes>=1536,"request reservation omits firing seeds or traffic metadata");
    std::printf("PASS flow storage reservation: 64x64 profiles=%zu shared=%llu profile=%llu reserved=%llu cap=%llu bytes\n",max.profiles,
        (unsigned long long)max.sharedBytes,(unsigned long long)max.profileBytes,(unsigned long long)max.reservedBytes,(unsigned long long)max.limitBytes);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
