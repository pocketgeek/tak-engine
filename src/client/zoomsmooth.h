#pragma once
// Zoom-dependent world filtering, separate from edge antialiasing.
//
//   * Zoom smoothing (Off / Smooth / Sharp) decides how MAGNIFIED art is resampled
//     when the camera is zoomed in: the terrain mosaic, bitmap scenery and 3DO model
//     textures. Smooth is plain bilinear -- retail's VisualOptions "Filtering"
//     (registry BiLinearFilter), which on the Glide path set grTexFilterMode for every
//     textured draw. Sharp is pixel-art-aware "sharp bilinear": art is first scaled by
//     an INTEGER factor with nearest sampling (crisp k x k texel blocks), then
//     bilinear-resampled to the real zoom, so only the one-pixel boundary between
//     texels is blended.
//   * Zoomed-out terrain supersampling: the existing terrain SelectiveAA pass, now
//     admitted only while zoom < 1 where minification shimmer exists. At zoom 1.0 the
//     flat tile mosaic is an exact integer copy and supersampling is a pixel-identical
//     no-op, so no target is held there.
//
// Everything here is client display state; nothing reaches the simulation.
#include "client/gpuvram.h"
#include "client/selectiveaa.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace tak {

enum ZoomSmoothing : int { kZoomOff = 0, kZoomSmooth = 1, kZoomSharp = 2 };
// Zoomed-out terrain choice. Auto picks the best level whose target stays modest.
enum ZoomOutTerrain : int { kZoomOutAuto = 0, kZoomOutOff = 1, kZoomOut2x = 2, kZoomOut4x = 4 };

inline const char* zoomSmoothingName(int mode) {
    return mode == kZoomOff ? "off" : mode == kZoomSmooth ? "smooth" : "sharp";
}
// Parses a saved value; anything unknown returns `fallback`.
inline int parseZoomSmoothing(const std::string& v, int fallback) {
    if (v == "off" || v == "0") return kZoomOff;
    if (v == "smooth" || v == "1") return kZoomSmooth;
    if (v == "sharp" || v == "2") return kZoomSharp;
    return fallback;
}
inline const char* zoomOutTerrainName(int choice) {
    return choice == kZoomOutOff ? "off" : choice == kZoomOut2x ? "2" : choice == kZoomOut4x ? "4" : "auto";
}
inline int parseZoomOutTerrain(const std::string& v, int fallback) {
    if (v == "auto") return kZoomOutAuto;
    if (v == "off" || v == "0") return kZoomOutOff;
    if (v == "2") return kZoomOut2x;
    if (v == "4") return kZoomOut4x;
    return fallback;
}
// The old Terrain AA slider (0/2/4) as a zoomed-out choice. Its Off was the shipped
// default rather than a decision, so it becomes Auto, which costs nothing at zoom >= 1.
inline int zoomOutFromLegacyTerrainAA(int samples) {
    const int s = aaStep(samples, 4);
    return s == 4 ? kZoomOut4x : s == 2 ? kZoomOut2x : kZoomOutAuto;
}

// Auto's ceiling: the highest supported level whose drawable-sized target fits.
// 1920x1080 and 3840x2160 get 4x; 7680x2160 gets 2x (126.56 MiB) instead of 4x
// (253.13 MiB) -- an 8 GB card at that mode is the one that starved the compositor.
inline constexpr size_t kZoomOutAutoBytes = 128u << 20;

// Supersample level requested for the zoomed-out terrain pass at this drawable size.
inline int zoomOutTerrainSamples(int choice, int w, int h) {
    switch (choice) {
        case kZoomOutOff: return 0;
        case kZoomOut2x: return 2;
        case kZoomOut4x: return 4;
        default: {
            for (int n : {4, 2}) {
                const double s = std::sqrt(double(n));
                const uint64_t bytes = uint64_t(std::ceil(w * s)) * uint64_t(std::ceil(h * s)) * 4;
                if (bytes <= kZoomOutAutoBytes) return n;
            }
            return 0;
        }
    }
}

// Zoom regimes. Exactly 1.0 (the retail 1:1 view) is neither: no pass runs there.
inline bool zoomedIn(float zoom) { return zoom > 1.0005f; }
inline bool zoomedOut(float zoom) { return zoom < 0.9995f; }

