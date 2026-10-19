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
        // The squad marker's drawSolid(): every fully covered pixel is exactly the
        // requested colour, and (unsmoothed) it covers the same pixels as the tinted
        // draw(). The tinted draw is checked to be MULTI-coloured, so the test would
        // catch the marker going back to the shaded glyph art.
        const SDL_Color green{70, 240, 90, 255};
        const auto render = [&](bool solid, float scale) {
            const int tw = 96, th = 48;
            SDL_Texture* rt = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
            check(rt, "solid-text render target");
            SDL_Texture* prev = SDL_GetRenderTarget(ren_);
            SDL_SetRenderTarget(ren_, rt);
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0); SDL_RenderClear(ren_);
            if (solid) hudFont_.drawSolid(ren_, "2F0", 4, 30, scale, green);
            else hudFont_.draw(ren_, "2F0", 4, 30, scale, green);
            std::vector<uint8_t> px(size_t(tw) * th * 4);
            SDL_RenderReadPixels(ren_, nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), tw * 4);
            SDL_SetRenderTarget(ren_, prev);
            gpuvram::destroy(rt);
            return px;
        };
        for (bool on : {false, true}) {
            s.smoothArt = on; applySettings(s);
            for (float scale : {1.0f, 1.7f}) {
                const auto solid = render(true, scale), tinted = render(false, scale);
                size_t opaque = 0, sameCover = 0, covered = 0, offColour = 0;
                std::set<uint32_t> tintedColours;
                for (size_t p = 0; p < solid.size(); p += 4) {
                    if (solid[p + 3] == 255) {
                        ++opaque;
                        if (solid[p] != green.r || solid[p + 1] != green.g || solid[p + 2] != green.b) ++offColour;
                    }
                    if (tinted[p + 3] == 255)
                        tintedColours.insert(uint32_t(tinted[p]) << 16 | uint32_t(tinted[p + 1]) << 8 | tinted[p + 2]);
                    if (solid[p + 3] || tinted[p + 3]) { ++covered; sameCover += (solid[p + 3] > 0) == (tinted[p + 3] > 0); }
                }
                check(opaque > 40, "solid text drew nothing");
                check(offColour == 0, "solid text is not one flat colour");
                check(tintedColours.size() > 4, "tinted glyph art is unexpectedly flat (fixture lost its contrast)");
                if (!on && scale == 1.0f) check(sameCover == covered, "solid text changed the glyph shapes");
            }
        }
        s.smoothArt = initial; applySettings(s);
        std::puts("PASS: HUD smoothing applies live, keeps layout/state and releases textures");
        std::puts("PASS: solid HUD text is one flat colour in the glyph shapes");
    }

    void GameView::setupScenario() {
        writableWorld().setTerrain(mapView_.map().heights, mapView_.map().width,
                          mapView_.map().height, mapView_.map().seaLevel,
                          &mapView_.map().features);
        tak::sim::registerMapFeatures(writableWorld(), mapView_.map(), vfs_, &registry_);
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
                if (tak::sim::Unit* su = writableWorld().unit(uid)) {   // apply the .crt stats
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
            writableWorld().setScenario(std::make_unique<tak::sim::ScenarioScript>(
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
            writableWorld().setPlayerCount(n);
            for (int i = 0; i < n; ++i) writableWorld().setTeam(i, teams[size_t(i)]);
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
                writableWorld().player(i).godType = registry_.find(fkSide + "god");
                const FactionKit& fk = kit(fkSide);
                float mx = spots[size_t(i)].first, mz = spots[size_t(i)].second;
                int mon = spawn(fk.monarch, mx, mz, 0, i);
                if (i == 0) { playerMonarchId_ = mon; builderId_ = mon; }
                for (int s = 0; s < 4; ++s)
                    spawn(fk.squad[s % 4], mx + (s % 2) * 26 - 13, mz - 50 + (s / 2) * 26, 0, i);
                writableWorld().player(i).mana = 2800;
            }
            ffaPlayers_ = n;
            for (auto& u : world_.units()) {
                if (!u.type || u.type->canMove) continue;
                tak::sim::blockFootprint(writableWorld().nav(), *u.type, u.x.toFloat(), u.z.toFloat(), true);
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
        writableWorld().player(0).mana = 2800;
        writableWorld().player(1).mana = 2800;
        if (demo) {
            // Showcase: skip the slow build-up and pit two ready armies at the
            // start positions against each other.
            std::vector<int> playerA, playerB;
            // TAK_SHOT_SQUADS=1: a still life for the under-unit squad marker -- the
            // first three troops in control group 1, the rest in formation 2, the
            // monarch in group 10 ("0"), spread out with no keeps and no attack orders
            // so the labels neither overlap nor walk away.
            const bool squadShot = tak::devFlag("TAK_SHOT_SQUADS");
            const float sp = squadShot ? 2.5f : 1.0f;
            for (int i = 0; i < 6; ++i) {
                int a = spawn(pk.squad[i % 4], px + float(i % 2) * 26 * sp + (sp - 1) * 40,
                              pz - 60 * sp + float(i / 2) * 30 * sp, pFace, 0);
                int b = spawn(ak.squad[i % 4], ax + float(i % 2) * 26,
                              az - 60 + float(i / 2) * 30, aFace, 1);
                if (a >= 0) playerA.push_back(a);
                if (b >= 0) playerB.push_back(b);
            }
            if (pk.keep[0] && !squadShot) keepId_ = spawn(pk.keep, px, pz + 60, pFace, 0);
            if (ak.keep[0] && !squadShot) aiKeepId_ = spawn(ak.keep, ax, az + 60, aFace, 1);
            if (squadShot) {
                for (size_t k = 0; k < playerA.size(); ++k)
                    writableWorld().unit(playerA[k])->squad = k < 3 ? 1 : -2;
                if (auto* m = writableWorld().unit(playerMonarchId_)) m->squad = 10;
                std::fprintf(stderr, "squad shot: player army at %.0f %.0f\n", px, pz);
            }
            if (!squadShot && !playerA.empty() && !playerB.empty()) {
                for (size_t k = 0; k < playerA.size(); ++k)
                    writableWorld().attack(playerA[k], playerB[k % playerB.size()], false);
                for (size_t k = 0; k < playerB.size(); ++k)
                    writableWorld().attack(playerB[k], playerA[k % playerA.size()], false);
            }
            // Showcase: pre-select the player's keep so the command panel shows its
            // conjure/build menu and the info bar shows the portrait (a fuller HUD for
            // screenshots). Harmless otherwise -- demo is a dev/showcase mode.
            if (keepId_ >= 0) selectOnly(keepId_);
            else if (!playerA.empty() && !squadShot) selectOnly(playerA.front());
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
        const auto spots = tak::sim::setupMatch(writableWorld(), registry_, cfg);
        if (world_.units().size() != 16000)
            throw std::runtime_error("patrol performance map must fit exactly 16000 units");
        writableWorld().setUnitCap(2000);
        for (const auto& u : world_.units())
            if (u.type && !u.type->isStructure())
                writableWorld().patrol(u.id, float(writableWorld().nav().width() * 16) - u.x.toFloat(),
                              u.z.toFloat());
        noFog_ = true;
        writableWorld().setVisPlayer(-1);
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
            writableWorld().attack(a[i], b[i % b.size()], false);
        for (size_t i = 0; i < b.size(); ++i)
            writableWorld().attack(b[i], a[i % a.size()], false);
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
        // Formation flyers over the front (Legion, for --shot captures mid-move and on arrival with --time):
        // an Alt+1 formation of 30 swordsmen and 8 flyers right-clicked TAK_FLYER_FRONT px east of the map's
        // centre (negative: west). TAK_FLYER_FRONT_RETAIL=1 keeps Retail pathfinding for a comparison;
        // TAK_FLYER_FRONT_GROUND=N spawns N swordsmen instead of 30 (1: a squad narrower than its flyers).
        if (const char* front=tak::devEnv("TAK_FLYER_FRONT")) {
            writableWorld().setPathfindingMode(tak::devFlag("TAK_FLYER_FRONT_RETAIL") ? tak::sim::PathfindingMode::Retail
                                                                                       : tak::sim::PathfindingMode::Legion);
            writableWorld().setPathService(true);   // as a real match's setup (sim/matchsetup.cpp): Legion plans through it
            const float x0=float(mapView_.map().blocksX)*16.0f,z0=float(mapView_.map().blocksY)*16.0f,east=float(std::atof(front));
            const float side=east<0?1.f:-1.f;   // the block starts on the far side of the centre from its goal
            const char* flyer=tak::devEnv("TAK_FLYER_FRONT_TYPE");
            selection_.clear();
            const char* ground=tak::devEnv("TAK_FLYER_FRONT_GROUND");
            for(int i=0,n=ground?std::max(std::atoi(ground),1):30;i<n;++i)selection_.push_back(spawn("arasword",x0+side*(300+float(i/6)*40),z0-100+float(i%6)*40,0,localPlayer_));
            for(int i=0;i<8;++i)selection_.push_back(spawn(flyer?flyer:"arafly",x0+side*(500+float(i/4)*60),z0-90+float(i%4)*60,0,localPlayer_));
            std::erase_if(selection_,[](int id){return id<0;});
            for(const int id:selection_)issueSquad(id,-1);
            captureFrame();beginFrame();
            rightClickOrder(x0+east,z0,false);
            return;
        }
        // Info-bar portrait strip still life (--testbuild --shot): TAK_SELPORTRAITS=
        // "arasword:5,araarch:3,..." spawns that many of each type at the map centre and
        // selects them all, in that order (the first type leads the strip).
        if (const char* list=tak::devEnv("TAK_SELPORTRAITS")) {
            const float x0=float(mapView_.map().blocksX)*16.0f,z0=float(mapView_.map().blocksY)*16.0f;
            selection_.clear();
            std::string spec(list);
            int k=0;
            for (size_t p=0;p<spec.size();) {
                size_t comma=spec.find(',',p);
                std::string one=spec.substr(p,comma==std::string::npos?std::string::npos:comma-p);
                size_t colon=one.find(':');
                const std::string type=one.substr(0,colon);
                const int n=colon==std::string::npos?1:std::max(1,std::atoi(one.c_str()+colon+1));
                for (int i=0;i<n;++i,++k)
                    selection_.push_back(spawn(type,x0-300+float(k%16)*40,z0-120+float(k/16)*40,0,localPlayer_));
                if (comma==std::string::npos) break;
                p=comma+1;
            }
            std::erase_if(selection_,[](int id){return id<0;});
            // TAK_SELPORTRAITS_HURT: every other unit at a partial HP and every caster at a
            // partial mana pool, so the bar's group totals differ from any one unit's.
            // TAK_SELPORTRAITS_SQUAD=N (-N formation): put them all in that squad.
            const bool hurt=tak::devFlag("TAK_SELPORTRAITS_HURT");
            const char* sq=tak::devEnv("TAK_SELPORTRAITS_SQUAD");
            for (size_t i=0;i<selection_.size();++i) {
                auto* su=writableWorld().unit(selection_[i]);
                if (!su || !su->type) continue;
                if (hurt && i%2==0) su->hp=tak::sim::Fixed::fromFloat(float(su->type->maxHp)*(0.25f+0.1f*float(i%5)));
                if (hurt && su->type->maxMana>0) su->mana=double(su->type->maxMana)*(i%3==0?0.2:0.9);
                if (sq) su->squad=int8_t(std::atoi(sq));
            }
            // TAK_SELPORTRAITS_CONJURE=type: the front unit (a builder) lays that site at
            // the first legal spot near it, a third built, so the bar shows a conjure target.
            if (const char* ct=tak::devEnv("TAK_SELPORTRAITS_CONJURE"); ct && !selection_.empty())
                if (const auto* tt=registry_.find(ct); tt)
                    if (const auto* b=world_.unit(selection_.front()))
                        for (float r=90,bx=b->x.toFloat(),bz=b->z.toFloat();r<400;r+=24)
                            for (float a=0;a<6.28f;a+=0.5f)
                                if (const float x=bx+std::cos(a)*r,z=bz+std::sin(a)*r;world_.canPlace(tt,x,z))
                                    if (const int site=writableWorld().startBuild(selection_.front(),tt,x,z)) {
                                        writableWorld().unit(site)->hp=tak::sim::Fixed::fromFloat(float(tt->maxHp)/3);
                                        r=400;break;
                                    }
            noFog_=true;edgeScrollOn_=false;
            mapView_.setOffset(x0-640/mapView_.zoom(),z0-400/mapView_.zoom());
            captureFrame();   // publish them, or the next frame pin prunes the selection
            std::fprintf(stderr,"selportraits: %zu units selected\n",selection_.size());
            return;
        }
        if (tak::devFlag("TAK_SPECTATOR_BARS_TEST")) {
            // A spectator sees every side's health bars. Spectating stops the fog
            // pass, but the world keeps the last fog it built for the seat the client
            // was given (localPlayer_), so the overlays must answer from noFog_ and
            // not from that stale buffer. As a player the same fixture still hides
            // an enemy's bar under fog.
            const auto check=[](bool ok,const std::string& what) {
                if (!ok) throw std::runtime_error("spectator bars: "+what);
                std::fprintf(stderr,"PASS: spectator bars: %s\n",what.c_str());
            };
            constexpr int W=1280,H=960;
            constexpr float zoom=0.6f;
            noFog_=false;edgeScrollOn_=false;tacticalDotsOpt_=false;healthBars_=1;
            writableWorld().setPlayerCount(3);
            const float x0=float(mapView_.map().blocksX)*16.0f,z0=float(mapView_.map().blocksY)*16.0f;
            const int own=spawn("arasword",x0,z0,0,0);
            const int foeSeen=spawn("arasword",x0+60,z0-80,0,1);
            const int foeHidden=spawn("arasword",x0+700,z0+200,0,1);
            const int thirdHidden=spawn("arasword",x0-700,z0-200,0,2);
            check(own>=0 && foeSeen>=0 && foeHidden>=0 && thirdHidden>=0,"fixture spawned");
            check(!world_.allied(0,1) && !world_.allied(0,2) && !world_.allied(1,2),"three separate sides");
            // A third of a second of sim builds the seat's fog the way a game's opening does.
            for (int i=0;i<10;++i) writableWorld().tick(1.f/30);
            for (const int id:{own,foeSeen,foeHidden,thirdHidden})
                if (auto* u=writableWorld().unit(id)) u->hp=tak::sim::Fixed::fromFloat(float(u->type->maxHp)/2);
            const auto frame=[&] {
                captureFrame();beginFrame();cosmeticStep(0);
                mapView_.setZoom(zoom);
                mapView_.setOffset(x0-float(W-300)*0.5f/zoom,z0-float(H)*0.5f/zoom);
                prepare(W,H);draw(W,H);
                endFrame();
            };
            const auto bar=[&](int id) {
                return std::find(debugHealthBarIds_.begin(),debugHealthBarIds_.end(),id)!=debugHealthBarIds_.end();
            };
            frame();
            check(bar(own) && bar(foeSeen),"player: own and in-sight enemy bars drawn");
            check(!bar(foeHidden) && !bar(thirdHidden),"player: enemy bars under fog stay hidden");
            beginSpectating();
            frame();
            check(!frameVisibility().empty(),"spectator: the seat's fog buffer is still there (stale)");
            check(bar(own) && bar(foeSeen) && bar(foeHidden) && bar(thirdHidden),
                  "spectator: every side's bars drawn");
            std::fprintf(stderr,"PASS: spectator bars (%zu bars)\n",debugHealthBarIds_.size());
            return;
        }
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
            writableWorld().setPlayerCount(2);
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
            {   // World effects and shots follow the dots: an explosion glow in plain
                // sight draws with models, not with dots, and survives the dots frame
                // (drawing is skipped, the effect itself is not touched).
                explosionGlows_.clear();
                explosionGlows_.push_back({x0,z0,0,front().gameTick,0,std::nullopt});
                frame(zIn);
                check(!worldFxFrame() && debugGlowDrawCount_==0,"dots: no explosion glow drawn");
                check(explosionGlows_.size()==1,"dots: the skipped glow is still alive");
                tacticalDotsOpt_=false;frame(zIn);
                check(worldFxFrame() && debugGlowDrawCount_==1,"models: the same glow draws again");
                tacticalDotsOpt_=true;frame(zIn);
                check(dotsFrame_ && debugGlowDrawCount_==0,"back to dots: glow hidden again");
                explosionGlows_.clear();
            }
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
                    retiredFixtureId=writableWorld().spawn(&dummy,1800,2100,0,0);
                    writableWorld().scenarioDestroy(retiredFixtureId);
                }
                for(int i=0;i<60;++i)writableWorld().tick(1.f/30);
                for(const auto& u:world_.units())
                    if(u.type==&dummy)throw std::runtime_error("UI fixture retained final corpses");
            }
            writableWorld().setPlayerCount(8);writableWorld().setTeam(1,0);localPlayer_=0;
            resultParticipants_=uint8_t((1u<<0)|(1u<<1)|(1u<<2)|(1u<<7));
            playerName_[0]="PLAYER";playerName_[1]="ALLY";playerName_[2]="ENEMY";playerName_[7]="DEFEATED";
            {   // Humans in slots 0 (you), 1 and 7; the ENEMY in slot 2 is a computer player.
                tak::net::RoomView room;
                for(int i:{0,1,7})room.slots[i].type=1;
                room.slots[2].type=2;
                resetDiplomacy(room);
            }
            writableWorld().player(7).defeated=true;hotkeys_.load({});
            const int monarch=spawn("araking",1800,2100,0,0);
            const int soldier=spawn("arasword",1650,2100,0,0);
            spawn("arasword",2000,2100,0,1);spawn("tarsword",2200,2100,0,2);
            writableWorld().unit(soldier)->squad=3;
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
            // VOICE: chat's column again, same defaults (everyone) and the same players; your
            // own box is shown but inert (your voice never comes back to you).
            check(voiceRecipients_==chatRecipients_ && voiceRecipients_==0xff,"voice defaults equal chat's");
            for(int p:{1,2,7})check(has(DiplomacyAction::Voice,p),"every other participant needs a voice control");
            check(!has(DiplomacyAction::Voice,0) && !has(DiplomacyAction::Voice,3),"no voice control for yourself or an empty slot");
            click(DiplomacyAction::Chat,2);check(!(chatRecipients_ & 4),"chat checkbox updates recipients");
            check(voiceRecipients_==0xff,"the chat column leaves voice alone");
            click(DiplomacyAction::Voice,2);check(!(voiceRecipients_ & 4) && (voiceRecipients_ & 2),"voice checkbox updates its recipients");
            click(DiplomacyAction::Voice,2);check(voiceRecipients_==0xff,"and back");
            // VOLUME: a slider for each other human (ALLY, DEFEATED), none for yourself or the
            // computer player, every one at 100; greyed and inert while voice chat is off.
            const auto allFull=[&]{return std::all_of(voiceVolume_.begin(),voiceVolume_.end(),[](uint8_t v){return v==100;});};
            const auto volumeHot=[&](int p) {
                auto found=std::find_if(giveUnitsHots_.begin(),giveUnitsHots_.end(),[&](const auto& h){return h.action==DiplomacyAction::Volume && h.player==p;});
                check(found!=giveUnitsHots_.end(),"VOLUME slider unavailable");
                return found->rect;
            };
            const auto mouse=[&](Uint32 type,float mx,float my,bool held) {
                SDL_Event e{};e.type=type;
                if(type==SDL_MOUSEMOTION){e.motion.x=int(mx);e.motion.y=int(my);e.motion.state=held?SDL_BUTTON_LMASK:0;}
                else{e.button.button=SDL_BUTTON_LEFT;e.button.x=int(mx);e.button.y=int(my);}
                input(e,1000,700);
            };
            check(allFull(),"every VOLUME starts at 100");
            check(voiceVolumeSlider(1) && voiceVolumeSlider(7),"a VOLUME slider for every other human");
            check(!voiceVolumeSlider(0) && !voiceVolumeSlider(2) && !voiceVolumeSlider(3),"none for yourself, a computer player or an empty slot");
            check(!has(DiplomacyAction::Volume,1) && !has(DiplomacyAction::Volume,7),"no voice engine: the sliders are greyed");
            tak::VoiceEngine engine;   // opens nothing: it is never pumped, only asked whether voice is on
            tak::Settings voiceCfg;
            engine.configure(voiceCfg,"");voice_=&engine;drawGiveUnitsMenu(1000,700);
            check(!has(DiplomacyAction::Volume,1) && !has(DiplomacyAction::Volume,7),"voice chat off: the sliders are greyed");
            {   // ...and inert: a press where ALLY's slider sits changes nothing.
                voiceCfg.voiceEnabled=true;engine.configure(voiceCfg,"");drawGiveUnitsMenu(1000,700);
                const SDL_FRect r=volumeHot(1);
                voiceCfg.voiceEnabled=false;engine.configure(voiceCfg,"");drawGiveUnitsMenu(1000,700);
                mouse(SDL_MOUSEBUTTONDOWN,r.x+r.w*0.3f,r.y+r.h/2,true);mouse(SDL_MOUSEBUTTONUP,r.x+r.w*0.3f,r.y+r.h/2,false);
                check(allFull() && voiceVolumeDrag_<0,"a greyed slider ignores the mouse");
            }
            voiceCfg.voiceEnabled=true;engine.configure(voiceCfg,"");drawGiveUnitsMenu(1000,700);
            check(has(DiplomacyAction::Volume,1) && has(DiplomacyAction::Volume,7),"voice chat on: the sliders are live");
            check(!has(DiplomacyAction::Volume,0) && !has(DiplomacyAction::Volume,2) && !has(DiplomacyAction::Volume,3),
                  "still no slider for yourself or the computer player");
            {
                const SDL_FRect r=volumeHot(1);
                mouse(SDL_MOUSEBUTTONDOWN,r.x+r.w*0.3f,r.y+r.h/2,true);
                check(voiceVolume_[1]>=29 && voiceVolume_[1]<=31,"a press sets ALLY's VOLUME where it lands");
                mouse(SDL_MOUSEBUTTONUP,r.x+r.w*0.3f,r.y+r.h/2,false);
                // A new game puts every slider back to 100.
                tak::net::RoomView room;
                for(int i:{0,1,7})room.slots[i].type=1;
                room.slots[2].type=2;
                resetDiplomacy(room);
                check(allFull() && humanSlots_==((1u<<0)|(1u<<1)|(1u<<7)),"a new game starts every VOLUME at 100");
                input(key,1000,700);drawGiveUnitsMenu(1000,700);check(giveUnitsMenu_,"diplomacy reopens");
                // Drag: past either end clamps to 0..100, and the release, wherever it lands,
                // ends the drag without clicking what is under it (ENEMY's CHAT box here).
                mouse(SDL_MOUSEBUTTONDOWN,r.x+r.w*0.5f,r.y+r.h/2,true);
                check(voiceVolume_[1]>=49 && voiceVolume_[1]<=51 && voiceVolumeDrag_==1,"a press starts a drag");
                mouse(SDL_MOUSEMOTION,r.x+r.w+80,r.y-40,true);check(voiceVolume_[1]==100,"dragged past the end: 100");
                mouse(SDL_MOUSEMOTION,r.x-80,r.y,true);check(voiceVolume_[1]==0,"dragged past the start: 0");
                mouse(SDL_MOUSEMOTION,r.x+r.w*0.5f,r.y,true);
                const uint8_t chat=chatRecipients_;
                auto box=std::find_if(giveUnitsHots_.begin(),giveUnitsHots_.end(),[&](const auto& h){return h.action==DiplomacyAction::Chat && h.player==2;});
                check(box!=giveUnitsHots_.end(),"ENEMY has a CHAT box");
                mouse(SDL_MOUSEBUTTONUP,box->rect.x+box->rect.w/2,box->rect.y+box->rect.h/2,false);
                check(voiceVolumeDrag_<0 && chatRecipients_==chat && voiceVolume_[1]>=49 && voiceVolume_[1]<=51,
                      "releasing a drag over another control only ends the drag");
                check(voiceVolume_[7]==100 && voiceVolume_[0]==100 && voiceVolume_[2]==100,"one slider moves one player");
                const SDL_FRect r7=volumeHot(7);
                mouse(SDL_MOUSEBUTTONDOWN,r7.x+r7.w*0.25f,r7.y+r7.h/2,true);mouse(SDL_MOUSEBUTTONUP,r7.x+r7.w*0.25f,r7.y+r7.h/2,false);
                check(voiceVolume_[7]>=24 && voiceVolume_[7]<=26 && voiceVolume_[1]>=49 && voiceVolume_[1]<=51,"DEFEATED's slider on its own");
            }
            if(const char* cap=tak::devEnv("TAK_DIPLOMACY_CAPTURE")) {   // the screen as drawn, to look at
                SDL_SetRenderDrawColor(ren_,40,60,40,255);SDL_RenderClear(ren_);drawGiveUnitsMenu(1000,700);
                int ow=0,oh=0;SDL_GetRendererOutputSize(ren_,&ow,&oh);
                std::vector<uint8_t> px(size_t(ow)*size_t(oh)*4);
                if (SDL_RenderReadPixels(ren_,nullptr,SDL_PIXELFORMAT_RGBA32,px.data(),ow*4)==0)tak::png::write(cap,ow,oh,px);
                voiceCfg.voiceEnabled=false;engine.configure(voiceCfg,"");
                SDL_SetRenderDrawColor(ren_,40,60,40,255);SDL_RenderClear(ren_);drawGiveUnitsMenu(1000,700);
                if (SDL_RenderReadPixels(ren_,nullptr,SDL_PIXELFORMAT_RGBA32,px.data(),ow*4)==0)
                    tak::png::write(std::string(cap)+".off.png",ow,oh,px);
                voiceCfg.voiceEnabled=true;engine.configure(voiceCfg,"");
            }
            // Push to talk never fires while a text field has focus.
            check(!textEntryActive(),"no text field focused");
            chatTyping_=true;check(textEntryActive(),"the chat composer counts as typing");chatTyping_=false;
            SDL_StartTextInput();check(textEntryActive(),"any focused text field counts as typing");SDL_StopTextInput();
            lbField_=4;check(!textEntryActive(),"a lobby field still marked once the game runs is not typing");lbField_=0;
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
            voice_=nullptr;   // the harness's engine is not the game's
            std::fprintf(stderr,"PASS: diplomacy hotkey, roster, defaults, checkboxes, voice column, volume sliders, eligibility, gift and selection cleanup\n");
            return;
        }
        if (tak::devFlag("TAK_CURSOR_TEST")) {
            noFog_=true;edgeScrollOn_=false;
            writableWorld().setPlayerCount(8);
            const int builder=spawn("araking",1800,2100,0,0);
            const int soldier=spawn("arasword",1650,2100,0,0);
            const int site=spawn("creacad",2000,2000,0,0);
            writableWorld().unit(site)->underConstruction=true;writableWorld().unit(site)->buildBegun=true;
            writableWorld().unit(site)->hp=tak::sim::Fixed::fromInt(10);
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
            writableWorld().cancelBuilds(builder);writableWorld().stop(builder);SDL_SetModState(savedMod);publish();point(site);
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
            writableWorld().stop(builder);SDL_SetModState(savedMod);publish();point(site);
            std::fprintf(stderr,"PASS: Shift placement release clears the build icon, keeps the builder and queue, supports both keys and mid-drag release\n");
            // Armed orders follow the same rule: Shift queues the order and keeps
            // the command armed for the next click; releasing Shift disarms.
            writableWorld().stop(builder);publish();
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
            writableWorld().stop(builder);SDL_SetModState(savedMod);publish();point(site);
            std::fprintf(stderr,"PASS: Shift keeps an armed order armed and queues each click; releasing Shift disarms\n");
            const auto* manaType=registry_.find("aralode");
            if(!manaType || manaSpots_.empty())throw std::runtime_error("area trail fixture needs lodestones and deposits");
            writableWorld().queueManaBuildArea(builder,manaType,0,0,float(mapView_.map().width*16-1),
                float(mapView_.map().height*16-1),false);
            writableWorld().order(builder,2200,2300,true);
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
            writableWorld().stop(builder);publish();drawOrderTrails(mapViewW(winW_),winH_);
            SDL_SetModState(savedMod);point(site);
            std::fprintf(stderr,"PASS: live area trail shows remaining sites and queued tail without changing simulation state\n");
            const auto* original=world_.unit(builder)->type;
            auto restricted=*original;restricted.builderLimited=true;
            writableWorld().unit(builder)->type=&restricted;publish();point(site);
            expect(tak::CursorId::Green,"restricted builder cannot assist an unavailable type");
            pendingCmd_='r';expect(tak::CursorId::Normal,"armed repair refuses unavailable construction");
            pendingCmd_=0;writableWorld().unit(builder)->type=original;publish();point(site);
            rightClickOrder(2000,2000,false);
            if (world_.unit(builder)->buildSiteId!=site) throw std::runtime_error("assist cursor click did not assist");
            writableWorld().cancelBuilds(builder);writableWorld().stop(builder);
            writableWorld().unit(site)->underConstruction=false;writableWorld().unit(site)->hp=tak::sim::Fixed::fromInt(10);
            publish();point(site);
            expect(tak::CursorId::Repair,"damaged friendly advertises repair");
            rightClickOrder(2000,2000,false);
            if (world_.unit(builder)->orders.empty() || world_.unit(builder)->orders.back().repairTarget!=site)
                throw std::runtime_error("repair cursor click did not repair");
            writableWorld().stop(builder);selection_.clear();
            expect(tak::CursorId::Select,"unselected building uses its displayed bounds");
            selection_={soldier};pendingCmd_='r';
            expect(tak::CursorId::Normal,"non-builder cannot arm repair");
            pendingCmd_='u';expect(tak::CursorId::Normal,"non-transport cannot arm unload");
            pendingCmd_='c';expect(tak::CursorId::Normal,"non-reclaimer cannot arm reclaim");
            pendingCmd_=0;
            const auto savedPathfinding=world_.pathfindingMode();
            selection_={builder,soldier};mouseX_=mouseY_=-1000;
            for(const auto mode:{tak::sim::PathfindingMode::Retail,tak::sim::PathfindingMode::Legion}) {
                writableWorld().setPathfindingMode(mode);publish();
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
                    writableWorld().stop(id);
                }
            }
            const int flyer=spawn("zonhunt",1650,2100,0,0);
            selection_={builder,flyer};publish();
            rightClickOrder(2400,2400,false);
            if(world_.unit(builder)->orders.back().x.toFloat()!=2400.f ||
               world_.unit(flyer)->orders.back().x.toFloat()!=2340.f)
                throw std::runtime_error("shared group right-click changed independent flight destinations");
            writableWorld().stop(builder);writableWorld().stop(flyer);
            writableWorld().setPathfindingMode(savedPathfinding);
            std::fprintf(stderr,"PASS: group right-click shares supported destination areas and preserves Retail/fallback offsets\n");
            resultParticipants_=uint8_t((1u<<0)|(1u<<3));
            playerName_[0]="PLAYER";playerName_[3]="AI";
            writableWorld().player(3).defeated=true;publish();
            const auto results=resultStats();
            if (results.rows.size()!=2 || results.rows[1].name!="AI" || !results.rows[1].defeated)
                throw std::runtime_error("sparse results roster lost a participant or included an empty slot");
            std::fprintf(stderr,"PASS: sparse results show participants, including defeated players\n");
            resultParticipants_.reset();selection_={builder};
            return;
        }
        // Repeatable rendering workload for the shadow performance regression.
        // This entry point is only reached by the local development harness.
        // Render-optimization matrix knobs (tools/render_benchmark.py):
        //   TAK_RENDER_CENTER=x,z   army centre (default 5000,5000)
        //   TAK_RENDER_OFFSET=x,z   camera top-left world offset after setup
        //   TAK_RENDER_EXTRAS=1     add construction sites, ghosts, flyers, corpses
        //   TAK_RENDER_NAVAL=N      N floating units on the water nearest the centre
        //   TAK_RENDER_FOREST=1     centre army + camera on the densest scenery region
        //   TAK_RENDER_ROSTER=a,b   unit types replacing the 6-type roster ("structures": all of them)
        //   TAK_RENDER_OVERLAYS=1   control squads, factories with production queues,
        //                           builders with queued build orders (ghosts)
        // All of them only shape the local harness scene; none run in a real game.
        if (const char* countText=tak::devEnv("TAK_SHADOW_BENCH")) {
            const int count=std::clamp(std::atoi(countText),1,16000);
            float centerX=5000,centerZ=5000;
            if(const char* center=tak::devEnv("TAK_RENDER_CENTER"))
                std::sscanf(center,"%f,%f",&centerX,&centerZ);
            int forestTrees=0;
            if(tak::devFlag("TAK_RENDER_FOREST")) {
                // Centre on the 512px region holding the most scenery (trees count
                // double, since they sway and carry shadows); first wins a tie.
                std::map<std::pair<int,int>,int> trees;
                for(const auto& f:features_)if(f.tex)trees[{int(f.x)/512,int(f.z)/512}]+=f.tree ? 2 : 1;
                for(const auto& [cell,n]:trees)if(n>forestTrees) {
                    forestTrees=n;centerX=float(cell.first)*512+256;centerZ=float(cell.second)*512+256;
                }
            }
            const char* only=tak::devEnv("TAK_SHADOW_BENCH_TYPE");
            std::vector<std::string> roster={"zonter","araarch","aralode","tarlode","zonroc","versword"};
            if(const char* custom=tak::devEnv("TAK_RENDER_ROSTER")) {
                // Comma-separated unit types replacing the 6-type roster.
                roster.clear();
                std::string list=custom;
                for(size_t at=0;at<=list.size();) {
                    const size_t comma=std::min(list.find(',',at),list.size());
                    if(comma>at)roster.push_back(list.substr(at,comma-at));
                    at=comma+1;
                }
                if(roster.size()==1 && roster[0]=="structures") {
                    // Every structure type, by id (workstream M: palette-lit models).
                    roster.clear();
                    for(const auto& [id,type]:registry_.types())if(type.isStructure())roster.push_back(id);
                    std::sort(roster.begin(),roster.end());
                    std::string list;
                    for(const auto& id:roster)list+=(list.empty() ? "" : ",")+id;
                    std::fprintf(stderr,"render roster: %s\n",list.c_str());
                }
                if(roster.empty())roster.push_back("araarch");
            }
            const int columns=std::max(1,int(std::ceil(std::sqrt(float(count)*1.6f))));
            const char* spacingText=tak::devEnv("TAK_RENDER_SPACING");
            const float spacing=spacingText?std::clamp(float(std::atof(spacingText)),16.f,256.f):40.f;
            const int rows=(count+columns-1)/columns;
            int spawned=0;
            std::vector<int> spawnedIds;
            for(int i=0;i<count;++i) {
                const char* name=only ? only : roster[size_t(i)%roster.size()].c_str();
                const auto* type=registry_.find(name);
                if(!type || world_.atUnitCap(localPlayer_) || world_.atTypeCap(localPlayer_,type)) continue;
                const float x=centerX+(float(i%columns)-float(columns-1)*0.5f)*spacing;
                const float z=centerZ+(float(i/columns)-float(rows-1)*0.5f)*spacing;
                const bool battle=tak::devFlag("TAK_RENDER_BATTLE");
                const int owner=battle?i%2:localPlayer_;
                if(const int id=spawn(name,x,z,float(i%8)*0.785398163f,owner)) {
                    ++spawned;spawnedIds.push_back(id);
                    if(tak::devFlag("TAK_RENDER_MOTION"))writableWorld().order(id,x+320,z+160,false);
                    if(battle)writableWorld().attackMove(id,centerX,centerZ,false);
                }
            }
            int sites=0,ghosts=0,flyers=0,corpses=0,boats=0;
            if(tak::devFlag("TAK_RENDER_EXTRAS")) {
                // Construction: monarchs start real build sites south of the army.
                const float siteZ=centerZ+float(rows)*spacing*0.5f+160;
                for(int k=0;k<6;++k) {
                    const float bx=centerX+float(k-3)*160,bz=siteZ;
                    const int builder=spawn("araking",bx,bz+96,0,localPlayer_);
                    if(builder<=0)continue;
                    for(const auto& option:registry_.buildable("araking")) {
                        const auto* site=registry_.find(option);
                        if(!site || !isStructure(site) || site->footX>4 || site->footZ>4)continue;
                        if(writableWorld().startBuild(builder,site,bx,bz)) {++sites;break;}
                    }
                }
                // Translucent spectral bodies and airborne units north of the army.
                const float northZ=centerZ-float(rows)*spacing*0.5f-160;
                for(const auto& [id,type]:registry_.types()) {
                    if(type.ghost && type.canMove && ghosts<8)
                        for(int k=0;k<8 && ghosts<8;++k)
                            ghosts+=spawn(id,centerX-480+float(k)*48,northZ,0,localPlayer_)>0;
                    if(type.canFly && type.canMove && flyers<16 && id.rfind("zon",0)!=0)
                        for(int k=0;k<4 && flyers<16;++k)
                            flyers+=spawn(id,centerX+120+float(flyers)*56,northZ-48,0,localPlayer_)>0;
                }
                // Corpses: every 9th grid unit dies at once and leaves its body.
                for(size_t k=0;k<spawnedIds.size();k+=9) {writableWorld().destroy(spawnedIds[k]);++corpses;}
            }
            int squads=0,factories=0,ghostQueues=0;
            if(tak::devFlag("TAK_RENDER_OVERLAYS")) {
                // Per-unit overlays (workstream L): control squads on every fifth grid unit
                // (every third of those a formation), factories across the middle of the
                // army with long production queues (progress bars), and builders with
                // queued build orders (ghosts).
                for(size_t k=0;k<spawnedIds.size();k+=5) {
                    const int n=int(k/5)%10+1;
                    writableWorld().setSquad(spawnedIds[k],(k/5)%3==2 ? -n : n);++squads;
                }
                for(const auto& [id,type]:registry_.types()) {
                    if(factories>=4)break;
                    if(!type.producesUnits())continue;
                    const tak::sim::UnitType* product=nullptr;
                    for(const auto& option:registry_.buildable(id))
                        if(const auto* t=registry_.find(option);t && !isStructure(t)) {product=t;break;}
                    if(!product)continue;
                    const float fx=centerX+(float(factories)-1.5f)*500,fz=centerZ;   // in the army's middle
                    if(const int factory=spawn(id,fx,fz,0,localPlayer_);factory>0) {
                        writableWorld().train(factory,product,50);++factories;
                    }
                }
                // Builders with queued (shift) build orders: the queued-build ghosts.
                for(int k=0;k<3;++k) {
                    const float bx=centerX+float(k-1)*600,bz=centerZ+300;
                    const int builder=spawn("araking",bx,bz,0,localPlayer_);
                    if(builder<=0)continue;
                    for(const auto& option:registry_.buildable("araking")) {
                        const auto* site=registry_.find(option);
                        if(!site || !isStructure(site) || site->footX>4 || site->footZ>4)continue;
                        for(int q=0;q<3;++q)writableWorld().queueBuild(builder,site,bx+float(q)*96,bz+160,true);
                        ++ghostQueues;break;
                    }
                }
            }
            if(const char* navalText=tak::devEnv("TAK_RENDER_NAVAL")) {
                const int wanted=std::clamp(std::atoi(navalText),1,2000);
                std::vector<const tak::sim::UnitType*> hulls;
                for(const auto& [id,type]:registry_.types())
                    if(type.floater && type.canMove && !type.canFly && !isStructure(&type))hulls.push_back(&type);
                const auto& map=mapView_.map();
                // Water cells nearest the centre, in a deterministic distance order.
                std::vector<std::pair<float,std::pair<int,int>>> water;
                for(int z=1;z+1<map.height;z+=3)for(int x=1;x+1<map.width;x+=3) {
                    const float wx=(float(x)+0.5f)*16,wz=(float(z)+0.5f)*16;
                    if(world_.isWater(wx,wz))
                        water.push_back({(wx-centerX)*(wx-centerX)+(wz-centerZ)*(wz-centerZ),{x,z}});
                }
                std::sort(water.begin(),water.end());
                for(size_t k=0;k<water.size() && boats<wanted && !hulls.empty();++k) {
                    const auto [x,z]=water[k].second;
                    boats+=spawn(hulls[size_t(boats)%hulls.size()]->id,(float(x)+0.5f)*16,(float(z)+0.5f)*16,
                                 float(boats%8)*0.785398163f,localPlayer_)>0;
                }
                if(!water.empty()) {
                    centerX=(float(water[0].second.first)+0.5f)*16;centerZ=(float(water[0].second.second)+0.5f)*16;
                }
            }
            noFog_=!tak::devFlag("TAK_PROFILE_FOG");edgeScrollOn_=false;
            float zoom=std::min(1.5f,std::min(1400.f/(columns*40),760.f/(rows*40)));
            if(const char* requested=tak::devEnv("TAK_PROFILE_ZOOM"))
                zoom=std::clamp(float(std::atof(requested)),0.05f,4.0f);   // the game's zoom range
            mapView_.setZoom(zoom);
            mapView_.setOffset(centerX-760/zoom,centerZ-terrainLift(centerX,centerZ)-420/zoom);
            if(const char* offset=tak::devEnv("TAK_RENDER_OFFSET")) {
                float ox=0,oz=0;
                if(std::sscanf(offset,"%f,%f",&ox,&oz)==2)mapView_.setOffset(ox,oz);
            }
            std::fprintf(stderr,"shadow benchmark: requested=%d spawned=%d columns=%d zoom=%.3f type=%s\n",
                count,spawned,columns,zoom,only ? only : "mixed");
            std::fprintf(stderr,"render scene: sites=%d ghosts=%d flyers=%d corpses=%d boats=%d scenery=%d squads=%d factories=%d build_queues=%d center=%.0f,%.0f offset=%.0f,%.0f\n",
                sites,ghosts,flyers,corpses,boats,forestTrees,squads,factories,ghostQueues,centerX,centerZ,mapView_.offX(),mapView_.offY());
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
                    tak::sim::applyCommand(writableWorld(),registry_,command);
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
                writableWorld().unit(targetId)->hp=tak::sim::Fixed::fromFloat(targetType->maxHp*0.5f);
                tak::net::Command command;command.kind=tak::net::Cmd::Repair;
                command.unitId=id;command.targetId=targetId;command.player=localPlayer_;
                tak::sim::applyCommand(writableWorld(),registry_,command);
                std::printf("repair capture: worker #%d -> target #%d\n",id,targetId);
            } else writableWorld().queueBuild(id,targetType,siteX,siteZ,false);
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
                    int id = writableWorld().startBuild(builderId_, lode, x, z);
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
            writableWorld().order(id, cx+ddx[i], cz+ddz[i], false);
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
                for(const auto& frame:source->second.frames) {
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
            // The pre-M source record held a palette copy, a shade-table copy and RGBA
            // frames per texture: its byte count for the same sources (excluding names
            // and sheet records, which are unchanged).
            size_t before=0,after=0;
            for(const auto& [n,source]:paletteTextureSources_) {
                before+=sizeof(tak::gaf::Palette)+tak::render::palette::kShadeTableBytes+
                        source.frames.size()*sizeof(tak::gaf::Frame);
                after+=source.frames.capacity()*sizeof(tak::render::palette::Frame);
                for(const auto& frame:source.frames) {before+=size_t(frame.width)*frame.height*4;after+=frame.la.capacity();}
            }
            for(const auto& bank:paletteBanks_)after+=sizeof(bank)+bank.shades.capacity();
            std::fprintf(stderr,"PALETTE_ROSTER pages=%zu missing=%zu mode=%s gpuMiB=%.2f cpuMiB=%.2f uploadMiB=%.2f "
                "sources=%zu sourceMiB=%.2f preMSourceMiB=%.2f\n",
                palettePages_.size(),missing,paletteMode_==PaletteMode::Indexed ? "indexed" : "baked",
                double(paletteGpuBytes())/(1024*1024),double(paletteCpuBytes())/(1024*1024),
                double(paletteUploadBytes_)/(1024*1024),paletteTextureSources_.size(),
                double(after)/(1024*1024),double(before)/(1024*1024));
            if(missing)throw std::runtime_error("shipped structure palette sheets exceed capacity");
            std::fprintf(stderr,"PASS: full structure palette texture roster fits bounded pages\n");
            return;
        }
        float cx = mapView_.map().blocksX * 16.0f, cz = mapView_.map().blocksY * 16.0f;
        if (tak::devFlag("TAK_CREON_DEATH_SOUND_TEST")) {
            // Every Creon unit whose Killed/Dying authors PLAY_SOUND, plus two
            // non-Creon archers as controls, killed through the combat damage
            // path and heard through the real client pipeline: the simulation
            // runs Killed/Dying on the shared game stream and queues what each
            // dying VM plays (World::scriptSounds); the client drains that into
            // sounds_ behind the class/fog/selection gate and drops its display
            // VM's own death sounds. Per death:
            //  (a) the client requests exactly the sounds the simulation chose
            //      for that unit, in order;
            //  (b) another peer hears the same. TAK_CREON_DEATH_SOUND_CATCHUP=1
            //      replays this game with each batch presented late, as after a
            //      rejoin or a spectator catch-up, so its display VMs register
            //      later and roll fewer idle RANDs before the hit;
            //      tools/creon_death_sound.cmake compares the two runs'
            //      CREON_DEATH_CRY lines and world hashes;
            //  (c) one voice per death: never a second cry or more of the other
            //      death sounds than one Killed/Dying authors (a display VM cry on
            //      top of the simulation's);
            //  (d) fog: an enemy dying out of the local player's sight is silent.
            //      Shipped death sounds are all class 4, so the other classes
            //      replay a queued cry with new flags: only class 7 carries out
            //      of sight; in sight 2-6 play, 0/1 only for a selected unit;
            //  (e) the authored rates and variants (0x10057000 is the LOGICAL
            //      and, so "deathType!=3 && RAND(1,4)" is always true for a
            //      non-explosion death);
            //  (f) a presentation backlog keeps its cries in live play (a hitch,
            //      like the impact sounds), but a rejoin's or spectator's replayed
            //      history (before cmdReplayEnd_) stays silent;
            //  (g) petrified and frozen deaths (statues, severity 0) stay silent.
            // rate: chance of any death sound; cries: distinct authored *DIE* variants;
            // extra: other death-callback sounds per death (crefire's Killed CANNON8
            // -- its 2nd/3rd follow sleeps, after Dying's stop value 26 halts the
            // owner VM; crebomb's 13-sound fuse).
            struct Expect { const char* type; double normal, explosion; int cries, criesExplosion, extra; };
            const Expect roster[]={
                {"creauto",1,0,3,0,0},{"crebeas",1,0,3,0,0},{"crechie",1,0,3,0,0},
                {"cremech",1,0,3,0,0},{"cretort",1,0,3,0,0},{"crefire",1,1,2,0,1},
                {"cregod",1,1,3,3,0},{"cregatl",.25,.25,1,1,0},{"creshoc",.25,0,1,0,0},
                {"crebomb",1,1,0,0,13},{"araarch",.25,0,1,0,0},{"tararch",.25,0,1,0,0}};
            const auto* normalWeapon=static_cast<const tak::sim::Weapon*>(nullptr);
            const auto* blastWeapon=normalWeapon;
            std::string normalFrom,blastFrom;
            for (const auto& [id,type]:registry_.types()) for (const auto& weapon:type.weapons) {
                if (weapon.status!=tak::sim::Weapon::Status::None || weapon.mindControl || weapon.damage<=0) continue;
                if (!normalWeapon && weapon.dmgType==1) {normalWeapon=&weapon;normalFrom=id+":"+weapon.name;}
                if (!blastWeapon && weapon.dmgType==3) {blastWeapon=&weapon;blastFrom=id+":"+weapon.name;}
            }
            if (!normalWeapon || !blastWeapon) throw std::runtime_error("no shipped dmgType 1/3 weapon");
            const bool catchUp=tak::devFlag("TAK_CREON_DEATH_SOUND_CATCHUP");
            std::fprintf(stderr,"CREON_DEATH weapons normal=%s explosion=%s noFog=%d players=%d catchup=%d\n",
                normalFrom.c_str(),blastFrom.c_str(),int(noFog_),world_.numPlayers(),int(catchUp));
            const auto lower=[](std::string s) {
                std::transform(s.begin(),s.end(),s.begin(),::tolower);return s;
            };
            std::vector<SoundBank::RequestTrace> requests;
            sounds_.requestHook=[&](const SoundBank::RequestTrace& r){requests.push_back(r);};
            // The simulation's choice: what each dying VM played, per unit id
            // (scriptSounds() holds the last tick's).
            std::map<int,std::vector<tak::sim::World::ScriptSound>> chosen;
            const auto soundName=[&](const tak::sim::World::ScriptSound& s) {
                return lower(s.type->script()->name(uint32_t(s.name)));
            };
            const int enemy=localPlayer_==0 ? 1 : 0;
            // An idle crechie animates corpses within reach into creautos, which then
            // attack the next batch; every newcomer is made passive like the victims.
            int newest=0;
            for (const auto& u:world_.units()) newest=std::max(newest,u.id);
            const auto simulate=[&] {
                simStep(1.0f/30.0f);
                for (const auto& sound:world_.scriptSounds()) chosen[sound.unitId].push_back(sound);
                for (const auto& u:world_.units())
                    if (u.id>newest) {
                        newest=u.id;
                        if (u.alive() && u.stance!=2) writableWorld().setStance(u.id,2);
                    }
            };
            const auto frame=[&] {
                simulate();
                beginFrame();cosmeticStep(1.0f/30.0f);animFrame(1.0f/30.0f);endFrame();
            };
            // The name-table sounds requested at (x,z) since the last clear, in order.
            const auto heardAt=[&](float x,float z,const std::vector<std::string>& names) {
                std::vector<const SoundBank::RequestTrace*> out;
                for (const auto& r:requests) {
                    if (!r.positional || std::abs(r.x-x)>1 || std::abs(r.z-z)>1) continue;
                    const auto name=lower(r.name);
                    if (std::any_of(names.begin(),names.end(),[&](const std::string& n){return lower(n)==name;}))
                        out.push_back(&r);
                }
                return out;
            };
            const auto join=[](const std::vector<std::string>& list) {
                std::string out;
                for (const auto& name:list) out+=(out.empty() ? "" : ",")+name;
                return out.empty() ? std::string("-") : out;
            };
            const auto lethal=[&](int id,int death) {
                const auto* u=world_.unit(id);
                if (!u || !u->alive()) throw std::runtime_error("victim died before the hit");
                const int from=u->player==localPlayer_ ? enemy : localPlayer_;
                tak::sim::Weapon w=death==3 ? *blastWeapon : *normalWeapon;
                w.aoe=0;w.dmgVs.clear();w.dmgVsIds.clear();
                w.damage=std::min(30000,int32_t(float(u->maximumHp())*(1.25f+float(id%3)))); // HP is 16.16
                // Armour and veterancy scale damage; strike until the hit is lethal.
                for (int hit=0;hit<8 && world_.unit(id)->hp>tak::sim::Fixed();++hit)
                    writableWorld().debugApplyHit(w,id,from);
            };
            lookAt(cx,cz);
            sounds_.setListener(cx,cz,640,400);
            // Local observers above and below the grid keep every enemy victim inside
            // the local player's sight after the local victims of the same batch die:
            // the sound host tests retail's view plane, where one 232-pixel sage
            // north of the grid does not reach its far row.
            const int observer=spawn("cresage",cx,cz+190,0,localPlayer_);
            const int observer2=spawn("cresage",cx,cz-190,0,localPlayer_);
            if (observer<0 || observer2<0) throw std::runtime_error("observer did not spawn");
            writableWorld().setStance(observer,2);writableWorld().setStance(observer2,2);
            for (int t=0;t<15;++t) frame();
            int failures=0,deathsTotal=0,mismatched=0,doubled=0;
            std::map<std::string,std::pair<bool,float>> files;
            std::map<int,std::string> cryLines;
            // Ticks before each hit: long enough for the display VMs' idle loops to
            // roll RAND (crebeas every 2 s, SmokeControl every 5-6 s). The late peer
            // simulates all but the last kShown of them without presenting a frame.
            constexpr int kLife=210,kShown=15;
            std::fprintf(stderr,"CREON_DEATH %-8s %-9s %6s %8s %8s %8s %6s  %s\n","unit","death","deaths",
                "cried","rate","expect","played","sounds observed (count)");
            for (const auto& expect:roster) {
                const auto* type=registry_.find(expect.type);
                if (!type || !type->script()) {++failures;std::fprintf(stderr,"FAIL %s missing\n",expect.type);continue;}
                const auto& names=type->script()->names;
                for (int death:{1,3}) {
                    const int deaths=48;
                    int cried=0,played=0,extras=0;
                    std::map<std::string,int> seen,other;
                    for (int batch=0;batch*16<deaths;++batch) {
                        std::vector<int> victims;
                        for (int i=0;i<16 && batch*16+i<deaths;++i) {
                            const float x=cx-144+float(i%4)*96,z=cz-144+float(i/4)*96;
                            // Alternate owners per batch so victims never fight each other.
                            const int owner=(batch&1) ? enemy : localPlayer_;
                            // The simulation's spawn: the client registers the body when a
                            // presented frame first shows it, as for every unit in a game.
                            const int id=writableWorld().spawn(type,x,z,0.0f,owner);
                            if (id<0) throw std::runtime_error(std::string("spawn failed: ")+expect.type);
                            writableWorld().setStance(id,2);
                            victims.push_back(id);
                        }
                        for (int t=0;t<kLife;++t) {
                            if (catchUp && t<kLife-kShown) simulate();
                            else frame();
                        }
                        requests.clear();sounds_.debugSilence();
                        std::map<int,std::pair<float,float>> where;
                        for (int id:victims) {
                            const auto* u=world_.unit(id);
                            if (!u || !u->alive()) throw std::runtime_error("victim died before the hit");
                            where[id]={u->x.toFloat(),u->z.toFloat()};
                            lethal(id,death);
                        }
                        for (int t=0;t<240;++t) frame();
                        for (int id:victims) {
                            const auto* u=world_.unit(id);
                            if (u && u->alive()) throw std::runtime_error("victim survived the hit");
                            if (u && u->deathType!=death) throw std::runtime_error("victim deathType differs from weapon");
                            const auto [x,z]=where[id];
                            const auto heard=heardAt(x,z,names);
                            std::vector<std::string> heardNames,simNames;
                            for (const auto* r:heard) heardNames.push_back(lower(r->name));
                            for (const auto& sound:chosen[id]) simNames.push_back(soundName(sound));
                            ++deathsTotal;
                            // (a) the simulation's sounds, exactly and in order.
                            if (heardNames!=simNames && ++mismatched<=8)
                                std::fprintf(stderr,"CREON_DEATH differs: %s %s unit %d heard %s, simulation %s\n",
                                    expect.type,death==3?"explosion":"normal",id,
                                    join(heardNames).c_str(),join(simNames).c_str());
                            // (c) one voice per death.
                            int heardCries=0,heardOthers=0;
                            for (const auto& name:heardNames) {
                                if (name.find("die")!=std::string::npos) ++heardCries;
                                else if (name=="cannon8" || name.rfind("cremiscbhit",0)==0) ++heardOthers;
                            }
                            doubled+=heardCries>1 || heardOthers>expect.extra;
                            cryLines[id]=std::string(expect.type)+" "+(death==3?"explosion":"normal")+" "+
                                ((batch&1) ? "enemy" : "local")+" "+join(heardNames);
                            int mine=0;
                            for (const auto* r:heard) {
                                const auto req=lower(r->name);
                                if (req.find("die")==std::string::npos && req!="cannon8" && req.rfind("cremiscbhit",0)!=0) {
                                    ++other[r->name];continue;
                                }
                                ++mine;++seen[r->name];played+=r->slot>=0;
                                extras+=req.find("die")==std::string::npos;
                                files[r->name]={r->indexed && r->loaded,r->peak};
                            }
                            cried+=mine>0;
                        }
                    }
                    // (e) the authored rates and variants.
                    const double expected=death==3 ? expect.explosion : expect.normal;
                    const double rate=double(cried)/deaths;
                    std::string list;
                    for (const auto& [name,count]:seen) list+=name+"("+std::to_string(count)+") ";
                    for (const auto& [name,count]:other) list+="[non-death "+name+"("+std::to_string(count)+")] ";
                    std::fprintf(stderr,"CREON_DEATH %-8s %-9s %6d %8d %8.2f %8.2f %6d  %s\n",expect.type,
                        death==3 ? "explosion" : "normal",deaths,cried,rate,expected,played,list.c_str());
                    const double sigma=std::sqrt(std::max(expected*(1-expected),.01)/deaths);
                    if (std::abs(rate-expected)>3.5*sigma) {
                        ++failures;std::fprintf(stderr,"FAIL %s %s: cry rate %.2f, script authors %.2f\n",
                            expect.type,death==3?"explosion":"normal",rate,expected);
                    }
                    int variants=0,cries=0;
                    for (const auto& [name,count]:seen) {
                        if (lower(name).find("die")!=std::string::npos) {++variants;cries+=count;}
                    }
                    const int authored=death==3 ? expect.criesExplosion : expect.cries;
                    // Each of `authored` uniform variants should appear in 48 cries.
                    if (authored>1 && cries>=24 && variants<authored) {
                        ++failures;std::fprintf(stderr,"FAIL %s %s: %d of %d authored cry variants chosen in %d cries\n",
                            expect.type,death==3?"explosion":"normal",variants,authored,cries);
                    }
                    if (extras!=expect.extra*deaths) {
                        ++failures;std::fprintf(stderr,"FAIL %s %s: %d other death sounds, script authors %d\n",
                            expect.type,death==3?"explosion":"normal",extras,expect.extra*deaths);
                    }
                }
            }
            std::fprintf(stderr,"CREON_DEATH exact: %d deaths, %d heard other sounds than the simulation chose, "
                "%d heard a death sound twice\n",deathsTotal,mismatched,doubled);
            if (mismatched) {
                ++failures;std::fprintf(stderr,"FAIL (a) %d deaths did not sound the simulation's choice\n",mismatched);
            }
            if (doubled) {
                ++failures;std::fprintf(stderr,"FAIL (c) %d deaths sounded twice\n",doubled);
            }
            // (d) Fog: sixteen enemy creauto (a cry on every normal death) die
            // where no local unit sees them.
            {
                const auto* type=registry_.find("creauto");
                if (!type || !type->script()) throw std::runtime_error("creauto missing");
                const auto& names=type->script()->names;
                float fx=0,fz=0;
                bool found=false;
                for (const auto& [px,pz]:{std::pair{cx*.3f,cz*.3f},std::pair{cx*1.7f,cz*.3f},
                                          std::pair{cx*.3f,cz*1.7f},std::pair{cx*1.7f,cz*1.7f}}) {
                    bool unseen=true;
                    for (int i=0;i<16;++i)
                        unseen=unseen && !cellVisibleR(px-144+float(i%4)*96,pz-144+float(i/4)*96);
                    if (unseen) {fx=px;fz=pz;found=true;break;}
                }
                if (!found) throw std::runtime_error("no unseen corner for the fog check");
                std::vector<int> hidden;
                std::map<int,std::pair<float,float>> where;
                for (int i=0;i<16;++i) {
                    const int id=writableWorld().spawn(type,fx-144+float(i%4)*96,fz-144+float(i/4)*96,0.0f,enemy);
                    if (id<0) throw std::runtime_error("spawn failed: creauto");
                    writableWorld().setStance(id,2);
                    hidden.push_back(id);
                }
                for (int t=0;t<30;++t) frame();
                requests.clear();
                for (int id:hidden) {
                    const auto* u=world_.unit(id);
                    if (!u || !u->alive()) throw std::runtime_error("fog victim died before the hit");
                    where[id]={u->x.toFloat(),u->z.toFloat()};
                    if (cellVisibleR(where[id].first,where[id].second))
                        throw std::runtime_error("fog victim is in the local player's sight");
                    lethal(id,1);
                }
                for (int t=0;t<240;++t) frame();
                int chosenCries=0,heardCries=0,global=0;
                for (int id:hidden) {
                    chosenCries+=!chosen[id].empty();
                    heardCries+=!heardAt(where[id].first,where[id].second,names).empty();
                }
                for (const auto& r:requests) {
                    const auto name=lower(r.name);
                    global+=!r.positional && std::any_of(names.begin(),names.end(),
                        [&](const std::string& n){return lower(n)==name;});
                }
                std::fprintf(stderr,"CREON_DEATH fog: 16 enemy deaths out of sight, simulation cried %d, "
                    "client heard %d positional + %d global\n",chosenCries,heardCries,global);
                if (chosenCries!=16) {
                    ++failures;std::fprintf(stderr,"FAIL (d) the simulation cried for %d of 16 hidden deaths\n",chosenCries);
                }
                if (heardCries || global) {
                    ++failures;std::fprintf(stderr,"FAIL (d) a class-4 cry out of sight was heard\n");
                }
                // The same queued cry with every class: out of sight, at the observer
                // (in sight), and at the observer while it is selected.
                if (chosenCries) {
                    const auto source=std::find_if(hidden.begin(),hidden.end(),[&](int id){return !chosen[id].empty();});
                    const auto base=chosen[*source].front();
                    const auto name=base.type->script()->name(uint32_t(base.name));
                    std::string row[3];
                    for (int place=0;place<3;++place) for (int cls=0;cls<8;++cls) {
                        const auto* eye=world_.unit(observer);
                        if (!eye || !eye->alive()) throw std::runtime_error("observer died");
                        const float x=place ? eye->x.toFloat() : base.x.toFloat();
                        const float z=place ? eye->z.toFloat() : base.z.toFloat();
                        const std::array<int32_t,3> at=place ? std::array{eye->x.v,eye->groundY.v,eye->z.v}
                                                             : std::array{base.x.v,base.y.v,base.z.v};
                        if (place==2) selection_={observer};
                        requests.clear();
                        {
                            std::lock_guard<std::mutex> lock(hitQueueMutex_);
                            unitSoundQueue_.push_back({front().gameTick+1,place==2 ? observer : base.unitId,
                                base.player,x,z,(base.flags&~7)|cls,name,at,false,base.type});
                        }
                        frame();
                        selection_.clear();
                        bool globalHeard=false,positionalHeard=false;
                        for (const auto& r:requests) {
                            if (lower(r.name)!=lower(name)) continue;
                            globalHeard=globalHeard || (!r.positional && r.priority==7);
                            positionalHeard=positionalHeard || (r.positional && r.priority==cls &&
                                std::abs(r.x-x)<=1 && std::abs(r.z-z)<=1);
                        }
                        const bool wantGlobal=cls==7,wantPositional=place && cls<7 && (cls>1 || place==2);
                        row[place]+=globalHeard ? 'G' : positionalHeard ? 'P' : '-';
                        if (globalHeard!=wantGlobal || positionalHeard!=wantPositional) {
                            ++failures;std::fprintf(stderr,"FAIL (d) class %d %s: heard global=%d positional=%d\n",cls,
                                place==0 ? "out of sight" : place==1 ? "in sight" : "in sight, selected",
                                int(globalHeard),int(positionalHeard));
                        }
                    }
                    std::fprintf(stderr,"CREON_DEATH fog classes 0-7 (G global, P positional): out of sight %s, "
                        "in sight %s, selected %s\n",row[0].c_str(),row[1].c_str(),row[2].c_str());
                }
            }
            // (h) Local units dying where no other local unit sees them. Retail
            // keeps a dying local unit's sight stamp for 60 ticks (4c6750) on the
            // plane the sound host 50def0 tests, so their cries play, and a
            // crebomb's fuse sounds keep playing through that window; display fog
            // drops a dead unit's sight with its next pass and cut them short.
            for (const char* id:{"creauto","crebomb"}) {
                const auto* type=registry_.find(id);
                if (!type || !type->script()) throw std::runtime_error(std::string(id)+" missing");
                const auto& names=type->script()->names;
                float lx=0,lz=0;
                bool found=false;
                for (const auto& [px,pz]:{std::pair{cx*1.7f,cz*1.7f},std::pair{cx*.3f,cz*1.7f},
                                          std::pair{cx*1.7f,cz*.3f},std::pair{cx*.3f,cz*.3f}}) {
                    bool unseen=true;
                    for (int i=0;i<4;++i)
                        unseen=unseen && !cellVisibleR(px-48+float(i%2)*96,pz-48+float(i/2)*96);
                    if (unseen) {lx=px;lz=pz;found=true;break;}
                }
                if (!found) throw std::runtime_error("no unseen corner for the lone-death check");
                std::vector<int> lone;
                std::map<int,std::pair<float,float>> where;
                for (int i=0;i<4;++i) {
                    const int victim=writableWorld().spawn(type,lx-48+float(i%2)*96,lz-48+float(i/2)*96,0.0f,
                                                           localPlayer_);
                    if (victim<0) throw std::runtime_error(std::string("spawn failed: ")+id);
                    writableWorld().setStance(victim,2);
                    lone.push_back(victim);
                }
                for (int t=0;t<30;++t) frame();
                requests.clear();sounds_.debugSilence();
                for (int victim:lone) {
                    const auto* u=world_.unit(victim);
                    if (!u || !u->alive()) throw std::runtime_error("lone victim died before the hit");
                    where[victim]={u->x.toFloat(),u->z.toFloat()};
                    lethal(victim,1);
                }
                for (int t=0;t<120;++t) {
                    frame();
                    if (!(t%4)) sounds_.debugSilence();   // keep voices free: count every request
                }
                int sounded=0,expected=0,heard=0,fogged=0;
                for (int victim:lone) {
                    const auto& made=chosen[victim];
                    sounded+=int(made.size());
                    // The stamp is dropped once its deadline (death + 60) is past.
                    for (const auto& sound:made) expected+=int32_t(sound.tick-made.front().tick)<=60;
                    heard+=int(heardAt(where[victim].first,where[victim].second,names).size());
                    fogged+=!cellVisibleR(where[victim].first,where[victim].second);
                }
                std::fprintf(stderr,"CREON_DEATH lone %s: 4 local deaths unwatched (%d now fogged), "
                    "simulation sounded %d (%d within the stamp), client heard %d\n",
                    id,fogged,sounded,expected,heard);
                if (!expected || heard!=expected) {
                    ++failures;std::fprintf(stderr,"FAIL (h) %s: unwatched local death sounds lost\n",id);
                }
            }
            // (f) A backlog: deaths the presentation skipped over. In live play
            // (a render hitch) they still cry when it resumes, with their impact
            // sounds; deaths inside a rejoin's or spectator's replayed history
            // (ticks before cmdReplayEnd_) stay silent rather than cry at once.
            {
                const auto* type=registry_.find("creauto");
                if (!type || !type->script()) throw std::runtime_error("creauto missing");
                const auto& names=type->script()->names;
                constexpr int kSkipped=45;
                for (const bool history:{false,true}) {
                    std::vector<int> victims;
                    std::map<int,std::pair<float,float>> where;
                    for (int i=0;i<16;++i) {
                        const int id=writableWorld().spawn(type,cx-144+float(i%4)*96,cz-144+float(i/4)*96,0.0f,
                                                           localPlayer_);
                        if (id<0) throw std::runtime_error("spawn failed: creauto");
                        writableWorld().setStance(id,2);
                        victims.push_back(id);
                    }
                    for (int t=0;t<30;++t) frame();
                    requests.clear();sounds_.debugSilence();
                    for (int id:victims) {
                        const auto* u=world_.unit(id);
                        if (!u || !u->alive()) throw std::runtime_error("backlog victim died before the hit");
                        where[id]={u->x.toFloat(),u->z.toFloat()};
                        lethal(id,1);
                    }
                    // They die on the next tick; the presentation resumes kSkipped ticks
                    // later, at the end of the replayed history when `history` is set.
                    if (history) cmdReplayEnd_=world_.tickCount()+kSkipped;
                    for (int t=0;t<kSkipped;++t) simulate();
                    for (int t=0;t<240;++t) frame();
                    cmdReplayEnd_=0;
                    int chosenCries=0,heardCries=0;
                    for (int id:victims) {
                        chosenCries+=!chosen[id].empty();
                        heardCries+=!heardAt(where[id].first,where[id].second,names).empty();
                    }
                    std::fprintf(stderr,"CREON_DEATH backlog: presentation resumed %d ticks after 16 deaths (%s), "
                        "simulation cried %d, client heard %d\n",kSkipped,history ? "replayed history" : "live",
                        chosenCries,heardCries);
                    if (chosenCries!=16 || heardCries!=(history ? 0 : 16)) {
                        ++failures;std::fprintf(stderr,"FAIL (f) %d of 16 cries heard %d ticks late in %s, want %d\n",
                            heardCries,kSkipped,history ? "replayed history" : "live play",history ? 0 : 16);
                    }
                }
            }
            // (g) Petrified and frozen bodies stand as statues without Killed or
            // Dying: the simulation chooses no cry and the client hears none.
            {
                const tak::sim::Weapon* statusWeapon[2]={nullptr,nullptr};
                for (const auto& [id,type]:registry_.types()) for (const auto& weapon:type.weapons) {
                    if (weapon.status==tak::sim::Weapon::Status::Stoned && !statusWeapon[0]) statusWeapon[0]=&weapon;
                    if (weapon.status==tak::sim::Weapon::Status::Frozen && !statusWeapon[1]) statusWeapon[1]=&weapon;
                }
                if (!statusWeapon[0] || !statusWeapon[1]) throw std::runtime_error("no shipped petrify/freeze weapon");
                // A cry on every normal death, and both statues.
                const auto* type=registry_.find("crebeas");
                if (!type || !type->script()) throw std::runtime_error("crebeas missing");
                const auto& names=type->script()->names;
                std::vector<int> victims;
                std::map<int,std::pair<float,float>> where;
                for (int i=0;i<16;++i) {
                    const int id=writableWorld().spawn(type,cx-144+float(i%4)*96,cz-144+float(i/4)*96,0.0f,
                                                       localPlayer_);
                    if (id<0) throw std::runtime_error("spawn failed: crebeas");
                    writableWorld().setStance(id,2);
                    victims.push_back(id);
                }
                for (int t=0;t<30;++t) frame();
                requests.clear();
                for (size_t i=0;i<victims.size();++i) {
                    const auto* u=world_.unit(victims[i]);
                    if (!u || !u->alive()) throw std::runtime_error("statue victim died before the hit");
                    where[victims[i]]={u->x.toFloat(),u->z.toFloat()};
                    tak::sim::Weapon w=*statusWeapon[i&1];
                    w.aoe=0;w.dmgVs.clear();w.dmgVsIds.clear();
                    writableWorld().debugApplyHit(w,victims[i],enemy);
                }
                for (int t=0;t<240;++t) frame();
                int statues=0,chosenCries=0,heardCries=0;
                for (int id:victims) {
                    const auto* u=world_.unit(id);
                    statues+=u && !u->alive() && u->corpseStatue>=0;
                    chosenCries+=!chosen[id].empty();
                    heardCries+=!heardAt(where[id].first,where[id].second,names).empty();
                }
                std::fprintf(stderr,"CREON_DEATH statues: 16 petrified/frozen deaths, %d statues, simulation cried %d, "
                    "client heard %d\n",statues,chosenCries,heardCries);
                if (statues!=16 || chosenCries || heardCries) {
                    ++failures;std::fprintf(stderr,"FAIL (g) petrified/frozen deaths: %d statues, %d chosen, %d heard\n",
                        statues,chosenCries,heardCries);
                }
            }
            for (const auto& [name,state]:files)
                std::fprintf(stderr,"CREON_DEATH file %-14s loaded=%d peak=%.3f\n",name.c_str(),int(state.first),state.second);
            // (b) What this peer heard, death by death, for the two-peer comparison.
            for (const auto& [id,line]:cryLines) std::fprintf(stderr,"CREON_DEATH_CRY %d %s\n",id,line.c_str());
            std::fprintf(stderr,"CREON_DEATH_WORLD tick=%u hash=%016llx\n",world_.tickCount(),
                static_cast<unsigned long long>(world_.stateHash()));
            sounds_.requestHook=nullptr;
            if (failures) throw std::runtime_error("Creon death sounds: "+std::to_string(failures)+" failures");
            std::fprintf(stderr,"PASS: Creon death sounds are the simulation's, at their authored rates, "
                "through the client pipeline\n");
            return;
        }
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
            auto* owner=writableWorld().unit(ownerId);
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
            auto* owner=writableWorld().unit(ownerId);
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
                writableWorld().unit(passengerId)->inTransport=aboard ? carrierId : 0;
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
            writableWorld().unit(passengerId)->inTransport=carrierId;
            writableWorld().unit(passengerId)->deadFor=0;
            captureFrame();beginFrame();endFrame();
            const auto effectsBefore=effects_.size(),particlesBefore=particles_.size();
            cosmeticStep(0);
            if(effects_.size()!=effectsBefore || particles_.size()!=particlesBefore)
                throw std::runtime_error("hidden destroyed cargo spawned ordinary death effects");
            // Control: an ordinary unembarked archer also gets no fabricated
            // body blast before its authored Killed/Dying scripts are advanced.
            const int ordinary=spawn("araarch",cx+40,cz,0,localPlayer_);
            if(ordinary<0)throw std::runtime_error("ordinary death control failed to spawn");
            writableWorld().unit(ordinary)->deadFor=0;
            writableWorld().unit(ordinary)->deathType=1;
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
                writableWorld().unit(statue)->deadFor=0;
                writableWorld().unit(statue)->deathType=deathType;
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
            unitType_.set(-123,"__debris_fixture");
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
                    for(const auto& page:palettePages_)
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
            writableWorld().order(owner,cx+240,cz,false);
            int width=0,height=0;SDL_GetRendererOutputSize(ren_,&width,&height);
            mapView_.setZoom(2.f);
            mapView_.setOffset(cx+80-float(mapViewW(width))*0.5f/mapView_.zoom(),
                cz-float(height-barH())*0.5f/mapView_.zoom());
            return;
        }
        if (tak::devEnv("TAK_SMOKE_TEST")) {
            edgeScrollOn_=false;follow_=false;initialCamera_.reset();
            const int owner=spawn("arakeep",cx,cz,0,0);
            if(auto* unit=writableWorld().unit(owner))unit->hp=tak::sim::Fixed::fromInt(unit->maximumHp()/5);
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
            if (auto* u=writableWorld().unit(target)) {
                u->hp=tak::sim::Fixed::fromInt(25000);writableWorld().setStance(target,2);
            }
            if (shooter>=0 && target>=0) writableWorld().attack(shooter,target,false);
            int width=0,height=0;SDL_GetRendererOutputSize(ren_,&width,&height);
            const float viewport=float(mapViewW(width));
            mapView_.setZoom(std::min(1.5f,std::max(0.25f,(viewport-100.f)/400.f)));
            mapView_.setOffset(cx-viewport*0.5f/mapView_.zoom(),
                cz-float(height-barH())*0.5f/mapView_.zoom());
            return;
        }
        int a = spawn("araarch", cx - 40, cz, 1.57f, 0);
        int e = spawn("tararch", cx + 200, cz, -1.57f, 1);
        writableWorld().attack(a, e, false);
        // Aim-pipeline check: a Veruna watch tower (5-TURN AimWeapon) with an
        // enemy off-axis to its north-east -- the turret must visibly swing to
        // face it (auto-acquire), proving the AimWeapon heading sign.
        int tw = spawn("vertower", cx - 40, cz + 220, 0.0f, 0);
        int zm = spawn("tarzom", cx + 160, cz + 100, -1.57f, 1);
        writableWorld().attack(tw, zm, false);   // explicit order: aim runs even without LOS
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
        writableWorld().attack(dr, ar, false);
        // Statue-death check: the Basilisk's gaze petrifies -- the victim must
        // die on the spot and stand as a stone-gray, resurrectable statue.
        int bs = spawn("zonbasil", tx - 120, tz + 90, 1.57f, 1);
        int vic = spawn("arabow", tx + 40, tz + 90, -1.57f, 0);
        writableWorld().attack(bs, vic, false);
        // Ordered corpse-reclaim check: a builder is sent (negative target id)
        // to consume a fresh corpse -- the body must vanish when it arrives.
        int rcv = spawn("arasword", cx - 200, cz + 120, 0.0f, 0);
        if (auto* rd = writableWorld().unit(rcv)) rd->hp = tak::sim::Fixed();   // dies this tick, normal corpse
        int rcb = spawn("arabuild", cx - 250, cz + 160, 1.57f, 0);
        writableWorld().reclaim(rcb, -rcv, false);
        // Animate check: an idle necromancer beside the (soon) archer corpse
        // must channel and raise a Ghoul from it. Hold-fire stance so it never
        // auto-acquires (the channel needs it order-free).
        int nec = spawn("tarpries", tx + 30, tz - 40, -1.57f, 1);
        if (auto* np = writableWorld().unit(nec)) np->stance = 2;
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
                else writableWorld().guard(u.id, leader, false);
            }
        if (leader >= 0) writableWorld().order(leader, 1500, 950, false);
    }

    void GameView::marchTo(float dx, float dz) {
        ClickScope click(*this);   // one click: a Legion net game sends it as explicit orders
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
        const auto& ground = writableWorld().nav();
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
                writableWorld().loadInto(id, transportId_);
                ++amphibSquad_;
            }
            ++i;
        }
    }


    // Long-running churn for the render matrix: every `period` frames the oldest
    // twentieth of the living mobile army dies (leaving corpses) and as many new
    // units spawn beside the army. Unit ids keep growing, so retained render
    // entries see destruction, fresh ids and replacements. Deterministic: it
    // runs from the frame counter of the fixed-step render study.
    void GameView::renderStudyChurn(unsigned frame, unsigned period) {
        if(!period || !frame || frame%period)return;
        std::vector<int> mobile;
        for(const auto& unit:world_.units())
            if(unit.alive() && unit.type && unit.type->canMove && !unit.type->canFly)mobile.push_back(unit.id);
        if(mobile.empty())return;
        std::sort(mobile.begin(),mobile.end());
        const size_t kill=std::max<size_t>(1,mobile.size()/20);
        const char* roster[]={"zonter","araarch","versword","tararch"};
        for(size_t k=0;k<kill;++k) {
            const auto* old=world_.unit(mobile[k]);
            const float x=old->x.toFloat(),z=old->z.toFloat();
            writableWorld().destroy(mobile[k]);
            const float dx=float(int((frame/period+k)%9)-4)*24;
            spawn(roster[(frame/period+k)%4],x+dx,z+48,float(k%8)*0.785398163f,localPlayer_);
        }
    }

    // Material-only invalidation for the render matrix (workstream E): every
    // `period` frames every fifth living mobile unit moves to the next veteran
    // level in 4..9 (gold body tint; level 10 would swap the model).
    void GameView::renderStudyVeteran(unsigned frame, unsigned period) {
        if(!period || !frame || frame%period)return;
        size_t k=0;
        for(const auto& unit:world_.units()) {
            if(!unit.alive() || !unit.type || !unit.type->canMove || k++%5)continue;
            if(auto* u=writableWorld().unit(unit.id))u->veteran=4+int((frame/period+k)%6);
        }
    }

#endif   // !NDEBUG
