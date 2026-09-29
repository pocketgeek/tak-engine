#include "cartographer/editor.h"
#include "cartographer/document.h"
#include "cartographer/textedit.h"
#include "hpi/hpi.h"
#include "tnt/ota.h"
#include <chrono>
#include <filesystem>
#include "crt/crt.h"
#include "tnt/mapgen.h"
#include "cartographer/font5x7.h"
#include <iostream>
#include <stdexcept>
#include <vector>

static void key(SDL_Keycode k,Uint16 mod=0) {
    SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=k;e.key.keysym.mod=mod;SDL_PushEvent(&e);
}
static void text(const std::string& value) {
    // SDL text events hold short UTF-8 segments; these test strings are ASCII.
    for(size_t i=0;i<value.size();i+=20) {
        SDL_Event e{};e.type=SDL_TEXTINPUT;const auto part=value.substr(i,20);SDL_strlcpy(e.text.text,part.c_str(),sizeof(e.text.text));SDL_PushEvent(&e);
    }
}
static int generationWorkflow(const char* data) {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("tak-editor-generation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    SDL_setenv("XDG_CONFIG_HOME",root.string().c_str(),1);SDL_setenv("XDG_DATA_HOME",root.string().c_str(),1);
    std::vector<std::string> args={"cartographer","Ulasem Arena","--data",data,"--out",root.string()};
    std::vector<char*> raw;for(auto& arg:args)raw.push_back(arg.data());
    int stage=0;std::string failure;const auto begun=std::chrono::steady_clock::now();
    const auto result=cart::runEditor(int(raw.size()),raw.data(),[&](SDL_Window* window,SDL_Renderer* renderer,int) {
        const std::string title=SDL_GetWindowTitle(window);
        auto check=[&](bool ok,const char* why){if(!ok && failure.empty())failure=why;};
        const auto elapsed=std::chrono::steady_clock::now()-begun;
        if(elapsed>std::chrono::seconds(50)) {failure="generation workflow timed out";key(SDLK_ESCAPE);SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);return;}
        auto randomButton=[&] {
            float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);
            SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;
            e.button.x=int((w/sx/2-36)*sx);e.button.y=int((h/sy/2+93)*sy);SDL_PushEvent(&e);
            e.type=SDL_MOUSEBUTTONUP;SDL_PushEvent(&e);
        };
        switch(stage) {
        case 0: key(SDLK_n,KMOD_CTRL);++stage;break;
        case 1: text("Generated preview");randomButton();++stage;break;
        case 2: text("123456");key(SDLK_RETURN);++stage;break;
        case 3:
            check(title.find("Ulasem Arena")!=std::string::npos && !title.ends_with(" *"),"generation changed original before acceptance");
            if(title.find("[Generated preview]")!=std::string::npos) {key(SDLK_ESCAPE);++stage;}break;
        case 4: check(title=="Cartographer -- Ulasem Arena","discard preserves original document");key(SDLK_n,KMOD_CTRL);++stage;break;
        case 5: text("Generated preview");randomButton();++stage;break;
        case 6: text("123456");key(SDLK_RETURN);key(SDLK_ESCAPE);++stage;break;
        case 7:
            check(title.find("[Generated preview]")==std::string::npos,"cancelled generation must not present preview");
            if(title.find("[Generating]")==std::string::npos) {
                check(title=="Cartographer -- Ulasem Arena","cancelled generation preserves original");key(SDLK_n,KMOD_CTRL);++stage;
            }break;
        case 8: text("Generated preview");randomButton();++stage;break;
        case 9: text("123456");key(SDLK_RETURN);++stage;break;
        case 10: if(title.find("[Generated preview]")!=std::string::npos) {key(SDLK_RETURN);++stage;}break;
        case 11: check(title=="Cartographer -- Generated preview *","accept adopts unsaved generated map");key(SDLK_s,KMOD_CTRL);++stage;break;
        case 12: check(!title.ends_with(" *"),"generated map saved");key(SDLK_RETURN);++stage;break;
        case 13: {SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);++stage;break;}
        }
        SDL_Delay(1);
    });
    try {
        if(result || !failure.empty())throw std::runtime_error(failure.empty()?"editor failed":failure);
        if(stage!=14)throw std::runtime_error("generation workflow exited early");
        tak::hpi::Archive archive(root/"Generated preview.kmp");bool found=false;
        for(const auto& entry:archive.entries())if(entry.path.ends_with(".ota")) {
            const auto bytes=archive.read(entry);const auto metadata=tak::tnt::Scenario::parse(std::string(bytes.begin(),bytes.end()));
            if(metadata.starts.size()!=2 || !metadata.missionDescription.starts_with("Generator recipe: "))throw std::runtime_error("generated metadata missing");
            const auto params=tak::mapgen::decodeMapId(metadata.missionDescription.substr(18));
            if(params.seed!=123456 || params.players!=2)throw std::runtime_error("generated recipe differs from chosen options");
            found=true;
        }
        if(!found)throw std::runtime_error("generated metadata absent");
        fs::remove_all(root);std::cout<<"PASS: background generation, preview/discard, accept and saved recipe\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<"; files: "<<root<<'\n';return 1;}
}
static int overlayWorkflow(const char* data) {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("tak-editor-overlay-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    SDL_setenv("XDG_CONFIG_HOME",root.string().c_str(),1);SDL_setenv("XDG_DATA_HOME",root.string().c_str(),1);
    std::vector<std::string> args={"cartographer","Ulasem Arena","--data",data,"--out",root.string()};
    std::vector<char*> raw;for(auto& arg:args)raw.push_back(arg.data());
    int stage=0;bool failed=false;auto began=std::chrono::steady_clock::now();
    const int result=cart::runEditor(int(raw.size()),raw.data(),[&](SDL_Window* window,SDL_Renderer* renderer,int) {
        auto click=[&](int x,int y) {
            float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);
            SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;e.button.x=int(x*sx);e.button.y=int(y*sy);SDL_PushEvent(&e);
            e.type=SDL_MOUSEBUTTONUP;SDL_PushEvent(&e);
        };
        const std::string title=SDL_GetWindowTitle(window);
        if(std::chrono::steady_clock::now()-began>std::chrono::seconds(30)) {failed=true;key(SDLK_ESCAPE);SDL_Event q{};q.type=SDL_QUIT;SDL_PushEvent(&q);return;}
        if(stage==0) {click(250,10);click(250,212);++stage;}
        else if(stage==1 && title.find("[Terrain overlay]")!=std::string::npos) {failed|=title.ends_with(" *");click(250,10);click(250,252);++stage;}
        else if(stage==2) {failed|=title!="Cartographer -- Ulasem Arena";SDL_Event q{};q.type=SDL_QUIT;SDL_PushEvent(&q);++stage;}
        SDL_Delay(1);
    });
    fs::remove_all(root);
    if(result || failed || stage!=3) {std::cerr<<"overlay menu workflow failed\n";return 1;}
    std::cout<<"PASS: background water overlay, unchanged document and hide control\n";return 0;
}
static int regionWorkflow(const char* data) {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("tak-editor-regions-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    SDL_setenv("XDG_CONFIG_HOME",root.string().c_str(),1);SDL_setenv("XDG_DATA_HOME",root.string().c_str(),1);
    std::vector<std::string> args={"cartographer","Ulasem Arena","--data",data,"--out",root.string()};
    std::vector<char*> raw;for(auto& arg:args)raw.push_back(arg.data());
    bool failed=false;int lastFrame=0;
    const int result=cart::runEditor(int(raw.size()),raw.data(),[&](SDL_Window* window,SDL_Renderer* renderer,int frame) {
        lastFrame=frame;float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);
        auto mouse=[&](Uint32 type,int x,int y) {
            SDL_Event e{};e.type=type;
            if(type==SDL_MOUSEMOTION) {e.motion.state=SDL_BUTTON_LMASK;e.motion.x=int(x*sx);e.motion.y=int(y*sy);}
            else {e.button.button=SDL_BUTTON_LEFT;e.button.x=int(x*sx);e.button.y=int(y*sy);}
            SDL_PushEvent(&e);
        };
        auto click=[&](int x,int y) {mouse(SDL_MOUSEBUTTONDOWN,x,y);mouse(SDL_MOUSEBUTTONUP,x,y);};
        auto drag=[&](int x,int y,int toX,int toY) {mouse(SDL_MOUSEBUTTONDOWN,x,y);mouse(SDL_MOUSEMOTION,toX,toY);mouse(SDL_MOUSEBUTTONUP,toX,toY);};
        const bool dirty=std::string(SDL_GetWindowTitle(window)).ends_with(" *");
        switch(frame) {
        case 0: key(SDLK_1);click(700,32);break;
        case 1: drag(361,205,521,365);break; // cells 10,10 through 20,20
        case 2: failed|=!dirty;drag(441,285,473,317);break; // move two cells
        case 3: drag(569,413,601,429);break; // resize right/bottom by two/one cells
        case 4: click(601,429);key(SDLK_z,KMOD_CTRL);break; // no-motion click must not consume undo
        case 5: key(SDLK_y,KMOD_CTRL);click(480,330);key(SDLK_RETURN);break;
        case 6: text("Canvas region");key(SDLK_RETURN);break;
        case 7: mouse(SDL_MOUSEBUTTONDOWN,393,333);mouse(SDL_MOUSEMOTION,280,333);key(SDLK_ESCAPE);mouse(SDL_MOUSEBUTTONUP,280,333);break;
        case 8: key(SDLK_s,KMOD_CTRL);break;
        case 9: failed|=dirty;key(SDLK_RETURN);key(SDLK_o,KMOD_CTRL);break;
        case 10: text((root/"Ulasem Arena.kmp").string());key(SDLK_RETURN);break;
        case 11: failed|=dirty;key(SDLK_s,KMOD_CTRL);break; // serialize the reopened document too
        case 12: failed|=dirty;key(SDLK_RETURN);break;
        case 13: {SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);break;}
        default: if(frame>20) {failed=true;key(SDLK_ESCAPE);key(SDLK_RETURN);}
        }
    });
    try {
        if(result || failed || lastFrame>20)throw std::runtime_error("region canvas UI workflow failed");
        tak::hpi::Archive archive(root/"Ulasem Arena.kmp");bool found=false;
        for(const auto& entry:archive.entries())if(entry.path.ends_with(".crt")) {
            const auto scenario=tak::crt::parse(archive.read(entry));
            for(const auto& r:scenario.regions)if(r.name=="Canvas region") {
                if(r.x1!=12 || r.z1!=12 || r.x2!=24 || r.z2!=23)throw std::runtime_error("region bounds differ after draw/move/resize/cancel/undo/reopen");
                found=true;
            }
        }
        if(!found)throw std::runtime_error("canvas region not saved");
        fs::remove_all(root);std::cout<<"PASS: region canvas drawing, movement, handles, cancellation, undo/redo and reopen\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<"; files: "<<root<<'\n';return 1;}
}
static int validationWorkflow(const char* data,const char* mapName="Ulasem Arena") {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("tak-editor-validation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    SDL_setenv("XDG_CONFIG_HOME",root.string().c_str(),1);SDL_setenv("XDG_DATA_HOME",root.string().c_str(),1);
    auto vfs=tak::hpi::mountRetailRoot(data);const auto path=tak::hpi::findMap(vfs,mapName);
    auto map=tak::tnt::Map::load(vfs.read(path),path);
    std::cout<<"Validation fixture: "<<mapName<<" ("<<map.width/32<<" x "<<map.height/32<<")\n";
    map.features[size_t(map.height/2)*map.width+map.width/2]=uint16_t(map.featureNames.size());
    map.featureNames.push_back("MissingEditorTestFeature");
    tak::tnt::Scenario metadata;metadata.kingdom="aramon";metadata.starts={{1,10,10},{2,20,20}};
    std::string error;
    if(!cart::writeDocumentBundle(root/"Validation.kmp",cart::documentFiles(map,metadata,{}, {},{},"Validation"),error))throw std::runtime_error(error);
    std::vector<std::string> args={"cartographer","Ulasem Arena","--data",data,"--out",root.string()};
    std::vector<char*> raw;for(auto& arg:args)raw.push_back(arg.data());
    int stage=0;bool failed=false;auto began=std::chrono::steady_clock::now();
    const int result=cart::runEditor(int(raw.size()),raw.data(),[&](SDL_Window* window,SDL_Renderer* renderer,int) {
        auto click=[&](int x,int y) {
            float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);
            SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;e.button.x=int(x*sx);e.button.y=int(y*sy);SDL_PushEvent(&e);
            e.type=SDL_MOUSEBUTTONUP;SDL_PushEvent(&e);
        };
        const std::string title=SDL_GetWindowTitle(window);failed|=title.ends_with(" *");
        if(std::chrono::steady_clock::now()-began>std::chrono::seconds(40)) {failed=true;key(SDLK_ESCAPE);SDL_Event q{};q.type=SDL_QUIT;SDL_PushEvent(&q);return;}
        switch(stage) {
        case 0: key(SDLK_o,KMOD_CTRL);++stage;break;
        case 1: text((root/"Validation.kmp").string());key(SDLK_RETURN);++stage;break;
        case 2: key(SDLK_c);key(SDLK_ESCAPE);++stage;break;
        case 3: if(title.find("[Checking map]")==std::string::npos) {
            failed|=title.find("[Validation results]")!=std::string::npos;key(SDLK_c);++stage;
        } break;
        case 4: if(title.find("[Validation results]")!=std::string::npos) {
            float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);
            click(int(w/sx/2),int(h/sy/2-150));++stage;
        } break;
        case 5: failed|=title!="Cartographer -- Validation";click(320,10);click(330,172);++stage;break;
        case 6: failed|=title.find("[Validation results]")==std::string::npos;key(SDLK_ESCAPE);++stage;break;
        case 7: {SDL_Event q{};q.type=SDL_QUIT;SDL_PushEvent(&q);++stage;break;}
        }
        SDL_Delay(1);
    });
    fs::remove_all(root);
    if(result || failed || stage!=8) {std::cerr<<"map validation workflow failed\n";return 1;}
    std::cout<<"PASS: background map validation, cancel, located result click and results reopening\n";return 0;
}
static int recoveryWorkflow(const char* data,const char* folder,bool restore) {
    namespace fs=std::filesystem;const fs::path root=folder;
    fs::create_directories(root);
    SDL_setenv("XDG_CONFIG_HOME",folder,1);SDL_setenv("XDG_DATA_HOME",folder,1);
    std::vector<std::string> args={"cartographer"};if(!restore)args.push_back("Ulasem Arena");
    args.insert(args.end(),{"--data",data,"--out",folder});
    std::vector<char*> raw;for(auto& arg:args)raw.push_back(arg.data());
    uint64_t now=0;cart::EditorHooks hooks;hooks.recoveryClock=[&]{return now;};hooks.recoveryChoice=[] {return 1;};
    const auto begun=std::chrono::steady_clock::now();std::string failure;
    const int result=cart::runEditor(int(raw.size()),raw.data(),[&](SDL_Window* window,SDL_Renderer*,int frame) {
        if(std::chrono::steady_clock::now()-begun>std::chrono::seconds(20))std::_Exit(3);
        const bool dirty=std::string(SDL_GetWindowTitle(window)).ends_with(" *");
        if(!restore) {
            if(frame==0)key(SDLK_p);
            if(frame==1) {text("Recovered unsaved map");key(SDLK_RETURN);}
            if(frame==2)now=61000;
            if(frame>2)for(const auto& entry:fs::recursive_directory_iterator(root)) {
                if(!entry.is_regular_file() || !entry.path().filename().string().starts_with("recovery-") || entry.path().extension()!=".kmp")continue;
                tak::hpi::Archive archive(entry.path());
                for(const auto& member:archive.entries())if(member.path.ends_with(".ota")) {
                    const auto bytes=archive.read(member);
                    const auto metadata=tak::tnt::Scenario::parse(std::string(bytes.begin(),bytes.end()));
                    if(metadata.missionName=="Recovered unsaved map")std::_Exit(0); // deliberately bypass editor cleanup
                }
            }
        } else {
            if(frame==0) {if(!dirty)failure="recovered document is not marked unsaved";key(SDLK_s,KMOD_CTRL);}
            if(frame==1) {if(dirty)failure="recovered map could not be saved";key(SDLK_RETURN);}
            if(frame==2) {SDL_Event q{};q.type=SDL_QUIT;SDL_PushEvent(&q);}
        }
        SDL_Delay(1);
    },hooks);
    if(result || !failure.empty()) {std::cerr<<failure<<'\n';return 1;}
    if(restore) {
        tak::hpi::Archive archive(root/"Ulasem Arena.kmp");bool found=false;
        for(const auto& member:archive.entries())if(member.path.ends_with(".ota")) {
            const auto bytes=archive.read(member);const auto metadata=tak::tnt::Scenario::parse(std::string(bytes.begin(),bytes.end()));
            found=metadata.missionName=="Recovered unsaved map";
        }
        if(!found)return 2;
        for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.path().filename().string().starts_with("recovery-"))return 4;
    }
    return 0;
}
int main(int argc,char** argv) {
    if(argc==5 && std::string(argv[2])=="recovery")return recoveryWorkflow(argv[1],argv[3],std::string(argv[4])=="restore");
    if((argc==3 || argc==4) && std::string(argv[2])=="validation")return validationWorkflow(argv[1],argc==4?argv[3]:"Ulasem Arena");
    if(argc==3 && std::string(argv[2])=="regions")return regionWorkflow(argv[1]);
    if(argc==3 && std::string(argv[2])=="overlay")return overlayWorkflow(argv[1]);
    if(argc==3 && std::string(argv[2])=="generation")return generationWorkflow(argv[1]);
    if(argc!=2)return 2;
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("tak-editor-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    SDL_setenv("XDG_CONFIG_HOME",root.string().c_str(),1);SDL_setenv("XDG_DATA_HOME",root.string().c_str(),1);
    std::vector<std::string> args={"cartographer","Ulasem Arena","--data",argv[1],"--out",root.string()};
    std::vector<char*> raw;for(auto& arg:args)raw.push_back(arg.data());
    std::string failure;
    auto check=[&](bool value,const char* message){if(!value && failure.empty())failure=message;};
    const int result=cart::runEditor(int(raw.size()),raw.data(),[&](SDL_Window* window,SDL_Renderer* renderer,int frame) {
        const bool dirty=std::string(SDL_GetWindowTitle(window)).ends_with(" *");
        auto click=[&](int x,int y) {
            float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);
            SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;
            e.button.x=int(x*sx);e.button.y=int(y*sy);SDL_PushEvent(&e);
            e.type=SDL_MOUSEBUTTONUP;SDL_PushEvent(&e);
        };
        switch(frame) {
        case 0: key(SDLK_p);break;
        case 1: text("Editor integration test");key(SDLK_TAB);text("Saved after undo and redo");key(SDLK_RETURN,KMOD_SHIFT);text("Second description line");key(SDLK_RETURN);break;
        case 2: check(dirty,"property editing marks document dirty");key(SDLK_z,KMOD_CTRL);break;
        case 3: check(!dirty,"undo restores saved revision");key(SDLK_y,KMOD_CTRL);break;
        case 4: check(dirty,"redo restores edited revision");key(SDLK_s,KMOD_CTRL);break;
        case 5: check(!dirty,"successful save clears dirty marker");key(SDLK_RETURN);key(SDLK_o,KMOD_CTRL);break;
        case 6: text((root/"Ulasem Arena.kmp").string());key(SDLK_RETURN);break;
        case 7: check(!dirty,"reopening saved map is clean");key(SDLK_p);break;
        case 8: text("Do not apply this edit");key(SDLK_ESCAPE);break;
        case 9: check(!dirty,"cancelled properties do not mutate document");key(SDLK_s,KMOD_CTRL|KMOD_SHIFT);break;
        case 10: text("Copied map");key(SDLK_RETURN);break;
        case 11: check(!dirty,"Save As marks renamed document saved");key(SDLK_RETURN);key(SDLK_o,KMOD_CTRL);break;
        case 12: text("Map which does not exist");key(SDLK_RETURN);break;
        case 13: check(std::string(SDL_GetWindowTitle(window)).find("Copied map")!=std::string::npos,"failed open preserves current document");key(SDLK_RETURN);break;
        case 14: click(320,10);click(330,152);break; // Scenario > Regions
        case 15: key(SDLK_n,KMOD_CTRL);break;
        case 16: text("UI test region");key(SDLK_TAB);text("10");key(SDLK_TAB);text("11");key(SDLK_TAB);text("20");key(SDLK_TAB);text("21");key(SDLK_RETURN);break;
        case 17: check(dirty,"region creation marks document dirty");key(SDLK_ESCAPE);key(SDLK_s,KMOD_CTRL);break;
        case 18: check(!dirty,"region save succeeds");key(SDLK_RETURN);break;
        case 19: key(SDLK_t);break;
        case 20: {
            float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);
            click(int(w/sx/2-354),int(h/sy/2+240));break;
        }
        case 21: key(SDLK_d,KMOD_CTRL);break;
        case 22: key(SDLK_ESCAPE);key(SDLK_s,KMOD_CTRL);break;
        case 23: check(!dirty,"duplicated script rules saved");key(SDLK_RETURN);break;
        case 24: key(SDLK_o,KMOD_CTRL);break;
        case 25: text("ulasem");break;
        case 26: {
            float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);
            click(int(w/sx/2),int(h/sy/2-130));break;
        }
        case 27: check(std::string(SDL_GetWindowTitle(window)).find("Ulasem Arena")!=std::string::npos,"searchable Open chooses recent map");
            {SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);break;}
        default: if(frame>30) {failure="editor did not exit";key(SDLK_ESCAPE);key(SDLK_RETURN);}
        }
    });
    try {
        if(result!=0)throw std::runtime_error("editor returned failure");
        if(!failure.empty())throw std::runtime_error(failure);
        for(const auto& file:{"Ulasem Arena.kmp","Copied map.kmp"}) {
            tak::hpi::Archive archive(root/file);bool found=false;
            for(const auto& entry:archive.entries())if(entry.path.ends_with(".ota")) {
                const auto bytes=archive.read(entry);const auto metadata=tak::tnt::Scenario::parse(std::string(bytes.begin(),bytes.end()));
                if(metadata.missionName!="Editor integration test" || metadata.missionDescription!="Saved after undo and redo\nSecond description line")throw std::runtime_error("saved metadata differs after UI workflow");
                found=true;
            }
            if(!found)throw std::runtime_error("saved bundle lacks metadata");
            if(std::string(file)=="Copied map.kmp") {
                bool regionFound=false;
                for(const auto& entry:archive.entries())if(entry.path.ends_with(".crt")) {
                    const auto scenario=tak::crt::parse(archive.read(entry));
                    if(scenario.players.empty() || scenario.players[0].size()!=2)throw std::runtime_error("script duplicate workflow did not save two rules");
                    for(const auto& r:scenario.regions)if(r.name=="UI test region" && r.x1==10 && r.z1==11 && r.x2==20 && r.z2==21)regionFound=true;
                }
                if(!regionFound)throw std::runtime_error("region UI workflow did not save expected CRT bounds");
            }
        }
        // UTF-8 editing must erase a character, not leave partial byte sequences.
        cart::TextEdit edit;std::string unicode="A\xc3\xa9\xe6\xb0\xb4";edit.focus(unicode,false);
        SDL_Event back{};back.type=SDL_KEYDOWN;back.key.keysym.sym=SDLK_BACKSPACE;
        edit.input(back,unicode);if(unicode!="A\xc3\xa9")throw std::runtime_error("UTF-8 deletion");
        edit.input(back,unicode);if(unicode!="A")throw std::runtime_error("UTF-8 deletion twice");
        std::string multiline="ab\xc3\xa9\nx\nabcdef";edit.focus(multiline,false);
        auto navigate=[&](SDL_Keycode key,Uint16 mod=0) {SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=key;e.key.keysym.mod=mod;edit.input(e,multiline,false,true,20);};
        navigate(SDLK_HOME,KMOD_CTRL);navigate(SDLK_RIGHT);navigate(SDLK_RIGHT);
        navigate(SDLK_DOWN);if(edit.caret!=6)throw std::runtime_error("vertical movement clamps short line");
        navigate(SDLK_DOWN);if(edit.caret!=9)throw std::runtime_error("vertical movement retains desired column");
        navigate(SDLK_UP);navigate(SDLK_UP,KMOD_SHIFT);
        if(edit.caret!=2 || edit.anchor!=6)throw std::runtime_error("multiline shifted navigation preserves selection anchor");
        navigate(SDLK_END);if(edit.caret!=4)throw std::runtime_error("End targets current line");
        edit.click(multiline,20,2,3);if(edit.caret!=10)throw std::runtime_error("multiline mouse caret position");
        const auto wrapped=cart::TextEdit::lines("one two three",5);
        if(wrapped.size()!=3 || wrapped[0].end!=4 || wrapped[1].begin!=4)throw std::runtime_error("word wrapping");
        if(cart::textWidth("A\xc3\xa9\xe6\xb0\xb4",1)!=18)throw std::runtime_error("Unicode display width counts code points");
        fs::remove_all(root);std::cout<<"PASS: editor properties, undo/redo, save, reopen, Save As, cancel, failed open, UTF-8 editing\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<"; files: "<<root<<'\n';return 1;}
}
