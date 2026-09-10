// Cartographer -- a clean-room re-implementation of the retail TA:Kingdoms map
// editor (see docs/cartographer-port.md). Static-analysis RE of the shipped
// Cartographer.exe drives the behaviour; this shares the engine's rendering
// stack (hpi VFS, tnt loader, terrain compositor, MapView) so terrain looks
// byte-identical to the game.
//
// Phase 0 skeleton: mount a retail install, open a .tnt map, render the terrain
// with pan/zoom, and frame it in the editor chrome (menu bar + status bar).
// Painting tools, the object model, property dialogs, and the trigger system
// land in later phases.

#include <SDL.h>

#include "cartographer/dialog.h"
#include "cartographer/features.h"
#include "cartographer/font5x7.h"
#include "cartographer/newmap.h"
#include "cartographer/sections.h"
#include "cartographer/units.h"
#include "client/mapview.h"
#include "terrain/terrain.h"
#include "util/jpeg.h"
#include "hpi/hpi.h"
#include "tnt/ota.h"
#include "util/png.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>

namespace {

constexpr int kMenuH = 22;    // top menu-bar strip
constexpr int kStatusH = 22;  // bottom status strip

void fillRect(SDL_Renderer* r, int x, int y, int w, int h, Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

}  // namespace

int main(int argc, char** argv) {
    std::string dataRoot, mapName, outDir = ".", exportPath, stampName, newWorld = "aramon", shotPath;
    int stampBX = 0, stampBY = 0, newW = 0, newH = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) dataRoot = argv[++i];
        else if (a == "--out" && i + 1 < argc) outDir = argv[++i];   // Save destination
        else if (a == "--save" && i + 1 < argc) exportPath = argv[++i];  // headless export+exit
        else if (a == "--stamp" && i + 3 < argc) {   // headless: stamp <name> <bx> <by>
            stampName = argv[++i]; stampBX = std::atoi(argv[++i]); stampBY = std::atoi(argv[++i]);
        }
        else if (a == "--new" && i + 1 < argc) {   // headless: --new WxH (Units) + --save
            std::sscanf(argv[++i], "%dx%d", &newW, &newH);
        }
        else if (a == "--world" && i + 1 < argc) newWorld = argv[++i];
        else if (a == "--shot" && i + 1 < argc) shotPath = argv[++i];   // render 1 frame -> PNG
        else if (a[0] != '-') mapName = a;
    }
    // Data root: --data wins; otherwise the local directory if it holds a valid
    // install (drop the editor into a game folder and it just works).
    if (dataRoot.empty()) {
        std::error_code ec;
        std::filesystem::path here = std::filesystem::current_path(ec);
        if (!ec && tak::hpi::validInstall(here, nullptr)) {
            dataRoot = here.string();
            std::fprintf(stderr, "data: using the local directory %s\n", dataRoot.c_str());
        }
    }
    if (dataRoot.empty() || (mapName.empty() && newW <= 0)) {
        std::fprintf(stderr,
            "Cartographer (TA:Kingdoms map editor) -- phase 2\n"
            "usage: cartographer \"<map name>\" [--data <retail-install-dir>]\n"
            "         (--data is optional when run from inside a game folder)\n"
            "         [--out <dir>]        Ctrl+S save destination (default .)\n"
            "         [--save <file.tnt>]  headless: save the map and exit\n");
        return 2;
    }

    SDL_SetMainReady();   // we own main() (SDL_MAIN_HANDLED); tell SDL not to hijack it
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow(
        ("Cartographer -- " + mapName).c_str(), SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 1280, 800, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!win || !ren) {
        std::fprintf(stderr, "window/renderer: %s\n", SDL_GetError());
        return 1;
    }

    // Mount the retail install exactly like the engine (loose + *.hpi, retail
    // precedence). The Vfs must outlive the MapView (it borrows it by ref).
    tak::hpi::Vfs vfs;
    try {
        vfs = tak::hpi::mountRetailRoot(dataRoot, tak::hpi::OverridePolicy::Full);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "mount %s: %s\n", dataRoot.c_str(), e.what());
        return 1;
    }
    // --new WxH: build a fresh flat map for --world and save it (headless). The
    // in-editor New dialog arrives with the widget layer; this is the create path.
    if (newW > 0 && newH > 0) {
        cart::SectionLibrary nsections;
        nsections.scan(vfs, newWorld);
        tak::terrain::Compositor ncomp(vfs);
        tak::tnt::Map nm = cart::newBlankMap(vfs, nsections, ncomp, newWorld, newW, newH);
        if (nm.width == 0) {
            std::fprintf(stderr, "new: no sections for world '%s'\n", newWorld.c_str());
            return 1;
        }
        tak::tnt::Scenario nsc;
        nsc.kingdom = newWorld;
        nsc.missionName = mapName.empty() ? "Untitled" : mapName;
        nsc.sizeW = newW; nsc.sizeH = newH;
        std::string base = exportPath.empty()
            ? (outDir + "/" + (mapName.empty() ? "new" : mapName) + ".tnt") : exportPath;
        std::string otaP = base.substr(0, base.rfind('.')) + ".ota";
        auto put = [](const std::string& p, const void* d, size_t n) {
            std::FILE* f = std::fopen(p.c_str(), "wb");
            if (!f) return false;
            bool ok = std::fwrite(d, 1, n, f) == n; std::fclose(f);
            std::fprintf(stderr, "saved %s (%zu bytes)\n", p.c_str(), n);
            return ok;
        };
        std::vector<uint8_t> tb = nm.save();
        std::string ot = nsc.write();
        bool ok = put(base, tb.data(), tb.size()) & put(otaP, ot.data(), ot.size());
        std::fprintf(stderr, "new: %dx%d Units (%dx%d cells) world=%s\n",
                     newW, newH, nm.width, nm.height, newWorld.c_str());
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        return ok ? 0 : 1;
    }

    // Resolve the map name to its .tnt VFS path (Maps/<name>.tnt or a .kmp's
    // kmap/<name>.tnt), same resolution the game uses.
    std::string mapPath;
    for (const auto& [name, path] : tak::hpi::listMaps(vfs)) {
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

    MapView mapView(ren, vfs, mapPath);
    mapView.setBilinear(true);

    // Load the companion .ota scenario (metadata + start positions) so a Save
    // round-trips the whole map, not just terrain. Missing/parse-fail = defaults.
    tak::tnt::Scenario scenario;
    {
        std::string otaPath = mapPath.substr(0, mapPath.rfind('.')) + ".ota";
        try {
            auto b = vfs.read(otaPath);
            scenario = tak::tnt::Scenario::parse(std::string(b.begin(), b.end()));
        } catch (const std::exception&) { /* no .ota: keep defaults */ }
    }
    std::fprintf(stderr,
                 "cartographer: editing '%s' (%s), %dx%d blocks; scenario '%s' "
                 "kingdom=%s, %zu start positions\n",
                 mapName.c_str(), mapPath.c_str(), mapView.map().blocksX,
                 mapView.map().blocksY, scenario.missionName.c_str(),
                 scenario.kingdom.c_str(), scenario.starts.size());

    auto writeFile = [](const std::string& path, const void* data, size_t n) -> bool {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) { std::fprintf(stderr, "save: cannot open %s\n", path.c_str()); return false; }
        size_t w = std::fwrite(data, 1, n, f);
        std::fclose(f);
        std::fprintf(stderr, "saved %s (%zu bytes)\n", path.c_str(), w);
        return w == n;
    };
    // Set when the terrain is painted, so Save regenerates the minimaps (an
    // unedited save stays byte-identical to the source; an edited one gets a
    // fresh overview reflecting the paint).
    bool edited = false;
    // Save the map as loose <stem>.tnt + <stem>.ota. Loose Maps/<name>.* is
    // read directly by the engine VFS, so a saved map is immediately playable.
    auto saveMap = [&](const std::string& tntPath) -> bool {
        if (edited) {
            std::string wld = scenario.kingdom.empty() ? "aramon" : scenario.kingdom;
            cart::generateMinimaps(mapView.editMap(), mapView.compositor(),
                                   cart::loadWorldPalette(vfs, wld));
        }
        std::vector<uint8_t> tnt = mapView.map().save();
        bool ok = writeFile(tntPath, tnt.data(), tnt.size());
        std::string ota = tntPath.substr(0, tntPath.rfind('.')) + ".ota";
        std::string otaText = scenario.write();
        ok &= writeFile(ota, otaText.data(), otaText.size());
        return ok;
    };

    // Section-prefab palette for this map's world (falls back to aramon).
    cart::SectionLibrary sections;
    std::string world = scenario.kingdom.empty() ? "aramon" : scenario.kingdom;
    sections.scan(vfs, world);
    cart::FeatureLibrary features;
    features.scan(vfs, world);
    std::fprintf(stderr, "cartographer: %zu placeable features for '%s'\n",
                 features.list().size(), world.c_str());

    // Placed units (from the map's .crt) + the unit-type list for the palette.
    std::string crtPath = mapPath.substr(0, mapPath.rfind('.')) + ".crt";
    std::vector<cart::PlacedUnit> units = cart::loadUnits(vfs, crtPath);
    std::vector<std::string> unitTypes = cart::unitTypeNames(vfs);
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
                    mapView.tilesEdited();
                    edited = true;
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
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        return ok ? 0 : 1;
    }

    // Headless one-shot export: --save <file.tnt> writes and exits (round-trip
    // / convert path, also how the save is regression-tested).
    if (!exportPath.empty()) {
        bool ok = saveMap(exportPath);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        return ok ? 0 : 1;
    }

    // --- Interactive editor state ---------------------------------------------
    constexpr int kPaletteW = 200;   // left section-palette panel
    constexpr int kThumb = 88;       // section thumbnail cell (px)
    const int cols = std::max(1, (kPaletteW - 8) / (kThumb + 4));
    int selected = sections.list().empty() ? -1 : 0;   // section index (TERRAIN)
    int selectedFeat = features.list().empty() ? -1 : 0; // feature index (FEATURES)
    int paletteScroll = 0;
    bool showGrid = false;
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
        if (cart::stampSection(mapView.editMap(), *sec, snapX, snapY)) {
            mapView.tilesEdited();
            edited = true;
        }
        (void)w; (void)h;
    };

    // --- Object tools (terrain paint, feature placement, start positions) -----
    enum Tool { TERRAIN, FEATURES, UNITS, STARTS };
    int selectedType = unitTypes.empty() ? -1 : 0;   // unit-type index (UNITS palette)
    int currentPlayer = 0;                            // player id new units get
    int draggingUnit = -1;                            // index into units while dragging
    // 8 player marker colours.
    static const Uint8 kPlayerCol[8][3] = {
        {80,120,255},{230,70,60},{80,200,90},{235,210,80},
        {200,90,220},{80,210,220},{240,140,40},{200,200,210}};
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
    // Place/erase a feature in the .tnt feature plane at the mouse cell.
    auto placeFeature = [&](int mx, int my, bool erase) {
        if (selectedFeat < 0 || mx < kPaletteW) return;
        float wx = mapView.offX() + float(mx - kPaletteW) / mapView.zoom();
        float wz = mapView.offY() + float(my - kMenuH) / mapView.zoom();
        int cx = int(wx / 16.0f), cz = int(wz / 16.0f);
        auto& mp = mapView.editMap();
        if (cx < 0 || cz < 0 || cx >= mp.width || cz >= mp.height) return;
        size_t ci = size_t(cz) * mp.width + cx;
        if (erase) { mp.features[ci] = 0xFFFF; edited = true; return; }
        const std::string& name = features.list()[size_t(selectedFeat)].name;
        uint16_t idx = 0xFFFF;   // intern the feature name into the map's table
        for (size_t i = 0; i < mp.featureNames.size(); ++i)
            if (mp.featureNames[i] == name) { idx = uint16_t(i); break; }
        if (idx == 0xFFFF) { mp.featureNames.push_back(name); idx = uint16_t(mp.featureNames.size() - 1); }
        mp.features[ci] = idx;
        edited = true;
    };
    int draggingStart = -1;           // index into scenario.starts while dragging
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

    // --- Modal dialogs (Scenario Properties, Resize) --------------------------
    enum Modal { M_NONE, M_SCENARIO, M_RESIZE };
    Modal modal = M_NONE;
    std::string mf[2];              // field buffers
    bool mfNumeric[2] = {false, false};
    int mfocus = 0;
    SDL_Rect mBox[2]{}, mOK{}, mCancel{};   // render-computed hit rects
    auto openModal = [&](Modal m) {
        modal = m; mfocus = 0;
        if (m == M_SCENARIO) {
            mf[0] = scenario.missionName; mf[1] = scenario.missionDescription;
            mfNumeric[0] = mfNumeric[1] = false;
        } else if (m == M_RESIZE) {
            mf[0] = std::to_string(mapView.map().width / 32);
            mf[1] = std::to_string(mapView.map().height / 32);
            mfNumeric[0] = mfNumeric[1] = true;
        }
        SDL_StartTextInput();
    };
    auto applyModal = [&]() {
        if (modal == M_SCENARIO) {
            scenario.missionName = mf[0];
            scenario.missionDescription = mf[1];
        } else if (modal == M_RESIZE) {
            int wu = std::max(1, std::atoi(mf[0].c_str()));
            int hu = std::max(1, std::atoi(mf[1].c_str()));
            std::string wld = scenario.kingdom.empty() ? "aramon" : scenario.kingdom;
            cart::resizeMap(mapView.editMap(), mapView.compositor(),
                            cart::loadWorldPalette(vfs, wld), wu, hu);
            scenario.sizeW = wu; scenario.sizeH = hu;
            mapView.tilesEdited();
            edited = true;
        }
        modal = M_NONE; SDL_StopTextInput();
    };

    if (!shotPath.empty()) {
        mapView.setZoom(0.3f);          // fit-ish view for the shot
        mapView.finishChunks();         // wait for the terrain to decode+upload
    }
    bool running = true;
    while (running) {
        int w, h;
        SDL_GetRendererOutputSize(ren, &w, &h);
        int canvasH = h - kMenuH - kStatusH;
        int canvasW = w - kPaletteW;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; continue; }
            // A modal dialog swallows all input while up.
            if (modal != M_NONE) {
                if (e.type == SDL_TEXTINPUT) {
                    for (const char* c = e.text.text; *c; ++c)
                        if (!mfNumeric[mfocus] || (*c >= '0' && *c <= '9')) mf[mfocus] += *c;
                } else if (e.type == SDL_KEYDOWN) {
                    SDL_Keycode k = e.key.keysym.sym;
                    if (k == SDLK_BACKSPACE && !mf[mfocus].empty()) mf[mfocus].pop_back();
                    else if (k == SDLK_TAB) mfocus = (mfocus + 1) % 2;
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) applyModal();
                    else if (k == SDLK_ESCAPE) { modal = M_NONE; SDL_StopTextInput(); }
                } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                           e.button.button == SDL_BUTTON_LEFT) {
                    int mx = e.button.x, my = e.button.y;
                    if (cart::pointIn(mx, my, mBox[0])) mfocus = 0;
                    else if (cart::pointIn(mx, my, mBox[1])) mfocus = 1;
                    else if (cart::pointIn(mx, my, mOK)) applyModal();
                    else if (cart::pointIn(mx, my, mCancel)) { modal = M_NONE; SDL_StopTextInput(); }
                }
                continue;
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                running = false;
            else if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & KMOD_CTRL) &&
                     e.key.keysym.sym == SDLK_s) {
                bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                saveMap(outDir + "/" + mapName + (shift ? "-edit" : "") + ".tnt");
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_p) {
                openModal(M_SCENARIO);   // Scenario -> Properties
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_r) {
                openModal(M_RESIZE);     // Scenario -> Resize
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
                       e.button.button == SDL_BUTTON_LEFT && e.button.y < kMenuH) {
                // Toolbar buttons: TERRAIN | FEATURES | STARTS (each 72px).
                int bi = (e.button.x - 96) / 72;
                if (bi >= 0 && bi < 4) tool = Tool(bi);
            } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                       e.button.button == SDL_BUTTON_LEFT && e.button.x < kPaletteW &&
                       e.button.y >= kMenuH && e.button.y < h - kStatusH) {
                // Palette click -> select a section/feature (thumbnails) or a unit
                // type (UNITS = a text list, 14px rows).
                if (tool == UNITS) {
                    int row = (e.button.y - kMenuH + paletteScroll) / 14;
                    if (row >= 0 && row < int(unitTypes.size())) selectedType = row;
                } else {
                    int px = e.button.x - 4;
                    int py = e.button.y - kMenuH + paletteScroll;
                    int col = px / (kThumb + 4), row = py / (kThumb + 4);
                    if (col >= 0 && col < cols) {
                        int idx = row * cols + col;
                        if (tool == FEATURES) {
                            if (idx >= 0 && idx < int(features.list().size())) selectedFeat = idx;
                        } else if (idx >= 0 && idx < int(sections.list().size())) selected = idx;
                    }
                }
            } else if (e.type == SDL_MOUSEWHEEL) {
                int mxp, myp; SDL_GetMouseState(&mxp, &myp);
                if (mxp < kPaletteW) {   // scroll the palette (tool-dependent count)
                    if (tool == UNITS) {
                        int maxS = std::max(0, int(unitTypes.size()) * 14 - canvasH);
                        paletteScroll = std::clamp(paletteScroll - e.wheel.y * 40, 0, maxS);
                        continue;
                    }
                    int n = tool == FEATURES ? int(features.list().size())
                                             : int(sections.list().size());
                    int rows = (n + cols - 1) / cols;
                    int maxScroll = std::max(0, rows * (kThumb + 4) - canvasH);
                    paletteScroll = std::clamp(paletteScroll - e.wheel.y * 40, 0, maxScroll);
                } else {
                    mapView.input(e);   // zoom the canvas
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                       e.button.button == SDL_BUTTON_LEFT && e.button.x >= kPaletteW) {
                if (tool == TERRAIN) {
                    stampAtMouse(e.button.x, e.button.y, canvasW, canvasH);
                } else if (tool == FEATURES) {
                    placeFeature(e.button.x, e.button.y, false);
                } else if (tool == UNITS) {   // grab an existing unit, else place one
                    int hit = unitAt(e.button.x, e.button.y);
                    int cx, cz;
                    if (hit >= 0) draggingUnit = hit;
                    else if (selectedType >= 0 && mouseCell(e.button.x, e.button.y, cx, cz)) {
                        cart::PlacedUnit u;
                        u.type = unitTypes[size_t(selectedType)];
                        u.player = currentPlayer;
                        u.x = cx * 16.0f + 8; u.z = cz * 16.0f + 8;
                        units.push_back(u);
                        draggingUnit = int(units.size()) - 1;
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
                        scenario.starts.push_back({num - 1, cx, cz});
                        draggingStart = int(scenario.starts.size()) - 1;
                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                       e.button.button == SDL_BUTTON_RIGHT && tool == STARTS) {
                int hit = startAt(e.button.x, e.button.y);   // right-click deletes a start
                if (hit >= 0) scenario.starts.erase(scenario.starts.begin() + hit);
            } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                       e.button.button == SDL_BUTTON_RIGHT && tool == FEATURES &&
                       e.button.x >= kPaletteW) {
                placeFeature(e.button.x, e.button.y, true);   // right-click erases
            } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                       e.button.button == SDL_BUTTON_RIGHT && tool == UNITS) {
                int hit = unitAt(e.button.x, e.button.y);   // right-click deletes a unit
                if (hit >= 0) units.erase(units.begin() + hit);
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                draggingStart = -1; draggingUnit = -1;
            } else if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK) &&
                       e.motion.x >= kPaletteW) {
                if (tool == TERRAIN) {
                    stampAtMouse(e.motion.x, e.motion.y, canvasW, canvasH);   // drag-paint
                } else if (tool == FEATURES) {
                    placeFeature(e.motion.x, e.motion.y, false);   // drag-place features
                } else if (tool == UNITS && draggingUnit >= 0) {
                    int cx, cz;   // drag a unit to a new cell centre
                    if (mouseCell(e.motion.x, e.motion.y, cx, cz)) {
                        units[size_t(draggingUnit)].x = cx * 16.0f + 8;
                        units[size_t(draggingUnit)].z = cz * 16.0f + 8;
                    }
                } else if (draggingStart >= 0) {
                    int cx, cz;   // drag a start marker to a new cell
                    if (mouseCell(e.motion.x, e.motion.y, cx, cz)) {
                        scenario.starts[size_t(draggingStart)].xpos = cx;
                        scenario.starts[size_t(draggingStart)].zpos = cz;
                    }
                }
            } else if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_RMASK)) {
                if (tool == FEATURES && e.motion.x >= kPaletteW) {
                    placeFeature(e.motion.x, e.motion.y, true);   // right-drag erases
                } else {
                    // Right-drag pans; feed MapView a synthetic left-drag motion.
                    SDL_Event pan = e;
                    pan.motion.state = SDL_BUTTON_LMASK;
                    mapView.input(pan);
                }
            } else if (e.type != SDL_MOUSEBUTTONDOWN && e.type != SDL_MOUSEMOTION) {
                mapView.input(e);
            }
        }

        mapView.ensureChunks(canvasW, canvasH);

        SDL_SetRenderDrawColor(ren, 24, 26, 32, 255);
        SDL_RenderClear(ren);

        // Map canvas (right of the palette, between menu and status).
        SDL_Rect canvas{kPaletteW, kMenuH, canvasW, canvasH};
        SDL_RenderSetViewport(ren, &canvas);
        mapView.draw(canvasW, canvasH);
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
        // Placed features: draw each non-empty feature-plane cell's sprite at its
        // cell, anchored like the game. Culled to the visible canvas.
        {
            const auto& mp = mapView.map();
            float zm = mapView.zoom();
            for (int cz = 0; cz < mp.height; ++cz)
                for (int cx = 0; cx < mp.width; ++cx) {
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
            const Uint8* pc = kPlayerCol[units[i].player & 7];
            SDL_SetRenderDrawColor(ren, pc[0], pc[1], pc[2], 255);
            SDL_Rect r{int(sx) - 5, int(sy) - 5, 10, 10};
            SDL_RenderFillRect(ren, &r);
            SDL_SetRenderDrawColor(ren, 12, 12, 16, 255);
            SDL_RenderDrawRect(ren, &r);
            if (mapView.zoom() > 0.28f)   // label only when there's room
                cart::drawText(ren, units[i].type,
                               int(sx) - cart::textWidth(units[i].type, 1) / 2,
                               int(sy) - 15, 1, 235, 235, 245);
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
        SDL_RenderSetViewport(ren, nullptr);

        // Palette panel (left).
        fillRect(ren, 0, kMenuH, kPaletteW, canvasH, 30, 32, 40);
        SDL_Rect palClip{0, kMenuH, kPaletteW, canvasH};
        SDL_RenderSetClipRect(ren, &palClip);
        if (tool == UNITS) {   // a scrollable text list of unit types
            for (int i = 0; i < int(unitTypes.size()); ++i) {
                int ty = kMenuH + 2 + i * 14 - paletteScroll;
                if (ty + 12 < kMenuH || ty > h - kStatusH) continue;
                if (i == selectedType)
                    fillRect(ren, 0, ty - 1, kPaletteW, 13, 70, 66, 40);
                cart::drawText(ren, unitTypes[size_t(i)], 6, ty + 2, 1,
                               i == selectedType ? 255 : 200, i == selectedType ? 220 : 205,
                               i == selectedType ? 120 : 215);
            }
        } else {
        int palN = tool == FEATURES ? int(features.list().size())
                                    : int(sections.list().size());
        int palSel = tool == FEATURES ? selectedFeat : selected;
        for (int i = 0; i < palN; ++i) {
            int col = i % cols, row = i / cols;
            int cx = 4 + col * (kThumb + 4);
            int cy = kMenuH + 4 + row * (kThumb + 4) - paletteScroll;
            if (cy + kThumb < kMenuH || cy > h - kStatusH) continue;   // cull
            SDL_Rect cell{cx, cy, kThumb, kThumb};
            SDL_Texture* t = tool == FEATURES ? featTextureFor(features.list()[size_t(i)])
                                              : thumbFor(sections.list()[size_t(i)].path);
            fillRect(ren, cx, cy, kThumb, kThumb, 44, 46, 54);   // cell backing
            if (t) {
                if (tool == FEATURES) {   // feature sprite: fit-centre, keep aspect
                    const cart::FeatSprite* sp = features.sprite(vfs, features.list()[size_t(i)]);
                    float sc = std::min(float(kThumb) / std::max(sp->w, 1),
                                        float(kThumb) / std::max(sp->h, 1));
                    int dw = int(sp->w * sc), dh = int(sp->h * sc);
                    SDL_Rect fc{cx + (kThumb - dw) / 2, cy + (kThumb - dh) / 2, dw, dh};
                    SDL_RenderCopy(ren, t, nullptr, &fc);
                } else SDL_RenderCopy(ren, t, nullptr, &cell);
            }
            if (i == palSel) {   // selection highlight
                SDL_SetRenderDrawColor(ren, 255, 210, 90, 255);
                SDL_Rect b{cx - 1, cy - 1, kThumb + 2, kThumb + 2};
                SDL_RenderDrawRect(ren, &b);
                SDL_Rect b2{cx - 2, cy - 2, kThumb + 4, kThumb + 4};
                SDL_RenderDrawRect(ren, &b2);
            }
        }
        }   // end else (thumbnail palette)
        SDL_RenderSetClipRect(ren, nullptr);

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
            fillRect(ren, bx, 3, 68, kMenuH - 6, active ? 90 : 60, active ? 80 : 62,
                     active ? 40 : 74);
            cart::drawText(ren, names[t], bx + 8, 7, 1, active ? 255 : 190,
                           active ? 220 : 190, active ? 120 : 200);
        }

        // Status bar: cursor cell, tool, zoom, start count.
        int mxg, myg; SDL_GetMouseState(&mxg, &myg);
        int ccx, ccz;
        std::string coord = mouseCell(mxg, myg, ccx, ccz)
            ? "( " + std::to_string(ccx) + ", " + std::to_string(ccz) + " )" : "";
        char zbuf[16];
        std::snprintf(zbuf, sizeof zbuf, "%d%%", int(mapView.zoom() * 100 + 0.5f));
        std::string status = coord + "   TOOL: " + names[int(tool)] + "   ZOOM: " + zbuf;
        if (tool == UNITS)
            status += "   PLAYER: " + std::to_string(currentPlayer) +
                      "   UNITS: " + std::to_string(units.size());
        else
            status += "   STARTS: " + std::to_string(scenario.starts.size());
        cart::drawText(ren, status, 6, h - kStatusH + 7, 1, 200, 205, 215);

        // Modal dialog over everything.
        if (modal != M_NONE) {
            const char* title = modal == M_SCENARIO ? "SCENARIO PROPERTIES" : "RESIZE MAP";
            const char* l0 = modal == M_SCENARIO ? "SCENARIO NAME" : "WIDTH (UNITS)";
            const char* l1 = modal == M_SCENARIO ? "DESCRIPTION" : "HEIGHT (UNITS)";
            SDL_Rect ct = cart::drawPanel(ren, w, h, 320, 150, title);
            mBox[0] = cart::drawField(ren, ct.x, ct.y, ct.w, l0, mf[0], mfocus == 0);
            mBox[1] = cart::drawField(ren, ct.x, ct.y + 40, ct.w, l1, mf[1], mfocus == 1);
            mOK = cart::drawButton(ren, ct.x + ct.w - 150, ct.y + ct.h - 20, 70, 18, "OK", true);
            mCancel = cart::drawButton(ren, ct.x + ct.w - 74, ct.y + ct.h - 20, 70, 18,
                                       "CANCEL", false);
        }

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

    for (auto& [k, t] : thumbs) if (t) SDL_DestroyTexture(t);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
