// Exercise actual reclaim -> queued construction, including cancellation/refusals.
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <cstdio>
using namespace tak;
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
    int failures=0;
    auto check=[&](bool ok,const char* message) {
        if (!ok) {++failures;std::fprintf(stderr,"FAIL: %s\n",message);}
    };
    for (bool balance:{false,true}) {
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,balance);
        for (const char* worker:{"araking","zonhunt"}) {
            const auto* builder=registry.find(worker);
            check(builder!=nullptr,"real builder exists");if (!builder) continue;
            sim::UnitType site;site.id="test_site";site.maxVel=sim::Fixed();site.footX=4;site.footZ=4;
            site.maxHp=100;site.buildTime=20;site.buildCost=10;
            sim::World w;w.setVisPlayer(-1);
            w.setTerrain(std::vector<uint8_t>(64*64,100),64,64,20);
            const int bid=w.spawn(builder,512,624,std::nullopt,0);
            w.player(0).mana=1000;
            sim::FeatType tree;tree.reclaimable=true;
            sim::FeatType decor;decor.reclaimable=false;
            w.setFeatureTypes({tree,decor});
            w.addFeature(101,504,504,50,20,1,1,true,0);
            w.addFeature(102,520,520,50,20,1,1,true,0);
            w.addFeature(103,584,504,50,20,1,1,true,0);
            std::vector<int> clearing;
            check(!w.canPlace(&site,512,512,0),"uncleared site is blocked");
            check(w.clearableForPlacement(&site,512,512,clearing,0) && clearing.size()==2,
                  "only footprint blockers selected");
            w.queueBuild(bid,&site,512,512,false);
            check(w.unit(bid)->orders.size()==3,"single build queues two reclaims and construction");
            w.stop(bid);
            for (int i=0;i<10;++i) w.tick(1.f/30);
            check(w.feature(101)->alive && w.feature(102)->alive && w.unit(bid)->orders.empty(),
                  "stop cancels clearing and construction");
            w.order(bid,512,608,false);
            w.queueBuild(bid,&site,512,512,true);
            check(w.unit(bid)->orders.size()>=4 && !w.unit(bid)->orders.front().reclaimFeat,
                  "queued construction preserves earlier movement");
            int built=0;
            for (int i=0;i<1800;++i) {
                w.tick(1.f/30);
                for (const auto& u:w.units()) if (u.type==&site && u.alive()) built=u.id;
                if (built) {
                    check(!w.feature(101)->alive && !w.feature(102)->alive,"foundation waits for all clearing");
                    if (!w.unit(built)->underConstruction) break;
                }
            }
            check(built && !w.unit(built)->underConstruction,"builder clears and finishes construction");
            check(w.feature(103)->alive,"nearby tree outside footprint survives");
            w.addFeature(104,760,760,50,20,1,1,true,1);
            check(!w.clearableForPlacement(&site,768,768,clearing,0),"nonreclaimable feature refuses");
            w.queueBuild(bid,&site,768,768,false);
            check(w.unit(bid)->orders.empty(),"invalid site adds no reclaim orders");
            site.onMana=true;w.setManaSpots({{128,128}});
            check(!w.clearableForPlacement(&site,592,512,clearing,0),"clearing cannot bypass mana-site rules");
            std::printf("%s balance=%d hash=%016llx\n",worker,balance,(unsigned long long)w.stateHash());
        }
    }
    return failures?1:0;
}
