// hpitool — command-line tool for TAK HPI archives.
//
//   hpitool inspect <archive.hpi>...          show header info
//   hpitool list <archive.hpi>                list contents
//   hpitool cat <archive.hpi> <path>          write one file to stdout
//   hpitool extract <archive.hpi> <outdir>    extract everything

#include "hpi/hpi.h"

#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static int usage() {
    std::cerr << "usage: hpitool inspect <archive.hpi>...\n"
                 "       hpitool list <archive.hpi>\n"
                 "       hpitool cat <archive.hpi> <internal-path>\n"
                 "       hpitool extract <archive.hpi> <outdir>\n";
    return 2;
}

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string cmd = argv[1];

    try {
        if (cmd == "inspect") {
            for (int i = 2; i < argc; ++i) {
                std::cout << "== " << argv[i] << " ==\n"
                          << tak::hpi::describe(tak::hpi::inspect(argv[i])) << "\n";
            }
        } else if (cmd == "list") {
            tak::hpi::Archive ar(argv[2]);
            size_t files = 0;
            uint64_t total = 0;
            for (const auto& e : ar.entries()) {
                if (e.isDirectory) {
                    std::cout << "         [" << e.path << "]\n";
                } else {
                    std::printf("%9u %s\n", e.decompressedSize, e.path.c_str());
                    ++files;
                    total += e.decompressedSize;
                }
            }
            std::cout << files << " files, " << total << " bytes\n";
        } else if (cmd == "cat" && argc >= 4) {
            tak::hpi::Archive ar(argv[2]);
            const auto* e = ar.find(argv[3]);
            if (!e) {
                std::cerr << "not found: " << argv[3] << "\n";
                return 1;
            }
            auto data = ar.read(*e);
            std::cout.write(reinterpret_cast<const char*>(data.data()),
                            static_cast<std::streamsize>(data.size()));
        } else if (cmd == "extract" && argc >= 4) {
            tak::hpi::Archive ar(argv[2]);
            fs::path outDir = argv[3];
            size_t files = 0;
            for (const auto& e : ar.entries()) {
                fs::path out = outDir / e.path;
                if (e.isDirectory) {
                    fs::create_directories(out);
                    continue;
                }
                fs::create_directories(out.parent_path());
                auto data = ar.read(e);
                std::ofstream f(out, std::ios::binary);
                f.write(reinterpret_cast<const char*>(data.data()),
                        static_cast<std::streamsize>(data.size()));
                if (!f) throw std::runtime_error("write failed: " + out.string());
                ++files;
            }
            std::cout << "extracted " << files << " files to " << outDir.string() << "\n";
        } else {
            return usage();
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
