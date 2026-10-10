// Legion flyer instruments (PLAN 4 W6 step 0), asset-free.
//
//   legion_flyers_test CASE
//
// Measurement cases for the flyer themes (T5 Ctrl/Alt flight stations, T6 lift,
// touchdown, formation release, command link). Each builds a small flat map with
// feature walls (the same placement plane a loaded match uses), prints the
// numbers the W6 steps move, and asserts determinism (the serial run equals the
// workers run). With W6_REQUIRE=1 it also asserts the PLAN targets, so each case
// fails on a head that does not meet them yet and is the acceptance check of the
// step that fixes it (docs/legion-w6-step0.md lists case, target and head value).
//
// Cases: mixedsquad mixedseal mv08group deadendrejoin(mv08deadend) ctrlmixed
// ctrlmixedbig factoryjoin landunder descentwalkin auditlift auditlift40 fl04
// fl05match fl05unreach splitpatrol liftcombat.
// Env: W6_REQUIRE (assert targets), W6_VERBOSE (traces), W6_SHIFT (start offset in cells).
// Same as tools/legion_world_test.cpp: no libm transcendentals reach the sim from here
// (std::hypot below only measures).
#include "sim/sim.h"
#include "sim/matchsetup.h"
#include "sim/footprint.h"
#include "legion_issue_selection.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tak::sim {
// World keeps its flight stations private (sim.h grants this probe friendship).
struct RetailReplayProbe {
    // 0: none; 1: a station is active; 2: active and holding the leg open.
    static int station(const World& w,const Unit& u) {
        const auto* s=w.legionFlightStation(u);
        return !s?0:s->hold?2:1;
    }
};
}

