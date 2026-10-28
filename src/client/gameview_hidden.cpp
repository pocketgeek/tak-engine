#include "client/gameview.h"

// HIDDEN UNITS (Options > Graphics; a non-retail extra, default SHADOW). Units that a
// tree, a rock or a building paints over are shown anyway: through the cover, as a
// faint shadow (SHADOW: ghost ops in the body pass), or by their tactical dot on top
// (DOTS: after the body pass). The policy -- modes, painter layers, cover tests,
// screen bins, ghost order -- is in client/hiddenunits.h; this file runs it against
// the frame's painter list. On OpenGL with a stencil on the window, SHADOW marks the
// hidden units and their covers for the body pass instead of planning ghosts
// (client/hiddenstencil.h).
//
// Who: the units the painter list holds (the frame draws them: own, allied, enemies
// in sight -- never fog, never an undrawn cloaked enemy), alive, not a corpse, not a
// structure, on the ground layer. Covers: tall features (the low layer is drawn
// before every unit) and structures drawn solid and whole (not a site still being
// conjured or an FBI ghost: translucent; not clipped by a terrain wall), painted
// AFTER the unit. Another mobile unit is never a cover. Display only: nothing here
// reads or writes simulation state.

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
        if (g.boundsRevision != g.revision) {
            for (const SDL_Vertex& v : g.verts) add(v.position);
            g.bodyBounds = {b.x0, b.y0, b.x1, b.y1};
            g.boundsRevision = g.revision;
        }
        return {g.bodyBounds[0], g.bodyBounds[1], g.bodyBounds[2], g.bodyBounds[3]};
    }

    void GameView::findHiddenUnits(int mvw, int winH) {
        hiddenDotIds_.clear();
        hiddenRecs_.clear();
        hiddenGhosts_.clear();
        hiddenRedraw_.clear();
        hiddenStencilFrame_ = false;
        hiddenVeils_ = 0;
#ifndef NDEBUG
        debugHiddenCandidates_ = 0;
        debugHiddenGhostIds_.clear();
        debugHiddenExactIds_.clear();
        debugHiddenStencil_ = false;
        debugHiddenPassVeils_ = 0;
        // Debug: TAK_HIDDEN_UNITS=off|dots|shadow overrides the option (benchmarks);
        // TAK_HIDDEN_STENCIL=0 draws SHADOW by the redraw form on OpenGL too (A/B).
        static const char* forced = tak::devEnv("TAK_HIDDEN_UNITS");
        if (forced) hiddenUnitsMode_ = hu::parse(forced);
        static bool stencilEnvRead = false;
        if (!stencilEnvRead) {
            stencilEnvRead = true;
            if (const char* v = tak::devEnv("TAK_HIDDEN_STENCIL")) hiddenStencilAllowed_ = std::atoi(v) != 0;
        }
#endif
        if (hiddenUnitsMode_ == hu::kOff || dotsFrame_) return;
        using RS = tak::render::Counter;
        tak::render::ScopedPhase phase(renderStats_, tak::render::Phase::HiddenUnits);
        const bool shadow = hiddenUnitsMode_ == hu::kShadow;
        // SHADOW by the stencil (client/hiddenstencil.h): the items are marked, the
        // ghosts below are not planned.
        const bool stencil = shadow && hiddenStencilUsable();
        const auto mark = [&](uint32_t item, HiddenMark m) {
            if (hiddenItemMark_.size() != paintItems_.size()) hiddenItemMark_.assign(paintItems_.size(), kHiddenNone);
            hiddenItemMark_[item] = m;
        };
        if (stencil) hiddenItemMark_.clear();
        const float zm = mapView_.zoom();
        const hu::Rect screen{0, 0, float(mvw), float(winH)};
        const auto& items = paintItems_;
        hiddenCoverRec_.clear();
        hiddenGrid_.reset(float(mvw), float(winH));
        const bool ghosts = shadow && !stencil;
        if (ghosts) hiddenItemGrid_.reset(float(mvw), float(winH));
        // Every rec is binned for the redraw form (its id is its index): what a ghost redraws.
        const auto keep = [&](const HiddenRec& rec) {
            if (ghosts) hiddenItemGrid_.insert(rec.rect);
            hiddenRecs_.push_back(rec);
        };
        size_t eligible = 0, candidates = 0;
        // Backwards through the painter list: when a unit is reached, every cover in
        // the grid paints after it. A unit is looked at closely only when its model's
        // box over all headings (unitHitBox, from its anchor, with room for animation
        // past the rest pose) reaches a cover; then its exact body bounds must overlap
        // a cover's (a sprite's rectangle, a structure's body bounds) to make it a
        // candidate. The last of the covers it overlaps is where its ghost goes.
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
                keep({uint32_t(k), HiddenKind::Feature, false, -1, 0, r});
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
                    // Its whole body solid on screen. Repair or conjure particles, a
                    // builder at work and emotes go through drawUnit too, but leave the
                    // body as it is: those still cover.
                    const bool clipped = !g.canFly && g.occY < g.ay - 2.0f;   // drawUnit's wall clip
                    if (!u.underConstruction && !u.type->ghost && !clipped) kind = HiddenKind::Structure;
                } else if (u.alive() && !u.corpsePhase) kind = HiddenKind::Unit;
            }
            if (kind == HiddenKind::Structure) {
                const hu::Rect r = hiddenBodyBounds(slot).clipped(screen);
                if (r.empty()) continue;
                hiddenCoverRec_.push_back(uint32_t(hiddenRecs_.size()));
                hiddenGrid_.insert(r);
                keep({uint32_t(k), kind, false, slot, 0, r});
                continue;
            }
            const SDL_FRect& hb = unitHitBox(u.type);
            constexpr float kPad = 4.0f;
            const hu::Rect full{g.ax + hb.x * zm - kPad, g.ay + hb.y * zm - kPad,
                                g.ax + (hb.x + hb.w) * zm + kPad, g.ay + (hb.y + hb.h) * zm + kPad};
            const hu::Rect box = full.clipped(screen);
            // SHADOW by redrawing: a ghost must redraw every item of its range that it overlaps, and
            // the box is the model at rest -- a swing reaches past it (over a third of the
            // box measured), a fall further. So a ground unit is binned by its box grown
            // by its size and its exact bounds read only when a ghost's rectangle meets
            // that (loose); a dead, dying or unfinished one by its exact bounds.
            hu::Rect reach = box;
            bool loose = false;
            if (ghosts && it.layer == hu::kLayerGround) {
                if (kind == HiddenKind::Unit) {
                    const float grow = std::max(hb.w, hb.h) * zm;
                    reach = hu::Rect{full.x0 - grow, full.y0 - grow, full.x1 + grow, full.y1 + grow}.clipped(screen);
                    loose = true;
                } else reach = hiddenBodyBounds(slot).clipped(screen);
            }
            if (reach.empty()) continue;
            HiddenRec rec{uint32_t(k), kind, false, slot, uint32_t(hiddenGrid_.size()), reach};
            rec.loose = loose;
            if (kind == HiddenKind::Unit && !box.empty()) {
                ++eligible;
                bool near = false;
                hiddenGrid_.query(box, [&](uint32_t) { near = true; return false; });
                if (near) {
                    const hu::Rect body = hiddenBodyBounds(slot).clipped(screen);
                    hiddenGrid_.query(body, [&](uint32_t id) {
                        const HiddenRec& cov = hiddenRecs_[hiddenCoverRec_[id]];
                        if (cov.rect.overlaps(body)) {
                            rec.candidate = true;
                            rec.lastCover = std::max(rec.lastCover, cov.item);
                            if (stencil) mark(cov.item, kHiddenCover);
                        }
                        return true;
                    });
                    if (rec.candidate) {
                        rec.rect = body;
                        ++candidates;
                        if (stencil) mark(uint32_t(k), kHiddenUnit);
                    }
                    if (ghosts) { rec.rect = body; rec.loose = false; }   // read anyway: exact
                }
            }
            keep(rec);
        }
        renderStats_.add(RS::HiddenEligible, eligible);
        renderStats_.add(RS::HiddenCandidates, candidates);
