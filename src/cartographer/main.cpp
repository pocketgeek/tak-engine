#include "util/winargv.h"
// Cartographer -- a clean-room re-implementation of the retail TA:Kingdoms map
// editor (see docs/cartographer-port.md). Static-analysis RE of the shipped
// Cartographer.exe drives the behaviour; this shares the engine's rendering
// stack (hpi VFS, tnt loader, terrain compositor, MapView) so terrain looks
// byte-identical to the game.
//
// Document serialization and undo live in document/history; selection operations
// live in selection.h. This file starts the editor (Session: command line, data
// root, window, document load) and runs the frame loop; editorstate.h maps the
// other concerns to their files.
// See docs/cartographer-improvements-progress.md for completed and pending work.

#include "net/netcompat.h"
#include <SDL.h>

#include "cartographer/editorstate.h"
#include "cartographer/dialog.h"
#include "cartographer/document.h"
#include "cartographer/generator.h"
#include "client/settings.h"
#include "client/dirpicker.h"
#include "util/appicon.h"
#include "util/virtualpath.h"
#include "terrain/terrain.h"

#include <cstdio>
#include <cstdlib>
#include <chrono>

float cart::kUIScale = 1.5f;

cart::FreshMap cart::buildFreshMap(const tak::hpi::Vfs& vfs, cart::SectionLibrary& sections,
                       tak::terrain::Compositor& comp, const std::string& world,
                       int wUnits, int hUnits, bool random, const tak::mapgen::Params* requested) {
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

// Preferences/recovery folder. TAK_CARTOGRAPHER_PROFILE (tests) overrides it because SDL_GetPrefPath
// ignores XDG_* off Linux: Windows/macOS test runs would otherwise read and write the user's real profile.
static std::filesystem::path profileFolder() {
    if(const char* o=SDL_getenv("TAK_CARTOGRAPHER_PROFILE")) if(*o) return std::filesystem::u8path(o);
    std::filesystem::path out;
    if(char* path=SDL_GetPrefPath("TAKengine","Cartographer")) {out=std::filesystem::u8path(path);SDL_free(path);}
    return out;
}

cart::Session::Session(int argc,char** argv,const FrameHook& frameHook,const EditorHooks& hooks)
    : frameHook(frameHook), hooks(hooks) {
    parseArguments(argc,argv);
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
    if(dataRoot.empty() && !SDL_getenv("TAK_CARTOGRAPHER_PROFILE")) {   // (a test profile ignores the game's saved data dir)
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
        throw Exit{2};
    }

    if(!explicitOutput) outDir=pathText(std::filesystem::u8path(dataRoot)/"Maps");

    SDL_SetMainReady();   // we own main() (SDL_MAIN_HANDLED); tell SDL not to hijack it
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        throw Exit{1};
    }
    sdlLifetime.active=true;
    preferencesFolder=profileFolder();
    preferences=cart::loadEditorPreferences(preferencesFolder);
    SDL_Rect display{0,0,1600,1000};SDL_GetDisplayUsableBounds(0,&display);
    const int initialWidth=std::min(preferences.width?preferences.width:1600,std::max(800,int(display.w*.9f)));
    const int initialHeight=std::min(preferences.height?preferences.height:1000,std::max(600,int(display.h*.9f)));
    win = SDL_CreateWindow(
        ("Cartographer -- " + mapName).c_str(), SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, initialWidth, initialHeight, SDL_WINDOW_RESIZABLE);
    windowOwner.reset(win);
    {   // Application icon: the compass-rose badge (src/util/appicon).
        std::vector<uint8_t> ic = tak::appicon::render(tak::appicon::Kind::Cartographer, 64);
        if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
                ic.data(), 64, 64, 32, 64 * 4, SDL_PIXELFORMAT_RGBA32)) {
            SDL_SetWindowIcon(win, s);
            SDL_FreeSurface(s);
        }
    }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    rendererOwner.reset(ren);
    // Draw everything at kUIScale: the logical canvas stays 1280x800-ish while
    // the window is that many times larger, so the UI is magnified uniformly.
    if (ren) SDL_RenderSetScale(ren, float(kUIScale), float(kUIScale));
    if (!win || !ren) {
        std::fprintf(stderr, "window/renderer: %s\n", SDL_GetError());
        throw Exit{1};
    }

    editorFont.emplace(ren);

    try {
        vfs = tak::hpi::mountRetailRoot(std::filesystem::u8path(dataRoot), tak::hpi::OverridePolicy::Full);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "mount %s: %s\n", dataRoot.c_str(), e.what());
        throw Exit{1};
    }
    interactive=shotPath.empty() && exportPath.empty() && bundlePath.empty() && stampName.empty();
    recoveryFolder=profileFolder();
    if(interactive && !recoveryFolder.empty()) {
        try {
            recoverySession=cart::RecoveryFile::create(recoveryFolder);
            if(mapName.empty() && !newW)recoveredSession=cart::RecoveryFile::claimNewest(recoveryFolder);
        } catch(const std::exception& e) {recoveryStatus=std::string("Recovery unavailable: ")+e.what();}
        if(recoveredSession) {
            const SDL_MessageBoxButtonData buttons[]={{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Recover"},{0,2,"Discard recovery"},{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Cancel"}};
            SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION,win,"Recover map","An unsaved map recovery is available.",3,buttons,nullptr};
            int choice=0;if(hooks.recoveryChoice)choice=hooks.recoveryChoice();else SDL_ShowMessageBox(&box,&choice);
            if(choice==0)throw Exit{0};
            if(choice==2) {
                std::string error;
                if(!recoveredSession->discard(error)) {SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Recovery cleanup failed",error.c_str(),win);throw Exit{1};}
                recoveredSession.reset();
            }
            if(choice==1) {
                try {
                    auto snapshot=cart::readRecoverySnapshot(recoveredSession->path());
                    recoveredMapPath=snapshot.mapPath;mapName=tak::vpath::stem(snapshot.mapPath);
                    if(snapshot.destination) {mapName=snapshot.destination->name;if(!explicitOutput)outDir=snapshot.destination->directory;}
                    if(snapshot.fromBackup)recoveryStatus="Recovered previous snapshot; newest recovery was unreadable";
                    vfs.setMapFiles(std::move(snapshot.files));recoveredFrom=recoveredSession->path();
                } catch(const std::exception& e) {SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Recovery failed",e.what(),win);throw Exit{1};}
            }
        }
    }

    // Resolve the map name to its .tnt VFS path (Maps/<name>.tnt or a .kmp's
    // kmap/<name>.tnt), same resolution the game uses. An empty name (or --new
    // without --save) starts a fresh blank map instead -- mapPath stays empty so
    // there are no sibling scenario files to load.
    showWelcome=interactive && mapName.empty() && !newW && !preferences.guideSeen;
    mapPath=recoveredMapPath;
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
            throw Exit{1};
        }
        mapViewPtr = std::make_unique<MapView>(ren, vfs, mapPath);
        // Companion .ota (metadata + start positions), so a Save round-trips the
        // whole map. Missing/parse-fail = defaults.
        std::string otaPath = mapPath.substr(0, mapPath.rfind('.')) + ".ota";
        try {
            auto b = vfs.read(otaPath);
            scenario = tak::tnt::Scenario::parse(std::string(b.begin(), b.end()));
        } catch (const std::exception&) { /* no .ota: keep defaults */ }
        scenario.generatorRecipe=cart::mapGeneratorRecipe(vfs,mapPath,scenario.generatorRecipe);
    } else {
        int fw = newW > 0 ? newW : 8, fh = newH > 0 ? newH : 8;
        cart::SectionLibrary ns; ns.scan(vfs, newWorld);
        tak::terrain::Compositor nc(vfs);
        FreshMap fm = buildFreshMap(vfs, ns, nc, newWorld, fw, fh, randomTerrain);
        if (fm.map.width == 0) {
            std::fprintf(stderr, "new: could not build terrain for world '%s'\n", newWorld.c_str());
            throw Exit{1};
        }
        mapName = "Untitled";
        scenario.kingdom = newWorld; scenario.sizeW = fw; scenario.sizeH = fh;
        scenario.missionName = mapName;
        scenario.generatorRecipe=fm.recipe;
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

    std::string crtPath = mapPath.substr(0, mapPath.rfind('.')) + ".crt";
    try {scen=cart::loadScenario(vfs,crtPath);}
    catch(const std::exception& error) {
        std::fprintf(stderr,"open scenario: %s\n",error.what());
        if(interactive)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Cannot open map",error.what(),win);
        throw Exit{1};
    }
    units = cart::toPlaced(scen);

    std::string useOnlyTdf = mapPath.substr(0, mapPath.rfind('.')) + ".tdf";
    if (!scenario.useOnlyUnits.empty())
        for (auto& t : cart::loadUseOnly(vfs, useOnlyTdf)) useOnly.insert(t);
    dirty = mapPath.empty() || !recoveredFrom.empty();
    history.reset(historySnapshot(),!dirty);

    world = scenario.kingdom.empty() ? "aramon" : scenario.kingdom;
    sections.scan(vfs, world);
    features.scan(vfs, world);
    std::fprintf(stderr, "cartographer: %zu placeable features for '%s'\n",
                 features.list().size(), world.c_str());

    // Unit-type list for the palette (units + scen loaded above, before saveMap).
    unitTypes = cart::unitTypeNames(vfs);
    tak::sim::setupRegistry(unitRegistry,vfs,false);
    units=cart::toPlaced(scen,&unitRegistry);
    std::fprintf(stderr, "cartographer: %zu placed units, %zu unit types\n",
                 units.size(), unitTypes.size());
    std::fprintf(stderr, "cartographer: %zu sections for world '%s'\n",
                 sections.list().size(), world.c_str());

    if(const auto code=runHeadless())throw Exit{*code};
}

