#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace tak::sim::cooperative {
// One tick's already-computed native moves. Dependencies order live collision
// checks; they never promise that an occupied footprint will become vacant.
class MovementBatch {
public:
    static constexpr size_t maxAttempts=16384,maxBlockers=4;
    static constexpr size_t memoryLimit=2*1024*1024;
    static constexpr size_t probeBudget=65536;
    static constexpr size_t indexSlots=32768;
    struct Attempt {
        uint64_t controller=0;
        int id=0;
        uint32_t issuedTick=0;
        // Expected position after the original refusal and the absolute target
        // computed before it. Retrying this target cannot add a second step.
        int32_t fromX=0,fromZ=0,toX=0,toZ=0,speed=0;
        std::array<int,maxBlockers> blockers{};
        uint8_t footX=1,footZ=1,blockerCount=0,player=0;
    };
    enum class Outcome : uint8_t { Moved,Blocked,Invalid };
    struct Stats {
        uint64_t queued=0,attempted=0,moved=0,blocked=0,cycle=0,deferred=0,invalid=0,probes=0;
    };
    // Overflow/invalid requests fail closed. Callers must also fail closed if
    // their blocker collection exceeds maxBlockers, including diagonal sides.
    bool add(const Attempt&);
    // The callback must revalidate ownership/position and current reservations,
    // then prove the complete live endpoint and diagonal-side footprints before
    // committing. It may use at most four footprint queries, including commit.
    // A missing dependency is checked live: it may already have moved normally.
    Stats flush(uint32_t tick,const std::function<Outcome(const Attempt&)>&);
    void clear();
    size_t size() const {return entries_.size();}
    size_t bytes() const;
private:
    struct Entry {
        Attempt attempt;
        uint32_t first=UINT32_MAX;
        uint8_t remaining=0;
        bool failed=false,finished=false;
    };
    struct Edge {uint32_t next;uint16_t follower;};
    static_assert(sizeof(Entry)<=64 && sizeof(Edge)<=8);
    static_assert(maxAttempts<UINT16_MAX && indexSlots==32768 && indexSlots>=2*maxAttempts);
    static_assert(maxAttempts*(64+maxBlockers*8+sizeof(uint16_t))+indexSlots*sizeof(uint16_t)+4096<=memoryLimit);
    std::vector<Entry> entries_;
    std::vector<Edge> edges_;
    std::vector<uint16_t> ready_,index_;
    Stats pending_;
};
}
