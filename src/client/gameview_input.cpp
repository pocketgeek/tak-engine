#include "client/gameview.h"
#include "client/buildline.h"

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
        ClickScope click(*this);   // one click: a Legion net game sends it as explicit orders
        winW_ = winW;
        winH_ = winH;
        // Placement mode and the selected builder are separate. Only disarm an
        // icon after actual queued placement, and wait for the final Shift key.
        // Handle this ahead of overlays so they cannot swallow the release.
        if (e.type == SDL_KEYUP &&
            (e.key.keysym.sym == SDLK_LSHIFT || e.key.keysym.sym == SDLK_RSHIFT)) {
            const auto released = e.key.keysym.sym == SDLK_LSHIFT ? KMOD_LSHIFT : KMOD_RSHIFT;
            if (!(e.key.keysym.mod & KMOD_SHIFT & ~released) && shiftBuildPlaced_ &&
                !buildDrag_ && !manaBuildDrag_) {
                placing_ = nullptr;
                shiftBuildPlaced_ = false;
            }
            if (!(e.key.keysym.mod & KMOD_SHIFT & ~released) && shiftOrderArmed_) {
                pendingCmd_ = 0;
                shiftOrderArmed_ = false;
            }
        }
        // A Shift release we never saw (focus moved away): the next mouse event
        // with Shift up disarms, as retail's mouse handler does (0x5280a0).
        if (shiftOrderArmed_ && !(SDL_GetModState() & KMOD_SHIFT) &&
            (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEMOTION)) {
            pendingCmd_ = 0;
            shiftOrderArmed_ = false;
        }
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL ||
            (e.type == SDL_KEYDOWN &&
             (e.key.keysym.sym < SDLK_0 || e.key.keysym.sym > SDLK_9 ||
              (e.key.keysym.mod & (KMOD_CTRL | KMOD_ALT)))))
            lastRecalledSquad_ = 0;
        // A benchmark RUN is hands-off: swallow ALL input (mouse + keys). Only Esc
        // does anything -- it ends the run early and brings up the stats screen.
        if (benchmarkMode_ && !benchStatsShown_) {
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) stopBenchmark();
            return;
        }
        // Push to talk, ahead of the lobby and every overlay but CONTROLS (which may be
        // capturing that very key): talking is allowed with a menu open, typing is not.
        if (voiceKey(e)) return;
        if (inLobbyPhase()) {
            if (!missionStem_.empty()) {
                // Hidden lobby controls must not react behind the loading plate.
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
                    menuRequested_ = true;
            } else lobbyInput(e, winW, winH);
            return;
        }
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
            if (options_->input(e, winW, winH)) {   // BACK / Esc (SAVE is explicit)
                if (options_->voiceRequested()) {   // Options > VOICE CHAT
                    const bool dirty = options_->dirty();
                    openVoiceOptions();
                    voiceFromOptions_ = true; optionsDirtyCarry_ = dirty;
                } else if (voiceFromOptions_ && options_->page() == tak::OptionsPage::Voice) {
                    const bool dirty = optionsDirtyCarry_ && !options_->saved();
                    openOptions();                  // BACK -> Options, where it came from
                    if (dirty) options_->markDirty();
                } else {
                    options_.reset();
                }
            }
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
        if (giveUnitsMenu_) {
            if (e.type == SDL_MOUSEMOTION) { mouseX_ = float(e.motion.x); mouseY_ = float(e.motion.y); }
            if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_ESCAPE ||
                hotkeys_.match(e.key.keysym.sym, e.key.keysym.mod) == tak::Act::GiveUnits))
                giveUnitsMenu_ = false;
            // VOLUME sliders: press on one to set it and drag along it (as the Options
            // sliders do). A slider only has a hot area while it is live.
            const auto volumeHot = [&](int p) -> const DiplomacyHot* {
                for (const auto& hot : giveUnitsHots_)
                    if (hot.action == DiplomacyAction::Volume && hot.player == p) return &hot;
                return nullptr;
            };
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                for (const auto& hot : giveUnitsHots_) {
                    const auto& r=hot.rect;
                    if (hot.action != DiplomacyAction::Volume || e.button.x < r.x || e.button.x >= r.x+r.w ||
                        e.button.y < r.y || e.button.y >= r.y+r.h) continue;
                    voiceVolumeDrag_ = hot.player;
                    setVoiceVolumeAt(r, hot.player, float(e.button.x));
                    break;
                }
            }
            if (e.type == SDL_MOUSEMOTION && voiceVolumeDrag_ >= 0) {
                const DiplomacyHot* hot = volumeHot(voiceVolumeDrag_);
                if (hot && (e.motion.state & SDL_BUTTON_LMASK)) setVoiceVolumeAt(hot->rect, voiceVolumeDrag_, float(e.motion.x));
                else voiceVolumeDrag_ = -1;   // released unseen, or the slider went grey
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && voiceVolumeDrag_ >= 0) {
                voiceVolumeDrag_ = -1;   // the end of a drag, not a click on whatever it ended over
                return;
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                for (const auto& hot : giveUnitsHots_) {
                    const auto& r=hot.rect;
                    if (e.button.x < r.x || e.button.x >= r.x+r.w ||
                        e.button.y < r.y || e.button.y >= r.y+r.h) continue;
                    switch (hot.action) {
                        case DiplomacyAction::Give: giveSelectedUnits(hot.player); break;
                        case DiplomacyAction::Chat: chatRecipients_ ^= uint8_t(1u<<hot.player); break;
                        case DiplomacyAction::Voice: voiceRecipients_ ^= uint8_t(1u<<hot.player); break;
                        case DiplomacyAction::Volume: break;   // set on the press (above)
                        case DiplomacyAction::Mana: {
                            uint8_t mask=requestedManaSharing_.value_or(framePlayer(localPlayer_).manaShareMask);
                            mask ^= uint8_t(1u<<hot.player);
                            requestedManaSharing_=mask;
                            tak::net::Command c;c.kind=tak::net::Cmd::ShareMana;
                            c.targetId=hot.player;c.queue=bool(mask & (1u<<hot.player));
                            std::lock_guard<std::mutex> lock(simMutex_);issue(c);
                            break;
                        }
                        case DiplomacyAction::Close: giveUnitsMenu_=false; break;
                    }
                    break;
                }
            }
            return;
        }
        if (inputUnitInfo(e, winW, winH)) return;
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
                    if (mp_ && !chatDraft_.empty()) submitChat(chatDraft_);
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
            else if (unitInfoType_) unitInfoType_ = nullptr;   // close Unit Info first
            else if (outcome_ != 0) { menuRequested_ = true; }
            else if (placing_ || pendingCmd_) {
                placing_ = nullptr; pendingCmd_ = 0;buildDrag_=manaBuildDrag_=false;
                shiftBuildPlaced_ = shiftOrderArmed_ = false;
            }
            else if (!selection_.empty()) selection_.clear();
            else exitMenu_ = true;
        } else if (e.type == SDL_KEYDOWN && e.key.repeat &&
                   e.key.keysym.sym >= SDLK_0 && e.key.keysym.sym <= SDLK_9) {
            // Holding a number is not a second press to follow the squad.
        } else if (e.type == SDL_KEYDOWN && handleKey(e.key.keysym.sym,
                                                       SDL_GetModState())) {
            // handled by the hotkey dispatcher
        } else if (e.type == SDL_MOUSEWHEEL && buildDrag_) {
            // Shift + left held on a build line: the wheel spaces the sites (up =
            // wider), one empty cell per notch, and leaves the camera alone.
#if SDL_VERSION_ATLEAST(2, 0, 18)
            float delta = e.wheel.preciseY;
#else
            float delta = float(e.wheel.y);
#endif
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) delta = -delta;
            buildLineGap_ = std::clamp(buildLineGap_ + tak::client::buildLineWheelSteps(delta, buildLineWheel_),
                                       0, tak::client::kBuildLineMaxGap);
            buildLineGapShown_ = 1.5f;
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
                   overBuildMenu(float(e.button.x), float(e.button.y))) {
            // Build-strip borders and gaps are UI too, not world order targets.
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   selPortraitClick(float(e.button.x), float(e.button.y),
                                    e.button.button == SDL_BUTTON_LEFT)) {
            // info-bar type portrait: select only that type (Shift: drop it)
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
            const bool queue = (SDL_GetModState() & KMOD_SHIFT) != 0;
            issueArmedOrder(pendingCmd_, wx, wz, queue, /*precise=*/true);
            armedOrderIssued(queue);
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   placing_) {
            // Placement pick: a building takes the flat cell under the cursor (so the
            // green/red square sits under the mouse and the flat-rendered building
            // lands there); a conjured unit uses the height-aware pick so it drops on
            // the elevated cell drawn under the cursor, not the low cell behind.
            float wx, wz;
            pickWorld(float(e.button.x), float(e.button.y), wx, wz);
            if (placing_->onMana) {
                manaBuildDrag_=true;buildDrag_=false;
                bdX0_=wx;bdZ0_=wz;bdAnchor_=dragAnchorAt(float(e.button.x),float(e.button.y));
                bdQueue_=(SDL_GetModState() & KMOD_SHIFT)!=0;
            } else if (SDL_GetModState() & KMOD_SHIFT) {
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
                shiftBuildPlaced_ = false;
            } else if (!selection_.empty()) {
                // Blocked only by clearable doodads: send the builder to reclaim them
                // and queue the build behind. Anything else still refuses, as retail
                // does. See issueClearThenBuild.
                std::vector<int> feats;
                if (clearableAt(placing_, wx, wz, feats) && !feats.empty()) {
                    int bid = selectedBuilder() ? selectedBuilder()->id : selection_.front();
                    issueClearThenBuild(bid, placing_, wx, wz, feats, false);
                    placing_ = nullptr;
                    shiftBuildPlaced_ = false;
                }
            }
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT &&
                   manaBuildDrag_) {
            manaBuildDrag_=false;
            float wx,wz;pickWorld(float(e.button.x),float(e.button.y),wx,wz);
            // Under the area threshold this is a single lodestone placement.
            const bool box=areaBoxAt(bdAnchor_,float(e.button.x),float(e.button.y));
            if (box) {
                if (placeManaBuildBox(bdX0_,bdZ0_,wx,wz,bdQueue_) && bdQueue_)
                    shiftBuildPlaced_=true;
                if (!bdQueue_) { placing_=nullptr;shiftBuildPlaced_=false; }
            } else if (placing_ && !selection_.empty()) {
                std::vector<int> clearing;
                if (canPlaceLocked(placing_,wx,wz) || clearableAt(placing_,wx,wz,clearing)) {
                    tak::net::Command c;c.kind=tak::net::Cmd::Build;
                    c.unitId=selectedBuilder() ? selectedBuilder()->id : selection_.front();
                    c.x=wx;c.z=wz;c.queue=bdQueue_;
                    std::snprintf(c.type,sizeof c.type,"%s",placing_->id.c_str());issue(c);
                    if (bdQueue_) shiftBuildPlaced_=true;
                    else { placing_=nullptr;shiftBuildPlaced_=false; }
                }
            }
            // Shift may have been released during this drag, before any work
            // was issued. Finish the drag normally, then disarm its build icon.
            if (shiftBuildPlaced_ && !(SDL_GetModState() & KMOD_SHIFT)) {
                placing_=nullptr;shiftBuildPlaced_=false;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT &&
                   buildDrag_) {
            buildDrag_ = false;
            float ewx, ewz;
            pickWorld(float(e.button.x), float(e.button.y), ewx, ewz);
            if (placeBuildLine(bdX0_, bdZ0_, ewx, ewz)) shiftBuildPlaced_=true;
            if (shiftBuildPlaced_ && !(SDL_GetModState() & KMOD_SHIFT)) {
                placing_ = nullptr;shiftBuildPlaced_=false;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT &&
                   reclaimDrag_) {
            reclaimDrag_ = false;
            bool queue = (SDL_GetModState() & KMOD_SHIFT) != 0;
            float ex = float(e.button.x), ey = float(e.button.y), ewx, ewz;
            pickWorld(ex, ey, ewx, ewz);
            // Only a deliberate box clears an area (both sides at least the area
            // threshold, measured from where the press's ground is drawn NOW, so a pan
            // mid-drag counts as the box the player sees); anything less was a click ->
            // the ordinary contextual order.
            // A click orders at the PRESS, as a plain right-click does on press: a
            // twitch (now up to the area threshold) must not move the target off what
            // was clicked. Units are picked where the pressed ground is drawn now.
            if (areaBoxAt(rdAnchor_, ex, ey)) {
                issueReclaimBox(rdX0_, rdZ0_, ewx, ewz, queue);
            } else {
                const auto a = dragStartOnScreen(rdAnchor_);
                const float mx = mouseX_, my = mouseY_;
                mouseX_ = a.x; mouseY_ = a.y;
                rightClickOrder(rdX0_, rdZ0_, queue);
                mouseX_ = mx; mouseY_ = my;
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT &&
                   placing_) {
            placing_ = nullptr;
            buildDrag_ = manaBuildDrag_ = false;
            shiftBuildPlaced_ = false;
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                   !spectating_) {   // a spectator watches only -- no unit selection
            dragging_ = true;
            dragAnchor_ = dragAnchorAt(float(e.button.x), float(e.button.y));
            dragX1_ = float(e.button.x);
            dragY1_ = float(e.button.y);
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
            uint16_t selMod = SDL_GetModState();
            bool addSel = (selMod & KMOD_SHIFT) != 0;   // Shift: add to the selection
            bool subSel = (selMod & KMOD_CTRL) != 0;    // Ctrl:  remove from it
            // The press corner is kept on the map: after a pan mid-drag the box
            // selects what was drawn, from the press's ground to the pointer.
            const auto a = dragStartOnScreen(dragAnchor_);
            float sx0 = std::min(a.x, dragX1_), sx1 = std::max(a.x, dragX1_);
            float sy0 = std::min(a.y, dragY1_), sy1 = std::max(a.y, dragY1_);
            bool isClick = !tak::client::areadrag::isSelectBox(dragAnchor_, a, dragX1_, dragY1_, uiScale_);
            std::vector<int> matched;
            if (isClick) {
                // Pick the unit nearest the cursor in SCREEN space (matching the
                // render lift + flyer altitude), so a unit on a lifted wall top or a
                // Monarch cruising overhead is selected where it's drawn. The click is
                // at the press (re-projected), so a unit pressed on near the screen edge
                // is still the one selected after edge scroll moved it under a held
                // pointer.
                float ccx = a.x, ccy = a.y;
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
                if (hit >= 0) matched.push_back(hit);
            } else {
                for (const UnitR* _up : front().live) {
                    const UnitR& u = *_up;
                    if (u.alive() && u.player == localPlayer_ && !u.underConstruction) {
                        // With Tactical Dots up, a unit is where its dot is drawn.
                        SDL_FPoint p = dotsFrame_ ? dotCentre(frameUnit(u.id))
                                                  : unitScreen(frameUnit(u.id));
                        if (p.x >= sx0 && p.x <= sx1 && p.y >= sy0 && p.y <= sy1)
                            matched.push_back(u.id);
                    }
                }
                // Army filter: if a box caught any mobile ATTACKING unit, drop
                // buildings and builders -- dragging over a base picks the army,
                // not the workers/structures. (Click-select is exempt.)
                bool hasCombat = false;
                for (int id : matched)
                    if (const auto* u = frameUnitP(id); u && u->type)
                        if (u->type->canMove && !u->type->isBuilder && u->type->weapon.damage > 0) {
                            hasCombat = true; break;
                        }
                if (hasCombat)
                    matched.erase(std::remove_if(matched.begin(), matched.end(), [&](int id) {
                        const auto* u = frameUnitP(id);
                        return u && u->type && (u->type->isStructure() || u->type->isBuilder);
                    }), matched.end());
            }
            // Combine with the existing selection per the modifier.
            if (subSel) {
                for (int id : matched)
                    selection_.erase(std::remove(selection_.begin(), selection_.end(), id),
                                     selection_.end());
            } else if (addSel) {
                for (int id : matched)
                    if (std::find(selection_.begin(), selection_.end(), id) == selection_.end())
                        selection_.push_back(id);
            } else {
                selection_ = std::move(matched);
            }
            if (!addSel && !subSel && isClick && selection_.size() == 1)
                voice(selection_.front(), "select");
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   e.button.button == SDL_BUTTON_RIGHT && !selection_.empty()) {
            float wx, wz;
            pickWorld(float(e.button.x), float(e.button.y), wx, wz);
            // A mobile reclaimer selected: arm a right-drag "clear this area" box and
            // defer the normal order to button-up, so a plain click still works.
            if (haveReclaimer()) {
                reclaimDrag_ = true;
                rdX0_ = wx; rdZ0_ = wz;
                rdAnchor_ = dragAnchorAt(float(e.button.x), float(e.button.y));
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
        return liveWorld()->canPlace(type, x, z, localPlayer_);
    }

    GameView::PlacePreview& GameView::placePreviewEntry(const tak::sim::UnitType* type, float x, float z) {
        for (auto& e : placePreview_)
            if (e.type == type && e.x == x && e.z == z) return e;
        if (placePreview_.size() >= 256) placePreview_.clear();   // a build line is a few dozen
        placePreview_.push_back({type, x, z, ~0u});
        return placePreview_.back();
    }

    bool GameView::canPlacePreview(const tak::sim::UnitType* type, float x, float z) {
        PlacePreview& e = placePreviewEntry(type, x, z);
        const uint32_t tick = front().gameTick;
        if (e.tick != tick && !placePreviewBusy_) {
            if (const auto live = tryLiveWorld()) {
                e.ok = (*live)->canPlace(type, x, z, localPlayer_);
                e.tick = tick; e.haveClear = false;
            } else placePreviewBusy_ = true;
        }
        if (e.tick == ~0u) return placePreviewLast_;   // never computed and the worker is busy
        return placePreviewLast_ = e.ok;
    }

    // Whether a blocked site is blocked only by clearable features (the ghost is not red-washed).
    bool GameView::placePreviewClearable(const tak::sim::UnitType* type, float x, float z) {
        PlacePreview& e = placePreviewEntry(type, x, z);
        const uint32_t tick = front().gameTick;
        if ((e.tick != tick || !e.haveClear) && !placePreviewBusy_) {
            if (const auto live = tryLiveWorld()) {
                const auto* builder = selectedBuilder();
                std::vector<int> feats;
                e.clearable = builder && builder->type->canReclaim &&
                    (*live)->clearableForPlacement(type, x, z, feats, localPlayer_) && !feats.empty();
                e.haveClear = true;
                if (e.tick != tick) {e.ok = (*live)->canPlace(type, x, z, localPlayer_); e.tick = tick;}
            } else placePreviewBusy_ = true;
        }
        return e.haveClear && e.clearable;
    }

#ifndef NDEBUG
    // TAK_PACELOG: how long the render thread waits for the sim worker on a per-frame check.
    void GameView::lockSimTimed(std::unique_lock<std::mutex>& lk) const {
        if (!paceOn()) { lk.lock(); return; }
        const auto t0 = std::chrono::steady_clock::now();
        lk.lock();
        paceLockWaitNs_ += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - t0).count());
    }
#endif

    bool GameView::clearableAt(const tak::sim::UnitType* type, float x, float z,
                               std::vector<int>& outFeatures) {
        const auto* builder=selectedBuilder();
        if (!builder || !builder->type->canReclaim) return false;
        return liveWorld()->clearableForPlacement(type, x, z, outFeatures, localPlayer_);
    }

    // The simulation queues footprint reclamation before admitting construction.
    void GameView::issueClearThenBuild(int builderId, const tak::sim::UnitType* type,
                                       float x, float z, const std::vector<int>& feats,
                                       bool queue) {
        const UnitR* b=frameUnitP(builderId);
        if (!type || feats.empty() || !b || !b->type || !b->type->canReclaim) return;
        tak::net::Command bc;
        bc.kind = tak::net::Cmd::Build;
        bc.unitId = builderId;
        bc.x = x;
        bc.z = z;
        bc.queue = uint8_t(queue);
        std::snprintf(bc.type, sizeof bc.type, "%s", type->id.c_str());
        issue(bc);
        notice_ = "CLEARING " + std::to_string(feats.size());
        noticeTimer_ = 2;
        voice(builderId, "move");
    }

    const UnitR* GameView::selectedBuilder() const {
        if (selection_.size() != 1) return nullptr;
        const auto* u = frameUnitP(selection_.front());
        return u && u->alive() && u->type && u->type->isBuilder ? u : nullptr;
    }

    bool GameView::overBuildMenu(float mx, float my) const {
        return selectedBuilder() && buildMenuRect_.w > 0 && buildMenuRect_.h > 0 &&
               mx >= buildMenuRect_.x && mx <= buildMenuRect_.x + buildMenuRect_.w &&
               my >= buildMenuRect_.y && my <= buildMenuRect_.y + buildMenuRect_.h;
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

    bool GameView::placeManaBuildBox(float x0,float z0,float x1,float z1,bool queue) {
        if (!placing_ || !placing_->onMana || selection_.empty()) return false;
        const auto* builder=selectedBuilder();
        if (!builder) return false;
        tak::net::Command c;c.kind=tak::net::Cmd::BuildManaArea;
        c.unitId=builder->id;c.x=x0;c.z=z0;c.x2=x1;c.z2=z1;c.queue=queue;
        std::snprintf(c.type,sizeof c.type,"%s",placing_->id.c_str());issue(c);
        return true;
    }

    bool GameView::placeBuildLine(float x0, float z0, float x1, float z1) {
        if (!placing_ || selection_.empty()) return false;
        bool placed=false;
        int builderId = selectedBuilder() ? selectedBuilder()->id : selection_.front();
        // Lock the sim ONCE for the whole line: a build-line drag is often dozens of sites,
        // and O(N) separate canPlaceLocked() calls would each risk waiting a full tick.
        const auto live = liveWorld();
        const auto sites = buildLinePositions(x0, z0, x1, z1);
        const int fx = placing_->footX, fz = placing_->footZ;
        // A walking builder heads for the cell of the site's build rectangle nearest
        // to where it stands (RetailRectGoal::navigationCell, retail 4e38e0). In a
        // tight line, after building one site it stands beside it, and for the next
        // site that nearest cell can lie inside the building it has just made -- when
        // it works along the line's axis or down a diagonal -- where it pushes for good
        // (the old one-cell gap trapped it the same way). So keep it on ONE side of the
        // line: where that nearest cell would overlap the line or sit on the far side,
        // a queued move first takes it to the nearest rectangle cell on its side that
        // is clear of the whole line, and it builds from there. Consecutive side cells
        // are a footprint apart along the line, so it never has to round a corner.
        const auto* b = live->unit(builderId);
        const auto* bt = b ? b->type : nullptr;
        const bool walks = bt && !bt->canFly && bt->footX > 0 && bt->footZ > 0;
        int px = 0, pz = 0;   // the builder's footprint origin when this line's work starts
        if (walks) {
            // Where its queue leaves it (a build order's point is its approach cell).
            const auto at = b->orders.empty() ? std::pair{b->x, b->z}
                                              : std::pair{b->orders.back().x, b->orders.back().z};
            px = tak::sim::footprintOrigin(at.first, bt->footX);
            pz = tak::sim::footprintOrigin(at.second, bt->footZ);
        }
        std::vector<std::pair<int, int>> line;   // site footprint origins
        for (const auto& [x, z] : sites)
            line.push_back({tak::sim::footprintOrigin(x, fx), tak::sim::footprintOrigin(z, fz)});
        const auto overlaps = [&](const std::vector<std::pair<int, int>>& set, int x, int z) {
            for (const auto& [ox, oz] : set)
                if (x < ox + fx && ox < x + bt->footX && z < oz + fz && oz < z + bt->footZ) return true;
            return false;
        };
        // Which side of the line the builder works from: the one it starts on.
        const float ldx = sites.back().first - sites.front().first, ldz = sites.back().second - sites.front().second;
        const auto sideOf = [&](int x, int z) {
            const float vx = float(x * 16 + bt->footX * 8) - sites.front().first;
            const float vz = float(z * 16 + bt->footZ * 8) - sites.front().second;
            const float cross = ldx * vz - ldz * vx;
            return cross > 0 ? 1 : cross < 0 ? -1 : 0;
        };
        int side = walks ? (sideOf(px, pz) ? sideOf(px, pz) : 1) : 0;
        const auto standable = [&](int x, int z) {
            const auto& nav = live->navFor(bt);
            for (int j = 0; j < bt->footZ; ++j)
                for (int i = 0; i < bt->footX; ++i)
                    if (!nav.walkable(x + i, z + j)) return false;
            return true;
        };
        // A straight walk from cell (ax,az) to (bx,bz) that never clips a site already
        // queued: the mover does not round a building's corner it cuts, it stalls on it.
        std::vector<std::pair<int, int>> queued;
        const auto clearWalk = [&](int ax, int az, int bx, int bz) {
            const int steps = 4 * std::max({std::abs(bx - ax), std::abs(bz - az), 1});
            for (int i = 0; i <= steps; ++i) {
                const float cx = float(ax) + float(bx - ax) * float(i) / float(steps);
                const float cz = float(az) + float(bz - az) * float(i) / float(steps);
                const int x0 = int(std::floor(cx)), z0 = int(std::floor(cz));
                const int x1 = int(std::ceil(cx)), z1 = int(std::ceil(cz));
                if (overlaps(queued, x0, z0) || overlaps(queued, x1, z0) ||
                    overlaps(queued, x0, z1) || overlaps(queued, x1, z1)) return false;
            }
            return true;
        };
        const auto move = [&](int x, int z) {
            tak::net::Command m;
            m.kind = tak::net::Cmd::Move;
            m.unitId = builderId;
            m.x = float(x * 16 + bt->footX * 8);
            m.z = float(z * 16 + bt->footZ * 8);
            m.queue = true;
            issue(m);
        };
        for (size_t k = 0; k < sites.size(); ++k) {
            const auto [x, z] = sites[k];
            std::vector<int> clearing;
            if (!live->canPlace(placing_,x,z,localPlayer_) &&
                !live->clearableForPlacement(placing_,x,z,clearing,localPlayer_)) continue;
            if (walks && sites.size() > 1) {
                const auto [sx, sz] = line[k];
                const tak::sim::RetailRectGoal rect{sx - bt->footX, sx + fx, sz - bt->footZ, sz + fz};
                auto [tx, tz] = rect.navigationCell(px, pz);
                if (overlaps(line, tx, tz) || sideOf(tx, tz) != side || !clearWalk(px, pz, tx, tz)) {
                    // The nearest clear cell on our side, walked to directly or by one
                    // square corner (along one axis, then the other).
                    int best = -1, bx = 0, bz = 0, wx = 0, wz = 0;
                    bool via = false;
                    // Our side first; past a long building that side may only be reached
                    // round its far end, so then the other side, and stay there.
                    for (int pass = 0; pass < 2 && best < 0; ++pass, side = -side)
                    rect.enumerate([&](int x2, int z2) {
                        if (overlaps(line, x2, z2) || sideOf(x2, z2) != side || !standable(x2, z2)) return;
                        const int dx = std::abs(x2 - px), dz = std::abs(z2 - pz);
                        int d = 16 * std::max(dx, dz) + 6 * std::min(dx, dz);
                        bool corner = false;
                        int cx = 0, cz = 0;
                        if (!clearWalk(px, pz, x2, z2)) {
                            d = 16 * (dx + dz);
                            if (standable(x2, pz) && clearWalk(px, pz, x2, pz) && clearWalk(x2, pz, x2, z2)) { cx = x2; cz = pz; }
                            else if (standable(px, z2) && clearWalk(px, pz, px, z2) && clearWalk(px, z2, x2, z2)) { cx = px; cz = z2; }
                            else return;
                            corner = true;
                        }
                        if (best < 0 || d < best) { best = d; bx = x2; bz = z2; via = corner; wx = cx; wz = cz; }
                    });
                    if (best >= 0) {
                        side = -side;   // undo the loop's last flip: the side we found it on
                        if (via) move(wx, wz);
                        move(bx, bz);
                        tx = bx; tz = bz;
                    }
                }
                px = tx; pz = tz;
            }
            tak::net::Command c;
            c.kind = tak::net::Cmd::Build;
            c.unitId = builderId;
            c.x = x;
            c.z = z;
            c.queue = true;   // all queued; the first starts if the builder is free
            std::snprintf(c.type, sizeof c.type, "%s", placing_->id.c_str());
            issue(c);
            queued.push_back(line[k]);
            placed=true;
        }
        return placed;
    }

#ifndef NDEBUG
    int GameView::debugQueueDemo() {
        const UnitR* pick = nullptr;
        for (const UnitR* up : front().live)
            if (up && up->alive() && up->type && up->type->canMove &&
                up->player == localPlayer_ && !up->type->isStructure()) { pick = up; break; }
        if (!pick) return 0;
        selectOnly(pick->id);
        // An L-shaped queue: out, across, and back -- three legs, so the picture
        // shows the beads turning corners and not just one straight run.
        const float legs[3][2] = {{pick->x + 260, pick->z + 40},
                                  {pick->x + 260, pick->z + 300},
                                  {pick->x - 60,  pick->z + 300}};
        for (int i = 0; i < 3; ++i) {
            tak::net::Command c;
            c.kind = tak::net::Cmd::Move;
            c.unitId = pick->id;
            c.x = legs[i][0];
            c.z = legs[i][1];
            c.queue = uint8_t(i == 0 ? 0 : 1);
            issue(c);
        }
        return pick->id;
    }
#endif

std::array<bool,8> GameView::eligibleGiftRecipients() {
    // Drawn every frame the menu is open, so never wait on the worker's tick for it: keep
    // the last answer when it holds the world (giveSelectedUnits checks again, blocking).
    const auto live = tryLiveWorld();
    if (!live) return giftEligibleLast_;
    std::array<bool,8> eligible{};
    for (int p=0;p<(*live)->numPlayers();++p)
        for (int id:selection_)
            if ((*live)->canGiveUnit(id,localPlayer_,p)) {eligible[size_t(p)]=true;break;}
    return giftEligibleLast_ = eligible;
}

void GameView::giveSelectedUnits(int recipient) {
    // Check the same rules as the referee, not just ownership. The referee
    // checks again at execution, including caps changed by earlier gifts.
    const auto live = liveWorld();
    std::erase_if(selection_,[&](int id) {
        if (!live->canGiveUnit(id,localPlayer_,recipient)) return false;
        tak::net::Command c;
        c.kind = tak::net::Cmd::GiveUnit;
        c.unitId = id;
        c.targetId = recipient;
        issue(c);
        return true; // no duplicate gifts while waiting for the lockstep reply
    });
    trackSel_ = false;
    lastRecalledSquad_ = 0;
}
