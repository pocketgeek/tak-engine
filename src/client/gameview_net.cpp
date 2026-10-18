#include <ctime>
#include "tnt/ota.h"
#include "client/gameview.h"
#include "net/matchconfig.h"
#include "campaign/campaign.h"

// Out-of-line GameView method definitions (net concern), split from the
// class body in gameview.h so editing a body recompiles only this translation
// unit. Trivial getters, ctors, static, template, constexpr and default-arg
// methods stay inline in the header. Grouping is by name heuristic.

    uint8_t GameView::mpCapacity(bool mapScenario) const {
        // A generated map's capacity is the player count baked into its "~gen1~" id.
        // The lobby sets mpMapId_; the headless "game" flow only has mapPath_ -- honour
        // either, since sim::parseStartPositions can't parse a synthetic id.
        const std::string& gid = tak::mapgen::isGeneratedMapId(mpMapId_) ? mpMapId_ : mapPath_;
        if (tak::mapgen::isGeneratedMapId(gid))
            return uint8_t(std::clamp<int>(tak::mapgen::decodeMapId(gid).players, 2, tak::net::kMaxSlots));
        int n = int(parseStartPositions().size());
        // Capacity is available seats, not a minimum player count. A room that plays
        // its map's scenario opens every seat that scenario needs (its placement owners
        // and the rule banks that do something), which can lie past the start positions;
        // the verified lobby may still start a solo scenario without filling the rest.
        auto path=mapPath_;
        tak::hpi::Vfs scenarioData(&vfs_);
        if(const auto cached=vfs_.cachedMap(path)) {
            path=cached->first;scenarioData.setMapFiles(cached->second);
        }
        const auto scenario=tak::sim::inspectScenario(scenarioData,path);
        if(scenario.active(mapScenario))
            for(int i=0;i<tak::net::kMaxSlots;++i)
                if(scenario.requiredSeats()>>i & 1)n=std::max(n,i+1);
        return uint8_t(std::clamp(n < 2 ? 2 : n, 2, tak::net::kMaxSlots));
    }

    tak::sim::ScenarioInfo GameView::mapScenarioInfo(const std::string& path) const {
        try {
            auto mapPath=path;
            tak::hpi::Vfs view(&vfs_);
            if(const auto cached=vfs_.cachedMap(path)) {
                mapPath=cached->first;view.setMapFiles(cached->second);
            }
            return tak::sim::inspectScenario(view,mapPath,&registry_);
        } catch(const std::exception&) {return {};}   // an unreadable MapCache archive
    }

    void GameView::setMpClient(tak::net::MpClient* mp) {
        if (mp_ && mp_ != mp) mp_->setRecordingRestartHook(nullptr);
        mp_ = mp;
        // A new game on this connection: finish the old game's file while its bundles
        // are still in the log (the hook runs before the log is cleared). The new game
        // opens its own once it is set up (startMpGame).
        if (mp_) mp_->setRecordingRestartHook([this] {
            if ((replayRec_ || replayRecRetryMs_) &&
                (replayRecGame_ != mp_->room().id || replayRecSeed_ != mp_->startSeed()))
                finishReplayRecording();
        });
    }

    // Watch-only view of a live net game (spectator join or create-as-spectator):
    // no fog, no control, the F4 scoreboard on. The fog pass stops here, but the
    // world keeps the last fog it built for localPlayer_ -- noFog_, not an empty
    // visibility buffer, is what tells the overlays the spectator sees everything.
    void GameView::beginSpectating() {
        spectating_ = true;
        noFog_ = true;
        writableWorld().setVisPlayer(-1);   // sees everything -> skip the wasted O(units) fog pass
        showCounts_ = true;
        gameStartMs_ = SDL_GetTicks64();
    }

    // The whole game is kept in memory (mp_->replayLog()), so a recording that cannot
    // start now -- the folder unwritable or the disk full at this moment -- is tried
    // again every few seconds (pumpReplayRecording) and a last time when the game
    // ends (finishReplayRecording); once it opens it still begins at tick 0. Only the
    // first failure is logged.
    void GameView::tryOpenReplayRecording(bool first) {
        const uint64_t now = SDL_GetTicks64();
        replayRecRetryMs_ = now + 5000;
        const std::string dir = tak::replayDir();
        if (dir.empty()) {
            if (first) std::fprintf(stderr, "replay: no replay folder yet; will keep trying\n");
            return;
        }
        auto rec = std::make_unique<tak::net::ReplayWriter>();
        if (!rec->open(dir, replayRecHeader_, replayRecGame_, uint64_t(std::time(nullptr)))) {
            if (first)
                std::fprintf(stderr, "replay: cannot record yet (%s); will keep trying\n", rec->error().c_str());
            return;
        }
        std::fprintf(stderr, "replay: recording to %s\n", rec->partPath().c_str());
        replayRec_ = std::move(rec);
        replayRecRetryMs_ = 0;
        replayFlushMs_ = replaySyncMs_ = now;
    }

    // Main thread, every mpStep. Bundles are handed to the OS about once a second, so
    // a crash or a kill loses at most that much; they reach the disk itself every ten
    // seconds (a power cut), and at once when the result lands.
    void GameView::pumpReplayRecording() {
        if (!mp_) return;
        if (!replayRec_ && replayRecRetryMs_ && SDL_GetTicks64() >= replayRecRetryMs_) tryOpenReplayRecording(false);
        if (!replayRec_) return;
        replayRec_->append(mp_->replayLog(), mp_->hashLog());
        const uint64_t now = SDL_GetTicks64();
        const bool result = replayResultSync_.exchange(false, std::memory_order_relaxed);
        if (result || replayRec_->buffered() >= (64u << 10) ||
            (replayRec_->buffered() && now - replayFlushMs_ >= 1000)) {
            // A failed write (disk full) keeps its bytes and is retried at the next
            // flush and at finish(); it is logged once per run of failures.
            const bool wasFailing = replayRec_->failing();
            if (!replayRec_->flush() && !wasFailing)
                std::fprintf(stderr, "replay: %s; will keep trying\n", replayRec_->error().c_str());
            else if (wasFailing && !replayRec_->failing())
                std::fprintf(stderr, "replay: writing %s again\n", replayRec_->partPath().c_str());
            replayFlushMs_ = now;
        }
        if (result || now - replaySyncMs_ >= 10000) {
            replayRec_->syncAsync();
            replaySyncMs_ = now;
        }
        if (result) {
            // What is on disk now is the game up to its result. Recording goes on:
            // whatever is played or watched after it belongs to the replay too.
            std::fprintf(stderr, "replay: result at tick %u, %u ticks recorded; recording continues\n",
                         netTick_, replayRec_->bundles());
            if (!replayRec_->failing()) postNotice("REPLAY SAVED", 5);
        }
    }

    // ownLog: mp_'s log is still the recorded game's (the destructor, the restart
    // hook). beginGameRecording passes false: by then the log is the next game's.
    void GameView::finishReplayRecording(bool ownLog) {
        ownLog = ownLog && mp_;
        if (!replayRec_ && replayRecRetryMs_ && ownLog) tryOpenReplayRecording(false);   // a last try
        replayRecRetryMs_ = 0;   // this game's recording is over, opened or not
        if (!replayRec_) return;
        if (ownLog) replayRec_->append(mp_->replayLog(), mp_->hashLog());
        const uint32_t ticks = replayRec_->bundles();
        const std::string path = replayRec_->finish();
        if (!path.empty())
            std::fprintf(stderr, "replay: wrote %s (%u ticks)\n", path.c_str(), ticks);
        else if (ticks)
            std::fprintf(stderr, "replay: could not finish %s (%s); it is completed at the next start\n",
                         replayRec_->partPath().c_str(), replayRec_->error().c_str());
        replayRec_.reset();
    }

    void GameView::resetDiplomacy(const tak::net::RoomView& room) {
        chatRecipients_=0xff;voiceRecipients_=0xff;requestedManaSharing_.reset();
        voiceVolume_.fill(100);voiceVolumeDrag_=-1;humanSlots_=0;
        for (int i=0;i<tak::net::kMaxSlots && i<8;++i)
            if (room.slots[i].type==1) humanSlots_|=uint8_t(1u<<i);
        giveUnitsMenu_=false;giveUnitsHots_.clear();
    }

    void GameView::startMpGame(const tak::net::RoomView& room, uint32_t seed) {
        {
            std::lock_guard<std::mutex> lock(hitQueueMutex_);
            missionSoundQueue_.clear();
            unitSoundQueue_.clear();
        }
        resetDiplomacy(room);
        // A room field (SAY, a password) focused when the host started is let go: the game
        // has no such field, and its text input would otherwise hold push to talk off.
        if (lbField_) { lbField_ = 0; SDL_StopTextInput(); }
        resultParticipants_.reset();
        if (room.mission.empty()) {
            resultParticipants_=0;
            for (int i=0;i<tak::net::kMaxSlots;++i)
                if (room.slots[i].type==1 || room.slots[i].type==2)
                    *resultParticipants_|=uint8_t(1u<<i);
        }
        int maxSlot = 0;
        for (int i = 0; i < tak::net::kMaxSlots; ++i)
            if (room.slots[i].type == 1 || room.slots[i].type == 2) maxSlot = i;
        // Retail's loading screen covers the whole start: everything below blocks the
        // render loop for a second or more, and the wait on the other players after it.
        loadScreen_ = std::make_unique<tak::LoadScreen>(
            ren_, vfs_, room.mission.empty() ? room.mapId : room.mission, settings_);
        {
            std::array<std::string, 8> names{};
            for (int i = 0; i <= maxSlot && i < 8; ++i) {
                if (room.slots[i].type != 1 && room.slots[i].type != 2) continue;
                names[size_t(i)] = !room.slots[i].name.empty()
                                       ? room.slots[i].name
                                       : (room.slots[i].type == 2 ? "COMPUTER" : "PLAYER");
                // An AI slot has nothing to load and never reports in, so its bar is
                // full from the start -- only humans are worth waiting on.
                if (room.slots[i].type == 2) loadScreen_->setSlotDone(i);
            }
            loadScreen_->setPlayers(names, room.mySlot);
        }
        loadScreen_->step("MOUNTING GAME DATA", 5);
        // Build the world through the SHARED setup so the server's referee sim and
        // every client produce a bit-identical world (and hash). Client-only bits
        // (colours, camera, local player, panel) stay here.
        // The game's balance AND override tier are the ROOM's (lobby choices), which
        // may differ from how this client was launched. Remount to the room's tier
        // (so gameplay overrides -- or their absence -- match the referee) and
        // rebuild the registry, so our world and hash agree with the server's
        // referee. remountPolicy already rebuilds the registry for the current
        // crusades setting; handle a crusades-only change separately.
        resetMinimap();
        mapView_.quiesce();
        remountPolicy(room.opts.overridePolicy);
        vfs_.setMapFiles(mp_ && mp_->mapPackage() && room.mission.empty() &&
            !tak::mapgen::isGeneratedMapId(room.mapId) ? mp_->mapPackage()->files : room.mission.empty()
                ? std::make_shared<const tak::hpi::Vfs::Files>() : nullptr);
        featureDefs_.clear(); featurePals_.clear();
        for (auto& [name, art] : featureArt_) {
            for (auto texture : art.frames) if (texture) gpuvram::destroy(texture);
            for (auto texture : art.shadowFrames) if (texture) gpuvram::destroy(texture);
        }
        featureArt_.clear();
        if ((room.opts.crusades != 0) != crusades_) {
            crusades_ = room.opts.crusades != 0;
            registry_ = tak::sim::TypeRegistry{};
            tak::sim::setupRegistry(registry_, vfs_, crusades_);
        }
        // Campaign mission: build the SAME world the referee did. setupMission is
        // deterministic (terrain + placed units + the in-sim god script), so our sim,
        // the referee, and every peer stay byte-identical -- the mission runs in
        // lockstep with no relayed actions. See docs/campaign-design.md.
        if (!room.mission.empty()) {
            mapPath_ = "missions/" + room.mission + ".tnt";
            resetMinimap();   // its thread reads the map being swapped
            loadScreen_->step("LOADING TERRAIN", 30);
            mapView_.reload(vfs_, mapPath_);
            int human = 0;
            tak::sim::MissionSetup ms;
            tak::sim::setupMission(writableWorld(), registry_, vfs_, room.mission, human, &ms);
            missionFullVision_ = ms.fullVision;
            missionPreMapped_ = ms.preMapped;
            loadFeatures();
            if (loadScreen_) loadScreen_->step("LOADING CURSORS", 35);
            warmCursors();   // cursor art + hardware reconstruction, off the frame path
            // Per-mission unit restriction: missions/<stem>.tdf lists the unit ids this
            // mission allows; the human's conjure menu is filtered to it (UI only).
            missionAllowed_.clear();
            if (std::string tdfp = "missions/" + room.mission + ".tdf"; vfs_.has(tdfp)) {
                std::vector<uint8_t> tb = vfs_.read(tdfp);
                tak::tdf::Node root = tak::tdf::parseText(std::string(tb.begin(), tb.end()), tdfp);
                for (const auto& [name, node] : root.children) { (void)node; missionAllowed_.push_back(name); }
            }
            missionObjectives_ = tak::loadObjectives(vfs_, room.mission);   // in-game panel
            showObjectives_ = true;
            // The player commands the mission's human player; for the common case its
            // index equals our room slot (TODO: seat the client at `human` otherwise).
            localPlayer_ = human;
            writableWorld().setVisPlayer(localPlayer_);
            for (auto& u : world_.units())
                if (u.player == localPlayer_ && u.type) { playerMonarchId_ = u.id; builderId_ = u.id; break; }
            if (!missionAllowed_.empty())
                std::fprintf(stderr, "mission %s: conjure menu restricted to %zu allowed unit types\n",
                             room.mission.c_str(), missionAllowed_.size());
            const char* sides[5] = {"ara", "tar", "ver", "zon", "cre"};
            if (room.mySlot >= 0) side_ = sides[room.slots[room.mySlot].faction % 5];
            loadScreen_->step("LOADING INTERFACE", 90);
            loadPanel(side_);
            loadGui(side_);
            loadScreen_->step("WAITING FOR PLAYERS", 100);
            startAtMonarch();
            beginGameRecording();
            return;
        }
        // Adopt the ROOM's map (the host's / lobby selection), which may differ from
        // this session's launch map. Point mapPath_ at it AND reload the render terrain
        // (mapView_), so the rendered map, the local sim, and the referee all agree.
        // Without this, picking a non-default map drew the launch map's terrain under a
        // different map's sim -- phantom water, a monarch out in it, and misaligned fog.
        mapPath_ = mp_ && mp_->mapPackage() && !tak::mapgen::isGeneratedMapId(room.mapId)
            ? mp_->mapPackage()->mapPath : tak::hpi::findMap(vfs_, room.mapId);
        if (mapPath_.empty()) throw std::runtime_error("selected map is missing: " + room.mapId);
        resetMinimap();   // its thread reads the map being swapped
        loadScreen_->step("LOADING TERRAIN", 30);
        mapView_.reload(vfs_, mapPath_);
        loadScreen_->step("BUILDING THE WORLD", 65);
        // The referee's own builder (net/matchconfig.h) -- the same seed, slot table,
        // Absurd AI income and player count -- so hashed state matches (lockstep).
        tak::sim::MatchConfig cfg = tak::net::matchConfigFor(room.opts, room.slots, seed);
        cfg.vfs = &vfs_;
        cfg.mapPath = mapPath_;
        cfg.scenarioViewPlayer = localPlayer_;
        cfg.scenarioTrace = scenarioTrace_;
        if (room.opts.benchmark) benchmarkLevel_ = room.opts.benchmark;   // for the results label
        for (int i = 0; i <= maxSlot; ++i) {
            const auto& s = room.slots[i];
            colorSlot_[i & 7] = s.color % 10;
            playerAi_[i & 7] = (s.type == 2);
            playerName_[i & 7] = !s.name.empty()
                                     ? s.name   // human name, or the AI's random name
                                     : s.type == 2 ? ("AI " + std::to_string(i + 1))
                                                   : ("Player " + std::to_string(i + 1));
        }
        auto spots = tak::sim::setupMatch(writableWorld(), registry_, cfg);
        if (world_.hasScenarioOutcomes())   // the map's own scenario (MAP SCENARIO or a Test Map copy)
            std::fprintf(stderr, "map scenario: %zu units at the start\n", world_.units().size());
        // Rebuild the rendered feature sprites (features_) from the map we actually
        // loaded -- they were built once in the ctor from the launch map, so on a
        // different chosen map the trees/houses you SEE would be the launch map's,
        // while the reclaimable features in the sim (world_.features()) are this map's.
        // That mismatch made right-drag reclaim miss (the box covered the wrong stuff).
        // setupMatch is authoritative for the sim; loadFeatures only re-adds the same
        // map's mana/nav idempotently (setTerrain already rebuilt the nav).
        loadFeatures();
        warmCursors();   // cursor art + hardware reconstruction, off the frame path
        // client-only presentation
        localPlayer_ = room.mySlot < 0 ? 0 : room.mySlot;
        writableWorld().setVisPlayer(localPlayer_);
        if (room.opts.fogExplored == 1) writableWorld().revealTerrain();
        // A mission overrides the room's fog rule with its own: lineofsight=0 plays
        // revealed, mapping=1 starts the terrain explored. Display-only either way.
        if (missionFullVision_) noFog_ = true;
        if (room.opts.fogExplored == 2) noFog_ = true;       // FULL VISION: no fog at all --
                                                             // same spectator path, so the
                                                             // wasted-visibility work is skipped too
        if (benchmarkMode_) benchmarkBaseline();             // t=0 baseline for the perf samples
        for (auto& u : world_.units())
            if (u.player == localPlayer_ && u.type) { playerMonarchId_ = u.id; builderId_ = u.id; break; }
        const char* sides[5] = {"ara", "tar", "ver", "zon", "cre"};
        side_ = sides[room.slots[localPlayer_].faction % 5];
        loadScreen_->step("LOADING INTERFACE", 90);
        loadPanel(side_);
        loadGui(side_);
        loadScreen_->step("WAITING FOR PLAYERS", 100);
        startAtMonarch();
        beginGameRecording();
    }

    // The last step of startMpGame, on BOTH of its paths (a campaign mission returns
    // early). Record from here: the header needs the data this game runs on (the
    // room's tier, mounted above) and nothing has been simulated yet. A rejoin or a
    // spectator gets the backlog first, so its recording starts from tick 0 too.
    void GameView::beginGameRecording() {
        gameDataHash_ = gameDataHash();   // reported at Loaded and recorded in the replay header
        if (!mp_ || !mp_->recording() || replayMode_) return;
        // The Benchmark is a performance run, not a game: it is never recorded.
        if (benchmarkMode_ || mp_->room().opts.benchmark) return;
        if ((replayRec_ || replayRecRetryMs_) && replayRecGame_ == mp_->room().id &&
            replayRecSeed_ == mp_->startSeed())
            return;   // a rejoin of the game already being recorded: keep its file
        finishReplayRecording(/*ownLog=*/false);   // normally done already, by the restart hook
        // Everything the file needs about this game, taken now: the restart hook can
        // run (and try a last open) after the next game's GameStarting replaced it.
        replayRecHeader_ = clientReplayHeader(*mp_, gameDataHash_);
        replayRecGame_ = mp_->room().id;
        replayRecSeed_ = mp_->startSeed();
        replayResultSync_.store(false, std::memory_order_relaxed);
        tryOpenReplayRecording(/*first=*/true);
    }

    bool GameView::showInitialCampaignBriefing(const std::string& stem) {
        if (stem.empty()) return true;
        // Headless synchronization runs do not need a modal render; screenshot
        // runs still exercise the loaded-world briefing and its first-tick gate.
        const char* driver=SDL_GetCurrentVideoDriver();
        if (driver && std::strcmp(driver,"dummy")==0 && !tak::devEnv("TAK_SHOT_BRIEFING")) return true;
#ifndef NDEBUG
        const bool verifyBriefing=tak::devEnv("TAK_SHOT_BRIEFING")!=nullptr;
        const auto briefingHash=verifyBriefing ? world_.stateHash() : 0;
#endif
        // mpAutoStep can run mid-frame with a render target, scale, viewport or clip
        // bound. A modal must present to the actual window, then restore the caller's
        // render state.
        struct RenderState {
            SDL_Renderer* renderer;
            SDL_Texture* target;
            float scaleX,scaleY;
            SDL_Rect viewport,clip;
            SDL_bool clipped;
            explicit RenderState(SDL_Renderer* r):renderer(r),target(SDL_GetRenderTarget(r)),clipped(SDL_RenderIsClipEnabled(r)) {
                SDL_RenderGetScale(r,&scaleX,&scaleY);
                SDL_RenderGetViewport(r,&viewport);SDL_RenderGetClipRect(r,&clip);
                SDL_SetRenderTarget(r,nullptr);SDL_RenderSetScale(r,1,1);
                SDL_RenderSetViewport(r,nullptr);SDL_RenderSetClipRect(r,nullptr);
            }
            ~RenderState() {
                SDL_SetRenderTarget(renderer,target);SDL_RenderSetScale(renderer,scaleX,scaleY);
                SDL_RenderSetViewport(renderer,&viewport);SDL_RenderSetClipRect(renderer,clipped?&clip:nullptr);
            }
        } renderState(ren_);
        std::string title=stem;
        int chapterNumber=0;
        for (const auto& campaign:tak::loadCampaigns(vfs_))
            if (const auto chapter=tak::campaignChapter(campaign,stem)) {
                title=chapter->title;chapterNumber=chapter->index+1;break;
            }

        // No Loaded acknowledgement has been sent: the referee cannot advance
        // its first tick. Poll only the connection while the player reads, so a
        // long briefing cannot trip keepalive or silently consume mission time.
        auto loading=std::move(loadScreen_);
        writableWorld().prepareInitialVisibility();
        int width=0,height=0;
        SDL_GetRendererOutputSize(ren_,&width,&height);
        prepare(width,height);   // apply the new map's initial camera before capture
        finishTerrain();
        captureFrame();beginFrame();cosmeticStep(0);
        const bool objectives=showObjectives_;
        showObjectives_=false;   // the modal itself owns the objective text
        draw(width,height);
        showObjectives_=objectives;
        std::unique_ptr<SDL_Texture,decltype(&gpuvram::destroy)> background(nullptr,&gpuvram::destroy);
        if (width>0 && height>0) {
            std::vector<uint8_t> pixels(size_t(width)*height*4);
            if (SDL_RenderReadPixels(ren_,nullptr,SDL_PIXELFORMAT_ABGR8888,pixels.data(),width*4)==0) {
                background.reset(gpuvram::create(ren_,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_STATIC,width,height));
                if (background) SDL_UpdateTexture(background.get(),nullptr,pixels.data(),width*4);
            }
        }
        const bool dismissed=tak::BriefingScreen::run(ren_,vfs_,stem,title,settings_,nullptr,
            background.get(),[this]{return mp_->poll();},chapterNumber);
        briefingPaused_=true;
        loadScreen_=std::move(loading);
#ifndef NDEBUG
        if (verifyBriefing) {
            if (world_.tickCount()!=0 || world_.stateHash()!=briefingHash)
                throw std::runtime_error("initial briefing changed authoritative mission state");
            std::fprintf(stderr,"PASS: loaded campaign briefing preserves tick-zero state\n");
        }
#endif
        return dismissed;
    }

    bool GameView::mpStep() {
        if (outcome_ != 0 && !spectating_ && !replayMode_ && missionStem_.empty()) {
            const uint64_t now = SDL_GetTicks64();
            if (!resultStartedMs_) resultStartedMs_ = now;
            if (now - resultStartedMs_ >= 3000) menuRequested_ = true;
        }

        pumpReplayRecording();
        if (!mp_->poll()) { netError_ = mp_->error().empty() ? "disconnected" : mp_->error(); return false; }
        if (mp_->desynced()) { netError_ = mp_->desyncReason(); return false; }
#ifndef NDEBUG
        // Regression probe: an eliminated, non-acking client must not stall survivors.
        if (outcome_ < 0 && tak::devFlag("TAK_DEFEAT_STALL")) return true;
        speedProbe();
#endif
        // Send at the rate the server DRAINS (kCmdCapPerTick per sim tick), metered
        // by elapsed ticks rather than by frames. Two earlier versions of this were
        // both frame-coupled: flushing the whole outbox every render step offered
        // far more than the server accepts, and flushing one batch per step still
        // tied throughput to the frame rate in the other direction -- at 10fps that
        // is 640 commands/sec against a server willing to take 1920.
        //
        // Credit saturates at kCmdQueueCap, exactly the queue the server keeps, so
        // an honest client can never have more outstanding than the server can hold:
        // overflow stops being reachable without the loss being the client's own
        // doing. See cmdSendCredit.
        if (netTick_ != lastSendTick_) {
            cmdCredit_ = tak::net::cmdSendCredit(cmdCredit_, netTick_ - lastSendTick_);
            lastSendTick_ = netTick_;
        }
        // Rate AND window: the credit above says how fast, cmdInFlight_ says how
        // much may be outstanding. Without the window the rate limiter alone let a
        // stalled uplink accumulate 1024 unacknowledged commands against a 512
        // queue, and the server dropped the difference when they all landed.
        //
        // The outbox is ONE FIFO of commands and explicit orders (Cmd::Order, protocol
        // 267). A run of commands goes out as one PlayerCommands message under the
        // rate and window above; an order goes out whole as its transaction
        // (MpClient::sendOrder) under its own window (orderSendWindow), never behind
        // the command credit. Whatever cannot go yet waits, and so does everything
        // behind it: a Stop issued after a big Move never overtakes it.
        // Input the server dropped never comes back in a bundle: the server says so
        // (Msg::InputDropped) and that releases it from the windows, so a drop can
        // never hold the outbox shut.
        for (const auto& d : mp_->takeInputDropped()) {
            cmdInFlight_ -= std::min(cmdInFlight_, int(std::min<uint32_t>(d.commands, uint32_t(INT32_MAX))));
            if (d.serial) orderWindow_.retire(d.serial);
            if (tak::devEnv("TAK_NETLOG"))
                std::fprintf(stderr, "server dropped %u commands, order %u\n", d.commands, d.serial);
        }
        if (outboxHead_ < outbox_.size() && !cmdCatchUp_) {
            int sendable = tak::net::cmdSendWindow(cmdCredit_, cmdInFlight_);
            size_t at = outboxHead_;
            while (at < outbox_.size()) {
                size_t end = at;
                while (end < outbox_.size() && outbox_[end].kind != tak::net::Cmd::Order &&
                       int(end - at) < sendable) ++end;
                if (end > at) {
                    const int n = int(end - at);
                    cmdCredit_ -= n;
                    cmdInFlight_ += n;
                    sendable -= n;
                    mp_->sendCommands({outbox_.begin() + ptrdiff_t(at), outbox_.begin() + ptrdiff_t(end)});
                    at = end;
                    continue;
                }
                auto& item = outbox_[at];
                if (item.kind != tak::net::Cmd::Order || !item.order) break;   // out of command credit
                const size_t bytes = tak::net::orderRecordBytes(*item.order);
                if (!orderWindow_.fits(bytes)) break;
                // The serial is the one current when the order LEAVES, not when it was clicked: an
                // order clicked during a rejoin's catch-up waits here, and nextOrderSerial_ only
                // passes the orders that activated before the drop as that history is replayed.
                // flushClick made this order (non-const) and nothing else holds it until it is
                // sent, so the serial is set in place.
                std::const_pointer_cast<tak::net::SelectionOrder>(item.order)->serial = nextOrderSerial_++;
                mp_->sendOrder(*item.order);
                orderWindow_.sent(item.order->serial, bytes);
                ++at;
            }
            outboxHead_ = at;
            if (outboxHead_ == outbox_.size()) {  // fully drained
                outbox_.clear();
                outboxHead_ = 0;
            } else if (outboxHead_ > outbox_.size() / 2 && outboxHead_ > 64) {
                // Amortised compaction: each command moves at most once.
                outbox_.erase(outbox_.begin(), outbox_.begin() + ptrdiff_t(outboxHead_));
                outboxHead_ = 0;
            }
        }
        // Decide once whether to run the sim on its own worker thread. On for interactive
        // net games (the whole point -- keeps world_.tick off the render thread); off for the
        // headless harness/replay (inline, byte-identical + deterministic) unless
        // TAK_SIM_THREAD forces it on to VERIFY the threaded sim against the referee.
        if (!simThreadDecided_) {
            simThreadDecided_ = true;
            wantSimThread_ = simThreadMode_;
#ifndef NDEBUG
            if (const char* e = tak::devEnv("TAK_SIM_SLOW_US")) simSlowUs_ = std::max(0, std::atoi(e));
#endif
            if (wantSimThread_) startSimThread();
        }
        // Simulate every delivered tick, but cap per frame so a big catch-up
        // (rejoin replay) stays responsive rather than freezing for seconds.
        tak::net::Bundle bd;
        int drained = 0;
#ifndef NDEBUG
        int paceBuffered = int(mp_->bufferedBundles());
        float paceRate = 0;
        bool paceFF = false;
#endif
        auto simTick = [&] {
            // Our own commands coming back in this bundle are the server's
            // acknowledgement that it took them (lockstep relays every tick to
            // every peer, sender included). That is what retires in-flight credit.
            //
            // NOT during a rejoin replay. Catch-up feeds the whole bundle log back,
            // including this player's commands from BEFORE the disconnect -- history,
            // not acknowledgement. Counting those retired commands we had only just
            // sent, reopening the send window past what the server can hold. We send
            // nothing while catching up anyway (see cmdCatchUp_), so there is nothing
            // legitimate to retire here.
            if (int ms = mp_->room().mySlot; ms >= 0)
                for (const auto& c : bd.cmds)   // serials continue above every own order that activated
                    if (int(c.player) == ms && c.kind == tak::net::Cmd::Order && c.order)
                        nextOrderSerial_ = std::max(nextOrderSerial_, c.order->serial + 1);
            if (!cmdCatchUp_)
                if (int ms = mp_->room().mySlot; ms >= 0) {
                    for (const auto& c : bd.cmds) {
                        if (int(c.player) != ms) continue;
                        if (c.kind == tak::net::Cmd::Order) {
                            if (c.order) orderWindow_.retire(c.order->serial);
                        } else if (cmdInFlight_ > 0) --cmdInFlight_;
                    }
#ifndef NDEBUG
                    // TAK_AUTOPLAY_FORMATION delivery probe: own commands applied this tick (stderr only).
                    if (formPending_ > 0) {
                        int own = 0;   // unit commands: an explicit order counts each member
                        for (const auto& c : bd.cmds)
                            if (int(c.player) == ms)
                                own += c.kind == tak::net::Cmd::Order && c.order ? int(c.order->members.size()) : 1;
                        if (own && !formLanded_) formFirstTick_ = netTick_;
                        formLanded_ += own;
                        if (formLanded_ >= formPending_) {
                            std::fprintf(stderr, "FORMATION_DELIVERY issue=%u n=%d landed=%d first=%u last=%u\n",
                                         formIssueTick_, formPending_, formLanded_, formFirstTick_, netTick_);
                            formPending_ = 0;
                        }
                    }
#endif
                }
            if (useSimThread_) {
                // Hand this tick's bundle to the sim worker (FIFO == lockstep tick order).
                // world_ is simulated there; the state hash comes back via simOutbox_ and is
                // sent to the server below. The main thread stays free for the render.
                SimJob job;
                job.bundle = bd;
                job.tick = netTick_;
                job.wantHash = (netTick_ % uint32_t(tak::net::kHashPeriod) == 0);
                job.spectator = mp_->isSpectator();
                job.wantPos = job.wantHash && mp_->recording() && !job.spectator;
                { std::lock_guard<std::mutex> lk(inboxMutex_); simInbox_.push_back(std::move(job)); }
                inboxCv_.notify_one();
            } else {
#ifndef NDEBUG
                PaceSpan jobSpan(paceJobWallNs_, &paceJobCpuNs_);
#endif
                for (const auto& c : bd.cmds) apply(c);
                for (const auto& e : bd.events) applyEvent(e);
                // Only the SIM half per bundle (speedMult() is 1 in net games): the
                // cosmetic half runs once after the drain, so a rejoin/spectate
                // catch-up replays lockstep state without replaying 512 ticks of
                // sounds, effects and VM dispatch per frame.
                simStep(1.0f / 30.0f);
#ifndef NDEBUG
                if (simSlowUs_ > 0) std::this_thread::sleep_for(std::chrono::microseconds(simSlowUs_));
#endif
                // Report our processed tick every kHashPeriod. A seated player sends its
                // state hash (desync check + flow-control ack); a spectator sends one too
                // -- purely as a progress ACK so the server can pace an all-AI watch game
                // to what the spectator can sustain (its hash is never desync-checked, since
                // an all-AI room has no seated players to form a consensus). A spectator's
                // hash is a progress ACK only, so skip the O(units) stateHash for it.
                if (netTick_ % uint32_t(tak::net::kHashPeriod) == 0)
                    mp_->sendHash(netTick_, reportedHash(mp_->isSpectator(), netTick_),
                                  mp_->recording() && !mp_->isSpectator() ? world_.posDigest() : 0);
            }
            ++netTick_;
            ++drained;
            // During a heavy catch-up (a spectator fast-forwarding a big backlog at high
            // game speed), service the connection every so often -- answer the server's
            // keepalive pings and keep draining our recv buffer. Otherwise a spectator
            // busy simulating for >15s stops ponging and the server drops it ("peer
            // closed"). poll() is cheap (recv + ping/pong + flush); we ignore its result
            // here, the next top-of-mpStep poll() surfaces any real error.
            if ((drained & 63) == 0) mp_->poll();
        };
        if (netDelay_ == -2) {   // one-time init from the env
            // The adaptive jitter buffer is ON by default: it only ever reduces
            // stalls and is pure pacing (byte-identical sim). TAK_NET_DELAY overrides
            // -- "0" disables it (drain every bundle immediately), a positive integer
            // pins a fixed reserve depth, "auto" (or unset) self-sizes to the link.
            const char* e = tak::devEnv("TAK_NET_DELAY");
            if (!e || std::string(e) == "auto") { netAuto_ = true; netDelay_ = 3; mp_->enableRttProbe(); }
            else netDelay_ = std::max(0, std::atoi(e));
        }
        if (netAuto_) {
            // Size the buffer to cover the measured bundle-arrival jitter, with an
            // RTT-scaled floor, clamped. Recomputed each frame so it tracks the link.
            // Everything is in ticks at the CURRENT game speed (tickMs = 1000/(30*speed)): a
            // reserve sized in 1x ticks is a handful of ms at 8x, shorter than one display frame.
            // The frame term keeps at least one frame's worth of arrivals in hand, since a frame
            // consumes up to frameMs/tickMs ticks at once.
            const float sp = std::max(1, int(mp_->gameSpeed())) / 10.0f;
            const float tickMs = 1000.0f / (30.0f * sp);
            const uint64_t nowF = SDL_GetTicks64();
            if (netFrameAt_) {
                const float f = std::min(250.0f, float(nowF - netFrameAt_));
                netFrameMs_ += 0.1f * (f - netFrameMs_);
            }
            netFrameAt_ = nowF;
            int kJit = int(std::ceil(mp_->arrivalJitterMs() / tickMs));
            int kRtt = int(std::ceil(mp_->rttMs() / 60.0f));   // gentle RTT floor (1x ticks, as before)
            int kFrame = netFrameMs_ > tickMs ? int(std::ceil(netFrameMs_ / tickMs)) : 0;   // sub-tick frames: 1x unchanged
            netDelay_ = std::clamp(2 + std::max({kJit, kRtt, kFrame}), 2, std::max(16, int(16.0f * sp)));
        }
        // WE S3: a seated player's sim worker is fed only while its inbox holds kSimInboxCap ticks
        // or fewer at the start of the frame. The rest wait in mp_, where the jitter servo, the
        // 900-tick guard and the server's flow control (Msg::Progress below) all see them, so a
        // client that cannot sustain the speed slows the game to what it can play instead of
        // queueing 120-150 ticks of command latency in the inbox. Rejoin catch-up and spectators
        // keep the 512-tick fast-forward: they replay history the server is not waiting on.
        constexpr size_t kSimInboxCap = 2;
        const bool capInbox = useSimThread_ && !cmdCatchUp_ && !mp_->isSpectator();
        bool inboxFull = false;
        if (capInbox) {
            std::lock_guard<std::mutex> lk(inboxMutex_);
            inboxFull = simInbox_.size() > kSimInboxCap;
        }
        if (netDelay_ <= 0) {
            // Default: drain to the newest delivered bundle every frame.
            while (!inboxFull && drained < 512 && netTick_ < netStopTick_ && mp_->takeBundle(netTick_, bd)) simTick();
            // Stall metric: 0 ticks played this frame while future bundles ARE
            // buffered means the one we need is late (head-of-line block) -- a stall.
            ++netBenchFrames_;
            if (drained == 0 && mp_->bufferedBundles() > 0) ++netBenchStalls_;
        } else {
            // Jitter buffer: fill an initial reserve of netDelay_ bundles, then pace
            // the sim on the wall clock at ~30 Hz, staying that many bundles behind
            // the newest received. A late bundle is covered from the reserve; only a
            // gap deeper than the reserve stalls. Same bundles, same order -> the sim
            // and every hash are byte-identical, so this is a pure pacing change.
            uint64_t now = SDL_GetTicks64();
            float dt = netStepMs_ ? std::min(0.25f, float(now - netStepMs_) / 1000.0f) : 0.0f;
            netStepMs_ = now;
            int buffered = int(mp_->bufferedBundles());
            if (!netBufReady_) {
                if (buffered < netDelay_) return true;   // still filling the reserve
                netBufReady_ = true; netAccum_ = 0;
            }
            // Adaptive playout: run the sim clock slightly fast/slow to servo the
            // buffer depth to the target (netDelay_). Too deep -> play a touch faster
            // (drain toward target, shed latency); too shallow -> slower (rebuild the
            // reserve). Holds the added latency tight at ~netDelay_ ticks.
            float err = float(buffered - netDelay_);
            // Base playout tracks the game speed (server ticks that much faster/slower).
            float sp = std::max(1, int(mp_->gameSpeed())) / 10.0f;
            float rate = 30.0f * sp * std::clamp(1.0f + 0.06f * err, 0.7f, 1.3f);
            netAccum_ += dt * rate;            // accumulates fractional TICKS now
            int budget = int(netAccum_);
            netAccum_ -= float(budget);
            // A deep backlog (rejoin replay, or the client fell behind) is NOT jitter
            // -- fast-forward it back down to the target reserve instead of pacing.
            const bool fastForward = buffered > netDelay_ + 60 && !capInbox;
            if (fastForward) budget = 512;
            if (inboxFull) budget = 0;
#ifndef NDEBUG
            paceBuffered = buffered; paceRate = rate; paceFF = fastForward;
#endif
            budget = std::min(budget, 512);
            while (drained < budget && netTick_ < netStopTick_ && mp_->takeBundle(netTick_, bd)) simTick();
            // Stall metric: the wall clock wanted more ticks than we could play
            // because the next bundle isn't buffered yet (jitter exceeded the
            // reserve). One count per starved frame.
            ++netBenchFrames_;
            if (drained < budget && !mp_->haveBundle(netTick_)) ++netBenchStalls_;
        }
        // Caught up: the replayed log is spent and what arrives now is live. Reset the
        // window rather than trust a count accumulated across the reconnect -- the
        // server cleared this connection's queue when we left.
        //
        // AFTER both drain paths, not inside one. There are two -- the immediate
        // drain (netDelay_ <= 0) and the jitter-buffered one -- and the default is
        // the buffered one, so a check placed in the other branch never ran at all
        // and a rejoined player stayed locked out of issuing orders for the whole
        // game.
        //
        // The gate is the SERVER'S boundary, not our buffer depth. Two local
        // heuristics failed here: "buffered == 0" essentially never fires because
        // the adaptive buffer keeps a reserve in hand, and "down to the reserve"
        // cannot tell the end of history from a GAP BETWEEN REPLAY CHUNKS -- the
        // replay is streamed in socket-paced chunks, so the buffer legitimately
        // runs dry mid-history and the gate opened early, putting the false-
        // acknowledgement bug straight back.
        if (cmdCatchUp_ && netTick_ >= cmdReplayEnd_) {
            cmdCatchUp_ = false;
            if (tak::devEnv("TAK_NETLOG"))
                std::fprintf(stderr, "catch-up complete at tick %u\n", netTick_);
            cmdInFlight_ = 0;
            cmdCredit_ = tak::net::kCmdCapPerTick;
            orderWindow_.clear();
        }
        // Send the worker's finished per-tick state hashes to the server (lockstep desync
        // check + flow-control ack). Drained here on the main thread -- mp_ has a single owner.
        if (useSimThread_) {
            std::deque<HashJob> done;
            { std::lock_guard<std::mutex> lk(outboxMutex_); done.swap(simOutbox_); }
            for (const auto& h : done) mp_->sendHash(h.tick, h.hash, h.pos);
            drainPendingNotice();   // apply any HUD notice the worker posted (god/mission/scenario)
        }
        // WE S3 flow control: report the tick actually PROCESSED (the worker's, never the push
        // position) every kProgressPeriod ticks, so the server runs flowLeadTicks() past it rather
        // than 120 ticks past a hash sent every 30. Seated players and spectators alike (a spectator
        // paces an all-AI room).
        if (netTick_ > 0) {
            const uint32_t pushed = netTick_ - 1;
            const uint32_t done = useSimThread_ ? std::min(simProcessedTick_.load(std::memory_order_relaxed), pushed)
                                                : pushed;
            if (!progressSent_ || done < progressSentTick_ ||
                done - progressSentTick_ >= uint32_t(tak::net::kProgressPeriod)) {
                mp_->sendProgress(done);
                progressSent_ = true;
                progressSentTick_ = done;
            }
        }
#ifndef NDEBUG
        if (!paceInit_) { paceInit_ = true; pace_.open(tak::devEnv("TAK_PACELOG")); }
        if (pace_.on()) {
            // What the player saw this frame: the worker's finished ticks with the sim thread on, the
            // drained count inline. The wall clock is read here, so a long sim stall shows in wallMs.
            const uint64_t nowMs = SDL_GetTicks64();
            tak::PaceLog::Frame fr;
            fr.wallMs = paceLastMs_ ? double(nowMs - paceLastMs_) : 0.0;
            paceLastMs_ = nowMs;
            if (useSimThread_) {
                const uint32_t done = simProcessedTick_.load(std::memory_order_relaxed);
                fr.ticks = int(done - paceLastTick_);
                paceLastTick_ = done;
                std::lock_guard<std::mutex> lk(inboxMutex_);
                fr.inbox = int(simInbox_.size());
                fr.tick = done;
            } else {
                fr.ticks = drained;
                fr.tick = netTick_;
            }
            fr.buffered = paceBuffered; fr.rate = paceRate; fr.fastForward = paceFF;
            fr.backlog = int(mp_->bufferedBundles());
            if (useSimThread_ && netTick_ > 0)
                fr.backlog += int(std::max<long long>(0, (long long)netTick_ - 1 -
                                                         (long long)simProcessedTick_.load(std::memory_order_relaxed)));
            // What the frame shows: the pinned snapshot, interpolated the way the render does.
            fr.shown = front().gameTick;
            if (present_.on) {
                // The presentation clock's own record of this frame (presentFrame).
                fr.td = present_.td; fr.dtd = present_.dtd; fr.nominal = present_.nominal;
                fr.freeze = present_.freeze; fr.gate = present_.gate; fr.snap = present_.snap;
                fr.lagTarget = float(present_.lagTarget);
            } else {
                const double rawAlpha = front().tickDurMs > 0.0f
                    ? double(nowMs - front().tickMs) / double(front().tickDurMs) : 1.0;
                fr.td = double(fr.shown) - 1.0 + std::clamp(rawAlpha, 0.0, 1.0);
                fr.freeze = rawAlpha >= 1.0;   // the newest pose is held: nothing newer to interpolate toward
                // Display time advanced this frame against the frame's nominal step (its wall time at
                // the game speed), timed on the performance counter: millisecond ticks would quantise a
                // 4.17 ms frame by 24%.
                const uint64_t nowC = SDL_GetPerformanceCounter();
                if (paceLastCounter_ && paceLastTd_ >= 0) {
                    const double dtS = double(nowC - paceLastCounter_) / double(SDL_GetPerformanceFrequency());
                    fr.nominal = paused_ ? 0.0 : dtS * 30.0 * double(animSpeed());
                    fr.dtd = fr.td - paceLastTd_;
                }
                paceLastCounter_ = nowC;
                paceLastTd_ = fr.td;
            }
            fr.lockWaitUs = double(paceLockWaitNs_) / 1000.0;
            paceLockWaitNs_ = 0;
            const uint64_t now[5] = {paceWorldNs_.load(), paceCaptureNs_.load(), paceHashNs_.load(),
                                     paceJobWallNs_.load(), paceJobCpuNs_.load()};
            double* out[5] = {&fr.worldMs, &fr.captureMs, &fr.hashMs, &fr.jobWallMs, &fr.jobCpuMs};
            for (size_t i = 0; i < 5; ++i) {
                *out[i] = double(now[i] - paceSeenNs_[i]) / 1e6;
                paceSeenNs_[i] = now[i];
            }
            pace_.frame(fr);
        }
#endif
        // Cosmetics once per frame, covering the game time actually DISPLAYED: the presentation
        // clock's advance (particles, timers and the tick-stamped event queues then follow what is
        // on screen), or without it the ticks played.
        if (present_.on) { if (present_.dtd > 0) cosmeticStep(float(present_.dtd) / 30.0f); }
        else if (drained > 0) cosmeticStep(float(drained) / 30.0f);
        // Spectator progress heartbeat: keep the server's flow-control ack FRESH even
        // when we're caught up and not draining. The per-tick hash send only fires while
        // draining (at 30-tick boundaries), so a caught-up spectator would stop acking;
        // the server -- pacing an all-AI game to that now-stale ack -- would then hold
        // forever (ACTUAL 0.0x) and eventually time us out ("recv failed"). One ack per
        // ~0.4s wall keeps the server's view of our position current.
        if (mp_->isSpectator()) {
            uint64_t nowHb = SDL_GetTicks64();
            if (nowHb - lastSpecAckMs_ > 400) {
                lastSpecAckMs_ = nowHb;
                // A spectator's hash is a pure progress ACK -- the server never
                // desync-checks it (an all-AI room has no seated consensus). So send a
                // trivial value, NOT world_.stateHash(): folding thousands of units into an
                // FNV every 0.4s on the render thread was a periodic hitch that scaled with
                // the battle. The TICK is what the server's flow control reads -- and it must
                // be the tick actually PROCESSED (the worker's, when threaded), not the push
                // position netTick_, or the server thinks we're further along than we are and
                // over-delivers (growing the sim backlog).
                uint32_t ackTick = useSimThread_ ? simProcessedTick_.load(std::memory_order_relaxed)
                                                 : (netTick_ ? netTick_ - 1 : 0);
                mp_->sendHash(ackTick, 0);
            }
        }
        // Measure the ACTUAL game speed: how fast our sim really advances (ticks/sec
        // over a ~0.5s window, /30 = a speed multiplier). At the requested speed this
        // tracks it; when a client can't keep up (or the server paces to the slowest)
        // it reads lower. Shown on the F4 board next to the requested speed.
        {
            uint64_t na = SDL_GetTicks64();
            if (actualSpeedT0_ == 0) { actualSpeedT0_ = na; actualSpeedTick0_ = netTick_; }
            else if (na - actualSpeedT0_ >= 500) {
                float ips = float(int64_t(netTick_) - int64_t(actualSpeedTick0_)) * 1000.0f
                            / float(na - actualSpeedT0_);
                // CAPPED AT THE REQUESTED SPEED. The playout controller deliberately
                // runs up to 1.3x to drain a deep buffer (see the clamp on `rate`
                // above), and at the start of a game or benchmark the buffer fills
                // ahead of playout, so it pins to exactly that -- which is why a fresh
                // game read "1.3x" before it settled. The sim was not going faster than
                // asked; the buffer was catching up. Reporting the transient made the
                // readout look wrong at the one moment everybody watches it.
                //
                // So the ceiling is the requested speed, and the meaning is kept simple:
                // at the requested multiplier the sim is healthy, below it the client
                // (or the server, pacing to the slowest) cannot sustain the pace. That
                // is the only question this row answers, and undershoot -- the half that
                // matters -- is untouched.
                const float reqSp = std::max(1, int(mp_->gameSpeed())) / 10.0f;
                const float sample = std::min(ips / 30.0f, reqSp);
                // SMOOTHED, because the raw sample is mostly quantisation noise. A ~0.5s
                // window holds about 15 ticks at 1x, so one tick landing either side of
                // the boundary moves the reading ~7% -- the number changed constantly
                // while telling the reader nothing. An exponential average over roughly
                // the last two seconds still drops promptly when a client genuinely
                // cannot keep up, which is the only thing this readout is for.
                //
                // Seeded from the first sample rather than ramping up from zero, so the
                // display is honest immediately instead of climbing for two seconds.
                // A CHANGE OF REQUESTED SPEED IS A DISCONTINUITY, NOT NOISE. Smoothing
                // across it is wrong in both directions, and badly so downwards: after
                // a host drops 4x to 1x the stored average is still ~4, and capping only
                // the incoming sample leaves the row reading 3.2x for several seconds
                // while playback is already sustaining exactly 1x. Restart the average
                // from the first sample at the new speed instead. (Found by review.)
                if (reqSp != actualSpeedReq_) { actualSpeed_ = 0.0f; actualSpeedReq_ = reqSp; }
                actualSpeed_ = actualSpeed_ > 0.0f
                                   ? actualSpeed_ + (sample - actualSpeed_) * 0.25f
                                   : sample;
                // Belt and braces: the average can only ever sit at or below the speed
                // that was asked for, whatever route it took to get here.
                actualSpeed_ = std::min(actualSpeed_, reqSp);
                actualSpeedT0_ = na; actualSpeedTick0_ = netTick_;
            }
        }
        // "Machine too slow" guard: if the backlog stays deep for a sustained
        // stretch, this client can't process ticks as fast as they arrive and
        // will never catch up -- fail clearly instead of falling ever further
        // behind (or reconnect-looping). With the sim worker, the main thread drains
        // mp_'s buffer FAST (into simInbox_), so bufferedBundles() no longer reflects a
        // slow client -- the backlog is (pushed - processed). Count both.
        uint64_t now = SDL_GetTicks64();
        long long simBacklog = useSimThread_
            ? std::max<long long>(0, (long long)netTick_ - 1 - (long long)simProcessedTick_.load(std::memory_order_relaxed))
            : 0;
        if (mp_->bufferedBundles() + simBacklog > 900) {
            if (!mpSlowSinceMs_) mpSlowSinceMs_ = now;
            else if (now - mpSlowSinceMs_ > 5000) {
                if (mp_->isSpectator()) {
                    // A spectator is a WATCHER, not a lockstep participant -- it holds
                    // nobody up. If it can't sustain the game speed (e.g. a big battle
                    // at 4x) it just LAGS behind the live game instead of disconnecting;
                    // the keepalive poll in the drain keeps the connection healthy. Show
                    // a transient hint and carry on. (Lowering the speed catches it up.)
                    notice_ = "SPEED TOO HIGH -- SPECTATOR LAGGING";
                    noticeTimer_ = 2;
                    mpSlowSinceMs_ = now;   // re-arm; don't spam
                } else {
                    netError_ = "this machine can't keep up with the game speed";
                    return false;
                }
            }
        } else {
            mpSlowSinceMs_ = 0;
        }
        return true;
    }

