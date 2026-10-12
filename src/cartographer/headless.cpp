// Command line and the non-interactive paths: --stamp / --save / --bundle
// one-shots (run before any UI state exists) and --shot, which renders a
// single frame of the full editor and writes it as a PNG.

#include "cartographer/editorstate.h"
#include "util/png.h"

#include <cstdio>
#include <cstdlib>

void cart::Session::parseArguments(int argc,char** argv) {
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
}

std::optional<int> cart::Session::runHeadless() {
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
    return std::nullopt;
}

void cart::Editor::prepareShot() {
    if (!shotPath.empty()) {
        mapView.setZoom(0.3f);          // fit-ish view for the shot
        mapView.finishChunks();         // wait for the terrain to decode+upload
    }
}

void cart::Editor::captureShot() {
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