// Integer prescale for sharp bilinear. round(zoom) keeps the bilinear step close to
// 1:1 (between 0.67x and 1.5x of an output pixel), so texel boundaries blend over
// about one pixel at every zoom. `cap` bounds k/zoom: the intermediate target is at
// most `cap` times the drawable per axis. k < 2 means "no prescale".
inline int sharpFactor(float zoom, float cap = 4.f / 3.f) {
    if (!(zoom > 0)) return 1;
    int k = int(std::lround(zoom));
    k = std::min(k, int(std::floor(double(zoom) * cap + 1e-4)));
    return std::max(1, k);
}

struct SharpPlan {
    int w = 0, h = 0;
    float cap = 0;
    size_t bytes = 0;
    bool operator==(const SharpPlan&) const = default;
};
// The target is sized once for the largest k/zoom ratio so continuous wheel zooming
// never reallocates. 4/3 is the worst case of round(); 1.0 (k = floor(zoom)) is the
// fallback when that does not fit.
inline SharpPlan sharpPlan(int w, int h, int limitW, int limitH, size_t budget) {
    if (w <= 0 || h <= 0) return {};
    for (int third : {4, 3}) {   // cap = third/3, in exact integer arithmetic
        const int tw = (w * third + 2) / 3 + 2, th = (h * third + 2) / 3 + 2;
        const size_t bytes = size_t(tw) * size_t(th) * 4;
        if (tw <= limitW && th <= limitH && bytes <= budget) return {tw, th, third / 3.f, bytes};
    }
    return {};
}

// Where the terrain is drawn inside the sharp target and how the target maps back to
// the screen. Texel edges land on integer target pixels; the sub-pixel camera
// remainder moves into the final bilinear copy, so panning stays smooth.
struct SharpView {
    int k = 1;
    float offX = 0, offY = 0;   // snapped world offset used inside the target
    int usedW = 0, usedH = 0;   // target pixels drawn
    SDL_FRect dest{};           // screen rect of the used target region
};
inline SharpView sharpView(float zoom, float offX, float offY, int w, int h, int k, int targetW, int targetH) {
    SharpView v;
    v.k = k;
    const double fx = double(offX) * k, fy = double(offY) * k;
    const double sx = std::floor(fx), sy = std::floor(fy);
    v.offX = float(sx / k);
    v.offY = float(sy / k);
    v.usedW = std::min(targetW, int(std::ceil(double(w) * k / zoom)) + 2);
    v.usedH = std::min(targetH, int(std::ceil(double(h) * k / zoom)) + 2);
    const double scale = double(zoom) / k;
    v.dest = {float(-(fx - sx) * scale), float(-(fy - sy) * scale),
              float(v.usedW * scale), float(v.usedH * scale)};
    return v;
}

