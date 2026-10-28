#pragma once

// HIDDEN UNITS (Options > Graphics; a non-retail extra, default SHADOW): a unit
// that stands behind a tree, a rock or a building is still shown where it is.
//   OFF     -- as retail: whatever paints over a unit hides it.
//   DOTS    -- when the scenery covers the centre of a unit, that unit's tactical
//              dot (client/tacticaldots.h: same size, colour and selection rim)
//              is drawn on top. A unit whose centre is in plain sight gets none.
//   SHADOW  -- the cover turns see-through where it hides the unit: the covered
//              part shows as a faint shadow through the tree or the wall, the part
//              in sight is untouched (see Ghost below).
// Marked are the units the frame draws anyway (own, allied, and enemies in sight:
// nothing under fog, no cloaked enemy that is not drawn), standing on the ground
// (an airborne flyer paints after every tree and is never covered). What covers is
// map scenery and buildings painted over the unit; another mobile unit never does.
// Pure display -- the sim, the lockstep hash and the network never see it.
//
// This header is the policy, kept free of GameView so it can be tested on its own:
// the option values, the painter layers (the retail order of low features), the
// exact cover tests, the screen bins that find a unit's possible covers and the
// order of the SHADOW ghosts.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace tak::hiddenunits {

enum Mode : int { kOff = 0, kDots = 1, kShadow = 2 };
inline constexpr int kDefault = kShadow;

// Persisted as hiddenUnits = off | dots | shadow. A file from before the option
// (no key), one from the OUTLINE mode SHADOW replaced ("outline") or an unknown
// value reads as the default.
inline const char* name(int mode) {
    return mode == kOff ? "off" : mode == kDots ? "dots" : "shadow";
}
inline int parse(std::string_view value) {
    if (value == "off" || value == "0") return kOff;
    if (value == "dots" || value == "1") return kDots;
    return kDefault;
}

// ---- Painter layers ---------------------------------------------------------
// Retail's world pass (KINGDOMS.icd 0x4fc648..0x4fcc46) scans the map cells and
// draws every feature whose definition height (feature TDF `height`, def +0x138,
// parsed at 0x4939ef/0x493a0e) is below 10 at once, before any unit; taller
// features are drawn with the ground units in depth order, and units that are not
// on the ground go last. So a stump, a crop row, a mana deposit or a smudge never
// paints over a unit's feet, whichever side of it the unit stands on.
inline constexpr int kLowFeatureHeight = 10;
inline constexpr int kLayerLowFeature = -1;   // flat scenery: under every unit
inline constexpr int kLayerGround = 0;        // tall features + ground units, by depth
inline constexpr int kLayerAir = 1;           // airborne flyers, after everything
inline constexpr bool lowFeature(int tdfHeight) { return tdfHeight < kLowFeatureHeight; }
inline constexpr int featureLayer(int tdfHeight) {
    return lowFeature(tdfHeight) ? kLayerLowFeature : kLayerGround;
}

// ---- Screen rectangles -------------------------------------------------------
struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return !(x1 > x0 && y1 > y0); }
    bool overlaps(const Rect& o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
    bool contains(float x, float y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
    Rect clipped(const Rect& o) const {
        return {std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1)};
    }
    void unite(const Rect& o) {
        if (o.empty()) return;
        if (empty()) { *this = o; return; }
        x0 = std::min(x0, o.x0); y0 = std::min(y0, o.y0);
        x1 = std::max(x1, o.x1); y1 = std::max(y1, o.y1);
    }
};

// ---- Exact cover tests -------------------------------------------------------
// One bit per texel of a sprite frame: opaque or not. Feature art is palette art
// whose texels are either fully transparent or opaque; half way is the cut.
class AlphaMask {
public:
    void build(const uint8_t* rgba, int width, int height, uint8_t threshold = 128) {
        w_ = std::max(0, width); h_ = std::max(0, height);
        stride_ = (size_t(w_) + 63) / 64;
        bits_.assign(stride_ * size_t(h_), 0);
        if (!rgba) return;
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x)
                if (rgba[(size_t(y) * size_t(w_) + size_t(x)) * 4 + 3] >= threshold)
                    bits_[size_t(y) * stride_ + size_t(x) / 64] |= uint64_t(1) << (x % 64);
    }
    int width() const { return w_; }
    int height() const { return h_; }
    bool opaque(int x, int y) const {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return false;
        return (bits_[size_t(y) * stride_ + size_t(x) / 64] >> (x % 64)) & 1;
    }
    size_t bytes() const { return bits_.capacity() * sizeof(uint64_t); }

private:
    int w_ = 0, h_ = 0;
    size_t stride_ = 0;
    std::vector<uint64_t> bits_;
};

