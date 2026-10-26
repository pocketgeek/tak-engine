// Retail-backed check of the Crusades balance rule (KINGDOMS.icd 0x518080, 0x4c1950,
// 0x515ac0). With Crusades on, the unit roster is UnitsCB\ alone -- no fallback to
// Units\ -- and every build menu is the STANDARD CanBuild\ tree filtered by that
// roster and sorted by ascending [Menu] Priority. Retail never reads CanBuildCB.
#include "sim/matchsetup.h"
#include "hpi/hpi.h"
#include "tdf/tdf.h"
#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

static std::string lower(std::string s) {
    for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    SDL_SetMainReady();
    try {
        auto mounts=tak::hpi::mountRetailRoot(argv[1]);
        tak::hpi::Vfs vfs(&mounts,true);
        int failures=0,checks=0;
        auto check=[&](bool ok,const std::string& context) {
            ++checks;if(!ok){++failures;std::cerr<<"FAIL "<<context<<'\n';}
        };
        tak::sim::TypeRegistry standard,crusades;
        tak::sim::setupRegistry(standard,vfs,false);
        tak::sim::setupRegistry(crusades,vfs,true);
        if(!crusades.find("zontrain")){std::cerr<<"no Crusades data\n";return 1;}

        // Units with a units/ FBI but none in unitscb/ do not exist in a Crusades game.
        for(const char* id:{"monelem","monmum","tarcship","tarhel","tarnecr2","zonspide"}) {
            check(crusades.find(id)==nullptr,std::string("Crusades roster lacks ")+id);
            check(!vfs.has(std::string("unitscb/")+id+".fbi"),std::string("no unitscb FBI for ")+id);
        }
        // (Iron Plague units; a plain 3.0 install has none of them.)
        for(const char* id:{"monelem","monmum","tarcship","tarhel","zonspide"})
            if(vfs.has(std::string("units/")+id+".fbi"))
                check(standard.find(id)!=nullptr,std::string("standard roster has ")+id);
        for(const auto& [id,type]:crusades.types())
            check(vfs.has("unitscb/"+id+".fbi"),"Crusades type "+id+" comes from unitscb");

        // The Beast Tamer: exactly retail's nine, in priority order, in both balances.
        const std::vector<std::string> tamer{"zonlode","zonkrak","zongryp","zonwolf","zonbasil",
                                             "zonharp","zonglyph","zonhand","zonlord"};
        check(crusades.buildable("zontrain")==tamer,"Crusades zontrain menu is retail's nine");
        check(standard.buildable("zontrain")==tamer,"standard zontrain menu is retail's nine");
        auto has=[](const tak::sim::TypeRegistry& r,const char* builder,const char* id) {
            const auto& m=r.buildable(builder);
            return std::find(m.begin(),m.end(),id)!=m.end();
        };
        // Crusades keeps the standard tech tree: gods on monarchs, Orm/Barracuda on
        // the Shaman, Swamp Beast on the Handler, and the Ark has no menu.
        check(has(crusades,"zonsham","zonamoe") && has(crusades,"zonsham","zonbar"),
              "Crusades zonsham builds zonamoe and zonbar");
        check(!has(crusades,"zonsham","zongod"),"Crusades zonsham does not build zongod");
        check(has(crusades,"araking","aragod"),"Crusades araking builds aragod");
        check(has(crusades,"zonhand","zonswamp"),"Crusades zonhand builds zonswamp");
        check(!has(crusades,"arapries","aragod"),"Crusades arapries does not build aragod");
        check(crusades.buildable("aratrans").empty(),"Crusades aratrans has no build menu");
        check(!has(crusades,"aracastl","aragren"),"Crusades aracastl has no aragren");

        // Standard balance is exactly the canbuild tree (every entry is a unit), and
        // the retail stable priority sort equals our former (priority, name) sort:
        // the shipped data has no priority ties and no missing priorities.
        std::map<std::string,std::vector<std::pair<double,std::string>>> source;
        for(const auto& path:vfs.list("canbuild")) {
            auto key=lower(path);if(!key.ends_with(".tdf"))continue;
            auto slash=key.find('/',9);
            if(slash==std::string::npos || key.find('/',slash+1)!=std::string::npos)continue;
            auto bytes=vfs.read(path);
            auto doc=tak::tdf::parseText(std::string(bytes.begin(),bytes.end()),path);
            auto menu=doc.child("menu");
            check(menu && menu->value("priority")!=nullptr,key+" has a priority");
            source[key.substr(9,slash-9)].push_back({menu?menu->numberOr("priority",99):99,
                                                     std::filesystem::path(key).stem().string()});
        }
        check(source.size()>20,"canbuild tree present");
        for(auto [builder,entries]:source) {
            std::sort(entries.begin(),entries.end());   // the former (priority, name) rule
            std::vector<std::string> former;
            for(size_t i=0;i<entries.size();++i) {
                if(i)check(entries[i].first!=entries[i-1].first,builder+" has no priority tie");
                former.push_back(entries[i].second);
            }
            check(standard.buildable(builder)==former,"standard "+builder+" menu unchanged");
            std::vector<std::string> filtered;
            for(const auto& id:former)if(crusades.find(id))filtered.push_back(id);
            if(crusades.find(builder))
                check(crusades.buildable(builder)==filtered,"Crusades "+builder+" = canbuild filtered by roster");
        }
        std::cout<<checks<<" checks, "<<failures<<" failures\n";
        return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
