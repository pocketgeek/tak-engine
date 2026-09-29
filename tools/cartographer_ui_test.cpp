#include "cartographer/editor.h"
#include "cartographer/textedit.h"
#include "hpi/hpi.h"
#include "tnt/ota.h"
#include <chrono>
#include <filesystem>
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
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("tak-editor-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    SDL_setenv("XDG_CONFIG_HOME",root.string().c_str(),1);SDL_setenv("XDG_DATA_HOME",root.string().c_str(),1);
    std::vector<std::string> args={"cartographer","Ulasem Arena","--data",argv[1],"--out",root.string()};
    std::vector<char*> raw;for(auto& arg:args)raw.push_back(arg.data());
    std::string failure;
    auto check=[&](bool value,const char* message){if(!value && failure.empty())failure=message;};
    const int result=cart::runEditor(int(raw.size()),raw.data(),[&](SDL_Window* window,SDL_Renderer*,int frame) {
        const bool dirty=std::string(SDL_GetWindowTitle(window)).ends_with(" *");
        switch(frame) {
        case 0: key(SDLK_p);break;
        case 1: text("Editor integration test");key(SDLK_TAB);text("Saved after undo and redo");key(SDLK_RETURN);break;
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
        case 14: {SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);break;}
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
                if(metadata.missionName!="Editor integration test" || metadata.missionDescription!="Saved after undo and redo")throw std::runtime_error("saved metadata differs after UI workflow");
                found=true;
            }
            if(!found)throw std::runtime_error("saved bundle lacks metadata");
        }
        // UTF-8 editing must erase a character, not leave partial byte sequences.
        cart::TextEdit edit;std::string unicode="A\xc3\xa9\xe6\xb0\xb4";edit.focus(unicode,false);
        SDL_Event back{};back.type=SDL_KEYDOWN;back.key.keysym.sym=SDLK_BACKSPACE;
        edit.input(back,unicode);if(unicode!="A\xc3\xa9")throw std::runtime_error("UTF-8 deletion");
        edit.input(back,unicode);if(unicode!="A")throw std::runtime_error("UTF-8 deletion twice");
        fs::remove_all(root);std::cout<<"PASS: editor properties, undo/redo, save, reopen, Save As, cancel, failed open, UTF-8 editing\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<"; files: "<<root<<'\n';return 1;}
}
