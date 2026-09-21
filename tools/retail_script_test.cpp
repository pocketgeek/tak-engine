#include "cob/retailvm.h"
#include "cob/retailpieces.h"
#include "cob/retailstate.h"
#include "sim/retailrng.h"
#include "sim/retailpiecepose.h"
#include "sim/footprint.h"
#include "tdo/tdo.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <fstream>

namespace tak::sim {
struct RetailReplayProbe {
    static void cacheYard(World& world) {
        World::SearchGradePlane plane;plane.nav=&world.nav_;
        plane.footX=plane.footZ=1;plane.navVersion=plane.nav->version();
        plane.cells.assign(32*32,6);world.searchGrades_.push_back(std::move(plane));
        world.refreshSearchRect(world.searchGrades_.back(),0,0,32,32);
    }
    static int cachedYard(const World& world,int x,int z) {
        return world.searchGrades_.back().cells[size_t(z)*32+x]&7;
    }
    static bool tickYard(World& world,int id) {
        world.rebuildOccupancy();world.tickUnitScript(*world.unit(id));
        return world.unitScripts_.at(id).yardOpen;
    }
    static bool automaticYard(World& world,int id) {
        world.rebuildOccupancy();world.tickAutomaticGates();world.tickUnitScript(*world.unit(id));
        return world.unitScripts_.at(id).yardOpen;
    }
    static bool yardOpen(const World& world,int id) { return world.unitScripts_.at(id).yardOpen; }
    static uint64_t completedSearches(const World& world) { return world.paths_.completions(); }
    static void gateSeed(World& world) { world.gameRng_=1; }
    static void gateState(const World& world,int id) {
        const auto& unit=*world.unit(id);const auto& script=world.unitScripts_.at(id);
        const auto& state=script.state;
        std::cout<<unit.active<<' '<<script.yardOpen<<' '<<state.vm.active<<' '<<world.gameRng_;
        for (auto v:state.vm.statics) std::cout<<' '<<v;
        for (const auto& thread:state.vm.threads) for (auto v:thread.words) std::cout<<' '<<v;
        for (const auto& piece:state.pieces) {
            std::cout<<' '<<piece.active;
            for (auto* values:{&piece.moveTarget,&piece.moveSpeed,&piece.turnTarget,&piece.turnSpeed,
                              &piece.spinTarget,&piece.spinAcceleration,&piece.move,&piece.turn})
                for (auto value:*values) std::cout<<' '<<uint32_t(value);
        }
        const auto bodies=world.searchBodyRect(footprintOrigin(unit.x,unit.type->footX),
            footprintOrigin(unit.z,unit.type->footZ),unit.type->footX,unit.type->footZ);
        for (const auto* body:bodies.cells) std::cout<<' '<<(body ? body->id : 0);
        std::cout<<'\n';
    }
    static bool yard(World& world,int id,const char* request) {
        world.rebuildOccupancy();
        world.notifyUnitScript(*world.unit(id),request);
        return world.unitScripts_.at(id).yardOpen;
    }
};
}

struct Host {
    uint32_t seed=1;
    std::vector<int32_t> events;
    bool waiting(bool,int,int) { return false; }
    uint32_t random(int32_t bound) { return tak::sim::retailRandom(seed,bound); }
    uint32_t get(int id,const std::array<uint32_t,4>&) { return id==18 ? 1 : 100; }
    void set(int id,int value) { events.insert(events.end(),{id,value}); }
    void piece(uint32_t,int,int,int32_t,int32_t) {}
    void effect(uint32_t,int,int32_t) {}
    uint32_t sound(int,int32_t priority) { return uint32_t(priority); }
};

