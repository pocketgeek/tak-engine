#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace tak {

struct ProjectileModelDisplayScale {
    float along = 1.0f;
    float across = 1.0f;
};

// Keep elongated authored 3DOs readable at map zoom. This only changes the
// projected mesh silhouette; projectile trajectory and collision stay native.
inline ProjectileModelDisplayScale projectileModelDisplayScale(
    std::string_view name, float zoom) {
    static constexpr std::array<std::string_view, 13> readableModels = {
        "araarrow", "araarrow2", "araarrow3", "arabolt", "cregatl1",
        "araharp1", "verbal1", "verbal1_vet", "verhpoon", "verspear",
        "verspear_10", "zonterspear", "zonterspearvet"
    };
    if (std::find(readableModels.begin(), readableModels.end(), name) == readableModels.end())
        return {};

    const float along = std::clamp(2.0f / std::max(zoom, 0.01f), 1.0f, 1.5f);
    const float nearZoom = (along - 1.0f) / 0.5f;
    return {along, along * (1.0f + 1.25f * nearZoom)};
}

} // namespace tak
