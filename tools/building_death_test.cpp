#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/footprint.h"
#include "sim/retailinstantcorpse.h"
#include "cob/vm.h"
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
        int restored=0;uint64_t hash=0;
        for (const auto& [name,type]:registry.types()) {
            if (!type.explosionCorpse || type.instantCorpse) continue;
            ++restored;
            // Independent execution of the authored handler verifies the analyzer.
            cob::Vm vm(*type.script());vm.enableRetailAnimation();
            vm.call("Killed",{50,0,3});
            check(vm.lastLocals().size()>1 && vm.lastLocals()[1]==1,
                  "authored explosion handler requests corpse 1");
            const int ct=definitions.corpseTypeOf(&type);
            check(ct>=0,"restored corpse definition exists");if (ct<0) continue;
            for (bool water:{false,true}) {
                sim::World w;w.setVisPlayer(-1);
                w.setTerrain(std::vector<uint8_t>(64*64,water?0:100),64,64,20);
                w.setFeatureTypes(definitions.featureTypes());w.mapCorpse(&type,ct);
                const int id=w.spawn(&type,512,512,std::nullopt,0);
                w.unit(id)->hp={};w.unit(id)->deathType=3;
                w.tick(1.f/30);
                check(w.unit(id)->deathHasCorpse,
                      "explosion preserves the authored corpse on land and water");
                for(int i=0;i<1500 && w.unit(id)->deadFor<w.unit(id)->corpseAnimationTicks();++i)
                    w.tick(1.f/30);
                check(w.unit(id)->deadFor==w.unit(id)->corpseAnimationTicks() &&
                      w.unit(id)->corpseUntil>w.unit(id)->deadFor,
                      "restored corpse survives its authored death-animation handoff");
                if(water) {
                    for(int i=0;i<76;++i)w.tick(1.f/30);
                    check(w.unit(id)->corpseUntil==0,"water corpse retains its sinking/expiry lifetime");
                }
                hash=(hash*1099511628211ULL)^w.stateHash();
            }
            if(!balance)std::printf("EXPLOSION_CORPSE %s\n",name.c_str());
        }
        check(restored==29,"all 29 omitted corpse types restored");
        check(!registry.find("arasword")->explosionCorpse,"ordinary soldier explosion still gibs");
        check(!registry.find("arafly")->explosionCorpse,"handler that refuses a corpse stays excluded");
        for(bool unfinished:{false,true}) {
            sim::World w;w.setVisPlayer(-1);
            w.setTerrain(std::vector<uint8_t>(64*64,100),64,64,20);
            const auto* type=registry.find("araat");
            w.setFeatureTypes(definitions.featureTypes());w.mapCorpse(type,definitions.corpseTypeOf(type));
            const int id=w.spawn(type,512,512,std::nullopt,0);
            sim::UnitType builderType;builderType.maxHp=100;
            if(unfinished) {
                const int builder=w.spawn(&builderType,32,32,std::nullopt,0);
                w.unit(builder)->buildSiteId=id;
            }
            auto* unit=w.unit(id);unit->underConstruction=unfinished;unit->buildBegun=unfinished;
            unit->hp={};unit->deathType=unfinished?3:sim::Unit::kDeathSelfDestruct;
            w.tick(1.f/30);
            check(w.unit(id)->deadFor==0,"actual death rather than abandoned-site removal");
            check(w.unit(id)->corpseUntil<=w.unit(id)->corpseAnimationTicks(),
                  "unfinished and self-destructed units remain excluded");
        }
        std::printf("explosion corpses balance=%d hash=%016llx\n",balance,(unsigned long long)hash);
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
