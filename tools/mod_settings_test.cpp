#include "client/settings.h"
#include "net/crypto.h"
#include <SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
        s.gameModPacks={"Sound Pack #1","Textures", "Accents-é"};
        s.cosmeticModPacks={"My local art"};
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
        s.uiScale=0.5f;s.scorecardScale=1.75f;
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        const auto loaded=tak::loadSettings();
        if(loaded.gameModPacks!=s.gameModPacks || loaded.cosmeticModPacks!=s.cosmeticModPacks)
            throw std::runtime_error("mod selections did not survive save/load");
        {   // Written under the new keys; the old override keys are neither written nor read.
            std::ifstream saved(path);std::string text((std::istreambuf_iterator<char>(saved)),{});
            if(text.find("mods.gamePack = ")==text.npos || text.find("mods.cosmeticPack = ")==text.npos)
                throw std::runtime_error("mod selections not written under the mods.* keys");
            // 0.8.13's multiplayer GAME MODS toggle is gone: never written.
            if(text.find("gameCreate.gameMods")!=text.npos)throw std::runtime_error("the removed GAME MODS key is still written");
            if(text.find("overrides")!=text.npos)throw std::runtime_error("an old overrides key is still written");
            std::ofstream(path)<<"overrides.hostPack = 4f6c64\noverrides.cosmeticPack = 4f6c64\ngameCreate.overrides = 2\n";
            const auto old=tak::loadSettings();
            if(!old.gameModPacks.empty() || !old.cosmeticModPacks.empty())
                throw std::runtime_error("old overrides keys must be ignored (no migration)");
            // A 0.8.13 file's gameCreate.gameMods loads (skipped like any unknown key)
            // and vanishes on the next save.
            std::ofstream(path)<<"gameCreate.gameMods = 1\nmods.gamePack = 4b656570\n";
            auto kept=tak::loadSettings();
            if(kept.gameModPacks!=std::vector<std::string>{"Keep"})throw std::runtime_error("a file with the old GAME MODS key did not load");
            if(!tak::saveSettings(kept))throw std::runtime_error("save failed");
            std::ifstream again(path);std::string resaved((std::istreambuf_iterator<char>(again)),{});
            if(resaved.find("gameCreate.gameMods")!=resaved.npos)throw std::runtime_error("the old GAME MODS key survived a save");
        }
        {   // takclient --game-mod (Cartographer's TEST IN GAME): a launch-only selection never
            // replaces the player's saved ticks, until the player changes the ticks themselves.
            if(!tak::saveSettings(s))throw std::runtime_error("save failed");   // the player's ticks on disk
            auto run=s;run.gameModPacks={"My AI"};
            tak::setLaunchGameModPacks(run.gameModPacks);
            run.uiScale=0.75f;
            if(!tak::saveSettings(run))throw std::runtime_error("save failed");
            auto back=tak::loadSettings();
            if(back.gameModPacks!=s.gameModPacks || back.uiScale!=0.75f)
                throw std::runtime_error("a launch-only game mod selection was saved over the player's ticks (or blocked the save)");
            run.gameModPacks={"My AI","Textures"};   // the player ticks another pack in that room
            if(!tak::saveSettings(run) || tak::loadSettings().gameModPacks!=run.gameModPacks)
                throw std::runtime_error("the player's own new ticks were not saved");
            run.gameModPacks={"My AI"};   // the launch selection ended with that change
            if(!tak::saveSettings(run) || tak::loadSettings().gameModPacks!=run.gameModPacks)
                throw std::runtime_error("a launch selection outlived the player's own change");
            tak::setLaunchGameModPacks(std::nullopt);
            if(!tak::saveSettings(s))throw std::runtime_error("save failed");
            // An EMPTY launch selection (--game-mod naming an always-enabled pack: nothing
            // ticked for the run) still keeps the player's saved ticks.
            auto none=s;none.gameModPacks={};
            tak::setLaunchGameModPacks(none.gameModPacks);
            none.lastMap="Somewhere";
            if(!tak::saveSettings(none) || tak::loadSettings().gameModPacks!=s.gameModPacks)
                throw std::runtime_error("an empty launch-only selection was saved over the player's ticks");
            tak::setLaunchGameModPacks(std::nullopt);
            if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        }
        if(loaded.uiScale!=0.5f || loaded.scorecardScale!=1.75f)
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
        // START LOCATIONS was a RANDOM on/off toggle under the same key: 0 and 1 keep their
        // meaning (a hand-written true/on is RANDOM too), 2 is BY SLOT, and absent is FIXED.
        struct StartsCase { const char* text; int expected; };
        for(const StartsCase& c:std::initializer_list<StartsCase>{
                {"gameCreate.randomStarts = 0\n",0},{"gameCreate.randomStarts = 1\n",1},
                {"gameCreate.randomStarts = true\n",1},{"gameCreate.randomStarts = on\n",1},
                {"gameCreate.randomStarts = 2\n",2},{"gameCreate.crusades = 1\n",0}}) {
            std::ofstream(path)<<c.text;
            if(tak::loadSettings().gameCreate.randomStarts!=c.expected)
                throw std::runtime_error(std::string("start locations preference not read: ")+c.text);
        }
        for(int mode:{2,1,0}) {
            auto starts=s;starts.gameCreate.randomStarts=mode;
            if(!tak::saveSettings(starts) || tak::loadSettings().gameCreate.randomStarts!=mode)
                throw std::runtime_error("start locations preference did not persist: "+std::to_string(mode));
        }
        auto changed=s;changed.scorecardScale=1.0f;
        if(changed==s)throw std::runtime_error("scorecard scale changes not detected");
        // Texture Filtering persists all three levels under its own key (the legacy
        // migrations are checked with the other graphics keys below).
        for(int level=0;level<3;++level) {
            auto filtered=s;filtered.textureFilter=level;
            if(!tak::saveSettings(filtered) || tak::loadSettings().textureFilter!=level)
                throw std::runtime_error("texture filtering level did not persist");
        }
        {
            std::ifstream saved(path);std::string text((std::istreambuf_iterator<char>(saved)),{});
            if(text.find("textureFilter = 2\n")==text.npos || text.find("bilinearFilter")!=text.npos)
                throw std::runtime_error("texture filtering is not written as textureFilter");
        }
        // GPU MEMORY LIMIT: every choice persists under its own key, a file from before
        // the option (or one still saying "auto") loads as the 1280 MiB default, which is
        // saved as its number, and a hand-edited value is snapped or falls back.
        for(int mib:tak::kGpuMemoryLimits) {
            auto limited=s;limited.gpuMemoryLimit=mib;
            if(!tak::saveSettings(limited) || tak::loadSettings().gpuMemoryLimit!=mib)
                throw std::runtime_error("GPU memory limit did not persist: "+std::to_string(mib));
        }
        {
            auto limited=s;limited.gpuMemoryLimit=-1;tak::saveSettings(limited);
            std::ifstream saved(path);std::string text((std::istreambuf_iterator<char>(saved)),{});
            if(text.find("\ngpuMemoryLimit = unlimited\n")==text.npos)throw std::runtime_error("Unlimited must be written as 'gpuMemoryLimit = unlimited'");
            limited.gpuMemoryLimit=0;tak::saveSettings(limited);
            std::ifstream again(path);std::string defText((std::istreambuf_iterator<char>(again)),{});
            if(defText.find("\ngpuMemoryLimit = 1280\n")==defText.npos)throw std::runtime_error("the default must be written as 'gpuMemoryLimit = 1280'");
        }
        std::ofstream(path)<<"vsync = 1\n";
        if(tak::loadSettings().gpuMemoryLimit!=1280)throw std::runtime_error("pre-option file must load GPU memory limit as 1280");
        struct GpuCase { const char* text; int expected; };
        for(const GpuCase& c:std::initializer_list<GpuCase>{
                {"gpuMemoryLimit = 2048\n",2048},{"gpuMemoryLimit =  UNLIMITED \n",-1},{"gpuMemoryLimit = 900\n",1024},
                {"gpuMemoryLimit = 64\n",512},{"gpuMemoryLimit = lots\n",1280},{"gpuMemoryLimit =\n",1280},
                {"gpuMemoryLimit = auto\n",1280},{"gpuMemoryLimit = AUTO\n",1280},{"gpuMemoryLimit = 0\n",1280}}) {
            std::ofstream(path)<<c.text;
            if(tak::loadSettings().gpuMemoryLimit!=c.expected)
                throw std::runtime_error(std::string("GPU memory limit value not normalised: ")+c.text);
        }
        std::ofstream(path)<<"uiScale = 2\n";
        if(tak::loadSettings().scorecardScale!=1.0f)throw std::runtime_error("scorecard inherits HUD scale");
        std::ofstream(path)<<"scorecardScale = 9\n";
        if(tak::loadSettings().scorecardScale!=2.0f)throw std::runtime_error("scorecard upper limit");
        std::ofstream(path)<<"scorecardScale = 0\n";
        if(tak::loadSettings().scorecardScale!=0.5f)throw std::runtime_error("scorecard lower limit");
        // Streaming SAVE A LOCAL COPY: off by default and in files written before it
        // existed; the choice survives a save and load (a restart) both ways.
        if(tak::Settings{}.streamLocalCopy)throw std::runtime_error("local copy must default to off");
        std::ofstream(path)<<"uiScale = 2\n";
        if(tak::loadSettings().streamLocalCopy)throw std::runtime_error("local copy must load as off when absent");
        {
            auto copy=s;copy.streamLocalCopy=true;
            if(!tak::saveSettings(copy) || !tak::loadSettings().streamLocalCopy)throw std::runtime_error("local copy on must persist");
            copy.streamLocalCopy=false;
            if(!tak::saveSettings(copy) || tak::loadSettings().streamLocalCopy)throw std::runtime_error("local copy off must persist");
            if(tak::serializeSettings(copy).find("streamLocalCopy = 0\n")==std::string::npos)
                throw std::runtime_error("local copy saved as an on/off key");
            if(tak::videoDir()!=tak::replayDir()+"videos"+tak::replayDir().back())
                throw std::runtime_error("local copies go in the user folder's videos folder");
        }
        // Renderer: AUTO ("") by default and when the key is absent (every file written
        // before the option existed); stored by SDL driver id, written as "auto" for AUTO.
        if(!tak::Settings{}.renderer.empty())throw std::runtime_error("renderer must default to Auto");
        std::ofstream(path)<<"vsync = 1\n";
        if(!tak::loadSettings().renderer.empty())throw std::runtime_error("pre-option file must load as Auto");
        for(const char* id:{"","opengl","opengles2","software","direct3d11","metal"}) {
            tak::Settings v;v.renderer=id;
            if(!tak::saveSettings(v) || tak::loadSettings().renderer!=id)
                throw std::runtime_error(std::string("renderer did not round trip: ")+id);
        }
        {
            tak::Settings v;
            if(!tak::saveSettings(v))throw std::runtime_error("save failed");
            std::ifstream in(path);std::string all((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
            if(all.find("\nrenderer = auto\n")==std::string::npos)throw std::runtime_error("Auto must be written as 'renderer = auto'");
        }
        struct RendererCase { const char* text; const char* expected; };
        for(const RendererCase& c:std::initializer_list<RendererCase>{
                {"renderer = auto\n",""},{"renderer = AUTO\n",""},{"renderer =\n",""},
                {"renderer = OpenGL\n","opengl"},{"renderer =  software  \n","software"},
                // a well-formed id this build lacks is kept; startup falls back for that launch
                {"renderer = vulkan\n","vulkan"},
                // junk never reaches SDL_SetHint
                {"renderer = open gl\n",""},{"renderer = ../../x\n",""}}) {
            std::ofstream(path)<<c.text;
            if(tak::loadSettings().renderer!=c.expected)
                throw std::runtime_error(std::string("renderer value not normalised: ")+c.text);
        }
        {
            tak::Settings a,b;b.renderer="software";
            if(a==b)throw std::runtime_error("renderer changes not detected");
            if(!tak::preferenceDefaults(b).renderer.empty())throw std::runtime_error("DEFAULTS must reset the renderer to Auto");
        }
        // Graphics defaults: Texture Filtering Everything, art filters on.
        auto graphicsDefaults=[](const tak::Settings& v) {
            if(v.textureFilter!=2 || !v.smoothArt || !v.videoDeblock)throw std::runtime_error("graphics defaults changed");
        };
        graphicsDefaults(tak::Settings{});
        if(!tak::Settings{}.treeSway)throw std::runtime_error("tree sway must default on");
        // Tactical Dots is a non-retail extra: on by default, on for a file written
        // before the key existed, and its own key round-trips both ways.
        if(!tak::Settings{}.tacticalDots)throw std::runtime_error("tactical dots must default on");
        if(tak::Settings{}.tacticalDotsZoom!=0)throw std::runtime_error("tactical dots zoom must default to 0%");
        std::ofstream(path)<<"treeSway = 0\nunitShadows = 1\n";
        if(!tak::loadSettings().tacticalDots)throw std::runtime_error("pre-dots settings file must load dots on");
        for(const char* text:{"tacticalDots = 1\n","tacticalDots = on\n","tacticalDots = true\n"}) {
            std::ofstream(path)<<text;
            if(!tak::loadSettings().tacticalDots)throw std::runtime_error(std::string("tactical dots not read: ")+text);
        }
        for(bool dots:{true,false}) {
            tak::Settings v;v.tacticalDots=dots;v.tacticalDotsZoom=dots?73:0;
            if(!tak::saveSettings(v) || !(tak::loadSettings()==v) || tak::loadSettings().tacticalDots!=dots)
                throw std::runtime_error("tactical dots failed round trip");
        }
        for(int level:{0,1,2}) {
            tak::Settings v;v.textureFilter=level;v.smoothArt=v.videoDeblock=true;v.treeSway=false;v.vsync=false;
            if(!tak::saveSettings(v) || !(tak::loadSettings()==v))
                throw std::runtime_error("graphics preferences failed round trip");
        }
        // Older keys, used only when textureFilter is absent. 0.7.25-0.7.28's
        // bilinearFilter, Zoom Smoothing's Smooth and 0.7.24's bilinear = 1 were all
        // the Units level; every other choice and every AA key has no successor and
        // loads as the default (Everything).
        struct Legacy { const char* text; int expected; };
        for(const Legacy& c:std::initializer_list<Legacy>{
                {"bilinear = 1\n",1},{"bilinear = 0\n",0},
                {"zoomSmoothing = smooth\n",1},{"zoomSmoothing = 1\n",1},
                {"zoomSmoothing = sharp\n",0},{"zoomSmoothing = off\n",0},{"zoomSmoothing = bogus\n",0},
                // the newer Zoom Smoothing key wins over 0.7.24's bilinear, in either order
                {"bilinear = 1\nzoomSmoothing = sharp\n",0},{"zoomSmoothing = sharp\nbilinear = 1\n",0},
                {"bilinear = 0\nzoomSmoothing = smooth\n",1},
                // bilinearFilter wins over both, in any order
                {"zoomSmoothing = smooth\nbilinear = 1\nbilinearFilter = 0\n",0},
                {"bilinearFilter = 0\nzoomSmoothing = smooth\nbilinear = 1\n",0},
                {"bilinearFilter = 1\nzoomSmoothing = off\n",1},
                // and textureFilter wins over everything, in any order
                {"bilinearFilter = 1\ntextureFilter = 0\nbilinear = 1\n",0},
                {"textureFilter = 2\nbilinearFilter = 0\n",2},
                // a hand-edited level out of range clamps like every other number
                {"textureFilter = 7\nbilinearFilter = 0\n",2},{"bilinearFilter = 1\ntextureFilter = -3\n",0},
                {"antiAlias = 4\nterrainAA = 4\nmodelAA = 16\nunitEdgeAA = 16\nzoomedOutTerrain = 4\n",2}}) {   // no filter key at all: the default, Everything
            std::ofstream(path)<<c.text<<"smoothArt = on\nvideoDeblock = 1\ntreeSway = 0\nvsync = 0\n";
            const auto loaded=tak::loadSettings();
            if(loaded.textureFilter!=c.expected)throw std::runtime_error(std::string("legacy filter not migrated: ")+c.text);
            if(!loaded.smoothArt || !loaded.videoDeblock || loaded.treeSway || loaded.vsync)
                throw std::runtime_error("legacy graphics keys disturbed other preferences");
        }
        // The retired keys are dropped on the next save; only textureFilter is written.
        std::ofstream(path)<<"antiAlias = 4\nterrainAA = 2\nmodelAA = 16\nbilinear = 1\nunitEdgeAA = 8\n"
                             "zoomSmoothing = smooth\nzoomedOutTerrain = 4\nsmoothArt = 1\n";
        s=tak::loadSettings();
        if(s.textureFilter!=1 || !s.smoothArt)throw std::runtime_error("legacy file did not load");
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        std::ifstream saved(path);
        const std::string contents((std::istreambuf_iterator<char>(saved)),{});
        for(const char* old:{"antiAlias","terrainAA","modelAA","bilinear =","bilinearFilter","unitEdgeAA","zoomSmoothing","zoomedOutTerrain"})
            if(contents.find(old)!=std::string::npos)throw std::runtime_error(std::string("retired graphics key still saved: ")+old);
        for(const char* key:{"textureFilter = 1","smoothArt = 1","videoDeblock ="})
            if(contents.find(key)==std::string::npos)throw std::runtime_error("graphics preference not saved");
        if(!(tak::loadSettings()==s))throw std::runtime_error("migrated settings did not round trip");
        s.textureFilter=2;s.smoothArt=s.videoDeblock=true;
        if(!tak::saveSettings(s))throw std::runtime_error("save failed");
        graphicsDefaults(tak::loadSettings());
        fs::remove_all(root);std::cout<<"PASS: game and cosmetic mod selections persist; old override keys ignored\n";return 0;
    }catch(const std::exception& e){fs::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
