// Cartographer -- a clean-room re-implementation of the retail TA:Kingdoms map
// editor (see docs/cartographer-port.md). Static-analysis RE of the shipped
// Cartographer.exe drives the behaviour; this shares the engine's rendering
// stack (hpi VFS, tnt loader, terrain compositor, MapView) so terrain looks
// byte-identical to the game.
//
// Document serialization and undo live in document/history; selection operations
// live in selection.h. This file wires the desktop UI and editor rendering.
// See docs/cartographer-improvements-progress.md for completed and pending work.

#include "net/netcompat.h"
#include <SDL.h>

#include "cartographer/dialog.h"
#include "cartographer/editor.h"
#include "cartographer/document.h"
#include "cartographer/history.h"
#include "cartographer/textedit.h"
#include "cartographer/selection.h"
#include "cartographer/validation.h"
#include "cartographer/regions.h"
#include "cartographer/generator.h"
#include "cartographer/overlay.h"
#include "cartographer/ruleedit.h"
#include "cartographer/preferences.h"
#include <fstream>
#include <charconv>
#include "cartographer/features.h"
#include "cartographer/font5x7.h"
#include "cartographer/newmap.h"
#include "cartographer/sections.h"
#include "cartographer/triggers.h"
#include "cartographer/units.h"
#include "client/mapview.h"
#include "client/settings.h"
#include "client/dirpicker.h"
#include "util/appicon.h"
#include "util/virtualpath.h"
#include "terrain/terrain.h"
#include "util/jpeg.h"
#include "hpi/hpi.h"
#include "tnt/mapgen.h"
#include "sim/matchsetup.h"
#include "tnt/ota.h"
#include "util/png.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <future>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace {
std::string pathText(const std::filesystem::path& path) {const auto text=path.u8string();return {text.begin(),text.end()};}

constexpr int kMenuH = 44;    // top menu-bar strip
constexpr int kStatusH = 22;  // bottom status strip
// UI magnification. The whole editor is drawn in a fixed logical coordinate
// space and scaled up by this factor via SDL_RenderSetScale, so every panel,
// glyph, and dialog doubles uniformly on hi-DPI displays. Input coordinates are
// divided back down to logical space at the event source.
float kUIScale = 1.5f;

// A fresh map for the New dialog / --new launch: either a flat ground stamp or
// procedural terrain from the engine's map generator (the "~gen1~" generator the
// lobby uses -- coastlines, relief, trees/rocks/mana, and start positions).
struct FreshMap {
    tak::tnt::Map map;
    std::string recipe;
    std::vector<tak::tnt::StartPos> starts;   // only for random terrain
};
FreshMap buildFreshMap(const tak::hpi::Vfs& vfs, cart::SectionLibrary& sections,
                       tak::terrain::Compositor& comp, const std::string& world,
                       int wUnits, int hUnits, bool random, const tak::mapgen::Params* requested=nullptr) {
    FreshMap out;
    if (random) {
        tak::mapgen::Params gp=requested?*requested:tak::mapgen::Params{};
        static const char* kW[] = {"aramon", "taros", "veruna", "zhon", "creon"};
        gp.mapType = tak::mapgen::Aramon;
        for (uint8_t i = 0; i < 5; ++i) if (world == kW[i]) gp.mapType = i;
        gp.widthCells = uint16_t(wUnits * 32);
        gp.heightCells = uint16_t(hUnits * 32);
        if(!requested) {gp.players=4;gp.seed=uint64_t(SDL_GetPerformanceCounter());}
        gp=tak::mapgen::sanitize(gp);out.recipe=tak::mapgen::encodeMapId(gp);
        auto res = tak::mapgen::generate(tak::mapgen::sanitize(gp), vfs);
        out.map = std::move(res.map);
        int n = 1;
        for (auto& [sx, sz] : res.starts) out.starts.push_back({n++, sx, sz});
    } else {
        out.map = cart::newBlankMap(vfs, sections, comp, world, wUnits, hUnits);
    }
    return out;
}

void fillRect(SDL_Renderer* r, int x, int y, int w, int h, Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

}  // namespace

