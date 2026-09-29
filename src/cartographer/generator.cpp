#include "cartographer/generator.h"
#include <charconv>
namespace cart {
GeneratorFields generatorFields(const tak::mapgen::Params& p) {
    return {std::to_string(p.seed),std::to_string(p.players),tak::mapgen::layoutName(p.layout),
        std::to_string(p.treeDensity),std::to_string(p.rockDensity),std::to_string(p.manaDensity),
        std::to_string(p.waterDensity),std::to_string(p.reliefDensity)};
}
bool parseGeneratorFields(const GeneratorFields& f,tak::mapgen::Params& p,std::string& error) {
    auto next=p;
    auto number=[&](size_t i,uint64_t min,uint64_t max,uint64_t& value) {
        const auto result=std::from_chars(f[i].data(),f[i].data()+f[i].size(),value);
        return result.ec==std::errc{} && result.ptr==f[i].data()+f[i].size() && value>=min && value<=max;
    };
    uint64_t value=0;
    if(!number(0,0,UINT64_MAX,value)) {error="Seed must be an unsigned 64-bit whole number.";return false;}next.seed=value;
    if(!number(1,2,8,value)) {error="Choose 2 to 8 players.";return false;}next.players=uint8_t(value);
    bool layout=false;
    for(uint8_t i=0;i<=tak::mapgen::Islands;++i)if(f[2]==tak::mapgen::layoutName(i)) {next.layout=i;layout=true;}
    if(!layout) {error="Choose Mainland, Lakes or Islands.";return false;}
    uint8_t* density[]={&next.treeDensity,&next.rockDensity,&next.manaDensity,&next.waterDensity,&next.reliefDensity};
    for(size_t i=3;i<f.size();++i) {
        if(!number(i,0,255,value)) {error="Densities must be whole numbers from 0 to 255.";return false;}
        *density[i-3]=uint8_t(value);
    }
    p=next;error.clear();return true;
}
}
