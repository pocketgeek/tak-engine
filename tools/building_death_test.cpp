#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/footprint.h"
#include <cstdio>
using namespace tak;
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
    int failures=0;
    auto check=[&](bool ok,const char* message) {
        if (!ok) {++failures;std::fprintf(stderr,"FAIL: %s\n",message);}
    };
    for (bool balance:{false,true}) {
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,balance);
        sim::World definitions;
        sim::registerMapFeatures(definitions,tnt::Map{},vfs,&registry);
        check(registry.find("arakeep")->instantCorpse,"Keep has immediate constant corpse handler");
        check(!registry.find("arasword")->instantCorpse,"animated mobile death remains animated");
        for (const auto& [name,type]:registry.types()) {
            if (!type.instantCorpse) continue;
            const int ct=definitions.corpseTypeOf(&type);
            check(ct>=0,"instant wreck has feature definition");if (ct<0) continue;
            if (!balance) std::printf("INSTANT %s\n",name.c_str());
            for (int death:{1,3}) {
                sim::World w;w.setVisPlayer(-1);
                w.setTerrain(std::vector<uint8_t>(64*64,100),64,64,20);
                w.setFeatureTypes(definitions.featureTypes());w.mapCorpse(&type,ct);
                const int id=w.spawn(&type,512,512,std::nullopt,0);
                const auto originX=w.unit(id)->x,originZ=w.unit(id)->z;
                w.unit(id)->hp={};w.unit(id)->deathType=uint8_t(death);
                w.tick(1.f/30);
                const auto& corpse=*w.unit(id);const auto& feature=w.featureTypes()[size_t(ct)];
                const int cx=sim::footprintOrigin(originX,type.footX)+type.corpseAdjX;
                const int cz=sim::footprintOrigin(originZ,type.footZ)+type.corpseAdjZ;
                check(!corpse.alive() && corpse.corpseAnimationTicks()==0 && corpse.corpseUntil>0,
                      "ordinary and explosion deaths produce immediate ruins");
                check(corpse.x==originX && corpse.z==originZ,"ruin retains the original model origin despite footprint adjustment");
                if (feature.blocking) check(!w.nav().walkable(cx,cz),"ruin blocks immediately");
            }
        }
    }
    return failures?1:0;
}
