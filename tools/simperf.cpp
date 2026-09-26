// Explicit performance harness, not a CTest: the full workloads take minutes.
#include "sim/matchsetup.h"
#include "ai/ai.h"
#include "hpi/hpi.h"
#include "tnt/mapgen.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) try {
    using namespace tak;
    std::string mode="movement",data="assets/game",mapName;
    int count=16000,ticks=3600;
    bool crusades=false,serial=false;
    for (int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if (arg=="--serial") {serial=true;continue;}
        if (arg=="--crusades") {crusades=true;continue;}
        if (arg=="--help") {
            std::puts("simperf [--mode movement|match|patrol|crowd] [--units 16000] [--ticks 3600] [--data assets/game] [--map NAME] [--crusades] [--serial]\n"
                      "movement: synthetic flat terrain, unarmed moving units; excludes scripts/AI/rendering.\n"
                      "match: generated flat map, mixed retail armies and eight Absurd AIs; includes combat/deaths.\n"
                      "patrol: same mixed retail armies, allied and patrolling; includes scripts, excludes AI/combat/rendering.\n"
                      "crowd: allied mixed armies converging near their starting positions; includes scripts and destination congestion.\n"
                      "--map selects a retail map for match/patrol/crowd; the default remains the generated flat map.\n"
                      "Times include periodic lockstep hashes, exclude setup, and do not measure rendering.");
            return 0;
        }
        if (i+1>=argc) throw std::invalid_argument("missing option value");
        const std::string value=argv[++i];
        if (arg=="--mode") mode=value;
        else if (arg=="--data") data=value;
        else if (arg=="--map") mapName=value;
        else if (arg=="--units") count=std::stoi(value);
        else if (arg=="--ticks") ticks=std::stoi(value);
        else throw std::invalid_argument("unknown option: "+arg);
    }
    if (count<1 || count>16000 || ticks<1 || ticks>18000)
        throw std::invalid_argument("units must be 1..16000 and ticks 1..18000");
    if (mode!="movement" && mode!="match" && mode!="patrol" && mode!="crowd") throw std::invalid_argument("invalid mode");
    if (mode!="movement" && count%8) throw std::invalid_argument("army unit count must be divisible by 8");

    if (mode=="movement" && !mapName.empty()) throw std::invalid_argument("--map requires match, patrol or crowd mode");

    // Lifetimes exceed World and its AI controllers: unit types and profiles are referenced.
    hpi::Vfs vfs;
    sim::TypeRegistry registry;
    sim::UnitType mover{};
    ai::Profile profile;
    sim::World world;
    world.setSerialThreads(serial);
    world.setVisPlayer(-1);
    std::vector<std::unique_ptr<ai::Controller>> controllers;
    std::vector<std::pair<float,float>> crowdGoals;
    const auto setupStart=std::chrono::steady_clock::now();
    if (mode=="movement") {
        world.setTerrain(std::vector<uint8_t>(1280*1024,100),1280,1024,20);
        world.setMapPlacementFeatures(std::vector<uint16_t>(1280*1024,0xffff),{});
        world.setPathService(true);
        world.setPlayerCount(8);world.setUnitCap(2000);
        mover.name=mover.id="probe";
        mover.maxVel=sim::Fixed::fromFloat(70.f/30);mover.turnRate=10000;
        mover.maxHp=100;mover.canMove=true;mover.footX=mover.footZ=2;
        for (int i=0;i<count;++i) {
            const int x=256+(i%200)*40,z=256+(i/200)*40;
            const int id=world.spawn(&mover,float(x),float(z),0,i/2000);
            world.order(id,float(x+11000),float(z),false);
        }
    } else {
        vfs=hpi::mountRetailRoot(data);
        sim::setupRegistry(registry,vfs,crusades);
        mapgen::Params map;
        map.widthCells=map.heightCells=768;map.players=8;
        map.waterDensity=map.treeDensity=map.rockDensity=map.reliefDensity=0;
        sim::MatchConfig config;
        config.vfs=&vfs;
        config.mapPath=mapName.empty() ? mapgen::encodeMapId(map) : hpi::findMap(vfs,mapName);
        if (config.mapPath.empty()) throw std::invalid_argument("map not found: "+mapName);
        config.stressTest=count>8;config.unitCap=(((count/8)-1)*100+94)/95;
        for (int p=0;p<8;++p) config.slots.push_back({true,p%5,mode=="patrol" || mode=="crowd" ? 0 : p,2,true,false});
        const auto starts=sim::setupMatch(world,registry,config);
        if (world.units().size()!=size_t(count))
            throw std::runtime_error("map placed only "+std::to_string(world.units().size())+
                " units; expected "+std::to_string(count)+" including monarchs; reduce --units or choose another map");
        world.setUnitCap(count/8);
        std::printf("map=%s terrain_cells=%dx%d\n",config.mapPath.c_str(),
                    world.nav().width(),world.nav().height());
        if (mode=="patrol") {
            for (const auto& u:world.units())
                if (u.type && !u.type->isStructure())
                    world.patrol(u.id,float(world.nav().width()*16)-u.x.toFloat(),u.z.toFloat());
        }
        if (mode=="crowd") {
            crowdGoals=starts;
            for (auto& goal:crowdGoals) goal.second+=300;
            for (const auto& u:world.units())
                if (u.type && !u.type->isStructure())
                    world.order(u.id,crowdGoals[size_t(u.player)].first,crowdGoals[size_t(u.player)].second,false);
        }
        profile=ai::loadProfile(vfs);
        for (int p=0;p<8 && mode=="match";++p) {
            std::vector<std::pair<float,float>> enemies;
            for (const auto& u:world.units())
                if (u.player!=p && u.type && u.type->commander)
                    enemies.emplace_back(u.x.toFloat(),u.z.toFloat());
            controllers.push_back(std::make_unique<ai::Controller>(p,registry,profile,2002+p,
                ai::Difficulty::Absurd,enemies));
        }
    }
    std::printf("mode=%s requested=%d initial=%zu setup_seconds=%.3f\n",mode.c_str(),count,
        world.units().size(),std::chrono::duration<double>(std::chrono::steady_clock::now()-setupStart).count());
    std::vector<double> durations;
    durations.reserve(size_t(ticks));
    int minAlive=int(world.units().size());
    double hashMs=0;uint64_t firedTicks=0,hitEvents=0;
    std::vector<std::pair<int32_t,int32_t>> previousPositions;
    for (const auto& u:world.units()) previousPositions.emplace_back(u.x.v,u.z.v);
    const auto start=std::chrono::steady_clock::now();
    for (int tick=0;tick<ticks;++tick) {
        const auto tickStart=std::chrono::steady_clock::now();
        for (auto& controller:controllers)
            controller->tick(world,uint32_t(tick),[&](const net::Command& c){sim::applyCommand(world,registry,c);});
        world.tick(1.f/30);
        hitEvents+=world.hits().size();
        durations.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tickStart).count());
        if (tick%30==29) {
            int living=0,moving=0,firing=0,stationary=0,nearGoal=0;
            previousPositions.resize(world.units().size());
            for (size_t i=0;i<world.units().size();++i) {
                const auto& u=world.units()[i];
                living+=u.alive();moving+=u.alive() && u.speed.v>0;
                firing+=u.firedWeapons!=0;
                const std::pair<int32_t,int32_t> position{u.x.v,u.z.v};
                stationary+=u.alive() && previousPositions[i]==position;
                previousPositions[i]=position;
                if (u.alive() && !crowdGoals.empty()) {
                    const auto [gx,gz]=crowdGoals[size_t(u.player)];
                    const float dx=u.x.toFloat()-gx,dz=u.z.toFloat()-gz;
                    nearGoal+=dx*dx+dz*dz<=512.f*512.f;
                }
            }
            minAlive=std::min(minAlive,living);firedTicks+=firing;
            const auto hashStart=std::chrono::steady_clock::now();
            const auto hash=world.stateHash();
            hashMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-hashStart).count();
            std::printf("tick=%d hash=%016llx ms=%.3f alive=%d moving=%d firing=%d projectiles=%zu stationary_1s=%d near_goal_512=%d\n",tick+1,
                static_cast<unsigned long long>(hash),durations.back(),living,moving,firing,world.projectiles().size(),stationary,nearGoal);
            std::fflush(stdout);
        }
    }
    const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    int alive=0,moving=0,displaced=0;
    for (const auto& u:world.units()) {
        alive+=u.alive();moving+=u.alive() && u.speed.v>0;
        displaced+=u.alive() && (u.x!=u.homeX || u.z!=u.homeZ);
    }
    std::sort(durations.begin(),durations.end());
    std::printf("minimum_sampled_alive=%d sampled_firing=%llu hit_events=%llu hash_ms=%.3f p99_ms=%.3f\n",
        minAlive,static_cast<unsigned long long>(firedTicks),static_cast<unsigned long long>(hitEvents),hashMs,durations[durations.size()*99/100]);
    std::printf("alive=%d moving=%d displaced=%d allocated=%zu sim_seconds=%.3f wall_seconds=%.3f speed=%.3fx median_ms=%.3f p95_ms=%.3f max_ms=%.3f\n",
        alive,moving,displaced,world.units().size(),ticks/30.0,wall,(ticks/30.0)/wall,
        durations[durations.size()/2],durations[durations.size()*95/100],durations.back());
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr,"simperf: %s\n",e.what());return 1;
}
