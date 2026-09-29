#include "cartographer/validation.h"
#include "cartographer/triggers.h"
#include "sim/matchsetup.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>

namespace cart {
namespace {
std::string folded(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});
    return value;
}
}
std::vector<MapIssue> validateMap(const tak::tnt::Map& map,
    const tak::tnt::Scenario& metadata,const tak::crt::Scenario& scenario,
    const std::vector<PlacedUnit>& units,const std::set<std::string>& useOnly,
    const tak::sim::TypeRegistry& registry,const tak::hpi::Vfs& vfs) {
    std::vector<MapIssue> issues;
    auto issue=[&](std::string text,float x=-1,float z=-1,bool error=false) {
        issues.push_back({error?MapIssue::Severity::Error:MapIssue::Severity::Warning,std::move(text),x,z});
    };
    if(map.width<=0 || map.height<=0 || map.heights.size()!=size_t(map.width)*map.height ||
       map.features.size()!=map.heights.size()) {
        issue("Invalid terrain dimensions or cell arrays",-1,-1,true);return issues;
    }
    auto inside=[&](float x,float z) {return std::isfinite(x) && std::isfinite(z) && x>=0 && z>=0 && x<map.width*16.f && z<map.height*16.f;};
    auto world=std::make_unique<tak::sim::World>();
    world->setTerrain(map.heights,map.width,map.height,map.seaLevel,&map.features);
    world->buildNavClasses(registry);
    tak::sim::registerMapFeatures(*world,map,vfs,&registry);
    std::set<std::string> restrictions,names,regions;
    for(const auto& type:useOnly)restrictions.insert(folded(type));
    for(const auto& unit:units) {
        const auto label=unit.name.empty()?unit.type:unit.name;
        if(!inside(unit.x,unit.z)) {issue(label+": outside map",-1,-1,true);continue;}
        if(unit.player<0 || unit.player>8)issue(label+": invalid owner",unit.x,unit.z,true);
        if(!unit.name.empty() && !names.insert(folded(unit.name)).second)issue(label+": duplicate unique name",unit.x,unit.z,true);
        if(!restrictions.empty() && !restrictions.count(folded(unit.type)))issue(label+": excluded by Use Only",unit.x,unit.z);
        const auto* type=registry.find(folded(unit.type));
        if(!type) {issue(label+": unknown unit type",unit.x,unit.z,true);continue;}
        // Preplaced scenarios may deliberately bypass construction constraints.
        // Report these as warnings rather than forbidding an authored placement.
        if(!type->canFly && !world->canPlace(type,unit.x,unit.z))
            issue(label+": engine placement rejects this terrain or feature footprint",unit.x,unit.z);
    }
    if(metadata.starts.empty())issue("No player start positions");
    std::set<std::pair<int,int>> startCells;
    for(size_t i=0;i<metadata.starts.size();++i) {
        const auto& start=metadata.starts[i];float x=start.xpos*16.f,z=start.zpos*16.f;
        const auto label="Start "+std::to_string(i+1);
        if(!inside(x,z)) {issue(label+": outside map",-1,-1,true);continue;}
        if(!startCells.insert({start.xpos,start.zpos}).second)issue(label+": overlaps another start",x,z);
        std::string blocked;
        for(const char* monarch:tak::sim::kMonarchs) {
            const auto* type=registry.find(monarch);
            if(type && !type->canFly && !world->canPlace(type,x,z)) {if(!blocked.empty())blocked+=", ";blocked+=type->name;}
        }
        if(!blocked.empty())issue(label+": unsuitable for "+blocked,x,z);
    }
    for(const auto& region:scenario.regions) {
        if(region.name.empty() || !regions.insert(folded(region.name)).second)issue("Region name is empty or duplicated: "+region.name,-1,-1,true);
        if(std::min(region.x1,region.x2)<0 || std::min(region.z1,region.z2)<0 || std::max(region.x1,region.x2)>=map.width || std::max(region.z1,region.z2)>=map.height)
            issue("Region has out-of-map bounds: "+region.name,-1,-1,true);
    }
    for(size_t p=0;p<scenario.players.size();++p)for(size_t g=0;g<scenario.players[p].size();++g) {
        const auto& group=scenario.players[p][g];
        for(bool action:{false,true}) {
            const auto& rules=action?group.actions:group.conditions;
            const auto& definitions=action?actionDefs():conditionDefs();
            for(const auto& rule:rules) {
                const auto label="Player "+std::to_string(p)+", rule "+std::to_string(g+1)+": ";
                if(rule.opcode<0 || size_t(rule.opcode)>=definitions.size()) {issue(label+"unknown opcode",-1,-1,true);continue;}
                const auto& params=definitions[rule.opcode].params;
                for(size_t s=0;s<params.size();++s) {
                    const auto value=folded(rule.slot[s]);
                    if(rule.slot[s].size()>63)issue(label+"operand exceeds the CRT 63-byte limit",-1,-1,true);
                    if(params[s]==PKind::Location && !value.empty() && value!="anywhere" && !regions.count(value))issue(label+"unknown region: "+rule.slot[s],-1,-1,true);
                }
            }
        }
    }
    return issues;
}
}
