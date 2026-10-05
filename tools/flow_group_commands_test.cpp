// End-to-end Flowfield movement checks. These deliberately assert physical
// progress and rest, rather than treating a removed order as proof of arrival.
#include "sim/sim.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace tak::sim;
namespace {
constexpr int kCount=32;
constexpr int kGoalX=3000,kGoalZ=2048;
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
UnitType mover(bool boat=false) {
    UnitType t{};t.id=t.name=boat?"group-boat":"group-ground";
    t.maxVel=Fixed::fromInt(3);t.turnRate=10000;t.maxHp=100;
    t.canMove=true;t.footX=t.footZ=2;t.sight=4096;t.buildTime=1;
    if(boat) {t.domain=UnitType::Domain::Water;t.floater=true;t.minWaterDepth=4;t.maxWaterDepth=255;}
    return t;
}
void setup(World& w,bool serial=true,bool boat=false) {
    w.setVisPlayer(-1);w.setSerialThreads(serial);w.setPathService(true);
    w.setPathfindingMode(PathfindingMode::Flowfield);
    w.setTerrain(std::vector<uint8_t>(256*256,boat?20:100),256,256,64);
    w.setMapPlacementFeatures(std::vector<uint16_t>(256*256,0xffff),{});
}
void ticks(World& w,int count) {for(int i=0;i<count;++i)w.tick(1.f/30);}
std::vector<int> cohort(World& w,const UnitType& type) {
    std::vector<int> ids;
    for(int i=0;i<kCount;++i)ids.push_back(w.spawn(&type,float(512+i%4*48),
        float(kGoalZ-168+i/4*48),std::nullopt,0));
    return ids;
}
bool finished(const World& w,const std::vector<int>& ids) {
    return std::all_of(ids.begin(),ids.end(),[&](int id){return w.unit(id)->orders.empty();});
}
void activePositions(const World& w,const std::vector<int>& ids) {
    for(int id:ids)if(!w.unit(id)->orders.empty()) {
        const auto& u=*w.unit(id);const auto& goal=u.orders[World::currentLeg(u.orders)];
        const auto target=goal.missionTarget.value_or(std::pair{goal.x,goal.z});
        std::printf("active id=%d at=%.2f,%.2f target=%.2f,%.2f next=%.2f,%.2f speed=%.3f blocked=%d radius=%u guard=%d orders=%zu\n",
            id,u.x.toFloat(),u.z.toFloat(),target.first.toFloat(),target.second.toFloat(),
            u.orders.front().x.toFloat(),u.orders.front().z.toFloat(),u.speed.toFloat(),
            u.bodyBlockStreak,goal.missionRadius,goal.guard,u.orders.size());
    }
}
void standable(const World& w,const std::vector<int>& ids) {
    for(int id:ids) {
        const auto& u=*w.unit(id);
        check(w.mobilePlacement(u,footprintOrigin(u.x,u.type->footX),
            footprintOrigin(u.z,u.type->footZ),false),"arrival overlaps terrain or another footprint");
    }
}
void compact(const World& w,const std::vector<int>& ids,int x,int z,int radius) {
    for(int id:ids) {
        const auto& u=*w.unit(id);const int64_t dx=u.x.floorInt()-x,dz=u.z.floorInt()-z;
        if(dx*dx+dz*dz>int64_t(radius)*radius)
            std::printf("far arrival id=%d at=%d,%d target=%d,%d radius=%d orders=%zu\n",
                id,u.x.floorInt(),u.z.floorInt(),x,z,radius,u.orders.size());
        check(dx*dx+dz*dz<=int64_t(radius)*radius,"unit gave up outside the destination area");
    }
}
int destinationAreaRadiusCells(const World& w,const std::vector<int>& ids) {
    // The group shares its occupied area. Each mover supplies its own two
    // footprint strides of contact margin below, including mixed cohorts.
    uint64_t area=0;
    for(int id:ids) {
        const auto& type=*w.unit(id)->type;
        area+=uint64_t(std::clamp(type.footX,1,64)+1)*(std::clamp(type.footZ,1,64)+1);
    }
    return int(std::sqrt(double(std::min<uint64_t>(area,512*512))));
}
int destinationRadiusCells(int areaRadius,int footX,int footZ) {
    return areaRadius+2*(std::max(footX,footZ)+1);
}
bool withinDestinationCells(Fixed x,Fixed z,int footX,int footZ,int goalX,int goalZ,int radius) {
    // Traffic's mission identity divides integer destination pixels by 16;
    // its standing unit uses the parity-aware footprint anchor.
    const int64_t dx=footprintCell(x,footX)-goalX/16,dz=footprintCell(z,footZ)-goalZ/16;
    return dx*dx+dz*dz<=int64_t(radius)*radius;
}
int destinationWorldRadius(int cells,int x,int z,int footX,int footZ) {
    // footprintCell gives even footprints [16a-8,16a+8), odd [16a,16a+16).
    // compact() measures floored world pixels, so their residual intervals are
    // [-8,7] and [0,15]. Subtract the actual goal's offset from its floor cell;
    // the two maximum axis errors give a Euclidean rounding bound. The cell
    // radius remains exact: 32 2x2 bodies allow 22 cells, 2,000 allow 140.
    const auto error=[](int target,int foot) {
        const int offset=target-(target/16)*16;
        const int first=foot%2?0:-8,last=first+15;
        return std::max(std::abs(first-offset),std::abs(last-offset));
    };
    return int(std::ceil(cells*16+std::hypot(double(error(x,footX)),double(error(z,footZ)))));
}
void compactDestination(const World& w,const std::vector<int>& ids,int x,int z) {
    const int areaRadius=destinationAreaRadiusCells(w,ids);
    for(int id:ids) {
        const auto& u=*w.unit(id);
        const int cells=destinationRadiusCells(areaRadius,u.type->footX,u.type->footZ);
        const bool inside=withinDestinationCells(u.x,u.z,u.type->footX,u.type->footZ,x,z,cells);
        if(!inside)std::printf("far arrival anchor id=%d at=%d,%d target=%d,%d radius_cells=%d\n",
            id,footprintCell(u.x,u.type->footX),footprintCell(u.z,u.type->footZ),x/16,z/16,cells);
        check(inside,"unit gave up outside the destination anchor area");
        compact(w,{id},x,z,destinationWorldRadius(cells,x,z,u.type->footX,u.type->footZ));
    }
}
void arrivalBounds() {
    check(destinationRadiusCells(20,2,2)==26&&destinationRadiusCells(20,4,4)==30,
        "mixed arrival cohort borrowed another mover's footprint margin");
    // These two physical positions exceed the old world-pixel assertions but
    // satisfy the unchanged 140/22-cell limit. The adjacent outer cell fails.
    check(withinDestinationCells(Fixed::fromInt(4105),Fixed::fromInt(2900),2,2,6000,4096,140),
        "large arrival bound ignored footprint-anchor rounding");
    check(withinDestinationCells(Fixed::fromInt(2993),Fixed::fromInt(2839),2,2,3007,3192,22),
        "guard arrival bound ignored its moving target's cell offset");
    check(!withinDestinationCells(Fixed::fromInt(2992),Fixed::fromInt(2816),2,2,3007,3192,22),
        "world rounding allowance enlarged the anchor arrival radius");
    check(destinationWorldRadius(140,6000,4096,2,2)==2252&&
          destinationWorldRadius(22,3007,3192,2,2)==381&&
          destinationWorldRadius(22,3000,2048,3,3)==369,
        "destination world bound lost its exact parity and target-offset allowance");
}
void rest(World& w,const std::vector<int>& ids,int tickCount=300) {
    // These synthetic movers retain the conservative default brake rate, so a
    // fast member can take ~240 ticks to coast to rest after its order finishes.
    // Measure sustained rest after that finite deceleration.
    ticks(w,300);
    std::vector<std::pair<Fixed,Fixed>> points;
    for(int id:ids)points.emplace_back(w.unit(id)->x,w.unit(id)->z);
    for(int tick=0;tick<tickCount;++tick) {
        w.tick(1.f/30);
        for(size_t i=0;i<ids.size();++i) {
            const auto& u=*w.unit(ids[i]);
            if(std::pair{u.x,u.z}!=points[i])
                std::printf("rest drift tick=%u id=%d from=%.2f,%.2f to=%.2f,%.2f orders=%zu\n",
                    w.tickCount(),u.id,points[i].first.toFloat(),points[i].second.toFloat(),
                    u.x.toFloat(),u.z.toFloat(),u.orders.size());
            check(std::pair{u.x,u.z}==points[i],"settled unit resumed circling without a new command");
        }
    }
}
enum class Command {Move,Fight,Queued};
uint64_t group(Command command,bool serial,bool boat=false) {
    World w;setup(w,serial,boat);auto type=mover(boat);const auto ids=cohort(w,type);
    for(int id:ids) {
        if(command==Command::Fight)w.attackMove(id,kGoalX,kGoalZ,false);
        else w.order(id,kGoalX,kGoalZ,false);
        if(command==Command::Queued)w.order(id,kGoalX,3200,true);
    }
    std::array<bool,kCount> firstVisited{};bool widthMeasured=false;int width=0;
    for(int tick=0;tick<6000&&!finished(w,ids);++tick) {
        w.tick(1.f/30);int64_t sumX=0;std::vector<int> zs;
        for(size_t i=0;i<ids.size();++i) {
            const auto& u=*w.unit(ids[i]);sumX+=u.x.floorInt();zs.push_back(u.z.floorInt());
            const int64_t dx=u.x.floorInt()-kGoalX,dz=u.z.floorInt()-kGoalZ;
            firstVisited[i]=firstVisited[i]||dx*dx+dz*dz<=320*320;
        }
        // Measure the middle half of the formation before arrival. Isolated
        // stragglers must not hide a convoy that has collapsed to single file.
        if(!widthMeasured&&sumX/int(ids.size())>=1800) {
            std::sort(zs.begin(),zs.end());width=zs[3*zs.size()/4]-zs[zs.size()/4];widthMeasured=true;
        }
    }
    int done=0;for(int id:ids)done+=w.unit(id)->orders.empty();
    const auto stats=w.flowStats();
    std::printf("group command=%d boat=%d serial=%d tick=%u done=%d/%zu middle_width=%d requests=%llu deliveries=%llu bytes=%zu\n",
        int(command),boat,serial,w.tickCount(),done,ids.size(),width,
        (unsigned long long)stats.requests,(unsigned long long)stats.deliveries,stats.bytes);
    if(!finished(w,ids))activePositions(w,ids);
    check(finished(w,ids),"shared group destination never settled");
    check(widthMeasured&&width>=96,"open-ground group collapsed to a narrow single-file stream");
    if(command==Command::Queued)
        check(std::all_of(firstVisited.begin(),firstVisited.end(),[](bool v){return v;}),
            "queued group skipped its earlier destination area");
    compactDestination(w,ids,kGoalX,command==Command::Queued?3200:kGoalZ);
    standable(w,ids);rest(w,ids);
    return w.stateHash();
}
void diagonalDenseGroup() {
    // Touching 2x2 bodies are legal, but choosing cardinal equal-cost steps
    // towards a diagonal goal funnels both halves of this square into x=z.
    // Check physical progress as well as width: a stationary army retains its
    // original spread and must not pass a width-only regression.
    constexpr int count=64,goal=3500;
    World w;setup(w);auto type=mover();std::vector<int> ids;
    for(int i=0;i<count;++i) {
        const int id=w.spawn(&type,float(512+i%8*32),float(512+i/8*32),std::nullopt,0);
        ids.push_back(id);auto& unit=*w.unit(id);
        unit.heading=fxAtan2(Fixed::fromInt(goal)-unit.x,Fixed::fromInt(goal)-unit.z);
        w.order(id,goal,goal,false);
    }
    standable(w,ids);ticks(w,600);
    std::vector<int> progress,lateral;int64_t total=0;
    for(int i=0;i<count;++i) {
        const auto& unit=*w.unit(ids[i]);
        const int advance=unit.x.floorInt()-(512+i%8*32)+unit.z.floorInt()-(512+i/8*32);
        progress.push_back(advance);total+=advance;
        lateral.push_back(unit.x.floorInt()-unit.z.floorInt());
    }
    std::sort(progress.begin(),progress.end());std::sort(lateral.begin(),lateral.end());
    const int width=lateral[3*count/4]-lateral[count/4];
    std::printf("dense diagonal tick=%u mean_l1_progress=%lld lower_quartile=%d lateral_width=%d\n",
        w.tickCount(),(long long)(total/count),progress[count/4],width);
    check(total/count>=800&&progress[count/4]>=512,
        "dense diagonal group stalled while converging onto one shared diagonal");
    check(width>=96,"dense diagonal group collapsed its transverse marching width");
    standable(w,ids);
}
void patrol() {
    World w;setup(w);auto type=mover();const auto ids=cohort(w,type);
    for(int id:ids){w.patrolTo(id,kGoalX,kGoalZ,false);w.patrolTo(id,600,kGoalZ,true);}
    std::array<int,kCount> laps{};std::array<bool,kCount> outbound{};
    for(int tick=0;tick<12000;++tick) {
        w.tick(1.f/30);
        for(size_t i=0;i<ids.size();++i) {
            const auto& u=*w.unit(ids[i]);
            if(u.x.floorInt()>kGoalX-320)outbound[i]=true;
            if(outbound[i]&&u.x.floorInt()<920){++laps[i];outbound[i]=false;}
            check(!u.orders.empty(),"patrol group discarded its repeating mission");
        }
    }
    std::printf("group patrol minimum_laps=%d\n",*std::min_element(laps.begin(),laps.end()));
    check(std::all_of(laps.begin(),laps.end(),[](int n){return n>=2;}),
        "patrol group jammed at a shared endpoint instead of continuing");
    standable(w,ids);
}
void guard() {
    World w;setup(w);auto type=mover();const int anchor=w.spawn(&type,kGoalX,kGoalZ,std::nullopt,0);
    const auto ids=cohort(w,type);for(int id:ids)w.guard(id,anchor,false);
    ticks(w,6000);const auto stats=w.flowStats();
    std::printf("guard tick=%u requests=%llu deliveries=%llu bytes=%zu\n",w.tickCount(),
        (unsigned long long)stats.requests,(unsigned long long)stats.deliveries,stats.bytes);
    activePositions(w,ids);compactDestination(w,ids,kGoalX,kGoalZ);standable(w,ids);rest(w,ids);
    for(int id:ids)check(!w.unit(id)->orders.empty()&&w.unit(id)->orders.back().guard,
        "settled escort lost its persistent guard order");
    w.order(anchor,kGoalX,3200,false);ticks(w,6000);
    check(w.unit(anchor)->orders.empty(),"protected unit did not reach its new position");
    compactDestination(w,ids,w.unit(anchor)->x.floorInt(),w.unit(anchor)->z.floorInt());
    standable(w,ids);rest(w,ids);
}
void guardStructure(int footprint) {
    World w;setup(w);auto type=mover(),building=type;building.id="guard-building";
    building.maxVel={};building.footX=building.footZ=footprint;
    const int anchor=w.spawn(&building,kGoalX,kGoalZ,std::nullopt,0);
    const int escort=w.spawn(&type,512,kGoalZ,std::nullopt,0);w.guard(escort,anchor,false);
    ticks(w,6000);const std::vector<int> ids{escort};
    std::printf("guard structure footprint=%d escort=%.2f,%.2f speed=%.3f\n",
        footprint,w.unit(escort)->x.toFloat(),w.unit(escort)->z.toFloat(),w.unit(escort)->speed.toFloat());
    check(!w.unit(escort)->orders.empty()&&w.unit(escort)->orders.back().guard,
        "escort discarded its guard-building command");
    // The guard area has to clear both complete footprints. Include one cell
    // of standing-room padding and the final grid-center rounding allowance.
    const int radius=std::max(70,(footprint+type.footX)*8+16)+24;
    compact(w,ids,kGoalX,kGoalZ,radius);standable(w,ids);rest(w,ids);
}
void formation(bool guardFormation=false) {
    World w;setup(w);auto slow=mover(),fast=slow;slow.maxVel=Fixed::fromInt(1);fast.maxVel=Fixed::fromInt(4);
    slow.id="group-slow";fast.id="group-fast";
    // Start the fast member behind: a catch-up exception must not let a
    // Flowfield formation exceed its slowest member's actual speed.
    const int fastId=w.spawn(&fast,480,2000,std::nullopt,0),slowId=w.spawn(&slow,640,2100,std::nullopt,0);
    std::vector<int> ids{fastId,slowId};
    const int anchor=guardFormation?w.spawn(&fast,kGoalX,kGoalZ,std::nullopt,0):0;
    for(int id:ids) {
        w.setSquad(id,-1);
        if(guardFormation)w.guard(id,anchor,false);else w.order(id,kGoalX,kGoalZ,false);
    }
    for(int tick=0;tick<1000;++tick) {
        const Fixed oldX=w.unit(fastId)->x,oldZ=w.unit(fastId)->z;w.tick(1.f/30);
        Fixed cap=w.unit(slowId)->baseSpeed;
        if(w.unit(slowId)->groundTerrainFlags&0x800)cap=cap*slow.roadMult;
        else if(w.unit(slowId)->groundTerrainFlags&0x1000)cap=cap*slow.waterMult;
        const Fixed step=fxLen(w.unit(fastId)->x-oldX,w.unit(fastId)->z-oldZ);
        // The retained movement step uses a Q13 sine table and rounds each
        // component to Q16. Its vector norm need not equal scalar speed exactly.
        const int32_t rounding=cap.v/4096+2;
        if(w.unit(fastId)->speed>cap||step.v>cap.v+rounding)
            std::printf("formation step tick=%u distance=%.3f speed=%.3f base=%.3f slow=%.3f flags=%x\n",
                w.tickCount(),step.toFloat(),w.unit(fastId)->speed.toFloat(),
                w.unit(fastId)->baseSpeed.toFloat(),w.unit(slowId)->baseSpeed.toFloat(),
                w.unit(fastId)->groundTerrainFlags);
        check(w.unit(fastId)->speed<=cap&&step.v<=cap.v+rounding,
            "fast formation member exceeded its slowest member's speed");
        check(w.unit(fastId)->x.floorInt()-w.unit(slowId)->x.floorInt()<=64,
            "fast formation member ran away from its slow member");
    }
    if(guardFormation) {
        ticks(w,5000);
        for(int id:ids)check(!w.unit(id)->orders.empty()&&w.unit(id)->orders.back().guard,
            "mixed-speed guard formation lost its persistent command");
    } else {
        for(int tick=0;tick<8000&&!finished(w,ids);++tick)w.tick(1.f/30);
        check(finished(w,ids),"mixed-speed formation never settled");
    }
    compact(w,ids,kGoalX,kGoalZ,160);rest(w,ids);
}
void flyingFormation(PathfindingMode mode,bool formed,bool guardFormation,bool checksums=false) {
    World w;setup(w);w.setPathfindingMode(mode);auto slow=mover(),fast=slow;
    slow.canFly=fast.canFly=true;slow.cruiseAlt=fast.cruiseAlt=64;
    slow.id="air-group-slow";fast.id="air-group-fast";
    slow.maxVel=Fixed::fromInt(1);fast.maxVel=Fixed::fromInt(4);
    const int fastId=w.spawn(&fast,480,2000,std::nullopt,0),slowId=w.spawn(&slow,640,2100,std::nullopt,0);
    const int anchor=guardFormation?w.spawn(&fast,kGoalX,kGoalZ,std::nullopt,0):0;
    for(int id:{fastId,slowId}) {
        if(formed)w.setSquad(id,-1);
        if(guardFormation)w.guard(id,anchor,false);else w.order(id,kGoalX,kGoalZ,false);
    }
    Fixed maximumStep;
    const auto firstHeight=w.unit(fastId)->flightY;
    for(int tick=1;tick<=900;++tick) {
        const auto x=w.unit(fastId)->x,z=w.unit(fastId)->z;w.tick(1.f/30);
        const Fixed step=fxLen(w.unit(fastId)->x-x,w.unit(fastId)->z-z);
        maximumStep=fxMax(maximumStep,step);
        if(formed&&mode==PathfindingMode::Flowfield) {
            const Fixed cap=fxMin(w.unit(fastId)->baseSpeed,w.unit(slowId)->baseSpeed);
            // Flight altitude remains native and independent of horizontal
            // formation pacing. Check map displacement, not 3D climb speed.
            if(step.v>cap.v+2)std::printf("flying formation guard=%d tick=%d step=%.6f cap=%.6f\n",
                guardFormation,tick,step.toFloat(),cap.toFloat());
            check(step.v<=cap.v+2,"flying formation exceeded its slowest member's horizontal speed");
        }
        if(checksums&&tick%100==0)std::printf("flight mode=%d formed=%d tick=%d hash=%016llx\n",
            int(mode),formed,tick,(unsigned long long)w.stateHash());
    }
    check(w.unit(fastId)->x>Fixed::fromInt(980)&&w.unit(slowId)->x>Fixed::fromInt(1140),
        "flying formation failed to make forward progress");
    check(w.unit(fastId)->flightY>firstHeight+Fixed::fromInt(32),
        "formation pacing suppressed ordinary takeoff altitude");
    if(!formed)check(maximumStep>Fixed::fromInt(2),"ordinary flyer inherited an unrelated formation speed cap");
}
void mixedFootprints(bool formationGroup) {
    World w;setup(w);auto small=mover(),large=small;large.id="group-large";
    large.footX=large.footZ=4;large.maxVel=Fixed::fromFloat(1.5f);
    std::vector<int> ids;
    for(int i=0;i<kCount;++i) {
        const auto& type=i%4==0?large:small;
        const int id=w.spawn(&type,float(384+i%4*80),float(kGoalZ-280+i/4*80),std::nullopt,0);
        ids.push_back(id);if(formationGroup)w.setSquad(id,-1);
        w.order(id,kGoalX,kGoalZ,false);
    }
    for(int tick=0;tick<9000&&!finished(w,ids);++tick)w.tick(1.f/30);
    int done=0;for(int id:ids)done+=w.unit(id)->orders.empty();
    std::printf("mixed footprints formation=%d tick=%u done=%d/%zu\n",
        formationGroup,w.tickCount(),done,ids.size());
    if(!finished(w,ids))activePositions(w,ids);
    check(finished(w,ids),"mixed-footprint group never settled");
    compactDestination(w,ids,kGoalX,kGoalZ);standable(w,ids);rest(w,ids);
}
void production(bool mobile,bool fight,bool queued) {
    World w;setup(w);auto type=mover(),producer=mover();producer.id="group-producer";
    producer.isBuilder=true;producer.workerTime=1000;producer.footX=producer.footZ=6;
    if(!mobile)producer.maxVel={};
    const int id=w.spawn(&producer,600,1600,std::nullopt,0);w.player(0).mana=1e9;
    w.setRepeat(id,&type);
    if(fight)w.attackMove(id,kGoalX,kGoalZ,false);else w.order(id,kGoalX,kGoalZ,false);
    if(queued)w.order(id,kGoalX,3200,true);
    bool stopped=false;std::vector<int> ids;
    for(int tick=0;tick<10000;++tick) {
        w.tick(1.f/30);ids.clear();
        for(const auto& u:w.units())if(u.id!=id&&u.alive()&&!u.underConstruction)ids.push_back(u.id);
        if(!stopped&&ids.size()>=kCount){w.stop(id);stopped=true;}
        if(stopped&&finished(w,ids))break;
    }
    std::printf("group production mobile=%d fight=%d queued=%d tick=%u outputs=%zu\n",
        mobile,fight,queued,w.tickCount(),ids.size());
    if(!finished(w,ids))activePositions(w,ids);
    check(stopped&&ids.size()>=kCount,"production fixture never created its cohort");
    check(finished(w,ids),"produced units kept searching their shared rally destination");
    compactDestination(w,ids,kGoalX,queued?3200:kGoalZ);standable(w,ids);rest(w,ids);
}
void productionPatrol(bool mobile) {
    World w;setup(w);auto type=mover(),producer=mover();producer.id="patrol-producer";
    producer.isBuilder=true;producer.workerTime=1000;producer.footX=producer.footZ=6;
    if(!mobile)producer.maxVel={};
    const int id=w.spawn(&producer,600,kGoalZ,std::nullopt,0);w.player(0).mana=1e9;
    w.setRepeat(id,&type);w.patrol(id,kGoalX,kGoalZ);
    bool stopped=false;std::vector<int> ids;std::vector<bool> outbound(64);
    std::vector<int> laps(64);
    for(int tick=0;tick<12000;++tick) {
        w.tick(1.f/30);ids.clear();
        for(const auto& u:w.units())if(u.id!=id&&u.alive()&&!u.underConstruction) {
            ids.push_back(u.id);check(size_t(u.id)<laps.size(),"patrol producer exceeded its cohort bound");
            if(u.x.floorInt()>kGoalX-320)outbound[u.id]=true;
            if(outbound[u.id]&&u.x.floorInt()<1100){++laps[u.id];outbound[u.id]=false;}
            check(!u.orders.empty(),"produced patrol discarded its repeating mission");
        }
        if(!stopped&&ids.size()>=kCount){w.stop(id);stopped=true;}
    }
    check(stopped&&ids.size()>=kCount,"patrol factory never produced its cohort");
    int minimum=100;for(int child:ids)minimum=std::min(minimum,laps[child]);
    std::printf("production patrol mobile=%d outputs=%zu minimum_laps=%d\n",mobile,ids.size(),minimum);
    check(minimum>=2,"produced patrol group jammed at its shared rally endpoints");standable(w,ids);
}
void nearbyProduction(bool mobile) {
    World w;setup(w);auto type=mover(),producer=mover();producer.id="nearby-producer";
    producer.isBuilder=true;producer.workerTime=1000;producer.footX=producer.footZ=6;
    if(!mobile)producer.maxVel={};
    const int parent=w.spawn(&producer,1600,1600,std::nullopt,0);w.player(0).mana=1e9;
    w.setRepeat(parent,&type);Fixed outputX,outputY,outputZ;
    check(w.productionPosition(parent,&type,outputX,outputY,outputZ),"nearby production fixture has no output position");
    const int rallyX=outputX.floorInt()+96,rallyZ=outputZ.floorInt()+96;
    w.order(parent,float(rallyX),float(rallyZ),false);
    std::array<std::optional<std::pair<Fixed,Fixed>>,64> births{};
    std::array<bool,64> cleared{};bool stopped=false;std::vector<int> ids;
    for(int tick=0;tick<12000;++tick) {
        w.tick(1.f/30);ids.clear();
        for(const auto& u:w.units())if(u.id!=parent&&u.alive()&&!u.underConstruction) {
            check(size_t(u.id)<births.size(),"nearby producer exceeded its observation cohort");ids.push_back(u.id);
            if(!births[u.id]) {
                for(const auto& order:u.orders)if(order.productionExit){births[u.id]=order.productionExit;break;}
                check(births[u.id].has_value(),"new output lost its birthplace before clearing its footprint");
            }
            const auto origin=*births[u.id];
            cleared[u.id]=cleared[u.id]||std::abs(int64_t(u.x.v)-origin.first.v)>=int64_t(type.footX)*16*65536||
                std::abs(int64_t(u.z.v)-origin.second.v)>=int64_t(type.footZ)*16*65536;
            check(!u.orders.empty()||cleared[u.id],"nearby rally let a new output park on its original footprint");
        }
        if(!stopped&&ids.size()>=kCount){w.stop(parent);stopped=true;}
        if(stopped&&finished(w,ids))break;
    }
    std::printf("nearby production mobile=%d tick=%u outputs=%zu\n",mobile,w.tickCount(),ids.size());
    if(!finished(w,ids))activePositions(w,ids);
    check(stopped&&ids.size()>=kCount,"nearby rally occupied the production point and stopped its queue");
    check(finished(w,ids),"nearby rally left its production cohort circling");
    compactDestination(w,ids,rallyX,rallyZ);standable(w,ids);rest(w,ids);
}
void retailChecksums() {
    // Compare this output using the pre-change and rebuilt simulation libraries.
    // This is deliberately separate from the Flowfield behavioral assertions:
    // Retail must keep its existing trajectory, even where Flowfield improves it.
    for(bool boat:{false,true}) {
        World w;setup(w,true,boat);w.setPathfindingMode(PathfindingMode::Retail);
        auto slow=mover(boat),fast=slow;slow.maxVel=Fixed::fromInt(1);
        std::vector<int> ids;
        for(int i=0;i<16;++i) {
            const int id=w.spawn(i%3?&fast:&slow,float(512+i%4*64),
                float(1700+i/4*64),std::nullopt,0);
            ids.push_back(id);if(i<4)w.setSquad(id,-1);
            if(i%3==0)w.order(id,kGoalX,kGoalZ,false);
            else if(i%3==1)w.attackMove(id,kGoalX,kGoalZ,false);
            else w.patrol(id,kGoalX,kGoalZ);
        }
        for(int tick=1;tick<=3600;++tick) {
            if(tick==901)for(int id:ids)w.order(id,600,3000,true);
            if(tick==1801){w.stop(ids[0]);w.guard(ids[1],ids[0],false);}
            if(tick==2401)w.order(ids[0],2400,2800,false);
            w.tick(1.f/30);
            if(tick%300==0)std::printf("retail boat=%d tick=%d hash=%016llx\n",
                boat,tick,(unsigned long long)w.stateHash());
        }
    }
}
void large() {
    constexpr int count=2000,limit=24000;
    World w;setup(w);w.setTerrain(std::vector<uint8_t>(512*512,100),512,512,64);
    w.setMapPlacementFeatures(std::vector<uint16_t>(512*512,0xffff),{});
    auto type=mover();type.sight=128;std::vector<int> ids;ids.reserve(count);
    for(int i=0;i<count;++i) {
        const int id=w.spawn(&type,float(512+i%45*48),float(3072+i/45*48),std::nullopt,0);
        ids.push_back(id);w.order(id,6000,4096,false);
    }
    w.updateNavigationExploration();
    auto& known=const_cast<std::vector<uint16_t>&>(w.navigationExploration());
    std::fill(known.begin(),known.end(),0xffff);
    std::vector<double> timings;timings.reserve(limit);double sum=0;size_t peak=0;
    for(int tick=0;tick<limit&&!finished(w,ids);++tick) {
        const auto before=std::chrono::steady_clock::now();w.tick(1.f/30);
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
        timings.push_back(ms);sum+=ms;peak=std::max(peak,w.flowStats().bytes);
        if((tick+1)%3000==0) {
            int done=0;for(int id:ids)done+=w.unit(id)->orders.empty();
            std::printf("large progress tick=%d done=%d/%d\n",tick+1,done,count);
        }
    }
    std::sort(timings.begin(),timings.end());int done=0;for(int id:ids)done+=w.unit(id)->orders.empty();
    const auto stats=w.flowStats();
    std::printf("large group tick=%u done=%d/%d mean_ms=%.3f p95_ms=%.3f max_ms=%.3f peak_flow_bytes=%zu requests=%llu deliveries=%llu\n",
        w.tickCount(),done,count,sum/timings.size(),timings[(timings.size()-1)*95/100],timings.back(),peak,
        (unsigned long long)stats.requests,(unsigned long long)stats.deliveries);
    for(bool idle:{false,true}) {
        int members=0,east=0,minX=8192,maxX=0,minZ=8192,maxZ=0;int64_t sumX=0,sumZ=0;
        std::vector<int> distances;
        for(int id:ids) {
            const auto& u=*w.unit(id);if(u.orders.empty()!=idle)continue;
            ++members;const int x=u.x.floorInt(),z=u.z.floorInt();sumX+=x;sumZ+=z;east+=x>=6000;
            minX=std::min(minX,x);maxX=std::max(maxX,x);minZ=std::min(minZ,z);maxZ=std::max(maxZ,z);
            distances.push_back(fxLen(u.x-Fixed::fromInt(6000),u.z-Fixed::fromInt(4096)).floorInt());
        }
        if(!members)continue;
        std::sort(distances.begin(),distances.end());
        std::printf("large area idle=%d count=%d east_of_goal=%d center=%lld,%lld bounds=%d..%d,%d..%d radius_p50=%d radius_max=%d\n",
            idle,members,east,(long long)(sumX/members),(long long)(sumZ/members),minX,maxX,minZ,maxZ,
            distances[distances.size()/2],distances.back());
    }
    if(done!=count)activePositions(w,ids);
    check(done==count,"large group left units circling its occupied destination");
    compactDestination(w,ids,6000,4096);standable(w,ids);rest(w,ids);
}
}
int main(int argc,char** argv) {
    const std::string selected=argc==2?argv[1]:"all";
    if(argc>2)return 2;
    if(selected=="--retail-checksums"){retailChecksums();return 0;}
    if(selected=="--flight-checksums") {
        flyingFormation(PathfindingMode::Retail,false,false,true);
        flyingFormation(PathfindingMode::Retail,true,false,true);
        flyingFormation(PathfindingMode::Flowfield,false,false,true);return 0;
    }
    if(selected=="large") {
        try {large();std::puts("PASS large");return 0;}
        catch(const std::exception& e){std::printf("FAIL large: %s\n",e.what());return 1;}
    }
    int failed=0,ran=0;
    const auto run=[&](const char* name,const std::function<void()>& test) {
        if(selected!="all"&&selected!=name)return;
        ++ran;try {test();std::printf("PASS %s\n",name);}
        catch(const std::exception& e){++failed;std::printf("FAIL %s: %s\n",name,e.what());}
    };
    run("arrival-bounds",arrivalBounds);
    run("move",[]{group(Command::Move,true);});
    run("fight",[]{group(Command::Fight,true);});
    run("queued",[]{group(Command::Queued,true);});
    run("boats",[]{group(Command::Move,true,true);});
    run("diagonal-dense",diagonalDenseGroup);
    run("patrol",patrol);run("guard",[]{guard();guardStructure(6);guardStructure(12);});
    run("formation",[]{formation();formation(true);});
    run("flying-formation",[]{
        flyingFormation(PathfindingMode::Flowfield,true,false);
        flyingFormation(PathfindingMode::Flowfield,true,true);
        flyingFormation(PathfindingMode::Flowfield,false,false);
    });
    run("mixed",[]{mixedFootprints(false);mixedFootprints(true);});
    run("production",[]{
        for(bool mobile:{false,true})for(bool fight:{false,true})production(mobile,fight,false);
        production(true,false,true);
    });
    run("production-patrol",[]{productionPatrol(false);productionPatrol(true);});
    run("nearby-production",[]{nearbyProduction(false);nearbyProduction(true);});
    run("determinism",[]{check(group(Command::Queued,true)==group(Command::Queued,false),
        "group command simulation differs by worker mode");});
    std::printf("flow group commands: %d cases, %d failures\n",ran,failed);
    return failed?1:ran?0:2;
}
