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
            std::puts("simperf [--mode movement|match|patrol] [--units 16000] [--ticks 3600] [--data assets/game] [--map NAME] [--crusades] [--serial]\n"
                      "movement: synthetic flat terrain, unarmed moving units; excludes scripts/AI/rendering.\n"
                      "match: generated flat map, mixed retail armies and eight Absurd AIs; includes combat/deaths.\n"
                      "patrol: same mixed retail armies, allied and patrolling; includes scripts, excludes AI/combat/rendering.\n"
                      "--map selects a retail map for match/patrol; the default remains the generated flat map.\n"
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
    if (mode!="movement" && mode!="match" && mode!="patrol") throw std::invalid_argument("invalid mode");
    if (mode!="movement" && count%8) throw std::invalid_argument("army unit count must be divisible by 8");

    if (mode=="movement" && !mapName.empty()) throw std::invalid_argument("--map requires match or patrol mode");

    // Lifetimes exceed World and its AI controllers: unit types and profiles are referenced.
    hpi::Vfs vfs;
    sim::TypeRegistry registry;
    sim::UnitType mover{};
    ai::Profile profile;
    sim::World world;
    world.setSerialThreads(serial);
    world.setVisPlayer(-1);
    std::vector<std::unique_ptr<ai::Controller>> controllers;
    const auto setupStart=std::chrono::steady_clock::now();
    if (mode=="movement") {
        world.setTerrain(std::vector<uint8_t>(1280*1024,100),1280,1024,20);
        world.setMapPlacementFeatures(std::vector<uint16_t>(1280*1024,0xffff),{});
        world.setPathService(true);
        mover.name=mover.id="probe";
        mover.maxVel=sim::Fixed::fromFloat(70.f/30);mover.turnRate=10000;
        mover.maxHp=100;mover.canMove=true;mover.footX=mover.footZ=2;
        for (int i=0;i<count;++i) {
            const int x=256+(i%200)*40,z=256+(i/200)*40;
            const int id=world.spawn(&mover,float(x),float(z),0,0);
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
        config.stressTest=true;config.unitCap=((count/8)*100+94)/95;
        for (int p=0;p<8;++p) config.slots.push_back({true,p%5,mode=="patrol" ? 0 : p,2,true,false});
        sim::setupMatch(world,registry,config);
        if (world.units().size()<size_t(count+8))
            throw std::runtime_error("map placed only "+std::to_string(world.units().size())+
                " units; expected "+std::to_string(count+8)+" including monarchs; reduce --units or choose another map");
        std::printf("map=%s terrain_cells=%dx%d\n",config.mapPath.c_str(),
                    world.nav().width(),world.nav().height());
        if (mode=="patrol") {
            for (const auto& u:world.units())
                if (u.type && !u.type->isStructure())
                    world.patrol(u.id,float(world.nav().width()*16)-u.x.toFloat(),u.z.toFloat());
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
    const auto start=std::chrono::steady_clock::now();
    for (int tick=0;tick<ticks;++tick) {
        const auto tickStart=std::chrono::steady_clock::now();
        for (auto& controller:controllers)
            controller->tick(world,uint32_t(tick),[&](const net::Command& c){sim::applyCommand(world,registry,c);});
        world.tick(1.f/30);
        durations.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tickStart).count());
        if (mode=="patrol") {
            int living=0;
            for (const auto& u:world.units()) living+=u.alive();
            minAlive=std::min(minAlive,living);
        }
        if (tick%30==29) {
            std::printf("tick=%d hash=%016llx ms=%.3f\n",tick+1,
                static_cast<unsigned long long>(world.stateHash()),durations.back());
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
    if (mode=="patrol") std::printf("minimum_alive=%d\n",minAlive);
    std::printf("alive=%d moving=%d displaced=%d allocated=%zu sim_seconds=%.3f wall_seconds=%.3f speed=%.3fx median_ms=%.3f p95_ms=%.3f max_ms=%.3f\n",
        alive,moving,displaced,world.units().size(),ticks/30.0,wall,(ticks/30.0)/wall,
        durations[durations.size()/2],durations[durations.size()*95/100],durations.back());
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr,"simperf: %s\n",e.what());return 1;
}
