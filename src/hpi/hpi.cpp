#include "hpi/hpi.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace tak::hpi {

HeaderInfo inspect(const std::filesystem::path& archive) {
    std::ifstream in(archive, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + archive.string());
    }

    HeaderInfo info;
    info.fileSize = std::filesystem::file_size(archive);

    char magic[4] = {};
    uint32_t words[4] = {};
    in.read(magic, sizeof magic);
    in.read(reinterpret_cast<char*>(words), sizeof words);
    if (!in) {
        throw std::runtime_error(archive.string() + ": too short for an HPI header");
    }

    info.magic.assign(magic, 4);
    info.version = words[0];
    info.word2 = words[1];
    info.word3 = words[2];
    info.word4 = words[3];
    return info;
}

std::string describe(const HeaderInfo& info) {
    std::ostringstream out;
    out << "magic:    " << (info.magic == "HAPI" ? "HAPI (ok)" : "'" + info.magic + "' (not an HPI archive?)") << "\n";

    char buf[64];
    std::snprintf(buf, sizeof buf, "version:  0x%08x", info.version);
    out << buf;
    switch (info.version) {
        case 0x00010000: out << " (classic Total Annihilation)"; break;
        case 0x00020000: out << " (TA: Kingdoms)"; break;
        case 0x4B4E4142: out << " ('BANK' savegame)"; break;
        default:         out << " (unknown)"; break;
    }
    out << "\n";

    std::snprintf(buf, sizeof buf, "word2:    0x%08x\nword3:    0x%08x\nword4:    0x%08x\n",
                  info.word2, info.word3, info.word4);
    out << buf;
    out << "size:     " << info.fileSize << " bytes\n";
    return out.str();
}

} // namespace tak::hpi