#ifndef NDEBUG
        debugHiddenCandidates_ = candidates;
#endif
        if (stencil && candidates) {
            // Every hidden unit writes 1, every cover over one is drawn twice.
            hiddenStencilFrame_ = true;
            for (const uint8_t m : hiddenItemMark_) hiddenVeils_ += m == kHiddenCover;
            renderStats_.add(RS::HiddenMarked, candidates);
            renderStats_.add(RS::HiddenRedrawn, hiddenVeils_);
#ifndef NDEBUG
            debugHiddenStencil_ = true;
            for (const HiddenRec& rec : hiddenRecs_)
                if (rec.candidate) {
                    debugHiddenGhostIds_.push_back(items[rec.item].u->id);
                    debugHiddenExactIds_.push_back(items[rec.item].u->id);   // per pixel, always
                }
#endif
            return;
        }
        if (!candidates || !shadow || stencil) return;
        // SHADOW. The hidden units, in painter order. One drawn through drawUnit or
        // translucent cannot be redrawn exactly: a ghost of its own, translucent.
        const auto plain = [&](const UnitR& u, const UnitGeom& g) {
            return !specialUnit(u, g.canFly, g.occY, g.ay) && !(u.type && u.type->ghost);
        };
        hiddenMembers_.clear();
        hiddenGroupRects_.clear();
        for (size_t i = hiddenRecs_.size(); i-- > 0;) {
            const HiddenRec& rec = hiddenRecs_[i];
            if (!rec.candidate) continue;
            const bool p = plain(*items[rec.item].u, visGeom(size_t(rec.slot)));
            hiddenMembers_.push_back({rec.item, rec.lastCover, rec.slot, p, rec.rect});
            hiddenGroupRects_.push_back(p ? rec.rect : hu::Rect{});
        }
        // Grouped by overlap (hiddenunits.h, groupOverlaps), members of a group kept
        // together in painter order, one ghost per group.
        hu::groupOverlaps(hiddenGroupRects_, hiddenGroupGrid_, float(mvw), float(winH), hiddenGroup_);
        {
            std::vector<uint32_t>& ghostOf = hiddenGroupGhost_;
            ghostOf.assign(hiddenMembers_.size(), UINT32_MAX);
            for (size_t i = 0; i < hiddenMembers_.size(); ++i) {
                const HiddenMember& m = hiddenMembers_[i];
                uint32_t& gi = ghostOf[hiddenGroup_[i]];
                if (gi == UINT32_MAX) {
                    gi = uint32_t(hiddenGhosts_.size());
                    hiddenGhosts_.push_back({{m.rect, m.item, m.last}, m.plain});
                }
                HiddenGhost& ghost = hiddenGhosts_[gi];
                ghost.range.rect.unite(m.rect);
                ghost.range.start = std::min(ghost.range.start, m.item);
                ghost.range.last = std::max(ghost.range.last, m.last);
                ++ghost.memberCount;
            }
            // Members by ghost (a counting sort; painter order within each).
            uint32_t at = 0;
            for (HiddenGhost& ghost : hiddenGhosts_) { ghost.members = at; at += ghost.memberCount; ghost.memberCount = 0; }
            std::vector<HiddenMember>& sorted = hiddenSortedMembers_;
            sorted.resize(hiddenMembers_.size());
            for (size_t i = 0; i < hiddenMembers_.size(); ++i) {
                HiddenGhost& ghost = hiddenGhosts_[ghostOf[hiddenGroup_[i]]];
                sorted[ghost.members + ghost.memberCount++] = hiddenMembers_[i];
            }
            hiddenMembers_.swap(sorted);
        }
        std::sort(hiddenGhosts_.begin(), hiddenGhosts_.end(), [](const HiddenGhost& a, const HiddenGhost& b) {
            return a.range.last != b.range.last ? a.range.last < b.range.last : a.range.start < b.range.start;
        });
        hu::chainStarts(std::span<HiddenGhost>(hiddenGhosts_), [](HiddenGhost& g) -> hu::Ghost& { return g.range; },
                        hiddenGhostGrid_, float(mvw), float(winH));
        // What each ghost redraws: the items of its range over its rectangle, in
        // painter order. Exact only when every one of them redraws its own pixels:
        // a plain body or a cover.
        for (HiddenGhost& ghost : hiddenGhosts_) {
            const hu::Ghost& range = ghost.range;
            ghost.first = uint32_t(hiddenRedraw_.size());
            if (ghost.exact)
                hiddenItemGrid_.query(range.rect, [&](uint32_t id) {
                    HiddenRec& rec = hiddenRecs_[id];
                    if (rec.item < range.start || rec.item > range.last || !rec.rect.overlaps(range.rect)) return true;
                    if (rec.loose) {   // the reach meets it: does the body?
                        rec.rect = hiddenBodyBounds(rec.slot).clipped(screen);
                        rec.loose = false;
                        if (!rec.rect.overlaps(range.rect)) return true;
                    }
                    if (rec.kind == HiddenKind::Feature || rec.kind == HiddenKind::Structure) {
                        hiddenRedraw_.push_back({rec.item, HiddenRedraw::Veil, rec.slot});
                        return true;
                    }
                    if (!plain(*items[rec.item].u, visGeom(size_t(rec.slot)))) {
                        ghost.exact = false;
                        return false;
                    }
                    hiddenRedraw_.push_back({rec.item, HiddenRedraw::Body, rec.slot});
                    return true;
                });
            if (!ghost.exact) hiddenRedraw_.resize(ghost.first);   // its units alone, translucent
            std::sort(hiddenRedraw_.begin() + ghost.first, hiddenRedraw_.end(),
                      [](const HiddenRedrawItem& a, const HiddenRedrawItem& b) { return a.item < b.item; });
            ghost.count = uint32_t(hiddenRedraw_.size()) - ghost.first;
#ifndef NDEBUG
            for (uint32_t m = ghost.members; m < ghost.members + ghost.memberCount; ++m) {
                debugHiddenGhostIds_.push_back(items[hiddenMembers_[m].item].u->id);
                if (ghost.exact) debugHiddenExactIds_.push_back(items[hiddenMembers_[m].item].u->id);
            }
#endif
        }
        renderStats_.add(RS::HiddenMarked, hiddenMembers_.size());
        renderStats_.add(RS::HiddenRedrawn, hiddenRedraw_.size());
