#pragma once
// Legion convoys (docs/legion-pathfinding.md "Convoys"): the orders one click
// gives a selection are one convoy, even when the client's 64-per-tick uplink
// lands them over several ticks.
//
// The client gives every Legion-shared mover (legionSharedClick) the click
// point itself and every other unit the click plus its offset from the
// selection centroid, clamped to +-60 px per axis. The server cannot recover
// the centroid, so a convoy is anchored on its first SHARED point (the click)
// and every order is tested against it per axis, never as a Euclidean
// distance:
//   - a shared order within 1 cell of the anchor;
//   - a non-shared order within 60 px + 1 cell of the anchor;
//   - before any anchor exists, a shared order within 60 px + 1 cell and a
//     non-shared one within 120 px + 1 cell of EVERY point already taken
//     (tested on the convoy's bounding box).
// A convoy is open while now <= last + kGapTicks and now <= first + kSpanTicks.
// An order matching several open convoys of its player and class takes the
// nearest anchor (or first point while unanchored) by Chebyshev distance,
// then the lowest `first`, then the lowest id; otherwise it opens a convoy.
// Matching runs in the server's command-application order.
//
// Lookup is by index, not by a scan of the open convoys (crowdbench and the AI
// give thousands of distinct points in one tick). `fine` holds the anchored
// convoys by anchor cell (16 px): a shared order probes the 3x3 cells round
// its own (reach 1 cell), a non-shared one the 11x11 (reach 76 px, under 5
// cells). `coarse` holds the unanchored convoys by the 256 px tile of their
// first point, probed 3x3 by every order (reach at most 136 px, under one
// tile); a convoy moves from coarse to fine when it gets its anchor. Each
// probe is one row of cells or tiles (an ordered range), and it and every
// candidate tested count in Stats::tests: a shared order costs 6 probes, a
// non-shared one 14, plus the convoys near it.
//
// Hashed state (World::stateHash folds checksum() while the table is not
// empty): the table only. The indexes are derived from it and checked against
// a rebuild by indexesMatch() under TAK_LEGION_VERIFY.
#include <cstdint>
#include <map>
#include <set>
#include <tuple>

namespace tak::sim {

// The order classes a convoy keeps apart: a fight and a patrol sent to one
// point a moment apart stay separate orders.
enum class ConvoyClass : uint8_t {Move=0,Fight=1,Patrol=2};

class ConvoyTable {
public:
    static constexpr uint32_t kNone=0xffffffffu;   // Order::convoyTick unset
    // Up to 8 ticks with no command between two orders of one convoy (a
    // 512-command uplink window with a round trip of 16 ticks resumes 9 ticks
    // after its last full tick), and at most 33 ticks of commands in all.
    static constexpr uint32_t kGapTicks=9,kSpanTicks=32;
    static constexpr int32_t kCell=16<<16;                    // raw Fixed
    static constexpr int32_t kSharedReach=kCell;              // shared vs anchor
    static constexpr int32_t kOffsetReach=(60<<16)+kCell;     // offset vs anchor; shared vs every point
    static constexpr int32_t kSpreadReach=(120<<16)+kCell;    // offset vs every point
    static constexpr int kFineShift=20,kCoarseShift=24;       // 16 px cells, 256 px tiles
    static constexpr int kOffsetCells=5;                      // kOffsetReach in cells, rounded up

    struct Convoy {
        uint32_t id=0,first=0,last=0;
        int player=0;
        ConvoyClass cls=ConvoyClass::Move;
        bool anchored=false;
        int32_t ax=0,az=0;          // anchor (raw Fixed), valid when anchored
        int32_t fx=0,fz=0;          // first point
        int32_t minX=0,maxX=0,minZ=0,maxZ=0;   // every point taken
    };
    // Observation only, never hashed: per joined order, the index probes plus
    // candidates tested.
    struct Stats {
        uint64_t orders=0,tests=0,testsMax=0,over16=0,over64=0;
    };

    // Join the matching open convoy or open one; returns it.
    const Convoy& join(int player,ConvoyClass cls,int32_t x,int32_t z,bool shared,uint32_t now);
    // The convoy join() would pick, by a scan of the whole table (nullptr: it
    // would open one). The reference the indexes are checked against.
    const Convoy* bruteMatch(int player,ConvoyClass cls,int32_t x,int32_t z,bool shared,uint32_t now) const;
    // Erase every convoy no order at `now` can join. Idempotent per tick.
    void prune(uint32_t now);
    void clear();
    bool empty() const {return table_.empty();}
    size_t size() const {return table_.size();}
    uint64_t checksum() const;
    // Both indexes equal a rebuild from the table, and every convoy is open at `now`.
    bool indexesMatch(uint32_t now) const;
    const Stats& stats() const {return stats_;}
    // Test hook: every open convoy, in hash order (player, class, first, id).
    template<class F> void forEach(F&& f) const {for(const auto& [k,c]:table_)f(c);}

private:
    using Key=std::tuple<int,uint8_t,uint32_t,uint32_t>;                       // player, class, first, id
    using FineKey=std::tuple<int,uint8_t,int32_t,int32_t,uint32_t,uint32_t>;   // player, class, cz, cx, first, id
    using CoarseKey=std::tuple<int,uint8_t,int32_t,int32_t,uint32_t,uint32_t>; // player, class, tz, tx, first, id
    static Key keyOf(const Convoy& c) {return {c.player,uint8_t(c.cls),c.first,c.id};}
    static FineKey fineOf(const Convoy& c);
    static CoarseKey coarseOf(const Convoy& c);
    static bool open(const Convoy& c,uint32_t now) {return now<=c.last+kGapTicks&&now<=c.first+kSpanTicks;}
    static bool accepts(const Convoy& c,int32_t x,int32_t z,bool shared);
    // Is a better than b for an order at (x, z)?
    static bool nearer(const Convoy& a,const Convoy& b,int32_t x,int32_t z);
    void index(const Convoy& c);
    void unindex(const Convoy& c);

    std::map<Key,Convoy> table_;
    std::set<FineKey> fine_;
    std::set<CoarseKey> coarse_;
    uint32_t nextId_=1;
    uint32_t prunedAt_=kNone;   // a cache of the last prune(now); never hashed
    Stats stats_;
};

}
