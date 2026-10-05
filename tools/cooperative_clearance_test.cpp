#include "sim/cooperativeclearance.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace tak::sim;
namespace {
using Clearance=cooperative::Clearance;
using Result=Clearance::Result;
void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
flow::Topology grid(int width,int height) {
    flow::Topology map;map.width=width;map.height=height;map.tilesX=(width+63)/64;map.tilesZ=(height+63)/64;
    for(int n=0;n<map.tilesX*map.tilesZ;++n) {auto tile=std::make_shared<flow::Tile>();tile->cost.fill(4);map.tiles.push_back(tile);}
    return map;
}
void block(flow::Topology& map,flow::Cell cell) {
    const size_t tile=size_t(map.tileAt(cell));auto copy=std::make_shared<flow::Tile>(*map.tiles[tile]);
    copy->cost[size_t(cell.z%64)*64+cell.x%64]=0;map.tiles[tile]=copy;
}
std::vector<bool> proofs(const flow::Topology& map) {
    std::vector<bool> result;
    for(const auto& tile:map.tiles)result.push_back(std::all_of(tile->cost.begin(),tile->cost.end(),[](auto cost){return cost>0;}));
    return result;
}
template<class Fresh>
Result original(const flow::Topology& map,flow::Cell at,bool axis,Fresh&& fresh) {
    bool stale=false;
    const auto ray=[&](int side){for(int offset=1;offset<=16;++offset){
        const flow::Cell cell{at.x+(axis?side*offset:0),at.z+(axis?0:side*offset)};
        const int tile=map.tileAt(cell);if(tile>=0&&!fresh(tile)){stale=true;return false;}
        if(!map.cost(cell))return false;
    }return true;};
    const bool open=ray(1)||ray(-1);return stale?Result::Stale:open?Result::Open:Result::Narrow;
}
void sharesPublishedProofs() {
    auto map=grid(192,192);auto bits=proofs(map);uint64_t reads=0,hits=0;
    const auto fresh=[](int){return true;};const auto open=[&](int tile){return bits[size_t(tile)];};
    for(int z=40;z<80;++z)for(int x=40;x<80;++x)for(bool axis:{false,true})
        check(Clearance::screen(map,{x,z},axis,fresh,open,reads,hits)==Result::Open,"whole-tile proof rejected an open ray");
    check(!reads&&hits>3200,"neighboring routes repeated whole-tile terrain reads");
    // A separately published footprint/profile snapshot supplies its own bits.
    auto narrow=map;block(narrow,{48,49});block(narrow,{48,47});auto narrowBits=proofs(narrow);
    check(Clearance::screen(narrow,{48,48},false,fresh,[&](int tile){return narrowBits[size_t(tile)];},reads,hits)==Result::Narrow,
          "another published profile reused an open proof");
    check(Clearance::screen(map,{48,48},false,fresh,open,reads,hits)==Result::Open,"preparing another topology changed the published proof");
}
void exactOrderedScreening() {
    auto map=grid(130,129);
    for(int z=9;z<129;z+=19)for(int x=7;x<130;x+=23)block(map,{x,z});
    // Unknown interior snapshots deliberately assign positive cost 64.
    auto unknown=std::make_shared<flow::Tile>();unknown->cost.fill(64);map.tiles[4]=unknown;
    const auto bits=proofs(map);uint64_t reads=0,hits=0;
    for(int dirty=-1;dirty<int(map.tiles.size());++dirty){
        const auto fresh=[&](int tile){return tile!=dirty;};
        for(int z=-1;z<=129;++z)for(int x=-1;x<=130;++x)for(bool axis:{false,true}){
            const auto before=reads;
            check(Clearance::screen(map,{x,z},axis,fresh,[&](int tile){return bits[size_t(tile)];},reads,hits)==original(map,{x,z},axis,fresh),
                  "proof changed ordered dirty/wall/unknown/map-edge screening");
            check(reads-before<=32,"one screen exceeded the original probe bound");
        }
    }
    check(hits>0&&reads>0,"test failed to exercise both proof and per-cell paths");
}
}
int main(){try{sharesPublishedProofs();exactOrderedScreening();std::puts("PASS published whole-tile clearance, profile isolation, ordered freshness, positive unknown and map edges");return 0;}
catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}
