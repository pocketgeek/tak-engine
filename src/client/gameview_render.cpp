#include "client/gameview.h"

// Out-of-line GameView method definitions (render concern), split from the
// class body in gameview.h so editing a body recompiles only this translation
// unit. Trivial getters, ctors, static, template, constexpr and default-arg
// methods stay inline in the header. Grouping is by name heuristic.

    void GameView::draw(int winW, int winH) {
        // Keep the last-known window size current from the draw path too, not just
        // from SDL input events: a headless capture (dummy video driver) gets no
        // events, so without this winW_/winH_ stay 0 and the HUD (command panel,
        // culling) lays out against the wrong size. A real window syncs the same
        // values via input(), so this is a no-op there.
        winW_ = winW;
        winH_ = winH;
        manageMusic();
        discoSound();     // fire the disco track from a monarch when its player starts dancing
        headbangSound();  // ...and the metal track on headbang
        if (inLobbyPhase()) {
            // The lobby always lays out at its design size (kLobbyW x kLobbyH logical)
            // and is scaled to fit + centred in the window -- so it shows fully at any
            // window size or aspect (never clustered top-left, never clipped). Renders
            // large for legible text; lbHot/lobbyInput undo the scale + centre offset.
            lobbyScale_ = std::min(winW / kLobbyW, winH / kLobbyH);
            float lw = winW / lobbyScale_, lh = winH / lobbyScale_;   // logical window
            lobbyOffX_ = std::floor((lw - kLobbyW) * 0.5f);
            lobbyOffY_ = std::floor((lh - kLobbyH) * 0.5f);
            // Darker surround + a thin frame so the centred lobby reads as a panel.
            // (SDL_RenderClear ignores the viewport, so the ground-clear lives here,
            // not in drawLobby.) Drawn in pixel space (scale 1, no viewport).
            SDL_SetRenderDrawColor(ren_, 8, 9, 13, 255);
            SDL_RenderClear(ren_);
            float px = lobbyOffX_ * lobbyScale_, py = lobbyOffY_ * lobbyScale_;
            float pw = kLobbyW * lobbyScale_, ph = kLobbyH * lobbyScale_;
            SDL_FRect panelBg{px, py, pw, ph};
            SDL_SetRenderDrawColor(ren_, 16, 18, 26, 255);
            SDL_RenderFillRectF(ren_, &panelBg);
            SDL_FRect frame{px - 2, py - 2, pw + 4, ph + 4};
            SDL_SetRenderDrawColor(ren_, 60, 66, 90, 255);
            SDL_RenderDrawRectF(ren_, &frame);
            // Content: scaled + viewport-offset so it draws inside the centred panel.
            SDL_Rect vp{int(lobbyOffX_), int(lobbyOffY_), int(kLobbyW), int(kLobbyH)};
            SDL_RenderSetScale(ren_, lobbyScale_, lobbyScale_);
            SDL_RenderSetViewport(ren_, &vp);
            drawLobby(int(kLobbyW), int(kLobbyH));
            SDL_RenderSetViewport(ren_, nullptr);
            SDL_RenderSetScale(ren_, 1.0f, 1.0f);
            return;
        }
        // Pull any chat that arrived and age the overlay on a real wall clock, so
        // it fades even while the game is paused or catching up on ticks.
        if (mp_) {
            for (auto& m : mp_->takeChat()) gameChat_.push_back({m.first, m.second, 0});
            uint64_t nowMs = SDL_GetTicks64();
            float cdt = chatLastMs_ ? (nowMs - chatLastMs_) / 1000.0f : 0;
            chatLastMs_ = nowMs;
            if (!chatTyping_) for (auto& g : gameChat_) g.age += cdt;
            if (gameChat_.size() > 16) gameChat_.erase(gameChat_.begin(), gameChat_.end() - 16);
        }
        // Camera shake: nudge the map offset by a decaying oscillation for this
        // frame, so the whole world jolts; the offset is restored at the end so
        // the camera and UI stay put. shakemagnitude ~3 => a few px of jolt.
        float shakeBaseX = mapView_.offX(), shakeBaseY = mapView_.offY();
        bool shaking = shakeTime_ > 0 && shakeDur_ > 0;
        if (shaking) {
            float decay = shakeTime_ / shakeDur_;
            float amp = shakeMag_ * 3.0f * decay;    // pixels
            float dx = amp * std::sin(animClock_ * 91.0f);
            float dz = amp * std::cos(animClock_ * 73.0f);
            float zm = std::max(mapView_.zoom(), 1e-3f);
            mapView_.setOffset(shakeBaseX + dx / zm, shakeBaseY + dz / zm);
        }
        // Everything world-space (map, units, effects, bars) is clipped to the map
        // viewport so it never bleeds under the right-hand panel.
        int mvw = mapViewW(winW);
        SDL_Rect worldClip{0, 0, mvw, winH};
        SDL_RenderSetClipRect(ren_, &worldClip);
        mapView_.setUnderlay(miniTex_);   // low-res gap filler (null until the overview bakes)
        mapView_.draw(mvw, winH);
        float zm0 = mapView_.zoom();

        // Painter list: features and units together, sorted by map z. Lives in a
        // member so its capacity survives across frames (it was the last per-frame
        // buffer here still heap-allocated fresh every draw).
        using Item = PaintItem;
        auto& items = paintItems_;
        items.clear();
        const auto& vis = frameVisibility();
        int vw = frameVisW();
        for (const auto& f : features_) {
            if (!world_.featureAliveAt(f.x, f.z)) continue;   // reclaimed away by a builder
            int cx = int(f.x) / 16, cz = int(f.z) / 16;
            if (!noFog_ && !vis.empty() && (cx < 0 || cz < 0 || cx >= vw ||
                                 vis[size_t(cz) * vw + cx] == 0))
                continue;   // unexplored
            // Cull on the LIFTED position (features lift onto the relief like units).
            float sx = (f.x - mapView_.offX()) * zm0 - terrainLiftX(f.x, f.z) * zm0;
            float sy = (f.z - mapView_.offY()) * zm0 - terrainLift(f.x, f.z) * zm0;
            if (sx < -200 || sy < -200 || sx > winW + 200 || sy > winH + 200) continue;
            // Mana deposit markers are flat ground decals a lodestone is built
            // on top of, so bias their sort key back to keep them painted
            // under the building rather than over it.
            float key = f.mana ? f.z - 24.0f : f.z;
            items.push_back({key, nullptr, &f});
        }
        for (const UnitR* _up : front().live) {
            const UnitR& r = *_up;   // this tick's snapshot (front().live mirrors world_.units())
            if (r.deadFor >= 4.0f || r.embarked()) continue;
            // Unregistered (e.g. a type whose model failed to load): not drawable,
            // and every render path does unitType_.at(u.id) -- skip it here so none
            // of them throw (a throw in the parallel projection aborts the process).
            if (!unitType_.count(r.id)) continue;
            if (!noFog_ && !r.alliedToLocal && !cellVisibleR(r.x, r.z)) continue;
            // Frustum cull: only units whose anchor falls in (or just outside) the
            // map viewport are projected and drawn. The margin is generous and
            // asymmetric -- models extend well above their anchor, so a unit above
            // the top edge can still show its lower body. Cull on the LIFTED anchor
            // (where the unit is actually drawn), else a unit lifted onto the screen
            // from just below the edge on high ground vanishes.
            float sx = (r.x - mapView_.offX()) * zm0 - terrainLiftX(r.x, r.z) * zm0;
            float sy = (r.z - mapView_.offY()) * zm0 - terrainLift(r.x, r.z) * zm0;
            if (sx < -160 || sx > mvw + 160 || sy < -260 || sy > winH + 120) continue;
            items.push_back({r.z, &r, nullptr});
        }
        std::stable_sort(items.begin(), items.end(),
                  [](const Item& a, const Item& b) { return a.z < b.z; });

        // Project every visible unit's model in parallel before drawing. This is
        // the heavy per-frame CPU work (matrix-transforming each unit's model tree
        // and building its vertex buffer); the render thread then only submits the
        // finished geometry, one texture-batched draw call per unit. Without this
        // the whole frame is single-threaded and pegs one core at large unit counts.
        ++sprTick_;   // frame counter for sprite-page LRU (stamps SprPage.lastUse below)
        visUnits_.clear();
        geomIndex_.assign(front().units.size(), -1);   // id -> slot; -1 = not in view
                                                        // (front().units is id-indexed, sized to cover every live id)
        for (const auto& it : items)
            if (it.u && it.u->id >= 0 && size_t(it.u->id) < geomIndex_.size()) {
                geomIndex_[size_t(it.u->id)] = int(visUnits_.size());
                visUnits_.push_back(it.u);
            }
        if (geomPool_.size() < visUnits_.size()) geomPool_.resize(visUnits_.size());
        // Fraction through the current sim-tick interval, for motion interpolation this
        // frame (clamped: a late tick just holds at the newest pose until it arrives).
        interpAlpha_ = front().tickDurMs > 0.0f
            ? std::clamp(float(SDL_GetTicks64() - front().tickMs) / front().tickDurMs, 0.0f, 1.0f)
            : 1.0f;
        // Build the texture atlas for every colour slot in view (main thread; the
        // parallel pass below only reads the finished atlas pointers).
        bool builtGlow = false;
        uint32_t atlasSeen = 0;   // build each in-view colour slot's atlas ONCE, not per unit
        for (const auto* u : visUnits_) {
            int slot = colorSlot_[u->player & 7];
            uint32_t bit = (slot >= 0 && slot < 32) ? (1u << slot) : 0u;
            if (!bit || !(atlasSeen & bit)) { atlasFor(slot); atlasSeen |= bit; }
            if (!u->underConstruction)
                if (auto it = anims_.find(u->id);
                    it != anims_.end() && it->second.usesGlow)
                    builtGlow = true;
        }
        // Cycle lodestone/mana/fire crystal frames -- but only once a built glow-unit
        // is on screen, so a still-conjuring lodestone stays dark until it's finished.
        animateGlowTextures(builtGlow);
        // Ensure an impostor sprite exists for every visible model when zoomed out
        // enough that LOD may kick in (main thread; the parallel pass only reads it).
        // Budgeted: a few NEW bakes per frame, so a first zoom-out over a mixed army
        // spreads its render-target allocations/captures across frames instead of
        // bursting them all into one already-slow frame (units not yet baked just
        // draw as full models for a few more frames).
        if (lodEnabled_ && mapView_.zoom() < kLodZoomGate) {
            int budget = 2;
            for (const auto* u : visUnits_) {
                if (!u->type) continue;
                auto key = std::make_pair(unitType_.at(u->id), colorSlot_[u->player & 7]);
                if (impostors_.count(key)) continue;
                if (budget-- <= 0) break;
                ensureImpostor(key.first, key.second, u->type->canMove);
            }
        }
        // Bake sprite sheets for visible models when sprite mode is on -- one NEW
        // set per frame (each is kSprFacings x kSprFrames render-target captures).
        if (spritesEnabled_) {
            // Pass 1: mark every ON-SCREEN sprite set's page used THIS frame, so the LRU
            // in newSprPage() never evicts a page whose sprites are still visible.
            for (const auto* u : visUnits_) {
                if (!u->type) continue;
                auto sit = sprites_.find(std::make_pair(unitType_.at(u->id), colorSlot_[u->player & 7]));
                if (sit != sprites_.end() && sit->second.ready && sit->second.page)
                    for (auto& pg : sprPages_)
                        if (pg.tex == sit->second.page) { pg.lastUse = sprTick_; break; }
            }
            // Pass 2: bake ONE new set this frame (spread the cost); a bake may evict a
            // page NOT stamped above -- never a visible one, never the in-progress page.
            for (const auto* u : visUnits_) {
                if (!u->type) continue;
                auto key = std::make_pair(unitType_.at(u->id), colorSlot_[u->player & 7]);
                if (sprites_.count(key)) continue;
                bakeSprites(key.first, key.second, u->type->canMove, u->type->canFly);
                break;
            }
        }
        double _pt0 = double(SDL_GetPerformanceCounter());
        pool_.parallelFor(visUnits_.size(), [this](size_t b, size_t e) {
            thread_local std::vector<Tri> scratch;
            for (size_t i = b; i < e; ++i)
                buildUnitGeom(*visUnits_[i], geomPool_[i], scratch);
        });
        double _ptFreq = double(SDL_GetPerformanceFrequency()) / 1000.0;
        profProjMs_ += (double(SDL_GetPerformanceCounter()) - _pt0) / _ptFreq;
        // Tally impostor vs full-model draws this frame (for TAK_PROF).
        for (size_t _i = 0; _i < visUnits_.size(); ++_i) {
            const auto& _g = geomPool_[_i];
            if (_g.runs.empty()) continue;
            if (impAtlas_ && _g.runs[0].first == impAtlas_) ++lodDrawn_;
            else ++fullDrawn_;
        }
        double _st0 = double(SDL_GetPerformanceCounter());

        // Is this unit drawn whole by drawUnit (needs clip rects / interleaved
        // effects) rather than folded into the shared batches?
        auto special = [&](const UnitR& u, const UnitGeom& g) {
            bool occluded = !g.canFly && g.occY < g.ay - 2.0f;
            bool conjuring = u.underConstruction && u.type;
            // A worker (conjuring a site or reclaiming) routes through drawUnit too, so
            // the build/reclaim nano-sparkle -- drawn there over BOTH the worker and its
            // target -- shows for any builder/reclaimer, not just occluded/dancing ones.
            bool working = u.type && (u.buildSiteId != 0 || u.reclaimId != 0);
            return occluded || conjuring || working || dancing(u) || headbanging(u);
        };

        // Pass 1: every normal unit's ground shadows, batched. Soft blobs go into
        // one untextured triangle batch; FBI shadow sprites are batched per shadow
        // texture. Drawn first so all shadows sit under all bodies. (Special units
        // draw their own shadow inside drawUnit in pass 2.)
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        shadowBatch_.clear();
        for (const auto& it : items) {
            if (!it.u) continue;
            const auto& u = *it.u;
            // Soft blob batches for every mobile ground unit, special or not (a
            // special unit's own body still draws whole in pass 2).
            if (u.alive() && u.type && u.type->canMove && !u.type->canFly) {
                float sx = (u.x - mapView_.offX()) * zm0 - terrainLiftX(u.x, u.z) * zm0;
                float sy = (u.z - mapView_.offY()) * zm0 + 2 * zm0
                           - terrainLift(u.x, u.z) * zm0;
                pushQuad(shadowBatch_, sx - 7 * zm0, sy - 2.5f * zm0,
                         14 * zm0, 5 * zm0, SDL_Color{0, 0, 0, 70});
            }
        }
        if (!shadowBatch_.empty())
            SDL_RenderGeometry(ren_, nullptr, shadowBatch_.data(),
                               int(shadowBatch_.size()), nullptr, 0);
        {   // FBI shadow sprites, batched by shadow texture (flush on change).
            unitBatch_.clear();
            SDL_Texture* st = nullptr;
            auto flush = [&] {
                if (!unitBatch_.empty())
                    SDL_RenderGeometry(ren_, st, unitBatch_.data(),
                                       int(unitBatch_.size()), nullptr, 0);
                unitBatch_.clear();
            };
            for (const auto& it : items) {
                if (!it.u) continue;
                const auto& u = *it.u;
                int gslot = geomSlot(u.id);
                if (gslot < 0) continue;
                const UnitGeom& g = geomPool_[size_t(gslot)];
                if (special(u, g) || !u.type || u.underConstruction) continue;
                // Impostor-sized units are too small for a ground shadow to read.
                if (impAtlas_ && !g.runs.empty() && g.runs[0].first == impAtlas_) continue;
                const ShadowTex* sh = shadowFor(u.type->shadowArt);
                if (!sh) continue;
                if (sh->tex != st) { flush(); st = sh->tex; }
                float sox = (6.0f + g.alt * 0.5f) * zm0, soy = (3.0f + g.alt * 0.25f) * zm0;
                pushQuad(unitBatch_, g.ax - sh->xoff * zm0 + sox,
                         g.ay - sh->yoff * zm0 + soy, sh->w * zm0, sh->h * zm0,
                         SDL_Color{255, 255, 255, 255});
            }
            flush();
        }

        // Pass 2: bodies (feature sprites + unit models) in depth order. Unit
        // models are accumulated into one batch and flushed only when the texture
        // changes or a feature/special unit interrupts the run -- so a crowd of one
        // unit type collapses to a handful of draw calls instead of one per unit.
        // Plan the draw order serially (no vertex copies), scatter the vertex
        // copies across the worker pool, then replay the draws. This spreads the
        // ~1.5M-vertex body assembly that used to peg one core, while keeping the
        // exact painter order (segments broken by features / special units).
        copyTasks_.clear();
        drawOps_.clear();
        int destOff = 0;
        SDL_Texture* segTex = nullptr;
        int segStart = 0, segCount = 0;
        auto closeSeg = [&] {
            if (segCount > 0) {
                drawOps_.push_back({nullptr, nullptr, segTex, segStart, segCount});
                segCount = 0;
            }
            // Force the next run to re-anchor segStart to the current destOff. Without
            // this, a unit whose texture matches segTex but follows a feature/special
            // unit (which closed the segment) keeps the PREVIOUS segment's segStart and
            // gets drawn from the wrong vertices -- so it renders an earlier unit's body
            // and vanishes from its own spot (camera-dependent, as depth order shifts).
            segTex = nullptr;
        };
        for (const auto& it : items) {
            if (it.f) {
                closeSeg();
                drawOps_.push_back({nullptr, it.f, nullptr, 0, 0});
            } else {
                const auto& u = *it.u;
                int gslot = geomSlot(u.id);
                if (gslot < 0) continue;
                const UnitGeom& g = geomPool_[size_t(gslot)];
                if (special(u, g)) {
                    closeSeg();
                    drawOps_.push_back({&u, nullptr, nullptr, 0, 0});
                    continue;
                }
                int src = 0;
                for (const auto& r : g.runs) {
                    if (r.first != segTex) { closeSeg(); segTex = r.first; segStart = destOff; }
                    copyTasks_.push_back({gslot, src, r.second, destOff});
                    destOff += r.second; segCount += r.second; src += r.second;
                }
            }
        }
        closeSeg();
        bodyVerts_.resize(size_t(destOff));
        pool_.parallelFor(copyTasks_.size(), [this](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const CopyTask& t = copyTasks_[i];
                const auto& gv = geomPool_[size_t(t.geom)].verts;
                std::copy(gv.begin() + t.src, gv.begin() + t.src + t.count,
                          bodyVerts_.begin() + t.dst);
            }
        });
        for (const DrawOp& op : drawOps_) {
            if (op.f) {
                const auto& f = *op.f;
                // Lift the decal onto the terrain relief just like a unit, so a mana
                // deposit sits at the height its heightmap claims (and lodestones/units
                // built on it line up) instead of the decal being flat.
                float lfx = terrainLiftX(f.x, f.z) * zm0, lfy = terrainLift(f.x, f.z) * zm0;
                if (f.shadow) {
                    SDL_FRect sd{(f.x - mapView_.offX() - float(f.sxoff)) * zm0 - lfx,
                                 (f.z - mapView_.offY() - float(f.syoff)) * zm0 - lfy,
                                 float(f.sw) * zm0, float(f.sh) * zm0};
                    SDL_RenderCopyF(ren_, f.shadow, nullptr, &sd);
                }
                SDL_FRect dst{(f.x - mapView_.offX() - float(f.xoff)) * zm0 - lfx,
                              (f.z - mapView_.offY() - float(f.yoff)) * zm0 - lfy,
                              float(f.w) * zm0, float(f.h) * zm0};
                SDL_Texture* tex = f.tex;
                if (f.frames && f.frames->size() > 1)
                    tex = (*f.frames)[(size_t(animClock_ * 8) + size_t(f.seed)) %
                                      f.frames->size()];
                SDL_RenderCopyF(ren_, tex, nullptr, &dst);
            } else if (op.u) {
                drawUnit(*op.u);
            } else if (op.count > 0) {
                SDL_RenderGeometry(ren_, op.tex, bodyVerts_.data() + op.start,
                                   op.count, nullptr, 0);
            }
        }
        profSubmitMs_ += (double(SDL_GetPerformanceCounter()) - _st0) / _ptFreq;

        // Ghosts of the local player's queued (shift) build orders.
        for (const UnitR* _up : front().live) {
            const UnitR& u = *_up;
            if (u.alive() && u.player == localPlayer_)
                for (const auto& bo : u.buildOrders)
                    if (bo.type) drawGhostAt(bo.type, bo.x, bo.z);
        }

        // Projectiles: drawn per weapon family (only where visible).
        float zm = mapView_.zoom();
        for (const auto& p : front().projectiles) {
            if (!cellVisibleR(p.x, p.z)) continue;
            float t = std::clamp(p.age / std::max(p.flight, 0.05f), 0.0f, 1.0f);
            // Flyer shots: lift the whole trajectory by the altitude interpolated
            // from the firing unit down to the target (0.8x, matching the sprite
            // lift), so a drake's breath leaves its mouth and arcs to the ground.
            float palt = (unitAltById(p.fromId) * (1 - t) + unitAltById(p.targetId) * t)
                         * 0.8f * zm;
            if (p.fx == tak::sim::WeaponFx::Lightning) {
                // Flat, fast, jagged blue-white bolt from source toward target.
                float sx = (p.x - mapView_.offX()) * zm - terrainLiftX(p.x, p.z) * zm;
                float sy = (p.z - mapView_.offY()) * zm - 12 * zm - terrainLift(p.x, p.z) * zm
                           - palt;
                float len = 22.0f;
                float bx = -p.vx, bz = -p.vz;
                float bl = std::max(std::sqrt(bx * bx + bz * bz), 1e-3f);
                bx /= bl; bz /= bl;
                float px = sx, py = sy;
                SDL_SetRenderDrawColor(ren_, 210, 230, 255, 255);
                for (int s = 1; s <= 4; ++s) {
                    float d = len * zm * s / 4.0f;
                    float jitter = ((s * 1327 + int(p.age * 900)) % 7 - 3) * 1.6f * zm;
                    float nx = sx + bx * d - bz * jitter;
                    float ny = sy + bz * d + bx * jitter - 12 * zm * s / 4.0f;
                    SDL_RenderDrawLineF(ren_, px, py, nx, ny);
                    px = nx; py = ny;
                }
            } else if (p.fx == tak::sim::WeaponFx::Fire) {
                // Flame breath: a short stream of flickering orange/yellow puffs
                // trailing behind the leading tip, not a single fireball.
                float bx = -p.vx, bz = -p.vz;
                float bl = std::max(std::sqrt(bx * bx + bz * bz), 1e-3f);
                bx /= bl; bz /= bl;
                SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
                for (int s = 0; s < 5; ++s) {
                    float back = s * 6.0f;   // world px behind the tip
                    float wob = ((s * 811 + int(p.age * 1000)) % 5 - 2) * 2.0f;
                    float fx = p.x + bx * back - bz * wob;
                    float fz = p.z + bz * back + bx * wob;
                    float sx = (fx - mapView_.offX()) * zm - terrainLiftX(fx, fz) * zm;
                    float sy = (fz - mapView_.offY()) * zm - 12 * zm - terrainLift(fx, fz) * zm
                               - palt;
                    float r = (4.0f - s * 0.6f) * zm;   // shrinks toward the tail
                    Uint8 aA = Uint8(200 - s * 30);
                    // outer orange
                    SDL_SetRenderDrawColor(ren_, 230, 90, 25, aA);
                    SDL_FRect o{sx - r, sy - r, 2 * r, 2 * r};
                    SDL_RenderFillRectF(ren_, &o);
                    // hot yellow core at the leading puffs
                    if (s < 2) {
                        SDL_SetRenderDrawColor(ren_, 255, 220, 110, aA);
                        SDL_FRect c{sx - r * 0.45f, sy - r * 0.45f, r * 0.9f, r * 0.9f};
                        SDL_RenderFillRectF(ren_, &c);
                    }
                }
            } else {
                // Arrow/bolt/cannonball: a yellow streak. Ballistic weapons (FBI
                // type = Ballistic) lob a high arc scaled by flight time; other
                // shots (line-of-sight bolts) fly nearly flat.
                bool bal = p.wsrc && p.wsrc->ballistic;
                float peak = bal ? std::min(95.0f, p.flight * 55.0f)
                                 : std::min(18.0f, p.flight * 12.0f);
                float h = 8 + 4 * peak * t * (1 - t);
                float sx = (p.x - mapView_.offX()) * zm - terrainLiftX(p.x, p.z) * zm;
                float sy = (p.z - mapView_.offY()) * zm - h * zm - terrainLift(p.x, p.z) * zm
                           - palt;
                SDL_SetRenderDrawColor(ren_, 255, 235, 140, 255);
                SDL_RenderDrawLineF(ren_, sx, sy, sx - p.vx * 0.035f * zm,
                                    sy - p.vz * 0.035f * zm + (t < 0.5f ? 2.5f : -2.5f) * zm);
            }
        }
        drawParticles();
        drawEffects();
        drawUnitFx();

        drawFog();
        if (buildDrag_ && placing_) {
            float mx, mz;
            pickWorld(mouseX_, mouseY_, mx, mz);
            for (auto& [x, z] : buildLinePositions(bdX0_, bdZ0_, mx, mz))
                drawGhostAt(placing_, x, z, !canPlaceLocked(placing_, x, z));
        } else if (placing_) {
            drawGhost();
        }
        if (reclaimDrag_) {
            // Screen-space "clear this area" box, with a marker on every reclaimable
            // feature it currently catches.
            float zm = mapView_.zoom();
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            float x0 = std::min(rdSx0_, mouseX_), y0 = std::min(rdSy0_, mouseY_);
            float x1 = std::max(rdSx0_, mouseX_), y1 = std::max(rdSy0_, mouseY_);
            SDL_FRect box{x0, y0, x1 - x0, y1 - y0};
            SDL_SetRenderDrawColor(ren_, 255, 170, 40, 40);
            SDL_RenderFillRectF(ren_, &box);
            SDL_SetRenderDrawColor(ren_, 255, 190, 70, 220);
            SDL_RenderDrawRectF(ren_, &box);
            float mx, mz;
            pickWorld(mouseX_, mouseY_, mx, mz);
            float minx = std::min(rdX0_, mx), maxx = std::max(rdX0_, mx);
            float minz = std::min(rdZ0_, mz), maxz = std::max(rdZ0_, mz);
            SDL_SetRenderDrawColor(ren_, 255, 210, 90, 230);
            for (const auto& f : world_.features()) {
                if (!f.alive || f.x < minx || f.x > maxx || f.z < minz || f.z > maxz) continue;
                float fsx = (f.x - mapView_.offX()) * zm - terrainLiftX(f.x, f.z) * zm;
                float fsy = (f.z - mapView_.offY()) * zm - terrainLift(f.x, f.z) * zm;
                SDL_FRect m{fsx - 4, fsy - 4, 8, 8};
                SDL_RenderDrawRectF(ren_, &m);
            }
        }

        // Selection membership as a hash set: the old code did frameUnitP(id) (a
        // linear scan) per selected unit and std::find(selection_) per world unit
        // -- both O(n^2) once a big army was selected, which tanked the frame.
        selSet_.clear();
        selSet_.insert(selection_.begin(), selection_.end());
        // Selection brackets: iterate units once, batched into a single draw
        // (viewport-culled, thin green quads).
        shadowBatch_.clear();
        if (!selSet_.empty()) {
            const SDL_Color grn{70, 240, 90, 255};
            float zms = mapView_.zoom();
            // Iterate the (few) selected ids, not the whole world -- frameUnitP(id)
            // is O(1). (A duplicate id would just redraw the same brackets in place.)
            for (int selId : selection_) {
                const UnitR* up = frameUnitP(selId);
                if (!up || !up->alive() || !up->type) continue;
                const UnitR& u = *up;
                float cx = (u.x - mapView_.offX()) * zms - uLiftX(u) * zms;
                float cy = (u.z - mapView_.offY()) * zms - uLiftY(u) * zms;
                if (cx < -40 || cx > mvw + 40 || cy < -40 || cy > winH + 40) continue;
                // Size the brackets to the unit's footprint (world half-extent =
                // foot cells * 8) so a building is boxed at its real size, not a single
                // cell; a small floor keeps mobile units at the old marker size.
                float halfX = std::max(std::max(u.type->footX, 1) * 8.0f, 11.0f);
                float halfZ = std::max(std::max(u.type->footZ, 1) * 8.0f, 8.0f);
                float rx = halfX * zms, ry = halfZ * zms;
                if (rx < 9.0f) {
                    // Tiny on screen (a whole army zoomed out): one small marker
                    // quad instead of eight bracket segments -- 8x less geometry.
                    float s = std::max(2.0f, rx * 0.6f);
                    pushQuad(shadowBatch_, cx - s, cy - s * 0.65f, 2 * s, 2 * s * 0.65f, grn);
                    continue;
                }
                float Lx = std::max(3.0f, rx * 0.4f), Ly = std::max(3.0f, ry * 0.4f);
                float th = std::max(1.0f, 1.2f * zms);
                for (int sx = -1; sx <= 1; sx += 2)
                    for (int sy = -1; sy <= 1; sy += 2) {
                        float px = cx + sx * rx, py = cy + sy * ry;
                        pushQuad(shadowBatch_, std::min(px, px - sx * Lx), py - th * 0.5f,
                                 Lx, th, grn);
                        pushQuad(shadowBatch_, px - th * 0.5f,
                                 std::min(py, py - sy * Ly), th, Ly, grn);
                    }
                for (const auto& o : u.orders)   // move-order rings (few)
                    if (o.targetId == 0) drawRing(o.x, o.z, 4);
            }
            // Attack-target indicator: RED brackets on any enemy a selected unit is
            // ordered to attack, so you can see what you've told them to hit.
            const SDL_Color red{245, 70, 60, 255};
            auto& targets = targetSet_;   // member: reused across frames
            targets.clear();
            for (int sid : selection_)
                if (const auto* su = frameUnitP(sid))
                    for (const auto& o : su->orders)
                        if (o.targetId > 0) targets.insert(o.targetId);
            for (int tid : targets) {
                const auto* t = frameUnitP(tid);
                if (!t || !t->alive() || !t->type) continue;
                SDL_FPoint p = unitScreen(frameUnit(tid));   // includes flyer altitude
                float cx = p.x, cy = p.y + 12.0f * zms;   // undo unitScreen's body bias
                if (cx < -40 || cx > mvw + 40 || cy < -40 || cy > winH + 40) continue;
                // Match the green selection brackets: sized to the target's footprint.
                float halfX = std::max(std::max(t->type->footX, 1) * 8.0f, 11.0f);
                float halfZ = std::max(std::max(t->type->footZ, 1) * 8.0f, 8.0f);
                float rx = halfX * zms, ry = halfZ * zms;
                float Lx = std::max(3.0f, rx * 0.4f), Ly = std::max(3.0f, ry * 0.4f);
                float th = std::max(1.0f, 1.2f * zms);
                for (int sx = -1; sx <= 1; sx += 2)
                    for (int sy = -1; sy <= 1; sy += 2) {
                        float px = cx + sx * rx, py = cy + sy * ry;
                        pushQuad(shadowBatch_, std::min(px, px - sx * Lx), py - th * 0.5f,
                                 Lx, th, red);
                        pushQuad(shadowBatch_, px - th * 0.5f,
                                 std::min(py, py - sy * Ly), th, Ly, red);
                    }
            }
            if (!shadowBatch_.empty()) {
                SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
                SDL_RenderGeometry(ren_, nullptr, shadowBatch_.data(),
                                   int(shadowBatch_.size()), nullptr, 0);
            }
        }

        // Health bars for damaged or selected units -- viewport-culled and batched
        // into one draw call (each was two state-changing FillRects, so a damaged
        // crowd used to break the render batch thousands of times a frame).
        shadowBatch_.clear();
        for (const UnitR* _up : front().live) {
            const UnitR& u = *_up;
            if (!u.alive() || u.embarked() || !u.type) continue;
            if (u.underConstruction && !u.buildBegun) continue;   // ghost: no bar
            if (!alliedToLocal(u.player) && !cellVisibleR(u.x, u.z)) continue;
            float frac = std::clamp(u.hp / u.type->maxHp, 0.0f, 1.0f);
            // Options "HEALTH BARS": 0 = never, 1 = only damaged units (default,
            // the retail behaviour), 2 = every unit, full HP included.
            if (healthBars_ == 0) break;
            if (healthBars_ == 1 && frac >= 1.0f) continue;
            float bw = 26 * zm, bh = std::max(2.0f, 3 * zm);
            float bx = (u.x - mapView_.offX()) * zm - bw / 2 - uLiftX(u) * zm;
            float by = (u.z - mapView_.offY()) * zm - 30 * zm - uLiftY(u) * zm;
            if (bx < -40 || bx > mvw + 40 || by < -40 || by > winH + 40) continue;
            pushQuad(shadowBatch_, bx - 1, by - 1, bw + 2, bh + 2,
                     SDL_Color{10, 10, 10, 220});
            pushQuad(shadowBatch_, bx, by, bw * frac, bh,
                     SDL_Color{uint8_t(230 * (1 - frac) + 40 * frac),
                               uint8_t(200 * frac + 40 * (1 - frac)), 40, 255});
        }
        if (!shadowBatch_.empty()) {
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(ren_, nullptr, shadowBatch_.data(),
                               int(shadowBatch_.size()), nullptr, 0);
        }

        // Control-squad marker: retail-style bright-green number centred BELOW the
        // unit (under where the selection ring/bar sits), "<N>F" for a formation.
        // The number is the recall key (squad 10 shows as "0"). Skipped when zoomed
        // far out so it doesn't clutter the field.
        if (hudFont_.ok() && zm > 0.55f)
            for (const UnitR* _up : front().live) {
                const UnitR& u = *_up;
                if (!u.alive() || u.embarked() || !u.type || u.player != localPlayer_ ||
                    u.squad == 0)
                    continue;
                if (u.underConstruction && !u.buildBegun) continue;
                int num = std::abs(int(u.squad));
                char key = (num == 10) ? '0' : char('0' + num);
                std::string lbl(1, key);
                if (u.squad < 0) lbl += 'F';   // formation
                float cx = (u.x - mapView_.offX()) * zm - uLiftX(u) * zm;
                float cy = (u.z - mapView_.offY()) * zm - uLiftY(u) * zm
                           + (float(std::max(u.type->footZ, 1)) * 8.0f + 7.0f) * zm;
                if (cx < -20 || cx > mvw + 20 || cy < -30 || cy > winH + 30) continue;
                float sc = std::clamp(1.1f * zm, 0.9f, 1.7f);
                float tw = float(hudFont_.width(lbl, sc));
                hudFont_.draw(ren_, lbl, cx - tw / 2, cy, sc,
                              SDL_Color{70, 240, 90, 255});   // the selection-UI green
            }

        // Production progress above busy buildings -- viewport-culled and batched
        // into one draw call, same treatment as the health bars above (the two
        // state-changing FillRects per building broke the render batch each time,
        // and map-wide AI production drew bars at off-screen coordinates).
        shadowBatch_.clear();
        for (const UnitR* _up : front().live) {
            const UnitR& u = *_up;
            if (!u.alive() || u.buildQueue.empty() || !u.type) continue;
            if (!alliedToLocal(u.player) && !cellVisibleR(u.x, u.z)) continue;
            float total = u.buildQueue.front()->buildTime /
                          std::max(u.type->workerTime, 0.01f);
            float frac = std::clamp(u.buildProgress / total, 0.0f, 1.0f);
            float bw = 40 * zm, bh = std::max(3.0f, 4 * zm);
            float bx = (u.x - mapView_.offX()) * zm - bw / 2 - uLiftX(u) * zm;
            float by = (u.z - mapView_.offY()) * zm - float(u.type->footZ) * 8 * zm - 14 * zm
                       - uLiftY(u) * zm;
            if (bx < -60 || bx > mvw + 60 || by < -60 || by > winH + 60) continue;
            pushQuad(shadowBatch_, bx - 1, by - 1, bw + 2, bh + 2,
                     SDL_Color{10, 10, 10, 220});
            pushQuad(shadowBatch_, bx, by, bw * frac, bh,
                     SDL_Color{90, 170, 255, 255});
        }
        if (!shadowBatch_.empty()) {
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(ren_, nullptr, shadowBatch_.data(),
                               int(shadowBatch_.size()), nullptr, 0);
        }

        // Player mana bar top left (legacy; only without the bottom bar). A spectator
        // isn't a player -- no personal mana readout.
        if (!panelTex_ && !spectating_) {
            const PlayerR& tm = framePlayer(localPlayer_);
            float cap = std::max(tm.storage, 100.0f);
            SDL_FRect bg{10, 10, 180, 12};
            SDL_SetRenderDrawColor(ren_, 20, 20, 30, 230);
            SDL_RenderFillRectF(ren_, &bg);
            SDL_FRect fg{12, 12, 176 * std::clamp(tm.mana / cap, 0.0f, 1.0f), 8};
            SDL_SetRenderDrawColor(ren_, 80, 200, 255, 255);
            SDL_RenderFillRectF(ren_, &fg);
            if (hudFont_.ok() && !panelTex_) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "MANA %d/%d  +%d", int(tm.mana), int(cap),
                              int(tm.income));
                hudFont_.draw(ren_, buf, 198, 21, 1.5f, {170, 225, 255, 255});
                if (!selection_.empty()) {
                    const auto* u = frameUnitP(selection_.front());
                    if (u && u->alive() && u->type) {
                        std::snprintf(buf, sizeof buf, "%s  %d/%d", u->type->name.c_str(),
                                      int(u->hp), int(u->type->maxHp));
                        hudFont_.draw(ren_, buf, 12, 40, 1.5f, {220, 220, 190, 255});
                        if (u->type->isBuilder) {
                            const auto menu = conjureMenu(u->type->id);   // mission-filtered
                            std::string m;
                            for (size_t i = 0; i < menu.size() && i < 6; ++i) {
                                const auto* bt = registry_.find(menu[i]);
                                m += std::to_string(i + 1) + ":" +
                                     (bt ? bt->name : menu[i]) + "  ";
                            }
                            if (!m.empty())
                                hudFont_.draw(ren_, m, 12, 58, 1.5f, {180, 200, 170, 255});
                            if (!u->buildQueue.empty()) {
                                std::snprintf(buf, sizeof buf, "TRAINING %s (%zu queued)",
                                              u->buildQueue.front()->name.c_str(),
                                              u->buildQueue.size());
                                hudFont_.draw(ren_, buf, 12, 76, 1.5f, {150, 200, 255, 255});
                            }
                        }
                    }
                }
            }
        }

        // Restore the un-shaken camera so the HUD/panel stays rock-steady.
        if (shaking) mapView_.setOffset(shakeBaseX, shakeBaseY);

        // Done with world-space: drop the clip and draw the right-hand panel and
        // its minimap + order column on a solid strip (never over the map).
        SDL_RenderSetClipRect(ren_, nullptr);
        // Neutral panel strip behind the minimap (kept for spectators too -- it's plain
        // chrome, not the faction art). The command panel + mana bulb + conjure menu
        // (renderGui) and the bottom InfoPanel chrome (drawPanel) are PLAYER-only: a
        // spectator isn't a player and gets neither, just the minimap and the F4 board.
        SDL_SetRenderDrawColor(ren_, 16, 14, 12, 255);
        SDL_FRect panelStrip{float(mvw), 0, float(winW - mvw), float(winH) - barH()};
        SDL_RenderFillRectF(ren_, &panelStrip);
        drawMinimap(winW, winH);
        if (!spectating_) {
            renderGui(winW, winH);
            drawPanel(winW, winH);
            drawObjectivesPanel(winW, winH);
        }
        if (showCounts_) drawUnitCounts(winW);
        if (showHDebug_) drawHDebug();

        // Mission briefing (first 30s) and event notices.
        if (briefTimer_ > 0 && hudFont_.ok()) {
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_FRect bg{float(winW) - 560, 8, 552,
                         14.0f + 16.0f * float(briefing_.size())};
            SDL_SetRenderDrawColor(ren_, 10, 10, 20, 170);
            SDL_RenderFillRectF(ren_, &bg);
            float y = 24;
            for (const auto& l : briefing_) {
                hudFont_.draw(ren_, l, bg.x + 8, y, 1.4f, {220, 215, 180, 255});
                y += 16;
            }
        }
        if (mp_ && hudFont_.ok()) {
            // Dev net-status readout (TAK_NETDEBUG): off by default -- it sat over the
            // bottom-right mana panel. The BEHIND-BY lag warning below always shows.
            static const bool kNetDebug = tak::devEnv("TAK_NETDEBUG") != nullptr;
            if (kNetDebug) {
                char nb[64];
                std::snprintf(nb, sizeof nb, "NET P%d  TICK %u", localPlayer_ + 1, netTick_);
                hudFont_.draw(ren_, nb, 12, float(winH) - barH() - 16, 1.4f,
                              {140, 200, 255, 255});
            }
            // "Behind by N s": how far this client's view lags the live game --
            // the depth of received-but-unplayed bundles (30 Hz). The adaptive
            // buffer keeps only a tiny intended reserve (~netDelay_ ticks, <0.2 s),
            // so surface this only once the lag is clearly abnormal: the machine
            // can't keep up, or a link stall is draining faster than it refills.
            // Only shown past 2 s so ordinary jitter never flashes the warning.
            // Escalates amber -> red toward the ~10 s server reconnect threshold.
            float behindSec = float(mp_->bufferedBundles()) / float(tak::net::kServerHz);
            if (behindSec > 2.0f && outcome_ == 0 && !paused_) {
                char bb[48];
                std::snprintf(bb, sizeof bb, "BEHIND BY %.1fs", behindSec);
                float t = std::min(1.0f, behindSec / 10.0f);
                SDL_Color col{255, uint8_t(210 - int(150 * t)), uint8_t(90 - int(60 * t)), 255};
                hudBanner(bb, 78, 1.8f, col, winW);
            }
            if (!netError_.empty())
                hudBanner("NETWORK: " + netError_, 150, 2.0f, {255, 140, 120, 255}, winW);
        }
        if (scenUnit_ && scenTime_ > 0 && outcome_ == 0 && hudFont_.ok()) {
            char sb[96];
            int rem = int(scenTime_ - scenClock_);
            std::snprintf(sb, sizeof sb, "%s IN %s: %d:%02d", scenUnit_->name.c_str(),
                          scenRegion_.name.c_str(), rem / 60, rem % 60);
            hudBanner(sb, 46, 1.5f, {255, 220, 140, 255}, winW, 12);
        }
        if (pendingCmd_ && hudFont_.ok()) {
            const char* msg = pendingCmd_ == 'a'   ? "ATTACK: CLICK TARGET"
                              : pendingCmd_ == 'f' ? "FIGHT-MOVE: CLICK DESTINATION"
                              : pendingCmd_ == 'p' ? "PATROL: CLICK WAYPOINT"
                              : pendingCmd_ == 'g' ? "GUARD: CLICK FRIENDLY UNIT"
                              : pendingCmd_ == 'c' ? "RECLAIM: CLICK FEATURE"
                              : pendingCmd_ == 'r' ? "REPAIR: CLICK DAMAGED UNIT"
                              : pendingCmd_ == 'l' ? "LOAD: CLICK UNIT TO CARRY"
                              : pendingCmd_ == 'u' ? "UNLOAD: CLICK DESTINATION"
                                                   : "MOVE: CLICK DESTINATION";
            hudBanner(msg, 100, 1.6f, {255, 200, 120, 255}, winW, 12);
        }
        if (noticeTimer_ > 0 && hudFont_.ok() && !notice_.empty())
            hudBanner(notice_, 120, 2.5f, {255, 230, 120, 255}, winW);
        if (paused_ && bigFont_.ok()) {
            const char* msg = "PAUSED";
            float tw = float(bigFont_.width(msg, 1.2f));
            bigFont_.draw(ren_, msg, (winW - tw) / 2, 60, 1.2f, {255, 230, 120, 255});
        }

        // Victory / defeat banner. A spectator/replay viewer isn't a participant,
        // so it names the winner instead of "VICTORY"/"DEFEAT".
        if (outcome_ != 0 && bigFont_.ok()) {
            std::string msg;
            SDL_Color col{255, 220, 90, 255};
            if (spectating_ || replayMode_) {
                int wt = frameWinningTeam();
                msg = wt >= 0 ? "TEAM " + std::to_string(wt + 1) + " WINS" : "GAME OVER";
            } else {
                msg = outcome_ > 0 ? "VICTORY" : "DEFEAT";
                if (outcome_ < 0) col = {255, 90, 70, 255};
            }
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_FRect shade{0, float(winH) / 2 - 60, float(winW), 120};
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 150);
            SDL_RenderFillRectF(ren_, &shade);
            float tw = float(bigFont_.width(msg, 1.5f));
            bigFont_.draw(ren_, msg, (winW - tw) / 2, float(winH) / 2 + 24, 1.5f, col);
            const char* hint = "PRESS ESC FOR MENU";
            blockText(hint, (winW - blockWidth(hint, 2.0f)) / 2, float(winH) / 2 + 74, 2.0f,
                      {220, 220, 230, 255});
        }

        if (dragging_) {
            SDL_FRect r{std::min(dragX0_, dragX1_), std::min(dragY0_, dragY1_),
                        std::abs(dragX1_ - dragX0_), std::abs(dragY1_ - dragY0_)};
            SDL_SetRenderDrawColor(ren_, 120, 255, 150, 200);
            SDL_RenderDrawRectF(ren_, &r);
        }

        // Spectator badge: a live watcher can pan/zoom but issues no orders. Use the
        // crisp 5x7 block font (the scaled GAF hudFont smeared/overlapped here).
        if (spectating_) {
            std::string m = "SPECTATING";
            if (benchmarkMode_) {   // append a countdown from the benchmark duration to 0
                uint32_t end = world_.benchmarkEndTick(), gt = front().gameTick;
                int rem = (end == 0) ? int(end) : (gt < end ? int((end - gt + 29) / 30) : 0);
                if (end == 0) rem = 60;   // not started yet -> full duration
                char buf[40];
                std::snprintf(buf, sizeof buf, "BENCHMARKING  %d:%02d", rem / 60, rem % 60);
                m = buf;
            }
            float px = 3.0f;
            float tw = blockWidth(m, px), th = 7 * px;
            float bx = (winW - tw) / 2, by = 24;
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_FRect bg{bx - 14, by - 8, tw + 28, th + 16};
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 150);
            SDL_RenderFillRectF(ren_, &bg);
            blockText(m, bx, by, px, {235, 175, 110, 255});
        }

        // Replay scrubber: elapsed/total time and a progress bar above the HUD.
        if (replayMode_ && hudFont_.ok()) {
            int cur = int(replayTick_) / 30, tot = int(replayLength()) / 30;
            char sb[64];
            std::snprintf(sb, sizeof sb, "REPLAY  %d:%02d / %d:%02d%s",
                          cur / 60, cur % 60, tot / 60, tot % 60,
                          paused_ ? "  PAUSED" : "");
            float bw = 360, bx = (winW - bw) / 2, by = float(winH) - 118;
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_FRect bg{bx - 8, by - 22, bw + 16, 44};
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 150);
            SDL_RenderFillRectF(ren_, &bg);
            SDL_FRect track{bx, by + 6, bw, 6};
            SDL_SetRenderDrawColor(ren_, 90, 95, 110, 220);
            SDL_RenderFillRectF(ren_, &track);
            float frac = replayLength() ? float(replayTick_) / float(replayLength()) : 0;
            SDL_FRect fill{bx, by + 6, bw * frac, 6};
            SDL_SetRenderDrawColor(ren_, 235, 205, 110, 255);
            SDL_RenderFillRectF(ren_, &fill);
            hudFont_.draw(ren_, sb, bx, by - 14, 1.6f, {235, 230, 210, 255});
        }

        // In-game chat: recent lines bottom-left, plus a composer while typing.
        if (mp_ && hudFont_.ok() && (chatTyping_ || !gameChat_.empty())) {
            const float kFade = 10.0f;   // seconds a line stays up when not typing
            // Gather what will be drawn (bottom-up) so we can back it with one
            // translucent strip -- readable over bright terrain.
            std::vector<std::pair<std::string, SDL_Color>> lines;
            if (chatTyping_)
                lines.push_back({"SAY> " + chatDraft_ + "_", {255, 245, 180, 255}});
            int shown = 0;
            for (auto it = gameChat_.rbegin(); it != gameChat_.rend() && shown < 6; ++it) {
                if (!chatTyping_ && it->age > kFade) continue;
                lines.push_back({it->who + ": " + it->text, {225, 228, 236, 255}});
                ++shown;
            }
            float x = 14, y = float(winH) - barH() - 14;
            float wMax = 0;
            for (auto& l : lines) wMax = std::max(wMax, float(hudFont_.width(l.first, 1.7f)));
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_FRect bg{x - 8, y - float(lines.size()) * 21 + 4,
                         std::min(wMax + 16, float(winW) - x), float(lines.size()) * 21 + 6};
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 140);
            SDL_RenderFillRectF(ren_, &bg);
            for (auto& l : lines) {           // lines[0] is the composer / newest
                float sc = (&l == &lines.front() && chatTyping_) ? 1.8f : 1.6f;
                hudFont_.draw(ren_, l.first, x, y, sc, l.second);
                y -= 21;
            }
        }

        // In-game exit menu overlay -- drawn last so it sits above the HUD. Screen
        // space (like the game-over banner); hit-rects are rebuilt here each frame
        // and consumed by input() (see the exitMenu_ branch).
        if (exitMenu_) {
            exitHots_.clear();
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_FRect dim{0, 0, float(winW), float(winH)};
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 150);
            SDL_RenderFillRectF(ren_, &dim);

            const float bw = 280, bh = 50, gap = 14, pad = 32, titlePx = 3.0f;
            const int nBtn = canReturnToMenu_ ? 5 : 4;
            const float titleH = 7 * titlePx + 24;
            const float pw = bw + pad * 2;
            const float ph = pad * 2 + titleH + nBtn * bh + (nBtn - 1) * gap;
            const float px0 = (winW - pw) / 2, py0 = (winH - ph) / 2;

            SDL_FRect panel{px0, py0, pw, ph};
            SDL_SetRenderDrawColor(ren_, 26, 28, 36, 240);
            SDL_RenderFillRectF(ren_, &panel);
            SDL_SetRenderDrawColor(ren_, 120, 130, 160, 255);
            SDL_RenderDrawRectF(ren_, &panel);

            const char* title = "GAME MENU";
            blockText(title, px0 + (pw - blockWidth(title, titlePx)) / 2, py0 + pad, titlePx,
                      {235, 225, 180, 255});

            float bx = px0 + pad, by = py0 + pad + titleH;
            auto btn = [&](const std::string& label, std::function<void()> action) {
                SDL_FRect r{bx, by, bw, bh};
                bool hot = mouseX_ >= r.x && mouseX_ <= r.x + r.w &&
                           mouseY_ >= r.y && mouseY_ <= r.y + r.h;
                SDL_SetRenderDrawColor(ren_, hot ? 90 : 60, hot ? 110 : 66, hot ? 150 : 86, 255);
                SDL_RenderFillRectF(ren_, &r);
                SDL_SetRenderDrawColor(ren_, hot ? 180 : 90, hot ? 200 : 100, hot ? 240 : 130, 255);
                SDL_RenderDrawRectF(ren_, &r);
                float tpx = 2.5f, tw = blockWidth(label, tpx);
                blockText(label, bx + (bw - tw) / 2, by + (bh - 7 * tpx) / 2, tpx,
                          {228, 232, 242, 255});
                exitHots_.push_back({r, std::move(action)});
                by += bh + gap;
            };
            btn("RESUME", [this] { exitMenu_ = false; });
            btn("OPTIONS", [this] { exitMenu_ = false; openOptions(); });
            btn("CONTROLS", [this] { exitMenu_ = false; openHotkeys(); });   // hotkey rebinding
            if (canReturnToMenu_) btn("MAIN MENU", [this] { menuRequested_ = true; });
            btn("QUIT", [this] { quitRequested_ = true; });
        }
        if (benchStatsShown_) renderBenchmarkStats(winW, winH);   // benchmark results, above the frozen game
        if (options_) options_->render(winW, winH);   // topmost of all
        if (hotkeysScreen_) hotkeysScreen_->render(winW, winH);   // above Options
    }

    const tak::tdo::Model* GameView::ghostModel(const std::string& typeId) {
        auto it = visuals_.find(typeId);
        if (it != visuals_.end()) return &it->second.model;
        try {
            visuals_[typeId] = {tak::tdo::load(vread("objects3d/" + typeId + ".3do"))};
            return &visuals_[typeId].model;
        } catch (const std::exception&) {}
        return nullptr;
    }

    void GameView::animateGlowTextures(bool live) {
        AaScaleReset _sr(ren_);   // bakes render at 1:1 even when whole-frame AA is on
        if (animatedTex_.empty()) return;
        int frame = live ? int(animClock_ * 4.0f) : 0;   // ~4 fps
        SDL_Texture* prev = SDL_GetRenderTarget(ren_);
        bool onAny = false;
        for (SDL_Texture* atlas : atlasTex_) {
            if (!atlas) continue;
            SDL_SetRenderTarget(ren_, atlas);
            onAny = true;
            for (const auto& name : animatedTex_) {
                auto rit = atlasRect_.find(name);
                auto tit = textures_.find(name);
                if (rit == atlasRect_.end() || tit == textures_.end() ||
                    tit->second.size() < 2)
                    continue;
                SDL_Texture* f = tit->second[size_t(frame) % tit->second.size()];
                SDL_Rect r = rit->second;
                SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
                SDL_RenderFillRect(ren_, &r);          // clear the region (transparent)
                SDL_BlendMode fb;
                SDL_GetTextureBlendMode(f, &fb);
                SDL_SetTextureBlendMode(f, SDL_BLENDMODE_NONE);
                SDL_RenderCopy(ren_, f, nullptr, &r);  // overwrite with this frame's RGBA
                SDL_SetTextureBlendMode(f, fb);
            }
        }
        if (onAny) SDL_SetRenderTarget(ren_, prev);
    }

    void GameView::autoTuneSprites(float frameMs) {
        frameEma_ = frameEma_ * 0.85f + frameMs * 0.15f;
        if (spriteMode_ == SPR_ON)  { spritesEnabled_ = true;  return; }
        if (spriteMode_ == SPR_OFF) {
            spritesEnabled_ = false;
            if (!sprPages_.empty()) freeSpritePages();
            return;
        }
        // AUTO. Sprites help only a real crowd, so gate both on frames slower than
        // ~55fps (18ms) AND enough on-screen units -- that keeps a startup/asset
        // hitch with few units from latching them on. A kept-up frame reads ~16.6ms
        // (cap) or faster, so it can't reveal how much headroom a light scene has;
        // turn sprites back off by crowd size (wide 64-on / 32-off gap = no flapping).
        if (!spritesEnabled_) {
            if (frameEma_ > 18.0f && visUnits_.size() >= 64) spritesEnabled_ = true;
        } else if (visUnits_.size() < 32) {
            spritesEnabled_ = false;
            freeSpritePages();   // return the 64MB/page VRAM while sprites are off
        }
    }

    void GameView::invalidateRenderTargets() {
        freeSpritePages();
        for (SDL_Texture* t : atlasTex_) if (t) gpuvram::destroy(t);
        atlasTex_.clear();
        impostors_.clear();
        if (impAtlas_) { gpuvram::destroy(impAtlas_); impAtlas_ = nullptr; }
        impCurX_ = impCurY_ = impShelfH_ = 0;
    }

    void GameView::freeSpritePages() {
        for (auto& p : sprPages_) if (p.tex) gpuvram::destroy(p.tex);
        sprPages_.clear();
        sprites_.clear();
    }

    void GameView::buildAtlasLayout() {
        atlasLaidOut_ = true;
        struct Item { const std::string* name; int w, h; };
        std::vector<Item> items;
        items.reserve(textures_.size());
        for (auto& [name, frames] : textures_) {
            if (frames.empty()) continue;
            int w = 0, h = 0;
            SDL_QueryTexture(frames[0], nullptr, nullptr, &w, &h);
            if (w <= 0 || h <= 0 || w > 512 || h > 512) continue;   // skip oddities
            items.push_back({&name, w, h});
        }
        // Tallest first packs tightest.
        std::sort(items.begin(), items.end(),
                  [](const Item& a, const Item& b) { return a.h > b.h; });
        const int pad = 2, W = 2048;
        int x = pad, y = pad, shelfH = 0;
        for (const auto& it : items) {
            if (x + it.w + pad > W) { x = pad; y += shelfH + pad; shelfH = 0; }
            atlasRect_[*it.name] = SDL_Rect{x, y, it.w, it.h};
            x += it.w + pad;
            shelfH = std::max(shelfH, it.h);
        }
        atlasW_ = W;
        atlasH_ = y + shelfH + pad;
    }

    SDL_Texture* GameView::atlasFor(int slot) {
        AaScaleReset _sr(ren_);
        if (slot < 0) slot = 0;
        if (!atlasLaidOut_) buildAtlasLayout();
        if (atlasW_ <= 0) return nullptr;
        if (int(atlasTex_.size()) <= slot) atlasTex_.resize(size_t(slot) + 1, nullptr);
        if (atlasTex_[slot]) return atlasTex_[slot];
        if (gpuAllocBlocked()) return nullptr;   // don't retry a failed alloc per frame
        SDL_Texture* atlas = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32,
                                               SDL_TEXTUREACCESS_TARGET, atlasW_, atlasH_);
        if (!atlas) { noteGpuAllocFail(); return nullptr; }
        SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
        SDL_Texture* prev = SDL_GetRenderTarget(ren_);
        SDL_SetRenderTarget(ren_, atlas);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
        SDL_RenderClear(ren_);
        for (const auto& [name, r] : atlasRect_) {
            auto it = textures_.find(name);
            if (it == textures_.end() || it->second.empty()) continue;
            size_t ci = size_t(slot) < it->second.size() ? size_t(slot) : 0;
            SDL_Rect dst = r;
            SDL_RenderCopy(ren_, it->second[ci], nullptr, &dst);
        }
        SDL_SetRenderTarget(ren_, prev);
        SDL_SetTextureScaleMode(atlas, SDL_ScaleModeNearest);   // no atlas edge bleed
        atlasTex_[slot] = atlas;
        return atlas;
    }

    void GameView::bakeSprites(const std::string& typeId, int slot, bool canMove, bool canFly) {
        AaScaleReset _sr(ren_);
        auto key = std::make_pair(typeId, slot);
        if (sprites_.count(key)) return;
        // During an allocation backoff, don't reserve the key yet -- so the bake
        // happens properly once VRAM pressure clears, instead of never.
        if (gpuAllocBlocked()) return;
        sprites_[key] = SpriteSet{};   // reserve (not ready)
        auto vt = visuals_.find(typeId);
        if (vt == visuals_.end()) return;
        std::vector<std::string> names;
        auto vm = loadTypeVm(typeId, names);
        if (!vm) return;
        Anim tmp;
        tmp.pieceNames = &names;   // local to this bake; read-only while tmp is alive
        tmp.vm = std::move(vm);
        tmp.flyGate = flyGateOf(*tmp.vm);
        tmp.moveGate = walkGateOf(tmp.vm->file());
        bool hasWalk = hasWalkCycle(tmp.vm->file());
        bool animated = canMove || canFly;
        // (Re)start the locomotion animation from the top -- used before each bake
        // attempt so a retry on a fresh page re-captures the same frames.
        auto initAnim = [&] {
            tmp.vm->reset();
            if (canFly) { tmp.vm->setStatic(tmp.flyGate, 1); tmp.vm->start("fly");
                          for (int s = 0; s < 8; ++s) tmp.vm->tick(1.0f / 30); }
            else if (canMove && hasWalk) {
                // Ground walk scripts gate their leg motion on their moving-flag
                // static (walkGateOf: 0 for most, 3 for the Veruna monarch); without it
                // walk_legs no-ops and every baked frame is the same standing pose. Set
                // it, exactly as the live update loop does, so the bake captures a real
                // walk cycle.
                tmp.vm->setStatic(tmp.moveGate, 1);
                tmp.vm->start("walk") || tmp.vm->start("walk_legs");
            }
            else {
                // Static buildings AND mobile no-walk movers (ships' oars, wheeled
                // vehicles' wheels/props): run the COB constructor exactly as registerUnit
                // does for the live unit, then let it settle, so the bake reflects the same
                // default piece visibility (e.g. the Death Totem's Create hides its
                // vetskull* pieces) and captures a moving unit's ambient loop.
                tmp.vm->start("Create");
                for (int s = 0; s < 6; ++s) tmp.vm->tick(1.0f / 30);
            }
        };
        // Advance the bake VM one step. A ground walk script is single-pass, so (like
        // the live loop) re-invoke it once its thread ends, keeping the legs cycling
        // across the whole bake window instead of freezing after the first pass.
        auto stepVm = [&](float dt) {
            tmp.vm->tick(dt);
            if (canMove && !canFly && tmp.vm->threadCount() == 0) {
                if (hasWalk) tmp.vm->start("walk") || tmp.vm->start("walk_legs");
                else tmp.vm->start("Create");   // no-walk mover: keep its ambient loop going
            }
        };
        SDL_Texture* atlas = atlasFor(slot);
        // A page-allocation failure is transient (VRAM pressure) -- un-reserve the
        // key so this type rebakes after the backoff, unlike the permanent
        // no-COB/no-model reservations above.
        if (sprPages_.empty() && !newSprPage()) { sprites_.erase(key); return; }
        SpriteSet ss;
        ss.frames = animated ? kSprFrames : 1;
        std::vector<Tri> scratch;
        SDL_Texture* prev = SDL_GetRenderTarget(ren_);
        const float S = kImpScale, kPi = 3.14159265f;
        // Estimate the real locomotion cycle length so the baked frames span one
        // actual cycle (not a guessed 0.9s). TA locomotion scripts play once then
        // hold, so the cycle = how long the pose keeps changing; if it never
        // settles (a truly looping script) or settles instantly, fall back to 0.9s.
        auto sig = [&] {
            double s = 0;
            for (const auto& pc : tmp.vm->pieces())
                s += std::sin(pc.rot[0]) + std::sin(pc.rot[1]) + std::sin(pc.rot[2])
                   + pc.move[0] + pc.move[1] + pc.move[2];
            return float(s);
        };
        float period = 0.9f;
        if (animated) {
            initAnim();
            const float dt = 1.0f / 60.0f;
            float prev = sig();
            int stable = 0;
            for (float t = dt; t < 2.5f; t += dt) {
                tmp.vm->tick(dt);   // no re-invoke: let one walk pass settle = the cycle
                float s = sig();
                if (std::fabs(s - prev) < 1e-4f) {
                    if (++stable >= 6 && t - 6 * dt > 0.25f) { period = t - 6 * dt; break; }
                } else stable = 0;
                prev = s;
            }
            period = std::clamp(period, 0.3f, 2.0f);
        }
        bool atlasFull = false, pageAllocFailed = false;
        SDL_Texture* target = sprPages_.back().tex;
        // Capture the current VM pose at facing fi into a packed cell of `target`.
        auto capture = [&](int fi, SDL_Rect& outR, SDL_FRect& outB) {
            float heading = float(fi) / kSprFacings * 2.0f * kPi;
            // Every mover, flyers included, faces -heading: with the piece X/Y
            // negation the fly pose no longer needs a per-flyer 180 (retail has no
            // flyer facing branch, root matrix 0x4ee620 identical for all units).
            float facing = (canFly || canMove) ? -heading : 0.0f;
            scratch.clear();
            collect(scratch, atlas, vt->second.model.root, Xform{}, &tmp, facing, 0, false);
            std::stable_sort(scratch.begin(), scratch.end(),
                      [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            for (auto& t : scratch)
                for (int i = 0; i < 3; ++i) {
                    minX = std::min(minX, t.v[i].position.x);
                    minY = std::min(minY, t.v[i].position.y);
                    maxX = std::max(maxX, t.v[i].position.x);
                    maxY = std::max(maxY, t.v[i].position.y);
                }
            if (scratch.empty()) { minX = minY = 0; maxX = maxY = 1; }
            const int pad = 2;
            int w = std::clamp(int(std::ceil((maxX - minX) * S)) + 2 * pad, 2, 400);
            int h = std::clamp(int(std::ceil((maxY - minY) * S)) + 2 * pad, 2, 400);
            SprPage& pg = sprPages_.back();   // the in-progress bake target owns the cursor
            if (pg.curX + w > sprAtlasDim_) { pg.curX = 0; pg.curY += pg.shelfH + 1; pg.shelfH = 0; }
            if (pg.curY + h > sprAtlasDim_) { atlasFull = true; return; }
            int rx = pg.curX, ry = pg.curY;
            for (auto& t : scratch) {
                SDL_Vertex v[3];
                for (int i = 0; i < 3; ++i) {
                    v[i] = t.v[i];
                    v[i].position.x = (t.v[i].position.x - minX) * S + float(rx + pad);
                    v[i].position.y = (t.v[i].position.y - minY) * S + float(ry + pad);
                }
                SDL_RenderGeometry(ren_, t.tex, v, 3, nullptr, 0);
            }
            outR = SDL_Rect{rx, ry, w, h};
            outB = SDL_FRect{minX - pad / S, minY - pad / S, w / S, h / S};
            pg.curX += w + 1;
            pg.shelfH = std::max(pg.shelfH, h);
        };
        // Bake all frames into the current page; if it overflows, start a fresh
        // page and re-bake from the top (at most one retry -- a type that can't fit
        // an empty page is left not-ready and just uses its full model).
        for (int attempt = 0; attempt < 2; ++attempt) {
            initAnim();
            target = sprPages_.back().tex;
            SDL_SetRenderTarget(ren_, target);
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            atlasFull = false;
            for (int k = 0; k < ss.frames && !atlasFull; ++k) {
                if (k > 0) for (int s = 0; s < 4; ++s) stepVm(period / ss.frames / 4);
                for (int fi = 0; fi < kSprFacings && !atlasFull; ++fi)
                    capture(fi, ss.rect[fi][k], ss.bbox[fi][k]);
            }
            if (!atlasFull) { ss.page = target; break; }
            if (attempt == 0 && !newSprPage()) { pageAllocFailed = true; break; }
        }
        SDL_SetRenderTarget(ren_, prev);
        // Transient VRAM failure: un-reserve so the type rebakes after the backoff.
        if (pageAllocFailed) { sprites_.erase(key); return; }
        if (atlasFull || !ss.page) return;   // doesn't fit a fresh page -> full model
        ss.period = period;
        ss.ready = true;
        sprites_[key] = ss;
    }

    void GameView::buildUnitGeom(const UnitR& u, UnitGeom& g, std::vector<Tri>& scratch) {
        g.verts.clear();
        g.runs.clear();
        g.canFly = u.type && u.type->canFly;
        if (u.underConstruction && !u.buildBegun) return;   // ghost drawn serially
        auto ut = unitType_.find(u.id);   // defensive: a throw here would abort
        if (ut == unitType_.end()) return;
        auto vt = visuals_.find(ut->second);
        if (vt == visuals_.end()) return;
        const Anim* anim = nullptr;
        auto at = anims_.find(u.id);
        if (at != anims_.end()) anim = &at->second;

        float zm = mapView_.zoom();
        int slot = colorSlot_[u.player & 7];
        // Interpolated pose so the unit glides between 30Hz sim ticks (lift computed at the
        // interpolated spot so it stays seated on the terrain as it moves).
        float ix, iz, ih; interpPose(u, ix, iz, ih);
        float ax = (ix - mapView_.offX()) * zm - terrainLiftX(ix, iz) * zm;
        float ay = (iz - mapView_.offY()) * zm - terrainLift(ix, iz) * zm;
        // Sprite sheet: draw a moving/idle unit as one animated quad from the baked
        // locomotion cycle. Attack/death poses keep the full 3D model (rare).
        if (spritesEnabled_) {
            auto sit = sprites_.find(std::make_pair(vt->first, slot));
            // A grounded/idle unit shows a static frame (no flap); it only cycles
            // the animation while airborne (flyers) or moving (ground). Attack/death
            // poses keep the full 3D model.
            bool grounded = (u.type && u.type->canFly) && !(anim && anim->airborne);
            bool special = anim && (anim->dying || anim->firing);
            if (sit != sprites_.end() && sit->second.ready && !special) {
                const SpriteSet& ss = sit->second;
                int fi = facingIndex((u.type && u.type->canMove) ? ih : 0.0f,
                                     kSprFacings);
                int frame = 0;
                if (ss.frames > 1 && !grounded) {
                    bool moving = (u.type && u.type->canFly) || u.moving();
                    if (moving) {
                        float rate = float(ss.frames) / std::max(0.05f, ss.period);
                        frame = (int(animClock_ * rate) + u.id) % ss.frames;
                    }
                }
                const SDL_Rect& r = ss.rect[fi][frame];
                const SDL_FRect& bb = ss.bbox[fi][frame];
                if (r.w > 2 && r.h > 2) {   // else falls to full model
                    float alt = g.canFly ? (anim ? anim->altitude : u.type->cruiseAlt) : 0.0f;
                    float qx = ax + bb.x * zm;
                    float qy = ay + bb.y * zm - alt * 0.8f * zm;
                    float inv = 1.0f / float(sprAtlasDim_);
                    pushQuadUV(g.verts, qx, qy, bb.w * zm, bb.h * zm,
                               float(r.x) * inv, float(r.y) * inv,
                               float(r.x + r.w) * inv, float(r.y + r.h) * inv,
                               SDL_Color{255, 255, 255, 255});
                    g.runs.push_back({ss.page, 6});
                    g.ax = ax; g.ay = ay; g.alt = alt;
                    g.occY = wallOcclusionY(u.x, u.z);
                    return;
                }
            }
        }
        // Level of detail: only when really zoomed out (zoom below the gate), draw a
        // unit small enough on screen (< lodPx_ tall) as a cached impostor billboard
        // instead of its model. At normal/close zoom every unit keeps full 3D.
        if (lodEnabled_ && zm < kLodZoomGate) {
            auto hit = modelH_.find(vt->first);
            auto iit = impostors_.find(std::make_pair(vt->first, slot));
            if (hit != modelH_.end() && iit != impostors_.end() && iit->second.ready
                && hit->second * zm < lodPx_) {
                const Impostor& imp = iit->second;
                int f = facingIndex((u.type && u.type->canMove) ? ih : 0.0f, kFacings);
                const SDL_Rect& r = imp.rect[f];
                const SDL_FRect& bb = imp.bbox[f];
                float alt = g.canFly ? (anim ? anim->altitude : u.type->cruiseAlt) : 0.0f;
                float qx = ax + bb.x * zm;
                float qy = ay + bb.y * zm - alt * 0.8f * zm;   // ~lift a flyer's sprite
                float inv = 1.0f / float(impAtlasDim_);
                pushQuadUV(g.verts, qx, qy, bb.w * zm, bb.h * zm,
                           float(r.x) * inv, float(r.y) * inv,
                           float(r.x + r.w) * inv, float(r.y + r.h) * inv,
                           SDL_Color{255, 255, 255, 255});
                g.runs.push_back({impAtlas_, 6});
                g.ax = ax; g.ay = ay; g.alt = alt;
                g.occY = wallOcclusionY(u.x, u.z);
                return;
            }
        }

        scratch.clear();
        Xform base;
        if (u.type && u.type->canFly && u.type->cruiseAlt > 0)
            base.t[1] = anim ? anim->altitude : u.type->cruiseAlt;
        // Flyers face -heading exactly like ground movers (no flyer facing branch).
        float facing = (u.type && (u.type->canMove || u.type->canFly)) ? -ih : 0.0f;
        // Disco emote: a dancing monarch spins, bobs and hue-cycles. Local wall-time
        // (animClock_) drives the smooth motion; world_.discoActive() (a synced sim
        // timer) gates it. Pure client-side eye-candy -- nothing here is hashed.
        bool disco = dancing(u);
        float discoBob = 0.0f, discoMix = 0.0f;
        SDL_Color discoCol{};
        if (disco) {
            float t = animClock_;
            facing += t * 6.2831853f;                                // ~1 rev/sec
            discoBob = std::fabs(std::sin(t * 8.0f)) * 11.0f * zm;    // bounce, px
            discoCol = discoHue(t * 0.8f);                           // body tint hue
            discoMix = 0.5f;
        }
        // Headbang emote (Shift+H): no spin -- a sharp downward nod synced to the metal
        // beat (~152 BPM), the body whipping side to side and flashing red on each bang.
        if (headbanging(u)) {
            float ph = animClock_ * 2.533f * 6.2831853f;             // ~152 bangs/min
            float bang = std::pow(std::max(0.0f, std::sin(ph)), 2.0f);
            discoBob = -bang * 16.0f * zm;                           // dip DOWN (nod)
            facing += std::sin(ph) * 0.55f;                          // hair-whip
            discoCol = SDL_Color{210, 40, 40, 255};                 // deep metal red
            discoMix = bang * 0.5f;                                  // flash on the bang
            disco = true;                                            // reuse the tint path
        }
        bool mirror = false;
        SDL_Texture* atlas = (slot >= 0 && size_t(slot) < atlasTex_.size())
                                 ? atlasTex_[size_t(slot)] : nullptr;
        collect(scratch, atlas, vt->second.model.root, base, anim, facing, u.player, mirror);
        std::stable_sort(scratch.begin(), scratch.end(),
                  [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
        g.ax = ax; g.ay = ay;
        g.alt = anim ? anim->altitude : 0.0f;
        g.occY = wallOcclusionY(u.x, u.z);
        // "Materialising" = the summon fade-in/shimmer: either a site still conjuring
        // (progress = HP fraction) or a unit just summoned from a building (progress =
        // its viewer-only birth ramp). Both fade alpha in and glow while p < 1.
        float birthP = birthProgress(u.id);
        bool conjuring = u.type && (u.underConstruction || birthP < 1.0f);
        float p = !u.type ? 1.0f
                  : u.underConstruction ? std::clamp(u.hp / u.type->maxHp, 0.0f, 1.0f)
                                        : birthP;
        Uint8 alpha = Uint8(p * 255.0f);
        float vetGold = (!conjuring && u.veteran >= 4)
                            ? float(std::min(u.veteran, 10) - 3) / 7.0f * 0.5f : 0.0f;
        SDL_Texture* cur = nullptr;
        int runStart = 0;
        for (auto& t : scratch) {
            if (t.tex != cur) {
                if (int(g.verts.size()) > runStart)
                    g.runs.push_back({cur, int(g.verts.size()) - runStart});
                cur = t.tex;
                runStart = int(g.verts.size());
            }
            for (int i = 0; i < 3; ++i) {
                SDL_Vertex v = t.v[i];
                v.position.x = v.position.x * zm + ax;
                v.position.y = v.position.y * zm + ay - discoBob;   // disco bounce (0 otherwise)
                if (vetGold > 0) {
                    v.color.r = Uint8(v.color.r + (255 - v.color.r) * vetGold);
                    v.color.g = Uint8(v.color.g + (200 - v.color.g) * vetGold * 0.85f);
                    v.color.b = Uint8(v.color.b * (1.0f - vetGold * 0.7f));
                }
                if (conjuring) {
                    float pulse = 0.5f + 0.5f * std::sin(animClock_ * 7.0f +
                                                         v.position.y * 0.03f);
                    float glow = (1.0f - p) * pulse;
                    v.color.a = alpha;
                    v.color.r = Uint8(v.color.r * (1.0f - 0.75f * glow));
                    v.color.g = Uint8(v.color.g * (1.0f - 0.25f * glow));
                }
                if (disco) {   // blend the body toward the cycling disco hue
                    v.color.r = Uint8(int(v.color.r) + int((int(discoCol.r) - int(v.color.r)) * discoMix));
                    v.color.g = Uint8(int(v.color.g) + int((int(discoCol.g) - int(v.color.g)) * discoMix));
                    v.color.b = Uint8(int(v.color.b) + int((int(discoCol.b) - int(v.color.b)) * discoMix));
                }
                g.verts.push_back(v);
            }
        }
        if (int(g.verts.size()) > runStart)
            g.runs.push_back({cur, int(g.verts.size()) - runStart});
    }

    void GameView::drawUnit(const UnitR& u) {
        // A placed-but-not-yet-started site shows as a faint ghost until the
        // builder arrives and it begins conjuring for real.
        if (u.underConstruction && !u.buildBegun) {
            if (u.type) drawGhostAt(u.type, u.x, u.z);
            return;
        }
        auto vt = visuals_.find(unitType_.at(u.id));
        if (vt == visuals_.end()) return;
        const Anim* anim = nullptr;
        auto at = anims_.find(u.id);
        if (at != anims_.end()) anim = &at->second;

        // The model projection (collect + sort + screen transform + colour) was
        // done for every visible unit in parallel on the worker pool this frame;
        // here we just look up the result and submit its draw calls.
        int gslot = geomSlot(u.id);
        if (gslot < 0) return;
        UnitGeom& g = geomPool_[size_t(gslot)];
        float zm = mapView_.zoom();
        float ax = g.ax, ay = g.ay;
        // Terrain occlusion: if a wall between the unit and the camera projects its
        // top above the unit's feet, clip the model to that line and re-draw the
        // hidden part as a faint player-tinted silhouette showing through the wall.
        // Flyers ride above the terrain, so a wall never hides them.
        float occY = g.occY;
        bool occluded = !g.canFly && occY < ay - 2.0f;
        int outW = 0, outH = 0;
        SDL_bool hadClip = SDL_FALSE;
        SDL_Rect prevClip{};
        if (occluded) {
            SDL_GetRendererOutputSize(ren_, &outW, &outH);
            hadClip = SDL_RenderIsClipEnabled(ren_);
            if (hadClip) SDL_RenderGetClipRect(ren_, &prevClip);
            int line = std::clamp(int(occY), 0, outH);
            SDL_Rect top{0, 0, outW, line};   // only pixels above the wall top show
            SDL_RenderSetClipRect(ren_, &top);
        }
        // Ground shadow (FBI shadowart, from shadows.gaf): drawn under the model
        // at the unit's ground point, nudged for the sun; a flyer's shadow sits
        // further out and stays on the ground while the model rides its altitude.
        if (u.type && !u.underConstruction) {
            if (const ShadowTex* sh = shadowFor(u.type->shadowArt)) {
                float alt = anim ? anim->altitude : 0.0f;
                float sox = (6.0f + alt * 0.5f) * zm, soy = (3.0f + alt * 0.25f) * zm;
                SDL_FRect dst{ax - sh->xoff * zm + sox, ay - sh->yoff * zm + soy,
                              sh->w * zm, sh->h * zm};
                SDL_RenderCopyF(ren_, sh->tex, nullptr, &dst);
            }
        }
        // Disco dance floor: a pulsing, hue-cycling glow disc under a dancing monarch.
        if (dancing(u)) {
            float t = animClock_;
            SDL_Color dc = discoHue(t * 0.8f + 0.5f);   // offset from the body tint
            float rad = (float(std::max(u.type->footX, u.type->footZ)) * 12.0f + 22.0f)
                        * (0.85f + 0.15f * std::sin(t * 8.0f)) * zm;   // pulse with the bob
            const int N = 24;
            std::vector<SDL_Vertex> fan;
            fan.reserve(N * 3);
            SDL_Vertex ctr{{ax, ay}, {dc.r, dc.g, dc.b, 150}, {0, 0}};
            auto rim = [&](float a) {
                return SDL_Vertex{{ax + std::cos(a) * rad, ay + std::sin(a) * rad * 0.5f},
                                  {dc.r, dc.g, dc.b, 0}, {0, 0}};
            };
            for (int i = 0; i < N; ++i) {
                fan.push_back(ctr);
                fan.push_back(rim(float(i) / N * 6.2831853f));
                fan.push_back(rim(float(i + 1) / N * 6.2831853f));
            }
            SDL_BlendMode pbm;
            SDL_GetRenderDrawBlendMode(ren_, &pbm);
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_ADD);
            SDL_RenderGeometry(ren_, nullptr, fan.data(), int(fan.size()), nullptr, 0);
            SDL_SetRenderDrawBlendMode(ren_, pbm);
        }
        // Headbang: a red mosh-pit glow that flares on each downbeat.
        if (headbanging(u)) {
            float ph = animClock_ * 2.533f * 6.2831853f;
            float bang = std::pow(std::max(0.0f, std::sin(ph)), 2.0f);
            SDL_Color dc{Uint8(150 + 90 * bang), Uint8(20 + 20 * bang), 20, 255};
            float rad = (float(std::max(u.type->footX, u.type->footZ)) * 12.0f + 22.0f)
                        * (0.75f + 0.45f * bang) * zm;
            const int N = 24;
            std::vector<SDL_Vertex> fan;
            fan.reserve(N * 3);
            SDL_Vertex ctr{{ax, ay}, {dc.r, dc.g, dc.b, Uint8(120 + 100 * bang)}, {0, 0}};
            auto rim = [&](float a) {
                return SDL_Vertex{{ax + std::cos(a) * rad, ay + std::sin(a) * rad * 0.5f},
                                  {dc.r, dc.g, dc.b, 0}, {0, 0}};
            };
            for (int i = 0; i < N; ++i) {
                fan.push_back(ctr);
                fan.push_back(rim(float(i) / N * 6.2831853f));
                fan.push_back(rim(float(i + 1) / N * 6.2831853f));
            }
            SDL_BlendMode pbm;
            SDL_GetRenderDrawBlendMode(ren_, &pbm);
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_ADD);
            SDL_RenderGeometry(ren_, nullptr, fan.data(), int(fan.size()), nullptr, 0);
            SDL_SetRenderDrawBlendMode(ren_, pbm);
        }
        // Submit the pre-built, depth-sorted vertex runs -- one SDL_RenderGeometry
        // per texture (usually 1 per unit). The veterancy/conjure colour tint was
        // already baked into the vertices on the worker pool.
        bool conjuring = u.type && (u.underConstruction || birthProgress(u.id) < 1.0f);
        int off = 0;
        for (const auto& r : g.runs) {
            SDL_RenderGeometry(ren_, r.first, g.verts.data() + off, r.second, nullptr, 0);
            off += r.second;
        }

        // Build/reclaim nano-sparkle. Retail sparkles BOTH ends -- the worker unit AND
        // its target -- but only once the job has really STARTED: a placed site shows as
        // a ghost until buildBegun (above), and a reclaim sparkles only once the builder
        // is in range (not while it walks/flies over).
        auto sideLower = [&] {
            std::string s = u.type ? u.type->side : std::string();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        auto uFootW = [&] { return std::max(u.type->footX, 1) * 16.0f * zm; };
        auto uFootH = [&] { return std::max(u.type->footZ, 1) * 16.0f * zm; };

        // A conjuring/summoning SITE (this unit) sparkles over itself.
        if (conjuring)
            sprinkleBuildFx(sideLower(), ax, ay, uFootW(), uFootH());

        // A builder actively conjuring a site sparkles over ITSELF too (the worker end).
        if (u.type && u.buildSiteId != 0) {
            const auto* site = frameUnitP(u.buildSiteId);
            if (site && site->buildBegun)
                sprinkleBuildFx(sideLower(), ax, ay, uFootW(), uFootH());
        }

        // A reclaimer IN RANGE (the reclaim has really started -- range test mirrors
        // World::tickReclaim): sparkle the reclaimer AND the feature it is chewing on.
        if (u.type && u.reclaimId != 0) {
            const auto* feat = world_.feature(u.reclaimId);
            if (feat && feat->alive) {
                float dxr = feat->x - u.x, dzr = feat->z - u.z;
                float reach = 24.0f + 8.0f * float(std::max(feat->fx, feat->fz)) +
                              (u.type->buildDist > 0 ? u.type->buildDist : 0.0f);
                if (dxr * dxr + dzr * dzr <= reach * reach) {
                    sprinkleBuildFx(sideLower(), ax, ay, uFootW(), uFootH());   // the reclaimer
                    float fsx = (feat->x - mapView_.offX()) * zm - terrainLiftX(feat->x, feat->z) * zm;
                    float fsy = (feat->z - mapView_.offY()) * zm - terrainLift(feat->x, feat->z) * zm;
                    sprinkleBuildFx(sideLower(), fsx, fsy,
                                    std::max(feat->fx, 1) * 16.0f * zm,
                                    std::max(feat->fz, 1) * 16.0f * zm);         // the feature
                }
            }
        }

        // Occluded: re-draw the hidden lower part as a faint, flat player-coloured
        // silhouette through the wall, so a unit behind cover is never fully lost.
        if (occluded) {
            int line = std::clamp(int(occY), 0, outH);
            SDL_Rect bot{0, line, outW, std::max(0, outH - line)};
            SDL_RenderSetClipRect(ren_, &bot);
            SDL_Color tc = playerColor(u.player);
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            // The silhouette is flat, untextured and single-colour, so the whole
            // model collapses to ONE draw call: re-tint every vertex and submit.
            triBatch_.clear();
            for (const SDL_Vertex& sv : g.verts) {
                SDL_Vertex v = sv;
                v.color = SDL_Color{tc.r, tc.g, tc.b, 70};
                triBatch_.push_back(v);
            }
            if (!triBatch_.empty())
                SDL_RenderGeometry(ren_, nullptr, triBatch_.data(),
                                   int(triBatch_.size()), nullptr, 0);
            if (hadClip) SDL_RenderSetClipRect(ren_, &prevClip);
            else SDL_RenderSetClipRect(ren_, nullptr);
        }
    }

    void GameView::drawRing(float wx, float wz, float r) {
        float zm = mapView_.zoom();
        float lift = terrainLift(wx, wz) * zm;
        float liftX = terrainLiftX(wx, wz) * zm;
        SDL_SetRenderDrawColor(ren_, 90, 255, 120, 255);
        SDL_FPoint pts[25];
        for (int i = 0; i <= 24; ++i) {
            float a = float(i) / 24 * 2 * 3.14159f;
            pts[i] = {(wx + std::cos(a) * r - mapView_.offX()) * zm - liftX,
                      (wz + std::sin(a) * r * 0.7f - mapView_.offY()) * zm - lift};
        }
        SDL_RenderDrawLinesF(ren_, pts, 25);
    }

    const GameView::ShadowTex* GameView::shadowFor(const std::string& art) {
        if (art.empty()) return nullptr;
        if (!shadowsLoaded_) {
            shadowsLoaded_ = true;
            const auto* pal = featurePalette("aramon");
            if (pal) try {
                for (auto& sq : tak::gaf::load(vread("anims/shadows.gaf"), *pal, -1,
                                               "anims/shadows.gaf")) {
                    if (sq.frames.empty() || sq.frames[0].width == 0) continue;
                    auto& fr = sq.frames[0];
                    std::vector<uint8_t> px = fr.rgba;   // silhouette -> translucent black
                    for (size_t i = 0; i + 3 < px.size(); i += 4) {
                        px[i] = px[i + 1] = px[i + 2] = 0;
                        px[i + 3] = px[i + 3] ? 90 : 0;
                    }
                    SDL_Texture* t = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32,
                                                       SDL_TEXTUREACCESS_STATIC,
                                                       fr.width, fr.height);
                    SDL_UpdateTexture(t, nullptr, px.data(), fr.width * 4);
                    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                    std::string name = sq.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    shadowTex_[name] = {t, fr.width, fr.height, fr.xoff, fr.yoff};
                }
            } catch (const std::exception&) {}
        }
        auto it = shadowTex_.find(art);
        return it != shadowTex_.end() ? &it->second : nullptr;
    }

    void GameView::loadFeatureDefs() {
        if (!featureDefs_.empty()) return;
        try {
            for (const std::string& path : vfs_.list("features")) {
                if (std::filesystem::path(path).extension() != ".tdf") continue;
                try {
                    auto root = vtdf(path);
                    for (const auto& n : root.childOrder) {
                        std::string k = n;
                        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
                        featureDefs_[k] = root.children.at(n);
                    }
                } catch (const std::exception&) {}
            }
        } catch (const std::exception&) {}
    }

    const tak::gaf::Palette* GameView::featurePalette(std::string world) {
        std::transform(world.begin(), world.end(), world.begin(), ::tolower);
        auto it = featurePals_.find(world);
        if (it != featurePals_.end()) return &it->second;
        const std::string cands[] = {world + "_features.pcx", world + ".pcx",
                                     std::string("aramon_features.pcx")};
        for (const std::string& cand : cands) {
            try {
                std::string pp = "palettes/" + cand;
                return &featurePals_
                            .emplace(world, tak::gaf::Palette::fromBytes(vread(pp), pp))
                            .first->second;
            } catch (const std::exception&) {}
        }
        return nullptr;
    }

    GameView::FeatArt* GameView::featureArtFor(const tak::tdf::Node& def) {
        std::string file = def.valueOr("filename", "");
        std::string seq = def.valueOr("seqname", "");
        std::string seqShad = def.valueOr("seqnameshad", "");
        std::string key = file + "|" + seq;
        auto it = featureArt_.find(key);
        if (it != featureArt_.end()) return it->second.tex ? &it->second : nullptr;
        FeatArt a{};
        const auto* pal = featurePalette(def.valueOr("world", "aramon"));
        if (pal) {
            try {
                std::string f = file;
                std::transform(f.begin(), f.end(), f.begin(), ::tolower);
                auto ieq = [](const std::string& x, const std::string& y) {
                    if (x.size() != y.size()) return false;
                    for (size_t i = 0; i < x.size(); ++i)
                        if (std::tolower(x[i]) != std::tolower(y[i])) return false;
                    return true;
                };
                for (auto& sq : tak::gaf::load(vread("anims/" + f + ".gaf"), *pal, -1,
                                               "anims/" + f + ".gaf")) {
                    if (sq.frames.empty() || sq.frames[0].width == 0) continue;
                    auto& fr = sq.frames[0];
                    if (ieq(sq.name, seq)) {
                        bool animate = def.numberOr("animating", 0) != 0 ||
                                       def.numberOr("animatable", 0) != 0;
                        size_t nf = animate ? sq.frames.size() : 1;
                        for (size_t fi = 0; fi < nf; ++fi) {
                            auto& ff = sq.frames[fi];
                            if (ff.width == 0 || ff.height != fr.height ||
                                ff.width != fr.width)
                                continue;   // keep uniform dimensions only
                            SDL_Texture* t = gpuvram::create(
                                ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                ff.width, ff.height);
                            SDL_UpdateTexture(t, nullptr, ff.rgba.data(), ff.width * 4);
                            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                            if (bilinear_) SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
                            a.frames.push_back(t);
                        }
                        if (!a.frames.empty()) {
                            a.tex = a.frames[0];
                            a.w = fr.width; a.h = fr.height;
                            a.xoff = fr.xoff; a.yoff = fr.yoff;
                        }
                    } else if (!seqShad.empty() && ieq(sq.name, seqShad)) {
                        // Shadow: silhouette drawn as translucent black.
                        std::vector<uint8_t> px = fr.rgba;
                        for (size_t i = 0; i + 3 < px.size(); i += 4) {
                            px[i] = px[i + 1] = px[i + 2] = 0;
                            px[i + 3] = px[i + 3] ? 90 : 0;
                        }
                        a.shadow = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32,
                                                     SDL_TEXTUREACCESS_STATIC, fr.width,
                                                     fr.height);
                        SDL_UpdateTexture(a.shadow, nullptr, px.data(), fr.width * 4);
                        SDL_SetTextureBlendMode(a.shadow, SDL_BLENDMODE_BLEND);
                        if (bilinear_) SDL_SetTextureScaleMode(a.shadow, SDL_ScaleModeLinear);
                        a.sw = fr.width; a.sh = fr.height;
                        a.sxoff = fr.xoff; a.syoff = fr.yoff;
                    }
                }
            } catch (const std::exception&) {}
        }
        featureArt_[key] = a;
        return featureArt_[key].tex ? &featureArt_[key] : nullptr;
    }

    bool GameView::addFeature(const std::string& rawName, float x, float z, bool blockNav) {
        loadFeatureDefs();
        std::string key = rawName;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        auto di = featureDefs_.find(key);
        if (di == featureDefs_.end()) return false;
        FeatArt* a = featureArtFor(di->second);
        if (!a) return false;
        FeatureInst inst;
        inst.tex = a->tex;
        inst.frames = &a->frames;
        inst.seed = int(features_.size() * 7);
        inst.shadow = a->shadow;
        inst.w = a->w; inst.h = a->h; inst.xoff = a->xoff; inst.yoff = a->yoff;
        inst.sw = a->sw; inst.sh = a->sh; inst.sxoff = a->sxoff; inst.syoff = a->syoff;
        inst.x = x;
        inst.z = z;
        // Mana deposits ("Sacred Stone", category=Mana) are the spots you build
        // lodestones ON, so they must stay buildable (walkable) — never block
        // the nav grid for them, or canPlace rejects the deposit itself.
        std::string cat = di->second.valueOr("category", "");
        std::transform(cat.begin(), cat.end(), cat.begin(), ::tolower);
        inst.mana = (cat == "mana");
        // The buildable spot is the animated Sacred Stone centre; the static
        // Standing Stones sharing the category are just ruins around it.
        inst.glowy = inst.mana && di->second.numberOr("animating", 0) != 0;
        features_.push_back(inst);
        // Retail nav-blocking: ordinary obstacle features AND the static Standing
        // Stones (blocking=1) block; only the glowy Sacred Stone centre stays
        // walkable, so a lodestone can build on it. (Standing Stones default to
        // blocking; the centre has no blocking field, so it never blocks.)
        bool blocks = !inst.glowy && (!inst.mana || di->second.numberOr("blocking", 0) != 0);
        if (blockNav && blocks) {
            int fx = int(di->second.numberOr("footprintx", 1));
            int fz = int(di->second.numberOr("footprintz", 1));
            world_.nav().block(int(x) / 16 - fx / 2, int(z) / 16 - fz / 2, fx, fz, true);
        }
        return true;
    }

    void GameView::loadFeatures() {
        features_.clear();   // full rebuild -- safe to call again on a map change
        const auto& names = mapView_.map().featureNames;
        if (names.empty()) return;
        loadFeatureDefs();
        const auto& map = mapView_.map();
        int placed = 0;
        for (int cz = 0; cz < map.height; ++cz)
            for (int cx = 0; cx < map.width; ++cx) {
                uint16_t v = map.features[size_t(cz) * map.width + cx];
                if (v >= names.size()) continue;
                if (addFeature(names[v], float(cx) * 16 + 8, float(cz) * 16 + 8, true))
                    ++placed;
            }
        std::printf("features: %d placed\n", placed);
        // Register the Sacred Stone deposits so lodestones can only build on
        // them (and the AI knows where to put them). The buildable spot is the
        // GLOWING centre (animated Sacred Stone) -- NOT the ring of static
        // Standing Stones around it, which share category=mana but are just
        // ruins; including them pulled the spot off-centre. One deposit can have
        // a couple of adjacent glowy stones, so still cluster (union-find, 60px
        // link) and keep ONE spot per cluster. Fallback: a deposit with no glowy
        // centre at all (odd data) uses its category=mana features instead.
        std::vector<std::pair<float, float>> raw;
        for (const auto& f : features_)
            if (f.glowy) raw.push_back({f.x, f.z});
        if (raw.empty())
            for (const auto& f : features_)
                if (f.mana) raw.push_back({f.x, f.z});
        std::vector<int> par(raw.size());
        for (size_t i = 0; i < par.size(); ++i) par[i] = int(i);
        std::function<int(int)> find = [&](int a) {
            while (par[size_t(a)] != a) { par[size_t(a)] = par[size_t(par[size_t(a)])]; a = par[size_t(a)]; }
            return a;
        };
        const float link2 = 60.0f * 60.0f;
        for (size_t i = 0; i < raw.size(); ++i)
            for (size_t j = i + 1; j < raw.size(); ++j) {
                float dx = raw[i].first - raw[j].first, dz = raw[i].second - raw[j].second;
                if (dx * dx + dz * dz < link2) par[size_t(find(int(i)))] = find(int(j));
            }
        std::map<int, std::pair<std::pair<double, double>, int>> acc;   // root -> (sum, count)
        for (size_t i = 0; i < raw.size(); ++i) {
            auto& a = acc[find(int(i))];
            a.first.first += raw[i].first; a.first.second += raw[i].second; ++a.second;
        }
        manaSpots_.clear();
        for (auto& [root, a] : acc)
            manaSpots_.push_back({float(a.first.first / a.second),
                                  float(a.first.second / a.second)});
        world_.setManaSpots(manaSpots_);
        // Standing Stones block nav, but a large one can reach the glowy centre;
        // keep every deposit buildable by carving the 2x2 lodestone footprint
        // clear at each spot (canPlace tests exactly these cells).
        for (const auto& [sx, sz] : manaSpots_)
            world_.nav().block(int(sx) / 16 - 1, int(sz) / 16 - 1, 2, 2, false);
        std::printf("mana deposits: %zu (from %zu features)\n",
                    manaSpots_.size(), raw.size());
        addShorelineWaves();
    }

    void GameView::addShorelineWaves() {
        // Retail shipped animated wave sprites (category=waves) that map authors dot
        // along coasts. We place them procedurally at water cells touching land, so
        // generated maps get surf too. Display only: addFeature(..., blockNav=false)
        // adds a render instance with no nav/mana/reclaim side effect, and they
        // animate through the same GAF pipeline as any feature.
        const auto& map = mapView_.map();
        const int W = map.width, H = map.height, sea = map.seaLevel;
        if (W <= 0 || H <= 0 || int(map.heights.size()) < size_t(W) * H) return;
        // World prefix (AraWave/TarWave/VerWave/ZonWave) from the map's feature names.
        std::string wp = "Ara";
        for (const auto& n : map.featureNames) {
            std::string p = n.substr(0, 3);
            if (p == "Tar" || p == "Ver" || p == "Zon" || p == "Ara") { wp = p; break; }
        }
        auto land = [&](int x, int z) {
            return x >= 0 && z >= 0 && x < W && z < H && map.heights[size_t(z) * W + x] >= sea;
        };
        // Variant by which way the land lies -- the sprite draws (via its anchor)
        // toward the land, so foam sits at the shoreline. Calibrated from the GAF
        // anchors: cardinals land-N->13 S->01 E->07 W->03; inside corners (two
        // adjacent cardinals) and convex corners (a diagonal only) use the diagonal
        // variants NW->04 NE->06 SE->16 SW->10.
        const int S = 4;                         // ~one wave per 4x4 stretch of coast
        std::vector<uint8_t> used(size_t(W / S + 1) * (H / S + 1), 0);
        int placed = 0;
        for (int z = 0; z < H; ++z)
            for (int x = 0; x < W; ++x) {
                if (land(x, z)) continue;        // waves sit on the water side
                bool n = land(x, z - 1), s = land(x, z + 1), e = land(x + 1, z), w = land(x - 1, z);
                bool nw = land(x - 1, z - 1), ne = land(x + 1, z - 1),
                     sw = land(x - 1, z + 1), se = land(x + 1, z + 1);
                if (!(n || s || e || w || nw || ne || sw || se)) continue;
                size_t uc = size_t(z / S) * (W / S + 1) + (x / S);
                if (used[uc]) continue;          // spacing
                used[uc] = 1;
                int v;
                if (n && w) v = 4; else if (n && e) v = 6;          // inside corners
                else if (s && e) v = 16; else if (s && w) v = 10;
                else if (n) v = 13; else if (s) v = 1;             // straight edges
                else if (e) v = 7; else if (w) v = 3;
                else if (nw) v = 4; else if (ne) v = 6;            // convex (diagonal-only)
                else if (se) v = 16; else v = 10;                 // sw
                char nm[24];
                std::snprintf(nm, sizeof nm, "%sWave%02d", wp.c_str(), v);
                if (addFeature(nm, float(x) * 16 + 8, float(z) * 16 + 8, false)) ++placed;
            }
        std::printf("shoreline waves: %d placed (%sWave)\n", placed, wp.c_str());
    }

    bool GameView::buildIconClick(float mx, float my, bool lmb, bool rmb) {
        if (!lmb && !rmb) return false;
        for (const auto& [r, bt] : iconRects_) {
            if (mx < r.x || mx > r.x + r.w || my < r.y || my > r.y + r.h) continue;
            const auto* b = selectedBuilder();
            if (!b || !bt) return true;   // consume the click even if it can't act
            uint16_t mod = SDL_GetModState();
            bool ctrl = (mod & KMOD_CTRL) != 0, shift = (mod & KMOD_SHIFT) != 0;
            if (isStructure(bt) || !isStructure(b->type)) {
                if (lmb) placing_ = bt;   // manual placement (buildings / mobile conjurers)
                return true;
            }
            tak::net::Command c;
            c.unitId = b->id;
            std::snprintf(c.type, sizeof c.type, "%s", bt->id.c_str());
            if (lmb && ctrl && !shift) {
                c.kind = tak::net::Cmd::RepeatTrain;
            } else {
                c.kind = lmb ? tak::net::Cmd::Train : tak::net::Cmd::Unqueue;
                c.targetId = (ctrl && shift) ? 10 : shift ? 5 : 1;
            }
            issue(c);
            return true;
        }
        return false;
    }

    SDL_Texture* GameView::iconFor(const std::string& typeId) {
        auto it = icons_.find(typeId);
        if (it != icons_.end()) return it->second;
        SDL_Texture* tex = nullptr;
        try {
            auto img = tak::jpeg::load(vread("anims/buildpic/" + typeId + ".jpg"));
            tex = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STATIC, img.width, img.height);
            SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.width * 4);
        } catch (const std::exception&) {}
        icons_[typeId] = tex;
        return tex;
    }

    SDL_Texture* GameView::modelIconTex(const std::string& id, int slot, bool canMove) {
        AaScaleReset _sr(ren_);
        auto keyp = std::make_pair(id, slot);
        if (auto it = modelIcons_.find(keyp); it != modelIcons_.end()) return it->second;
        if (gpuAllocBlocked()) return nullptr;   // retry after the alloc backoff lapses
        modelIcons_[keyp] = nullptr;   // cache the attempt (success or failure) up front
        if (!ghostModel(id)) return nullptr;
        auto vt = visuals_.find(id);
        if (vt == visuals_.end()) return nullptr;
        SDL_Texture* atlas = atlasFor(slot);
        std::vector<Tri> scratch;
        float facing = canMove ? -0.6f : 0.0f;   // slight 3/4 turn reads as a portrait
        collect(scratch, atlas, vt->second.model.root, Xform{}, nullptr, facing, 0, false);
        if (scratch.empty()) return nullptr;
        std::stable_sort(scratch.begin(), scratch.end(),
                         [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (auto& t : scratch)
            for (int i = 0; i < 3; ++i) {
                minX = std::min(minX, t.v[i].position.x);
                minY = std::min(minY, t.v[i].position.y);
                maxX = std::max(maxX, t.v[i].position.x);
                maxY = std::max(maxY, t.v[i].position.y);
            }
        const int ICON = 64;
        SDL_Texture* tgt = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_TARGET, ICON, ICON);
        // Transient VRAM failure: un-cache so the icon renders after the backoff
        // (the up-front nullptr stays only for permanent no-model failures).
        if (!tgt) { noteGpuAllocFail(); modelIcons_.erase(keyp); return nullptr; }
        SDL_SetTextureBlendMode(tgt, SDL_BLENDMODE_BLEND);
        SDL_Texture* prev = SDL_GetRenderTarget(ren_);
        SDL_SetRenderTarget(ren_, tgt);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
        SDL_RenderClear(ren_);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        float span = std::max({maxX - minX, maxY - minY, 1.0f});
        float s = 0.82f * float(ICON) / span;
        float ox = float(ICON) * 0.5f - (minX + maxX) * 0.5f * s;
        float oy = float(ICON) * 0.5f - (minY + maxY) * 0.5f * s;
        for (auto& t : scratch) {
            SDL_Vertex v[3];
            for (int i = 0; i < 3; ++i) {
                v[i] = t.v[i];
                v[i].position.x = t.v[i].position.x * s + ox;
                v[i].position.y = t.v[i].position.y * s + oy;
            }
            SDL_RenderGeometry(ren_, t.tex, v, 3, nullptr, 0);
        }
        SDL_SetRenderTarget(ren_, prev);
        modelIcons_[keyp] = tgt;
        return tgt;
    }

    void GameView::drawHDebug() {
        const auto& m = mapView_.map();
        if (m.heights.empty()) return;
        float zm = mapView_.zoom();
        // Tint cells whose height is well above ground (candidate "wall" cells).
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        int cx0 = std::max(0, int(mapView_.offX()) / 16 - 1);
        int cz0 = std::max(0, int(mapView_.offY()) / 16 - 1);
        int cx1 = std::min(m.width, cx0 + int(1000 / zm / 16) + 3);
        int cz1 = std::min(m.height, cz0 + int(1000 / zm / 16) + 3);
        for (int cz = cz0; cz < cz1; ++cz)
            for (int cx = cx0; cx < cx1; ++cx) {
                int h = m.heights[size_t(cz) * m.width + cx];
                int d = h - (heightRef_ < 0 ? 0 : heightRef_);
                if (d < 8) continue;
                Uint8 a = Uint8(std::min(150, 30 + d));
                SDL_SetRenderDrawColor(ren_, 220, 40, 40, a);
                SDL_FRect r{(cx * 16.0f - mapView_.offX()) * zm,
                            (cz * 16.0f - mapView_.offY()) * zm, 16 * zm, 16 * zm};
                SDL_RenderFillRectF(ren_, &r);
            }
        char buf[64];
        for (const UnitR* _up : front().live) { const UnitR& u = *_up;
            if (!u.alive() || !u.type) continue;
            if (!alliedToLocal(u.player) && !cellVisibleR(u.x, u.z) && !noFog_) continue;
            float sx = (u.x - mapView_.offX()) * zm;
            float rawY = (u.z - mapView_.offY()) * zm;
            float lift = terrainLift(u.x, u.z);
            float liftX = terrainLiftX(u.x, u.z) * zm;
            float liftY = rawY - lift * zm;
            // raw foot (magenta) and lifted foot (cyan, incl. sideways tilt)
            SDL_SetRenderDrawColor(ren_, 255, 0, 255, 255);
            SDL_FRect rr{sx - 2, rawY - 2, 4, 4};
            SDL_RenderFillRectF(ren_, &rr);
            SDL_SetRenderDrawColor(ren_, 0, 255, 255, 255);
            SDL_FRect lr{sx - liftX - 2, liftY - 2, 4, 4};
            SDL_RenderFillRectF(ren_, &lr);
            SDL_SetRenderDrawColor(ren_, 255, 0, 255, 200);
            SDL_RenderDrawLineF(ren_, sx, rawY, sx, liftY);
            int cx = std::clamp(int(u.x) / 16, 0, m.width - 1);
            int cz = std::clamp(int(u.z) / 16, 0, m.height - 1);
            int h = m.heights[size_t(cz) * m.width + cx];
            std::snprintf(buf, sizeof buf, "h%d L%d", h, int(lift + 0.5f));
            blockText(buf, sx + 5, liftY - 30, 1.4f, SDL_Color{255, 255, 120, 255});
            // Computed occlusion clip line (green): units are clipped above this.
            float occ = wallOcclusionY(u.x, u.z);
            if (occ < rawY) {
                SDL_SetRenderDrawColor(ren_, 40, 255, 40, 255);
                SDL_RenderDrawLineF(ren_, sx - 30, occ, sx + 30, occ);
            }
        }
    }

    void GameView::drawFog() {
        if (noFog_) return;
        const auto& vis = frameVisibility();
        if (vis.empty()) return;
        int w = frameVisW(), h = frameVisH();
        if (!fogTex_) {
            fogTex_ = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STREAMING, w, h);
            SDL_SetTextureBlendMode(fogTex_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(fogTex_, SDL_ScaleModeLinear);
        }
        // The fog CONTENT only changes when the sim recomputes visibility (4Hz);
        // frames render far more often (up to 240Hz), so rewrite + re-upload the
        // streaming texture only when the vis generation actually advanced.
        if (fogTexGen_ != frameVisGeneration()) {
            fogTexGen_ = frameVisGeneration();
            void* px = nullptr;
            int pitch = 0;
            if (SDL_LockTexture(fogTex_, nullptr, &px, &pitch) == 0) {
                for (int z = 0; z < h; ++z) {
                    uint32_t* row = reinterpret_cast<uint32_t*>(
                        static_cast<uint8_t*>(px) + size_t(z) * size_t(pitch));
                    for (int x = 0; x < w; ++x) {
                        uint8_t v = vis[size_t(z) * w + x];
                        uint8_t a = v == 2 ? 0 : (v == 1 ? 110 : 235);
                        row[x] = uint32_t(a) << 24;   // black with alpha (RGBA32 LE)
                    }
                }
                SDL_UnlockTexture(fogTex_);
            }
        }
        float zm = mapView_.zoom();
        // Lift the fog to sit on the terrain relief, exactly like units do, so the
        // cleared area follows a unit up a hill instead of staying at ground level.
        // Draw it as a grid of quads lifted per-cell (the terrain art itself is a
        // flat mosaic, but units are drawn lifted onto it -- the fog must match).
        heightAbove(0.0f, 0.0f);   // ensure heightRef_/scales are initialised
        float maxLy = float(255 - std::max(heightRef_, 0)) * kHeightScale_;
        float maxLx = float(255 - std::max(heightRef_, 0)) * kHeightScaleX_;
        float ox = mapView_.offX(), oy = mapView_.offY();
        int gx0 = std::clamp(int(ox / 16) - 1, 0, w);
        int gx1 = std::clamp(int((ox + winW_ / zm + maxLx) / 16) + 2, 0, w);
        int gz0 = std::clamp(int(oy / 16) - 1, 0, h);
        int gz1 = std::clamp(int((oy + winH_ / zm + maxLy) / 16) + 2, 0, h);
        // One height sample per grid CORNER, shared by all four adjacent quads.
        // vert() used to pay two independent bilinear samples per corner PER QUAD
        // (terrainLift + terrainLiftX each re-sampling), 8 samples per cell -- at a
        // 7680-wide window that alone measured ~2ms/frame; the shared corner grid
        // is ~8x fewer samples, each yielding both lifts.
        int cw = std::max(0, gx1 - gx0 + 1), ch = std::max(0, gz1 - gz0 + 1);
        fogLift_.assign(size_t(cw) * size_t(ch), 0.0f);
        for (int gz = gz0; gz <= gz1 && ch > 0; ++gz)
            for (int gx = gx0; gx <= gx1; ++gx)
                fogLift_[size_t(gz - gz0) * cw + size_t(gx - gx0)] =
                    heightAbove(float(gx) * 16.0f, float(gz) * 16.0f);
        auto vert = [&](int gx, int gz) {
            float wx = float(gx) * 16.0f, wz = float(gz) * 16.0f;
            float ha = fogLift_[size_t(gz - gz0) * cw + size_t(gx - gx0)];
            SDL_Vertex v;
            v.position = {(wx - ox) * zm - ha * kHeightScaleX_ * zm,
                          (wz - oy) * zm - ha * kHeightScale_ * zm};
            v.tex_coord = {float(gx) / float(w), float(gz) / float(h)};
            v.color = {255, 255, 255, 255};
            return v;
        };
        // Skip cells whose whole 3x3 neighbourhood is fully visible: their quad is
        // invisible (alpha 0) and, because the texture is linear-filtered, only a
        // fogged NEIGHBOUR can bleed alpha across the edge. Late-game this culls
        // most of the vertex stream.
        auto fogged = [&](int x, int z) {
            if (x < 0 || z < 0 || x >= w || z >= h) return true;   // map edge: keep
            return vis[size_t(z) * w + x] != 2;
        };
        fogVerts_.clear();
        for (int gz = gz0; gz < gz1; ++gz)
            for (int gx = gx0; gx < gx1; ++gx) {
                bool any = false;
                for (int dz = -1; dz <= 1 && !any; ++dz)
                    for (int dx = -1; dx <= 1 && !any; ++dx)
                        any = fogged(gx + dx, gz + dz);
                if (!any) continue;
                SDL_Vertex a = vert(gx, gz), b = vert(gx + 1, gz),
                           c = vert(gx + 1, gz + 1), d = vert(gx, gz + 1);
                fogVerts_.push_back(a); fogVerts_.push_back(b); fogVerts_.push_back(c);
                fogVerts_.push_back(a); fogVerts_.push_back(c); fogVerts_.push_back(d);
            }
        if (!fogVerts_.empty())
            SDL_RenderGeometry(ren_, fogTex_, fogVerts_.data(), int(fogVerts_.size()),
                               nullptr, 0);
    }

    void GameView::drawGhost() {
        float zm = mapView_.zoom();
        // Resolve the elevated cell drawn under the cursor. Draw a translucent ghost
        // of the thing being placed, lifted onto the relief exactly like the finished
        // unit/building -- a plain ghost where it can go, red-washed where it can't.
        float wx, wz;
        pickWorld(mouseX_, mouseY_, wx, wz);
        bool ok = canPlaceLocked(placing_, wx, wz);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        drawGhostAt(placing_, wx, wz, !ok);
        // Small name tag above the ghost so the player still sees what's queued.
        float cxp = (wx - mapView_.offX()) * zm - terrainLiftX(wx, wz) * zm;
        float czp = (wz - mapView_.offY()) * zm - terrainLift(wx, wz) * zm;
        float hh = float(placing_->footZ) * 8 * zm;
        float px = 1.8f;
        float tw = blockWidth(placing_->name, px);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 190);
        SDL_FRect tb{cxp - tw / 2 - 4, czp - hh - 24, tw + 8, 20};
        SDL_RenderFillRectF(ren_, &tb);
        blockText(placing_->name, cxp - tw / 2, czp - hh - 20, px, {230, 230, 200, 255});
    }

    const GameView::EffectAnim* GameView::effectFor(const std::string& animName) {
        auto it = effectAnims_.find(animName);
        if (it != effectAnims_.end())
            return it->second.frames.empty() ? nullptr : &it->second;
        EffectAnim ea;
        const auto* pal = featurePalette("aramon");   // ignored for truecolor TAF
        // "file:sequence" targets a specific GAF sequence (e.g. "flames:flame large");
        // a bare name uses the file of that name and its like-named (or first) sequence.
        std::string file = animName, seqWant = animName;
        if (auto c = animName.find(':'); c != std::string::npos) {
            file = animName.substr(0, c);
            seqWant = animName.substr(c + 1);
        }
        for (const std::string suf : {"_4444.taf", "_1555.taf", ".taf", ".gaf"}) {
            if (!ea.frames.empty()) break;
            try {
                std::string ap = "anims/" + file + suf;
                auto seqs = tak::gaf::load(vread(ap), pal ? *pal : tak::gaf::Palette{}, -1, ap);
                const tak::gaf::Sequence* seq = nullptr;
                for (auto& s : seqs) {
                    if (s.frames.empty()) continue;
                    if (!seq) seq = &s;
                    std::string sn = s.name;
                    std::transform(sn.begin(), sn.end(), sn.begin(), ::tolower);
                    if (sn == seqWant) { seq = &s; break; }
                }
                if (!seq) continue;
                for (auto& fr : seq->frames) {
                    if (fr.width == 0 || fr.height == 0) continue;
                    SDL_Texture* t = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32,
                                                       SDL_TEXTUREACCESS_STATIC,
                                                       fr.width, fr.height);
                    SDL_UpdateTexture(t, nullptr, fr.rgba.data(), fr.width * 4);
                    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);   // fiery glow
                    ea.frames.push_back({t, fr.width, fr.height, fr.xoff, fr.yoff});
                }
            } catch (const std::exception&) {}
        }
        auto& stored = effectAnims_[animName];
        stored = std::move(ea);
        return stored.frames.empty() ? nullptr : &stored;
    }

    void GameView::updateEffects(float dt) {
        for (auto& e : effects_) e.age += dt;
        std::erase_if(effects_, [](const EffectInst& e) {
            if (!e.anim || e.anim->frames.empty()) return true;
            float local = e.age - e.delay;
            return local >= effLoopLen(e) * float(std::max(e.loops, 1));
        });
    }

    void GameView::spawnRing(const std::string& anim, float x, float z, float delay,
                   float dur, int sprites, float maxR) {
        const EffectAnim* ea = effectFor(anim);
        if (ea) rings_.push_back({ea, x, z, 0.0f, delay, dur, maxR,
                                  std::clamp(sprites, 6, 48)});
    }

    void GameView::updateRings(float dt) {
        for (auto& r : rings_) r.age += dt;
        std::erase_if(rings_, [](const RingFx& r) { return r.age - r.delay >= r.dur; });
    }

    void GameView::drawRings() {
        float zm = mapView_.zoom();
        for (const auto& r : rings_) {
            float local = r.age - r.delay;
            if (local < 0 || !r.anim || r.anim->frames.empty()) continue;
            if (!cellVisibleR(r.x, r.z)) continue;
            float t = std::clamp(local / std::max(r.dur, 1e-3f), 0.0f, 1.0f);
            float radius = r.maxR * t;
            int nf = int(r.anim->frames.size());
            const EFrame& f = r.anim->frames[size_t(std::clamp(int(t * nf), 0, nf - 1))];
            for (int k = 0; k < r.sprites; ++k) {
                float a = 6.2831853f * float(k) / float(r.sprites);
                float wx = r.x + std::cos(a) * radius, wz = r.z + std::sin(a) * radius;
                float sx = (wx - mapView_.offX()) * zm - f.ax * zm - terrainLiftX(r.x, r.z) * zm;
                float sy = (wz - mapView_.offY()) * zm - f.ay * zm - terrainLift(r.x, r.z) * zm;
                SDL_FRect dst{sx, sy, f.w * zm, f.h * zm};
                SDL_RenderCopyF(ren_, f.tex, nullptr, &dst);
            }
        }
    }

    void GameView::drawEffects() {
        drawRings();
        float zm = mapView_.zoom();
        for (const auto& e : effects_) {
            if (!e.anim || e.anim->frames.empty()) continue;
            float local = e.age - e.delay;
            if (local < 0) continue;                // still waiting to start
            if (!cellVisibleR(e.x, e.z)) continue;
            float per = effLoopLen(e);
            float within = local - std::floor(local / per) * per;   // into this loop
            int nf = int(e.anim->frames.size());
            int fi = std::clamp(int(within / per * float(nf)), 0, nf - 1);
            const EFrame& f = e.anim->frames[size_t(fi)];
            float sx = (e.x - mapView_.offX()) * zm - f.ax * zm - terrainLiftX(e.x, e.z) * zm;
            float sy = (e.z - mapView_.offY()) * zm - f.ay * zm - terrainLift(e.x, e.z) * zm
                       - e.alt * zm;
            SDL_FRect dst{sx, sy, f.w * zm, f.h * zm};
            SDL_RenderCopyF(ren_, f.tex, nullptr, &dst);
        }
    }

    void GameView::drawUnitFx() {
        float zm = mapView_.zoom();
        const float kLinger = 0.8f;   // seconds after the last emit to keep drawing
        for (auto& [id, a] : anims_) {
            bool fire = a.fireFx && a.fireT < kLinger;
            bool smk = a.smokeFx && a.smokeT < kLinger;
            if (!fire && !smk) continue;
            const auto* u = frameUnitP(id);
            if (!u || !u->type) continue;
            if (!noFog_ && !cellVisibleR(u->x, u->z)) continue;
            auto draw = [&](const EffectAnim* ea, float lift, float fps) {
                int nf = int(ea->frames.size());
                int fi = int(animClock_ * fps + float(id) * 0.37f) % nf;
                if (fi < 0) fi += nf;
                const EFrame& f = ea->frames[size_t(fi)];
                float sx = (u->x - mapView_.offX()) * zm - f.ax * zm - terrainLiftX(u->x, u->z) * zm;
                float sy = (u->z - mapView_.offY()) * zm - f.ay * zm - terrainLift(u->x, u->z) * zm
                           - lift * zm;
                SDL_FRect dst{sx, sy, f.w * zm, f.h * zm};
                SDL_RenderCopyF(ren_, f.tex, nullptr, &dst);
            };
            if (smk) draw(a.smokeFx, a.smokeLift, 12.0f);
            if (fire) draw(a.fireFx, a.fireLift, 18.0f);   // flame over its smoke
        }
    }

    void GameView::updateParticles(float dt) {
        static const bool kLog = tak::devEnv("TAK_FXLOG") != nullptr;
        if (kLog && !particles_.empty()) {
            static size_t peak = 0;
            if (particles_.size() > peak) {
                peak = particles_.size();
                std::fprintf(stderr, "particles: %zu live (peak)\n", peak);
            }
        }
        for (auto& p : particles_) {
            p.life -= dt;
            p.x += p.vx * dt; p.z += p.vz * dt;
            p.alt += p.valt * dt;
            p.valt -= (p.kind == 1 ? 6.0f : 90.0f) * dt;   // smoke floats, sparks fall
            p.vx *= 0.92f; p.vz *= 0.92f;
        }
        std::erase_if(particles_, [](const Particle& p) { return p.life <= 0; });
    }

    void GameView::drawParticles() {
        // One batched geometry call instead of a state change + FillRect per
        // particle (battles keep hundreds live -- that pattern broke the render
        // batch hundreds of times a frame). Off-screen particles cost nothing.
        float zm = mapView_.zoom();
        partBatch_.clear();
        for (const auto& p : particles_) {
            float sx = (p.x - mapView_.offX()) * zm;
            float sy = (p.z - mapView_.offY()) * zm - p.alt * zm;
            if (sx < -40 || sx > float(winW_) + 40 || sy < -80 || sy > float(winH_) + 40)
                continue;   // cheap screen cull before the fog + lift lookups
            if (!cellVisibleR(p.x, p.z)) continue;
            float t = std::clamp(p.life / std::max(p.maxLife, 1e-3f), 0.0f, 1.0f);
            sx -= terrainLiftX(p.x, p.z) * zm;
            sy -= terrainLift(p.x, p.z) * zm;
            float r = p.size * zm * (p.kind == 1 ? (1.4f - t) : t);
            Uint8 a = Uint8(std::clamp(t * 255.0f, 0.0f, 255.0f));
            pushQuad(partBatch_, sx - r, sy - r, 2 * r, 2 * r, SDL_Color{p.r, p.g, p.b, a});
        }
        if (!partBatch_.empty()) {
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_RenderGeometry(ren_, nullptr, partBatch_.data(),
                               int(partBatch_.size()), nullptr, 0);
        }
    }

