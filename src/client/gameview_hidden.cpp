#include "client/gameview.h"

// HIDDEN UNITS (Options > Graphics; a non-retail extra, default OUTLINE). After the
// body pass, units that a tree, a rock or a building painted over are shown anyway:
// their tactical dot on top (DOTS), or the covered part as a player-coloured outline
// (OUTLINE). The policy -- modes, painter layers, cover tests, screen bins -- is in
// client/hiddenunits.h; this file runs it against the frame's painter list.
//
// Who: the units the painter list holds (the frame draws them: own, allied, enemies
// in sight -- never fog, never an undrawn cloaked enemy), alive, not a corpse, not a
// structure, on the ground layer. Covers: tall features (the low layer is drawn
// before every unit) and structures not drawn through drawUnit (a site still being
// conjured is translucent), painted AFTER the unit. Another mobile unit is never a
// cover. Display only: nothing here reads or writes simulation state.

#include <algorithm>
#include <cmath>

namespace {

namespace hu = tak::hiddenunits;

// The OUTLINE pass's blend modes (GL needs one operation for colour and alpha).
// The coverage target holds, per pixel, the player colour of the eligible unit
// drawn there last and an alpha that says what happened to it since:
//   0      nothing (or something that is not a cover drew over it)
//   128    an eligible unit, still in sight
//   255    that unit, then a cover over it: HIDDEN
// cover:  rgb kept, a += a * src.a  -- turns 128 into 255 and leaves 0 alone, so a
//         cover only ever marks pixels a unit drew (also used to double alpha)
SDL_BlendMode coverBlend() {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
}
// erase: rgba *= 1 - src.a (a distant image that is no cover, a flyer: over everything)
SDL_BlendMode eraseBlend() {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
}
// lower: rgb kept, a -= src.a (128 -> 0, 255 -> 127; then doubled by cover)
SDL_BlendMode lowerBlend() {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT);
}
// erode: rgb kept, a *= src.a (shifted copies of the hidden part: its interior)
SDL_BlendMode erodeBlend() {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
}
// edge: rgb = src.rgb, a = src.a * (1 - dst.a) (the hidden part minus its interior)
SDL_BlendMode edgeBlend() {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD);
}

