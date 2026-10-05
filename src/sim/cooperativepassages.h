#pragma once
#include "cooperative.h"
#include "flowfield.h"
#include <functional>
#include <map>
#include <optional>
#include <tuple>

namespace tak::sim::cooperative {
// Sparse probes of immutable, footprint-eroded terrain. A descriptor identifies
// a cardinal strip between wider areas, in raw terrain-cell geometry. Short,
// bounded bays between matching narrow sections share the same passage permit.
// Curves, offset walls and footprint-erased short bays need local coordination.
class Passages {
public:
    enum class Status:uint8_t {Pending,None,Found};
    struct Result {Status status=Status::None;std::optional<Traffic::Passage> passage;};
    struct Limits {size_t probesPerTick=4096,probesPerCall=128,entries=512;int maxWidth=16;};
    struct Stats {uint64_t probes=0,hits=0,evictions=0;size_t entries=0,pending=0,remaining=0;};
    static constexpr size_t memoryLimit=512*1024;
    static constexpr uint32_t pendingLifetimeTicks=256;
    Passages();
    explicit Passages(Limits);
    // profile/generation are stable deterministic identities, never addresses.
    // Change generation (or invalidate) on dirty geometry, including while a
    // replacement topology is pending. Pending must not authorize entry.
    Result probe(uint32_t tick,uint64_t profile,uint64_t generation,const flow::Topology&,
                 int footX,int footZ,flow::Cell anchor,flow::Cell direction);
    // The engine calls this every tick before route delivery. Pending proofs
    // make progress even when a combat request exhausts the delivery quota.
    // Return nullptr for stale/unavailable generations; no pointer is retained.
    void advance(uint32_t tick,const std::function<const flow::Topology*(uint64_t,uint64_t)>&);
    void invalidate(uint64_t profile);
    void clear();
    size_t bytes() const;
    uint64_t checksum() const;
    Stats stats() const;
private:
    using Cell=flow::Cell;
    struct Key {
        uint64_t profile=0,generation=0;
        int width=0,height=0,footX=0,footZ=0,x=0,z=0,axis=0;
        auto operator<=>(const Key&) const=default;
    };
    struct Entry {
        // cross: 0=center, 1=negative transverse scan, 2=positive scan.
        // side: 0=seed section, 1=negative axial scan, 2=positive scan.
        int section=0,across=0,low=0,high=0,baseLow=0,baseHigh=0,first=0,last=0;
        int bayCells=0,bayWidth=0,extentWidth=0;
        uint8_t cross=0,side=0;
        bool complete=false;
        std::optional<Traffic::Passage> passage;
        uint64_t used=0;
        uint32_t admitted=0,worked=0,seen=0;
        bool hasAdmission=false,hasWorked=false;
    };
    static constexpr size_t baseCharge=4096,entryCharge=512;
    static_assert(sizeof(std::pair<const Key,Entry>)+64<=entryCharge);
    Limits limits_;
    std::map<Key,Entry> entries_;
    // A small deterministic working set shares a proved strip across nearby
    // route anchors without a per-unit alias table or a full-map cell index.
    std::array<Key,32> recent_{};
    size_t recentCount_=0;
    std::optional<Key> cursor_;
    uint64_t clock_=0,probes_=0,hits_=0,evictions_=0;
    uint32_t tick_=0;
    size_t remaining_=0,admissions_=0;
    bool started_=false,autonomous_=false;
    void begin(uint32_t tick);
    void remember(const Key&);
    bool room();
    void advance(const Key&,Entry&,const flow::Topology&);
    Result result(const Entry&,bool reverse) const;
};
}
