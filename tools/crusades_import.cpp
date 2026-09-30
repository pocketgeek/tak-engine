// Converts user-supplied original territory metadata to the modern campaign format.
// No historical graph, ownership, battle maps or executable rules are inferred.
#include "client/crusadesmap.h"
#include "server/crusades/campaign.h"
#include "util/winargv.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::string quoted(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out + '"';
}
int run(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: crusades_import <Darien.def> <campaign-id> <output.campaign>\n"
                     "Imports metadata only; ownership, adjacency and battle maps stay unknown.\n";
        return 2;
    }
    try {
        const auto input = std::filesystem::u8path(argv[1]);
        const auto output = std::filesystem::u8path(argv[3]);
        if (std::filesystem::exists(output))
            throw std::runtime_error("output already exists; choose a new path to preserve authored data");
        std::ifstream in(input, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open source definition");
        tak::crusadesmap::Bytes bytes;
        std::array<char, 8192> buffer{};
        while (in.read(buffer.data(), buffer.size()) || in.gcount()) {
            const auto count = size_t(in.gcount());
            if (bytes.size() + count > 8u * 1024u * 1024u)
                throw std::runtime_error("source definition exceeds 8 MiB");
            bytes.insert(bytes.end(), buffer.data(), buffer.data() + count);
        }
        if (in.bad() || !in.eof()) throw std::runtime_error("cannot read source definition");
        const auto source = tak::crusadesmap::parseDefinition(bytes);
        std::string text = "# Imported local territory metadata; modern engine format.\n"
                           "# Ownership, adjacency and battle maps are intentionally unknown.\n";
        text += "campaign 1 " + quoted(argv[2]) + " " + quoted(source.name) + "\n";
        for (const auto& entry : source.parcels) {
            const auto& p = entry.second;
            const std::string id = std::to_string(p.id);
            text += "territory " + id + " " + quoted(p.name) + "\n";
            text += "native " + id + " " + quoted(p.nativeFaction) + "\n";
            text += "terrain " + id + " " + quoted(p.terrain) + "\n";
        }
        const auto validated = tak::srv::crusades::loadDefinitionText(text);
        std::ofstream out(output, std::ios::binary);
        if (!out) throw std::runtime_error("cannot create output definition");
        out.write(text.data(), std::streamsize(text.size()));
        out.close();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(output, ignored);
            throw std::runtime_error("cannot write output definition");
        }
        std::cout << "Imported " << validated.territories().size() << " territories.\n";
        std::cerr << "Battle-map identifiers must be authored separately before launching battles.\n"
                     "No initial ownership or adjacency was inferred. Keep asset-derived output with your local game data.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "crusades_import: " << e.what() << '\n';
        return 1;
    }
}
}
int main(int argc, char** argv) {
#ifdef _WIN32
    (void)argc; (void)argv;
    return tak::utf8Main(run);
#else
    return run(argc, argv);
#endif
}
