#include "client/settings.h"
#include "net/crypto.h"
#include <SDL.h>
#include <algorithm>
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
        static_assert(uint8_t(PathMode::Retail)==0&&uint8_t(PathMode::Flowfield)==1&&uint8_t(PathMode::Cooperative)==2&&
                      uint8_t(PathMode::RetailPlus)==3&&uint8_t(PathMode::Legion)==4);
        for(auto mode:{PathMode::Retail,PathMode::Flowfield,PathMode::Cooperative,PathMode::RetailPlus,PathMode::Legion}) {
            auto next=tak::sim::cyclePathfindingMode(mode,1);
            auto around=next;for(int i=0;i<4;++i)around=tak::sim::cyclePathfindingMode(around,1);
            if(tak::sim::cyclePathfindingMode(next,-1)!=mode || around!=mode)
                throw std::runtime_error("pathfinding choices do not cycle in both directions");
        }
        // Display order: Retail, Retail+, Flowfield, Cooperative, Legion (and back to Retail).
        if(tak::sim::cyclePathfindingMode(PathMode::Retail,-1)!=PathMode::Legion ||
           tak::sim::cyclePathfindingMode(PathMode::Legion,-1)!=PathMode::Cooperative ||
           tak::sim::cyclePathfindingMode(PathMode::Retail,1)!=PathMode::RetailPlus ||
           tak::sim::cyclePathfindingMode(PathMode::RetailPlus,1)!=PathMode::Flowfield ||
           tak::sim::cyclePathfindingMode(PathMode::Flowfield,1)!=PathMode::Cooperative ||
           tak::sim::cyclePathfindingMode(PathMode::Cooperative,1)!=PathMode::Legion ||
           tak::sim::cyclePathfindingMode(PathMode::Legion,1)!=PathMode::Retail ||
           std::string(tak::sim::pathfindingModeName(PathMode::Cooperative))!="Cooperative" ||
           std::string(tak::sim::pathfindingModeName(PathMode::Legion))!="Legion")
            throw std::runtime_error("pathfinder choice order or display name missing");
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
        auto cooperative=s;cooperative.gameCreate.pathfindingMode=PathMode::Cooperative;
        if(cooperative==s || !tak::saveSettings(cooperative) || tak::loadSettings().gameCreate.pathfindingMode!=PathMode::Cooperative)
            throw std::runtime_error("Cooperative create preference did not persist independently");
        auto retailPlus=s;retailPlus.gameCreate.pathfindingMode=PathMode::RetailPlus;
        if(retailPlus==s || !tak::saveSettings(retailPlus) || tak::loadSettings().gameCreate.pathfindingMode!=PathMode::RetailPlus)
            throw std::runtime_error("Retail+ create preference did not persist independently");
        auto legion=s;legion.gameCreate.pathfindingMode=PathMode::Legion;
        if(legion==s || !tak::saveSettings(legion) || tak::loadSettings().gameCreate.pathfindingMode!=PathMode::Legion)
            throw std::runtime_error("Legion create preference did not persist independently");
        std::ofstream(path)<<"gameCreate.pathfindingMode = 4\n";
        if(tak::loadSettings().gameCreate.pathfindingMode!=PathMode::Legion)
            throw std::runtime_error("saved value 4 did not load as Legion");
        if(std::string(tak::sim::pathfindingModeName(PathMode::RetailPlus))!="Retail+")
            throw std::runtime_error("Retail+ display name changed");
        auto modeChanged=s;modeChanged.gameCreate.pathfindingMode=PathMode::Retail;
        if(modeChanged==s)throw std::runtime_error("pathfinding preference changes not detected");
        std::ofstream(path)<<"gameCreate.crusades = true\ngameCreate.doubleSight = true\ngameCreate.unitCap = 2000\n";
        if(tak::loadSettings().gameCreate.pathfindingMode!=PathMode::Retail)
            throw std::runtime_error("legacy create preferences must remain Retail");
        for(const char* value:{"", "0", "-1", "5", "255", "256", "04", "4 4", "flowfield", "cooperative", "legion", "1oops", "2oops", "4oops"}) {
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
        auto defaultsOff=[](const tak::Settings& v) {
            if(v.terrainAA || v.modelAA || v.bilinear || v.smoothArt || v.videoDeblock)
                throw std::runtime_error("graphics quality options must default off");
        };
        defaultsOff(tak::Settings{});
        if(!tak::Settings{}.treeSway)throw std::runtime_error("tree sway must default on");
        for(int terrain:{0,2,4})for(int model:{0,2,4,8,16}) {
            std::ofstream(path)<<"antiAlias = 4\nterrainAA = "<<terrain<<"\nmodelAA = "<<model
                <<"\nbilinear = true\nsmoothArt = on\nvideoDeblock = 1\ntreeSway = 0\nvsync = 0\n";
            const auto loaded=tak::loadSettings();
            if(loaded.terrainAA!=terrain || loaded.modelAA!=model || !loaded.bilinear || !loaded.smoothArt || !loaded.videoDeblock)
                throw std::runtime_error("independent graphics preferences not loaded");
            if(loaded.treeSway)throw std::runtime_error("tree sway off preference ignored");
            if(loaded.vsync)throw std::runtime_error("unrelated preference ignored");
            if(!tak::saveSettings(loaded) || !(tak::loadSettings()==loaded))
                throw std::runtime_error("graphics preferences failed round trip");
        }
        for(const char* text:{"antiAlias = 4\nterrainAA = 0\n", "terrainAA = 0\nantiAlias = 4\n"}) {
            std::ofstream(path)<<text;
            const auto v=tak::loadSettings();
            if(v.terrainAA!=0 || v.modelAA!=4)throw std::runtime_error("legacy AA overrides explicit terrain choice");
        }
        for(const char* text:{"antiAlias = 2\nmodelAA = 16\n", "modelAA = 16\nantiAlias = 2\n"}) {
            std::ofstream(path)<<text;
            const auto v=tak::loadSettings();
            if(v.terrainAA!=2 || v.modelAA!=16)throw std::runtime_error("legacy AA overrides explicit model choice");
        }
        for(int value:{-1,0,1,2,3,4,7,8,15,16,99}) {
            std::ofstream(path)<<"terrainAA = "<<value<<"\nmodelAA = "<<value<<"\n";
            const auto v=tak::loadSettings();
            const int expected=value>=16?16:value>=8?8:value>=4?4:value>=2?2:0;
            if(v.terrainAA!=std::min(4,expected) || v.modelAA!=expected)
                throw std::runtime_error("invalid AA levels not clamped to supported steps");
        }
        s.terrainAA=4;s.modelAA=16;s.bilinear=s.smoothArt=s.videoDeblock=true;s.treeSway=false;
        s.terrainAAEffective=2;s.modelAAEffective=4;
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        const auto graphics=tak::loadSettings();
        if(graphics.terrainAA!=4 || graphics.modelAA!=16 || !graphics.bilinear || !graphics.smoothArt || !graphics.videoDeblock)
            throw std::runtime_error("enabled graphics preferences not saved");
        if(graphics.terrainAAEffective!=-1 || graphics.modelAAEffective!=-1)
            throw std::runtime_error("runtime AA fallback saved as a preference");
        if(tak::loadSettings().treeSway)throw std::runtime_error("tree sway off did not persist");
        s.treeSway=true;
        if(!tak::saveSettings(s) || !tak::loadSettings().treeSway)throw std::runtime_error("tree sway on did not persist");
        std::ifstream saved(path);
        const std::string contents((std::istreambuf_iterator<char>(saved)),{});
        if(contents.find("antiAlias =")!=std::string::npos)throw std::runtime_error("legacy AA preference still saved");
        for(const char* key:{"terrainAA =", "modelAA =", "bilinear =", "smoothArt =", "videoDeblock ="})
            if(contents.find(key)==std::string::npos)throw std::runtime_error("graphics preference not saved");
        s.terrainAA=s.modelAA=0;s.bilinear=s.smoothArt=s.videoDeblock=false;
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        defaultsOff(tak::loadSettings());
        fs::remove_all(root);std::cout<<"PASS: independent host/guest selections persist while Off\n";return 0;
    }catch(const std::exception& e){fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
