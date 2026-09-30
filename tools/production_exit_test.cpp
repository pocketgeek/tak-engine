// Exercise shipped movement/footprints/scripts: synthetic units with no retail
// goal-crowding tolerance did not expose output parking over the next birthplace.
#include "sim/matchsetup.h"
#include "hpi/hpi.h"
#include <cstdio>
#include <array>
#include <algorithm>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto vfs=tak::hpi::mountRetailRoot(argv[1]);int failures=0,cases=0;
    for(bool crusades:{false,true}) {
        tak::sim::TypeRegistry registry;tak::sim::setupRegistry(registry,vfs,crusades);
        struct Case {const char* producer;const char* output;bool infinite;int count;};
        std::array<Case,5> roster{{{"arakeep","arasword",false,24},
            {"zonhunt","zonhand",true,8},{"zonhand","zonter",true,8},{"verasy",nullptr,false,6},{"zonlord",nullptr,true,8}}};
        for(const auto& c:roster)for(int direction:{0,1,2,3}) {
            auto producer=*registry.find(c.producer);producer.storage=1000000;
            const auto* output=c.output ? registry.find(c.output) : nullptr;
            if(!c.output)for(const auto& id:registry.buildable(c.producer)) {
                const auto* t=registry.find(id);
                if(t && !t->isStructure() && t->domain==(std::string(c.producer)=="verasy" ? tak::sim::UnitType::Domain::Water : tak::sim::UnitType::Domain::Ground)) {output=t;break;}
            }
            if(!output){std::puts("FAIL: production fixture output missing");++failures;continue;}
            const auto& menu=registry.buildable(c.producer);
            if(std::none_of(menu.begin(),menu.end(),[&](const auto& name){return registry.find(name)==output;})) {
                std::printf("FAIL: %s cannot produce %s in this balance\n",c.producer,output->id.c_str());++failures;continue;
            }
            tak::sim::World world;world.setVisPlayer(-1);
            const bool water=output->domain==tak::sim::UnitType::Domain::Water;
            world.setTerrain(std::vector<uint8_t>(256*256,water?0:100),256,256,water?100:20);
            world.buildNavClasses(registry);world.setPathService(true);
            const int id=world.spawn(&producer,1800,1800,float(direction)*1.57079632679f,0);
            world.player(0).mana=1000000;
            if(c.infinite)world.setRepeat(id,output);else world.train(id,output,c.count);
            int complete=0,tick=0;
            for(;tick<24000 && complete<c.count;++tick) {
                world.tick(1.f/30);complete=0;
                for(const auto& u:world.units())if(u.id!=id && u.alive() && !u.underConstruction)++complete;
            }
            const bool pass=complete>=c.count && world.unit(id)->rally.empty() &&
                (!c.infinite || world.unit(id)->repeatType==output);
            std::printf("%s: balance=%s producer=%s output=%s direction=%d repeat=%d completed=%d ticks=%d hash=%016llx\n",
                pass?"PASS":"FAIL",crusades?"Crusades":"standard",c.producer,output->id.c_str(),direction,
                c.infinite,complete,tick,(unsigned long long)world.stateHash());
            ++cases;if(!pass)++failures;
        }
    }
    std::printf("production exits: %d cases, %d failures\n",cases,failures);
    return failures?1:0;
}
