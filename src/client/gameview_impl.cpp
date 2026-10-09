#include "util/virtualpath.h"
#include "client/retailaim.h"
#include "gaf/nimbus.h"
#include "client/gameview.h"
#include "client/shadowmask.h"
#include "client/retailfeatureclock.h"
#include "client/retailbuilderanimation.h"
#include "client/retailmovementcallbacks.h"
#include "client/retaildeathsfx.h"
#include "client/runtimesettings.h"
#include "sim/retailmotion.h"
#include <cmath>
#include <cstdlib>

// Out-of-line GameView method definitions (core concern), split from the
// class body in gameview.h so editing a body recompiles only this translation
// unit. Trivial getters, ctors, static, template, constexpr and default-arg
// methods stay inline in the header. Grouping is by name heuristic.

    std::vector<std::pair<float, float>> GameView::parseStartPositions() const {
        return tak::sim::parseStartPositions(vfs_, mapPath_);
    }

    void GameView::discoSound() {
        for (int p = 0; p < 8 && p < world_.numPlayers(); ++p) {
            bool on = frameDiscoActive(p);
            if (on) {
                // Centroid of this player's visible dancing units.
                float cx = 0, cz = 0; int n = 0;
                for (const UnitR* _up : front().live) {
                    const UnitR& u = *_up;
                    if (u.player != p || !u.alive() || !dancing(u)) continue;
                    if (!alliedToLocal(p) && !noFog_ && !cellVisibleR(u.x, u.z)) continue;
                    cx += u.x; cz += u.z; ++n;
                }
                if (n) {
                    cx /= n; cz /= n;
                    if (!discoWas_[p]) sounds_.discoAt(cx, cz);               // start the track
                    else sounds_.repositionWorld("disco", cx, cz);           // keep it panned
                }
            }
            discoWas_[p] = on;
        }
    }

    void GameView::headbangSound() {
        for (int p = 0; p < 8 && p < world_.numPlayers(); ++p) {
            bool on = frameHeadbangActive(p);
            if (on) {
                float cx = 0, cz = 0; int n = 0;
                for (const UnitR* _up : front().live) {
                    const UnitR& u = *_up;
                    if (u.player != p || !u.alive() || !headbanging(u)) continue;
                    if (!alliedToLocal(p) && !noFog_ && !cellVisibleR(u.x, u.z)) continue;
                    cx += u.x; cz += u.z; ++n;
                }
                if (n) {
                    cx /= n; cz /= n;
                    if (!headbangWas_[p]) sounds_.metalAt(cx, cz);
                    else sounds_.repositionWorld("metal", cx, cz);
                }
            }
            headbangWas_[p] = on;
        }
    }

    void GameView::manageMusic() {
        int want = inLobbyPhase() ? 1 : 2;
        if (want == musicMode_) return;
        musicMode_ = want;
        // The menu-launched front-end owns the lobby BGM (so it plays continuously
        // from the menu into the lobby); suppress ours there, but still switch to
        // faction music once the game proper begins.
        if (want == 1) { if (!externalLobbyMusic_) sounds_.startMusic(vfs_, {15}); }
        else if (mp_ && mp_->isSpectator()) sounds_.startMusic(vfs_, allFactionMusicTracks());
        else sounds_.startMusic(vfs_, factionMusicTracks(side_));
    }

    void GameView::applySettings(const tak::Settings& s) {
        // Push the display options that render code reads from globals (smooth art,
        // cursor factor, movie deblocking). This is GameView's one "settings changed"
        // hook, so wiring it here reaches the Esc-menu Options and its DEFAULTS button
        // without either of them having to know the globals exist.
        tak::applyRuntimeSettings(s);
        if (smoothArt_ != s.smoothArt) {
            smoothArt_ = s.smoothArt;
            reloadInterfaceArt(s);
        }
        sounds_.setMasterVolume(s.masterVol);
        sounds_.setMusicVolume(s.bgmVol);
        sounds_.setSfxVolume(s.sfxVol);
        for (int i = 0; i < sounds_.channelCount(); ++i) sounds_.setChannelGain(i, s.chanGain[i]);
        mapView_.setZoomSpeed(s.mouseZoomSpeed);
        edgeScrollSpeed_ = s.edgeScrollSpeed;
        edgeScrollOn_ = s.edgeScroll;
        uiScale_ = s.uiScale;
        buildBarAlign_ = std::clamp(s.buildBarAlign, 0, 2);   // Options: build-menu row
        buildBarScale_ = std::clamp(s.buildBarScale, 0.75f, 4.0f);
        setBilinear(s.bilinear);
        healthBars_ = std::clamp(s.healthBars, 0, 2);
        statsPanel_ = s.statsPanel;                       // Options: minimap-strip readout
        tacticalDotsOpt_ = s.tacticalDots;                // Options: zoomed-out unit dots
        tacticalDotsZoom_ = s.tacticalDotsZoom;
        hotkeys_.load(s.hotkeys);                         // Options: rebindable hotkeys
    }

    void GameView::openOptions() {
        if (!settings_) return;
        options_ = std::make_unique<tak::OptionsScreen>(ren_, *settings_,
            [this, fsWas = settings_->fullscreen, vsWas = settings_->vsync]() mutable {
                applySettings(*settings_);
                // Only touch the window/renderer when that display setting actually changed
                // since Options opened -- re-issuing it on every tweak re-commits the
                // (Wayland) surface and later clicks miss/die. See the menu Options callback.
                if (settings_->fullscreen != fsWas) {
                    if (SDL_Window* w = SDL_RenderGetWindow(ren_))
                        SDL_SetWindowFullscreen(w, settings_->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    fsWas = settings_->fullscreen;
                }
                if (settings_->vsync != vsWas) {
                    SDL_RenderSetVSync(ren_, settings_->vsync ? 1 : 0);
                    vsWas = settings_->vsync;
                }
            },
            [this] { saveSettings(*settings_); },
            sounds_.channelCount(),
            [this] { openHotkeys(); },
            [this] { sounds_.reopenDevice(); });
    }

    void GameView::writeResume(uint32_t gid, uint64_t tok) const {
        if (mpResumePath_.empty()) return;
        if (FILE* f = std::fopen(mpResumePath_.c_str(), "w")) {
            std::fprintf(f, "%u %llu\n", gid, (unsigned long long)tok);
            std::fclose(f);
        }
    }

    bool GameView::readResume(uint32_t& gid, uint64_t& tok) const {
        if (mpResumePath_.empty()) return false;
        FILE* f = std::fopen(mpResumePath_.c_str(), "r");
        if (!f) return false;
        unsigned long long t = 0;
        bool ok = std::fscanf(f, "%u %llu", &gid, &t) == 2;
        std::fclose(f);
        tok = t;
        return ok;
    }

    bool GameView::haveReclaimer() {
        for (int id : selection_)
            if (const auto* u = frameUnitP(id))
                if (u->alive() && u->type && u->type->isBuilder && u->type->canMove &&
                    u->type->canReclaim && u->player == localPlayer_)
                    return true;
        return false;
    }

    int GameView::firstReclaimer() {
        for (int id : selection_)
            if (const auto* u = frameUnitP(id))
                if (u->alive() && u->type && u->type->isBuilder && u->type->canMove &&
                    u->type->canReclaim && u->player == localPlayer_)
                    return id;
        return 0;
    }

    void GameView::issueReclaimBox(float x0, float z0, float x1, float z1, bool queue) {
        const int builderId = firstReclaimer();
        if (!frameUnitP(builderId)) return;
        tak::net::Command c;
        c.kind = tak::net::Cmd::ReclaimArea;
        c.unitId = builderId;
        c.x = x0; c.z = z0; c.x2 = x1; c.z2 = z1;
        c.queue = uint8_t(queue);
        issue(c);
        notice_ = "CLEAR AREA";
        noticeTimer_ = 2;
        voice(builderId, "move");
    }

    std::pair<int, size_t> GameView::keytestPickOwnUnit() const {
        int pick = -1, any = -1;
        for (const UnitR* _up : front().live) {
            const UnitR& u = *_up;
            if (!u.alive() || u.player != 0 || !u.type) continue;
            any = u.id;
            if (u.type->isBuilder) pick = u.id;
        }
        return { pick >= 0 ? pick : any, front().live.size() };
    }

    void GameView::issue(tak::net::Command c) {
        if (replayMode_ || spectating_) return;   // watch-only: can't order units
        c.player = uint8_t(localPlayer_);
        if (mp_) outbox_.push_back(c);
        else apply(c);
    }

    void GameView::startReplay(tak::sim::MatchConfig cfg,
                     std::vector<tak::net::Bundle> bundles,
                     const std::string& mission) {
        // Read through this view's own VFS (we own it), not a caller pointer.
        cfg.vfs = &vfs_;
        cfg.mapPath = mapPath_;
        std::vector<std::pair<float, float>> spots;
        if (!mission.empty()) {
            // A CAMPAIGN recording rebuilds the mission -- its placements and its
            // in-sim script -- exactly as the game did. Replaying it through
            // setupMatch produced a plain skirmish on the mission's map: none of the
            // placed units, none of the scripting, so the recorded commands landed in
            // a world that had nothing to do with the one they came from. The
            // recorded mission stem is new in format 6; before it, the loader could
            // not have known.
            missionStem_ = mission;
            int human = 0;
            tak::sim::MissionSetup ms;
            if (tak::sim::setupMission(world_, registry_, vfs_, mission, human, &ms))
                spots = ms.slotPos;
            else
                std::fprintf(stderr, "replay: mission '%s' not in this data\n",
                             mission.c_str());
        } else {
            spots = tak::sim::setupMatch(world_, registry_, cfg);
        }
        replayBundles_ = std::move(bundles);
        replayMode_ = true;
        noFog_ = true;             // a spectator sees the whole map
        localPlayer_ = 0;
        world_.setVisPlayer(-1);   // no fog needed (noFog_) -> skip the O(units) fog pass
        side_ = "ara";
        loadPanel(side_);
        loadGui(side_);
        if (!spots.empty())
            mapView_.setOffset(spots[0].first - 640 / 0.9f, spots[0].second - 400 / 0.9f);
    }

    void GameView::replayStep(float dt) {
        // The early return used to sit HERE, which quietly broke the last snapshot:
        // captureFrame only publishes the buffer, while the registration that makes a
        // unit drawable happens in cosmeticStep (it walks front().live and calls
        // registerUnit). Stop calling cosmeticStep and any unit that FIRST appears in
        // the final snapshot is never registered -- so a replay consumed in one frame
        // still renders an empty map, which is exactly the bug this was meant to fix.
        // Publishing and registering are two steps and both have to keep running.
        //
        // Pausing is the same story: a paused replay still has to draw what it froze on.
        if (!paused_ && replayTick_ < replayBundles_.size()) {
            replayAdvance(dt);
        }
        // ...but with the SIM'S time, not the wall clock. Two things follow from that:
        //
        //   paused  -> zero. The pass still has to RUN (registration lives in it), but
        //              real elapsed time would keep particles moving, expire effects and
        //              rings and drift flyer altitude in a scene that is supposed to be
        //              frozen. Zero registers without animating.
        //   playing -> dt * speedMult(), because replayAdvance advances the sim by
        //              exactly that. Unscaled dt made effects outlive combat by the
        //              speed factor -- ten times longer at 10x playback, and expiring
        //              too fast when slowed.
        cosmeticStep(paused_ ? 0.0f : dt * speedMult());
    }

    void GameView::replayAdvance(float dt) {
        replayAccum_ += dt * speedMult();
        int guard = 0;
        bool advanced = false;
        while (replayAccum_ >= 1.0f / 30.0f && replayTick_ < replayBundles_.size() && guard < 64) {
#ifndef NDEBUG
            if (situation_.armed()) situation_.observe(uint32_t(replayTick_), world_, replayBundles_);
#endif
            const auto& bd = replayBundles_[replayTick_];
            for (const auto& c : bd.cmds) apply(c);
            for (const auto& e : bd.events) applyEvent(e);
            world_.tick(1.0f / 30.0f);
            captureTransportEffects();
            ++replayTick_;
#ifndef NDEBUG
            if (postrail_.active()) postrail_.record(uint32_t(replayTick_ - 1), world_);
#endif
            // COMPARE against what the original game computed at this tick. Recording
            // the checkpoints was only half of it -- playback printed the count and
            // checked nothing, so a replay that diverged still ran happily to the end
            // and looked like a faithful reproduction. Report the FIRST divergence and
            // then stop reporting: after the first, every later tick differs too and
            // the rest is noise.
            if (replayCheckAt_ < replayChecks_.size()) {
                const auto& ck = replayChecks_[replayCheckAt_];
                if (ck.tick + 1 == uint32_t(replayTick_)) {
                    ++replayCheckAt_;
                    const uint64_t mine = world_.stateHash();
                    // Keeps checking the position digest after the first hash divergence:
                    // that is what separates a hash-layout change from changed play.
                    if (replayTracker_.observe(ck, mine, [&] { return world_.posDigest(); })) {
                        std::fprintf(stderr,
                            "replay: DIVERGED at tick %u -- recorded %016llx, replayed "
                            "%016llx\n", ck.tick, (unsigned long long)ck.hash,
                            (unsigned long long)mine);
                        postNotice("REPLAY DIVERGED FROM THE RECORDING", 8);
                    }
                } else if (ck.tick + 1 < uint32_t(replayTick_)) {
                    ++replayCheckAt_;   // checkpoint we stepped past; keep in step
                }
            }
            replayAccum_ -= 1.0f / 30.0f;
            ++guard;
            advanced = true;
        }
        // PUBLISH what we just simulated, exactly as update() does for a live game
        // (simStep ends with captureFrame, then cosmeticStep runs). replayStep did
        // neither, so front() was never filled: the render reads front().live, which
        // stayed empty, and playback drew the terrain with none of the recorded
        // armies on it. Effects follow the same snapshot, so cosmeticStep comes after.
        if (advanced) captureFrame();
    }

    size_t GameView::aliveUnits() const {
        size_t n = 0; for (auto& u : world_.units()) if (u.alive() && u.type) ++n; return n;
    }

    // Build the end-of-game statistics table from the last render frame. Rows follow
    // slot order so the table reads the same for everyone in a multiplayer game; a
    // player still standing when the game ended is timed at the full match length.
    tak::ResultStats GameView::resultStats() const {
        tak::ResultStats st;
        const Frame& f = front();
        st.matchSec = int(f.gameTick / 30);
        static const char* kSides[5] = {"ara", "tar", "ver", "zon", "cre"};
        for (int i = 0; i < 5; ++i) if (side_ == kSides[i]) st.faction = i;
        for (int p = 0; p < f.numPlayers && p < int(f.players.size()); ++p) {
            if (resultParticipants_ && !(*resultParticipants_ & (1u<<p))) continue;
            const PlayerR& pr = f.players[size_t(p)];
            tak::ResultRow row;
            row.name = !playerName_[p & 7].empty() ? playerName_[p & 7]
                                                   : "PLAYER " + std::to_string(p + 1);
            row.colorSlot = colorSlot_[p & 7];
            row.side = pr.side;
            row.built = pr.built;
            row.score = pr.score;
            row.kills = pr.kills;
            row.losses = pr.losses;
            row.defeated = pr.defeated;
            row.isLocal = (p == localPlayer_) && !spectating_;
            row.timeSec = pr.defeatedAt >= 0 ? int(pr.defeatedAt) : st.matchSec;
            st.rows.push_back(std::move(row));
        }
        return st;
    }

    void GameView::navyDemo() {
        struct S { const char* t; float x, z; int player; };
        const S fleet[] = {
            {"verflag", 1150, 1250, 0}, {"verman", 1080, 1150, 0},
            {"verman", 1220, 1130, 0},  {"verharp", 1020, 1260, 0},
            {"vertre", 1100, 1360, 0},
            {"npcbotl", 1750, 1500, 1}, {"npcbotl", 1830, 1600, 1},
            {"monpiran", 1700, 1400, 1}, {"monpiran", 1780, 1420, 1},
            {"monpiran", 1650, 1500, 1},
        };
        std::vector<int> a, b;
        for (const auto& sp : fleet) {
            int id = spawn(sp.t, sp.x, sp.z, sp.player == 0 ? 1.57f : -1.57f, sp.player);
            if (id >= 0) (sp.player == 0 ? a : b).push_back(id);
        }
        for (size_t i = 0; i < a.size(); ++i)
            world_.attack(a[i], b[i % b.size()], false);
        for (size_t i = 0; i < b.size(); ++i)
            world_.attack(b[i], a[i % a.size()], false);
        mapView_.setOffset(1500 - 640 / 0.9f, 1380 - 400 / 0.9f);
    }

    void GameView::creonDemo() {
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        const char* squad[] = {"cregod",  "creiron", "creauto", "creauto",
                               "crebeas", "cregatl", "creshoc", "credrag"};
        int i = 0;
        for (const char* t : squad) {
            spawn(t, cx - 100 + float(i % 4) * 60, cz - 40 + float(i / 4) * 70, 1.57f, 0);
            ++i;
        }
        mapView_.setOffset(cx - 640 / 0.9f, cz - 400 / 0.9f);
    }

    void GameView::missionTest() {
        // Plant 4 Watch Towers + escorts inside mission06's forest-edge
        // region (cells 147,119-269,210) to exercise the victory script.
        for (int i = 0; i < 4; ++i)
            spawn("verat", 3300 + float(i % 2) * 60, 2600 + float(i / 2) * 60, 0, 0);
        spawn("versword", 3260, 2700, 0, 0);
        mapView_.setOffset(3300 - 640 / 0.9f, 2620 - 400 / 0.9f);
    }

    void GameView::testBuild() {
#ifndef NDEBUG
        if (tak::devFlag("TAK_TACTICAL_DOTS_TEST")) {
            // Tactical Dots through the real draw(): dots replace models past the
            // threshold, the hysteresis band holds either state, fog-hidden enemies
            // get no dot, and click/box selection land on the dots.
            const auto check=[](bool ok,const std::string& what) {
                if (!ok) throw std::runtime_error("tactical dots: "+what);
                std::fprintf(stderr,"PASS: tactical dots: %s\n",what.c_str());
            };
            constexpr int W=1280,H=960;
            noFog_=false;edgeScrollOn_=false;
            world_.setPlayerCount(2);
            const float x0=float(mapView_.map().blocksX)*16.0f,z0=float(mapView_.map().blocksY)*16.0f;
            const tak::sim::UnitType* flyerType=nullptr;
            for (const auto& [name,type]:registry_.types())
                if (type.canFly && !type.isStructure() && !type.commander) {flyerType=&type;break;}
            const int infantry=spawn("arasword",x0,z0,0,0);
            const int keep=spawn("arakeep",x0+160,z0+40,0,0);
            const int flyer=flyerType ? spawn(flyerType->id,x0-120,z0,0,0) : -1;
            const int foeSeen=spawn("arasword",x0+60,z0-80,0,1);
            const int foeHidden=spawn("arasword",x0+900,z0+300,0,1);
            check(infantry>=0 && keep>=0 && flyer>=0 && foeSeen>=0 && foeHidden>=0,"fixture spawned");
            check(!world_.allied(0,1),"fixture enemy is not allied");
            selection_={keep};
            const auto frame=[&](float zoom,const char* capture=nullptr) {
                captureFrame();beginFrame();cosmeticStep(0);
                // Local fog for the fixture: everything explored, only a 400px disc
                // around the fixture centre currently in sight -- the foe 950px away
                // is in fog, exactly what cellVisibleR reports in a real game.
                Frame& f=frameBuf_[renderReadIdx_];
                f.visW=mapView_.map().blocksX*2;f.visH=mapView_.map().blocksY*2;
                f.vis.assign(size_t(f.visW)*size_t(f.visH),1);
                for (int cz=0;cz<f.visH;++cz) for (int cx=0;cx<f.visW;++cx) {
                    const float dx=float(cx)*16+8-x0,dz=float(cz)*16+8-z0;
                    if (dx*dx+dz*dz<400.0f*400.0f) f.vis[size_t(cz)*size_t(f.visW)+size_t(cx)]=2;
                }
                mapView_.setZoom(zoom);
                // Keep the hidden foe on screen too, so "not drawn" means the fog
                // rule and not the viewport cull.
                mapView_.setOffset(x0+300-float(W-300)*0.5f/zoom,z0+100-float(H)*0.5f/zoom);
                prepare(W,H);draw(W,H);
                if (capture) {   // TAK_TACTICAL_DOTS_CAPTURE: the frame as drawn
                    int ow=0,oh=0;SDL_GetRendererOutputSize(ren_,&ow,&oh);
                    std::vector<uint8_t> px(size_t(ow)*size_t(oh)*4);
                    if (SDL_RenderReadPixels(ren_,nullptr,SDL_PIXELFORMAT_RGBA32,px.data(),ow*4)==0)
                        tak::png::write(capture,ow,oh,px);
                }
                endFrame();
            };
            const auto has=[&](int id) {
                return std::find(debugDotIds_.begin(),debugDotIds_.end(),id)!=debugDotIds_.end();
            };
            namespace td=tak::tacticaldots;
            // Slider at 50%: halfway (in wheel notches) from this window's floor to 1.0,
            // leaving room both below and above the threshold.
            tacticalDotsZoom_=50;
            const float zT=td::thresholdZoom(50,mapView_.minZoom(W,H))*td::kSlack;
            const float zIn=zT*0.95f,zBand=zT*1.08f,zOut=zT*td::kBuffer*1.05f;
            check(zIn>=mapView_.minZoom(W,H),"fixture has room below the threshold");
            tacticalDotsOpt_=false;frame(zIn);
            check(!dotsFrame_ && !visUnits_.empty(),"option off draws models even far out");
            tacticalDotsOpt_=true;frame(zOut);
            check(!dotsFrame_ && !visUnits_.empty(),"above the threshold models still draw");
            frame(zBand);
            check(!dotsFrame_ && !visUnits_.empty(),"entering the hysteresis band from above keeps models");
            frame(zIn);
            check(dotsFrame_ && visUnits_.empty() && debugDotCount_>0,"below the threshold dots replace every model");
            check(has(infantry) && has(keep) && has(flyer) && has(foeSeen),"own units, building, flyer and seen enemy get dots");
            check(!has(foeHidden),"enemy in fog gets no dot");
            {   // The same unit is shown by the minimap rule, the cull only aside.
                const auto* hidden=frameUnitP(foeHidden);
                check(hidden && !radarVisible(*hidden),"dots and minimap share the fog rule");
                const auto c=dotCentre(*hidden);
                check(c.x>0 && c.x<W-300 && c.y>0 && c.y<H,"hidden enemy is inside the view (fog, not cull)");
            }
            frame(zBand);
            check(dotsFrame_ && visUnits_.empty(),"leaving through the hysteresis band keeps dots");
            frame(zOut);
            check(!dotsFrame_ && !visUnits_.empty(),"past the exit zoom models return");
            frame(zIn);
            check(dotsFrame_,"dots come back below the threshold");
            // Picking: a click on the dot selects it, a small box around it too.
            const auto* inf=frameUnitP(infantry);
            const SDL_FPoint c=dotCentre(*inf);
            check(unitUnderCursor(*inf,c.x,c.y),"cursor over a dot hits its unit");
            const auto mouse=[&](uint32_t kind,float x,float y) {
                SDL_Event e{};e.type=kind;
                if (kind==SDL_MOUSEMOTION) {e.motion.x=int(x);e.motion.y=int(y);}
                else {e.button.button=SDL_BUTTON_LEFT;e.button.x=int(x);e.button.y=int(y);}
                input(e,W,H);
            };
            const auto savedMod=SDL_GetModState();SDL_SetModState(KMOD_NONE);
            selection_.clear();
            mouse(SDL_MOUSEBUTTONDOWN,c.x,c.y);mouse(SDL_MOUSEBUTTONUP,c.x,c.y);
            check(selection_==std::vector<int>{infantry},"click on a dot selects the unit");
            selection_.clear();
            mouse(SDL_MOUSEBUTTONDOWN,c.x-12,c.y-12);mouse(SDL_MOUSEMOTION,c.x+12,c.y+12);
            mouse(SDL_MOUSEBUTTONUP,c.x+12,c.y+12);
            check(std::find(selection_.begin(),selection_.end(),infantry)!=selection_.end() &&
                  std::find(selection_.begin(),selection_.end(),foeSeen)==selection_.end(),
                  "box around a dot selects own unit, never an enemy");
            SDL_SetModState(savedMod);
            selection_={infantry,keep,flyer};
            frame(zIn,tak::devEnv("TAK_TACTICAL_DOTS_CAPTURE"));
            check(debugDotCount_>0 && dotsFrame_,"selected dots still drawn");
            tacticalDotsOpt_=false;frame(zIn);
            check(!dotsFrame_ && !visUnits_.empty(),"turning the option off restores models at once");
            {   // The slider, as on a 7680x2160 window where the map-fills-window
                // floor (0.75 on Ulasem) sits far above any fixed zoom threshold.
                const float floor=0.75f,notch=td::kNotch;
                td::Switch s;
                check(!s.update(true,0,floor*notch*notch,floor),"0%: two notches above the floor: models");
                check(s.update(true,0,floor,floor),"0%: fully zoomed out: dots");
                check(s.update(true,0,floor*notch,floor),"0%: one notch in from the floor keeps dots");
                check(!s.update(true,0,floor*notch*notch,floor),"0%: second notch in brings models back");
                check(!s.update(true,0,floor*notch,floor),"0%: one notch above the floor, coming out: still models");
                check(!s.update(false,0,floor,floor),"option off overrides the slider");
                check(std::fabs(td::thresholdZoom(100,0.19f)-1.0f)<1e-4f,"100%: dots from normal size");
                check(std::fabs(td::thresholdZoom(0,0.19f)-0.19f)<1e-4f,"0%: dots only at the floor");
                check(td::thresholdZoom(20,0.19f)>0.19f && td::thresholdZoom(20,0.19f)<td::thresholdZoom(50,0.19f),
                      "threshold grows with the slider");
                check(std::fabs(td::thresholdZoom(70,1.5f)-1.5f)<1e-4f,"floor above normal size: always at the floor");
                td::Switch d;
                check(d.update(true,td::kDefaultPercent,0.19f,0.19f),"default 20%: fully zoomed out shows dots");
                check(!d.update(true,td::kDefaultPercent,0.5f,0.19f),"default 20%: zoom 0.5 at 1080p shows models");
                // Size: building > infantry, a minimum, and compact at a high zoom.
                check(td::dotSide(6,6,0.25f)>td::dotSide(2,2,0.25f),"building dot larger than infantry");
                check(td::dotSide(1,1,0.05f)>=td::kMinDotPx,"dots never shrink below the minimum");
                check(td::dotSide(2,2,1.5f)==td::dotSide(2,2,td::kCompactZoom),"dots stay compact when zoomed in");
            }
            // Leave dots on with a selection so the --shot capture shows both.
            tacticalDotsOpt_=true;selection_={infantry,keep,flyer};mapView_.setZoom(zIn);
            std::fprintf(stderr,"PASS: tactical dots (threshold %.3f/%.3f zoom at 50%%, min zoom here %.3f)\n",
                         zT,zT*td::kBuffer,mapView_.minZoom(W,H));
            return;
        }
#endif
        if (tak::devFlag("TAK_RUIN_TEST")) {
            const bool previousShadows=shadowsOnFrame_;
            shadowsOnFrame_=true;
            int checked=0;
            for (const auto& [name,type]:registry_.types()) {
                if (!type.explosionCorpse) continue;
                const int corpse=world_.corpseTypeOf(&type);
                if (corpse<0) throw std::runtime_error("building ruin definition missing");
                const int id=spawn(name,512,512,0,localPlayer_);
                UnitR unit;unit.id=id;unit.type=&type;unit.player=localPlayer_;
                unit.x=unit.px=512;unit.z=unit.pz=512;
                unit.deadFor=0;unit.corpsePhase=true;unit.corpseFeat=corpse;
                maybeSwapCorpseModel(unit);
                UnitGeom geometry;std::vector<Tri> scratch;
                buildUnitGeom(unit,geometry,scratch);
                // Flat rubble decals need not cast a separate raised shadow.
                if (geometry.verts.empty())
                    throw std::runtime_error("empty building ruin geometry: "+name);
                std::fprintf(stderr,"PASS: %s ruin body=%zu shadow=%zu\n",name.c_str(),
                    geometry.verts.size(),geometry.shadowVerts.size());
                ++checked;
            }
            shadowsOnFrame_=previousShadows;
            if (checked!=45) throw std::runtime_error("explosion corpse roster changed");
            return;
        }
#ifndef NDEBUG
        if (tak::devFlag("TAK_DIPLOMACY_TEST")) {
            int retiredFixtureId=0;
            if(tak::devFlag("TAK_RETIREMENT_TEST")) {
                tak::sim::UnitType dummy;dummy.id="retired-ui";dummy.maxHp=100;dummy.footX=dummy.footZ=1;
                for(int i=0;i<240;++i) {
                    retiredFixtureId=world_.spawn(&dummy,1800,2100,0,0);
                    world_.scenarioDestroy(retiredFixtureId);
                }
                for(int i=0;i<60;++i)world_.tick(1.f/30);
                for(const auto& u:world_.units())
                    if(u.type==&dummy)throw std::runtime_error("UI fixture retained final corpses");
            }
            world_.setPlayerCount(8);world_.setTeam(1,0);localPlayer_=0;
            resultParticipants_=uint8_t((1u<<0)|(1u<<1)|(1u<<2)|(1u<<7));
            playerName_[0]="PLAYER";playerName_[1]="ALLY";playerName_[2]="ENEMY";playerName_[7]="DEFEATED";
            world_.player(7).defeated=true;hotkeys_.load({});
            const int monarch=spawn("araking",1800,2100,0,0);
            const int soldier=spawn("arasword",1650,2100,0,0);
            spawn("arasword",2000,2100,0,1);spawn("tarsword",2200,2100,0,2);
            world_.unit(soldier)->squad=3;
            const auto publish=[&] {captureFrame();beginFrame();};
            const auto check=[](bool yes,const char* why) {if(!yes)throw std::runtime_error(why);};
            const auto has=[&](DiplomacyAction action,int p) {
                return std::any_of(giveUnitsHots_.begin(),giveUnitsHots_.end(),[&](const auto& h){return h.action==action && h.player==p;});
            };
            const auto click=[&](DiplomacyAction action,int p) {
                auto found=std::find_if(giveUnitsHots_.begin(),giveUnitsHots_.end(),[&](const auto& h){return h.action==action && h.player==p;});
                check(found!=giveUnitsHots_.end(),"diplomacy control unavailable");
                SDL_Event e{};e.type=SDL_MOUSEBUTTONUP;e.button.button=SDL_BUTTON_LEFT;
                e.button.x=int(found->rect.x+found->rect.w/2);e.button.y=int(found->rect.y+found->rect.h/2);
                input(e,1000,700);
            };
            selection_.clear();publish();
            if(tak::devFlag("TAK_RETIREMENT_TEST") &&
               (front().units.size()!=world_.units().size() || !frameUnitP(monarch) || frameUnitP(retiredFixtureId)))
                throw std::runtime_error("compacted render snapshot lost high-ID units");
            SDL_Event key{};key.type=SDL_KEYDOWN;key.key.keysym.sym=SDLK_d;
            input(key,1000,700);check(giveUnitsMenu_,"D must open diplomacy without a selection");
            drawGiveUnitsMenu(1000,700);
            check(!has(DiplomacyAction::Give,1),"empty selection must disable gifts");
            check(has(DiplomacyAction::Mana,1) && !has(DiplomacyAction::Mana,2) && !has(DiplomacyAction::Mana,0),"only teammates can receive mana");
            for(int p:{0,1,2,7})check(has(DiplomacyAction::Chat,p),"all participants need chat controls");
            check(!has(DiplomacyAction::Chat,3),"empty slots must not appear as players");
            check(chatRecipients_==0xff && framePlayer(0).manaShareMask==0xff,"default sharing/chat preferences");
            click(DiplomacyAction::Chat,2);check(!(chatRecipients_ & 4),"chat checkbox updates recipients");
            click(DiplomacyAction::Mana,1);check(!(world_.player(0).manaShareMask & 2),"mana checkbox issues command");
            selection_={monarch};drawGiveUnitsMenu(1000,700);
            check(!has(DiplomacyAction::Give,1),"ineligible monarch must disable gifts");
            selection_={monarch,soldier};drawGiveUnitsMenu(1000,700);
            check(has(DiplomacyAction::Give,1) && !has(DiplomacyAction::Give,2),"eligible gifts only enabled for allies");
            click(DiplomacyAction::Give,1);publish();drawGiveUnitsMenu(1000,700);
            check(giveUnitsMenu_ && !has(DiplomacyAction::Give,1),"gift keeps menu open and disables exhausted selection");
            check(selection_==std::vector<int>{monarch} && world_.unit(soldier)->player==1 && world_.unit(soldier)->squad==0,"gift filters selection and removes old control group");
            input(key,1000,700);check(!giveUnitsMenu_,"D closes diplomacy");
            input(key,1000,700);drawGiveUnitsMenu(1000,700);
            std::fprintf(stderr,"PASS: diplomacy hotkey, roster, defaults, checkboxes, eligibility, gift and selection cleanup\n");
            return;
        }
        if (tak::devFlag("TAK_CURSOR_TEST")) {
            noFog_=true;edgeScrollOn_=false;
            world_.setPlayerCount(8);
            const int builder=spawn("araking",1800,2100,0,0);
            const int soldier=spawn("arasword",1650,2100,0,0);
            const int site=spawn("creacad",2000,2000,0,0);
            world_.unit(site)->underConstruction=true;world_.unit(site)->buildBegun=true;
            world_.unit(site)->hp=tak::sim::Fixed::fromInt(10);
            mapView_.setZoom(1);
            mapView_.setOffset(1400,1500);
            const auto publish=[&] {captureFrame();beginFrame();cosmeticStep(0);};
            const auto point=[&](int id) {
                const auto* u=frameUnitP(id);const auto p=unitScreen(*u);
                const auto& box=unitHitBox(u->type);const float zoom=mapView_.zoom();
                mouseX_=p.x+(box.x+box.w*.5f)*zoom;
                mouseY_=p.y+(12+box.y+box.h*.5f)*zoom;
            };
            const auto expect=[&](tak::CursorId expected,const char* message) {
                bool tint=false;const auto got=desiredCursor(tint);
                if (got!=expected) throw std::runtime_error(std::string(message)+": cursor "+
                    std::to_string(int(got))+" expected "+std::to_string(int(expected)));
                std::fprintf(stderr,"PASS: %s\n",message);
            };
            publish();point(site);selection_={soldier};
            expect(tak::CursorId::Green,"non-builder cannot assist unfinished construction");
            selection_={builder};
            expect(tak::CursorId::Repair,"unrestricted builder can assist own construction");
            winW_=1000;winH_=700;drawPanel(winW_,winH_);
            if (iconRects_.empty() || selectedBuilder()->id!=builder)
                throw std::runtime_error("single builder lost its build controls");
            const auto icon=iconRects_.front().first;
            const auto* buildType=iconRects_.front().second;
            const float offX=mapView_.offX(),offY=mapView_.offY();
            const float ix=icon.x+icon.w*.5f,iy=icon.y+icon.h*.5f;
            // Put unfinished construction directly behind a real build icon.
            mapView_.setOffset(offX+mouseX_-ix,offY+mouseY_-iy);point(site);
            const auto strip=buildMenuRect_;buildMenuRect_={};
            expect(tak::CursorId::Repair,"construction remains pickable beneath the build strip");
            buildMenuRect_=strip;
            expect(tak::CursorId::Normal,"build icon hides the construction cursor behind it");
            placing_=buildType;
            expect(tak::CursorId::Normal,"build icon hides an armed placement cursor");
            placing_=nullptr;pendingCmd_='f';
            bool tint=true;
            if (desiredCursor(tint)!=tak::CursorId::Normal || tint)
                throw std::runtime_error("build icon retained an armed fight cursor/tint");
            pendingCmd_=0;
            mouseX_=buildMenuRect_.x+1;mouseY_=buildMenuRect_.y+1;
            expect(tak::CursorId::Normal,"build strip padding is also UI");
            SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_RIGHT;
            click.button.x=int(mouseX_);click.button.y=int(mouseY_);
            input(click,winW_,winH_);
            if (!world_.unit(builder)->orders.empty() || world_.unit(builder)->buildSiteId)
                throw std::runtime_error("build strip padding issued a world order");
            for (const auto& ids:std::array<std::vector<int>,3>{{{builder,soldier},{soldier,builder},{builder,site}}}) {
                selection_=ids;
                if (selectedBuilder() || buildIconClick(ix,iy,true,false))
                    throw std::runtime_error("mixed/multiple builders used stale build icons");
                drawPanel(winW_,winH_);
                if (!iconRects_.empty() || buildMenuRect_.w!=0)
                    throw std::runtime_error("multiple selection displayed build controls");
            }
            selection_={soldier};drawPanel(winW_,winH_);
            if (!iconRects_.empty()) throw std::runtime_error("non-builder displayed build controls");
            selection_={builder};mapView_.setOffset(offX,offY);point(site);
            drawPanel(winW_,winH_);
            std::fprintf(stderr,"PASS: build controls cover world picking and require one builder\n");
            // Real icon / mouse / key events: queued placement must leave the
            // builder selected, and only commit the Shift-release reset after
            // at least one accepted placement. These are client input checks;
            // the following cursor fixtures still start with an empty queue.
            const auto buildIcon=std::find_if(iconRects_.begin(),iconRects_.end(),
                [](const auto& entry){return entry.second && !entry.second->onMana;});
            if(buildIcon==iconRects_.end())throw std::runtime_error("placement test needs a non-mana build icon");
            const auto* queuedType=buildIcon->second;
            const auto arm=[&] {
                SDL_Event e{};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;
                e.button.x=int(buildIcon->first.x+buildIcon->first.w*.5f);
                e.button.y=int(buildIcon->first.y+buildIcon->first.h*.5f);input(e,winW_,winH_);
                if(placing_!=queuedType || shiftBuildPlaced_)throw std::runtime_error("new build icon did not start fresh placement");
            };
            const auto releaseShift=[&](SDL_Keycode key,SDL_Keymod remaining) {
                SDL_SetModState(remaining);SDL_Event e{};e.type=SDL_KEYUP;e.key.keysym.sym=key;
                e.key.keysym.mod=remaining;input(e,winW_,winH_);
                if(selection_!=std::vector<int>{builder})throw std::runtime_error("Shift release deselected the builder");
            };
            const auto mouse=[&](uint32_t kind,int x,int y) {
                SDL_Event e{};e.type=kind;e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;
                input(e,winW_,winH_);
            };
            const auto savedMod=SDL_GetModState();SDL_SetModState(KMOD_NONE);
            arm();releaseShift(SDLK_LSHIFT,KMOD_NONE);
            if(placing_!=queuedType)throw std::runtime_error("Shift without placement disarmed the build icon");
            int placeX=-1,placeY=-1;
            for(int y=40;y<winH_-barH()-20 && placeX<0;y+=16)
                for(int x=40;x<mapViewW(winW_)-20;x+=16) {
                    float wx,wz;pickWorld(float(x),float(y),wx,wz);
                    if(!overBuildMenu(float(x),float(y)) && world_.canPlace(queuedType,wx,wz,localPlayer_)) {
                        placeX=x;placeY=y;break;
                    }
                }
            if(placeX<0)throw std::runtime_error("placement fixture could not find a valid site");
            SDL_SetModState(KMOD_LSHIFT);
            mouse(SDL_MOUSEBUTTONDOWN,placeX,placeY);mouse(SDL_MOUSEBUTTONUP,placeX,placeY);
            if(placing_!=queuedType || !shiftBuildPlaced_ || world_.unit(builder)->orders.empty())
                throw std::runtime_error("Shift click did not queue and retain the armed build icon");
            const auto queued=world_.unit(builder)->orders.size();
            releaseShift(SDLK_RSHIFT,KMOD_LSHIFT);
            if(placing_!=queuedType)throw std::runtime_error("releasing one Shift disarmed while the other was held");
            releaseShift(SDLK_LSHIFT,KMOD_NONE);
            if(placing_ || shiftBuildPlaced_ || world_.unit(builder)->orders.size()!=queued)
                throw std::runtime_error("last Shift release did not disarm without changing queued work");
            arm();SDL_SetModState(KMOD_LSHIFT);
            mouse(SDL_MOUSEBUTTONDOWN,placeX,placeY);
            releaseShift(SDLK_LSHIFT,KMOD_NONE);
            if(!buildDrag_ || placing_!=queuedType)throw std::runtime_error("mid-drag release lost pending placement");
            mouse(SDL_MOUSEBUTTONUP,placeX,placeY);
            if(placing_ || buildDrag_ || selection_!=std::vector<int>{builder})
                throw std::runtime_error("drag completion after Shift release did not keep the builder");
            arm();SDL_SetModState(KMOD_LSHIFT);
            mouse(SDL_MOUSEBUTTONDOWN,placeX,placeY);mouse(SDL_MOUSEBUTTONUP,placeX,placeY);
            arm();releaseShift(SDLK_LSHIFT,KMOD_NONE);
            if(placing_!=queuedType)throw std::runtime_error("fresh icon inherited a prior Shift placement");
            placing_=nullptr;shiftBuildPlaced_=false;
            world_.cancelBuilds(builder);world_.stop(builder);SDL_SetModState(savedMod);publish();point(site);
            // An invalid placement must not count as a queued build.
            int invalidX=-1,invalidY=-1;
            for(int y=40;y<winH_-barH()-20 && invalidX<0;y+=16)
                for(int x=40;x<mapViewW(winW_)-20;x+=16) {
                    float wx,wz;pickWorld(float(x),float(y),wx,wz);std::vector<int> clearing;
                    if(!overBuildMenu(float(x),float(y)) && !world_.canPlace(queuedType,wx,wz,localPlayer_) &&
                       !world_.clearableForPlacement(queuedType,wx,wz,clearing,localPlayer_)) {
                        invalidX=x;invalidY=y;break;
                    }
                }
            if(invalidX<0)throw std::runtime_error("placement fixture needs a blocked site");
            arm();SDL_SetModState(KMOD_LSHIFT);
            mouse(SDL_MOUSEBUTTONDOWN,invalidX,invalidY);mouse(SDL_MOUSEBUTTONUP,invalidX,invalidY);
            releaseShift(SDLK_LSHIFT,KMOD_NONE);
            if(placing_!=queuedType || shiftBuildPlaced_ || !world_.unit(builder)->orders.empty())
                throw std::runtime_error("invalid placement counted as queued work");
            SDL_SetModState(KMOD_LSHIFT);mouse(SDL_MOUSEBUTTONDOWN,invalidX,invalidY);
            releaseShift(SDLK_LSHIFT,KMOD_NONE);mouse(SDL_MOUSEBUTTONUP,invalidX,invalidY);
            if(placing_!=queuedType || shiftBuildPlaced_)
                throw std::runtime_error("invalid drag disarmed without any queued placement");
            const auto manaIcon=std::find_if(iconRects_.begin(),iconRects_.end(),
                [](const auto& entry){return entry.second && entry.second->onMana;});
            if(manaIcon==iconRects_.end())throw std::runtime_error("placement fixture needs a mana build icon");
            mouse(SDL_MOUSEBUTTONDOWN,int(manaIcon->first.x+manaIcon->first.w*.5f),
                int(manaIcon->first.y+manaIcon->first.h*.5f));
            SDL_SetModState(KMOD_LSHIFT);
            mouse(SDL_MOUSEBUTTONDOWN,100,100);mouse(SDL_MOUSEBUTTONUP,180,180);
            if(!shiftBuildPlaced_ || world_.unit(builder)->orders.empty() ||
               !world_.unit(builder)->orders.back().manaBuildArea)
                throw std::runtime_error("Shift area placement did not queue a mana job");
            releaseShift(SDLK_LSHIFT,KMOD_NONE);
            if(placing_ || world_.unit(builder)->orders.empty())
                throw std::runtime_error("Shift release lost queued mana-area work or kept placement armed");
            world_.stop(builder);SDL_SetModState(savedMod);publish();point(site);
            std::fprintf(stderr,"PASS: Shift placement release clears the build icon, keeps the builder and queue, supports both keys and mid-drag release\n");
            // Armed orders follow the same rule: Shift queues the order and keeps
            // the command armed for the next click; releasing Shift disarms.
            world_.stop(builder);publish();
            const auto goals=[&] {
                size_t n=0;for(const auto& o:world_.unit(builder)->orders)n+=o.goal;return n;
            };
            pendingCmd_='m';SDL_SetModState(KMOD_LSHIFT);
            mouse(SDL_MOUSEBUTTONDOWN,placeX,placeY);mouse(SDL_MOUSEBUTTONUP,placeX,placeY);publish();
            mouse(SDL_MOUSEBUTTONDOWN,invalidX,invalidY);mouse(SDL_MOUSEBUTTONUP,invalidX,invalidY);publish();
            if(pendingCmd_!='m' || goals()!=2)
                throw std::runtime_error("Shift-clicked armed move did not queue and stay armed");
            pendingCmd_='p';
            mouse(SDL_MOUSEBUTTONDOWN,placeX,placeY);mouse(SDL_MOUSEBUTTONUP,placeX,placeY);publish();
            if(pendingCmd_!='p' || goals()!=3 || !world_.unit(builder)->orders.back().patrol)
                throw std::runtime_error("Shift-clicked armed patrol did not append to the move queue");
            releaseShift(SDLK_LSHIFT,KMOD_NONE);
            if(pendingCmd_ || goals()!=3)throw std::runtime_error("Shift release did not disarm the queued order mode");
            pendingCmd_='m';
            mouse(SDL_MOUSEBUTTONDOWN,placeX,placeY);mouse(SDL_MOUSEBUTTONUP,placeX,placeY);publish();
            if(pendingCmd_ || goals()!=1)throw std::runtime_error("plain armed move did not replace the queue and disarm");
            pendingCmd_='m';SDL_SetModState(KMOD_LSHIFT);
            mouse(SDL_MOUSEBUTTONDOWN,invalidX,invalidY);mouse(SDL_MOUSEBUTTONUP,invalidX,invalidY);
            SDL_SetModState(KMOD_NONE);
            {SDL_Event motion{};motion.type=SDL_MOUSEMOTION;motion.motion.x=placeX;motion.motion.y=placeY;input(motion,winW_,winH_);}
            if(pendingCmd_)throw std::runtime_error("an unseen Shift release did not disarm on the next mouse event");
            world_.stop(builder);SDL_SetModState(savedMod);publish();point(site);
            std::fprintf(stderr,"PASS: Shift keeps an armed order armed and queues each click; releasing Shift disarms\n");
            const auto* manaType=registry_.find("aralode");
            if(!manaType || manaSpots_.empty())throw std::runtime_error("area trail fixture needs lodestones and deposits");
            world_.queueManaBuildArea(builder,manaType,0,0,float(mapView_.map().width*16-1),
                float(mapView_.map().height*16-1),false);
            world_.order(builder,2200,2300,true);
            publish();syncBurningFeatures();
            if(!cursors_.ok()) {cursorsInit_=true;cursors_.load(ren_,vfs_,settings_);}
            const auto previewHash=world_.stateHash();SDL_SetModState(KMOD_LSHIFT);
            drawOrderTrails(mapViewW(winW_),winH_);
            const auto& preview=orderTrailCache_[0];
            size_t sitesShown=0;for(const auto& o:preview.orders)if(o.buildType && o.trailPreview)++sitesShown;
            if(preview.unitId!=builder || sitesShown<2 || preview.orders.empty() ||
               preview.orders.back().buildType || preview.orders.back().x.toFloat()!=2200 ||
               world_.stateHash()!=previewHash)
                throw std::runtime_error("live area-build overlay lost remaining sites, queued tail, or changed simulation state");
            drawOrderTrails(mapViewW(winW_),winH_);
            if(world_.stateHash()!=previewHash)throw std::runtime_error("cached area preview changed simulation state");
            world_.stop(builder);publish();drawOrderTrails(mapViewW(winW_),winH_);
            SDL_SetModState(savedMod);point(site);
            std::fprintf(stderr,"PASS: live area trail shows remaining sites and queued tail without changing simulation state\n");
            const auto* original=world_.unit(builder)->type;
            auto restricted=*original;restricted.builderLimited=true;
            world_.unit(builder)->type=&restricted;publish();point(site);
            expect(tak::CursorId::Green,"restricted builder cannot assist an unavailable type");
            pendingCmd_='r';expect(tak::CursorId::Normal,"armed repair refuses unavailable construction");
            pendingCmd_=0;world_.unit(builder)->type=original;publish();point(site);
            rightClickOrder(2000,2000,false);
            if (world_.unit(builder)->buildSiteId!=site) throw std::runtime_error("assist cursor click did not assist");
            world_.cancelBuilds(builder);world_.stop(builder);
            world_.unit(site)->underConstruction=false;world_.unit(site)->hp=tak::sim::Fixed::fromInt(10);
            publish();point(site);
            expect(tak::CursorId::Repair,"damaged friendly advertises repair");
            rightClickOrder(2000,2000,false);
            if (world_.unit(builder)->orders.empty() || world_.unit(builder)->orders.back().repairTarget!=site)
                throw std::runtime_error("repair cursor click did not repair");
            world_.stop(builder);selection_.clear();
            expect(tak::CursorId::Select,"unselected building uses its displayed bounds");
            selection_={soldier};pendingCmd_='r';
            expect(tak::CursorId::Normal,"non-builder cannot arm repair");
            pendingCmd_='u';expect(tak::CursorId::Normal,"non-transport cannot arm unload");
            pendingCmd_='c';expect(tak::CursorId::Normal,"non-reclaimer cannot arm reclaim");
            pendingCmd_=0;
            const auto savedPathfinding=world_.pathfindingMode();
            selection_={builder,soldier};mouseX_=mouseY_=-1000;
            for(const auto mode:{tak::sim::PathfindingMode::Retail,tak::sim::PathfindingMode::Legion}) {
                world_.setPathfindingMode(mode);publish();
                rightClickOrder(2400,2400,false);
                for(const int id:selection_) {
                    const auto* u=world_.unit(id);
                    if(u->orders.empty())throw std::runtime_error("group right-click lost a selected mover");
                    const auto& order=u->orders.back();
                    const auto target=order.missionTarget.value_or(std::pair{order.x,order.z});
                    // Legion gives every unit it plans one shared point (docs/legion-pathfinding.md).
                    const bool shared=mode==tak::sim::PathfindingMode::Legion;
                    const float expected=shared?2400.f:
                        (id==builder?2460.f:2340.f);
                    if(target.first.toFloat()!=expected || target.second.toFloat()!=2400.f)
                        throw std::runtime_error("group right-click split shared destinations or changed Retail offsets");
                    world_.stop(id);
                }
            }
            const int flyer=spawn("zonhunt",1650,2100,0,0);
            selection_={builder,flyer};publish();
            rightClickOrder(2400,2400,false);
            if(world_.unit(builder)->orders.back().x.toFloat()!=2400.f ||
               world_.unit(flyer)->orders.back().x.toFloat()!=2340.f)
                throw std::runtime_error("shared group right-click changed independent flight destinations");
            world_.stop(builder);world_.stop(flyer);
            world_.setPathfindingMode(savedPathfinding);
            std::fprintf(stderr,"PASS: group right-click shares supported destination areas and preserves Retail/fallback offsets\n");
            resultParticipants_=uint8_t((1u<<0)|(1u<<3));
            playerName_[0]="PLAYER";playerName_[3]="AI";
            world_.player(3).defeated=true;publish();
            const auto results=resultStats();
            if (results.rows.size()!=2 || results.rows[1].name!="AI" || !results.rows[1].defeated)
                throw std::runtime_error("sparse results roster lost a participant or included an empty slot");
            std::fprintf(stderr,"PASS: sparse results show participants, including defeated players\n");
            resultParticipants_.reset();selection_={builder};
            return;
        }
#endif
        // Repeatable rendering workload for the shadow performance regression.
        // This entry point is only reached by the local development harness.
        if (const char* countText=tak::devEnv("TAK_SHADOW_BENCH")) {
            const int count=std::clamp(std::atoi(countText),1,16000);
            const char* only=tak::devEnv("TAK_SHADOW_BENCH_TYPE");
            const char* roster[]={"zonter","araarch","aralode","tarlode","zonroc","versword"};
            const int columns=std::max(1,int(std::ceil(std::sqrt(float(count)*1.6f))));
            const char* spacingText=tak::devEnv("TAK_RENDER_SPACING");
            const float spacing=spacingText?std::clamp(float(std::atof(spacingText)),16.f,256.f):40.f;
            const int rows=(count+columns-1)/columns;
            int spawned=0;
            for(int i=0;i<count;++i) {
                const char* name=only ? only : roster[i%6];
                const auto* type=registry_.find(name);
                if(!type || world_.atUnitCap(localPlayer_) || world_.atTypeCap(localPlayer_,type)) continue;
                const float x=5000+(float(i%columns)-float(columns-1)*0.5f)*spacing;
                const float z=5000+(float(i/columns)-float(rows-1)*0.5f)*spacing;
                const bool battle=tak::devFlag("TAK_RENDER_BATTLE");
                const int owner=battle?i%2:localPlayer_;
                if(const int id=spawn(name,x,z,float(i%8)*0.785398163f,owner)) {
                    ++spawned;
                    if(tak::devFlag("TAK_RENDER_MOTION"))world_.order(id,x+320,z+160,false);
                    if(battle)world_.attackMove(id,5000,5000,false);
                }
            }
            noFog_=!tak::devFlag("TAK_PROFILE_FOG");edgeScrollOn_=false;
            float zoom=std::min(1.5f,std::min(1400.f/(columns*40),760.f/(rows*40)));
            if(const char* requested=tak::devEnv("TAK_PROFILE_ZOOM"))
                zoom=std::clamp(float(std::atof(requested)),0.05f,4.0f);   // the game's zoom range
            mapView_.setZoom(zoom);
            mapView_.setOffset(5000-760/zoom,5000-terrainLift(5000,5000)-420/zoom);
            std::fprintf(stderr,"shadow benchmark: requested=%d spawned=%d columns=%d zoom=%.3f type=%s\n",
                count,spawned,columns,zoom,only ? only : "mixed");
            return;
        }
        // Capture the reclaim particle fix through the existing local build harness.
        if (tak::devFlag("TAK_RECLAIM_CAPTURE")) {
            const char* name=tak::devEnv("TAK_CONJURE_BUILDER");
            if (!name) name="araking";
            const auto* type=registry_.find(name);
            if (!type || !type->canReclaim) return;
            for (const auto& feature:world_.features()) {
                if (!feature.alive || feature.manaYield<250 || feature.work.toFloat()<250) continue;
                const float x=feature.x.toFloat(),z=feature.z.toFloat();
                for (const auto& [dx,dz]:std::array<std::pair<float,float>,4>{
                         {{-96,0},{96,0},{0,-96},{0,96}}}) {
                    if (!world_.canPlace(type,x+dx,z+dz)) continue;
                    const int id=spawn(name,x+dx,z+dz,0,localPlayer_);
                    tak::net::Command command;
                    command.kind=tak::net::Cmd::Reclaim;command.player=localPlayer_;
                    command.unitId=id;command.targetId=feature.id;
                    tak::sim::applyCommand(world_,registry_,command);
                    selection_={id};noFog_=true;edgeScrollOn_=false;
                    mapView_.setZoom(3);
                    mapView_.setOffset(x+dx-500/3.f,
                        z+dz-terrainLift(x+dx,z+dz)-400/3.f);
                    std::printf("reclaim capture: %s #%d at %.0f,%.0f -> feature %d at %.0f,%.0f\n",
                        name,id,x+dx,z+dz,feature.id,x,z);
                    return;
                }
            }
            std::fprintf(stderr,"reclaim capture: no suitable feature/site\n");
            return;
        }
        if (tak::devFlag("TAK_CONJURE_TEST")) {
            const char* builder=tak::devEnv("TAK_CONJURE_BUILDER");
            const char* target=tak::devEnv("TAK_CONJURE_TARGET");
            const char* builderXEnv=tak::devEnv("TAK_CONJURE_BUILDER_X");
            const char* builderZEnv=tak::devEnv("TAK_CONJURE_BUILDER_Z");
            const char* siteXEnv=tak::devEnv("TAK_CONJURE_SITE_X");
            const char* siteZEnv=tak::devEnv("TAK_CONJURE_SITE_Z");
            if (!builder) builder="zonhunt";
            if (!target) target="zonter";
            if (!registry_.find(builder) || !registry_.find(target)) {
                std::fprintf(stderr,"conjure fixture: unknown builder %s or target %s\n",builder,target);
                return;
            }
            auto parseFloat=[](const char* name,const char* value,float& out) {
                char* end=nullptr;
                out=std::strtof(value,&end);
                if (end==value || !end || *end || !std::isfinite(out)) {
                    std::fprintf(stderr,"conjure fixture: invalid %s value '%s'\n",name,value);
                    return false;
                }
                return true;
            };
            if (bool(builderXEnv)!=bool(builderZEnv) || bool(siteXEnv)!=bool(siteZEnv)) {
                std::fprintf(stderr,"conjure fixture: builder/site coordinates require both X and Z\n");
                return;
            }
            const bool explicitBuilder=builderXEnv && builderZEnv;
            const bool explicitSite=siteXEnv && siteZEnv;
            float builderX=1920,builderZ=1616,siteX=0,siteZ=0;
            if (explicitBuilder &&
                (!parseFloat("TAK_CONJURE_BUILDER_X",builderXEnv,builderX) ||
                 !parseFloat("TAK_CONJURE_BUILDER_Z",builderZEnv,builderZ))) return;
            if (explicitSite &&
                (!parseFloat("TAK_CONJURE_SITE_X",siteXEnv,siteX) ||
                 !parseFloat("TAK_CONJURE_SITE_Z",siteZEnv,siteZ))) return;
            float zoom=3.0f,screenX=500,screenY=400;
            if (const char* value=tak::devEnv("TAK_CONJURE_ZOOM"))
                if (!parseFloat("TAK_CONJURE_ZOOM",value,zoom) || zoom<=0) return;
            const char* screenXEnv=tak::devEnv("TAK_CONJURE_SCREEN_X");
            const char* screenYEnv=tak::devEnv("TAK_CONJURE_SCREEN_Y");
            if (bool(screenXEnv)!=bool(screenYEnv)) {
                std::fprintf(stderr,"conjure fixture: screen coordinates require both X and Y\n");
                return;
            }
            if (screenXEnv &&
                (!parseFloat("TAK_CONJURE_SCREEN_X",screenXEnv,screenX) ||
                 !parseFloat("TAK_CONJURE_SCREEN_Y",screenYEnv,screenY))) return;
            int id=0;
            if (!explicitBuilder && tak::devEnv("TAK_CONJURE_BUILDER"))
                for (const auto& u:world_.units())
                    if (u.alive() && u.player==localPlayer_ && u.type==registry_.find(builder)) {
                        id=u.id;break;
                    }
            if (!id) id=spawn(builder,builderX,builderZ,0,0);
            const auto* fixtureBuilder=world_.unit(id);
            if (!fixtureBuilder) return;
            const float originX=fixtureBuilder->x.toFloat()+128;
            const float originZ=fixtureBuilder->z.toFloat();
            if (!explicitSite) { siteX=originX;siteZ=originZ; }
            const auto* targetType=registry_.find(target);
            if (explicitSite) {
                if (!world_.canPlace(targetType,siteX,siteZ)) {
                    std::fprintf(stderr,"conjure fixture: no legal site for %s at %.3f,%.3f\n",
                                 target,siteX,siteZ);
                    return;
                }
            } else if (tak::devEnv("TAK_CONJURE_REPLACE") && targetType->onMana) {
                float best=std::numeric_limits<float>::max();
                for (const auto& [x,z]:world_.manaSpots()) {
                    const float sx=tak::sim::footprintWaypoint(tak::sim::footprintCell(x,targetType->footX),targetType->footX).toFloat();
                    const float sz=tak::sim::footprintWaypoint(tak::sim::footprintCell(z,targetType->footZ),targetType->footZ).toFloat();
                    const float d=(sx-originX)*(sx-originX)+(sz-originZ)*(sz-originZ);
                    if (d<best && world_.canPlace(targetType,sx,sz)) {best=d;siteX=sx;siteZ=sz;}
                }
                if (best==std::numeric_limits<float>::max())
                    throw std::runtime_error("conjure replacement fixture has no free mana spot");
            } else if (tak::devEnv("TAK_CONJURE_TARGET")) {
                bool found=false;
                for (int r=0;r<=640 && !found;r+=32)
                    for (int dz=-r;dz<=r && !found;dz+=32)
                        for (int dx=-r;dx<=r && !found;dx+=32) {
                            if (std::max(std::abs(dx),std::abs(dz))!=r) continue;
                            if (world_.canPlace(targetType,originX+dx,originZ+dz)) {
                                siteX=originX+dx;siteZ=originZ+dz;found=true;
                            }
                        }
                if (!found) {
                    std::fprintf(stderr,"conjure fixture: no legal site for %s\n",target);
                    return;
                }
            }
            if (const char* base=tak::devEnv("TAK_CONJURE_REPLACE")) {
                const auto* baseType=registry_.find(base);
                float bx=siteX,bz=siteZ,best=24.0f*24.0f;
                for (const auto& [x,z]:world_.manaSpots()) {
                    const float d=(x-siteX)*(x-siteX)+(z-siteZ)*(z-siteZ);
                    if (d<best) {best=d;bx=x;bz=z;}
                }
                if (!baseType || !world_.canPlace(baseType,bx,bz))
                    throw std::runtime_error("conjure replacement fixture has no legal base");
                spawn(base,bx,bz,0,localPlayer_);
            }
            if (tak::devFlag("TAK_CONJURE_REPAIR")) {
                const int targetId=spawn(target,siteX,siteZ,0,localPlayer_);
                world_.unit(targetId)->hp=tak::sim::Fixed::fromFloat(targetType->maxHp*0.5f);
                tak::net::Command command;command.kind=tak::net::Cmd::Repair;
                command.unitId=id;command.targetId=targetId;command.player=localPlayer_;
                tak::sim::applyCommand(world_,registry_,command);
                std::printf("repair capture: worker #%d -> target #%d\n",id,targetId);
            } else world_.queueBuild(id,targetType,siteX,siteZ,false);
            if (const auto* u=world_.unit(id))
                std::printf("conjure fixture: %s #%d at %.0f,%.0f -> %s at %.0f,%.0f; orders=%zu\n",
                            builder,id,u->x.toFloat(),u->z.toFloat(),target,siteX,siteZ,u->orders.size());
            selection_={id};
            noFog_=true;edgeScrollOn_=false;
            mapView_.setZoom(zoom);
            mapView_.setOffset(siteX-screenX/zoom,
                               siteZ-terrainLift(siteX,siteZ)-screenY/zoom);
            return;
        }
        const auto* keep = world_.unit(keepId_);
        if (!keep) return;
        const auto* lode = registry_.find("aralode");
        // Probe outward from the keep for the first legal site.
        for (float r = 90; r < 400; r += 24) {
            for (float a = 0; a < 6.28f; a += 0.5f) {
                float x = keep->x.toFloat() + std::cos(a) * r, z = keep->z.toFloat() + std::sin(a) * r;
                if (world_.canPlace(lode, x, z)) {
                    int id = world_.startBuild(builderId_, lode, x, z);
                    std::printf("testbuild: site id %d at %.0f,%.0f\n", id, x, z);
                    return;
                }
            }
        }
        std::printf("testbuild: no site found\n");
    }

    void GameView::startAtMonarch() {
        const tak::sim::Unit* target=nullptr;
        for (const auto& u : world_.units()) {
            if (u.player!=localPlayer_ || !u.alive() || !u.type) continue;
            if (!target || u.type->commander) target=&u;
            if (u.type->commander) break;
        }
        if (target) {
            playerMonarchId_=target->id;builderId_=target->id;
            initialCamera_=std::pair{target->x.toFloat(),target->z.toFloat()};
        }
    }

    void GameView::lookAt(float x, float z) {
        initialCamera_.reset();
        mapView_.setOffset(x - 640 / mapView_.zoom(), z - 400 / mapView_.zoom());
    }

    void GameView::soundTest() {
        int id = spawn("araarch", 900, 1000, 0, 0);
        selection_ = {id};
        for (int i = 0; i < 8; i++) voice(id, "move");
    }

    void GameView::faceTest() {
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        // araarch (correct, +h) vs zonhand, both walking east, arrows on.
        float ddx[4]={0,400,0,-400}, ddz[4]={-400,0,400,0};
        for (int i=0;i<4;i++){
            int id=spawn("araarch", cx+ddx[i]*0.15f, cz+ddz[i]*0.15f, 0, 0);
            world_.order(id, cx+ddx[i], cz+ddz[i], false);
        }
        mapView_.setOffset(cx - 640 / mapView_.zoom(), cz - 400 / mapView_.zoom());
    }

    void GameView::fireTest() {
        if(tak::devFlag("TAK_PALETTE_ROSTER_TEST")) {
            std::set<std::string> names;
            const auto gather=[&](const auto& self,const PieceMeta& meta)->void {
                if(!meta.skip)names.insert(meta.primTex.begin(),meta.primTex.end());
                for(const auto& child:meta.children)self(self,child);
            };
            size_t structures=0;
            for(const auto& [name,type]:registry_.types())if(type.isStructure()) {
                if(loadVisual(name)) {gather(gather,visuals_.at(name).meta);++structures;}
            }
            size_t pixels=0,frames=0,missing=0;
            for(const auto& name:names) {
                auto source=paletteTextureSources_.find(name);
                if(source==paletteTextureSources_.end())continue;
                for(const auto& frame:source->second.indexed) {
                    pixels+=size_t(frame.width)*frame.height;++frames;
                }
            }
            std::fprintf(stderr,"PALETTE_ROSTER structures=%zu textures=%zu frames=%zu rawMiB=%.2f\n",
                structures,names.size(),frames,double(pixels*20*4)/(1024*1024));
            for(const auto& name:names) {
                preparePaletteTexture(name);
                auto sheet=paletteTextureSheets_.find(name);
                if(sheet!=paletteTextureSheets_.end())for(const auto& frame:sheet->second)
                    missing+=!frame.texture;
            }
            std::fprintf(stderr,"PALETTE_ROSTER pages=%zu missing=%zu\n",paletteTexturePages_.size(),missing);
            if(missing)throw std::runtime_error("shipped structure palette sheets exceed capacity");
            std::fprintf(stderr,"PASS: full structure palette texture roster fits bounded pages\n");
            return;
        }
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        if (tak::devEnv("TAK_WEAPON_IMPACT_EFFECT_TEST")) {
            // Every shipped travelling projectile has authoritative XYZ. Only
            // deliberately incomplete synthetic Weapon fixtures use the old 2D path.
            for(const auto& [id,type]:registry_.types())for(const auto& weapon:type.weapons)
                if(weapon.projVel>0 && !weapon.melee && !weapon.beam &&
                   weapon.kind==tak::sim::Weapon::Kind::Normal && !weapon.ballistic)
                    throw std::runtime_error("production weapon reaches legacy 2D path: "+id);
            const auto* arapult = registry_.find("arapult");
            if (!arapult || arapult->weapons.empty())
                throw std::runtime_error("weapon impact effect fixture has no Arapult weapon");
            const auto& authored = arapult->weapons.front();
            if (authored.explosionClass.empty() || authored.waterExplosionClass.empty())
                throw std::runtime_error("Arapult weapon is missing an authored impact class");

            int landX = -1, landZ = -1, waterX = -1, waterZ = -1;
            const auto& map = mapView_.map();
            for (int z = 0; z < map.height && (landX < 0 || waterX < 0); ++z)
                for (int x = 0; x < map.width && (landX < 0 || waterX < 0); ++x) {
                    const float wx = (float(x) + 0.5f) * 16.0f;
                    const float wz = (float(z) + 0.5f) * 16.0f;
                    if (world_.isWater(wx, wz)) {
                        if (waterX < 0) { waterX = x; waterZ = z; }
                    } else if (landX < 0) {
                        landX = x; landZ = z;
                    }
                }
            if (landX < 0 || waterX < 0)
                throw std::runtime_error("weapon impact effect fixture needs both land and water");

            // Exercise the actual firing-event consumer: an ordinary arrow
            // does not create an extra generic muzzle particle burst.
            const float ax=(float(landX)+0.5f)*16.f,az=(float(landZ)+0.5f)*16.f;
            const int archer=spawn("araarch",ax,az,0,localPlayer_);
            if(archer<0)throw std::runtime_error("arrow firing-effect control failed to spawn");
            captureFrame();beginFrame();endFrame();
            const auto beforeArrow=particles_.size();
            weaponAnimationQueue_.push(front().gameTick,archer,{},1,ax,az);
            cosmeticStep(0);
            if(particles_.size()!=beforeArrow)
                throw std::runtime_error("ordinary arrow created an unrequested muzzle flash");

            const auto testAt = [&](float x, float z, const std::string& expectedClass,
                                    const tak::sim::Weapon& weapon, bool struckUnit=false) {
                effects_.clear();
                const size_t particleCount = particles_.size();
                const uint32_t tick = front().gameTick;
                spawnWeaponImpact(weapon, x, z, 0.0f, {}, struckUnit);
                if (effects_.size() != 1 || particles_.size() != particleCount)
                    throw std::runtime_error("authored impact did not create exactly one effect instance");
                const auto effect = effects_.back();
                const auto variants = explosionClasses_.find(expectedClass);
                if (variants == explosionClasses_.end() || variants->second.empty() ||
                    std::none_of(variants->second.begin(), variants->second.end(),
                        [&](const std::string& name) { return effectFor(name) == effect.anim; }) ||
                    !effect.authoredTiming || effect.started != tick || effect.x != x || effect.z != z)
                    throw std::runtime_error("impact class did not resolve to its authored variant and location");
                uint32_t duration = 0;
                for (const uint16_t frameTicks : effect.anim->durations)
                    duration += std::max(1u, unsigned(frameTicks));
                if (!duration) throw std::runtime_error("authored impact animation has no duration");

                effects_.clear();
                auto atLastFrame = effect;
                atLastFrame.started = tick - (duration - 1);
                effects_.push_back(atLastFrame);
                updateEffects(0);
                if (effects_.size() != 1)
                    throw std::runtime_error("authored impact expired before its last frame");
                effects_.back().started = tick - duration;
                updateEffects(0);
                if (!effects_.empty())
                    throw std::runtime_error("authored impact survived its authored duration");
                return variants->second.size();
            };

            const float lx = (float(landX) + 0.5f) * 16.0f;
            const float lz = (float(landZ) + 0.5f) * 16.0f;
            const float wx = (float(waterX) + 0.5f) * 16.0f;
            const float wz = (float(waterZ) + 0.5f) * 16.0f;
            const size_t landVariants = testAt(lx, lz, authored.explosionClass, authored);
            const size_t waterVariants = testAt(wx, wz, authored.waterExplosionClass, authored);
            auto waterFallback = authored;
            waterFallback.waterExplosionClass.clear();
            const size_t directVariants = testAt(wx, wz, authored.explosionClass, authored, true);
            effects_.clear();
            spawnWeaponImpact(waterFallback,wx,wz);
            if(!effects_.empty()) throw std::runtime_error("missing water class substituted land art");
            const std::array<int32_t,3> airborne{int32_t(lx*65536),193*65536+123,int32_t(lz*65536)};
            spawnWeaponImpact(authored,lx,lz,0,airborne,true);
            if(effects_.size()!=1 || effects_.back().worldPosition!=airborne)
                throw std::runtime_error("airborne impact lost captured contact XYZ");

            auto missingClass = authored;
            missingClass.explosionClass = "__missing impact class__";
            missingClass.waterExplosionClass.clear();
            effects_.clear();
            const size_t particlesBeforeFallback = particles_.size();
            spawnWeaponImpact(missingClass, lx, lz, 0.0f);
            if (!effects_.empty() || particles_.size() != particlesBeforeFallback)
                throw std::runtime_error("missing impact art created an unauthored effect");
            effects_.clear();
            particles_.resize(particlesBeforeFallback);
            // Exercise the Harpy's status-shot presentation through the real
            // hit consumer. It used to draw a beam and discard its direct WAV.
            const auto* harpy = registry_.find("zonharp");
            if (!harpy || harpy->weapons.empty())
                throw std::runtime_error("Harpy presentation fixture has no weapon");
            const auto& spell = harpy->weapons.front();
            tak::sim::World::HitFx hit;
            hit.weapon = &spell;
            hit.fromX = cx - 120; hit.fromZ = cz;
            hit.x = cx + 120; hit.z = cz;
            hit.fromPosition=std::array<int32_t,3>{int32_t((cx-120)*65536),200*65536,int32_t(cz*65536)};
            hit.position=std::array<int32_t,3>{int32_t((cx+120)*65536),80*65536,int32_t(cz*65536)};
            hitQueue_.push_back(hit);
            cosmeticStep(0);
            if (beams_.empty() || beams_.back().fromPosition != hit.fromPosition ||
                beams_.back().position != hit.position || beams_.back().sprite != spell.weaponArt ||
                !effectFor(beams_.back().sprite) || !sounds_.has(spell.soundHit) ||
                sounds_.peakOf(spell.soundHit) <= 0)
                throw std::runtime_error("Harpy authored projectile/sound failed to load");
            auto cob = tak::cob::load(vfs_.read("scripts/zonharp.cob"));
            tak::cob::Vm vm(std::move(cob), true);
            vm.enableRetailAnimation();
            std::string firedSound;
            vm.onPlaySound = [&](int32_t index,int32_t) { firedSound = vm.file().name(uint32_t(index)); };
            vm.start("FireWeapon", {0});
            vm.tick(1.0f / 30.0f);
            std::transform(firedSound.begin(), firedSound.end(), firedSound.begin(), ::tolower);
            if (firedSound != "swoosh2" || !sounds_.has(firedSound) || sounds_.peakOf(firedSound) <= 0)
                throw std::runtime_error("Harpy firing script did not supply playable SWOOSH2");
            const auto* basilisk = registry_.find("zonbasil");
            if (!basilisk || basilisk->weapons.empty())
                throw std::runtime_error("Basilisk presentation fixture has no weapon");
            const int basiliskId = spawn("zonbasil", ax + 80, az, 0, localPlayer_);
            if (basiliskId < 0) throw std::runtime_error("Basilisk fixture failed to spawn");
            captureFrame(); beginFrame(); endFrame();
            std::printf("BASILISK_AUDIO_BEGIN\n");
            sounds_.setVerbose(true);
            weaponAnimationQueue_.push(front().gameTick, basiliskId, {}, 1, ax + 80, az);
            cosmeticStep(0);
            std::printf("BASILISK_AUDIO_IMPACT\n");
            hit.weapon = &basilisk->weapons.front();
            hitQueue_.push_back(hit);
            cosmeticStep(0);
            sounds_.setVerbose(false);
            std::printf("BASILISK_AUDIO_END\n");
            beams_.back().age = beams_.back().life * 0.5f;
            lookAt(cx, cz);
            std::fprintf(stderr, "PASS: Harpy FireballD sprite, direct ARROW08 impact, scripted SWOOSH2 fire\n");
            std::fprintf(stderr,
                "PASS: Arapult land/water authored impact variants, direct-hit class, exact tick/contact XYZ and authored expiry; missing-class silence (%zu/%zu/%zu variants)\n",
                landVariants, waterVariants, directVariants);
            return;
        }
        if(tak::devFlag("TAK_SCRIPT_263_TEST")) {
            // Kirenna's live MoveControl script emits detached SFX 263 when its
            // water movement state changes. Ulasem Arena has no water, so this
            // fixture is run on a shipped water map (for example Lake Lokken).
            const auto& map=mapView_.map();
            const auto* type=registry_.find("vermage");
            if(!type)throw std::runtime_error("script transient fixture has no vermage type");
            int waterX=-1,waterZ=-1;
            const int margin=std::max(type->footX,type->footZ)+2;
            for(int radius=0;radius<std::max(map.width,map.height) && waterX<0;radius++) {
                for(int z=std::max(margin,map.height/2-radius);z<std::min(map.height-margin,map.height/2+radius+1) && waterX<0;++z) {
                    for(int x=std::max(margin,map.width/2-radius);x<std::min(map.width-margin,map.width/2+radius+1);++x) {
                        if(std::max(std::abs(x-map.width/2),std::abs(z-map.height/2))!=radius)continue;
                        bool water=true;
                        for(int dz=-type->footZ/2;dz<type->footZ-type->footZ/2 && water;++dz)
                            for(int dx=-type->footX/2;dx<type->footX-type->footX/2;++dx) {
                                const int mx=x+dx,mz=z+dz;
                                if(map.heights[size_t(mz)*map.width+size_t(mx)]>=map.seaLevel) {water=false;break;}
                            }
                        if(water) {waterX=x;waterZ=z;break;}
                    }
                }
            }
            if(waterX<0)throw std::runtime_error("SFX 263 fixture needs a shipped map with an open-water spawn cell");
            const float spawnX=(float(waterX)+0.5f)*16.0f;
            const float spawnZ=(float(waterZ)+0.5f)*16.0f;
            const int ownerId=spawn("vermage",spawnX,spawnZ,0,localPlayer_);
            if(ownerId<0)throw std::runtime_error("SFX 263 fixture could not spawn vermage");
            const auto stepWithoutRender=[&] {
                simStep(1.0f/30.0f);
                beginFrame();endFrame();
            };
            std::optional<tak::sim::World::ScriptEmission> emission;
            for(int tick=0;tick<1200 && !emission;++tick) {
                stepWithoutRender();
                std::lock_guard<std::mutex> lock(hitQueueMutex_);
                for(const auto& queued:smokeTickQueue_) {
                    const auto found=std::find_if(queued.emissions.begin(),queued.emissions.end(),
                        [&](const auto& event) {return event.unitId==ownerId && event.code==263;});
                    if(found!=queued.emissions.end()) {emission=*found;break;}
                }
            }
            if(!emission)throw std::runtime_error("vermage live MoveControl emitted no SFX 263 within 1200 ticks on water");
            const auto* expectedArt=effectFor("deathmagic:purpledeath");
            if(!expectedArt || expectedArt->durations.empty())
                throw std::runtime_error("deathmagic:purpledeath authored animation is unavailable");
            uint32_t duration=0;
            for(const auto ticks:expectedArt->durations)duration+=std::max(1u,unsigned(ticks));
            for(int skipped=0;skipped<8 && front().gameTick-emission->tick<2;++skipped)
                stepWithoutRender();
            const uint32_t skippedTicks=front().gameTick-emission->tick;
            if(skippedTicks<2 || skippedTicks>=duration)
                throw std::runtime_error("SFX 263 fixture did not create a live skipped-render interval");
            auto* owner=world_.unit(ownerId);
            if(!owner)throw std::runtime_error("SFX 263 fixture owner vanished before retirement check");
            owner->deadFor=tak::sim::World::kRetiredTicks;
            captureFrame();beginFrame();endFrame();
            const auto* retired=frameUnitP(ownerId);
            if(!retired || retired->alive() || retired->deadFor*30.0f<tak::sim::World::kRetiredTicks)
                throw std::runtime_error("SFX 263 fixture failed to retire its source unit");
            const auto before=effects_.size();
            cosmeticStep(0);
            const auto matchesEmission=[&](const EffectInst& effect) {
                return effect.anim==expectedArt && effect.authoredTiming &&
                       effect.started==emission->tick && effect.worldPosition==emission->position;
            };
            const auto findEffect=[&]() {
                return std::find_if(effects_.begin()+std::ptrdiff_t(std::min(before,effects_.size())),
                    effects_.end(),matchesEmission);
            };
            if(findEffect()==effects_.end())
                throw std::runtime_error("delayed live SFX 263 lost its authored asset, XYZ, or callback tick after owner retirement");
            const auto setAge=[&](uint32_t age) {
                while(uint32_t(front().gameTick-emission->tick)<age)stepWithoutRender();
            };
            setAge(duration-1);updateEffects(0);
            if(std::none_of(effects_.begin(),effects_.end(),matchesEmission))
                throw std::runtime_error("detached deathmagic expired before its last authored tick");
            setAge(duration);updateEffects(0);
            if(std::any_of(effects_.begin(),effects_.end(),matchesEmission))
                throw std::runtime_error("detached deathmagic did not expire at its authored duration");
            std::fprintf(stderr,"PASS: live vermage SFX 263 survives two skipped render ticks and owner retirement with exact XYZ/tick, then expires at authored duration\n");
            return;
        }
        if(tak::devFlag("TAK_SCRIPT_TRANSIENT_TEST")) {
            // verpill's real Create script emits extended SFX 264 (pillaroflight).
            // Do not call cosmeticStep while collecting ticks: this deliberately
            // exercises the same queue used when rendering skips simulation ticks.
            const int ownerId=spawn("verpill",cx,cz,0,localPlayer_);
            if(ownerId<0)throw std::runtime_error("script transient fixture could not spawn verpill");
            const auto stepWithoutRender=[&] {
                simStep(1.0f/30.0f);
                beginFrame();endFrame();
            };
            std::optional<tak::sim::World::ScriptEmission> emission;
            for(int tick=0;tick<1200 && !emission;++tick) {
                stepWithoutRender();
                std::lock_guard<std::mutex> lock(hitQueueMutex_);
                for(const auto& queued:smokeTickQueue_) {
                    const auto found=std::find_if(queued.emissions.begin(),queued.emissions.end(),
                        [&](const auto& event) {return event.unitId==ownerId && event.code==264;});
                    if(found!=queued.emissions.end()) {emission=*found;break;}
                }
            }
            if(!emission)throw std::runtime_error("verpill live Create emitted no SFX 264 within 1200 ticks");
            const auto* expectedArt=effectFor("pillaroflight");
            if(!expectedArt || expectedArt->durations.empty())
                throw std::runtime_error("pillaroflight authored animation is unavailable");
            uint32_t duration=0;
            for(const auto ticks:expectedArt->durations)duration+=std::max(1u,unsigned(ticks));

            // Advance two more sim ticks without a render/cosmetic pass, then
            // retire the source unit before consuming its already-captured SFX.
            for(int skipped=0;skipped<8 && front().gameTick-emission->tick<2;++skipped)
                stepWithoutRender();
            const uint32_t skippedTicks=front().gameTick-emission->tick;
            if(skippedTicks<2 || skippedTicks>=duration) {
                std::fprintf(stderr,"script transient interval: event=%u front=%u duration=%u\n",
                    emission->tick,front().gameTick,duration);
                throw std::runtime_error("script transient fixture did not create a live skipped-render interval");
            }
            auto* owner=world_.unit(ownerId);
            if(!owner)throw std::runtime_error("script transient owner vanished before retirement check");
            owner->deadFor=tak::sim::World::kRetiredTicks;
            captureFrame();beginFrame();endFrame();
            const auto* retired=frameUnitP(ownerId);
            if(!retired || retired->alive() || retired->deadFor*30.0f<tak::sim::World::kRetiredTicks)
                throw std::runtime_error("script transient fixture failed to retire its source unit");

            const auto before=effects_.size();
            cosmeticStep(0);
            const auto matchesEmission=[&](const EffectInst& effect) {
                return effect.anim==expectedArt && effect.authoredTiming &&
                       effect.started==emission->tick && effect.worldPosition==emission->position;
            };
            const auto findEffect=[&]() {
                return std::find_if(effects_.begin()+std::ptrdiff_t(std::min(before,effects_.size())),
                    effects_.end(),matchesEmission);
            };
            auto liveEffect=findEffect();
            if(liveEffect==effects_.end())
                throw std::runtime_error("delayed live SFX 264 lost its authored asset, XYZ, or callback tick after owner retirement");
            const EffectInst captured=*liveEffect;

            const auto setAge=[&](uint32_t age) {
                while(uint32_t(front().gameTick-emission->tick)<age)stepWithoutRender();
            };
            setAge(duration-1);
            updateEffects(0);
            if(std::none_of(effects_.begin(),effects_.end(),matchesEmission))
                throw std::runtime_error("detached pillaroflight expired before its last authored tick");
            setAge(duration);
            updateEffects(0);
            if(std::any_of(effects_.begin(),effects_.end(),matchesEmission))
                throw std::runtime_error("detached pillaroflight did not expire at its authored duration");
            (void)captured;
            std::fprintf(stderr,"PASS: live verpill SFX 264 survives two skipped render ticks and owner retirement with exact XYZ/tick, then expires at authored duration\n");
            return;
        }
#ifndef NDEBUG
        if (tak::devFlag("TAK_TRANSPORT_EFFECT_TEST")) {
            tak::sim::World::TransportFx event;
            event.tick=front().gameTick-4;
            event.passenger={int32_t(cx*65536),101*65536+32768,int32_t(cz*65536)};
            event.carrier={int32_t((cx+80)*65536),181*65536+32768,int32_t(cz*65536)};
            const auto before=effects_.size();
            transportEffectQueue_.push_back(event);
            cosmeticStep(0);
            if (!transportEffectQueue_.empty() || effects_.size()!=before+2)
                throw std::runtime_error("transport event did not create two loaded effects");
            const auto passenger=effects_[before],carrier=effects_[before+1];
            if (passenger.worldPosition!=event.passenger || carrier.worldPosition!=event.carrier ||
                passenger.started!=event.tick || carrier.started!=event.tick ||
                !passenger.authoredTiming || !carrier.authoredTiming)
                throw std::runtime_error("transport effects lost captured position or event tick");
            cosmeticStep(0);
            if (effects_.size()!=before+2)
                throw std::runtime_error("transport event was consumed more than once");
            effects_.clear();
            for (auto sample:{passenger,carrier}) {
                uint32_t duration=0;
                for (auto delay:sample.anim->durations) duration+=std::max(1u,unsigned(delay));
                for (uint32_t age:{0u,duration-1,duration,duration+20}) {
                    sample.started=front().gameTick-age;
                    effects_.push_back(sample);updateEffects(0);
                    if (effects_.empty()!=(age>=duration))
                        throw std::runtime_error("transport effect expiry differs from authored duration");
                    effects_.clear();
                }
            }
            effects_.push_back(passenger);effects_.push_back(carrier);
            // Exercise the actual draw paths across a passenger's visibility
            // transition. This is separate from detached transport-beam effects.
            const int passengerId=spawn("araarch",cx,cz,0,localPlayer_);
            const int carrierId=spawn("zonroc",cx+80,cz,0,localPlayer_);
            const auto* art=effectFor("bigsmoke");
            if(passengerId<0 || carrierId<0 || !art || art->frames.empty())
                throw std::runtime_error("transport attached-effect assets unavailable");
            smokeSprites_.clear();pointParticles_.clear();damageFlames_.clear();nimbusEffects_.clear();
            const std::array<int32_t,3> point{int32_t(cx*65536),0,int32_t(cz*65536)};
            tak::RetailSmokeParticle smoke;smoke.position=point;
            smokeSprites_[passengerId].push_back({smoke,art});
            tak::RetailPointParticle wake;wake.position=point;
            pointParticles_[passengerId].push_back(wake);
            DamageFlameSprite flame;flame.position=point;flame.art=art;flame.clock.start(art->durations);
            damageFlames_[passengerId].push_back(flame);
            nimbusEffects_[passengerId]={art,world_.tickCount()};
            int width=0,height=0;SDL_GetRendererOutputSize(ren_,&width,&height);
            winW_=width;winH_=height; // draw normally sets these before culling
            mapView_.setZoom(1);
            mapView_.setOffset(cx-float(mapViewW(width))*0.5f,cz-float(height-barH())*0.5f);
            for(bool aboard:{false,true,false}) {
                world_.unit(passengerId)->inTransport=aboard ? carrierId : 0;
                captureFrame();beginFrame();endFrame();
                drawUnitFx();drawEffects();
                const int expected=aboard ? 0 : 1;
                if(debugSmokeDrawCount_!=expected || debugPointDrawCount_!=expected ||
                   debugDamageFlameDrawCount_!=expected || debugNimbusDrawCount_!=expected) {
                    std::fprintf(stderr,"cargo effects: aboard=%d smoke=%d points=%d flames=%d nimbus=%d\n",
                        int(aboard),debugSmokeDrawCount_,debugPointDrawCount_,debugDamageFlameDrawCount_,debugNimbusDrawCount_);
                    throw std::runtime_error("passenger attached effects did not hide aboard and resume after unloading");
                }
            }
            if(smokeSprites_[passengerId].size()!=1 || pointParticles_[passengerId].size()!=1 ||
               damageFlames_[passengerId].size()!=1 || !nimbusEffects_.contains(passengerId))
                throw std::runtime_error("boarding discarded attached effect lifetime state");
            // A passenger destroyed aboard must not spawn visible death
            // sprites/bursts at the carrier's position or run its death scripts.
            world_.unit(passengerId)->inTransport=carrierId;
            world_.unit(passengerId)->deadFor=0;
            captureFrame();beginFrame();endFrame();
            const auto effectsBefore=effects_.size(),particlesBefore=particles_.size();
            cosmeticStep(0);
            if(effects_.size()!=effectsBefore || particles_.size()!=particlesBefore)
                throw std::runtime_error("hidden destroyed cargo spawned ordinary death effects");
            // Control: an ordinary unembarked archer also gets no fabricated
            // body blast before its authored Killed/Dying scripts are advanced.
            const int ordinary=spawn("araarch",cx+40,cz,0,localPlayer_);
            if(ordinary<0)throw std::runtime_error("ordinary death control failed to spawn");
            world_.unit(ordinary)->deadFor=0;
            world_.unit(ordinary)->deathType=1;
            captureFrame();beginFrame();endFrame();
            const auto ordinaryEffects=effects_.size(),ordinaryParticles=particles_.size();
            cosmeticStep(0);
            if(effects_.size()!=ordinaryEffects || particles_.size()!=ordinaryParticles)
                throw std::runtime_error("ordinary death fabricated effects outside its authored script");
            // A statue must retain its death-edge pose, including any piece
            // turn/move already in progress when script threads are stopped.
            for(int deathType:{14,15}) {
                const int statue=spawn("zonhunt",cx+60,cz,0,localPlayer_);
                if(statue<0)throw std::runtime_error("statue animation control failed to spawn");
                auto& animation=anims_.at(statue);
                animation.vm->start("setSFXoccupy",{5});
                animation.vm->start("BeginFlight");
                animation.vm->start("StartBuilding");
                bool moving=false;
                for(int tick=0;tick<120 && !moving;++tick) {
                    animation.vm->tick(1.f/30);
                    for(const auto& piece:animation.vm->pieces())
                        for(int axis=0;axis<3;++axis)
                            moving|=piece.moving[axis] || piece.turning[axis] || piece.spin[axis]!=0;
                }
                if(!moving)throw std::runtime_error("statue control never began piece motion");
                world_.unit(statue)->deadFor=0;
                world_.unit(statue)->deathType=deathType;
                captureFrame();beginFrame();endFrame();cosmeticStep(0);
                const auto frozenPose=animation.vm->pieces();
                for(int tick=0;tick<30;++tick)animFrame(1.f/30);
                const auto& afterPose=animation.vm->pieces();
                for(size_t piece=0;piece<frozenPose.size();++piece)
                    for(int axis=0;axis<3;++axis)
                        if(frozenPose[piece].move[axis]!=afterPose[piece].move[axis] ||
                           frozenPose[piece].rot[axis]!=afterPose[piece].rot[axis])
                            throw std::runtime_error("statue continued piece animation after death");
            }
            std::fprintf(stderr,"PASS: transport effects, cargo hide/release, authored deaths, and frozen statue poses\n");
            return;
        }
#endif
        if(tak::devEnv("TAK_FEATURE_CACHE_TEST")) {
            auto definition = featureDefs_.at("vertree01");
            definition.values["animating"] = "0";
            definition.values["animatable"] = "0";
            const auto* standing = featureArtFor(definition);
            definition.values["seqnameburn"] = definition.valueOr("seqname", "");
            definition.values["seqnameburnshad"] = definition.valueOr("seqnameshad", "");
            const auto* burning = featureArtFor(definition, "seqnameburn", "seqnameburnshad");
            definition.values["animating"] = "1";
            const auto* animated = featureArtFor(definition);
            definition.values["seqnameshad"] = "";
            const auto* unshadowed = featureArtFor(definition);
            if (!standing || !burning || !animated || !unshadowed ||
                standing == burning || standing == animated || animated == burning ||
                animated == unshadowed || burning->shadowLoop ||
                !unshadowed->shadowFrames.empty() || featureArtFor(definition) != unshadowed)
                throw std::runtime_error("feature art cache aliases distinct playback/shadow definitions");
            std::fprintf(stderr, "PASS: feature cache separates static, animated, burn and shadow variants; repeated lookup reuses art\n");
            return;
        }
        if(tak::devEnv("TAK_FEATURE_SMOKE_QUEUE_TEST")) {
            const auto definition=featureDefs_.find("vertree01");
            const auto* art=effectFor("bigsmoke");
            if(definition==featureDefs_.end() || !art)throw std::runtime_error("feature smoke fixture art unavailable");
            smokeTickQueue_.clear();smokeSprites_.clear();featureSmokeSprites_.clear();
            smokeTick_=front().gameTick-4;
            tak::RetailSmokeParticle unitSmoke;unitSmoke.frameLimit=100;
            smokeSprites_[77].push_back({unitSmoke,art});
            SmokeTick first;first.tick=front().gameTick-3;
            for(int i=0;i<12;++i)first.features.push_back({77,"vertree01",{0,0,0},0});
            first.features.push_back({78,"vertree01",{0,0,0},0});
            smokeTickQueue_.push_back(first);consumeSmokeTicks();
            if(featureSmokeSprites_[77].size()!=10 || featureSmokeSprites_[78].size()!=1)
                throw std::runtime_error("feature smoke capacity or ownership mismatch");
            const auto before=featureSmokeSprites_[78][0].particle.position;
            const auto countdown=featureSmokeSprites_[78][0].particle.countdown;
            SmokeTick second;second.tick=front().gameTick-2;second.windX=123;second.windZ=-45;
            // Active burns send a tick event even when they emit no new smoke.
            // Finished burns stop advancing their feature-owned particles.
            second.features.push_back({78,"vertree01",{0,0,0},0});
            second.features.back().emit=false;
            SmokeTick third=second;third.tick=front().gameTick-1;third.removedFeatures={77};
            smokeTickQueue_.push_back(second);smokeTickQueue_.push_back(third);consumeSmokeTicks();
            const auto after=featureSmokeSprites_[78][0].particle;
            if(featureSmokeSprites_.contains(77) || smokeSprites_[77].size()!=1 ||
               after.countdown!=countdown-2 ||
               uint32_t(after.position[0])!=uint32_t(before[0])+uint32_t(123*16) ||
               uint32_t(after.position[2])!=uint32_t(before[2])+uint32_t(-45*16))
                throw std::runtime_error("feature smoke delayed tick/retirement isolation failed");
            consumeSmokeTicks();
            if(featureSmokeSprites_[78][0].particle.position!=after.position)
                throw std::runtime_error("feature smoke queue was consumed twice");
            featureSmokeSprites_.clear();smokeSprites_.clear();
            std::fprintf(stderr,"PASS: feature smoke cap10, delayed tick motion, single consumption and unit/feature retirement isolation\n");
            return;
        }
        if(tak::devEnv("TAK_DEBRIS_TEST") || tak::devEnv("TAK_BLOOD_TEST")) {
            edgeScrollOn_=false;follow_=false;initialCamera_.reset();
            Visual visual;auto& root=visual.model.root;root.name="piece";
            root.vertices={-30,0,0,30,0,0,0,50,0};
            root.primitives={{200,"",{0,1,2}},{200,"",{2,1,0}}};
            visuals_["__debris_fixture"]=std::move(visual);
            unitType_[-123]="__debris_fixture";
            UnitR unit;unit.id=-123;unit.x=cx;unit.z=cz;
            unit.worldPosition={int32_t(cx*65536),int32_t((rawHeight(cx,cz)+100)*65536),int32_t(cz*65536)};
            const std::vector<std::string> names={"piece"};
            std::vector<tak::cob::PieceState> poses(1);
            Anim snapshot;snapshot.pieceNames=&names;snapshot.capturedPose=poses;
            tak::cob::File bloodScript;bloodScript.scripts={{"QueryBlood",0}};
            bloodScript.code={0x10021001,0,0x10023002,0,0x10021001,0,0x10065000};
            tak::cob::Vm bloodVm(std::move(bloodScript),true);bloodVm.enableRetailAnimation();
            tak::sim::UnitType bloodType;unit.type=&bloodType;snapshot.effectQueryVm=&bloodVm;
            if(!explodePiece(unit,snapshot,0,0))
                throw std::runtime_error("detached model admission rejected an empty pool");
            if(detachedPieces_.size()!=1 || detachedPieces_[0].model.vertices.size()!=9 ||
               !particles_.empty())throw std::runtime_error("detached model creation failed");
            if(detachedPieces_[0].blood.size()!=100)
                throw std::runtime_error("QueryBlood did not attach 100 particles");
            (void)heightAbove(cx,cz);
            const bool savedShadowFrame=shadowsOnFrame_;
            shadowsOnFrame_=true;
            buildDetachedShadow(detachedPieces_[0],2.0f);
            if(detachedShadowVerts_.size()<3 || detachedShadowVerts_.size()%3)
                throw std::runtime_error("detached model did not produce retail-projected shadow triangles");
            shadowsOnFrame_=false;
            buildDetachedShadow(detachedPieces_[0],2.0f);
            if(!detachedShadowVerts_.empty() || !detachedMaskedShadows_.empty())
                throw std::runtime_error("detached model ignored the unit-shadow toggle");
            shadowsOnFrame_=savedShadowFrame;
            while(detachedPieces_.size()<100)detachedPieces_.push_back(detachedPieces_.front());
            if(explodePiece(unit,snapshot,0,0) || detachedPieces_.size()!=100)
                throw std::runtime_error("detached model admission exceeded the retail pool");
            detachedPieces_.resize(1);
            visuals_.erase("__debris_fixture");unitType_.erase(-123);
            if(detachedPieces_[0].model.vertices[0]!=-30)
                throw std::runtime_error("detached geometry depended on source lifetime");
            if(tak::devEnv("TAK_BLOOD_TEST")) {
                mapView_.setZoom(2);
                int width,height;SDL_GetRendererOutputSize(ren_,&width,&height);
                mapView_.setOffset(cx-float(mapViewW(width))/4,cz-float(height-barH())/4);
                std::fprintf(stderr,"PASS: live blood fixture created 100 script-enabled particles\n");
                return;
            }
            auto impact=detachedPieces_[0];
            impact.tick=front().gameTick-1;impact.motion.flags=16;
            impact.motion.velocity[1]=-300*65536;
            const auto position=impact.motion.position;
            detachedPieces_.push_back(std::move(impact));
            const auto glows=explosionGlows_.size();
            detachedPieces_[0].tick=front().gameTick-1;
            for(auto& particle:detachedPieces_[0].blood)particle.velocity[1]=-150*65536;
            updateEffects(0);
            // First contact below sea leaves no stain; use a separate above-sea
            // contact case for persistent marks in the particle oracle.
            if(!detachedPieces_[0].blood.empty())
                throw std::runtime_error("blood particles failed ground removal");
            if(detachedPieces_.size()!=1 || explosionGlows_.size()!=glows+1 ||
               explosionGlows_.back().worldPosition!=position ||
               explosionGlows_.back().started!=front().gameTick ||
               explosionGlows_.back().kind!=0)
                throw std::runtime_error("debris ground impact lost its positioned glow");
            const bool savedLava=debrisLavaWorld_,savedNoSea=debrisNoSeaTrigger_;
            for(bool lava:{false,true})for(bool suppressed:{false,true}) {
                debrisLavaWorld_=lava;debrisNoSeaTrigger_=suppressed;
                auto splash=detachedPieces_[0];splash.blood.clear();
                splash.tick=front().gameTick-1;splash.motion.flags=16;
                splash.motion.position[1]=int32_t(uint8_t(mapView_.map().seaLevel))*65536;
                const auto splashPosition=splash.motion.position;
                detachedPieces_.push_back(std::move(splash));
                const auto before=effects_.size(),beforeGlow=explosionGlows_.size();
                updateEffects(0);
                if(detachedPieces_.size()!=1 || effects_.size()!=before+size_t(!suppressed) ||
                   explosionGlows_.size()!=beforeGlow)
                    throw std::runtime_error("debris splash suppression/glow mismatch");
                if(!suppressed) {
                    bool matched=false;
                    for(const auto& variant:explosionClasses_.at(explosionClassOrder_.at(lava?10:9)))
                        matched|=effects_.back().anim==effectFor(variant);
                    if(!matched || effects_.back().worldPosition!=splashPosition ||
                       effects_.back().started!=front().gameTick)
                        throw std::runtime_error("debris splash class/position/tick mismatch");
                }
            }
            debrisLavaWorld_=savedLava;debrisNoSeaTrigger_=savedNoSea;
            mapView_.setZoom(2);
            int width,height;SDL_GetRendererOutputSize(ren_,&width,&height);
            mapView_.setOffset(cx-float(mapViewW(width))/4,cz-float(height-barH())/4);
            std::fprintf(stderr,"PASS: production EXPLODE retains independent model geometry without generic particles\n");
            return;
        }
        if (tak::devEnv("TAK_PIECE_VISIBILITY_TEST")) {
            {
                tak::cob::File script;script.pieces={"root","arm","hand"};
                script.scripts={{"Test",0}};
                script.code={0x10021001,64,0x10071000,1,0x10021001,0,0x10065000};
                Anim test;test.vm=std::make_unique<tak::cob::Vm>(std::move(script));
                test.vm->enableRetailAnimation();
                tak::tdo::Object model;model.name="ROOT";model.children.resize(1);
                model.children[0].name="arm";model.children[0].children.resize(1);
                model.children[0].children[0].name="hand";
                configureExplosionHierarchy(test,model);
                // The replacement model moves the hand out of the arm branch.
                model.children.push_back(model.children[0].children[0]);
                model.children[0].children.clear();
                configureExplosionHierarchy(test,model);
                test.vm->start("Test");test.vm->tick(1.0f/30);
                if(test.vm->pieces()[1].visible || !test.vm->pieces()[2].visible)
                    throw std::runtime_error("replacement model retained stale detachment hierarchy");
            }
            // Exercise the production collector with a hidden, transformed parent.
            tak::tdo::Object root;root.name="parent";root.x=7;root.y=3;
            root.vertices={0,0,0, 12,0,0, 0,12,4};
            root.primitives={{0,"",{0,1,2}},{0,"",{2,1,0}}};
            root.children.push_back(root);
            root.children[0].name="child";root.children[0].x=19;
            const std::vector<std::string> names={"parent","child"};
            std::vector<tak::cob::PieceState> poses(2);
            poses[0].move[0]=11;poses[0].rot[1]=0.6f;
            Anim snapshot;snapshot.pieceNames=&names;snapshot.capturedPose=poses;
            for(bool shadow:{false,true}) {
                std::vector<Tri> baseline,hidden,all;
                collect(all,nullptr,root,Xform{},&snapshot,0.3f,0,false,false,shadow);
                auto reference=root;reference.primitives.clear();
                collect(baseline,nullptr,reference,Xform{},&snapshot,0.3f,0,false,false,shadow);
                poses[0].visible=false;
                collect(hidden,nullptr,root,Xform{},&snapshot,0.3f,0,false,false,shadow);
                if(baseline.empty() || all.size()<=baseline.size() || hidden.size()!=baseline.size())
                    throw std::runtime_error("hidden-parent collector lost visible child geometry");
                for(size_t i=0;i<hidden.size();++i)for(int v=0;v<3;++v)
                    if(hidden[i].v[v].position.x!=baseline[i].v[v].position.x ||
                       hidden[i].v[v].position.y!=baseline[i].v[v].position.y)
                        throw std::runtime_error("hidden-parent collector lost inherited transform");
                poses[1].visible=false;hidden.clear();
                collect(hidden,nullptr,root,Xform{},&snapshot,0.3f,0,false,false,shadow);
                if(!hidden.empty())throw std::runtime_error("hidden child still draws geometry");
                poses[0].visible=poses[1].visible=true;
            }
            // RENDER_OFF and DONT_CACHE must keep the body while removing only
            // this piece's shadow. Compare both collectors with all four flags.
            poses[1].visible=false;
            PieceMeta flagMeta;buildPieceMeta(root,flagMeta,false);
            for(unsigned flags=0;flags<16;++flags) {
                poses[0].visible=flags&1;poses[0].cached=flags&2;
                poses[0].shaded=flags&4;poses[0].rendered=flags&8;
                PreparedPiece prepared;
                preparePiece(root,flagMeta,Xform{},&snapshot,std::cos(0.3f),std::sin(0.3f),prepared);
                for(bool shadow:{false,true}) {
                    std::vector<Tri> reference,fast;
                    collect(reference,nullptr,root,Xform{},&snapshot,0.3f,0,false,false,shadow,
                            nullptr,&flagMeta);
                    collect(fast,nullptr,root,Xform{},&snapshot,0.3f,0,false,false,shadow,
                            nullptr,&flagMeta,false,&prepared);
                    const bool expected=shadow ? (flags&11)==11 : bool(flags&1);
                    if(reference.empty()==expected || fast.size()!=reference.size())
                        throw std::runtime_error("piece render/cache flags changed wrong body/shadow pass");
                    for(size_t i=0;i<fast.size();++i)for(int v=0;v<3;++v)
                        if(fast[i].v[v].position.x!=reference[i].v[v].position.x ||
                           fast[i].v[v].position.y!=reference[i].v[v].position.y)
                            throw std::runtime_error("piece flags prepared/reference transform mismatch");
                }
            }
            std::fprintf(stderr,"PASS: all16 piece flags preserve native body/shadow admission in both collectors\n");
            {
                const auto* model=loadVisual("arakeep");
                if(!model)throw std::runtime_error("missing palette lighting fixture model");
                const auto& meta=visuals_.at("arakeep").meta;
                preparePaletteTextures(meta);
                std::vector<std::string> pieceNames;
                const auto gather=[&](const auto& self,const tak::tdo::Object& object)->void {
                    pieceNames.push_back(object.name);
                    std::transform(pieceNames.back().begin(),pieceNames.back().end(),
                                   pieceNames.back().begin(),::tolower);
                    for(const auto& child:object.children)self(self,child);
                };
                gather(gather,model->root);
                std::vector<tak::cob::PieceState> state(pieceNames.size());
                Anim pose;pose.pieceNames=&pieceNames;pose.capturedPose=state;
                std::vector<Tri> shaded,neutral,preparedGeometry;
                auto* atlas=atlasFor(0);
                collect(shaded,atlas,model->root,Xform{},&pose,0.4f,0,false,true,
                        false,nullptr,&meta,false,nullptr,true);
                PreparedPiece prepared;
                preparePiece(model->root,meta,Xform{},&pose,std::cos(0.4f),std::sin(0.4f),prepared);
                collect(preparedGeometry,atlas,model->root,Xform{},&pose,0.4f,0,false,true,
                        false,nullptr,&meta,false,&prepared,true);
                if(shaded.size()!=preparedGeometry.size())
                    throw std::runtime_error("palette prepared geometry count mismatch");
                for(auto& piece:state)piece.shaded=false;
                collect(neutral,atlas,model->root,Xform{},&pose,0.4f,0,false,true,
                        false,nullptr,&meta,false,nullptr,true);
                if(shaded.empty() || neutral.size()!=shaded.size())
                    throw std::runtime_error("palette lighting fixture geometry missing");
                bool usesPalette=false,changed=false;
                for(size_t i=0;i<shaded.size();++i) {
                    for(const auto& page:paletteTexturePages_)
                        usesPalette|=shaded[i].tex==page.texture;
                    if(shaded[i].tex!=preparedGeometry[i].tex)
                        throw std::runtime_error("palette prepared texture mismatch");
                    for(int v=0;v<3;++v) {
                        if(std::memcmp(&shaded[i].v[v],&preparedGeometry[i].v[v],sizeof(SDL_Vertex)))
                            throw std::runtime_error("palette prepared vertex mismatch");
                        changed|=shaded[i].v[v].tex_coord.x!=neutral[i].v[v].tex_coord.x ||
                                 shaded[i].v[v].tex_coord.y!=neutral[i].v[v].tex_coord.y;
                    }
                }
                if(!usesPalette || !changed)
                    throw std::runtime_error("authored SHADE/DONT_SHADE did not select palette bands");
                std::fprintf(stderr,"PASS: Aramon Keep palette lighting selects authored bands and matches prepared geometry\n");
            }
            std::fprintf(stderr,"PASS: production body/shadow collector preserves visible children and hidden-parent transforms\n");
            return;
        }
        if (tak::devEnv("TAK_EXPLOSION_CLASSES_TEST")) {
            loadExplosionClasses();
            if(explosionClassOrder_.size()<9)throw std::runtime_error("missing numeric explosion classes");
            UnitR unit{};unit.x=cx;unit.z=cz;
            Anim snapshot;
            const auto particleCount=particles_.size(),effectCount=effects_.size();
            for(int flags:{0x28,0x30,0x38})explodePiece(unit,snapshot,-1,flags);
            if(particles_.size()!=particleCount || effects_.size()!=effectCount)
                throw std::runtime_error("bitmap-only debris flags spawned immediate effects");
            for(unsigned cls=0;cls<9;++cls) {
                const auto before=effects_.size();
                explodePiece(unit,snapshot,-1,int32_t(0x20u|(0x100u<<cls)));
                if(effects_.size()!=before+1)throw std::runtime_error("numeric explosion class failed to spawn");
                if(!effects_.back().authoredTiming || effects_.back().started!=front().gameTick)
                    throw std::runtime_error("explosion class did not start its tick clock");
            }
            const auto before=effects_.size();
            explodePiece(unit,snapshot,-1,0x1ff20);
            if(effects_.size()!=before+9)throw std::runtime_error("combined explosion flags lost effects");
            const auto samples=effects_;
            effects_.clear();
            for (auto sample:samples) {
                uint32_t duration=0;
                for (const auto delay:sample.anim->durations) duration+=std::max(1u,unsigned(delay));
                if (!duration) throw std::runtime_error("authored effect has no frame duration");
                for (const uint32_t age:{0u,duration-1,duration}) {
                    sample.started=front().gameTick-age;
                    effects_.push_back(sample);
                    updateEffects(0);
                    if (effects_.empty()!=(age==duration))
                        throw std::runtime_error("authored effect expired on the wrong tick");
                    effects_.clear();
                }
            }
            effects_=samples;
            std::fprintf(stderr,"PASS: live authored effect expiry retains the last frame and removes at exact duration\n");
            std::fprintf(stderr,"PASS: all nine authored explosion classes and combined flags spawn independently\n");
            return;
        }
        if (tak::devEnv("TAK_GLOW_TEST")) {
            edgeScrollOn_=false;follow_=false;initialCamera_.reset();
            for(unsigned kind=0;kind<3;++kind)
                explosionGlows_.push_back({cx+(float(kind)-1)*100,cz,0,front().gameTick,kind});
            int width=0,height=0;SDL_GetRendererOutputSize(ren_,&width,&height);
            mapView_.setZoom(1.5f);
            mapView_.setOffset(cx-float(mapViewW(width))*0.5f/mapView_.zoom(),
                cz-float(height-barH())*0.5f/mapView_.zoom());
            return;
        }
        if (tak::devEnv("TAK_POINT_TEST")) {
            edgeScrollOn_=false;follow_=false;initialCamera_.reset();
            const auto& map=mapView_.map();
            bool found=false;
            for(int z=16;z<map.height-16 && !found;++z)
                for(int x=16;x<map.width-32 && !found;++x) {
                    bool water=true;
                    for(int dz=-4;dz<=4 && water;++dz)for(int dx=-4;dx<=20;++dx)
                        if(map.heights[size_t(z+dz)*map.width+size_t(x+dx)]>=map.seaLevel-8) {water=false;break;}
                    if(water) {cx=float(x*16);cz=float(z*16);found=true;}
                }
            if(!found)throw std::runtime_error("point fixture requires a stretch of deep water");
            const int owner=spawn("vertrans",cx,cz,0,0);
            world_.order(owner,cx+240,cz,false);
            int width=0,height=0;SDL_GetRendererOutputSize(ren_,&width,&height);
            mapView_.setZoom(2.f);
            mapView_.setOffset(cx+80-float(mapViewW(width))*0.5f/mapView_.zoom(),
                cz-float(height-barH())*0.5f/mapView_.zoom());
            return;
        }
        if (tak::devEnv("TAK_SMOKE_TEST")) {
            edgeScrollOn_=false;follow_=false;initialCamera_.reset();
            const int owner=spawn("arakeep",cx,cz,0,0);
            if(auto* unit=world_.unit(owner))unit->hp=tak::sim::Fixed::fromInt(unit->maximumHp()/5);
            int width=0,height=0;SDL_GetRendererOutputSize(ren_,&width,&height);
            mapView_.setZoom(1.5f);
            mapView_.setOffset(cx-float(mapViewW(width))*0.5f/mapView_.zoom(),
                cz-float(height-barH())*0.5f/mapView_.zoom());
            return;
        }
        if (const char* type=tak::devEnv("TAK_PROJECTILE_TEST")) {
            // Screenshot fixtures must not drift with the dummy driver's
            // pointer at (0,0), and must fit the actual map viewport.
            edgeScrollOn_=false;follow_=false;initialCamera_.reset();
            const int shooter=spawn(type,cx-200,cz,1.57f,0);
            const int target=spawn("tarzom",cx+200,cz,-1.57f,1);
            if (auto* u=world_.unit(target)) {
                u->hp=tak::sim::Fixed::fromInt(25000);world_.setStance(target,2);
            }
            if (shooter>=0 && target>=0) world_.attack(shooter,target,false);
            int width=0,height=0;SDL_GetRendererOutputSize(ren_,&width,&height);
            const float viewport=float(mapViewW(width));
            mapView_.setZoom(std::min(1.5f,std::max(0.25f,(viewport-100.f)/400.f)));
            mapView_.setOffset(cx-viewport*0.5f/mapView_.zoom(),
                cz-float(height-barH())*0.5f/mapView_.zoom());
            return;
        }
        int a = spawn("araarch", cx - 40, cz, 1.57f, 0);
        int e = spawn("tararch", cx + 200, cz, -1.57f, 1);
        world_.attack(a, e, false);
        // Aim-pipeline check: a Veruna watch tower (5-TURN AimWeapon) with an
        // enemy off-axis to its north-east -- the turret must visibly swing to
        // face it (auto-acquire), proving the AimWeapon heading sign.
        int tw = spawn("vertower", cx - 40, cz + 220, 0.0f, 0);
        int zm = spawn("tarzom", cx + 160, cz + 100, -1.57f, 1);
        world_.attack(tw, zm, false);   // explicit order: aim runs even without LOS
        // Feature-burning check: park a victim right beside a flamable feature
        // and have a dragon breathe on it -- the splash must ignite the tree
        // (spread + burnt swap then follow on their own).
        float tx = cx + 60, tz = cz - 60;
        bool burnTarget=false;
        std::string burnType;
        const bool flameProbe=tak::devFlag("TAK_FEATURE_FLAME_TEST");
        float closest=std::numeric_limits<float>::max();
        for (const auto& ft : world_.features())
            if (ft.alive && ft.type >= 0 &&
                world_.featureTypes()[size_t(ft.type)].flamable) {
                if(flameProbe && !world_.featureTypes()[size_t(ft.type)].hasBurnAnim)continue;
                const auto& name=world_.featureTypes()[size_t(ft.type)].name;
                if(flameProbe) {
                    const auto definition=featureDefs_.find(name);
                    if(definition==featureDefs_.end() ||
                       (definition->second.valueOr("seqnamefrontflame","").empty() &&
                        definition->second.valueOr("seqnamebackflame","").empty()))continue;
                }
                float ddx = ft.x.toFloat() - cx, ddz = ft.z.toFloat() - cz;
                const float distance=ddx*ddx+ddz*ddz;
                if (distance < (flameProbe ? closest : 400 * 400)) {
                    tx = ft.x.toFloat() + 20; tz = ft.z.toFloat();burnTarget=true;closest=distance;
                    burnType=name;
                    if(!flameProbe)break;
                }
            }
        if(flameProbe) {
            if(!burnTarget)throw std::runtime_error("feature flame probe requires a map feature with authored burn animation");
#ifndef NDEBUG
            const auto& definition=featureDefs_.at(burnType);
            debugFeatureFlameExpected_=int(!definition.valueOr("seqnamefrontflame","").empty())+
                int(!definition.valueOr("seqnamebackflame","").empty());
#endif
            std::fprintf(stderr,"feature flame probe: target near %.0f,%.0f\n",tx,tz);
        }
        int dr = spawn("tardrag", tx - 260, tz - 40, 1.57f, 1);
        int ar = spawn("araarch", tx, tz, -1.57f, 0);
        world_.attack(dr, ar, false);
        // Statue-death check: the Basilisk's gaze petrifies -- the victim must
        // die on the spot and stand as a stone-gray, resurrectable statue.
        int bs = spawn("zonbasil", tx - 120, tz + 90, 1.57f, 1);
        int vic = spawn("arabow", tx + 40, tz + 90, -1.57f, 0);
        world_.attack(bs, vic, false);
        // Ordered corpse-reclaim check: a builder is sent (negative target id)
        // to consume a fresh corpse -- the body must vanish when it arrives.
        int rcv = spawn("arasword", cx - 200, cz + 120, 0.0f, 0);
        if (auto* rd = world_.unit(rcv)) rd->hp = tak::sim::Fixed();   // dies this tick, normal corpse
        int rcb = spawn("arabuild", cx - 250, cz + 160, 1.57f, 0);
        world_.reclaim(rcb, -rcv, false);
        // Animate check: an idle necromancer beside the (soon) archer corpse
        // must channel and raise a Ghoul from it. Hold-fire stance so it never
        // auto-acquires (the channel needs it order-free).
        int nec = spawn("tarpries", tx + 30, tz - 40, -1.57f, 1);
        if (auto* np = world_.unit(nec)) np->stance = 2;
        // Watch the burn, not the tower: centre the camera on the target tree.
        mapView_.setOffset(tx - 640 / mapView_.zoom(), tz - 400 / mapView_.zoom());
        if (tak::devEnv("TAK_FEATURE_FLAME_TEST")) {
            edgeScrollOn_=false;follow_=false;initialCamera_.reset();
            int width=0,height=0;SDL_GetRendererOutputSize(ren_,&width,&height);
            mapView_.setZoom(1.5f);
            mapView_.setOffset(tx-float(mapViewW(width))*0.5f/mapView_.zoom(),
                tz-float(height-barH())*0.5f/mapView_.zoom());
        }
    }

    void GameView::lodeTest() {
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        spawn(lodeUnit.empty()?"zonlode":lodeUnit, cx, cz, 3.14159f, 0);
        mapView_.setOffset(cx - 640 / mapView_.zoom(), cz - 400 / mapView_.zoom());
    }

    void GameView::guardTest() {
        // Squad guards the first unit; the first unit marches east alone.
        int leader = -1;
        for (auto& u : world_.units())
            if (u.alive() && u.player == 0 && u.type && u.type->canMove) {
                if (leader < 0) leader = u.id;
                else world_.guard(u.id, leader, false);
            }
        if (leader >= 0) world_.order(leader, 1500, 950, false);
    }

    void GameView::marchTo(float dx, float dz) {
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        for (auto& u : world_.units())
            if (u.player == localPlayer_ && u.type && u.type->canMove) {
                tak::net::Command c;
                c.kind = tak::net::Cmd::AttackMove;
                c.unitId = u.id;
                c.x = cx + dx;
                c.z = cz + dz;
                issue(c);
            }
    }

    void GameView::setPlayerColor(int player, int slot) {
        if (player >= 0 && player < 8 && slot >= 0) colorSlot_[player] = slot;
    }

    void GameView::simStep(float dt) {
        int64_t _sim0 = int64_t(SDL_GetPerformanceCounter());
        // Advance the sim in sub-steps capped at 1/30s so fast speeds (or a laggy
        // frame) can't move a unit far enough to tunnel a wall; effects/AI below use
        // the full scaled dt (they only interpolate, so a big step is harmless).
        {
#ifndef NDEBUG
        PaceSpan worldSpan(paceWorldNs_);
#endif
        for (float rem = dt, guard = 0; rem > 1e-5f && guard < 16; ++guard) {
            float step = std::min(rem, 1.0f / 30.0f);
            world_.tick(step);
            captureTransportEffects();
            rem -= step;
        }
        }
        profSimTicks_ += int64_t(SDL_GetPerformanceCounter()) - _sim0;
        // God economy: the SIM summons gods now (World::summonReadyGods, called from
        // tick), so all this does is announce one. It used to do the summoning here,
        // which meant the spawn happened on every client and never on the referee --
        // the server's world ran one unit short from the first summon onward and the
        // hashes split for the rest of the match.
        if (world_.godsEnabled())
            for (int t = 0; t < world_.numPlayers(); ++t)
                if (world_.player(t).godSummoned && !godAnnounced_[size_t(t) & 7]) {
                    godAnnounced_[size_t(t) & 7] = true;
                    if (!hudFont_.ok()) continue;
                    postNotice(t == localPlayer_ ? "YOUR GOD HAS ANSWERED"
                                                 : "AN ENEMY GOD RISES", 6);
                }
        // Scenario (.crt) "Display" actions: surface the sim runner's messages as
        // HUD notices for the viewing player. Drained on the sim thread (same as
        // scenario step, so no race on its queue); postNotice defers to main.
        if (auto* sc = world_.scenario())
            for (auto& m : sc->drainMessages()) postNotice(m.text, 8);
        if (amphib_) {
            auto* t = world_.unit(transportId_);
            if (t && t->alive()) {
                if (amphibPhase_ == 0 && int(t->cargo.size()) >= amphibSquad_) {
                    world_.unloadAt(transportId_, amphibSeaX_, amphibSeaZ_);
                    amphibPhase_ = 1;
                } else if (amphibPhase_ == 1 && t->cargo.empty()) {
                    for (auto& u : world_.units())
                        if (u.alive() && !u.embarked() && u.player == 0 && u.type &&
                            u.type->canMove && !u.type->canTransport)
                            world_.order(u.id, amphibLandX_, amphibLandZ_, false);
                    amphibPhase_ = 2;
                }
            }
        }
        // Victory check: last team standing. The sim computes winningTeam() and
        // per-player defeated flags each tick (deterministic across peers); the
        // viewer just maps that to this player's win/lose banner. Only armed once
        // at least two teams have fielded units (staged demos may field one).
        const int previousOutcome = outcome_.load();
        if (outcome_ == 0 && !missionStem_.empty()) {
            // Campaign mission: the in-sim god script + the .ota victory/defeat
            // conditions decide the result (last-team-standing doesn't apply -- the
            // objective may be an escort, a timer, a kill-target, etc.).
            outcome_ = world_.missionOutcome();
        } else if (outcome_ == 0 && world_.hasScenarioOutcomes()) {
            outcome_ = world_.scenarioOutcome(spectating_ ? -1 : localPlayer_);
        } else if (outcome_ == 0) {
            int teamsSeen = 0;
            for (int t = 0; t < world_.numPlayers(); ++t) {
                bool any = false;
                for (auto& u : world_.units())
                    if (u.alive() && u.type && world_.player(u.player).team == t) { any = true; break; }
                if (any) sawTeam_[t] = true;
                if (sawTeam_[t]) ++teamsSeen;
            }
            int win = world_.winningTeam();
            if (teamsSeen >= 2 && win >= 0) {
                outcome_ = (win == world_.player(localPlayer_).team) ? 1 : -1;
            } else if (!spectating_ && world_.player(localPlayer_).defeated && teamsSeen >= 2) {
                // A defeated player exits even when an allied player is still fighting.
                outcome_ = -1;
            }
        }
        // The result just landed: ask for this player's replay to be written. REQUEST,
        // not write -- simStep runs on the sim worker, while the net client appends to
        // its recorded bundles and hashes from the main thread. Serializing those
        // vectors from here would read them while they grow. The main thread picks
        // this up (see cosmeticStep) and does the work.
        if (previousOutcome == 0 && outcome_ != 0) replayWanted_.store(true, std::memory_order_relaxed);
#ifndef NDEBUG
        PaceSpan captureSpan(paceCaptureNs_);
#endif
        captureFrame();   // snapshot post-tick unit state for the render (poses + read fields)
    }

    void GameView::captureEffectVisibility() {
        if (noFog_) return;
        const auto& map=mapView_.map();
        if (!effectVisibility_ || effectVisibilityPlayer_!=localPlayer_ ||
            world_.tickCount()<=effectVisibilityTick_) {
            effectVisibilityPlayer_=localPlayer_;
            effectVisibility_=std::make_unique<tak::RetailEffectVisibility>(map.width/2,map.height/2,
                tak::sim::retailExplorationHeights(map.width,map.height,uint8_t(map.seaLevel),
                    [&](int x,int z) {return map.heights[size_t(z)*map.width+x];}));
        }
        effectVisibility_->expire(world_.tickCount());
        for (const auto& unit:world_.units()) {
            // Transport cargo keeps a live footprint (4f6a60 has no attachment test).
            const bool eligible=unit.type && unit.alive() && !unit.underConstruction &&
                                alliedToLocal(unit.player);
            effectVisibility_->track(unit.id,unit.player,localPlayer_,eligible,unit.alive(),
                                     unit.sightFootprint,world_.tickCount());
        }
        effectVisibilityTick_=world_.tickCount();
    }

    void GameView::captureTransportEffects() {
        captureEffectVisibility();
        SmokeTick smoke;
        smoke.tick=world_.tickCount();smoke.windX=world_.wind().x;smoke.windZ=world_.wind().z;
        if(smoke.tick<=smokeCaptureTick_) {smokeOwners_.clear();featureSmokeOwners_.clear();}
        smokeCaptureTick_=smoke.tick;
        for(auto it=smokeOwners_.begin();it!=smokeOwners_.end();) {
            const auto* owner=world_.unit(*it);
            if(!owner || tak::retailAttachedSfxOwnerRemoved(owner->alive(),owner->deadFor,
                    owner->corpseStatue,owner->corpseAnimationTicks())) {
                smoke.removedOwners.push_back(*it);it=smokeOwners_.erase(it);
            } else ++it;
        }
        for(auto it=featureSmokeOwners_.begin();it!=featureSmokeOwners_.end();) {
            const auto* feature=world_.feature(it->first);
            if(!feature || !feature->alive || !feature->burn ||
               feature->burnStarted!=it->second.burnStarted ||
               feature->burnSequence!=it->second.activationSequence) {
                smoke.removedFeatures.push_back(it->first);it=featureSmokeOwners_.erase(it);
            } else ++it;
        }
        for(const auto& feature:world_.features()) {
            const auto bodyAge=tak::retailFeatureSmokeAge(smoke.tick,feature.burnStarted);
            if(!feature.alive || !feature.burn || feature.type<0 || !bodyAge)continue;
            const auto& map=mapView_.map();
            const auto x=feature.x.v,z=feature.z.v;
            const int height=map.heights.empty() ? 0 : tak::sim::retailTerrainHeight(
                x,z,map.width,map.height,[&](int cx,int cz) {
                    return map.heights[size_t(cz)*map.width+size_t(cx)];
                });
            smoke.features.push_back({feature.id,world_.featureTypes()[size_t(feature.type)].name,
                {x,std::bit_cast<int32_t>(uint32_t(height)<<16),z},
                *bodyAge,feature.burnSequence,smoke.tick%3==0});
            featureSmokeOwners_[feature.id]={feature.burnStarted,feature.burnSequence};
        }
        tak::retailOrderFeatureSmokeNewestFirst(smoke.features);
        for(const auto& event:world_.scriptEmissions()) {
            if(!(event.code>=2 && event.code<=5) && event.code!=257 && event.code!=258 && event.code!=265 &&
               (event.code<260 || event.code>264))continue;
            const auto* owner=world_.unit(event.unitId);
            if(event.code>=2 && event.code<=5 && owner && !owner->type->canFly)continue;
            const bool transient=event.code==263 || event.code==264;
            if(owner && (transient || !tak::retailAttachedSfxOwnerRemoved(owner->alive(),
                    owner->deadFor,owner->corpseStatue,owner->corpseAnimationTicks())) &&
               (noFog_ || alliedToLocal(owner->player) ||
               world_.cellVisible(owner->x.toFloat(),owner->z.toFloat()))) {
                smoke.emissions.push_back(event);
                if(!transient)smokeOwners_.insert(event.unitId);
            }
        }
        {
            std::lock_guard<std::mutex> lock(hitQueueMutex_);
            smokeTickQueue_.push_back(std::move(smoke));
            // Capture every simulation step, including steps whose render
            // snapshot will be superseded before the next display frame.
            for (const auto& unit:world_.units()) {
                weaponAnimationQueue_.push(world_.tickCount(),unit.id,unit.weaponAnimations,
                    unit.firedWeapons,unit.x.toFloat(),unit.z.toFloat());
                if(unit.alive() && unit.type && unit.type->canFly)
                    flightAnimationQueue_.capture(world_.tickCount(),unit.id,unit.flightGroundMode==2,
                        unit.flightBeginCallbackSerial,unit.flightLandingCallbackSerial);
            }
        }
        if (!world_.transportEffects().empty()) {
            std::lock_guard<std::mutex> lock(hitQueueMutex_);
            for (const auto& event:world_.transportEffects()) {
                if (transportEffectQueue_.size() >= kMaxPendingHits) transportEffectQueue_.pop_front();
                transportEffectQueue_.push_back(event);
            }
        }
    }

    void GameView::captureFrame() {
        // Pick a spare buffer to write: never the published one (the render may pin it on
        // its next beginFrame) and never the one the render is currently pinned on. With
        // three buffers there is always exactly one such spare.
        int w;
        {
            std::lock_guard<std::mutex> lk(frameMutex_);
            for (w = 0; w < 3; ++w) if (w != published_ && w != reading_) break;
        }
        // Compact snapshots follow current records, not every lifetime unit ID.
        // The previous published buffer supplies same-ID interpolation poses.
        Frame& fb = frameBuf_[w];                // write buffer
        const Frame& pf = frameBuf_[published_]; // previously-published frame (last tick's poses)
        fb.gen = ++captureCounter_;     // records written this pass get gen==fb.gen (=> live this tick)
        fb.live.clear();
        fb.units.resize(world_.units().size());
        size_t slot=0;
        for (const auto& u : world_.units()) {
            UnitR& s=fb.units[slot];
            if(s.id!=u.id)s=UnitR{};
            if(fb.unitSlots.size()<=size_t(u.id))fb.unitSlots.resize(size_t(u.id)+1,-1);
            fb.unitSlots[size_t(u.id)]=int32_t(slot++);
            if (!u.type) { s.seeded = false; s.type = nullptr; continue; }
            // Render-read fields, captured for ALL units (alive + dead-recent: the death
            // animation and the deadFor>=4 cull both need a live value).
            s.gen = fb.gen;
            fb.live.push_back(&s);   // compact live list (mirrors world_.units())
            s.id = u.id; s.type = u.type; s.player = u.player;
            if (s.scenarioName != u.scenarioName) s.scenarioName = u.scenarioName;
            s.hp = u.hp.toFloat(); s.mana = u.mana; s.veteran = u.veteran;
            s.deadFor = u.deadFor < 0 ? -1.0f : float(u.deadFor) / 30.0f;   // ticks -> seconds
            s.inTransport = u.inTransport; s.squad = u.squad; s.stance = u.stance;
            s.standingOrder = u.standingOrder;
            s.weaponSlot = u.weaponSlot;
            for(size_t slot=0;slot<s.weaponReloads.size();++slot)
                s.weaponReloads[slot]=u.reloads[slot];
            s.underConstruction = u.underConstruction; s.buildBegun = u.buildBegun;
            s.replacementModel=nullptr;s.replacementOpacity=-1;
            if (u.underConstruction && u.lodestoneReplacement) {
                const float progress=u.constructionFraction();
                // An abandoned/damaged site fades out instead of keeping the old
                // building solid until the last unconjure tick.
                const float healthFade=std::clamp(u.hp.toFloat()/
                    (float(u.type->maxHp)*(0.05f+0.95f*progress)),0.0f,1.0f);
                s.replacementOpacity=std::abs(2.0f*progress-1.0f)*healthFade;
                if (progress<0.5f) {
                    const auto& old=*u.lodestoneReplacement;
                    s.replacementModel=old.type;
                    s.replacementX=old.x.toFloat();s.replacementZ=old.z.toFloat();
                    s.replacementHeading=tak::sim::radiansFromBam(old.heading);
                }
            }
            s.cloaked = u.cloaked; s.cloakOn = u.cloakOn; s.active = u.active;
            // The sim counts these in TICKS now (retail's representation); the HUD
            // wants seconds, so the conversion happens here, at the render boundary.
            s.frozenFor = float(u.frozenFor) / 30.0f;
            s.stonedFor = float(u.stonedFor) / 30.0f;
            s.paralyzedFor = float(u.paralyzedFor) / 30.0f;
            s.selfDestructT = u.selfDestructT < 0 ? -1.0f : float(u.selfDestructT) / 30.0f;
            const int conjureSiteId=u.buildSiteId ? u.buildSiteId : u.productionSiteId;
            // Zero means no site. Looking it up would fall back to a full unit
            // scan for every non-conjuring unit in this per-tick snapshot.
            const auto* conjureSite=conjureSiteId ? world_.unit(conjureSiteId) : nullptr;
            s.conjuring=conjureSite && conjureSite->underConstruction && conjureSite->buildBegun;
            s.hasConstructionEmitter=bool(u.constructionEmitter || u.cosmeticConstructionEmitter);
            s.constructionEmissions=u.constructionEmissions;
            if (u.constructionEmitter) s.constructionParticles=u.constructionEmitter->particles;
            else if (u.cosmeticConstructionEmitter) s.constructionParticles=u.cosmeticConstructionEmitter->particles;
            else s.constructionParticles.clear();
            s.buildSiteId = u.buildSiteId; s.productionSiteId = u.productionSiteId;
            s.reclaimId = u.reclaimId; s.repairId = u.repairId;
            s.yardOpen = world_.scriptYardOpen(u.id);
            s.scriptHealthPercent = int32_t(int16_t(u.hp.floorInt()))*100/std::max(u.maximumHp(),1);
            s.constructionPercentLeft = u.retailSite
                ? int(tak::sim::retailConstructionPercent(u.retailSite->progress.remaining))
                : int(u.underConstruction);
            s.buildProgress = float(u.buildProgress) / 30.0f;   // ticks -> seconds
            // Orders/rally/areaVisited are read for the VIEWED player's units only (the order
            // trails, queued-build ghosts and the HUD all gate on localPlayer_; a spectator or
            // replay viewer is seated as localPlayer_ too). Copying them for every unit was the
            // bulk of the capture cost at 10k units. Queues are tiny and only factories hold
            // one, so an ally's stay visible for the HUD.
            if (u.player==localPlayer_) s.captureOrders(u);
            else s.clearOrders();
            if (u.player==localPlayer_ || alliedToLocal(u.player)) s.buildQueue = u.buildQueue;
            else s.buildQueue.clear();
            s.cargo = u.cargo; s.repeatType = u.repeatType;
            s.captureMovement(u);
            s.corpseAnimationTicks=u.corpseAnimationTicks();
            s.corpsePhase = !u.alive() && u.deadFor < u.corpseUntil &&
                            u.deadFor >= u.corpseAnimationTicks();
            s.deathType = u.deathType;
            s.severity = u.severity;
            s.corpseFeat = u.corpseStatue >= 0 ? u.corpseStatue
                                               : world_.corpseTypeOf(u.type);
            s.corpseReclaimable=false;
            if (!u.alive() && s.corpseFeat>=0 && size_t(s.corpseFeat)<world_.featureTypes().size()) {
                const auto& corpse=world_.featureTypes()[size_t(s.corpseFeat)];
                s.corpseReclaimable=corpse.reclaimable;
                s.corpseCellX=tak::sim::footprintOrigin(u.x,u.type->footX)+u.type->corpseAdjX;
                s.corpseCellZ=tak::sim::footprintOrigin(u.z,u.type->footZ)+u.type->corpseAdjZ;
                s.corpseFootX=corpse.fx;s.corpseFootZ=corpse.fz;
            }
            s.corpseStatue = u.corpseStatue >= 0;
            // UnitR::speed is documented px/s and consumers (the flyer altitude servo,
            // the MotionControl percentage) rely on that; the sim keeps px/TICK now.
            s.justBuilt = u.justBuilt;
            s.disco = u.alive() && world_.discoTarget(u.player, u.id);
            s.headbang = u.alive() && world_.headbangTarget(u.player, u.id);
            s.alliedToLocal = alliedToLocal(u.player);
            // Pose: prev comes from the previously-published frame's curr for this SAME unit
            // (id live last tick + matching type). Interpolate alive units between ticks;
            // a dead unit holds its death pose. A big jump (teleport / id reuse) seeds fresh.
            const UnitR* prev=pf.unit(u.id);
            if(prev && prev->type!=u.type)prev=nullptr;
            s.headingWord=u.heading.v;
            s.captureOccupancy(u,world_.mapSea(),prev ? prev->animationOccupancy : 0);
            s.captureMoveRate(u,prev ? std::bit_cast<int16_t>(uint16_t(u.heading.v-prev->headingWord)) : 0);
            const auto multiplier=u.groundTerrainFlags&0x800 ? u.type->roadMult :
                u.groundTerrainFlags&0x1000 ? u.type->waterMult : tak::sim::Fixed::fromInt(1);
            s.turnSpeedPercent=prev ? tak::sim::retailTurnAnimationPercent(
                std::bit_cast<int16_t>(uint16_t(u.heading.v-prev->headingWord)),
                uint16_t(u.type->turnRate),uint16_t(u.type->turnInPlaceRate),
                multiplier.v,u.embarked()) : 0;
            if (u.alive() && prev &&
                std::abs(u.x.toFloat() - prev->x) <= 200.0f && std::abs(u.z.toFloat() - prev->z) <= 200.0f) {
                s.px = prev->x; s.pz = prev->z; s.ph = prev->heading;   // UnitR already holds radians
                s.x = u.x.toFloat(); s.z = u.z.toFloat(); s.heading = tak::sim::radiansFromBam(u.heading);   // render boundary: radians
                s.seeded = true;
                s.turnReqBam = u.turnReqBam;   // sim's requested turn this tick (TurnDirection)
            } else {
                s.x = u.x.toFloat(); s.z = u.z.toFloat(); s.heading = tak::sim::radiansFromBam(u.heading);   // render boundary: radians
                s.px = s.x; s.pz = s.z; s.ph = s.heading;
                s.turnReqBam = u.turnReqBam;
                s.seeded = u.alive();   // dead holds its pose (never interpolated)
            }
        }
        // Player table snapshot for the HUD/scoreboard.
        fb.numPlayers = world_.numPlayers();
        for (int p = 0; p < fb.numPlayers && p < int(fb.players.size()); ++p) {
            const auto& pl = world_.player(p);
            PlayerR& r = fb.players[size_t(p)];
            r.captureEconomy(pl);
            if (pl.godType) r.side = pl.godType->side;
            else {
                r.side = pf.players[size_t(p)].side;
                if (r.side.empty()) for (const auto& unit : world_.units())
                    if (unit.player == p && unit.type && !unit.type->side.empty()) {
                        r.side = unit.type->side;
                        break;
                    }
            }
            r.manaShareMask = pl.manaShareMask;
            r.kills = pl.kills; r.unitCount = pl.unitCount;
            r.built = pl.built; r.losses = pl.losses; r.score = pl.score;
            // sim keeps these in TICKS now; the scoreboard wants seconds.
            r.defeatedAt = pl.defeatedAt < 0 ? -1.0f : float(pl.defeatedAt) / 30.0f;
            r.team = pl.team; r.defeated = pl.defeated; r.godSummoned = pl.godSummoned;
            r.emoteSequence = pl.emoteSequence;
            r.emoteDisco = pl.emoteDisco;
        }
        fb.flames=world_.flames();
        fb.projectiles = world_.projectiles();   // sim push_back/erase each tick -> must copy
        fb.storms = world_.storms();             // ditto: the viewer draws these
        fb.hits = world_.hits();                  // weapon impacts this tick (cleared next tick)
        // ...and queue them for the render as well, so impacts survive a skipped
        // snapshot (see hitQueue_). Oldest out first when the renderer is so far
        // behind that the queue fills.
        if (!fb.hits.empty()) {
            std::lock_guard<std::mutex> hq(hitQueueMutex_);
            for (const auto& h : fb.hits) {
                pendingMonarchAlerts_ |= h.attackedMonarchPlayers;
                if (hitQueue_.size() >= kMaxPendingHits) hitQueue_.pop_front();
                hitQueue_.push_back(h);
            }
        }
        fb.shakeReq = world_.shakeRequest();       // copied under the worker's lock
        {
            auto sounds=world_.takeSoundRequests();
            std::lock_guard<std::mutex> lock(hitQueueMutex_);
            for(auto& sound:sounds) {
                if(missionSoundQueue_.size()>=256)missionSoundQueue_.pop_front();
                missionSoundQueue_.push_back(std::move(sound));
            }
        }
        fb.winningTeam = world_.winningTeam();
        fb.gameTick = world_.tickCount();
        fb.scenarioClock = world_.scenario() && world_.scenario()->showClock();
        fb.wind = world_.wind();
        // Fog snapshot: copy world_.vis_ into this buffer only when THIS buffer's fog is stale
        // (fog recomputes ~4Hz, so at most ~2 copies per change -- one per buffer). A spectator
        // (noFog_) leaves vis_ empty, so the copy is a no-op and cellVisibleR reveals all.
        if (fb.visGen != world_.visGeneration() || fb.vis.size() != world_.visibility().size()) {
            fb.vis = world_.visibility();
            fb.visW = world_.visW(); fb.visH = world_.visH();
            fb.visGen = world_.visGeneration();
        }
        if (effectVisibility_ && !noFog_) {
            const auto counts=effectVisibility_->counts();
            fb.effectVisibility.assign(counts.begin(),counts.end());
            fb.effectVisW=mapView_.map().width/2;fb.effectVisH=mapView_.map().height/2;
        } else fb.effectVisibility.clear();
        fb.tickMs = SDL_GetTicks64();
        fb.tickDurMs = (1000.0f / 30.0f) / std::max(0.1f, animSpeed());
        // Publish: the render's next beginFrame() will pin this buffer as front().
        {
            std::lock_guard<std::mutex> lk(frameMutex_);
            published_ = w;
        }
    }

    void GameView::interpPose(const UnitR& s, float& x, float& z, float& heading) const {
        x = s.x; z = s.z; heading = s.heading;
        if (!settings_ || !settings_->smoothMotion || !s.seeded) return;
        float a = interpAlpha_;
        x = s.px + (s.x - s.px) * a;
        z = s.pz + (s.z - s.pz) * a;
        float dh = s.heading - s.ph;
        while (dh >  3.14159265f) dh -= 6.28318531f;   // shortest-path turn
        while (dh < -3.14159265f) dh += 6.28318531f;
        heading = s.ph + dh * a;
    }

    const UnitR& GameView::frameUnit(int id) const {
        static const UnitR kEmpty{};
        const auto* u=front().unit(id);
        return u ? *u : kEmpty;
    }

    const UnitR* GameView::frameUnitP(int id) const {
        return front().unit(id);
    }

    const PlayerR& GameView::framePlayer(int p) const {
        static const PlayerR kEmpty{};
        const Frame& f = front();
        return (p >= 0 && p < int(f.players.size())) ? f.players[size_t(p)] : kEmpty;
    }

    bool GameView::cellVisibleR(float x, float z) const {
        const Frame& f = front();
        if (f.vis.empty()) return true;
        int cx = int(x) / 16, cz = int(z) / 16;
        if (cx < 0 || cz < 0 || cx >= f.visW || cz >= f.visH) return false;
        return f.vis[size_t(cz) * f.visW + cx] == 2;
    }

    bool GameView::effectVisibleR(const std::array<int32_t,3>& position) const {
        if (noFog_) return true;
        const auto& frame=front();
        if (frame.effectVisibility.empty()) return false;
        return tak::RetailEffectVisibility::visible(frame.effectVisibility,
            frame.effectVisW,frame.effectVisH,position);
    }

    int GameView::frameQueuedCount(int builderId, const tak::sim::UnitType* type) const {
        const UnitR* b = frameUnitP(builderId);
        if (!b || !type) return 0;
        int n = 0;
        for (const auto* q : b->buildQueue) if (q == type) ++n;
        return n;
    }

    void GameView::cosmeticStep(float dt) {
        // Reads the render SNAPSHOT (front()), never live world_, so it is safe
        // on the main thread while the sim worker ticks. Transient combat events
        // arrive through queues so skipped snapshots cannot discard them.
        // Deferred replay write, requested by the sim thread when the result landed.
        // Done HERE because this runs on the main thread, which is the only one that
        // may read the net client's recorded bundles while it is still connected.
        if (replayWanted_.exchange(false, std::memory_order_relaxed)) saveNetReplay();
        const bool newTick_ = (front().gen != lastCosmeticGen_);
        lastCosmeticGen_ = front().gen;
        // Weapon impacts this tick: play each weapon's soundhitclass, picking the
        // material-specific variant from the struck unit's bodytype (flesh/armor/..).
        // A storm announces itself once. Its ending animation and movement now
        // remain in the authoritative storm rather than a detached effect.
        if (newTick_) {
            std::unordered_map<int, StormTrack> live;
            for (const auto& st : front().storms) {
                auto prev = stormsSeen_.find(st.id);
                if (prev == stormsSeen_.end() && st.w && !st.w->soundHit.empty())
                    sounds_.playWorld(st.w->soundHit, st.x.toFloat(), st.z.toFloat());
                live.emplace(st.id, StormTrack{st.x.toFloat(), st.z.toFloat(), st.w});
            }
            stormsSeen_ = std::move(live);
        }
        // A mission script asking for a camera shake: fire on the sequence edge.
        if (newTick_) {
            const auto& sr = front().shakeReq;     // snapshot, not live world_
            if (sr.seq != shakeSeqSeen_) { shakeSeqSeen_ = sr.seq; triggerShake(sr.mag, sr.dur); }
        }
        std::vector<tak::sim::World::SoundReq> missionSounds;
        {
            std::lock_guard<std::mutex> lock(hitQueueMutex_);
            while(!missionSoundQueue_.empty() &&
                  int32_t(front().gameTick-missionSoundQueue_.front().tick)>=0) {
                missionSounds.push_back(std::move(missionSoundQueue_.front()));
                missionSoundQueue_.pop_front();
            }
        }
        for(const auto& request:missionSounds) {
            auto name=request.name;
            std::transform(name.begin(),name.end(),name.begin(),::tolower);
            if(name.ends_with(".wav"))name.resize(name.size()-4);
            // Native mission host 4d3580: always global, non-looping, authored
            // low-three-bit priority. SoundBank enforces the 0/1 free-voice gate.
            if(!name.empty())sounds_.play(name,1.f,1.f,tak::retailSoundPriority(request.flags),false);
        }
        // Drained, not gated on newTick_: the whole point is to pick up impacts
        // from ticks whose snapshots the render never saw.
        std::vector<tak::sim::World::HitFx> hitsToShow;
        uint8_t monarchAlerts = 0;
        {
            std::lock_guard<std::mutex> hq(hitQueueMutex_);
            monarchAlerts = std::exchange(pendingMonarchAlerts_, uint8_t(0));
            hitsToShow.assign(hitQueue_.begin(), hitQueue_.end());
            hitQueue_.clear();
        }
        if (monarchAlert_.admit(monarchAlerts, localPlayer_, spectating_, SDL_GetTicks64()))
            sounds_.play("alarmmon", 1.f, 1.f, 7, false);
        for (const auto& h : hitsToShow) {
            // Instant-hit weapons (FBI type = Line of Sight) spawn no projectile, so
            // nothing was ever drawn for them -- the Aramon King's Thunder, the Creon
            // tasers, the Zhon lightning and a dozen more fired completely INVISIBLY.
            // Draw the shot itself: a bolt struck from the firer to the victim, in
            // the weapon's own inner/middle/outer colours where it declares them.
            // Fire breath is a Line-of-Sight weapon too, but retail's flame class
            // draws an emitter, not a bolt -- and we already draw its flame. Excluding
            // it here stops a drake's breath coming with a spurious lightning streak.
            if (h.weapon && h.weapon->beam && !h.weapon->straight && !h.weapon->lightning && h.weapon->flameKind<0 && !h.weapon->melee &&
                h.weapon->fx != tak::sim::WeaponFx::Fire &&
                (noFog_ || cellVisibleR(h.x, h.z))) {
                BeamFx b;
                b.model = h.weapon->shotModel;
                b.sprite = h.weapon->weaponArt.empty() ? h.weapon->shotArt : h.weapon->weaponArt;
                b.player = h.fromPlayer;
                b.x1 = h.fromX; b.z1 = h.fromZ;
                b.x2 = h.x;     b.z2 = h.z;
                if(h.fromPosition) {
                    b.x1=float((*h.fromPosition)[0]>>16);b.z1=float((*h.fromPosition)[2]>>16);
                }
                if(h.position) {
                    b.x2=float((*h.position)[0]>>16);b.z2=float((*h.position)[2]>>16);
                }
                b.lightning = h.weapon->fx == tak::sim::WeaponFx::Lightning;
                for (int i = 0; i < 3; ++i) {
                    b.inner[i] = h.weapon->inner[i];
                    b.middle[i] = h.weapon->middle[i];
                    b.outer[i] = h.weapon->outer[i];
                }
                auto lift = [&](const auto& position,float x,float z) {
                    return position ? float((*position)[1] >> 16) * 0.5f -
                        float(heightRef_) * 0.5f - terrainLift(x,z) - 12.f : 0.f;
                };
                b.alt1 = lift(h.fromPosition,b.x1,b.z1);
                b.alt2 = lift(h.position,b.x2,b.z2);
                b.fromPosition=h.fromPosition;b.position=h.position;
                // Lifetime = flight time of the virtual shot. Clamped so a zero or
                // silly weaponvelocity can't leave a bolt on screen for a minute.
                float bdx = b.x2 - b.x1, bdz = b.z2 - b.z1;
                float bdist = std::sqrt(bdx * bdx + bdz * bdz);
                b.life = h.weapon->projVel > 0.0f
                             ? std::clamp(bdist / h.weapon->projVel, 0.05f, 1.5f)
                             : 0.16f;
                beams_.push_back(b);
            }
            // A wandering storm grinds EVERY TICK, so routing its hits through the
            // ordinary impact path meant 30 generic dust bursts and 30 hit sounds a
            // second, each one a dozen-plus particles. The storm's own animation and
            // its one-shot cast sound carry it instead.
            if (h.weapon && h.weapon->kind == tak::sim::Weapon::Kind::Wandering) continue;
            if (h.weapon && !h.weapon->soundHit.empty()) {
                const std::string& body = h.target ? h.target->bodyType : std::string("default");
                const std::string* wav = soundClasses_.pick(h.weapon->soundHit, body, salt_++);
                if (!wav) wav = soundClasses_.pick(h.weapon->soundHit, "default", salt_++);
                if (wav) sounds_.playWorld(*wav, h.x, h.z);
                // soundhit names a WAV directly; soundhitclass names a table.
                else if (sounds_.has(h.weapon->soundHit))
                    sounds_.playWorld(h.weapon->soundHit, h.x, h.z);
            }
            // Impact visual: play the weapon's real GAF/TAF explosion effect
            // (water variant over water). Missing art does not request extra particles.
            if (h.weapon) {
                spawnWeaponImpact(*h.weapon, h.x, h.z, 0, h.position, h.victimId != 0);
            }
            // Weapon area-effect: expanding shockwave rings (radiusart, staggered
            // by ringdelay) and ground fire (firestarter) at the impact.
            if (h.weapon) {
                float maxR = std::max(h.weapon->aoe * 0.5f, 48.0f);
                for (int i = 0; i < h.weapon->ringCount && i < 3; ++i)
                    if (!h.weapon->radiusArt[i].empty())
                        spawnRing(h.weapon->radiusArt[i], h.x, h.z,
                                  float(i) * h.weapon->ringDelay, h.weapon->ringDur,
                                  h.weapon->spriteCount, maxR);
                if (h.weapon->fireStarter && !world_.isWater(h.x, h.z))
                    spawnEffectAnim("flame", h.x, h.z, 0.0f, 0.0f, 5);   // fire lingers
                // Camera shake for heavy impacts you can actually see.
                if (h.weapon->shakeMag > 0 && cellVisibleR(h.x, h.z))
                    triggerShake(h.weapon->shakeMag, h.weapon->shakeDur);
            }
            if (h.target && h.target->bodyType == "flesh") {
                // Spray blood from the victim's SweetSpot (its body centre) rather
                // than the ground hit point -- retail asks the COB for that piece
                // (SweetSpot -> out-param local 0) and homes hit effects to it.
                float bx = h.x, bz = h.z;
                float ba = h.position ? float((*h.position)[1] >> 16) * 0.5f -
                    float(heightRef_) * 0.5f - terrainLift(bx,bz) : 0.f;
                if (h.victimId && (noFog_ || cellVisibleR(h.x, h.z)))
                    if (auto vi = anims_.find(h.victimId); vi != anims_.end() &&
                        vi->second.vm && vi->second.pieceNames)
                        if (const UnitR* v = frameUnitP(h.victimId); v && v->type) {
                            vi->second.vm->call("SweetSpot", {0});
                            const auto& ll = vi->second.vm->lastLocals();
                            int pc = ll.empty() ? -1 : ll[0];
                            if (pc >= 0 && pc < int(vi->second.pieceNames->size())) {
                                float sx, sz, sa;
                                if (pieceWorldFx(*v, vi->second,
                                                 (*vi->second.pieceNames)[size_t(pc)], sx, sz, sa)) {
                                    bx = sx; bz = sz; ba = sa;
                                }
                            }
                        }
                spawnBurst(bx, bz, 5, h.target->blood[0], h.target->blood[1],
                           h.target->blood[2], 26, 1.8f, 0, ba);
            }
            // (mission ScreenShake is handled once per tick, below the hit loop)
        }
        // Sim-driven feature fire: burn-anim playback, smoke, burnt-art swaps.
        syncBurningFeatures();
        // Wind is simulation state; consume the published snapshot so skipped
        // render frames and animation rates cannot change its random stream.
        windGen_ = front().wind.generation;
        windSpeed_ = float(front().wind.speed);
        windHeading_ = float(front().wind.heading) * (6.2831853f / 65536.0f);
        // (No world_.clearHits() here: World::tick already clears hits_ at the start of the
        //  next tick, so the viewer-side clear was redundant -- and dropping it keeps the
        //  render path from mutating sim state, a prerequisite for the sim-thread decouple.
        //  See docs/sim-render-decouple-plan.md.)
        updateParticles(dt);
        updateEffects(dt);
        updateRings(dt);
        if (tak::devEnv("TAK_STUCKSTAT")) {   // crowd-jam diagnostic
            static float acc = 0; acc += dt;
            if (acc >= 2.0f) {
                acc = 0;
                int moving = 0, stalled = 0, ordered = 0;
                for (const UnitR* _up : front().live) {
                    const UnitR& u = *_up;
                    // isStructure() again: canMove would count the Barracks as a
                    // ground mover that never moves, i.e. permanently "stalled".
                    if (!u.alive() || !u.type || u.type->isStructure() || u.type->canFly ||
                        u.orders.empty() || u.orders.front().targetId != 0) continue;
                    ordered++;
                    if (u.speed > 3.0f) moving++; else stalled++;
                }
                std::printf("stuckstat t=%.0f ordered=%d moving=%d stalled=%d\n",
                            animClock_, ordered, moving, stalled);
                std::fflush(stdout);
            }
        }
        // Register newly-seen units for rendering (load the model + COB once per type,
        // then build a per-unit animation VM and run its Create script). BUDGETED: a huge
        // simultaneous spawn -- the stress test, a mass transport unload, or a big army
        // revealed by fog -- would otherwise build thousands of VMs and run their Create
        // scripts in ONE frame, a multi-hundred-millisecond hitch that "settles" only once
        // every unit is registered. Cap new registrations per frame so the cost spreads
        // over ~a second; the render path already skips units not yet in unitType_, so the
        // stragglers just pop in a few frames later. Purely cosmetic -- the sim already has
        // them (they move/fight); this only gates when the client starts drawing them.
        int regBudget = kRegistrationsPerFrame;
        for (const UnitR* _up : front().live) {
            const UnitR& u = *_up;
            if (u.type && u.alive() && !unitType_.count(u.id)) {
                if (u.replacementModel && !visuals_.count(u.replacementModel->id))
                    loadVisual(u.replacementModel->id);
                registerUnit(u.id, u.type);
                if (--regBudget <= 0) break;
            }
        }
        consumeSmokeTicks();
        for(auto it=pointParticles_.begin();it!=pointParticles_.end();) {
            const auto* owner=frameUnitP(it->first);
            if(!owner || !owner->alive())it=pointParticles_.erase(it);else ++it;
        }
        // Drain mission-time events even when whole render snapshots were skipped.
        std::deque<tak::sim::World::TransportFx> transportEffects;
        std::unordered_map<int,std::vector<tak::RetailWeaponAnimation>> weaponAnimations;
        std::unordered_map<int,std::vector<tak::WeaponAnimationQueue::Shot>> weaponShots;
        std::unordered_map<int,std::vector<tak::RetailFlightAnimationQueue::Snapshot>> flightSnapshots;
        {
            std::lock_guard<std::mutex> lock(hitQueueMutex_);
            flightAnimationQueue_.drain(front().gameTick,[&](const auto& snapshot) {
                flightSnapshots[snapshot.unit].push_back(snapshot);
            });
            weaponAnimationQueue_.drain(front().gameTick,[&](int unit,const auto& callback) {
                weaponAnimations[unit].push_back(callback);
            },[&](int unit,const auto& shot) {
                weaponShots[unit].push_back(shot);
            });
            while (!transportEffectQueue_.empty() &&
                   int32_t(front().gameTick-transportEffectQueue_.front().tick)>=0) {
                transportEffects.push_back(transportEffectQueue_.front());
                transportEffectQueue_.pop_front();
            }
        }
        for (const auto& event:transportEffects) {
            const auto spawn=[&](const char* name,const std::array<int32_t,3>& point) {
                spawnEffectAnim(name,float(point[0])/65536.0f,float(point[2])/65536.0f,
                    0,0,1,0,true,point,event.tick);
            };
            spawn("mindspin",event.passenger);
            spawn("transportfx:transswirl",event.carrier);
        }
        // Kick off the summon fade-in/shimmer for anything just conjured from a
        // building (the producer flags justBuilt for that one tick); then age the
        // active effects and drop finished or dead ones. Cosmetic, viewer-only.
        for (const UnitR* _up : front().live) {
            const UnitR& u = *_up;
            if (u.justBuilt && !birthFx_.count(u.justBuilt)) birthFx_[u.justBuilt] = 0.0f;
        }
        for (auto it = birthFx_.begin(); it != birthFx_.end();) {
            const auto* bu = frameUnitP(it->first);
            it->second += dt;
            if (it->second >= kBirthFxDur || !bu || !bu->alive()) it = birthFx_.erase(it);
            else ++it;
        }
        animClock_ += dt;

        // (Corpses: the sim keeps the dead-unit record for the corpse window and
        // the paint pass draws it wearing the corpse mesh -- see
        // maybeSwapCorpseModel. The old path here tried to spawn a cosmetic
        // FEATURE, which never worked: corpse defs are object= models with no
        // sprite art, so addFeature always bailed before drawing anything.)
        if (noticeTimer_ > 0) noticeTimer_ -= dt;

        // (T-tracking / edge-scroll / shake now run per-frame in cameraFrame, so
        // the camera stays smooth when the net sim stalls.)
        if (follow_ && !front().live.empty()) {
            // Track moving friendly units; fall back to everyone.
            float cx = 0, cz = 0;
            int n = 0;
            for (const UnitR* _up : front().live) {
                const UnitR& u = *_up;
                if (u.alive() && u.player == 0 && u.type && u.type->canMove &&
                    u.moving()) { cx += u.x; cz += u.z; ++n; }
            }
            if (!n)
                for (const UnitR* _up : front().live) {
                    const UnitR& u = *_up;
                    if (u.alive()) { cx += u.x; cz += u.z; ++n; }
                }
            if (n)
                mapView_.setOffset(cx / float(n) - 640 / mapView_.zoom(),
                                   cz / float(n) - 400 / mapView_.zoom());
        }
        for (const UnitR* _up : front().live) {
            const UnitR& u = *_up;
            if (u.alive()) maybeSwapVeteranModel(u);
            else if (u.corpsePhase) maybeSwapCorpseModel(u);
            auto it = anims_.find(u.id);
            if (const auto shots=weaponShots.find(u.id);u.type && shots!=weaponShots.end())
            for (const auto& shot:shots->second)
            for (int slot=0;slot<int(u.type->weapons.size()) && slot<32;++slot) {
                if (!(shot.weapons & (uint32_t(1)<<slot))) continue;
                const auto& w = u.type->weapons[size_t(slot)];
                // These native initializers start one faction nimbus on the
                // caster, restarting any prior instance. Remote/wandering
                // initialization does not require projectile velocity.
                using Kind = tak::sim::Weapon::Kind;
                if (w.nimbus && (((w.straight || w.lightning) && w.projVel > 0) ||
                    w.kind == Kind::Remote || w.kind == Kind::Wandering)) {
                    if (!factionNimbusLoaded_) {
                        factionNimbus_ = tak::gaf::factionNimbus(vfs_);
                        factionNimbusLoaded_ = true;
                    }
                    std::string side = u.type->side;
                    std::transform(side.begin(), side.end(), side.begin(), ::tolower);
                    const auto name = factionNimbus_.find(side);
                    if (name != factionNimbus_.end() && !name->second.empty())
                        if (const auto* art = effectFor(name->second))
                            nimbusEffects_[u.id] = {art, shot.tick};
                }
                // FireWeapon/Attack COB callbacks own firing audio, including
                // deliberately silent branches. Native 530140 dispatches the
                // callback; it does not infer sounds from the weapon's visual kind.
                // Projectile creation already resolves the authoritative muzzle.
                // Script emissions and authored weapon art supply firing effects;
                // a generic puff here adds a flash even to ordinary arrows.
            }
            if (it == anims_.end()) continue;
            auto& a = it->second;
            if (!u.alive()) {
                if (!a.dying) {
                    a.dying = true;
                    // Destroyed cargo remains hidden. Native 512860 propagates
                    // a zero-severity/type-8 death packet to attached passengers,
                    // bypassing their ordinary Killed/Dying presentation.
                    // Explicit retirement includes a lodestone consumed by an
                    // upgrade. It is not a combat death and has no death script.
                    // Combat severity survives retirement, so a skipped render snapshot
                    // must not swallow the authored death sound and detached effects.
                    if (u.embarked() || (u.deadFor>=float(tak::sim::World::kRetiredTicks)/30.0f &&
                                         !u.severity)) {
                        a.vm->reset();a.ownerVmStopRequested=true;continue;
                    }
                    // Retail order (icd 0x512610): Killed(severity, corpseOut,
                    // deathType) runs first -- deathType 3 (explosion kill)
                    // EXPLODEs every piece there; no shipped script reads the
                    // severity, and the corpse decision is the SIM's
                    // (corpseUntil), so the out-param is ignored. Then
                    // Dying(deathType): the death cry (92 units PLAY_SOUND
                    // there, zero in `death`), the fall-over via CALL death,
                    // the final EXPLODEs.
                    int32_t dtype = u.deathType;
                    if (!u.severity) {
                        // Petrified/frozen: retail skips Killed AND Dying
                        // (severity forced 0) -- the victim simply freezes in
                        // its current pose and stands as the statue. reset()
                        // stops threads but leaves in-flight piece turns/moves
                        // active, so also stop display VM advancement.
                        a.vm->reset();a.ownerVmStopRequested=true;
                    } else {
                        // Keep the existing pose/statics and let the authored
                        // callbacks stop their own threads, as in the simulation.
                        if (dtype!=tak::sim::Unit::kDeathSelfDestruct)
                            a.vm->call("Killed", {int32_t(u.severity), 0, dtype});
                        if (!a.vm->start("Dying", {dtype})) a.ownerVmStopRequested=true;
                    }
                    // Death audio belongs to Killed/Dying, including deliberately
                    // silent branches. Do not invent a cry from a filename.
                    // Killed/Dying own death pieces and emitted effects. Units
                    // with EXPLODEAS also produce their authored weapon impact
                    // through the simulation. Do not add a generic body blast.
                    // (The mission "UnitDestroyed" hook now fires deterministically in
                    // simStep on the sim thread -- see the death-edge detection there --
                    // rather than here off the render-side death animation.)
                }
                continue;   // VM advanced in the parallel pass below
            }
            if (a.flying) {
                // Flight height already follows the retail controller in the sim.
                // The old independent 0.7s ascent and terrain servo could draw a
                // conjurer far north of her actual position, especially near hills.
                const float datum=flyerGround(u.x,u.z); // also resolves heightRef_
                const auto [ground,altitude]=u.flightRenderHeight(datum,heightRef_);
                a.groundY=ground;a.groundInit=true;a.altitude=altitude;
                // Occupancy is authoritative too: a hovering conjurer stays active
                // even when stationary; a landed flyer plays its landing pose.
                const bool air=u.flightGroundMode==2;
                if (a.hasFlightSM) {
                    // Drake VTOL state machine: retail's engine only ever calls
                    // BeginFlight (takeoff) and BeginLanding (descent), plus
                    // setSFXoccupy(5) to report ACTIVE (static 6). Two Create-started
                    // threads then drive the pose, both gated on static 6:
                    //   FlightControl -- plays launch->fly->soar while active and NOT
                    //     (aiming static 5 / building static 9); when aiming or building
                    //     it switches to `attack` for the airborne body.
                    //   RestoreWatcher -- while active AND building (static 10, set by
                    //     Go via StartBuilding->RequestState), loops `build`, the conjure
                    //     arm gesture; else eases back to rest.
                    // Both freeze if static 6 goes clear -- so the flyer must stay ACTIVE
                    // through hovering builder work, as reported by flightGroundMode. Traced
                    // with tools/re/emuphase.py + the cob VM; pinned by conjure_test,
                    // which drives this exact sequence through the real Vm. No reset()
                    // -- the loops must keep running.
                    const auto startFlightCallback=[&](auto call) {
                        switch (call) {
                            case tak::RetailFlightAnimationCall::BeginFlight:
                                a.vm->start("BeginFlight");
                                break;
                            case tak::RetailFlightAnimationCall::BeginLanding:
                                a.vm->start("BeginLanding");
                                break;
                            case tak::RetailFlightAnimationCall::EndTransport:
                                a.vm->start("EndTransport");
                                break;
                        }
                    };
                    if(const auto snapshots=flightSnapshots.find(u.id);snapshots!=flightSnapshots.end())
                        for(const auto& snapshot:snapshots->second)
                            tak::updateRetailFlightAnimation(a.flightAnimation,snapshot.airborne,
                                snapshot.beginFlightSerial,snapshot.landingSerial,
                                u.type->canTransport,startFlightCallback);
                    // Also initialize newly registered units and retain the
                    // existing mode fallback when no transition packet is due.
                    tak::updateRetailFlightAnimation(a.flightAnimation,air,
                        u.flightBeginCallbackSerial,u.flightLandingCallbackSerial,
                        u.type->canTransport,startFlightCallback);
                }
                // Other flyers (Priest and ambient birds) run their own
                // Create-started controllers, polling speed/vertical motion.
                // Resetting them to a hand-picked fly/land script kills those
                // controllers and the Priest's concurrent build animation.
            }
            // Every shipped walker has a Create-owned movement controller.
            // GET 29/28/34 drives its gait without resetting concurrent scripts.
            // Native 4dc800's common tail receives TurnDirection during the
            // mover, then MoveRate (4db350), then setSFXoccupy (4dc600). All
            // three can signal COB threads, so keep that order on coincident
            // transitions for both ground and flight movers.
            // TurnDirection uses the REQUESTED turn (want - heading, unclamped
            // BAM), converted with retail's truncating /182. Retail notifies only
            // when its turn state changes sign, including the stop transition.
            const int turnDegrees=u.turnReqBam/182;
            tak::updateRetailMovementAnimationCallbacks(a.hasTurnDir,turnDegrees,a.turnSign,
                u.animationMoveRate,a.moveRate,u.animationOccupancy,a.occupancy,
                [&](int degrees) { a.vm->start("TurnDirection",{degrees}); },
                [&](uint32_t rate) { a.vm->start("MoveRate",{int32_t(rate)}); },
                [&](uint32_t surface) { a.vm->start("setSFXoccupy",{int32_t(surface)}); });
            // The simulation owns readiness and callback order. Do not recompute
            // aim from interpolated render poses or a separate display handshake.
            if (const auto callbacks=weaponAnimations.find(u.id);callbacks!=weaponAnimations.end())
            for (const auto& event:callbacks->second) {
                switch(event.kind) {
                case tak::RetailWeaponAnimation::Aim:
                    a.vm->start("AimWeapon",{event.heading,event.pitch,event.slot});
                    break;
                case tak::RetailWeaponAnimation::Fire:
                    a.vm->start("FireWeapon",{event.slot}) || a.vm->start("attack1") || a.vm->start("fire");
                    a.firing=true;
                    break;
                case tak::RetailWeaponAnimation::Clear:
                    a.vm->start("TargetCleared",{event.slot});
                    break;
                case tak::RetailWeaponAnimation::Switch:
                    a.vm->start("SwitchWeapon",{event.slot});
                    break;
                case tak::RetailWeaponAnimation::Hit:
                    a.vm->start("HitByWeapon",{event.slot,
                        tak::sim::retailScaledCosine(event.heading,400),
                        tak::sim::retailScaledSine(event.heading,400),event.damage});
                    break;
                }
            }
            // Native 4d4520 passes wind bearing minus unit bearing in the
            // retail heading convention, without normalizing that difference.
            if (u.type->receivesWind && a.hasWind && a.windStamp != windGen_) {
                a.windStamp = windGen_;
                const auto bodyHeading=tak::sim::portHeadingToRetail(
                    tak::sim::bamFromRadians(u.heading));
                const int32_t relativeHeading=int32_t(front().wind.heading)-int32_t(bodyHeading);
                a.vm->start("WindChange", {front().wind.speed,relativeHeading});
            }
            // Builders: the conjure/build animation while actively working a site
            // (constructing, repairing, or reclaiming). Retail drives this via the COB
            // StartBuilding/StopBuilding hooks -- StartBuilding raises the script's own
            // "am building" gate, StopBuilding clears it. This now covers FLYING builders
            // too (arafly/zonhunt via their FlightControl, tarpries via a direct `build`),
            // which used to just flap in place while conjuring.
            if (u.type->isBuilder && !isStructure(u.type)) {
                // Each queued output gets its own site id. A non-empty queue
                // alone is not work: while capped there is no active conjure
                // site, and consecutive products must retrigger one-shot poses.
                tak::updateRetailBuilderAnimation(a.building,a.workId,
                    u.buildSiteId,u.repairId,u.reclaimId,!u.buildQueue.empty(),
                    u.productionSiteId,u.conjuring,u.walking(),u.type->canFly,
                    [&](bool working) {
                        if (!a.flying) {
                            // Ground builder.
                            if (working) a.vm->start("StartBuilding") || a.vm->start("startbuild");
                            // The script's own controller restores the pose after
                            // StopBuilding. Starting restore_x here races that
                            // controller and bypasses its attack/movement guards.
                            else a.vm->start("StopBuilding");
                        } else if (a.hasFlightSM) {
                            // SM flyer (arafly/zonhunt monarch): StartBuilding sets unit
                            // value 5 and fires RequestState->Go, raising statics 9/10;
                            // RestoreWatcher (a Create thread) then loops `build` -- NOT
                            // FlightControl, which plays `attack` for the body. Verified by
                            // driving the real Vm: arms conjure, body holds the attack pose.
                            // No reset.
                            a.vm->start(working ? "StartBuilding" : "StopBuilding");
                        } else {
                            // Generic flyer builder (tarpries): no FlightControl loop, so the
                            // flight re-kick above loops `build` while a.building (matching
                            // retail's BuildControl). Just flip the state + set the COB gate.
                            a.vm->start(working ? "StartBuilding" : "StopBuilding");
                        }
                    });
            }
            // Buildings: yard/production anims. Detect via isStructure (maxVel <= tak::sim::Fixed()),
            // NOT !canMove -- the Keep/Castle/Hell carry canmove=1 in their FBI, so
            // the old test skipped every factory and none of them ever animated
            // while training.
            if (u.type && isStructure(u.type)) {
                // Use exported state-machine entry points rather than internal
                // door routines; keep Create-owned ambient threads running.
                tak::updateRetailFactoryAnimation(a.producing,a.workId,u.underConstruction,
                    !u.buildQueue.empty(),u.productionSiteId,[&](auto call) {
                        using Call=tak::RetailFactoryAnimationCall;
                        switch(call) {
                        case Call::Activate: a.vm->start("Activate");break;
                        case Call::Deactivate: a.vm->start("Deactivate");break;
                        case Call::StartBuilding: a.vm->start("StartBuilding",{0,0});break;
                        case Call::StopBuilding: a.vm->start("StopBuilding");break;
                        }
                    });
            }
            // The simulation owns activation, including manual gate commands and
            // automatic AI gate occupancy checks. Mirror its edge so door poses
            // agree with the authoritative yard handshake below.
            if (u.type && u.type->onOffable && a.hasActivate && u.active != a.active) {
                a.active = u.active;
                a.vm->start(u.active ? "Activate" : "Deactivate");
            }
            // Cloak pose. StartCloaking/StopCloaking are real engine entry points
            // (they appear in the icd's call-script-by-name sites alongside Create
            // and Activate), and the two units that define them -- araspy and
            // npcheket -- fold their pieces with MOVE_NOWs in the `cloak` script
            // that StartCloaking starts. We were never calling either, so a
            // cloaking spy just went transparent in its walking pose.
            // Native callbacks follow the requested mode. Enemy proximity or
            // insufficient mana can reveal a unit without restoring its pose.
            if (a.hasCloakAnim && u.cloakOn != a.cloakOn) {
                a.cloakOn = u.cloakOn;
                a.vm->start(u.cloakOn ? "StartCloaking" : "StopCloaking");
            }
            // (The VM itself is advanced in the parallel pass below.)
        }

        // (The per-unit animation VMs are advanced in animFrame(), once per RENDER frame
        // rather than per sim tick, so the heavy parallel pass doesn't clump onto the
        // 1-in-8 net frame that also runs the sim tick, and animation runs at display rate.
        // The STATE transitions above -- which script/gait, script starts, sounds -- stay
        // here on the sim tick.)
    }

    void GameView::animFrame(float realDt) {
        if (paused_) return;
        // A game can also stop dead without anyone touching the pause key: when bundles
        // stop arriving a net game simply produces no ticks. Everything ELSE already
        // holds still when that happens -- unit positions clamp to the newest pose
        // (interpAlpha_), and cosmeticStep runs only for ticks actually drained -- but
        // the animation VM is deliberately decoupled from the tick rate so that the walk
        // cycle stays smooth between 30Hz ticks, and that decoupling meant it kept
        // playing through a stall: legs walking on the spot, wheels turning, rotors
        // spinning, while the world stood frozen. Freeze it on the same event, so a
        // stalled game reads as stopped rather than broken.
        //
        // Measured from when the SIM CLOCK last moved, NOT from the age of the newest
        // snapshot. front() is pinned for the whole frame, so the snapshot is always at
        // least one frame old, and an age test would call a healthy client on a slow
        // machine permanently stalled at any frame rate below the threshold.
        // Only once the game is actually running. Before the first tick lands -- loading,
        // the lobby, the pre-game hold -- gameTick sits at 0 and never "advances", which
        // the gate below would read as a stall and freeze on, then visibly un-freeze the
        // moment the first tick arrived. There is nothing to stall yet at tick 0.
        const uint64_t nowMs = SDL_GetTicks64();
        if (front().gameTick == 0) { animAdvanceMs_ = nowMs; animLastTick_ = 0; }
        else if (front().gameTick != animLastTick_ || animAdvanceMs_ == 0) {
            animLastTick_ = front().gameTick;
            animAdvanceMs_ = nowMs;
        }
        // Scaled by the tick interval rather than a flat millisecond figure: game speed
        // changes how long a tick takes, and a fixed threshold would read a deliberately
        // slowed game as a permanent stall. The floor keeps ordinary jitter from
        // flickering the animation off and on.
        const float stallMs = std::max(250.0f, front().tickDurMs * 6.0f);
        if (float(nowMs - animAdvanceMs_) > stallMs) return;
        float dt = realDt * animSpeed();
        vmTick_.clear();explosionVmTick_.clear();
        for (auto& [id, a] : anims_) {
            if(a.vm && tak::retailOwnerVmMayAdvance(a.ownerVmStopRequested)) {
                if(a.vm->mayReachExplosion(a.explosionReachability))explosionVmTick_.emplace_back(id,a.vm.get());
                else vmTick_.push_back(a.vm.get());
            }
            a.fireT += dt; a.smokeT += dt;   // age since last emit (fire fades if it stops)

        }
        pool_.parallelFor(vmTick_.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) vmTick_[i]->tick(dt);
        }, /*minParallel=*/1500);   // ~0.2us/VM: pool dispatch only pays off at scale
        std::sort(explosionVmTick_.begin(),explosionVmTick_.end(),
            [](const auto& a,const auto& b){return a.first<b.first;});
        for(const auto& [id,vm]:explosionVmTick_)vm->tick(dt);
        // Drain emit-sfx the VMs stashed (fire/smoke from FireControl-style loops),
        // now serially on the main thread, into the world-space effect system.
        for (auto& [id, a] : anims_) {
            if (a.pendingPoints.empty() && a.pendingSfx.empty() && a.pendingSnd.empty()) continue;
            const auto* u = frameUnitP(id);
            if (u && u->type && (noFog_ || cellVisibleR(u->x, u->z))) {
                for(auto& event:a.pendingPoints) {
                    event.unitId=u->id;event.player=u->player;event.position=u->worldPosition;
                    event.heading=uint16_t(u->headingWord+32768);event.pitch=u->bodyPitch;event.roll=u->bodyRoll;
                    emitPoint(event);
                }
                for (auto& [piece, sfx] : a.pendingSfx) emitSfx(*u, a, piece, sfx);
            }
            if (u && u->type && !a.pendingSnd.empty()) {
                const bool visible=noFog_ || cellVisibleR(u->x,u->z);
                const bool selected=std::find(selection_.begin(),selection_.end(),id)!=selection_.end();
                for (const auto& [index,flags]:a.pendingSnd) {
                    if (!tak::retailUnitSoundAudible(flags,visible,selected)) continue;
                    const auto& name=a.vm->file().name(uint32_t(index));
                    const int priority=tak::retailSoundPriority(flags);
                    if (priority==7) sounds_.play(name,1.f,1.f,priority,(flags&32)!=0);
                    else sounds_.playWorld(name,u->x,u->z,priority);
                }
            }
            a.pendingPoints.clear();
            a.pendingSfx.clear();
            a.pendingSnd.clear();
        }
    }

    float GameView::animSpeed() const {
        if (isNet() && mp_) return std::max(1, int(mp_->gameSpeed())) / 10.0f;
        return speedMult();
    }

    void GameView::update(float dt) {
        if (paused_) return;   // freeze the sim; input/render/camera keep running
        // Game speed: scale game time (sim, effects, AI, animation all follow dt).
        dt *= speedMult();
#ifndef NDEBUG
        if (patrolPerfAccum_ >= 0.0f) {
            patrolPerfAccum_ += dt;
            for (int steps = 0; patrolPerfAccum_ >= 1.0f / 30.0f && steps < 64; ++steps) {
                simStep(1.0f / 30.0f);
                patrolPerfAccum_ -= 1.0f / 30.0f;
                if (world_.tickCount() % 300 == 0) {
                    int alive = 0, moving = 0, displaced = 0;
                    for (const auto& u : world_.units()) {
                        alive += u.alive();
                        moving += u.alive() && u.speed.v > 0;
                        displaced += u.alive() && (u.x != u.homeX || u.z != u.homeZ);
                    }
                    std::printf("PATROL_PERF tick=%u alive=%d moving=%d displaced=%d\n",
                                world_.tickCount(), alive, moving, displaced);
                    std::fflush(stdout);
                }
            }
            cosmeticStep(dt);
            return;
        }
#endif
        simStep(dt);
        cosmeticStep(dt);
    }

    void GameView::prepare(int winW, int winH) {
        if (initialCamera_ && !inLobbyPhase() && !spectating_ && !replayMode_) {
            const auto [x,z]=*initialCamera_;
            float initialZoom=1.25f;
            // Reproducible render profiling through the normal network path.
            // devEnv is disabled in release; ordinary starting views stay fixed.
            if (const char* zoom=tak::devEnv("TAK_PROFILE_ZOOM"))
                initialZoom=std::clamp(float(std::atof(zoom)),0.1f,4.0f);
            mapView_.setZoom(initialZoom);
            mapView_.setOffset(x-terrainLiftX(x,z)-float(mapViewW(winW))*0.5f/mapView_.zoom(),
                               z-terrainLift(x,z)-12.0f-float(winH-barH())*0.5f/mapView_.zoom());
            initialCamera_.reset();
        }

        mapView_.ensureChunks(mapViewW(winW), winH);
        if (!miniTex_) buildMinimap();
        // Listener = the camera view (the map viewport), so positional sounds pan by
        // where the source sits on SCREEN, not by its absolute map position. This
        // runs every frame for SP, net, AND spectator -- update() is skipped on the
        // net/spectator path, so the listener can't live there.
        float zm = std::max(mapView_.zoom(), 1e-3f);
        float halfW = (mapViewW(winW) / 2.0f) / zm, halfH = (winH / 2.0f) / zm;
        sounds_.setListener(mapView_.offX() + halfW, mapView_.offY() + halfH, halfW, halfH);
    }

    void GameView::takeProf(double& projMs, double& submitMs, double& shadowMs,
                            double& simMs, uint64_t& unitsDrawn, uint64_t& shadowVerts) {
        // Deltas since the last call -- the counters themselves are monotonic so that the
        // per-frame TAK_SPIKES logger can take its own independent deltas off them.
        projMs   = profProjMs_   - profProjPrev_;    profProjPrev_   = profProjMs_;
        submitMs = profSubmitMs_ - profSubmitPrev_;  profSubmitPrev_ = profSubmitMs_;
        shadowMs = profShadowMs_ - profShadowPrev_;  profShadowPrev_ = profShadowMs_;
        unitsDrawn  = profUnits_       - profUnitsPrev_;       profUnitsPrev_       = profUnits_;
        shadowVerts = profShadowVerts_ - profShadowVertsPrev_; profShadowVertsPrev_ = profShadowVerts_;
        // profSimTicks_ is the sim WORKER's counter, exchanged (not delta'd): the spike
        // logger never reads it, so it has only the one consumer.
        simMs = double(profSimTicks_.exchange(0)) * 1000.0 / double(SDL_GetPerformanceFrequency());
    }

    void GameView::advance(float seconds) {
        float printed = 0;
        for (float t = 0; t < seconds; t += 1.0f / 30.0f) {
            update(1.0f / 30.0f);
            if (tak::devFlag("TAK_CONJURE_TEST") ||
                tak::devFlag("TAK_PROJECTILE_CAPTURE") || tak::devFlag("TAK_NIMBUS_CAPTURE") ||
                tak::devFlag("TAK_STORM_CAPTURE")) {
                beginFrame();
                cosmeticStep(1.0f / 30.0f);
                animFrame(1.0f / 30.0f);
                endFrame();
            }
            if (const char* phase=tak::devEnv("TAK_STORM_CAPTURE")) {
                for(const auto& storm:world_.storms()) if(int(storm.phase)==std::atoi(phase)) {
                    int width=0,height=0;SDL_GetRendererOutputSize(ren_,&width,&height);
                    mapView_.setOffset(storm.x.toFloat()-float(mapViewW(width))*0.5f/mapView_.zoom(),
                        storm.z.toFloat()-float(height-barH())*0.5f/mapView_.zoom());
                    std::printf("storm capture: tick=%u phase=%d frame=%u at=%.1f,%.1f\n",
                        world_.tickCount(),int(storm.phase),unsigned(storm.animation.frame),
                        storm.x.toFloat(),storm.z.toFloat());
                    paused_=true;return;
                }
            }
            if (tak::devFlag("TAK_NIMBUS_CAPTURE")) {
                for (const auto& [id, effect] : nimbusEffects_)
                    if (front().gameTick - effect.started >= uint32_t(std::max(1,
                            std::atoi(tak::devEnv("TAK_NIMBUS_CAPTURE"))))) {
                        std::printf("nimbus capture: tick=%u caster=%d age=%u\n", front().gameTick,
                            id, front().gameTick - effect.started);
                        paused_ = true;
                        return;
                    }
            }
            if(tak::devFlag("TAK_PROJECTILE_CAPTURE")) {
                const uint32_t captureAge=uint32_t(std::max(1,std::atoi(tak::devEnv("TAK_PROJECTILE_CAPTURE"))));
                for (const auto& shot:world_.flames())
                    if (int32_t(world_.tickCount()-shot.start)>=int32_t(captureAge) && !shot.particles.empty()) {
                        std::printf("flame capture: tick=%u age=%u kind=%d particles=%zu\n",
                            world_.tickCount(),world_.tickCount()-shot.start,
                            shot.weapon ? int(shot.weapon->flameKind) : -1,shot.particles.size());
                        paused_=true;return;
                    }
                for(const auto& shot:world_.projectiles())if(shot.lightningEffect &&
                    shot.age>=std::max(1,std::atoi(tak::devEnv("TAK_PROJECTILE_CAPTURE"))) &&
                    !shot.lightningEffect->particles.empty()) {
                    std::printf("projectile capture: tick=%u age=%d pixels=%zu nimbus=%zu\n",world_.tickCount(),shot.age,
                        shot.lightningEffect->pixels.size(), nimbusEffects_.size());
                    paused_=true; // Keep the selected tick while screenshot terrain uploads finish.
                    return;
                }
            }
            if (trace_ && t >= printed) {
                printed += 0.5f;
                for (auto& u : world_.units())
                    if (u.player == 0 && u.alive()) {
                        std::printf("TRACE %.1f %d %.1f %.1f %.1f\n", t, u.id,
                                    u.x.toFloat(),
                                    (u.type && u.type->canFly ? u.flightY : u.groundY).toFloat(),
                                    u.z.toFloat());
                        if (tak::devFlag("TAK_CONJURE_TEST")) {
                            std::printf("BUILDTRACE tick=%u site=%d orders=%zu events=%x\n",
                                        world_.tickCount(),u.buildSiteId,u.orders.size(),u.missionEvents);
                            for (const auto& o:u.orders)
                                std::printf("  stage=%u pending=%x wait=%x goal=%.0f,%.0f build=%s\n",
                                            unsigned(o.mission.stage),o.mission.pending,o.mission.waitMask,
                                            o.x.toFloat(),o.z.toFloat(),o.buildType?o.buildType->id.c_str():"-");
                            if (const char* name=tak::devEnv("TAK_CONJURE_TARGET"))
                                if (const auto* type=registry_.find(name))
                                    std::printf("  target footprint=%dx%d\n",type->footX,type->footZ);
                        }
                    }
            }
        }
    }

    int GameView::minWindowWidth() const {
        int n = int(registry_.maxBuildMenu());
        int rowW = n > 0 ? (n - 1) * 66 + 60 : 0;
        return rowW + 24 + panelW();
    }

    void GameView::loadDamageFlameClasses() {
        if(damageFlameClassesLoaded_)return;
        damageFlameClassesLoaded_=true;
        for(const auto& path:vfs_.list("gamedata/damageflames")) {
            if(!path.ends_with(".tdf"))continue;
            const auto definitions=vtdf(path);
            for(const auto& [name,node]:definitions.orderedChildren()) {
                const auto key=tak::hpi::MountSet::key(std::string(name));
                const int kind=key=="smallflame" ? 0 : key=="mediumflame" ? 1 : key=="largeflame" ? 2 : -1;
                if(kind<0)continue;
                const auto bank=node->valueOr("gaf","");
                const auto sequence=node->valueOr("anim","");
                if(bank.empty() || sequence.empty())continue;
                if(const auto* art=effectFor(bank+":"+sequence);art && !art->frames.empty())
                    damageFlameClasses_[size_t(kind)].push_back(art);
            }
        }
    }

    void GameView::consumeSmokeTicks() {
        std::deque<SmokeTick> ticks;
        {
            std::lock_guard<std::mutex> lock(hitQueueMutex_);
            while(!smokeTickQueue_.empty() && int32_t(front().gameTick-smokeTickQueue_.front().tick)>=0) {
                ticks.push_back(std::move(smokeTickQueue_.front()));smokeTickQueue_.pop_front();
            }
        }
        const auto random=[&] {return tak::sim::retailCrtRandom(smokeRandom_);};
        for(const auto& tick:ticks) {
            if(tick.tick<=smokeTick_) {smokeSprites_.clear();featureSmokeSprites_.clear();damageFlames_.clear();pointParticles_.clear();smokeRandom_=1;}
            smokeTick_=tick.tick;
            for(int owner:tick.removedOwners) {
                smokeSprites_.erase(owner);damageFlames_.erase(owner);pointParticles_.erase(owner);
            }
            for(int id:tick.removedFeatures)featureSmokeSprites_.erase(id);
            const auto updateFeatureSmoke=[&](const FeatureSmokeEmission& event) {
                const auto found=featureSmokeSprites_.find(event.id);
                if(found==featureSmokeSprites_.end())return;
                auto& sprites=found->second;
                std::erase_if(sprites,[&](auto& sprite) {
                    return !sprite.particle.tick(tick.windX,tick.windZ,8155,random);
                });
                if(sprites.empty())featureSmokeSprites_.erase(found);
            };
            const auto emitFeatureSmoke=[&](const FeatureSmokeEmission& event) {
                const auto definition=featureDefs_.find(event.type);
                if(definition==featureDefs_.end())return;
                const auto* body=featureArtFor(definition->second,"seqnameburn","seqnameburnshad");
                if(!body || body->fgeom.empty() || body->totalTicks<=0)return;
                const int age=int(event.age%uint32_t(body->totalTicks));
                const size_t index=size_t(std::upper_bound(body->tickEnd.begin(),body->tickEnd.end(),age)-body->tickEnd.begin());
                if(index>=body->fgeom.size())return;
                const auto& frame=body->fgeom[index];
                const int dx=frame.w/4+int(uint64_t(random())*unsigned(frame.w/2)/32768)-frame.xoff;
                const int dy=2*(frame.yoff-int(uint64_t(random())*unsigned(frame.h/2)/32768)-frame.h/4);
                auto& sprites=featureSmokeSprites_[event.id];
                if(sprites.size()>=10)return;
                const auto* art=effectFor("bigsmoke");
                if(!art || art->frames.size()<3)return;
                tak::RetailSmokeParticle particle;particle.position=event.position;
                particle.position[0]=std::bit_cast<int32_t>(uint32_t(particle.position[0])+(uint32_t(dx)<<16));
                particle.position[1]=std::bit_cast<int32_t>(uint32_t(particle.position[1])+(uint32_t(dy)<<16));
                particle.frameLimit=2+uint32_t(uint64_t(random())*(art->frames.size()-3)/32768);
                sprites.push_back({particle,art});
            };
            tak::retailStepFeatureSmoke(tick.features,updateFeatureSmoke,emitFeatureSmoke);
            const auto& terrain=mapView_.map();
            for(auto it=pointParticles_.begin();it!=pointParticles_.end();) {
                std::erase_if(it->second,[&](auto& particle) {
                    return !particle.tick(int(uint8_t(terrain.seaLevel)),[&](const auto& position) {
                        return terrain.heights.empty() ? -1 : tak::sim::retailTerrainHeight(
                            position[0],position[2],terrain.width,terrain.height,[&](int x,int z) {
                                return terrain.heights[size_t(z)*size_t(terrain.width)+size_t(x)];
                            });
                    });
                });
                if(it->second.empty())it=pointParticles_.erase(it);else ++it;
            }
            for(auto it=damageFlames_.begin();it!=damageFlames_.end();) {
                std::erase_if(it->second,[](auto& sprite) {
                    if(--sprite.life<=0)return true;
                    sprite.clock.tick(sprite.art->durations,sprite.art->loop);
                    return !sprite.clock.active;
                });
                if(it->second.empty())it=damageFlames_.erase(it);else ++it;
            }
            for(auto it=smokeSprites_.begin();it!=smokeSprites_.end();) {
                std::erase_if(it->second,[&](auto& sprite) {
                    return !sprite.particle.tick(tick.windX,tick.windZ,8155,random);
                });
                if(it->second.empty())it=smokeSprites_.erase(it);else ++it;
            }
            for(const auto& event:tick.emissions) {
                if(event.code>=2 && event.code<=5) {
                    emitPoint(event);
                    continue;
                }

                if(event.code==263 || event.code==264) {
                    const auto& point=event.position;
                    spawnEffectAnim(event.code==263 ? "deathmagic:purpledeath" : "pillaroflight",
                        float(point[0])/65536.f,float(point[2])/65536.f,0,0,1,0,true,point,event.tick);
                    continue;
                }
                if(event.code>=260 && event.code<=262) {
                    loadDamageFlameClasses();
                    auto& sprites=damageFlames_[event.unitId];
                    const auto& variants=damageFlameClasses_[size_t(event.code-260)];
                    if(sprites.size()>=40 || variants.empty())continue;
                    const auto* art=variants[size_t(uint64_t(random())*variants.size()/32768)];
                    DamageFlameSprite sprite; sprite.position=event.position;sprite.art=art;
                    sprite.clock.start(art->durations);sprites.push_back(sprite);
                    continue;
                }
                auto& sprites=smokeSprites_[event.unitId];
                if(sprites.size()>=10)continue;
                const char* name=event.code==265 ? "steam" : event.code==258 ? "smoke:smoke01" : "bigsmoke";
                const auto* art=effectFor(name);
                if(!art || art->frames.size()<3)continue;
                tak::RetailSmokeParticle particle;
                particle.position=event.position;particle.small=event.code==258;particle.steam=event.code==265;
                particle.frameLimit=2+uint32_t(uint64_t(random())*(art->frames.size()-3)/32768);
                sprites.push_back({particle,art});
            }
        }
    }

    void GameView::emitPoint(const tak::sim::World::ScriptEmission& event) {
        auto& particles=pointParticles_[event.unitId];
        if(particles.size()>=100 || event.pose.empty())return;
        std::array<std::array<int32_t,3>,2> endpoints;
        for(size_t i=0;i<2;++i) {
            const auto point=modelEmissionPoint(event.pose,event.vertices[i],
                event.heading,event.pitch,event.roll);
            for(size_t axis=0;axis<3;++axis) {
                const auto offset=modelEmissionFixed(point[axis]);
                endpoints[i][axis]=std::bit_cast<int32_t>(uint32_t(event.position[axis])+
                    (axis==2 ? 0u-uint32_t(offset) : uint32_t(offset)));
            }
        }
        if(event.code>=4)std::swap(endpoints[0],endpoints[1]);
        particles.push_back(tak::RetailPointParticle::emit(endpoints[0],endpoints[1],
            event.code%2 ? 8:16,[&] {return tak::sim::retailCrtRandom(smokeRandom_);}));
    }

    void GameView::emitSfx(const UnitR& u, Anim& a, int piece, int32_t sfx) {
        // These emissions come from the simulation instruction stream, not the
        // independently paced display VM.
        if(sfx==257 || sfx==258 || sfx==265 || (sfx>=260 && sfx<=262))return;
        const char* anim = sfxAnimFor(sfx);
        if (!anim) return;
        const EffectAnim* ea = effectFor(anim);
        if (!ea || ea->frames.empty()) return;
        // Refresh this unit's persistent flame/smoke; drawUnitFx cycles it smoothly.
        if (anim[0] == 's') { a.smokeFx = ea; a.smokeT = 0; a.smokePiece = piece; }
        else                { a.fireFx = ea;  a.fireT = 0;  a.firePiece = piece; }
    }

    bool GameView::explodePiece(const UnitR& u, Anim& a, int piece, int32_t flags) {
        bool accepted=false;
        float x, z, dAlt;
        scriptEffectOrigin(u, a, piece, x, z, dAlt);
        if(!(flags&0x20) && a.pieceNames && piece>=0 &&
           size_t(piece)<a.pieceNames->size()) {
            const auto visualName=unitType_.find(u.id);
            // An immediate corpse handoff may already have swapped the body
            // model. EXPLODE still detaches pieces from the original live model.
            const auto visual=u.corpsePhase && u.type
                ? visuals_.find(u.veteran>=10 && !u.type->veteranModel.empty()
                    ? u.type->veteranModel : u.type->id)
                : (visualName==unitType_.end()?visuals_.end():visuals_.find(visualName->second));
            if(visual!=visuals_.end()) {
                const auto& wanted=(*a.pieceNames)[size_t(piece)];
                const auto find=[&](auto&& self,const tak::tdo::Object& object)->const tak::tdo::Object* {
                    if(tak::hpi::MountSet::key(object.name)==wanted)return &object;
                    for(const auto& child:object.children)if(const auto* found=self(self,child))return found;
                    return nullptr;
                };
                if(const auto* source=find(find,visual->second.model.root)) {
                    tak::RetailDebrisMotion motion;
                    tak::RetailDebrisMotion::launch(uint32_t(flags),[&](unsigned bound) {
                        return unsigned(uint64_t(tak::sim::retailCrtRandom(smokeRandom_))*bound/32768u);
                    },motion);
                    if(detachedPieces_.size()<100) {
                        DetachedPiece debris;
                        debris.model=tak::retailDebrisModel(*source,(flags&64)!=0);
                        debris.names=*a.pieceNames;
                        debris.poses.assign(a.capturedPose.begin(),a.capturedPose.end());
                        debris.motion=motion;debris.motion.position=u.worldPosition;
                        if(size_t(piece)<debris.poses.size())for(unsigned axis=0;axis<3;++axis)
                            debris.motion.rotation[axis]=uint16_t(int32_t(std::lround(
                                debris.poses[size_t(piece)].rot[axis]*65536.f/6.28318530717959f)));
                        debris.tick=front().gameTick;debris.player=u.player;
                        if(a.effectQueryVm && u.type) {
                            a.effectQueryVm->call("QueryBlood",{-1});
                            const auto& output=a.effectQueryVm->lastLocals();
                            if(!output.empty() && output[0]!=-1)
                                for(unsigned i=0;i<100;++i)
                                    debris.blood.push_back(tak::RetailBloodParticle::emit(
                                        u.worldPosition,u.type->bloodColors,
                                        [&]{return tak::sim::retailCrtRandom(smokeRandom_);}));
                        }
                        detachedPieces_.push_back(std::move(debris));
                        accepted=true;
                    }
                }
            }
        }
        // Retail retains the legacy SMOKE/FIRE bits in the debris descriptor,
        // but they create neither immediate bursts nor attached emitters here.
        // The native attached blood emitter is gated separately by its script.
        loadExplosionClasses();
        // Native 50dd20 requests each set class bit independently. The first
        // nine entries in explosions.tdf are the script's numeric class table.
        for(size_t cls=0;cls<9 && cls<explosionClassOrder_.size();++cls)
            if(uint32_t(flags)&(0x100u<<cls)) {
                spawnEffect(explosionClassOrder_[cls],x,z,dAlt);
                if(cls<4)explosionGlows_.push_back({x,z,dAlt,front().gameTick,unsigned(std::min(cls,size_t(2)))});
            }
        return accepted;
    }

    // Corpse mesh: once the death anim finishes, the body draws as the FBI
    // corpse feature's `object=` model (arasword_dead etc.) through the normal
    // unit pipeline -- same swap trick as the veteran meshes. The old VM's piece
    // names don't match the corpse skeleton, so it renders in rest pose.
    void GameView::maybeSwapCorpseModel(const UnitR& u) {
        if (!u.type) return;
        int ct = u.corpseFeat;   // corpse OR statue def, resolved by the sim
        if (ct < 0) return;
        const std::string& obj = world_.featureTypes()[size_t(ct)].object;
        if (obj.empty()) return;
        auto it = unitType_.find(u.id);
        if (it == unitType_.end() || it->second == obj) return;
        if (!visuals_.count(obj)) {
            try {
                // Static ruins can put their entire mesh in the root object.
                // Only its selection polygon is a ground reference plate.
                loadVisual(obj, false);
            } catch (const std::exception&) { return; }   // no corpse mesh: keep pose
        }
        it->second = obj;
    }

    void GameView::maybeSwapVeteranModel(const UnitR& u) {
        if (!u.type || u.veteran < 10 || u.type->veteranModel.empty()) return;
        const std::string& vm = u.type->veteranModel;
        auto it = unitType_.find(u.id);
        if (it == unitType_.end() || it->second == vm) return;   // not drawn yet / done
        if (!visuals_.count(vm)) {
            try {
                loadVisual(vm);   // with its PieceMeta tree -- see loadVisual
            } catch (const std::exception&) { return; }   // no promoted mesh: keep base
        }
        it->second = vm;   // draw the promoted mesh from now on
        if(auto anim=anims_.find(u.id);anim!=anims_.end())
            configureExplosionHierarchy(anim->second,visuals_.at(vm).model.root);
    }

    void GameView::configureExplosionHierarchy(Anim& a,const tak::tdo::Object& root) {
        if(!a.vm)return;
        std::vector<std::vector<int>> descendants(a.vm->file().pieces.size());
        const auto visit=[&](auto&& self,const tak::tdo::Object& object,std::vector<int> ancestors)->void {
            const int index= [&] {
                const auto name=tak::hpi::MountSet::key(object.name);
                for(size_t i=0;i<a.vm->file().pieces.size();++i)
                    if(tak::hpi::MountSet::key(a.vm->file().pieces[i])==name)return int(i);
                return -1;
            }();
            if(index>=0) {
                for(int parent:ancestors)descendants[size_t(parent)].push_back(index);
                ancestors.push_back(index);
            }
            for(const auto& child:object.children)self(self,child,ancestors);
        };
        visit(visit,root,{});
        a.vm->setExplosionDescendants(std::move(descendants));
    }

    void GameView::registerUnit(int id, const tak::sim::UnitType* type) {
        const std::string& typeId = type->id;
        if (!visuals_.count(typeId)) {
            try {
                loadVisual(typeId);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "no model for %s: %s\n", typeId.c_str(), e.what());
                return;
            }
        }
        Anim a;
        try {
            // Per-type COB cache: parse the script once and share the immutable
            // bytecode across every unit of the type (each Vm previously owned a
            // full copy -- 60-140KB of code words per unit in a big army -- and
            // every spawn re-read + re-parsed the file).
            auto ci = cobCache_.find(typeId);
            if (ci == cobCache_.end()) {
                std::string cobPath = "scripts/" + typeId + ".cob";
                CobCache cc;
                cc.file = std::make_shared<const tak::cob::File>(
                    tak::cob::load(vread(cobPath), cobPath));
                cc.explosionReachability=tak::cob::explosionReachability(*cc.file);
                for (const auto& p : cc.file->pieces) {
                    std::string n = p;
                    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                    cc.pieceNames.push_back(n);
                }
                cc.pieceVertices.resize(cc.pieceNames.size());
                auto appendModel=[&](auto&& self,const tak::tdo::Object& object,int parent)->void {
                    auto name=object.name;
                    std::transform(name.begin(),name.end(),name.begin(),::tolower);
                    auto found=std::find(cc.pieceNames.begin(),cc.pieceNames.end(),name);
                    const int piece=found==cc.pieceNames.end() ? -1 : int(found-cc.pieceNames.begin());
                    const int index=int(cc.modelPieces.size());
                    cc.modelPieces.push_back({object.offsetRaw,parent,piece});
                    if(piece>=0) {
                        auto& vertices=cc.pieceVertices[size_t(piece)];
                        vertices=object.verticesRaw;
                        // 4dd2a0 uses these mirrored authored vertices directly:
                        // SweetSpot ignores piece offsets, animation and heading.
                        for(auto& vertex:vertices) for(int axis:{0,2})
                            vertex[size_t(axis)]=std::bit_cast<int32_t>(0u-uint32_t(vertex[size_t(axis)]));
                    }
                    for(const auto& child:object.children)self(self,child,index);
                };
                appendModel(appendModel,visuals_.at(typeId).model.root,-1);
                cc.hasAim = cc.file->scriptIndex("AimWeapon") >= 0;
                cc.hasCloakAnim = cc.file->scriptIndex("StartCloaking") >= 0;
                cc.hasFlinch = cc.file->scriptIndex("HitByWeapon") >= 0;
                cc.hasWind = cc.file->scriptIndex("WindChange") >= 0;
                cc.hasFlightSM = cc.file->scriptIndex("BeginFlight") >= 0;
                cc.hasActivate = cc.file->scriptIndex("Activate") >= 0;
                cc.hasTurnDir = cc.file->scriptIndex("TurnDirection") >= 0;
                ci = cobCache_.emplace(typeId, std::move(cc)).first;
            }
            a.pieceNames = &ci->second.pieceNames;
            a.hasCloakAnim = ci->second.hasCloakAnim;
            a.hasAim = ci->second.hasAim;
            a.hasFlinch = ci->second.hasFlinch;
            a.hasWind = ci->second.hasWind;
            a.hasFlightSM = ci->second.hasFlightSM;
            a.hasActivate = ci->second.hasActivate;
            a.hasTurnDir = ci->second.hasTurnDir;
            // Gate Create leaves the doors closed; replay an active snapshot's
            // Activate even when the gate became active before it was visible.
            // Other on/off units retain their authored initial state.
            a.active = type && type->gate ? false : (type ? type->activateWhenBuilt : true);
            a.vm = std::make_unique<tak::cob::Vm>(ci->second.file);
            a.vm->enableRetailAnimation();
            a.explosionReachability=ci->second.explosionReachability;
            configureExplosionHierarchy(a,visuals_.at(typeId).model.root);
            // TA COB unit-state queries answered from the sim.
            int unitId = id;
            a.vm->onGet = [this, unitId](int32_t valId,
                                         const std::vector<int32_t>&) -> int32_t {
                // Ticked on the worker pool (animFrame) -- read the pinned render snapshot,
                // never live world_, which the sim thread mutates concurrently under Stage B.
                const auto* su = frameUnitP(unitId);
                if (!su || !su->type) return 0;
                // TAK's GET_UNIT_VALUE numbering (icd getter table @0x50d394) --
                // NOT TA-1997's. The old TA-numbered cases here answered ids no
                // shipped TAK script asks for, while id 4 (HEALTH%) returned 0 --
                // making every building COB's SmokeControl/DamageFlameControl
                // believe it was at 0 HP and belch damage smoke from spawn.
                switch (valId) {
                    case 1:  return su->buildQueue.empty() ? 0 : 1;   // ACTIVATION
                    case 4:  return su->scriptHealthPercent;  // HEALTH %
                    case 6:  return su->moving() ? 1 : 0;             // BUSY
                    case 9:  {                                        // UNIT_XZ
                        int32_t x = int32_t(su->x) & 0xFFFF;
                        int32_t z = int32_t(su->z) & 0xFFFF;
                        return (x << 16) | z;
                    }
                    case 17: return su->constructionPercentLeft;
                    case 27:                                           // HEADING (16-bit angle)
                        return tak::sim::portHeadingToRetail(tak::sim::bamFromRadians(su->heading));
                    // Native GET 28/34 report mover flags. Recomputing from
                    // the display map disagrees at shore/road transitions and
                    // for bodies whose support height differs from one cell.
                    case 28: return (su->movementTerrainFlags&0x1000)!=0;
                    case 34: return (su->movementTerrainFlags&0x800)!=0;
                    case 29:                                           // CURRENT_SPEED (% of max:
                        return su->animationSpeedPercent();           // repeated refusal reports zero
                    case 32: return su->veteran;                       // VETERAN LEVEL (StatusControl
                                                                       // swaps golden weapon pieces)
                    // Reflect the authoritative yard handshake, including a
                    // blocked transition and a successful close (zero).
                    case 18: return su->yardOpen;
                    case 30: return su->verticalSpeedPercent;
                    case 33: return su->turnSpeedPercent; // native signed percentage
                    case 46: return su->standingOrder; // native standing unit order (holstering)
                    default: return 0;
                }
            };
            a.flying = type && type->canFly;
        } catch (const std::exception&) { /* unit stays unanimated */ }
        if (a.vm) {
            Anim& st = anims_[id] = std::move(a);
            // The VM is ticked on the worker pool, so emit-sfx only stashes into this
            // unit's own buffer (std::map nodes are pointer-stable); the main thread
            // drains it into effects_ after the parallel tick.
            st.vm->onEmitSfx = [buf = &st.pendingSfx,
                    points=&st.pendingPoints,vm=st.vm.get(),cache=&cobCache_.at(typeId),
                    flying=st.flying](int piece, int32_t sfx) {
                // Attached death flames now come from the same authoritative
                // instruction stream as live emissions, exactly once per tick.
                if(sfx>=260 && sfx<=262)return;
                if(!flying && sfx>=2 && sfx<=5) {
                    if(piece<0 || size_t(piece)>=cache->pieceVertices.size() ||
                       cache->pieceVertices[size_t(piece)].size()<2)return;
                    tak::sim::World::ScriptEmission event;event.code=sfx;event.piece=piece;
                    std::copy_n(cache->pieceVertices[size_t(piece)].begin(),2,event.vertices.begin());
                    int index=-1;
                    for(size_t i=0;i<cache->modelPieces.size();++i)
                        if(cache->modelPieces[i].scriptPiece==piece) {index=int(i);break;}
                    const auto states=vm->retailPieces();
                    for(;index>=0;index=cache->modelPieces[size_t(index)].parent) {
                        const auto& node=cache->modelPieces[size_t(index)];
                        tak::cob::EmissionPose pose;pose.offset=node.offset;
                        if(node.scriptPiece>=0 && size_t(node.scriptPiece)<states.size())
                            for(size_t axis=0;axis<3;++axis) {
                                pose.move[axis]=states[size_t(node.scriptPiece)].move[axis];
                                pose.turn[axis]=uint16_t(states[size_t(node.scriptPiece)].turn[axis]);
                            }
                        event.pose.push_back(pose);
                    }
                    std::reverse(event.pose.begin(),event.pose.end());
                    points->push_back(std::move(event));return;
                }
                buf->push_back({piece, sfx});
            };
            st.vm->onPlaySound = [buf = &st.pendingSnd](int32_t idx,int32_t flags) {
                buf->emplace_back(idx,flags);
            };
            st.vm->onSetUnitValue = [state=&st](int32_t valueId,int32_t) {
                if(tak::retailOwnerVmStopsOnSetUnitValue(valueId)) {
                    state->ownerVmStopRequested=true;
                }
            };
            st.vm->onExplode = [this,id, state=&st, vm=st.vm.get()](int piece, int32_t flags) {
                const auto* unit=frameUnitP(id);
                if(!unit || !unit->type || (!noFog_ && !cellVisibleR(unit->x,unit->z)))return false;
                std::vector<tak::cob::PieceState> pose;
                const auto states=vm->retailPieces();
                pose.resize(states.size());
                constexpr float angle=2*3.14159265358979f/65536.0f;
                for(size_t i=0;i<states.size();++i) {
                    pose[i].visible=states[i].visible;
                    pose[i].cached=states[i].cached;
                    pose[i].shaded=states[i].shaded;
                    pose[i].rendered=states[i].rendered;
                    for(size_t axis=0;axis<3;++axis) {
                        pose[i].move[axis]=float(states[i].move[axis])/65536.0f;
                        pose[i].rot[axis]=float(states[i].turn[axis])*angle;
                    }
                }
                Anim snapshot;snapshot.pieceNames=state->pieceNames;
                snapshot.capturedPose=pose;snapshot.effectQueryVm=vm;
                return explodePiece(*unit,snapshot,piece,flags);
            };
            unitType_[id]=typeId;
            // Install effect sinks before immediate retail notifications run.
            st.vm->start("Create");
            st.vm->start("SetMaxReloadTime",{type->maxWeaponReloadMs});
        }
        unitType_[id] = typeId;
    }

    int GameView::spawn(const std::string& typeId, float x, float z, float heading, int player) {
        const auto* type = registry_.find(typeId);
        if (!type) return -1;
        int id = world_.spawn(type, x, z, heading, player);
        // registerUnit mutates the client render maps (visuals_/cobCache_/anims_/unitType_),
        // which the render thread + the animFrame VM pool read/iterate. It must run ONLY on
        // the main thread. When spawn() is reached from the SIM WORKER (summonGod / mission
        // scripted reinforcements, all inside simStep), skip it: the main thread's
        // cosmeticStep lazily registers every live snapshot unit, so it is redundant there.
        if (std::this_thread::get_id() == mainThreadId_) {
            registerUnit(id, type);
            if (!unitType_.count(id)) return -1;
        }
        return id;
    }

    void GameView::loadTextures() {
        // Faction texture banks use their own palettes (palettes/<side>_textures.pcx).
        // The VFS merges base + Iron Plague (cre) texture GAFs into one namespace.
        std::map<std::string, tak::gaf::Palette> pals;
        for (const char* side : {"ara", "tar", "ver", "zon", "aid", "cre"}) {
            std::string pp = std::string("palettes/") + side + "_textures.pcx";
            try { if (vfs_.has(pp)) pals[side] = tak::gaf::Palette::fromBytes(vread(pp), pp); }
            catch (const std::exception&) {}
        }
        if (!pals.count("ara")) return;   // no palettes available
        for (const std::string& path : vfs_.list("textures")) {
            if (tak::vpath::extension(path) != ".gaf") continue;
            std::string stem = tak::vpath::stem(path);
            std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
            const auto* pal = &pals.at("ara");
            auto pit = pals.find(stem.substr(0, 3));
            if (pit != pals.end()) pal = &pit->second;
            try {
                const auto bytes=vread(path);
                // Decode through an index-identity palette as well: palette colours
                // are not unique, so reconstructing indices from RGB loses authored
                // shade mappings. This also preserves composite-frame transparency.
                tak::gaf::Palette indexPalette{};
                for(int i=0;i<256;++i) {
                    indexPalette.rgba[i][0]=uint8_t(i);
                    indexPalette.rgba[i][3]=255;
                }
                auto indexed=tak::gaf::load(bytes,indexPalette,5,path);
                const std::string bank=pit!=pals.end() ? pit->first : "ara";
                std::vector<uint8_t> shades;
                const std::string shadePath="palettes/"+bank+"_textures.shd";
                if(vfs_.has(shadePath)) shades=vread(shadePath);
                size_t sequenceIndex=0;
                for (auto& seq : tak::gaf::load(bytes, *pal, 5, path)) {
                    auto& indexSequence=indexed.at(sequenceIndex++);
                    if (seq.frames.empty()) continue;
                    std::string name = seq.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (textures_.count(name)) continue;
                    if(shades.size()==32*256 && std::all_of(seq.frames.begin(),seq.frames.end(),
                        [](const auto& frame){return frame.encoding==0 || frame.encoding==1;})) {
                        paletteTextureSources_.emplace(name,PaletteTextureSource{
                            *pal,shades,std::move(indexSequence.frames)});
                    }
                    // Retail 4be8e3..4be978 retains every multi-frame sequence.
                    // Only ten-frame names containing "logo" select player colours;
                    // every other multi-frame model texture registers an animation.
                    const size_t n = seq.frames.size();
                    const bool playerColours = n == 10 && name.find("logo") != std::string::npos;
                    if (n > 1 && !playerColours) {
                        animatedTex_.insert(name);
                        auto& animation = modelTextureAnimations_[name];
                        animation.loop = seq.loopFlag != 0;
                        for (const auto& frame : seq.frames)
                            animation.durations.push_back(frame.retailDelayTicks);
                    }
                    // Sample the actual 10 player-colour RGBs once, from a logo/
                    // insignia texture (mostly pure player colour), so the HUD and
                    // minimap can match whatever colour a player renders in.
                    if (!sampledColors_ && n == 10 && !animatedTex_.count(name) &&
                        name.find("logo") != std::string::npos) {
                        for (size_t i = 0; i < 10; ++i) {
                            const auto& f = seq.frames[i];
                            // Saturation-weighted average: the pure player-colour
                            // pixels dominate, grey shading/outlines contribute little.
                            double r = 0, g = 0, b = 0, wsum = 0;
                            for (size_t k = 0; k + 3 < f.rgba.size(); k += 4) {
                                if (f.rgba[k + 3] < 128) continue;
                                int R = f.rgba[k], G = f.rgba[k + 1], B = f.rgba[k + 2];
                                int mx = std::max({R, G, B}), mn = std::min({R, G, B});
                                if (mx < 45) continue;                 // skip outlines
                                double w = double(mx - mn) + 4.0;      // ~saturation
                                w *= w;                                // emphasise colour
                                r += R * w; g += G * w; b += B * w; wsum += w;
                            }
                            if (wsum > 0) {
                                // Brighten a touch so a swatch reads clearly.
                                auto up = [](double v) {
                                    return Uint8(std::min(255.0, v * 1.25));
                                };
                                playerColors_[i] = {up(r / wsum), up(g / wsum),
                                                    up(b / wsum), 255};
                            }
                        }
                        sampledColors_ = true;
                    }
                    std::vector<SDL_Texture*> frames;
                    std::vector<ShadowMaskFrame> masks(n);
                    bool hasMask=false;
                    for (size_t i = 0; i < n; ++i) {
                        auto& f = seq.frames[i];
                        if (f.width == 0 || f.height == 0) break;
                        SDL_Texture* t = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32,
                                                           SDL_TEXTUREACCESS_STATIC,
                                                           f.width, f.height);
                        SDL_UpdateTexture(t, nullptr, f.rgba.data(), f.width * 4);
                        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                        frames.push_back(t);
                        bool cutout=false;
                        for (size_t k=3;k<f.rgba.size();k+=4)
                            if (f.rgba[k]!=255) { cutout=true; break; }
                        if (cutout) {
                            // MOD leaves white texels untouched; opaque texels
                            // multiply the ground by the usual shadow level.
                            masks[i]=addShadowMask(f.rgba,f.width,f.height);
                            hasMask |= masks[i].texture!=nullptr;
                        }
                    }
                    if (hasMask) shadowMasks_[name]=std::move(masks);
                    if (!frames.empty()) textures_[name] = std::move(frames);
                }
            } catch (const std::exception&) {}
        }
    }

    GameView::ShadowMaskFrame GameView::addShadowMask(std::span<const uint8_t> rgba,
                                                      int width,int height) {
        SDL_RendererInfo info{};SDL_GetRendererInfo(ren_,&info);
        const auto makeCoverage=[&](int w,int h) -> SDL_Texture* {
            if(!(info.flags&SDL_RENDERER_ACCELERATED) || !SDL_RenderTargetSupported(ren_))return nullptr;
            constexpr size_t limit=16u*1024u*1024u;
            size_t used=0;
            for(const auto& page:shadowMaskPages_)
                if(page.coverage)used+=size_t(page.width)*page.height*4;
            const size_t bytes=size_t(w)*h*4;
            if(bytes>limit || used>limit-bytes || gpuvram::blocked() || !gpuvram::wouldFit(bytes))return nullptr;
            auto* texture=gpuvram::create(ren_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,w,h);
            if(texture)SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
            return texture;
        };
        const auto standalone=[&]() -> ShadowMaskFrame {
            auto* texture=gpuvram::create(ren_,SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STATIC,width,height);
            if(!texture)return {};
            const auto pixels=tak::shadowMaskPixels(rgba,kShadowLevel);
            if(SDL_UpdateTexture(texture,nullptr,pixels.data(),width*4)!=0) {
                gpuvram::destroy(texture);return {};
            }
            SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_MOD);
            // A full page cannot receive later atlas frames. Ownership remains
            // common to both paths so GPU reset destroys every texture once.
            auto* coverage=makeCoverage(width,height);
            if(coverage) {
                const auto alpha=tak::shadowCoveragePixels(rgba);
                if(SDL_UpdateTexture(coverage,nullptr,alpha.data(),width*4)!=0) {
                    gpuvram::destroy(coverage);coverage=nullptr;
                } else shadowCoverageTextures_[texture]=coverage;
            }
            shadowMaskPages_.push_back({texture,width,height,width,height,0,coverage});
            return {texture,{0,0,1,1}};
        };
        // Keep the software rasterizer's existing standalone sampling path;
        // its batching/sampling behavior differs from the accelerated backend.
        if(!(info.flags&SDL_RENDERER_ACCELERATED))return standalone();
        const int paddedWidth=width+2,paddedHeight=height+2;
        ShadowMaskPage* page=nullptr;
        for(auto& candidate:shadowMaskPages_) {
            if(candidate.x+paddedWidth>candidate.width) {
                candidate.x=0;candidate.y+=candidate.rowHeight;candidate.rowHeight=0;
            }
            if(candidate.y+paddedHeight<=candidate.height && paddedWidth<=candidate.width) {
                page=&candidate;break;
            }
        }
        if(!page) {
            const int maxWidth=info.max_texture_width>0 ? info.max_texture_width : 1024;
            const int maxHeight=info.max_texture_height>0 ? info.max_texture_height : 1024;
            if(paddedWidth>maxWidth || paddedHeight>maxHeight)return standalone();
            const int pageWidth=std::max(paddedWidth,std::min(1024,maxWidth));
            const int pageHeight=std::max(paddedHeight,std::min(1024,maxHeight));
            auto* texture=gpuvram::create(ren_,SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STATIC,pageWidth,pageHeight);
            if(!texture)return standalone();
            SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_MOD);
            auto* coverage=makeCoverage(pageWidth,pageHeight);
            if(coverage)shadowCoverageTextures_[texture]=coverage;
            shadowMaskPages_.push_back({texture,pageWidth,pageHeight,0,0,0,coverage});
            page=&shadowMaskPages_.back();
        }
        // Replicate a one-texel border: UV endpoints retain the standalone
        // texture's clamp behavior instead of sampling its atlas neighbor.
        const auto pixels=tak::paddedShadowMaskPixels(rgba,width,height,kShadowLevel);
        const SDL_Rect rect{page->x,page->y,paddedWidth,paddedHeight};
        if(SDL_UpdateTexture(page->texture,&rect,pixels.data(),paddedWidth*4)!=0)return standalone();
        if(page->coverage) {
            const auto alpha=tak::paddedShadowCoveragePixels(rgba,width,height);
            if(SDL_UpdateTexture(page->coverage,&rect,alpha.data(),paddedWidth*4)!=0) {
                shadowCoverageTextures_.erase(page->texture);
                gpuvram::destroy(page->coverage);page->coverage=nullptr;
            }
        }
        ShadowMaskFrame frame{page->texture,{float(page->x+1)/page->width,
            float(page->y+1)/page->height,float(width)/page->width,float(height)/page->height}};
        page->x+=paddedWidth;page->rowHeight=std::max(page->rowHeight,paddedHeight);
        return frame;
    }

    const tak::cob::PieceState* GameView::pieceFor(const Anim* a, const std::string& objName) const {
        if (!a || !a->pieceNames) return nullptr;
        const auto poses=!a->capturedPose.empty() ? a->capturedPose :
            a->vm ? std::span<const tak::cob::PieceState>(a->vm->pieces()) :
                    std::span<const tak::cob::PieceState>{};
        std::string n = objName;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        for (size_t i = 0; i < a->pieceNames->size(); ++i)
            if ((*a->pieceNames)[i] == n && i<poses.size()) return &poses[i];
        return nullptr;
    }



    bool GameView::dancing(const UnitR& u) const {
        return u.disco && u.type && !u.type->isStructure() && frameDiscoActive(u.player);
    }

    bool GameView::headbanging(const UnitR& u) const {
        return u.headbang && u.type && !u.type->isStructure() && frameHeadbangActive(u.player);
    }

    tak::tdf::Node GameView::vtdf(const std::string& p) const {
        auto b = vfs_.read(p);
        return tak::tdf::parseText(std::string(b.begin(), b.end()), p);
    }

    std::string GameView::mapSibling(const char* ext) const {
        return tak::vpath::replaceExtension(mapPath_,ext);
    }

    void GameView::remountPolicy(uint8_t p) {
        auto pol = tak::hpi::OverridePolicy(p <= 2 ? p : 2);
        if (installRoot_.empty()) return;
        policy_ = pol;
        if(mp_) {
            const bool host=mp_->room().hostId==mp_->myClientId();
            const auto localPolicy=pol==tak::hpi::OverridePolicy::None
                ? tak::hpi::OverridePolicy::None : tak::hpi::OverridePolicy::Cosmetic;
            const auto local=host?mp_->hostOverridePacks():settings_?settings_->cosmeticOverridePacks:std::vector<std::string>{};
            vfs_=mp_->overrideVfs(localPolicy,local);
        } else vfs_ = tak::hpi::mountRetailRoot(std::filesystem::u8path(installRoot_), pol,settings_?settings_->hostOverridePacks:std::vector<std::string>{});
        // Lobby choices are applied before match setup; discard art/audio cached
        // while the lobby still used the previous data set.
        destroyGpuTextures();anims_.clear();cobCache_.clear();visuals_.clear();
        atlasRect_.clear();atlasLaidOut_=false;modelTextureAnimations_.clear();animatedTex_.clear();
        hitBoxes_.clear();ringBoxes_.clear();kingdomPals_.clear();
        featureDefs_.clear();featurePals_.clear();cursorsInit_=false;
        loadTextures();loadOrderButtons();
        loadInterfaceFonts();
        sounds_.reload(vfs_);soundClasses_.load(vfs_);musicMode_=0;
        registry_ = tak::sim::TypeRegistry{};
        tak::sim::setupRegistry(registry_, vfs_, crusades_);
    }

    float GameView::birthProgress(int id) const {
        auto it = birthFx_.find(id);
        return it == birthFx_.end() ? 1.0f
                                    : std::clamp(it->second / kBirthFxDur, 0.0f, 1.0f);
    }

    float GameView::speedMult() const {
        return mp_ ? 1.0f : std::pow(10.0f, float(gameSpeed_) / 10.0f);
    }

    SDL_Color GameView::playerColor(int player) const {
        int s = playerColorSlot(player);
        return playerColors_[(s >= 0 && s < 10) ? s : 0];
    }

    // Raw bilinear heightmap value at a world point -- NOT shifted by the ground
    // reference and NOT clamped at zero, so water cells report their true (low)
    // height. heightAbove() below is this minus the reference, floored at 0; only
    // the waterline sink needs the unclamped value.
    float GameView::rawHeight(float wx, float wz) {
        const auto& m = mapView_.map();
        if (m.heights.empty() || m.width <= 0) return 0.0f;
        float gx = (wx - 8.0f) / 16.0f, gz = (wz - 8.0f) / 16.0f;
        int x0 = std::clamp(int(std::floor(gx)), 0, m.width - 1);
        int z0 = std::clamp(int(std::floor(gz)), 0, m.height - 1);
        int x1 = std::min(x0 + 1, m.width - 1), z1 = std::min(z0 + 1, m.height - 1);
        float fx = std::clamp(gx - float(x0), 0.0f, 1.0f);
        float fz = std::clamp(gz - float(z0), 0.0f, 1.0f);
        auto H = [&](int x, int z) { return float(m.heights[size_t(z) * m.width + x]); };
        return H(x0, z0) * (1 - fx) * (1 - fz) + H(x1, z0) * fx * (1 - fz) +
               H(x0, z1) * (1 - fx) * fz + H(x1, z1) * fx * fz;
    }

    // Retail anchors a wading or floating unit at max(terrainHeight, waterLevel -
    // waterline) (KINGDOMS.icd 0x51b220 for canhover, 0x4dad78 for floater), so the
    // model sinks up to `waterline` height units and no further once the seabed
    // rises to meet it. Our sim has no Y, so this is a pure screen-Y offset, added
    // where terrainLift is subtracted.
    float GameView::waterSink(const tak::sim::UnitType* t, float wx, float wz) {
        if (!t || t->waterline <= 0 || !(t->canHover || t->floater)) return 0.0f;
        const auto& m = mapView_.map();
        if (m.heights.empty()) return 0.0f;
        float depth = float(m.seaLevel) - rawHeight(wx, wz);
        return std::clamp(depth, 0.0f, float(t->waterline)) * kHeightScale_;
    }

    float GameView::heightAbove(float wx, float wz) {
        const auto& m = mapView_.map();
        if (m.heights.empty() || m.width <= 0) return 0.0f;
        // Geometry workers call this concurrently. The terrain datum is resolved
        // on the render thread before dispatch; sample without shared memo state.
        if (heightRef_ < 0) {
            long hist[256] = {0};
            for (uint8_t v : m.heights) hist[v]++;
            int best = 0;
            for (int i = 1; i < 256; ++i) if (hist[i] > hist[best]) best = i;
            heightRef_ = best;
            if (const char* e = tak::devEnv("TAK_HSCALE")) kHeightScale_ = std::stof(e);
            if (const char* e = tak::devEnv("TAK_HSCALEX")) kHeightScaleX_ = std::stof(e);
            if (tak::devEnv("TAK_HDEBUG")) showHDebug_ = true;
        }
        float gx = (wx - 8.0f) / 16.0f, gz = (wz - 8.0f) / 16.0f;
        int x0 = std::clamp(int(std::floor(gx)), 0, m.width - 1);
        int z0 = std::clamp(int(std::floor(gz)), 0, m.height - 1);
        int x1 = std::min(x0 + 1, m.width - 1), z1 = std::min(z0 + 1, m.height - 1);
        float fx = std::clamp(gx - float(x0), 0.0f, 1.0f);
        float fz = std::clamp(gz - float(z0), 0.0f, 1.0f);
        auto H = [&](int x, int z) { return float(m.heights[size_t(z) * m.width + x]); };
        float h = H(x0, z0) * (1 - fx) * (1 - fz) + H(x1, z0) * fx * (1 - fz) +
                  H(x0, z1) * (1 - fx) * fz + H(x1, z1) * fx * fz;
        return std::max(0.0f, h - float(heightRef_));
    }

    // Retail's flyer datum, built once per map (icd 0x50e740). One byte per
    // 128x128-world-unit sector = the MAX terrain height over its 8x8 block of
    // 16-unit cells, floored at the water datum; then a 3x3 max dilation, first
    // along X then along Z, so the value a flyer reads is the highest ground
    // within 384x384 world units of it. Cheap: a few hundred bytes, computed on
    // the first query after a map change.
    void GameView::buildFlyerGround() {
        const auto& m = mapView_.map();
        flyGroundMap_ = &m;
        flyGround_.clear();
        flyGroundW_ = flyGroundH_ = 0;
        if (m.heights.empty() || m.width <= 0 || m.height <= 0) return;
        heightAbove(0, 0);                       // force heightRef_ to be resolved
        const int kCells = 8;                    // 8 cells of 16 units = one 128-unit sector
        flyGroundW_ = (m.width + kCells - 1) / kCells;
        flyGroundH_ = (m.height + kCells - 1) / kCells;
        std::vector<uint8_t> maxv(size_t(flyGroundW_) * size_t(flyGroundH_), 0);
        for (int z = 0; z < m.height; ++z)
            for (int x = 0; x < m.width; ++x) {
                int h = int(m.heights[size_t(z) * m.width + x]) - heightRef_;
                if (h < 0) h = 0;                // below the water datum reads as flat
                uint8_t& dst = maxv[size_t(z / kCells) * size_t(flyGroundW_) + size_t(x / kCells)];
                if (h > int(dst)) dst = uint8_t(h > 255 ? 255 : h);
            }
        // 3x3 max dilation, separably: along X, then along Z in place.
        std::vector<uint8_t> tmp = maxv;
        for (int z = 0; z < flyGroundH_; ++z)
            for (int x = 0; x < flyGroundW_; ++x) {
                uint8_t v = maxv[size_t(z) * size_t(flyGroundW_) + size_t(x)];
                if (x > 0) v = std::max(v, maxv[size_t(z) * size_t(flyGroundW_) + size_t(x - 1)]);
                if (x + 1 < flyGroundW_) v = std::max(v, maxv[size_t(z) * size_t(flyGroundW_) + size_t(x + 1)]);
                tmp[size_t(z) * size_t(flyGroundW_) + size_t(x)] = v;
            }
        flyGround_.assign(tmp.begin(), tmp.end());
        for (int z = 0; z < flyGroundH_; ++z)
            for (int x = 0; x < flyGroundW_; ++x) {
                uint8_t v = tmp[size_t(z) * size_t(flyGroundW_) + size_t(x)];
                if (z > 0) v = std::max(v, tmp[size_t(z - 1) * size_t(flyGroundW_) + size_t(x)]);
                if (z + 1 < flyGroundH_) v = std::max(v, tmp[size_t(z + 1) * size_t(flyGroundW_) + size_t(x)]);
                flyGround_[size_t(z) * size_t(flyGroundW_) + size_t(x)] = v;
            }
    }

    float GameView::flyerGround(float wx, float wz) {
        const auto& m = mapView_.map();
        if (&m != flyGroundMap_) buildFlyerGround();
        if (flyGround_.empty()) return 0.0f;
        int sx = std::clamp(int(wx / 128.0f), 0, flyGroundW_ - 1);
        int sz = std::clamp(int(wz / 128.0f), 0, flyGroundH_ - 1);
        return float(flyGround_[size_t(sz) * size_t(flyGroundW_) + size_t(sx)]);
    }

    float GameView::unitAltById(int id) const {
        auto it = anims_.find(id);
        return it != anims_.end() ? it->second.altitude : 0.0f;
    }

    SDL_FPoint GameView::unitScreen(const UnitR& u) {
        float zm = mapView_.zoom();
        float ix, iz, ih; interpPose(u, ix, iz, ih);   // match the gliding model position
        // uLiftX/uLiftY, NOT terrainLift: a flyer rides the coarse dilated datum,
        // and picking has to use the same one the body is drawn with or you click
        // where the unit is not. The waterline sink moves the drawn body too, so
        // this follows that as well -- else a wading god can't be clicked where you
        // see it.
        return {(ix - mapView_.offX()) * zm - uLiftX(u) * zm,
                (iz - mapView_.offY()) * zm - uLiftY(u) * zm
                    + waterSink(u.type, ix, iz) * zm - altLift(u) * zm - 12.0f * zm};
    }

    const SDL_FRect& GameView::unitHitBox(const tak::sim::UnitType* type) {
        auto it = hitBoxes_.find(type->id);
        if (it != hitBoxes_.end()) return it->second;
        // Fallback (model not loaded): a small box just above the anchor.
        SDL_FRect box{-12.0f, -28.0f, 24.0f, 30.0f};
        auto vt = visuals_.find(type->id);
        if (vt != visuals_.end()) {
            std::vector<Tri> scratch;
            // Buildings also carry a birth heading. Cache bounds that cover
            // every heading, as for movers, so rotated bodies remain selectable.
            RadialExtent ext;
            collect(scratch, nullptr, vt->second.model.root, Xform{}, nullptr,
                    0.0f, 0, false, true, false, &ext);
            if (ext.any)
                box = SDL_FRect{-ext.maxR, ext.minY, 2.0f * ext.maxR, ext.maxY-ext.minY};
        }
        return hitBoxes_.emplace(type->id, box).first->second;
    }


    const GameView::RingBox& GameView::unitRingBox(const tak::sim::UnitType* type) {
        auto it = ringBoxes_.find(type->id);
        if (it != ringBoxes_.end()) return it->second;
        RingBox rb;   // defaults cover a model that never loaded
        auto vt = visuals_.find(type->id);
        if (vt != visuals_.end()) {
            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
            bool any = false;
            // Walk the rest pose accumulating MODEL-space bounds. Ground plates are
            // skipped for the same reason collect() refuses to draw them: they are
            // invisible spread-out polygons that would blow the ring out to nothing
            // like the unit's size.
            static const float kNoRot[3] = {0, 0, 0};   // rest pose: no COB rotation
            auto walk = [&](auto&& self, const tak::tdo::Object& o, const Xform& parent,
                            bool isRoot) -> void {
                    Xform xf = parent.then(o.x, o.y, o.z, kNoRot);
                    std::string on = o.name;
                    std::transform(on.begin(), on.end(), on.begin(), ::tolower);
                    auto ends = [&](const char* suf) {
                        size_t n = std::strlen(suf);
                        return on.size() >= n && on.compare(on.size() - n, n, suf) == 0;
                    };
                    bool plate = isRoot || ends("gp") || ends("null") || ends("off") ||
                                 on.find("ground") != std::string::npos ||
                                 on.find("gpoly") != std::string::npos ||
                                 on.find("gpoint") != std::string::npos;
                    if (!plate)
                        for (const auto& p : o.primitives)
                            for (uint16_t vi : p.indices) {
                                size_t v = size_t(vi) * 3;
                                if (v + 2 >= o.vertices.size()) continue;
                                float w[3];
                                xf.apply(o.vertices[v], o.vertices[v + 1], o.vertices[v + 2], w);
                                for (int k = 0; k < 3; ++k) {
                                    lo[k] = std::min(lo[k], w[k]);
                                    hi[k] = std::max(hi[k], w[k]);
                                }
                                any = true;
                            }
                    for (const auto& c : o.children) self(self, c, xf, false);
                };
            walk(walk, vt->second.model.root, Xform{}, true);
            if (any) {
                rb.halfX = std::max(1.0f, (hi[0] - lo[0]) * 0.5f);
                rb.halfZ = std::max(1.0f, (hi[2] - lo[2]) * 0.5f);
                rb.midY = (lo[1] + hi[1]) * 0.5f;
            }
        }
        return ringBoxes_.emplace(type->id, rb).first->second;
    }

    void GameView::pickWorld(float sx, float sy, float& wx, float& wz) {
        float zm = mapView_.zoom();
        // Flat (no-lift) world position of the click.
        float cwx = mapView_.offX() + sx / zm;
        float cwz = mapView_.offY() + sy / zm;
        wx = cwx; wz = cwz;
        const auto& m = mapView_.map();
        if (m.heights.empty()) return;
        heightAbove(cwx, cwz);   // init ref/scales
        // A surface cell of height h renders at flat position (wx - h*scaleX,
        // wz - h*scaleY), so a point that projects to this click satisfies
        // (wx, wz) = (cwx + h*scaleX, cwz + h*scaleY) with h = heightAbove(wx, wz).
        // March h outward (both axes move together along the tilt) and take the
        // front-most self-consistent surface -- the largest h where the candidate's
        // actual height drops through the assumed h. Handles the X/Z coupling so a
        // click on a wall top resolves to that top, not the ground behind it.
        float maxH = std::max(0.0f, float(255 - (heightRef_ < 0 ? 0 : heightRef_)));
        float bestH = 0.0f;
        float prevDiff = heightAbove(cwx, cwz);   // actualH - 0 at h = 0
        const float step = 1.0f;
        for (float h = step; h <= maxH + 2; h += step) {
            float ax = cwx + h * kHeightScaleX_;
            float az = cwz + h * kHeightScale_;
            float diff = heightAbove(ax, az) - h;
            // Front-most self-consistent surface: interpolate the zero-crossing of
            // (actualHeight - h) so we land ON the surface, not past its far edge.
            if (prevDiff > 0 && diff <= 0)
                bestH = (h - step) + step * prevDiff / (prevDiff - diff);
            prevDiff = diff;
        }
        wx = std::clamp(cwx + bestH * kHeightScaleX_, 0.0f, float(m.width * 16 - 1));
        wz = std::clamp(cwz + bestH * kHeightScale_, 0.0f, float(m.height * 16 - 1));
    }

    float GameView::wallOcclusionY(float wx, float wz) {
        const auto& m = mapView_.map();
        if (m.heights.empty()) return 1e9f;
        terrainLift(wx, wz);   // ensure heightRef_/kHeightScale_ are initialised
        float zm = mapView_.zoom();
        int cx = std::clamp(int(wx) / 16, 0, m.width - 1);
        int cz0 = std::clamp(int(wz) / 16, 0, m.height - 1);
        int hUnit = m.heights[size_t(cz0) * m.width + cx];
        float best = 1e9f;
        for (int dz = 1; dz <= kOccScan_; ++dz) {
            int cz = cz0 + dz;
            if (cz >= m.height) break;
            int h = m.heights[size_t(cz) * m.width + cx];
            if (h <= hUnit + 24) continue;   // not a wall relative to this unit
            // The wall's painted top projects north of its heightmap footprint by
            // ~height*kHeightScale_ (the art leans it up-and-back) -- the same scale
            // the unit lift and picking use, so occlusion, seating and clicks agree.
            float proj = std::max(0.0f, float(h - heightRef_) * kHeightScale_);
            float wy = (float(cz) * 16 + 8 - mapView_.offY()) * zm - proj * zm;
            if (wy < best) best = wy;
        }
        return best;
    }

    void GameView::simWorkerLoop() {
        for (;;) {
            SimJob job;
            {
                std::unique_lock<std::mutex> lk(inboxMutex_);
                inboxCv_.wait(lk, [&]{ return simQuit_.load() || !simInbox_.empty(); });
                // Cancelled: drop whatever is queued. The tick in flight (if any)
                // already finished above, so the world is left on a tick boundary.
                if (simCancel_.load()) { simInbox_.clear(); return; }
                if (simInbox_.empty()) return;   // quit signalled and nothing left to process
                job = std::move(simInbox_.front());
                simInbox_.pop_front();
            }
            uint64_t hash = 0, pos = 0;
            {
#ifndef NDEBUG
                PaceSpan jobSpan(paceJobWallNs_, &paceJobCpuNs_);
#endif
                std::lock_guard<std::mutex> lk(simMutex_);
                for (const auto& c : job.bundle.cmds) apply(c);
                for (const auto& e : job.bundle.events) applyEvent(e);
                simStep(1.0f / 30.0f);   // world_.tick + captureFrame (publishes a snapshot)
                if (job.wantHash) hash = reportedHash(job.spectator, job.tick);
                if (job.wantPos) pos = world_.posDigest();
            }
            if (job.wantHash) {
                std::lock_guard<std::mutex> lk(outboxMutex_);
                simOutbox_.push_back({job.tick, hash, pos});
            }
            simProcessedTick_.store(job.tick, std::memory_order_relaxed);   // for backlog/ack tracking
        }
    }

    void GameView::startSimThread() {
        if (useSimThread_) return;
        useSimThread_ = true;
        simQuit_ = false;
        simThread_ = std::thread([this]{ simWorkerLoop(); });
    }

    void GameView::stopSimThread(bool drain) {
        if (!useSimThread_) return;
        {
            std::lock_guard<std::mutex> lk(inboxMutex_);
            if (!drain) { simCancel_ = true; simInbox_.clear(); }
            simQuit_ = true;
        }
        inboxCv_.notify_one();
        if (simThread_.joinable()) simThread_.join();
        useSimThread_ = false;
    }

    void GameView::beginFrame() {
        std::lock_guard<std::mutex> lk(frameMutex_);
        renderReadIdx_ = published_;
        reading_ = published_;
        placePreviewBusy_ = false;   // one failed try_lock per frame at most (canPlacePreview)
        if (!gameStartMs_ && front().gameTick > 0) gameStartMs_ = SDL_GetTicks64();
        // A previously selected enemy must stop exposing its live state on leaving sight.
        std::erase_if(selection_, [this](int id) {
            const auto* u = frameUnitP(id);
            return !u || !u->alive() || !canPickUnit(*u);
        });

    }

    void GameView::endFrame() {
        std::lock_guard<std::mutex> lk(frameMutex_);
        reading_ = -1;
    }

    void GameView::finishTerrain() {
        mapView_.finishChunks();
        for (int i = 0; i < 1000 && !miniTex_; ++i) {   // adopt once the crunch lands
            buildMinimap();
            if (!miniTex_) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    std::vector<int> GameView::factionMusicTracks(const std::string& side) {
        std::string want = side;   // "ara" -> match "ARAMON" etc.
        std::transform(want.begin(), want.end(), want.begin(), ::toupper);
        const std::string prefixes[] = {
            std::string("ARA=ARAMON"), "TAR=TAROS", "VER=VERUNA", "ZON=ZHON",
            "CRE=CREON"};
        std::string full;
        for (const auto& m : prefixes)
            if (m.substr(0, 3) == want) full = m.substr(4);
        std::vector<int> out;
        try {
            auto sd = vtdf("gamedata/sidedata.tdf");
            for (const auto& key : sd.childOrder) {
                const auto& sec = sd.children.at(key);
                std::string nm = sec.valueOr("name", "");
                std::transform(nm.begin(), nm.end(), nm.begin(), ::toupper);
                if (nm != full) continue;
                std::istringstream ts(sec.valueOr("musictracks", ""));
                int t;
                while (ts >> t) out.push_back(t);
                if (!out.empty()) return out;
            }
        } catch (const std::exception&) {}
        return out;
    }

    std::vector<int> GameView::allFactionMusicTracks() {
        std::vector<int> out;
        try {
            auto sd = vtdf("gamedata/sidedata.tdf");
            for (const auto& key : sd.childOrder) {
                std::istringstream ts(sd.children.at(key).valueOr("musictracks", ""));
                int t;
                while (ts >> t) out.push_back(t);
            }
        } catch (const std::exception&) {}
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;   // empty -> startMusic falls back to all 20
    }


    void GameView::issuePerUnit(tak::net::Cmd kind, int value) {
        for (int id : selection_) {
            const auto* u = frameUnitP(id);
            if (!u || u->player != localPlayer_) continue;
            tak::net::Command c;
            c.kind = kind;
            c.unitId = id;
            c.targetId = value;
            issue(c);
        }
    }

    // Retail's own mission labels, from translate/unitmissions.tdf -- the table
    // keyed by the UNITMISSIONCODE_* strings the engine's command table carries.
    // Several of these used to be reasonable-sounding paraphrases that retail
    // simply does not use: it says "Clearing" and not RECLAIMING, "Loading" and
    // not BOARDING, and a half-built unit is an "Intangible Mass".
    const char* GameView::unitStatusText(const UnitR* u) const {
        if (!u || !u->type) return "";
        // UNITMISSIONCODE_SELFDESTRUCT. First, because it is what the unit is
        // doing -- it has stopped taking orders and is walking out on you.
        if (u->selfDestructT >= 0) return "LEAVING YOUR COMMAND";
        if (u->stonedFor > 0) return "PETRIFIED";
        if (u->frozenFor > 0) return "FROZEN";
        if (u->paralyzedFor > 0) return "PARALYZED";
        if (u->underConstruction) return "INTANGIBLE MASS";
        if (!u->active) return "INACTIVE";
        if (u->repairId != 0) return "REPAIRING";
        if (u->reclaimId != 0) return "CLEARING";
        if (u->buildSiteId != 0 || !u->buildQueue.empty()) return "CONJURING";
        if (!u->orders.empty()) {
            const auto& o = u->orders.front();
            if (o.guard) return "GUARDING";
            if (o.patrol) return "PATROLLING";
            if (o.load) return "LOADING";
            if (o.unload) return "UNLOADING";
            if (o.reclaimFeat || o.reclaimArea) return "CLEARING AREA";
            if (o.targetId != 0) return "ATTACKING";
            if (o.attackMove) return "SEEKING TO ATTACK";
            return "MOVING";
        }
        if (u->cloaked) return "CLOAKING";
        return "STANDBY";   // retail's idle label
    }

    bool GameView::handleKey(SDL_Keycode key, uint16_t mod) {
        bool ctrl = (mod & KMOD_CTRL) != 0;
        bool shift = (mod & KMOD_SHIFT) != 0;
        bool alt = (mod & KMOD_ALT) != 0;

        // Pause toggle works without a selection.
        if (key == SDLK_PAUSE) {
            paused_ = !paused_;
            // In a net game (which single-player also is -- it runs a private local
            // server) the sim is driven by delivered ticks, not by update(), so a
            // local flag froze only the animation while the game carried on. Ask the
            // SERVER to stop issuing ticks; every peer then freezes together.
            if (isNet() && mp_) mp_->setPause(paused_);
            return true;
        }
        // +/- (and keypad +/-) step game speed over -10..+10 (0 = normal).
        if (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS ||
            key == SDLK_MINUS || key == SDLK_KP_MINUS) {
            bool up = (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS);
            if (mp_) {   // lockstep: only the host may re-cadence, and only if unlocked
                if (!mp_->room().opts.speedUnlock) {
                    notice_ = "SPEED LOCKED (host can unlock in the lobby)";
                    noticeTimer_ = 2; return true;
                }
                if (mp_->room().hostId != mp_->myClientId()) {
                    notice_ = "ONLY THE HOST CAN CHANGE SPEED";
                    noticeTimer_ = 2; return true;
                }
                auto o = mp_->room().opts;
                int ns = std::clamp(int(mp_->gameSpeed()) + (up ? 5 : -5), 5, tak::net::kMaxGameSpeed);
                if (ns != o.speed) { o.speed = uint8_t(ns); mp_->setGameOptions(o); }
                char nb[24]; std::snprintf(nb, sizeof nb, "GAME SPEED %.1fx", ns / 10.0f);
                notice_ = nb; noticeTimer_ = 2;
                return true;
            }
            gameSpeed_ = std::clamp(gameSpeed_ + (up ? 1 : -1), -10, 10);
            notice_ = "GAME SPEED " + std::string(gameSpeed_ > 0 ? "+" : "") +
                      std::to_string(gameSpeed_);
            noticeTimer_ = 2;
            return true;
        }

        // The number row 1-9,0 addresses control squads (0 = squad 10). CTRL+N assigns a
        // GROUP, ALT+N a FORMATION; SHIFT appends instead of replacing. A plain digit
        // recalls squad N. Squads are lockstep sim state (Cmd::SetSquad); a unit is in one
        // squad at a time, and a number is a group XOR a formation (assigning replaces).
        // Structural (the whole number row), not rebindable.
        int digit = -1;
        if (key >= SDLK_0 && key <= SDLK_9) digit = int(key - SDLK_0);
        if (digit >= 0 && !spectating_) {
            int num = digit == 0 ? 10 : digit;
            if (ctrl || alt) assignSquad(num, alt ? -1 : +1, shift);
            else             recallSquad(num);
            return true;
        }

        // Everything below is a REBINDABLE action: resolve the pressed chord to an
        // Act via the user's hotkey config (src/client/hotkeys) and dispatch.
        switch (hotkeys_.match(int32_t(key), mod)) {
            case tak::Act::GiveUnits:
                if (!spectating_ && !replayMode_ && missionStem_.empty()) {
                    giveUnitsMenu_ = true;
                    giveUnitsHots_.clear();
                    dragging_ = false;
                }
                return true;
            case tak::Act::ToggleCounts: showCounts_ = !showCounts_; return true;
            case tak::Act::UnitInfo: toggleUnitInfo(); return true;
            case tak::Act::RetailZoom: {
                // Retail draws the map at native 1:1 scale. Preserve the centre
                // of the world viewport rather than zooming toward the pointer.
                const float cx=float(mapViewW(winW_))*0.5f;
                const float cy=float(winH_-barH())*0.5f;
                const float oldZoom=std::max(mapView_.zoom(),1e-3f);
                const float wx=mapView_.offX()+cx/oldZoom;
                const float wz=mapView_.offY()+cy/oldZoom;
                mapView_.setZoom(1.0f);
                mapView_.setOffset(wx-cx,wz-cy);
                notice_="RETAIL ZOOM";noticeTimer_=2;
                return true;
            }
            case tak::Act::FullScreenRadar:
                // A VIEW action, so it belongs here and not in the order switch
                // below -- that one returns early when nothing is selected, and
                // the full-screen map is exactly what you want with an empty
                // selection. Toggle only: retail's handler ignores its argument
                // list entirely and there is no dismiss-on-click or -on-Escape
                // path, so TAB again is the only way out there too.
                fsRadar_ = !fsRadar_;
                pendingCmd_ = 0;
                return true;
            case tak::Act::SelfDestruct: {   // self-destruct the selected unit(s)
                // Through the command path (Cmd::Destroy), not a direct hp write --
                // a local mutation would silently desync a networked game.
                // Cmd::Destroy TOGGLES a 5s countdown; if any selected unit is
                // already counting down, this press cancels instead of arming.
                int n = 0;
                bool anyArmed = false;
                for (int id : selection_)
                    if (auto* su = frameUnitP(id))
                        if (su->alive() && su->player == localPlayer_) {
                            if (su->selfDestructT >= 0.0f) anyArmed = true;
                            tak::net::Command c;
                            c.kind = tak::net::Cmd::Destroy;
                            c.unitId = id;
                            issue(c);
                            ++n;
                        }
                if (n) {
                    notice_ = anyArmed ? "SELF-DESTRUCT CANCELLED" : "SELF-DESTRUCT IN 5...";
                    noticeTimer_ = 2;
                }
                return true;
            }
            case tak::Act::Disco:
            case tak::Act::Headbang: {
                // One emote at a time -- don't even send the command while a disco or
                // headbang is already running on the real-time clock. The busy
                // notice reflects what you're ACTUALLY doing, not the key you pressed.
                bool emoting = frameDiscoActive(localPlayer_) || frameHeadbangActive(localPlayer_);
                if (emoting) {
                    notice_ = frameDiscoActive(localPlayer_) ? "ALREADY GROOVING" : "ALREADY ROCKING";
                    noticeTimer_ = 2; return true;
                }
                bool disco = hotkeys_.match(int32_t(key), mod) == tak::Act::Disco;
                tak::net::Command c;
                c.kind = disco ? tak::net::Cmd::Disco : tak::net::Cmd::Headbang;
                if (selection_.empty()) issue(c); // living monarch fallback
                else for (int id : selection_) {
                    const auto* u = frameUnitP(id);
                    if (!u || !u->alive() || u->player != localPlayer_ || !u->type || u->type->isStructure()) continue;
                    c.unitId = id;
                    issue(c); // capture selection through lockstep
                    c.queue = 1; // further targets belong to the same emote trigger
                }
                notice_ = disco ? "DISCO TIME" : "HEADBANG!!";
                noticeTimer_ = 2;
                return true;
            }
            // Selection commands (no armed order, no selection prerequisite).
            case tak::Act::SelectAll:
                selectOwned([](const UnitR&){ return true; });
                return true;
            case tak::Act::SelectSameType: {   // expand every type in the original selection
                std::set<const tak::sim::UnitType*> types;
                for (int id : selection_)
                    if (const auto* u = frameUnitP(id); u && u->type)
                        types.insert(u->type);
                // Snapshot the types first: selectOwned replaces selection_.
                if (!types.empty())
                    selectOwned([&types](const UnitR& u){ return types.contains(u.type); });
                return true;
            }
            case tak::Act::SelectOnScreen:
                selectOwned([this](const UnitR& u){ return onScreen(u); });
                return true;
            case tak::Act::SelectMonarch: {   // select the Monarch and track it
                const auto* m = playerMonarchId_ >= 0 ? frameUnitP(playerMonarchId_) : nullptr;
                if (m && m->alive()) {
                    selection_ = {playerMonarchId_};
                    trackSel_ = true;
                    centerOnSelection();
                    voice(playerMonarchId_, "select");
                } else {
                    notice_ = "NO MONARCH"; noticeTimer_ = 2;
                }
                return true;
            }
            // Category selects: all owned units matching a predicate (workers, army,
            // navy, casters, flyers, ...). Any-weapon checks scan the weapons list.
            case tak::Act::SelectBuilders:
                selectOwned([](const UnitR& u){ return u.type->isBuilder; });
                return true;
            case tak::Act::SelectFactory:
                selectOwned([](const UnitR& u){ return u.type->isBuilder && u.type->isStructure(); });
                return true;
            case tak::Act::SelectMelee:
                selectOwned([](const UnitR& u){
                    if (!u.type->canMove) return false;
                    for (const auto& w : u.type->weapons) if (w.melee) return true;
                    return false;
                });
                return true;
            case tak::Act::SelectMagic:   // casters carry a personal mana pool
                selectOwned([](const UnitR& u){ return u.type->maxMana > 0 && u.type->canMove; });
                return true;
            case tak::Act::SelectBoats:
                selectOwned([](const UnitR& u){
                    return u.type->domain == tak::sim::UnitType::Domain::Water;
                });
                return true;
            case tak::Act::SelectBallistic:
                selectOwned([](const UnitR& u){
                    for (const auto& w : u.type->weapons) if (w.ballistic) return true;
                    return false;
                });
                return true;
            case tak::Act::SelectTroops:   // mobile armed, no navy, not the Monarch
                selectOwned([](const UnitR& u){
                    if (!u.type->canMove || u.type->commander) return false;
                    if (u.type->domain == tak::sim::UnitType::Domain::Water) return false;
                    for (const auto& w : u.type->weapons) if (w.damage > 0) return true;
                    return false;
                });
                return true;
            case tak::Act::SelectArmed:    // anything with a weapon except the Monarch
                selectOwned([](const UnitR& u){
                    if (u.type->commander) return false;
                    for (const auto& w : u.type->weapons) if (w.damage > 0) return true;
                    return false;
                });
                return true;
            case tak::Act::SelectOnScreenType: {   // on-screen units of the selected type
                const auto* first = selection_.empty() ? nullptr : frameUnitP(selection_.front());
                const auto* t = first ? first->type : nullptr;
                if (t) selectOwned([this, t](const UnitR& u){ return u.type == t && onScreen(u); });
                return true;
            }
            case tak::Act::SelectFlying:
                selectOwned([](const UnitR& u){ return u.type->canFly; });
                return true;
            default: break;
        }
        if (ctrl) return false;   // other CTRL combos fall through to the map view

        // Order commands need at least one selected unit.
        if (selection_.empty()) return false;
        switch (hotkeys_.match(int32_t(key), mod)) {
            case tak::Act::FightMove: pendingCmd_ = 'f'; return true;
            case tak::Act::Move:      pendingCmd_ = 'm'; return true;
            case tak::Act::Attack:    pendingCmd_ = 'a'; return true;
            case tak::Act::Patrol:    pendingCmd_ = 'p'; return true;
            case tak::Act::Guard:     pendingCmd_ = 'g'; return true;
            case tak::Act::Heal:      pendingCmd_ = 'r'; return true;   // repair a damaged friendly
            case tak::Act::Load:      pendingCmd_ = 'l'; return true;   // click a unit to carry
            case tak::Act::Unload:    pendingCmd_ = 'u'; return true;   // click the drop destination
            case tak::Act::ClearOrders:                       // clear the whole order queue
                for (int id : selection_) {
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Stop;
                    c.unitId = id;
                    issue(c);
                }
                pendingCmd_ = 0;
                return true;
            case tak::Act::CycleWeapon:                       // cycle active weapon
                if (const auto* u = multiWeaponSel()) {
                    int n = int(u->type->weapons.size());
                    selectWeapon((u->weaponSlot + 1) % n);
                }
                pendingCmd_ = 0;
                return true;
            case tak::Act::Stop:                              // stop (immediate)
                for (int id : selection_) {
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Stop;
                    c.unitId = id;
                    issue(c);
                }
                pendingCmd_ = 0;
                return true;
            case tak::Act::ToggleCloak: {   // cloak on if any selected cloaker is off
                bool anyOn = false, anyCloaker = false;
                for (int id : selection_)
                    if (const auto* u = frameUnitP(id); u && u->type && u->type->canCloak) {
                        anyCloaker = true;
                        if (u->cloakOn) anyOn = true;
                    }
                if (anyCloaker) issuePerUnit(tak::net::Cmd::Cloak, anyOn ? 0 : 1);
                pendingCmd_ = 0;
                return true;
            }
            case tak::Act::ToggleGate: {   // open/close: toggle active on gates (onoffable)
                bool anyActive = false, anyGate = false;
                for (int id : selection_)
                    if (const auto* u = frameUnitP(id); u && u->type && u->type->onOffable) {
                        anyGate = true;
                        if (u->active) anyActive = true;
                    }
                if (anyGate) issuePerUnit(tak::net::Cmd::SetActive, anyActive ? 0 : 1);
                pendingCmd_ = 0;
                return true;
            }
            case tak::Act::TrackSelection:                    // track/untrack selection
                trackSel_ = !trackSel_;
                if (trackSel_) centerOnSelection();
                pendingCmd_ = 0;
                return true;
            case tak::Act::NextUnit: cycleNextUnit(); return true;
            default: return false;
        }
    }

    void GameView::issueSquad(int unitId, int val) {   // val: 0 none, +N group N, -N formation N
        tak::net::Command c;
        c.kind = tak::net::Cmd::SetSquad;
        c.unitId = unitId;
        c.targetId = val;
        issue(c);
    }

    void GameView::assignSquad(int num, int sign, bool append) {
        if (spectating_ || num < 1 || num > 10 || selection_.empty()) return;
        int want = sign * num;
        std::unordered_set<int> sel(selection_.begin(), selection_.end());
        for (const UnitR* _up : front().live) {
            const UnitR& u = *_up;
            if (u.alive() && u.player == localPlayer_ && std::abs(int(u.squad)) == num &&
                sel.find(u.id) == sel.end())
                issueSquad(u.id, append ? want : 0);   // append: retype it; else evict it
        }
        for (int id : selection_)
            if (const auto* u = frameUnitP(id); u && u->alive() && u->player == localPlayer_)
                issueSquad(id, want);
    }

    void GameView::recallSquad(int num) {
        if (spectating_) return;
        const bool follow = lastRecalledSquad_ == num;
        selection_.clear();
        for (const UnitR* _up : front().live) {
            const UnitR& u = *_up;
            if (u.alive() && u.player == localPlayer_ && u.type &&
                std::abs(int(u.squad)) == num && !u.type->isBuilder)
                selection_.push_back(u.id);
        }
        lastRecalledSquad_ = selection_.empty() ? 0 : num;
        trackSel_ = follow && !selection_.empty();
        if (trackSel_) centerOnSelection();
        if (!selection_.empty()) voice(selection_.front(), "select");
    }

    void GameView::clearSquad() {
        if (spectating_) return;
        for (int id : selection_)
            if (const auto* u = frameUnitP(id);
                u && u->alive() && u->player == localPlayer_ && u->squad)
                issueSquad(id, 0);
    }

    bool GameView::onScreen(const UnitR& u) const {
        float sx = (u.x - mapView_.offX()) * mapView_.zoom();
        float sy = (u.z - mapView_.offY()) * mapView_.zoom();
        return sx >= 0 && sy >= 0 && sx <= float(winW_) && sy <= float(winH_) - barH();
    }

    bool GameView::lbHot(const SDL_FRect& r) const {
        float mx = mouseX_ / lobbyScale_ - lobbyOffX_, my = mouseY_ / lobbyScale_ - lobbyOffY_;
        return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
    }

    void GameView::lbField(float x, float y, float w, const std::string& label,
                 const std::string& value, int id) {
        blockText(label, x, y, 1.6f, {150, 155, 170, 255});
        SDL_FRect r{x, y + 14, w, 22};
        bool active = (lbField_ == id);
        SDL_SetRenderDrawColor(ren_, 24, 26, 34, 255);
        SDL_RenderFillRectF(ren_, &r);
        SDL_SetRenderDrawColor(ren_, active ? 200 : 90, active ? 210 : 100, active ? 130 : 130, 255);
        SDL_RenderDrawRectF(ren_, &r);
        std::string shown = value + (active ? "_" : "");
        blockText(shown, x + 6, y + 20, 1.8f, {225, 230, 240, 255});
        lobbyHots_.push_back({r, [this, id] { lbField_ = id; SDL_StartTextInput(); }});
    }

    std::string GameView::readOta(const std::string& tntPath) const {
        const auto ota = tak::vpath::replaceExtension(tntPath,".ota");
        try { auto d = vfs_.read(ota); return std::string(d.begin(), d.end()); }
        catch (...) { return {}; }
    }

    std::string* GameView::lbFieldBuf() {
        switch (lbField_) {
            case 1: return &createName_; case 2: return &createPass_;
            case 3: return &joinPass_; case 4: return &chatDraft_;
            case 5: return &genParams_.name;
            default: return nullptr;
        }
    }

    std::vector<std::pair<float, float>> GameView::buildLinePositions(
        float x0, float z0, float x1, float z1) const {
        std::vector<std::pair<float, float>> out;
        if (!placing_) return out;
        // Full footprint width plus a one-cell gap: edge-to-edge spacing lets integer
        // cell rounding of the un-aligned drag tip adjacent sites into a shared
        // footprint cell, which makes canPlace reject every other one.
        float sp = (std::max({placing_->footX, placing_->footZ, 1}) + 1) * 16.0f;
        float dx = x1 - x0, dz = z1 - z0, len = std::sqrt(dx * dx + dz * dz);
        int n = int(len / sp);
        float ux = len > 1e-3f ? dx / len : 0, uz = len > 1e-3f ? dz / len : 0;
        for (int i = 0; i <= n; ++i)
            out.push_back({x0 + ux * sp * i, z0 + uz * sp * i});
        return out;
    }

    void GameView::voice(int unitId, const std::string& event) {
        // Retail command feedback: one WEIGHTED draw from the sound class's
        // event pool -- the symbolic "_NN-note" entry (weight 100) vs the voice
        // lines (weight 1 each), so units bong ~99% of the time and speak
        // occasionally. The note names no file: it is the faction click tone
        // (sounds/tone<side>.wav, what click.hpi packs replace) pitch-shifted
        // to the named chromatic semitone (A=1..G#=12) -- a different pitch
        // per command type. Units without a class fall back to the plain tone
        // on command events.
        const auto* u = frameUnitP(unitId);
        const std::string* wav = nullptr;
        if (u && u->type && !u->type->soundClass.empty())
            wav = soundClasses_.pick(u->type->soundClass, event, salt_++);
        auto isTone = [](const std::string& w) {
            return w.size() >= 4 &&
                   std::tolower((unsigned char)w[0]) == 't' &&
                   std::tolower((unsigned char)w[1]) == 'o' &&
                   std::tolower((unsigned char)w[2]) == 'n' &&
                   std::tolower((unsigned char)w[3]) == 'e';
        };
        if (wav && !wav->empty() && (*wav)[0] == '_') {
            int semi = std::atoi(wav->c_str() + 1);
            playClickTone(semi);
        } else if (wav && isTone(*wav)) {
            // Some classes name the faction tone directly (ARAKING's every
            // event is just TONEARA): route it through the boosted tone path,
            // or it plays half-volume and quiet click.hpi replacements vanish.
            playClickTone();
        } else if (wav) {
            sounds_.playWorld(*wav, u->x, u->z);
        } else if (event != "select") {
            playClickTone();
        }
    }

    void GameView::loadExplosionClasses() {
        if (explosionsLoaded_) return;
        explosionsLoaded_ = true;
        explosionClasses_.clear();explosionClassOrder_.clear();
        try {
            auto root = vtdf("gamedata/explosions/explosions.tdf");
            for (const auto& cls : root.childOrder) {
                explosionClassOrder_.push_back(cls);
                const auto& node = root.children.at(cls);
                auto& list = explosionClasses_[cls];   // cls is already lowercased
                for (const auto& vn : node.childOrder) {
                    const auto& v = node.children.at(vn);
                    std::string a = v.valueOr("anim", v.valueOr("gaf", ""));
                    if (!a.empty()) {
                        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                        list.push_back(a);
                    }
                }
            }
        } catch (const std::exception&) {}
    }

    void GameView::spawnWeaponImpact(const tak::sim::Weapon& weapon,
                                     float x, float z, float alt,
                                     std::optional<std::array<int32_t,3>> position, bool struckUnit) {
        // Native 529c10: only an environmental water hit selects the water
        // class. A direct hit uses the ordinary class even above water; a -1
        // (missing) selected class requests no visual, not a generic substitute.
        const std::string& cls = (world_.isWater(x, z) && !struckUnit)
                                     ? weapon.waterExplosionClass
                                     : weapon.explosionClass;
        if (spawnEffect(cls, x, z, alt) && position) effects_.back().worldPosition = position;
    }

    void GameView::triggerShake(float mag, float dur) {
        if (mag <= 0 || dur <= 0) return;
        // Let a stronger/longer quake override a fading one.
        if (mag * dur >= shakeMag_ * shakeTime_) {
            shakeMag_ = mag; shakeDur_ = dur; shakeTime_ = dur;
        }
    }