#ifndef NDEBUG
// TAK_SPEED_AT=<tick>:<speed>[,<tick>:<speed>...] -- the host changes the game speed (tenths)
// in-game when the played tick reaches <tick>, exactly as the +/- keys do. Each change, and one
// line per wall second, is printed as SPEEDLOG (wall ms, played tick, requested speed) so a
// harness can measure the real playout rate across a change. Debug-only, pacing only.
void GameView::speedProbe() {
    static const char* spec = tak::devEnv("TAK_SPEED_AT");
    if (!spec || !mp_ || mp_->state() != tak::net::MpClient::State::InGame) return;
    if (speedAt_.empty() && !speedAtParsed_) {
        speedAtParsed_ = true;
        std::string s = spec;
        for (size_t p = 0; p < s.size();) {
            size_t e = s.find(',', p); if (e == std::string::npos) e = s.size();
            const std::string item = s.substr(p, e - p);
            const size_t c = item.find(':');
            if (c != std::string::npos)
                speedAt_.push_back({uint32_t(std::atoi(item.c_str())), std::atoi(item.c_str() + c + 1)});
            p = e + 1;
        }
    }
    const uint64_t now = SDL_GetTicks64();
    if (!speedLogMs_) speedLogMs_ = now;
    if (speedAtNext_ < speedAt_.size() && netTick_ >= speedAt_[speedAtNext_].first) {
        auto o = mp_->room().opts;
        o.speed = uint8_t(std::clamp(speedAt_[speedAtNext_].second, 1, tak::net::kMaxDevGameSpeed));
        mp_->setGameOptions(o);
        std::fprintf(stderr, "SPEEDLOG set %llu tick %u speed %d\n", (unsigned long long)now, netTick_, int(o.speed));
        ++speedAtNext_;
    }
    if (now - speedLogMs_ >= 1000) {
        speedLogMs_ += 1000;
        std::fprintf(stderr, "SPEEDLOG wall %llu tick %u speed %d buffered %d delay %d\n", (unsigned long long)now,
                     netTick_, int(mp_->gameSpeed()), int(mp_->bufferedBundles()), netDelay_);
    }
}

