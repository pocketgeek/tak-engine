#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/flowtraffic.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string_view>
using namespace tak;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void setup(sim::World& w,bool serial){w.setVisPlayer(-1);w.setSerialThreads(serial);w.setPathService(true);w.setPathfindingMode(sim::PathfindingMode::Flowfield);w.setTerrain(std::vector<uint8_t>(128*128,100),128,128,64);}
void areaArrivals(){
    using Traffic=sim::flow::Traffic;using Cell=sim::flow::Cell;
    Traffic area;Traffic::Context c;c.player=0;c.tick=1;c.target={100,100};c.plainMove=true;
    c.footX=c.footZ=2;c.free=[](Cell){return true;};c.arrivalReachable=[](Cell){return true;};
    for(int id=1;id<=16;++id){c.id=id;c.controller=id;c.position={84,88+(id%8)*3};area.registerMove(c);}
    check(area.arrivalRadiusSquared(c)==144,"destination population was not registered before following");
    {auto distant=c;distant.blocked=4;distant.tick=301;
        check(!area.needsArrivalNeighbors(distant),"travelling army with no arrived members requested destination neighbor scans");}
    std::vector<std::pair<Cell,int>> slots;
    for(int id=1;id<=4;++id){c.id=id;c.controller=id;c.position={84,88+(id%8)*3};check(!area.update(c).detour,"arrival slot bypassed bounded admission");}
    c.tick=2;
    for(int id=1;id<=4;++id){
        c.id=id;c.controller=id;c.position={84,88+(id%8)*3};const auto result=area.update(c);
        check(result.detour.has_value(),"moving group was forced to collide before getting an area slot");
        const Cell at=*result.detour;
        check((at.x-100)*(at.x-100)+(at.z-100)*(at.z-100)<=144,"arrival slot escaped compact destination area");
        for(const auto& [prior,foot]:slots)check(std::abs(at.x-prior.x)>=foot||std::abs(at.z-prior.z)>=foot,"simultaneous arrivals reserved overlapping footprints");
        slots.emplace_back(at,2);
    }
    c.tick=3;
    for(int id=1;id<=4;++id){c.id=id;c.controller=id;c.position={84,88+(id%8)*3};check(area.update(c).detour==slots[size_t(id-1)].first,"arrival slot changed while its mover approached");}
    c.id=1;c.controller=1;c.position=slots.front().first;++c.tick;
    auto done=area.update(c);if(!done.settled){++c.tick;done=area.update(c);}
    check(done.settled&&area.settled(1,c.position),"arrival required an occupied centre or body refusal");
    ++c.tick;check(area.update(c).settled,"persistent area arrival woke itself without a changed order");
    check(!area.settled(1,{c.position.x+1,c.position.z}),"displaced unit retained settled position");
    c.position.x+=2;area.refreshSettled(1,c.position);
    check(area.settled(1,c.position)&&area.arrivalRadiusSquared(c)==144,"ordinary arrival coasting shrank the destination population");
    const auto ordinary=area.arrivalRadiusSquared(c);c.targetId=99;
    check(area.arrivalRadiusSquared(c)==0,"escort population merged into an unrelated move");c.targetId=0;
    check(area.arrivalRadiusSquared(c)==ordinary,"escort lookup changed normal destination population");
    area.prune(16384,[](int,int,uint64_t,Cell,bool){return false;});
    check(area.bytes()==0,"arrival cancellation leaked footprint reservations");
    // Proximity cannot authorize arrival across a wall, on a blocked footprint,
    // or when topology is still incomplete. A lone mover keeps point precision.
    for(int kind=0;kind<3;++kind){
        Traffic blocked;Traffic::Context q=c;q.tick=1;q.position={95,100};q.id=1;q.controller=1;
        q.free=[=](Cell){return kind!=0;};q.arrivalReachable=[=](Cell){return kind!=1;};
        blocked.registerMove(q);
        if(kind!=2){q.id=2;q.controller=2;blocked.registerMove(q);}
        blocked.update(q);q.tick=2;const auto result=blocked.update(q);
        check(!result.settled&&!result.detour,"area arrival ignored terrain, occupancy, or single-unit precision");
    }
    // A larger model's reservation includes its complete footprint rather than
    // just its centre cell, including reservations spanning bucket boundaries.
    Traffic mixed;c.tick=1;c.id=1;c.controller=1;c.position={80,100};c.footX=c.footZ=8;
    mixed.registerMove(c);c.id=2;c.controller=2;c.footX=c.footZ=2;mixed.registerMove(c);
    c.id=1;c.controller=1;c.footX=c.footZ=8;mixed.update(c);c.tick=2;const auto large=mixed.update(c);
    check(large.detour.has_value(),"large footprint had no arrival slot");
    c.id=2;c.controller=2;c.position={90,100};c.footX=c.footZ=2;mixed.update(c);c.tick=3;const auto small=mixed.update(c);
    check(small.detour.has_value(),"small footprint had no separate arrival slot");
    check(std::abs(large.detour->x-small.detour->x)>=5||std::abs(large.detour->z-small.detour->z)>=5,"mixed-size slots overlap");
    {
        Traffic army;Traffic::Context q=c;q.tick=1;q.target={500,500};q.position={100,100};
        q.footX=q.footZ=2;
        for(int id=1;id<=2000;++id){q.id=id;q.controller=id;army.registerMove(q);}
        check(army.arrivalRadiusSquared(q)==18000,"two-thousand-unit arrival area cannot hold its population");
        for(int id=2001;id<=16000;++id){q.id=id;q.controller=id;army.registerMove(q);}
        check(army.arrivalRadiusSquared(q)==144000,"sixteen-thousand-unit arrival area was capped below population");
        army.prune(16384,[](int,int,uint64_t,Cell,bool){return false;});check(army.bytes()==0,"large destination population survived pruning");
    }
    {
        Traffic wall;Traffic::Context q=c;q.tick=1;q.target={100,100};q.footX=q.footZ=2;q.free=[](Cell){return true;};
        q.id=1;q.controller=1;q.position=q.target;q.goalReached=true;check(wall.update(q).settled,"contact anchor did not settle");
        q.id=2;q.controller=2;q.goalReached=false;q.position={103,100};q.blocked=2;
        Traffic::Neighbor anchor{1,0,{100,100},2,2,true};q.neighbors=std::span(&anchor,1);
        q.terrainFree=[](Cell at){return at.x!=102;};q.arrivalReachable=[](Cell){return false;};
        check(!wall.update(q).settled,"destination contact propagated through a terrain wall");
    }
    {
        Traffic contact;Traffic::Context q;q.tick=1;q.player=0;q.target={100,100};q.footX=q.footZ=2;q.plainMove=true;
        q.free=[](Cell){return true;};q.terrainFree=[](Cell){return true;};q.arrivalReachable=[](Cell){return false;};
        q.id=1;q.controller=1;q.position=q.target;q.goalReached=true;contact.update(q);
        q.id=2;q.controller=2;q.position={103,100};q.goalReached=false;
        Traffic::Neighbor anchor{1,0,{100,100},2,2,true};q.neighbors=std::span(&anchor,1);contact.registerMove(q);
        check(!contact.needsArrivalNeighbors(q),"progressing unblocked mover requested unnecessary neighbor work");
        q.tick=301;check(contact.needsArrivalNeighbors(q),"stagnant unblocked arrival never requested its contacting neighbors");
        q.contactReachable=[](Cell){return false;};
        check(!contact.update(q).settled,"contact arrival accepted optimistic terrain when its strict proof failed");
        bool contactDeferred=false;q.arrivalDeferred=&contactDeferred;
        q.contactReachable=[&](Cell){contactDeferred=true;return false;};++q.tick;
        check(!contact.update(q).settled&&contactDeferred,"contact arrival ignored unavailable proof work");
        q.contactReachable=[&](Cell){contactDeferred=false;return true;};++q.tick;
        check(contact.update(q).settled,"stagnant same-goal contact required an actual body collision");
    }
    {
        Traffic deferred;Traffic::Context q=c;q.tick=1;q.id=1;q.controller=1;q.target={100,100};q.position={96,100};q.footX=q.footZ=2;
        q.free=[](Cell){return true;};q.arrivalReachable=[](Cell){return true;};deferred.registerMove(q);
        q.id=2;q.controller=2;deferred.registerMove(q);deferred.update(q);q.tick=2;
        const auto approach=deferred.update(q);check(approach.arrivalApproach&&approach.detour.has_value(),"deferred proof fixture has no slot");
        q.position=*approach.detour;q.tick=3;deferred.update(q);
        bool exhausted=false;q.arrivalDeferred=&exhausted;
        q.arrivalReachable=[&](Cell){exhausted=true;return false;};q.tick=4;
        const auto wait=deferred.update(q);
        check(wait.wait&&wait.arrivalApproach&&wait.detour==approach.detour,"exhausted proof work discarded a valid reserved arrival");
        q.arrivalReachable=[&](Cell){exhausted=false;return true;};bool arrived=false;
        for(int tick=5;tick<=7&&!arrived;++tick){q.tick=tick;arrived=deferred.update(q).settled;}
        check(arrived,"deferred arrival did not resume after proof work became available");
    }
    {
        Traffic wide;Traffic::Context q;q.tick=1;q.id=1;q.controller=1;q.player=0;
        q.target={50,50};q.position=q.target;q.footX=q.footZ=2;q.plainMove=q.goalReached=true;
        q.free=[](Cell){return true;};wide.update(q);
        q.id=2;q.controller=2;q.position={10,50};q.goalReached=false;q.blocked=0;
        q.steeringTarget=Cell{20,50};q.arrivalReachable=[&](Cell at){return at==q.position;};
        q.obstruction=Traffic::Neighbor{1,0,{50,50},2,2,true};
        q.free=[](Cell at){return at.x<=10||at.z>=98||at.z<=2;};q.terrainFree=[](Cell){return true;};
        wide.registerMove(q);q.tick=301;wide.update(q);q.tick=302;const auto around=wide.update(q);
        check(around.detour==Cell{10,98},"stagnant arrival could not bypass a wide parked formation");
        q.position={10,74};q.blocked=0;q.tick=500;check(wide.update(q).detour==around.detour,"slow progress lost a long lateral bypass");
        q.position=*around.detour;q.tick=700;const auto onward=wide.update(q);
        check(onward.detour==Cell{58,98},"wide bypass lost its committed forward continuation");
        q.position={30,98};q.tick=900;check(wide.update(q).detour==onward.detour,"slow forward progress lost a long bypass");
        q.tick=1100;check(wide.update(q).detour==onward.detour,"wide continuation reused a short-stride timeout");
        q.tick=1201;check(!wide.update(q).detour,"stagnant wide bypass never released its stale target");
    }
    {
        // A failed standing-slot proof must not consume the same unit's
        // admission again before its ordinary mobile avoidance can run.
        Traffic blocked;Traffic::Context q;q.tick=1;q.player=0;q.target={100,100};
        q.footX=q.footZ=2;q.plainMove=true;q.free=[](Cell){return true;};
        q.terrainFree=q.free;q.arrivalReachable=[](Cell){return false;};
        for(int id=1;id<=64;++id) {
            q.id=id;q.controller=id;q.position=id==1?Cell{80,100}:Cell{100+id*4,80};
            blocked.registerMove(q);
        }
        q.id=1;q.controller=1;q.position={80,100};q.tick=301;q.blocked=2;
        q.obstruction=Traffic::Neighbor{100,0,{81,100},2,2,false};q.obstructionFriendly=true;
        blocked.update(q);++q.tick;const auto escape=blocked.update(q);
        check(escape.detour.has_value(),"failed standing arrival proof starved admitted mobile avoidance");
    }
    for(bool deferred:{false,true}) {
        // The destination lies east, but terrain requires the current leg to
        // go north. A nearby arrived peer cannot authorize a global-goal flank
        // when the strict destination connection is blocked or unavailable.
        Traffic maze;Traffic::Context q;q.tick=1;q.id=1;q.controller=1;q.player=0;
        q.target={50,50};q.position=q.target;q.footX=q.footZ=2;q.plainMove=q.goalReached=true;
        q.free=[](Cell){return true;};maze.update(q);
        q.id=2;q.controller=2;q.position={10,50};q.goalReached=false;q.blocked=2;
        q.steeringTarget=Cell{10,30};q.obstruction=Traffic::Neighbor{1,0,{50,50},2,2,true};
        q.terrainFree=[](Cell){return true;};bool exhausted=false;q.arrivalDeferred=&exhausted;
        q.arrivalReachable=[&](Cell){exhausted=deferred;return false;};
        maze.registerMove(q);q.tick=301;maze.update(q);q.tick=302;const auto turn=maze.update(q);
        if(deferred)check(turn.wait&&!turn.detour,"unavailable goal connection started a global-goal bypass");
        else check(turn.detour==Cell{13,50},
            "blocked goal connection replaced the installed terrain route with a destination bypass");
    }
    {
        Traffic flank;Traffic::Context q;q.tick=1;q.id=1;q.controller=1;q.player=0;
        q.target={50,50};q.position=q.target;q.footX=q.footZ=2;q.plainMove=q.goalReached=true;
        q.free=[](Cell){return true;};flank.update(q);
        q.id=2;q.controller=2;q.position={10,50};q.goalReached=false;q.steeringTarget=Cell{10,51};q.blocked=2;q.obstructionFriendly=true;
        q.arrivalReachable=[&](Cell at){return at==q.position;};q.terrainFree=[](Cell){return true;};
        q.obstruction=Traffic::Neighbor{1,0,{50,50},2,2,true};
        q.free=[](Cell at){return (at.x<=10&&at.z>=50)||at.z>=150;};
        flank.registerMove(q);q.tick=301;flank.update(q);q.tick=302;const auto lateral=flank.update(q);
        check(lateral.detour==Cell{10,114},"wide formation required an entire ingress route inside one local window");
        q.position=*lateral.detour;q.steeringTarget=Cell{10,115};q.arrivalReachable=[](Cell){return false;};q.tick=350;check(flank.update(q).wait,"continued flank search bypassed work admission");
        ++q.tick;const auto turn=flank.update(q);
        check(turn.detour==Cell{10,162},"committed flank reversed or forgot its side at a local-window boundary");
        q.position=*turn.detour;q.tick=400;const auto inward=flank.update(q);
        check(inward.detour==Cell{58,162},"committed flank did not take its newly available inward leg");
    }
    {
        for(bool sameGoal:{false,true}) {
            Traffic fringe;Traffic::Context q;q.tick=1;q.player=0;q.target={50,50};q.footX=q.footZ=2;q.plainMove=true;
            q.id=1;q.controller=1;q.position=q.target;q.goalReached=true;q.free=[](Cell){return true;};fringe.update(q);
            q.id=2;q.controller=2;q.position={26,50};q.goalReached=false;if(!sameGoal)q.target={80,50};fringe.registerMove(q);
            q.id=3;q.controller=3;q.position={10,50};q.target={50,50};q.steeringTarget=Cell{20,50};
            q.obstruction=Traffic::Neighbor{2,0,{26,50},2,2,false};
            q.free=[](Cell at){return at.x<=10||at.z>=98||at.z<=2;};q.terrainFree=[](Cell){return true;};
            q.arrivalReachable=[&](Cell at){return at==q.position;};fringe.registerMove(q);
            q.tick=301;fringe.update(q);q.tick=302;const auto around=fringe.update(q);
            check(sameGoal?around.detour==Cell{10,98}:!around.detour,
                "arrival bypass ignored a same-goal active fringe or affected an unrelated moving cohort");
        }
    }
    {
        Traffic pocket;Traffic::Context q;q.tick=1;q.player=0;q.target={50,50};q.footX=q.footZ=2;q.plainMove=true;
        q.id=1;q.controller=1;q.position=q.target;q.goalReached=true;q.free=[](Cell){return true;};pocket.update(q);
        q.id=2;q.controller=2;q.position={10,50};q.goalReached=false;q.steeringTarget=Cell{20,50};
        q.obstruction=Traffic::Neighbor{1,0,{50,50},2,2,true};q.arrivalReachable=[&](Cell at){return at==q.position;};
        q.terrainFree=[](Cell){return true;};q.free=[](Cell at){return at.x<=10&&at.z>=50&&at.z<=51;};
        pocket.registerMove(q);q.tick=301;pocket.update(q);q.tick=302;
        const auto first=pocket.update(q);check(first.detour==Cell{10,51},"arrival bypass rejected its only one-cell flank pocket");
        q.position=*first.detour;q.obstruction.reset();q.tick=350;
        q.free=[](Cell at){return (at.x<=10&&at.z>=50)||at.z>=110;};
        check(pocket.update(q).wait,"one-cell flank escape discarded its committed side");
        ++q.tick;check(pocket.update(q).detour==Cell{10,115},"one-cell flank escape did not continue toward the open side");
    }
    {
        // A single footprint on the chosen flank requires backing and two
        // turns before the long arrival bypass can continue on the same side.
        Traffic flank;Traffic::Context q;q.tick=1;q.id=1;q.controller=1;q.player=0;
        q.target={50,50};q.position=q.target;q.footX=q.footZ=2;q.plainMove=q.goalReached=true;
        q.free=[](Cell){return true;};flank.update(q);
        q.id=2;q.controller=2;q.position={10,50};q.goalReached=false;q.blocked=2;q.obstructionFriendly=true;
        q.steeringTarget=Cell{20,50};q.obstruction=Traffic::Neighbor{1,0,q.target,2,2,true};
        q.arrivalReachable=[&](Cell at){return at==q.position;};q.terrainFree=[](Cell){return true;};
        q.free=[](Cell p){return p==Cell{10,50}||(p.x==9&&p.z>=50&&p.z<=52)||(p.x==10&&p.z>=52&&p.z<=130);};
        flank.registerMove(q);q.tick=301;flank.update(q);++q.tick;auto result=flank.update(q);
        for(Cell corner:std::array<Cell,4>{{{9,50},{9,52},{10,52},{10,56}}}) {
            check(result.detour==corner,"arrival flank lost its bounded route around a side obstruction");
            check(sim::flow::directRoute(q.position,corner,42,q.free,[&](Cell p){return p==corner;}),
                "arrival flank crossed an occupied diagonal side");
            q.position=corner;++q.tick;result=flank.update(q);
        }
        if(!result.detour){++q.tick;result=flank.update(q);}
        check(result.detour==Cell{10,120},"local side route discarded the committed long arrival flank");
    }
    {
        // No westward exit exists in this component, but backing north opens
        // an eastward flank. A failed first side must still try the other.
        Traffic flank;Traffic::Context q;q.tick=1;q.id=1;q.controller=1;q.player=0;
        q.target={20,60};q.position=q.target;q.footX=q.footZ=2;q.plainMove=q.goalReached=true;
        q.free=[](Cell){return true;};flank.update(q);
        q.id=2;q.controller=2;q.position={20,20};q.goalReached=false;q.blocked=2;q.obstructionFriendly=true;
        q.steeringTarget=Cell{20,21};q.obstruction=Traffic::Neighbor{1,0,q.target,2,2,true};
        q.arrivalReachable=[&](Cell at){return at==q.position;};q.terrainFree=[](Cell){return true;};
        q.free=[](Cell p){return p==Cell{20,20}||(p.z==19&&p.x>=20&&p.x<=32);};
        flank.registerMove(q);q.tick=301;flank.update(q);++q.tick;auto result=flank.update(q);
        check(result.detour==Cell{20,19},"closed first flank prevented a legal opposite-side escape");
        q.position=*result.detour;++q.tick;result=flank.update(q);
        check(result.detour==Cell{26,19},"opposite-side flank lost its complete retained route");
    }
    for(int geometry=0;geometry<4;++geometry) {
        // Actual touching peers may fill two footprint strides around the
        // compact area. Every center shares that hard bound; contact cannot
        // grow a chain indefinitely, including alongside a single cliff.
        Traffic shell;Traffic::Context q;q.tick=1;q.player=0;q.target={100,100};
        q.footX=q.footZ=2;q.plainMove=true;q.free=[](Cell){return true;};
        q.arrivalReachable=[](Cell){return false;};
        q.terrainFree=[=](Cell p){return geometry==1?p.x!=144:geometry==3?p.z>=100:true;};
        if(geometry!=2)q.contactReachable=[&](Cell at){
            return sim::flow::directRoute(q.position,at,64,q.terrainFree,[&](Cell p){return p==at;});
        };
        for(int id=1;id<=211;++id){q.id=id;q.controller=id;q.position={100+(id-1)*3,100};shell.registerMove(q);}
        check(shell.arrivalRadiusSquared(q)==1899,"contact shell fixture lost its population area");
        q.id=1;q.controller=1;q.position=q.target;q.goalReached=true;shell.update(q);
        q.goalReached=false;q.blocked=2;
        for(int id=2;id<=20;++id) {
            q.id=id;q.controller=id;q.position={100+(id-1)*3,100};
            Traffic::Neighbor peer{id-1,0,{q.position.x-3,100},2,2,true};q.neighbors=std::span(&peer,1);
            const bool settled=shell.update(q).settled;
            const int last=geometry==1?15:geometry==2?16:17;
            check(settled==(id<=last),"contact shell crossed its hard radius, terrain wall, or missing strict proof");
        }
    }
    {
        // Widely separated giant reservations exhaust the bucket bound before
        // the unit-record bound. Refusal stays safe and all indexes prune away.
        Traffic capacity;Traffic::Context q=c;q.tick=1;q.footX=q.footZ=64;
        q.free=[](Cell){return true;};q.arrivalReachable=[](Cell){return true;};
        int admitted=0;
        for(int group=0;group<512;++group) {
            q.target={100+group*128,100};q.position={q.target.x-16,100};q.id=group*2+1;q.controller=q.id;capacity.registerMove(q);
            ++q.id;q.controller=q.id;capacity.registerMove(q);
            q.tick=uint32_t(group*2+1);capacity.update(q);++q.tick;
            admitted+=capacity.update(q).arrivalApproach;
        }
        check(admitted>0&&admitted<512,"reservation buckets were not bounded");
        check(capacity.bytes()<4*1024*1024,"bounded reservation fixture exceeded its memory charge");
        capacity.prune(16384,[](int,int,uint64_t,Cell,bool){return false;});check(capacity.bytes()==0,"capacity eviction retained reservation buckets");
    }
}
void localEscapes(){
    using Traffic=sim::flow::Traffic;using Cell=sim::flow::Cell;
    const auto context=[] {
        Traffic::Context c;c.id=1;c.player=0;c.controller=1;c.tick=1;c.plainMove=true;
        c.position={20,20};c.target={0,20};c.steeringTarget=c.target;c.footX=c.footZ=2;c.blocked=2;
        c.obstructionFriendly=true;c.obstruction=Traffic::Neighbor{1000,0,{19,20},2,2,true};
        c.terrainFree=[](Cell){return true;};return c;
    };
    const auto corridor=[](Cell p) {
        return (p.z==20&&p.x>=20&&p.x<=22)||(p.x==22&&p.z>=20&&p.z<=24)||
            (p.z==24&&p.x>=0&&p.x<=22);
    };
    const auto begin=[](Traffic& traffic,Traffic::Context& c) {
        traffic.registerMove(c);c.tick=301;traffic.update(c);++c.tick;return traffic.update(c);
    };
    {
        // Replay Hunter 86 needed to back out, step around a parked row, then
        // continue west. Returning to the field after the first leg looped.
        Traffic traffic;auto c=context();c.free=corridor;
        auto result=begin(traffic,c);
        for(Cell corner:std::array<Cell,3>{{{22,20},{22,24},{14,24}}}) {
            check(result.detour==corner,"local escape discarded its retreat/side/forward route");
            check(sim::flow::directRoute(c.position,corner,42,c.free,[&](Cell p){return p==corner;}),
                "local escape crossed a blocked footprint or diagonal side");
            c.position=corner;++c.tick;result=traffic.update(c);
        }
        check(result.repath,"completed local escape did not reanchor its terrain route");
    }
    {
        // This route exceeds one packed word and changes direction after it.
        Traffic traffic;auto c=context();c.position={32,32};c.target={0,32};c.steeringTarget=c.target;
        c.free=[](Cell p){return (p.z==32&&p.x>=32&&p.x<=38)||
            (p.x==38&&p.z>=32&&p.z<=48)||(p.z==48&&p.x>=0&&p.x<=38);};
        auto result=begin(traffic,c);
        for(Cell corner:std::array<Cell,3>{{{38,32},{38,48},{26,48}}}) {
            check(result.detour==corner,"long local route lost a retained direction");
            c.position=corner;++c.tick;result=traffic.update(c);
        }
        check(result.repath,"long local escape never returned to terrain navigation");
    }
    {
        Traffic traffic;auto c=context();c.free=corridor;auto result=begin(traffic,c);
        check(result.detour==Cell{22,20},"changed-body escape fixture has no route");
        c.position=*result.detour;++c.tick;result=traffic.update(c);
        check(result.detour==Cell{22,24},"changed-body fixture lost its second leg");
        c.free=[&](Cell p){return corridor(p)&&p!=Cell{22,22};};++c.tick;
        result=traffic.update(c);
        check(!result.detour,"retained local leg crossed a newly occupied intermediate cell");
        ++c.tick;result=traffic.update(c);
        check(result.wait&&!result.detour,"invalidated local route did not wait in its now-closed component");
    }
    {
        Traffic traffic;auto c=context();c.free=corridor;auto result=begin(traffic,c);
        check(result.detour.has_value(),"new-order escape fixture has no route");
        ++c.tick;++c.controller;++c.issuedTick;c.target={60,20};c.steeringTarget=c.target;
        c.blocked=0;c.obstruction.reset();c.free=[](Cell){return true;};
        check(!traffic.update(c).detour,"new command retained an old local escape");
    }
    {
        // Replay 58's next terrain corner was occupied. Its free west anchor
        // and then south anchor can reanchor the route without skipping a wall.
        Traffic traffic;auto c=context();c.position={335,167};c.target={300,200};
        c.steeringTarget=Cell{335,168};c.obstruction->position={335,168};
        c.free=[](Cell p){return p==Cell{335,167}||p==Cell{334,167}||p==Cell{334,168};};
        auto result=begin(traffic,c);
        check(result.detour==Cell{334,167},"occupied terrain corner prevented a legal lateral escape");
        c.position=*result.detour;++c.tick;result=traffic.update(c);
        check(result.detour==Cell{334,168},"local escape cut a diagonal occupied corner");
    }
    for(int kind=0;kind<3;++kind) {
        Traffic traffic;auto c=context();int queries=0;
        c.free=[&](Cell p) {++queries;return kind==0?p==c.position:
            kind==1?p.x==20&&p.z<=20:p.x>=20;};
        const auto result=begin(traffic,c);
        if(kind==0)check(result.wait&&!result.detour,"fully enclosed body kept circling inside its cell");
        else check(result.detour&&!result.wait,"bounded search mistook its window/work limit for an enclosure");
        check(queries<=600,"one local escape exceeded its fixed query limit and fallback allowance");
    }
    {
        // A crowded tick shares its footprint-work allowance, and deferred
        // higher IDs must eventually receive it even while earlier routes live.
        Traffic traffic;auto c=context();int queries=0;c.free=[&](Cell p){++queries;return corridor(p);};
        for(int id=1;id<=64;++id){c.id=id;c.controller=id;traffic.registerMove(c);}
        c.tick=301;for(int id=1;id<=64;++id){c.id=id;c.controller=id;traffic.update(c);}
        std::array<bool,64> admitted{};queries=0;
        for(c.tick=302;c.tick<334;++c.tick) {
            for(int id=1;id<=64;++id){c.id=id;c.controller=id;admitted[size_t(id-1)]|=bool(traffic.update(c).detour);}
            if(c.tick==302)check(queries*4<=4096,"local searches exceeded the shared full-footprint work allowance");
        }
        check(std::all_of(admitted.begin(),admitted.end(),[](bool value){return value;}),
            "shared local-search quota starved later moving units");
    }
    for(bool corridorGeometry:{false,true}) {
        // A cliff on one side leaves room to spread near the goal. Only a real
        // narrow corridor may extend arrival contact into a population queue.
        Traffic traffic;auto c=context();c.target={100,100};c.steeringTarget=c.target;
        c.obstruction.reset();c.free=[](Cell){return true;};c.arrivalReachable=[](Cell){return false;};
        c.terrainFree=[=](Cell p){return corridorGeometry?p.z==100:p.z>=100;};
        for(int id=1;id<=16;++id){c.id=id;c.controller=id;c.position={100+(id-1)*2,100};traffic.registerMove(c);}
        bool last=false;
        for(int id=1;id<=16;++id) {
            c.id=id;c.controller=id;c.position={100+(id-1)*2,100};c.goalReached=id==1;
            Traffic::Neighbor anchor{id-1,0,{c.position.x-2,100},2,2,true};
            c.neighbors=id==1?std::span<const Traffic::Neighbor>{}:std::span(&anchor,1);
            last=traffic.update(c).settled;
        }
        check(last==corridorGeometry,"arrival treated a one-sided cliff as a narrow holding corridor");
    }
}
void trafficRules(){
    sim::flow::Traffic traffic;
    sim::flow::Traffic::Context c;c.id=1;c.player=0;c.controller=1;c.tick=1;c.position={50,50};c.target={50,50};c.plainMove=true;c.goalReached=true;c.free=[](auto){return true;};
    check(traffic.update(c).settled,"arrival not recorded");
    sim::flow::Traffic::Neighbor n{1,0,{50,50},1,1,true};c.id=2;c.controller=2;c.position={52,50};c.blocked=4;c.goalReached=false;c.neighbors=std::span(&n,1);
    check(traffic.update(c).settled,"contact with same-destination arrival did not settle");
    c.id=3;c.controller=3;c.plainMove=false;
    check(!traffic.update(c).settled,"non-move mission incorrectly settled");
    c.id=4;c.controller=4;c.plainMove=true;c.target={100,100};
    check(!traffic.update(c).settled,"unrelated destination incorrectly settled");
    c.id=5;c.controller=5;c.target={50,50};n.player=1;
    check(!traffic.update(c).settled,"transferred arrival incorrectly settled");
    c.id=6;c.controller=6;n.player=0;n.position={51,50};
    check(!traffic.update(c).settled,"moved arrival incorrectly settled");
    // Terrain-constrained arrival queues remain finite. Open-ground contact
    // admits one footprint beyond the area, without an unbounded chain.
    for(bool constrained:{false,true}) {
        sim::flow::Traffic queue;
        sim::flow::Traffic::Context q;q.player=0;q.tick=1;q.target={100,100};
        q.plainMove=true;q.blocked=2;q.footX=2;q.footZ=2;q.free=[](auto){return true;};
        int queries=0;q.terrainFree=[&](auto at){++queries;return !constrained||at.z==100;};
        for(int id=1;id<=34;++id) {
            q.id=id;q.controller=id;q.position={100+(id-1)*2,100};q.goalReached=id==1;
            sim::flow::Traffic::Neighbor anchor{id-1,0,{q.position.x-2,100},2,2,true};
            q.neighbors=id==1?std::span<const sim::flow::Traffic::Neighbor>{}:std::span(&anchor,1);
            const bool settled=queue.update(q).settled;
            if(constrained)check(settled==(id<=33),"constrained arrival queue crossed its 64-cell bound or failed contact propagation");
            else check(settled==(id<=14),"open-ground arrival expanded beyond its single contact row");
            if(id<=13)check(queries==0,"ordinary arrival performed extra terrain queries");
        }
        check(queries>0,"arrival fixture did not exercise constrained fallback");
    }
    for(bool progress:{false,true})for(bool standable:{false,true}) {
        sim::flow::Traffic queue;
        sim::flow::Traffic::Context q;q.player=0;q.tick=1;q.target={100,100};
        q.plainMove=true;q.blocked=2;q.footX=q.footZ=2;
        q.terrainFree=[](auto){return true;};
        q.free=[&](auto at){return standable&&at==q.position;};
        for(int id=1;id<=15;++id) {
            q.id=id;q.controller=id;q.position={100+(id-1)*2,100};q.goalReached=id==1;
            sim::flow::Traffic::Neighbor anchor{id-1,0,{q.position.x-2,100},2,2,true};
            q.neighbors=id==1?std::span<const sim::flow::Traffic::Neighbor>{}:std::span(&anchor,1);
            check(queue.update(q).settled==(id<=14),"ordinary arrivals expanded their compact area");
        }
        sim::flow::Traffic::Neighbor anchor{14,0,{126,100},2,2,true};q.neighbors=std::span(&anchor,1);
        q.tick=200;++q.controller;check(!queue.update(q).settled,"internal retry prematurely finished a move");
        q.tick=300;check(!queue.update(q).settled,"arrival watchdog fired before 300 stagnant ticks");
        q.tick=301;if(progress)q.position.x=127;
        check(queue.update(q).settled==(!progress&&standable),
              "crowded arrival ignored progress or footprint legality");
        if(progress) {
            q.tick=600;check(!queue.update(q).settled,"actual progress failed to reset the arrival watchdog");
            q.tick=601;check(queue.update(q).settled==standable,"stagnant contact never finished after progress stopped");
        }
        q.id=16;q.controller=16;q.position={1000,100};q.tick=1000;
        check(!queue.nearArrival(q)&&!queue.update(q).settled,"arrival watchdog accepted a far-away convoy member");
        if(!progress&&standable) {
            q.id=15;q.position={128,100};++q.controller;q.issuedTick=q.tick;
            check(!queue.update(q).settled,"a fresh order inherited the previous arrival watchdog");
        }
    }
    {
        sim::flow::Traffic mixed;
        sim::flow::Traffic::Context q;q.tick=1;q.player=0;q.target={100,100};q.plainMove=true;
        q.id=1;q.controller=1;q.position=q.target;q.goalReached=true;q.footX=5;q.footZ=5;
        q.free=[](auto){return true;};q.terrainFree=[](auto){return false;};
        check(mixed.update(q).settled,"large arrival anchor did not settle");
        sim::flow::Traffic::Neighbor anchor{1,0,{100,100},5,5,true};
        q.id=2;q.controller=2;q.position={104,100};q.goalReached=false;q.blocked=2;q.footX=1;q.footZ=1;q.neighbors=std::span(&anchor,1);
        check(mixed.update(q).settled,"mixed-footprint contact did not settle");
        q.id=3;q.controller=3;q.target={101,100};
        check(!mixed.update(q).settled,"constrained arrival borrowed a different destination's anchor");
    }
    c.id=7;c.controller=7;c.position={10,10};c.target={100,10};c.plainMove=false;c.neighbors={};c.obstruction=sim::flow::Traffic::Neighbor{100,0,{11,10},1,1,false};
    traffic.update(c);++c.tick;
    auto turn=traffic.update(c);
    check(turn.detour&&turn.detour->x==12&&turn.detour->z==12,"opposing traffic did not prefer right hand side");
    ++c.tick;c.position={11,10};c.blocked=0;c.obstruction.reset();
    check(traffic.update(c).detour==turn.detour,"temporary detour was not retained while turning");
    ++c.tick;c.blocked=3;c.free=[](auto){return false;};
    check(!traffic.update(c).detour,"fully blocked footprint admitted an illegal detour");
    traffic.prune(16384,[](int,int,uint64_t,auto,bool){return false;});
    check(traffic.bytes()==0,"traffic cancellation pruning retained records");
    {
        // A legal lateral step does not certify its diagonal continuation.
        // The first onward step has a blocked side anchor even though every
        // sampled centre and the final endpoint are free.
        sim::flow::Traffic corner;
        sim::flow::Traffic::Context q;q.tick=1;q.id=1;q.player=0;
        q.position={10,10};q.target={100,100};q.steeringTarget=q.target;q.blocked=2;
        q.free=[](auto at){return at!=sim::flow::Cell{10,12}&&at!=sim::flow::Cell{8,13};};
        q.terrainFree=q.free;q.obstruction=sim::flow::Traffic::Neighbor{2,0,{11,11},1,1,false};
        corner.update(q);++q.tick;
        const auto lateral=corner.update(q);
        check(lateral.detour==sim::flow::Cell{8,12},"diagonal continuation fixture missed its legal lateral step");
        q.position=*lateral.detour;q.obstruction.reset();q.blocked=0;++q.tick;
        const auto onward=corner.update(q);
        check(!onward.detour&&onward.repath,"local continuation cut a blocked diagonal side anchor");
    }
    sim::flow::Traffic scheduled;
    c={};c.tick=1;c.position={10,10};c.target={100,10};c.blocked=2;c.free=[](auto){return true;};c.obstruction=sim::flow::Traffic::Neighbor{1000,0,{11,10},1,1,false};
    for(int id=1;id<=300;++id){c.id=id;c.controller=id;check(!scheduled.update(c).detour,"new request bypassed admission tick");}
    int admitted=0;c.tick=2;
    for(int id=1;id<=300;++id){c.id=id;c.controller=id;admitted+=bool(scheduled.update(c).detour);}
    check(admitted==256,"traffic exceeded or underused fixed detour admission budget");
    admitted=0;c.tick=3;
    for(int id=1;id<=300;++id){c.id=id;c.controller=id;admitted+=bool(scheduled.update(c).detour);}
    check(admitted==300,"round-robin traffic admission starved waiting units");
    {
        sim::flow::Traffic maze;
        sim::flow::Traffic::Context m;m.id=1;m.controller=1;m.tick=1;
        m.position={10,10};m.target={0,0};m.steeringTarget=sim::flow::Cell{20,10};
        m.blocked=2;m.free=[](auto){return true;};m.obstruction=sim::flow::Traffic::Neighbor{2,0,{11,10},1,1,false};
        maze.update(m);m.tick=2;
        const auto turn=maze.update(m);
        check(turn.detour&&turn.detour->x==12&&turn.detour->z==12,
              "maze avoidance steered toward global goal instead of installed route");
    }
    {
        // A catapult in the eight-AI maze had this full-footprint clearance:
        // only the immediately northern anchor was free. It never qualified
        // for opposing-traffic yield, so full-stride-only avoidance froze it.
        sim::flow::Traffic packed;sim::flow::Traffic::Context m;
        m.id=290;m.controller=1;m.tick=1;m.position={530,550};m.target={576,512};
        m.steeringTarget=sim::flow::Cell{543,537};m.footX=m.footZ=3;m.blocked=2;
        m.obstruction=sim::flow::Traffic::Neighbor{594,0,{533,547},3,3,false};
        m.free=[](sim::flow::Cell at){return at==sim::flow::Cell{530,550}||at==sim::flow::Cell{530,549};};
        packed.update(m);++m.tick;const auto escape=packed.update(m);
        check(escape.detour==sim::flow::Cell{530,549},"packed same-direction traffic ignored a legal short escape");
    }
    {
        sim::flow::Traffic parked;sim::flow::Traffic::Context m;
        m.id=1;m.controller=1;m.tick=1;m.position={10,10};m.target={100,10};m.blocked=2;
        m.steeringTarget=sim::flow::Cell{14,10};m.footX=m.footZ=2;
        m.obstruction=sim::flow::Traffic::Neighbor{2,0,{11,10},2,2,true};
        m.free=[](sim::flow::Cell at){return at.x<=10||at.z>=16||at.z<=4;};
        m.terrainFree=[](auto){return true;};
        parked.update(m);m.tick=2;const auto bypass=parked.update(m);
        check(bypass.detour==sim::flow::Cell{10,16},"parked formation did not get a clear lateral bypass");
        m.position=*bypass.detour;m.blocked=0;m.tick=180;
        const auto forward=parked.update(m);
        check(forward.detour==sim::flow::Cell{19,16},"bypass returned to its occupied intermediate corner");
        m.tick=361;
        check(parked.update(m).detour==forward.detour,"forward bypass inherited the lateral leg's expired deadline");
    }
    {
        sim::flow::Traffic terrain;
        sim::flow::Traffic::Context m;m.id=1;m.controller=1;m.tick=1;
        m.position={10,10};m.target={20,10};m.blocked=2;m.free=[](auto){return true;};
        terrain.update(m);m.tick=2;
        check(!terrain.update(m).detour,"static terrain refusal started a mobile detour");
        m.obstruction=sim::flow::Traffic::Neighbor{2,0,{11,10},1,1,true};
        m.obstruction->mobile=false;m.tick=3;terrain.update(m);m.tick=4;
        check(!terrain.update(m).detour,"static building refusal started a mobile detour");
    }
    {
        const auto pair=[](sim::flow::Traffic::Context& a,sim::flow::Traffic::Context& b) {
            a.id=1;a.controller=1;a.tick=1;a.position={10,10};a.target={20,10};a.blocked=2;
            b=a;b.id=2;b.controller=2;b.position={12,10};b.target={0,10};
            a.obstruction=sim::flow::Traffic::Neighbor{2,0,{12,10},1,1,false};
            a.obstruction->identity=sim::flow::Traffic::Identity{2,{0,10},0,0};
            b.obstruction=sim::flow::Traffic::Neighbor{1,0,{10,10},1,1,false};
            b.obstruction->identity=sim::flow::Traffic::Identity{1,{20,10},0,0};
            a.obstructionFriendly=b.obstructionFriendly=true;
        };
        // Static corridor classification can differ across a corner. A unit
        // must not wait merely because its own view gives it priority.
        {
            sim::flow::Traffic t;sim::flow::Traffic::Context a,b;pair(a,b);
            a.free=[](auto p){return p.z==10&&p.x<10;};b.free=[](auto){return false;};
            a.terrainFree=[](auto p){return p.z==10;};b.terrainFree=[](auto){return true;};
            t.update(a);t.update(b);a.tick=b.tick=2;
            const auto result=t.update(a);
            check(!result.wait&&result.detour&&result.detour->x<10,
                  "unacknowledged corridor priority blocked an available retreat");
        }
        {
            sim::flow::Traffic t;sim::flow::Traffic::Context a,b;pair(a,b);
            a.free=[](auto){return false;};b.free=[](auto p){return p.z==10&&p.x>12;};
            a.terrainFree=b.terrainFree=[](auto p){return p.z==10;};
            auto peer=*b.obstruction;b.lookup=[&](int){return std::optional(peer);};
            t.update(a);t.update(b);a.tick=b.tick=2;t.update(a);
            check(bool(t.update(b).detour),"corridor peer did not commit a retreat");
            a.tick=3;t.update(a);a.tick=4;const auto winner=t.update(a);
            check(!winner.wait&&!winner.detour,"acknowledged winner could not follow its route past the retreating peer");
            // A turn can put the winner's next footprint back across a parked
            // yielder even after its nominal lateral offset was reached.
            b.position={12,14};b.free=[](auto){return true;};
            peer.position={11,14};peer.steeringTarget=sim::flow::Cell{20,14};
            b.tick=200;t.update(b);b.tick=201;
            check(bool(t.update(b).detour),"parked yielder still blocked the winner's next footprint");
        }
        // Diagonal retreat needs its cardinal components at an L corner.
        // A single free cell is enough; requiring the full stride deadlocks.
        for(int space:{1,4}) {
            sim::flow::Traffic t;sim::flow::Traffic::Context a,b;pair(a,b);
            a.target={20,20};b.position={12,12};b.target={0,0};
            a.obstruction->position=b.position;a.obstruction->identity->target=b.target;
            b.obstruction->identity->target=a.target;
            const auto free=[=](sim::flow::Cell p){return
                (p.z==12&&p.x>=12&&p.x<=12+space)||(p.x==12&&p.z>=12&&p.z<=12+space);};
            a.free=b.free=a.terrainFree=b.terrainFree=free;
            t.update(a);t.update(b);a.tick=b.tick=2;t.update(a);
            const auto retreat=t.update(b);
            check(retreat.detour&&retreat.detour->x>12&&retreat.detour->z==12,
                  "diagonal corridor could not use a short cardinal escape");
        }
    }
    for(int change=0;change<8;++change) {
        sim::flow::Traffic corridor;
        sim::flow::Traffic::Context a,b;
        a.id=1;a.player=0;a.controller=1;a.tick=1;a.position={10,10};a.target={20,10};a.blocked=2;
        b=a;b.id=2;b.controller=2;b.position={12,10};b.target={0,10};
        a.free=b.free=[](auto p){return p.z==10&&p.x>10;};
        a.terrainFree=b.terrainFree=[](auto p){return p.z==10;};
        std::optional<sim::flow::Traffic::Neighbor> peer=sim::flow::Traffic::Neighbor{1,0,{10,10},1,1,false};
        peer->identity=sim::flow::Traffic::Identity{1,{20,10},0,0};
        a.obstruction=sim::flow::Traffic::Neighbor{2,0,{12,10},1,1,false};
        a.obstruction->identity=sim::flow::Traffic::Identity{2,{0,10},0,0};b.obstruction=peer;
        a.obstructionFriendly=b.obstructionFriendly=true;b.lookup=[&](int){return peer;};
        corridor.update(a);corridor.update(b);a.tick=b.tick=2;
        check(!corridor.update(a).wait,"unacknowledged priority stopped the winning stream");
        const auto retreat=corridor.update(b);
        check(retreat.detour&&retreat.detour->x>b.position.x,"narrow opposing stream did not back out");
        if(change==0)peer.reset();
        if(change==1)peer->idle=true;
        if(change==2)peer->player=1;
        if(change==3){a.tick=3;a.controller=9;a.target={30,10};corridor.update(a);}
        // These four changes deliberately leave the peer's Traffic record
        // stale, as happens while a retargeted unit remains in combat.
        if(change==4)peer->identity->controller=9;
        if(change==5)peer->identity->target={30,10};
        if(change==6)peer->identity->kind=2;
        if(change==7)peer->identity->targetId=19;
        b.tick=3;b.blocked=0;
        const auto released=corridor.update(b);
        check(released.repath&&!released.detour,"dead/stopped/captured/reordered blocker retained a yield");
    }
}
// Flowfield calls updateUnblockedFar before it builds neighbor and arrival
// callbacks. It must match full updates exactly, including near the arrival
// area and across record replacement, cancellation and dense-index fallback.
void unblockedFarEquivalence(){
    using Traffic=sim::flow::Traffic;using Cell=sim::flow::Cell;
    Traffic reference,fast;
    std::array<Traffic::Context,12> members;
    for(size_t i=0;i<members.size();++i){
        auto& c=members[i];c.id=i%2?int(i+1):sim::flow::IdIndex<int*>::dense+int(i);c.player=0;c.controller=uint64_t(i+1);
        c.tick=1;c.issuedTick=1;c.position={100,100+int(i)*3};c.target={520,118};c.steeringTarget=c.target;
        c.footX=c.footZ=2;c.plainMove=true;c.missionKind=1;
        reference.registerMove(c);fast.registerMove(c);
    }
    unsigned shortcuts=0,declines=0;
    for(uint32_t tick=1;tick<=700;++tick){
        if(tick==350){reference=Traffic{};fast=Traffic{};} // Moved-in empty tables.
        for(size_t i=0;i<members.size();++i){
            auto& c=members[i];c.tick=tick;
            if(c.position.x<c.target.x-int(i))++c.position.x;
            if(tick==100&&i%3==0)c.footZ=3;
            if(tick==150&&i%4==1){++c.issuedTick;c.target.z+=6;}
            if(tick==200&&i<3){reference.cancelUnsettled(c.id);fast.cancelUnsettled(c.id);}
            if(tick==250&&i==5)++c.controller;
            c.blocked=tick%97<3?2:0;
            c.goalReached=std::abs(c.position.x-c.target.x)+std::abs(c.position.z-c.target.z)<=4;
            auto full=c;
            full.free=[](Cell){return true;};full.terrainFree=full.free;
            full.arrivalReachable=[](Cell){return true;};full.contactReachable=full.arrivalReachable;
            const auto expected=reference.update(full);
            Traffic::Result actual;
            const auto before=fast.checksum();
            if(fast.updateUnblockedFar(c))++shortcuts;
            else{check(fast.checksum()==before,"declined Flowfield shortcut changed state");++declines;actual=fast.update(full);}
            check(expected.settled==actual.settled&&expected.detour==actual.detour&&expected.wait==actual.wait&&
                  expected.repath==actual.repath&&expected.arrivalApproach==actual.arrivalApproach&&
                  reference.checksum()==fast.checksum(),"Flowfield unblocked shortcut changed traffic results or state");
        }
    }
    check(shortcuts>2000&&declines>500,"Flowfield shortcut fixture missed fast or arrival paths");
    // Small groups: with 2-3 members of 1x1 footprint nearArrival's radius no
    // longer covers the arrival-area window (sqrt(area)+16)^2, so members that
    // stop between the two must still take the full update's area path.
    for(int n=2;n<=3;++n)for(int stop:{15,17,24}){
        Traffic ref,quick;std::vector<Traffic::Context> group(size_t(n),Traffic::Context{});
        for(int i=0;i<n;++i){
            auto& c=group[size_t(i)];c.id=i+1;c.player=0;c.controller=uint64_t(i+1);
            c.tick=1;c.issuedTick=1;c.position={100+i*3,118};c.target={520,118};c.steeringTarget=c.target;
            c.footX=c.footZ=1;c.plainMove=true;c.missionKind=1;ref.registerMove(c);quick.registerMove(c);
        }
        unsigned fast=0;
        for(uint32_t tick=1;tick<=600;++tick)for(auto& c:group){
            c.tick=tick;if(c.position.x<c.target.x-stop)++c.position.x;
            c.blocked=0;c.goalReached=false;
            auto full=c;
            full.free=[](Cell){return true;};full.terrainFree=full.free;
            full.arrivalReachable=[](Cell){return true;};full.contactReachable=full.arrivalReachable;
            const auto expected=ref.update(full);
            Traffic::Result actual;
            if(quick.updateUnblockedFar(c))++fast;else actual=quick.update(full);
            check(expected.settled==actual.settled&&expected.detour==actual.detour&&expected.wait==actual.wait&&
                  expected.repath==actual.repath&&expected.arrivalApproach==actual.arrivalApproach&&
                  ref.checksum()==quick.checksum(),"Flowfield small-group shortcut skipped the arrival-area path");
        }
        check(fast>0,"Flowfield small-group fixture never took the shortcut");
    }
}
}
int main(int argc,char**argv){if(argc!=2)return 2;try{
    areaArrivals();trafficRules();localEscapes();unblockedFarEquivalence();if(std::string_view(argv[1])=="--traffic-only"){std::puts("PASS flow traffic geometry, admission and local escapes");return 0;}auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,false);
    auto type=*registry.find("arasword");type.weapons.clear();type.weapon.damage=0;
    sim::World serial,parallel;setup(serial,true);setup(parallel,false);
    for(auto* w:{&serial,&parallel})for(int i=0;i<100;++i){const int id=w->spawn(&type,float(160+(i%10)*40),float(160+(i/10)*40),std::nullopt,0);w->order(id,1500,1500,false);}
    int completed=0;
    for(int tick=0;tick<5000;++tick){serial.tick(1.f/30);parallel.tick(1.f/30);check(serial.stateHash()==parallel.stateHash(),"crowd differs with worker execution");completed=0;for(int id=1;id<=100;++id)completed+=serial.unit(id)->orders.empty();if(completed==100)break;}
    int blocked=0;for(int id=1;id<=100;++id){const auto* u=serial.unit(id);blocked+=u->bodyBlockStreak>=2;if(u->orders.empty())check(std::abs(u->x.floorInt()-1500)<=512&&std::abs(u->z.floorInt()-1500)<=512,"crowd settled far from destination");}
    std::printf("flow crowd: tick=%u completed=%d blocked=%d hash=%016llx\n",serial.tickCount(),completed,blocked,(unsigned long long)serial.stateHash());
    if(completed!=100)for(int id=1;id<=100;++id)if(!serial.unit(id)->orders.empty()) {
        const auto& u=*serial.unit(id);std::printf("crowd active id=%d at=%.2f,%.2f footprint=%d,%d speed=%.3f blocked=%d\n",
            id,u.x.toFloat(),u.z.toFloat(),u.type->footX,u.type->footZ,u.speed.toFloat(),u.bodyBlockStreak);
    }
    check(completed==100,"crowd did not settle");
    {
        sim::World w;setup(w,true);
        for(int i=0;i<40;++i){const int id=w.spawn(&type,float(i<20?320+(i%5)*40:1500+(i%5)*40),float(720+(i%4)*40),std::nullopt,0);w.order(id,i<20?1700.f:160.f,800,false);}
        int done=0;
        for(int tick=0;tick<5000;++tick){w.tick(1.f/30);done=0;for(int id=1;id<=40;++id)done+=w.unit(id)->orders.empty();if(done==40)break;}
        std::printf("flow opposing: tick=%u completed=%d\n",w.tickCount(),done);
        if(done!=40)for(int id=1;id<=40;++id){const auto* u=w.unit(id);if(!u->orders.empty())std::printf("opposing blocked id=%d x=%d z=%d streak=%d\n",id,u->x.floorInt(),u->z.floorInt(),u->bodyBlockStreak);}
        check(done==40,"opposing groups remained deadlocked");
    }
    {
        sim::World w;setup(w,true);
        const int id=w.spawn(&type,160,800,std::nullopt,0);
        const int idle=w.spawn(&type,1000,800,std::nullopt,0);
        w.order(id,1776,800,false);
        for(int tick=0;tick<5000&&!w.unit(id)->orders.empty();++tick)w.tick(1.f/30);
        check(w.unit(id)->orders.empty()&&w.unit(id)->x.floorInt()>1700,"stationary unrelated body blocked flow route permanently");
        check(w.unit(idle)->x.floorInt()==1000&&w.unit(idle)->z.floorInt()==800,"traffic moved an idle unit without an order");
    }
    {
        auto giant=type;giant.id="flow_arrival_giant";giant.footX=giant.footZ=12;
        sim::World w;setup(w,true);
        const int anchor=w.spawn(&giant,1000,1000,std::nullopt,0);
        const int follower=w.spawn(&type,888,1000,std::nullopt,0);
        w.order(anchor,1000,1000,false);w.order(follower,1000,1000,false);
        for(int tick=0;tick<1500&&(!w.unit(anchor)->orders.empty()||!w.unit(follower)->orders.empty());++tick)w.tick(1.f/30);
        check(w.unit(anchor)->orders.empty()&&w.unit(follower)->orders.empty(),"large-footprint arrival anchor was missed by local traffic lookup");
        check(w.unit(follower)->x.floorInt()<920,"mixed group arrival overlapped the giant footprint");
    }
    for(int variant=0;variant<3;++variant) {
        const int width=variant==1?256:128;
        const int laneZ=variant==2?16:800;
        sim::World a,b;
        for(auto* w:{&a,&b}) {
            setup(*w,w==&a);w->setTerrain(std::vector<uint8_t>(width*128,100),width,128,64);
            const int end=width==128?88:160;
            if(variant==2)w->blockCells(40,3,end-40,125,true);
            else {w->blockCells(40,0,end-40,49,true);w->blockCells(40,52,end-40,76,true);}
            const int left=w->spawn(&type,480,float(laneZ),std::nullopt,0);
            const int right=w->spawn(&type,float(end*16+144),float(laneZ),std::nullopt,0);
            w->order(left,float(end*16+392),float(laneZ),false);w->order(right,320,float(laneZ),false);
        }
        for(int tick=0;tick<8000&&(!a.unit(1)->orders.empty()||!a.unit(2)->orders.empty());++tick) {
            a.tick(1.f/30);b.tick(1.f/30);
            check(a.stateHash()==b.stateHash(),"corridor yielding differs with workers");
        }
        std::printf("flow corridor: width=%d tick=%u done=%d,%d\n",width,a.tickCount(),a.unit(1)->orders.empty(),a.unit(2)->orders.empty());
        check(a.unit(1)->orders.empty()&&a.unit(2)->orders.empty(),"narrow opposing corridor did not drain");
    }
    {
        // A winner can switch to stationary combat without refreshing its
        // traffic record. The retreating peer must observe the live order.
        sim::World w;setup(w,true);
        w.blockCells(40,0,48,49,true);w.blockCells(40,52,48,76,true);
        auto armed=*registry.find("arasword");
        const int left=w.spawn(&armed,480,800,std::nullopt,0);
        const int right=w.spawn(&type,1552,800,std::nullopt,0);
        w.order(left,1800,800,false);w.order(right,320,800,false);
        int minimum=1552;bool retreating=false;
        for(int tick=0;tick<4000;++tick){w.tick(1.f/30);const int x=w.unit(right)->x.floorInt();minimum=std::min(minimum,x);if(x>minimum+48){retreating=true;break;}}
        check(retreating,"combat cancellation fixture did not start yielding");
        const int before=w.unit(right)->x.floorInt();
        const int winnerX=w.unit(left)->x.floorInt();
        auto dummy=type;dummy.maxHp=20000;
        const int enemy=w.spawn(&dummy,float(winnerX-40),800,std::nullopt,1);
        w.attack(left,enemy,false);
        bool released=false;
        for(int tick=0;tick<300;++tick){w.tick(1.f/30);if(w.unit(right)->x.floorInt()<before-16){released=true;break;}}
        check(w.unit(enemy)&&w.unit(enemy)->hp>sim::Fixed(),"combat cancellation target died unexpectedly");
        check(released,"live stationary combat order retained old corridor yield");
        check(std::abs(w.unit(left)->x.floorInt()-winnerX)<16,"combat peer was not stationary");
    }
    {
        // Packed diagonal following used to latch old body refusals: all
        // units technically moved, but the interior stayed nearly stationary.
        sim::World a,b;
        constexpr int count=4000;
        for(auto* w:{&a,&b}) {
            setup(*w,w==&a);w->setTerrain(std::vector<uint8_t>(2048*2048,100),2048,2048,64);
            for(int i=0;i<count;++i){const int id=w->spawn(&type,float(512+i%128*32),float(512+i/128*32),std::nullopt,0);w->order(id,24000,24000,false);}
        }
        for(int tick=0;tick<1200;++tick){a.tick(1.f/30);b.tick(1.f/30);check(a.stateHash()==b.stateHash(),"dense convoy differs with workers");}
        int64_t progress=0;
        for(int i=0;i<count;++i){const auto* u=a.unit(i+1);progress+=u->x.floorInt()-512-i%128*32;progress+=u->z.floorInt()-512-i/128*32;}
        std::printf("flow convoy: mean forward displacement=%.1f\n",double(progress)/count);
        check(progress>int64_t(count)*350,"dense convoy latched old body refusals");
    }
    for(int kind=0;kind<2;++kind) {
        sim::World w;setup(w,true);w.blockCells(63,0,2,65,true);
        const int id=w.spawn(&type,160,160,std::nullopt,0);
        if(kind==0)w.attackMove(id,1776,192,false);else w.patrolTo(id,1776,192,false);
        bool reached=false;
        for(int tick=0;tick<10000;++tick){w.tick(1.f/30);if(w.unit(id)->x.floorInt()>1700){reached=true;break;}}
        std::printf("flow mission: kind=%d tick=%u reached=%d\n",kind,w.tickCount(),reached);
        check(reached,"fight/patrol failed to navigate around static obstacle");
        check(w.flowStats().deliveries>0,"fight/patrol bypassed flow route service");
    }
    std::puts("PASS flow mission traffic");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
