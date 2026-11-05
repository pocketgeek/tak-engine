#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/footprint.h"
#include "sim/retailinstantcorpse.h"
#include "cob/vm.h"
#include <cstdio>
using namespace tak;
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    auto vfs=hpi::mountRetailRoot(argv[1]);
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
        int restored=0,waterStays=0,waterFades=0;uint64_t hash=0;
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
                sim::World w;
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
                    // Over a bed of 0 (4959c0): a body on the bed is at rest
                    // there at once and its wreck stays; a ship's, at its
                    // waterline, fades out as it sinks.
                    const auto* u=w.unit(id);
                    const int64_t life=sim::retailWaterGraveLife(u->groundY.v,0,20);
                    check(life<0 ? u->corpseUntil==INT32_MAX
                                 : int64_t(u->corpseUntil)-u->corpseAnimationTicks()==life,
                          "a water grave lives as the feature update moves it");
                    (life<0 ? waterStays : waterFades)++;
                    for(int i=0;i<300 && w.unit(id) && w.unit(id)->corpseUntil>0;++i)w.tick(1.f/30);
                    check(life<0 ? w.unit(id) && w.unit(id)->corpseUntil==INT32_MAX
                                 : !w.unit(id) || w.unit(id)->corpseUntil==0,
                          "a wreck at rest stays and a fading one is gone");
                }
                hash=(hash*1099511628211ULL)^w.stateHash();
            }
            if(!balance)std::printf("EXPLOSION_CORPSE %s\n",name.c_str());
        }
        // 512ee0 (512f8f..512fab, emulated over 3000 layouts): the corpse is a
        // water grave when 511170's interpolated ground height under the body
        // is at or below sea level, whatever its cell's corner says.
        for (const auto& [name,type]:registry.types()) {
            if (!type.explosionCorpse || type.instantCorpse) continue;
            const int ct=definitions.corpseTypeOf(&type);
            int decomposeOverride=-1;
            auto lifetime=[&](const std::vector<uint8_t>& heights,int32_t x,int32_t z,int32_t y=-1) {
                sim::World w;w.setTerrain(heights,64,64,20);
                auto features=definitions.featureTypes();
                if (decomposeOverride>=0) features[size_t(ct)].decomposeTicks=decomposeOverride;
                w.setFeatureTypes(features);w.mapCorpse(&type,ct);
                const int id=w.spawn(&type,512,512,std::nullopt,0);
                w.unit(id)->x=sim::Fixed::raw(x);w.unit(id)->z=sim::Fixed::raw(z);
                if (y>=0) w.unit(id)->groundY=sim::Fixed::raw(y);
                w.unit(id)->hp={};w.unit(id)->deathType=3;
                w.tick(1.f/30);
                for(int i=0;i<1500 && w.unit(id)->deadFor<w.unit(id)->corpseAnimationTicks();++i)
                    w.tick(1.f/30);
                return int64_t(w.unit(id)->corpseUntil)-w.unit(id)->deadFor;
            };
            const int decompose=definitions.featureTypes()[size_t(ct)].decomposeTicks;
            const int64_t land=lifetime(std::vector<uint8_t>(64*64,100),512<<16,512<<16);
            // 4959c0 (E, check_corpse_decay.py): the decomposition, then 60
            // updates sinking into the ground. Before: the decomposition alone.
            check(decompose>0 ? land==decompose+58 : land>=INT32_MAX-3000,
                  "a wreck on land decomposes, then sinks into the ground");
            decomposeOverride=90;
            check(lifetime(std::vector<uint8_t>(64*64,100),512<<16,512<<16)==90+58,
                  "a wreck with a decomposition of 90 ticks goes 148 ticks after it is laid");
            check(lifetime(std::vector<uint8_t>(64*64,5),512<<16,512<<16,18<<16)>=INT32_MAX-3000,
                  "a water grave has no decomposition");
            decomposeOverride=-1;
            check(lifetime(std::vector<uint8_t>(64*64,20),512<<16,512<<16)>=INT32_MAX-3000,
                  "a body on ground level with the sea is a water grave at rest at once: it stays");
            std::vector<uint8_t> corner(64*64,100);corner[32*64+32]=0;
            check(lifetime(corner,(32*16+15)<<16,(32*16+15)<<16)==land,
                  "a body on dry ground over a sunken cell corner became a water grave");
            // 4959c0 (E, check_water_grave.py): a floating wreck sinks 0x2ccc
            // a tick; over a bed of 0 it fades out on the way, over any other
            // bed it comes to rest there for good. Before: 75 ticks either way.
            const int64_t fade=sim::retailWaterGraveLife(18<<16,0,20);
            check(fade>90 && fade<110 && lifetime(std::vector<uint8_t>(64*64,0),512<<16,512<<16,18<<16)==fade,
                  "a wreck over a bed of 0 fades out as it sinks");
            check(lifetime(std::vector<uint8_t>(64*64,5),512<<16,512<<16,18<<16)>=INT32_MAX-3000,
                  "a wreck over a bed above 0 comes to rest there and stays");
            break;
        }
        // The Crusades roster has no Cult of Lokken Ship (tarcship has no unitscb FBI).
        check(restored==(balance?28:29),"all 29 (Crusades 28) omitted corpse types restored");
        check(waterStays>0 && waterFades>0,"both a wreck at rest on a bed of 0 and a fading one were seen");
        check(!registry.find("arasword")->explosionCorpse,"ordinary soldier explosion still gibs");
        check(!registry.find("arafly")->explosionCorpse,"handler that refuses a corpse stays excluded");
        for(bool unfinished:{false,true}) {
            sim::World w;
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
                sim::World w;
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
