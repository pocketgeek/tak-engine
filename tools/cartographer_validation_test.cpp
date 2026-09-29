#include "cartographer/validation.h"
#include "cartographer/triggers.h"
#include "cartographer/regions.h"
#include "cartographer/generator.h"
#include "cartographer/overlay.h"
#include "cartographer/ruleedit.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <stdexcept>
static void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main() {
    try {
        cart::RegionDrag drag;std::vector<tak::crt::Region> regions{{"Region 1",10,10,20,20}};
        check(drag.begin(regions,-1,30,30,.5f,true,64,64),"begin region drawing");
        drag.update(25,22,64,64);
        check(drag.preview.name=="Region 2" && drag.preview.x1==25 && drag.preview.z1==22 && drag.preview.x2==30 && drag.preview.z2==30,"reversed region drawing and unique default name");
        drag.cancel();check(regions.size()==1 && !drag.active(),"cancel leaves source regions unchanged");
        check(drag.begin(regions,0,15,15,.5f,false,64,64) && drag.part==cart::RegionDrag::Move,"region body starts move");
        drag.update(-100,1000,64,64);
        check(drag.preview.x1==0 && drag.preview.x2==10 && drag.preview.z1==53 && drag.preview.z2==63,"region move clamps without changing size");
        check(drag.begin(regions,0,20.5f,20.5f,1,false,64,64),"pick corner with grab offset");
        drag.update(20.5f,20.5f,64,64);
        check(drag.preview.x2==20 && drag.preview.z2==20,"click near resize handle does not alter bounds");
        drag.update(24.5f,23.5f,64,64);
        check(drag.preview.x2==24 && drag.preview.z2==23 && drag.preview.x1==10,"corner drag changes both dimensions");
        drag.update(-100,-100,64,64);
        check(drag.preview.x2==10 && drag.preview.z2==10,"resize cannot invert region");
        check(!drag.begin(regions,-1,-.1f,3,.5f,true,64,64),"negative canvas coordinate is outside map");
        std::vector<tak::crt::RuleGroup> rules(2);
        rules[0].conditions.push_back({0,{}});rules[0].actions.push_back({18,{"100"}});rules[0].actions.push_back({14,{}});
        rules[1].conditions.push_back({1,{"60"}});rules[1].actions.push_back({5,{}});
        cart::RuleClipboard clipboard;int group=0,row=0;
        check(clipboard.copy(rules,group,row,cart::RuleColumn::Action),"copy action");
        check(!clipboard.paste(rules,group,row,cart::RuleColumn::Condition),"cannot paste action as condition");
        check(clipboard.paste(rules,group,row,cart::RuleColumn::Action) && row==1 && rules[0].actions[1].slot[0]=="100","paste preserves action operands");
        check(cart::moveRule(rules,group,row,cart::RuleColumn::Action,1) && row==2 && rules[0].actions[1].opcode==14,"reorder action updates selection");
        check(!cart::moveRule(rules,group,row,cart::RuleColumn::Action,1),"cannot move beyond action list");
        check(clipboard.copy(rules,0,0,cart::RuleColumn::Group,true),"copy player rules");
        std::vector<tak::crt::RuleGroup> other;group=row=-1;
        check(clipboard.paste(other,group,row,cart::RuleColumn::Group) && other.size()==2,"paste complete player rules");
        other[0].actions[0].slot[0]="200";
        check(rules[0].actions[0].slot[0]=="100","clipboard creates independent records");
        check(cart::moveRule(other,group,row,cart::RuleColumn::Group,1) && group==1 && other[0].conditions[0].opcode==1,"reorder rule groups");
        tak::mapgen::Params params;params.seed=UINT64_MAX;params.players=8;params.layout=tak::mapgen::Islands;
        auto fields=cart::generatorFields(params);std::string generatorError;
        tak::mapgen::Params parsed;
        check(cart::parseGeneratorFields(fields,parsed,generatorError),"generator fields parse");
        check(tak::mapgen::encodeMapId(params)==tak::mapgen::encodeMapId(parsed),"all generator options roundtrip exactly");
        const auto original=tak::mapgen::encodeMapId(parsed);
        tak::mapgen::Params restored;
        check(cart::restoreGeneratorRecipe(original,"A description edited by the author",restored,generatorError) && tak::mapgen::encodeMapId(restored)==original,"saved generator recipe restores independently of description");
        check(cart::restoreGeneratorRecipe("","Generator recipe: "+original+"\nAuthor notes",restored,generatorError),"legacy description recipe restores");
        check(!cart::restoreGeneratorRecipe(original+"garbage","Generator recipe: "+original,restored,generatorError) && tak::mapgen::encodeMapId(restored)==original,"invalid recipe does not fall back to defaults or a different description recipe");
        check(!cart::restoreGeneratorRecipe("","No recipe",restored,generatorError),"missing recipe gives a visible error");
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
        (*files)["kmap/recipe-test.recipe"]={original.begin(),original.end()};vfs.setMapFiles(files);
        check(cart::mapGeneratorRecipe(vfs,"kmap/recipe-test.tnt","")==original,"game-generated map recipe sidecar imports");
        check(cart::mapGeneratorRecipe(vfs,"kmap/recipe-test.tnt","embedded")=="embedded","explicit metadata recipe wins over sidecar");
        const std::string fbi="[UNITINFO] { \nUnitName=TEST;\nName=Test Unit;\nFootprintX=1;\nFootprintZ=1;\nMaxSlope=20;\nMaxWaterDepth=0;\nCanMove=1;\nMaxVelocity=1;\n }";
        (*files)["units/test.fbi"]={fbi.begin(),fbi.end()};vfs.setMapFiles(files);registry.loadDir(vfs,"units");
        check(registry.find("test")!=nullptr,"synthetic unit loaded");
        auto operandHas=[&](bool action,const tak::crt::Rule& r,const std::string& message) {
            const auto issues=cart::validateRuleOperands(action,r,{},registry);
            return std::any_of(issues.begin(),issues.end(),[&](const auto& issue){return issue.message.find(message)!=std::string::npos;});
        };
        check(operandHas(false,{1,{"12seconds"}},"whole number"),"partially parsed numeric operand rejected");
        check(operandHas(false,{1,{"999999999999999999"}},"whole number"),"numeric overflow rejected");
        check(operandHas(false,{1,{"2147483647"}},"tick range"),"game time conversion overflow diagnosed");
        check(!operandHas(true,{2,{"score"," -12 "}},"whole number"),"signed flag values remain allowed");
        check(!operandHas(true,{2,{"score","+12"}},"whole number"),"explicit positive values remain allowed");
        check(operandHas(true,{2,{"score","+-12"}},"whole number"),"double sign rejected");
        check(operandHas(false,{23,{"101"}},"probability"),"out-of-range probability warned");
        check(operandHas(true,{7,{"missing","Anywhere"}},"unknown unit type"),"rule type references validated");
        check(!operandHas(true,{7,{"TEST","Anywhere"}},"unknown unit type"),"rule type matching ignores case");
        check(operandHas(true,{13,{"Player 99","Message"}},"Player 1 through"),"invalid display player rejected");
        check(!operandHas(true,{13,{"All Players","Message"}},"Player 1 through"),"broadcast display remains valid");
        check(operandHas(true,{2,{"","1"}},"flag name is empty"),"empty flag diagnosed");
        tak::crt::Rule truncated{13,{"Player 1",std::string("a\0b",3)}};
        check(operandHas(true,truncated,"embedded NUL"),"embedded NUL cannot silently truncate CRT operand");
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
        std::vector<cart::PlacedUnit> occupants{{"TEST",0,88,88}};
        reference.spawn(registry.find("test"),88,88);
        const auto occupiedOverlay=cart::terrainOverlay(map,registry,vfs,cart::OverlayKind::Buildability,"test",occupants);
        for(int z=0;z<32;++z)for(int x=0;x<32;++x)
            check((occupiedOverlay.rgba[(z*32+x)*4+1]==210)==reference.canPlace(registry.find("test"),x*16.f+8,z*16.f+8),"occupied buildability overlay agrees with engine at every cell");
        check(occupiedOverlay.rgba[(5*32+5)*4]==235 && build.rgba[(5*32+5)*4+1]==210,"preplaced unit changes buildability");
        map.heights[12*32+12]=60;
        tak::tnt::Scenario metadata;metadata.starts={{1,12,12},{2,24,24}};
        tak::crt::Scenario scenario;scenario.regions.push_back({"Area",15,15,5,5});
        std::vector<cart::PlacedUnit> units(1);units[0].type="TEST";units[0].x=200;units[0].z=200;units[0].name="First";
        auto run=[&] {return cart::validateMap(map,metadata,scenario,units,{},registry,vfs);};
        auto has=[](const auto& issues,const std::string& text) {for(const auto& issue:issues)if(issue.message.find(text)!=std::string::npos)return true;return false;};
        check(run().empty(),"flat map, unit and reversed inclusive region validate");
        metadata.starts[1].number=1;check(has(run(),"duplicate start number"),"duplicate start numbers cannot silently replace a slot");
        metadata.starts[1].number=9;check(has(run(),"must be 1 through 8"),"start number range");metadata.starts[1].number=2;
        map.features[0]=7;check(has(run(),"Missing feature definition"),"invalid feature reference diagnosed");map.features[0]=0xffff;
        map.seaLevel=100;check(has(run(),"engine placement rejects"),"engine rejects ground unit in deep water");map.seaLevel=0;
        units.push_back(units.front());check(has(run(),"duplicate unique name"),"duplicate names detected");
        check(has(run(),"blocked by earlier preplaced units"),"preplaced occupancy diagnosed separately from terrain");
        units.back().x=400;units.back().name="Second";
        check(!has(run(),"blocked by earlier preplaced units"),"separated preplaced units remain clear");units.pop_back();
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
        const std::string monarch="[UNITINFO] {\nUnitName=araking;\nName=Test Monarch;\nSide=ARA;\nFootprintX=1;\nFootprintZ=1;\nMaxSlope=20;\nMaxWaterDepth=0;\nCanMove=1;\nMaxVelocity=1;\n}";
        const std::string stone="[UNITINFO] {\nUnitName=teststone;\nName=Test Lodestone;\nSide=ARA;\nFootprintX=2;\nFootprintZ=2;\nYardMap=SSSS;\nMaxSlope=20;\nMaxWaterDepth=0;\nMaxVelocity=0;\n}";
        const std::string deposit="[TestMana] {\ncategory=mana;\nanimating=1;\nblocking=0;\nfootprintx=1;\nfootprintz=1;\n}";
        (*files)["units/araking.fbi"]={monarch.begin(),monarch.end()};(*files)["units/teststone.fbi"]={stone.begin(),stone.end()};
        (*files)["features/aramon/testmana.tdf"]={deposit.begin(),deposit.end()};vfs.setMapFiles(files);
        tak::sim::TypeRegistry diagnostics;diagnostics.loadDir(vfs,"units");
        check(diagnostics.find("teststone")->onMana,"fixture is a lodestone");
        check(has(cart::validateMap(map,metadata,{}, {},{},diagnostics,vfs),"No recognized mana deposits"),"missing mana economy diagnosed");
        auto disconnected=map;disconnected.featureNames={"TestMana"};disconnected.features[8*32+24]=0;
        for(int z=0;z<32;++z)disconnected.heights[z*32+16]=255;
        metadata.starts={{1,4,8},{2,27,8}};
        auto connectivity=cart::validateMap(disconnected,metadata,{}, {},{},diagnostics,vfs);
        check(has(connectivity,"no ground approach route to another start") && has(connectivity,"no ground approach route to a mana deposit"),"engine connectivity diagnoses isolated starts and mana");
        check(!has(connectivity,"rejects lodestone footprint"),"flat mana deposit accepts lodestone footprint");
        tak::sim::World deposits;deposits.setTerrain(disconnected.heights,32,32,0,&disconnected.features);deposits.buildNavClasses(diagnostics);
        tak::sim::registerMapFeatures(deposits,disconnected,vfs,&diagnostics);
        check(!deposits.manaSpots().empty(),"occupancy fixture has mana spot");
        const auto [mx,mz]=deposits.manaSpots().front();
        occupants={{"TEST",0,mx,mz}};
        check(has(cart::validateMap(disconnected,metadata,{},occupants,{},diagnostics,vfs),"preplaced units currently block"),"preplaced units blocking lodestones diagnosed");
        auto atStart=occupants;atStart[0].x=metadata.starts[0].xpos*16.f;atStart[0].z=metadata.starts[0].zpos*16.f;
        check(has(cart::validateMap(disconnected,metadata,{},atStart,{},diagnostics,vfs),"Start 1: preplaced units currently block"),"preplaced units blocking monarch starts diagnosed");
        occupants[0].type="TESTSTONE";
        check(!has(cart::validateMap(disconnected,metadata,{},occupants,{},diagnostics,vfs),"preplaced units currently block"),"existing lodestone is intentional mana occupancy");
        disconnected.heights[8*32+24]=255;
        connectivity=cart::validateMap(disconnected,metadata,{}, {},{},diagnostics,vfs);
        check(has(connectivity,"rejects lodestone footprint"),"steep mana deposit fails actual engine placement");
        std::cout<<"PASS: editor engine placement and scenario validation\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
