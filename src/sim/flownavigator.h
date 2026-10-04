#pragma once
#include "fixed.h"
#include "flowtraffic.h"
#include <cstddef>
#include <cstdint>
#include <memory>
namespace tak::sim {
class World;
struct Unit;
// World owns this only for Flowfield matches. The opaque implementation keeps
// caches, worker handles and flow-specific retry state out of Retail games.
class FlowNavigator {
public:
    struct Stats {
        uint64_t snapshotWork=0,fieldWork=0,localWork=0,localDeliveries=0,requests=0,deliveries=0,failures=0;
        uint64_t profileEvictions=0,snapshotFailures=0;
        size_t profiles=0,pending=0,fields=0,bytes=0;
        uint64_t firingRays=0,firingCells=0;
        uint64_t tileCacheHits=0,tileCacheEvictions=0,preparedTiles=0,uniformTiles=0;
        uint64_t pendingAgeP95=0,pendingAgeMax=0,deliveredAgeMax=0,deliveredAgeTotal=0;
        size_t cachedTiles=0;
    };
    explicit FlowNavigator(World& world);
    ~FlowNavigator();
    FlowNavigator(const FlowNavigator&)=delete;
    FlowNavigator& operator=(const FlowNavigator&)=delete;
    bool request(Unit& unit,Fixed x,Fixed z);
    bool pending(int unit) const;
    bool routeBlocked(const Unit& unit) const;
    flow::Traffic::Result traffic(Unit& unit);
    void cancel(int unit);
    void tick();
    void dirty(int x,int z,int w,int h,uint16_t viewers=0xffff);
    uint64_t checksum() const;
    Stats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
