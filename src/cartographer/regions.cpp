#include "cartographer/regions.h"
#include "cartographer/triggers.h"
#include <algorithm>
#include <cctype>
namespace cart {
namespace {
std::string lower(std::string s) {for(char& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
template<class F> void locations(tak::crt::Scenario& scenario,F fn) {
    for(auto& player:scenario.players)for(auto& group:player)for(bool action:{false,true}) {
        auto& rules=action?group.actions:group.conditions;
        const auto& defs=action?actionDefs():conditionDefs();
        for(auto& rule:rules)if(rule.opcode>=0 && size_t(rule.opcode)<defs.size())
            for(size_t i=0;i<defs[rule.opcode].params.size();++i)
                if(defs[rule.opcode].params[i]==PKind::Location)fn(rule.slot[i]);
    }
}
}
bool setRegion(tak::crt::Scenario& scenario,int index,tak::crt::Region region,int width,int height,std::string& error) {
    error.clear();
    if(index<-1 || index>=int(scenario.regions.size())) {error="Region no longer exists.";return false;}
    const auto name=lower(region.name);
    if(name.empty() || name=="anywhere" || name.size()>63 ||
       std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isspace(c);})) {
        error="Use a unique region name of 1 to 63 bytes, other than Anywhere.";return false;
    }
    for(int i=0;i<int(scenario.regions.size());++i)if(i!=index && lower(scenario.regions[i].name)==name) {
        error="That region name is already used.";return false;
    }
    if(region.x1>region.x2)std::swap(region.x1,region.x2);
    if(region.z1>region.z2)std::swap(region.z1,region.z2);
    if(region.x1<0 || region.z1<0 || region.x2>=width || region.z2>=height) {
        error="All region corners must be inside the map. Bounds include both corner cells.";return false;
    }
    if(index<0)scenario.regions.push_back(std::move(region));
    else {
        const auto old=lower(scenario.regions[index].name);
        locations(scenario,[&](std::string& value){if(lower(value)==old)value=region.name;});
        scenario.regions[index]=std::move(region);
    }
    return true;
}
bool removeRegion(tak::crt::Scenario& scenario,int index,std::string& error) {
    error.clear();
    if(index<0 || index>=int(scenario.regions.size())) {error="Select a region first.";return false;}
    const auto name=lower(scenario.regions[index].name);bool referenced=false;
    locations(scenario,[&](const std::string& value){referenced|=lower(value)==name;});
    if(referenced) {error="A scenario rule uses this region. Change its location before deleting the region.";return false;}
    scenario.regions.erase(scenario.regions.begin()+index);return true;
}
}
