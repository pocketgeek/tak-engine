// hidden_units_test -- the HIDDEN UNITS policy (client/hiddenunits.h).
//
// The option's persisted names, the painter layer that puts flat features (TDF height
// below 10) under every unit, and the pieces the DOTS test is built from: the sprite
// opacity mask with the sway shear, the model triangle test and the screen bins the
// frame walks backwards. The draw itself (cover order, fog, outline pixels) is checked
// through the real renderer by the hidden_units harness test (TAK_HIDDEN_UNITS_TEST).

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
    check("default is OUTLINE", hu::kDefault == hu::kOutline);
    check("names round trip", hu::parse(hu::name(hu::kOff)) == hu::kOff &&
                              hu::parse(hu::name(hu::kDots)) == hu::kDots &&
                              hu::parse(hu::name(hu::kOutline)) == hu::kOutline);
    check("unknown value reads as the default", hu::parse("") == hu::kDefault && hu::parse("halo") == hu::kDefault);

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

    // ---- the outline's edge ----
    check("edge 2 px at 100%, wider on a scaled output", hu::edgePixels(1.0f) == 2 && hu::edgePixels(2.0f) == 4 &&
                                                          hu::edgePixels(0.1f) >= 1 && hu::edgePixels(100.0f) <= 6);

    std::printf(g_fail ? "hidden_units_test: %d FAILURE(S)\n" : "hidden_units_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
