#include "cartographer/validation.h"
#include "cartographer/triggers.h"
#include "cartographer/regions.h"
#include "cartographer/generator.h"
#include "cartographer/overlay.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <stdexcept>
static void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main() {
    try {
        tak::mapgen::Params params;params.seed=UINT64_MAX;params.players=8;params.layout=tak::mapgen::Islands;
        auto fields=cart::generatorFields(params);std::string generatorError;
        tak::mapgen::Params parsed;
        check(cart::parseGeneratorFields(fields,parsed,generatorError),"generator fields parse");
        check(tak::mapgen::encodeMapId(params)==tak::mapgen::encodeMapId(parsed),"all generator options roundtrip exactly");
        const auto original=tak::mapgen::encodeMapId(parsed);
        fields[0]="18446744073709551616";
        check(!cart::parseGeneratorFields(fields,parsed,generatorError) && tak::mapgen::encodeMapId(parsed)==original,"seed overflow leaves settings unchanged");
        fields=cart::generatorFields(params);fields[3]="256";
        check(!cart::parseGeneratorFields(fields,parsed,generatorError),"density range checked");
        fields=cart::generatorFields(params);fields[1]="3players";
        check(!cart::parseGeneratorFields(fields,parsed,generatorError),"partial numeric input rejected");
        tak::hpi::Vfs vfs;tak::sim::TypeRegistry registry;
        const auto root=std::filesystem::temp_directory_path()/("tak-editor-generator-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}} cleanup{root};
        std::filesystem::create_directories(root/"features"/"aramon");
        {
            std::ofstream file(root/"features"/"aramon"/"fixture.tdf");
            for(int i=1;i<=3;++i)file<<"[AraMana0"<<i<<"] {\nfootprintx=2;\nfootprintz=2;\nblocking=0;\n}\n";
        }
        vfs.addLayer(tak::hpi::MountSet(root));
        params.layout=tak::mapgen::Mainland;params.waterDensity=params.reliefDensity=0;
        fields=cart::generatorFields(params);check(cart::parseGeneratorFields(fields,parsed,generatorError),"edited generator settings parse");
        const auto first=tak::mapgen::generate(params,vfs),second=tak::mapgen::generate(parsed,vfs);
        check(first.starts==second.starts && first.map.heights==second.map.heights && first.map.features==second.map.features &&
              first.map.tileKeys==second.map.tileKeys && first.map.tileCols==second.map.tileCols && first.map.tileRows==second.map.tileRows,
              "editor settings reproduce actual generator output");
        auto files=std::make_shared<tak::hpi::Vfs::Files>();
        const std::string fbi="[UNITINFO] { \nUnitName=TEST;\nName=Test Unit;\nFootprintX=1;\nFootprintZ=1;\nMaxSlope=20;\nMaxWaterDepth=0;\nCanMove=1;\nMaxVelocity=1;\n }";
        (*files)["units/test.fbi"]={fbi.begin(),fbi.end()};vfs.setMapFiles(files);registry.loadDir(vfs,"units");
        check(registry.find("test")!=nullptr,"synthetic unit loaded");
        tak::tnt::Map map;map.width=map.height=32;map.heights.resize(1024,60);map.features.resize(1024,0xffff);
        auto movement=cart::terrainOverlay(map,registry,vfs,cart::OverlayKind::Movement,"test");
        check(movement.width==32 && movement.rgba[(12*32+12)*4+1]==210,"flat ground movement overlay is passable");
        map.seaLevel=100;
        movement=cart::terrainOverlay(map,registry,vfs,cart::OverlayKind::Movement,"test");
        check(movement.rgba[(12*32+12)*4]==235,"ground movement overlay rejects deep water");
        auto depth=cart::terrainOverlay(map,registry,vfs,cart::OverlayKind::WaterDepth,"");
        check(depth.rgba[2]==160 && depth.rgba[3]==150,"water depth uses sea minus low corner");
        map.seaLevel=0;
        map.heights[12*32+12]=100;
        auto slope=cart::terrainOverlay(map,registry,vfs,cart::OverlayKind::Slope,"");
        check(slope.rgba[(11*32+11)*4]==255 && slope.rgba[0]==0,"slope overlay reads four-corner spread");
        auto build=cart::terrainOverlay(map,registry,vfs,cart::OverlayKind::Buildability,"test");
        tak::sim::World reference;reference.setTerrain(map.heights,32,32,0,&map.features);reference.buildNavClasses(registry);
        for(int z=0;z<32;++z)for(int x=0;x<32;++x)
            check((build.rgba[(z*32+x)*4+1]==210)==reference.canPlace(registry.find("test"),x*16.f+8,z*16.f+8),"buildability overlay agrees with engine placement at every cell");
        map.heights[12*32+12]=60;
        tak::tnt::Scenario metadata;metadata.starts.push_back({1,12,12});
        tak::crt::Scenario scenario;scenario.regions.push_back({"Area",15,15,5,5});
        std::vector<cart::PlacedUnit> units(1);units[0].type="TEST";units[0].x=200;units[0].z=200;units[0].name="First";
        auto run=[&] {return cart::validateMap(map,metadata,scenario,units,{},registry,vfs);};
        auto has=[](const auto& issues,const std::string& text) {for(const auto& issue:issues)if(issue.message.find(text)!=std::string::npos)return true;return false;};
        check(run().empty(),"flat map, unit and reversed inclusive region validate");
        map.seaLevel=100;check(has(run(),"engine placement rejects"),"engine rejects ground unit in deep water");map.seaLevel=0;
        units.push_back(units.front());check(has(run(),"duplicate unique name"),"duplicate names detected");units.pop_back();
        units[0].x=-1;check(has(run(),"outside map"),"out of bounds detected");units[0].x=200;
        units[0].type="MISSING";check(has(run(),"unknown unit type"),"missing type detected");units[0].type="TEST";
        scenario.players.resize(1);scenario.players[0].resize(1);
        auto& rule=scenario.players[0][0].conditions.emplace_back();
        const auto& definitions=cart::conditionDefs();size_t location=0;
        for(size_t i=0;i<definitions.size();++i) {
            bool found=false;
            for(size_t s=0;s<definitions[i].params.size();++s)if(definitions[i].params[s]==cart::PKind::Location) {rule.opcode=int(i);location=s;found=true;break;}
            if(found)break;
        }
        rule.slot[location]="Missing region";check(has(run(),"unknown region"),"unresolved location diagnosed");
        rule.slot[location]="AREA";check(!has(run(),"unknown region"),"region matching ignores case");
        rule.slot[location].clear();check(!has(run(),"unknown region"),"empty location means whole map in engine");
        std::string error;
        rule.slot[location]="AREA";
        check(cart::setRegion(scenario,0,{"Renamed",12,13,4,5},32,32,error),"region rename and resize");
        check(rule.slot[location]=="Renamed" && scenario.regions[0].x1==4 && scenario.regions[0].x2==12,"rename updates location references and normalizes bounds");
        check(!cart::removeRegion(scenario,0,error),"cannot delete a referenced region");
        check(!cart::setRegion(scenario,-1,{"renamed",1,1,2,2},32,32,error),"case insensitive unique region names");
        check(!cart::setRegion(scenario,-1,{"Anywhere",1,1,2,2},32,32,error),"whole map token reserved");
        check(!cart::setRegion(scenario,0,{"Outside",0,0,32,32},32,32,error),"inclusive bounds stay within map");
        check(scenario.regions[0].name=="Renamed" && rule.slot[location]=="Renamed","failed region edit is atomic");
        auto roundtrip=tak::crt::parse(tak::crt::write(scenario));
        check(roundtrip.regions[0].x2==12 && roundtrip.players[0][0].conditions[0].slot[location]=="Renamed","region and rewritten rules survive CRT roundtrip");
        rule.slot[location]="Anywhere";check(cart::removeRegion(scenario,0,error),"unused region can be deleted");
        check(map.heights[0]==60 && units[0].name=="First","validation preserves document");
        std::cout<<"PASS: editor engine placement and scenario validation\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
