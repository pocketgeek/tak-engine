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
        if (hiddenWhite_) gpuvram::destroy(hiddenWhite_);
        hiddenMask_ = hiddenEdge_ = hiddenWhite_ = nullptr;
        hiddenTexW_ = hiddenTexH_ = 0;
    }

    tak::hiddenunits::Rect GameView::hiddenBodyBounds(int slot) {
        const auto& distant = distantModelItems_[size_t(slot)];
        hu::Rect b{1e30f, 1e30f, -1e30f, -1e30f};
        const auto add = [&](const SDL_FPoint& p) {
            b.x0 = std::min(b.x0, p.x); b.y0 = std::min(b.y0, p.y);
            b.x1 = std::max(b.x1, p.x); b.y1 = std::max(b.y1, p.y);
        };
        if (distant.texture) {   // drawn from its image this frame
            for (const SDL_Vertex& v : distant.quad) add(v.position);
            return b;
        }
        UnitGeom& g = visGeom(size_t(slot));
        if (g.bodyDeferred) materializeDistantBody(size_t(slot));   // not expected: no image, no body
        if (g.boundsRevision != g.revision) {
            for (const SDL_Vertex& v : g.verts) add(v.position);
            g.bodyBounds = {b.x0, b.y0, b.x1, b.y1};
            g.boundsRevision = g.revision;
        }
        return {g.bodyBounds[0], g.bodyBounds[1], g.bodyBounds[2], g.bodyBounds[3]};
    }

    void GameView::drawHiddenUnits(int mvw, int winH) {
        hiddenDotIds_.clear();
#ifndef NDEBUG
        debugHiddenCandidates_ = debugHiddenOutlined_ = 0;
        debugHiddenOutlineRan_ = false;
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
        // the grid paints after it. A unit is looked at closely only when its model's
        // box over all headings (unitHitBox, from its anchor, with room for animation
        // past the rest pose) reaches a cover; then its exact body bounds must overlap
        // a cover's (a sprite's rectangle, a structure's body bounds) to make it a
        // candidate. The covers it overlaps are marked for the OUTLINE replay.
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
            HiddenKind kind = HiddenKind::Other;
            if (it.layer == hu::kLayerGround) {
                if (isStructure(u.type)) {
                    if (!specialUnit(u, g.canFly, g.occY, g.ay)) kind = HiddenKind::Structure;
                } else if (u.alive() && !u.corpsePhase) kind = HiddenKind::Unit;
            }
            if (kind == HiddenKind::Structure) {
                const hu::Rect r = hiddenBodyBounds(slot).clipped(screen);
                if (r.empty()) continue;
                hiddenCoverRec_.push_back(uint32_t(hiddenRecs_.size()));
                hiddenGrid_.insert(r);
                hiddenRecs_.push_back({uint32_t(k), kind, false, slot, 0, r});
                continue;
            }
            const SDL_FRect& hb = unitHitBox(u.type);
            constexpr float kPad = 4.0f;
            const hu::Rect box = hu::Rect{g.ax + hb.x * zm - kPad, g.ay + hb.y * zm - kPad,
                g.ax + (hb.x + hb.w) * zm + kPad, g.ay + (hb.y + hb.h) * zm + kPad}.clipped(screen);
            if (box.empty()) continue;
            HiddenRec rec{uint32_t(k), kind, false, slot, uint32_t(hiddenGrid_.size()), box};
            if (kind == HiddenKind::Unit) {
                ++eligible;
                bool near = false;
                hiddenGrid_.query(box, [&](uint32_t) { near = true; return false; });
                if (near) {
                    const hu::Rect body = hiddenBodyBounds(slot).clipped(screen);
                    hiddenGrid_.query(body, [&](uint32_t id) {
                        HiddenRec& cov = hiddenRecs_[hiddenCoverRec_[id]];
                        if (cov.rect.overlaps(body)) { rec.candidate = true; cov.replay = true; }
                        return true;
                    });
                    if (rec.candidate) { rec.rect = body; ++candidates; }
                }
            }
            hiddenRecs_.push_back(rec);
        }
        renderStats_.add(RS::HiddenEligible, eligible);
        renderStats_.add(RS::HiddenCandidates, candidates);
#ifndef NDEBUG
        debugHiddenCandidates_ = candidates;
