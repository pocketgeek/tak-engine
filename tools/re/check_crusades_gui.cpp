// Research-only driver for the existing numeric GUI parser. No proprietary data.
// Build: c++ -std=c++17 -Isrc tools/re/check_crusades_gui.cpp src/gui/gui.cpp -o /tmp/check-crusades-gui
#include "gui/gui.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: check-crusades-gui FILE...\n";
        return 2;
    }
    bool failed = false;
    for (int i = 1; i < argc; ++i) {
        try {
            std::ifstream input(argv[i], std::ios::binary);
            if (!input) throw std::runtime_error("cannot open input");
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
            const auto parsed = tak::gui::parse(bytes, argv[i]);
            if (parsed.gadgets.empty()) throw std::runtime_error("empty GUI");
            std::map<int, size_t> types;
            size_t images = 0, texts = 0, states = 0;
            for (const auto& gadget : parsed.gadgets) {
                ++types[gadget.type];
                images += gadget.imgs.size();
                texts += gadget.texts.size();
                states += gadget.states.size();
            }
            std::cout << argv[i] << '\t' << parsed.gadgets.size() << '\t'
                      << images << '\t' << texts << '\t' << states << '\t';
            for (const auto& [type, count] : types)
                std::cout << type << ':' << count << ',';
            std::cout << '\n';
        } catch (const std::exception& error) {
            std::cerr << argv[i] << ": " << error.what() << '\n';
            failed = true;
        }
    }
    return failed ? 1 : 0;
}
