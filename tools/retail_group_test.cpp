// Retail mode group pacing (51b890 / 4d95f0 / 402880): slowest type speed,
// straggler slowdown, and re-forming through Move_Ground_Formation. Retail+
// keeps its own documented pacing and must not see any of this.
#include "cooperative_test_common.h"
#include <cstdlib>

using namespace cooperative_test;
namespace {
bool formationMission(const Unit& u) {
    return std::any_of(u.orders.begin(),u.orders.end(),[](const Order& o){return o.formationLevel!=0;});
}

// A Ctrl-group (+N) paces at the slowest member's type speed.
void slowest() {
    World world;setup(world,PathfindingMode::Retail);auto slow=mover(1),fast=mover(0);
    const int a=world.spawn(&slow,640,2048,std::nullopt,0),b=world.spawn(&fast,640,2096,std::nullopt,0);
    for(int id:{a,b}){world.setSquad(id,1);world.order(id,3200,2072,false);}
    known(world);Fixed maximum;
    int reform=0;
    for(int tick=0;tick<600;++tick){const auto x=world.unit(b)->x,z=world.unit(b)->z;
        // A member re-forming (Move_Ground_Formation) is not paced: retail's
        // formation mission lacks the 0x2000000 flag the cap keys on.
        const bool paced=!formationMission(*world.unit(b));world.tick(1.f/30);
        reform+=!paced||formationMission(*world.unit(b));
        (void)x;(void)z;
        if(!paced||formationMission(*world.unit(b)))continue;
        // The mover's speed, not the displacement: the Q13 heading table
        // rounds a diagonal step up to ~1% longer than the speed.
        const Fixed step=world.unit(b)->speed;maximum=fxMax(maximum,step);
// Retail scales the group speed by the member's own/type maximum
        // (individual speed variation), as 4d95f0 does.
        const auto& fu=*world.unit(b);
        const Fixed cap=Fixed::raw(retailGroupSpeedCap(slow.maxVel.v,fu.baseSpeed.v,fu.type->maxVel.v,fu.type->maxVel.v));
        require(step<=cap,"grouped fast member exceeded the slowest type speed");}
    std::printf("slowest: reform ticks=%d\n",reform);
    const auto* record=world.retailGroupRecord(b);
    require(record&&record->active&&record->groundSpeed==slow.maxVel.v,"group speed is not the slowest type maximum");
    require(maximum>slow.maxVel-Fixed::fromFloat(0.05f)&&maximum<slow.maxVel+Fixed::fromFloat(0.05f),"grouped fast member did not reach the group speed");
    // Ungrouped, the same fast unit keeps its own maximum.
    World alone;setup(alone,PathfindingMode::Retail);
    const int c=alone.spawn(&fast,640,2096,std::nullopt,0);alone.order(c,3200,2072,false);known(alone);
    Fixed free;
    for(int tick=0;tick<300;++tick){const auto x=alone.unit(c)->x;alone.tick(1.f/30);free=fxMax(free,alone.unit(c)->x-x);}
    require(free>slow.maxVel+Fixed::fromFloat(0.5f),"ungrouped unit was paced");
    std::printf("scenario=slowest grouped_max=%.4f ungrouped_max=%.4f\n",maximum.toFloat(),free.toFloat());
}

// A member far behind slows the group by 0xaa7e/65536 instead of sprinting,
// then re-forms through a Move_Ground_Formation mission.
void straggler(PathfindingMode selected) {
    World world;setup(world,selected);auto type=mover(1);std::vector<int> ids;
    for(int i=0;i<3;++i)ids.push_back(world.spawn(&type,float(1200),float(2000+i*48),std::nullopt,0));
    ids.push_back(world.spawn(&type,600,2048,std::nullopt,0));
    for(int id:ids){world.setSquad(id,2);world.order(id,3600,2048,false);}
    known(world);world.tick(1.f/30);
    const auto* record=world.retailGroupRecord(ids[0]);
    if(selected!=PathfindingMode::Retail) {
        require(!record,"non-Retail mode exposed a native group record");
    } else {
        require(record&&record->active,"Retail group record inactive");
        require(record->groundSpeed==retailGroupStragglerSpeed(type.maxVel.v,type.maxVel.v),
                "one straggler did not slow the group by 0xaa7e/65536");
        require(record->groundSpeed==0xaa7e,"straggler slowdown is not the native factor");
    }
    bool reformed=false,retired=false,fast=false;
    for(int tick=0;tick<12000&&!finished(world,ids);++tick) {
        world.tick(1.f/30);
        const auto& u=*world.unit(ids[3]);
        if(formationMission(u))reformed=true;else if(reformed)retired=true;
        if(u.speed>u.baseSpeed)fast=true;
        for(int id:ids)require(selected==PathfindingMode::Retail||!formationMission(*world.unit(id)),
            "non-Retail mode pushed a formation mission");
        if(tick%30==0)legal(world,ids);
    }
    for(int id:ids){const auto& u=*world.unit(id);std::printf("member id=%d x=%.1f z=%.1f orders=%zu formation=%d\n",id,u.x.toFloat(),u.z.toFloat(),u.orders.size(),formationMission(u));
}
    require(!fast,"straggler sprinted past its own maximum");
    require(finished(world,ids),"group did not complete its move");
    if(selected==PathfindingMode::Retail) {
        require(reformed,"straggler never received Move_Ground_Formation");
        require(retired,"Move_Ground_Formation never retired");
    }
    std::printf("scenario=straggler mode=%d reformed=%d retired=%d tick=%u\n",int(selected),reformed,retired,world.tickCount());
}

// Native slot geometry spot checks (51c700/51ce40/51d1e0).
void geometry() {
    RetailGroupRecord g;g.active=true;
    auto& all=g.all[0];all.count=4;all.area=16;all.centre={100,0,200};
    auto& moving=g.moving[0];moving.count=3;moving.area=12;moving.centre={120,0,200};
    RetailGroupMember u;u.area=4;u.position={300,0,200};
    require(retailOctDistance(-30,40)==47,"octagonal distance");
    const auto c=retailGroupCentre(g,u,true,true);
    require(c.x==(3*120+300)/4 && c.y==c.x && c.z==200,"self-inclusive ground centre");
    require(retailGroupRadius(g,u,true,true)==16 && retailGroupRadius(g,u,false,false)==16,"radius");
    // distance 180 px -> 11 cells; 121/4=30 > 12*2 at level 4 (moving, not self).
    require(retailGroupOutOfSlot(g,u,true,4,false),"level-4 straggler test");
    u.position={150,0,200};
    require(!retailGroupOutOfSlot(g,u,true,4,false),"member inside its slot");
    g.moving[0].count=0;   // empty moving subset relaxes the level by one
    require(!retailGroupOutOfSlot(g,u,true,5,false),"level 5 relaxed to 6 is never out of slot");
    require(retailGroupSpeedCap(65536,117964,117964,117964)==65536,"cap at group speed");
    require(retailGroupSpeedCap(10000,117964,117964,117964)==int32_t((int64_t(65536)*29491)>>16),"25% floor");
    require(retailGroupSpeedCap(200000,117964,117964,117964)==0,"no cap above own maximum");
}
}

int main() {
    try {
        geometry();slowest();
        straggler(PathfindingMode::Retail);straggler(PathfindingMode::RetailPlus);
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
    std::puts("retail group pacing: PASS");
    return 0;
}
