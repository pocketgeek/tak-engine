// tnttool — inspect TAK TNT maps.
//
//   tnttool info <map.tnt>            header + layer summary
//   tnttool heightmap <map.tnt> <out.png>
//   tnttool minimap <map.tnt> <out.png>   (grayscale; palette applied later)

#include "terrain/terrain.h"
#include "tnt/tnt.h"
#include "util/png.h"

#include <iostream>
#include <set>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: tnttool info|heightmap|minimap <map.tnt> [out.png]\n"
                     "       tnttool render <map.tnt> <terrain-dir> <out.png>\n";
        return 2;
    }
    std::string cmd = argv[1];
    try {
        auto m = tak::tnt::Map::load(argv[2]);

        if (cmd == "info") {
            std::cout << m.width << "x" << m.height << " cells ("
                      << m.width * 16 << "x" << m.height * 16 << " px)\n";
            std::set<uint32_t> keys(m.tileKeys.begin(), m.tileKeys.end());
            std::cout << "terrain JPGs: " << keys.size() << " distinct\n";
            size_t feats = 0;
            for (auto f : m.features)
                if (f != 0xFFFF) ++feats;
            std::cout << "feature cells: " << feats << "\n";
            std::cout << "minimap: " << m.minimapW << "x" << m.minimapH << "\n";
        } else if (cmd == "render" && argc >= 5) {
            tak::terrain::Compositor comp(argv[3]);
            auto img = comp.renderMap(m);
            tak::png::write(argv[4], img.width, img.height, img.rgba);
            std::cout << "wrote " << argv[4] << " (" << img.width << "x" << img.height
                      << ")\n";
        } else if ((cmd == "heightmap" || cmd == "minimap") && argc >= 4) {
            int w, h;
            const std::vector<uint8_t>* src;
            if (cmd == "heightmap") {
                w = m.width; h = m.height; src = &m.heights;
            } else {
                w = m.minimapW; h = m.minimapH; src = &m.minimap;
            }
            std::vector<uint8_t> rgba(size_t(w) * h * 4);
            for (size_t i = 0; i < src->size(); ++i) {
                uint8_t v = (*src)[i];
                rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = v;
                rgba[i * 4 + 3] = 255;
            }
            tak::png::write(argv[3], w, h, rgba);
            std::cout << "wrote " << argv[3] << " (" << w << "x" << h << ")\n";
        } else {
            std::cerr << "unknown command\n";
            return 2;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
