#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/flowtraffic.h"
#include <cstdio>
#include <stdexcept>
using namespace tak;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void setup(sim::World& w,bool serial){w.setVisPlayer(-1);w.setSerialThreads(serial);w.setPathService(true);w.setPathfindingMode(sim::PathfindingMode::Flowfield);w.setTerrain(std::vector<uint8_t>(128*128,100),128,128,64);}
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
}
int main(int argc,char**argv){if(argc!=2)return 2;try{
    trafficRules();auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,false);
    auto type=*registry.find("arasword");type.weapons.clear();type.weapon.damage=0;
    sim::World serial,parallel;setup(serial,true);setup(parallel,false);
    for(auto* w:{&serial,&parallel})for(int i=0;i<100;++i){const int id=w->spawn(&type,float(160+(i%10)*40),float(160+(i/10)*40),std::nullopt,0);w->order(id,1500,1500,false);}
    int completed=0;
    for(int tick=0;tick<5000;++tick){serial.tick(1.f/30);parallel.tick(1.f/30);check(serial.stateHash()==parallel.stateHash(),"crowd differs with worker execution");completed=0;for(int id=1;id<=100;++id)completed+=serial.unit(id)->orders.empty();if(completed==100)break;}
    int blocked=0;for(int id=1;id<=100;++id){const auto* u=serial.unit(id);blocked+=u->bodyBlockStreak>=2;if(u->orders.empty())check(std::abs(u->x.floorInt()-1500)<=512&&std::abs(u->z.floorInt()-1500)<=512,"crowd settled far from destination");}
    std::printf("flow crowd: tick=%u completed=%d blocked=%d hash=%016llx\n",serial.tickCount(),completed,blocked,(unsigned long long)serial.stateHash());
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
