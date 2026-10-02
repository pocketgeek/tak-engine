#include "client/settings.h"
#include "net/crypto.h"
#include <SDL.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
int main() {
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("tak-settings-"+tak::crypto::toHex(tak::crypto::randomVec(8)));
    try {
        fs::create_directories(root);
        SDL_setenv("XDG_DATA_HOME",root.string().c_str(),1);
        const auto path=fs::path(tak::settingsPath());
        if(path.string().find(root.string()+"/")!=0)throw std::runtime_error("settings not isolated");
        tak::Settings s;
        s.hostOverridePacks={"Sound Pack #1","Textures", "Accents-é"};
        s.cosmeticOverridePacks={"My local art"};
        s.gameCreate.overrides=0;
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        const auto loaded=tak::loadSettings();
        if(loaded.hostOverridePacks!=s.hostOverridePacks || loaded.cosmeticOverridePacks!=s.cosmeticOverridePacks || loaded.gameCreate.overrides!=0)
            throw std::runtime_error("pack selections did not survive save/load while off");
        fs::remove_all(root);std::cout<<"PASS: independent host/guest selections persist while Off\n";return 0;
    }catch(const std::exception& e){fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
