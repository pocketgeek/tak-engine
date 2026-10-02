#include "client/settings.h"
#include "net/crypto.h"
#include <SDL.h>
#include <filesystem>
#include <fstream>
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
        for(int terrain:{0,2,4})for(int model:{0,2,4,8,16}) {
            s.terrainAA=terrain;s.modelAA=model;
            if(!tak::saveSettings(s))throw std::runtime_error("AA save failed");
            const auto roundtrip=tak::loadSettings();
            if(roundtrip.terrainAA!=terrain || roundtrip.modelAA!=model)throw std::runtime_error("AA persistence changed independent choices");
        }
        auto parse=[&](const char* text){std::ofstream(path)<<text;return tak::loadSettings();};
        for(int old:{0,2,4}) {
            std::ofstream(path)<<"antiAlias = "<<old<<"\n";
            const auto migrated=tak::loadSettings();
            if(migrated.terrainAA!=old || migrated.modelAA!=old)throw std::runtime_error("legacy AA migration");
        }
        for(const char* text:{"antiAlias = 4\nterrainAA = 0\nmodelAA = 16\n", "modelAA = 16\nterrainAA = 0\nantiAlias = 4\n"}) {
            auto migrated=parse(text);
            if(migrated.terrainAA!=0 || migrated.modelAA!=16)throw std::runtime_error("legacy overwrote explicit AA settings");
        }
        auto clamped=parse("terrainAA = 16\nmodelAA = 9\n");
        if(clamped.terrainAA!=4 || clamped.modelAA!=8)throw std::runtime_error("unsupported AA step accepted");
        fs::remove_all(root);std::cout<<"PASS: independent host/guest selections persist while Off\n";return 0;
    }catch(const std::exception& e){fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