// One reusable, opaque terrain target for Sharp zoom smoothing. Allocated only while
// zoomed in (k >= 2) and released three seconds after it was last needed, so crossing
// a zoom threshold back and forth does not churn VRAM. Allocation failure and the
// shared gpuvram backoff both fall back to plain bilinear for that frame.
class ZoomSharpTarget {
    SDL_Texture* tex_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SharpPlan plan_;
    int width_ = 0, height_ = 0;
    uint64_t lastWanted_ = 0, retryAt_ = 0;
    std::string reason_;
public:
    static constexpr size_t maxBytes = 256u << 20;
    static constexpr uint64_t holdMs = 3000;
    ZoomSharpTarget() = default;
    ZoomSharpTarget(const ZoomSharpTarget&) = delete;
    ZoomSharpTarget& operator=(const ZoomSharpTarget&) = delete;
    ~ZoomSharpTarget() { clear(); }
    size_t bytes() const { return plan_.bytes; }
    float cap() const { return tex_ ? plan_.cap : 0.f; }
    bool ready() const { return tex_ != nullptr; }
    const std::string& reason() const { return reason_; }
    uint64_t allocations = 0;
    void clear() {
        gpuvram::destroy(tex_);
        tex_ = nullptr; plan_ = {}; renderer_ = nullptr; retryAt_ = 0;
    }
    // Call once per frame before render(). `wanted` = Sharp mode and k >= 2 now.
    void configure(SDL_Renderer* r, int w, int h, bool wanted, uint64_t now = SDL_GetTicks64()) {
        if (renderer_ != r || width_ != w || height_ != h) {
            clear(); renderer_ = r; width_ = w; height_ = h;
        }
        if (wanted) lastWanted_ = now;
        if (!wanted) {
            if (tex_ && now - lastWanted_ > holdMs) clear(), renderer_ = r;
            return;
        }
        if (tex_ || now < retryAt_) return;
        retryAt_ = now + 3000;
        if (gpuvram::blocked()) { reason_ = "GPU allocation backoff"; return; }
        SDL_RendererInfo info{};
        if (SDL_GetRendererInfo(r, &info) != 0) { reason_ = "renderer information unavailable"; return; }
        if (!(info.flags & SDL_RENDERER_ACCELERATED) || !SDL_RenderTargetSupported(r)) {
            reason_ = "render targets unavailable"; retryAt_ = UINT64_MAX; return;
        }
        const size_t available = gpuvram::cap() - std::min(gpuvram::bytes(), gpuvram::cap());
        const size_t budget = std::min({maxBytes, gpuvram::cap() / 2, available});
        auto plan = sharpPlan(w, h, info.max_texture_width, info.max_texture_height, budget);
        if (!plan.w) { reason_ = "texture-memory budget"; return; }
        tex_ = gpuvram::create(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, plan.w, plan.h);
        ++allocations;
        if (!tex_ || SDL_SetTextureScaleMode(tex_, SDL_ScaleModeLinear) != 0 ||
            SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_NONE) != 0) {
            reason_ = std::string("render-target allocation failed: ") + SDL_GetError();
            if (!tex_) gpuvram::noteFail();
            gpuvram::destroy(tex_); tex_ = nullptr;
            return;
        }
        plan_ = plan; reason_.clear();
    }
    // Draws the terrain through the target. `draw(k, offX, offY, w, h)` must render
    // the terrain at integer zoom k with that world offset into a w x h surface.
    // Returns false (nothing drawn) when the caller should draw natively instead.
    template <class F> bool render(SDL_Renderer* r, float zoom, float offX, float offY, int w, int h, F draw) {
        if (!tex_ || !zoomedIn(zoom)) return false;
        const int k = sharpFactor(zoom, plan_.cap);
        if (k < 2) return false;
        SDL_Texture* previous = SDL_GetRenderTarget(r);
        float sx, sy; SDL_RenderGetScale(r, &sx, &sy);
        if (previous || sx != 1 || sy != 1) return false;
        const SharpView v = sharpView(zoom, offX, offY, w, h, k, plan_.w, plan_.h);
        SDL_Rect viewport, clip; SDL_RenderGetViewport(r, &viewport); SDL_RenderGetClipRect(r, &clip);
        const bool clipped = SDL_RenderIsClipEnabled(r);
        SDL_BlendMode blend; Uint8 cr, cg, cb, ca;
        SDL_GetRenderDrawBlendMode(r, &blend); SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
        auto restore = [&] {
            SDL_SetRenderTarget(r, previous);
            SDL_RenderSetViewport(r, &viewport);
            SDL_RenderSetClipRect(r, clipped ? &clip : nullptr);
            SDL_SetRenderDrawBlendMode(r, blend); SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
        };
        if (SDL_SetRenderTarget(r, tex_) != 0) { restore(); return false; }
        SDL_RenderSetViewport(r, nullptr); SDL_RenderSetClipRect(r, nullptr);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 18, 18, 26, 255);
        const SDL_Rect used{0, 0, v.usedW, v.usedH};
        SDL_RenderFillRect(r, &used);
        SDL_SetRenderDrawBlendMode(r, blend); SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
        SDL_RenderSetClipRect(r, &used);
        try { draw(k, v.offX, v.offY, v.usedW, v.usedH); } catch (...) { restore(); throw; }
        restore();
        if (SDL_RenderCopyF(r, tex_, &used, &v.dest) != 0) return false;
        lastK = k;
        return true;
    }
    int lastK = 0;
};

}  // namespace tak