// A sprite drawn into the screen quad (x, y, w, h) whose TOP edge is shifted
// sideways by `shear` (the tree sway; 0 for a plain sprite): is the screen point
// (px, py) on one of its opaque texels?
inline bool spriteCovers(const AlphaMask& mask, float x, float y, float w, float h, float shear,
                         float px, float py) {
    if (!(w > 0) || !(h > 0) || mask.width() <= 0 || mask.height() <= 0) return false;
    const float v = (py - y) / h;
    if (!(v >= 0.0f && v < 1.0f)) return false;
    const float u = (px - x - shear * (1.0f - v)) / w;
    if (!(u >= 0.0f && u < 1.0f)) return false;
    return mask.opaque(int(u * float(mask.width())), int(v * float(mask.height())));
}

// Is (px, py) inside the triangle (edges included)? Either winding.
inline bool pointInTriangle(float px, float py, float ax, float ay, float bx, float by,
                            float cx, float cy) {
    const float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
    const float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
    const float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0;
    const bool pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

// A model's screen triangles (a plain list, three vertices each; anything with
// .position.x/.y, i.e. SDL_Vertex): does one of them contain the point?
template <class Vertex>
bool trianglesCover(std::span<const Vertex> verts, float px, float py) {
    for (size_t i = 0; i + 2 < verts.size(); i += 3) {
        const auto& a = verts[i].position;
        const auto& b = verts[i + 1].position;
        const auto& c = verts[i + 2].position;
        if (px < std::min({a.x, b.x, c.x}) || px > std::max({a.x, b.x, c.x}) ||
            py < std::min({a.y, b.y, c.y}) || py > std::max({a.y, b.y, c.y})) continue;
        if (pointInTriangle(px, py, a.x, a.y, b.x, b.y, c.x, c.y)) return true;
    }
    return false;
}

// ---- Screen bins -------------------------------------------------------------
// The covers seen so far, binned by screen cell. The frame walks the painter list
// BACKWARDS, so when it reaches a unit every cover in the grid paints after it:
// a query answers "what may cover this unit" in about O(covers nearby). Each
// cover is reported once per query. Storage is kept between frames.
class CoverGrid {
public:
    void reset(float width, float height, float cell = 64.0f) {
        cell_ = std::max(cell, 1.0f);
        cols_ = std::max(1, int(std::ceil(std::max(width, 1.0f) / cell_)));
        rows_ = std::max(1, int(std::ceil(std::max(height, 1.0f) / cell_)));
        const size_t n = size_t(cols_) * size_t(rows_);
        if (bins_.size() < n) bins_.resize(n);
        for (size_t i = 0; i < n; ++i) bins_[i].clear();
        stamps_.clear();
        query_ = 0;
        count_ = 0;
    }
    size_t size() const { return count_; }
    // Ids are dense: 0, 1, 2, ... in insertion order.
    uint32_t insert(const Rect& r) {
        const uint32_t id = uint32_t(count_++);
        stamps_.push_back(0);
        int c0, r0, c1, r1;
        if (!cells(r, c0, r0, c1, r1)) return id;
        for (int y = r0; y <= r1; ++y)
            for (int x = c0; x <= c1; ++x) bins_[size_t(y) * size_t(cols_) + size_t(x)].push_back(id);
        return id;
    }
    template <class F> void query(const Rect& r, F&& visit) {
        int c0, r0, c1, r1;
        if (!cells(r, c0, r0, c1, r1)) return;
        if (++query_ == 0) { std::fill(stamps_.begin(), stamps_.end(), 0u); query_ = 1; }
        for (int y = r0; y <= r1; ++y)
            for (int x = c0; x <= c1; ++x)
                for (uint32_t id : bins_[size_t(y) * size_t(cols_) + size_t(x)]) {
                    if (stamps_[id] == query_) continue;
                    stamps_[id] = query_;
                    if (!visit(id)) return;   // the visitor returns false to stop
                }
    }
    size_t capacityBytes() const {
        size_t b = stamps_.capacity() * sizeof(uint32_t) + bins_.capacity() * sizeof(bins_[0]);
        for (const auto& v : bins_) b += v.capacity() * sizeof(uint32_t);
        return b;
    }

private:
    bool cells(const Rect& r, int& c0, int& r0, int& c1, int& r1) const {
        if (r.empty()) return false;
        c0 = std::max(0, int(std::floor(r.x0 / cell_)));
        r0 = std::max(0, int(std::floor(r.y0 / cell_)));
        c1 = std::min(cols_ - 1, int(std::floor(r.x1 / cell_)));
        r1 = std::min(rows_ - 1, int(std::floor(r.y1 / cell_)));
        return c0 <= c1 && r0 <= r1;
    }
    float cell_ = 64.0f;
    int cols_ = 1, rows_ = 1;
    std::vector<std::vector<uint32_t>> bins_;
    std::vector<uint32_t> stamps_;
    uint32_t query_ = 0;
    size_t count_ = 0;
};

// ---- SHADOW ghosts ------------------------------------------------------------
// The redraw form, for renderers without the stencil form (client/hiddenstencil.h:
// OpenGL with a stencil on the window marks the hidden units' pixels and draws each
// cover over them see-through at the same opacities, exact per pixel, with nothing
// drawn again but the covers; only the cover search below is shared).
// A covered unit gets a ghost: right after the LAST cover over it in painter order
// (so whatever stands in front of that cover still paints over the ghost) the
// painter items from `start` to `last` that overlap the unit's body rectangle are
// drawn again, in painter order, clipped to that rectangle: units as they were
// (opaque, so they repaint their own pixels), every cover see-through (kVeilAlpha;
// kBuildingVeilAlpha for a building). Where a cover hides the unit the screen ends
// up kShadowOpacity of the unit over the cover; the part of a unit in sight keeps
// its pixels, because an opaque redraw changes nothing. (Except the soft edge of a
// distant image, which drawn twice comes out a little stronger.) Off the unit the
// cover mostly keeps its pixels too, but not quite: inside the rectangle a cover
// drawn over another (a tree over a tree) or over its own faces (a building), or
// with soft or glowing texels (a lodestone), lets a little of what it painted over
// back through (the hidden_units harness: up to ~75 levels on a few hundred pixels).
// Exact there would take a coverage mask. No render target, no coverage mask, no
// special blending.
//
// Why see-through COVER and not a darkened, tinted copy of the UNIT: a copy drawn
// over the whole unit would change the part in sight too, and with plain blending
// nothing can undo that without knowing which pixels were covered. The cover's own
// dark or stone pixels are what darken the shadow; the unit's player colours show
// through like the rest of it.
//
// A ghost whose redraw would not be exact (the unit itself or an item in its range
// is drawn translucent or through drawUnit with effects: conjuring, a builder at
// work, an emote, a spectral unit) draws the unit alone at kShadowOpacity over the
// cover instead; its visible part then shifts slightly where the model's own faces
// overlap, and a unit in front of it within its rectangle shows it faintly.
// Through a building the unit shows stronger: its light stone and roof tiles wash a
// 45% unit out, where a tree's dark crown does not.
inline constexpr uint8_t kShadowOpacity = 115;                         // ~45%: scenery
inline constexpr uint8_t kBuildingShadowOpacity = 166;                 // ~65%: a building
inline constexpr uint8_t kVeilAlpha = 255 - kShadowOpacity;            // the cover, redrawn over the unit
inline constexpr uint8_t kBuildingVeilAlpha = 255 - kBuildingShadowOpacity;
inline constexpr uint8_t scaleAlpha(uint8_t a, uint8_t by) { return uint8_t((int(a) * int(by) + 127) / 255); }

// Where an earlier ghost's rectangle overlaps a later one's, the later must start
// no later than the earlier did: else it veils the earlier ghost's covers again
// over what that ghost drew (the earlier unit's shadow fading to kShadowOpacity
// squared in a rectangle). With the earlier range included, the overlap is redrawn
// from the same items and comes out the same. `ghosts` in draw order (by `last`);
// `start` is lowered in place. Painter indices.
struct Ghost {
    Rect rect;
    uint32_t start = 0, last = 0;
};
// `ghostOf(element)` gives the element's Ghost (by reference).
template <class T, class GhostOf>
void chainStarts(std::span<T> ghosts, GhostOf ghostOf, CoverGrid& grid, float width, float height) {
    grid.reset(width, height);
    for (T& element : ghosts) {
        Ghost& g = ghostOf(element);
        grid.query(g.rect, [&](uint32_t id) {
            const Ghost& o = ghostOf(ghosts[id]);
            if (o.rect.overlaps(g.rect)) g.start = std::min(g.start, o.start);
            return true;
        });
        grid.insert(g.rect);   // id == index: inserted in order
    }
}
inline void chainStarts(std::span<Ghost> ghosts, CoverGrid& grid, float width, float height) {
    chainStarts(ghosts, [](Ghost& g) -> Ghost& { return g; }, grid, width, height);
}

// Hidden units whose rectangles overlap, directly or through others, share one ghost:
// a redraw over the bounding rectangle of all of them, from the earliest start to the
// latest last. An item near several hidden units is then drawn again once, not once
// per unit, and the redraw batches like the body pass. (Extending a ghost's range to
// a later last is always allowed: the items in between are redrawn in order.)
// group[i] = the representative (lowest index) of rects[i]'s group; an empty
// rectangle is a group of its own.
inline void groupOverlaps(std::span<const Rect> rects, CoverGrid& grid, float width, float height,
                          std::vector<uint32_t>& group) {
    group.resize(rects.size());
    for (size_t i = 0; i < rects.size(); ++i) group[i] = uint32_t(i);
    const auto root = [&](uint32_t i) {
        while (group[i] != i) i = group[i] = group[group[i]];
        return i;
    };
    grid.reset(width, height);
    for (size_t i = 0; i < rects.size(); ++i) {
        grid.query(rects[i], [&](uint32_t id) {
            if (!rects[id].overlaps(rects[i])) return true;
            const uint32_t a = root(uint32_t(i)), b = root(id);
            group[std::max(a, b)] = std::min(a, b);
            return true;
        });
        grid.insert(rects[i]);   // id == index
    }
    for (size_t i = 0; i < rects.size(); ++i) group[i] = root(uint32_t(i));
}

}  // namespace tak::hiddenunits