#endif
        if (!candidates) return;   // no unit overlaps a later cover: nothing more to do
        if (hiddenUnitsMode_ == hu::kOutline && !hiddenOutlineUnsupported_) {
            // Forward: what is drawn over a candidate without covering it (another
            // unit, a flyer, a corpse) must erase it from the coverage layer.
            hu::Rect area;
            hiddenCandGrid_.reset(float(mvw), float(winH));
            hiddenCandRec_.clear();
            for (size_t i = hiddenRecs_.size(); i-- > 0;) {
                HiddenRec& rec = hiddenRecs_[i];
                if (rec.candidate) {
                    area.unite(rec.rect);
                    hiddenCandRec_.push_back(uint32_t(i));
                    hiddenCandGrid_.insert(rec.rect);
                } else if (rec.kind == HiddenKind::Unit || rec.kind == HiddenKind::Other) {
                    hiddenCandGrid_.query(rec.rect, [&](uint32_t id) {
                        if (hiddenRecs_[hiddenCandRec_[id]].rect.overlaps(rec.rect)) { rec.replay = true; return false; }
                        return true;
                    });
                }
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
                    if (visGeom(size_t(cov.slot)).bodyDeferred) materializeDistantBody(size_t(cov.slot));
                    covered = hu::trianglesCover(std::span<const SDL_Vertex>(visGeom(size_t(cov.slot)).verts),
                                                 c.x, c.y);
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
    //  1. coverage (hiddenMask_): the painter list replayed over the area -- each
    //     candidate as flat player colour at alpha 128 (replacing what was there),
    //     the covers over a candidate raising the alpha of a unit pixel to 255 (and
    //     of nothing to nothing), anything else drawn over a candidate erasing. Then
    //     alpha 128 -> 0 and 255 -> 255: the layer holds exactly the hidden part, in
    //     its unit's colour.
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
            // ---- the replay, in layers (see HiddenDraw) ----
            const SDL_BlendMode blends[3] = {SDL_BLENDMODE_NONE, coverBlend(), eraseBlend()};
            hiddenDraws_.clear();
            hiddenLayerGrid_.reset(float(mvw), float(winH));
            for (size_t i = hiddenRecs_.size(); i-- > 0;) {   // forward: painter order
                HiddenRec& rec = hiddenRecs_[i];
                if (!(rec.candidate || rec.replay) || !rec.rect.overlaps(logical)) continue;
                HiddenDraw d{uint32_t(i), 0, nullptr, 0, rec.rect};
                if (rec.kind == HiddenKind::Feature) {
                    d.tex = featureSprite(*paintItems_[rec.item].f, zm).tex;
                    if (!d.tex) continue;
                    d.blend = 1;
                } else {
                    if (rec.candidate) {
                        UnitGeom& g = visGeom(size_t(rec.slot));   // its body, never its image
                        if (g.bodyDeferred) materializeDistantBody(size_t(rec.slot));
                    } else {
                        d.tex = distantModelItems_[size_t(rec.slot)].texture;   // null: its body
                        d.blend = rec.kind == HiddenKind::Structure ? 1 : d.tex ? 2 : 0;
                        d.rect = hiddenBodyBounds(rec.slot);   // exact: it is ordered by it
                    }
                }
                hiddenLayerGrid_.query(d.rect, [&](uint32_t id) {
                    const HiddenDraw& o = hiddenDraws_[id];
                    if (o.rect.overlaps(d.rect))
                        d.layer = std::max(d.layer, o.layer + (o.tex != d.tex || o.blend != d.blend ? 1u : 0u));
                    return true;
                });
                hiddenLayerGrid_.insert(d.rect);
                hiddenDraws_.push_back(d);
            }
            std::stable_sort(hiddenDraws_.begin(), hiddenDraws_.end(), [](const HiddenDraw& x, const HiddenDraw& y) {
                if (x.layer != y.layer) return x.layer < y.layer;
                if (x.tex != y.tex) return std::less<SDL_Texture*>()(x.tex, y.tex);
                return x.blend < y.blend;
            });
            // Native flat runs: OpenGL at output scale 1 (debug TAK_HIDDEN_NATIVE=0: SDL only).
            static const bool nativeOff = tak::devEnv("TAK_HIDDEN_NATIVE") && !tak::devFlag("TAK_HIDDEN_NATIVE");
            const bool native = !nativeOff && sx == 1 && sy == 1 && hiddenSubmit_.nativeCapable(ren_) && [&] {
                if (hiddenWhite_) return true;
                hiddenWhite_ = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 1, 1);
                const uint32_t white = 0xffffffffu;
                return hiddenWhite_ && SDL_UpdateTexture(hiddenWhite_, nullptr, &white, 4) == 0;
            }();
            const auto put = [&](const SDL_FPoint& p, SDL_Color color) {
                hiddenXY_.push_back(p.x * sx - float(px0));
                hiddenXY_.push_back(p.y * sy - float(py0));
                hiddenColor_.push_back(color);
            };
            // A body's flat colour: its player's at 128 for a candidate, else cover or erase.
            const auto colourOf = [&](const HiddenDraw& d) -> SDL_Color {
                const HiddenRec& rec = hiddenRecs_[d.rec];
                if (rec.candidate) {
                    const SDL_Color pc = playerColor(paintItems_[rec.item].u->player);
                    return {pc.r, pc.g, pc.b, 128};
                }
                return d.blend == 1 ? SDL_Color{0, 0, 0, 255} : SDL_Color{0, 0, 0, 0};
            };
            const auto drawGroup = [&](size_t first, size_t last) {
                const HiddenDraw& head = hiddenDraws_[first];
                if (!head.tex && native) {
                    SDL_SetTextureBlendMode(hiddenWhite_, blends[head.blend]);
                    if (hiddenSubmit_.beginPass(ren_, hiddenWhite_)) {
                        const bool flat = hiddenSubmit_.passFlatBegin({-float(px0), -float(py0)});
                        if (flat) {
                            for (size_t k = first; k < last; ++k) {
                                const HiddenDraw& d = hiddenDraws_[k];
                                hiddenSubmit_.passFlat(visGeom(size_t(hiddenRecs_[d.rec].slot)).verts, colourOf(d));
                            }
                            hiddenSubmit_.passFlatEnd();
                        }
                        hiddenSubmit_.endPass();
                        if (flat) return;
                    }
                    // (A refused pass drew nothing: the SDL path below draws the group.)
                }
                hiddenXY_.clear(); hiddenUV_.clear(); hiddenColor_.clear(); hiddenIndex_.clear();
                for (size_t k = first; k < last; ++k) {
                    const HiddenDraw& d = hiddenDraws_[k];
                    const HiddenRec& rec = hiddenRecs_[d.rec];
                    if (!d.tex) {
                        const SDL_Color c = colourOf(d);
                        for (const SDL_Vertex& v : visGeom(size_t(rec.slot)).verts) put(v.position, c);
                        continue;
                    }
                    const SDL_Color white{255, 255, 255, 255};
                    const int base = int(hiddenXY_.size() / 2);
                    if (rec.kind == HiddenKind::Feature) {
                        const FeatureSprite sp = featureSprite(*paintItems_[rec.item].f, zm);
                        const SDL_FRect& r = sp.dst;
                        const SDL_FPoint q[4] = {{r.x + sp.shear, r.y}, {r.x + r.w + sp.shear, r.y},
                                                 {r.x + r.w, r.y + r.h}, {r.x, r.y + r.h}};
                        const float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
                        for (int v = 0; v < 4; ++v) put(q[v], white);
                        hiddenUV_.insert(hiddenUV_.end(), uv, uv + 8);
                        for (int v : {0, 1, 2, 0, 2, 3}) hiddenIndex_.push_back(base + v);
                    } else {
                        // Drawn from its distant image: the image's own coverage.
                        const auto& quad = distantModelItems_[size_t(rec.slot)].quad;
                        for (size_t v = 0; v < quad.size(); ++v) {
                            put(quad[v].position, white);
                            hiddenUV_.push_back(quad[v].tex_coord.x); hiddenUV_.push_back(quad[v].tex_coord.y);
                            hiddenIndex_.push_back(base + int(v));
                        }
                    }
                }
                const int n = int(hiddenXY_.size() / 2);
                if (!n) return;
                tak::render::countSdlGeometry(size_t(n), size_t(n) * (sizeof(float) * 4 + sizeof(SDL_Color)));
                if (head.tex) {
                    SDL_BlendMode was{};
                    SDL_GetTextureBlendMode(head.tex, &was);
                    SDL_SetTextureBlendMode(head.tex, blends[head.blend]);
                    SDL_RenderGeometryRaw(ren_, head.tex, hiddenXY_.data(), 2 * sizeof(float), hiddenColor_.data(),
                        sizeof(SDL_Color), hiddenUV_.data(), 2 * sizeof(float), n, hiddenIndex_.data(),
                        int(hiddenIndex_.size()), sizeof(int));
                    SDL_SetTextureBlendMode(head.tex, was);
                } else {
                    SDL_SetRenderDrawBlendMode(ren_, blends[head.blend]);
                    SDL_RenderGeometryRaw(ren_, nullptr, hiddenXY_.data(), 2 * sizeof(float), hiddenColor_.data(),
                        sizeof(SDL_Color), nullptr, 0, n, nullptr, 0, 0);
                }
            };
            ok = target(hiddenMask_);
            if (ok) {
                SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
                SDL_RenderFillRect(ren_, &whole);
                for (size_t g = 0; g < hiddenDraws_.size();) {
                    size_t e = g + 1;
                    while (e < hiddenDraws_.size() && hiddenDraws_[e].layer == hiddenDraws_[g].layer &&
                           hiddenDraws_[e].tex == hiddenDraws_[g].tex && hiddenDraws_[e].blend == hiddenDraws_[g].blend) ++e;
                    drawGroup(g, e);
                    g = e;
                }
                for (const HiddenDraw& d : hiddenDraws_) outlined += hiddenRecs_[d.rec].candidate;
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
