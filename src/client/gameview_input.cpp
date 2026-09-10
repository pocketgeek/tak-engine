#include "client/gameview.h"

// Out-of-line GameView method definitions (input concern), split from the
// class body in gameview.h so editing a body recompiles only this translation
// unit. Trivial getters, ctors, static, template, constexpr and default-arg
// methods stay inline in the header. Grouping is by name heuristic.

    void GameView::openHotkeys() {
        if (!settings_) return;
        hotkeysScreen_ = std::make_unique<tak::HotkeysScreen>(ren_, *settings_,
            [this] { hotkeys_.load(settings_->hotkeys); },   // live-apply the rebind
            [this] { saveSettings(*settings_); });
    }

    void GameView::input(const SDL_Event& e, int winW, int winH) {
        winW_ = winW;
        winH_ = winH;
        // A benchmark RUN is hands-off: swallow ALL input (mouse + keys). Only Esc
        // does anything -- it ends the run early and brings up the stats screen.
        if (benchmarkMode_ && !benchStatsShown_) {
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) stopBenchmark();
            return;
        }
        if (inLobbyPhase()) { lobbyInput(e, winW, winH); return; }
        // Benchmark results overlay: topmost of all. DONE / Esc / any click returns to menu.
        if (benchStatsShown_) {
            if (e.type == SDL_MOUSEMOTION) { mouseX_ = float(e.motion.x); mouseY_ = float(e.motion.y); }
            else if ((e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) ||
                     (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT))
                menuRequested_ = true;   // main() tears down the game and returns to the front-end
            return;
        }
        // Hotkey-config overlay (opened FROM Options) sits on top of it, so it takes
        // input first while up. Cursor tracking as below.
        if (hotkeysScreen_) {
            if (e.type == SDL_MOUSEMOTION) { mouseX_ = float(e.motion.x); mouseY_ = float(e.motion.y); }
            if (hotkeysScreen_->input(e, winW, winH)) hotkeysScreen_.reset();   // BACK / Esc -> Options
            return;
        }
        // Options overlay (opened from the Esc menu) takes all input while up.
        if (options_) {
            // Keep the drawn cursor tracking the mouse (same as the exitMenu_ branch
            // below): the custom cursor renders from mouseX_/mouseY_, so swallowing
            // motion events here froze it for as long as Options was open.
            if (e.type == SDL_MOUSEMOTION) { mouseX_ = float(e.motion.x); mouseY_ = float(e.motion.y); }
            if (options_->input(e, winW, winH)) options_.reset();   // BACK / Esc (SAVE is explicit)
            return;
        }
        // In-game exit menu (opened with Esc). While it's up, all game input is
        // swallowed and only its own buttons / Esc respond. The sim keeps running
        // underneath -- a true pause isn't possible in the lockstep MP model (even
        // local single-player runs on a private server). Buttons fire on release;
        // hit-rects come from the last draw (see the overlay in draw()).
        if (exitMenu_) {
            if (e.type == SDL_MOUSEMOTION) { mouseX_ = float(e.motion.x); mouseY_ = float(e.motion.y); }
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) exitMenu_ = false;
            else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                float mx = float(e.button.x), my = float(e.button.y);
                for (auto& [r, action] : exitHots_)
                    if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) { action(); break; }
            }
            return;
        }
        // In-game chat capture. While composing, keyboard goes to the draft;
        // mouse events still fall through so the camera stays usable.
        if (chatTyping_) {
            if (e.type == SDL_TEXTINPUT) {
                if (chatDraft_.size() < 200) chatDraft_ += e.text.text;
                return;
            }
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                    if (mp_ && !chatDraft_.empty()) mp_->chat(chatDraft_);
                    chatDraft_.clear(); chatTyping_ = false; SDL_StopTextInput();
                } else if (k == SDLK_ESCAPE) {
                    chatDraft_.clear(); chatTyping_ = false; SDL_StopTextInput();
                } else if (k == SDLK_BACKSPACE && !chatDraft_.empty()) {
                    while (!chatDraft_.empty() && (chatDraft_.back() & 0xC0) == 0x80)
                        chatDraft_.pop_back();          // drop a UTF-8 continuation
                    if (!chatDraft_.empty()) chatDraft_.pop_back();
                }
                return;
            }
        }
        // Enter opens the chat composer (net games only -- there's no one to
        // talk to offline or in a recording).
        if (mp_ && e.type == SDL_KEYDOWN &&
            (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER)) {
            chatTyping_ = true; chatDraft_.clear(); SDL_StartTextInput();
            return;
        }
        float zm = mapView_.zoom();
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            // Ctrl+Esc removes the selection from its squad; plain Escape cancels a
            // pending placement/order first, then a stray selection, else the in-game
            // menu. Once the game is over it returns to the front-end menu directly.
            if (SDL_GetModState() & KMOD_CTRL) clearSquad();
            else if (outcome_ != 0) { menuRequested_ = true; }
            else if (placing_ || pendingCmd_) { placing_ = nullptr; pendingCmd_ = 0; }
            else if (!selection_.empty()) selection_.clear();
            else exitMenu_ = true;
        } else if (e.type == SDL_KEYDOWN && handleKey(e.key.keysym.sym,
                                                       SDL_GetModState())) {
            // handled by the hotkey dispatcher
        } else if (e.type == SDL_MOUSEWHEEL) {
            // Zoom toward the cursor: keep the world point under the mouse fixed.
            float wx = mapView_.offX() + mouseX_ / mapView_.zoom();
            float wz = mapView_.offY() + mouseY_ / mapView_.zoom();
            mapView_.input(e);                  // applies the zoom step
            mapView_.clampZoom(winW, winH);     // ...then the map-fills-window floor,
            float nz = mapView_.zoom();         // so the recentre below uses the real zoom
            mapView_.setOffset(wx - mouseX_ / nz, wz - mouseY_ / nz);
        } else if (e.type == SDL_KEYDOWN) {
            // Arrow-key panning takes the camera off the tracked selection.
            switch (e.key.keysym.sym) {
                case SDLK_LEFT: case SDLK_RIGHT: case SDLK_UP: case SDLK_DOWN:
                    trackSel_ = false; break;
                case SDLK_o:   // toggle the in-mission objectives panel
                    if (!missionObjectives_.empty()) showObjectives_ = !showObjectives_;
                    break;
                default: break;
            }
            mapView_.input(e);
        } else if (e.type == SDL_MOUSEMOTION) {
            mouseX_ = float(e.motion.x);
            mouseY_ = float(e.motion.y);
            if (draggingMinimap_) {
                minimapClick(mouseX_, mouseY_, winW, winH);
            } else if (e.motion.state & SDL_BUTTON_MMASK) {   // middle-drag scrolls
                trackSel_ = false;
                mapView_.setOffset(mapView_.offX() - e.motion.xrel / zm,
                                   mapView_.offY() - e.motion.yrel / zm);
            }
            if (dragging_) { dragX1_ = float(e.motion.x); dragY1_ = float(e.motion.y); }
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   buildIconClick(float(e.button.x), float(e.button.y),
                                  e.button.button == SDL_BUTTON_LEFT,
                                  e.button.button == SDL_BUTTON_RIGHT)) {
            // conjure/build icon (bottom-left, above the bar) handled
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   e.button.y > winH - barH()) {
            // bottom bar: build icons already handled above; swallow the rest so a stray
            // click on the bar chrome doesn't deselect / order into the world
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   (gui_.gadgets.empty()
                        ? orderColumnClick(float(e.button.x), float(e.button.y), winW)
                        : guiClick(float(e.button.x), float(e.button.y)))) {
            // command-panel button handled
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   minimapArmedOrder(float(e.button.x), float(e.button.y), winW, winH)) {
            // An armed order (F/M/A/P/G) + minimap click issues that order at the
            // clicked location, instead of moving the camera there.
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   minimapClick(float(e.button.x), float(e.button.y), winW, winH)) {
            draggingMinimap_ = true;   // camera follows the drag until release
            trackSel_ = false;
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   e.button.x > mapViewW(winW) && e.button.y < winH - barH()) {
            // Right-hand panel press. A right-click on the minimap orders the
            // selection to that world point; every other panel press is swallowed
            // so it can't start a box-select or drop an order on the map. (Only the
            // PRESS -- releases still fall through to end a drag/box-select.)
            if (e.button.button == SDL_BUTTON_RIGHT &&
                minimapOrder(float(e.button.x), float(e.button.y), winW, winH,
                             (SDL_GetModState() & KMOD_SHIFT) != 0))
                trackSel_ = false;
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   pendingCmd_) {
            float wx, wz;
            pickWorld(float(e.button.x), float(e.button.y), wx, wz);
            issueArmedOrder(pendingCmd_, wx, wz,
                            (SDL_GetModState() & KMOD_SHIFT) != 0, /*precise=*/true);
            pendingCmd_ = 0;
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   placing_) {
            // Placement pick: a building takes the flat cell under the cursor (so the
            // green/red square sits under the mouse and the flat-rendered building
            // lands there); a conjured unit uses the height-aware pick so it drops on
            // the elevated cell drawn under the cursor, not the low cell behind.
            float wx, wz;
            pickWorld(float(e.button.x), float(e.button.y), wx, wz);
            if (SDL_GetModState() & KMOD_SHIFT) {
                // Shift: begin a drag — a whole line of these gets queued on
                // release (a single shift-click is just a zero-length line).
                buildDrag_ = true;
                bdX0_ = wx;
                bdZ0_ = wz;
            } else if (!selection_.empty() && canPlaceLocked(placing_, wx, wz)) {
                tak::net::Command c;
                c.kind = tak::net::Cmd::Build;
                c.unitId = selectedBuilder() ? selectedBuilder()->id : selection_.front();
                c.x = wx;
                c.z = wz;
                std::snprintf(c.type, sizeof c.type, "%s", placing_->id.c_str());
                issue(c);
                placing_ = nullptr;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT &&
                   buildDrag_) {
            buildDrag_ = false;
            float ewx, ewz;
            pickWorld(float(e.button.x), float(e.button.y), ewx, ewz);
            placeBuildLine(bdX0_, bdZ0_, ewx, ewz);
            if (!(SDL_GetModState() & KMOD_SHIFT)) placing_ = nullptr;
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT &&
                   reclaimDrag_) {
            reclaimDrag_ = false;
            bool queue = (SDL_GetModState() & KMOD_SHIFT) != 0;
            float ex = float(e.button.x), ey = float(e.button.y), ewx, ewz;
            pickWorld(ex, ey, ewx, ewz);
            // A tiny drag was really a click -> the ordinary contextual order.
            if (std::fabs(ex - rdSx0_) < 6.0f && std::fabs(ey - rdSy0_) < 6.0f)
                rightClickOrder(ewx, ewz, queue);
            else
                issueReclaimBox(rdX0_, rdZ0_, ewx, ewz, queue);
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT &&
                   placing_) {
            placing_ = nullptr;
            buildDrag_ = false;
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   !spectating_) {   // a spectator watches only -- no unit selection
            dragging_ = true;
            dragX0_ = dragX1_ = float(e.button.x);
            dragY0_ = dragY1_ = float(e.button.y);
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT &&
                   draggingMinimap_) {
            draggingMinimap_ = false;
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT &&
                   dragging_) {
            // Only complete a box/click-select if the press actually began on
            // the map. A release after an icon click or a build placement must
            // NOT clear the selection using stale drag coordinates.
            dragging_ = false;
            // Screen-space marquee: a unit is boxed by where it's DRAWN (terrain lift
            // + flyer altitude), not its flat ground cell -- else a lifted/airborne
            // unit (e.g. the flying Monarch) escapes a box drawn around its sprite.
            float sx0 = std::min(dragX0_, dragX1_), sx1 = std::max(dragX0_, dragX1_);
            float sy0 = std::min(dragY0_, dragY1_), sy1 = std::max(dragY0_, dragY1_);
            bool isClick = (sx1 - sx0) < 6 && (sy1 - sy0) < 6;
            selection_.clear();
            if (isClick) {
                // Pick the unit nearest the cursor in SCREEN space (matching the
                // render lift + flyer altitude), so a unit on a lifted wall top or a
                // Monarch cruising overhead is selected where it's drawn.
                float ccx = (dragX0_ + dragX1_) / 2, ccy = (dragY0_ + dragY1_) / 2;
                int hit = -1;
                float best = 1e30f;
                for (const UnitR* _up : front().live) {
                    const UnitR& u = *_up;
                    if (!u.alive() || u.underConstruction || !u.type) continue;  // not-yet-built: unselectable
                    // Hit region = the drawn sprite box (unitUnderCursor: projected
                    // model bounds + lift + flyer altitude -- shared with the hover
                    // cursor so pointer feedback and the click always agree).
                    // Overlaps resolve to the nearest sprite centre.
                    float d = 0;
                    if (unitUnderCursor(u, ccx, ccy, &d) && d < best) { best = d; hit = u.id; }
                }
                if (hit >= 0) {
                    selection_.push_back(hit);
                    voice(hit, "select");
                }
            } else {
                for (const UnitR* _up : front().live) {
                    const UnitR& u = *_up;
                    if (u.alive() && u.player == localPlayer_ && !u.underConstruction) {
                        SDL_FPoint p = unitScreen(frameUnit(u.id));
                        if (p.x >= sx0 && p.x <= sx1 && p.y >= sy0 && p.y <= sy1)
                            selection_.push_back(u.id);
                    }
                }
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   e.button.button == SDL_BUTTON_RIGHT && !selection_.empty()) {
            float wx, wz;
            pickWorld(float(e.button.x), float(e.button.y), wx, wz);
            // A mobile reclaimer selected: arm a right-drag "clear this area" box and
            // defer the normal order to button-up, so a plain click still works.
            if (haveReclaimer()) {
                reclaimDrag_ = true;
                rdX0_ = wx; rdZ0_ = wz;
                rdSx0_ = float(e.button.x); rdSy0_ = float(e.button.y);
                return;
            }
            rightClickOrder(wx, wz, (SDL_GetModState() & KMOD_SHIFT) != 0);
            return;
        }
    }

    void GameView::edgeScroll(float dt, float zm) {
        if (winW_ <= 0 || winH_ <= 0 || !edgeScrollOn_) return;
        if (mouseX_ < 0 || mouseX_ > winW_ || mouseY_ < 0 || mouseY_ > winH_) return;
        const float margin = 24.0f, panPx = 2000.0f * edgeScrollSpeed_;   // px/s at zoom 1
        // Trigger at the real window edges (incl. the far right, past the panel),
        // so the player pushes to the screen edge to scroll -- not to the map edge.
        float sx = 0, sz = 0;
        if (mouseX_ < margin) sx = -1;
        else if (mouseX_ > winW_ - margin) sx = 1;
        if (mouseY_ < margin) sz = -1;
        else if (mouseY_ > winH_ - margin) sz = 1;
        if (sx == 0 && sz == 0) return;
        follow_ = false;   // the player is driving the camera now
        trackSel_ = false;
        mapView_.setOffset(mapView_.offX() + sx * panPx * dt / zm,
                           mapView_.offY() + sz * panPx * dt / zm);
    }

    void GameView::cameraFrame(float dt) {
        sounds_.pollMusic();
        if (inLobbyPhase()) return;
        edgeScroll(dt, std::max(mapView_.zoom(), 1e-3f));   // cursor-at-edge pan (real time)
        if (trackSel_ && !centerOnSelection()) trackSel_ = false;   // follow selection
        if (shakeTime_ > 0) shakeTime_ = std::max(0.0f, shakeTime_ - dt);   // shake decay
    }

    bool GameView::canPlaceLocked(const tak::sim::UnitType* type, float x, float z) {
        std::lock_guard<std::mutex> lk(simMutex_);
        return world_.canPlace(type, x, z);
    }

    const UnitR* GameView::selectedBuilder() {
        for (int id : selection_) {
            const auto* u = frameUnitP(id);
            if (u && u->alive() && u->type && u->type->isBuilder) return u;
        }
        return nullptr;
    }

    void GameView::cycleNextUnit() {
        if (spectating_) return;   // watch-only
        std::vector<int> owned;
        for (const UnitR* _up : front().live) {
            const UnitR& u = *_up;
            if (u.alive() && u.player == localPlayer_ && u.type && u.type->canMove &&
                !u.underConstruction)
                owned.push_back(u.id);
        }
        if (owned.empty()) return;
        int cur = selection_.empty() ? -1 : selection_.front();
        auto it = std::find(owned.begin(), owned.end(), cur);
        int next = (it == owned.end() || it + 1 == owned.end())
                       ? owned.front()
                       : *(it + 1);
        selection_ = {next};
        centerOn(next);
        voice(next, "select");
    }

    void GameView::centerOn(int id) {
        const auto* u = frameUnitP(id);
        if (!u) return;
        mapView_.setOffset(u->x - (winW_ / 2.0f) / mapView_.zoom(),
                           u->z - (winH_ / 2.0f) / mapView_.zoom());
    }

    bool GameView::centerOnSelection() {
        float cx = 0, cz = 0;
        int n = 0;
        for (int id : selection_) {
            const auto* u = frameUnitP(id);
            if (u && u->alive()) { cx += u->x; cz += u->z; ++n; }
        }
        if (!n) return false;
        mapView_.setOffset(cx / n - (winW_ / 2.0f) / mapView_.zoom(),
                           cz / n - (winH_ / 2.0f) / mapView_.zoom());
        return true;
    }

    void GameView::setMapScrollFromThumb() {
        if (mapListRect_.h <= 0 || mapTotalRows_ <= mapVisRows_) return;
        float mx, my; lobbyMouse(mx, my);
        float frac = (my - mapListRect_.y) / mapListRect_.h;   // 0..1 down the box
        mapScroll_ = int(std::lround(frac * mapTotalRows_ - mapVisRows_ * 0.5f));
        clampMapScroll();
    }

    void GameView::placeBuildLine(float x0, float z0, float x1, float z1) {
        if (!placing_ || selection_.empty()) return;
        int builderId = selectedBuilder() ? selectedBuilder()->id : selection_.front();
        // Lock the sim ONCE for the whole line: a build-line drag is often dozens of sites,
        // and O(N) separate canPlaceLocked() calls would each risk waiting a full tick.
        std::lock_guard<std::mutex> lk(simMutex_);
        for (auto& [x, z] : buildLinePositions(x0, z0, x1, z1)) {
            if (!world_.canPlace(placing_, x, z)) continue;   // simMutex_ already held
            tak::net::Command c;
            c.kind = tak::net::Cmd::Build;
            c.unitId = builderId;
            c.x = x;
            c.z = z;
            c.queue = true;   // all queued; the first starts if the builder is free
            std::snprintf(c.type, sizeof c.type, "%s", placing_->id.c_str());
            issue(c);
        }
    }

