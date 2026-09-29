#include "cartographer/document.h"
#include "cartographer/history.h"
#include "cartographer/selection.h"
#include <random>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
static std::vector<uint8_t> read(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};
}
int main() {
    namespace fs=std::filesystem;
    auto root=fs::temp_directory_path()/("tak-cartographer-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        tak::tnt::Map map;map.width=map.height=32;map.blocksX=map.blocksY=16;
        map.heights.resize(1024,60);map.features.resize(1024,0xffff);
        map.tileKeys.resize(256);map.tileCols.resize(256);map.tileRows.resize(256);
        tak::tnt::Scenario meta;meta.kingdom="zhon";
        tak::crt::Scenario scenario;scenario.players.resize(9);
        tak::crt::RuleGroup group;group.conditions.push_back({});group.actions.push_back({});
        scenario.players[0].push_back(group);
        auto files=cart::documentFiles(map,meta,scenario,{}, {},"Review");
        std::string error;
        check(cart::writeDocumentFiles(root,files,error),error.c_str());
        auto crt=tak::crt::parse(read(root/"Review.crt"));
        check(crt.players.size()==9 && crt.players[0].size()==1,"rule-only scenario saved");
        auto ota=read(root/"Review.ota");
        check(tak::tnt::Scenario::parse(std::string(ota.begin(),ota.end())).hasScenario,"scenario metadata follows content");
        check(!meta.hasScenario,"saving does not mutate document metadata");
        check(cart::writeDocumentBundle(root/"Review.kmp",files,error),error.c_str());
        const auto original=read(root/"Review.kmp");
        scenario.players[0].clear();
        files=cart::documentFiles(map,meta,scenario,{}, {},"Review");
        check(cart::writeDocumentBundle(root/"Review.kmp",files,error),error.c_str());
        check(read(root/"Review.kmp.bak")==original,"previous bundle retained");
        check(cart::writeDocumentFiles(root,files,error),error.c_str());
        check(tak::crt::parse(read(root/"Review.crt")).players[0].empty(),"deletion replaces stale scenario");
        check(tak::crt::parse(read(root/"Review.crt.bak")).players[0].size()==1,"loose backup preserved");
        const auto saved=read(root/"Review.tnt");
        fs::create_directory(root/"blocked");
        check(!cart::writeDocumentFiles(root,{{"Review.tnt",{1,2,3}},{"blocked",{4}}},error),"invalid destination must fail");
        check(read(root/"Review.tnt")==saved,"failed export preserves previous file");
        check(!cart::writeDocumentFiles(root,{{"../escape",{1}}},error),"reject traversal");
        check(!cart::writeDocumentFiles(root,{{"duplicate",{1}},{"duplicate",{2}}},error),"reject duplicate members");
        check(!cart::writeDocumentBundle(root/"missing"/"Review.kmp",files,error),"missing folder fails");
        check(cart::validDocumentName("My Map") && !cart::validDocumentName("bad/name"),"document names");
        std::vector<cart::PlacedUnit> selectionUnits(3);
        selectionUnits[0].x=20;selectionUnits[0].z=30;selectionUnits[0].name="Named";
        selectionUnits[0].health=73;selectionUnits[0].player=4;
        selectionUnits[1].x=60;selectionUnits[1].z=50;
        selectionUnits[2].x=90;selectionUnits[2].z=90;
        cart::UnitSelection selection;
        selection.box(selectionUnits,65,55,15,25,false);
        check(selection.indices==std::set<int>({0,1}),"reversed box selects contained units");
        selection.beginDrag(selectionUnits,20,30);
        check(selection.drag(selectionUnits,-100,500,100,100),"group drag moves");
        check(selectionUnits[0].x==8 && selectionUnits[1].x==48 && selectionUnits[1].z==92,
              "group drag clamps together and preserves spacing");
        selection.copy(selectionUnits);
        check(selection.paste(selectionUnits,95,95,100,100),"paste group near edge");
        check(selectionUnits.size()==5 && selectionUnits[3].name.empty() && selectionUnits[3].health==73 && selectionUnits[3].player==4,
              "paste preserves properties without duplicating trigger names");
        check(selectionUnits[4].x-selectionUnits[3].x==40 && selectionUnits[4].z-selectionUnits[3].z==20,
              "paste preserves relative placement");
        check(selection.remove(selectionUnits) && selectionUnits.size()==3 && selection.indices.empty(),"delete selected group only");
        check(!selection.paste(selectionUnits,0,0,30,30),"reject group too large for map");
        selectionUnits[1].x=200;selection.indices={0,1};selection.beginDrag(selectionUnits,0,0);
        check(!selection.drag(selectionUnits,50,50,100,100),"oversize selection cannot distort group");
        cart::History history;
        auto initial=cart::historyState(map,meta,scenario,{}, {},"Review");
        history.reset(initial,true);
        auto changed=initial;changed.terrain[500]^=7;changed.metadata+="changed";
        changed.scenario.resize(changed.scenario.size()+500,17);
        check(history.commit(changed) && history.dirty(),"history captures an edit");
        check(history.retainedBytes()<initial.terrain.size()*2,"small edits retain deltas");
        check(history.undo() && *history.redo()==changed,"redo restores all fields");
        check(*history.undo()==initial && !history.dirty(),"undo returns to saved state");
        check(history.redo()!=nullptr,"redo exists");history.markSaved();
        check(history.undo()!=nullptr && history.dirty(),"undo away from saved revision is dirty");
        auto branch=initial;branch.name="Branch";history.commit(branch);
        check(!history.canRedo() && history.dirty(),"branch invalidates redo and stays dirty");
        std::mt19937 random(47);std::vector<cart::HistoryState> expected{branch};
        for(int n=0;n<80;++n) {
            auto next=expected.back();next.terrain.resize(200+random()%900,0);
            for(int j=0;j<30;++j) next.terrain[random()%next.terrain.size()]=uint8_t(random());
            next.scenario.resize(random()%350,uint8_t(n));next.name=std::to_string(n);
            history.commit(next);expected.push_back(next);
        }
        for(size_t i=expected.size()-1;i>0;--i)check(*history.undo()==expected[i-1],"variable-length delta undo");
        for(size_t i=1;i<expected.size();++i)check(*history.redo()==expected[i],"variable-length delta redo");
        cart::History bounded(2500);bounded.reset(initial,true);
        for(int i=0;i<20;++i) {auto state=initial;state.name=std::to_string(i);bounded.commit(state);}
        check(bounded.retainedBytes()<2500,"history respects memory budget");
        fs::remove_all(root);
        std::cout<<"PASS: rule-only saves, metadata, stale deletion, backups, failed writes, names\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';fs::remove_all(root);return 1;}
}
