#pragma once
#include "retailplus.h"
#include <memory>

namespace tak::sim {
class World;
struct Unit;
// Retail long-distance routes with a separately bounded local traffic policy.
class RetailPlusNavigator {
public:
    struct Stats {
        retailplus::Traffic::Stats traffic;
        uint64_t proofCells=0;
        size_t bytes=0;
        uint64_t bodyEntries=0,bodyDeferrals=0;
        // Optional wall-clock observers, excluded from simulation state/hash.
        uint64_t contextNanoseconds=0,setupNanoseconds=0,policyNanoseconds=0,maintenanceNanoseconds=0;
        uint64_t fastUpdates=0,fullUpdates=0;
    };
    explicit RetailPlusNavigator(World&);
    ~RetailPlusNavigator();
    bool supports(const Unit&) const;
    void registerMove(Unit&);
    void cancel(int);
    void tick();
    flow::Traffic::Result traffic(Unit&);
    // Same as traffic(Unit&) for a caller that has just evaluated supports().
    flow::Traffic::Result traffic(Unit&,bool supported);
    uint64_t checksum() const;
    Stats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
