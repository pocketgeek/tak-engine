#include "sim/scenario.h"
#include "sim/sim.h"
#include "hpi/hpi.h"
#include <iostream>
#include <stdexcept>

using namespace tak;
static void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
int main() try {
    {
        crt::Scenario source;source.units.push_back({"soldier","named"});
        source.players.resize(9);source.players[0].push_back({{{19,{}}},{{13,{"All Players","message"}}}});
        source.regions.push_back({"Region",1,2,3,4});source.customTypes.push_back({"soldier"});
        const auto bytes=crt::write(source);
        check(crt::parse(bytes).version==1,"valid CRT rejected");
        for(size_t n=0;n<bytes.size();++n) {
            const auto truncated=crt::parse({bytes.begin(),bytes.begin()+n});
            check(truncated.version==0 && truncated.units.empty() && truncated.players.empty() &&
                  truncated.regions.empty() && truncated.customTypes.empty(),"truncated CRT accepted or partial state leaked");
        }
        for(const uint32_t version : {0x7fc00000u,0x7f800000u,0x40000000u}) {
            auto invalid=bytes;for(int i=0;i<4;++i)invalid[i]=uint8_t(version>>(i*8));
            check(crt::parse(invalid).version==0,"invalid CRT version accepted");
        }
        auto residue=bytes;residue[4+4+256-1]=0x80; // Retail raw records may retain bytes after a NUL.
        check(crt::parse(residue).version==1,"retail record residue rejected");
    }
    auto files=std::make_shared<hpi::Vfs::Files>();
    for(const char* name:{"soldier","builder"}) {
        const auto text=std::string("[UNITINFO]{\nunitname=")+name+";\nobjectname="+name+
            ";\nmaxdamage=100;\nfootprintx=1;\nfootprintz=1;\ncanmove=1;\nmaxvelocity=1;\n}";
        (*files)[std::string("units/")+name+".fbi"]={text.begin(),text.end()};
    }
    hpi::Vfs vfs;vfs.setMapFiles(files);sim::TypeRegistry registry;registry.loadDir(vfs,"units");
    const auto* soldier=registry.find("soldier");const auto* builder=registry.find("builder");
    check(soldier && builder,"synthetic types missing");
    sim::World world;world.setPlayerCount(2);
    world.setTerrain(std::vector<uint8_t>(64*64,40),64,64,0);
    world.spawn(soldier,168,168,0,0);
    world.spawn(builder,184,168,0,0);
    world.spawn(soldier,600,600,0,0); // Outside the named region.
    world.spawn(soldier,168,184,0,1); // Another owner's unit does not count.
    int dead=world.spawn(soldier,184,184,0,0);world.unit(dead)->hp=sim::Fixed();world.unit(dead)->deadFor=0;
    auto fires=[&](int opcode,std::string first,std::string second,std::string third="") {
        crt::Scenario scenario;scenario.players.resize(2);
        scenario.regions.push_back({"Hill",10,10,12,12});
        crt::RuleGroup group;group.conditions.push_back({opcode,{first,second,third}});
        group.actions.push_back({13,{"Player 1","fired"}});scenario.players[0].push_back(group);
        // Exercise the actual serialized operand order used by Cartographer.
        scenario=crt::parse(crt::write(scenario));
        sim::ScenarioScript script(scenario,registry,0,2,64,64);script.start(world);script.step(world,1.f/30);
        auto messages=script.drainMessages();
        script.step(world,1.f/30);check(script.drainMessages().empty(),"unchanged condition fired twice");
        return !messages.empty();
    };
    check(fires(15,"0","soldier","Hill"),"control greater-than uses count then type");
    check(!fires(15,"1","soldier","Hill"),"greater-than excludes equality, dead units and other owners");
    check(fires(16,"2","soldier","Hill"),"control less-than uses count then type");
    check(!fires(16,"1","soldier","Hill"),"less-than excludes equality");
    check(fires(15,"1","aNy UnIt","Hill"),"Any Unit counts multiple types");
    check(!fires(15,"2","Any Unit","Hill"),"Any Unit respects region bounds");
    check(fires(15,"2","Any Unit","Anywhere"),"Anywhere counts units outside named regions");
    check(!fires(15,"0","missing","Hill"),"unknown type is not a wildcard");
    check(fires(13,"Any Unit","Hill"),"control-most supports Any Unit");
    check(!fires(14,"Any Unit","Hill"),"control-least supports Any Unit");
    {
        crt::Scenario scenario;scenario.players.resize(2);
        crt::RuleGroup group;group.conditions.push_back({0,{}});
        group.actions.push_back({2,{"progress","42"}});
        group.actions.push_back({25,{"Player 1","Collected ","progress"," items"}});
        group.actions.push_back({25,{"Player 2","Remaining ","unset"," items"}});
        scenario.players[0].push_back(group);scenario=crt::parse(crt::write(scenario));
        sim::ScenarioScript human(scenario,registry,0,2,64,64),server(scenario,registry,-1,2,64,64);
        human.start(world);server.start(world);human.step(world,1.f/30);server.step(world,1.f/30);
        const auto visible=human.drainMessages();const auto all=server.drainMessages();
        check(visible.size()==1 && visible[0].text=="Collected 42 items","flag message interpolation or recipient filtering");
        check(all.size()==2 && all[1].text=="Remaining 0 items","unset flag message should use zero");
        uint64_t clientHash=0,serverHash=0;human.foldHash(clientHash);server.foldHash(serverHash);
        check(clientHash==serverHash,"message filtering changed deterministic rule state");
    }
    std::cout<<"PASS: CRT control operands, wildcard counts, region/owner filters and flag messages\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
