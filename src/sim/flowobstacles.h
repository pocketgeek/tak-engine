#pragma once
#include <array>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace tak::sim::flow {
// Flow-only structure occupancy. Changes are coalesced by stable unit ID; even
// mass construction/destruction has a fixed cell-work budget. Readers use this
// layer only when settled(), so a partially removed footprint is never sampled.
class Obstacles {
public:
    struct Stamp {
        int x=0,z=0,w=0,h=0;
        std::string_view yard; // immutable type-owned data, outlives the layer
        bool open=false;
        int automaticGateOwner=-1; // only c/C cells; same-owner automatic passage
        bool operator==(const Stamp& other) const {
            return x==other.x&&z==other.z&&w==other.w&&h==other.h&&open==other.open&&automaticGateOwner==other.automaticGateOwner&&
                yard.size()==other.yard.size()&&(yard.data()==other.yard.data()||yard==other.yard);
        }
    };
    enum class Edit { Unchanged,Changed,Full };
    Obstacles(int width,int height,size_t limit=32768,size_t gateTileLimit=1024);
    // Full is backpressure: retry after removal work drains. Updates/removals
    // of existing IDs remain admissible at capacity. nullopt removes a body.
    Edit set(int id,std::optional<Stamp> stamp);
    size_t step(size_t budget);
    bool settled() const {return pending_.empty();}
    bool blocked(int x,int z,int player=-1) const;
    // Conservative: allocated owner-permission planes remain relevant even
    // after the last gate is removed, so sharing never depends on live scans.
    bool playerSensitive(int x,int z,int w,int h) const;
    size_t gateTiles() const {return gateTiles_;}
    uint64_t gateLimitHits() const {return gateLimitHits_;}
    size_t bytes() const;
    uint64_t work() const {return work_;}
private:
    struct Entry {
        std::optional<Stamp> installed,desired,adding;
        size_t cursor=0;
        bool started=false,remove=true;
    };
    int width_,height_,cursorId_=0;
    size_t limit_;
    uint64_t work_=0;
    std::vector<uint16_t> count_;
    // Lazily allocated 64x64 permission tiles per owner, at most 8 MiB by
    // default. Never recycle a tile while stamps exist: denied permissions
    // remain denied, so later removals cannot subtract an uninstalled grant.
    using GateTile=std::array<uint16_t,4096>;
    std::vector<std::unique_ptr<GateTile>> gateCounts_;
    size_t gateTileLimit_,gateTiles_=0;
    uint64_t gateLimitHits_=0;
    int tileWidth_=0,tileCount_=0;
    std::map<int,Entry> entries_;
    std::map<int,bool> pending_;
    void cell(const Stamp&,size_t index,bool remove);
};
}
