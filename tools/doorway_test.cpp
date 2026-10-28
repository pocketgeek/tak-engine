// doorway_test -- a builder guarding or helping a factory must not hold its door.
//
// Reported (0.9.8): "when a builder is guarding a factory and/or helping build
// sometimes they stand in the doorway so nothing gets built". Retail's BUILD
// stage 2 (401deb) admits the next output only when no body stands on its pad
// and never shoves one off it (BUGGER_OFF is stored, never acted on), so a body
// parked in the yard stops production for good.
//
// Every shipped ground factory, at the middle of its buildangle range on a loaded
// map, makes four of its smallest ground unit with one of its side's builders
// (one that may help it, 0x519e60) standing in front of the door:
//  * guard: the builder guards the factory from the start (Follow_Ground 0x407f90:
//    it helps each output, and between outputs it follows the factory);
//  * assist: the builder helps the first output only, then stands where it was.
// Both pathfinding modes. Every output must finish, and once all is done no unit
// may stand in the factory's rectangle (its door rows included).
#include "sim/matchsetup.h"
#include "hpi/hpi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
namespace {
using namespace tak::sim;
struct Rect {int x,z,w,h;};
Rect footprint(const Unit& u) {
    return {footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),u.type->footX,u.type->footZ};
}
bool intersects(const Rect& a,const Rect& b) {return a.x<b.x+b.w && b.x<a.x+a.w && a.z<b.z+b.h && b.z<a.z+a.h;}
bool groundOutput(const UnitType* t) {
    return t && !t->isStructure() && !t->canFly && t->domain==UnitType::Domain::Ground;
}
enum class Mode {Guard,Assist};
bool doorCase(const TypeRegistry& registry,const UnitType& factory,const UnitType* output,const UnitType& helper,Mode mode,bool legion,bool crusades,int side) {
    auto producer=factory;producer.storage=1000000;
    World world;world.setVisPlayer(-1);
    if(legion)world.setPathfindingMode(PathfindingMode::Legion);
    world.setTerrain(std::vector<uint8_t>(256*256,100),256,256,20);
    world.setMapPlacementFeatures(std::vector<uint16_t>(256*256,0xffff),{});
    world.buildNavClasses(registry);world.setPathService(true);
    const float cx=float(112*16+factory.footX%2*8),cz=float(112*16+factory.footZ%2*8);
    const int id=world.spawn(&producer,cx,cz,float(int16_t(factory.orientation))*6.28318530718f/65536.f,0);
    world.player(0).mana=1000000;
    world.train(id,output,4);
    // The first site shows where the pad (and so the door) is.
    int firstSite=0;
    for(int t=0;t<600 && !firstSite;++t) {
        world.tick(1.f/30);
        firstSite=world.unit(id)->productionSiteId;
    }
    if(!firstSite) {std::printf("FAIL: %s never placed its first %s\n",factory.id.c_str(),output->id.c_str());return false;}
    const Unit* site=world.unit(firstSite);
    float dx=site->x.toFloat()-cx,dz=site->z.toFloat()-cz;
    const float len=std::max(1.f,std::sqrt(dx*dx+dz*dz));
    dx/=len;dz/=len;
    // In front of the door, or level with it to either side.
    const float out=8.f*float(std::max(factory.footX,factory.footZ))+64.f;
    const float bx=cx+dx*out-dz*float(side)*out*0.75f,bz=cz+dz*out+dx*float(side)*out*0.75f;
    const int builder=world.spawn(&helper,bx,bz,0.f,0);
    if(mode==Mode::Guard)world.guard(builder,id,false);
    else world.assist(builder,firstSite,false);
    const Rect yard=footprint(*world.unit(id));
    int complete=0,tick=0,helped=0;
    for(;tick<20000 && complete<4;++tick) {
        world.tick(1.f/30);complete=0;
        const Unit* b=world.unit(builder);
        if(b && b->buildSiteId)helped=std::max(helped,1);
        for(const auto& u:world.units())
            if(u.id!=id && u.id!=builder && u.alive() && u.type==output && !u.underConstruction)++complete;
    }
    for(int settle=0;settle<900;++settle)world.tick(1.f/30);
    int inYard=0;std::string who;
    for(const auto& u:world.units())
        if(u.id!=id && u.alive() && u.type && intersects(footprint(u),yard)) {
            ++inYard;who+=" "+u.type->id+(u.id==builder?"(helper)":"");
        }
    const bool pass=complete>=4 && inYard==0 && helped;
    std::printf("%s: balance=%s mode=%s %s side=%d factory=%s output=%s helper=%s completed=%d helped=%d inYard=%d%s ticks=%d hash=%016llx\n",
        pass?"PASS":"FAIL",crusades?"Crusades":"standard",legion?"Legion":"Retail",mode==Mode::Guard?"guard":"assist",side,
        factory.id.c_str(),output->id.c_str(),helper.id.c_str(),complete,helped,inYard,who.c_str(),tick,
        (unsigned long long)world.stateHash());
    return pass;
}
}
int main(int argc,char** argv) {
    if(argc<2)return 2;
    const std::string only=argc>2 ? argv[2] : "";
    auto vfs=tak::hpi::mountRetailRoot(argv[1]);int failures=0,cases=0;
    for(bool crusades:{false,true}) {
        TypeRegistry registry;setupRegistry(registry,vfs,crusades);
        for(const auto& [name,type]:registry.types()) {
            if(!type.producesUnits() || !type.productionScript)continue;
            if(!only.empty() && name!=only)continue;
            const UnitType* small=nullptr;
            for(const auto& id:registry.buildable(name)) {
                const auto* t=registry.find(id);
                if(!groundOutput(t))continue;
                if(!small || t->footX*t->footZ<small->footX*small->footZ ||
                   (t->footX*t->footZ==small->footX*small->footZ && t->buildTime<small->buildTime))small=t;
            }
            if(!small)continue;
            // A builder of the factory's side that may help its output.
            const UnitType* helper=nullptr;
            for(const auto& [bn,b]:registry.types())
                if(b.isBuilder && !b.isStructure() && !b.canFly && b.side==type.side && b.domain==UnitType::Domain::Ground &&
                   (!b.builderLimited || std::find(b.buildMenu.begin(),b.buildMenu.end(),small->id)!=b.buildMenu.end()) &&
                   (!helper || b.buildDist<helper->buildDist))helper=&b;
            // (Veruna's builders are all builderlimited without soldiers: any side's.)
            if(!helper)for(const auto& [bn,b]:registry.types())
                if(b.isBuilder && !b.isStructure() && !b.canFly && b.domain==UnitType::Domain::Ground && !b.builderLimited &&
                   (!helper || b.buildDist<helper->buildDist))helper=&b;
            if(!helper){std::printf("FAIL: %s has no helper for %s\n",name.c_str(),small->id.c_str());++failures;continue;}
            for(bool legion:{false,true})for(Mode mode:{Mode::Guard,Mode::Assist})for(int side:{-1,0,1}) {
                ++cases;if(!doorCase(registry,type,small,*helper,mode,legion,crusades,side))++failures;
            }
        }
    }
    std::printf("doorway: %d cases, %d failures\n",cases,failures);
    return failures||!cases?1:0;
}
