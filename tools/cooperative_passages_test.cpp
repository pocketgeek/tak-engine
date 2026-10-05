#include "sim/cooperativepassages.h"
#include <cstdio>
#include <functional>
#include <stdexcept>

using namespace tak::sim;
using cooperative::Passages;
using Passage=cooperative::Traffic::Passage;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::shared_ptr<const flow::Topology> topology(int width,int height,int footX,int footZ,
        const std::function<bool(int,int)>& raw) {
    flow::TopologyBuilder builder(width,height);
    for(int z=0;z<height;++z)for(int x=0;x<width;++x) {
        bool free=true;
        for(int dz=0;dz<footZ&&free;++dz)for(int dx=0;dx<footX;++dx) {
            const int sx=x-footX/2+dx,sz=z-footZ/2+dz;
            if(sx<0||sz<0||sx>=width||sz>=height||!raw(sx,sz)){free=false;break;}
        }
        if(free)builder.setCost({x,z},64);
    }
    while(!builder.done()&&!builder.failed())builder.step(8192);
    check(builder.done(),"synthetic topology unavailable");return builder.finish();
}
Passages::Result finish(Passages& cache,const flow::Topology& map,int footX,int footZ,
        flow::Cell at,flow::Cell direction,uint64_t profile=1,uint64_t generation=1,uint32_t firstTick=1) {
    for(uint32_t tick=firstTick;tick<firstTick+10000;++tick) {
        const auto result=cache.probe(tick,profile,generation,map,footX,footZ,at,direction);
        if(result.status!=Passages::Status::Pending)return result;
    }
    throw std::runtime_error("bounded passage job failed to finish");
}
void same(const Passage& actual,flow::Cell first,flow::Cell last,int width,const char* message) {
    if(!(actual.first==first&&actual.last==last&&actual.width==width))
        std::fprintf(stderr,"actual passage=(%d,%d)-(%d,%d) width=%u\n",actual.first.x,actual.first.z,actual.last.x,actual.last.z,actual.width);
    check(actual.first==first&&actual.last==last&&actual.width==width,message);
}
void longStrip() {
    const auto map=topology(2048,64,2,2,[](int x,int z){return x<96||x>=1950||(z>=30&&z<32);});
    Passages cache;
    auto pending=cache.probe(1,1,1,*map,2,2,{100,31},{1,0});
    check(pending.status==Passages::Status::Pending,"long strip was published before both exits were proved");
    check(cache.stats().probes<=128,"one call exceeded its work quota");
    const auto found=finish(cache,*map,2,2,{100,31},{1,0},1,1,2);
    check(found.status==Passages::Status::Found,"long one-file strip was missed");
    same(*found.passage,{96,31},{1950,31},2,"long strip endpoints are incomplete");
    const auto work=cache.stats().probes;
    const auto reversed=cache.probe(100,1,1,*map,2,2,{100,31},{-1,0});
    same(*reversed.passage,{1950,31},{96,31},2,"opposite direction changed canonical geometry");
    check(cache.stats().probes==work,"completed probe repeated terrain work");
    for(int x:{96,500,1000,1949}) {
        const auto shared=cache.probe(101,1,1,*map,2,2,{x,31},{1,0});
        check(shared.status==Passages::Status::Found,"another anchor did not share the proved strip");
        same(*shared.passage,{96,31},{1950,31},2,"shared anchor changed passage identity");
    }
    check(cache.stats().probes==work&&cache.stats().entries==1,"repeated anchors rescanned or duplicated a completed strip");
    check(cache.bytes()<=Passages::memoryLimit,"passage cache exceeded its logical cap");
}
void canonicalFootprints() {
    const auto raw=[](int x,int z){return x<40||x>=220||(z>=30&&z<34);};
    const auto small=topology(256,64,2,2,raw),large=topology(256,64,4,4,raw);
    Passages cache;
    for(int z:{31,32,33}) {
        const auto found=finish(cache,*small,2,2,{50,z},{1,0},2,1,uint32_t(z*100));
        check(found.status==Passages::Status::Found,"shifted small-footprint center missed strip");
        same(*found.passage,{40,32},{220,32},4,"two-abreast width must use raw cells, not anchor count");
    }
    const auto found=finish(cache,*large,4,4,{210,32},{-1,0},4,1,4000);
    check(found.status==Passages::Status::Found,"large-footprint strip was missed");
    same(*found.passage,{220,32},{40,32},4,"mixed footprints did not normalize to one passage");
    const auto vertical=topology(64,256,4,2,[](int x,int z){return z<40||z>=220||(x>=30&&x<34);});
    const auto north=finish(cache,*vertical,4,2,{32,80},{0,-1},5,1,5000);
    check(north.status==Passages::Status::Found,"vertical rectangular footprint was missed");
    same(*north.passage,{32,220},{32,40},4,"axis swap changed passage normalization");
    const auto before=finish(cache,*small,2,2,{25,32},{1,0},2,1,6000);
    check(before.status==Passages::Status::None,"open approach was labeled a passage");
    const auto entry=finish(cache,*small,2,2,{40,32},{1,0},2,1,6100);
    check(entry.status==Passages::Status::Found,"look-ahead route cell did not find passage before entry");
}
void baysAndUnavailable() {
    const auto raw=[](int x,int z) {
        if((x>=30&&x<80)||(x>=100&&x<150))return z>=30&&z<34;
        if(x>=80&&x<100)return z>=28&&z<36;
        return true;
    };
    Passages cache;
    for(int foot:{2,4}) {
        const auto map=topology(192,64,foot,foot,raw);
        const auto left=finish(cache,*map,foot,foot,{50,32},{1,0},uint64_t(foot),1,uint32_t(foot*100));
        const auto right=finish(cache,*map,foot,foot,{120,32},{-1,0},uint64_t(foot),1,uint32_t(foot*100+50));
        check(left.status==Passages::Status::Found&&right.status==Passages::Status::Found,"widening bay was missed");
        same(*left.passage,{30,32},{80,32},4,"widening bay did not split first strip");
        same(*right.passage,{150,32},{100,32},4,"widening bay did not split second strip");
    }
    const auto shifted=topology(192,64,2,2,[](int x,int z){
        if(x<30||x>=150)return true;
        return x<80?(z>=30&&z<34):(z>=31&&z<35);
    });
    check(finish(cache,*shifted,2,2,{50,32},{1,0},10,1,1000).status==Passages::Status::None,
          "offset walls were extrapolated into a straight strip");
    const auto closed=topology(192,64,2,2,[](int x,int z){return x<30||(x<150&&z>=30&&z<34);});
    check(finish(cache,*closed,2,2,{50,32},{1,0},11,1,1100).status==Passages::Status::None,
          "closed dead end acquired a passage permit");
    const auto border=topology(192,64,2,2,[](int,int z){return z>=30&&z<34;});
    check(finish(cache,*border,2,2,{50,32},{1,0},12,1,1200).status==Passages::Status::None,
          "unavailable axial map boundary counted as an open exit");
    flow::Topology missing=*border;missing.tiles.front().reset();
    check(finish(cache,missing,2,2,{50,32},{1,0},13,1,1300).status==Passages::Status::None,
          "missing immutable tile counted as proved geometry");
    check(cache.probe(1400,14,1,*border,2,2,{50,32},{1,1}).status==Passages::Status::None,
          "diagonal route was treated as cardinal");
}
void shortBays() {
    for(int bayLength:{8,16,17}) {
        const auto raw=[&](int x,int z) {
            if(x<112||x>=136+bayLength)return true;
            if(x>=124&&x<124+bayLength)return z>=26&&z<38;
            return z>=30&&z<34;
        };
        for(int foot:{2,4}) {
            const auto map=topology(192,64,foot,foot,raw);
            for(bool reverse:{false,true}) {
                Passages cache({7,4,8,16});
                const auto result=finish(cache,*map,foot,foot,
                    {reverse?132+bayLength:116,foot==2?(reverse?33:31):32},{reverse?-1:1,0});
                check(result.status==Passages::Status::Found,"short-bay passage disappeared");
                if(bayLength<=16) {
                    same(*result.passage,reverse?flow::Cell{136+bayLength,32}:flow::Cell{112,32},
                        reverse?flow::Cell{112,32}:flow::Cell{136+bayLength,32},4,
                        "short-bay chain changed with traversal or footprint");
                    check(result.passage->extentWidth==12,"merged bay lost its lateral extent");
                    const auto work=cache.stats().probes;
                    const auto shared=cache.probe(1000,1,1,*map,foot,foot,{128,32},{1,0});
                    check(shared.status==Passages::Status::Found&&shared.passage->first==flow::Cell{112,32}&&
                          cache.stats().probes==work,"bay anchor did not reuse the complete chain");
                } else {
                    same(*result.passage,reverse?flow::Cell{136+bayLength,32}:flow::Cell{112,32},
                        reverse?flow::Cell{124+bayLength,32}:flow::Cell{124,32},4,
                        "long bay exceeded the bounded merge window");
                    check(result.passage->extentWidth==0,"unmerged bay changed permit width");
                }
            }
        }
    }
    const auto openBay=topology(192,64,2,2,[](int x,int z) {
        if(x<112||x>=152||(x>=124&&x<132))return true;
        return z>=30&&z<34;
    });
    Passages cache;
    const auto separate=finish(cache,*openBay,2,2,{116,32},{1,0});
    check(separate.status==Passages::Status::Found,"open-bay approach disappeared");
    same(*separate.passage,{112,32},{124,32},4,"open ground was merged as an enclosed bay");
    const auto offsetBay=topology(192,64,2,2,[](int x,int z) {
        if(x<112||x>=152)return true;
        if(x>=124&&x<132)return z>=28&&z<40;
        return z>=30&&z<34;
    });
    const auto offset=finish(cache,*offsetBay,2,2,{116,32},{1,0},2,1,100);
    check(offset.status==Passages::Status::Found,"offset bay approach disappeared");
    same(*offset.passage,{112,32},{124,32},4,"offset bay understated centered permit extent");
}
void generationsAndQuotas() {
    const auto strip=topology(256,64,2,2,[](int x,int z){return x<30||x>=220||(z>=30&&z<34);});
    const auto open=topology(256,64,2,2,[](int,int){return true;});
    Passages shared({128,128,8,16});
    check(shared.probe(1,1,1,*strip,2,2,{100,32},{1,0}).status==Passages::Status::Pending,"partial proof fixture ended early");
    const auto partialWork=shared.stats().probes;
    check(shared.probe(1,1,1,*strip,2,2,{95,31},{-1,0}).status==Passages::Status::Pending&&
          shared.stats().entries==1&&shared.stats().probes==partialWork,
          "nearby anchor did not share unfinished proof and its one-call quota");
    check(finish(shared,*strip,2,2,{95,31},{-1,0},1,1,2).status==Passages::Status::Found&&shared.stats().entries==1,
          "shared unfinished job lost its canonical seed");
    Passages cache;
    check(finish(cache,*strip,2,2,{50,32},{1,0}).status==Passages::Status::Found,"invalidation setup failed");
    check(finish(cache,*open,2,2,{50,32},{1,0},1,2,100).status==Passages::Status::None,
          "new generation reused stale passage");
    cache.invalidate(1);check(cache.stats().entries==0,"profile invalidation retained stale jobs");
    check(finish(cache,*open,2,2,{50,32},{1,0},1,1,200).status==Passages::Status::None,
          "invalidated generation retained old geometry");
    Passages tiny({7,4,4,16}),copy({7,4,4,16});
    uint64_t previous=0;bool completed=false;
    for(uint32_t tick=1;tick<2000;++tick) {
        const auto first=tiny.probe(tick,1,1,*strip,2,2,{50,32},{1,0});
        copy.probe(tick,1,1,*strip,2,2,{50,32},{1,0});
        const auto sameTick=tiny.stats().probes;
        tiny.probe(tick,1,1,*strip,2,2,{50,32},{1,0});copy.probe(tick,1,1,*strip,2,2,{50,32},{1,0});
        check(tiny.stats().probes==sameTick,"same pending job consumed two call quotas in one tick");
        const auto second=tiny.probe(tick,1,1,*strip,2,2,{200,32},{-1,0});
        copy.probe(tick,1,1,*strip,2,2,{200,32},{-1,0});
        check(tiny.stats().probes-previous<=7,"shared tick probe quota exceeded");previous=tiny.stats().probes;
        check(tiny.checksum()==copy.checksum(),"same logical schedule changed cache state");
        if(first.status==Passages::Status::Found&&second.status==Passages::Status::Found){completed=true;break;}
    }
    check(completed,"rotating admission starved a pending strip");
    Passages limited({128,128,1,16});
    check(limited.probe(1,1,1,*strip,2,2,{50,32},{1,0}).status==Passages::Status::Pending,"pending-cache fixture too short");
    check(limited.probe(1,2,1,*strip,2,2,{200,32},{-1,0}).status==Passages::Status::Pending&&limited.stats().entries==1,
          "capacity pressure replaced a pending descriptor");
    check(finish(limited,*strip,2,2,{50,32},{1,0},1,1,2).status==Passages::Status::Found,"pending descriptor lost progress");
    check(finish(limited,*open,2,2,{50,32},{1,0},2,1,100).status==Passages::Status::None&&limited.stats().evictions==1,
          "completed LRU entry was not evicted deterministically");
    limited.clear();check(limited.stats().entries==0&&limited.stats().probes==0,"cache clear retained state");
    Passages abandoned({8,4,2,16});
    check(abandoned.probe(1,1,1,*strip,2,2,{50,32},{1,0}).status==Passages::Status::Pending&&
          abandoned.probe(1,1,1,*strip,2,2,{200,32},{-1,0}).status==Passages::Status::Pending,
          "abandoned job fixture completed too early");
    // Both original requesters disappear. A different live route must recover
    // cache admission without depending on cancellation of those unit IDs.
    check(finish(abandoned,*strip,2,2,{120,32},{1,0},2,1,2).status==Passages::Status::Found,
          "orphaned pending jobs permanently denied a live route");
    check(abandoned.stats().evictions>=2,"orphaned jobs were never reclaimed");
}
void independentProgress() {
    const auto strip=topology(2048,64,2,2,[](int x,int z){return x<96||x>=1950||(z>=30&&z<32);});
    const auto open=topology(64,64,2,2,[](int,int){return true;});
    const auto lookup=[&](uint64_t profile,uint64_t generation)->const flow::Topology* {
        if(generation!=1)return nullptr;
        return profile==1?strip.get():profile==2?open.get():nullptr;
    };
    Passages cache;
    cache.advance(1,lookup);
    check(cache.probe(1,1,1,*strip,2,2,{100,31},{1,0}).status==Passages::Status::Pending,
          "independent long-strip fixture finished immediately");
    for(uint32_t tick=2;tick<=301;++tick) {
        cache.advance(tick,lookup);
        if(tick==2) {
            const auto before=cache.stats().probes;
            check(cache.probe(tick,1,1,*strip,2,2,{100,31},{1,0}).status==Passages::Status::Pending&&
                  cache.stats().probes==before,"caller repeated an independently consumed work quantum");
        }
        cache.probe(tick,2,1,*open,2,2,{32,32},{1,0});
    }
    const auto ready=cache.probe(301,1,1,*strip,2,2,{100,31},{1,0});
    check(ready.status==Passages::Status::Found&&cache.stats().evictions==0,
          "delivery delay expired a live independently advancing proof");
    same(*ready.passage,{96,31},{1950,31},2,"independent work changed geometry");

    Passages limited({7,4,2,16});bool admitted=false;uint64_t previous=0;
    for(uint32_t tick=1;tick<20000;++tick) {
        limited.advance(tick,lookup);
        if(tick==1) {
            // Both requesters disappear, leaving the bounded cache full.
            limited.probe(tick,1,1,*strip,2,2,{100,31},{1,0});
            limited.probe(tick,1,1,*strip,2,2,{1900,31},{-1,0});
        } else {
            admitted=limited.probe(tick,2,1,*open,2,2,{32,32},{1,0}).status==Passages::Status::None;
        }
        check(limited.stats().probes-previous<=7,"independent and caller work exceeded their shared tick quota");
        previous=limited.stats().probes;
        if(admitted)break;
    }
    check(admitted&&limited.stats().evictions>0,"abandoned active jobs permanently blocked a later caller");
    check(limited.bytes()<=Passages::memoryLimit,"independent advancement exceeded cache memory");

    Passages invalid;
    invalid.advance(1,lookup);
    invalid.probe(1,1,1,*strip,2,2,{100,31},{1,0});
    const auto work=invalid.stats().probes;
    invalid.advance(2,[](uint64_t,uint64_t)->const flow::Topology*{return nullptr;});
    check(invalid.stats().entries==0&&invalid.stats().probes==work,
          "unavailable generation continued its old terrain proof");
}
}
int main(){try {
    longStrip();canonicalFootprints();baysAndUnavailable();shortBays();generationsAndQuotas();independentProgress();
    std::puts("PASS bounded cooperative passage geometry, canonical footprints, generations and admission");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}
