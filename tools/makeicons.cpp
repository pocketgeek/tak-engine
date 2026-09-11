// makeicons -- render the app icons (src/util/appicon) to PNGs.
//   makeicons <outdir>
// Writes takclient.png, cartographer.png, takserver.png at 256px into <outdir>.

#include "util/appicon.h"
#include "util/png.h"

#include <cstdio>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    std::filesystem::create_directories(dir);
    struct { tak::appicon::Kind kind; const char* name; } apps[] = {
        {tak::appicon::Kind::Client, "takclient"},
        {tak::appicon::Kind::Cartographer, "cartographer"},
        {tak::appicon::Kind::Server, "takserver"},
    };
    const int sz = 256;
    for (auto& a : apps) {
        auto px = tak::appicon::render(a.kind, sz);
        std::string p = dir + "/" + a.name + ".png";
        tak::png::write(p, sz, sz, px);
        std::printf("wrote %s (%dx%d)\n", p.c_str(), sz, sz);
    }
    return 0;
}
