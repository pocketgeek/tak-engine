// hpitool — command-line inspector for TAK HPI archives.
// Usage: hpitool inspect <archive.hpi> [...]

#include "hpi/hpi.h"

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3 || std::string(argv[1]) != "inspect") {
        std::cerr << "usage: hpitool inspect <archive.hpi> [...]\n";
        return 2;
    }

    int failures = 0;
    for (int i = 2; i < argc; ++i) {
        std::cout << "== " << argv[i] << " ==\n";
        try {
            std::cout << tak::hpi::describe(tak::hpi::inspect(argv[i]));
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            ++failures;
        }
        std::cout << "\n";
    }
    return failures ? 1 : 0;
}
