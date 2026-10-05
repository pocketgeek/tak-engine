#pragma once
#include "fixed.h"
#include "flowtraffic.h"
#include "navigationtelemetry.h"
#include <cstddef>
#include <cstdint>
#include <memory>
namespace tak::sim {
class World;
struct Unit;
// Shared terrain navigation for Flowfield and Cooperative matches. The opaque
// implementation keeps caches, worker handles and retry state out of Retail.
class FlowNavigator {
public:
    struct Stats {
        uint64_t snapshotWork=0,fieldWork=0,localWork=0,localDeliveries=0,requests=0,deliveries=0,failures=0;
        uint64_t profileEvictions=0,snapshotFailures=0;
        size_t profiles=0,pending=0,fields=0,bytes=0;
        uint64_t firingRays=0,firingCells=0;
        uint64_t arrivalCells=0;
        uint64_t tileCacheHits=0,tileCacheEvictions=0,preparedTiles=0,uniformTiles=0;
        uint64_t pendingAgeP95=0,pendingAgeMax=0,deliveredAgeMax=0,deliveredAgeTotal=0;
        size_t cachedTiles=0;
        uint64_t cooperativeProbes=0,cooperativeSearches=0,cooperativeRoutes=0;
        uint64_t cooperativeWaits=0,cooperativeConflicts=0;
        uint64_t cooperativeCompleteFailures=0,cooperativeDeferredSearches=0,cooperativeRetrySkips=0;
        size_t cooperativeRecords=0,cooperativeReservations=0,cooperativeBytes=0;
        uint64_t cooperativePassageProbes=0,cooperativePassageHits=0,cooperativePassageScreeningCells=0;
        size_t cooperativePassages=0;
        uint64_t cooperativeClearanceHits=0,cooperativeClearanceRebuilds=0,cooperativeClearanceEvictions=0;
        size_t cooperativeClearanceEntries=0,cooperativeClearanceBytes=0;
        // Diagnostics only (never hashed or read by decisions). The *Ns
        // fields are wall-clock and must only be used as exploratory timing.
        uint64_t dirtyEvents=0,dirtyProfileTiles=0,snapshotStarts=0,topologyPublications=0,unchangedPublications=0;
        uint64_t serviceDestinations=0,serviceDestinationWork=0,serviceFieldsBuilt=0,serviceFieldWork=0;
        uint64_t serviceInvalidations=0,serviceInvalidatedDestinations=0,serviceInvalidatedFields=0;
        uint64_t serviceInvalidatedBuilders=0,serviceInvalidatedBindings=0,serviceEvictedGroups=0,serviceEvictedFields=0;
        uint64_t unboundRequests=0,staleDeliveryBlocks=0,serviceSharedFieldsBuilt=0,serviceSharedResolutions=0,serviceSharedReuses=0,serviceRetainedFields=0;
        uint64_t serviceNs=0,snapshotNs=0,deliverNs=0,tickNs=0;
    };
    explicit FlowNavigator(World& world);
    ~FlowNavigator();
    FlowNavigator(const FlowNavigator&)=delete;
    FlowNavigator& operator=(const FlowNavigator&)=delete;
    void setTelemetry(NavigationTelemetry* observer);
    bool request(Unit& unit,Fixed x,Fixed z);
    bool pending(int unit) const;
    bool routeBlocked(const Unit& unit) const;
    bool settled(const Unit& unit) const;
    flow::Traffic::Result traffic(Unit& unit);
    bool allowFollowerStep(int id,flow::Cell from,flow::Cell to) const;
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
