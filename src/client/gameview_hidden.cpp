#include "client/gameview.h"

// HIDDEN UNITS (Options > Graphics; a non-retail extra, default FADE). Units that a
// tree, a rock or a building paints over are shown anyway: FADE fades that whole
// tree, rock or building (hiddenFades_, drawn faded by the body pass), DOTS draws
// the unit's tactical dot on top (after the body pass). The policy -- modes, painter
// layers, cover tests, screen bins, fades -- is in client/hiddenunits.h; this file
// runs it against the frame's painter list.
//
// Who: the units the painter list holds (the frame draws them: own, allied, enemies
// in sight -- never fog, never an undrawn cloaked enemy), alive, not a corpse, not a
// structure, on the ground layer. Covers: tall features (the low layer is drawn
// before every unit) and structures drawn solid and whole (DOTS: not a site still
// being conjured or an FBI ghost: translucent; not clipped by a terrain wall),
// painted AFTER the unit. Another mobile unit is never a cover. Display only:
// nothing here reads or writes simulation state.

#include <algorithm>
#include <cmath>

namespace {
namespace hu = tak::hiddenunits;
}  // namespace

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
        // Cached per geometry revision (one GameView-wide counter: a value is never
        // repeated), so a still building is not scanned again every frame.
        if (hiddenBounds_.size() <= size_t(slot)) hiddenBounds_.resize(size_t(slot) + 1);
        HiddenBounds& cached = hiddenBounds_[size_t(slot)];
        if (cached.revision != g.revision) {
            for (const SDL_Vertex& v : g.verts) add(v.position);
            cached = {g.revision, b};
        }
        return cached.rect;
    }

    void GameView::findHiddenUnits(int mvw, int winH) {
        hiddenDotIds_.clear();
        hiddenRecs_.clear();
#ifndef NDEBUG
        debugHiddenCandidates_ = 0;
        // Debug: TAK_HIDDEN_UNITS=off|dots|fade overrides the option (benchmarks).
        static const char* forced = tak::devEnv("TAK_HIDDEN_UNITS");
        if (forced) hiddenUnitsMode_ = hu::parse(forced);
#endif
        using RS = tak::render::Counter;
        // FADE's display clock: whatever this frame finds, the fades advance at the end
        // (a cover with no unit under it, or out of view, fades back or is dropped).
        uint64_t nowMs = SDL_GetTicks64();
#ifndef NDEBUG
        if (debugHiddenFadeNowMs_) nowMs = debugHiddenFadeNowMs_;
#endif
        const float fadeMs = hiddenFadeLastMs_ && nowMs >= hiddenFadeLastMs_
                                 ? float(std::min<uint64_t>(nowMs - hiddenFadeLastMs_, 1000)) : 0.0f;
        hiddenFadeLastMs_ = nowMs;
        hiddenFades_.begin();
        if (hiddenUnitsMode_ == hu::kOff || dotsFrame_) {
            hiddenFades_.advance(fadeMs);   // nothing hit, nothing in view: all dropped
            return;
        }
        tak::render::ScopedPhase phase(renderStats_, tak::render::Phase::HiddenUnits);
        const bool fade = hiddenUnitsMode_ == hu::kFade;
        const float zm = mapView_.zoom();
        const hu::Rect screen{0, 0, float(mvw), float(winH)};
        const auto& items = paintItems_;
        hiddenCoverRec_.clear();
        hiddenGrid_.reset(float(mvw), float(winH));
        size_t eligible = 0, candidates = 0;
        // Backwards through the painter list: when a unit is reached, every cover in
        // the grid paints after it. A unit is looked at closely only when its model's
        // box over all headings (unitHitBox, from its anchor, with room for animation
        // past the rest pose) reaches a cover; then its exact body bounds must overlap
        // a cover's (a sprite's rectangle, a structure's body bounds): DOTS keeps it as
        // a candidate for the dot test, FADE fades each cover whose opaque texels or
        // triangles reach the body.
        for (size_t k = items.size(); k-- > 0;) {
            const PaintItem& it = items[k];
            if (it.layer == hu::kLayerLowFeature) break;   // sorted first: never over a unit
            if (it.layer != hu::kLayerGround) continue;     // airborne: never covered, never a cover
            if (it.f) {
                const FeatureSprite sp = featureSprite(*it.f, zm);
                if (!sp.tex) continue;
                const hu::Rect r = hu::Rect{sp.dst.x + std::min(0.0f, sp.shear), sp.dst.y,
                    sp.dst.x + sp.dst.w + std::max(0.0f, sp.shear), sp.dst.y + sp.dst.h}.clipped(screen);
                if (r.empty()) continue;
                if (fade && it.f->simId >= 0) hiddenFades_.inView(hu::featureCover(uint32_t(it.f->simId)));
                hiddenCoverRec_.push_back(uint32_t(hiddenRecs_.size()));
                hiddenGrid_.insert(r);
                hiddenRecs_.push_back({uint32_t(k), HiddenKind::Feature, -1, 0, r});
                continue;
            }
            const UnitR& u = *it.u;
            const int slot = geomSlot(u.id);
            if (slot < 0) continue;
            const UnitGeom& g = visGeom(size_t(slot));
            if (g.runs.empty() && !distantModelItems_[size_t(slot)].texture) continue;   // nothing drawn
            if (isStructure(u.type)) {
                // DOTS: its whole body solid on screen. Repair or conjure particles, a
                // builder at work and emotes go through drawUnit too, but leave the
                // body as it is: those still cover. FADE draws its covers whole,
                // through drawUnit when need be: a site being conjured and one behind
                // a wall fade too (not a placed site still waiting for its builder:
                // that is a faint ghost already).
                const bool clipped = !g.canFly && g.occY < g.ay - 2.0f;   // drawUnit's wall clip
                const bool covers = fade ? !u.type->ghost && (!u.underConstruction || u.buildBegun)
                                         : !u.underConstruction && !u.type->ghost && !clipped;
                if (!covers) continue;
                const hu::Rect r = hiddenBodyBounds(slot).clipped(screen);
                if (r.empty()) continue;
                if (fade) hiddenFades_.inView(hu::structureCover(uint32_t(u.id)));
                hiddenCoverRec_.push_back(uint32_t(hiddenRecs_.size()));
                hiddenGrid_.insert(r);
                hiddenRecs_.push_back({uint32_t(k), HiddenKind::Structure, slot, 0, r});
                continue;
            }
            if (!u.alive() || u.corpsePhase) continue;
            const SDL_FRect& hb = unitHitBox(u.type);
            constexpr float kPad = 4.0f;
            const hu::Rect box = hu::Rect{g.ax + hb.x * zm - kPad, g.ay + hb.y * zm - kPad,
                                          g.ax + (hb.x + hb.w) * zm + kPad, g.ay + (hb.y + hb.h) * zm + kPad}
                                     .clipped(screen);
            if (box.empty()) continue;
            ++eligible;
            bool nearCover = false;
            hiddenGrid_.query(box, [&](uint32_t) { nearCover = true; return false; });
            if (!nearCover) continue;
            const hu::Rect body = hiddenBodyBounds(slot).clipped(screen);
            bool hit = false;
            hiddenGrid_.query(body, [&](uint32_t id) {
                const HiddenRec& cov = hiddenRecs_[hiddenCoverRec_[id]];
                if (!cov.rect.overlaps(body)) return true;
                if (!fade) { hit = true; return false; }   // DOTS: the dot test decides
                if (cov.kind == HiddenKind::Feature) {
                    // A tree fades if its opaque texels reach the body.
                    const FeatureInst& f = *items[cov.item].f;
                    const FeatureSprite sp = featureSprite(f, zm);
                    if (f.simId >= 0 && (!sp.mask || hu::spriteCoversRect(*sp.mask, sp.dst.x, sp.dst.y, sp.dst.w,
                                                                          sp.dst.h, sp.shear, body))) {
                        hiddenFades_.cover(hu::featureCover(uint32_t(f.simId)));
                        hit = true;
                    }
                    return true;
                }
                // A building fades if one of its drawn triangles (or its distant
                // image) reaches the body.
                const auto& distant = distantModelItems_[size_t(cov.slot)];
                const UnitGeom& cg = visGeom(size_t(cov.slot));
                if (distant.texture || hu::trianglesCoverRect(std::span<const SDL_Vertex>(cg.verts), body)) {
                    hiddenFades_.cover(hu::structureCover(uint32_t(items[cov.item].u->id)));
                    hit = true;
                }
                return true;
            });
            if (!hit) continue;
            ++candidates;
            if (!fade)   // DOTS: kept for the dot test after the body pass
                hiddenRecs_.push_back({uint32_t(k), HiddenKind::Unit, slot, uint32_t(hiddenGrid_.size()), body});
        }
        hiddenFades_.advance(fadeMs);
        renderStats_.add(RS::HiddenEligible, eligible);
        renderStats_.add(RS::HiddenCandidates, candidates);
        renderStats_.add(RS::HiddenFaded, hiddenFades_.size());
#ifndef NDEBUG
        debugHiddenCandidates_ = candidates;
#endif
    }

    // FADE: a building's body (its distant image or its runs) with every alpha scaled
    // by `alpha` (and the colour too where the image page is premultiplied).
    void GameView::drawFadedBody(int slot, uint8_t alpha) {
        const auto draw = [&](SDL_Texture* texture, const SDL_Vertex* vertices, int count, bool premultiplied) {
            if (!texture || count <= 0) return;   // a null run texture: skipped, as drawUnit does
            hiddenVerts_.assign(vertices, vertices + count);
            for (SDL_Vertex& v : hiddenVerts_) {
                if (premultiplied) {   // colour and alpha scale together
                    v.color.r = hu::scaleAlpha(v.color.r, alpha);
                    v.color.g = hu::scaleAlpha(v.color.g, alpha);
                    v.color.b = hu::scaleAlpha(v.color.b, alpha);
                }
                v.color.a = hu::scaleAlpha(v.color.a, alpha);
            }
            drawModelRun(texture, hiddenVerts_.data(), count, true);
        };
        const auto& distant = distantModelItems_[size_t(slot)];
        if (distant.texture) {
            // The image pages blend premultiplied; on a renderer that composites them
            // with plain blending, alpha alone scales.
            SDL_BlendMode mode{};
            SDL_GetTextureBlendMode(distant.texture, &mode);
            draw(distant.texture, distant.quad.data(), int(distant.quad.size()), mode != SDL_BLENDMODE_BLEND);
            return;
        }
        UnitGeom& g = visGeom(size_t(slot));
        if (g.bodyDeferred) materializeDistantBody(size_t(slot));
        int off = 0;
        for (const auto& [texture, count] : g.runs) {
            draw(texture, g.verts.data() + off, count, false);
            off += count;
        }
    }

    // DOTS: the unit's dot centre against the opaque pixels of every cover that
    // paints after it (feature sprite texels, structure triangles).
    void GameView::drawHiddenDots(int mvw, int winH) {
        if (hiddenUnitsMode_ != hu::kDots || dotsFrame_ || hiddenRecs_.empty()) return;
        tak::render::ScopedPhase phase(renderStats_, tak::render::Phase::HiddenUnits);
        const float zm = mapView_.zoom();
        const hu::Rect screen{0, 0, float(mvw), float(winH)};
        const auto& items = paintItems_;
        const SDL_Color rim{12, 12, 16, 220};
        overlayBatch_.clear();
        for (size_t i = hiddenRecs_.size(); i-- > 0;) {   // forward: painter order
            const HiddenRec& rec = hiddenRecs_[i];
            if (rec.kind != HiddenKind::Unit) continue;   // a cover
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
        renderStats_.add(tak::render::Counter::HiddenMarked, hiddenDotIds_.size());
        if (!overlayBatch_.empty()) {
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(ren_, nullptr, overlayBatch_.data(), int(overlayBatch_.size()), nullptr, 0);
        }
    }