using namespace tak::sim;
namespace {
void check(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
int envInt(const char* key,int fallback) {const char* e=std::getenv(key);return e?std::atoi(e):fallback;}
bool requireW6() {return std::getenv("W6_REQUIRE")!=nullptr;}
bool verbose() {return std::getenv("W6_VERBOSE")!=nullptr;}
int g_shift=envInt("W6_SHIFT",0);   // cells every spawn is shifted by (the offsets of the baselined tests: 0, +1, -1, +2, -2)

UnitType mover(int foot,Fixed vel=Fixed::raw(117964)) {
    UnitType t{};t.id=t.name="legion-foot-"+std::to_string(foot);
    t.canMove=true;t.maxHp=100;t.footX=t.footZ=foot;t.sight=4096;t.maxVel=vel;
    t.accel=t.brake=Fixed::fromInt(10);t.turnRate=t.turnInPlaceRate=2500;t.halfCellTicks=3;t.buildTime=1;
    return t;
}
// The formation flyer of legion_world_test's mixedformation (vtolStandby: an idle flyer lands).
UnitType flyerType(int foot,bool standby=true) {
    UnitType flyer{};flyer.id=flyer.name="legion-flyer";
    flyer.canFly=flyer.canMove=true;flyer.maxHp=100;flyer.footX=flyer.footZ=foot;flyer.sight=4096;
    flyer.maxVel=Fixed::fromInt(4);flyer.accel=flyer.brake=Fixed::fromInt(1);flyer.turnRate=1200;flyer.cruiseAlt=80;flyer.buildTime=1;
    flyer.vtolStandby=standby;
    return flyer;
}

struct Fixture {
    World world;
    int width,height;
    std::vector<uint16_t> cells;
    TypeRegistry registry;
    Fixture(int w,int h,bool serial=true):width(w),height(h),cells(size_t(w)*h,0xffff) {
        world.setGameSeed(7);world.setVisPlayer(-1);world.setSerialThreads(serial);world.setPathService(true);
        world.setPathfindingMode(PathfindingMode::Legion);
        world.setPlayerCount(2);world.setTeam(0,0);world.setTeam(1,1);
        world.setTerrain(std::vector<uint8_t>(size_t(w)*h,100),w,h,64);
    }
    void wall(int x,int z) {
        if(x<0||z<0||x>=width||z>=height)return;
        cells[size_t(z)*width+x]=0;world.blockCells(x,z,1,1,true);
    }
    void rect(int x,int z,int w,int h) {for(int j=0;j<h;++j)for(int i=0;i<w;++i)wall(x+i,z+j);}
    void publish() {world.setMapPlacementFeatures(cells,{{"legion-wall",1,1,true,true,false,0}});}
    int spawn(const UnitType& t,int cx,int cz,int player=0) {
        const int id=world.spawn(&t,float((cx+g_shift)*16),float((cz+g_shift)*16),std::nullopt,player);
        check(id>0,"spawn failed");return id;
    }
    void start() {
        world.tick(1.f/30);
        world.updateNavigationExploration();
        auto& explored=const_cast<std::vector<uint16_t>&>(world.navigationExploration());
        std::fill(explored.begin(),explored.end(),0xffff);
    }
    bool legal(int id) {
        const auto& u=*world.unit(id);
        return world.mobilePlacement(u,footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),false);
    }
    void command(tak::net::Cmd k,int id,int target,float x,float z,int player=0) {
        tak::net::Command c;c.kind=k;c.player=player;c.unitId=id;c.targetId=target;c.x=x;c.z=z;c.queue=0;
        applyCommand(world,registry,c);
    }
    void tick(int n=1) {for(int i=0;i<n;++i)world.tick(1.f/30);}
    // Land a click's batches tick by tick, as the sim applies them.
    void land(const std::vector<tak::scn::CmdBatch>& batches) {
        for(const auto& b:batches) {
            while(world.tickCount()<b.tick)world.tick(1.f/30);
            for(const auto& c:b.cmds)applyCommand(world,registry,c);
        }
    }
    size_t click(const std::vector<int>& sel,tak::scn::Verb verb,float x,float z) {
        tak::scn::SelectionOrder o;o.verb=verb;o.x=x;o.z=z;
        auto b=tak::scn::issueSelection(world,sel,o,world.tickCount(),0,-1);
        land(b);return b.size();
    }
};

struct Out {std::string text;uint64_t hash=0;};
int field(const Out& o,const char* key) {const auto p=o.text.find(std::string(key)+"=");return p==std::string::npos?-1:std::atoi(o.text.c_str()+p+std::strlen(key)+1);}
void print(const Out& o) {std::printf("%s hash=%016llx\n",o.text.c_str(),(unsigned long long)o.hash);}
// Determinism: the workers run prints and hashes exactly as the serial run.
void checkWorkers(const char* name,const std::function<Out(bool)>& run,const Out& serial) {
    const Out workers=run(false);
    std::printf("%s determinism serial=%016llx workers=%016llx\n",name,(unsigned long long)serial.hash,(unsigned long long)workers.hash);
    check(workers.hash==serial.hash&&workers.text==serial.text,std::string(name)+": serial and workers differ");
}

// Airborne, all but still, not descending: the hover of mixedformation.
bool hovering(const Unit& u,int t) {return u.flightGroundMode==2&&u.speed<Fixed::fromFloat(0.25f)&&!u.landing&&t>=60;}
float dist(const Unit& u,float x,float z) {return std::hypot(u.x.toFloat()-x,u.z.toFloat()-z);}
// The ground-flyer overlap metric: pairs (landed flyer, ground body) whose footprints share a cell.
int landedOverlap(World& w,const std::vector<int>& flyers,const std::vector<int>& ground) {
    int n=0;
    for(int fid:flyers) {
        const auto& v=*w.unit(fid);
        if(v.flightGroundMode!=1)continue;
        const int fx=v.type->footX,fz=v.type->footZ,vx=footprintOrigin(v.x,fx),vz=footprintOrigin(v.z,fz);
        for(int gid:ground) {
            const auto& u=*w.unit(gid);
            const int gx=u.type->footX,gz=u.type->footZ,ux=footprintOrigin(u.x,gx),uz=footprintOrigin(u.z,gz);
            n+=ux<vx+fx&&vx<ux+gx&&uz<vz+fz&&vz<uz+gz;
        }
    }
    return n;
}
std::vector<int> cat(std::vector<int> a,const std::vector<int>& b) {a.insert(a.end(),b.begin(),b.end());return a;}
template<class T> T quantile(std::vector<T> v,double q) {
    if(v.empty())return T(-1);
    std::sort(v.begin(),v.end());
    return v[std::min(v.size()-1,size_t(q*double(v.size())))];
}
// The most common convoyTick on the current legs of `ids` (ConvoyTable::kNone when none carry one).
uint32_t modalConvoy(World& w,const std::vector<int>& ids) {
    std::map<uint32_t,int> n;
    for(int id:ids) {
        const auto& u=*w.unit(id);
        if(!u.orders.empty()&&u.orders.back().convoyTick!=ConvoyTable::kNone)++n[u.orders.back().convoyTick];
    }
    uint32_t best=ConvoyTable::kNone;int most=0;
    for(const auto& [t,c]:n)if(c>most) {most=c;best=t;}
    return best;
}
uint32_t convoyOf(const Unit& u) {return u.orders.empty()?ConvoyTable::kNone:u.orders.back().convoyTick;}

// ---------------------------------------------------------------------------
// mixedsquad / mixedseal: legion_world_test's mixedformation (20 ground bodies and 8 flyers
// behind a wall with a gap at its south end, one order) with the squad as a parameter, the
// ground-flyer overlap metric, the station hold counter and the flyers' convoy link.
// squad -1: Alt+1 formation, +1: Ctrl+1 group, 0: no squad. `seal` closes the gap (MIXED_SEAL):
// the ground walks to the wall and holds with its order kept.
// ---------------------------------------------------------------------------
struct MixedOpts {
    int squad=-1;bool legion=true;tak::net::Cmd kind=tak::net::Cmd::Move;bool seal=false;bool serial=true;int ticks=0;
};
struct MixedRun {
    int groundDone=-1,flyersDone=-1;float maxAway=0,meanAway=0,endAway=0;uint64_t hash=0;int landedAhead=0;
    int windows=0,stalls=0,moving=0,misaligned=0,maxHover=0,maxLandDelay=0,landed=0;double flown=0,marched=0;
    int overlap=0,illegal=0,holdTicks=0,convoyMatch=0;uint64_t lifts=0,relifts=0;std::string text;
};
MixedRun mixedRun(const MixedOpts& o) {
    Fixture f(260,100,o.serial);
    // A wall across the straight line: the ground detours through the gap at its south end.
    f.rect(110,o.seal?0:8,4,o.seal?100:72);f.publish();
    if(!o.legion)f.world.setPathfindingMode(PathfindingMode::Retail);
    const auto type=mover(2);
    const auto flyer=flyerType(3);
    std::vector<int> ground,flyers,all;
    for(int i=0;i<20;++i)ground.push_back(f.spawn(type,14+(i%5)*3,40+(i/5)*4));
    for(int i=0;i<8;++i)flyers.push_back(f.spawn(flyer,16+(i%4)*4,58+(i/4)*4));
    all=ground;all.insert(all.end(),flyers.begin(),flyers.end());
    f.start();
    if(o.squad)for(int id:all)f.command(tak::net::Cmd::SetSquad,id,o.squad,0,0);
    for(int t=0;t<300;++t)f.world.tick(1.f/30);
    const float px=220*16,pz=50*16;
    // As the move UI does: flyers keep their offset from the selection's centre, clamped to
    // 60 px per axis; Legion surface movers share the point.
    float sx=0,sz=0;
    for(int id:all) {sx+=f.world.unit(id)->x.toFloat();sz+=f.world.unit(id)->z.toFloat();}
    sx/=float(all.size());sz/=float(all.size());
    for(int id:all) {
        const auto& u=*f.world.unit(id);
        const bool offset=o.kind==tak::net::Cmd::Move&&(u.type->canFly||!o.legion);
        f.command(o.kind,id,0,offset?px+std::clamp(u.x.toFloat()-sx,-60.f,60.f):px,
                  offset?pz+std::clamp(u.z.toFloat()-sz,-60.f,60.f):pz);
    }
    MixedRun r;double sum=0;int samples=0;
    std::map<int,std::pair<float,float>> windowStart;float windowCx=0,windowCz=0;
    std::map<int,int> idleAt,hoverRun;
    const uint32_t groundConvoy=modalConvoy(f.world,ground);
    for(int id:flyers)r.convoyMatch+=groundConvoy!=ConvoyTable::kNone&&convoyOf(*f.world.unit(id))==groundConvoy;
    const int ticks=o.ticks?o.ticks:o.kind==tak::net::Cmd::Patrol?3000:6000;
    auto centroid=[&](float& cx,float& cz) {
        double ax=0,az=0;for(int id:ground){ax+=f.world.unit(id)->x.toFloat();az+=f.world.unit(id)->z.toFloat();}
        cx=float(ax/ground.size());cz=float(az/ground.size());
    };
    for(int t=0;t<ticks;++t) {
        f.world.tick(1.f/30);
        // Ground footprints that are not legal placements (a body on a body, on a landed flyer or on a wall).
        if(o.legion) {bool bad=false;for(int id:ground)bad|=!f.legal(id);r.illegal+=bad;}
        bool groundBusy=false,flyersBusy=false;
        for(int id:ground)groundBusy|=!f.world.unit(id)->orders.empty();
        for(int id:flyers)flyersBusy|=!f.world.unit(id)->orders.empty();
        if(!groundBusy&&r.groundDone<0)r.groundDone=t;
        if(!flyersBusy&&r.flyersDone<0)r.flyersDone=t;
        r.overlap+=landedOverlap(f.world,flyers,ground);
        if(t%30==0) {
            // Keeping up: over each 30-tick window in which the ground's centroid moved at
            // least 9 px, a travelling flyer covers at least 40% of that distance.
            float cx,cz;centroid(cx,cz);
            const float marched=std::hypot(cx-windowCx,cz-windowCz);
            for(int id:flyers) {
                const auto& u=*f.world.unit(id);
                const auto it=windowStart.find(id);
                const bool travelling=!u.orders.empty()&&dist(u,u.orders.back().x.toFloat(),u.orders.back().z.toFloat())>300.f;
                if(t>=300&&groundBusy&&it!=windowStart.end()&&travelling&&marched>=9.f) {
                    const float flown=std::hypot(u.x.toFloat()-it->second.first,u.z.toFloat()-it->second.second);
                    ++r.windows;r.stalls+=flown<0.4f*marched;r.flown+=flown;r.marched+=marched;
                }
                windowStart[id]={u.x.toFloat(),u.z.toFloat()};
            }
            windowCx=cx;windowCz=cz;
        }
        for(int id:flyers) {
            const auto& u=*f.world.unit(id);
            if(!u.orders.empty()&&u.flightGroundMode==2&&u.speed>Fixed::fromFloat(0.3f)) {
                ++r.moving;
                const uint16_t way=uint16_t(retailDirection(Fixed::raw(u.flightVelocity.x),Fixed::raw(u.flightVelocity.z)).v);
                const int off=int16_t(uint16_t(way-uint16_t(u.heading.v)));
                r.misaligned+=std::abs(off)>0x1555;
            }
            hoverRun[id]=hovering(u,t)?hoverRun[id]+1:0;
            r.maxHover=std::max(r.maxHover,hoverRun[id]);
            r.holdTicks+=RetailReplayProbe::station(f.world,u)==2;
            if(u.orders.empty()&&u.flightGroundMode==2&&!idleAt.count(id))idleAt[id]=t;
            if(!u.orders.empty())idleAt.erase(id);
            if(const auto it=idleAt.find(id);it!=idleAt.end()&&(u.landing||u.flightGroundMode==1)&&it->second>=0) {
                r.maxLandDelay=std::max(r.maxLandDelay,t-it->second);it->second=-1;
            }
        }
        if(groundBusy&&t>=150) {
            float cx,cz;centroid(cx,cz);
            for(int id:flyers) {
                const auto& u=*f.world.unit(id);
                const float d=dist(u,cx,cz);
                r.maxAway=std::max(r.maxAway,d);sum+=d;++samples;
                if(t%30==0&&u.flightGroundMode==1&&u.x.toFloat()>cx+300)++r.landedAhead;
            }
        }
        if(verbose()&&t%150==0) {
            float cx,cz;centroid(cx,cz);
            std::printf("  t=%d ground %.0f,%.0f busy=%d |",t,cx,cz,int(groundBusy));
            for(int id:flyers) {const auto& u=*f.world.unit(id);
                std::printf(" %.0f,%.0f/%zu%d",u.x.toFloat(),u.z.toFloat(),u.orders.size(),int(u.flightGroundMode));}
            std::printf("\n");
        }
        if(o.kind!=tak::net::Cmd::Patrol&&r.groundDone>=0&&r.flyersDone>=0&&t>r.groundDone+600)break;
    }
    float cx,cz;centroid(cx,cz);
    for(int id:flyers)r.endAway=std::max(r.endAway,dist(*f.world.unit(id),cx,cz));
    for(int id:flyers)r.landed+=f.world.unit(id)->flightGroundMode==1;
    for(const auto& [id,at]:idleAt)if(at>=0)r.maxLandDelay=std::max(r.maxLandDelay,9999);
    r.meanAway=samples?float(sum/samples):0;r.hash=f.world.stateHash();
    const auto stats=f.world.legionStats();r.lifts=stats.lifts;r.relifts=stats.relifts;
    const char* kind=o.kind==tak::net::Cmd::Move?"move":o.kind==tak::net::Cmd::AttackMove?"fight":"patrol";
    char buf[700];
    std::snprintf(buf,sizeof buf,"%s %s %s squad=%d ground_done=%d flyers_done=%d away_max=%.0f away_mean=%.0f end_away=%.0f landed_ahead=%d "
        "pace=%.2f stalls=%d/%d misaligned=%d/%d max_hover=%d land_delay=%d landed=%d overlap=%d illegal_ticks=%d hold_ticks=%d convoy_match=%d/8 lifts=%llu relifts=%llu",
        o.seal?"mixedseal":"mixedsquad",o.legion?"legion":"retail",kind,o.squad,r.groundDone,r.flyersDone,r.maxAway,r.meanAway,r.endAway,r.landedAhead,
        r.marched>0?r.flown/r.marched:0.0,r.stalls,r.windows,r.misaligned,r.moving,r.maxHover,r.maxLandDelay,r.landed,r.overlap,r.illegal,r.holdTicks,
        r.convoyMatch,(unsigned long long)r.lifts,(unsigned long long)r.relifts);
    r.text=buf;
    return r;
}
Out asOut(const MixedRun& r) {return {r.text,r.hash};}

void mixedsquad() {
    using tak::net::Cmd;
    // Retail (reported: the 417e02 re-forming port) beside Legion, for the three squads.
    for(int squad:{-1,1})print(asOut(mixedRun({squad,false,Cmd::Move})));
    MixedRun ctrl,alt,none;
    for(int squad:{1,-1,0}) {
        auto r=mixedRun({squad,true,Cmd::Move});
        print(asOut(r));
        (squad==1?ctrl:squad==-1?alt:none)=r;
    }
    // The five start offsets of the baselined tests, Alt+1 (the stock mixedformation) and Ctrl+1: the ground-flyer
    // overlap is offset-sensitive (+1 puts a body on a landed flyer at the destination).
    int overlapOffsets=0,illegalOffsets=0;const int base=g_shift;
    for(int squad:{-1,1})for(int shift:{0,1,-1,2,-2}) {
        g_shift=shift;
        const auto r=mixedRun({squad,true,Cmd::Move});
        std::printf("mixedsquad offsets squad=%d shift=%d ground_done=%d away_max=%.0f landed_ahead=%d overlap=%d illegal_ticks=%d max_hover=%d\n",
            squad,shift,r.groundDone,r.maxAway,r.landedAhead,r.overlap,r.illegal,r.maxHover);
        overlapOffsets+=r.overlap;illegalOffsets+=r.illegal;
    }
    g_shift=base;
    // Ctrl+1 on the other order classes (reported).
    print(asOut(mixedRun({1,true,Cmd::AttackMove})));
    print(asOut(mixedRun({1,true,Cmd::Patrol})));
    if(requireW6()) {
        // PLAN 3.5 (T5) acceptance, Ctrl+1: the ground finishes, the flyers stay over it, none lands ahead.
        check(ctrl.groundDone>=0&&ctrl.groundDone<=2650,"Ctrl mixedformation: ground_done must be <= 2650");
        check(ctrl.maxAway<=300.f,"Ctrl mixedformation: away_max must be <= 300");
        check(ctrl.landedAhead==0,"Ctrl mixedformation: no flyer lands ahead of the ground");
        check(ctrl.maxHover<=60,"Ctrl mixedformation: max_hover must be <= 60");
        check(ctrl.overlap==0&&alt.overlap==0&&none.overlap==0,"a landed flyer shares a cell with a ground body");
        check(ctrl.illegal==0&&alt.illegal==0&&none.illegal==0,"a ground body stands on an illegal footprint");
        check(overlapOffsets==0&&illegalOffsets==0,"a landed flyer shares a cell with a ground body at a start offset");
    }
    checkWorkers("mixedsquad",[&](bool serial) {return asOut(mixedRun({1,true,Cmd::Move,false,serial}));},asOut(ctrl));
}
void mixedseal() {
    using tak::net::Cmd;
    const auto retail=mixedRun({-1,false,Cmd::Move,true,true,6000});
    const auto r=mixedRun({-1,true,Cmd::Move,true,true,6000});
    print(asOut(retail));print(asOut(r));
    if(requireW6()) {
        // PLAN 3.6 FL-04: MIXED_SEAL max_hover 5149 -> <= 1200, landed 0 -> 8.
        check(r.maxHover<=1200,"MIXED_SEAL: a sealed formation's flyers must not hover more than 1200 ticks");
        check(r.landed==8,"MIXED_SEAL: all 8 flyers must land");
    }
    checkWorkers("mixedseal",[&](bool serial) {return asOut(mixedRun({-1,true,Cmd::Move,true,serial,6000}));},asOut(r));
}

// ---------------------------------------------------------------------------
// mv08group (MV-08): 10 slow (1.8 px/tick) and 10 fast (3.6) 2x2 movers and 8 flyers, one Move
// 206 cells away round a wall, as Ctrl+1 (+1), untagged (0) or Alt+1 (-1); Legion and Retail.
// ---------------------------------------------------------------------------
Out mv08Run(int squad,bool legion,bool serial=true) {
    Fixture f(260,100,serial);
    f.rect(110,8,4,72);f.publish();
    if(!legion)f.world.setPathfindingMode(PathfindingMode::Retail);
    const auto slowType=mover(2),fastType=mover(2,Fixed::raw(235928));
    const auto flyer=flyerType(3);
    std::vector<int> ground,slow,fast,flyers,all;
    for(int i=0;i<20;++i) {
        const int id=f.spawn(i%2?fastType:slowType,14+(i%5)*3,40+(i/5)*4);
        ground.push_back(id);(i%2?fast:slow).push_back(id);
    }
    for(int i=0;i<8;++i)flyers.push_back(f.spawn(flyer,16+(i%4)*4,58+(i/4)*4));
    all=cat(ground,flyers);
    f.start();
    if(squad)for(int id:all)f.command(tak::net::Cmd::SetSquad,id,squad,0,0);
    f.tick(300);
    const float px=220*16,pz=50*16;
    float sx=0,sz=0;
    for(int id:all) {sx+=f.world.unit(id)->x.toFloat();sz+=f.world.unit(id)->z.toFloat();}
    sx/=float(all.size());sz/=float(all.size());
    for(int id:all) {
        const auto& u=*f.world.unit(id);
        const bool offset=u.type->canFly||!legion;
        f.command(tak::net::Cmd::Move,id,0,offset?px+std::clamp(u.x.toFloat()-sx,-60.f,60.f):px,
                  offset?pz+std::clamp(u.z.toFloat()-sz,-60.f,60.f):pz);
    }
    auto centroidOf=[&](const std::vector<int>& ids,float& cx,float& cz) {
        double ax=0,az=0;for(int id:ids){ax+=f.world.unit(id)->x.toFloat();az+=f.world.unit(id)->z.toFloat();}
        cx=float(ax/ids.size());cz=float(az/ids.size());
    };
    int firstFastIdle=-1,fastDone=-1,firstSlowIdle=-1,slowDone=-1,flyDone=-1,landedAhead=0,samples=0,overlap=0;
    float maxLead=0,awayMax=0;double awaySum=0;
    for(int t=0;t<6000;++t) {
        f.world.tick(1.f/30);
        int fastBusy=0,slowBusy=0,flyBusy=0;
        for(int id:fast)fastBusy+=!f.world.unit(id)->orders.empty();
        for(int id:slow)slowBusy+=!f.world.unit(id)->orders.empty();
        for(int id:flyers)flyBusy+=!f.world.unit(id)->orders.empty();
        if(firstFastIdle<0&&fastBusy<int(fast.size()))firstFastIdle=t;
        if(fastDone<0&&!fastBusy)fastDone=t;
        if(firstSlowIdle<0&&slowBusy<int(slow.size()))firstSlowIdle=t;
        if(slowDone<0&&!slowBusy)slowDone=t;
        if(flyDone<0&&!flyBusy)flyDone=t;
        overlap+=landedOverlap(f.world,flyers,ground);
        float gx,gz;centroidOf(ground,gx,gz);
        if(slowBusy&&t>=150) {
            float fx,fz,lx,lz;centroidOf(fast,fx,fz);centroidOf(slow,lx,lz);
            maxLead=std::max(maxLead,std::hypot(fx-lx,fz-lz));
        }
        if(fastBusy+slowBusy&&t>=150)for(int id:flyers) {
            const auto& u=*f.world.unit(id);
            const float d=dist(u,gx,gz);awayMax=std::max(awayMax,d);awaySum+=d;++samples;
            if(t%30==0&&u.flightGroundMode==1&&u.x.toFloat()>gx+300)++landedAhead;
        }
        if(slowDone>=0&&flyDone>=0&&t>slowDone+300)break;
    }
    char buf[500];
    std::snprintf(buf,sizeof buf,"mv08group %s squad=%d first_fast_idle=%d fast_done=%d first_slow_idle=%d slow_done=%d fly_done=%d "
        "max_fast_lead=%.0f fly_away_max=%.0f fly_away_mean=%.0f landed_ahead=%d overlap=%d",legion?"legion":"retail",squad,firstFastIdle,fastDone,
        firstSlowIdle,slowDone,flyDone,maxLead,awayMax,samples?awaySum/samples:0.0,landedAhead,overlap);
    return {buf,f.world.stateHash()};
}
void mv08group() {
    for(int squad:{1,0})print(mv08Run(squad,false));
    Out ctrl;
    for(int squad:{1,0,-1}) {
        const Out o=mv08Run(squad,true);
        print(o);if(squad==1)ctrl=o;
    }
    // PLAN 3.5: Ctrl+1 fly_away_max <= 300, landed_ahead 0, slow_done <= 2700. Parsed back from the text.
    auto num=[&](const Out& o,const char* key) {
        const auto p=o.text.find(std::string(key)+"=");
        return p==std::string::npos?-1:std::atoi(o.text.c_str()+p+std::strlen(key)+1);
    };
    if(requireW6()) {
        check(num(ctrl,"fly_away_max")<=300,"mv08group Ctrl+1: flyers must stay within 300 px of the ground");
        check(num(ctrl,"landed_ahead")==0,"mv08group Ctrl+1: no flyer lands ahead of the ground");
        check(num(ctrl,"slow_done")>=0&&num(ctrl,"slow_done")<=2700,"mv08group Ctrl+1: slow_done must be <= 2700");
    }
    checkWorkers("mv08group",[&](bool serial) {return mv08Run(1,true,serial);},ctrl);
}

// ---------------------------------------------------------------------------
// deadendrejoin (mv08deadend is width 6): 120 2x2 bodies as an Alt+1 formation ordered to the
// closed end of a dead-end corridor of `width` cells, run on after the formation is at rest to
// see the rest-time rejoin. reorders: orders given to a body that had finished;
// anchored_reordered: those given to a body within 3 cells of the commanded point.
// ---------------------------------------------------------------------------
Out deadendRejoinRun(int width,bool squad,bool serial=true,int count=120) {
    Fixture f(260,100,serial);
    const int zc=48+width/2;
    f.rect(100,0,140,48);f.rect(100,48+width,140,52-width);f.rect(240,48,20,width);
    f.publish();
    const auto type=mover(2);
    std::vector<int> ids;
    for(int i=0;i<count;++i)ids.push_back(f.spawn(type,14+(i%12)*3,32+(i/12)*4));
    f.start();
    if(squad) {for(int id:ids)f.command(tak::net::Cmd::SetSquad,id,-1,0,0);f.tick(600);}
    const float px=236*16,pz=float(zc*16);
    for(int id:ids)f.command(tak::net::Cmd::Move,id,0,px,pz);
    std::map<int,bool> wasIdle;int reorders=0,anchored=0,allIdle=-1,lastReorder=-1;
    std::set<int> reordered;
    constexpr int kTicks=12000;
    int t=0;
    for(;t<kTicks;++t) {
        f.world.tick(1.f/30);
        int idle=0;
        for(int id:ids) {
            const auto& u=*f.world.unit(id);
            const bool empty=u.orders.empty();
            idle+=empty;
            auto it=wasIdle.find(id);
            if(it!=wasIdle.end()&&it->second&&!empty) {
                ++reorders;lastReorder=t;reordered.insert(id);
                anchored+=dist(u,px,pz)<=48.f;
            }
            wasIdle[id]=empty;
        }
        if(idle==count&&allIdle<0)allIdle=t;
        if(allIdle>=0&&t>allIdle+900)break;
    }
    int holding=0;float nearest=1e9f;int nearestId=0;
    for(int id:ids) {
        const auto& u=*f.world.unit(id);
        holding+=!u.orders.empty();
        const float d=dist(u,px,pz);
        if(d<nearest) {nearest=d;nearestId=id;}
    }
    char buf[400];
    std::snprintf(buf,sizeof buf,"deadendrejoin width=%d alt=%d n=%d all_idle=%d ran=%d holding=%d reorders=%d reordered_units=%zu "
        "anchored_reordered=%d last_reorder=%d nearest_to_point_px=%.0f nearest_id=%d",width,int(squad),count,allIdle,t,holding,reorders,
        reordered.size(),anchored,lastReorder,nearest,nearestId);
    return {buf,f.world.stateHash()};
}
void deadendrejoin() {
    Out w6;
    for(int width:{4,6,8,12}) {
        const Out o=deadendRejoinRun(width,true);
        print(o);
        if(width==6)w6=o;
        if(requireW6()) {
            auto num=[&](const char* key) {const auto p=o.text.find(std::string(key)+"=");return std::atoi(o.text.c_str()+p+std::strlen(key)+1);};
            check(num("holding")==0,"deadendrejoin: nobody holds once the formation has settled");
            check(num("anchored_reordered")==0,"deadendrejoin: an anchored body was re-ordered");
        }
    }
    print(deadendRejoinRun(6,false));
    checkWorkers("deadendrejoin",[&](bool serial) {return deadendRejoinRun(6,true,serial);},w6);
}
void mv08deadend() {
    const Out o=deadendRejoinRun(6,true);
    print(o);
    if(requireW6()) {
        // PLAN 3.5 (T5): width 6: 120/120 settled, nobody holding, the body at the commanded point not re-ordered,
        // and the nearest body ends within 32 px of the point (head: id 60 re-ordered off it, holding 1).
        check(field(o,"holding")==0,"mv08deadend: a body still holds");
        check(field(o,"anchored_reordered")==0,"mv08deadend: the body at the commanded point was re-ordered");
        check(field(o,"nearest_to_point_px")<=32,"mv08deadend: no body ends within 32 px of the commanded point");
    }
    checkWorkers("mv08deadend",[&](bool serial) {return deadendRejoinRun(6,true,serial);},o);
}

// ---------------------------------------------------------------------------
// ctrlmixed: a Ctrl+1 group of ground and flyers given orders through issueSelection (the client's HUD
// split and 64-per-tick uplink). Per flyer: away_max from the ground of ITS OWN click, and whether
// the order it got carries the convoyTick of that click's ground (C12).
// ---------------------------------------------------------------------------
struct CtrlRun {std::string text;uint64_t hash=0;float awayMax=0;int cross=0;int convoyOwn=0,convoyAll=0;int parts=0;};
// kind 0: ctrlmixed two-click, separated; 1: two-click, second click 12 ticks later (its convoy has
// closed) 48 px from the first; 2: two-click close (48 px, +2 ticks); 3: big (200 ground + 24 flyers,
// 4 parts), flyers first in the selection; 4: big, flyers last; 5: huge (300 + 24, 5 parts), flyers
// first; 6: huge, flyers last.
CtrlRun ctrlRun(int kind,bool serial=true) {
    const bool big=kind>=3;
    const int nG=kind==3||kind==4?200:300,nF=24;
    Fixture f(big?400:300,big?200:140,serial);f.publish();
    const auto type=mover(2);const auto flyer=flyerType(3);
    struct Set {std::vector<int> ground,flyers;float px=0,pz=0;uint32_t convoy=ConvoyTable::kNone;};
    std::vector<Set> sets;
    if(!big) {
        sets.resize(2);
        for(int k=0;k<2;++k) {
            for(int i=0;i<30;++i)sets[size_t(k)].ground.push_back(f.spawn(type,14+(i%10)*3,(k?96:30)+(i/10)*3));
            for(int i=0;i<4;++i)sets[size_t(k)].flyers.push_back(f.spawn(flyer,8+(i%4)*4,k?132:14));
        }
    } else {
        sets.resize(1);
        for(int i=0;i<nG;++i)sets[0].ground.push_back(f.spawn(type,14+(i%22)*3,60+(i/22)*3));
        // Opposite corners, well over 60 px from the selection's centre on both axes.
        for(int i=0;i<nF;++i)sets[0].flyers.push_back(f.spawn(flyer,i<nF/2?4+(i%6)*4:90+(i%6)*4,i<nF/2?24+(i/6)*4:130+(i/6)*4));
    }
    f.start();
    for(const auto& s:sets)for(int id:cat(s.ground,s.flyers))f.command(tak::net::Cmd::SetSquad,id,1,0,0);
    f.tick(60);
    CtrlRun r;
    auto sel=[&](const Set& s,bool flyersFirst) {return flyersFirst?cat(s.flyers,s.ground):cat(s.ground,s.flyers);};
    if(!big) {
        sets[0].px=250*16;sets[0].pz=30*16;
        size_t parts=f.click(sel(sets[0],false),tak::scn::Verb::Move,sets[0].px,sets[0].pz);
        if(kind==1)f.tick(12);else if(kind==2)f.tick(2);else f.tick(2);
        sets[1].px=kind==0?250*16:250*16+48;sets[1].pz=kind==0?110*16:30*16+48;
        parts+=f.click(sel(sets[1],false),tak::scn::Verb::Move,sets[1].px,sets[1].pz);
        r.parts=int(parts);
    } else {
        sets[0].px=340*16;sets[0].pz=74*16;
        r.parts=int(f.click(sel(sets[0],kind==3||kind==5),tak::scn::Verb::Move,sets[0].px,sets[0].pz));
    }
    for(auto& s:sets)s.convoy=modalConvoy(f.world,s.ground);
    for(size_t k=0;k<sets.size();++k)for(int id:sets[k].flyers) {
        const uint32_t c=convoyOf(*f.world.unit(id));
        ++r.convoyAll;r.convoyOwn+=c!=ConvoyTable::kNone&&c==sets[k].convoy;
        for(size_t j=0;j<sets.size();++j)r.cross+=j!=k&&c!=ConvoyTable::kNone&&c==sets[j].convoy&&sets[j].convoy!=sets[k].convoy;
    }
    const int ticks=big?7000:5000;
    std::vector<float> away(sets.size(),0.f);int lastBusy=-1,hover=0;std::map<int,int> run;
    for(int t=0;t<ticks;++t) {
        f.world.tick(1.f/30);
        bool any=false;
        for(size_t k=0;k<sets.size();++k) {
            double ax=0,az=0;bool busy=false;
            for(int id:sets[k].ground) {const auto& u=*f.world.unit(id);ax+=u.x.toFloat();az+=u.z.toFloat();busy|=!u.orders.empty();}
            ax/=double(sets[k].ground.size());az/=double(sets[k].ground.size());
            any|=busy;
            if(busy&&t>=200)for(int id:sets[k].flyers)away[k]=std::max(away[k],dist(*f.world.unit(id),float(ax),float(az)));
        }
        for(const auto& s:sets)for(int id:s.flyers) {
            run[id]=hovering(*f.world.unit(id),t)?run[id]+1:0;hover=std::max(hover,run[id]);
        }
        if(verbose()&&t%300==0) {
            std::printf("  t=%d",t);
            for(size_t k=0;k<sets.size();++k) {
                int busy=0;double ax=0,az=0;for(int id:sets[k].ground){const auto& u=*f.world.unit(id);busy+=!u.orders.empty();ax+=u.x.toFloat();az+=u.z.toFloat();}
                std::printf(" | set%zu busy=%d ground %.0f,%.0f",k,busy,ax/sets[k].ground.size(),az/sets[k].ground.size());
            }
            std::printf("\n");
        }
        if(any)lastBusy=t;
        if(!any&&t>lastBusy+300)break;
    }
    for(float a:away)r.awayMax=std::max(r.awayMax,a);
    int landed=0;for(const auto& s:sets)for(int id:s.flyers)landed+=f.world.unit(id)->flightGroundMode==1;
    const auto stats=f.world.legionStats();
    static const char* names[]={"two-click-separated","two-click-closed-convoy","two-click-close","big-flyers-first","big-flyers-last","huge-flyers-first","huge-flyers-last"};
    char buf[460];
    std::snprintf(buf,sizeof buf,"ctrlmixed %s parts=%d distinct_convoys=%d away_max=%.0f convoy_own=%d/%d cross_pairs=%d max_hover=%d landed=%d/%d ground_last_busy=%d "
        "lifts=%llu relifts=%llu station_overflow=%llu",names[kind],r.parts,sets.size()>1&&sets[0].convoy!=sets[1].convoy,r.awayMax,r.convoyOwn,r.convoyAll,r.cross,hover,landed,
        r.convoyAll,lastBusy,(unsigned long long)stats.lifts,(unsigned long long)stats.relifts,(unsigned long long)stats.stationOverflow);
    r.text=buf;r.hash=f.world.stateHash();
    return r;
}
void ctrlmixed() {
    Out first;
    for(int kind=0;kind<=2;++kind) {
        const auto r=ctrlRun(kind);print({r.text,r.hash});
        if(kind==0)first={r.text,r.hash};
        if(requireW6()) {
            // PLAN 3.5: every flyer within 300 px of its own click's ground, each carries that click's convoyTick.
            check(r.awayMax<=300.f,"ctrlmixed: a flyer strayed from its own click's ground");
            check(r.convoyOwn==r.convoyAll,"ctrlmixed: a flyer's order does not carry its own ground's convoyTick");
            if(kind!=2)check(r.cross==0,"ctrlmixed: a flyer was paired with the other click's ground");
        }
    }
    checkWorkers("ctrlmixed",[&](bool serial) {const auto r=ctrlRun(0,serial);return Out{r.text,r.hash};},first);
}
void ctrlmixedbig() {
    Out first;
    for(int kind=3;kind<=6;++kind) {
        const auto r=ctrlRun(kind);print({r.text,r.hash});
        if(kind==3)first={r.text,r.hash};
        if(requireW6()) {
            check(r.awayMax<=300.f,"ctrlmixed-big/huge: a flyer strayed from its click's ground");
            check(r.convoyOwn==r.convoyAll,"ctrlmixed-big/huge: a flyer's order does not carry its ground's convoyTick");
        }
    }
    checkWorkers("ctrlmixedbig",[&](bool serial) {const auto r=ctrlRun(3,serial);return Out{r.text,r.hash};},first);
}

// ---------------------------------------------------------------------------
// factoryjoin (T5 2.4): a resting Alt+1 formation of 60 bodies; a squad-member factory then
// produces one more body, which joins the squad on its own. It should settle at the crowd's edge
// within 600 ticks of reaching it (within 48 px of a member) and be re-ordered at most once.
// ---------------------------------------------------------------------------
Out factoryJoinRun(bool serial=true) {
    Fixture f(220,90,serial);f.publish();
    const auto type=mover(2);
    UnitType factory{};factory.id=factory.name="legion-factory";factory.footX=6;factory.footZ=6;factory.maxHp=1000;
    factory.isBuilder=true;factory.workerTime=1000;factory.buildTime=1;
    factory.maxVel=Fixed();
    const int fid=f.spawn(factory,66,72);
    std::vector<int> crowd;
    for(int i=0;i<60;++i)crowd.push_back(f.spawn(type,70+(i%10)*3,30+(i/10)*4));
    f.start();
    for(int id:cat(crowd,{fid}))f.command(tak::net::Cmd::SetSquad,id,-1,0,0);
    f.tick(300);
    const float px=100*16,pz=45*16;
    for(int id:crowd)f.command(tak::net::Cmd::Move,id,0,px,pz);
    int rest=-1;
    for(int t=0;t<5000&&rest<0;++t) {
        f.world.tick(1.f/30);
        bool busy=false;for(int id:crowd)busy|=!f.world.unit(id)->orders.empty();
        if(!busy)rest=t;
    }
    f.tick(150);
    std::map<int,bool> idle;int crowdReorders=0;
    for(int id:crowd)idle[id]=f.world.unit(id)->orders.empty();
    f.world.setRepeat(fid,&type);
    int joiner=0,appeared=-1,settled=-1,reached=-1,joinerOrders=0,squadded=0;bool wasIdle=true;
    const std::set<int> crowdSet(crowd.begin(),crowd.end());
    for(int t=0;t<2400;++t) {
        f.world.tick(1.f/30);
        if(!joiner)for(const auto& u:f.world.units())
            if(u.alive()&&!u.underConstruction&&u.type==&type&&!crowdSet.count(u.id)&&u.id!=fid) {joiner=u.id;appeared=t;f.world.stop(fid);squadded=u.squad!=0;wasIdle=true;break;}
        for(int id:crowd) {
            const bool e=f.world.unit(id)->orders.empty();
            crowdReorders+=idle[id]&&!e;idle[id]=e;
        }
        if(joiner) {
            const auto& u=*f.world.unit(joiner);
            if(verbose()&&t%60==0)std::printf("  t=%d joiner %.0f,%.0f speed=%.2f orders=%zu\n",t,u.x.toFloat(),u.z.toFloat(),u.speed.toFloat(),u.orders.size());
            const bool e=u.orders.empty();
            if(wasIdle&&!e)++joinerOrders;
            wasIdle=e;
            if(reached<0) {
                float nearest=1e9f;for(int id:crowd)nearest=std::min(nearest,dist(*f.world.unit(id),u.x.toFloat(),u.z.toFloat()));
                if(nearest<=48.f)reached=t;
            }
            if(e&&u.speed==Fixed()&&settled<0&&t>appeared+10)settled=t;
            if(!e||u.speed!=Fixed())settled=-1;
        }
        if(joiner&&t>appeared+1500)break;
    }
    float cx=0,cz=0;for(int id:crowd){cx+=f.world.unit(id)->x.toFloat();cz+=f.world.unit(id)->z.toFloat();}
    cx/=float(crowd.size());cz/=float(crowd.size());
    char buf[460];
    std::snprintf(buf,sizeof buf,"factoryjoin crowd_rest=%d joiner=%d squadded=%d appeared=%d reached_crowd_after=%d settled_after=%d settled_after_reaching=%d joiner_order_starts=%d "
        "joiner_to_crowd_px=%.0f crowd_reorders=%d",rest,joiner,squadded,appeared,reached>=0?reached-appeared:-1,settled>=0?settled-appeared:-1,settled>=0&&reached>=0?settled-reached:-1,joinerOrders,joiner?dist(*f.world.unit(joiner),cx,cz):-1.f,crowdReorders);
    return {buf,f.world.stateHash()};
}
void factoryjoin() {
    const Out o=factoryJoinRun();print(o);
    if(requireW6()) {
        auto num=[&](const char* key) {const auto p=o.text.find(std::string(key)+"=");return std::atoi(o.text.c_str()+p+std::strlen(key)+1);};
        // PLAN 3.5: the joiner settles within 600 ticks and is re-ordered at most once.
        check(num("joiner")>0,"factoryjoin: the factory produced nothing");
        check(num("settled_after_reaching")>=0&&num("settled_after_reaching")<=600,"factoryjoin: the joiner did not settle within 600 ticks of reaching the crowd");
        check(num("joiner_order_starts")<=1,"factoryjoin: the joiner was re-ordered more than once");
    }
    checkWorkers("factoryjoin",[&](bool serial) {return factoryJoinRun(serial);},o);
}

// ---------------------------------------------------------------------------
// landunder / descentwalkin (FL-03): a flyer on its way down (stage 3 of the landing mission) over
// a free site; ground bodies then walk in under it. The flyer hovers at the site at tick 0 with its
// landing installed, as retail_motion_test's onGround does.
// ---------------------------------------------------------------------------
struct DescentRun {Out out;int overlap=0,enterTick=-1,arrive=-1,landed=0,left=0;};
// who: 0 enemy flyer, 1 own squad (flyer and column in formation -1), 2 allied idle flyer. column: bodies
// (1 = a single walker). foot: the flyer's footprint. start: the column's start cell (x); the flyer's site is cell 32.
DescentRun descentRun(int who,int column,int foot,int start,bool legion=true,bool serial=true,Fixed vel=Fixed::raw(117964)) {
    Fixture f(64,64,serial);f.publish();
    if(!legion)f.world.setPathfindingMode(PathfindingMode::Retail);
    const auto body=mover(2,vel);auto flyer=flyerType(foot);flyer.sight=0;
    // The site: tile (32,32) at 16 px per cell.
    const int fid=f.spawn(flyer,32,32,who==0?1:0);
    std::vector<int> ids;
    for(int i=0;i<column;++i)ids.push_back(f.spawn(body,start-(i%3)*3,column==1?32:29+(i/3)*3));
    f.start();
    auto& v=*f.world.unit(fid);
    v.flightGroundMode=2;v.flightY=Fixed::fromInt(180);v.standbyActive=true;v.standbyState={1,0,0xffffffffu,0,0};
    if(who==1)for(int id:cat(ids,{fid}))f.command(tak::net::Cmd::SetSquad,id,-1,0,0);
    for(int id:ids)f.command(tak::net::Cmd::Move,id,0,44*16,32*16);
    DescentRun r;
    int stage3=-1;
    for(int t=0;t<1500;++t) {
        f.world.tick(1.f/30);
        const auto& a=*f.world.unit(fid);
        if(stage3<0&&a.landing&&a.landing->mission.stage==3)stage3=t;
        const int fx=a.type->footX,fz=a.type->footZ,ax=footprintOrigin(a.x,fx),az=footprintOrigin(a.z,fz);
        bool over=false,enter=false;
        for(int id:ids) {
            const auto& u=*f.world.unit(id);
            const int ux=footprintOrigin(u.x,2),uz=footprintOrigin(u.z,2);
            const bool o=ux<ax+fx&&ax<ux+2&&uz<az+fz&&az<uz+2;
            over|=o&&a.flightGroundMode==1;
            enter|=o&&a.flightGroundMode==2&&stage3>=0;
        }
        r.overlap+=over;
        if(enter&&r.enterTick<0)r.enterTick=t;
        bool busy=false;for(int id:ids)busy|=!f.world.unit(id)->orders.empty();
        if(!busy&&r.arrive<0)r.arrive=t;
    }
    r.landed=f.world.unit(fid)->flightGroundMode==1;
    for(int id:ids)r.left+=!f.world.unit(id)->orders.empty();
    static const char* names[]={"enemy","own-squad","allied"};
    char buf[360];
    std::snprintf(buf,sizeof buf,"%s %s %s column=%d foot=%d start=%d speed=%.2f stage3_at=%d enter_during_descent=%d landed=%d landed_overlap=%d arrive=%d unfinished=%d go_arounds=%llu",
        column==1?"landunder":"descentwalkin",legion?"legion":"retail",names[who],column,foot,start,vel.toFloat(),stage3,r.enterTick,r.landed,r.overlap,r.arrive,r.left,
        (unsigned long long)f.world.legionStats().goArounds);
    r.out={buf,f.world.stateHash()};
    return r;
}
void descentwalkin() {
    // PLAN 3.6 FL-03: enemy and own-squad: landed_overlap 1460 -> 0, the column arrives <= 286.
    // Allied idle: unchanged (overlap <= 1, arrive ~210). Retail enemy: reported.
    const auto enemy=descentRun(0,9,2,27),own=descentRun(1,9,2,27),allied=descentRun(2,9,2,27);
    const auto enemy3=descentRun(0,9,3,27);
    for(const auto* r:{&enemy,&own,&allied,&enemy3})print(r->out);
    print(descentRun(0,9,2,27,false).out);
    if(requireW6()) {
        check(enemy.overlap==0&&own.overlap==0,"descentwalkin: a column ended up under a landed flyer");
        check(enemy.arrive>=0&&enemy.arrive<=286&&own.arrive>=0&&own.arrive<=286,"descentwalkin: the column did not arrive within 286 ticks");
        check(allied.overlap<=1,"descentwalkin: an allied idle flyer must keep lifting");
    }
    checkWorkers("descentwalkin",[&](bool serial) {return descentRun(0,9,2,27,true,serial).out;},enemy.out);
}
void landunder() {
    // A single body walks under a flyer in stage 3 (standing is the control: the site is refused).
    const Fixed slow=Fixed::raw(32768);   // 0.5 px/tick: still inside the footprint when the flyer touches down
    const auto fast=descentRun(0,1,2,29),walkingLate=descentRun(0,1,2,31,true,true,slow),walkingOwn=descentRun(1,1,2,30,true,true,slow),
        walking=descentRun(0,1,2,30,true,true,slow);
    for(const auto* r:{&fast,&walkingLate,&walking,&walkingOwn})print(r->out);
    if(requireW6()) {
        check(fast.overlap==0&&walking.overlap==0&&walkingOwn.overlap==0&&walkingLate.overlap==0,"landunder: a body ended up under a landed flyer");
        check(walking.arrive>=0&&walkingOwn.arrive>=0,"landunder: the walker never arrived");
    }
    checkWorkers("landunder",[&](bool serial) {return descentRun(0,1,2,30,true,serial,slow).out;},walking.out);
}

// ---------------------------------------------------------------------------
// auditlift / auditlift40 (FL-01): idle landed allied flyers inside a big group's destination area (where 0), on
// its way (1) or half way (2): how long do lifted flyers hover before landing again? Per flyer: takeoffs,
// longest hover, whether it is still up at the end.
// ---------------------------------------------------------------------------
Out auditLiftRun(int where,int count,int nfly=12,bool serial=true) {
    Fixture f(240,100,serial);f.publish();
    const auto type=mover(2);
    auto flyer=flyerType(2,false);
    std::vector<int> flyers,ids;
    const int fx=where==0?176:where==1?120:150;
    for(int i=0;i<nfly;++i)flyers.push_back(f.spawn(flyer,fx+(i%4)*4,42+(i/4)*4,0));
    for(int i=0;i<count;++i)ids.push_back(f.spawn(type,14+(i%12)*3,28+(i/12)*4));
    f.start();
    f.tick(89);
    for(int id:ids)f.world.order(id,180*16,50*16,false);
    std::vector<uint8_t> mode(flyers.size(),1);std::vector<int> up(flyers.size(),-1),takeoffs(flyers.size(),0),longest(flyers.size(),0);
    std::vector<int> hovers;
    int allIdle=-1,t=0;
    for(;t<12000;++t) {
        f.world.tick(1.f/30);
        for(size_t a=0;a<flyers.size();++a) {
            const auto& v=*f.world.unit(flyers[a]);
            if(mode[a]==1&&v.flightGroundMode==2) {++takeoffs[a];up[a]=t;}
            if(mode[a]==2&&v.flightGroundMode==1&&up[a]>=0) {hovers.push_back(t-up[a]);longest[a]=std::max(longest[a],t-up[a]);up[a]=-1;}
            mode[a]=v.flightGroundMode;
        }
        int idle=0;for(int id:ids)idle+=f.world.unit(id)->orders.empty();
        if(allIdle<0&&idle==count)allIdle=t;
        if(allIdle>=0&&t>allIdle+3000)break;
    }
    int airborne=0,totalTakeoffs=0,maxTakeoffs=0,over2=0;
    for(size_t a=0;a<flyers.size();++a) {
        if(up[a]>=0) {++airborne;hovers.push_back(t-up[a]);longest[a]=std::max(longest[a],t-up[a]);}
        totalTakeoffs+=takeoffs[a];maxTakeoffs=std::max(maxTakeoffs,takeoffs[a]);over2+=takeoffs[a]>2;
    }
    std::sort(hovers.begin(),hovers.end());
    int holding=0;for(int id:ids)holding+=!f.world.unit(id)->orders.empty();
    const auto stats=f.world.legionStats();
    std::string text="auditlift where="+std::to_string(where)+" count="+std::to_string(count)+" flyers="+std::to_string(nfly)+
        " ground_all_idle_at="+std::to_string(allIdle)+" holding_at_end="+std::to_string(holding)+" takeoffs="+std::to_string(totalTakeoffs)+
        " max_takeoffs="+std::to_string(maxTakeoffs)+" flyers_over_2_takeoffs="+std::to_string(over2)+" still_airborne_at_end="+std::to_string(airborne)+
        " max_hover="+std::to_string(hovers.empty()?0:hovers.back())+" relifts="+std::to_string(stats.relifts)+" lifts="+std::to_string(stats.lifts)+
        " cap_hits="+std::to_string(stats.capHits)+" hover_ticks:";
    for(int h:hovers)text+=" "+std::to_string(h);
    text+=" | per_flyer takeoffs/longest:";
    for(size_t a=0;a<flyers.size();++a)text+=" "+std::to_string(takeoffs[a])+"/"+std::to_string(longest[a])+(up[a]>=0?"^":"");
    return {text,f.world.stateHash()};
}
void auditlift() {
    const Out base=auditLiftRun(0,120);
    print(base);
    Out w[2];
    for(int where:{1,2}) {w[where-1]=auditLiftRun(where,120);print(w[where-1]);}
    print(auditLiftRun(0,40));
    if(requireW6()) {
        auto maxHover=[](const Out& o) {const auto p=o.text.find("max_hover=");return std::atoi(o.text.c_str()+p+10);};
        // PLAN 3.6 FL-01: auditlift where=0 and where=2 max hover <= 1800.
        check(maxHover(base)<=1800,"auditlift where=0: max hover must be <= 1800");
        check(maxHover(w[1])<=1800,"auditlift where=2: max hover must be <= 1800");
    }
    checkWorkers("auditlift",[&](bool serial) {return auditLiftRun(0,120,12,serial);},base);
}
void auditlift40() {
    const Out o=auditLiftRun(0,40);
    print(o);
    if(requireW6()) {
        auto num=[&](const char* key) {const auto p=o.text.find(std::string(key)+"=");return std::atoi(o.text.c_str()+p+std::strlen(key)+1);};
        // PLAN 3.6 FL-01: still airborne 4 -> 0, max hover <= 1800, ground all idle <= 4500, <= 2 takeoffs per flyer.
        check(num("still_airborne_at_end")==0,"auditlift40: a flyer is still airborne at the end");
        check(num("max_hover")<=1800,"auditlift40: max hover must be <= 1800");
        check(num("ground_all_idle_at")>=0&&num("ground_all_idle_at")<=4500,"auditlift40: the ground must all be idle by 4500");
        check(num("max_takeoffs")<=2,"auditlift40: a flyer took off more than twice");
    }
    checkWorkers("auditlift40",[&](bool serial) {return auditLiftRun(0,40,12,serial);},o);
}

// ---------------------------------------------------------------------------
// fl04 (FL-04): formation flyers while the ground is slow to start or never finishes.
//   corridor: AR-02 geometry (a 4-wide dead end), 120 ground + 8 flyers, 12000 ticks
//   box:      the point inside a closed 40x40 box, 120 + 8, 6000
//   big:      big440, an open 400x200 map, 440 + 24, 4500
//   door:     the slow-start door: a 3-wide door (FL04_DOOR cells) in a full wall; 120 bodies queue and go through a few
//             abreast (the centroid creeps and the front ranks advance while the tail waits); 120 + 8, 9000
//   door2:    the same with a 2-wide door: reported only -- on the step-0 head 110 of 120 bodies hold at it for ever
//             (a ground that never arrives, W5), so the flyers hover with them
// Reports the hover, the landings and the would-be release instants of the planned rules: A (no busy ground
// member sets a new best distance to its point for 450 ticks) and B (the ground centroid within 16 px of a
// reference for 900 ticks), whether the centroid was still outside the release radius at that instant, and
// whether the ground completed afterwards ("early": the release would have fired and the ground still finished).
// ---------------------------------------------------------------------------
Out fl04Run(const std::string& scen,bool serial=true) {
    const bool big=scen=="big",door=scen.rfind("door",0)==0;
    const int nGround=big?440:120,nFlyers=big?24:8;
    const int W=big?400:260,H=big?200:100;
    Fixture f(W,H,serial);
    float px,pz;
    if(scen=="corridor") {f.rect(100,0,140,48);f.rect(100,52,140,48);f.rect(240,48,20,4);px=236*16;pz=50*16;}
    else if(scen=="box") {f.rect(180,30,40,2);f.rect(180,68,40,2);f.rect(180,30,2,40);f.rect(218,30,2,40);px=200*16;pz=50*16;}
    else if(door) {const int dw=scen=="door2"?2:envInt("FL04_DOOR",3);f.rect(100,0,4,50-dw/2);f.rect(100,50+(dw+1)/2,4,50-(dw+1)/2);px=220*16;pz=50*16;}
    else {px=float(W-50)*16;pz=float(H/2)*16;}
    f.publish();
    const auto type=mover(2);const auto flyer=flyerType(3);
    std::vector<int> ground,flyers,all;
    const int cols=big?22:12;
    for(int i=0;i<nGround;++i)ground.push_back(f.spawn(type,14+(i%cols)*3,(H/2-(nGround/cols)*2)+(i/cols)*4));
    for(int i=0;i<nFlyers;++i)flyers.push_back(f.spawn(flyer,16+(i%8)*4,(H/2+(nGround/cols)*2+6)+(i/8)*4));
    all=cat(ground,flyers);
    f.start();
    for(int id:all)f.command(tak::net::Cmd::SetSquad,id,-1,0,0);
    f.tick(300);
    float sx=0,sz=0;
    for(int id:all) {sx+=f.world.unit(id)->x.toFloat();sz+=f.world.unit(id)->z.toFloat();}
    sx/=float(all.size());sz/=float(all.size());
    for(int id:all) {
        const auto& u=*f.world.unit(id);
        const bool offset=u.type->canFly;
        f.command(tak::net::Cmd::Move,id,0,offset?px+std::clamp(u.x.toFloat()-sx,-60.f,60.f):px,
                  offset?pz+std::clamp(u.z.toFloat()-sz,-60.f,60.f):pz);
    }
    auto centroid=[&](float& cx,float& cz) {
        double ax=0,az=0;for(int id:ground){ax+=f.world.unit(id)->x.toFloat();az+=f.world.unit(id)->z.toFloat();}
        cx=float(ax/ground.size());cz=float(az/ground.size());
    };
    const int ticks=scen=="corridor"?12000:big?7000:door?9000:6000;
    const int64_t area=int64_t(nGround)*4;
    const float radius=float(std::max<int64_t>(256,32*int64_t(std::sqrt(double(area)))));
    float cx0,cz0;centroid(cx0,cz0);
    std::map<int,int> hoverRun;std::map<int,float> best;
    int maxHover=0,groundStart=-1,groundDone=-1,flyersDone=-1,allStillTicks=0,holdTicks=0;
    int lastProgress=0;float refX=cx0,refZ=cz0;int refTick=0;
    int fireA=-1,fireB=-1;bool outsideA=false,outsideB=false;int maxNoProgress=0,maxStatic=0;
    for(int t=0;t<ticks;++t) {
        f.world.tick(1.f/30);
        float cx,cz;centroid(cx,cz);
        int busy=0;
        for(int id:ground) {
            const auto& u=*f.world.unit(id);
            if(u.orders.empty())continue;
            ++busy;
            const float d=dist(u,u.orders.back().x.toFloat(),u.orders.back().z.toFloat());
            auto it=best.find(id);
            if(it==best.end()||d<it->second-0.5f) {best[id]=d;lastProgress=t;}
        }
        bool fb=false;for(int id:flyers)fb|=!f.world.unit(id)->orders.empty();
        if(!busy&&groundDone<0)groundDone=t;
        if(!fb&&flyersDone<0)flyersDone=t;
        if(groundStart<0&&std::hypot(cx-cx0,cz-cz0)>=16.f)groundStart=t;
        if(std::hypot(cx-refX,cz-refZ)>16.f) {refX=cx;refZ=cz;refTick=t;}
        const bool outside=std::hypot(cx-px,cz-pz)>radius;
        if(busy&&t>=300) {
            maxNoProgress=std::max(maxNoProgress,t-lastProgress);maxStatic=std::max(maxStatic,t-refTick);
            if(fireA<0&&t-lastProgress>=450) {fireA=t;outsideA=outside;}
            if(fireB<0&&t-refTick>=900) {fireB=t;outsideB=outside;}
        }
        int still=0;
        for(int id:flyers) {
            const auto& u=*f.world.unit(id);
            const bool s=hovering(u,t);
            hoverRun[id]=s?hoverRun[id]+1:0;maxHover=std::max(maxHover,hoverRun[id]);still+=s;
            holdTicks+=RetailReplayProbe::station(f.world,u)==2;
        }
        if(still==nFlyers)++allStillTicks;
        if(verbose()&&(t%300==0||t==ticks-1)) {
            int airborne=0,landed=0;float far=0;
            for(int id:flyers) {const auto& u=*f.world.unit(id);airborne+=u.flightGroundMode==2;landed+=u.flightGroundMode==1;far=std::max(far,dist(u,px,pz));}
            std::printf("  t=%d ground busy=%d/%d centroid_to_point=%.0f | flyers airborne=%d landed=%d still=%d maxhover=%d far=%.0f\n",t,busy,nGround,
                std::hypot(cx-px,cz-pz),airborne,landed,still,maxHover,far);
        }
        if(groundDone>=0&&flyersDone>=0&&t>std::max(groundDone,flyersDone)+600)break;
    }
    int holding=0,landed=0;float farMax=0;
    for(int id:ground)holding+=!f.world.unit(id)->orders.empty();
    for(int id:flyers) {const auto& u=*f.world.unit(id);landed+=u.flightGroundMode==1;farMax=std::max(farMax,dist(u,px,pz));}
    const bool earlyA=fireA>=0&&outsideA&&groundDone>fireA,earlyB=fireB>=0&&outsideB&&groundDone>fireB;
    char buf[700];
    std::snprintf(buf,sizeof buf,"fl04 scen=%s ground=%d flyers=%d ground_start=%d ground_done=%d flyers_done=%d holding=%d max_hover=%d all_still_ticks=%d "
        "landed=%d/%d flyers_far_px=%.0f hold_ticks=%d release_radius=%.0f max_no_progress=%d max_static=%d would_release_A=%d(outside=%d,early=%d) would_release_B=%d(outside=%d,early=%d)",
        scen.c_str(),nGround,nFlyers,groundStart,groundDone,flyersDone,holding,maxHover,allStillTicks,landed,nFlyers,farMax,holdTicks,radius,
        maxNoProgress,maxStatic,fireA,int(outsideA),int(earlyA),fireB,int(outsideB),int(earlyB));
    return {buf,f.world.stateHash()};
}
void fl04() {
    const std::string only=std::getenv("FL04_SCEN")?std::getenv("FL04_SCEN"):"";
    Out corridor;
    for(const char* scen:{"corridor","box","big","door","door2"}) {
        if(!only.empty()&&only!=scen)continue;
        const Out o=fl04Run(scen);print(o);
        if(std::string(scen)=="corridor")corridor=o;
        if(requireW6()) {
            // PLAN 3.6 FL-04: corridor max hover 9630 -> <= 1200 and landed >= 7/8; box lands within the ground's stop + 1500
            // and ends <= 128 px from its point; big440 arrival hover <= 300; the door case must not release early.
            if(std::string(scen)=="corridor") {check(field(o,"max_hover")<=1200,"fl04 corridor: max hover must be <= 1200");check(field(o,"landed")>=7,"fl04 corridor: at least 7 of 8 flyers must land");}
            if(std::string(scen)=="box") {check(field(o,"landed")==8,"fl04 box: all 8 flyers must land");check(field(o,"flyers_far_px")<=128,"fl04 box: flyers must end within 128 px of the point");}
            if(std::string(scen)=="big")check(field(o,"max_hover")<=300,"fl04 big440: arrival hover must be <= 300");
        }
    }
    if(!only.empty()&&only!="corridor")return;
    checkWorkers("fl04",[&](bool serial) {return fl04Run("corridor",serial);},corridor);
}

// ---------------------------------------------------------------------------
// fl05match / fl05unreach (FL-05): a mixed Alt+1 formation of 20 ground bodies and 8 flyers. The ground gets one
// shared point; each flyer then gets its own point from a separate command (match80: 80 px from the ground's
// point, ordered 100 ticks later; match200: 200 px; match0: inside the ground's offset ring, at once). unreach:
// a full wall, so the ground walks to the nearest reachable spot and holds with its order kept.
// ---------------------------------------------------------------------------
Out fl05Run(const char* label,bool fullWall,float gx,float gz,const std::function<std::pair<float,float>(int)>& flyerPoint,int flyerOrderTick,
            int ticks,bool serial=true,bool legion=true) {
    Fixture f(260,100,serial);
    if(!legion)f.world.setPathfindingMode(PathfindingMode::Retail);
    if(fullWall)f.rect(110,0,4,100);else f.rect(110,8,4,72);
    f.publish();
    const auto type=mover(2);const auto flyer=flyerType(3);
    std::vector<int> ground,flyers,all;
    for(int i=0;i<20;++i)ground.push_back(f.spawn(type,14+(i%5)*3,40+(i/5)*4));
    for(int i=0;i<8;++i)flyers.push_back(f.spawn(flyer,16+(i%4)*4,58+(i/4)*4));
    all=cat(ground,flyers);
    f.start();
    for(int id:all)f.command(tak::net::Cmd::SetSquad,id,-1,0,0);
    f.tick(300);
    {
        float sx=0,sz=0;for(int id:all){sx+=f.world.unit(id)->x.toFloat();sz+=f.world.unit(id)->z.toFloat();}
        sx/=float(all.size());sz/=float(all.size());
        for(int id:ground) {
            const auto& u=*f.world.unit(id);
            f.command(tak::net::Cmd::Move,id,0,legion?gx:gx+std::clamp(u.x.toFloat()-sx,-60.f,60.f),legion?gz:gz+std::clamp(u.z.toFloat()-sz,-60.f,60.f));
        }
    }
    struct Fl {int id;int done=-1,hoverRun=0,maxHover=0,landedAt=-1;};
    std::vector<Fl> fl;for(int id:flyers)fl.push_back({id});
    int groundDone=-1;
    std::vector<int> hoverAfterLand(fl.size(),0);   // longest airborne-still run that begins after the flyer first landed
    for(int t=0;t<ticks;++t) {
        if(t==flyerOrderTick)for(int i=0;i<8;++i) {auto [x,z]=flyerPoint(i);f.command(tak::net::Cmd::Move,flyers[size_t(i)],0,x,z);}
        f.world.tick(1.f/30);
        bool groundBusy=false;
        for(int id:ground)groundBusy|=!f.world.unit(id)->orders.empty();
        if(!groundBusy&&groundDone<0)groundDone=t;
        for(size_t k=0;k<fl.size();++k) {
            auto& x=fl[k];const auto& u=*f.world.unit(x.id);
            if(t>flyerOrderTick) {
                if(u.orders.empty()&&x.done<0)x.done=t;
                x.hoverRun=hovering(u,t)?x.hoverRun+1:0;x.maxHover=std::max(x.maxHover,x.hoverRun);
                if(x.landedAt<0&&u.flightGroundMode==1&&x.done>=0)x.landedAt=t;
                if(x.landedAt>=0)hoverAfterLand[k]=std::max(hoverAfterLand[k],x.hoverRun);
            }
        }
        if(verbose()&&t%300==0) {
            std::printf("  [%s] t=%d busy=%d |",label,t,int(groundBusy));
            for(auto& x:fl) {const auto& u=*f.world.unit(x.id);std::printf(" %.0f,%.0f/%zu m%d v%.1f",u.x.toFloat(),u.z.toFloat(),u.orders.size(),int(u.flightGroundMode),u.speed.toFloat());}
            std::printf("\n");
        }
    }
    int dmin=INT_MAX,dmax=-1,maxHover=0,landed=0,secondHover=0,landedBy=-1;float endFar=0;
    for(size_t k=0;k<fl.size();++k) {
        const auto& x=fl[k];const auto& u=*f.world.unit(x.id);
        if(x.done>=0) {dmin=std::min(dmin,x.done);dmax=std::max(dmax,x.done);}
        maxHover=std::max(maxHover,x.maxHover);secondHover=std::max(secondHover,hoverAfterLand[k]);landed+=u.flightGroundMode==1;
        if(x.landedAt>=0)landedBy=std::max(landedBy,x.landedAt);
        auto [x0,z0]=flyerPoint(int(k));endFar=std::max(endFar,dist(u,x0,z0));
    }
    char buf[400];
    std::snprintf(buf,sizeof buf,"fl05 %s %s ground_done=%d flyers_done=%d..%d max_hover=%d hover_after_first_landing=%d landed=%d/8 landed_by=%d flyers_end_far_px=%.0f",
        label,legion?"legion":"retail",groundDone,dmin==INT_MAX?-1:dmin,dmax,maxHover,secondHover,landed,landedBy,endFar);
    return {buf,f.world.stateHash()};
}
void fl05match() {
    const float gx=220*16,gz=50*16;
    const auto p80=[&](int i) {return std::pair{gx+80.f,gz+float((i%4)*4-6)};};
    const auto p200=[&](int i) {return std::pair{gx+200.f,gz+float((i%4)*4-6)};};
    const auto p0=[&](int i) {return std::pair{gx+float(std::clamp((i%4)*64-96,-60,60)),gz+60.f};};
    const Out m80=fl05Run("match80",false,gx,gz,p80,100,6000);
    print(m80);print(fl05Run("match200",false,gx,gz,p200,100,6000));print(fl05Run("match0",false,gx,gz,p0,0,6000));
    if(requireW6()) {
        // PLAN 3.6 FL-05: match80 flyers done 2227-2337 -> 883-1046 (+-10%).
        check(field(m80,"flyers_done")>=0,"fl05match80: the flyers never finished");
        const auto p=m80.text.find("flyers_done=")+12;
        const int hi=std::atoi(m80.text.c_str()+m80.text.find("..",p)+2);
        check(hi<=int(1046*1.1),"fl05match80: the flyers must fly free of the ground's pace (done <= 1151)");
    }
    checkWorkers("fl05match",[&](bool serial) {return fl05Run("match80",false,gx,gz,p80,100,6000,serial);},m80);
}
void fl05unreach() {
    const float gx=220*16,gz=50*16;
    const auto p0=[&](int i) {return std::pair{gx+float(std::clamp((i%4)*64-96,-60,60)),gz+60.f};};
    const int ticks=envInt("W6_UNREACH_TICKS",24000);
    print(fl05Run("unreach",true,gx,gz,p0,0,ticks,true,false));
    const Out o=fl05Run("unreach",true,gx,gz,p0,0,ticks);print(o);
    if(requireW6()) {
        // PLAN 3.6 FL-05: unreach hover 8528 -> <= 1200, no second hover after the first landing, ending <= 128 px.
        check(field(o,"max_hover")<=1200,"fl05unreach: the flyers hovered more than 1200 ticks");
        check(field(o,"hover_after_first_landing")<=0,"fl05unreach: a second hover after the first landing");
        check(field(o,"flyers_end_far_px")<=128,"fl05unreach: the flyers ended more than 128 px from their points");
    }
    checkWorkers("fl05unreach",[&](bool serial) {return fl05Run("unreach",true,gx,gz,p0,0,ticks,serial);},o);
}

// ---------------------------------------------------------------------------
// splitpatrol (FL-05): an air patrol is not captured by a separate ground patrol. The ground patrols near its start
// and the flyers, as a separate command, patrol a route 150 cells away. station_ticks counts flyer-ticks on which
// a flight station was active for the flyer (target: 0), hold_ticks those on which it held the flyer's leg open.
// ---------------------------------------------------------------------------
Out splitPatrolRun(bool sameRoute,bool serial=true) {
    Fixture f(260,100,serial);f.publish();
    const auto type=mover(2);const auto flyer=flyerType(3);
    std::vector<int> ground,flyers,all;
    for(int i=0;i<20;++i)ground.push_back(f.spawn(type,14+(i%5)*3,40+(i/5)*4));
    for(int i=0;i<8;++i)flyers.push_back(f.spawn(flyer,16+(i%4)*4,58+(i/4)*4));
    all=cat(ground,flyers);
    f.start();
    for(int id:all)f.command(tak::net::Cmd::SetSquad,id,-1,0,0);
    f.tick(300);
    for(int id:ground)f.command(tak::net::Cmd::Patrol,id,0,60*16,40*16);
    for(int id:flyers)f.command(tak::net::Cmd::Patrol,id,0,sameRoute?60*16:200*16,sameRoute?40*16:80*16);
    int stationTicks=0,holdTicks=0;float awayMax=0,speedMax=0;
    for(int t=0;t<1500;++t) {
        f.world.tick(1.f/30);
        double ax=0,az=0;for(int id:ground){ax+=f.world.unit(id)->x.toFloat();az+=f.world.unit(id)->z.toFloat();}
        ax/=double(ground.size());az/=double(ground.size());
        for(int id:flyers) {
            const auto& u=*f.world.unit(id);
            const int s=RetailReplayProbe::station(f.world,u);
            stationTicks+=s>0;holdTicks+=s==2;
            awayMax=std::max(awayMax,dist(u,float(ax),float(az)));speedMax=std::max(speedMax,u.speed.toFloat());
        }
    }
    char buf[300];
    std::snprintf(buf,sizeof buf,"splitpatrol %s station_ticks=%d hold_ticks=%d away_max=%.0f flyer_speed_max=%.2f",sameRoute?"same-route":"separate-route",
        stationTicks,holdTicks,awayMax,speedMax);
    return {buf,f.world.stateHash()};
}
void splitpatrol() {
    const Out same=splitPatrolRun(true),sep=splitPatrolRun(false);
    print(same);print(sep);
    if(requireW6()) {
        check(field(sep,"station_ticks")==0,"splitpatrol: an air patrol was captured by a separate ground patrol");
    }
    checkWorkers("splitpatrol",[&](bool serial) {return splitPatrolRun(false,serial);},sep);
}

// ---------------------------------------------------------------------------
// liftcombat (FL-01): armed idle flyers are lifted by a passing allied ground group (the liftflyers layout:
// 12 landed flyers, 40 bodies walking 90 cells past). When the first flyer lifts, an enemy body starts walking
// toward them from 30 cells away. react: ticks from the enemy coming within the weapons' range to the first
// damage it takes (never: -1). mode 0: lifted flyers (the group passes); mode 1: the control, no group, the
// flyers stay landed. landed_in_range: flyer-ticks landed while the enemy was in range after a lift.
// ---------------------------------------------------------------------------
Out liftCombatRun(int mode,bool serial=true) {
    Fixture f(200,100,serial);f.publish();
    const auto type=mover(2);
    auto flyer=flyerType(2,false);
    Weapon weapon;weapon.name="legion-flyer-gun";weapon.range=160;weapon.damage=1;weapon.reload=0.5f;weapon.aimTol=32767;
    flyer.weapon=weapon;flyer.weapons.push_back(weapon);
    auto enemy=mover(2);enemy.maxHp=100000;enemy.maxVel=Fixed::raw(117964);
    std::vector<int> flyers,ids;
    for(int i=0;i<12;++i)flyers.push_back(f.spawn(flyer,70+(i%4)*3,46+(i/4)*3,0));
    if(mode==0)for(int i=0;i<40;++i)ids.push_back(f.spawn(type,20+(i%8)*3,44+(i/8)*3));
    const int foe=f.spawn(enemy,74,90,1);
    f.world.unit(foe)->fireState=0;
    f.start();
    f.tick(89);
    for(int id:flyers)check(f.world.unit(id)->flightGroundMode==1,"liftcombat: a flyer is not landed");
    for(int id:ids)f.world.order(id,160*16,50*16,false);
    const Fixed hp0=f.world.unit(foe)->hp;
    int firstLift=-1,inRange=-1,firstHit=-1,landedInRange=0,liftedAtRange=0;
    bool ordered=false;
    for(int t=0;t<2400;++t) {
        f.world.tick(1.f/30);
        bool lifted=false;for(int id:flyers)lifted|=f.world.unit(id)->flightGroundMode==2;
        if(firstLift<0&&(lifted||(mode==1&&t==240)))firstLift=t;
        if(!ordered&&firstLift>=0) {f.world.order(foe,74*16,60*16,false);ordered=true;}
        const auto& e=*f.world.unit(foe);
        float nearest=1e9f;for(int id:flyers)nearest=std::min(nearest,dist(*f.world.unit(id),e.x.toFloat(),e.z.toFloat()));
        if(inRange<0&&nearest<=160.f) {inRange=t;for(int id:flyers)liftedAtRange+=f.world.unit(id)->flightGroundMode==2;}
        if(inRange>=0&&firstHit<0&&e.hp<hp0)firstHit=t;
        if(inRange>=0&&mode==0)for(int id:flyers)landedInRange+=f.world.unit(id)->flightGroundMode==1&&firstLift>=0&&f.world.unit(id)->legionLiftRest>0;
        if(firstHit>=0&&t>firstHit+300)break;
    }
    char buf[300];
    std::snprintf(buf,sizeof buf,"liftcombat %s first_lift=%d enemy_in_range_at=%d lifted_when_in_range=%d first_hit=%d react=%d landed_flyer_ticks_after_lift_in_range=%d",
        mode==0?"lifted":"control-landed",firstLift,inRange,liftedAtRange,firstHit,inRange>=0&&firstHit>=0?firstHit-inRange:-1,landedInRange);
    return {buf,f.world.stateHash()};
}
void liftcombat() {
    const Out lifted=liftCombatRun(0),control=liftCombatRun(1);
    print(lifted);print(control);
    if(requireW6()) {
        // PLAN 3.6 FL-01: a lifted flyer acquires within 16 ticks of an enemy entering range.
        check(field(lifted,"lifted_when_in_range")>0,"liftcombat: the flyers were not lifted when the enemy came into range");
        check(field(lifted,"react")>=0&&field(lifted,"react")<=16,"liftcombat: a lifted flyer must acquire within 16 ticks");
    }
    checkWorkers("liftcombat",[&](bool serial) {return liftCombatRun(0,serial);},lifted);
}
}

int main(int argc,char** argv) {
    const std::map<std::string_view,std::function<void()>> cases{
        {"mixedsquad",mixedsquad},{"mixedseal",mixedseal},{"mv08group",mv08group},{"mv08deadend",mv08deadend},{"deadendrejoin",deadendrejoin},
        {"ctrlmixed",ctrlmixed},{"ctrlmixedbig",ctrlmixedbig},{"factoryjoin",factoryjoin},{"landunder",landunder},{"descentwalkin",descentwalkin},
        {"auditlift",auditlift},{"auditlift40",auditlift40},{"fl04",fl04},{"fl05match",fl05match},{"fl05unreach",fl05unreach},
        {"splitpatrol",splitpatrol},{"liftcombat",liftcombat}};
    try {
        if(argc<2) {for(const auto& [name,fn]:cases)fn();}
        else {
            const auto found=cases.find(argv[1]);
            if(found==cases.end())throw std::runtime_error("unknown case");
            found->second();
        }
    } catch(const std::exception& e) {std::fprintf(stderr,"legion_flyers_test: %s\n",e.what());return 1;}
    std::puts("ok");
    return 0;
}