#ifndef NDEBUG
        // Debug TAK_HIDDEN_VERIFY=1: every exact ghost redraws each item of its range
        // whose drawn body meets its rectangle (a plain scan of the range, exact bounds).
        static const bool verify = tak::devFlag("TAK_HIDDEN_VERIFY");
        if (verify)
            for (const HiddenGhost& ghost : hiddenGhosts_) {
                if (!ghost.exact) continue;
                for (uint32_t k = ghost.range.start; k <= ghost.range.last; ++k) {
                    hu::Rect r;
                    if (const auto* f = items[k].f) {
                        const FeatureSprite sp = featureSprite(*f, zm);
                        if (!sp.tex) continue;
                        r = {sp.dst.x + std::min(0.0f, sp.shear), sp.dst.y, sp.dst.x + sp.dst.w + std::max(0.0f, sp.shear),
                             sp.dst.y + sp.dst.h};
                    } else {
                        const int slot = geomSlot(items[k].u->id);
                        if (slot < 0 || (visGeom(size_t(slot)).runs.empty() && !distantModelItems_[size_t(slot)].texture))
                            continue;
                        r = hiddenBodyBounds(slot);
                    }
                    if (!r.clipped(screen).overlaps(ghost.range.rect)) continue;
                    const HiddenRedrawItem* end = hiddenRedraw_.data() + ghost.first + ghost.count;
                    if (std::find_if(end - ghost.count, end,
                                     [&](const HiddenRedrawItem& d) { return d.item == k; }) == end)
                        throw std::runtime_error("hidden units: a SHADOW ghost misses painter item " + std::to_string(k));
                }
            }
