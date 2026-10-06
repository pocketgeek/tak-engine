// Zoom smoothing (src/client/zoomsmooth.h): the sharp-bilinear plans, the zoomed-out
// terrain level choice, and -- on an accelerated renderer (--gpu) -- the actual
// sharp terrain target: crisp texel interiors with a ~1 pixel blended boundary,
// against NEAREST (no blend) and plain LINEAR (a zoom-wide ramp).
#include "client/zoomsmooth.h"
#include <SDL.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
using namespace tak;
static void check(bool b, const std::string& text) { if (!b) throw std::runtime_error(text); }

int main(int argc, char** argv) try {
    const bool gpu = argc > 1 && std::string(argv[1]) == "--gpu";
    // Integer prescale: round(zoom), never more than 4/3 of the drawable per axis.
    check(sharpFactor(1.0f) == 1 && sharpFactor(1.4f) == 1, "k below 1.5");
    check(sharpFactor(1.5f) == 2 && sharpFactor(2.0f) == 2 && sharpFactor(2.49f) == 2, "k around 2");
    check(sharpFactor(2.5f) == 3 && sharpFactor(4.0f) == 4, "k around 3/4");
    check(sharpFactor(1.6f, 1.f) == 1 && sharpFactor(2.4f, 1.f) == 2, "k = floor with a 1.0 cap");
    for (float z = 1.01f; z <= 4.0f; z += 0.01f) {
        const int k = sharpFactor(z);
        check(k >= 1 && double(k) / z <= 4.0 / 3.0 + 1e-3, "prescale exceeds the target cap at " + std::to_string(z));
        if (k >= 2) check(z / k >= 0.74 && z / k <= 1.26, "bilinear step not near 1:1 at " + std::to_string(z));
    }
    // Target plans: one allocation for every zoom, with a 1.0-cap fallback.
    auto p = sharpPlan(7680, 2160, 16384, 16384, ZoomSharpTarget::maxBytes);
    check(p.cap > 1.3f && p.w == 10242 && p.h == 2882 && p.bytes < (size_t(113) << 20), "7680x2160 sharp plan");
    p = sharpPlan(7680, 2160, 16384, 16384, size_t(70) << 20);
    check(p.cap == 1.f && p.w == 7682 && p.h == 2162, "budget fallback to cap 1.0");
    check(sharpPlan(7680, 2160, 8192, 8192, ZoomSharpTarget::maxBytes).cap == 1.f, "dimension fallback");
    check(sharpPlan(7680, 2160, 16384, 16384, 1 << 20).w == 0, "tiny budget");
    // Snapped view: texel edges land on integer target pixels, and the screen rect
    // reproduces the camera exactly (world x -> (x - offX) * zoom).
    for (float zoom : {1.5f, 2.f, 2.37f, 3.f, 4.f}) for (float off : {0.f, 10.3f, 77.77f}) {
        const int k = sharpFactor(zoom);
        const auto v = sharpView(zoom, off, off * 0.5f, 1000, 700, k, 4000, 4000);
        const double edge = (std::floor(off) + 3 - v.offX) * k;   // a texel edge in target px
        check(std::abs(edge - std::round(edge)) < 1e-3, "texel edge off the target grid");
        const double screen = v.dest.x + edge * v.dest.w / v.usedW;
        check(std::abs(screen - (std::floor(off) + 3 - off) * zoom) < 1e-2, "sharp view moves the camera");
        check(v.dest.x <= 0 && v.dest.x + v.dest.w >= 1000, "sharp view does not cover the view");
    }
    // Zoomed-out terrain Auto: 4x while its target stays within 128 MiB, else 2x.
    check(zoomOutTerrainSamples(kZoomOutAuto, 1920, 1080) == 4, "auto 1080p");
    check(zoomOutTerrainSamples(kZoomOutAuto, 3840, 2160) == 4, "auto 4K");
    check(zoomOutTerrainSamples(kZoomOutAuto, 7680, 2160) == 2, "auto 7680x2160");
    check(zoomOutTerrainSamples(kZoomOutOff, 640, 480) == 0 && zoomOutTerrainSamples(kZoomOut4x, 7680, 4320) == 4 &&
          zoomOutTerrainSamples(kZoomOut2x, 640, 480) == 2, "explicit zoomed-out levels");
    check(!zoomedIn(1.f) && !zoomedOut(1.f) && zoomedIn(1.01f) && zoomedOut(0.99f), "1.0 runs no pass");
    check(zoomOutFromLegacyTerrainAA(0) == kZoomOutAuto && zoomOutFromLegacyTerrainAA(2) == kZoomOut2x &&
          zoomOutFromLegacyTerrainAA(4) == kZoomOut4x && zoomOutFromLegacyTerrainAA(3) == kZoomOut2x, "legacy terrain AA");
    check(parseZoomSmoothing("smooth", 2) == kZoomSmooth && parseZoomSmoothing("bogus", 2) == 2 &&
          parseZoomOutTerrain("4", 0) == kZoomOut4x && parseZoomOutTerrain("8", 0) == kZoomOutAuto, "parsers");

    SDL_SetMainReady(); check(SDL_Init(SDL_INIT_VIDEO) == 0, SDL_GetError());
    const int W = 320, H = 200;
    auto* window = SDL_CreateWindow("zoom smoothing test", 0, 0, W, H, SDL_WINDOW_HIDDEN);
    check(window, SDL_GetError());
    auto* r = SDL_CreateRenderer(window, -1, gpu ? SDL_RENDERER_ACCELERATED : SDL_RENDERER_SOFTWARE);
    check(r, SDL_GetError()); SDL_RenderSetVSync(r, 0);
    // A 64x64 checker of 1-texel cells, black/white: every texel edge is a boundary.
    std::vector<unsigned char> px(64 * 64 * 4);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
        const unsigned char c = ((x + y) & 1) ? 255 : 0;
        unsigned char* q = &px[(y * 64 + x) * 4]; q[0] = q[1] = q[2] = c; q[3] = 255;
    }
    auto* art = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 64, 64);
    SDL_UpdateTexture(art, nullptr, px.data(), 64 * 4);
    ZoomSharpTarget sharp;
    auto drawArt = [&](float zoom, float ox, float oy, SDL_ScaleMode mode) {
        SDL_SetTextureScaleMode(art, mode);
        const SDL_FRect d{-ox * zoom, -oy * zoom, 64 * zoom, 64 * zoom};
        SDL_RenderCopyF(r, art, nullptr, &d);
    };
    // Classify one readback row: blended pixels per texel boundary, and interior errors.
    auto measure = [&](float zoom, float off, int mode) {
        SDL_SetRenderDrawColor(r, 18, 18, 26, 255); SDL_RenderClear(r);
        if (mode == 2) {
            sharp.configure(r, W, H, true);
            check(sharp.render(r, zoom, off, off, W, H, [&](int k, float x, float y, int, int) {
                drawArt(float(k), x, y, SDL_ScaleModeNearest); }), "sharp target did not render");
        } else drawArt(zoom, off, off, mode ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        std::vector<unsigned char> rb(W * H * 4);
        SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, rb.data(), W * 4);
        // A row through the middle of a texel row, so only vertical edges are crossed.
        int y = 10; double bestDy = -1;
        for (int row = 10; row < 150; ++row) {
            const double t = (row + 0.5) / zoom + off, d = std::abs(t - std::round(t)) * zoom;
            if (d > bestDy) { bestDy = d; y = row; }
        }
        int blended = 0, interiorBad = 0, cols = 0;
        for (int x = 0; x < W; ++x) {
            const double wx = x + 0.5, tx = wx / zoom + off;
            const double wy = y + 0.5, ty = wy / zoom + off;
            if (tx >= 63 || ty >= 63) continue;
            ++cols;
            const int v = rb[(y * W + x) * 4];
            const double dx = std::abs(tx - std::round(tx)) * zoom, dy = std::abs(ty - std::round(ty)) * zoom;
            if (v > 8 && v < 247) ++blended;
            // Farther than one output pixel from any texel edge: must be the texel's colour.
            if (dx > 1.0 && dy > 1.0) {
                const int want = ((int(std::floor(tx)) + int(std::floor(ty))) & 1) ? 255 : 0;
                if (std::abs(v - want) > 3) ++interiorBad;
            }
        }
        const double boundaries = double(cols) / zoom;
        return std::pair<double, int>{blended / std::max(1.0, boundaries), interiorBad};
    };
    if (!gpu) {
        sharp.configure(r, W, H, true);
        check(!sharp.ready() && sharp.bytes() == 0, "software renderer must not get a sharp target");
        check(!sharp.render(r, 2.f, 0, 0, W, H, [](int, float, float, int, int) {}), "software render must fall back");
        std::puts("PASS zoom smoothing plans (software: sharp target falls back)");
        return 0;
    }
    for (float zoom : {2.0f, 2.6f, 3.3f, 4.0f}) for (float off : {0.f, 0.37f}) {
        const auto nearest = measure(zoom, off, 0), linear = measure(zoom, off, 1), sh = measure(zoom, off, 2);
        std::printf("zoom %.2f off %.2f blended px per texel edge: nearest %.2f linear %.2f sharp %.2f; sharp interior errors %d\n",
                    zoom, off, nearest.first, linear.first, sh.first, sh.second);
        check(sh.second == 0, "sharp texel interiors are not crisp");
        check(sh.first <= 2.05, "sharp boundary wider than about one pixel");
        check(linear.first > sh.first + 0.4 || zoom < 2.1f, "linear should blend more than sharp");
        if (off != 0.f || std::abs(zoom - std::round(zoom)) > 1e-3) check(sh.first > 0.3, "sharp boundaries are not antialiased");
    }
    // Hold, release and VRAM accounting.
    const size_t held = sharp.bytes();
    check(held > 0 && gpuvram::bytes() >= held, "sharp target not accounted");
    const uint64_t t0 = SDL_GetTicks64();
    sharp.configure(r, W, H, false, t0 + 1000);
    check(sharp.ready(), "target released inside the hold window");
    sharp.configure(r, W, H, false, t0 + 10000);
    check(!sharp.ready() && sharp.bytes() == 0, "target not released after the hold window");
    const auto allocations = sharp.allocations;
    gpuvram::g_backoffUntil = SDL_GetTicks() + 1000;
    sharp.configure(r, W, H, true, t0 + 20000);
    check(!sharp.ready() && sharp.allocations == allocations, "allocated during gpuvram backoff");
    gpuvram::g_backoffUntil = 0;
    sharp.clear();
    SDL_DestroyTexture(art); SDL_DestroyRenderer(r); SDL_DestroyWindow(window); SDL_Quit();
    std::puts("PASS zoom smoothing plans and accelerated sharp-bilinear terrain target");
    return 0;
} catch (const std::exception& e) { std::fprintf(stderr, "FAIL %s\n", e.what()); return 1; }
