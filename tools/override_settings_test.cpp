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
        using PathMode=tak::sim::PathfindingMode;
        if(s.gameCreate.pathfindingMode!=PathMode::Retail)
            throw std::runtime_error("new create preferences must default to Retail");
        s.gameCreate.pathfindingMode=PathMode::Flowfield;
        s.gameCreate.overrides=0;
        s.uiScale=0.75f;s.scorecardScale=1.75f;
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        const auto loaded=tak::loadSettings();
        if(loaded.hostOverridePacks!=s.hostOverridePacks || loaded.cosmeticOverridePacks!=s.cosmeticOverridePacks || loaded.gameCreate.overrides!=0)
            throw std::runtime_error("pack selections did not survive save/load while off");
        if(loaded.uiScale!=0.75f || loaded.scorecardScale!=1.75f)
            throw std::runtime_error("independent scorecard scale did not persist");
        if(loaded.gameCreate.pathfindingMode!=PathMode::Flowfield)
            throw std::runtime_error("Flowfield create preference did not persist");
        auto modeChanged=s;modeChanged.gameCreate.pathfindingMode=PathMode::Retail;
        if(modeChanged==s)throw std::runtime_error("pathfinding preference changes not detected");
        std::ofstream(path)<<"gameCreate.crusades = true\ngameCreate.doubleSight = true\ngameCreate.unitCap = 2000\n";
        if(tak::loadSettings().gameCreate.pathfindingMode!=PathMode::Retail)
            throw std::runtime_error("legacy create preferences must remain Retail");
        for(const char* value:{"", "0", "-1", "2", "256", "flowfield", "1oops"}) {
            std::ofstream(path)<<"gameCreate.pathfindingMode = "<<value<<"\n";
            if(tak::loadSettings().gameCreate.pathfindingMode!=PathMode::Retail)
                throw std::runtime_error("unknown pathfinding preference must fall back to Retail");
        }
        if(!tak::saveSettings(modeChanged) || tak::loadSettings().gameCreate.pathfindingMode!=PathMode::Retail)
            throw std::runtime_error("Retail create preference did not persist");
        auto changed=s;changed.scorecardScale=1.0f;
        if(changed==s)throw std::runtime_error("scorecard scale changes not detected");
        std::ofstream(path)<<"uiScale = 2\n";
        if(tak::loadSettings().scorecardScale!=1.0f)throw std::runtime_error("scorecard inherits HUD scale");
        std::ofstream(path)<<"scorecardScale = 9\n";
        if(tak::loadSettings().scorecardScale!=2.0f)throw std::runtime_error("scorecard upper limit");
        std::ofstream(path)<<"scorecardScale = 0\n";
        if(tak::loadSettings().scorecardScale!=0.75f)throw std::runtime_error("scorecard lower limit");
        auto disabled=[](const tak::Settings& v) {
            if(v.terrainAA || v.modelAA || v.bilinear || v.smoothArt || v.videoDeblock)
                throw std::runtime_error("disabled graphics options enabled by saved preferences");
        };
        disabled(tak::Settings{});
        if(!tak::Settings{}.treeSway)throw std::runtime_error("tree sway must default on");
        for(int terrain:{0,2,4})for(int model:{0,2,4,8,16}) {
            std::ofstream(path)<<"antiAlias = 4\nterrainAA = "<<terrain<<"\nmodelAA = "<<model
                <<"\nbilinear = true\nsmoothArt = on\nvideoDeblock = 1\ntreeSway = 0\nvsync = 0\n";
            const auto loaded=tak::loadSettings();
            disabled(loaded);
            if(loaded.treeSway)throw std::runtime_error("tree sway off preference ignored");
            if(loaded.vsync)throw std::runtime_error("unrelated preference ignored");
        }
        s.terrainAA=4;s.modelAA=16;s.bilinear=s.smoothArt=s.videoDeblock=true;s.treeSway=false;
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        disabled(tak::loadSettings());
        if(tak::loadSettings().treeSway)throw std::runtime_error("tree sway off did not persist");
        s.treeSway=true;
        if(!tak::saveSettings(s) || !tak::loadSettings().treeSway)throw std::runtime_error("tree sway on did not persist");
        std::ifstream saved(path);
        const std::string contents((std::istreambuf_iterator<char>(saved)),{});
        for(const char* key:{"antiAlias =", "terrainAA =", "modelAA =", "bilinear =", "smoothArt =", "videoDeblock ="})
            if(contents.find(key)!=std::string::npos)throw std::runtime_error("disabled preference still saved");
        fs::remove_all(root);std::cout<<"PASS: independent host/guest selections persist while Off\n";return 0;
    }catch(const std::exception& e){fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
