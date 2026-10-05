#pragma once
#include "flowfield.h"
#include <algorithm>

namespace tak::sim::cooperative {
struct Clearance {
    enum class Result:uint8_t {Open,Narrow,Stale};
    // fullOpen is a proof for this published immutable tile, not a promise
    // about live terrain. Freshness and map boundaries are checked in the
    // original positive-then-negative ray order before using that proof.
    template<class Fresh,class FullOpen>
    static Result screen(const flow::Topology& topology,flow::Cell at,bool transverseX,
                         Fresh&& fresh,FullOpen&& fullOpen,uint64_t& probes,uint64_t& hits) {
        bool stale=false;
        const auto ray=[&](int side) {
            for(int offset=1;offset<=16;) {
                const flow::Cell cell{at.x+(transverseX?side*offset:0),at.z+(transverseX?0:side*offset)};
                const int tile=topology.tileAt(cell);
                if(tile<0){++probes;return false;}
                if(!fresh(tile)){++probes;stale=true;return false;}
                const int coordinate=transverseX?cell.x:cell.z,local=coordinate%flow::kTileSize;
                const int dimension=transverseX?topology.width:topology.height;
                const int length=std::min(17-offset,side>0?
                    std::min(flow::kTileSize-local,dimension-coordinate):local+1);
                if(fullOpen(tile)){++hits;offset+=length;continue;}
                const auto& costs=topology.tiles[size_t(tile)]->cost;
                for(int n=0;n<length;++n) {
                    ++probes;
                    const int index=local+side*n;
                    const size_t position=transverseX?size_t(cell.z%flow::kTileSize)*flow::kTileSize+index:
                        size_t(index)*flow::kTileSize+cell.x%flow::kTileSize;
                    if(!costs[position])return false;
                }
                offset+=length;
            }
            return true;
        };
        const bool open=ray(1)||ray(-1);
        return stale?Result::Stale:open?Result::Open:Result::Narrow;
    }
};
}