cart::Editor::Editor(int argc,char** argv,const FrameHook& frameHook,const EditorHooks& hooks)
    : Session(argc,argv,frameHook,hooks) {
    for(uint8_t i=0;i<tak::mapgen::kLayouts;++i)generatorLayouts.push_back(tak::mapgen::layoutName(i));
    if(showWelcome) {
        openMessage("WELCOME TO CARTOGRAPHER",
            "Start with File > Open (Ctrl+O), or New (Ctrl+N). Pick a tool and search the left browser (Ctrl+F).\n"
            "Place adds objects; Select moves or edits them. Right-drag pans in any mode; the mouse wheel zooms. Click the minimap to travel.\n"
            "Ctrl+Z undoes edits. Ctrl+S saves a playable map bundle. Unsaved work gets recovery copies every minute.\n"
            "Before playing, use Scenario > Check map (C). Hover over toolbar controls for help; Help > Editor controls lists shortcuts.\n"
            "For fair multiplayer maps, the Symmetry menu mirrors your edits live (M) or copies one half onto the other (Shift+M).");
        preferences.guideSeen=true;persistPreferences();
    }
    nextRecovery=recoveryClock()+60000;
    prepareShot();
    // A bare launch (no map named, no --new size) opens on the blank map with
    // the New Map dialog already up, so the first thing is "what shall we make?".
    if (mapPath.empty() && newW == 0 && shotPath.empty()) openModal(M_NEW);
}

