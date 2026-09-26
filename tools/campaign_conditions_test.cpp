#include "sim/mission.h"
#include "sim/sim.h"
#include "hpi/hpi.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace tak;
int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--native") {
        char mode;
        while(std::cin>>mode) {
            if(mode=='A') {int cell,line;std::cin>>cell>>line;std::cout<<sim::retailCampaignAxis(cell,line)<<'\n';}
            if(mode=='R') {int32_t x,z,cx,cz,r;std::cin>>x>>z>>cx>>cz>>r;std::cout<<sim::retailCampaignRadius(x,z,cx,cz,r)<<'\n';}
            if(mode=='D') {int kind,owner,match,commander,mobile,survivor,met;sim::RetailCampaignConditionState c;
                std::cin>>kind>>owner>>match>>commander>>mobile>>survivor>>met>>c.remaining;c.met=met;
                sim::retailCampaignDeath(sim::RetailCampaignConditionState::Kind(kind),c,owner,match,commander,mobile,survivor);
                std::cout<<c.met<<' '<<c.remaining<<'\n';}
        }
        return 0;
    }
    int failed=0;
    auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failed+=!ok;};
    const auto root=std::filesystem::temp_directory_path()/
        ("tak-campaign-conditions-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{root};
    std::filesystem::create_directories(root/"units");
    for(const char* name:{"soldier","monarch","pretender","building"}) {
        std::ofstream f(root/"units"/(std::string(name)+".fbi"));
        f<<"[UNITINFO]{\nunitname="<<name<<";\nobjectname="<<name<<";\nmaxdamage=100;\nfootprintx=1;\nfootprintz=1;\n";
        if(std::string(name)!="building")f<<"canmove=1;\nmaxvelocity=1;\n";
        if(std::string(name)=="monarch" || std::string(name)=="pretender")f<<"commander=1;\n";
        f<<"}";
    }
    hpi::Vfs vfs;vfs.addLayer(hpi::MountSet(root));sim::TypeRegistry registry;registry.loadDir(vfs,"units");
    const auto* soldier=registry.find("soldier");const auto* monarch=registry.find("monarch");const auto* building=registry.find("building");
    if(!soldier || !monarch || !building)return 2;
    auto make=[&](std::string rules){for(size_t i=0;(i=rules.find(';',i))!=std::string::npos;i+=2)rules.insert(i+1,"\n");auto mission=std::make_unique<sim::MissionScript>(std::vector<uint8_t>{},
        tdf::parseText("[GlobalHeader]{"+rules+"}").children.at("globalheader"),registry,0,"conditions fixture");mission->setPlayerCommanders({monarch,monarch,monarch});return mission;};
    auto dead=[&](sim::World& w,sim::MissionScript& m,int id){w.unit(id)->deadFor=0;m.unitDied(w,id);};
    {
        sim::World w;w.spawn(soldier,500,500,0,0);int one=w.spawn(monarch,500,500,0,1),two=w.spawn(monarch,500,500,0,1);
        auto m=make("KillEnemyCommander=1;");m->start(w);dead(w,*m,one);m->step(w,1.f/30);
        check(m->outcome()==1 && w.unit(two)->alive(),"enemy commander death is an event even if another commander survives");
    }
    {
        sim::World w;w.spawn(soldier,500,500,0,0);int a=w.spawn(soldier,500,500,0,1),b=w.spawn(soldier,500,500,0,1);
        auto m=make("KillUnitType=soldier,2;VictoryTimerRunsOut=1;");m->start(w);
        dead(w,*m,a);m->step(w,1.f/30);check(m->outcome()==0,"kill counter waits for required enemy deaths");
        dead(w,*m,b);m->step(w,1.f/30);check(m->outcome()==0,"all victory conditions must hold");
        for(int i=2;i<30;++i)m->step(w,1.f/30);
        check(m->outcome()==1,"latched kill quota combines with timer at exactly30ticks");
    }
    {
        sim::World w;int human=w.spawn(monarch,500,500,0,0);w.spawn(soldier,500,500,0,0);w.spawn(soldier,500,500,0,1);
        auto m=make("VictoryTimerRunsOut=100;CommanderKilled=1;DeathTimerRunsOut=100;");m->start(w);dead(w,*m,human);m->step(w,1.f/30);
        check(m->outcome()==-1,"any defeat condition suffices without losing every soldier");
    }
    {
        sim::World w;w.spawn(soldier,500,500,0,1);int escort=w.spawn(soldier,10*16,10*16,0,0);
        auto m=make("UnitTypePassesZ=soldier,10;");m->start(w);m->step(w,1.f/30);
        check(m->outcome()==1,"axis condition already within two cells wins immediately");
        auto n=make("UnitTypePassesZ=soldier,10;");w.unit(escort)->z=sim::Fixed::fromInt(7*16);n->start(w);n->step(w,1.f/30);
        check(n->outcome()==0,"axis condition outside tolerance does not infer crossing side");
        w.unit(escort)->z=sim::Fixed::fromInt(12*16);n->step(w,1.f/30);check(n->outcome()==1,"axis tolerance includes exactly two cells");
    }
    {
        sim::World w;w.spawn(soldier,320,320,0,0);int enemy=w.spawn(soldier,160,160,0,1);
        auto m=make("MoveUnitToRadius=soldier,10,10,1;");m->start(w);m->step(w,1.f/30);
        check(m->outcome()==0,"enemy unit cannot satisfy human radius objective");
        w.unit(enemy)->player=0;m->step(w,1.f/30);check(m->outcome()==1,"radius uses authored origin without extra half cell");
    }
    {
        sim::World w;w.spawn(building,100,100,0,0);w.spawn(soldier,100,100,0,1);
        auto m=make("KillEnemyCommander=0;VictoryTimerRunsOut=0;AllUnitsKilled=1;");m->start(w);m->step(w,1.f/30);
        check(m->outcome()==0,"disabled boolean/timer conditions do not complete; own building prevents all-units defeat");
    }
    {
        sim::World w;w.spawn(soldier,100,100,0,0);int enemy=w.spawn(soldier,100,100,0,1);
        auto m=make("CaptureUnitType=soldier;");m->start(w);m->unitCaptured(w,enemy);w.unit(enemy)->player=0;m->step(w,1.f/30);
        check(m->outcome()==1,"capture condition latches matching enemy conversion");
    }
    {
        sim::World w;w.spawn(soldier,100,100,0,0);w.spawn(soldier,100,100,0,1);int target=w.spawn(building,100,100,0,0);
        w.unit(target)->underConstruction=true;auto m=make("BuildUnitType=building;");m->start(w);m->step(w,1.f/30);
        check(m->outcome()==0,"unfinished building does not satisfy build condition");
        w.unit(target)->underConstruction=false;m->step(w,1.f/30);check(m->outcome()==1,"completed owned building satisfies build condition");
    }
    {
        sim::World w;w.spawn(soldier,100,100,0,0);int enemy=w.spawn(registry.find("pretender"),100,100,0,1);
        auto m=make("KillEnemyCommander=1;");m->start(w);dead(w,*m,enemy);m->step(w,1.f/30);
        check(m->outcome()==0,"commander flag alone cannot substitute for authored faction monarch");
    }
    {
        sim::World w;w.spawn(soldier,100,100,0,0);w.spawn(soldier,100,100,0,1);
        auto m=make("VictoryTimerRunsOut=1;DeathTimerRunsOut=1;");m->start(w);
        for(int i=0;i<30;++i)m->step(w,1.f/30);
        check(m->outcome()==1,"simultaneous normal campaign outcome resolves victory first");
    }
    {
        sim::World w;w.spawn(soldier,100,100,0,0);
        auto m=make("");m->start(w);m->step(w,1.f/30);
        check(m->outcome()==0,"empty campaign victory list waits for a scripted outcome even without enemies");
    }
    {
        sim::World w;w.spawn(soldier,100,100,0,0);w.spawn(soldier,0,100,0,1);
        auto m=make("AnyUnitPassesX=0;");m->start(w);m->step(w,1.f/30);
        check(m->outcome()==-1,"enemy axis defeat accepts authored zero line");
    }
    for(int next:{1,0}) {
        sim::World w;w.spawn(soldier,100,100,0,0);int enemy=w.spawn(soldier,100,100,0,1);
        const std::vector<uint32_t> code={0x10021001,uint32_t(enemy),0x10021001,uint32_t(next),
            0x10073000,0,2,0x10024000,0x10021001,0,0x10065000};
        std::vector<uint8_t> cob(512);
        auto put=[&](size_t offset,uint32_t value){std::memcpy(cob.data()+offset,&value,4);};
        put(0,6);put(4,1);put(12,uint32_t(code.size()));put(24,52);put(28,56);
        put(36,128);put(44,60);put(48,1);put(52,0);put(56,72);put(60,80);
        std::memcpy(cob.data()+72,"Start",6);std::memcpy(cob.data()+80,"Capture",8);
        for(size_t i=0;i<code.size();++i)put(128+i*4,code[i]);
        tdf::Node header;header.values["captureunittype"]="soldier";
        sim::MissionScript m(std::move(cob),header,registry,0,"capture fixture");m.start(w);m.step(w,1.f/30);
        check(m.outcome()==(next==0?1:0) && w.unit(enemy)->player==next,
            next==0?"scripted ownership transfer satisfies capture objective":"same-owner scripted Capture is a no-op");
    }
    return failed?1:0;
}
