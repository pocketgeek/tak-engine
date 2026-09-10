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

#include "client/mapview.h"
#include "hpi/hpi.h"

#include <cstdio>
#include <cstring>
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
    std::string dataRoot, mapName;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) dataRoot = argv[++i];
        else if (a[0] != '-') mapName = a;
    }
    if (dataRoot.empty() || mapName.empty()) {
        std::fprintf(stderr,
            "Cartographer (TA:Kingdoms map editor) -- phase 0\n"
            "usage: cartographer \"<map name>\" --data <retail-install-dir>\n");
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
    std::fprintf(stderr, "cartographer: editing '%s' (%s), %dx%d blocks\n",
                 mapName.c_str(), mapPath.c_str(),
                 mapView.map().blocksX, mapView.map().blocksY);

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                running = false;
            else {
                // The map canvas owns pan/zoom below the menu strip; MapView reads
                // in window coords, so this is 1:1 for now (chrome offset comes
                // when the canvas gets its own viewport in phase 1).
                mapView.input(e);
            }
        }

        int w, h;
        SDL_GetRendererOutputSize(ren, &w, &h);
        int canvasH = h - kMenuH - kStatusH;

        mapView.ensureChunks(w, canvasH);

        // Editor ground: a neutral slate behind everything.
        SDL_SetRenderDrawColor(ren, 24, 26, 32, 255);
        SDL_RenderClear(ren);

        // Map canvas between the menu and status strips.
        SDL_Rect canvas{0, kMenuH, w, canvasH};
        SDL_RenderSetViewport(ren, &canvas);
        mapView.draw(w, canvasH);
        SDL_RenderSetViewport(ren, nullptr);

        // Chrome: menu bar (top) + status bar (bottom). Real menus/text arrive
        // with the font + UI layer in phase 1; these are the structural strips.
        fillRect(ren, 0, 0, w, kMenuH, 46, 48, 58);
        fillRect(ren, 0, kMenuH - 1, w, 1, 12, 12, 16);
        fillRect(ren, 0, h - kStatusH, w, kStatusH, 38, 40, 50);
        fillRect(ren, 0, h - kStatusH, w, 1, 12, 12, 16);

        SDL_RenderPresent(ren);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
