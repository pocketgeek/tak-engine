#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace tak {
// A MOD texture must be white outside the silhouette: alpha alone does not
// mask SDL_BLENDMODE_MOD. Preserve fractional coverage at antialiased edges.
inline std::vector<uint8_t> shadowMaskPixels(std::span<const uint8_t> rgba,
                                           uint8_t shadowLevel) {
    std::vector<uint8_t> pixels(rgba.begin(),rgba.end());
    for (size_t i=0;i+3<pixels.size();i+=4) {
        const auto value=uint8_t(255-(255-shadowLevel)*pixels[i+3]/255);
        pixels[i]=pixels[i+1]=pixels[i+2]=value;
        pixels[i+3]=255;
    }
    return pixels;
}
} // namespace tak
