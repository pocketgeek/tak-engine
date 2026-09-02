#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tak::cob {

// COB unit-script bytecode (TAK version 6; same opcode set as TA's v4 with
// a 13-word header). Scripts animate 3DO pieces by name.

struct Script {
    std::string name;
    uint32_t entry = 0;      // word offset into code
};

struct File {
    std::vector<Script> scripts;
    std::vector<std::string> pieces;   // index = piece number
    std::vector<uint32_t> code;        // 32-bit word stream
    uint32_t numStatics = 0;

    int scriptIndex(const std::string& name) const;   // -1 if absent
};

File load(const std::filesystem::path& path);

// Disassemble one script (by index) to text.
std::string disassemble(const File& f, int script);

} // namespace tak::cob
