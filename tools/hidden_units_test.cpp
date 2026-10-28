// hidden_units_test -- the HIDDEN UNITS policy (client/hiddenunits.h).
//
// The option's persisted names, the painter layer that puts flat features (TDF height
// below 10) under every unit, the pieces the DOTS test is built from (the sprite
// opacity mask with the sway shear, the model triangle test, the screen bins the frame
// walks backwards) and the SHADOW ghosts' alpha and chained ranges. The draw itself
// (cover order, fog, shadow pixels) is checked through the real renderer by the
// hidden_units harness test (TAK_HIDDEN_UNITS_TEST).

#include "client/hiddenunits.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace hu = tak::hiddenunits;

static int g_fail = 0;

static void check(const char* what, bool ok) {
    std::printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++g_fail;
}

struct Point { float x, y; };
struct Vertex { Point position; };

int main() {
    std::printf("hidden_units_test: the HIDDEN UNITS policy\n");

    // ---- the option ----
    check("default is FADE", hu::kDefault == hu::kFade);
    check("names round trip", hu::parse(hu::name(hu::kOff)) == hu::kOff &&
                              hu::parse(hu::name(hu::kDots)) == hu::kDots &&
                              hu::parse(hu::name(hu::kShadow)) == hu::kShadow &&
                              hu::parse(hu::name(hu::kFade)) == hu::kFade);
    check("saved as fade", std::string(hu::name(hu::kFade)) == "fade" && hu::parse("3") == hu::kFade);
    check("four modes", hu::kModeCount == 4 && hu::kFade == hu::kModeCount - 1);
    check("saved as shadow", std::string(hu::name(hu::kShadow)) == "shadow");
    check("unknown value reads as the default", hu::parse("") == hu::kDefault && hu::parse("halo") == hu::kDefault);
    check("shadow still read", hu::parse("shadow") == hu::kShadow && hu::parse("2") == hu::kShadow);
    check("the replaced OUTLINE reads as the default", hu::parse("outline") == hu::kDefault);

    // ---- the low layer ----
    check("height 0 and 9 are low, 10 and 226 are not",
          hu::lowFeature(0) && hu::lowFeature(9) && !hu::lowFeature(10) && !hu::lowFeature(226));
    {
        // The painter list's order: layer, then depth, stable. A smudge south of a
        // unit (larger z) still goes first; a tree south of it goes after it.
        struct Item { float z; int layer; char what; };
        std::vector<Item> items = {{120, hu::featureLayer(0), 's'}, {100, hu::kLayerGround, 'u'},
                                   {110, hu::featureLayer(226), 't'}, {90, hu::kLayerAir, 'f'},
                                   {80, hu::featureLayer(9), 'm'}};
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            if (a.layer != b.layer) return a.layer < b.layer;
            return a.z < b.z;
        });
        std::string order;
        for (const Item& i : items) order += i.what;
        check("flat features first, then ground by depth, flyers last", order == "msutf");
    }

    // ---- sprite cover ----
    {
        // 4x2 sprite: only column 2 of the top row and column 0 of the bottom row opaque.
        std::vector<uint8_t> px(4 * 2 * 4, 0);
        px[(0 * 4 + 2) * 4 + 3] = 255;
        px[(1 * 4 + 0) * 4 + 3] = 255;
        px[(1 * 4 + 1) * 4 + 3] = 100;   // below the cut: transparent
        hu::AlphaMask m;
        m.build(px.data(), 4, 2);
        check("mask: opaque texels", m.opaque(2, 0) && m.opaque(0, 1) && !m.opaque(1, 1) && !m.opaque(0, 0));
        check("mask: outside is clear", !m.opaque(-1, 0) && !m.opaque(4, 0) && !m.opaque(0, 2));
        // Drawn at (100, 50), 40x20 on screen: a texel is 10x10.
        check("sprite: point on an opaque texel", hu::spriteCovers(m, 100, 50, 40, 20, 0, 125, 55));
        check("sprite: point on a clear texel", !hu::spriteCovers(m, 100, 50, 40, 20, 0, 105, 55));
        check("sprite: point outside", !hu::spriteCovers(m, 100, 50, 40, 20, 0, 99, 55) &&
                                       !hu::spriteCovers(m, 100, 50, 40, 20, 0, 125, 70));
        // Swayed: the top edge moved 8 px right, the bottom edge stays.
        check("sway: the crown moves with the shear", hu::spriteCovers(m, 100, 50, 40, 20, 8, 132, 51) &&
                                                     !hu::spriteCovers(m, 100, 50, 40, 20, 0, 132, 51));
        check("sway: the base stays put", hu::spriteCovers(m, 100, 50, 40, 20, 8, 101, 69));
        check("sprite: degenerate quad covers nothing", !hu::spriteCovers(m, 100, 50, 0, 20, 0, 100, 55));
    }

    // ---- structure triangles ----
    {
        const std::vector<Vertex> tris = {{{0, 0}}, {{10, 0}}, {{0, 10}},      // lower-left half
                                          {{20, 20}}, {{30, 20}}, {{20, 30}}};
        check("triangle: inside", hu::trianglesCover(std::span<const Vertex>(tris), 2, 2));
        check("triangle: edge counts", hu::trianglesCover(std::span<const Vertex>(tris), 5, 5));
        check("triangle: outside", !hu::trianglesCover(std::span<const Vertex>(tris), 8, 8));
        check("triangle: second one", hu::trianglesCover(std::span<const Vertex>(tris), 22, 22));
        check("triangle: either winding", hu::pointInTriangle(2, 2, 0, 0, 0, 10, 10, 0));
        const std::vector<Vertex> partial = {{{0, 0}}, {{10, 0}}};   // not a whole triangle
        check("triangle: a partial triple is ignored", !hu::trianglesCover(std::span<const Vertex>(partial), 1, 0));
    }

    // ---- screen bins (the backwards walk) ----
    {
        hu::CoverGrid g;
        g.reset(640, 480, 64);
        // Painter order: unit A, tree T1 (over A), unit B, tree T2 (far away).
        // Walked backwards: T2, B, T1, A.
        const uint32_t t2 = g.insert({500, 300, 560, 400});
        size_t seen = 0;
        g.query({100, 100, 140, 160}, [&](uint32_t) { ++seen; return true; });
        check("B: the far tree is no cover", seen == 0);
        const uint32_t t1 = g.insert({90, 60, 200, 200});   // spans many cells
        std::vector<uint32_t> ids;
        g.query({100, 100, 140, 160}, [&](uint32_t id) { ids.push_back(id); return true; });
        check("A: the tree after it found once", ids == std::vector<uint32_t>{t1});
        ids.clear();
        g.query({0, 0, 640, 480}, [&](uint32_t id) { ids.push_back(id); return true; });
        std::sort(ids.begin(), ids.end());
        check("whole screen: every cover once", ids == (std::vector<uint32_t>{t2, t1}) && g.size() == 2);
        size_t visits = 0;
        g.query({0, 0, 640, 480}, [&](uint32_t) { ++visits; return false; });
        check("a visitor can stop the query", visits == 1);
        g.query({-50, -50, -10, -10}, [&](uint32_t) { ++visits; return true; });
        check("off screen: nothing", visits == 1);
        g.reset(320, 240, 64);
        g.query({0, 0, 320, 240}, [&](uint32_t) { ++visits; return true; });
        check("reset empties the bins", visits == 1 && g.size() == 0);
    }

    // ---- rectangles ----
    {
        hu::Rect a{0, 0, 10, 10}, b{10, 0, 20, 10}, u;
        check("touching rectangles do not overlap", !a.overlaps(b));
        u.unite(a); u.unite(b); u.unite(hu::Rect{});
        check("union", u.x0 == 0 && u.x1 == 20 && u.y1 == 10);
        check("clip", a.clipped({5, 5, 50, 50}).x0 == 5 && hu::Rect{20, 20, 30, 30}.clipped(a).empty());
    }

    // ---- SHADOW ----
    check("shadow about 45%, the veil the rest", hu::kShadowOpacity >= 100 && hu::kShadowOpacity <= 130 &&
                                                 hu::kVeilAlpha + hu::kShadowOpacity == 255);
    check("stronger through a building", hu::kBuildingShadowOpacity > hu::kShadowOpacity &&
                                         hu::kBuildingVeilAlpha + hu::kBuildingShadowOpacity == 255);
    check("alpha scaling", hu::scaleAlpha(255, 115) == 115 && hu::scaleAlpha(0, 200) == 0 &&
                           hu::scaleAlpha(128, 255) == 128 && hu::scaleAlpha(200, 128) == 100);
    {
        // Draw order A, B, C, D (by last cover). A and B overlap, B and C overlap, D is
        // apart: B starts no later than A, C no later than B (so A, through B), D keeps
        // its own start.
        std::vector<hu::Ghost> g = {{{0, 0, 20, 20}, 50, 60},
                                    {{10, 10, 30, 30}, 55, 70},
                                    {{25, 25, 45, 45}, 65, 80},
                                    {{300, 300, 320, 320}, 10, 90}};
        hu::CoverGrid grid;
        hu::chainStarts(std::span<hu::Ghost>(g), grid, 640, 480);
        check("chain: the first keeps its start", g[0].start == 50);
        check("chain: an overlap starts no later than the earlier ghost", g[1].start == 50);
        check("chain: through a chain of overlaps", g[2].start == 50);
        check("chain: a ghost apart keeps its start", g[3].start == 10);
        std::vector<hu::Ghost> touching = {{{0, 0, 10, 10}, 5, 6}, {{10, 0, 20, 10}, 7, 8}};
        hu::chainStarts(std::span<hu::Ghost>(touching), grid, 640, 480);
        check("chain: touching rectangles do not chain", touching[1].start == 7);
    }
    {
        // A-B overlap, B-C overlap (A and C do not): one group; D apart; E empty.
        const std::vector<hu::Rect> r = {{0, 0, 20, 20}, {300, 300, 320, 320}, {15, 15, 40, 40},
                                         {35, 35, 60, 60}, {}};
        std::vector<uint32_t> group;
        hu::CoverGrid grid;
        hu::groupOverlaps(std::span<const hu::Rect>(r), grid, 640, 480, group);
        check("group: overlaps join, through others too", group[0] == 0 && group[2] == 0 && group[3] == 0);
        check("group: apart and empty stay alone", group[1] == 1 && group[4] == 4);
    }

    // ---- FADE ----
    check("fade default 65%", hu::kDefaultFadePercent == 65 && hu::fadeOpacity(65) == 166);
    check("fade opacity ends", hu::fadeOpacity(0) == 0 && hu::fadeOpacity(100) == 255 &&
                               hu::fadeOpacity(-3) == 0 && hu::fadeOpacity(140) == 255 && hu::fadeOpacity(50) == 128);
    check("cover keys apart", hu::featureCover(5) != hu::structureCover(5) &&
                              hu::structureCover(5) == hu::structureCover(5));
    {
        // The triangle (0,0) (100,0) (0,100): its hypotenuse runs x + y = 100.
        check("tri-rect: inside", hu::triangleOverlapsRect(0, 0, 100, 0, 0, 100, {10, 10, 20, 20}));
        check("tri-rect: past the hypotenuse", !hu::triangleOverlapsRect(0, 0, 100, 0, 0, 100, {60, 60, 80, 80}));
        check("tri-rect: across the hypotenuse", hu::triangleOverlapsRect(0, 0, 100, 0, 0, 100, {45, 45, 80, 80}));
        check("tri-rect: rect holds the triangle", hu::triangleOverlapsRect(10, 10, 12, 10, 10, 12, {0, 0, 50, 50}));
        check("tri-rect: apart", !hu::triangleOverlapsRect(0, 0, 100, 0, 0, 100, {120, 0, 140, 20}));
        struct V { struct { float x, y; } position; };
        const std::vector<V> tris = {{{0, 0}}, {{100, 0}}, {{0, 100}}};
        check("tris cover rect", hu::trianglesCoverRect(std::span<const V>(tris), {10, 10, 20, 20}) &&
                                 !hu::trianglesCoverRect(std::span<const V>(tris), {60, 60, 80, 80}));
    }
    check("fades out faster than back in", hu::kFadeOutMs >= 150 && hu::kFadeOutMs <= 250 &&
                                           hu::kFadeInMs >= 300 && hu::kFadeInMs <= 400);
    {
        constexpr uint8_t kOp = hu::fadeOpacity(hu::kDefaultFadePercent);
        hu::CoverFades f;
        const auto frame = [&](float ms, std::initializer_list<uint64_t> hits, std::initializer_list<uint64_t> view) {
            f.begin();
            for (uint64_t id : view) f.inView(id);
            for (uint64_t id : hits) f.cover(id);
            f.advance(ms);
        };
        check("fade: nothing fading", f.empty() && f.alpha(7, kOp) == 255);
        frame(16, {7, 7, 3}, {3, 7, 9});
        check("fade: covers over a unit start fading", f.size() == 2 && f.alpha(7, kOp) < 255 && f.alpha(7, kOp) > 200 &&
                                                       f.alpha(9, kOp) == 255);
        frame(hu::kFadeOutMs, {3, 7}, {3, 7, 9});
        check("fade: fully faded after the fade-out time", f.alpha(7, kOp) == kOp &&
                                                            f.alpha(3, kOp) == kOp);
        frame(hu::kFadeInMs * 0.5f, {3}, {3, 7, 9});
        check("fade: the unit gone, coming back", f.alpha(7, kOp) > kOp && f.alpha(7, kOp) < 255 &&
                                                   f.alpha(3, kOp) == kOp);
        frame(hu::kFadeInMs * 0.6f, {3}, {3, 7, 9});
        check("fade: fully back, dropped", f.alpha(7, kOp) == 255 && f.size() == 1);
        frame(16, {}, {7});
        check("fade: out of view, dropped", f.empty() && f.alpha(3, kOp) == 255);
        frame(16, {5}, {});
        frame(0, {5}, {});
        const uint8_t still = f.alpha(5, kOp);
        frame(0, {5}, {5});
        check("fade: no time, no change", f.alpha(5, kOp) == still);
        check("fade: 100% never fades", f.alpha(5, 255) == 255);
    }
    {
        // The sprite: 4x2 texels, only the top-right one opaque, drawn 40x20 at (100, 50).
        hu::AlphaMask m;
        uint8_t px[4 * 2 * 4] = {};
        px[3 * 4 + 3] = 255;
        m.build(px, 4, 2);
        check("rect: on the opaque texel", hu::spriteCoversRect(m, 100, 50, 40, 20, 0, {128, 52, 134, 58}));
        check("rect: only clear texels", !hu::spriteCoversRect(m, 100, 50, 40, 20, 0, {100, 50, 128, 70}));
        check("rect: outside the sprite", !hu::spriteCoversRect(m, 100, 50, 40, 20, 0, {150, 50, 200, 70}));
        check("rect: thinner than a step", hu::spriteCoversRect(m, 100, 50, 40, 20, 0, {135, 51, 135.5f, 51.5f}));
    }

    std::printf(g_fail ? "hidden_units_test: %d FAILURE(S)\n" : "hidden_units_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
