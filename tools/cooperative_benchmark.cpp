// Opt-in, run only after ordinary builds/tests stop. Timings cover World::tick;
// motion, legality samples, and logical memory accompany every timing result.
#include "cooperative_test_common.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <chrono>
#include <cstdlib>
using namespace cooperative_test;
template<class Stats>void coordinatorStats(const Stats& stats) {
    if constexpr(requires{stats.cooperativeBytes;})
        std::printf("coordinator_bytes=%zu records=%zu reservations=%zu probes=%llu searches=%llu routes=%llu waits=%llu conflicts=%llu\n",
            stats.cooperativeBytes,stats.cooperativeRecords,stats.cooperativeReservations,
            (unsigned long long)stats.cooperativeProbes,(unsigned long long)stats.cooperativeSearches,
            (unsigned long long)stats.cooperativeRoutes,(unsigned long long)stats.cooperativeWaits,(unsigned long long)stats.cooperativeConflicts);
    if constexpr(requires{stats.cooperativePassageProbes;})
        std::printf("passage_probes=%llu passage_screening_cells=%llu passage_cache_hits=%llu passage_entries=%zu\n",
            (unsigned long long)stats.cooperativePassageProbes,
            (unsigned long long)stats.cooperativePassageScreeningCells,
            (unsigned long long)stats.cooperativePassageHits,stats.cooperativePassages);
    if constexpr(requires{stats.cooperativeClearanceHits;})
        std::printf("clearance_hits=%llu clearance_rebuilds=%llu clearance_evictions=%llu clearance_entries=%zu clearance_bytes=%zu\n",
            (unsigned long long)stats.cooperativeClearanceHits,
            (unsigned long long)stats.cooperativeClearanceRebuilds,
            (unsigned long long)stats.cooperativeClearanceEvictions,
            stats.cooperativeClearanceEntries,stats.cooperativeClearanceBytes);
    if constexpr(requires{stats.cooperativeCompleteFailures;})
        std::printf("coordinator_complete_failures=%llu coordinator_deferred_searches=%llu coordinator_retry_skips=%llu\n",
            (unsigned long long)stats.cooperativeCompleteFailures,
            (unsigned long long)stats.cooperativeDeferredSearches,
            (unsigned long long)stats.cooperativeRetrySkips);
}
template<class WorldType>void movementStats(const WorldType& world) {
    if constexpr(requires{world.cooperativeMovementStats();}) {
        const auto stats=world.cooperativeMovementStats();
        std::printf("movement_queued=%llu movement_attempted=%llu movement_moved=%llu movement_blocked=%llu movement_cycles=%llu movement_deferred=%llu movement_invalid=%llu movement_probes=%llu\n",
            (unsigned long long)stats.queued,(unsigned long long)stats.attempted,
            (unsigned long long)stats.moved,(unsigned long long)stats.blocked,
            (unsigned long long)stats.cycle,(unsigned long long)stats.deferred,
            (unsigned long long)stats.invalid,(unsigned long long)stats.probes);
    }
}
int main(int argc,char** argv) {
    if(argc<6||argc>7){std::fprintf(stderr,"usage: cooperative_benchmark DATA UNITS TICKS MODE serial|workers [touching|spaced|sparse]\n");return 2;}
    try {
        const int count=std::atoi(argv[2]),ticks=std::atoi(argv[3]);const auto selected=mode(argv[4]);
        require(count>=1&&count<=16000&&ticks>=1&&ticks<=10000,"benchmark dimensions outside supported limits");
        require(std::string_view(argv[5])=="serial"||std::string_view(argv[5])=="workers","invalid worker mode");
        const bool serial=std::string_view(argv[5])=="serial";
        const std::string_view layout=argc==7?argv[6]:"touching";
        require(layout=="touching"||layout=="spaced"||layout=="sparse","invalid layout");
        const int spacing=layout=="touching"?32:layout=="spaced"?48:96;
        auto vfs=tak::hpi::mountRetailRoot(argv[1],tak::hpi::OverridePolicy::None);TypeRegistry registry;setupRegistry(registry,vfs,false);
        require(registry.find("arasword"),"benchmark unit missing");auto type=*registry.find("arasword");type.weapons.clear();type.weapon.damage=0;
        World world;setup(world,selected,serial,false,2048);std::vector<int> ids;ids.reserve(size_t(count));
        for(int i=0;i<count;++i){const int id=world.spawn(&type,float(512+i%128*spacing),float(512+i/128*spacing),std::nullopt,0);
            ids.push_back(id);world.order(id,24000,24000,false);}
        std::vector<double> times;times.reserve(size_t(ticks));double sum=0;size_t peak=0;
        for(int tick=0;tick<ticks;++tick){const auto before=std::chrono::steady_clock::now();world.tick(1.f/30);
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
            times.push_back(ms);sum+=ms;peak=std::max(peak,world.flowStats().bytes);}
        int moved=0;int64_t forward=0;std::vector<int> displacement;
        for(int i=0;i<count;++i){const auto& u=*world.unit(ids[size_t(i)]);const int d=u.x.floorInt()-512-i%128*spacing+u.z.floorInt()-512-i/128*spacing;
            moved+=d>0;forward+=d;displacement.push_back(d);}
        std::sort(times.begin(),times.end());std::sort(displacement.begin(),displacement.end());
        std::vector<int> sample;for(int i=0;i<std::min(count,64);++i)sample.push_back(ids[size_t(i)*ids.size()/std::min(count,64)]);legal(world,sample);
        const auto stats=world.flowStats();
        std::printf("mode=%s units=%d ticks=%d serial=%d spacing=%d mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f max_ms=%.3f peak_navigation_bytes=%zu requests=%llu deliveries=%llu pending=%zu hash=%016llx\n",
            argv[4],count,ticks,serial,spacing,sum/ticks,times[size_t(ticks)/2],times[size_t(ticks-1)*95/100],times.back(),peak,
            (unsigned long long)stats.requests,(unsigned long long)stats.deliveries,stats.pending,(unsigned long long)world.stateHash());
        std::printf("forward_moved=%d/%d mean_l1_forward_px=%.1f p10_forward_px=%d p50_forward_px=%d legal_samples=%zu\n",
            moved,count,double(forward)/count,displacement[size_t(count-1)/10],displacement[size_t(count-1)/2],sample.size());coordinatorStats(stats);movementStats(world);
        std::printf("snapshot_work=%llu field_work=%llu local_work=%llu local_deliveries=%llu arrival_cells=%llu firing_rays=%llu firing_cells=%llu profiles=%zu fields=%zu cached_tiles=%zu prepared_tiles=%llu uniform_tiles=%llu pending_age_p95=%llu pending_age_max=%llu\n",
            (unsigned long long)stats.snapshotWork,(unsigned long long)stats.fieldWork,
            (unsigned long long)stats.localWork,(unsigned long long)stats.localDeliveries,
            (unsigned long long)stats.arrivalCells,(unsigned long long)stats.firingRays,
            (unsigned long long)stats.firingCells,stats.profiles,stats.fields,stats.cachedTiles,
            (unsigned long long)stats.preparedTiles,(unsigned long long)stats.uniformTiles,
            (unsigned long long)stats.pendingAgeP95,(unsigned long long)stats.pendingAgeMax);
        require(peak<=512ull*1024*1024,"navigation exceeds its common memory ceiling");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