int cart::runEditor(int argc, char** argv, const std::function<void(SDL_Window*,SDL_Renderer*,int)>& frameHook,const EditorHooks& hooks) {
    const auto recoveryClock=[&]() {return hooks.recoveryClock?hooks.recoveryClock():SDL_GetTicks64();};
    std::string dataRoot, mapName, outDir = ".", exportPath, bundlePath, stampName, newWorld = "aramon", shotPath;
    int stampBX = 0, stampBY = 0, newW = 0, newH = 0;
    bool explicitOutput=false;
    bool randomTerrain = false;   // --random: generate procedural terrain for --new
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) dataRoot = argv[++i];
        else if (a == "--out" && i + 1 < argc) {outDir = argv[++i];explicitOutput=true;}   // Save destination
        else if (a == "--save" && i + 1 < argc) exportPath = argv[++i];  // headless export+exit
        else if (a == "--bundle" && i + 1 < argc) bundlePath = argv[++i];  // headless: write a .kmp + exit
        else if (a == "--stamp" && i + 3 < argc) {   // headless: stamp <name> <bx> <by>
            stampName = argv[++i]; stampBX = std::atoi(argv[++i]); stampBY = std::atoi(argv[++i]);
        }
        else if (a == "--new" && i + 1 < argc) {   // headless: --new WxH (Units) + --save
            std::sscanf(argv[++i], "%dx%d", &newW, &newH);
        }
        else if (a == "--world" && i + 1 < argc) newWorld = argv[++i];
        else if (a == "--random") randomTerrain = true;   // --new: procedural terrain
        else if (a == "--shot" && i + 1 < argc) shotPath = argv[++i];   // render 1 frame -> PNG
        else if (a[0] != '-') mapName = a;
    }
    // Data root: --data wins; otherwise the local directory if it holds a valid
    // install (drop the editor into a game folder and it just works).
    if (dataRoot.empty()) {
        std::error_code ec;
        std::filesystem::path here = std::filesystem::current_path(ec);
        if (!ec && tak::hpi::validInstall(here, nullptr)) {
            dataRoot = pathText(here);
            std::fprintf(stderr, "data: using the local directory %s\n", dataRoot.c_str());
        }
    }
    if(dataRoot.empty()) {
        const auto saved=tak::loadSettings();
        if(!saved.dataDir.empty() && tak::hpi::validInstall(std::filesystem::u8path(saved.dataDir),nullptr))
            dataRoot=saved.dataDir;
    }
    if(dataRoot.empty() && (!std::getenv("SDL_VIDEODRIVER") || (std::string(std::getenv("SDL_VIDEODRIVER"))!="dummy" && std::string(std::getenv("SDL_VIDEODRIVER"))!="offscreen"))) {
        auto picked=tak::pickDirectory("Choose your Total Annihilation Kingdoms installation","");
        if(!picked.empty() && tak::hpi::validInstall(std::filesystem::u8path(picked),nullptr)) {
            dataRoot=picked;auto settings=tak::loadSettings();settings.dataDir=picked;tak::saveSettings(settings);
        }
    }
    if (dataRoot.empty()) {
        std::fprintf(stderr,
            "Cartographer -- TA:Kingdoms map editor\n"
            "usage: cartographer \"<map name>\" [--data <retail-install-dir>]\n"
            "         (--data is optional when run from inside a game folder)\n"
            "         [--out <dir>]        Ctrl+S / Ctrl+B save destination (default .)\n"
            "         [--new WxH --world <w> [--random]]  start a blank/random map\n"
            "                                 (or press N in-editor)\n"
            "         [--save <file.tnt>]  headless: save loose .tnt/.ota/.crt and exit\n"
            "         [--bundle <file.kmp>] headless: save a packed .kmp map and exit\n"
            "in-editor: Tab tools, 1-5 zoom, G grid, N new, P/R/U/C/T/K scenario menu,\n"
            "           Ctrl+S save .kmp, Ctrl+Shift+S export loose files\n");
        return 2;
    }

    if(!explicitOutput) outDir=pathText(std::filesystem::u8path(dataRoot)/"Maps");

    SDL_SetMainReady();   // we own main() (SDL_MAIN_HANDLED); tell SDL not to hijack it
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    struct SdlLifetime { ~SdlLifetime() { SDL_Quit(); } } sdlLifetime;
    std::filesystem::path preferencesFolder;
    if(char* path=SDL_GetPrefPath("TAKengine","Cartographer")) {preferencesFolder=std::filesystem::u8path(path);SDL_free(path);}
    auto preferences=cart::loadEditorPreferences(preferencesFolder);
    SDL_Rect display{0,0,1600,1000};SDL_GetDisplayUsableBounds(0,&display);
    const int initialWidth=std::min(preferences.width?preferences.width:1600,std::max(800,int(display.w*.9f)));
    const int initialHeight=std::min(preferences.height?preferences.height:1000,std::max(600,int(display.h*.9f)));
    SDL_Window* win = SDL_CreateWindow(
        ("Cartographer -- " + mapName).c_str(), SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, initialWidth, initialHeight, SDL_WINDOW_RESIZABLE);
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> windowOwner(win,SDL_DestroyWindow);
    {   // Application icon: the compass-rose badge (src/util/appicon).
        std::vector<uint8_t> ic = tak::appicon::render(tak::appicon::Kind::Cartographer, 64);
        if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
                ic.data(), 64, 64, 32, 64 * 4, SDL_PIXELFORMAT_RGBA32)) {
            SDL_SetWindowIcon(win, s);
            SDL_FreeSurface(s);
        }
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    // Declared before MapView: its worker and textures must die before SDL's
    // renderer, including on export/error returns below.
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> rendererOwner(ren,SDL_DestroyRenderer);
    // Draw everything at kUIScale: the logical canvas stays 1280x800-ish while
    // the window is that many times larger, so the UI is magnified uniformly.
    if (ren) SDL_RenderSetScale(ren, float(kUIScale), float(kUIScale));
    if (!win || !ren) {
        std::fprintf(stderr, "window/renderer: %s\n", SDL_GetError());
        return 1;
    }

    auto persistPreferences=[&]() {
        SDL_GetWindowSize(win,&preferences.width,&preferences.height);
        std::string error;if(!cart::saveEditorPreferences(preferencesFolder,preferences,error))std::fprintf(stderr,"editor preferences: %s\n",error.c_str());
    };

    // Mount the retail install exactly like the engine (loose + *.hpi, retail
    // precedence). The Vfs must outlive the MapView (it borrows it by ref).
    tak::hpi::Vfs vfs;
    try {
        vfs = tak::hpi::mountRetailRoot(dataRoot, tak::hpi::OverridePolicy::Full);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "mount %s: %s\n", dataRoot.c_str(), e.what());
        return 1;
    }
    const bool interactive=shotPath.empty() && exportPath.empty() && bundlePath.empty() && stampName.empty();
    std::filesystem::path recoveryFolder, recoveredFrom;
    std::string recoveredMapPath;
    if(char* pref=SDL_GetPrefPath("TAKengine","Cartographer")) {
        recoveryFolder=std::filesystem::u8path(pref);SDL_free(pref);
    }
    const auto recoveryFile=recoveryFolder/("recovery-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+".kmp");
    if(interactive && mapName.empty() && !newW && !recoveryFolder.empty()) {
        std::error_code ec;std::filesystem::path newest;
        for(const auto& entry:std::filesystem::directory_iterator(recoveryFolder,ec)) {
            const auto filename=pathText(entry.path().filename());
            if(filename.starts_with("recovery-") && entry.path().extension()==".kmp" &&
               (newest.empty() || entry.last_write_time(ec)>std::filesystem::last_write_time(newest,ec))) newest=entry.path();
        }
        if(!newest.empty()) {
            const SDL_MessageBoxButtonData buttons[]={{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Recover"},{0,2,"Discard recovery"},{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Cancel"}};
            SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION,win,"Recover map","An unsaved map recovery is available.",3,buttons,nullptr};
            int choice=0;if(hooks.recoveryChoice)choice=hooks.recoveryChoice();else SDL_ShowMessageBox(&box,&choice);
            if(choice==0)return 0;
            if(choice==2) {std::filesystem::remove(newest,ec);auto backup=newest;backup+=".bak";std::filesystem::remove(backup,ec);}
            if(choice==1) {
                try {
                    auto files=std::make_shared<tak::hpi::Vfs::Files>();
                    tak::hpi::Archive archive(newest);
                    for(const auto& entry:archive.entries()) if(!entry.isDirectory) {
                        (*files)[tak::hpi::MountSet::key(entry.path)]=archive.read(entry);
                        if(tak::vpath::extension(entry.path)==".tnt") {mapName=tak::vpath::stem(entry.path);recoveredMapPath=tak::hpi::MountSet::key(entry.path);}
                    }
                    if(recoveredMapPath.empty())throw std::runtime_error("Recovery archive contains no map terrain");
                    vfs.setMapFiles(files);recoveredFrom=newest;
                } catch(const std::exception& e) {SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Recovery failed",e.what(),win);return 1;}
            }
        }
    }

    // Resolve the map name to its .tnt VFS path (Maps/<name>.tnt or a .kmp's
    // kmap/<name>.tnt), same resolution the game uses. An empty name (or --new
    // without --save) starts a fresh blank map instead -- mapPath stays empty so
    // there are no sibling scenario files to load.
    std::string mapPath=recoveredMapPath;
    tak::tnt::Scenario scenario;
    std::unique_ptr<MapView> mapViewPtr;
    if (!mapName.empty()) {
        if(mapPath.empty())for (const auto& [name, path] : tak::hpi::listMaps(vfs)) {
            std::string lo = name;
            for (char& c : lo) c = char(std::tolower((unsigned char)c));
            std::string want = mapName;
            for (char& c : want) c = char(std::tolower((unsigned char)c));
            if (lo == want) { mapPath = path; break; }
        }
        if (mapPath.empty()) {
            std::fprintf(stderr, "map '%s' not found in %s\n", mapName.c_str(), dataRoot.c_str());
            return 1;
        }
        mapViewPtr = std::make_unique<MapView>(ren, vfs, mapPath);
        // Companion .ota (metadata + start positions), so a Save round-trips the
        // whole map. Missing/parse-fail = defaults.
        std::string otaPath = mapPath.substr(0, mapPath.rfind('.')) + ".ota";
        try {
            auto b = vfs.read(otaPath);
            scenario = tak::tnt::Scenario::parse(std::string(b.begin(), b.end()));
        } catch (const std::exception&) { /* no .ota: keep defaults */ }
    } else {
        int fw = newW > 0 ? newW : 8, fh = newH > 0 ? newH : 8;
        cart::SectionLibrary ns; ns.scan(vfs, newWorld);
        tak::terrain::Compositor nc(vfs);
        FreshMap fm = buildFreshMap(vfs, ns, nc, newWorld, fw, fh, randomTerrain);
        if (fm.map.width == 0) {
            std::fprintf(stderr, "new: could not build terrain for world '%s'\n", newWorld.c_str());
            return 1;
        }
        mapName = "Untitled";
        scenario.kingdom = newWorld; scenario.sizeW = fw; scenario.sizeH = fh;
        scenario.missionName = mapName;
        scenario.starts = std::move(fm.starts);
        mapViewPtr = std::make_unique<MapView>(ren, vfs, std::move(fm.map));
        SDL_SetWindowTitle(win, "Cartographer -- Untitled");
    }
    MapView& mapView = *mapViewPtr;
    mapView.setBilinear(true);
    std::fprintf(stderr,
                 "cartographer: editing '%s' (%s), %dx%d blocks; scenario '%s' "
                 "kingdom=%s, %zu start positions\n",
                 mapName.c_str(), mapPath.c_str(), mapView.map().blocksX,
                 mapView.map().blocksY, scenario.missionName.c_str(),
                 scenario.kingdom.c_str(), scenario.starts.size());

    // The map's scenario .crt, kept in full so a save preserves the trigger
    // rules, regions, and custom types the unit tool doesn't edit. `units` is
    // the editor's working view (map pixels); `scen` is everything else.
    std::string crtPath = mapPath.substr(0, mapPath.rfind('.')) + ".crt";
    tak::crt::Scenario scen = cart::loadScenario(vfs, crtPath);
    std::vector<cart::PlacedUnit> units = cart::toPlaced(scen);
    bool unitsEdited = false;

    // Use-only restriction: the allowed unit types (UPPERCASE). Loaded from the
    // sibling <map>.tdf when the OTA references one; empty = unrestricted.
    std::set<std::string> useOnly;
    std::string useOnlyTdf = mapPath.substr(0, mapPath.rfind('.')) + ".tdf";
    if (!scenario.useOnlyUnits.empty())
        for (auto& t : cart::loadUseOnly(vfs, useOnlyTdf)) useOnly.insert(t);
    // Set when the terrain is painted, so Save regenerates the minimaps (an
    // unedited save stays byte-identical to the source; an edited one gets a
    // fresh overview reflecting the paint).
    bool edited = false;
    bool minimapRefreshNeeded=true;
    // Unsaved-changes flag for the exit prompt. Set by every edit (terrain,
    // features, units, starts, scenario props, use-only, triggers), cleared on a
    // successful save. Broader than `edited` (which only gates minimap regen).
    bool dirty = mapPath.empty() || !recoveredFrom.empty();
    bool historyPending=false, resetHistory=false;
    bool overlayInvalidated=false;
    cart::History history;
    auto historySnapshot = [&]() {return cart::historyState(mapView.map(),scenario,scen,units,useOnly,mapName);};
    history.reset(historySnapshot(),!dirty);
    auto commitHistory = [&]() {
        if(resetHistory) {history.reset(historySnapshot(),false);resetHistory=false;}
        else if(historyPending) history.commit(historySnapshot());
        historyPending=false;dirty=history.dirty();
    };
    bool wantNew = false;   // deferred "open New Map" after a discard confirmation
    std::string saveError;
    auto serializeDocument = [&](const std::string& name) {
        mapView.quiesce();
        if (edited) {
            std::string wld = scenario.kingdom.empty() ? "aramon" : scenario.kingdom;
            std::transform(wld.begin(),wld.end(),wld.begin(),[](unsigned char c){return char(std::tolower(c));});
            cart::generateMinimaps(mapView.editMap(),mapView.compositor(),cart::loadWorldPalette(vfs,wld));
            minimapRefreshNeeded=true;
        }
        return cart::documentFiles(mapView.map(),scenario,scen,units,useOnly,name);
    };
    auto saveMap = [&](const std::string& tntPath) -> bool {
        try {
            const auto path=std::filesystem::u8path(tntPath);
            const auto base=path.stem().u8string();
            commitHistory();
            const bool ok=cart::writeDocumentFiles(path.parent_path(),serializeDocument(std::string(base.begin(),base.end())),saveError);
            if(ok) {history.markSaved();dirty=false;preferences.remember(pathText(std::filesystem::absolute(path)));if(interactive)persistPreferences();}return ok;
        } catch(const std::exception& e) {saveError=e.what();return false;}
    };
    auto saveBundle = [&](const std::string& kmpPath) -> bool {
        try {
            const auto path=std::filesystem::u8path(kmpPath);
            const auto base=path.stem().u8string();
            commitHistory();
            const bool ok=cart::writeDocumentBundle(path,serializeDocument(std::string(base.begin(),base.end())),saveError);
            if(ok) {history.markSaved();dirty=false;preferences.remember(pathText(std::filesystem::absolute(path)));if(interactive)persistPreferences();}return ok;
        } catch(const std::exception& e) {saveError=e.what();return false;}
    };

    // Section-prefab palette for this map's world (falls back to aramon).
    cart::SectionLibrary sections;
    std::string world = scenario.kingdom.empty() ? "aramon" : scenario.kingdom;
    sections.scan(vfs, world);
    cart::FeatureLibrary features;
    features.scan(vfs, world);
    std::fprintf(stderr, "cartographer: %zu placeable features for '%s'\n",
                 features.list().size(), world.c_str());

    // Unit-type list for the palette (units + scen loaded above, before saveMap).
    std::vector<std::string> unitTypes = cart::unitTypeNames(vfs);
    tak::sim::TypeRegistry unitRegistry;tak::sim::setupRegistry(unitRegistry,vfs,false);
    std::fprintf(stderr, "cartographer: %zu placed units, %zu unit types\n",
                 units.size(), unitTypes.size());
    std::fprintf(stderr, "cartographer: %zu sections for world '%s'\n",
                 sections.list().size(), world.c_str());

    // Stamp a named section into the map at block (bx,by), returning whether it
    // matched a section. The editor calls this on canvas click; --stamp tests it.
    auto stampByName = [&](const std::string& name, int bx, int by) -> bool {
        for (const auto& s : sections.list())
            if (s.name == name) {
                const tak::tnt::Map* sec = sections.load(vfs, s.path);
                if (sec && cart::stampSection(mapView.editMap(), *sec, bx, by)) {
                    mapView.tilesEdited(bx,by,sec->blocksX,sec->blocksY);
                    edited = true; dirty = true; historyPending=true;
                    return true;
                }
            }
        return false;
    };

    // Headless one-shot: --stamp <name> <bx> <by> then save (edit-path test).
    if (!stampName.empty()) {
        bool hit = stampByName(stampName, stampBX, stampBY);
        std::fprintf(stderr, "stamp '%s' at (%d,%d): %s\n", stampName.c_str(),
                     stampBX, stampBY, hit ? "OK" : "no such section");
        bool ok = hit && saveMap(exportPath.empty() ? (outDir + "/" + mapName + "-edit.tnt")
                                                     : exportPath);
        if(!ok) std::fprintf(stderr,"save: %s\n",saveError.c_str());
        return ok ? 0 : 1;
    }

    // Headless one-shot export: --save <file.tnt> writes and exits (round-trip
    // / convert path, also how the save is regression-tested).
    if (!exportPath.empty()) {
        bool ok = saveMap(exportPath);
        if(!ok) std::fprintf(stderr,"save: %s\n",saveError.c_str());
        return ok ? 0 : 1;
    }
    // Headless one-shot: --bundle <file.kmp> writes the packed map and exits.
    if (!bundlePath.empty()) {
        bool ok = saveBundle(bundlePath);
        if(!ok) std::fprintf(stderr,"save: %s\n",saveError.c_str());
        return ok ? 0 : 1;
    }

    // --- Interactive editor state ---------------------------------------------
    constexpr int kPaletteW = 200;   // left section-palette panel
    constexpr int kPaletteTop=kMenuH+76;
    constexpr int kCellH=106;
    constexpr int kThumb = 88;       // section thumbnail cell (px)
    const int cols = std::max(1, (kPaletteW - 8) / (kThumb + 4));
    int selected = sections.list().empty() ? -1 : 0;   // section index (TERRAIN)
    int selectedFeat = features.list().empty() ? -1 : 0; // feature index (FEATURES)
    int paletteScroll = 0;
    bool showGrid = false;
    cart::StampLayers stampLayers;
    static const float kZoomLevels[5] = {1.0f, 0.75f, 0.5f, 0.25f, 0.125f};

    // Feature-sprite textures (GAF frame 0), cached by feature name, used both in
    // the palette and to draw placed features on the canvas.
    std::map<std::string, SDL_Texture*> featTex;
    auto featTextureFor = [&](const cart::FeatureRef& r) -> SDL_Texture* {
        auto it = featTex.find(r.name);
        if (it != featTex.end()) return it->second;
        SDL_Texture* t = nullptr;
        const cart::FeatSprite* sp = features.sprite(vfs, r);
        if (sp && sp->w > 0) {
            t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                  sp->w, sp->h);
            if (t) {
                SDL_UpdateTexture(t, nullptr, sp->rgba.data(), sp->w * 4);
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            }
        }
        featTex[r.name] = t;
        return t;
    };

    // Lazy section thumbnails: render a prefab's terrain (Compositor) once into a
    // small texture, cached by path. Only visible cells ever render.
    std::map<std::string, SDL_Texture*> thumbs;
    auto thumbFor = [&](const std::string& path) -> SDL_Texture* {
        auto it = thumbs.find(path);
        if (it != thumbs.end()) return it->second;
        SDL_Texture* t = nullptr;
        if (const tak::tnt::Map* sec = sections.load(vfs, path)) {
            tak::jpeg::Image img = mapView.compositor().renderMap(*sec);
            if (img.width > 0 && img.height > 0) {
                t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STATIC, img.width, img.height);
                if (t) {
                    SDL_UpdateTexture(t, nullptr, img.rgba.data(), img.width * 4);
                    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
                }
            }
        }
        thumbs[path] = t;   // cache even null (a prefab that won't render)
        return t;
    };

    int lastStampX=-1,lastStampY=-1;
    auto stampAtMouse = [&](int mx, int my, int w, int h) {
        if (selected < 0 || mx < kPaletteW) return;   // palette side, not the canvas
        const auto& s = sections.list()[size_t(selected)];
        const tak::tnt::Map* sec = sections.load(vfs, s.path);
        if (!sec || sec->blocksX <= 0) return;
        // Canvas-local -> world -> block, snapped to the section's own size so
        // sections tile cleanly.
        float lx = float(mx - kPaletteW), ly = float(my - kMenuH);
        int blkX = int((mapView.offX() + lx / mapView.zoom()) / 32.0f);
        int blkY = int((mapView.offY() + ly / mapView.zoom()) / 32.0f);
        int snapX = (blkX / sec->blocksX) * sec->blocksX;
        int snapY = (blkY / sec->blocksY) * sec->blocksY;
        if(snapX==lastStampX && snapY==lastStampY)return;
        lastStampX=snapX;lastStampY=snapY;
        // Painting changes tile arrays only. Workers use tile snapshots and
        // immutable source sections; no whole-map worker/cache reset is needed.
        if (cart::stampSection(mapView.editMap(), *sec, snapX, snapY,stampLayers)) {
            mapView.tilesEdited(snapX,snapY,sec->blocksX,sec->blocksY);
            edited = true; dirty = true; historyPending=true;
        }
        (void)w; (void)h;
    };

    // --- Object tools (terrain paint, feature placement, start positions) -----
    enum Tool { TERRAIN, FEATURES, UNITS, STARTS };
    int selectedType = unitTypes.empty() ? -1 : 0;   // unit-type index (UNITS palette)
    int currentPlayer = 0;                            // player id new units get
    int draggingUnit = -1;                            // index into units while dragging
    // 8 player marker colours.
    static const Uint8 kPlayerCol[9][3] = {
        {80,120,255},{230,70,60},{80,200,90},{235,210,80},
        {200,90,220},{80,210,220},{240,140,40},{200,200,210},{150,150,150}};
    // Placed unit whose marker is near screen (mx,my), or -1.
    auto unitAt = [&](int mx, int my) -> int {
        for (int i = int(units.size()) - 1; i >= 0; --i) {
            float sx = kPaletteW + (units[i].x - mapView.offX()) * mapView.zoom();
            float sy = kMenuH + (units[i].z - mapView.offY()) * mapView.zoom();
            if (std::abs(sx - mx) <= 8 && std::abs(sy - my) <= 8) return i;
        }
        return -1;
    };
    Tool tool = TERRAIN;
    enum EditMode { MODE_PLACE,MODE_SELECT,MODE_ERASE,MODE_PAN };
    EditMode editMode=MODE_PLACE;
    cart::UnitSelection selectedUnits;
    bool selectionBox=false,selectionAppend=false;
    int selectionX0=0,selectionZ0=0,selectionX1=0,selectionZ1=0;
    std::string paletteSearch;
    cart::TextEdit paletteEditor;
    bool paletteFocus=false;
    int categoryIndex=0,factionIndex=0;
    std::vector<int> paletteItems;
    std::vector<std::string> paletteCategories{"ALL"};
    std::string paletteKey;
    auto lowerText=[](std::string value) {std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});return value;};
    auto unitInfo=[&](int index) {return index>=0 && index<int(unitTypes.size())?unitRegistry.find(lowerText(unitTypes[size_t(index)])):nullptr;};
    auto unitCategory=[&](int index)->std::string {
        const auto* type=unitInfo(index);if(!type)return "OTHER";
        if(type->onMana)return "MANA";
        if(type->isBuilder)return "BUILDERS";
        if(type->isStructure())return "BUILDINGS";
        if(type->canFly)return "AIR";
        if(type->domain==tak::sim::UnitType::Domain::Water)return "NAVAL";
        return "GROUND";
    };
    auto paletteLabel=[&](int index) {
        if(tool==UNITS) {const auto* info=unitInfo(index);return info?info->name+" ("+unitTypes[size_t(index)]+")":unitTypes[size_t(index)];}
        if(tool==FEATURES)return features.list()[size_t(index)].name;
        if(tool==STARTS)return "PLAYER "+std::to_string(scenario.starts[size_t(index)].number);
        return sections.list()[size_t(index)].name;
    };
    auto refreshPalette=[&]() {
        const auto key=std::to_string(int(tool))+"/"+std::to_string(categoryIndex)+"/"+std::to_string(factionIndex)+"/"+world+"/"+paletteSearch+"/"+std::to_string(scenario.starts.size());
        if(key==paletteKey)return;
        const bool toolChanged=paletteKey.empty() || paletteKey[0]!=key[0];
        paletteKey=key;
        const int count=tool==UNITS?int(unitTypes.size()):tool==FEATURES?int(features.list().size()):tool==STARTS?int(scenario.starts.size()):int(sections.list().size());
        auto category=[&](int i) {return tool==UNITS?unitCategory(i):tool==FEATURES?features.list()[size_t(i)].category:tool==STARTS?std::string("STARTS"):sections.list()[size_t(i)].category;};
        std::set<std::string> categories;for(int i=0;i<count;++i)categories.insert(category(i));
        paletteCategories={"ALL"};paletteCategories.insert(paletteCategories.end(),categories.begin(),categories.end());
        if(toolChanged || categoryIndex>=int(paletteCategories.size()))categoryIndex=0;
        paletteItems.clear();const auto query=lowerText(paletteSearch);
        static const char* sides[]={"","ara","tar","ver","zon","cre"};
        for(int i=0;i<count;++i) {
            if(categoryIndex && category(i)!=paletteCategories[size_t(categoryIndex)])continue;
            if(tool==UNITS && factionIndex && !lowerText(unitTypes[size_t(i)]).starts_with(sides[factionIndex]))continue;
            if(!query.empty() && lowerText(paletteLabel(i)+" "+category(i)).find(query)==std::string::npos)continue;
            paletteItems.push_back(i);
        }
        paletteScroll=0;
    };

    // Place/erase a feature in the .tnt feature plane at the mouse cell.
    auto placeFeature = [&](int mx, int my, bool erase) {
        if (selectedFeat < 0 || mx < kPaletteW) return;
        float wx = mapView.offX() + float(mx - kPaletteW) / mapView.zoom();
        float wz = mapView.offY() + float(my - kMenuH) / mapView.zoom();
        int cx = int(wx / 16.0f), cz = int(wz / 16.0f);
        auto& mp = mapView.editMap();
        if (cx < 0 || cz < 0 || cx >= mp.width || cz >= mp.height) return;
        size_t ci = size_t(cz) * mp.width + cx;
        if (erase) { mp.features[ci] = 0xFFFF; edited = true; dirty = true; historyPending=true; return; }
        const std::string& name = features.list()[size_t(selectedFeat)].name;
        uint16_t idx = 0xFFFF;   // intern the feature name into the map's table
        for (size_t i = 0; i < mp.featureNames.size(); ++i)
            if (mp.featureNames[i] == name) { idx = uint16_t(i); break; }
        if (idx == 0xFFFF) { mp.featureNames.push_back(name); idx = uint16_t(mp.featureNames.size() - 1); }
        mp.features[ci] = idx;
        edited = true; dirty = true; historyPending=true;
    };
    int draggingStart = -1;           // index into scenario.starts while dragging
    bool clearArm = false, clearDrag = false;   // Clear Area drag-box (screen px)
    int clx0 = 0, cly0 = 0, clx1 = 0, cly1 = 0;
    // Canvas mouse -> map cell (16px). Returns false if off the canvas/map.
    auto mouseCell = [&](int mx, int my, int& cx, int& cz) -> bool {
        if (mx < kPaletteW) return false;
        float wx = mapView.offX() + float(mx - kPaletteW) / mapView.zoom();
        float wz = mapView.offY() + float(my - kMenuH) / mapView.zoom();
        cx = int(wx / 16.0f); cz = int(wz / 16.0f);
        return cx >= 0 && cz >= 0 && cx < mapView.map().width && cz < mapView.map().height;
    };
    // Start position whose marker is near screen (mx,my), or -1.
    auto startAt = [&](int mx, int my) -> int {
        for (int i = 0; i < int(scenario.starts.size()); ++i) {
            float sx = kPaletteW + (scenario.starts[i].xpos * 16.0f - mapView.offX()) * mapView.zoom();
            float sy = kMenuH + (scenario.starts[i].zpos * 16.0f - mapView.offY()) * mapView.zoom();
            if (std::abs(sx - mx) <= 10 && std::abs(sy - my) <= 10) return i;
        }
        return -1;
    };

    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> minimapTexture(nullptr,SDL_DestroyTexture);
    const uint8_t* minimapSource=nullptr;
    bool minimapDrag=false;
    auto refreshMinimap=[&]() {
        const auto& map=mapView.map();
        if(map.minimap.empty()) {minimapTexture.reset();minimapSource=nullptr;return;}
        if(!minimapRefreshNeeded && map.minimap.data()==minimapSource)return;
        auto palette=cart::loadWorldPalette(vfs,world);
        std::vector<uint8_t> rgba(map.minimap.size()*4);
        for(size_t i=0;i<map.minimap.size();++i) {const auto& color=palette.rgba[map.minimap[i]];std::copy(color,color+4,rgba.begin()+i*4);}
        minimapTexture.reset(SDL_CreateTexture(ren,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,map.minimapW,map.minimapH));
        if(minimapTexture) {SDL_UpdateTexture(minimapTexture.get(),nullptr,rgba.data(),map.minimapW*4);SDL_SetTextureScaleMode(minimapTexture.get(),SDL_ScaleModeLinear);}
        minimapSource=map.minimap.data();minimapRefreshNeeded=false;
    };

    // --- Modal dialogs (New, Scenario Properties, Resize, Unit/Rule props, Msg) -
    enum Modal { M_NONE, M_SCENARIO, M_RESIZE, M_UNIT, M_MESSAGE, M_RULE, M_CONFIRM, M_NEW, M_QUITSAVE, M_SAVEAS, M_OPENPATH, M_REGION, M_GENERATOR, M_GENERATING, M_GENERATED, M_ANALYZING };
    static constexpr int kMaxFields = 9;
    std::function<void()> confirmAction;   // M_CONFIRM: run on OK
    Modal modal = M_NONE;
    std::string mfOriginal[kMaxFields];
    std::string mf[kMaxFields];              // field buffers
    bool mfNumeric[kMaxFields] = {};
    // A field with a non-null choice list is a dropdown (click opens the list),
    // not a typed text box. Used by the New Map dialog for size/world selection.
    const std::vector<std::string>* mfChoices[kMaxFields] = {};
    int mDropScroll=0;
    std::vector<std::string> ruleChoices[kMaxFields];
    int mDropOpen = -1;                       // which field's dropdown list is open (-1 none)
    std::vector<SDL_Rect> mDropRects;         // hit rects of the open list's rows
    SDL_Rect mRandom{};                       // New Map: the RANDOM button rect
    // Dropdown option lists (New Map): map sizes in units, and the four worlds.
    const std::vector<std::string> kSizeOpts = {"8", "16", "24", "32", "48", "64"};
    const std::vector<std::string> kWorldOpts = {"aramon", "veruna", "taros", "zhon", "creon"};
    tak::mapgen::Params generatorParams;
    std::array<std::string,4> generatorDraft;
    const std::vector<std::string> generatorLayouts={tak::mapgen::layoutName(0),tak::mapgen::layoutName(1),tak::mapgen::layoutName(2)};
    const char* mLabel[kMaxFields] = {};
    std::string mTitle;
    int mN = 0;                              // active field count
    int mfocus = 0;
    cart::TextEdit fieldEditor;
    int descriptionScroll=0;bool descriptionFollowCaret=true;
    std::function<bool(const std::string&)> openDocument;
    std::vector<std::pair<std::string,std::string>> openCatalog,openMatches;
    SDL_Rect openList{};int openScroll=0;std::string openFilterPrevious;bool openFilterDirty=true;
    auto filterOpenMaps=[&]() {
        const auto query=lowerText(mf[0]);if(!openFilterDirty && query==openFilterPrevious)return;
        openFilterDirty=false;openFilterPrevious=query;openMatches.clear();openScroll=0;
        for(const auto& entry:openCatalog)if(query.empty() || lowerText(entry.first).find(query)!=std::string::npos || lowerText(entry.second).find(query)!=std::string::npos)openMatches.push_back(entry);
        openScroll=std::clamp(openScroll,0,std::max(0,int(openMatches.size())*20-openList.h));
    };
    bool regionsOpen=false,showRegions=true;
    int regionSelected=-1,regionScroll=0,editRegion=-1;
    SDL_Rect regionList{},regionNew{},regionEdit{},regionDelete{},regionDone{};
    int editUnit = -1;                       // UNITS: index being edited (M_UNIT)
    tak::crt::Rule* editRule = nullptr;      // M_RULE: rule whose params are edited
    std::vector<std::string> mMsg;           // M_MESSAGE: wrapped text lines
    SDL_Rect mBox[kMaxFields]{}, mOK{}, mCancel{}, mQuit{};   // render-computed hit rects
    // Pop a message box (word-wrapped to ~46 cols) — used by Check Map.
    auto openMessage = [&](const std::string& title, const std::string& text) {
        mMsg.clear();
        std::string line;
        std::string word;
        auto flush = [&]() { if (!line.empty()) { mMsg.push_back(line); line.clear(); } };
        for (size_t i = 0; i <= text.size(); ++i) {
            char c = i < text.size() ? text[i] : ' ';
            if (c == ' ' || c == '\n') {
                if (line.size() + word.size() + 1 > 46) flush();
                if (!line.empty()) line += ' ';
                line += word; word.clear();
                if (c == '\n') flush();
            } else word += c;
        }
        flush();
        mTitle = title; modal = M_MESSAGE; mN = 0; mfocus = 0;
    };
    // A yes/no confirmation that runs `action` on OK (used by Clear Area).
    auto openConfirm = [&](const std::string& title, const std::string& text,
                           std::function<void()> action) {
        openMessage(title, text);
        modal = M_CONFIRM; confirmAction = std::move(action);
    };
    auto openModal = [&](Modal m, int unitIdx = -1) {
        mfocus = 0; editUnit = unitIdx;descriptionScroll=0;descriptionFollowCaret=true;
        mDropOpen = -1;
        for (auto& c : mfChoices) c = nullptr;   // default: plain text fields
        if (m == M_SCENARIO) {
            mTitle = "SCENARIO PROPERTIES"; mN = 2;
            mLabel[0] = "SCENARIO NAME"; mf[0] = scenario.missionName;        mfNumeric[0] = false;
            mLabel[1] = "DESCRIPTION";   mf[1] = scenario.missionDescription; mfNumeric[1] = false;
        } else if(m==M_GENERATOR) {
            for(int i=0;i<4;++i)generatorDraft[i]=mf[i];
            mTitle="RANDOM MAP SETTINGS";mN=8;
            const auto fields=cart::generatorFields(generatorParams);
            const char* labels[]={"SEED (REPRODUCIBLE)","PLAYERS (2-8)","LAYOUT","TREES (0-255)","ROCKS (0-255)","MANA (0-255)","WATER INTENSITY (0-255)","RELIEF (0-255)"};
            for(int i=0;i<8;++i) {mf[i]=fields[i];mLabel[i]=labels[i];mfNumeric[i]=i!=2;}
            mfChoices[2]=&generatorLayouts;
        } else if(m==M_REGION) {
            editRegion=unitIdx;
            tak::crt::Region region;
            if(unitIdx>=0 && unitIdx<int(scen.regions.size()))region=scen.regions[unitIdx];
            else {
                region.name="Region "+std::to_string(scen.regions.size()+1);
                region.x1=std::clamp(int(mapView.offX()/16),0,mapView.map().width-1);
                region.z1=std::clamp(int(mapView.offY()/16),0,mapView.map().height-1);
                region.x2=std::min(region.x1+15,mapView.map().width-1);
                region.z2=std::min(region.z1+15,mapView.map().height-1);
            }
            mTitle=unitIdx<0?"NEW REGION":"REGION PROPERTIES";mN=5;
            mLabel[0]="NAME (RENAMES UPDATE RULES)";mf[0]=region.name;mfNumeric[0]=false;
            mLabel[1]="LEFT CELL (INCLUSIVE)";mf[1]=std::to_string(region.x1);
            mLabel[2]="TOP CELL (INCLUSIVE)";mf[2]=std::to_string(region.z1);
            mLabel[3]="RIGHT CELL (INCLUSIVE)";mf[3]=std::to_string(region.x2);
            mLabel[4]="BOTTOM CELL (INCLUSIVE)";mf[4]=std::to_string(region.z2);
            for(int i=1;i<5;++i)mfNumeric[i]=true;
        } else if (m == M_SAVEAS) {
            mTitle="SAVE MAP AS";mN=2;
            mLabel[0]="MAP FILE NAME";mf[0]=mapName;mfNumeric[0]=false;
            mLabel[1]="FOLDER";mf[1]=outDir;mfNumeric[1]=false;
        } else if (m == M_OPENPATH) {
            mTitle="OPEN MAP";mN=1;
            mLabel[0]="SEARCH MAPS OR ENTER KMP / TNT PATH";mf[0]="";mfNumeric[0]=false;
            openCatalog.clear();openScroll=0;openFilterDirty=true;
            for(const auto& recent:preferences.recent)openCatalog.emplace_back("Recent: "+recent,recent);
            for(const auto& [name,path]:tak::hpi::listMaps(vfs))openCatalog.emplace_back(name,path);
            filterOpenMaps();
        } else if (m == M_RESIZE) {
            mTitle = "RESIZE MAP"; mN = 2;
            mLabel[0] = "WIDTH (UNITS)";  mf[0] = std::to_string(mapView.map().width / 32);  mfNumeric[0] = true;
            mLabel[1] = "HEIGHT (UNITS)"; mf[1] = std::to_string(mapView.map().height / 32); mfNumeric[1] = true;
        } else if (m == M_NEW) {
            // Name is typed; size + world are dropdowns. RANDOM is a separate
            // button (drawn in the render pass) that generates procedural terrain
            // for the chosen size/world and jumps straight to it.
            generatorParams.seed=uint64_t(SDL_GetPerformanceCounter());
            mTitle = "NEW MAP"; mN = 4;
            mLabel[0] = "MAP NAME";  mf[0] = "Untitled"; mfNumeric[0] = false;
            mLabel[1] = "WIDTH";     mf[1] = "8";  mfChoices[1] = &kSizeOpts;
            mLabel[2] = "HEIGHT";    mf[2] = "8";  mfChoices[2] = &kSizeOpts;
            std::string w0 = world;
            std::transform(w0.begin(), w0.end(), w0.begin(), [](unsigned char c){return char(std::tolower(c));});
            if (std::find(kWorldOpts.begin(), kWorldOpts.end(), w0) == kWorldOpts.end())
                w0 = "aramon";
            mLabel[3] = "WORLD";     mf[3] = w0;   mfChoices[3] = &kWorldOpts;
        } else if (m == M_UNIT && unitIdx >= 0 && unitIdx < int(units.size())) {
            const auto& u = units[size_t(unitIdx)];
            mTitle = selectedUnits.indices.size()>1?"SELECTED UNIT PROPERTIES":"UNIT PROPERTIES"; mN = 9;
            mLabel[0] = "PLAYER (0-7, 8=NEUTRAL)";  mf[0] = std::to_string(u.player);      mfNumeric[0] = true;
            mLabel[1] = "HEALTH %";      mf[1] = std::to_string(u.health);      mfNumeric[1] = true;
            mLabel[2] = "ARMOR %";       mf[2] = std::to_string(u.armor);       mfNumeric[2] = true;
            mLabel[3] = "WEAPON %";      mf[3] = std::to_string(u.weapon);      mfNumeric[3] = true;
            mLabel[4] = "VETERAN (0-9)"; mf[4] = std::to_string(u.veteran);     mfNumeric[4] = true;
            mLabel[5] = "ANGLE (DEG)";   mf[5] = std::to_string(int(u.angle));  mfNumeric[5] = true;
            mLabel[6] = selectedUnits.indices.size()>1?"NAME (SINGLE UNIT ONLY)":"UNIQUE NAME";mf[6]=u.name;mfNumeric[6]=false;
            mLabel[7] = "X (CELL)";mf[7]=std::to_string(int(u.x/16));mfNumeric[7]=true;
            mLabel[8] = "Z (CELL)";mf[8]=std::to_string(int(u.z/16));mfNumeric[8]=true;
        } else {
            return;   // nothing to open
        }
        for(int i=0;i<mN;++i)mfOriginal[i]=mf[i];
        modal = m;fieldEditor.focus(mf[mfocus]);
        SDL_StartTextInput();
    };
    // Build a fresh map from the New Map dialog's fields and switch to it. `random`
    // picks procedural terrain (the RANDOM button) vs a flat stamp (CREATE). On
    // failure it pops a message and leaves the current map untouched; on success it
    // closes the dialog. Shared by the CREATE and RANDOM buttons.
    std::future<FreshMap> generatorJob;
    std::optional<FreshMap> generatedPreview;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> generatedTexture(nullptr,SDL_DestroyTexture);
    std::string generatingName,generatingWorld;
    bool cancelGeneration=false,quitAfterGeneration=false;
    auto adoptFreshMap=[&](FreshMap fm,const std::string& nm,const std::string& wld) {
        sections.scan(vfs,wld);features.scan(vfs,wld);
        for(auto& [key,texture]:featTex)if(texture)SDL_DestroyTexture(texture);
        featTex.clear();paletteKey.clear();
        mapView.quiesce();
        mapView.editMap() = std::move(fm.map);minimapSource=nullptr;
        mapView.tilesEdited();
        mapView.setOffset(0, 0);
        world = wld;
        scenario = tak::tnt::Scenario{};
        scenario.kingdom = wld; scenario.sizeW = mapView.map().width/32; scenario.sizeH = mapView.map().height/32;
        scenario.missionName = nm;
        if(!fm.recipe.empty())scenario.missionDescription="Generator recipe: "+fm.recipe;
        scenario.starts = std::move(fm.starts);
        mapName = nm;
        selectedUnits.indices.clear();selectedUnits.dragOrigins.clear();
        units.clear(); scen = tak::crt::Scenario{}; useOnly.clear();resetHistory=true;
        selected = sections.list().empty() ? -1 : 0;
        selectedFeat = features.list().empty() ? -1 : 0;
        paletteScroll = 0;
        edited = true;
        SDL_SetWindowTitle(win, ("Cartographer -- " + mapName).c_str());
        dirty = true; historyPending=true;
        modal = M_NONE; SDL_StopTextInput();
    };
    auto applyNewMap = [&](bool random) {
        const auto* fields=random?generatorDraft.data():mf;
        std::string nm=fields[0].empty()?"Untitled":fields[0],wld=fields[3];
        int wu=std::clamp(std::atoi(fields[1].c_str()),1,64),hu=std::clamp(std::atoi(fields[2].c_str()),1,64);
        std::transform(wld.begin(),wld.end(),wld.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(random) {
            auto params=generatorParams;params.widthCells=uint16_t(wu*32);params.heightCells=uint16_t(hu*32);
            const char* worlds[]={"aramon","taros","veruna","zhon","creon"};
            for(uint8_t i=0;i<5;++i)if(wld==worlds[i])params.mapType=i;
            params=tak::mapgen::sanitize(params);
            generatingName=nm;generatingWorld=wld;cancelGeneration=quitAfterGeneration=false;
            // The busy modal prevents VFS replacement until this read-only job ends.
            generatorJob=std::async(std::launch::async,[&,params,wld] {
                const auto recipe=tak::mapgen::encodeMapId(params);
                auto result=tak::mapgen::generate(params,vfs);FreshMap map;
                map.map=std::move(result.map);map.recipe=recipe;
                tak::terrain::Compositor compositor(vfs);
                cart::generateMinimaps(map.map,compositor,cart::loadWorldPalette(vfs,wld));
                int number=1;for(auto [x,z]:result.starts)map.starts.push_back({number++,x,z});
                return map;
            });
            modal=M_GENERATING;SDL_StopTextInput();return;
        }
        mapView.quiesce();
        try {
            cart::SectionLibrary freshSections;freshSections.scan(vfs,wld);
            auto map=buildFreshMap(vfs,freshSections,mapView.compositor(),wld,wu,hu,false);
            if(!map.map.width) {openMessage("NEW MAP","Could not build terrain for this world.");return;}
            adoptFreshMap(std::move(map),nm,wld);
        } catch(const std::exception& error) {openMessage("NEW MAP FAILED",error.what());}
    };
    auto applyModal = [&]() {
        const auto applying=modal;
        if(modal==M_GENERATOR) {
            cart::GeneratorFields fields;for(int i=0;i<8;++i)fields[i]=mf[i];std::string error;
            if(!cart::parseGeneratorFields(fields,generatorParams,error)) {openMessage("GENERATOR SETTINGS",error);return;}
            applyNewMap(true);return;
        } else if(modal==M_REGION) {
            int cells[4];
            for(int i=0;i<4;++i) {
                const auto& field=mf[i+1];const auto parsed=std::from_chars(field.data(),field.data()+field.size(),cells[i]);
                if(parsed.ec!=std::errc{} || parsed.ptr!=field.data()+field.size()) {openMessage("INVALID REGION","Corners must be whole cell numbers.");return;}
            }
            std::string error;
            if(!cart::setRegion(scen,editRegion,{mf[0],cells[0],cells[1],cells[2],cells[3]},mapView.map().width,mapView.map().height,error)) {openMessage("INVALID REGION",error);return;}
            regionSelected=editRegion<0?int(scen.regions.size())-1:editRegion;
            dirty=true;historyPending=true;
        } else if(modal==M_SAVEAS) {
            if(!cart::validDocumentName(mf[0])) {openMessage("INVALID NAME","Choose a map name without path separators or reserved filename characters.");return;}
            const auto nextName=mf[0],nextDir=mf[1];
            const auto path=pathText(std::filesystem::u8path(nextDir)/std::filesystem::u8path(nextName+".kmp"));
            auto performSave=[&,path,nextName,nextDir] {
                if(!saveBundle(path)) {openMessage("SAVE FAILED",saveError);return;}
                mapName=nextName;outDir=nextDir;historyPending=true;commitHistory();history.markSaved();dirty=false;
                openMessage("SAVED",path);
            };
            if(std::filesystem::exists(std::filesystem::u8path(path)) && (nextName!=mapName || nextDir!=outDir)) {
                openConfirm("REPLACE MAP", "Replace the existing map? Its previous version will be retained as a .bak file.",performSave);return;
            }
            performSave();return;
        } else if(modal==M_OPENPATH) {
            const auto request=mf[0];
            if(dirty) {openConfirm("OPEN MAP","Discard unsaved changes and open this map?",[&,request] {openDocument(request);});return;}
            if(!openDocument(request))return;
        } else if (modal == M_SCENARIO) {
            scenario.missionName = mf[0];
            scenario.missionDescription = mf[1]; dirty = true; historyPending=true;
        } else if (modal == M_RESIZE) {
            int wu=0,hu=0;
            const auto widthResult=std::from_chars(mf[0].data(),mf[0].data()+mf[0].size(),wu);
            const auto heightResult=std::from_chars(mf[1].data(),mf[1].data()+mf[1].size(),hu);
            if(widthResult.ec!=std::errc{} || widthResult.ptr!=mf[0].data()+mf[0].size() ||
               heightResult.ec!=std::errc{} || heightResult.ptr!=mf[1].data()+mf[1].size() || wu<1 || wu>64 || hu<1 || hu>64) {
                openMessage("RESIZE MAP","Width and height must be whole numbers from 1 to 64.");return;
            }
            for(const auto& start:scenario.starts) if(start.xpos>=wu*32 || start.zpos>=hu*32) {
                openMessage("RESIZE MAP","Move or remove start positions outside the new map first.");return;
            }
            for(const auto& unit:units) if(unit.x>=wu*512 || unit.z>=hu*512) {
                openMessage("RESIZE MAP","Move or remove units outside the new map first.");return;
            }
            for(const auto& region:scen.regions) if(std::max(region.x1,region.x2)>=wu*32 || std::max(region.z1,region.z2)>=hu*32) {
                openMessage("RESIZE MAP","Resize or remove regions outside the new map first.");return;
            }
            mapView.quiesce();minimapSource=nullptr;
            std::string wld = scenario.kingdom.empty() ? "aramon" : scenario.kingdom;
            cart::resizeMap(mapView.editMap(), mapView.compositor(),
                            cart::loadWorldPalette(vfs, wld), wu, hu);
            scenario.sizeW = wu; scenario.sizeH = hu;
            mapView.tilesEdited();
            edited = true; dirty = true; historyPending=true;
        } else if (modal == M_NEW) {
            applyNewMap(false);   // CREATE = flat stamp; closes on success
            return;               // (RANDOM is handled at its button click)
        } else if (modal == M_UNIT && editUnit >= 0 && editUnit < int(units.size())) {
            const auto& origin=units[size_t(editUnit)];
            auto targets=selectedUnits.indices;
            if(!targets.count(editUnit))targets={editUnit};
            const float dx=mf[7]==mfOriginal[7]?0:std::atoi(mf[7].c_str())*16.0f+8-origin.x;
            const float dz=mf[8]==mfOriginal[8]?0:std::atoi(mf[8].c_str())*16.0f+8-origin.z;
            if(targets.size()==1 && mf[6]!=origin.name) {
                if(mf[6].size()>255) {openMessage("UNIT NAME","Use a name shorter than 256 bytes.");return;}
                for(int i=0;i<int(units.size());++i)if(i!=editUnit && !mf[6].empty() && lowerText(units[i].name)==lowerText(mf[6])) {
                    openMessage("UNIT NAME","That unique name is already used by another unit.");return;
                }
            }
            for(int i:targets)if(i>=0 && i<int(units.size()) && (units[i].x+dx<0 || units[i].z+dz<0 || units[i].x+dx>=mapView.map().width*16 || units[i].z+dz>=mapView.map().height*16)) {
                openMessage("UNIT POSITION","The selection would extend outside the map.");return;
            }
            for(int i:targets)if(i>=0 && i<int(units.size())) {
                auto& u=units[size_t(i)];
                if(mf[0]!=mfOriginal[0])u.player=std::clamp(std::atoi(mf[0].c_str()),0,8);
                if(mf[1]!=mfOriginal[1])u.health=std::clamp(std::atoi(mf[1].c_str()),0,100);
                if(mf[2]!=mfOriginal[2])u.armor=std::clamp(std::atoi(mf[2].c_str()),0,1000);
                if(mf[3]!=mfOriginal[3])u.weapon=std::clamp(std::atoi(mf[3].c_str()),0,1000);
                if(mf[4]!=mfOriginal[4])u.veteran=std::clamp(std::atoi(mf[4].c_str()),0,9);
                if(mf[5]!=mfOriginal[5]) {int angle=std::atoi(mf[5].c_str())%360;u.angle=float(angle<0?angle+360:angle);}
                if(targets.size()==1)u.name=mf[6];
                u.x+=dx;u.z+=dz;
            }
            unitsEdited=true;dirty=true;historyPending=true;
        } else if (modal == M_RULE && editRule) {
            for(int i=0;i<mN;++i)if(mf[i].size()>63) {openMessage("RULE OPERAND TOO LONG","Each CRT operand must fit in 63 bytes. The rule was not changed.");return;}
            for (int i = 0; i < mN; ++i) editRule->slot[i] = mf[i];
            for (int i = mN; i < 5; ++i) editRule->slot[i].clear();
            editRule = nullptr; dirty = true; historyPending=true;
        } else if (modal == M_CONFIRM) {
            if (confirmAction) confirmAction();
            confirmAction = nullptr;
        }
        if(modal!=applying)return;
        modal = M_NONE; SDL_StopTextInput();
    };
    // Open the param editor for a condition/action rule (fields = its opcode's
    // parameters, in slot order).
    auto openRuleEditor = [&](tak::crt::Rule* r, bool isAction) {
        const auto& defs = isAction ? cart::actionDefs() : cart::conditionDefs();
        int op = std::clamp(r->opcode, 0, int(defs.size()) - 1);
        const auto& params = defs[size_t(op)].params;
        editRule = r; mfocus = 0; mDropOpen = -1;
        mN = std::min(int(params.size()), kMaxFields);
        mTitle = (isAction ? "ACTION: " : "CONDITION: ") + cart::formatRule(isAction, *r);
        for (int i = 0; i < mN; ++i) {
            mLabel[i] = cart::paramLabel(params[size_t(i)]);
            mf[i] = r->slot[size_t(i)];
            const auto kind=params[size_t(i)];
            mfNumeric[i] = kind==cart::PKind::Value;
            mfChoices[i] = nullptr;
            auto& choices=ruleChoices[i];choices.clear();
            if(kind==cart::PKind::Location) {choices.push_back("Anywhere");for(const auto& region:scen.regions)choices.push_back(region.name);}
            if(kind==cart::PKind::UnitType) {choices.push_back("Any Unit");choices.insert(choices.end(),unitTypes.begin(),unitTypes.end());}
            if(kind==cart::PKind::Player) {choices.push_back("All Players");for(int p=1;p<=8;++p)choices.push_back("Player "+std::to_string(p));}
            if(!choices.empty()) {
                if(std::find(choices.begin(),choices.end(),mf[i])==choices.end())choices.insert(choices.begin(),mf[i]);
                mfChoices[i]=&choices;
            }
        }
        modal = M_RULE;fieldEditor.focus(mf[0]);
        if (mN > 0) SDL_StartTextInput();
    };

    // --- Use Only restriction list + Check Map (phase 4) ----------------------
    bool useOnlyOpen = false;
    int useOnlyScroll = 0;
    SDL_Rect uoList{}, uoDone{}, uoClear{};   // render-computed hit rects
    std::future<cart::TerrainOverlay> overlayJob;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> overlayTexture(nullptr,SDL_DestroyTexture);
    std::string overlayLegend;
    int overlayWidth=0,overlayHeight=0;
    bool discardOverlay=false,quitAfterOverlay=false;
    auto buildOverlay=[&](cart::OverlayKind kind) {
        const auto type=selectedType>=0 && selectedType<int(unitTypes.size())?lowerText(unitTypes[selectedType]):std::string{};
        auto snapshot=mapView.map();
        discardOverlay=quitAfterOverlay=false;
        overlayJob=std::async(std::launch::async,[&,snapshot=std::move(snapshot),type,kind] {
            return cart::terrainOverlay(snapshot,unitRegistry,vfs,kind,type);
        });
        modal=M_ANALYZING;SDL_StopTextInput();
    };
    std::vector<cart::MapIssue> mapIssues;
    size_t issueIndex=0;
    auto showMapIssue=[&]() {
        if(mapIssues.empty()) {openMessage("CHECK MAP","No issues found by terrain, start, unit and scenario checks. Reachability and naval production checks are not yet included.");return;}
        const auto& issue=mapIssues[issueIndex];
        if(issue.x>=0 && issue.z>=0) {
            int w,h;SDL_GetRendererOutputSize(ren,&w,&h);
            mapView.setOffset(issue.x-(w/kUIScale-kPaletteW)/(2*mapView.zoom()),issue.z-(h/kUIScale-kMenuH-kStatusH)/(2*mapView.zoom()));
        }
        openMessage("MAP ISSUE "+std::to_string(issueIndex+1)+" / "+std::to_string(mapIssues.size()),
            std::string(issue.severity==cart::MapIssue::Severity::Error?"ERROR: ":"WARNING: ")+issue.message+". Scenario > Next issue moves to the next result. Recheck after edits.");
    };
    auto checkMap = [&]() {
        try {mapIssues=cart::validateMap(mapView.map(),scenario,scen,units,useOnly,unitRegistry,vfs);issueIndex=0;showMapIssue();}
        catch(const std::exception& error) {openMessage("CHECK FAILED",error.what());}
    };

    // --- Scenario Scripting (per-player trigger rules) overlay (phase 5) ------
    bool scriptOpen = false;
    int scrPlayer = 0;                     // 0..8
    int scrGroup = -1;                     // selected rule-group in this player
    int scrCondSel = -1, scrActSel = -1;   // selected condition / action row
    int scrRuleScroll = 0, scrCondScroll = 0, scrActScroll = 0;
    bool pickOpen = false, pickAction = false;   // opcode picker (add cond/act)
    int pickScroll = 0;
    SDL_Rect rRuleList{}, rCondList{}, rActList{}, rPickList{};   // render-computed
    SDL_Rect rPrevP{}, rNextP{}, rAddRule{}, rDelRule{}, rAddCond{}, rDelCond{},
             rAddAct{}, rDelAct{}, rScrDone{}, rPickCancel{};
    cart::RuleColumn ruleColumn=cart::RuleColumn::Group;
    cart::RuleClipboard ruleClipboard;
    SDL_Rect ruleCopy{},rulePaste{},ruleDuplicate{},ruleUp{},ruleDown{},playerCopy{},playerPaste{};
    auto scrGroups = [&]() -> std::vector<tak::crt::RuleGroup>& {
        return scen.players[size_t(scrPlayer)];
    };
    auto curGroup = [&]() -> tak::crt::RuleGroup* {
        auto& gs = scrGroups();
        return (scrGroup >= 0 && scrGroup < int(gs.size())) ? &gs[size_t(scrGroup)] : nullptr;
    };
    auto ruleOperation=[&](int operation) {
        auto& groups=scrGroups();
        int row=ruleColumn==cart::RuleColumn::Action?scrActSel:scrCondSel;
        bool changed=false;
        if(operation==0)ruleClipboard.copy(groups,scrGroup,row,ruleColumn);
        if(operation==1) {changed=ruleClipboard.paste(groups,scrGroup,row,ruleColumn);if(changed && ruleClipboard.column==cart::RuleColumn::Group)ruleColumn=cart::RuleColumn::Group;}
        if(operation==2) {cart::RuleClipboard duplicate;if(duplicate.copy(groups,scrGroup,row,ruleColumn))changed=duplicate.paste(groups,scrGroup,row,ruleColumn);}
        if(operation==3 || operation==4)changed=cart::moveRule(groups,scrGroup,row,ruleColumn,operation==3?-1:1);
        if(operation==5)ruleClipboard.copy(groups,scrGroup,row,ruleColumn,true);
        if(operation==6 && ruleClipboard.column==cart::RuleColumn::Group) {scrGroup=int(groups.size())-1;changed=ruleClipboard.paste(groups,scrGroup,row,cart::RuleColumn::Group);ruleColumn=cart::RuleColumn::Group;}
        if(changed) {
            if(ruleColumn==cart::RuleColumn::Action)scrActSel=row;
            else if(ruleColumn==cart::RuleColumn::Condition)scrCondSel=row;
            else scrCondSel=scrActSel=-1;
            auto reveal=[](int selected,int height,int& scroll) {
                if(selected<0)return;
                if(selected*12<scroll)scroll=selected*12;
                else if((selected+1)*12>scroll+height)scroll=std::max(0,(selected+1)*12-height);
            };
            reveal(scrGroup,rRuleList.h,scrRuleScroll);
            if(ruleColumn==cart::RuleColumn::Action)reveal(scrActSel,rActList.h,scrActScroll);
            if(ruleColumn==cart::RuleColumn::Condition)reveal(scrCondSel,rCondList.h,scrCondScroll);
            dirty=true;historyPending=true;
        }
    };
    auto openScripting = [&]() {
        if (int(scen.players.size()) < 9) scen.players.resize(9);   // retail writes 9
        scriptOpen = true; scrPlayer = 0;ruleColumn=cart::RuleColumn::Group;
        scrGroup = scen.players[0].empty() ? -1 : 0;
        scrCondSel = scrActSel = -1;
        scrRuleScroll = scrCondScroll = scrActScroll = 0;
    };

    openDocument = [&](const std::string& request) {
        try {
            auto nextVfs=tak::hpi::mountRetailRoot(dataRoot,tak::hpi::OverridePolicy::Full);
            std::string path,chosen=request;
            const auto disk=std::filesystem::u8path(request);
            if(std::filesystem::is_regular_file(disk)) {
                auto files=std::make_shared<tak::hpi::Vfs::Files>();
                if(lowerText(pathText(disk.extension()))==".kmp") {
                    tak::hpi::Archive archive(disk);
                    for(const auto& entry:archive.entries()) if(!entry.isDirectory && entry.path.starts_with("kmap/")) {
                        (*files)[tak::hpi::MountSet::key(entry.path)]=archive.read(entry);
                        if(tak::vpath::extension(entry.path)==".tnt")path=tak::hpi::MountSet::key(entry.path);
                    }
                } else if(lowerText(pathText(disk.extension()))==".tnt") {
                    const auto base=pathText(disk.stem());path="kmap/"+base+".tnt";
                    for(const char* ext:{".tnt",".ota",".crt",".tdf"}) {
                        const auto input=disk.parent_path()/std::filesystem::u8path(base+ext);std::ifstream stream(input,std::ios::binary);
                        if(stream)(*files)[tak::hpi::MountSet::key("kmap/"+base+ext)]={std::istreambuf_iterator<char>(stream),{}};
                    }
                }
                if(path.empty())throw std::runtime_error("Choose a KMP bundle or TNT map");
                nextVfs.setMapFiles(files);chosen=tak::vpath::stem(path);
            } else if(tak::vpath::extension(request)==".tnt" && nextVfs.has(request)) {path=request;chosen=tak::vpath::stem(request);}
            else path=tak::hpi::findMap(nextVfs,request);
            if(path.empty())throw std::runtime_error("Map not found. Enter an installed map name or a full KMP / TNT path.");
            (void)tak::tnt::Map::load(nextVfs.read(path),path);
            const auto stem=path.substr(0,path.rfind('.'));
            tak::tnt::Scenario nextMetadata;
            if(nextVfs.has(stem+".ota")) {const auto data=nextVfs.read(stem+".ota");nextMetadata=tak::tnt::Scenario::parse(std::string(data.begin(),data.end()));}
            auto nextScenario=cart::loadScenario(nextVfs,stem+".crt");
            auto nextUnits=cart::toPlaced(nextScenario);
            std::set<std::string> nextUseOnly;
            if(!nextMetadata.useOnlyUnits.empty())for(const auto& type:cart::loadUseOnly(nextVfs,stem+".tdf"))nextUseOnly.insert(type);
            mapView.quiesce();vfs=std::move(nextVfs);mapView.reload(vfs,path);mapView.setOffset(0,0);minimapSource=nullptr;
            scenario=std::move(nextMetadata);scen=std::move(nextScenario);units=std::move(nextUnits);useOnly=std::move(nextUseOnly);
            mapName=chosen;mapPath=path;world=scenario.kingdom.empty()?"aramon":scenario.kingdom;
            sections.scan(vfs,world);features.scan(vfs,world);
            for(auto& [key,t]:thumbs)if(t)SDL_DestroyTexture(t);
            thumbs.clear();
            for(auto& [key,t]:featTex)if(t)SDL_DestroyTexture(t);
            featTex.clear();
            selectedUnits.indices.clear();selectedUnits.dragOrigins.clear();
            selected=sections.list().empty()?-1:0;selectedFeat=features.list().empty()?-1:0;paletteScroll=0;
            edited=false;overlayInvalidated=true;historyPending=resetHistory=false;history.reset(historySnapshot(),true);dirty=false;
            editRule=nullptr;editUnit=draggingUnit=draggingStart=-1;scrGroup=-1;
            if(std::filesystem::is_regular_file(disk)) {outDir=pathText(std::filesystem::absolute(disk).parent_path());mapName=pathText(disk.stem());}
            preferences.remember(request);persistPreferences();
            modal=M_NONE;SDL_StopTextInput();return true;
        } catch(const std::exception& error) {openMessage("OPEN FAILED",error.what());return false;}
    };
    int menuOpen=-1;
    struct ViewBookmark {float x,z,zoom;};
    std::optional<ViewBookmark> viewBookmark;
    const std::vector<std::string> menuNames={"FILE","EDIT","VIEW","SCENARIO","HELP"};
    const std::vector<std::vector<std::string>> menuRows={
        {"New map (Ctrl+N)","Open map (Ctrl+O)","Save (Ctrl+S)","Save As (Ctrl+Shift+S)","Export loose files","Exit"},
        {"Undo (Ctrl+Z)","Redo (Ctrl+Y)","Clear area (K)","Terrain brush: protect objects"},
        {"Fit map","100% terrain zoom","Toggle grid (G)","Toggle regions","Frame selected units","Store view bookmark","Restore view bookmark","Overlay: movement","Overlay: buildability","Overlay: water depth","Overlay: slopes","Hide terrain overlay","Smaller UI","Larger UI"},
        {"Properties (P)","Resize (R)","Use Only units (U)","Check map (C)","Scripting (T)","Next issue","Regions"},
        {"Editor controls","About"}};
    auto menuAction = [&](int menu,int row) {
        if(menu==0) {
            if(row==0) {if(dirty)openConfirm("NEW MAP","Discard unsaved changes?",[&]{wantNew=true;});else openModal(M_NEW);}
            if(row==1)openModal(M_OPENPATH);
            if(row==2) {if(saveBundle(outDir+"/"+mapName+".kmp"))openMessage("SAVED",outDir+"/"+mapName+".kmp");else openMessage("SAVE FAILED",saveError);}
            if(row==3)openModal(M_SAVEAS);
            if(row==4) {if(saveMap(outDir+"/"+mapName+".tnt"))openMessage("EXPORTED",outDir+"/"+mapName+".tnt");else openMessage("EXPORT FAILED",saveError);}
            if(row==5) {SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);}
        } else if(menu==1) {
            if(row<2) {SDL_Event key{};key.type=SDL_KEYDOWN;key.key.keysym.sym=row?SDLK_y:SDLK_z;key.key.keysym.mod=KMOD_CTRL;SDL_PushEvent(&key);}
            else if(row==2) {clearArm=true;editMode=MODE_PLACE;}
            else {stampLayers.objects=!stampLayers.objects;lastStampX=lastStampY=-999999;}
        } else if(menu==2) {
            int w,h;SDL_GetRendererOutputSize(ren,&w,&h);w=int(w/kUIScale)-kPaletteW;h=int(h/kUIScale)-kMenuH-kStatusH;
            if(row==0) {mapView.setZoom(std::min(float(w)/(mapView.map().width*16),float(h)/(mapView.map().height*16)));mapView.setOffset(0,0);}
            if(row==1)mapView.setZoom(1);
            if(row==2)showGrid=!showGrid;
            if(row==3)showRegions=!showRegions;
            if(row==12 || row==13) {preferences.scalePercent=std::clamp(preferences.scalePercent+(row==12?-25:25),50,200);persistPreferences();}
            if(row>=7 && row<=10)buildOverlay(static_cast<cart::OverlayKind>(row-7));
            if(row==11) {overlayTexture.reset();overlayLegend.clear();}
            if(row==4) {
                bool any=false;float x0=0,z0=0,x1=0,z1=0;
                for(int i:selectedUnits.indices)if(i>=0 && i<int(units.size())) {
                    const auto& u=units[i];
                    if(!any) {x0=x1=u.x;z0=z1=u.z;any=true;}
                    else {x0=std::min(x0,u.x);x1=std::max(x1,u.x);z0=std::min(z0,u.z);z1=std::max(z1,u.z);}
                }
                if(any) {
                    const float zoom=std::min({1.f,float(w)/std::max(128.f,x1-x0+128),float(h)/std::max(128.f,z1-z0+128)});
                    mapView.setZoom(zoom);mapView.setOffset((x0+x1)/2-w/(2*zoom),(z0+z1)/2-h/(2*zoom));
                } else openMessage("FRAME SELECTION","Select one or more units first.");
            }
            if(row==5)viewBookmark=ViewBookmark{mapView.offX(),mapView.offY(),mapView.zoom()};
            if(row==6) {
                if(viewBookmark) {mapView.setZoom(viewBookmark->zoom);mapView.setOffset(viewBookmark->x,viewBookmark->z);}
                else openMessage("VIEW BOOKMARK","Store a view bookmark first.");
            }
        } else if(menu==3) {
            if(row==0)openModal(M_SCENARIO);
            if(row==1)openModal(M_RESIZE);
            if(row==2)useOnlyOpen=true;
            if(row==3)checkMap();
            if(row==4)openScripting();
            if(row==6) {regionsOpen=true;regionSelected=scen.regions.empty()?-1:0;}
            if(row==5) {if(!mapIssues.empty())issueIndex=(issueIndex+1)%mapIssues.size();showMapIssue();}
        } else openMessage(row?"ABOUT CARTOGRAPHER":"EDITOR CONTROLS",row?"TAK Engine map and scenario editor. Uses your original game assets.":
            "Choose terrain or objects in the left browser. Left-click uses the chosen Place, Select, Erase or Pan mode. Right-drag always pans. In Select mode, drag a box or move the selected units. Ctrl+C/X/V copies/cuts/pastes, Ctrl+D duplicates, Delete removes, and Enter opens properties. Tab changes tools. Ctrl+Z undoes; Ctrl+Y redoes. Ctrl+S saves a playable KMP. File offers Open, Save As and loose export. Double-click a unit or rule to edit it. Unsaved maps get recovery copies every minute.");
    };

    bool recoveryFilesPresent=!recoveredFrom.empty();
    std::future<std::string> recoveryJob;
    Uint64 nextRecovery=recoveryClock()+60000;
    std::string recoveryStatus;
    auto collectRecovery = [&]() {
        if(recoveryJob.valid()) {
            const auto error=recoveryJob.get();
            recoveryStatus=error.empty()?"Recovery saved":"Recovery failed: "+error;
        }
    };
    auto clearRecovery = [&]() {
        if(!recoveryFilesPresent && !recoveryJob.valid())return;
        collectRecovery();std::error_code ec;
        for(const auto& path:{recoveryFile,recoveredFrom}) if(!path.empty()) {
            std::filesystem::remove(path,ec);auto backup=path;backup+=".bak";std::filesystem::remove(backup,ec);
        }
        recoveredFrom.clear();recoveryFilesPresent=false;
    };

    auto restoreHistory = [&](const cart::HistoryState* state) {
        if(!state)return;
        overlayInvalidated=true;
        mapView.quiesce();
        auto map=tak::tnt::Map::load(state->terrain,"undo history");
        map.seaLevel=state->seaLevel;map.stockTerrain=state->stockTerrain;
        mapView.editMap()=std::move(map);mapView.tilesEdited();minimapSource=nullptr;
        scenario=tak::tnt::Scenario::parse(state->metadata);
        scen=tak::crt::parse(state->scenario);units=cart::toPlaced(scen);
        useOnly=state->useOnly;mapName=state->name;dirty=history.dirty();edited=true;
        selectedUnits.indices.clear();selectedUnits.dragOrigins.clear();
        editRule=nullptr;draggingUnit=draggingStart=-1;scrGroup=-1;
        const auto nextWorld=scenario.kingdom.empty()?"aramon":scenario.kingdom;
        if(nextWorld!=world) {
            world=nextWorld;sections.scan(vfs,world);features.scan(vfs,world);
            for(auto& [key,texture]:featTex)if(texture)SDL_DestroyTexture(texture);
            featTex.clear();selected=sections.list().empty()?-1:0;selectedFeat=features.list().empty()?-1:0;paletteScroll=0;
        }
    };

    if (!shotPath.empty()) {
        mapView.setZoom(0.3f);          // fit-ish view for the shot
        mapView.finishChunks();         // wait for the terrain to decode+upload
    }
    // A bare launch (no map named, no --new size) opens on the blank map with
    // the New Map dialog already up, so the first thing is "what shall we make?".
    if (mapPath.empty() && newW == 0 && shotPath.empty()) openModal(M_NEW);
    int frameNumber=0;
    bool running = true;
    while (running) {
        int w, h;
        SDL_GetRendererOutputSize(ren, &w, &h);
        kUIScale=std::max(.5f,std::min({preferences.scalePercent/100.f,float(w)/900.0f,float(h)/600.0f}));
        SDL_RenderSetScale(ren,kUIScale,kUIScale);
        w /= kUIScale; h /= kUIScale;   // physical -> logical (SDL_RenderSetScale)
        int canvasH = h - kMenuH - kStatusH;
        int canvasW = w - kPaletteW;
        const float miniScale=140.0f/std::max(mapView.map().width,mapView.map().height);
        const SDL_Rect miniRect{w-148,kMenuH+8,std::max(1,int(mapView.map().width*miniScale)),std::max(1,int(mapView.map().height*miniScale))};

        refreshPalette();
        if(generatorJob.valid() && generatorJob.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try {
                auto result=generatorJob.get();
                if(cancelGeneration)modal=M_NONE;
                else if(!result.map.width)openMessage("GENERATION FAILED","The generator returned no terrain.");
                else {
                    const auto palette=cart::loadWorldPalette(vfs,generatingWorld);
                    std::vector<uint8_t> rgba(result.map.minimap.size()*4);
                    for(size_t i=0;i<result.map.minimap.size();++i)std::copy(palette.rgba[result.map.minimap[i]],palette.rgba[result.map.minimap[i]]+4,rgba.begin()+i*4);
                    generatedTexture.reset(SDL_CreateTexture(ren,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,result.map.minimapW,result.map.minimapH));
                    if(!generatedTexture)throw std::runtime_error(SDL_GetError());
                    SDL_UpdateTexture(generatedTexture.get(),nullptr,rgba.data(),result.map.minimapW*4);
                    SDL_SetTextureScaleMode(generatedTexture.get(),SDL_ScaleModeLinear);
                    generatedPreview=std::move(result);modal=M_GENERATED;
                }
            } catch(const std::exception& error) {openMessage("GENERATION FAILED",error.what());}
            if(quitAfterGeneration) {SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);}
        }
        if(overlayJob.valid() && overlayJob.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try {
                auto result=overlayJob.get();modal=M_NONE;
                if(!discardOverlay) {
                    overlayTexture.reset(SDL_CreateTexture(ren,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,result.width,result.height));
                    if(!overlayTexture)throw std::runtime_error(SDL_GetError());
                    SDL_UpdateTexture(overlayTexture.get(),nullptr,result.rgba.data(),result.width*4);
                    SDL_SetTextureBlendMode(overlayTexture.get(),SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(overlayTexture.get(),SDL_ScaleModeNearest);
                    overlayWidth=result.width;overlayHeight=result.height;overlayLegend=std::move(result.legend);
                }
            } catch(const std::exception& error) {openMessage("OVERLAY FAILED",error.what());}
            if(quitAfterOverlay) {SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);}
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if(overlayJob.valid()) {
                if(e.type==SDL_QUIT) {discardOverlay=true;quitAfterOverlay=true;}
                if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE)discardOverlay=true;
                continue;
            }
            if(generatorJob.valid()) {
                if(e.type==SDL_QUIT) {cancelGeneration=true;quitAfterGeneration=true;}
                if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE)cancelGeneration=true;
                continue;
            }
            const float pointerDX=e.type==SDL_MOUSEMOTION?float(e.motion.xrel)/kUIScale:0;
            const float pointerDZ=e.type==SDL_MOUSEMOTION?float(e.motion.yrel)/kUIScale:0;
            if(e.type==SDL_RENDER_TARGETS_RESET || e.type==SDL_RENDER_DEVICE_RESET)
                mapView.invalidateRenderTargets();
            // Map input from physical window pixels into the logical draw space.
            if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                e.button.x /= kUIScale; e.button.y /= kUIScale;
            } else if (e.type == SDL_MOUSEMOTION) {
                e.motion.x /= kUIScale; e.motion.y /= kUIScale;
                e.motion.xrel /= kUIScale; e.motion.yrel /= kUIScale;
            }
            if(modal==M_GENERATED && generatedPreview) {
                const bool accept=(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_RETURN) ||
                    (e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT && cart::pointIn(e.button.x,e.button.y,mOK));
                const bool cancel=e.type==SDL_QUIT || (e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE) ||
                    (e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT && cart::pointIn(e.button.x,e.button.y,mCancel));
                if(accept) {adoptFreshMap(std::move(*generatedPreview),generatingName,generatingWorld);generatedPreview.reset();generatedTexture.reset();}
                if(cancel) {generatedPreview.reset();generatedTexture.reset();modal=M_NONE;}
                if(e.type!=SDL_QUIT)continue;
            }
            if (e.type == SDL_QUIT) {
                if (dirty) {
                    // Close any transient overlay so the quit prompt gets input.
                    useOnlyOpen = false; scriptOpen = false; pickOpen = false;regionsOpen=false;
                    modal = M_QUITSAVE; SDL_StopTextInput();
                } else running = false;
                continue;
            }
            if(modal==M_NONE && !scriptOpen && !useOnlyOpen && !regionsOpen) {
                if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE && menuOpen>=0) {menuOpen=-1;continue;}
                if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT) {
                    const int mx=e.button.x,my=e.button.y;
                    if(my<22 && mx>=96 && mx<96+int(menuNames.size())*72) {const int item=(mx-96)/72;menuOpen=menuOpen==item?-1:item;continue;}
                    if(menuOpen>=0) {
                        const int column=menuOpen;menuOpen=-1;
                        const int x=96+column*72,row=(my-22)/20;
                        if(mx>=x && mx<x+240 && my>=22 && row<int(menuRows[size_t(column)].size()))menuAction(column,row);
                        continue;
                    }
                }
                if(menuOpen>=0)continue;
            }
            if(modal==M_NONE && !scriptOpen && !useOnlyOpen && !regionsOpen) {
                auto navigateMinimap=[&](int x,int y) {
                    const float wx=std::clamp(float(x-miniRect.x)/miniRect.w,0.0f,1.0f)*mapView.map().width*16;
                    const float wz=std::clamp(float(y-miniRect.y)/miniRect.h,0.0f,1.0f)*mapView.map().height*16;
                    mapView.setOffset(wx-canvasW/(2*mapView.zoom()),wz-canvasH/(2*mapView.zoom()));
                };
                if(e.type==SDL_MOUSEBUTTONDOWN && cart::pointIn(e.button.x,e.button.y,miniRect)) {minimapDrag=true;navigateMinimap(e.button.x,e.button.y);continue;}
                if(e.type==SDL_MOUSEMOTION && minimapDrag) {navigateMinimap(e.motion.x,e.motion.y);continue;}
                if(e.type==SDL_MOUSEBUTTONUP && minimapDrag) {minimapDrag=false;continue;}
                if(e.type==SDL_KEYDOWN && (e.key.keysym.mod&(KMOD_CTRL|KMOD_GUI)) && e.key.keysym.sym==SDLK_f) {
                    paletteFocus=true;paletteEditor.focus(paletteSearch);SDL_StartTextInput();continue;
                }
                if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT) {
                    if(e.button.x<kPaletteW && e.button.y>=kMenuH && e.button.y<kPaletteTop) {
                        if(e.button.y<kMenuH+30) {paletteFocus=true;paletteEditor.focus(paletteSearch);SDL_StartTextInput();}
                        else if(e.button.y<kMenuH+53) {categoryIndex=(categoryIndex+1)%int(paletteCategories.size());refreshPalette();}
                        else if(tool==UNITS) {factionIndex=(factionIndex+1)%6;refreshPalette();}
                        continue;
                    }
                    if(paletteFocus) {paletteFocus=false;SDL_StopTextInput();}
                }
                if(paletteFocus && (e.type==SDL_TEXTINPUT || e.type==SDL_KEYDOWN)) {
                    if(e.type==SDL_KEYDOWN && (e.key.keysym.sym==SDLK_ESCAPE || e.key.keysym.sym==SDLK_RETURN)) {paletteFocus=false;SDL_StopTextInput();}
                    else {paletteEditor.input(e,paletteSearch);refreshPalette();}
                    continue;
                }
            }
            if(regionsOpen && modal==M_NONE) {
                auto focusRegion=[&]() {
                    if(regionSelected<0 || regionSelected>=int(scen.regions.size()))return;
                    const auto& r=scen.regions[regionSelected];
                    mapView.setOffset((r.x1+r.x2+1)*8.f-canvasW/(2*mapView.zoom()),(r.z1+r.z2+1)*8.f-canvasH/(2*mapView.zoom()));
                };
                if(e.type==SDL_KEYDOWN) {
                    if(e.key.keysym.sym==SDLK_ESCAPE)regionsOpen=false;
                    else if(e.key.keysym.sym==SDLK_n && (e.key.keysym.mod&(KMOD_CTRL|KMOD_GUI)))openModal(M_REGION);
                    else if(e.key.keysym.sym==SDLK_RETURN && regionSelected>=0)openModal(M_REGION,regionSelected);
                } else if(e.type==SDL_MOUSEWHEEL)regionScroll=std::clamp(regionScroll-e.wheel.y*36,0,std::max(0,int(scen.regions.size())*20-regionList.h));
                else if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT) {
                    int x=e.button.x,y=e.button.y;
                    if(cart::pointIn(x,y,regionDone))regionsOpen=false;
                    else if(cart::pointIn(x,y,regionNew))openModal(M_REGION);
                    else if(cart::pointIn(x,y,regionEdit) && regionSelected>=0)openModal(M_REGION,regionSelected);
                    else if(cart::pointIn(x,y,regionDelete)) {
                        std::string error;
                        if(cart::removeRegion(scen,regionSelected,error)) {regionSelected=std::min(regionSelected,int(scen.regions.size())-1);dirty=true;historyPending=true;}
                        else openMessage("REGION NOT DELETED",error);
                    } else if(cart::pointIn(x,y,regionList)) {
                        int index=(y-regionList.y+regionScroll)/20;
                        if(index<int(scen.regions.size())) {regionSelected=index;focusRegion();if(e.button.clicks>=2)openModal(M_REGION,index);}
                    }
                }
                continue;
            }
            // The Use Only checklist overlay swallows input while up.
            if (useOnlyOpen) {
                if (e.type == SDL_MOUSEWHEEL) {
                    int maxS = std::max(0, int(unitTypes.size()) * 14 - uoList.h);
                    useOnlyScroll = std::clamp(useOnlyScroll - e.wheel.y * 40, 0, maxS);
                } else if (e.type == SDL_KEYDOWN &&
                           (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN)) {
                    useOnlyOpen = false;
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int mx = e.button.x, my = e.button.y;
                    if (cart::pointIn(mx, my, uoDone)) useOnlyOpen = false;
                    else if (cart::pointIn(mx, my, uoClear)) { useOnly.clear(); dirty = true; historyPending=true; }
                    else if (cart::pointIn(mx, my, uoList)) {
                        int row = (my - uoList.y + useOnlyScroll) / 14;
                        if (row >= 0 && row < int(unitTypes.size())) {
                            const std::string& t = unitTypes[size_t(row)];
                            if (useOnly.count(t)) useOnly.erase(t); else useOnly.insert(t); dirty = true; historyPending=true;
                        }
                    }
                }
                continue;
            }
            // A modal dialog swallows all input while up.
            if (modal != M_NONE) {
                // Save-before-exit prompt: SAVE / DON'T SAVE / CANCEL.
                if (modal == M_QUITSAVE) {
                    auto saveThenQuit = [&]() {
                        // Only exit if the save actually succeeded -- otherwise
                        // keep the editor alive and say so, so a failed write
                        // (read-only dir, disk full) can't silently lose work.
                        if (saveBundle(outDir + "/" + mapName + ".kmp")) {
                            dirty = false; running = false; modal = M_NONE;
                        } else {
                            openMessage("SAVE FAILED",
                                        saveError);
                        }
                    };
                    if (e.type == SDL_KEYDOWN) {
                        SDL_Keycode k = e.key.keysym.sym;
                        if (k == SDLK_ESCAPE) modal = M_NONE;                    // cancel
                        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) saveThenQuit();
                    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                        int mx = e.button.x, my = e.button.y;
                        if (cart::pointIn(mx, my, mOK)) saveThenQuit();
                        else if (cart::pointIn(mx, my, mQuit)) { running = false; modal = M_NONE; }
                        else if (cart::pointIn(mx, my, mCancel)) modal = M_NONE;
                    }
                    continue;
                }
                if(modal==M_SCENARIO && e.type==SDL_MOUSEWHEEL) {descriptionScroll=std::max(0,descriptionScroll-e.wheel.y*3);descriptionFollowCaret=false;continue;}
                if(e.type==SDL_TEXTINPUT || e.type==SDL_KEYDOWN)descriptionFollowCaret=true;
                if(modal==M_OPENPATH && e.type==SDL_MOUSEWHEEL) {openScroll=std::clamp(openScroll-e.wheel.y*40,0,std::max(0,int(openMatches.size())*20-openList.h));continue;}
                if(modal==M_OPENPATH && e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT && cart::pointIn(e.button.x,e.button.y,openList)) {
                    const int row=(e.button.y-openList.y+openScroll)/20;
                    if(row>=0 && row<int(openMatches.size())) {mf[0]=openMatches[row].second;applyModal();}
                    continue;
                }
                if(e.type==SDL_MOUSEWHEEL && mDropOpen>=0 && mfChoices[mDropOpen]) {
                    mDropScroll=std::clamp(mDropScroll-e.wheel.y*3,0,std::max(0,int(mfChoices[mDropOpen]->size())-8));continue;
                }
                if (e.type == SDL_TEXTINPUT && mN > 0 && !mfChoices[mfocus]) {
                    fieldEditor.input(e,mf[mfocus],mfNumeric[mfocus],modal==M_SCENARIO && mfocus==1,std::max(1,(mBox[mfocus].w-8)/6));
                } else if (e.type == SDL_KEYDOWN) {
                    SDL_Keycode k = e.key.keysym.sym;
                    if (k == SDLK_TAB) {
                        mfocus = (mfocus + ((e.key.keysym.mod&KMOD_SHIFT)?std::max(1,mN)-1:1)) % std::max(1, mN);
                        mDropOpen = -1;fieldEditor.focus(mf[mfocus]);
                    }
                    else if ((k == SDLK_LEFT || k == SDLK_RIGHT) && mN > 0 && mfChoices[mfocus]) {
                        // Cycle a focused dropdown field with the arrow keys.
                        const auto& opts = *mfChoices[mfocus];
                        int cur = 0;
                        for (int j = 0; j < int(opts.size()); ++j)
                            if (opts[size_t(j)] == mf[mfocus]) { cur = j; break; }
                        cur = (cur + (k == SDLK_RIGHT ? 1 : int(opts.size()) - 1)) % int(opts.size());
                        mf[mfocus] = opts[size_t(cur)];
                    }
                    else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && !(modal==M_SCENARIO && mfocus==1 && (e.key.keysym.mod&KMOD_SHIFT))) applyModal();
                    else if (k == SDLK_ESCAPE) {
                        if (mDropOpen >= 0) mDropOpen = -1;   // first Esc closes an open list
                        else { modal = M_NONE; SDL_StopTextInput(); }
                    } else if(mN>0 && !mfChoices[mfocus])fieldEditor.input(e,mf[mfocus],mfNumeric[mfocus],modal==M_SCENARIO && mfocus==1,std::max(1,(mBox[mfocus].w-8)/6));
                } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                           e.button.button == SDL_BUTTON_LEFT) {
                    int mx = e.button.x, my = e.button.y;
                    // An open dropdown list eats the next click: a row selects, any
                    // click closes it (so a stray click just dismisses the list).
                    if (mDropOpen >= 0) {
                        for (size_t i = 0; i < mDropRects.size(); ++i)
                            if (cart::pointIn(mx, my, mDropRects[i])) {
                                mf[mDropOpen] = (*mfChoices[mDropOpen])[i+mDropScroll]; break;
                            }
                        mDropOpen = -1;
                        continue;
                    }
                    if (cart::pointIn(mx, my, mOK)) { applyModal(); continue; }
                    if (modal == M_NEW && cart::pointIn(mx, my, mRandom)) { openModal(M_GENERATOR); continue; }
                    if (cart::pointIn(mx, my, mCancel)) { modal = M_NONE; SDL_StopTextInput(); continue; }
                    // A choice field opens its dropdown; a text field takes focus.
                    for (int i = 0; i < mN; ++i)
                        if (cart::pointIn(mx, my, mBox[i])) {
                            if (mfChoices[i]) { mDropOpen = i; mfocus = i;mDropScroll=0; }
                            else {mfocus=i;
                                if(modal==M_SCENARIO && i==1) {
                                    fieldEditor.click(mf[i],std::max(1,(mBox[i].w-8)/6),(my-mBox[i].y-4)/12+descriptionScroll,(mx-mBox[i].x-4)/6,SDL_GetModState()&KMOD_SHIFT);
                                    descriptionFollowCaret=true;
                                } else fieldEditor.focus(mf[i]);
                            }
                            break;
                        }
                }
                continue;
            }
            // The Scripting (trigger) overlay swallows input while up.
            if (scriptOpen) {
                constexpr int kRow = 12;
                if(!pickOpen && e.type==SDL_KEYDOWN) {
                    const auto key=e.key.keysym.sym;const auto mod=e.key.keysym.mod;
                    if(mod&(KMOD_CTRL|KMOD_GUI)) {
                        if(key==SDLK_c) {ruleOperation(0);continue;}
                        if(key==SDLK_v) {ruleOperation(1);continue;}
                        if(key==SDLK_d) {ruleOperation(2);continue;}
                    }
                    if((mod&KMOD_ALT) && (key==SDLK_UP || key==SDLK_DOWN)) {ruleOperation(key==SDLK_UP?3:4);continue;}
                }
                if(!pickOpen && e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT) {
                    const SDL_Rect buttons[]={ruleCopy,rulePaste,ruleDuplicate,ruleUp,ruleDown,playerCopy,playerPaste};
                    bool handled=false;
                    for(int i=0;i<7;++i)if(cart::pointIn(e.button.x,e.button.y,buttons[i])) {ruleOperation(i);handled=true;break;}
                    if(handled)continue;
                }
                // Nested opcode picker (choose a condition/action type to add).
                if (pickOpen) {
                    const auto& defs = pickAction ? cart::actionDefs() : cart::conditionDefs();
                    if (e.type == SDL_MOUSEWHEEL) {
                        int maxS = std::max(0, int(defs.size()) * kRow - rPickList.h);
                        pickScroll = std::clamp(pickScroll - e.wheel.y * 36, 0, maxS);
                    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                        pickOpen = false;
                    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                        int mx = e.button.x, my = e.button.y;
                        if (cart::pointIn(mx, my, rPickCancel)) pickOpen = false;
                        else if (cart::pointIn(mx, my, rPickList)) {
                            int row = (my - rPickList.y + pickScroll) / kRow;
                            tak::crt::RuleGroup* g = curGroup();
                            if (g && row >= 0 && row < int(defs.size())) {
                                tak::crt::Rule r; r.opcode = row;
                                for (size_t i = 0; i < defs[size_t(row)].params.size() && i < 5; ++i)
                                    r.slot[i] = cart::defaultParam(defs[size_t(row)].params[i]);
                                if (pickAction) { g->actions.push_back(r); scrActSel = int(g->actions.size()) - 1; dirty = true; historyPending=true; }
                                else { g->conditions.push_back(r); scrCondSel = int(g->conditions.size()) - 1; dirty = true; historyPending=true; }
                                pickOpen = false;
                            }
                        }
                    }
                    continue;
                }
                auto& gs = scrGroups();
                tak::crt::RuleGroup* g = curGroup();
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                    scriptOpen = false;
                } else if (e.type == SDL_MOUSEWHEEL) {
                    int mx, my; SDL_GetMouseState(&mx, &my); mx /= kUIScale; my /= kUIScale;
                    if (cart::pointIn(mx, my, rRuleList)) {
                        int maxS = std::max(0, int(gs.size()) * kRow - rRuleList.h);
                        scrRuleScroll = std::clamp(scrRuleScroll - e.wheel.y * 36, 0, maxS);
                    } else if (g && cart::pointIn(mx, my, rCondList)) {
                        int maxS = std::max(0, int(g->conditions.size()) * kRow - rCondList.h);
                        scrCondScroll = std::clamp(scrCondScroll - e.wheel.y * 36, 0, maxS);
                    } else if (g && cart::pointIn(mx, my, rActList)) {
                        int maxS = std::max(0, int(g->actions.size()) * kRow - rActList.h);
                        scrActScroll = std::clamp(scrActScroll - e.wheel.y * 36, 0, maxS);
                    }
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int mx = e.button.x, my = e.button.y;
                    bool dbl = e.button.clicks >= 2;
                    if (cart::pointIn(mx, my, rScrDone)) scriptOpen = false;
                    else if (cart::pointIn(mx, my, rPrevP)) {
                        ruleColumn=cart::RuleColumn::Group;scrPlayer = (scrPlayer + 8) % 9; scrGroup = scen.players[size_t(scrPlayer)].empty() ? -1 : 0;
                        scrCondSel = scrActSel = -1; scrRuleScroll = scrCondScroll = scrActScroll = 0;
                    } else if (cart::pointIn(mx, my, rNextP)) {
                        ruleColumn=cart::RuleColumn::Group;scrPlayer = (scrPlayer + 1) % 9; scrGroup = scen.players[size_t(scrPlayer)].empty() ? -1 : 0;
                        scrCondSel = scrActSel = -1; scrRuleScroll = scrCondScroll = scrActScroll = 0;
                    } else if (cart::pointIn(mx, my, rAddRule)) {
                        ruleColumn=cart::RuleColumn::Group;gs.push_back({}); scrGroup = int(gs.size()) - 1; scrCondSel = scrActSel = -1; dirty = true; historyPending=true;
                    } else if (cart::pointIn(mx, my, rDelRule) && g) {
                        gs.erase(gs.begin() + scrGroup); dirty = true; historyPending=true;
                        scrGroup = gs.empty() ? -1 : std::min(scrGroup, int(gs.size()) - 1);
                        scrCondSel = scrActSel = -1;
                    } else if (cart::pointIn(mx, my, rAddCond)) {
                        if (!g) { gs.push_back({}); scrGroup = int(gs.size()) - 1; dirty = true; historyPending=true; }
                        pickOpen = true; pickAction = false; pickScroll = 0;
                    } else if (cart::pointIn(mx, my, rAddAct)) {
                        if (!g) { gs.push_back({}); scrGroup = int(gs.size()) - 1; dirty = true; historyPending=true; }
                        pickOpen = true; pickAction = true; pickScroll = 0;
                    } else if (cart::pointIn(mx, my, rDelCond) && g && scrCondSel >= 0 &&
                               scrCondSel < int(g->conditions.size())) {
                        g->conditions.erase(g->conditions.begin() + scrCondSel); scrCondSel = -1; dirty = true; historyPending=true;
                    } else if (cart::pointIn(mx, my, rDelAct) && g && scrActSel >= 0 &&
                               scrActSel < int(g->actions.size())) {
                        g->actions.erase(g->actions.begin() + scrActSel); scrActSel = -1; dirty = true; historyPending=true;
                    } else if (cart::pointIn(mx, my, rRuleList)) {
                        int row = (my - rRuleList.y + scrRuleScroll) / kRow;
                        if (row >= 0 && row < int(gs.size())) {
                            scrGroup = row;ruleColumn=cart::RuleColumn::Group; scrCondSel = scrActSel = -1;
                            scrCondScroll = scrActScroll = 0;
                        }
                    } else if (g && cart::pointIn(mx, my, rCondList)) {
                        int row = (my - rCondList.y + scrCondScroll) / kRow;
                        if (row >= 0 && row < int(g->conditions.size())) {
                            scrCondSel = row;ruleColumn=cart::RuleColumn::Condition;
                            if (dbl) openRuleEditor(&g->conditions[size_t(row)], false);
                        }
                    } else if (g && cart::pointIn(mx, my, rActList)) {
                        int row = (my - rActList.y + scrActScroll) / kRow;
                        if (row >= 0 && row < int(g->actions.size())) {
                            scrActSel = row;ruleColumn=cart::RuleColumn::Action;
                            if (dbl) openRuleEditor(&g->actions[size_t(row)], true);
                        }
                    }
                }
                continue;
            }
            if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT && e.button.y>=22 && e.button.y<kMenuH && e.button.x>=400 && e.button.x<656) {
                editMode=EditMode((e.button.x-400)/64);continue;
            }
            if(e.type==SDL_KEYDOWN && tool==UNITS) {
                const auto key=e.key.keysym.sym;const bool ctrl=e.key.keysym.mod&(KMOD_CTRL|KMOD_GUI);
                if(ctrl && (key==SDLK_c || key==SDLK_x || key==SDLK_d))selectedUnits.copy(units);
                if(key==SDLK_DELETE || (ctrl && key==SDLK_x)) {
                    if(selectedUnits.remove(units)) {dirty=true;historyPending=true;unitsEdited=true;}continue;
                }
                if(ctrl && (key==SDLK_v || key==SDLK_d)) {
                    int mx,my;SDL_GetMouseState(&mx,&my);int cx=0,cz=0;
                    if(!mouseCell(int(mx/kUIScale),int(my/kUIScale),cx,cz)) {cx=int((mapView.offX()+canvasW/(2*mapView.zoom()))/16);cz=int((mapView.offY()+canvasH/(2*mapView.zoom()))/16);}
                    if(selectedUnits.paste(units,cx*16+8,cz*16+8,mapView.map().width*16,mapView.map().height*16)) {dirty=true;historyPending=true;unitsEdited=true;editMode=MODE_SELECT;}
                    continue;
                }
                if(ctrl && key==SDLK_c)continue;
                if(key==SDLK_RETURN && !selectedUnits.indices.empty()) {openModal(M_UNIT,*selectedUnits.indices.begin());continue;}
            }
            if(e.type==SDL_KEYDOWN && (e.key.keysym.mod & (KMOD_CTRL|KMOD_GUI)) &&
               (e.key.keysym.sym==SDLK_z || e.key.keysym.sym==SDLK_y)) {
                commitHistory();
                const bool redo=e.key.keysym.sym==SDLK_y || (e.key.keysym.mod & KMOD_SHIFT);
                restoreHistory(redo?history.redo():history.undo());continue;
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                if (dirty) {
                    // Close any transient overlay so the quit prompt gets input.
                    useOnlyOpen = false; scriptOpen = false; pickOpen = false;regionsOpen=false;
                    modal = M_QUITSAVE; SDL_StopTextInput();
                } else running = false;
            } else if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & (KMOD_CTRL|KMOD_GUI)) &&
                     e.key.keysym.sym == SDLK_s) {
                if(e.key.keysym.mod & KMOD_SHIFT) {openModal(M_SAVEAS);continue;}
                const bool loose = false;
                const bool ok=loose ? saveMap(outDir+"/"+mapName+".tnt") : saveBundle(outDir+"/"+mapName+".kmp");
                if(ok) {dirty=false;openMessage("SAVED",outDir+"/"+mapName+(loose?".tnt (loose export)":".kmp"));}
                else openMessage("SAVE FAILED",saveError);
            } else if(e.type==SDL_KEYDOWN && (e.key.keysym.mod & (KMOD_CTRL|KMOD_GUI)) && e.key.keysym.sym==SDLK_o) {
                openModal(M_OPENPATH);
            } else if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & (KMOD_CTRL|KMOD_GUI)) &&
                       e.key.keysym.sym == SDLK_b) {
                // Ctrl+B: save the finished map as a single .kmp bundle.
                bool ok = saveBundle(outDir + "/" + mapName + ".kmp");
                if (ok) dirty = false;
                openMessage("SAVE BUNDLE", ok ? ("Saved " + mapName + ".kmp")
                                              : saveError);
            } else if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & (KMOD_CTRL|KMOD_GUI)) &&
                       e.key.keysym.sym == SDLK_l) {
                // Land Lasso: toggle between land (terrain-stamp) and object mode.
                tool = tool == TERRAIN ? FEATURES : TERRAIN;
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_k) {
                clearArm = !clearArm;editMode=MODE_PLACE;    // Edit -> Clear Area (drag a box)
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_n) {
                // File -> New Map. Guard unsaved edits (New replaces the whole map).
                if (dirty) openConfirm("NEW MAP", "Discard unsaved changes and start "
                                       "a new map?", [&]() { wantNew = true; });
                else openModal(M_NEW);
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_p) {
                openModal(M_SCENARIO);   // Scenario -> Properties
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_r) {
                openModal(M_RESIZE);     // Scenario -> Resize
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_u) {
                useOnlyOpen = true;      // Scenario -> Use Only (unit restriction)
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_c) {
                checkMap();              // Scenario -> Check Map
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_t) {
                openScripting();         // Scenario -> Scripting (triggers)
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_LEFTBRACKET) {
                currentPlayer = (currentPlayer + 7) % 8;   // UNITS: pick placement player
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RIGHTBRACKET) {
                currentPlayer = (currentPlayer + 1) % 8;
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_g) {
                showGrid = !showGrid;   // View -> Grid
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_TAB) {
                tool = Tool((int(tool) + 1) % 4);   // cycle TERRAIN->FEATURES->UNITS->STARTS
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym >= SDLK_1 &&
                       e.key.keysym.sym <= SDLK_5) {
                // Zoom levels 1..5 = 100/75/50/25/12.5% (retail's five steps).
                mapView.setZoom(kZoomLevels[e.key.keysym.sym - SDLK_1]);
            } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                       e.button.button == SDL_BUTTON_LEFT && e.button.y >=22 && e.button.y < kMenuH) {
                // Toolbar buttons: TERRAIN | FEATURES | STARTS (each 72px).
                int bi = (e.button.x - 96) / 72;
                if (bi >= 0 && bi < 4) tool = Tool(bi);
            } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                       e.button.button == SDL_BUTTON_LEFT && e.button.x < kPaletteW &&
                       e.button.y >= kPaletteTop && e.button.y < h - kStatusH) {
                int row=-1;
                if(tool==UNITS || tool==STARTS)row=(e.button.y-kPaletteTop+paletteScroll)/26;
                else row=((e.button.y-kPaletteTop+paletteScroll)/kCellH)*cols+(e.button.x-4)/(kThumb+4);
                if(row>=0 && row<int(paletteItems.size())) {
                    const int index=paletteItems[size_t(row)];
                    if(tool==UNITS)selectedType=index;
                    else if(tool==FEATURES)selectedFeat=index;
                    else if(tool==STARTS)mapView.setOffset(scenario.starts[size_t(index)].xpos*16-canvasW/(2*mapView.zoom()),scenario.starts[size_t(index)].zpos*16-canvasH/(2*mapView.zoom()));
                    else selected=index;
                }
            } else if (e.type == SDL_MOUSEWHEEL) {
                int mxp, myp; SDL_GetMouseState(&mxp, &myp); mxp /= kUIScale; myp /= kUIScale;
                if (mxp < kPaletteW) {   // scroll the palette (tool-dependent count)
                    const int contentH=h-kStatusH-kPaletteTop;
                    const int total=tool==UNITS || tool==STARTS?int(paletteItems.size())*26:((int(paletteItems.size())+cols-1)/cols)*kCellH;
                    paletteScroll=std::clamp(paletteScroll-e.wheel.y*40,0,std::max(0,total-contentH));
                } else {
                    const float before=mapView.zoom();
                    const float next=std::clamp(before*(e.wheel.y>0?1.2f:1.0f/1.2f),.025f,4.0f);
                    mapView.setOffset(mapView.offX()+(mxp-kPaletteW)*(1/before-1/next),
                                      mapView.offY()+(myp-kMenuH)*(1/before-1/next));
                    mapView.setZoom(next);
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                       e.button.button == SDL_BUTTON_LEFT && e.button.x >= kPaletteW && e.button.y>=kMenuH && e.button.y<h-kStatusH) {
                if(editMode==MODE_PAN)continue;
                if(editMode==MODE_ERASE) {
                    if(tool==FEATURES)placeFeature(e.button.x,e.button.y,true);
                    else if(tool==UNITS) {const int hit=unitAt(e.button.x,e.button.y);if(hit>=0) {selectedUnits.indices={hit};selectedUnits.remove(units);dirty=true;historyPending=true;}}
                    else if(tool==STARTS) {const int hit=startAt(e.button.x,e.button.y);if(hit>=0) {scenario.starts.erase(scenario.starts.begin()+hit);dirty=true;historyPending=true;}}
                    continue;
                }
                if(editMode==MODE_SELECT && tool!=UNITS) {
                    if(tool==STARTS)draggingStart=startAt(e.button.x,e.button.y);
                    continue;
                }
                if(editMode==MODE_SELECT && tool==UNITS) {
                    const int hit=unitAt(e.button.x,e.button.y);int cx,cz;
                    if(mouseCell(e.button.x,e.button.y,cx,cz)) {
                        if(hit>=0) {
                            selectedUnits.click(hit,SDL_GetModState()&KMOD_SHIFT);
                            if(e.button.clicks>=2)openModal(M_UNIT,hit);
                            else selectedUnits.beginDrag(units,cx*16+8,cz*16+8);
                        } else {selectionBox=true;selectionAppend=SDL_GetModState()&KMOD_SHIFT;selectionX0=selectionX1=e.button.x;selectionZ0=selectionZ1=e.button.y;}
                    }
                    continue;
                }
                if (clearArm) {   // Clear Area: begin the drag box
                    clearDrag = true;
                    clx0 = clx1 = e.button.x; cly0 = cly1 = e.button.y;
                } else if (tool == TERRAIN) {
                    stampAtMouse(e.button.x, e.button.y, canvasW, canvasH);
                } else if (tool == FEATURES) {
                    placeFeature(e.button.x, e.button.y, false);
                } else if (tool == UNITS) {   // grab an existing unit, else place one
                    int hit = unitAt(e.button.x, e.button.y);
                    int cx, cz;
                    if (hit >= 0 && e.button.clicks >= 2) {
                        selectedUnits.indices={hit};openModal(M_UNIT, hit);   // double-click: edit properties
                    } else if (hit >= 0) {
                        selectedUnits.indices={hit};draggingUnit = hit;
                    } else if (selectedType >= 0 && mouseCell(e.button.x, e.button.y, cx, cz)) {
                        cart::PlacedUnit u;
                        u.type = unitTypes[size_t(selectedType)];
                        u.player = currentPlayer;
                        u.x = cx * 16.0f + 8; u.z = cz * 16.0f + 8;
                        units.push_back(u);
                        draggingUnit = int(units.size()) - 1;selectedUnits.indices={draggingUnit};
                        unitsEdited = true; dirty = true; historyPending=true;
                    }
                } else {   // STARTS: grab an existing marker, else place a new one
                    int hit = startAt(e.button.x, e.button.y);
                    int cx, cz;
                    if (hit >= 0) draggingStart = hit;
                    else if (mouseCell(e.button.x, e.button.y, cx, cz)) {
                        int num = 1;   // next free StartPosN
                        for (bool used = true; used; ++num) {
                            used = false;
                            for (auto& s : scenario.starts) if (s.number == num) used = true;
                        }
                        scenario.starts.push_back({num - 1, cx, cz}); dirty = true; historyPending=true;
                        draggingStart = int(scenario.starts.size()) - 1;
                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if(selectionBox) {
                    auto wx=[&](int x){return mapView.offX()+(x-kPaletteW)/mapView.zoom();};
                    auto wz=[&](int z){return mapView.offY()+(z-kMenuH)/mapView.zoom();};
                    selectedUnits.box(units,wx(selectionX0),wz(selectionZ0),wx(selectionX1),wz(selectionZ1),selectionAppend);
                    selectionBox=false;
                }
                selectedUnits.dragOrigins.clear();
                lastStampX=lastStampY=-1;
                draggingStart = -1; draggingUnit = -1;
                if (clearDrag) {   // Clear Area: confirm, then remove units + features
                    clearDrag = false;
                    int a0, b0, a1, b1;
                    if (mouseCell(std::min(clx0, clx1), std::min(cly0, cly1), a0, b0) &&
                        mouseCell(std::max(clx0, clx1), std::max(cly0, cly1), a1, b1)) {
                        int lox = std::min(a0, a1), hix = std::max(a0, a1);
                        int loz = std::min(b0, b1), hiz = std::max(b0, b1);
                        int nUnits = 0;
                        for (const auto& u : units) {
                            int ux = int(u.x / 16.0f), uz = int(u.z / 16.0f);
                            if (ux >= lox && ux <= hix && uz >= loz && uz <= hiz) ++nUnits;
                        }
                        int cells = (hix - lox + 1) * (hiz - loz + 1);
                        openConfirm("CLEAR AREA",
                                    "Remove all units and features in this area?  (" +
                                        std::to_string(nUnits) + " unit(s), " +
                                        std::to_string(cells) + " cells)",
                                    [&units, &mapView, &edited, &unitsEdited, &dirty, &historyPending, &selectedUnits,
                                     lox, hix, loz, hiz]() {
                            auto& m = mapView.editMap();
                            for (int z = loz; z <= hiz; ++z)
                                for (int x = lox; x <= hix; ++x)
                                    m.features[size_t(z) * m.width + x] = 0xFFFF;
                            units.erase(std::remove_if(units.begin(), units.end(),
                                [&](const cart::PlacedUnit& u) {
                                    int ux = int(u.x / 16.0f), uz = int(u.z / 16.0f);
                                    return ux >= lox && ux <= hix && uz >= loz && uz <= hiz;
                                }), units.end());
                            selectedUnits.indices.clear();
                            edited = true; unitsEdited = true; dirty = true; historyPending=true;
                        });
                    }
                    clearArm = false;
                }
            } else if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK) &&
                       e.motion.x >= kPaletteW) {
                if(editMode==MODE_PAN) {mapView.setOffset(mapView.offX()-pointerDX/mapView.zoom(),mapView.offY()-pointerDZ/mapView.zoom());continue;}
                if(editMode==MODE_ERASE) {if(tool==FEATURES)placeFeature(e.motion.x,e.motion.y,true);continue;}
                if(editMode==MODE_SELECT && tool!=UNITS) {
                    int cx,cz;
                    if(tool==STARTS && draggingStart>=0 && mouseCell(e.motion.x,e.motion.y,cx,cz)) {
                        scenario.starts[size_t(draggingStart)].xpos=cx;scenario.starts[size_t(draggingStart)].zpos=cz;dirty=true;historyPending=true;
                    }
                    continue;
                }
                if(editMode==MODE_SELECT && tool==UNITS) {
                    if(selectionBox) {selectionX1=e.motion.x;selectionZ1=e.motion.y;}
                    else {int cx,cz;if(mouseCell(e.motion.x,e.motion.y,cx,cz) && selectedUnits.drag(units,cx*16+8,cz*16+8,mapView.map().width*16,mapView.map().height*16)) {dirty=true;historyPending=true;}}
                    continue;
                }
                if (clearDrag) {   // Clear Area: grow the box
                    clx1 = e.motion.x; cly1 = e.motion.y;
                } else if (tool == TERRAIN) {
                    stampAtMouse(e.motion.x, e.motion.y, canvasW, canvasH);   // drag-paint
                } else if (tool == FEATURES) {
                    placeFeature(e.motion.x, e.motion.y, false);   // drag-place features
                } else if (tool == UNITS && draggingUnit >= 0) {
                    int cx, cz;   // drag a unit to a new cell centre
                    if (mouseCell(e.motion.x, e.motion.y, cx, cz)) {
                        units[size_t(draggingUnit)].x = cx * 16.0f + 8;
                        units[size_t(draggingUnit)].z = cz * 16.0f + 8;
                        unitsEdited = true; dirty = true; historyPending=true;
                    }
                } else if (draggingStart >= 0) {
                    int cx, cz;   // drag a start marker to a new cell
                    if (mouseCell(e.motion.x, e.motion.y, cx, cz)) {
                        scenario.starts[size_t(draggingStart)].xpos = cx;
                        scenario.starts[size_t(draggingStart)].zpos = cz; dirty = true; historyPending=true;
                    }
                }
            } else if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_RMASK)) {
                mapView.setOffset(mapView.offX()-pointerDX/mapView.zoom(),mapView.offY()-pointerDZ/mapView.zoom());
            } else if (e.type != SDL_MOUSEBUTTONDOWN && e.type != SDL_MOUSEMOTION) {
                mapView.input(e);
            }
        }

        // A confirmed New (discarding unsaved edits) opens the dialog now, after
        // the confirm's applyModal has closed itself.
        if (wantNew) { wantNew = false; openModal(M_NEW); }

        if(overlayInvalidated || historyPending || resetHistory) {overlayTexture.reset();overlayLegend.clear();overlayInvalidated=false;}
        if((historyPending || resetHistory) && !(SDL_GetMouseState(nullptr,nullptr)&(SDL_BUTTON_LMASK|SDL_BUTTON_RMASK)))
            commitHistory();
        const std::string title="Cartographer -- "+mapName+(modal==M_GENERATING?" [Generating]":modal==M_GENERATED?" [Generated preview]":modal==M_ANALYZING?" [Analyzing]":overlayTexture?" [Terrain overlay]":"")+(dirty?" *":"");
        SDL_SetWindowTitle(win,title.c_str());
        if(interactive && !recoveryFolder.empty()) {
            if(recoveryJob.valid() && recoveryJob.wait_for(std::chrono::seconds(0))==std::future_status::ready)collectRecovery();
            if(!dirty)clearRecovery();
            else if(!recoveryJob.valid() && recoveryClock()>=nextRecovery && !historyPending) {
                nextRecovery=recoveryClock()+60000;
                try {
                    auto files=cart::documentFiles(mapView.map(),scenario,scen,units,useOnly,mapName);
                    recoveryFilesPresent=true;
                    recoveryJob=std::async(std::launch::async,[files=std::move(files),path=recoveryFile]() {
                        std::string error;cart::writeDocumentBundle(path,files,error);return error;
                    });
                } catch(const std::exception& error) {recoveryStatus=error.what();}
            }
        }
        refreshMinimap();
        mapView.ensureChunks(canvasW, canvasH);

        SDL_SetRenderDrawColor(ren, 24, 26, 32, 255);
        SDL_RenderClear(ren);

        // Map canvas (right of the palette, between menu and status).
        SDL_Rect canvas{kPaletteW, kMenuH, canvasW, canvasH};
        SDL_RenderSetViewport(ren, &canvas);
        mapView.draw(canvasW, canvasH);
        if(overlayTexture) {
            SDL_FRect destination{-mapView.offX()*mapView.zoom(),-mapView.offY()*mapView.zoom(),overlayWidth*16*mapView.zoom(),overlayHeight*16*mapView.zoom()};
            SDL_RenderCopyF(ren,overlayTexture.get(),nullptr,&destination);
            fillRect(ren,4,4,std::min(canvasW-8,cart::textWidth(overlayLegend,1)+8),17,15,20,30);
            cart::drawText(ren,overlayLegend,8,9,1,240,240,210);
        }
        // Grid overlay: section (512px) lines bright, block (32px) lines faint.
        if (showGrid) {
            float zm = mapView.zoom();
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            int firstBlkX = int(mapView.offX()) / 32, firstBlkY = int(mapView.offY()) / 32;
            for (int bxg = firstBlkX; ; ++bxg) {
                float sx = (bxg * 32 - mapView.offX()) * zm;
                if (sx > canvasW) break;
                bool section = (bxg % 16) == 0;
                SDL_SetRenderDrawColor(ren, 255, 255, 255, section ? 90 : 30);
                SDL_RenderDrawLine(ren, int(sx), 0, int(sx), canvasH);
            }
            for (int byg = firstBlkY; ; ++byg) {
                float sy = (byg * 32 - mapView.offY()) * zm;
                if (sy > canvasH) break;
                bool section = (byg % 16) == 0;
                SDL_SetRenderDrawColor(ren, 255, 255, 255, section ? 90 : 30);
                SDL_RenderDrawLine(ren, 0, int(sy), canvasW, int(sy));
            }
        }
        if(showRegions)for(const auto& region:scen.regions) {
            const float zoom=mapView.zoom();
            SDL_FRect box{(std::min(region.x1,region.x2)*16-mapView.offX())*zoom,(std::min(region.z1,region.z2)*16-mapView.offY())*zoom,
                (std::abs(region.x2-region.x1)+1)*16*zoom,(std::abs(region.z2-region.z1)+1)*16*zoom};
            SDL_SetRenderDrawBlendMode(ren,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(ren,80,190,245,45);SDL_RenderFillRectF(ren,&box);
            SDL_SetRenderDrawColor(ren,120,215,255,230);SDL_RenderDrawRectF(ren,&box);
            cart::drawText(ren,region.name,int(box.x)+3,int(box.y)+3,1,180,235,255);
        }
        // Placed features: draw each non-empty feature-plane cell's sprite at its
        // cell, anchored like the game. Culled to the visible canvas.
        {
            const auto& mp = mapView.map();
            float zm = mapView.zoom();
            const int minX=std::max(0,int((mapView.offX()-512)/16));
            const int minZ=std::max(0,int((mapView.offY()-512)/16));
            const int maxX=std::min(mp.width,int((mapView.offX()+canvasW/zm+512)/16)+1);
            const int maxZ=std::min(mp.height,int((mapView.offY()+canvasH/zm+512)/16)+1);
            for (int cz = minZ; cz < maxZ; ++cz)
                for (int cx = minX; cx < maxX; ++cx) {
                    uint16_t v = mp.features[size_t(cz) * mp.width + cx];
                    if (v >= 0xFFFA || v >= mp.featureNames.size()) continue;
                    float bx = (cx * 16.0f - mapView.offX()) * zm;
                    float by = (cz * 16.0f - mapView.offY()) * zm;
                    if (bx < -64 || by < -64 || bx > canvasW + 64 || by > canvasH + 64) continue;
                    const cart::FeatureRef* r = features.byName(mp.featureNames[v]);
                    if (!r) continue;
                    SDL_Texture* t = featTextureFor(*r);
                    const cart::FeatSprite* sp = features.sprite(vfs, *r);
                    if (!t || !sp || sp->w == 0) {   // no art: a small marker
                        SDL_SetRenderDrawColor(ren, 90, 200, 90, 220);
                        SDL_Rect dot{int(bx) - 2, int(by) - 2, 4, 4};
                        SDL_RenderFillRect(ren, &dot);
                        continue;
                    }
                    SDL_FRect dst{bx - sp->xoff * zm, by - sp->yoff * zm,
                                  sp->w * zm, sp->h * zm};
                    SDL_RenderCopyF(ren, t, nullptr, &dst);
                }
        }
        // Placed units: a player-coloured square with the type name above it.
        for (int i = 0; i < int(units.size()); ++i) {
            float sx = (units[i].x - mapView.offX()) * mapView.zoom();
            float sy = (units[i].z - mapView.offY()) * mapView.zoom();
            if (sx < -20 || sy < -20 || sx > canvasW + 20 || sy > canvasH + 20) continue;
            const Uint8* pc = kPlayerCol[std::clamp(units[i].player,0,8)];
            SDL_SetRenderDrawColor(ren, pc[0], pc[1], pc[2], 255);
            SDL_Rect r{int(sx) - 5, int(sy) - 5, 10, 10};
            SDL_RenderFillRect(ren, &r);
            SDL_SetRenderDrawColor(ren, 12, 12, 16, 255);
            SDL_RenderDrawRect(ren, &r);
            if(selectedUnits.contains(i)) {
                const auto* type=unitRegistry.find(lowerText(units[i].type));
                const float width=(type?type->footX:1)*16*mapView.zoom(),height=(type?type->footZ:1)*16*mapView.zoom();
                SDL_SetRenderDrawColor(ren,255,225,100,255);
                SDL_FRect footprint{sx-width/2,sy-height/2,width,height};SDL_RenderDrawRectF(ren,&footprint);
                const float angle=units[i].angle*3.14159265f/180;
                SDL_RenderDrawLineF(ren,sx,sy,sx+std::sin(angle)*22,sy-std::cos(angle)*22);
            }
            if (mapView.zoom() > 0.28f)   // label only when there's room
                cart::drawText(ren, units[i].type,
                               int(sx) - cart::textWidth(units[i].type, 1) / 2,
                               int(sy) - 15, 1, 235, 235, 245);
        }
        if(selectionBox) {
            SDL_SetRenderDrawColor(ren,255,220,110,255);
            SDL_Rect area{std::min(selectionX0,selectionX1)-kPaletteW,std::min(selectionZ0,selectionZ1)-kMenuH,std::abs(selectionX1-selectionX0),std::abs(selectionZ1-selectionZ0)};
            SDL_RenderDrawRect(ren,&area);
        }
        // Start-position markers (drawn in canvas-local coords: gold diamonds
        // with the StartPos number). Off-map ones simply fall outside.
        for (int i = 0; i < int(scenario.starts.size()); ++i) {
            float sx = (scenario.starts[i].xpos * 16.0f - mapView.offX()) * mapView.zoom();
            float sy = (scenario.starts[i].zpos * 16.0f - mapView.offY()) * mapView.zoom();
            if (sx < -12 || sy < -12 || sx > canvasW + 12 || sy > canvasH + 12) continue;
            SDL_Vertex d[4] = {
                {{sx, sy - 9}, {255, 205, 70, 255}, {0, 0}},
                {{sx + 9, sy}, {255, 205, 70, 255}, {0, 0}},
                {{sx, sy + 9}, {255, 205, 70, 255}, {0, 0}},
                {{sx - 9, sy}, {255, 205, 70, 255}, {0, 0}},
            };
            int di[6] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(ren, nullptr, d, 4, di, 6);
            SDL_SetRenderDrawColor(ren, 30, 20, 0, 255);
            SDL_Rect out{int(sx) - 9, int(sy) - 9, 18, 18};
            SDL_RenderDrawRect(ren, &out);
            cart::drawText(ren, std::to_string(scenario.starts[i].number),
                           int(sx) - 2, int(sy) - 3, 1, 20, 14, 0);
        }
        // Preview the actual snapped stamp / feature / footprint before committing.
        int previewX,previewZ;SDL_GetMouseState(&previewX,&previewZ);previewX=int(previewX/kUIScale);previewZ=int(previewZ/kUIScale);
        if(editMode==MODE_PLACE && modal==M_NONE && !scriptOpen && !useOnlyOpen && !regionsOpen && menuOpen<0 &&
           previewX>=kPaletteW && previewZ>=kMenuH && previewZ<h-kStatusH && !cart::pointIn(previewX,previewZ,miniRect)) {
            const float wx=mapView.offX()+(previewX-kPaletteW)/mapView.zoom(),wz=mapView.offY()+(previewZ-kMenuH)/mapView.zoom();
            SDL_FRect box{};SDL_Texture* ghost=nullptr;
            if(tool==TERRAIN && selected>=0) {
                const auto& section=sections.list()[size_t(selected)];const auto* map=sections.load(vfs,section.path);
                if(map && map->blocksX>0 && map->blocksY>0) {
                    const int x=(int(wx/32)/map->blocksX)*map->blocksX,z=(int(wz/32)/map->blocksY)*map->blocksY;
                    box={(x*32-mapView.offX())*mapView.zoom(),(z*32-mapView.offY())*mapView.zoom(),map->blocksX*32*mapView.zoom(),map->blocksY*32*mapView.zoom()};
                    ghost=thumbFor(section.path);
                }
            } else if(tool==FEATURES && selectedFeat>=0) {
                const auto& feature=features.list()[size_t(selectedFeat)];const auto* sprite=features.sprite(vfs,feature);
                ghost=featTextureFor(feature);
                if(sprite)box={(int(wx/16)*16-mapView.offX()-sprite->xoff)*mapView.zoom(),(int(wz/16)*16-mapView.offY()-sprite->yoff)*mapView.zoom(),sprite->w*mapView.zoom(),sprite->h*mapView.zoom()};
            } else {
                const auto* type=tool==UNITS?unitInfo(selectedType):nullptr;
                const float width=(type?type->footX:1)*16*mapView.zoom(),height=(type?type->footZ:1)*16*mapView.zoom();
                box={(int(wx/16)*16+8-mapView.offX())*mapView.zoom()-width/2,(int(wz/16)*16+8-mapView.offY())*mapView.zoom()-height/2,width,height};
            }
            if(ghost) {Uint8 alpha=255;SDL_BlendMode blend;SDL_GetTextureAlphaMod(ghost,&alpha);SDL_GetTextureBlendMode(ghost,&blend);SDL_SetTextureAlphaMod(ghost,120);SDL_SetTextureBlendMode(ghost,SDL_BLENDMODE_BLEND);SDL_RenderCopyF(ren,ghost,nullptr,&box);SDL_SetTextureAlphaMod(ghost,alpha);SDL_SetTextureBlendMode(ghost,blend);}
            SDL_SetRenderDrawColor(ren,255,220,90,220);SDL_RenderDrawRectF(ren,&box);
        }
        SDL_RenderSetViewport(ren, nullptr);

        if(minimapTexture) {
            SDL_RenderCopy(ren,minimapTexture.get(),nullptr,&miniRect);
            SDL_SetRenderDrawColor(ren,235,225,195,255);SDL_RenderDrawRect(ren,&miniRect);
            SDL_RenderSetClipRect(ren,&miniRect);
            const float sx=float(miniRect.w)/(mapView.map().width*16),sz=float(miniRect.h)/(mapView.map().height*16);
            SDL_FRect view{miniRect.x+mapView.offX()*sx,miniRect.y+mapView.offY()*sz,canvasW/mapView.zoom()*sx,canvasH/mapView.zoom()*sz};
            SDL_RenderDrawRectF(ren,&view);
            for(const auto& start:scenario.starts) {SDL_Rect marker{int(miniRect.x+start.xpos*16*sx)-2,int(miniRect.y+start.zpos*16*sz)-2,4,4};SDL_RenderFillRect(ren,&marker);}
            SDL_RenderSetClipRect(ren,nullptr);
        }

        // Palette panel (left).
        fillRect(ren, 0, kMenuH, kPaletteW, canvasH, 30, 32, 40);
        SDL_Rect palClip{0,kPaletteTop,kPaletteW,h-kStatusH-kPaletteTop};
        SDL_RenderSetClipRect(ren,&palClip);
        if(tool==UNITS || tool==STARTS) {
            for(int row=0;row<int(paletteItems.size());++row) {
                const int i=paletteItems[size_t(row)],y=kPaletteTop+row*26-paletteScroll;
                if(y+24<kPaletteTop || y>h-kStatusH)continue;
                if(tool==UNITS && i==selectedType)fillRect(ren,0,y,kPaletteW,25,70,66,40);
                if(tool==UNITS) {
                    const auto* info=unitInfo(i);
                    cart::drawText(ren,info?info->name:unitTypes[size_t(i)],6,y+3,1,235,230,200);
                    cart::drawText(ren,unitTypes[size_t(i)]+" - "+unitCategory(i),6,y+14,1,160,175,195);
                } else cart::drawText(ren,paletteLabel(i),6,y+7,1,235,210,100);
            }
        } else {
            const int selection=tool==FEATURES?selectedFeat:selected;
            for(int row=0;row<int(paletteItems.size());++row) {
                const int i=paletteItems[size_t(row)],x=4+(row%cols)*(kThumb+4),y=kPaletteTop+4+(row/cols)*kCellH-paletteScroll;
                if(y+kCellH<kPaletteTop || y>h-kStatusH)continue;
                SDL_Rect cell{x,y,kThumb,kThumb};fillRect(ren,x,y,kThumb,kThumb,44,46,54);
                auto* texture=tool==FEATURES?featTextureFor(features.list()[size_t(i)]):thumbFor(sections.list()[size_t(i)].path);
                if(texture) {
                    if(tool==FEATURES) {
                        const auto* sprite=features.sprite(vfs,features.list()[size_t(i)]);
                        const float scale=std::min(float(kThumb)/std::max(sprite->w,1),float(kThumb)/std::max(sprite->h,1));
                        SDL_Rect fit{x+(kThumb-int(sprite->w*scale))/2,y+(kThumb-int(sprite->h*scale))/2,int(sprite->w*scale),int(sprite->h*scale)};
                        SDL_RenderCopy(ren,texture,nullptr,&fit);
                    } else SDL_RenderCopy(ren,texture,nullptr,&cell);
                }
                if(i==selection) {SDL_SetRenderDrawColor(ren,255,210,90,255);SDL_RenderDrawRect(ren,&cell);}
                cart::drawText(ren,paletteLabel(i).substr(0,14),x,y+kThumb+4,1,210,210,215);
            }
        }
        SDL_RenderSetClipRect(ren,nullptr);
        cart::drawField(ren,4,kMenuH+3,kPaletteW-8,"SEARCH (CTRL+F)",paletteSearch,paletteFocus,paletteFocus?&paletteEditor:nullptr);
        cart::drawButton(ren,4,kMenuH+33,kPaletteW-8,18,paletteCategories[size_t(categoryIndex)].substr(0,28),false);
        if(tool==UNITS) {
            static const char* factionNames[]={"ALL FACTIONS","ARAMON","TAROS","VERUNA","ZHON","CREON"};
            cart::drawButton(ren,4,kMenuH+54,kPaletteW-8,18,factionNames[factionIndex],false);
        } else cart::drawText(ren,std::to_string(paletteItems.size())+" MATCHES",6,kMenuH+59,1,180,190,200);

        // Chrome strips.
        fillRect(ren, 0, 0, w, kMenuH, 46, 48, 58);
        fillRect(ren, 0, kMenuH - 1, w, 1, 12, 12, 16);
        fillRect(ren, kPaletteW - 1, kMenuH, 1, canvasH, 12, 12, 16);
        fillRect(ren, 0, h - kStatusH, w, kStatusH, 38, 40, 50);
        fillRect(ren, 0, h - kStatusH, w, 1, 12, 12, 16);

        // Menu strip: title + tool buttons (active one highlighted).
        cart::drawText(ren, "CARTOGRAPHER", 6, 7, 1, 200, 200, 210);
        const char* names[4] = {"TERRAIN", "FEATURES", "UNITS", "STARTS"};
        for (int t = 0; t < 4; ++t) {
            int bx = 96 + t * 72;
            bool active = int(tool) == t;
            fillRect(ren, bx, 25, 68, 16, active ? 90 : 60, active ? 80 : 62,
                     active ? 40 : 74);
            cart::drawText(ren, names[t], bx + 8, 29, 1, active ? 255 : 190,
                           active ? 220 : 190, active ? 120 : 200);
        }

        static const char* modes[]={"PLACE","SELECT","ERASE","PAN"};
        for(int i=0;i<4;++i)cart::drawButton(ren,400+i*64,25,60,16,modes[i],int(editMode)==i);

        for(size_t i=0;i<menuNames.size();++i) {
            const int x=96+int(i)*72;
            fillRect(ren,x,2,68,18,menuOpen==int(i)?90:46,menuOpen==int(i)?80:48,menuOpen==int(i)?40:58);
            cart::drawText(ren,menuNames[i],x+6,7,1,230,230,240);
        }
        if(menuOpen>=0) {
            const int x=96+menuOpen*72;const auto& rows=menuRows[size_t(menuOpen)];
            fillRect(ren,x,22,240,int(rows.size())*20,46,48,58);
            for(size_t i=0;i<rows.size();++i) {
                std::string label=rows[i];
                if(menuOpen==1 && i==3)label=std::string(stampLayers.objects?"[ ] ":"[X] ")+"Brush: protect objects";
                cart::drawText(ren,label,x+8,28+int(i)*20,1,235,235,240);
            }
        }

        // Status bar: cursor cell, tool, zoom, start count.
        int mxg, myg; SDL_GetMouseState(&mxg, &myg); mxg /= kUIScale; myg /= kUIScale;
        int ccx, ccz;
        std::string coord = mouseCell(mxg, myg, ccx, ccz)
            ? "( " + std::to_string(ccx) + ", " + std::to_string(ccz) + " )" : "";
        char zbuf[16];
        std::snprintf(zbuf, sizeof zbuf, "%d%%", int(mapView.zoom() * 100 + 0.5f));
        std::string status = coord + "   TOOL: " + names[int(tool)] + "   ZOOM: " + zbuf;
        if(tool==TERRAIN)status+=stampLayers.objects?"   BRUSH: TERRAIN + OBJECTS":"   BRUSH: OBJECTS PROTECTED";
        if (tool == UNITS)
            status += "   PLAYER: " + std::to_string(currentPlayer) +
                      "   UNITS: " + std::to_string(units.size())+"   SELECTED: "+std::to_string(selectedUnits.indices.size());
        else
            status += "   STARTS: " + std::to_string(scenario.starts.size());
        status += dirty?"   UNSAVED":"   SAVED";
        if (!useOnly.empty()) status += "   USEONLY: " + std::to_string(useOnly.size());
        if(!recoveryStatus.empty()) status+="   "+recoveryStatus;
        if (clearArm) status += "   CLEAR AREA: drag a box (K cancels)";
        if(mxg<kPaletteW && myg>=kPaletteTop && myg<h-kStatusH) {
            const int row=tool==UNITS || tool==STARTS?(myg-kPaletteTop+paletteScroll)/26:
                ((myg-kPaletteTop+paletteScroll)/kCellH)*cols+(mxg-4)/(kThumb+4);
            if(row>=0 && row<int(paletteItems.size()))status=paletteLabel(paletteItems[size_t(row)]);
        }
        cart::drawText(ren, status, 6, h - kStatusH + 7, 1, 200, 205, 215);

        // Clear Area drag box (screen-space rectangle).
        if (clearDrag) {
            SDL_Rect box{std::min(clx0, clx1), std::min(cly0, cly1),
                         std::abs(clx1 - clx0), std::abs(cly1 - cly0)};
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 230, 90, 80, 60); SDL_RenderFillRect(ren, &box);
            SDL_SetRenderDrawColor(ren, 240, 120, 100, 220); SDL_RenderDrawRect(ren, &box);
        }

        if(regionsOpen) {
            const auto ct=cart::drawPanel(ren,w,h,560,350,"REGIONS - CLICK TO LOCATE, DOUBLE CLICK TO EDIT");
            regionNew=cart::drawButton(ren,ct.x,ct.y,64,18,"NEW",false);
            regionEdit=cart::drawButton(ren,ct.x+72,ct.y,64,18,"EDIT",false);
            regionDelete=cart::drawButton(ren,ct.x+144,ct.y,64,18,"DELETE",false);
            regionDone=cart::drawButton(ren,ct.x+ct.w-64,ct.y,64,18,"DONE",true);
            regionList={ct.x,ct.y+26,ct.w,ct.h-26};SDL_RenderSetClipRect(ren,&regionList);
            for(int i=0;i<int(scen.regions.size());++i) {
                const auto& r=scen.regions[i];int y=regionList.y+i*20-regionScroll;
                if(i==regionSelected) {SDL_SetRenderDrawColor(ren,55,80,105,255);SDL_Rect row{ct.x,y,ct.w,20};SDL_RenderFillRect(ren,&row);}
                cart::drawText(ren,r.name+"  ("+std::to_string(r.x1)+","+std::to_string(r.z1)+") - ("+std::to_string(r.x2)+","+std::to_string(r.z2)+")",ct.x+4,y+6,1,215,230,245);
            }
            SDL_RenderSetClipRect(ren,nullptr);
        }
        // Scripting (trigger) overlay: per-player rule groups + their
        // conditions and actions. Drawn under the picker / param modal.
        if (scriptOpen) {
            constexpr int kRow = 12;
            SDL_Rect ct = cart::drawPanel(ren, w, h, 780, 520, "SCENARIO SCRIPTING");
            auto& gs = scen.players[size_t(scrPlayer)];
            tak::crt::RuleGroup* g = curGroup();
            // Header row: player nav, rule count, Done.
            rPrevP = cart::drawButton(ren, ct.x, ct.y, 18, 14, "<", false);
            cart::drawText(ren, "PLAYER " + std::to_string(scrPlayer), ct.x + 24, ct.y + 3, 1, 220, 224, 235);
            rNextP = cart::drawButton(ren, ct.x + 104, ct.y, 18, 14, ">", false);
            cart::drawText(ren, std::to_string(gs.size()) + " RULES", ct.x + 132, ct.y + 3, 1, 175, 185, 200);
            rScrDone = cart::drawButton(ren, ct.x + ct.w - 60, ct.y, 56, 14, "DONE", true);

            int colW = (ct.w - 20) / 3;
            int x0 = ct.x, x1 = ct.x + colW + 10, x2 = ct.x + 2 * colW + 20;
            ruleCopy=cart::drawButton(ren,ct.x,ct.y+21,52,16,"COPY",false);
            rulePaste=cart::drawButton(ren,ct.x+58,ct.y+21,52,16,"PASTE",false);
            ruleDuplicate=cart::drawButton(ren,ct.x+116,ct.y+21,64,16,"DUPLICATE",false);
            ruleUp=cart::drawButton(ren,ct.x+186,ct.y+21,38,16,"UP",false);
            ruleDown=cart::drawButton(ren,ct.x+230,ct.y+21,38,16,"DOWN",false);
            playerCopy=cart::drawButton(ren,ct.x+274,ct.y+21,86,16,"COPY PLAYER",false);
            playerPaste=cart::drawButton(ren,ct.x+366,ct.y+21,92,16,"APPEND PLAYER",false);
            cart::drawText(ren, ruleColumn==cart::RuleColumn::Group?"RULES *":"RULES", x0, ct.y + 46, 1, 150, 200, 150);
            cart::drawText(ren, ruleColumn==cart::RuleColumn::Condition?"CONDITIONS *":"CONDITIONS", x1, ct.y + 46, 1, 150, 200, 150);
            cart::drawText(ren, ruleColumn==cart::RuleColumn::Action?"ACTIONS *":"ACTIONS", x2, ct.y + 46, 1, 150, 200, 150);
            int listY = ct.y + 58, listH = ct.h - 58 - 24;
            rRuleList = {x0, listY, colW, listH};
            rCondList = {x1, listY, colW, listH};
            rActList = {x2, listY, colW, listH};
            for (const SDL_Rect* a : {&rRuleList, &rCondList, &rActList}) {
                SDL_SetRenderDrawColor(ren, 22, 24, 32, 255); SDL_RenderFillRect(ren, a);
                SDL_SetRenderDrawColor(ren, 70, 74, 90, 255); SDL_RenderDrawRect(ren, a);
            }
            // Rule (group) list.
            SDL_RenderSetClipRect(ren, &rRuleList);
            for (int i = 0; i < int(gs.size()); ++i) {
                int ry = listY + i * kRow - scrRuleScroll;
                if (ry + kRow < listY || ry > listY + listH) continue;
                bool sel = i == scrGroup;
                if (sel) { SDL_SetRenderDrawColor(ren, 58, 68, 95, 255); SDL_Rect hr{x0, ry, colW, kRow}; SDL_RenderFillRect(ren, &hr); }
                std::string lbl = "Rule " + std::to_string(i + 1) + "  " +
                    std::to_string(gs[size_t(i)].conditions.size()) + "c/" +
                    std::to_string(gs[size_t(i)].actions.size()) + "a";
                cart::drawText(ren, lbl, x0 + 3, ry + 2, 1, sel ? 255 : 200, sel ? 235 : 205, sel ? 200 : 215);
            }
            SDL_RenderSetClipRect(ren, nullptr);
            // Conditions + actions of the selected group.
            auto drawRules = [&](const SDL_Rect& area, int scroll, const std::vector<tak::crt::Rule>* rules,
                                 bool isAct, int selRow) {
                if (!rules) return;
                SDL_RenderSetClipRect(ren, &area);
                for (int i = 0; i < int(rules->size()); ++i) {
                    int ry = area.y + i * kRow - scroll;
                    if (ry + kRow < area.y || ry > area.y + area.h) continue;
                    bool sel = i == selRow;
                    if (sel) { SDL_SetRenderDrawColor(ren, 58, 68, 95, 255); SDL_Rect hr{area.x, ry, area.w, kRow}; SDL_RenderFillRect(ren, &hr); }
                    cart::drawText(ren, cart::formatRule(isAct, (*rules)[size_t(i)]), area.x + 3, ry + 2, 1,
                                   sel ? 255 : 205, sel ? 235 : 210, sel ? 200 : 220);
                }
                SDL_RenderSetClipRect(ren, nullptr);
            };
            drawRules(rCondList, scrCondScroll, g ? &g->conditions : nullptr, false, scrCondSel);
            drawRules(rActList, scrActScroll, g ? &g->actions : nullptr, true, scrActSel);
            // Column action buttons.
            int by = ct.y + ct.h - 18;
            rAddRule = cart::drawButton(ren, x0, by, 44, 16, "+RULE", true);
            rDelRule = cart::drawButton(ren, x0 + 48, by, 44, 16, "-RULE", false);
            rAddCond = cart::drawButton(ren, x1, by, 44, 16, "+COND", true);
            rDelCond = cart::drawButton(ren, x1 + 48, by, 44, 16, "-COND", false);
            rAddAct = cart::drawButton(ren, x2, by, 40, 16, "+ACT", true);
            rDelAct = cart::drawButton(ren, x2 + 44, by, 40, 16, "-ACT", false);
            cart::drawText(ren, "Ctrl+C/V/D: copy/paste/duplicate. Alt+Up/Down: reorder selected column.",
                           ct.x, by - 12, 1, 150, 154, 168);

            // Nested opcode picker (choose a condition/action type to add).
            if (pickOpen) {
                const auto& defs = pickAction ? cart::actionDefs() : cart::conditionDefs();
                SDL_Rect pc = cart::drawPanel(ren, w, h, 480, 400, pickAction ? "ADD ACTION" : "ADD CONDITION");
                rPickList = {pc.x, pc.y, pc.w, pc.h - 28};
                SDL_RenderSetClipRect(ren, &rPickList);
                for (int i = 0; i < int(defs.size()); ++i) {
                    int ry = pc.y + i * kRow - pickScroll;
                    if (ry + kRow < pc.y || ry > pc.y + rPickList.h) continue;
                    cart::drawText(ren, defs[size_t(i)].templ, pc.x + 3, ry + 2, 1, 210, 214, 225);
                }
                SDL_RenderSetClipRect(ren, nullptr);
                rPickCancel = cart::drawButton(ren, pc.x + pc.w - 74, pc.y + pc.h - 20, 70, 18, "CANCEL", false);
            }
        }

        // Save-before-exit prompt: SAVE / DON'T SAVE / CANCEL.
        if(modal==M_GENERATED && generatedPreview) {
            const auto ct=cart::drawPanel(ren,w,h,460,465,"GENERATED MAP PREVIEW");
            const auto& map=generatedPreview->map;
            const float scale=330.f/std::max(map.width,map.height);
            SDL_FRect area{float(ct.x)+(ct.w-map.width*scale)/2, float(ct.y)+28,map.width*scale,map.height*scale};
            SDL_RenderCopyF(ren,generatedTexture.get(),nullptr,&area);
            for(const auto& start:generatedPreview->starts) {
                const float x=area.x+start.xpos*scale,y=area.y+start.zpos*scale;
                SDL_SetRenderDrawColor(ren,255,220,80,255);SDL_FRect dot{x-3,y-3,6,6};SDL_RenderFillRectF(ren,&dot);
                cart::drawText(ren,std::to_string(start.number),int(x)+5,int(y)-3,1,255,240,120);
            }
            cart::drawText(ren,std::to_string(map.width/32)+" x "+std::to_string(map.height/32)+"  "+std::to_string(generatedPreview->starts.size())+" PLAYERS",ct.x,ct.y,1,230,235,245);
            cart::drawText(ren,"Your current map changes only when accepted.",ct.x,ct.y+370,1,195,210,225);
            mOK=cart::drawButton(ren,ct.x+ct.w-150,ct.y+ct.h-20,70,18,"ACCEPT",true);
            mCancel=cart::drawButton(ren,ct.x+ct.w-74,ct.y+ct.h-20,70,18,"DISCARD",false);
        } else if(modal==M_ANALYZING) {
            const auto ct=cart::drawPanel(ren,w,h,400,105,"ANALYZING TERRAIN");
            cart::drawText(ren,discardOverlay?"Discarding result when analysis finishes...":"Applying engine terrain and footprint rules...",ct.x,ct.y,1,225,230,240);
            cart::drawText(ren,"Esc cancels. Map editing resumes when ready.",ct.x,ct.y+25,1,190,205,220);
        } else if(modal==M_GENERATING) {
            const auto ct=cart::drawPanel(ren,w,h,400,105,"GENERATING MAP");
            const std::string dots((SDL_GetTicks64()/400)%4,'.');
            cart::drawText(ren,cancelGeneration?"Discarding result when generation finishes...":"Building terrain"+dots,ct.x,ct.y,1,225,230,240);
            cart::drawText(ren,"Esc cancels. Your current map is retained.",ct.x,ct.y+25,1,190,205,220);
        } else if (modal == M_QUITSAVE) {
            SDL_Rect ct = cart::drawPanel(ren, w, h, 380, 92, "UNSAVED CHANGES");
            cart::drawText(ren, "Save changes before exiting?", ct.x, ct.y, 1, 225, 228, 236);
            mOK = cart::drawButton(ren, ct.x, ct.y + ct.h - 20, 60, 18, "SAVE", true);
            mQuit = cart::drawButton(ren, ct.x + 66, ct.y + ct.h - 20, 78, 18, "DISCARD", false);
            mCancel = cart::drawButton(ren, ct.x + ct.w - 74, ct.y + ct.h - 20, 70, 18, "CANCEL", false);
        }
        // Message box (Check Map result) / confirmation: wrapped text + OK
        // (message) or OK + CANCEL (confirm).
        else if (modal == M_MESSAGE || modal == M_CONFIRM) {
            int ph = 66 + int(mMsg.size()) * 12;
            SDL_Rect ct = cart::drawPanel(ren, w, h, 360, ph, mTitle);
            for (size_t i = 0; i < mMsg.size(); ++i)
                cart::drawText(ren, mMsg[i], ct.x, ct.y + int(i) * 12, 1, 225, 228, 236);
            if (modal == M_CONFIRM) {
                mOK = cart::drawButton(ren, ct.x + ct.w - 150, ct.y + ct.h - 20, 70, 18, "OK", true);
                mCancel = cart::drawButton(ren, ct.x + ct.w - 74, ct.y + ct.h - 20, 70, 18, "CANCEL", false);
            } else {
                mOK = cart::drawButton(ren, ct.x + ct.w - 74, ct.y + ct.h - 20, 70, 18, "OK", true);
                mCancel = {};   // no cancel on a message box
            }
        } else if (modal != M_NONE) {
            // N-field dialog; height fits the field count.
            int ph = modal==M_OPENPATH?420:modal==M_SCENARIO?300:70+mN*40;
            SDL_Rect ct = cart::drawPanel(ren,w,h,(modal==M_OPENPATH || modal==M_SCENARIO)?560:320,ph,mTitle);
            if(modal==M_OPENPATH) {
                openList={ct.x,ct.y+40,ct.w,ct.h-70};filterOpenMaps();
                SDL_RenderSetClipRect(ren,&openList);
                for(int i=0;i<int(openMatches.size());++i) {
                    const int y=openList.y+i*20-openScroll;if(y+20<openList.y || y>openList.y+openList.h)continue;
                    fillRect(ren,openList.x,y,openList.w,19,25,30,40);
                    cart::drawText(ren,openMatches[i].first,openList.x+4,y+6,1,215,225,240);
                }
                SDL_RenderSetClipRect(ren,nullptr);
            }
            if (modal == M_UNIT && editUnit >= 0 && editUnit < int(units.size()))
                cart::drawText(ren, units[size_t(editUnit)].type, ct.x, ct.y - 16, 1, 200, 200, 200);
            for (int i = 0; i < mN; ++i) {
                if(modal==M_SCENARIO && i==1)
                    mBox[i]=cart::drawTextArea(ren,ct.x,ct.y+40,ct.w,160,"DESCRIPTION (SHIFT+ENTER: NEW LINE)",mf[i],mfocus==i,fieldEditor,descriptionScroll,descriptionFollowCaret);
                else if (mfChoices[i])
                    mBox[i] = cart::drawChoice(ren, ct.x, ct.y + i * 40, ct.w, mLabel[i],
                                               mf[i], mDropOpen == i);
                else
                    mBox[i] = cart::drawField(ren, ct.x, ct.y + i * 40, ct.w, mLabel[i],
                                              mf[i], mfocus == i,mfocus==i?&fieldEditor:nullptr);
            }
            // New Map gets a RANDOM shortcut (procedural terrain, jump straight in).
            if (modal == M_NEW) {
                mOK = cart::drawButton(ren, ct.x, ct.y + ct.h - 20, 74, 18, "CREATE", true);
                mRandom = cart::drawButton(ren, ct.x + 80, ct.y + ct.h - 20, 74, 18, "RANDOM", false);
            } else {
                mOK = cart::drawButton(ren, ct.x + ct.w - 150, ct.y + ct.h - 20, 70, 18, modal==M_GENERATOR?"GENERATE":"OK", true);
                mRandom = {};
            }
            mCancel = cart::drawButton(ren, ct.x + ct.w - 74, ct.y + ct.h - 20, 70, 18,
                                       "CANCEL", false);
            // Open dropdown list, drawn LAST so it overlays the fields below it.
            mDropRects.clear();
            if (mDropOpen >= 0 && mDropOpen < mN && mfChoices[mDropOpen]) {
                const auto& opts = *mfChoices[mDropOpen];
                const SDL_Rect& anchor = mBox[mDropOpen];
                constexpr int kRowH = 14;
                const int count=std::min(8,int(opts.size()));
                mDropScroll=std::clamp(mDropScroll,0,std::max(0,int(opts.size())-count));
                SDL_Rect list{anchor.x, std::min(anchor.y+anchor.h,h-count*kRowH-6), anchor.w,
                              kRowH * count + 2};
                SDL_SetRenderDrawColor(ren, 28, 30, 40, 255); SDL_RenderFillRect(ren, &list);
                SDL_SetRenderDrawColor(ren, 150, 200, 120, 255); SDL_RenderDrawRect(ren, &list);
                for (size_t i = size_t(mDropScroll); i < size_t(mDropScroll+count); ++i) {
                    SDL_Rect r{list.x + 1, list.y + 1 + (int(i)-mDropScroll) * kRowH, list.w - 2, kRowH};
                    if (opts[i] == mf[mDropOpen]) {
                        SDL_SetRenderDrawColor(ren, 60, 80, 50, 255); SDL_RenderFillRect(ren, &r);
                    }
                    cart::drawText(ren, opts[i], r.x + 4, r.y + 3, 1, 225, 230, 240);
                    mDropRects.push_back(r);
                }
            }
        }

        // Use Only checklist overlay: scrollable unit-type list, click to toggle.
        if (useOnlyOpen) {
            SDL_Rect ct = cart::drawPanel(ren, w, h, 320, 460, "USE ONLY UNITS");
            std::string hdr = useOnly.empty()
                ? "ALL UNITS ALLOWED (empty = no restriction)"
                : std::to_string(useOnly.size()) + " ALLOWED  (click to toggle)";
            cart::drawText(ren, hdr, ct.x, ct.y - 16, 1, 180, 205, 185);
            int listH = ct.h - 30;
            uoList = {ct.x, ct.y, ct.w, listH};
            SDL_RenderSetClipRect(ren, &uoList);
            for (int i = 0; i < int(unitTypes.size()); ++i) {
                int ry = ct.y + i * 14 - useOnlyScroll;
                if (ry + 12 < ct.y || ry > ct.y + listH) continue;   // cull
                bool on = useOnly.count(unitTypes[size_t(i)]) > 0;
                cart::drawText(ren, (on ? "[X] " : "[ ] ") + unitTypes[size_t(i)],
                               ct.x + 2, ry, 1, on ? 235 : 128, on ? 235 : 130, on ? 180 : 138);
            }
            SDL_RenderSetClipRect(ren, nullptr);
            uoClear = cart::drawButton(ren, ct.x, ct.y + ct.h - 20, 100, 18, "UNRESTRICT", false);
            uoDone = cart::drawButton(ren, ct.x + ct.w - 74, ct.y + ct.h - 20, 70, 18, "DONE", true);
        }

        if(frameHook)frameHook(win,ren,frameNumber++);
        SDL_RenderPresent(ren);
        if (!shotPath.empty()) {
            int ow, oh; SDL_GetRendererOutputSize(ren, &ow, &oh);
            std::vector<uint8_t> px(size_t(ow) * oh * 4);
            if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                                     px.data(), ow * 4) == 0) {
                tak::png::write(shotPath, ow, oh, px);
                std::fprintf(stderr, "shot %s (%dx%d)\n", shotPath.c_str(), ow, oh);
            }
            running = false;
        }
    }

    if(interactive) {clearRecovery();persistPreferences();}
    for (auto& [k, t] : thumbs) if (t) SDL_DestroyTexture(t);
    for (auto& [k, t] : featTex) if (t) SDL_DestroyTexture(t);
    return 0;
}

#ifndef TAK_CARTOGRAPHER_TEST
int main(int argc,char** argv) {return cart::runEditor(argc,argv);}
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int) {return main(__argc,__argv);}
#endif
#endif