int main(int argc,char** argv) {
    if (argc==5 && std::string(argv[1])=="--gate-timeline") {
        using namespace tak::sim;
        auto vfs=tak::hpi::mountRetailRoot(argv[2],tak::hpi::OverridePolicy::None);
        TypeRegistry registry;setupRegistry(registry,vfs,std::stoi(argv[4])!=0);
        const auto* type=registry.find(argv[3]);
        if (!type || !type->gate || !type->script()) return 2;
        World world;world.setVisPlayer(-1);
        world.setTerrain(std::vector<uint8_t>(64*64,0),64,64,0);
        world.setMapPlacementFeatures(std::vector<uint16_t>(64*64,0xffff),{});
        const int id=world.spawn(type,512,512);
        RetailReplayProbe::gateSeed(world);
        int active;
        while (std::cin>>active) {
            if (active< -1 || active>1) return 2;
            if (active>=0) world.setActive(id,active!=0);
            RetailReplayProbe::tickYard(world,id);
            RetailReplayProbe::gateState(world,id);
        }
        return 0;
    }
    if (argc==3 && std::string(argv[1])=="--gates") {
        using namespace tak::sim;
        auto vfs=tak::hpi::mountRetailRoot(argv[2],tak::hpi::OverridePolicy::None);
        unsigned count=0;
        for (bool crusades:{false,true}) {
            TypeRegistry registry;setupRegistry(registry,vfs,crusades);
            for (const auto& [name,type]:registry.types()) {
                if (!type.gate) continue;
                if (!type.script() || !type.onOffable) {
                    std::cerr<<name<<": gate activation inputs absent\n";return 1;
                }
                World world;world.setVisPlayer(-1);
                world.setTerrain(std::vector<uint8_t>(64*64,0),64,64,0);
                world.setMapPlacementFeatures(std::vector<uint16_t>(64*64,0xffff),{});
                const int id=world.spawn(&type,512,512);
                for (int tick=0;tick<1000;++tick) RetailReplayProbe::tickYard(world,id);
                for (bool open:{false,true,false,true,false}) {
                    world.setActive(id,open);
                    bool actual=false;
                    for (int tick=0;tick<1000;++tick) actual=RetailReplayProbe::tickYard(world,id);
                    if (actual!=open) {
                        std::cerr<<name<<": balance="<<crusades<<" requested="<<open<<" yard="<<actual<<'\n';return 1;
                    }
                }
                world.setActive(id,true);
                for (int tick=0;tick<1000;++tick) RetailReplayProbe::tickYard(world,id);
                const size_t passage=type.yardMap.find_first_of("cC");
                if (passage==std::string::npos) return 1;
                const int px=footprintOrigin(world.unit(id)->x,type.footX)+int(passage%type.footX);
                const int pz=footprintOrigin(world.unit(id)->z,type.footZ)+int(passage/type.footX);
                UnitType body;body.footX=body.footZ=1;
                const int occupant=world.spawn(&body,float(px*16+8),float(pz*16+8));
                RetailReplayProbe::tickYard(world,id);
                world.setActive(id,false);
                bool opened=false;
                for (int tick=0;tick<1000;++tick) opened=RetailReplayProbe::tickYard(world,id);
                if (!opened) { std::cerr<<name<<": closed on an occupied passage\n";return 1; }
                world.unit(occupant)->x=world.unit(occupant)->z=Fixed::fromInt(960);
                for (int tick=0;tick<1000;++tick) opened=RetailReplayProbe::tickYard(world,id);
                if (opened) { std::cerr<<name<<": did not close after passage cleared\n";return 1; }
                world.player(0).automaticGates=true;
                const int outsideZ=footprintOrigin(world.unit(id)->z,type.footZ)-1;
                world.unit(occupant)->x=Fixed::fromInt(px*16+8);
                world.unit(occupant)->z=Fixed::fromInt(outsideZ*16+8);
                world.unit(occupant)->speed=Fixed::fromInt(1);
                for (int tick=0;tick<1000;++tick) opened=RetailReplayProbe::automaticYard(world,id);
                if (!opened || !world.unit(id)->active) { std::cerr<<name<<": approach did not open gate\n";return 1; }
                world.unit(occupant)->z=Fixed::fromInt(pz*16+8);
                world.unit(occupant)->speed=Fixed();
                for (int tick=0;tick<1000;++tick) opened=RetailReplayProbe::automaticYard(world,id);
                if (!opened || !world.unit(id)->active) { std::cerr<<name<<": stopped occupant did not hold gate\n";return 1; }
                world.unit(occupant)->z=Fixed::fromInt(outsideZ*16+8);
                for (int tick=0;tick<1000;++tick) opened=RetailReplayProbe::automaticYard(world,id);
                if (opened || world.unit(id)->active) { std::cerr<<name<<": cleared gate did not close automatically\n";return 1; }
                const int originZ=footprintOrigin(world.unit(id)->z,type.footZ);
                const int originX=footprintOrigin(world.unit(id)->x,type.footX);
                std::vector<uint16_t> corridor(64*64,0xffff);
                for (int x=0;x<64;++x)
                    if (x<originX || x>=originX+type.footX) corridor[size_t(pz)*64+x]=0xfffc;
                world.setMapPlacementFeatures(corridor,{});
                world.blockCells(0,pz,originX,1,true);
                world.blockCells(originX+type.footX,pz,64-originX-type.footX,1,true);
                world.unit(occupant)->z=Fixed::fromInt((originZ-6)*16+8);
                world.setPathService(true);world.setPathBudget(503);
                for (bool returning:{false,true}) {
                    const auto completions=RetailReplayProbe::completedSearches(world);
                    const int targetX=px*16+8;
                    const int targetZ=(returning ? originZ-6 : originZ+type.footZ+6)*16+8;
                    world.order(occupant,float(targetX),float(targetZ),false);
                    bool crossed=false;
                    for (int tick=0;tick<2400;++tick) {
                        world.tick(1.0f/30.0f);
                        const auto& moving=*world.unit(occupant);
                        const int cx=footprintOrigin(moving.x,1),cz=footprintOrigin(moving.z,1);
                        const int gx=footprintOrigin(world.unit(id)->x,type.footX);
                        if (cx>=gx && cx<gx+type.footX && cz>=originZ && cz<originZ+type.footZ) {
                            const char cell=type.yardMap[size_t(cz-originZ)*type.footX+cx-gx];
                            if (cell=='c' || cell=='C') {
                                if (RetailReplayProbe::completedSearches(world)==completions) {
                                    std::cerr<<name<<": crossed without a completed route search\n";return 1;
                                }
                                crossed=true;
                                if (!RetailReplayProbe::yardOpen(world,id)) {
                                    std::cerr<<name<<": mover entered a closed passage\n";return 1;
                                }
                            }
                        }
                    }
                    const auto& arrived=*world.unit(occupant);
                    if (!crossed || std::abs(arrived.x.floorInt()-targetX)>16 ||
                        std::abs(arrived.z.floorInt()-targetZ)>16 || RetailReplayProbe::yardOpen(world,id)) {
                        std::cerr<<name<<": traversal failed balance="<<crusades<<" crossed="<<crossed
                                 <<" position="<<arrived.x.floorInt()<<','<<arrived.z.floorInt()
                                 <<" target="<<targetX<<','<<targetZ<<" open="<<RetailReplayProbe::yardOpen(world,id)<<'\n';
                        return 1;
                    }
                }
                ++count;
            }
        }
        std::cout<<"PASS: "<<count<<" shipped gate scripts under both balances across activation, occupied closure and full World traversal\n";
        return count ? 0 : 1;
    }
    if (argc==3 && (std::string(argv[1])=="--factories" || std::string(argv[1])=="--units")) {
        const bool allUnits=std::string(argv[1])=="--units";
        auto vfs=tak::hpi::mountRetailRoot(argv[2],tak::hpi::OverridePolicy::None);
        tak::sim::TypeRegistry registry; tak::sim::setupRegistry(registry,vfs,false);
        unsigned count=0;
        for (const auto& [id,type]:registry.types()) {
            if (allUnits ? !type.script() : !type.productionScript) continue;
            const auto& file=*type.script();
            tak::cob::RetailScriptState state(file);
            struct FactoryHost:Host {
                uint32_t get(int id,const std::array<uint32_t,4>&) {
                    if (id==4) return 100;
                    if (id==18) return 1;
                    return 0;
                }
            } host;
            const std::array<uint32_t,4> args{};
            state.vm.start(file,file.scriptIndex("Create"),args);
            try {
                for (int tick=0;tick<10000;++tick) {
                    if (tick==50) state.vm.start(file,file.scriptIndex("Activate"),args);
                    if (tick==100) state.vm.start(file,file.scriptIndex("StartBuilding"),args);
                    if (tick==2000) state.vm.start(file,file.scriptIndex("StopBuilding"),args);
                    if (tick==2500) state.vm.start(file,file.scriptIndex("Deactivate"),args);
                    state.tick(file,1,host);
                }
            } catch (const std::exception& error) {
                std::cerr<<id<<": "<<error.what()<<'\n'; return 1;
            }
            ++count;
        }
        std::cout<<"PASS: "<<count<<(allUnits ? " unit" : " factory")<<" scripts, 10000 updates each across build lifecycle callbacks\n";
        return count ? 0 : 1;
    }
    if (argc==7 && std::string(argv[1])=="--origin") {
        auto file=tak::cob::load(std::filesystem::path(argv[2]));
        auto model=tak::tdo::load(std::filesystem::path(argv[3]));
        std::ifstream input(argv[4],std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        tak::cob::RetailScriptState state(file); state.restore(file,bytes);
        std::vector<tak::sim::RetailModelPiece> pieces;
        auto lower=[](std::string s) { for (auto& c:s) c=char(std::tolower(static_cast<unsigned char>(c))); return s; };
        auto append=[&](auto&& self,const tak::tdo::Object& object,int parent)->void {
            int piece=-1;
            for (size_t i=0;i<file.pieces.size();++i)
                if (lower(file.pieces[i])==lower(object.name)) { piece=int(i); break; }
            const int index=int(pieces.size());
            pieces.push_back({object.offsetRaw,parent,piece});
            for (const auto& child:object.children) self(self,child,index);
        };
        append(append,model.root,-1);
        auto point=tak::sim::retailPieceOrigin(pieces,state.pieces,std::stoi(argv[6]),uint16_t(std::stoi(argv[5])));
        for (auto v:point) std::cout<<v<<' ';
        std::cout<<'\n'; return 0;
    }
    if (argc>1 && std::string(argv[1])=="--rotate") {
        int32_t a,b; unsigned angle;
        while (std::cin>>a>>b>>angle) {
            tak::sim::retailRotatePair(a,b,uint16_t(angle));
            std::cout<<a<<' '<<b<<'\n';
        }
        return 0;
    }
    if ((argc==5 && std::string(argv[1])=="--state") ||
        (argc==6 && std::string(argv[1])=="--state-start")) {
        auto file=tak::cob::load(std::filesystem::path(argv[2]));
        std::ifstream input(argv[3],std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        tak::cob::RetailScriptState state(file); state.restore(file,bytes);
        Host host;
        for (int tick=argc==6 ? -1 : 0;tick<std::stoi(argv[4]);++tick) {
            host.events.clear();
            if (tick<0) state.notify(file,std::stoi(argv[5]),host);
            else state.tick(file,1,host);
            std::cout<<state.vm.active<<' '<<host.seed;
            for (auto v:state.vm.statics) std::cout<<' '<<v;
            for (const auto& t:state.vm.threads) for (auto v:t.words) std::cout<<' '<<v;
            for (const auto& p:state.pieces) {
                std::cout<<' '<<p.active;
                for (auto* values:{&p.moveTarget,&p.moveSpeed,&p.turnTarget,&p.turnSpeed,
                                  &p.spinTarget,&p.spinAcceleration,&p.move,&p.turn})
                    for (auto v:*values) std::cout<<' '<<uint32_t(v);
            }
            std::cout<<' '<<host.events.size();
            for (auto v:host.events) std::cout<<' '<<v;
            std::cout<<'\n';
        }
        return 0;
    }
    if (argc>1 && std::string(argv[1])=="--pieces") {
        uint32_t op; int axis,target,speed,elapsed;
        while (std::cin>>op>>axis>>target>>speed>>elapsed) {
            tak::cob::RetailPiece p;
            for (auto* values:{&p.moveTarget,&p.moveSpeed,&p.turnTarget,&p.turnSpeed,
                              &p.spinTarget,&p.spinAcceleration,&p.move,&p.turn})
                for (auto& value:*values) std::cin>>value;
            std::cin>>p.active;
            if (op) p.command(op,axis,target,speed);
            p.tick(elapsed);
            for (auto* values:{&p.moveTarget,&p.moveSpeed,&p.turnTarget,&p.turnSpeed,
                              &p.spinTarget,&p.spinAcceleration,&p.move,&p.turn})
                for (auto value:*values) std::cout<<value<<' ';
            std::cout<<p.active<<'\n';
        }
        return 0;
    }
    if (argc>1 && std::string(argv[1])=="--oracle") {
        unsigned nc,ns,nv,nt;
        while (std::cin>>nc>>ns>>nv>>nt) {
            tak::cob::File file;
            file.code.resize(nc); file.scripts.resize(ns); file.numStatics=nv;
            for (auto& word:file.code) std::cin>>word;
            for (auto& script:file.scripts) std::cin>>script.entry;
            tak::cob::RetailVm vm(nv); Host host;
            for (auto& value:vm.statics) std::cin>>value;
            for (auto& thread:vm.threads) for (auto& value:thread.words) std::cin>>value;
            std::cin>>vm.active>>host.seed;
            for (unsigned tick=0;tick<nt;++tick) {
                int elapsed; std::cin>>elapsed;
                host.events.clear(); vm.tick(file,elapsed,host);
                std::cout<<vm.active<<' '<<host.seed;
                for (auto value:vm.statics) std::cout<<' '<<value;
                for (const auto& thread:vm.threads) for (auto value:thread.words) std::cout<<' '<<value;
                std::cout<<' '<<host.events.size();
                for (auto value:host.events) std::cout<<' '<<value;
                std::cout<<'\n';
            }
        }
        return 0;
    }
    {
        using namespace tak::sim;
        auto script=std::make_shared<tak::cob::File>();
        script->scripts={{"Open",0},{"Close",6}};
        script->code={0x10021001,18,0x10021001,1,0x10082000,0x10065000,
                      0x10021001,18,0x10021001,0,0x10082000,0x10065000};
        for (char cell:std::string(".oOfwcCSyY")) {
            World world;world.setVisPlayer(-1);
            world.setTerrain(std::vector<uint8_t>(32*32,0),32,32,0);
            UnitType factory;factory.id="yard";factory.maxVel=Fixed();
            factory.footX=factory.footZ=1;factory.yardMap=std::string(1,cell);
            factory.simulationScript=script;
            UnitType body;body.id="body";body.maxVel=Fixed::fromInt(1);body.footX=body.footZ=1;
            const int producer=world.spawn(&factory,128,128,0,0);
            const int occupant=world.spawn(&body,256,256,0,0);
            if (!RetailReplayProbe::yard(world,producer,"Open")) return 1;
            world.unit(occupant)->x=world.unit(producer)->x;
            world.unit(occupant)->z=world.unit(producer)->z;
            const bool blocksClosed=std::string("ofwcCS").find(cell)!=std::string::npos;
            if (RetailReplayProbe::yard(world,producer,"Close")!=blocksClosed) return 1;
            world.unit(occupant)->x=Fixed::fromInt(256);
            if (RetailReplayProbe::yard(world,producer,"Close")) return 1;
            world.unit(occupant)->x=world.unit(producer)->x;
            const bool blocksOpen=std::string("ofwSO").find(cell)!=std::string::npos;
            if (RetailReplayProbe::yard(world,producer,"Open")==blocksOpen) return 1;
            world.unit(occupant)->x=Fixed::fromInt(256);
            if (RetailReplayProbe::yard(world,producer,"Close")) return 1;
            world.unit(producer)->x=Fixed();
            world.unit(producer)->missionEvents=0;
            if (RetailReplayProbe::yard(world,producer,"Open") ||
                !(world.unit(producer)->missionEvents&4)) return 1;
        }
        std::cout<<"PASS: factory yard transitions wait for occupied blocking cells\n";
        {
            World world;world.setVisPlayer(-1);
            world.setTerrain(std::vector<uint8_t>(32*32,0),32,32,0);
            world.setMapPlacementFeatures(std::vector<uint16_t>(32*32,0xffff),{});
            UnitType gate;gate.id="gate";gate.maxVel=Fixed();gate.gate=true;
            gate.footX=gate.footZ=1;gate.yardMap="c";gate.simulationScript=script;
            UnitType body;body.footX=body.footZ=1;
            const int id=world.spawn(&gate,128,128),occupant=world.spawn(&body,256,256);
            RetailReplayProbe::cacheYard(world);
            auto grades=[&](int passage,int border) {
                return RetailReplayProbe::cachedYard(world,8,8)==passage &&
                       RetailReplayProbe::cachedYard(world,7,8)==border;
            };
            if (!grades(3,4)) return 1;
            if (!RetailReplayProbe::yard(world,id,"Open") || !grades(6,6)) return 1;
            world.unit(occupant)->x=world.unit(occupant)->z=Fixed::fromInt(128);
            if (!RetailReplayProbe::yard(world,id,"Close") || !grades(6,6)) return 1;
            world.unit(occupant)->x=Fixed::fromInt(256);
            if (RetailReplayProbe::yard(world,id,"Close") || !grades(3,4)) return 1;
            // Retail refreshes even an accepted assignment of the same state.
            world.unit(id)->x=Fixed::fromInt(144);
            if (RetailReplayProbe::yard(world,id,"Close") ||
                RetailReplayProbe::cachedYard(world,9,8)!=3) return 1;
            std::cout<<"PASS: gate yard changes refresh cached passages and clearance borders\n";
        }
        {
            World world;world.setVisPlayer(-1);
            world.setTerrain(std::vector<uint8_t>(32*32,0),32,32,0);
            world.setMapPlacementFeatures(std::vector<uint16_t>(32*32,0xffff),{});
            auto gateScript=std::make_shared<tak::cob::File>(*script);
            gateScript->scripts={{"Activate",0},{"Deactivate",6}};
            UnitType gate;gate.id="command-gate";gate.maxVel=Fixed();gate.gate=true;
            gate.onOffable=true;gate.activateWhenBuilt=false;
            gate.footX=gate.footZ=1;gate.yardMap="c";gate.simulationScript=gateScript;
            const int id=world.spawn(&gate,128,128);
            RetailReplayProbe::tickYard(world,id);
            RetailReplayProbe::cacheYard(world);
            TypeRegistry registry;
            tak::net::Command command;command.kind=tak::net::Cmd::SetActive;
            command.unitId=id;command.targetId=1;command.player=1;
            applyCommand(world,registry,command);
            if (world.unit(id)->active || RetailReplayProbe::cachedYard(world,8,8)!=3) return 1;
            command.player=0;applyCommand(world,registry,command);
            if (!world.unit(id)->active || RetailReplayProbe::cachedYard(world,8,8)!=6) return 1;
            world.unit(id)->missionEvents=0;applyCommand(world,registry,command);
            if (world.unit(id)->missionEvents) return 1; // unchanged state sends no callback
            command.targetId=0;applyCommand(world,registry,command);
            if (world.unit(id)->active || RetailReplayProbe::cachedYard(world,8,8)!=3) return 1;
            world.unit(id)->missionEvents=0;applyCommand(world,registry,command);
            if (world.unit(id)->missionEvents) return 1;
            std::cout<<"PASS: owned gate activation commands drive scripts and path grades immediately\n";
        }
    }
    {
        tak::cob::File animation;animation.pieces.resize(70);
        tak::cob::RetailScriptState indexed(animation);
        std::vector<tak::cob::RetailPiece> reference(70);
        Host animationHost;
        tak::cob::RetailScriptState::Adapter<Host> adapter{indexed,animationHost};
        const std::array<uint32_t,6> operations={0x10001000,0x10002000,0x10003000,
                                               0x10004000,0x1000b000,0x1000c000};
        uint32_t seed=73;
        auto next=[&] {seed=seed*1664525u+1013904223u;return seed;};
        std::array<bool,70> seen{};
        for (int step=0;step<3000;++step) {
            const int piece=int((next()>>8)%70),axis=int((next()>>8)%3);
            const auto op=operations[(next()>>8)%operations.size()];
            const int target=int((next()>>8)%131072)-65536,speed=int((next()>>8)%60000);
            seen[size_t(piece)]=true;
            adapter.piece(op,piece,axis,target,speed);
            reference[size_t(piece)].command(op,axis,target,speed);
            if (step==1500) {
                // Native restoration marks every piece active for its first
                // update, including pieces above the compact index's limit.
                std::vector<uint8_t> bytes(0xa48+70*0x6c,0);
                auto put=[&](size_t offset,uint32_t value) {
                    for (unsigned byte=0;byte<4;++byte) bytes[offset+byte]=uint8_t(value>>(byte*8));
                };
                for (size_t i=0;i<reference.size();++i) {
                    auto& pieceState=reference[i];size_t offset=0xa48+i*0x6c;
                    for (const auto* values:{&pieceState.moveTarget,&pieceState.moveSpeed,
                            &pieceState.turnTarget,&pieceState.turnSpeed,&pieceState.spinTarget,
                            &pieceState.spinAcceleration,&pieceState.move,&pieceState.turn})
                        for (auto value:*values) {put(offset,uint32_t(value));offset+=4;}
                    put(offset,pieceState.visible);put(offset+4,pieceState.cached);put(offset+8,pieceState.shaded);
                    pieceState.active=true;
                }
                indexed.restore(animation,bytes);
            }
            const int elapsed=int((next()>>8)%6);
            indexed.tick(animation,elapsed,animationHost);
            for (auto& pieceState:reference) pieceState.tick(elapsed);
            for (size_t i=0;i<reference.size();++i) {
                const auto& a=indexed.pieces[i];const auto& b=reference[i];
                if (a.moveTarget!=b.moveTarget || a.moveSpeed!=b.moveSpeed ||
                    a.turnTarget!=b.turnTarget || a.turnSpeed!=b.turnSpeed ||
                    a.spinTarget!=b.spinTarget || a.spinAcceleration!=b.spinAcceleration ||
                    a.move!=b.move || a.turn!=b.turn || a.active!=b.active ||
                    a.visible!=b.visible || a.cached!=b.cached || a.shaded!=b.shaded || a.rendered!=b.rendered)
                    return 1;
            }
        }
        if (!std::all_of(seen.begin(),seen.end(),[](bool value){return value;})) return 1;
        std::cout<<"PASS: indexed animation matches full scan through commands and restoration\n";
    }
    tak::cob::File file;
    file.scripts={{"parent",0},{"child",7}};
    file.code={0x10062000,1,0,0x10021001,0,0x10065000,0,
               0x10021001,34,0x10013000,0x10021001,0,0x10065000};
    tak::cob::RetailVm vm;
    Host host;
    if (vm.start(file,0)!=0) return 1;
    vm.tick(file,1,host);
    if (vm.active!=2 || vm.threads[0].flags()!=0x2800000 || vm.threads[1].words[3]!=1) return 1;
    {
        // Restoring a sleeping child and blocked parent must resume exactly as
        // uninterrupted execution, including the tick of the parent's wakeup.
        std::vector<uint8_t> bytes(0xa48,0);
        auto put=[&](size_t offset,uint32_t value) {
            for (unsigned byte=0;byte<4;++byte) bytes[offset+byte]=uint8_t(value>>(byte*8));
        };
        for (size_t slot=0;slot<vm.threads.size();++slot)
            for (size_t word=0;word<vm.threads[slot].words.size();++word)
                put(4+slot*0xa4+word*4,vm.threads[slot].words[word]);
        put(0xa44,vm.active);
        tak::cob::RetailScriptState restored(file);restored.restore(file,bytes);
        auto uninterrupted=vm;
        Host originalHost=host,restoredHost=host;
        for (int step=0;step<4;++step) {
            uninterrupted.tick(file,1,originalHost);restored.tick(file,1,restoredHost);
            if (uninterrupted.active!=restored.vm.active || originalHost.seed!=restoredHost.seed ||
                originalHost.events!=restoredHost.events) return 1;
            for (size_t slot=0;slot<vm.threads.size();++slot)
                if (uninterrupted.threads[slot].words!=restored.vm.threads[slot].words) return 1;
        }
        std::cout<<"PASS: restored sleeping threads preserve continuation and parent wakeup\n";
    }
    vm.tick(file,1,host);
    if (vm.active!=1 || vm.threads[0].flags()!=0x1000000) return 1;
    vm.tick(file,1,host);
    if (vm.active) return 1;
    std::cout<<"PASS: separate call slots, integer sleep and ordered parent wakeup\n";
}
