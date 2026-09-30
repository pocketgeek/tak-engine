#include "sim/sim.h"
#include "crt/crt.h"
#include "sim/matchsetup.h"
#include "hpi/hpi.h"
#include "tnt/ota.h"
#include "tnt/tnt.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace tak;
static void check(bool value,const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() try {
    auto files=std::make_shared<hpi::Vfs::Files>();
    auto textFile=[&](const std::string& path,const std::string& text) {
        (*files)[path]={text.begin(),text.end()};
    };
    for (const char* name:{"soldier","araking","tarnecro"})
        textFile(std::string("units/")+name+".fbi",std::string("[UNITINFO]{\nunitname=")+name+
            ";\nobjectname="+name+";\nmaxdamage=100;\nfootprintx=1;\nfootprintz=1;\ncanmove=1;\nmaxvelocity=1;\nbuilder=1;\nworkertime=100;\n}");
    tnt::Map map;map.width=map.height=64;map.blocksX=map.blocksY=32;
    map.heights.assign(4096,40);map.features.assign(4096,0xffff);
    map.tileKeys.assign(1024,0);map.tileCols.assign(1024,0);map.tileRows.assign(1024,0);
    (*files)["kmap/restricted.tnt"]=map.save();
    tnt::Scenario metadata;metadata.useOnlyUnits="restricted.tdf";
    const auto ota=metadata.write();
    textFile("kmap/restricted.ota",ota);
    textFile("kmap/restricted.tdf","[SOLDIER]{}\n");
    hpi::Vfs vfs;vfs.setMapFiles(files);
    sim::TypeRegistry registry;registry.loadDir(vfs,"units");
    const auto* allowed=registry.find("soldier"),*denied=registry.find("tarnecro");
    check(allowed && denied,"test unit types missing");
    sim::MatchConfig config;config.vfs=&vfs;config.mapPath="kmap/restricted.tnt";
    config.slots={{true,0,0},{true,1,1}};
    sim::World ordinary;sim::setupMatch(ordinary,registry,config);
    check(ordinary.buildAllowed(denied),"ordinary skirmish read authored restriction");
    const auto ordinaryHash=ordinary.stateHash();
    ordinary.setBuildRestrictions({"SOLDIER"});
    check(ordinary.stateHash()!=ordinaryHash,"restriction absent from lockstep hash");
    ordinary.setBuildRestrictions({},false);
    check(ordinary.stateHash()==ordinaryHash,"clearing restriction changed unrestricted world hash");
    textFile("kmap/restricted.ota",ota+"\n[TAKPlaytest]{\nauthoredscenario=1;\n}\n");
    sim::World client,referee;
    config.scenarioViewPlayer=0;sim::setupMatch(client,registry,config);
    config.scenarioViewPlayer=-1;sim::setupMatch(referee,registry,config);
    check(client.buildAllowed(allowed) && !client.buildAllowed(denied),"authored restriction not installed");
    check(!client.buildAllowed(nullptr),"null construction type accepted");
    check(client.stateHash()==referee.stateHash(),"restriction setup diverged");
    const auto canonical=client.stateHash();
    client.setBuildRestrictions({"SOLDIER","soldier"});
    check(client.stateHash()==canonical,"case/duplicates changed restriction hash");
    for (auto* world:{&client,&referee}) {
        const int builder=world->units().front().id;
        check(world->unit(builder)->type->isBuilder,"test monarch not builder");
        const auto before=world->stateHash();
        net::Command command;command.player=0;command.unitId=builder;command.x=400;command.z=400;
        std::strcpy(command.type,"tarnecro");
        for (auto cmd:{net::Cmd::Train,net::Cmd::RepeatTrain,net::Cmd::Build}) {
            command.kind=cmd;sim::applyCommand(*world,registry,command);
        }
        check(world->startBuild(builder,denied,400,400)==0,"direct construction bypassed restriction");
        const int otherBuilder=world->units()[1].id;
        world->train(otherBuilder,denied);
        world->setRepeat(otherBuilder,denied);
        check(world->stateHash()==before,"denied command changed queue/order state or another owner bypassed restriction");
        command.kind=net::Cmd::Train;std::strcpy(command.type,"soldier");
        sim::applyCommand(*world,registry,command);
        check(world->unit(builder)->buildQueue.size()==1,"allowed construction rejected");
        world->setRepeat(builder,allowed);
        check(world->unit(builder)->repeatType==allowed,"allowed repeat rejected");
        const int authored=world->spawn(denied,600,600,0,0);
        check(authored>0 && world->unit(authored)->type==denied,"placed/scripted spawn incorrectly restricted");
    }
    for(int tick=0;tick<30;++tick) {
        client.tick(1.f/30);referee.tick(1.f/30);
        check(client.stateHash()==referee.stateHash(),"restricted production diverged");
    }
    auto rejects=[&](const std::string& list,const char* message) {
        textFile("kmap/restricted.tdf",list);sim::World world;bool rejected=false;
        try {sim::setupMatch(world,registry,config);}catch(const std::exception&) {rejected=true;}
        check(rejected,message);check(world.units().empty(),"bad whitelist partially spawned world");
    };
    rejects("[missing]{}\n","unknown whitelist type accepted");
    files->erase("kmap/restricted.tdf");
    {
        sim::World world;bool rejected=false;
        try {sim::setupMatch(world,registry,config);}catch(const std::exception&) {rejected=true;}
        check(rejected,"missing whitelist silently unrestricted");
    }
    textFile("kmap/restricted.tdf","");
    {
        sim::World world;sim::setupMatch(world,registry,config);
        check(!world.buildAllowed(allowed),"explicit empty whitelist became unrestricted");
    }
    // Rules-only scenarios initialize after ordinary starting units exist, and
    // their initial trace records must reach the sink installed by match setup.
    metadata.hasScenario=true;
    crt::Scenario scene;scene.players.resize(9);
    scene.players[1].push_back({{{0,{}},{15,{"0","araking","Anywhere"}}},
                               {{13,{"All Players","ready"}},{14,{}}}});
    (*files)["kmap/restricted.crt"]=crt::write(scene);
    textFile("kmap/restricted.ota",metadata.write()+"\n[TAKPlaytest]{\nauthoredscenario=1;\n}\n");
    {
        int initialActions=0;
        config.scenarioTrace=[&](int tick,int player,int,int action,const crt::Rule*) {
            if(tick==0 && player==0 && action>=0)++initialActions;
        };
        sim::World world;sim::setupMatch(world,registry,config);
        check(world.scenario() && world.scenario()->drainMessages().size()==1,
              "initial rules did not see default monarch");
        check(initialActions==2,"initial scenario trace was installed too late");
        config.scenarioTrace={};
    }
    metadata.useOnlyUnits="../restricted.tdf";
    textFile("kmap/restricted.ota",metadata.write()+"\n[TAKPlaytest]{\nauthoredscenario=1;\n}\n");
    {
        sim::World world;bool rejected=false;
        try {sim::setupMatch(world,registry,config);}catch(const std::exception&) {rejected=true;}
        check(rejected,"restriction escaped its transferred map companions");
    }
    std::cout<<"PASS: authored Use Only loading, command enforcement, script bypass and lockstep parity\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
