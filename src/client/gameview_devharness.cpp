// GameView local dev/test harness: the standalone (server-less) scenario/demo/FFA
// match setup and the TAK_* / --testbuild / --firetest style fixtures that spawn
// units, issue orders and fast-forward the LOCAL sim. DEBUG builds only -- the
// whole file compiles to nothing under NDEBUG, and the callers (the ctor, and the
// localHarness block in main.cpp) are #ifndef NDEBUG too, so a release takclient
// carries none of it. Shared helpers the harness leans on (startAtMonarch, lookAt,
// collect, spawn) stay in gameview_impl.cpp / gameview.h.
#include "util/virtualpath.h"
#include "client/gameview.h"
#include "client/gamespeed.h"
#include "client/renderdriver.h"
#include "client/retailaim.h"
#include "gaf/nimbus.h"
#include "client/shadowmask.h"
#include "client/retailfeatureclock.h"
#include "client/retailbuilderanimation.h"
#include "client/retailmovementcallbacks.h"
#include "client/retaildeathsfx.h"
#include "client/runtimesettings.h"
#include "sim/retailmotion.h"
#include <cmath>
#include <cstdlib>

#ifndef NDEBUG

    const GameView::FactionKit& GameView::kit(const std::string& side) {
        static const std::map<std::string, FactionKit> kits = {
            {"ara", {"araking", "arakeep", "aralode", "arabuild",
                     {"araarch", "araarch", "arasword", "arasword"}}},
            {"tar", {"tarnecro", "tardung", "tarlode", "tarnecro",
                     {"tararch", "tararch", "tardemon", "tartb"}}},
            {"ver", {"vermage", "verkeep", "verlode", "verliege",
                     {"verarch", "verarch", "versword", "versword"}}},
            {"zon", {"zonhunt", "", "zonlode", "zonhand",
                     {"zongob", "zonter", "zontroll", "zonbat"}}},
            {"cre", {"cresage", "creacad", "crelode", "cremech",
                     {"creauto", "creauto", "crebeas", "creshoc"}}},
        };
        auto it = kits.find(side);
        return it != kits.end() ? it->second : kits.at("ara");
    }

    void GameView::guiSmoothTest() {
        const auto hash = world_.stateHash();
        const auto initial = smoothArt_;
        tak::Settings s; s.hardwareCursor = false; s.smoothArt = false;
        applySettings(s);
        cursorsInit_ = true; cursors_.load(ren_, vfs_, &s);
        const auto bytes = gpuvram::bytes(), count = gpuvram::count();
        const auto buttons = orderBtns_.size();
        const auto width = hudFont_.width("Smoothing test"), height = hudFont_.height();
        auto check = [](bool ok, const char* message) {
            if (!ok) throw std::runtime_error(message);
        };
        for (int i = 0; i < 3; ++i) for (bool on : {true, false}) {
            s.smoothArt = on; applySettings(s);
            int w = 0, h = 0;
            check(panelTex_ && SDL_QueryTexture(panelTex_, nullptr, nullptr, &w, &h) == 0,
                  "HUD panel missing during live smoothing toggle");
            check(w == panelW_ * (on ? 2 : 1) && h == panelH_ * (on ? 2 : 1),
                  "HUD smoothing did not apply live");
            check(orderBtns_.size() == buttons && hudFont_.width("Smoothing test") == width && hudFont_.height() == height,
                  "live smoothing changed interface layout");
            check(world_.stateHash() == hash, "interface smoothing changed simulation state");
            if (!on) check(gpuvram::bytes() == bytes && gpuvram::count() == count,
                           "HUD smoothing leaks textures");
        }
        s.smoothArt = initial; applySettings(s);
        std::puts("PASS: HUD smoothing applies live, keeps layout/state and releases textures");
    }

    void GameView::setupScenario() {
        world_.setTerrain(mapView_.map().heights, mapView_.map().width,
                          mapView_.map().height, mapView_.map().seaLevel,
                          &mapView_.map().features);
        tak::sim::registerMapFeatures(world_, mapView_.map(), vfs_, &registry_);
        std::string crtPath = mapSibling(".crt");
        tak::crt::Scenario scen = vhas(crtPath) ? tak::crt::parse(vread(crtPath))
                                                : tak::crt::Scenario{};
        std::printf("scenario: %zu placements, %zu regions\n",
                    scen.units.size(), scen.regions.size());
        float cx = 0, cz = 0;
        int n = 0;
        for (const auto& u : scen.units) {
            std::string id = u.objectName;
            std::transform(id.begin(), id.end(), id.begin(), ::tolower);
            int player = std::clamp(u.player, 0, 3);
            float wx = float(u.x) * 16.0f + 8.0f, wz = float(u.z) * 16.0f + 8.0f;
            // .crt facing is degrees (shipped maps use 180 = due south).
            float heading = float(u.angle) * 3.14159265f / 180.0f;
            int uid = spawn(id, wx, wz, heading, player);
            if (uid >= 0) {
                if (tak::sim::Unit* su = world_.unit(uid)) {   // apply the .crt stats
                    if (su->type)
                        su->hp = tak::sim::Fixed::fromFloat(su->type->maxHp * float(std::clamp(u.health, 0, 100)) / 100.0f);
                    su->veteran = std::clamp(u.veteran, 0, 10);
                }
                if (player == 0) { cx += wx; cz += wz; ++n; }
            }
        }
        if (n) mapView_.setOffset(cx / float(n) - 640 / 0.9f,
                                  cz / float(n) - 400 / 0.9f);
        loadFeatures();

        // The .crt's per-player trigger rules run in the SIM (lockstep +
        // hashed) via ScenarioScript, replacing the old client-side heuristic:
        // conditions/actions (spawn, victory/defeat, flags, timers, messages)
        // are evaluated in World::tick and folded into stateHash. Display
        // actions surface as HUD notices (drained in simStep).
        if (!scen.players.empty() || !scen.regions.empty())
            world_.setScenario(std::make_unique<tak::sim::ScenarioScript>(
                scen, registry_, localPlayer_, world_.numPlayers(),
                mapView_.map().width, mapView_.map().height));
    }

    // The local (server-less) match layouts: TAK_FFA=N, then the 2-player monarch
    // start (+ the demo armies). Returns true when it fully set the match up (FFA),
    // false to fall through to the ctor's startAtMonarch tail. Dev path only: it does
    // not call setupMatch, so it can drift from the real match setup (TAK_FFA resolves
    // godType by hand below for exactly that reason).
    bool GameView::setupLocalMatch(bool demo, bool bare, const std::string& side,
                                   const std::string& aiSide,
                                   const std::vector<std::pair<float, float>>& starts,
                                   float cx, float cz, float px, float pz, float ax, float az) {
        // Dev harness: TAK_FFA=N or TAK_FFA=N,t0.t1.t2... sets up an N-player
        // game (each on its own team unless a team list is given), one monarch +
        // a small army per player at N start positions, all AI-driven. Verifies
        // the 8-player / team / shared-vision / win-condition paths before the
        // real lobby exists. (multiplayer M1)
        if (const char* ff = tak::devEnv("TAK_FFA")) {
            int n = std::atoi(ff);
            n = std::clamp(n, 2, tak::sim::kMaxPlayers);
            std::vector<int> teams(size_t(n), 0);
            for (int i = 0; i < n; ++i) teams[size_t(i)] = i;   // default: FFA
            if (const char* comma = std::strchr(ff, ',')) {     // optional team list
                std::string ts(comma + 1);
                int i = 0;
                for (size_t p = 0; p < ts.size() && i < n; ) {
                    size_t dot = ts.find('.', p);
                    teams[size_t(i++)] = std::atoi(ts.substr(p, dot - p).c_str());
                    if (dot == std::string::npos) break;
                    p = dot + 1;
                }
            }
            world_.setPlayerCount(n);
            for (int i = 0; i < n; ++i) world_.setTeam(i, teams[size_t(i)]);
            // Spread players across the N most mutually-distant start positions
            // (fall back to a ring around centre when the map has too few).
            std::vector<std::pair<float, float>> spots = starts;
            while (int(spots.size()) < n) {
                float ang = float(spots.size()) / float(n) * 6.2831853f;
                spots.push_back({cx + std::cos(ang) * 300, cz + std::sin(ang) * 300});
            }
            const char* sides[5] = {"ara", "tar", "ver", "zon", "cre"};
            for (int i = 0; i < n; ++i) {
                std::string fkSide = sides[i % 5];
                // Gods are resolved in setupMatch, which this harness does not call, so
                // godType stayed null here. World::summonReadyGods marks the summon
                // HANDLED before checking it (a player with nothing on the map must not
                // bank one), so a null type does not defer the god -- it consumes the
                // favour and spawns nothing, permanently. Resolve it the same way
                // setupMatch does.
                world_.player(i).godType = registry_.find(fkSide + "god");
                const FactionKit& fk = kit(fkSide);
                float mx = spots[size_t(i)].first, mz = spots[size_t(i)].second;
                int mon = spawn(fk.monarch, mx, mz, 0, i);
                if (i == 0) { playerMonarchId_ = mon; builderId_ = mon; }
                for (int s = 0; s < 4; ++s)
                    spawn(fk.squad[s % 4], mx + (s % 2) * 26 - 13, mz - 50 + (s / 2) * 26, 0, i);
                world_.player(i).mana = 2800;
            }
            ffaPlayers_ = n;
            for (auto& u : world_.units()) {
                if (!u.type || u.type->canMove) continue;
                tak::sim::blockFootprint(world_.nav(), *u.type, u.x.toFloat(), u.z.toFloat(), true);
            }
            return true;
        }
        if (!bare) {
        const FactionKit& pk = kit(side);
        const FactionKit& ak = kit(aiSide);
        // Monarchs face one another.
        float pFace = std::atan2(ax - px, az - pz);
        float aFace = std::atan2(px - ax, pz - az);
        playerMonarchId_ = spawn(pk.monarch, px, pz, pFace, 0);
        builderId_ = playerMonarchId_;
        aiMonarchId_ = spawn(ak.monarch, ax, az, aFace, 1);
        // Enough mogrium to bootstrap the opening: a handful of lodestones for
        // income and the start of a keep, without being able to skip economy
        // and rush one to completion.
        world_.player(0).mana = 2800;
        world_.player(1).mana = 2800;
        if (demo) {
            // Showcase: skip the slow build-up and pit two ready armies at the
            // start positions against each other.
            std::vector<int> playerA, playerB;
            for (int i = 0; i < 6; ++i) {
                int a = spawn(pk.squad[i % 4], px + float(i % 2) * 26,
                              pz - 60 + float(i / 2) * 30, pFace, 0);
                int b = spawn(ak.squad[i % 4], ax + float(i % 2) * 26,
                              az - 60 + float(i / 2) * 30, aFace, 1);
                if (a >= 0) playerA.push_back(a);
                if (b >= 0) playerB.push_back(b);
            }
            if (pk.keep[0]) keepId_ = spawn(pk.keep, px, pz + 60, pFace, 0);
            if (ak.keep[0]) aiKeepId_ = spawn(ak.keep, ax, az + 60, aFace, 1);
            if (!playerA.empty() && !playerB.empty()) {
                for (size_t k = 0; k < playerA.size(); ++k)
                    world_.attack(playerA[k], playerB[k % playerB.size()], false);
                for (size_t k = 0; k < playerB.size(); ++k)
                    world_.attack(playerB[k], playerA[k % playerA.size()], false);
            }
            // Showcase: pre-select the player's keep so the command panel shows its
            // conjure/build menu and the info bar shows the portrait (a fuller HUD for
            // screenshots). Harmless otherwise -- demo is a dev/showcase mode.
            if (keepId_ >= 0) selectOnly(keepId_);
            else if (!playerA.empty()) selectOnly(playerA.front());
        }
        }
        return false;
    }

    void GameView::setupPatrolPerf() {
        // Exactly 16,000 living units: eight monarchs plus 1,999 troops per
        // faction. This is a local developer fixture, not a lobby cap option.
        tak::sim::MatchConfig cfg;
        cfg.vfs = &vfs_;
        cfg.mapPath = mapPath_;
        cfg.stressTest = true;
        cfg.stressImmediate = true;   // counts and orders the whole army below
        cfg.unitCap = (1999 * 100 + 94) / 95;
        for (int p = 0; p < 8; ++p)
            cfg.slots.push_back({true, p % 5, 0, 2, true, false});
        const auto spots = tak::sim::setupMatch(world_, registry_, cfg);
        if (world_.units().size() != 16000)
            throw std::runtime_error("patrol performance map must fit exactly 16000 units");
        world_.setUnitCap(2000);
        for (const auto& u : world_.units())
            if (u.type && !u.type->isStructure())
                world_.patrol(u.id, float(world_.nav().width() * 16) - u.x.toFloat(),
                              u.z.toFloat());
        noFog_ = true;
        world_.setVisPlayer(-1);
        edgeScrollOn_ = false;
        patrolPerfAccum_ = 0.0f;
        float zoom = 0.9f;
        if (const char* value = tak::devEnv("TAK_PATROL_PERF_ZOOM"))
            zoom = std::clamp(float(std::atof(value)), 0.1f, 3.0f);
        mapView_.setZoom(zoom);
        if (!spots.empty()) lookAt(spots[0].first, spots[0].second);
        std::printf("PATROL_PERF initial=%zu balance=%s (local, no AI/combat/network)\n",
                    world_.units().size(), crusades_ ? "crusades" : "standard");
        std::fflush(stdout);
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
            hitQueue_.push_back({front().gameTick, hit});
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
            hitQueue_.push_back({front().gameTick, hit});
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
            const auto& definition=featureDefs_.at(burnType);
            debugFeatureFlameExpected_=int(!definition.valueOr("seqnamefrontflame","").empty())+
                int(!definition.valueOr("seqnamebackflame","").empty());
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


    void GameView::amphibDemo() {
        amphib_ = true;
        const auto* shipType = registry_.find("vertrans");
        const auto& ground = world_.nav();
        const auto& water = world_.navFor(shipType);
        float cx = float(mapView_.map().width) * 8, cz = float(mapView_.map().height) * 8;

        // Walk outward from the island center along a direction: last land
        // cell with deep water a bit beyond = a beach; return both spots.
        auto findBeach = [&](float ax, float az, float* bx, float* bz, float* wx2,
                             float* wz2) {
            for (float r = 0; r < 4000; r += 16) {
                int gx = int(cx + ax * r) / 16, gz = int(cz + az * r) / 16;
                if (!ground.walkable(gx, gz)) {
                    for (float rw = r + 48; rw < r + 400; rw += 16) {
                        int wxc = int(cx + ax * rw) / 16, wzc = int(cz + az * rw) / 16;
                        if (water.walkable(wxc, wzc)) {
                            *bx = cx + ax * (r - 32);
                            *bz = cz + az * (r - 32);
                            *wx2 = cx + ax * rw;
                            *wz2 = cz + az * rw;
                            return true;
                        }
                    }
                }
            }
            return false;
        };
        float bax, baz, wax, waz, bbx, bbz, wbx, wbz;
        if (!findBeach(-0.9f, 0.44f, &bax, &baz, &wax, &waz) ||
            !findBeach(0.44f, -0.9f, &bbx, &bbz, &wbx, &wbz)) {
            std::printf("amphib: no beaches found\n");
            return;
        }
        std::printf("amphib: embark beach (%.0f,%.0f) landing (%.0f,%.0f)\n", bax, baz,
                    bbx, bbz);
        amphibLandX_ = bbx;
        amphibLandZ_ = bbz;
        amphibSeaX_ = wbx;
        amphibSeaZ_ = wbz;

        transportId_ = spawn("vertrans", wax, waz, 0, 0);
        const char* squad[] = {"araarch", "araarch", "arasword", "arasword"};
        int i = 0;
        for (const char* t : squad) {
            int id = spawn(t, bax + float(i % 2) * 24 - 12, baz + float(i / 2) * 24 - 12,
                           0, 0);
            if (id >= 0) {
                world_.loadInto(id, transportId_);
                ++amphibSquad_;
            }
            ++i;
        }
    }


#endif   // !NDEBUG
