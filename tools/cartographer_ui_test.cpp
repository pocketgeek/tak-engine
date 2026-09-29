#include <fstream>
#include "cartographer/editor.h"
#include "cartographer/document.h"
#include "cartographer/newmap.h"
#include "cartographer/thumbnails.h"
#include "cartographer/sections.h"
#include "terrain/terrain.h"
#include "cartographer/textedit.h"
#include "hpi/hpi.h"
#include "tnt/ota.h"
#include <chrono>
#include <filesystem>
#include "crt/crt.h"
#include "tnt/mapgen.h"
#include "cartographer/font5x7.h"
#include "util/png.h"
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
    std::vector<uint8_t> originalTerrain;
    auto readTerrain=[&] {tak::hpi::Archive archive(root/"Generated preview.kmp");for(const auto& entry:archive.entries())if(entry.path.ends_with(".tnt"))return archive.read(entry);return std::vector<uint8_t>{};};
    auto useSidecarRecipe=[&] {
        std::vector<tak::hpi::PackFile> files;
        {
            tak::hpi::Archive archive(root/"Generated preview.kmp");
            for(const auto& entry:archive.entries())if(!entry.isDirectory) {
                auto bytes=archive.read(entry);
                if(entry.path.ends_with(".ota")) {
                    auto metadata=tak::tnt::Scenario::parse(std::string(bytes.begin(),bytes.end()));
                    files.push_back({entry.path.substr(0,entry.path.size()-4)+".recipe",{metadata.generatorRecipe.begin(),metadata.generatorRecipe.end()}});
                    metadata.generatorRecipe.clear();const auto ota=metadata.write();bytes={ota.begin(),ota.end()};
                }
                files.push_back({entry.path,std::move(bytes)});
            }
        }
        std::string error;
        if(!cart::writeDocumentFiles(root,{{"Generated preview.kmp",tak::hpi::pack(files)}},error))throw std::runtime_error(error);
    };
    int stage=0;bool correctedSize=false;std::string failure;const auto begun=std::chrono::steady_clock::now();
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
        auto click=[&](int x,int y) {
            float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;
            e.button.x=int(x*sx);e.button.y=int(y*sy);SDL_PushEvent(&e);e.type=SDL_MOUSEBUTTONUP;SDL_PushEvent(&e);
        };
        auto recipeMenu=[&] {click(320,10);click(330,192);};
        switch(stage) {
        case 0: key(SDLK_n,KMOD_CTRL);++stage;break;
        case 1: text("bad/name");randomButton();key(SDLK_a,KMOD_CTRL);text("Generated preview");randomButton();++stage;break;
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
        case 12: check(!title.ends_with(" *"),"generated map saved");originalTerrain=readTerrain();key(SDLK_RETURN);key(SDLK_p);++stage;break;
        case 13: key(SDLK_TAB);text("Authored description");key(SDLK_RETURN);key(SDLK_s,KMOD_CTRL);++stage;break;
        case 14: key(SDLK_RETURN);key(SDLK_n,KMOD_CTRL);++stage;break; // change in-memory generator seed
        case 15: useSidecarRecipe();key(SDLK_ESCAPE);key(SDLK_o,KMOD_CTRL);++stage;break;
        case 16: text((root/"Generated preview.kmp").string());key(SDLK_RETURN);++stage;break;
        case 17: recipeMenu();++stage;break;
        case 18: key(SDLK_RETURN);++stage;break;
        case 19: if(title.find("[Generated preview]")!=std::string::npos) {key(SDLK_RETURN);++stage;}break;
        case 20: key(SDLK_s,KMOD_CTRL);++stage;break;
        case 21: check(readTerrain()==originalTerrain,"restored recipe reproduces saved terrain exactly");key(SDLK_RETURN);recipeMenu();++stage;break;
        case 22: key(SDLK_RETURN);++stage;break;
        case 23: if(title.find("[Generated preview]")!=std::string::npos) {
            float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);
            click(int(w/sx/2-166),int(h/sy/2+212));++stage;
        }break;
        case 24: text("789012");for(int i=0;i<8;++i)key(SDLK_TAB);text("16");key(SDLK_TAB);text("0");key(SDLK_RETURN);++stage;break;
        case 25:
            if(!correctedSize) {
                if(const char* path=SDL_getenv("TAK_EDITOR_TEST_CAPTURE")) {
                    int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);std::vector<uint8_t> pixels(size_t(w)*h*4);
                    if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),w*4)==0)tak::png::write(path,w,h,pixels);
                }
                check(title=="Cartographer -- Generated preview","invalid generator dimensions leave the map untouched");
                key(SDLK_a,KMOD_CTRL);text("12");key(SDLK_RETURN);correctedSize=true;
            } else if(title.find("[Generated preview]")!=std::string::npos) {key(SDLK_RETURN);++stage;}
            break;
        case 26: check(title.ends_with(" *"),"regeneration is an unsaved edit");key(SDLK_z,KMOD_CTRL);++stage;break;
        case 27: check(!title.ends_with(" *"),"undo regeneration restores saved revision");key(SDLK_y,KMOD_CTRL);++stage;break;
        case 28: check(title.ends_with(" *"),"redo regeneration restores new seed");key(SDLK_s,KMOD_CTRL);++stage;break;
        case 29: key(SDLK_RETURN);key(SDLK_z,KMOD_CTRL);++stage;break;
        case 30: key(SDLK_s,KMOD_CTRL);++stage;break;
        case 31: key(SDLK_RETURN);++stage;break;
        case 32: {SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);++stage;break;}
        }
        SDL_Delay(1);
    });
    try {
        if(result || !failure.empty())throw std::runtime_error(failure.empty()?"editor failed":failure);
        if(stage!=33)throw std::runtime_error("generation workflow exited early");
        tak::hpi::Archive archive(root/"Generated preview.kmp");bool found=false;
        for(const auto& entry:archive.entries())if(entry.path.ends_with(".ota")) {
            const auto bytes=archive.read(entry);const auto metadata=tak::tnt::Scenario::parse(std::string(bytes.begin(),bytes.end()));
            if(metadata.starts.size()!=2 || metadata.missionDescription!="Authored description")throw std::runtime_error("generated metadata missing");
            const auto params=tak::mapgen::decodeMapId(metadata.generatorRecipe);
            if(params.seed!=123456 || params.players!=2)throw std::runtime_error("generated recipe differs from chosen options");
            found=true;
        }
        if(!found)throw std::runtime_error("generated metadata absent");
        if(readTerrain()!=originalTerrain)throw std::runtime_error("undo regeneration did not preserve saved terrain");
        tak::hpi::Archive backup(root/"Generated preview.kmp.bak");bool variant=false;
        for(const auto& entry:backup.entries())if(entry.path.ends_with(".ota")) {
            const auto bytes=backup.read(entry);const auto metadata=tak::tnt::Scenario::parse(std::string(bytes.begin(),bytes.end()));
            const auto params=tak::mapgen::decodeMapId(metadata.generatorRecipe);
            variant=params.seed==789012 && params.widthCells==16*32 && params.heightCells==12*32;
        }
        if(!variant)throw std::runtime_error("preview Settings did not regenerate with the edited seed");
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
static int fontWorkflow() {
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 2;
    auto* surface=SDL_CreateRGBSurfaceWithFormat(0,128,64,32,SDL_PIXELFORMAT_RGBA32);
    auto* renderer=surface?SDL_CreateSoftwareRenderer(surface):nullptr;
    if(!renderer) {if(surface)SDL_FreeSurface(surface);SDL_Quit();return 2;}
    int result=0;
    {
        cart::EditorFont font(renderer);
        auto pixels=[&](const std::string& text) {
            SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
            cart::drawText(renderer,text,20,20,2,255,255,255);SDL_RenderPresent(renderer);
            std::vector<uint8_t> rgba(128*64*4);SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,rgba.data(),128*4);return rgba;
        };
        const auto missing=pixels("�"),latin=pixels("é"),greek=pixels("Ω"),cyrillic=pixels("Ж");
        if(latin==missing || greek==missing || cyrillic==missing || pixels("a")==pixels("A") || latin==pixels("e"))result=1;
        bool antialiased=false;for(size_t i=0;i<latin.size();i+=4)antialiased|=latin[i]>0 && latin[i]<255;
        if(!antialiased || cart::textWidth("éΩЖ",1)!=18)result=1;
    }
    SDL_DestroyRenderer(renderer);SDL_FreeSurface(surface);SDL_Quit();
    if(!result)std::cout<<"PASS: embedded mixed-case Unicode glyphs, antialiasing and fixed caret metrics\n";return result;
}
static int thumbnailWorkflow(const char* data) {
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 2;
    int result=0;
    {
        auto assets=tak::hpi::mountRetailRoot(data);cart::SectionLibrary sections;sections.scan(assets,"zhon");
        if(sections.list().size()<5) {SDL_Quit();return 2;}
        auto* surface=SDL_CreateRGBSurfaceWithFormat(0,512,512,32,SDL_PIXELFORMAT_RGBA32);
        auto* renderer=surface?SDL_CreateSoftwareRenderer(surface):nullptr;
        if(!renderer) {if(surface)SDL_FreeSurface(surface);SDL_Quit();return 2;}
        {
            cart::Thumbnails thumbnails(renderer,assets,2);
            auto awaitTexture=[&](const std::string& path) {
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
                SDL_Texture* texture=nullptr;
                while(!(texture=thumbnails.get(path))) {
                    if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("thumbnail worker timeout");
                    SDL_Delay(1);
                }
                return texture;
            };
            try {
                const auto& path=sections.list().front().path;
                const auto* source=sections.load(assets,path);tak::terrain::Compositor compositor(assets);
                const auto reference=compositor.renderMap(*source);
                auto* texture=awaitTexture(path);int w=0,h=0;SDL_QueryTexture(texture,nullptr,nullptr,&w,&h);
                if(w!=reference.width || h!=reference.height)throw std::runtime_error("thumbnail lost source resolution");
                if(w>512 || h>512)throw std::runtime_error("thumbnail fixture exceeds test surface");
                SDL_Rect dst{0,0,w,h};SDL_RenderCopy(renderer,texture,nullptr,&dst);SDL_RenderPresent(renderer);
                std::vector<uint8_t> rgba(size_t(w)*h*4);
                if(SDL_RenderReadPixels(renderer,&dst,SDL_PIXELFORMAT_RGBA32,rgba.data(),w*4)!=0 || rgba!=reference.rgba)
                    throw std::runtime_error("asynchronous thumbnail differs from source compositor");
                for(int i=1;i<4;++i)awaitTexture(sections.list()[i].path);
                if(thumbnails.entries()>2)throw std::runtime_error("thumbnail cache exceeds entry budget");
                thumbnails.get(sections.list()[4].path);thumbnails.reset();
                if(thumbnails.loading() || thumbnails.entries())throw std::runtime_error("thumbnail reset retained work");
                awaitTexture(path);
            } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';result=1;}
        }
        SDL_DestroyRenderer(renderer);SDL_FreeSurface(surface);
    }
    SDL_Quit();if(!result)std::cout<<"PASS: background thumbnail raster, cache eviction and reset with work pending\n";return result;
}
static int minimapWorkflow(const char* data,const char* mapName="Ulasem Arena") {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("tak-editor-minimap-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);SDL_setenv("XDG_CONFIG_HOME",root.string().c_str(),1);SDL_setenv("XDG_DATA_HOME",root.string().c_str(),1);
    auto vfs=tak::hpi::mountRetailRoot(data);const auto path=tak::hpi::findMap(vfs,mapName);
    const auto original=tak::tnt::Map::load(vfs.read(path),path);tak::terrain::Compositor compositor(vfs);
    auto cancelled=std::make_shared<std::atomic_bool>(true);
    const auto otaBytes=vfs.read(path.substr(0,path.size()-4)+".ota");
    auto world=tak::tnt::Scenario::parse(std::string(otaBytes.begin(),otaBytes.end())).kingdom;
    std::transform(world.begin(),world.end(),world.begin(),[](unsigned char c){return char(std::tolower(c));});
    const auto palette=cart::loadWorldPalette(vfs,world);
    if(!cart::minimapPreview(original,compositor,palette,cancelled).rgba.empty())throw std::runtime_error("cancelled minimap produced pixels");
    const auto originalPreview=cart::minimapPreview(original,compositor,palette).rgba;
    std::vector<std::string> args={"cartographer",mapName,"--data",data,"--out",root.string()};
    std::vector<char*> raw;for(auto& arg:args)raw.push_back(arg.data());
    cart::EditorHooks hooks;int updates=0;std::vector<uint8_t> pixels,paintedPixels;
    hooks.minimapUpdated=[&](const auto& rgba) {++updates;pixels=rgba;};
    int stage=0;bool failed=false;const auto began=std::chrono::steady_clock::now();
    const int result=cart::runEditor(int(raw.size()),raw.data(),[&](SDL_Window* window,SDL_Renderer* renderer,int) {
        if(std::chrono::steady_clock::now()-began>std::chrono::seconds(30))std::_Exit(3);
        const bool dirty=std::string(SDL_GetWindowTitle(window)).ends_with(" *");
        auto click=[&](int x,int y) {
            float sx,sy;SDL_RenderGetScale(renderer,&sx,&sy);SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;e.button.x=int(x*sx);e.button.y=int(y*sy);SDL_PushEvent(&e);
            e.type=SDL_MOUSEBUTTONUP;SDL_PushEvent(&e);
        };
        switch(stage) {
        case 0:key(SDLK_1);++stage;break;
        case 1:click(500,300);++stage;break;
        case 2:if(updates==1) {failed|=!dirty;paintedPixels=pixels;key(SDLK_z,KMOD_CTRL);++stage;}break;
        case 3:if(updates==2) {failed|=dirty || pixels!=originalPreview;key(SDLK_y,KMOD_CTRL);++stage;}break;
        case 4:if(updates==3) {failed|=!dirty || pixels!=paintedPixels;key(SDLK_s,KMOD_CTRL);++stage;}break;
        case 5:failed|=dirty;key(SDLK_RETURN);++stage;break;
        case 6:{SDL_Event q{};q.type=SDL_QUIT;SDL_PushEvent(&q);++stage;break;}
        }
        SDL_Delay(1);
    },hooks);
    try {
        if(result || failed || stage!=7)throw std::runtime_error("live minimap workflow failed");
        tak::hpi::Archive archive(root/(std::string(mapName)+".kmp"));bool found=false;
        for(const auto& entry:archive.entries())if(entry.path.ends_with(".tnt")) {
            const auto saved=tak::tnt::Map::load(archive.read(entry),entry.path);
            std::vector<uint8_t> expected(saved.minimap.size()*4);
            for(size_t i=0;i<saved.minimap.size();++i)std::copy(palette.rgba[saved.minimap[i]],palette.rgba[saved.minimap[i]]+4,expected.begin()+i*4);
            if(expected!=paintedPixels)throw std::runtime_error("background preview differs from saved minimap");
            found=true;
        }
        if(!found)throw std::runtime_error("saved minimap missing");
        fs::remove_all(root);std::cout<<"PASS: live background minimap, undo/redo, unchanged dirty state and saved image parity\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<"; files: "<<root<<'\n';return 1;}
}
static int featureWorkflow(const char* data) {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("tak-editor-features-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    SDL_setenv("XDG_CONFIG_HOME",root.string().c_str(),1);SDL_setenv("XDG_DATA_HOME",root.string().c_str(),1);
    auto vfs=tak::hpi::mountRetailRoot(data);const auto path=tak::hpi::findMap(vfs,"Ulasem Arena");
    auto map=tak::tnt::Map::load(vfs.read(path),path);
    if(map.featureNames.empty())return 2;
    map.features.assign(map.features.size(),0xFFFF);map.features[10*map.width+10]=0;map.features[10*map.width+12]=0;
    tak::tnt::Scenario metadata;metadata.kingdom="zhon";
    std::string error;
    if(!cart::writeDocumentBundle(root/"Features.kmp",cart::documentFiles(map,metadata,{}, {},{},"Features"),error))throw std::runtime_error(error);
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
        auto drag=[&](int x,int y,int tx,int ty) {mouse(SDL_MOUSEBUTTONDOWN,x,y);mouse(SDL_MOUSEMOTION,tx,ty);mouse(SDL_MOUSEBUTTONUP,tx,ty);};
        const bool dirty=std::string(SDL_GetWindowTitle(window)).ends_with(" *");
        switch(frame) {
        case 0:key(SDLK_o,KMOD_CTRL);break;
        case 1:text((root/"Features.kmp").string());key(SDLK_RETURN);break;
        case 2:key(SDLK_1);click(180,32);click(480,32);break;
        case 3:drag(345,189,425,237);break; // select both features
        case 4:click(250,10);click(250,312);break; // hide selected features
        case 5:failed|=dirty;key(SDLK_DELETE);key(SDLK_x,KMOD_CTRL);break; // hidden selection cannot be deleted/cut
        case 6:failed|=dirty;click(180,32);break; // selecting the feature tool reveals it again
        case 7:drag(361,205,393,253);break; // move two columns, three rows
        case 8:failed|=!dirty;key(SDLK_z,KMOD_CTRL);break;
        case 9:failed|=dirty;key(SDLK_y,KMOD_CTRL);break;
        case 10:failed|=!dirty;drag(377,237,457,285);break;
        case 11:key(SDLK_DELETE);break;
        case 12:key(SDLK_z,KMOD_CTRL);break;
        case 13:key(SDLK_s,KMOD_CTRL);break;
        case 14:failed|=dirty;key(SDLK_RETURN);key(SDLK_o,KMOD_CTRL);break;
        case 15:text((root/"Features.kmp").string());key(SDLK_RETURN);break;
        case 16:failed|=dirty;key(SDLK_s,KMOD_CTRL);break;
        case 17:key(SDLK_RETURN);break;
        case 18:{SDL_Event q{};q.type=SDL_QUIT;SDL_PushEvent(&q);break;}
        }
    });
    try {
        if(result || failed || lastFrame<18 || lastFrame>19)throw std::runtime_error("feature UI workflow failed at frame "+std::to_string(lastFrame));
        tak::hpi::Archive archive(root/"Features.kmp");bool found=false;
        for(const auto& entry:archive.entries())if(entry.path.ends_with(".tnt")) {
            const auto saved=tak::tnt::Map::load(archive.read(entry),entry.path);
            auto expected=map.features;expected[10*map.width+10]=expected[10*map.width+12]=0xFFFF;
            expected[13*map.width+12]=expected[13*map.width+14]=0;
            if(saved.features!=expected)throw std::runtime_error("feature group positions not preserved");
            found=true;
        }
        if(!found)throw std::runtime_error("saved feature terrain missing");
        fs::remove_all(root);std::cout<<"PASS: feature box selection, layer hiding, protected hidden selection, group drag, delete, undo/redo, save and reopen\n";return 0;
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
static int recoveryWorkflow(const char* data,const char* folder,const std::string& phase) {
    namespace fs=std::filesystem;const fs::path root=folder;
    fs::create_directories(root);
    SDL_setenv("XDG_CONFIG_HOME",folder,1);SDL_setenv("XDG_DATA_HOME",folder,1);
    const bool restore=phase=="restore",probe=phase=="probe";
    const auto destination=root/"original-destination";fs::create_directories(destination);
    std::vector<std::string> args={"cartographer"};if(!restore && !probe)args.push_back("Ulasem Arena");
    args.insert(args.end(),{"--data",data});
    if(!restore)args.insert(args.end(),{"--out",destination.string()});
    std::vector<char*> raw;for(auto& arg:args)raw.push_back(arg.data());
    uint64_t now=0;cart::EditorHooks hooks;hooks.recoveryClock=[&]{return now;};int prompts=0;hooks.recoveryChoice=[&] {++prompts;return probe?0:1;};
    const auto begun=std::chrono::steady_clock::now();std::string failure;
    const int result=cart::runEditor(int(raw.size()),raw.data(),[&](SDL_Window* window,SDL_Renderer*,int frame) {
        if(std::chrono::steady_clock::now()-begun>std::chrono::seconds(20))std::_Exit(3);
        const bool dirty=std::string(SDL_GetWindowTitle(window)).ends_with(" *");
        if(probe) {if(frame==0) {SDL_Event q{};q.type=SDL_QUIT;SDL_PushEvent(&q);}else key(SDLK_RETURN);}
        else if(!restore) {
            if(frame==0)key(SDLK_p);
            if(frame==1) {text("Recovered unsaved map");key(SDLK_RETURN);}
            if(frame==2)now=61000;
            if(frame>2)for(const auto& entry:fs::recursive_directory_iterator(root)) {
                if(!entry.is_regular_file() || !entry.path().filename().string().starts_with("recovery-") || entry.path().extension()!=".kmp")continue;
                tak::hpi::Archive archive(entry.path());
                for(const auto& member:archive.entries())if(member.path.ends_with(".ota")) {
                    const auto bytes=archive.read(member);
                    const auto metadata=tak::tnt::Scenario::parse(std::string(bytes.begin(),bytes.end()));
                    if(metadata.missionName=="Recovered unsaved map") {
                        if(phase=="write")std::_Exit(0); // deliberately bypass editor cleanup
                        std::ofstream(root/"writer-ready")<<"ready";
                    }
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
    if(probe && prompts)return 5;
    if(restore) {
        if(prompts!=1)return 6;
        tak::hpi::Archive archive(destination/"Ulasem Arena.kmp");bool found=false;
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
    if(argc==5 && std::string(argv[2])=="recovery")return recoveryWorkflow(argv[1],argv[3],argv[4]);
    if((argc==3 || argc==4) && std::string(argv[2])=="validation")return validationWorkflow(argv[1],argc==4?argv[3]:"Ulasem Arena");
    if(argc==2 && std::string(argv[1])=="font")return fontWorkflow();
    if(argc==3 && std::string(argv[2])=="thumbnails")return thumbnailWorkflow(argv[1]);
    if((argc==3 || argc==4) && std::string(argv[2])=="minimap")return minimapWorkflow(argv[1],argc==4?argv[3]:"Ulasem Arena");
    if(argc==3 && std::string(argv[2])=="features")return featureWorkflow(argv[1]);
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
        case 16: text("UI test region");key(SDLK_TAB);text("10");key(SDLK_TAB);text("11");key(SDLK_TAB);text("20");key(SDLK_TAB);text("99999");key(SDLK_RETURN);key(SDLK_a,KMOD_CTRL);text("21");key(SDLK_RETURN);break;
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
            key(SDLK_TAB);key(SDLK_TAB);click(500,300);key(SDLK_RETURN);break;
        case 28: text("9");key(SDLK_RETURN);key(SDLK_a,KMOD_CTRL);text("8");
            for(int i=0;i<6;++i)key(SDLK_TAB);
            text("UI neutral unit");key(SDLK_RETURN);break;
        case 29: check(dirty,"corrected unit properties apply");key(SDLK_s,KMOD_CTRL);break;
        case 30: check(!dirty,"unit properties saved");key(SDLK_RETURN);break;
        case 31:
            {SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);break;}
        default: if(frame>34) {failure="editor did not exit";key(SDLK_ESCAPE);key(SDLK_RETURN);}
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
            if(std::string(file)=="Ulasem Arena.kmp") {
                bool neutral=false;
                for(const auto& entry:archive.entries())if(entry.path.ends_with(".crt"))
                    for(const auto& unit:cart::toPlaced(tak::crt::parse(archive.read(entry))))
                        if(unit.name=="UI neutral unit" && unit.player==8)neutral=true;
                if(!neutral)throw std::runtime_error("corrected owner and retained unit fields not saved");
            }
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
