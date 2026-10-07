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
        // Graphics defaults: Bilinear Filtering off (retail's default), art filters off.
        auto graphicsDefaults=[](const tak::Settings& v) {
            if(v.bilinear || v.smoothArt || v.videoDeblock)throw std::runtime_error("graphics defaults changed");
        };
        graphicsDefaults(tak::Settings{});
        if(!tak::Settings{}.treeSway)throw std::runtime_error("tree sway must default on");
        for(bool bilinear:{false,true}) {
            tak::Settings v;v.bilinear=bilinear;v.smoothArt=v.videoDeblock=true;v.treeSway=false;v.vsync=false;
            if(!tak::saveSettings(v) || !(tak::loadSettings()==v))
                throw std::runtime_error("graphics preferences failed round trip");
        }
        // Older keys, used only when bilinearFilter is absent. Zoom Smoothing's Smooth
        // and 0.7.24's bilinear = 1 were this filter; every other choice and every AA
        // key has no successor and loads as Off.
        struct Legacy { const char* text; bool expected; };
        for(const Legacy& c:std::initializer_list<Legacy>{
                {"bilinear = 1\n",true},{"bilinear = 0\n",false},
                {"zoomSmoothing = smooth\n",true},{"zoomSmoothing = 1\n",true},
                {"zoomSmoothing = sharp\n",false},{"zoomSmoothing = off\n",false},{"zoomSmoothing = bogus\n",false},
                // the newer Zoom Smoothing key wins over 0.7.24's bilinear, in either order
                {"bilinear = 1\nzoomSmoothing = sharp\n",false},{"zoomSmoothing = sharp\nbilinear = 1\n",false},
                {"bilinear = 0\nzoomSmoothing = smooth\n",true},
                // bilinearFilter wins over both, in any order
                {"zoomSmoothing = smooth\nbilinear = 1\nbilinearFilter = 0\n",false},
                {"bilinearFilter = 0\nzoomSmoothing = smooth\nbilinear = 1\n",false},
                {"bilinearFilter = 1\nzoomSmoothing = off\n",true},
                {"antiAlias = 4\nterrainAA = 4\nmodelAA = 16\nunitEdgeAA = 16\nzoomedOutTerrain = 4\n",false}}) {
            std::ofstream(path)<<c.text<<"smoothArt = on\nvideoDeblock = 1\ntreeSway = 0\nvsync = 0\n";
            const auto loaded=tak::loadSettings();
            if(loaded.bilinear!=c.expected)throw std::runtime_error(std::string("legacy filter not migrated: ")+c.text);
            if(!loaded.smoothArt || !loaded.videoDeblock || loaded.treeSway || loaded.vsync)
                throw std::runtime_error("legacy graphics keys disturbed other preferences");
        }
        // The retired keys are dropped on the next save; only bilinearFilter is written.
        std::ofstream(path)<<"antiAlias = 4\nterrainAA = 2\nmodelAA = 16\nbilinear = 1\nunitEdgeAA = 8\n"
                             "zoomSmoothing = smooth\nzoomedOutTerrain = 4\nsmoothArt = 1\n";
        s=tak::loadSettings();
        if(!s.bilinear || !s.smoothArt)throw std::runtime_error("legacy file did not load");
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        std::ifstream saved(path);
        const std::string contents((std::istreambuf_iterator<char>(saved)),{});
        for(const char* old:{"antiAlias","terrainAA","modelAA","bilinear =","unitEdgeAA","zoomSmoothing","zoomedOutTerrain"})
            if(contents.find(old)!=std::string::npos)throw std::runtime_error(std::string("retired graphics key still saved: ")+old);
        for(const char* key:{"bilinearFilter = 1","smoothArt = 1","videoDeblock ="})
            if(contents.find(key)==std::string::npos)throw std::runtime_error("graphics preference not saved");
        if(!(tak::loadSettings()==s))throw std::runtime_error("migrated settings did not round trip");
        s.bilinear=s.smoothArt=s.videoDeblock=false;
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        graphicsDefaults(tak::loadSettings());
        fs::remove_all(root);std::cout<<"PASS: independent host/guest selections persist while Off\n";return 0;
    }catch(const std::exception& e){fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
