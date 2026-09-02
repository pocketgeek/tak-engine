#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace tak::jpeg {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;   // width*height*4
};

Image load(const std::filesystem::path& file);

} // namespace tak::jpeg