int cart::Editor::run() {
    int frameNumber=0;
    while (running) {
        if(const auto result=playtest.poll();result && *result!=0)
            openMessage("TEST MAP ENDED", "The game exited with an error ("+std::to_string(*result)+"). Your map is still open here.");
        SDL_GetRendererOutputSize(ren, &w, &h);
        kUIScale=std::max(.5f,std::min({preferences.scalePercent/100.f,float(w)/900.0f,float(h)/600.0f}));
        SDL_RenderSetScale(ren,kUIScale,kUIScale);
        w /= kUIScale; h /= kUIScale;   // physical -> logical (SDL_RenderSetScale)
        canvasH = h - kMenuH - kStatusH;
        canvasW = w - kPaletteW;
        const float miniScale=140.0f/std::max(mapView.map().width,mapView.map().height);
        miniRect={w-148,kMenuH+8,std::max(1,int(mapView.map().width*miniScale)),std::max(1,int(mapView.map().height*miniScale))};

        refreshPalette();
        pollJobs();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if(swallowWhileBusy(e))continue;
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
            if(handleRegionDrag(e))continue;
            if(handleGeneratedPreview(e))continue;
            if (e.type == SDL_QUIT) {
                if (dirty) {
                    // Close any transient overlay so the quit prompt gets input.
                    useOnlyOpen = false; scriptOpen = false; pickOpen = false;regionsOpen=false;
                    modal = M_QUITSAVE; SDL_StopTextInput();
                } else running = false;
                continue;
            }
            if(modal==M_NONE && !scriptOpen && !useOnlyOpen && !regionsOpen && handleMenuInput(e))continue;
            if(modal==M_NONE && !scriptOpen && !useOnlyOpen && !regionsOpen && (handleMinimapInput(e) || handlePaletteFocus(e)))continue;
            if(regionsOpen && modal==M_NONE) {handleRegionsList(e);continue;}
            if (useOnlyOpen) {handleUseOnly(e);continue;}
            if (modal != M_NONE) {handleModal(e);continue;}
            if (scriptOpen) {handleScripting(e);continue;}
            // Toolbar, canvas keys, global shortcuts, then palette and canvas
            // pointer input; the first handler that takes the event wins.
            if(handleToolbarModes(e) || handleCanvasKeys(e) || handleShortcuts(e) ||
               handleToolbarTools(e) || handlePaletteClick(e) || handlePaletteWheel(e))continue;
            handleCanvasInput(e,pointerDX,pointerDZ);
        }

        // A confirmed New (discarding unsaved edits) opens the dialog now, after
        // the confirm's applyModal has closed itself.
        if (wantNew) { wantNew = false; openModal(M_NEW); }

        if(overlayInvalidated || historyPending || resetHistory) {overlayTexture.reset();overlayLegend.clear();overlayInvalidated=false;}
        if((historyPending || resetHistory) && !(SDL_GetMouseState(nullptr,nullptr)&(SDL_BUTTON_LMASK|SDL_BUTTON_RMASK)))
            commitHistory();
        const std::string title="Cartographer -- "+mapName+(modal==M_OBJECTS?" [Placed units]":modal==M_MODEL_LOADING?" [Loading model]":modal==M_MODEL?" [Unit model]":modal==M_GENERATING?" [Generating]":modal==M_GENERATED?" [Generated preview]":modal==M_CHECKING?" [Checking map]":modal==M_ISSUES?" [Validation results]":modal==M_ANALYZING?" [Analyzing]":overlayTexture?" [Terrain overlay]":"")+(dirty?" *":"");
        SDL_SetWindowTitle(win,title.c_str());
        autosaveRecovery();
        refreshMinimap();updateLiveMinimap();
        mapView.ensureChunks(canvasW, canvasH);

        SDL_SetRenderDrawColor(ren, 24, 26, 32, 255);
        SDL_RenderClear(ren);

        drawCanvas();
        drawMinimap();
        drawPalette();
        drawChrome();
        drawSectionMenu();
        drawClearArea();
        drawRegionsList();
        drawScripting();
        drawModal();
        drawUseOnly();
        drawHints();

        if(frameHook)frameHook(win,ren,frameNumber++);
        SDL_RenderPresent(ren);
        captureShot();
    }

    minimapCancel->store(true,std::memory_order_relaxed);
    if(interactive) {clearRecovery();persistPreferences();}
    thumbs.reset();unitPortraits.reset();modelPreview.reset();
    for (auto& [k, t] : featTex) if (t) SDL_DestroyTexture(t);
    return 0;
}

int cart::runEditor(int argc, char** argv, const std::function<void(SDL_Window*,SDL_Renderer*,int)>& frameHook,const EditorHooks& hooks) {
    try {
        Editor editor(argc,argv,frameHook,hooks);
        return editor.run();
    } catch(const Session::Exit& exit) {return exit.code;}
}

#ifndef TAK_CARTOGRAPHER_TEST
int main(int argc,char** argv) {return cart::runEditor(argc,argv);}
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int) {return tak::utf8Main(main);}
#endif
#endif
