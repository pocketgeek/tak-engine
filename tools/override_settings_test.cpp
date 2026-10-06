#include "client/settings.h"
#include "client/zoomsmooth.h"
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
        // Stable identities: 1-3 belonged to the removed Flowfield,
        // Cooperative and Retail+ modes and are invalid.
        static_assert(uint8_t(PathMode::Retail)==0&&uint8_t(PathMode::Legion)==4);
        for(int value=0;value<256;++value)
            if(tak::sim::validPathfindingMode(uint8_t(value))!=(value==0||value==4))
                throw std::runtime_error("only Retail (0) and Legion (4) are valid pathfinder bytes");
        // Display order: Retail <-> Legion, in both directions.
        if(tak::sim::cyclePathfindingMode(PathMode::Retail,1)!=PathMode::Legion ||
           tak::sim::cyclePathfindingMode(PathMode::Retail,-1)!=PathMode::Legion ||
           tak::sim::cyclePathfindingMode(PathMode::Legion,1)!=PathMode::Retail ||
           tak::sim::cyclePathfindingMode(PathMode::Legion,-1)!=PathMode::Retail ||
           std::string(tak::sim::pathfindingModeName(PathMode::Retail))!="Retail" ||
           std::string(tak::sim::pathfindingModeName(PathMode::Legion))!="Legion")
            throw std::runtime_error("pathfinder choice order or display name missing");
        if(s.gameCreate.pathfindingMode!=PathMode::Retail)
            throw std::runtime_error("new create preferences must default to Retail");
        s.gameCreate.pathfindingMode=PathMode::Legion;
        s.gameCreate.overrides=0;
        s.uiScale=0.75f;s.scorecardScale=1.75f;
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        const auto loaded=tak::loadSettings();
        if(loaded.hostOverridePacks!=s.hostOverridePacks || loaded.cosmeticOverridePacks!=s.cosmeticOverridePacks || loaded.gameCreate.overrides!=0)
            throw std::runtime_error("pack selections did not survive save/load while off");
        if(loaded.uiScale!=0.75f || loaded.scorecardScale!=1.75f)
            throw std::runtime_error("independent scorecard scale did not persist");
        if(loaded.gameCreate.pathfindingMode!=PathMode::Legion)
            throw std::runtime_error("Legion create preference did not persist");
        auto retail=s;retail.gameCreate.pathfindingMode=PathMode::Retail;
        if(retail==s || !tak::saveSettings(retail) || tak::loadSettings().gameCreate.pathfindingMode!=PathMode::Retail)
            throw std::runtime_error("Retail create preference did not persist independently");
        std::ofstream(path)<<"gameCreate.pathfindingMode = 4\n";
        if(tak::loadSettings().gameCreate.pathfindingMode!=PathMode::Legion)
            throw std::runtime_error("saved value 4 did not load as Legion");
        // Saved Flowfield (1), Cooperative (2) and Retail+ (3) preferences
        // fall back to Retail now that those modes are gone.
        for(const char* removed:{"1","2","3"}) {
            std::ofstream(path)<<"gameCreate.pathfindingMode = "<<removed<<"\n";
            if(tak::loadSettings().gameCreate.pathfindingMode!=PathMode::Retail)
                throw std::runtime_error("a removed pathfinding preference did not fall back to Retail");
        }
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
        // Graphics defaults: edge/zoomed-out supersampling off/Auto, Zoom smoothing Sharp.
        auto graphicsDefaults=[](const tak::Settings& v) {
            if(v.unitEdgeAA || v.zoomSmoothing!=tak::kZoomSharp || v.zoomOutTerrain!=tak::kZoomOutAuto ||
               v.smoothArt || v.videoDeblock)
                throw std::runtime_error("graphics defaults changed");
        };
        graphicsDefaults(tak::Settings{});
        if(!tak::Settings{}.treeSway)throw std::runtime_error("tree sway must default on");
        // New keys round-trip for every combination.
        for(int out:{tak::kZoomOutAuto,tak::kZoomOutOff,tak::kZoomOut2x,tak::kZoomOut4x})
        for(int edge:{0,2,4,8,16})for(int zoom:{0,1,2}) {
            tak::Settings v;v.zoomOutTerrain=out;v.unitEdgeAA=edge;v.zoomSmoothing=zoom;
            v.smoothArt=v.videoDeblock=true;v.treeSway=false;v.vsync=false;
            if(!tak::saveSettings(v) || !(tak::loadSettings()==v))
                throw std::runtime_error("graphics preferences failed round trip");
        }
        // Migration from the old keys (in either line order), when the new key is absent.
        for(int terrain:{0,2,4})for(int model:{0,2,4,8,16})for(int bilinear:{0,1}) {
            std::ofstream(path)<<"antiAlias = 4\nterrainAA = "<<terrain<<"\nmodelAA = "<<model
                <<"\nbilinear = "<<bilinear<<"\nsmoothArt = on\nvideoDeblock = 1\ntreeSway = 0\nvsync = 0\n";
            const auto loaded=tak::loadSettings();
            const int out=terrain==4?tak::kZoomOut4x:terrain==2?tak::kZoomOut2x:tak::kZoomOutAuto;
            if(loaded.unitEdgeAA!=model || loaded.zoomOutTerrain!=out ||
               loaded.zoomSmoothing!=(bilinear?tak::kZoomSmooth:tak::kZoomSharp) || !loaded.smoothArt || !loaded.videoDeblock)
                throw std::runtime_error("legacy graphics preferences not migrated");
            if(loaded.treeSway)throw std::runtime_error("tree sway off preference ignored");
            if(loaded.vsync)throw std::runtime_error("unrelated preference ignored");
        }
        for(const char* text:{"antiAlias = 4\nterrainAA = 0\n", "terrainAA = 0\nantiAlias = 4\n"}) {
            std::ofstream(path)<<text;
            const auto v=tak::loadSettings();
            if(v.zoomOutTerrain!=tak::kZoomOutAuto || v.unitEdgeAA!=4)throw std::runtime_error("legacy AA overrides explicit terrain choice");
        }
        for(const char* text:{"antiAlias = 2\nmodelAA = 16\n", "modelAA = 16\nantiAlias = 2\n"}) {
            std::ofstream(path)<<text;
            const auto v=tak::loadSettings();
            if(v.zoomOutTerrain!=tak::kZoomOut2x || v.unitEdgeAA!=16)throw std::runtime_error("legacy AA overrides explicit model choice");
        }
        // New keys win over old ones regardless of order.
        for(const char* text:{"modelAA = 16\nunitEdgeAA = 2\nbilinear = 1\nzoomSmoothing = off\nterrainAA = 4\nzoomedOutTerrain = off\n",
                              "unitEdgeAA = 2\nzoomSmoothing = off\nzoomedOutTerrain = off\nmodelAA = 16\nbilinear = 1\nterrainAA = 4\n"}) {
            std::ofstream(path)<<text;
            const auto v=tak::loadSettings();
            if(v.unitEdgeAA!=2 || v.zoomSmoothing!=tak::kZoomOff || v.zoomOutTerrain!=tak::kZoomOutOff)
                throw std::runtime_error("legacy key overrides a new key");
        }
        for(int value:{-1,0,1,2,3,4,7,8,15,16,99}) {
            std::ofstream(path)<<"unitEdgeAA = "<<value<<"\nmodelAA = 2\n";
            const auto v=tak::loadSettings();
            const int expected=value>=16?16:value>=8?8:value>=4?4:value>=2?2:0;
            if(v.unitEdgeAA!=expected)throw std::runtime_error("invalid edge levels not clamped to supported steps");
        }
        for(const char* bad:{"7","sharpest","","-1"}) {
            std::ofstream(path)<<"zoomSmoothing = "<<bad<<"\nzoomedOutTerrain = "<<bad<<"\n";
            const auto v=tak::loadSettings();
            if(v.zoomSmoothing!=tak::kZoomSharp || v.zoomOutTerrain!=tak::kZoomOutAuto)
                throw std::runtime_error("invalid zoom values did not fall back to defaults");
        }
        s.zoomOutTerrain=tak::kZoomOut4x;s.unitEdgeAA=16;s.zoomSmoothing=tak::kZoomSmooth;s.smoothArt=s.videoDeblock=true;s.treeSway=false;
        s.zoomOutTerrainEffective=2;s.unitEdgeAAEffective=4;
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        const auto graphics=tak::loadSettings();
        if(graphics.zoomOutTerrain!=tak::kZoomOut4x || graphics.unitEdgeAA!=16 || graphics.zoomSmoothing!=tak::kZoomSmooth ||
           !graphics.smoothArt || !graphics.videoDeblock)
            throw std::runtime_error("enabled graphics preferences not saved");
        if(graphics.zoomOutTerrainEffective!=-1 || graphics.unitEdgeAAEffective!=-1)
            throw std::runtime_error("runtime AA fallback saved as a preference");
        if(tak::loadSettings().treeSway)throw std::runtime_error("tree sway off did not persist");
        s.treeSway=true;
        if(!tak::saveSettings(s) || !tak::loadSettings().treeSway)throw std::runtime_error("tree sway on did not persist");
        std::ifstream saved(path);
        const std::string contents((std::istreambuf_iterator<char>(saved)),{});
        for(const char* old:{"antiAlias =","terrainAA =","modelAA =","bilinear ="})
            if(contents.find(old)!=std::string::npos)throw std::runtime_error("legacy graphics preference still saved");
        for(const char* key:{"unitEdgeAA = 16","zoomSmoothing = smooth","zoomedOutTerrain = 4","smoothArt =","videoDeblock ="})
            if(contents.find(key)==std::string::npos)throw std::runtime_error("graphics preference not saved");
        s.zoomOutTerrain=tak::kZoomOutAuto;s.unitEdgeAA=0;s.zoomSmoothing=tak::kZoomSharp;s.smoothArt=s.videoDeblock=false;
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        graphicsDefaults(tak::loadSettings());
        fs::remove_all(root);std::cout<<"PASS: independent host/guest selections persist while Off\n";return 0;
    }catch(const std::exception& e){fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