#endif
    }

    void GameView::hiddenBatchFlush() {
        if (hiddenBatchTex_) {
            if (hiddenBatchOnly_) drawModelRun(hiddenBatchTex_, hiddenBatchOnly_, hiddenBatchOnlyCount_, true);
            else if (!hiddenVerts_.empty()) drawModelRun(hiddenBatchTex_, hiddenVerts_.data(), int(hiddenVerts_.size()), true);
        }
        hiddenBatchTex_ = nullptr;
        hiddenBatchOnly_ = nullptr;
        hiddenBatchOnlyCount_ = 0;
        hiddenVerts_.clear();
    }

    void GameView::hiddenBatchAdd(SDL_Texture* texture, const SDL_Vertex* vertices, int count, uint8_t alpha,
                                  bool premultiplied) {
        if (!texture || count <= 0) return;   // a null run texture: skipped, as drawUnit does
        if (texture != hiddenBatchTex_) { hiddenBatchFlush(); hiddenBatchTex_ = texture; }
        if (alpha == 255 && !hiddenBatchOnly_ && hiddenVerts_.empty()) {   // maybe alone: no copy yet
            hiddenBatchOnly_ = vertices;
            hiddenBatchOnlyCount_ = count;
            return;
        }
        if (hiddenBatchOnly_) {
            hiddenVerts_.assign(hiddenBatchOnly_, hiddenBatchOnly_ + hiddenBatchOnlyCount_);
            hiddenBatchOnly_ = nullptr;
        }
        const size_t at = hiddenVerts_.size();
        hiddenVerts_.insert(hiddenVerts_.end(), vertices, vertices + count);
        if (alpha != 255)
            for (size_t i = at; i < hiddenVerts_.size(); ++i) {
                SDL_Vertex& v = hiddenVerts_[i];
                if (premultiplied) {   // colour and alpha scale together
                    v.color.r = hu::scaleAlpha(v.color.r, alpha);
                    v.color.g = hu::scaleAlpha(v.color.g, alpha);
                    v.color.b = hu::scaleAlpha(v.color.b, alpha);
                }
                v.color.a = hu::scaleAlpha(v.color.a, alpha);
            }
    }

    void GameView::drawHiddenBody(int slot, uint8_t alpha) {
        const auto& distant = distantModelItems_[size_t(slot)];
        if (distant.texture) {
            // The image pages blend premultiplied; on a renderer that composites them
            // with plain blending, alpha alone scales.
            SDL_BlendMode mode{};
            SDL_GetTextureBlendMode(distant.texture, &mode);
            hiddenBatchAdd(distant.texture, distant.quad.data(), int(distant.quad.size()), alpha,
                           mode != SDL_BLENDMODE_BLEND);
            return;
        }
        UnitGeom& g = visGeom(size_t(slot));
        if (g.bodyDeferred) materializeDistantBody(size_t(slot));
        int off = 0;
        for (const auto& [texture, count] : g.runs) {
            hiddenBatchAdd(texture, g.verts.data() + off, count, alpha, false);
            off += count;
        }
    }

    // SHADOW: one ghost (client/hiddenunits.h, Ghost), drawn in the body pass right
    // after the last cover over its units, clipped to their bounding rectangle.
    void GameView::drawHiddenGhost(const HiddenGhost& ghost) {
        tak::render::ScopedPhase phase(renderStats_, tak::render::Phase::HiddenUnits);
        const hu::Rect& r = ghost.range.rect;
        SDL_Rect clip{int(std::floor(r.x0)), int(std::floor(r.y0)), 0, 0};
        clip.w = int(std::ceil(r.x1)) - clip.x;
        clip.h = int(std::ceil(r.y1)) - clip.y;
        const SDL_bool hadClip = SDL_RenderIsClipEnabled(ren_);
        SDL_Rect prevClip{};
        if (hadClip) {
            SDL_RenderGetClipRect(ren_, &prevClip);
            if (!SDL_IntersectRect(&prevClip, &clip, &clip)) return;
        }
        if (clip.w <= 0 || clip.h <= 0) return;
        SDL_RenderSetClipRect(ren_, &clip);
        const float zm = mapView_.zoom();
        tak::render::countLogicalDraw();
        if (!ghost.exact) {
            for (uint32_t m = ghost.members; m < ghost.members + ghost.memberCount; ++m)
                drawHiddenBody(hiddenMembers_[m].slot, hu::kShadowOpacity);
        } else if (!drawHiddenGhostNative(ghost)) {
            // SDL's path (another renderer, or a texture that cannot join a native
            // pass): same items, same pixels, bodies batched by texture.
            for (uint32_t i = ghost.first; i < ghost.first + ghost.count; ++i) {
                const HiddenRedrawItem& d = hiddenRedraw_[i];
                if (d.how == HiddenRedraw::Body) { drawHiddenBody(d.slot, 255); continue; }
                if (d.slot >= 0) { drawHiddenBody(d.slot, hu::kBuildingVeilAlpha); continue; }   // a structure
                // A feature, as the body pass drew it (same call, same sampling), see-through.
                hiddenBatchFlush();
                const FeatureSprite sp = featureSprite(*paintItems_[d.item].f, zm);
                if (!sp.tex) continue;
                if (sp.sway) {
                    const SDL_FRect& dst = sp.dst;
                    const SDL_Color wc{255, 255, 255, hu::kVeilAlpha};
                    const SDL_Vertex v[4] = {
                        {{dst.x + sp.shear, dst.y}, wc, {0, 0}},
                        {{dst.x + dst.w + sp.shear, dst.y}, wc, {1, 0}},
                        {{dst.x + dst.w, dst.y + dst.h}, wc, {1, 1}},
                        {{dst.x, dst.y + dst.h}, wc, {0, 1}},
                    };
                    static const int idx[6] = {0, 1, 2, 0, 2, 3};
                    SDL_RenderGeometry(ren_, sp.tex, v, 4, idx, 6);
                } else {
                    Uint8 was = 255;
                    SDL_GetTextureAlphaMod(sp.tex, &was);
                    SDL_SetTextureAlphaMod(sp.tex, hu::kVeilAlpha);
                    SDL_RenderCopyF(ren_, sp.tex, nullptr, &sp.dst);
                    SDL_SetTextureAlphaMod(sp.tex, was);
                }
            }
        }
        hiddenBatchFlush();
        SDL_RenderSetClipRect(ren_, hadClip ? &prevClip : nullptr);
    }

    bool GameView::drawHiddenGhostNative(const HiddenGhost& ghost) {
        if (!nativePassesOn_) return false;
        const float zm = mapView_.zoom();
        // Every texture must join a pass (resolved before it opens); the prime is the
        // first BLEND one, else a premultiplied one (as scanNativeSpan picks).
        SDL_Texture* prime = nullptr;
        SDL_Texture* premultipliedPrime = nullptr;
        const auto joins = [&](SDL_Texture* texture) {
            if (!texture) return true;   // skipped when drawn
            if (palettePage(texture) >= 0) { if (!prime) prime = texture; return true; }
            const SpanTexture& entry = spanTexture(texture);
            if (!entry.name) return false;
            if (!entry.premultiplied) { if (!prime) prime = texture; }
            else if (!premultipliedPrime) premultipliedPrime = texture;
            return true;
        };
        const auto bodyJoins = [&](int slot) {
            if (SDL_Texture* image = distantModelItems_[size_t(slot)].texture) return joins(image);
            UnitGeom& g = visGeom(size_t(slot));
            if (g.bodyDeferred) materializeDistantBody(size_t(slot));
            for (const auto& run : g.runs) if (!joins(run.first)) return false;
            return true;
        };
        for (uint32_t i = ghost.first; i < ghost.first + ghost.count; ++i) {
            const HiddenRedrawItem& d = hiddenRedraw_[i];
            if (d.slot >= 0 ? !bodyJoins(d.slot) : !joins(featureSprite(*paintItems_[d.item].f, zm).tex)) return false;
        }
        const bool primePremultiplied = !prime;
        if (!prime) prime = premultipliedPrime;
        if (!prime) return true;   // nothing with a texture: nothing to draw
        if (!nativePass_.begin(bodySubmit_, ren_, prime, primePremultiplied)) {
            ++nativePassRefused_;
            if (!nativePass_.usable()) nativePassesOn_ = false;   // not this context's way of drawing
            return false;
        }
        uint64_t commands = 0;
        SDL_Texture* bound = nullptr;
        const auto use = [&](SDL_Texture* texture) {
            if (texture != bound) { useSpanTexture(texture); bound = texture; }
            ++commands;
        };
        // Consecutive arena ranges of one texture and one storage: one multi-draw.
        SDL_Texture* pendingTexture = nullptr;
        GLuint pendingBuffer = 0;
        size_t pendingVertices = 0;
        hiddenFirsts_.clear();
        hiddenCounts_.clear();
        const auto flush = [&] {
            if (hiddenFirsts_.empty()) return;
            use(pendingTexture);
            bodySubmit_.passDrawBuffer(pendingBuffer, hiddenFirsts_, hiddenCounts_, pendingVertices);
            hiddenFirsts_.clear();
            hiddenCounts_.clear();
            pendingVertices = 0;
        };
        // A body as the body pass drew it, from the arena when it is there this frame;
        // alpha scaled (a building's veil) from a copy.
        const auto body = [&](int slot, uint8_t alpha) {
            const auto& image = distantModelItems_[size_t(slot)];
            const UnitGeom& g = visGeom(size_t(slot));
            const bool arena = alpha == 255 && g.arenaFrame == bodyArenaFrame_ && g.arenaStorage >= 0;
            const GLuint buffer = arena ? bodyArenaBuffers_[size_t(g.arenaStorage)].id() : 0;
            const auto draw = [&](SDL_Texture* texture, const SDL_Vertex* v, int count, int offset, bool premultiplied) {
                if (!texture || count <= 0) return;
                if (buffer) {
                    if (texture != pendingTexture || buffer != pendingBuffer) {
                        flush();
                        pendingTexture = texture;
                        pendingBuffer = buffer;
                    }
                    hiddenFirsts_.push_back(GLint(g.arenaFirst) + offset);
                    hiddenCounts_.push_back(GLsizei(count));
                    pendingVertices += size_t(count);
                    return;
                }
                flush();
                use(texture);
                if (alpha == 255) { bodySubmit_.passDraw(std::span<const SDL_Vertex>(v, size_t(count))); return; }
                hiddenVerts_.assign(v, v + count);
                for (SDL_Vertex& s : hiddenVerts_) {
                    if (premultiplied) {
                        s.color.r = hu::scaleAlpha(s.color.r, alpha);
                        s.color.g = hu::scaleAlpha(s.color.g, alpha);
                        s.color.b = hu::scaleAlpha(s.color.b, alpha);
                    }
                    s.color.a = hu::scaleAlpha(s.color.a, alpha);
                }
                bodySubmit_.passDraw(hiddenVerts_);
            };
            if (image.texture) {
                draw(image.texture, image.quad.data(), int(image.quad.size()), 0, spanTexture(image.texture).premultiplied);
                return;
            }
            int off = 0;
            for (const auto& [texture, count] : g.runs) {
                draw(texture, g.verts.data() + off, count, off, false);
                off += count;
            }
        };
        for (uint32_t i = ghost.first; i < ghost.first + ghost.count; ++i) {
            const HiddenRedrawItem& d = hiddenRedraw_[i];
            if (d.how == HiddenRedraw::Body) { body(d.slot, 255); continue; }
            if (d.slot >= 0) { body(d.slot, hu::kBuildingVeilAlpha); continue; }
            // A feature: the triangles the body pass drew, see-through by their vertex
            // alpha -- the swayed quad's (0 1 2, 0 2 3), or SDL's copy, a strip over
            // the other diagonal (0 1 3, 3 1 2 as GL takes it). The same triangles
            // sample the same texels.
            const FeatureSprite sp = featureSprite(*paintItems_[d.item].f, zm);
            if (!sp.tex) continue;
            const SDL_FRect& r = sp.dst;
            const float shear = sp.sway ? sp.shear : 0.0f;
            const SDL_Color c{255, 255, 255, hu::kVeilAlpha};
            const SDL_Vertex q[4] = {{{r.x + shear, r.y}, c, {0, 0}}, {{r.x + r.w + shear, r.y}, c, {1, 0}},
                                     {{r.x + r.w, r.y + r.h}, c, {1, 1}}, {{r.x, r.y + r.h}, c, {0, 1}}};
            const SDL_Vertex swayed[6] = {q[0], q[1], q[2], q[0], q[2], q[3]};
            const SDL_Vertex copied[6] = {q[0], q[1], q[3], q[3], q[1], q[2]};
            const SDL_Vertex* tri = sp.sway ? swayed : copied;
            flush();
            use(sp.tex);
            bodySubmit_.passDraw(std::span<const SDL_Vertex>(tri, 6));
        }
        flush();
        nativePass_.end(bodySubmit_);
        ++nativePasses_;
        nativePassCommands_ += commands;
        return true;
    }

    // ---- SHADOW, stencil form (client/hiddenstencil.h) ----
    // OpenGL with a stencil on the window, the world drawn into the window (no render
    // target), and not switched off (debug TAK_HIDDEN_STENCIL=0).
    bool GameView::hiddenStencilUsable() {
        return hiddenStencilAllowed_ && !SDL_GetRenderTarget(ren_) && hiddenStencil_.available(ren_);
    }

    void GameView::hiddenStencilUse(tak::HiddenStencilGL::State state, bool inPass) {
        hiddenStencil_.use(ren_, state, inPass);
    }

    // Inside the window: a hidden unit writes 1, a cover starts with its opaque draw
    // (under 0), any other body writes 0; a feature op sets its own states (its
    // shadow, flames and smoke leave the stencil alone).
    tak::HiddenStencilGL::State GameView::hiddenOpState(size_t opi) const {
        using HS = tak::HiddenStencilGL::State;
        if (!hiddenStencilFrame_ || opi < hiddenStencilFirst_ || opi > hiddenStencilLast_) return HS::Off;
        const DrawOp& op = drawOps_[opi];
        if (op.f) return HS::Off;
        return op.hidden == kHiddenUnit ? HS::Unit : op.hidden == kHiddenCover ? HS::Under0 : HS::Other;
    }

    // A building's body command again, see-through (kBuildingVeilAlpha): its vertices
    // with every alpha scaled (and the colour too where the page is premultiplied),
    // drawn where the stencil holds 1. In a native pass the command's texture is bound.
    void GameView::drawHiddenVeil(const DrawOp& op, bool inPass) {
        bool premultiplied = op.premultiplied;
        if (!inPass) {
            SDL_BlendMode mode{};
            SDL_GetTextureBlendMode(op.tex, &mode);
            premultiplied = palettePage(op.tex) < 0 && mode != SDL_BLENDMODE_BLEND;
        }
        hiddenVerts_.clear();
        for (uint32_t k = op.firstRange; k < op.firstRange + op.ranges; ++k) {
            const BodyRange& r = bodyRanges_[k];
            const SDL_Vertex* v = bodySources_[r.source].data + r.offset;
            hiddenVerts_.insert(hiddenVerts_.end(), v, v + r.count);
        }
        constexpr uint8_t a = hu::kBuildingVeilAlpha;
        for (SDL_Vertex& v : hiddenVerts_) {
            if (premultiplied) {
                v.color.r = hu::scaleAlpha(v.color.r, a);
                v.color.g = hu::scaleAlpha(v.color.g, a);
                v.color.b = hu::scaleAlpha(v.color.b, a);
            }
            v.color.a = hu::scaleAlpha(v.color.a, a);
        }
        if (hiddenVerts_.empty()) return;
        if (inPass) bodySubmit_.passDraw(hiddenVerts_);
        else drawModelRun(op.tex, hiddenVerts_.data(), int(hiddenVerts_.size()), true);
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
        renderStats_.add(tak::render::Counter::HiddenMarked, hiddenDotIds_.size());
        if (!overlayBatch_.empty()) {
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(ren_, nullptr, overlayBatch_.data(), int(overlayBatch_.size()), nullptr, 0);
        }
    }
