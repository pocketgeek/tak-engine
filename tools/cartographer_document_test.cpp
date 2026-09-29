#include "cartographer/recovery.h"
#include "cartographer/document.h"
#include "cartographer/history.h"
#include "cartographer/selection.h"
#include "cartographer/featureselection.h"
#include "cartographer/sections.h"
#include "cartographer/preferences.h"
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
        auto lease=cart::RecoveryFile::create(root);const auto snapshot=lease->path();
        std::ofstream(snapshot)<<"snapshot";
        check(!cart::RecoveryFile::claimNewest(root),"live session cannot be recovered");
        lease.reset();
        lease=cart::RecoveryFile::claimNewest(root);
        check(lease && lease->path()==snapshot,"abandoned session can be recovered");
        check(!cart::RecoveryFile::claimNewest(root),"claimed recovery stays exclusive");
        std::string recoveryError;check(lease->discard(recoveryError),"discard claimed recovery");lease.reset();
        check(!fs::exists(snapshot.parent_path()),"empty session directory cleaned");
        const auto legacy=root/"recovery-old.kmp";std::ofstream(legacy)<<"legacy";
        lease=cart::RecoveryFile::claimNewest(root);check(lease && lease->path()==legacy,"legacy recovery supported");
        check(lease->discard(recoveryError),"discard legacy recovery");lease.reset();
        const auto destination=root/fs::u8path("été maps");
        const auto info=cart::recoveryInfo("Recovered map",destination);
        const auto decoded=cart::readRecoveryInfo(info.data);
        check(decoded && decoded->name=="Recovered map" && fs::u8path(decoded->directory)==destination,"recovery destination UTF-8 roundtrip");
        check(!cart::readRecoveryInfo({1,2,3}),"bad recovery metadata rejected");
        cart::EditorPreferences preferences;preferences.guideSeen=true;preferences.width=1440;preferences.height=900;preferences.scalePercent=125;preferences.showFeatures=false;preferences.showUnits=false;preferences.showStarts=false;preferences.showRegions=false;preferences.showGrid=true;
        for(int i=0;i<20;++i)preferences.remember("Map "+std::to_string(i));
        preferences.remember("Carte été \"quote\".kmp");preferences.remember("Map 19");
        check(preferences.recent.size()==12 && preferences.recent[0]=="Map 19","recent maps bounded and deduplicated");
        std::string prefsError;check(cart::saveEditorPreferences(root,preferences,prefsError),prefsError.c_str());
        const auto loadedPreferences=cart::loadEditorPreferences(root);
        check(loadedPreferences.guideSeen && loadedPreferences.width==1440 && loadedPreferences.height==900 && loadedPreferences.scalePercent==125 && loadedPreferences.recent==preferences.recent,"window, scale and Unicode recent paths roundtrip");
        check(!loadedPreferences.showFeatures && !loadedPreferences.showUnits && !loadedPreferences.showStarts && !loadedPreferences.showRegions && loadedPreferences.showGrid,"layer preferences roundtrip");
        tak::tnt::Map map;map.width=map.height=32;map.blocksX=map.blocksY=16;
        map.heights.resize(1024,60);map.features.resize(1024,0xffff);
        map.tileKeys.resize(256);map.tileCols.resize(256);map.tileRows.resize(256);
        auto featureMap=map;featureMap.featureNames={"Tree","Rock"};featureMap.features[33]=0;featureMap.features[34]=1;
        featureMap.features[35]=0xFFFB;
        cart::FeatureSelection features;features.box(featureMap,0,0,5,5,false);
        check(features.cells==std::set<int>{33,34},"feature box excludes terrain markers");
        const auto originalFeatures=featureMap.features;
        check(!features.move(featureMap,1,0) && featureMap.features==originalFeatures,"group cannot overwrite terrain marker");
        check(features.move(featureMap,0,1) && featureMap.features[65]==0 && featureMap.features[66]==1 && featureMap.features[33]==0xFFFF,"group move preserves types and spacing");
        check(features.move(featureMap,1,0) && featureMap.features[66]==0 && featureMap.features[67]==1 && featureMap.features[65]==0xFFFF,"overlapping selected cells move without losing a feature");
        features.copy(featureMap);check(features.remove(featureMap),"delete selected features");
        check(featureMap.features[35]==0xFFFB,"feature deletion preserves terrain markers");
        featureMap.featureNames={"Rock","Tree"};
        check(features.paste(featureMap,5,5) && featureMap.features[165]==1 && featureMap.features[166]==0,"clipboard resolves feature names across map tables");
        const auto beforeRejected=featureMap.features;
        check(!features.paste(featureMap,5,5) && featureMap.features==beforeRejected,"paste collision is atomic");
        check(!features.paste(featureMap,31,31) && featureMap.features==beforeRejected,"off-map paste is atomic");
        auto painted=map,prefab=map;
        painted.featureNames={"Tree"};painted.features[0]=0;painted.features[1]=0xFFFB;
        prefab.featureNames={"Rock"};prefab.features[0]=0xFFFF;prefab.features[1]=0;prefab.features[2]=0xFFFB;
        prefab.heights.assign(1024,90);prefab.tileKeys.assign(256,42);
        check(cart::stampSection(painted,prefab,0,0,{false}),"protected terrain stamp succeeds");
        check(painted.features[0]==0 && painted.featureNames==std::vector<std::string>{"Tree"},"protected stamp keeps existing object and adds no prefab names");
        check(painted.features[1]==0xFFFF && painted.features[2]==0xFFFB,"protected stamp updates terrain markers without adding objects");
        check(painted.heights[0]==90 && painted.tileKeys[0]==42,"terrain art and heights change together");
        check(cart::stampSection(painted,prefab,0,0),"normal terrain stamp succeeds");
        check(painted.features[0]==0xFFFF && painted.featureNames[painted.features[1]]=="Rock","normal stamp replaces/remaps objects");
        auto untouched=painted.features;
        check(!cart::stampSection(painted,prefab,16,16,{false}) && painted.features==untouched,"off-map protected stamp does nothing");
        tak::tnt::Scenario meta;meta.kingdom="zhon";
        meta.generatorRecipe="~gen1~test-metadata";
        check(tak::tnt::Scenario::parse(meta.write()).generatorRecipe==meta.generatorRecipe,"generator metadata survives OTA roundtrip");
        for(const std::string text : {"First line\nSecond line", "\r\n\t", "  Leading and trailing ; ", "https://example.test/{map}", "été 水 ; test", "}\n[Other]\n{\nkey=value;\n}"}) {
            auto rich=meta;rich.missionDescription=text;rich.missionName=text;
            const auto encoded=rich.write();const auto decoded=tak::tnt::Scenario::parse(encoded);
            check(decoded.missionName==text && decoded.missionDescription==text,"OTA free text roundtrip without interpreting syntax");
            const auto extension=encoded.find("[TAKText]");
            const auto legacy=tak::tnt::Scenario::parse(encoded.substr(0,extension));
            check(legacy.kingdom=="zhon" && legacy.missionDescription.find('\n')==std::string::npos,"legacy metadata remains parseable with one-line description");
        }
        check(meta.write().find("[TAKText]")==std::string::npos,"ordinary OTA output needs no text extension");
        check(tak::tnt::Scenario::parse(meta.write()+"[TAKText]\n{\nmissiondescription=zz;\n}\n").missionDescription.empty(),"invalid text extension ignored");
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
        {
            auto recovery=cart::RecoveryFile::create(root);const auto file=recovery->path();
            check(cart::writeDocumentBundle(file,files,error),error.c_str());
            const auto recovered=cart::readRecoverySnapshot(file);
            check(!recovered.fromBackup && recovered.mapPath.ends_with("review.tnt"),"valid recovery is preferred");
            fs::copy_file(file,fs::path(file.string()+".bak"));
            std::ofstream(file,std::ios::binary|std::ios::trunc)<<"HAPI";
            check(cart::readRecoverySnapshot(file).fromBackup,"truncated archive falls back to previous snapshot");
            check(read(file)==std::vector<uint8_t>({'H','A','P','I'}),"fallback preserves damaged primary for diagnosis");
            fs::remove(file);recovery.reset();
            recovery=cart::RecoveryFile::claimNewest(root);
            check(recovery && recovery->path()==file && cart::readRecoverySnapshot(file).fromBackup,"backup-only abandoned session is recoverable");
            check(recovery->discard(error),error.c_str());
            auto broken=files;
            for(auto& member:broken)if(member.path.ends_with(".crt"))member.data={1,2,3};
            check(cart::writeDocumentBundle(file,broken,error),error.c_str());
            bool rejected=false;try {cart::readRecoverySnapshot(file);}catch(const std::exception&) {rejected=true;}
            check(rejected && fs::exists(file),"damaged scenario rejected without deleting snapshot");
            broken=files;
            for(auto& member:broken)if(member.path.ends_with(".tnt"))member.data={1,2,3};
            check(cart::writeDocumentBundle(file,broken,error),error.c_str());
            rejected=false;try {cart::readRecoverySnapshot(file);}catch(const std::exception&) {rejected=true;}
            check(rejected && fs::exists(file.string()+".bak"),"damaged terrain and scenario generations remain available for diagnosis");
            check(recovery->discard(error),error.c_str());
            const auto backup=fs::path(file.string()+".bak");fs::create_directory(backup);std::ofstream(backup/"unrelated")<<"keep";
            check(!recovery->discard(error) && !error.empty() && fs::exists(backup/"unrelated"),"cleanup failure preserves unrelated contents and reports error");
            fs::remove_all(backup);
        }
        tak::hpi::Archive openArchive(root/"Review.kmp"); // editor may retain a mapped archive while saving over it
        const auto oldEntry=*std::find_if(openArchive.entries().begin(),openArchive.entries().end(),[](const auto& entry){return !entry.isDirectory;});const auto oldBytes=openArchive.read(oldEntry);
        scenario.players[0].clear();
        files=cart::documentFiles(map,meta,scenario,{}, {},"Review");
        check(cart::writeDocumentBundle(root/"Review.kmp",files,error),error.c_str());
        check(read(root/"Review.kmp.bak")==original,"previous bundle retained");
        check(openArchive.read(oldEntry)==oldBytes,"open archive remains a stable snapshot after replacement");
        check(cart::writeDocumentFiles(root,files,error),error.c_str());
        check(tak::crt::parse(read(root/"Review.crt")).players[0].empty(),"deletion replaces stale scenario");
        check(tak::crt::parse(read(root/"Review.crt.bak")).players[0].size()==1,"loose backup preserved");
        const auto unicodeDir=root/std::filesystem::path(u8"Cartes été");fs::create_directory(unicodeDir);
        const auto unicodePath=unicodeDir/std::filesystem::path(u8"Île.kmp");
        check(cart::writeDocumentBundle(unicodePath,files,error),error.c_str());
        check(fs::file_size(unicodePath)>0,"UTF-8 destination directory and filename save");
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