// See gameview.h. Issues ordinary Move / AttackMove orders through GameView::issue,
// so they travel the real path -- outbox -> sendCommands -> server -> bundle -> every
// peer -- rather than being applied locally, which would desync by construction.
void GameView::autoplayStep() {
    ClickScope click(*this);   // one click: a Legion net game sends it as explicit orders
    static const int rate = [] {
        const char* e = tak::devEnv("TAK_AUTOPLAY");
        return e ? std::clamp(std::atoi(e), 0, 60) : 0;
    }();
    // TAK_AUTOPLAY_FORMATION: the human seat plays like a player who forms armies -- every 600 ticks it puts
    // all its mobile units in formation 1 (Alt+1) and sends them to fight at the enemy's base, one command per
    // unit through GameView::issue, so the client's rate credit spreads a big selection over the same ticks a
    // real click does. Without it the --mpai human never forms a group and the AI never does, so no Legion group
    // code runs at all in that harness.
    static const bool formation = tak::devFlag("TAK_AUTOPLAY_FORMATION");
    if ((!rate && !formation) || spectating_ || replayMode_ || localPlayer_ < 0) return;
    if (netTick_ < autoplayNext_) return;
    if (formation) {
        autoplayNext_ = netTick_ + 600u;
        std::vector<int> mine;
        float tx = -1, tz = -1;
        {
            const auto live = liveWorld();
            for (const auto& u : live->units()) {
                if (!u.alive() || !u.type) continue;
                if (int(u.player) == localPlayer_) {
                    // The monarch stays home: sending it into the enemy base ends the game, and with it the run.
                    if (u.type->maxVel > tak::sim::Fixed() && !u.type->commander && !u.embarked() && !u.underConstruction)
                        mine.push_back(u.id);
                } else if (!live->allied(localPlayer_, int(u.player)) && (tx < 0 || (u.type->commander && !u.type->canFly))) {
                    tx = u.x.toFloat(); tz = u.z.toFloat();   // the first enemy seen, or better, an enemy monarch
                }
            }
        }
        if (mine.empty() || tx < 0) return;
        for (int id : mine) {
            tak::net::Command c;
            c.kind = tak::net::Cmd::SetSquad;
            c.unitId = id;
            c.targetId = -1;     // formation 1
            issue(c);
        }
        for (int id : mine) {
            tak::net::Command c;
            c.kind = tak::net::Cmd::AttackMove;
            c.unitId = id;
            c.x = tx; c.z = tz;  // one shared point: the Legion group's click
            c.queue = 0;
            issue(c);
        }
        // Delivery probe: the issue tick and the commands to watch for (an unfinished earlier order's
        // count is reported as it stood).
        if (formPending_ > 0)
            std::fprintf(stderr, "FORMATION_DELIVERY issue=%u n=%d landed=%d first=%u last=-1 (superseded)\n",
                         formIssueTick_, formPending_, formLanded_, formFirstTick_);
        formIssueTick_ = netTick_;
        formPending_ = int(mine.size()) * 2;
        formLanded_ = 0;
        formFirstTick_ = 0;
        return;
    }

    // Seed once, from the slot and the game seed: same game + same slot -> same order
    // stream, so a run reproduces. Different slots diverge, which is the point --
    // eight players pulling in different directions is what interleaves commands.
    if (autoplayRng_ == 0)
        autoplayRng_ = uint32_t(localPlayer_ + 1) * 2654435761u ^
                       ((mp_ ? mp_->startSeed() : 0u) * 2246822519u) ^ 0x9e3779b9u;
    auto rnd = [&] {
        autoplayRng_ = autoplayRng_ * 1664525u + 1013904223u;
        return autoplayRng_ >> 8;
    };

    // Schedule the NEXT order off the tick clock, never the wall clock.
    autoplayNext_ = netTick_ + std::max(1u, 300u / uint32_t(rate));

    // Collect this player's mobile units. Stable order (units_ is append-only and
    // identical on every peer), so indexing into it is reproducible.
    //
    // UNDER simMutex_ (liveWorld): with TAK_SIM_THREAD the worker mutates world_ while this
    // runs on the main thread, and a spawn that reallocates units_ invalidates the iteration
    // outright -- not a torn read, a crash.
    std::vector<int> mine;
    {
        const auto live = liveWorld();
        for (const auto& u : live->units())
            if (u.alive() && u.type && int(u.player) == localPlayer_ && u.type->maxVel > tak::sim::Fixed() &&
                !u.embarked() && !u.underConstruction)
                mine.push_back(u.id);
    }
    if (mine.empty()) return;

    // Order a handful at a time: one unit per order is too thin to collide with
    // another client's commands in the same tick, which is the thing being tested.
    const int batch = 1 + int(rnd() % 4);
    const float w = float(mapView_.map().width) * 16.0f;
    const float h = float(mapView_.map().height) * 16.0f;
    for (int i = 0; i < batch; ++i) {
        tak::net::Command c;
        c.unitId = mine[rnd() % mine.size()];
        c.x = float(rnd() % uint32_t(w > 32 ? w - 32 : 32)) + 16.0f;
        c.z = float(rnd() % uint32_t(h > 32 ? h - 32 : 32)) + 16.0f;
        // Mostly plain moves, some attack-moves: attack-move drives acquisition and
        // combat, which is where the sim has the most state to disagree about.
        c.kind = (rnd() % 4 == 0) ? tak::net::Cmd::AttackMove : tak::net::Cmd::Move;
        c.queue = 0;
        issue(c);
    }
}
#endif

    bool GameView::mpAutoStep(int autoMode, const std::string& mapId, bool crusades) {
        using S = tak::net::MpClient::State;
        if (!mp_->poll()) { netError_ = mp_->error(); return false; }
        S st = mp_->state();
        if (st == S::Done) { if (netError_.empty()) netError_ = mp_->error(); return false; }
        // A START the server refused ends a harness run, with the reason as its error. Only
        // the auto-start paths set mpStarted_; a player in the room reads the refusal there.
        if (st == S::InRoom && mpStarted_ && !mp_->startError().empty()) {
            netError_ = "start refused: " + mp_->startError(); return false;
        }
        if (st == S::Lobby && (autoMode == 1 || autoMode == 4 || autoMode == 7 || autoMode == 8)) {
            tak::net::GameOptions o; o.crusades = crusades ? 1 : 0;
            o.overridePolicy = uint8_t(policy_);   // room tier = this host's launch tier
            // TAK_SPEED: set the game speed in tenths (10 = 1x) for headless timing
            // tests -- re-cadences the server without touching the (deterministic) sim.
            if (const char* sp = tak::devEnv("TAK_SPEED")) o.speed = uint8_t(std::clamp(std::atoi(sp), 1, tak::net::kMaxDevGameSpeed));
            // TAK_SPEED_AT schedules in-game speed changes (see mpStep), so the room must
            // allow them.
            if (tak::devEnv("TAK_SPEED_AT")) o.speedUnlock = 1;
            o.stressTest = tak::devFlag("TAK_STRESS") ? tak::net::kStressStaged : 0;   // headless: spawn ~95% cap per AI
            if (const char* be = tak::devEnv("TAK_BENCH")) {   // headless: benchmark run
                int lv = std::atoi(be);                        // TAK_BENCH=<level 1..6>, default High
                benchmarkLevel_ = (lv >= 1 && lv <= tak::sim::kBenchLevels) ? lv : 3;
                benchmarkMode_ = true;                         // (forces watch + 8 AI + cap 8 + Ulasem below)
            }
            if (const char* uc = tak::devEnv("TAK_UNITCAP")) o.unitCap = uint16_t(std::atoi(uc));
            // The remaining room options, as headless knobs. Without these a harness
            // could not reach the code they gate -- and GODS in particular is where a
            // desync has already hidden once: summoning used to run only on the client,
            // so the referee's world ran a unit short from the first god onward. A
            // desync hunt that cannot turn gods on cannot find that class of bug.
            // TAK_RANDOM_STARTS=2 picks BY SLOT; any other set value but 0 RANDOM.
            const char* starts = tak::devEnv("TAK_RANDOM_STARTS");
            o.randomStarts = starts && std::atoi(starts) == tak::net::kStartsBySlot ? tak::net::kStartsBySlot
                           : tak::devFlag("TAK_RANDOM_STARTS") ? tak::net::kStartsRandom : tak::net::kStartsFixed;
            o.monarchExpendable = tak::devFlag("TAK_MONARCH_EXPENDABLE") ? 1 : 0;
            o.forfeitSelfDestruct = tak::devFlag("TAK_FORFEIT_SELFDESTRUCT") ? 1 : 0;
            o.doubleSight = tak::devFlag("TAK_DOUBLE_SIGHT") ? 1 : 0;
            o.pathfindingMode = tak::devFlag("TAK_LEGION") ? tak::sim::PathfindingMode::Legion :
                tak::sim::PathfindingMode::Retail;
            // TAK_FOG=0|1|2 forces the room's fog rule (not explored / explored /
            // full vision) so the setting can be tested end to end without driving
            // the lobby by hand.
            if (const char* fg = tak::devEnv("TAK_FOG"))
                o.fogExplored = uint8_t(std::clamp(std::atoi(fg), 0, 2));
            // TAK_NO_SPECTATORS=1: the room refuses watchers (ALLOW SPECTATORS off), so
            // a --mpspectate client can be shown the server's refusal.
            o.allowSpectators = tak::devFlag("TAK_NO_SPECTATORS") ? 0 : 1;
            // TAK_MP_WATCH: host creates the game as a spectator (no slot) so every
            // slot can be an AI -- an all-AI game to watch.
            if (benchmarkMode_) o.benchmark = uint8_t(benchmarkLevel_);   // menu Benchmark: intensity level
            // Benchmark is an all-AI WATCH run (host takes no slot) on Ulasem Arena, forced
            // to 8 slots regardless of the map's start-position count (setupMatch synthesises
            // the extra starts), private (not in the browser).
            bool watch = (autoMode == 1 && tak::devEnv("TAK_MP_WATCH")) || benchmarkMode_;
            // Mode 7 is interactive SINGLE-PLAYER: a private game (hidden from the
            // browser) with one server-run AI opponent.
            // Mode 8 is a single-player CAMPAIGN mission: a private game whose world is
            // built from the mission bundle (server + every peer run setupMission).
            bool priv = autoMode == 7 || autoMode == 8 || benchmarkMode_;
            std::string mission = autoMode == 8 ? missionStem_ : std::string();
            // TAK_MAP_SCENARIO=1: create the room with MAP SCENARIO on (the server refuses START when
            // the map has no scenario or a needed seat is empty -- a harness can test both).
            o.mapScenario = autoMode != 8 && tak::devFlag("TAK_MAP_SCENARIO") ? 1 : 0;
            uint8_t cap = (autoMode == 8 || benchmarkMode_) ? tak::net::kMaxSlots : mpCapacity(o.mapScenario != 0);
            // Exact stem match (findMap): the map file is "ulasem arena.tnt".
            std::string createMap = benchmarkMode_ ? std::string("Ulasem Arena") : mapId;
            mp_->createGame(benchmarkMode_ ? "Benchmark" : (priv ? "Single Player" : "headless"),
                            "", createMap, o, cap, watch, priv, mission);
        } else if (st == S::Lobby && autoMode == 5) {
            // Rejoin: read the resume ticket the original session saved and
            // reconnect to the held slot.
            uint32_t gid = 0; uint64_t tok = 0;
            if (readResume(gid, tok) && gid) { mp_->rejoin(gid, tok); mpReadied_ = true; }
        } else if (st == S::Lobby && autoMode == 6) {
            // Spectate: poll the list and watch the first running game. Ask once
            // per refusal, not every frame: a room with ALLOW SPECTATORS off says no,
            // and the browser keeps showing why.
            if (SDL_GetTicks64() - mpListMs_ > 300) { mp_->listGames(); mpListMs_ = SDL_GetTicks64(); }
            if (mp_->joinError().empty())
                for (const auto& g : mp_->games())
                    if (g.running) { mp_->spectate(g.id, ""); break; }
        } else if (st == S::Lobby && (autoMode == 2 || autoMode == 3)) {
            // Poll the game list; join the first, or (mode 3) create if none appear.
            if (SDL_GetTicks64() - mpListMs_ > 300) { mp_->listGames(); mpListMs_ = SDL_GetTicks64(); }
            if (!mp_->games().empty()) mp_->joinGame(mp_->games().front().id, "");
            else if (autoMode == 3 && mpListMs_ && SDL_GetTicks64() - mpFirstListMs_ > 800) {
                tak::net::GameOptions o; o.crusades = crusades ? 1 : 0;
            o.overridePolicy = uint8_t(policy_);   // room tier = this host's launch tier
                o.mapScenario = tak::devFlag("TAK_MAP_SCENARIO") ? 1 : 0;   // as for modes 1/4/7
                mp_->createGame(mapId, "", mapId, o, mpCapacity(o.mapScenario != 0));
            }
            if (!mpFirstListMs_) mpFirstListMs_ = SDL_GetTicks64();
        } else if (st == S::InRoom && autoMode && !mpReadied_) {
            const auto& r = mp_->room();
            // Host-spectator (TAK_MP_WATCH): seat AIs in the LOW slots (0..N-1) and
            // don't seat self -- an all-AI game the host just watches.
            if (r.mySlot < 0 && (benchmarkMode_ || (autoMode == 1 && tak::devEnv("TAK_MP_WATCH")))) {
                const char* ai = tak::devEnv("TAK_MP_AIS");
                // Benchmark: always a full 8-faction FFA (each AI its own team -> they fight,
                // which is the point of the load test). FIXED factions by slot (k%5):
                // AI1 Aramon, AI2 Taros, AI3 Veruna, AI4 Zhon, AI5 Creon, AI6 Aramon,
                // AI7 Taros, AI8 Veruna -- never random.
                int nAi = benchmarkMode_ ? int(tak::net::kMaxSlots)
                                         : std::clamp(ai ? std::atoi(ai) : 2, 2, int(tak::net::kMaxSlots));
                for (int k = 0; k < nAi; ++k)
                    mp_->setSlot(k, 2, uint8_t(k % 5), uint8_t(k), uint8_t(k), 1, aiLevelEnv());
                mpReadied_ = true;
            } else if (autoMode == 8 && r.mySlot >= 0) {
                // Campaign mission: seat the human ready and start; the mission's own
                // script drives the enemies (no skirmish AI slots).
                mp_->setSlot(r.mySlot, 1, facIdx(side_), uint8_t(r.mySlot),
                             uint8_t(r.mySlot), 1);
                mp_->startGame();
                mpStarted_ = true;
                mpReadied_ = true;
            } else if (autoMode == 7 && r.mySlot >= 0) {
                // Single-player: seat self UNREADY and hand off to the interactive
                // Room, where the player adds one or more AI opponents, then readies
                // up and starts.
                mp_->setSlot(r.mySlot, 1, facIdx(side_), uint8_t(r.mySlot),
                             uint8_t(r.mySlot), 0);
                // Headless test hook: TAK_SP_AIS=N seats N AI opponents and starts
                // immediately (the interactive path leaves this to the player).
                if (const char* na = tak::devEnv("TAK_SP_AIS")) {
                    // The auto-start hook must ready-up the host (interactive SP now
                    // seats unready, which would otherwise block startGame()).
                    mp_->setSlot(r.mySlot, 1, facIdx(side_), uint8_t(r.mySlot),
                                 uint8_t(r.mySlot), 1);
                    int n = std::clamp(std::atoi(na), 1, int(tak::net::kMaxSlots) - 1);
                    for (int k = 0; k < n && k + 1 < int(tak::net::kMaxSlots); ++k) {
                        int slot = k + 1;
                        mp_->setSlot(slot, 2, uint8_t((facIdx(aiSide_) + k) % 5),
                                     uint8_t(slot), uint8_t(slot), 1, aiLevelEnv());
                    }
                    mp_->startGame();
                    mpStarted_ = true;
                }
                mpReadied_ = true;
            } else if (r.mySlot >= 0) {
                mp_->setSlot(r.mySlot, 1, uint8_t(r.mySlot % 5), uint8_t(r.mySlot),
                             uint8_t(r.mySlot), 1);
                // autoMode 4 (AI-game host): also seat one AI opponent in slot 1.
                if (autoMode == 4 && r.mySlot == 0)
                    mp_->setSlot(1, 2, 1, 1, 1, 1, aiLevelEnv());   // AI, tar, colour 1, team 1
                // Host stress harness: TAK_MP_AIS=N seats N server AIs in the TOP
                // slots, leaving the low slots for human joiners.
                if (autoMode == 1 && r.mySlot == 0)
                    if (const char* ai = tak::devEnv("TAK_MP_AIS")) {
                        int nAi = std::clamp(std::atoi(ai), 0, tak::net::kMaxSlots - 1);
                        for (int k = 0; k < nAi; ++k) {
                            int slot = tak::net::kMaxSlots - 1 - k;
                            mp_->setSlot(slot, 2, uint8_t(slot % 5), uint8_t(slot),
                                         uint8_t(slot), 1, aiLevelEnv());   // AI, distinct colour/team
                        }
                    }
                mpReadied_ = true;
            }
        } else if (st == S::InRoom && (autoMode == 1 || autoMode == 3 || autoMode == 4) &&
                   !mpStarted_ &&
                   mp_->room().hostId == mp_->myClientId()) {
            // (mode 7 single-player does NOT auto-start: the player adds AIs and
            //  clicks START in the Room.)
            int ready = 0;
            for (int i = 0; i < tak::net::kMaxSlots; ++i) {
                const auto& s = mp_->room().slots[i];
                if ((s.type == 1 && s.ready) || s.type == 2) ++ready;   // human-ready or AI
            }
            // Default: start with any 2 ready. TAK_MP_WAIT=N holds for a full lobby.
            static const int wantReady = [] {
                const char* w = tak::devEnv("TAK_MP_WAIT"); return w ? std::atoi(w) : 2;
            }();
            if (ready >= wantReady && mp_->room().mapsReady) { mp_->startGame(); mpStarted_ = true; }
        } else if (mp_->isRejoin()) {
            // Rejoin OR spectate (checked BEFORE the state branches -- the replayed
            // bundles may already have flipped the state to InGame): reset the sim
            // and replay from tick 0. The server has queued the whole bundle log
            // after the GameStarting; mpStep drains it, fast-forwarding to now.
            bool spec = mp_->isSpectator();
            mp_->clearRejoin();
            // Unit ids restart at 1: snapshots from here on carry a new world epoch,
            // so render caches never take a new unit for the old one with its id.
            worldEpoch_.fetch_add(1, std::memory_order_release);
            writableWorld().resetForReplay();
            netTick_ = 0; outcome_ = 0; resultStartedMs_ = 0; netError_.clear();
            // Replaying history from tick 0: hold our own orders and stop counting
            // acknowledgements until the log is spent (see cmdCatchUp_).
            // The SERVER says where history ends; we do not guess (see
            // Client::replayTicks and the gate below).
            cmdReplayEnd_ = mp_->replayTicks();
            cmdCatchUp_ = cmdReplayEnd_ > 0;
            cmdInFlight_ = 0;
            orderWindow_.clear();
            outbox_.clear();   // pre-disconnect orders are moot; the server dropped them
            outboxHead_ = 0;
            startMpGame(mp_->startRoom(), mp_->startSeed());
            if (spec) {
                beginSpectating();   // watch-only: no fog, no control, no resume
            } else {
                mp_->reportLoaded(gameDataHash_);
                writeResume(mp_->gameId(), mp_->resumeToken());
            }
            mpSetupDone_ = true;
            std::fprintf(stderr, spec ? "spectating -- replaying to catch up...\n"
                                      : "rejoined -- replaying to catch up...\n");
            mpStep();   // drain the replay this frame
        } else if (mp_->starting() && !mpSetupDone_) {
            startMpGame(mp_->startRoom(), mp_->startSeed());
            if (!mp_->isSpectator() && !showInitialCampaignBriefing(mp_->startRoom().mission)) return false;
            if (mp_->isSpectator()) {
                // Host-spectator (create-as-spectator): watch-only, no fog, no slot,
                // no resume ticket, and nothing to report loaded.
                beginSpectating();
            } else {
                mp_->reportLoaded(gameDataHash_);
                writeResume(mp_->gameId(), mp_->resumeToken());   // reconnect ticket
            }
            mpSetupDone_ = true;
        } else if (st == S::InGame) {
#ifndef NDEBUG
            autoplayStep();   // TAK_AUTOPLAY: headless humans that actually give orders
            // Exercise defeat through the ordinary synchronized Ctrl+D command.
            if (tak::devFlag("TAK_DEFEAT_TEST") && !debugDefeatSent_ && netTick_ >= 60) {
                for (const auto* u : front().live) {
                    if (u->player != localPlayer_ || !u->type || !u->type->commander) continue;
                    tak::net::Command c; c.kind = tak::net::Cmd::Destroy; c.unitId = u->id;
                    issue(c); debugDefeatSent_ = true; break;
                }
            }
#endif
            return mpStep();
        }
        return true;
    }

    const std::vector<uint8_t>* GameView::kingdomPalette(const std::string& kingdom) {
        if (kingdom.empty()) return nullptr;
        auto it = kingdomPals_.find(kingdom);
        if (it == kingdomPals_.end()) {
            std::vector<uint8_t> rgba;
            try {
                auto pal = tak::gaf::Palette::fromBytes(vfs_.read("palettes/" + kingdom + ".pcx"),
                                                        kingdom + ".pcx");
                rgba.assign(&pal.rgba[0][0], &pal.rgba[0][0] + 256 * 4);
            } catch (...) { rgba.clear(); }
            it = kingdomPals_.emplace(kingdom, std::move(rgba)).first;
        }
        return it->second.empty() ? nullptr : &it->second;
    }

    void GameView::clampMapScroll() {
        mapScroll_ = std::clamp(mapScroll_, 0, std::max(0, mapTotalRows_ - mapVisRows_));
    }
