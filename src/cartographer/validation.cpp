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
    std::set<uint16_t> missingFeatures;
    const auto& featureTypes=world->mapPlacementTypes();
    for(size_t i=0;i<map.features.size();++i) {
        const auto feature=map.features[i];if(feature>=0xfffa)continue;
        if((feature>=map.featureNames.size() || feature>=featureTypes.size() || featureTypes[feature].name.empty()) && missingFeatures.insert(feature).second)
            issue("Missing feature definition: "+(feature<map.featureNames.size()?map.featureNames[feature]:std::to_string(feature)),float(i%map.width)*16,float(i/map.width)*16,true);
    }
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
    else if(metadata.starts.size()<2)issue("Fewer than two player starts; ordinary skirmishes need at least two");
    if(metadata.starts.size()>8)issue("More than eight player starts; only eight players can join a match");
    std::set<std::pair<int,int>> startCells;
    std::set<int> startNumbers;
    for(size_t i=0;i<metadata.starts.size();++i) {
        const auto& start=metadata.starts[i];float x=start.xpos*16.f,z=start.zpos*16.f;
        const auto label="Start "+std::to_string(start.number);
        if(start.number<1 || start.number>8)issue(label+": start number must be 1 through 8",x,z,true);
        if(!startNumbers.insert(start.number).second)issue(label+": duplicate start number (the engine keeps only one)",x,z,true);
        if(!inside(x,z)) {issue(label+": outside map",-1,-1,true);continue;}
        if(!startCells.insert({start.xpos,start.zpos}).second)issue(label+": overlaps another start",x,z);
        std::string blocked;
        for(const char* monarch:tak::sim::kMonarchs) {
            const auto* type=registry.find(monarch);
            if(type && !type->canFly && !world->canPlace(type,x,z)) {if(!blocked.empty())blocked+=", ";blocked+=type->name;}
        }
        if(!blocked.empty())issue(label+": unsuitable for "+blocked,x,z);
    }
    for(int n=1;n<=int(metadata.starts.size()) && n<=8;++n)if(!startNumbers.count(n)) {
        issue("Start numbering has a gap at "+std::to_string(n)+"; use consecutive numbers for predictable slot order");break;
    }
    // Use the same observational connectivity query as the AI. It resolves a
    // blocked endpoint to nearby occupiable ground, so these are approach-route
    // warnings, not guarantees about the exact destination cell or build site.
    if(metadata.starts.size()<=8)for(const char* monarchId:tak::sim::kMonarchs) {
        const auto* monarch=registry.find(monarchId);if(!monarch || monarch->canFly)continue;
        for(size_t i=0;i<metadata.starts.size();++i) {
            const auto& start=metadata.starts[i];const float x=start.xpos*16.f,z=start.zpos*16.f;
            if(!inside(x,z))continue;
            bool connected=false;
            for(size_t j=0;j<metadata.starts.size();++j)if(i!=j) {
                const auto& other=metadata.starts[j];const float ox=other.xpos*16.f,oz=other.zpos*16.f;
                if(inside(ox,oz) && world->pathExists(monarch,ox,oz,x,z)) {connected=true;break;}
            }
            const auto label="Start "+std::to_string(start.number)+" ("+monarch->name+")";
            if(metadata.starts.size()>1 && !connected)issue(label+": no ground approach route to another start; transports may be intentional",x,z);
            if(world->hasManaSpots()) {
                bool reachable=false;
                for(const auto& [mx,mz]:world->manaSpots())if(world->pathExists(monarch,mx,mz,x,z)) {reachable=true;break;}
                if(!reachable)issue(label+": no ground approach route to a mana deposit",x,z);
            }
        }
    }
    if(!world->hasManaSpots() && std::any_of(registry.types().begin(),registry.types().end(),[](const auto& entry){return entry.second.onMana;}))
        issue("No recognized mana deposits; skirmish players cannot expand their lodestone economy");
    for(const auto& [x,z]:world->manaSpots()) {
        std::string blocked;
        for(const auto& [id,type]:registry.types())if(type.onMana &&
            (restrictions.empty() || restrictions.count(folded(id))) && !world->canPlace(&type,x,z)) {
            if(!blocked.empty())blocked+=", ";
            blocked+=type.name.empty()?id:type.name;
        }
        if(!blocked.empty())issue("Mana deposit: engine rejects lodestone footprint for "+blocked,x,z);
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
