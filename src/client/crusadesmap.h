#pragma once

#include "net/crusades.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tak::hpi { class Vfs; }
namespace tak::crusadesmap {
using Bytes = std::vector<uint8_t>;
struct Image {
    uint32_t width = 0, height = 0;
    Bytes rgba; // Row-major byte order R,G,B,A; suitable for SDL_PIXELFORMAT_RGBA32.
};
struct Parcel {
    uint32_t id = 0;
    std::string name, description, nativeFaction, terrain;
    int32_t fireX = 0, fireY = 0, textX = 0, textY = 0, nativeIcon = 0;
};
struct Definition {
    std::string name;
    // Original header labels, deliberately NOT reinterpreted as image axes.
    uint32_t declaredWidth = 0, declaredHeight = 0, declaredBorders = 0;
    std::map<uint32_t, Parcel> parcels;
};
// Strict bounded CP1252 tagged DarienMap parser and CRC-checked PNG decoder.
// Throw runtime_error with non-asset diagnostics; tests may use synthetic bytes.
Definition parseDefinition(const Bytes& bytes);
Image decodePng(const Bytes& bytes);

class Presentation {
public:
    uint32_t width() const { return borders_.width; }
    uint32_t height() const { return borders_.height; }
    const Definition& definition() const { return definition_; }
    const Parcel* parcel(uint32_t id) const;
    std::optional<uint32_t> regionAt(int x, int y) const;
    size_t unmappedParcelCount() const { return unmappedParcels_; }
    // Requires the complete authoritative ID/name set to match local parcels.
    // No native faction, owner, map assignment or adjacency is inferred.
    bool compatible(const net::crusades::Snapshot& snapshot, std::string* reason = nullptr) const;
    // Ownership comes exclusively from snapshot. Unknown/unmapped fill uses a
    // clearly neutral modern gray; it is never presented as Contested ownership.
    Image compose(const net::crusades::Snapshot& snapshot) const;
private:
    Definition definition_;
    Image borders_, honor_, terror_, contested_;
    std::vector<uint32_t> regions_; // Full-resolution hit-test IDs, zero unmapped.
    size_t unmappedParcels_ = 0;
    friend Presentation makePresentation(Definition, Image, Image, Image, Image);
};
// Reconstructed connected-key-color fills seeded by each parcel's fire anchor.
// Missing/special disconnected seeds remain unmapped; lists still expose parcels.
Presentation makePresentation(Definition definition, Image borders, Image honor, Image terror, Image contested);
struct LoadResult {
    std::optional<Presentation> presentation;
    std::string reason; // Missing/invalid local assets => explicit modern fallback.
};
LoadResult loadPresentation(const hpi::Vfs& vfs);
} // namespace tak::crusadesmap