// SDL target changes reset viewport/scale/clip. Restore all of them on every exit.
struct SavedState {
    SDL_Renderer* r;
    SDL_Texture* target;
    SDL_Rect viewport{}, clip{};
    SDL_bool clipped;
    float sx = 1, sy = 1;
    SDL_BlendMode blend{};
    Uint8 red = 0, green = 0, blue = 0, alpha = 0;
    explicit SavedState(SDL_Renderer* renderer) : r(renderer), target(SDL_GetRenderTarget(r)) {
        SDL_RenderGetViewport(r, &viewport); clipped = SDL_RenderIsClipEnabled(r);
        SDL_RenderGetClipRect(r, &clip); SDL_RenderGetScale(r, &sx, &sy);
        SDL_GetRenderDrawBlendMode(r, &blend); SDL_GetRenderDrawColor(r, &red, &green, &blue, &alpha);
    }
    ~SavedState() {
        SDL_SetRenderTarget(r, target); SDL_RenderSetScale(r, sx, sy);
        SDL_RenderSetViewport(r, &viewport); SDL_RenderSetClipRect(r, clipped ? &clip : nullptr);
        SDL_SetRenderDrawBlendMode(r, blend); SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    }
};

}  // namespace

    void GameView::releaseHiddenTargets() {
        if (hiddenMask_) gpuvram::destroy(hiddenMask_);
        if (hiddenEdge_) gpuvram::destroy(hiddenEdge_);
        hiddenMask_ = hiddenEdge_ = nullptr;
        hiddenTexW_ = hiddenTexH_ = 0;
    }

    void GameView::drawHiddenUnits(int mvw, int winH) {
        hiddenDotIds_.clear();
#ifndef NDEBUG
        debugHiddenCandidates_ = debugHiddenOutlined_ = 0;
        debugHiddenOutlineRan_ = false;
#endif
#ifndef NDEBUG
        // Debug: TAK_HIDDEN_UNITS=off|dots|outline overrides the option (benchmarks).
        static const char* forced = tak::devEnv("TAK_HIDDEN_UNITS");
        if (forced) hiddenUnitsMode_ = hu::parse(forced);
#endif
        // Outline targets nobody used for two seconds go back to the GPU budget.
        if ((hiddenMask_ || hiddenEdge_) && SDL_GetTicks64() - hiddenUsedMs_ > 2000) releaseHiddenTargets();
        if (hiddenUnitsMode_ == hu::kOff || dotsFrame_) return;
        using RS = tak::render::Counter;
        tak::render::ScopedPhase phase(renderStats_, tak::render::Phase::HiddenUnits);
        const float zm = mapView_.zoom();
        const hu::Rect screen{0, 0, float(mvw), float(winH)};
        const auto& items = paintItems_;
        hiddenRecs_.clear();
        hiddenCoverRec_.clear();
        hiddenGrid_.reset(float(mvw), float(winH));
        size_t eligible = 0, candidates = 0;
        // Backwards through the painter list: when a unit is reached, every cover in
        // the grid paints after it. A rectangle is the unit's box over all headings
        // (unitHitBox) from its anchor, with room for animation past the rest pose;
        // the exact tests below decide.
        for (size_t k = items.size(); k-- > 0;) {
            const PaintItem& it = items[k];
            if (it.layer == hu::kLayerLowFeature) break;   // sorted first: never over a unit
            if (it.f) {
                const FeatureSprite sp = featureSprite(*it.f, zm);
                if (!sp.tex) continue;
                const hu::Rect r = hu::Rect{sp.dst.x + std::min(0.0f, sp.shear), sp.dst.y,
                    sp.dst.x + sp.dst.w + std::max(0.0f, sp.shear), sp.dst.y + sp.dst.h}.clipped(screen);
                if (r.empty()) continue;
                hiddenCoverRec_.push_back(uint32_t(hiddenRecs_.size()));
                hiddenGrid_.insert(r);
                hiddenRecs_.push_back({uint32_t(k), HiddenKind::Feature, false, -1, 0, r});
                continue;
            }
            const UnitR& u = *it.u;
            const int slot = geomSlot(u.id);
            if (slot < 0) continue;
            const UnitGeom& g = visGeom(size_t(slot));
            if (g.runs.empty() && !distantModelItems_[size_t(slot)].texture) continue;   // nothing drawn
            const SDL_FRect& hb = unitHitBox(u.type);
            constexpr float kPad = 4.0f;
            const hu::Rect r = hu::Rect{g.ax + hb.x * zm - kPad, g.ay + hb.y * zm - kPad,
                g.ax + (hb.x + hb.w) * zm + kPad, g.ay + (hb.y + hb.h) * zm + kPad}.clipped(screen);
            if (r.empty()) continue;
            HiddenKind kind = HiddenKind::Other;
            if (it.layer == hu::kLayerGround) {
                if (isStructure(u.type)) {
                    if (!specialUnit(u, g.canFly, g.occY, g.ay)) kind = HiddenKind::Structure;
                } else if (u.alive() && !u.corpsePhase) kind = HiddenKind::Unit;
            }
            HiddenRec rec{uint32_t(k), kind, false, slot, uint32_t(hiddenGrid_.size()), r};
            if (kind == HiddenKind::Structure) {
                hiddenCoverRec_.push_back(uint32_t(hiddenRecs_.size()));
                hiddenGrid_.insert(r);
            } else if (kind == HiddenKind::Unit) {
                ++eligible;
                hiddenGrid_.query(r, [&](uint32_t) { rec.candidate = true; return false; });
                candidates += rec.candidate;
            }
            hiddenRecs_.push_back(rec);
        }
        renderStats_.add(RS::HiddenEligible, eligible);
        renderStats_.add(RS::HiddenCandidates, candidates);
#ifndef NDEBUG
        debugHiddenCandidates_ = candidates;
#endif
        if (!candidates) return;   // no unit overlaps a later cover: nothing more to do
        // A unit drawn from its distant image this frame left its body pending;
        // the exact tests and the outline need the triangles.
        const auto body = [&](int slot) -> const UnitGeom& {
            if (visGeom(size_t(slot)).bodyDeferred) materializeDistantBody(size_t(slot));
            return visGeom(size_t(slot));
        };
        if (hiddenUnitsMode_ == hu::kOutline && !hiddenOutlineUnsupported_) {
            // The exact extent of the candidates' bodies: the area the pass works in.
            hu::Rect area;
            for (const HiddenRec& rec : hiddenRecs_) {
                if (!rec.candidate) continue;
                const UnitGeom& g = body(rec.slot);
                hu::Rect b{1e30f, 1e30f, -1e30f, -1e30f};
                for (const SDL_Vertex& v : g.verts) {
                    b.x0 = std::min(b.x0, v.position.x); b.y0 = std::min(b.y0, v.position.y);
                    b.x1 = std::max(b.x1, v.position.x); b.y1 = std::max(b.y1, v.position.y);
                }
                area.unite(b.clipped(screen));
            }
            if (area.empty() || drawHiddenOutlines(mvw, winH, area)) return;
            // This renderer cannot draw the outline: dots for this frame instead.
        }
        // DOTS: the unit's dot centre against the opaque pixels of every cover that
        // paints after it (feature sprite texels, structure triangles).
        const SDL_Color rim{12, 12, 16, 220};
        overlayBatch_.clear();
        for (size_t i = hiddenRecs_.size(); i-- > 0;) {   // forward: painter order
            const HiddenRec& rec = hiddenRecs_[i];
            if (!rec.candidate) continue;
            const UnitR& u = *items[rec.item].u;
            const SDL_FPoint c = dotCentre(u);
            if (!screen.contains(c.x, c.y)) continue;
            bool covered = false;
            hiddenGrid_.query(hu::Rect{c.x, c.y, c.x + 0.01f, c.y + 0.01f}, [&](uint32_t id) {
                if (id >= rec.coversAfter) return true;   // paints before the unit
                const HiddenRec& cov = hiddenRecs_[hiddenCoverRec_[id]];
                if (!cov.rect.contains(c.x, c.y)) return true;
                if (cov.kind == HiddenKind::Feature) {
                    const FeatureSprite sp = featureSprite(*items[cov.item].f, zm);
                    covered = sp.mask && hu::spriteCovers(*sp.mask, sp.dst.x, sp.dst.y, sp.dst.w, sp.dst.h,
                                                          sp.shear, c.x, c.y);
                } else {
                    covered = hu::trianglesCover(std::span<const SDL_Vertex>(body(cov.slot).verts), c.x, c.y);
                }
                return !covered;
            });
            if (!covered) continue;
            appendDot(overlayBatch_, u, c, rim);
            hiddenDotIds_.push_back(u.id);
        }
        std::sort(hiddenDotIds_.begin(), hiddenDotIds_.end());
        renderStats_.add(RS::HiddenMarked, hiddenDotIds_.size());
        if (!overlayBatch_.empty()) {
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(ren_, nullptr, overlayBatch_.data(), int(overlayBatch_.size()), nullptr, 0);
        }
    }

    // OUTLINE. Two off-screen layers the size of `area` (in output pixels):
    //  1. coverage (hiddenMask_): the painter list replayed over the area -- eligible
    //     units covered later as flat player colour at alpha 128 (replacing what was
    //     there), covers raising the alpha of a unit pixel to 255 (and of nothing to
    //     nothing), anything else erasing. Then alpha 128 -> 0 and 255 -> 255: the
    //     layer holds exactly the hidden part, in its unit's colour.
    //  2. edge (hiddenEdge_): the hidden part eroded by shifted copies of it, then
    //     the hidden part minus that: an inner edge, edgePixels wide.
    // Composited over the frame: the hidden part faintly, its edge solid. Nothing in
    // sight is touched, since only hidden pixels have any alpha.
    bool GameView::drawHiddenOutlines(int mvw, int winH, const tak::hiddenunits::Rect& area) {
        SDL_RendererInfo info{};
        if (SDL_GetRendererInfo(ren_, &info) != 0 || !(info.flags & SDL_RENDERER_ACCELERATED) ||
            !SDL_RenderTargetSupported(ren_)) {
            hiddenOutlineUnsupported_ = true;   // software renderer: no custom blending
            return false;
        }
        float sx = 1, sy = 1;
        SDL_RenderGetScale(ren_, &sx, &sy);
        SDL_Rect vp{};
        SDL_RenderGetViewport(ren_, &vp);
        if (!(sx > 0) || !(sy > 0) || !std::isfinite(sx) || !std::isfinite(sy) || vp.x != 0 || vp.y != 0)
            return false;
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(ren_, &ow, &oh);
        const int edge = hu::edgePixels(sx);
        const int margin = edge + 2;
        const int px0 = std::max(0, int(std::floor(area.x0 * sx)) - margin);
        const int py0 = std::max(0, int(std::floor(area.y0 * sy)) - margin);
        const int px1 = std::min({ow, int(std::ceil(float(mvw) * sx)), int(std::ceil(area.x1 * sx)) + margin});
        const int py1 = std::min({oh, int(std::ceil(float(winH) * sy)), int(std::ceil(area.y1 * sy)) + margin});
        const int pw = px1 - px0, ph = py1 - py0;
        if (pw <= 0 || ph <= 0) return true;   // nothing of it on screen
        // Targets: grown in 256-pixel steps, never shrunk while in use.
        if (pw > hiddenTexW_ || ph > hiddenTexH_) {
            const int w = std::max(hiddenTexW_, (pw + 255) / 256 * 256);
            const int h = std::max(hiddenTexH_, (ph + 255) / 256 * 256);
            if ((info.max_texture_width > 0 && w > info.max_texture_width) ||
                (info.max_texture_height > 0 && h > info.max_texture_height)) return false;
            releaseHiddenTargets();
            if (gpuAllocBlocked() || !gpuvram::wouldFit(size_t(w) * size_t(h) * 8)) return false;
            hiddenMask_ = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
            hiddenEdge_ = hiddenMask_ ? gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h)
                                      : nullptr;
            if (!hiddenMask_ || !hiddenEdge_) { noteGpuAllocFail(); releaseHiddenTargets(); return false; }
            SDL_SetTextureScaleMode(hiddenMask_, SDL_ScaleModeNearest);
            SDL_SetTextureScaleMode(hiddenEdge_, SDL_ScaleModeNearest);
            // Every mode this pass uses, or none of it (then DOTS for good).
            for (const SDL_BlendMode m : {coverBlend(), eraseBlend(), lowerBlend(), erodeBlend(), edgeBlend()})
                if (SDL_SetTextureBlendMode(hiddenMask_, m) != 0) {
                    releaseHiddenTargets();
                    hiddenOutlineUnsupported_ = true;
                    return false;
                }
            hiddenTexW_ = w; hiddenTexH_ = h;
        }
        hiddenUsedMs_ = SDL_GetTicks64();
        renderStats_.add(tak::render::Counter::HiddenPixels, uint64_t(pw) * uint64_t(ph));
        const float zm = mapView_.zoom();
        const hu::Rect logical{float(px0) / sx, float(py0) / sy, float(px1) / sx, float(py1) / sy};
        const SDL_Rect whole{0, 0, pw, ph};
        bool ok = true;
        size_t outlined = 0;
        {
            SavedState saved(ren_);
            // Nothing is drawn unless the target switch worked (never onto the frame).
            const auto target = [&](SDL_Texture* t) {
                if (SDL_SetRenderTarget(ren_, t) != 0) return false;
                SDL_RenderSetScale(ren_, 1, 1); SDL_RenderSetViewport(ren_, nullptr);
                SDL_RenderSetClipRect(ren_, &whole);
                return true;
            };
            const auto toTarget = [&](float x, float y) {
                return SDL_FPoint{x * sx - float(px0), y * sy - float(py0)};
            };
            // Screen vertices -> target pixel positions (hiddenXY_), drawn untextured in
            // one colour with the current draw blend mode.
            const auto flat = [&](const std::vector<SDL_Vertex>& verts, SDL_Color color) {
                if (verts.empty()) return;
                hiddenXY_.resize(verts.size() * 2);
                for (size_t i = 0; i < verts.size(); ++i) {
                    const SDL_FPoint p = toTarget(verts[i].position.x, verts[i].position.y);
                    hiddenXY_[2 * i] = p.x; hiddenXY_[2 * i + 1] = p.y;
                }
                tak::render::countSdlGeometry(verts.size(), verts.size() * sizeof(float) * 2);
                SDL_RenderGeometryRaw(ren_, nullptr, hiddenXY_.data(), 2 * sizeof(float), &color, 0,
                                      nullptr, 0, int(verts.size()), nullptr, 0, 0);
            };
            // A textured quad/triangle list in the given texture blend mode (the
            // texture's own mode is put back afterwards).
            const auto textured = [&](SDL_Texture* tex, const SDL_Vertex* verts, int count, const int* indices,
                                      int indexCount, SDL_BlendMode mode) {
                SDL_BlendMode was{};
                SDL_GetTextureBlendMode(tex, &was);
                SDL_SetTextureBlendMode(tex, mode);
                hiddenQuad_.assign(verts, verts + count);
                for (SDL_Vertex& v : hiddenQuad_) {
                    v.position = toTarget(v.position.x, v.position.y);
                    v.color = {255, 255, 255, 255};
                }
                SDL_RenderGeometry(ren_, tex, hiddenQuad_.data(), count, indices, indexCount);
                SDL_SetTextureBlendMode(tex, was);
            };
            ok = target(hiddenMask_);
            if (ok) {
                SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
                SDL_RenderFillRect(ren_, &whole);
                for (size_t i = hiddenRecs_.size(); i-- > 0;) {   // forward: painter order
                    const HiddenRec& rec = hiddenRecs_[i];
                    if (!rec.rect.overlaps(logical)) continue;
                    if (rec.kind == HiddenKind::Feature) {
                        const FeatureSprite sp = featureSprite(*paintItems_[rec.item].f, zm);
                        if (!sp.tex) continue;
                        const SDL_FRect& d = sp.dst;
                        const SDL_Color white{255, 255, 255, 255};
                        const SDL_Vertex q[4] = {{{d.x + sp.shear, d.y}, white, {0, 0}},
                            {{d.x + d.w + sp.shear, d.y}, white, {1, 0}},
                            {{d.x + d.w, d.y + d.h}, white, {1, 1}}, {{d.x, d.y + d.h}, white, {0, 1}}};
                        static const int idx[6] = {0, 1, 2, 0, 2, 3};
                        textured(sp.tex, q, 4, idx, 6, coverBlend());
                        continue;
                    }
                    const auto& distant = distantModelItems_[size_t(rec.slot)];
                    const bool unitCover = rec.kind == HiddenKind::Structure;
                    if (rec.kind == HiddenKind::Unit && rec.candidate) {
                        // The candidate's own pixels, replacing whatever was there.
                        const UnitGeom& g = visGeom(size_t(rec.slot));   // materialized above
                        const UnitR& u = *paintItems_[rec.item].u;
                        const SDL_Color pc = playerColor(u.player);
                        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
                        flat(g.verts, {pc.r, pc.g, pc.b, 128});
                        ++outlined;
                    } else if (distant.texture) {
                        // Drawn from its distant image: the image's own coverage.
                        textured(distant.texture, distant.quad.data(), int(distant.quad.size()), nullptr, 0,
                                 unitCover ? coverBlend() : eraseBlend());
                    } else {
                        SDL_SetRenderDrawBlendMode(ren_, unitCover ? coverBlend() : SDL_BLENDMODE_NONE);
                        flat(visGeom(size_t(rec.slot)).verts, unitCover ? SDL_Color{0, 0, 0, 255} : SDL_Color{0, 0, 0, 0});
                    }
                }
                // 128 (in sight) -> 0, 255 (hidden) -> 127 -> 254.
                SDL_SetRenderDrawBlendMode(ren_, lowerBlend());
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 128);
                SDL_RenderFillRect(ren_, &whole);
                SDL_SetRenderDrawBlendMode(ren_, coverBlend());
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
                SDL_RenderFillRect(ren_, &whole);
                ok = target(hiddenEdge_);
            }
            if (ok) {
                // Edge: the hidden part, eroded by `edge` pixels in eight directions,
                // subtracted from itself.
                SDL_SetTextureBlendMode(hiddenMask_, SDL_BLENDMODE_NONE);
                SDL_RenderCopy(ren_, hiddenMask_, &whole, &whole);
                SDL_SetTextureBlendMode(hiddenMask_, erodeBlend());
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy) continue;
                        const SDL_Rect shifted{dx * edge, dy * edge, pw, ph};
                        SDL_RenderCopy(ren_, hiddenMask_, &whole, &shifted);
                    }
                SDL_SetTextureBlendMode(hiddenMask_, edgeBlend());
                SDL_RenderCopy(ren_, hiddenMask_, &whole, &whole);
            }
        }   // the frame's target, scale, viewport, clip and draw state are back
        if (!ok) return false;
        renderStats_.add(tak::render::Counter::HiddenMarked, outlined);
        // Over the frame (world clip in force): the hidden part faint, its edge solid.
        const SDL_FRect dst{float(px0) / sx, float(py0) / sy, float(pw) / sx, float(ph) / sy};
        SDL_SetTextureBlendMode(hiddenMask_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(hiddenMask_, hu::kFillAlpha);
        SDL_RenderCopyF(ren_, hiddenMask_, &whole, &dst);
        SDL_SetTextureAlphaMod(hiddenMask_, 255);
        SDL_SetTextureBlendMode(hiddenEdge_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(hiddenEdge_, hu::kEdgeAlpha);
        SDL_RenderCopyF(ren_, hiddenEdge_, &whole, &dst);
        SDL_SetTextureAlphaMod(hiddenEdge_, 255);
#ifndef NDEBUG
        debugHiddenOutlineRan_ = true;
        debugHiddenOutlined_ = outlined;
#endif
        return true;
    }
