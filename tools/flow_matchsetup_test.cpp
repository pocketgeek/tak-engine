#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <cstdio>
#include <stdexcept>
using namespace tak;
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
static std::vector<uint8_t> mapBytes(int width) {
    tnt::Map map;map.width=width;map.height=64;map.blocksX=width/2;map.blocksY=32;
    map.heights.assign(size_t(width)*64,100);map.features.assign(size_t(width)*64,0xffff);
    const auto blocks=size_t(map.blocksX)*map.blocksY;
    map.tileKeys.resize(blocks);map.tileCols.resize(blocks);map.tileRows.resize(blocks);
    map.minimapW=map.minimapH=map.overviewW=map.overviewH=1;
    map.minimap={0};map.overview={0};return map.save();
}
int main(){try {
    auto files=std::make_shared<hpi::Vfs::Files>();
    (*files)["maps/large.tnt"]=mapBytes(2050);(*files)["maps/small.tnt"]=mapBytes(64);
    hpi::Vfs vfs;vfs.setMapFiles(files);sim::TypeRegistry registry;
    for(int mode=0;mode<256;++mode)
        check(sim::validPathfindingMode(uint8_t(mode))==(mode<4),"pathfinder byte validation is not strict");
    check(!sim::isSharedPathfinding(sim::PathfindingMode::Retail)&&
          !sim::isSharedPathfinding(sim::PathfindingMode::RetailPlus)&&
          sim::isSharedPathfinding(sim::PathfindingMode::Flowfield)&&sim::isSharedPathfinding(sim::PathfindingMode::Cooperative),
          "shared pathfinder classification changed Retail");
    {
        sim::World world;world.setVisPlayer(-1);
        sim::MatchConfig cfg;cfg.vfs=&vfs;cfg.loadCrt=false;cfg.mapPath="maps/small.tnt";
        cfg.pathfindingMode=sim::PathfindingMode::RetailPlus;
        sim::setupMatch(world,registry,cfg);
        check(world.pathfindingMode()==sim::PathfindingMode::RetailPlus,"Retail+ lost its match configuration");
        world.tick(1.f/30);world.resetForReplay();
        cfg.pathfindingMode=sim::PathfindingMode::Retail;
        sim::setupMatch(world,registry,cfg);
        check(world.pathfindingMode()==sim::PathfindingMode::Retail,"Retail+ state survived replay reset");
    }
    for(auto mode:{sim::PathfindingMode::Flowfield,sim::PathfindingMode::Cooperative}) {
        sim::World world;world.setVisPlayer(-1);
        sim::MatchConfig cfg;cfg.vfs=&vfs;cfg.loadCrt=false;cfg.mapPath="maps/small.tnt";
        cfg.pathfindingMode=mode;
        sim::setupMatch(world,registry,cfg);world.tick(1.f/30);world.resetForReplay();
        cfg.mapPath="maps/large.tnt";cfg.pathfindingMode=sim::PathfindingMode::Retail;
        sim::setupMatch(world,registry,cfg);
        check(world.pathfindingMode()==sim::PathfindingMode::Retail&&world.nav().width()==2050,"previous shared pathfinder restricted Retail map");
        world.tick(1.f/30);world.resetForReplay();
        cfg.mapPath="maps/small.tnt";cfg.pathfindingMode=mode;
        sim::setupMatch(world,registry,cfg);
        check(world.pathfindingMode()==mode&&world.nav().width()==64,"previous Retail dimensions restricted shared pathfinder map");
        world.tick(1.f/30);world.resetForReplay();
        cfg.mapPath="maps/large.tnt";
        bool refused=false;try{sim::setupMatch(world,registry,cfg);}catch(const std::invalid_argument&){refused=true;}
        check(refused&&world.nav().width()==64,"unsupported shared pathfinder map mutated navigation before rejection");
        cfg.mapPath="maps/small.tnt";cfg.pathfindingMode=sim::PathfindingMode(4);
        refused=false;try{sim::setupMatch(world,registry,cfg);}catch(const std::invalid_argument&){refused=true;}
        check(refused&&world.nav().width()==64,"invalid pathfinder mutated navigation before rejection");
        cfg.mapPath="maps/large.tnt";
        cfg.pathfindingMode=sim::PathfindingMode::Retail;sim::setupMatch(world,registry,cfg);
        check(world.nav().width()==2050,"rejection poisoned later Retail setup");
    }
    std::puts("PASS match mode/terrain replacement, shared pathfinder admission and Retail recovery");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
