// Retail-backed contract checks: balance selection, scalar stats and whole menus.
#include "sim/matchsetup.h"
#include "hpi/hpi.h"
#include "tdf/tdf.h"
#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>

static std::string lower(std::string s) {
    for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
static tak::tdf::Node read(const tak::hpi::Vfs& vfs,const std::string& path) {
    auto bytes=vfs.read(path);
    return tak::tdf::parseText(std::string(bytes.begin(),bytes.end()),path);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    SDL_SetMainReady();
    try {
        auto mounts=tak::hpi::mountRetailRoot(argv[1],tak::hpi::OverridePolicy::None);
        tak::hpi::Vfs vfs(&mounts,true);
        int failures=0,checks=0,units=0,menus=0;
        auto check=[&](bool ok,const std::string& context) {
            ++checks;if(!ok){++failures;std::cerr<<"FAIL "<<context<<'\n';}
        };
        // These are independent source contracts, not a serialization of UnitType.
        struct Scalar {const char* key;float tak::sim::UnitType::* member;double fallback;};
        const Scalar scalars[]={
            {"buildcost",&tak::sim::UnitType::buildCost,0},
            {"buildtime",&tak::sim::UnitType::buildTime,0},
            {"healtime",&tak::sim::UnitType::healTime,0},
            {"workertime",&tak::sim::UnitType::workerTime,1},
            {"mogriumincome",&tak::sim::UnitType::income,0},
            {"mogriumstorage",&tak::sim::UnitType::storage,0},
            {"maxmana",&tak::sim::UnitType::maxMana,0},
            {"manarechargerate",&tak::sim::UnitType::manaRegen,0},
        };
        struct Integer {const char* key;int32_t tak::sim::UnitType::* member;int fallback;};
        const Integer integers[]={
            {"maxdamage",&tak::sim::UnitType::maxHp,100},
            {"sightdistance",&tak::sim::UnitType::sight,180},
            {"radardistance",&tak::sim::UnitType::radar,0},
            {"builddistance",&tak::sim::UnitType::buildDist,0}
        };
        std::set<std::string> ids,builders;
        check(!vfs.list("unitscb").empty(),"Crusades unit overlay is present");
        check(!vfs.list("canbuildcb").empty(),"Crusades menu overlay is present");
        for(const auto* prefix:{"units","unitscb"})for(const auto& path:vfs.list(prefix)) {
            if(!lower(path).ends_with(".fbi"))continue;
            auto doc=read(vfs,path);auto info=doc.child("unitinfo");
            if(!info)continue;
            auto stem=lower(std::filesystem::path(path).stem().string());
            if(lower(info->valueOr("objectname",stem))==stem)ids.insert(stem);
        }
        using Menu=std::map<std::pair<double,std::string>,std::string>;
        std::map<std::string,std::map<std::string,Menu>> sourceMenus;
        for(const std::string prefix:{"canbuild","canbuildcb"})for(const auto& path:vfs.list(prefix)) {
            auto key=lower(path);if(!key.ends_with(".tdf"))continue;
            auto slash=key.find('/',prefix.size()+1);
            if(slash==std::string::npos || key.find('/',slash+1)!=std::string::npos)continue;
            auto builder=key.substr(prefix.size()+1,slash-prefix.size()-1);
            auto id=std::filesystem::path(key).stem().string();
            auto doc=read(vfs,path);auto menu=doc.child("menu");
            sourceMenus[prefix][builder][{menu?menu->numberOr("priority",99):99,id}]=id;
            builders.insert(builder);
        }
        for(bool cb:{false,true}) {
            tak::sim::TypeRegistry registry;tak::sim::setupRegistry(registry,vfs,cb);
            std::string mode=cb?"Crusades ":"standard ";
            for(const auto& id:ids) {
                std::string path="units/"+id+".fbi";
                if(cb && vfs.has("unitscb/"+id+".fbi"))path="unitscb/"+id+".fbi";
                if(!vfs.has(path))continue;
                auto doc=read(vfs,path);auto info=doc.child("unitinfo");
                if(!info || lower(info->valueOr("objectname",id))!=id)continue;
                ++units;auto type=registry.find(id);
                check(type!=nullptr,mode+id+" exists");if(!type)continue;
                for(auto field:scalars)
                    check(type->*(field.member)==float(info->numberOr(field.key,field.fallback)),mode+id+" "+field.key);
                for(auto field:integers)
                    check(type->*(field.member)==int32_t(info->numberOr(field.key,field.fallback)),mode+id+" "+field.key);
                for(size_t slot=0;slot<3;++slot) {
                    auto weapon=info->child("weapon"+std::to_string(slot+1));
                    if(!weapon)continue;
                    check(slot<type->weapons.size(),mode+id+" weapon slot");
                    if(slot>=type->weapons.size())continue;
                    const auto& actual=type->weapons[slot];
                    check(actual.range==float(weapon->numberOr("range",0)),mode+id+" weapon range");
                    check(actual.reload==float(weapon->numberOr("reloadtime",1)),mode+id+" weapon reload");
                    check(actual.projVel==float(weapon->numberOr("weaponvelocity",0)),mode+id+" projectile speed");
                }
            }
            for(const auto& builder:builders) {
                const auto prefix=cb && sourceMenus["canbuildcb"].count(builder)?"canbuildcb":"canbuild";
                std::vector<std::string> expected;
                for(const auto& [priority,id]:sourceMenus[prefix][builder])expected.push_back(id);
                check(registry.buildable(builder)==expected,mode+builder+" complete ordered menu");++menus;
            }
        }
        check(units>0 && menus>0,"nonempty retail corpus");
        std::cout<<checks<<" checks, "<<units<<" unit-mode cases, "<<menus<<" builder-mode menus, "<<failures<<" failures\n";
        return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
