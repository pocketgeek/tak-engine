// cobtool — inspect COB unit scripts.
//
//   cobtool info <file.cob>            script + piece lists
//   cobtool disasm <file.cob> <name>   disassemble one script

#include "cob/cob.h"

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: cobtool info|disasm <file.cob> [script]\n";
        return 2;
    }
    std::string cmd = argv[1];
    try {
        auto f = tak::cob::load(argv[2]);
        if (cmd == "info") {
            std::cout << f.scripts.size() << " scripts, " << f.pieces.size()
                      << " pieces, " << f.code.size() << " code words, "
                      << f.numStatics << " statics\n";
            std::cout << "scripts:";
            for (const auto& s : f.scripts) std::cout << " " << s.name;
            std::cout << "\npieces:";
            for (const auto& p : f.pieces) std::cout << " " << p;
            std::cout << "\n";
        } else if (cmd == "disasm" && argc >= 4) {
            int idx = f.scriptIndex(argv[3]);
            if (idx < 0) { std::cerr << "no script " << argv[3] << "\n"; return 1; }
            std::cout << tak::cob::disassemble(f, idx);
        } else {
            return 2;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
