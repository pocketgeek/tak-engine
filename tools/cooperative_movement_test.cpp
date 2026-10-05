#include "sim/cooperativemovement.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

using tak::sim::cooperative::MovementBatch;
namespace {
uint64_t digest=1469598103934665603ull;
void fold(uint64_t value) {digest^=value;digest*=1099511628211ull;}
void check(bool good,const char* what) {
    if (!good) {std::cerr<<"FAIL: "<<what<<'\n';std::exit(1);}
}
struct Body {int x=0,z=0,foot=2;uint64_t controller=1;uint8_t player=0;};
struct Scene {
    std::map<int,Body> bodies;
    std::vector<int> committed;
    bool free(int id,int x,int z,int foot) const {
        for (const auto& [other,b]:bodies) if (other!=id &&
            x<b.x+b.foot && b.x<x+foot && z<b.z+b.foot && b.z<z+foot) return false;
        return true;
    }
    MovementBatch::Outcome commit(const MovementBatch::Attempt& a) {
        auto it=bodies.find(a.id);
        if (it==bodies.end() || it->second.controller!=a.controller || it->second.player!=a.player ||
            it->second.x!=a.fromX || it->second.z!=a.fromZ) return MovementBatch::Outcome::Invalid;
        auto& b=it->second;
        // This fixture models exact one-anchor native displacement. The real
        // adapter additionally checks terrain, reservations and raw positions.
        check(std::abs(a.toX-b.x)<=1 && std::abs(a.toZ-b.z)<=1,"retry cannot add a second step");
        if (!free(a.id,a.toX,a.toZ,b.foot) ||
            (a.toX!=b.x && a.toZ!=b.z &&
             (!free(a.id,a.toX,b.z,b.foot) || !free(a.id,b.x,a.toZ,b.foot))))
            return MovementBatch::Outcome::Blocked;
        b.x=a.toX;b.z=a.toZ;committed.push_back(a.id);return MovementBatch::Outcome::Moved;
    }
    void legal() const {
        for (const auto& [id,b]:bodies) {
            check(free(id,b.x,b.z,b.foot),"all final footprints remain disjoint");
            fold(uint64_t(id));fold(uint64_t(b.x));fold(uint64_t(b.z));
        }
    }
};
MovementBatch::Attempt attempt(int id,int x,int z,int tx,int tz,std::initializer_list<int> blockers={}) {
    MovementBatch::Attempt a;a.id=id;a.controller=1;a.issuedTick=1;
    a.fromX=x;a.fromZ=z;a.toX=tx;a.toZ=tz;a.speed=123;
    a.footX=a.footZ=2;a.blockerCount=uint8_t(blockers.size());
    std::copy(blockers.begin(),blockers.end(),a.blockers.begin());return a;
}
void linePropagatesActualVacancy() {
    MovementBatch batch;Scene scene;
    for (int id=1;id<=8;++id) scene.bodies[id]={2*(id-1),0};
    for (int id=1;id<8;++id) {
        const int x=2*(id-1);
        check(!scene.free(id,x+1,0,2),"original follower move is physically refused");
        check(batch.add(attempt(id,x,0,x+1,0,{id+1})),"admit refused follower");
    }
    // The native unit loop really moves the leader before dependency flush.
    ++scene.bodies[8].x;
    const auto out=batch.flush(4,[&](const auto& a){return scene.commit(a);});
    check(out.moved==7 && out.attempted==7 && out.blocked==0,"one actual vacancy releases the whole line");
    check(scene.committed==std::vector<int>({7,6,5,4,3,2,1}),"dependent bodies commit leader first");
    check(batch.size()==0 && batch.bytes()<=MovementBatch::memoryLimit,"batch is transient and fits reserved memory");
    scene.legal();fold(out.probes);
}
void diagonalNeedsBothVacancies() {
    MovementBatch batch;Scene scene;
    scene.bodies={{1,{0,0}},{2,{2,0}},{3,{0,2}},{4,{2,2}}};
    batch.add(attempt(1,0,0,1,1,{2,3,4}));
    batch.add(attempt(2,2,0,3,1,{4}));batch.add(attempt(3,0,2,1,3,{4}));
    scene.bodies[4].x=scene.bodies[4].z=3;
    const auto out=batch.flush(0,[&](const auto& a){return scene.commit(a);});
    check(out.moved==3 && scene.committed.back()==1,"diagonal follower waits for both side bodies");
    scene.legal();fold(out.probes);

    MovementBatch blocked;Scene side;
    side.bodies={{1,{0,0}},{2,{2,-1}}};
    blocked.add(attempt(1,0,0,1,1));
    const auto refused=blocked.flush(0,[&](const auto& a){return side.commit(a);});
    check(refused.blocked==1 && refused.moved==0,"an unlisted occupied diagonal side still blocks live commit");
    side.legal();
}
void stoppedLeaderAndCyclesWait() {
    MovementBatch batch;Scene scene;
    scene.bodies={{1,{0,0}},{2,{2,0}},{3,{4,0}}};
    batch.add(attempt(1,0,0,1,0,{2}));batch.add(attempt(2,2,0,3,0,{3}));
    const auto out=batch.flush(0,[&](const auto& a){return scene.commit(a);});
    check(out.attempted==1 && out.blocked==1 && out.deferred==1 && out.moved==0,
        "a stationary leader cannot promise a future vacancy");
    scene.legal();
    batch.add(attempt(1,0,0,1,0,{2}));batch.add(attempt(2,2,0,1,0,{1}));
    const auto cycle=batch.flush(3,[](const auto&){check(false,"cycle must not invoke commit");return MovementBatch::Outcome::Moved;});
    check(cycle.cycle==2 && cycle.attempted==0,"dependency cycles wait without swapping occupied footprints");
    fold(cycle.cycle);
}
void ownershipAndDuplicateProposals() {
    MovementBatch batch;Scene scene;scene.bodies[1]={0,0};
    batch.add(attempt(1,0,0,1,0));scene.bodies[1].controller=2;
    check(batch.flush(0,[&](const auto& a){return scene.commit(a);}).invalid==1,"new mission invalidates saved displacement");
    scene.bodies[1].controller=1;batch.add(attempt(1,0,0,1,0));scene.bodies[1].x=-1;
    check(batch.flush(0,[&](const auto& a){return scene.commit(a);}).invalid==1,"later displacement invalidates saved start");
    scene.bodies[1].x=0;batch.add(attempt(1,0,0,1,0));scene.bodies[1].player=1;
    check(batch.flush(0,[&](const auto& a){return scene.commit(a);}).invalid==1,"capture invalidates saved player ownership");
    batch.add(attempt(1,0,0,1,0));batch.add(attempt(1,0,0,1,0));
    const auto duplicate=batch.flush(0,[](const auto&){check(false,"duplicate must not execute");return MovementBatch::Outcome::Moved;});
    check(duplicate.invalid==2,"duplicate proposals fail closed instead of moving twice");
    batch.add(attempt(1,0,0,1,0));batch.clear();
    check(batch.flush(1,[](const auto&){check(false,"cleared attempts cannot survive tick");return MovementBatch::Outcome::Moved;}).queued==0,
        "tick reset removes all pending ownership");
}
void fixedCapacityAndWork() {
    MovementBatch batch;auto invalid=attempt(1,0,0,1,0);invalid.footX=65;
    check(!batch.add(invalid),"oversized footprint is rejected before callbacks");
    invalid=attempt(1,0,0,1,0);invalid.blockerCount=5;
    check(!batch.add(invalid),"too many blockers must not be silently truncated");
    batch.clear();
    std::array<unsigned,64> calls{};
    for (uint32_t tick=0;tick<64;++tick) {
        for (int id=1;id<=64;++id) {auto a=attempt(id,0,0,1,0);a.footX=a.footZ=32;batch.add(a);}
        unsigned ordinal=0;
        const auto out=batch.flush(tick,[&](const auto& a) {
            check(a.id==int((tick+ordinal)%64+1),"ready quota honors exact cyclic ID priority");
            ++ordinal;++calls[size_t(a.id-1)];fold(uint64_t(a.id));return MovementBatch::Outcome::Moved;
        });
        check(out.probes<=MovementBatch::probeBudget && out.attempted==16,"weighted query work has a fixed ceiling");
        fold(out.probes);
    }
    for (unsigned count:calls) check(count==16,"ready callers rotate under expensive footprint quota");
    for (size_t i=0;i<MovementBatch::maxAttempts;++i) check(batch.add(attempt(int(i+1),0,0,1,0)),"fixed capacity admits its full limit");
    check(!batch.add(attempt(int(MovementBatch::maxAttempts+1),0,0,1,0)),"capacity overflow defers");
    check(batch.bytes()<=MovementBatch::memoryLimit,"all admitted storage remains under canonical reservation");
    batch.clear();
}
void maximumChainIsIterative() {
    MovementBatch batch;
    for (size_t i=0;i<MovementBatch::maxAttempts;++i) {
        const int id=int(i+1);auto a=attempt(id,id,0,id+1,0,{id+1});
        a.footX=a.footZ=1;check(batch.add(a),"maximum chain admits every bounded node");
    }
    int expected=int(MovementBatch::maxAttempts);
    const auto out=batch.flush(19,[&](const auto& a) {
        check(a.id==expected,"long dependency chain always visits the actual leader first");
        --expected;return MovementBatch::Outcome::Moved;
    });
    check(expected==0 && out.moved==MovementBatch::maxAttempts && out.probes==MovementBatch::probeBudget,
        "maximum chain uses bounded iterative work without recursion");
    fold(out.moved);fold(out.probes);
}
void collidingLargeIdsKeepDependencies() {
    // More than64 IDs sharing the bounded index's bucket force its fallback.
    // Invert the odd32-bit multiplier so this does not require a large search.
    constexpr uint32_t multiplier=2654435761u;
    uint32_t inverse=1;
    for(int i=0;i<5;++i)inverse*=2u-multiplier*inverse;
    std::vector<int> ids;
    for(uint32_t low=0;ids.size()<80;++low) {
        const uint32_t id=((7u<<17)|low)*inverse;
        if(id>0&&id<=uint32_t(INT32_MAX))ids.push_back(int(id));
    }
    MovementBatch batch;
    for(size_t i=0;i<ids.size();++i)
        batch.add(attempt(ids[i],0,0,1,0,{i+1<ids.size()?ids[i+1]:INT32_MAX}));
    size_t expected=ids.size();
    const auto out=batch.flush(19,[&](const auto& a) {
        check(expected&&a.id==ids[--expected],"index collisions cannot drop or replace a dependency");
        fold(uint64_t(a.id));return MovementBatch::Outcome::Moved;
    });
    check(expected==0&&out.moved==ids.size()&&out.cycle==0,
        "overflowed index and absent leader use the exact sorted fallback");
    check(batch.bytes()<=MovementBatch::memoryLimit,"bounded index fits the existing reservation");
}
}
int main() {
    linePropagatesActualVacancy();diagonalNeedsBothVacancies();stoppedLeaderAndCyclesWait();
    ownershipAndDuplicateProposals();fixedCapacityAndWork();maximumChainIsIterative();collidingLargeIdsKeepDependencies();
    std::cout<<"cooperative movement tests PASS hash="<<std::hex<<digest<<'\n';
}
