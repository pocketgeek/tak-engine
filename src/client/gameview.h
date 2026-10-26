#pragma once
#include "client/areadrag.h"
#include "client/emotetiming.h"
#include "client/retailmodellighting.h"

// GameView -- the in-world game client: rendering, input, the retail HUD, fog,
// minimap, the COB animation VM host, benchmark, lobby, and the
// server/replay glue. Extracted from client/main.cpp (which keeps only the app
// shell: main(), the front-end loop, and local-server helpers). Client-only and
// NOT part of the hashed sim. Kept at global scope so main() references it
// unqualified. Method bodies are being split out into gameview_*.cpp.

// Must precede SDL.h: on Windows this pulls in winsock2 (with WIN32_LEAN_AND_MEAN)
// before SDL's <windows.h> would otherwise pull the incompatible winsock v1.
#include "net/netcompat.h"
#include "client/retailsmoke.h"
#include "client/weaponanimationqueue.h"
#include "client/retailpointparticle.h"
#include "client/retailglow.h"
#include "client/retaildebris.h"
#include "client/retaildebrismodel.h"
#include "client/retailblood.h"
#include "sim/retaileffectclock.h"
#include "sim/retailrng.h"

#include "client/briefingscreen.h"
#include "client/resultscreen.h"
#include "client/loadscreen.h"
#include "cob/vm.h"
#include "crt/crt.h"
#include "gaf/gaf.h"
#include "gui/gui.h"
#include "hpi/hpi.h"
#include "net/client.h"
#include "client/replayfile.h"   // ReplayCheckTracker
#include "net/replaywriter.h"    // the replay recording, streamed as the game is played
#include "client/situation.h"    // TAK_SITUATION (debug builds)
#include "client/postrail.h"      // TAK_POSTRAIL (debug builds)
#include "client/pacelog.h"       // TAK_PACELOG (debug builds)
#include <future>
#include "util/procmetrics.h"   // benchmark: cross-platform CPU/RSS sampling
#include "net/lockstep.h"
#include "net/orderpack.h"
#include "ai/ai.h"          // Difficulty <-> aiLevel + incomeMultFor (header-only helpers)
#include "sim/matchsetup.h"
#include "sim/scenario.h"
#include "sim/scenarioinfo.h"
#include "sim/sim.h"
#include "tdf/tdf.h"
#include "tdo/tdo.h"
#include "terrain/terrain.h"
#include "tnt/tnt.h"
#include "tnt/mapgen.h"
#include "util/png.h"
#include "version.h"
#include "client/cursors.h"
#include "client/selportraits.h"   // info-bar multi-selection strip
#include "client/dirpicker.h"   // first-run data-dir folder picker
#include "client/distantmodels.h"
#include "client/tacticaldots.h"   // zoomed-out unit dots (Options > Graphics)
#include "client/featureindex.h"
#include "client/unitindex.h"
#include "client/unittypetable.h"
#include "client/font.h"      // GAF bitmap font (extracted leaf class)
#include "client/uifont.h"    // the embedded TrueType face (chat box, MODERN UI FONT)
#include "client/uitext.h"    // plain interface text in the chosen UI FONT
#include "client/mapview.h"   // terrain pan/zoom + async chunk compositor (extracted leaf)
#include "client/modelpiece.h"
#include "client/modelmath.h"   // Tri/Xform/scriptRot (shared by GameView + model viewer)
#include "client/projectilemodelscale.h"
#include "client/modelview.h"   // standalone 3DO model viewer (extracted leaf)
#include "client/renderframe.h"   // UnitR/PlayerR/Frame render snapshot (extracted leaf)
#include "client/rendertelemetry.h" // per-frame render stats + generation vocabulary
#include "client/animatedatlas.h"   // per-atlas animated-region tracking
#include "client/geometrystages.h"    // staged unit geometry: local / placement / material (workstream D)
#include "client/geometryowners.h"    // stable per-unit geometry entries (workstream C)
#include "client/shadowtiles.h"       // shadow silhouette tile keys and reuse (workstream E)
#include "client/palettesheets.h"     // structure palette sheet layout + indexed form (workstream M)
#include "client/paletteindexed.h"    // indexed palette program on the OpenGL backend (workstream M)
#include "client/nativepass.h"        // explicitly owned native passes (workstream N)
#include "client/shadowatlas.h"       // persistent shadow tile cells (workstream F)
#include "client/cpubudget.h"         // retained CPU buffer trimming (workstream J)
#include "client/ordertrail.h"
#include "client/retailaim.h"
#include "client/retailflightanimation.h"
#include "client/retaileffectvisibility.h"
#include "client/replayfile.h"   // .takrep parser (extracted leaf)
#include "client/monarchalert.h"
#include "client/sound.h"     // WAV mixer + music + soundclasses (extracted leaf)
#include "client/threadpool.h"   // data-parallel worker pool (extracted leaf)
#include "client/workschedule.h"   // estimated-cost plans for the pool passes (workstream I)
#include "client/gpuvram.h"   // GPU memory accountant + safety-net cap (Options limit)
#include "client/hotkeys.h"
#include "client/hotkeysscreen.h"
#include "client/options.h"
#include "client/voice.h"
#include "client/voiceptt.h"
#include "client/settings.h"
#include "client/artscale.h"
#include "client/dev.h"
#include "client/appquit.h"
#include "client/mainmenu.h"
#include "client/menumusic.h"

// Keep our own main() on every platform (don't let SDL redefine it to SDL_main /
// pull in SDL2main + a WinMain); we call SDL_SetMainReady() in main() instead. This
// also keeps takclient usable as a console/headless tool on Windows. The build also
// defines this target-wide (CMake) so it holds even when a header pulls in <SDL.h>
// before this point; the guard avoids a redefinition warning.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <cassert>
#include <fstream>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <time.h>   // clock_gettime(CLOCK_THREAD_CPUTIME_ID): TAK_PACELOG worker CPU time
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "client/shadowopaque.h"
#include "client/geometrysubmit.h"
#include "client/vertexarena.h"
#include "client/fogupload.h"
#include "client/shadowmask.h"
#include "client/appglobals.h"   // kProjY/kProjZ, kSortZ/kSortY
#include "client/hudcorner.h"    // the top-left overlays' shared column

namespace tak { class CrusadesScreen; }

class GameView {
public:
#ifndef NDEBUG
    struct FactionKit {
        const char* monarch;   // hero commander the faction starts with
        const char* keep;      // production building the monarch builds first
        const char* lode;
        const char* builder;
        const char* squad[4];
    };
    static const FactionKit& kit(const std::string& side);   // gameview_devharness.cpp
#endif

    // Player start positions from the current map's sibling .ota, in world pixels,
    // ordered StartPos1, StartPos2, …. Delegates to the shared sim helper so the
    // client and the server's referee derive identical positions.
    std::vector<std::pair<float, float>> parseStartPositions() const;

    // Join the sim worker + minimap crunch before members die, then free every
    // GPU texture we own: the SDL_Renderer outlives the session (menu -> game ->
    // menu loops reuse it), so anything not destroyed here leaks REAL VRAM and
    // gpuvram budget across sessions -- a few benchmark runs used to pin the
    // budget and starve terrain-chunk uploads (map stuck at the low-res underlay).
    // Interactive teardown: cancel, do not drain (see simCancel_).
    ~GameView();   // out of line (gameview_impl.cpp): crusadesScreen_ is forward-declared here
    // This client's replay of the game, streamed to disk WHILE it is played
    // (net/replaywriter.h) into the user's config directory, beside settings.ini.
    // Opened when the game is set up (startMpGame), topped up from mpStep, finished
    // when the session really ends: the destructor, or the next game's GameStarting.
    // The result does not end it -- play after the result (a spectator watching on,
    // a defeated player's last seconds) is part of the replay. Players and
    // spectators both record; replay playback and the Benchmark (not a game) never do.
    // A recording a crash or kill left unfinished is completed by
    // tak::net::recoverReplayParts at the next start (main.cpp, the replay picker).
    void beginGameRecording();
    void tryOpenReplayRecording(bool first);
    void pumpReplayRecording();
    void finishReplayRecording(bool ownLog = true);

    GameView(SDL_Renderer* ren, tak::hpi::Vfs vfs, const std::string& mapPath,
             const std::string& installRoot, tak::hpi::ModUse mods,
             bool demo, bool scenario,
             bool bare, const std::string& side = "ara", const std::string& aiSide = "tar",
             bool crusades = false);

    // True while the multiplayer lobby is showing (before the game world exists).
    // Play the menu theme (track15) while in the lobby / not yet in a game, and the
    // faction playlist once the match starts. Called every frame; only (re)starts the
    // playlist on a state change so it doesn't restart the current track.
    int musicMode_ = 0;   // 0 = none yet, 1 = lobby, 2 = in-game
    tak::EmoteTiming emoteTiming_[8];
    uint64_t emoteNowMs_ = 0; // captured once per rendered frame, safe for geometry workers
    bool discoWas_[8] = {};       // per-player disco state, to fire the track once on start
    bool headbangWas_[8] = {};    // ...and the headbang state

    // On the rising edge of a player's disco (Shift+D), play the 10s disco loop as a
    // positional SFX from each of that player's dancing monarchs (enemies only if in
    // view). It runs alongside the faction music -- a dance-floor track from the unit.
    void discoSound();

    // Same as discoSound but for the Shift+H headbang -> the heavy-metal track.
    void headbangSound();

    void manageMusic();

    bool inLobbyPhase() const;
    void setMpMapId(const std::string& id) { mpMapId_ = id; }
    void setScenarioTrace(tak::sim::ScenarioScript::TraceSink sink) {scenarioTrace_=std::move(sink);}
    void setMissionStem(const std::string& s) { missionStem_ = s; }
    // Single-player from the menu: it's a private local game, so open the lobby on
    // the Create screen (the browser is empty by design) and mark it single-player
    // (labels change, no password / no game browser).
    void setSinglePlayer() { lobbyScreen_ = LobbyScreen::Create; singlePlayer_ = true; }
    void prepareLobbyMaps(tak::LoadScreen& progress) { buildMapList(&progress); }
    void openCrusades(uint32_t territory = 0);
    uint32_t crusadesSelectedTerritory() const;
    std::string takeCrusadesReplayPath();
    void setCrusadesReplayError(const std::string& error);
    void setCampaignReplayReturn() { campaignReplayReturn_ = true; }
    bool reconnectRequested() const { return reconnectRequested_; }
    // Return-to-menu request: a lobby/in-game action sets this; main()'s outer loop
    // tears the session down and re-shows the front-end menu.
    bool menuRequested() const { return menuRequested_; }
    bool quitRequested() const { return quitRequested_; }   // in-game QUIT -> exit app
    // Menu-launched sessions: the front-end owns the lobby BGM (see manageMusic).
    void setExternalLobbyMusic() { externalLobbyMusic_ = true; }
    // Menu-launched sessions can return to the front-end, so the in-game menu
    // offers MAIN MENU; a direct CLI game only offers RESUME/QUIT.
    void setCanReturnToMenu() { canReturnToMenu_ = true; }

    // Apply local Options (audio / camera / UI scale) live -- at startup and
    // whenever the in-game Options screen changes a value. Never touches the sim.
    void applySettings(const tak::Settings& s);

    // main()'s live settings, so the in-game Options screen can edit + persist them.
    void setSettings(tak::Settings* s) { settings_ = s; }

    // Open the in-game Options overlay (from the Esc menu). onChange applies audio,
    // camera, UI scale and window state live; the host saves on close.
    void openOptions();
    // Open the hotkey-config overlay (from the Options screen's CONTROLS button).
    void openHotkeys();
    // Voice chat (client/voice.h): main()'s engine, or null (headless, benchmark, no
    // voice). pumpVoice() runs once per frame: it applies the settings, relays frames
    // between the engine and the server, and keeps push to talk honest.
    void setVoice(tak::VoiceEngine* v) { voice_ = v; }
    void pumpVoice();
    // The VOICE CHAT screen, in the same overlay as Options (Esc menu > VOICE CHAT).
    void openVoiceOptions();
    // The disconnect wait, the client's side (protocol 289): beginReconnect starts
    // reconnecting when a seated player's link drops mid-game (false: nothing to come
    // back to), reconnectStep drives it each frame (false: given up, netError_ says why).
    bool beginReconnect();
    bool reconnectStep();
    // A bundle's lifecycle event, display side (main thread): an AI takeover's notice
    // and HUD label. The sim applies the event itself.
    void noteEvent(const tak::net::Event& e);
    // The WAITING FOR <name> countdowns (MpClient::waiting) and, while reconnecting,
    // our own; above the HUD.
    void drawNetWaiting(int winW);
    // The map's player capacity = its start-position count (2..8). The server has
    // no map data, so a creating client tells it how many slots the map supports.
    // A room that plays its map's scenario (`mapScenario`: the room's MAP SCENARIO
    // option; a Test Map copy always does) also opens every seat the scenario needs.
    uint8_t mpCapacity(bool mapScenario = false) const;
    // MAP SCENARIO facts of an installed map (sim::inspectScenario with this client's
    // registry). A MapCache entry's companions live in its archive, not the plain VFS.
    tak::sim::ScenarioInfo mapScenarioInfo(const std::string& path) const;

    void input(const SDL_Event& e, int winW, int winH);

    // Contextual right-click order, extracted so the reclaim right-drag can defer to
    // it on a plain click: transport unload/load, assist/guard, attack, or move.
    void rightClickOrder(float wx, float wz, bool queue);
    // Retail's order line: beads strung between a selected unit's queued orders.
    void drawOrderTrails(int mvw, int winH);
    static constexpr int kTrailUnits = 3;    // retail beaded only its few "focus" units
    static constexpr int kTrailBeads = 96;   // per leg, so a map-long order can't run away
    struct OrderTrailCache {
        int unitId=0;
        uint64_t signature=0;
        uint64_t sceneSignature=0;
        uint32_t featGen=0,visGen=0,tick=0;
        bool noFog=false;
        std::vector<RenderOrder> orders;
    };
    std::array<OrderTrailCache,kTrailUnits> orderTrailCache_;
    std::vector<tak::OrderTrailTarget> orderTrailTargets_;
    std::vector<tak::OrderTrailSite> orderTrailSites_;
    uint64_t orderTrailSceneSignature_=0;
    void refreshOrderTrailTargets();

    // A mobile, reclaim-capable builder of ours is selected (drives the right-drag).
    bool haveReclaimer();
    std::vector<int> reclaimers();   // every selected one, in selection order
    // Right-drag "clear this area": order EVERY selected reclaimer to clear the box
    // (each its own ReclaimArea, retail 5214b0); non-reclaimers are left alone.
    void issueReclaimBox(float x0, float z0, float x1, float z1, bool queue);
    // Single-feature reclaim (right-click / armed CLEAR): every reclaimer, same target.
    void issueReclaimFeature(int targetId, bool queue);

    // Attach the multiplayer client. The game world is set up later, from the
    // server's GameStarting (startMpGame), not from the constructor.
    void setMpClient(tak::net::MpClient* mp);
    bool isNet() const { return mp_ != nullptr; }
    // Run the sim on its own worker thread (Stage B1c). On by default for interactive
    // games; the headless harness disables it (inline == deterministic) unless verifying.
    void setSimThreadMode(bool on) { simThreadMode_ = on; }
    // Benchmark run: an all-AI watch game on Ulasem Arena with the staged benchmark spawn
    // plan (see MatchConfig::benchmark). Drives the createGame/seat path in mpAutoStep.
    void setBenchmark(int level) { benchmarkLevel_ = level; benchmarkMode_ = level > 0; }
    bool benchmarkMode() const { return benchmarkMode_; }
    void setLocalServerPid(long pid) { localServerPid_ = pid; }   // owned local server; shared by HUD and benchmark metrics
    bool benchmarkStatsShown() const { return benchStatsShown_; }
    // Establish the t=0 baseline for the CPU% deltas (called once when the run starts).
    void benchmarkBaseline();
#ifndef NDEBUG
    // Dev-harness ctor branches (gameview_devharness.cpp).
    void setupPatrolPerf();
    void guiSmoothTest();
    void setupScenario();
    bool setupLocalMatch(bool demo, bool bare, const std::string& side,
                         const std::string& aiSide,
                         const std::vector<std::pair<float, float>>& starts,
                         float cx, float cz, float px, float pz, float ax, float az);
#endif
    // Record one metrics row for game-second `gameSec`: client/server CPU% (delta over the
    // wall interval since the last sample), RSS, fps and sim speed.
    void pushBenchSample(int gameSec);
    // Take a metrics sample each time the game clock crosses a 5s milestone; when it reaches
    // the benchmark end tick, flag the stats overlay. Called once per rendered frame.
    void benchmarkSample();
    // Esc during a run: end the benchmark now. If the sim has started, record a closing
    // sample at the current second and show the stats screen; otherwise bail to the menu.
    void stopBenchmark();
    // Benchmark flythrough: each 10s leg tracks one AI's monarch (leg 0 -> AI1 .. leg 7 ->
    // AI8), starting fully zoomed out and zooming in across the leg, then cutting to the
    // next. Camera only -- view state is never hashed. Called each frame during the run.
    void benchmarkCamera(float dt, int winW, int winH);
    // The benchmark results overlay: a per-milestone table of client/server CPU + memory,
    // fps and sim speed, plus the display settings that produced them. A single centered
    // panel, scaled up on big displays. DONE / Esc / click -> main menu.
    void renderBenchmarkStats(int winW, int winH);
    // Flush + join the sim worker (drains any pending simInbox_ ticks first). For the headless
    // harness to call before reading the final world hash, so it reflects every processed tick.
    // Drains: the headless harness needs the final world hash to reflect every
    // tick it pushed, so this one waits for the backlog on purpose.
    void shutdownSim() { stopSimThread(/*drain=*/true); }
    // Dev --keytest helper: pick an own unit (builder preferred) + live count from the render
    // SNAPSHOT, so the harness never iterates live world_.units() while the worker ticks.
    std::pair<int, size_t> keytestPickOwnUnit() const;

    // Route a command: offline it applies immediately; in a net game it is queued
    // for the server, which stamps ownership and sequences it into a tick bundle.
    // Inside a ClickScope it is held until the scope closes (see below).
    void issue(tak::net::Command c);
    // One player click (protocol 267): the commands issued while a ClickScope is
    // open are one click. When the outermost scope closes, a Legion net game packs
    // them (net/orderpack.h) into explicit selection orders -- one order per
    // selection verb, applied to every member in one tick -- and everything else
    // (Retail, offline) gets them exactly as before, one command per unit. Scopes
    // nest; only the outermost flushes. setClickMove declares the right-click move
    // (its Move run is sent as OrderShape::Offsets at click (wx, wz)).
    struct ClickBuffer {
        std::vector<tak::net::Command> cmds;
        tak::net::ClickShape shape;
    };
    class ClickScope {
    public:
        explicit ClickScope(GameView& v) : v_(v), owner_(!v.click_) { if (owner_) v_.click_.emplace(); }
        ~ClickScope() { if (owner_) v_.flushClick(); }
        ClickScope(const ClickScope&) = delete;
        ClickScope& operator=(const ClickScope&) = delete;
    private:
        GameView& v_;
        bool owner_;
    };
    void setClickMove(float wx, float wz) {
        if (click_) { click_->shape.moveOffsets = true; click_->shape.x = wx; click_->shape.z = wz; }
    }
    void flushClick();

    // Set up the world for a multiplayer match from the server's final slot
    // table: one player per used slot (sim player index == slot), teams/colours
    // per slot, a monarch spawned at a start position each, seeded starting mana.
    void startMpGame(const tak::net::RoomView& room, uint32_t seed);
    void beginSpectating();   // watch-only: noFog_, no control, scoreboard on
    bool showInitialCampaignBriefing(const std::string& stem);
    bool consumeBriefingPause() { const bool value=briefingPaused_;briefingPaused_=false;return value; }

    // One networked frame: pump the connection, send this frame's local orders,
    // and simulate every tick the server has delivered a bundle for. Returns
    // false when the game/connection ends (see netError()).
    bool mpStep();

    // ---- replay playback (.takrep) ----------------------------------------
    // Build the world from a recorded match config and feed it the bundle log.
    // `mission` non-empty replays a CAMPAIGN recording: the world is rebuilt with
    // setupMission (placements + script), not as a skirmish on the same map.
    void startReplay(tak::sim::MatchConfig cfg,
                     std::vector<tak::net::Bundle> bundles,
                     const std::string& mission = {});
    bool replayMode() const { return replayMode_; }
    // (tick, hash) checkpoints from the ORIGINAL game. replayStep compares each one
    // as it passes it, so playback can say WHERE it stopped matching what happened
    // rather than merely running to the end and looking plausible.
    void setReplayChecks(std::vector<tak::net::ReplayCheck> c) { replayChecks_ = std::move(c); }
    bool replayDiverged() const { return replayTracker_.stateDiverged(); }
    std::string replayDivergenceSummary() const { return replayTracker_.summary(); }
    // Advance playback by `dt` (real seconds), scaled by the game-speed control;
    // Pause freezes it. Applies each recorded bundle then ticks the world.
    void replayStep(float dt);
    // Advance playback only. replayStep wraps this and ALWAYS runs cosmeticStep after
    // it, including once playback has finished -- see the note there.
    void replayAdvance(float dt);
    size_t replayTick() const { return replayTick_; }
    size_t replayLength() const { return replayBundles_.size(); }

    uint64_t worldHashPublic() const { return world_.stateHash(); }
    // Headless harness telemetry (observation only): Legion's counters.
    const tak::sim::LegionNavigator* legionPublic() { return writableWorld().legionNavigator(); }
    int missionOutcomePublic() const { return world_.missionOutcome(); }
    // Did this game resolve, and how? +1 win, -1 loss, 0 still running. Unlike
    // missionOutcomePublic this also covers a skirmish/MP last-team-standing result.
    int outcomePublic() const { return outcome_; }
    // The hash this client REPORTS to the referee. A spectator's is never checked --
    // Server::checkHashes returns immediately when no human slot is seated -- so it
    // sends a cheap 0 instead of folding thousands of units into an FNV every period.
    //
    // TAK_FAKE_DESYNC=TICK is fault injection: from that tick on, this client reports
    // a deliberately WRONG hash. It exists so a desync hunt can prove its own detector
    // FIRES. A sweep that has never caught a planted divergence has not been shown
    // capable of catching a real one -- which is exactly how a 32-run sweep of
    // spectator-only games came back "clean" while comparing nothing at all.
    uint64_t reportedHash(bool spectator, uint32_t tick) {
        if (spectator) return 0;
#ifndef NDEBUG
        PaceSpan span(paceHashNs_);
#endif
        uint64_t h = world_.stateHash();
        if (fakeDesyncTick_ && tick >= fakeDesyncTick_) h ^= 0x9e3779b97f4a7c15ull;
        return h;
    }
    // The end-of-game statistics table, in slot order. Read from the render frame
    // (not live world_), so it is safe to call after the sim thread has stopped.
    tak::ResultStats resultStats() const;
    size_t aliveUnits() const;
    // Units in the PUBLISHED render snapshot. Not the same question as aliveUnits(),
    // which reads the world: this is what the renderer would actually draw, and it is
    // zero when a mode simulates without publishing -- which is exactly how replay
    // playback managed to show an empty map (replayStep never called captureFrame).
    size_t framedUnits() const { return front().live.size(); }
    uint32_t framedTick() const { return front().gameTick; }
    size_t framedAliveUnits() const {
        const auto& f=front();
        size_t count=0;
        for (int p=0;p<f.numPlayers && p<int(f.players.size());++p)
            count+=size_t(std::max(0,f.players[size_t(p)].unitCount));
        return count;
    }

    // Drive one iteration of the multiplayer lobby + game. autoMode: 0 = don't
    // auto-drive the lobby (a real UI will), 1 = auto-host (create + start at 2+
    // ready), 2 = auto-join the first game. Returns false when the session ends.
    // (M3 uses the auto modes; the interactive lobby UI is follow-on work.)
    // AI difficulty for the headless / auto seat paths: TAK_AI_LEVEL (0-6), default
    // Normal. TAK_AI_LEVELS ("1,2,...") gives the n-th seated AI (`seat`, from 0) its
    // own level; seats past the list's end take TAK_AI_LEVEL. The interactive lobby
    // sets it per-slot via the Room UI instead.
    static uint8_t aiLevelEnv(int seat = 0) {
        const char* e = tak::devEnv("TAK_AI_LEVEL");
        int v = e ? std::atoi(e) : 2;   // default normal (0=passive..4=absurd, 5=absurdly defensive, 6=classic, shown as RETAIL)
        if (const char* list = tak::devEnv("TAK_AI_LEVELS")) {
            for (int k = 0; *list; ++k) {
                if (k == seat) { v = std::atoi(list); break; }
                const char* comma = std::strchr(list, ',');
                if (!comma) break;
                list = comma + 1;
            }
        }
        return uint8_t(std::clamp(v, 0, int(tak::ai::kAiLevels) - 1));
    }

    bool mpAutoStep(int autoMode, const std::string& mapId, bool crusades);

    void applyEvent(const tak::net::Event& e) { tak::sim::applyEvent(writableWorld(), e); }

    // Apply one command (shared with the server's referee sim, so both mutate
    // the world identically).
    void apply(const tak::net::Command& c) { tak::sim::applyCommand(writableWorld(), registry_, c); }

    // One lockstep step: returns false while stalled waiting for the peer.
    uint32_t netTick() const { return netTick_; }
    // Headless harness only: never play past this tick, so a --time run's final hash is
    // taken at exactly that tick instead of wherever a burst of buffered bundles overshot to.
    void setNetStopTick(uint32_t t) { netStopTick_ = t; }
#ifndef NDEBUG
    // TAK_AUTOPLAY=N -- a headless human issues roughly N orders per 10 seconds of
    // game time, so a multi-client run exercises the COMMAND path instead of eight
    // players standing still. Without it a headless human seats itself and never
    // orders anything, so several clients issuing orders on the same tick -- the
    // ordinary case in a real match, and the one that interleaves in the server's
    // per-tick command buffer -- is never tested at all.
    //
    // LOCKSTEP IS PRESERVED, REPRODUCIBILITY IS NOT. An order is a COMMAND, relayed by
    // the server and applied on every peer from the same bundle, so every client in a
    // game agrees no matter that each chose its orders locally -- measured: two
    // order-issuing clients finished a game on the same tick with the same hash and no
    // referee complaint.
    //
    // But the same seed does NOT give the same hash twice. Scheduling off the sim tick
    // makes the client's DECISION deterministic; the server still assigns the tick a
    // command EXECUTES on from when its packet arrives (Server::closeTick drains
    // cmdQueue into whichever tick is open), and that is wall-clock dependent. Two runs
    // of the same seed came back c575c6d0a9b8600d and a383346ec1473201.
    //
    // That is correct for real play -- human input arrives when it arrives -- so it is
    // not a bug to fix here. It does mean an autoplay run is a CONSENSUS test, not a
    // golden-hash test: use it to prove clients agree with each other and the referee,
    // never to compare a hash against a previous run.
    void autoplayStep();
    // TAK_BUILDLINE_NET: shift-drag build lines in a server game (see gameview_net.cpp).
    void buildlineNetProbe();
    struct NetLine {const tak::sim::UnitType* type; std::vector<std::pair<float, float>> sites;};
    std::vector<NetLine> netLines_;
    uint32_t netLinesTick_ = 0;
    bool netLinesDone_ = false;
#endif
    void selectOnly(int id) { if (spectating_) return; selection_.clear(); selection_.push_back(id); }

#ifndef NDEBUG
    // Harness hook: select one of the local player's mobile units and hand it a
    // short queue of moves, so a screenshot run can photograph the order line
    // without having to synthesise shift+right-clicks at guessed coordinates.
    // Returns the unit id, or 0 if there was nothing to order.
    int debugQueueDemo();
    int debugSmokeDrawCount_ = 0;
    int debugPointDrawCount_ = 0;
    int debugGlowDrawCount_ = 0;
    int debugNimbusDrawCount_ = 0;
    int debugDamageFlameDrawCount_ = 0;
    int debugFeatureFlameDrawCount_ = 0;
    int debugFeatureFlameExpected_ = 2;
    int debugFeatureSmokeDrawCount_ = 0;
    int debugFlameDrawCount_ = 0; // render-thread screenshot admission only
#endif
    const std::string& netError() const { return netError_; }

    void setFollow(float zoom) { initialCamera_.reset(); follow_ = true; mapView_.setZoom(zoom); }

#ifndef NDEBUG
    // Local dev/test harness (gameview_devharness.cpp): debug builds only.
    void amphibDemo();

    void navyDemo();

    void creonDemo();

    void missionTest();

    void testBuild();

    std::string lodeUnit;
    void soundTest();
    void faceTest();
    void fireTest();
    void lodeTest();

    void guardTest();

    void marchTo(float dx, float dz);
#endif

    void lookAt(float x, float z);
    void startAtMonarch();
    void cancelInitialCamera() { initialCamera_.reset(); }

    // Smoothed render FPS for the F4 overlay (set from the main loop each frame).
    void setFps(float f) { fps_ = fps_ > 0 ? fps_ * 0.9f + f * 0.1f : f; }

    // Choose which player-colour variant a player's units render in.
    void setPlayerColor(int player, int slot);

    // Mouse edge scrolling: pan the camera while the cursor rests in the margin
    // at a window edge — including the very bottom of the screen (the HUD panel
    // never sits at the extreme edge, so this doesn't fight the build icons).
    void edgeScroll(float dt, float zm);

    // Real-time per-RENDER-frame work: camera and audio that must stay smooth
    // regardless of the deterministic sim. In a net game the sim (update()) only
    // advances when a server bundle arrives, so anything lag-sensitive that isn't
    // "game state" belongs here, not in update() -- panning, zoom-follow, edge
    // scroll, camera shake, and music keep running even while the sim is stalled
    // waiting on the network. Called every frame in SP and net alike.
    void cameraFrame(float dt);
    // The SIM-MUTATING half of a game step: the world tick plus every block that
    // writes world or outcome state (god summons, scenario spawns, the legacy
    // mission VM, dev harnesses, the victory check). In multiplayer catch-up this
    // runs for EVERY drained bundle -- it is what lockstep requires -- while the
    // cosmetic half below runs ONCE per frame afterwards.
    void simStep(float dt);

    // Snapshot every unit's post-tick pose (x/z/heading) so the render can GLIDE units
    // between the 30Hz sim ticks instead of stepping them (visible stutter above 30Hz).
    // prev <- last tick's curr, curr <- now; a large jump (teleport / id reuse / respawn)
    // reseeds so we don't zip across the map. Client-only, viewer-only -- never hashed.
    void captureFrame();
    void captureTransportEffects();
    void captureEffectVisibility();

    // Interpolated render pose for a unit: glides x/z (and shortest-path heading) between
    // the last two ticks by interpAlpha_ (0..1 through the current tick interval). Falls
    // back to live state when the option is off or there's no history. Viewer-only.
    void interpPose(const UnitR& s, float& x, float& z, float& heading) const;
    // Lookup helper: the render snapshot record for a unit id (empty default if absent).
    const UnitR& frameUnit(int id) const;
    // Pointer form matching world_.unit()'s semantics: nullptr unless the id is a unit that
    // is LIVE this tick (captured with this frame's gen). Use to replace world_.unit(id) in
    // render/HUD reads (the null check keeps working).
    const UnitR* frameUnitP(int id) const;
    std::optional<int> reclaimTargetAt(float wx,float wz) const;   // feature id, or -corpse id
    bool canAssistSite(const UnitR& builder,const UnitR& site) const;
    bool canLoadPassenger(const UnitR& passenger, const UnitR& carrier) const;
    // Player snapshot accessors (mirror world_.player()/numPlayers() for the HUD).
    const PlayerR& framePlayer(int p) const;
    int frameNumPlayers() const { return front().numPlayers; }
    // Fog snapshot accessors (mirror world_.cellVisible/visibility/visW/visGeneration).
    const std::vector<uint8_t>& frameVisibility() const { return front().vis; }
    int frameVisW() const { return front().visW; }
    int frameVisH() const { return front().visH; }
    uint32_t frameVisGeneration() const { return front().visGen; }
    bool cellVisibleR(float x, float z) const;
    bool effectVisibleR(const std::array<int32_t,3>& position) const;
    // The native unit sound host 50def0's sight test on the effect plane, which keeps
    // a dying local unit's sight for 60 ticks: display fog drops it at once, and
    // silenced the cry of a unit no other local unit was watching.
    bool unitSoundVisibleR(int player,bool hidden,const tak::sim::UnitType* type,
                           const std::array<int32_t,3>& position) const;
    std::unique_ptr<tak::RetailEffectVisibility> effectVisibility_;
    uint32_t effectVisibilityTick_=0;
    int effectVisibilityPlayer_=-1;

    bool canPickPoint(float x, float z) const { return noFog_ || cellVisibleR(x, z); }
    bool canPickUnit(const UnitR& u) const {
        return !u.embarked() && (alliedToLocal(u.player) || canPickPoint(u.x, u.z));
    }
    // Impacts are ONE-SHOT and the sim clears them every tick, while the render
    // only ever sees the newest published snapshot. Whenever the sim outruns the
    // renderer -- replay catch-up, or simply a frame rate under the 30 Hz tick --
    // whole snapshots are skipped and their impacts went with them: silently no
    // sound, no bolt, no effect for those hits. So the worker also appends them
    // here, and the render drains this instead, which survives skipped frames.
    // Bounded: a renderer that has fallen catastrophically behind drops the
    // OLDEST rather than growing without limit, since stale impacts are the ones
    // least worth drawing.
    static constexpr size_t kMaxPendingHits = 4096;
    std::mutex hitQueueMutex_;
    // Each impact carries its tick: cosmeticStep releases it once the DISPLAYED tick (front())
    // reaches it, so with the presentation clock (WE S4) an impact lands with the poses it
    // belongs to rather than up to lagTarget ticks early.
    struct QueuedHit { uint32_t tick = 0; tak::sim::World::HitFx hit; };
    std::deque<QueuedHit> hitQueue_;
    uint8_t pendingMonarchAlerts_ = 0; // guarded by hitQueueMutex_; survives queue overflow
    tak::MonarchAlert monarchAlert_;
    std::deque<tak::sim::World::SoundReq> missionSoundQueue_;
    // Death sounds the simulation's dying unit VMs played (World::ScriptSound), the name
    // resolved at capture. Tick-stamped and released with the displayed tick, like hits.
    struct QueuedUnitSound {
        uint32_t tick = 0;
        int unitId = 0, player = 0;
        float x = 0, z = 0;
        int32_t flags = 0;
        std::string name;
        std::array<int32_t,3> position{};   // 16.16 x, height, z for the sight test
        bool hidden = false;                // cloaked or carried
        const tak::sim::UnitType* type = nullptr;
    };
    std::deque<QueuedUnitSound> unitSoundQueue_; // guarded by hitQueueMutex_
    int frameWinningTeam() const { return front().winningTeam; }
    bool frameDiscoActive(int p) const { return p>=0 && p<8 && emoteTiming_[p].active(emoteNowMs_) && framePlayer(p).emoteDisco; }
    bool frameHeadbangActive(int p) const { return p>=0 && p<8 && emoteTiming_[p].active(emoteNowMs_) && !framePlayer(p).emoteDisco; }
    float emoteSeconds(int p) const { return p>=0 && p<8 ? emoteTiming_[p].seconds(emoteNowMs_) : 0.f; }
    // Count this type in the builder's snapshotted production queue.
    int frameQueuedCount(int builderId, const tak::sim::UnitType* type) const;

    // The DISPLAY half: impact sounds/effects, particles, animation state and the
    // COB VMs, timers, camera-follow. Runs once per rendered frame with the game
    // time actually covered -- during a rejoin/spectate catch-up this used to run
    // its full body up to 512x per frame (~18ms of pure pool dispatch alone).
    void cosmeticStep(float dt);

    // Advance every unit's animation VM ONCE PER RENDER FRAME (decoupled from the 30Hz sim
    // tick). Each VM is independent: it only reads sim state through onGet (no writes), so
    // ticking it off the main thread is safe; the EMIT_SFX/PLAY_SOUND it stashes is drained
    // here serially. Viewer-only -- never touches the sim hash. `realDt` is the wall-clock
    // frame time, scaled to the game's apparent speed so the walk cycle matches movement.
    void animFrame(float realDt);

    // How fast the world appears to advance (so the animation walk cycle stays in step
    // with movement): the server-set game speed in a net game, else the local multiplier.
    float animSpeed() const;

    void update(float dt);

    // Create textures (terrain chunks, minimap) before the render pass.
    void prepare(int winW, int winH);

    // Fetch and reset the per-draw sub-phase timers (for TAK_PROF).
    void takeProf(double& projMs, double& submitMs, double& shadowMs, double& simMs,
                  uint64_t& unitsDrawn, uint64_t& shadowVerts);
    // Current accumulators WITHOUT resetting, so a per-frame delta can be taken.
    void profPeek(double& projMs, double& submitMs, double& shadowMs) const {
        projMs = profProjMs_; submitMs = profSubmitMs_; shadowMs = profShadowMs_;
    }

    void draw(int winW, int winH);
    // The stream status banner's bottom edge (Streaming::bannerBottom; 0 with none), set
    // before each draw(): the top-left overlays start below it.
    void setStreamBannerBottom(float y) { streamBannerBottom_ = y; }
#ifndef NDEBUG
    uint64_t geometryChecks() const { return geometryChecks_.load(); }
    uint64_t animatedTextureChecks() const { return animatedTextureChecks_.load(); }
    // TAK_GEOMETRY_VERIFY (workstream D): partial stage updates compared with a
    // complete rebuild, by redone stage, and local builds compared with the former
    // single-pass transform.
    uint64_t placementChecks() const { return placementChecks_.load(); }
    uint64_t materialChecks() const { return materialChecks_.load(); }
    uint64_t monolithicChecks() const { return monolithicChecks_.load(); }
    uint64_t shadowSkipChecks() const { return shadowSkipChecks_.load(); }
    // TAK_DISTANT_VERIFY (workstream G): frames checked, bodies whose generation
    // was deferred behind a held distant image (each regenerated in a copy and
    // compared), deferred bodies generated after prepare (catch-up) or inside it.
    struct DistantVerifyTotals { uint64_t frames=0, deferred=0, checked=0, catchup=0, late=0; };
    const DistantVerifyTotals& distantVerifyTotals() const { return distantVerify_; }
    // TAK_UNIT_INDEX_VERIFY (workstream L): every pass that took its units from the
    // broad phase was re-run over all live units and accepted the same units in the
    // same order, or draw() threw. passes = checked passes (painter list + overlays);
    // live/candidates/accepted are summed over them.
    // overlays[k] = units drawn by overlay k (health bars, self-destruct, squad
    // markers) over the checked passes; ghosts = units that drew queued-build
    // ghosts (taken from Frame::buildGhostUnits).
    struct UnitIndexVerifyTotals { uint64_t frames=0, passes=0, live=0, candidates=0, accepted=0, overlays[3]{}, ghosts=0; };
    const UnitIndexVerifyTotals& unitIndexVerifyTotals() const { return unitIndexVerifyStats_; }
    // TAK_SCHEDULE_VERIFY (workstream I): every geometry pass built each visible
    // unit exactly once under a well-formed plan; the shadow point checks, shadow
    // tile transforms and body copies of the planned passes equal a serial re-run,
    // bit for bit.
    struct ScheduleVerifyTotals {
        uint64_t frames=0, geometryPasses=0, geometryBuilds=0, parallelPasses=0, checks=0,
                 prepPasses=0, prepPoints=0, copyPasses=0, copyVertices=0;
    };
    const ScheduleVerifyTotals& scheduleVerifyTotals() const { return scheduleVerify_; }
    // Workstream H (TAK_BODY_ARENA_VERIFY): every arena command gathered from a
    // CPU mirror of the arena through its ranges equalled the assembled command,
    // and every assembled command equalled its sources, packed without gaps.
    struct BodyArenaVerifyTotals { uint64_t frames=0, commands=0, vertices=0, retained=0, uploaded=0,
                                   appends=0, rebuilds=0, streams=0, fallbacks=0,
                                   rollovers=0, drains=0, oldCommands=0,
                                   assembled=0, assembledVertices=0;   // assembled commands checked
                                   uint64_t streamReasons[6]{};   // by tak::render::ArenaStreamReason
                                 };
    const BodyArenaVerifyTotals& bodyArenaVerifyTotals() const { return bodyArenaVerify_; }
    // Workstream J (TAK_CPU_TRIM_VERIFY): see CpuTrimVerifyTotals.
    const auto& cpuTrimVerifyTotals() const { return cpuTrimVerify_; }
    struct ShadowCacheVerifyTotals {
        uint64_t frames=0, tiles=0, reused=0, body=0, translated=0, original=0;
        // Workstream F: tiles reused in a cell kept from an earlier frame, cells
        // allocated, full repacks, migrated tiles, cells freed under pressure, page
        // releases and shrinks, allocator consistency checks, page textures created,
        // frames whose tiles did not fit (whole-frame direct fallback).
        uint64_t kept=0, allocs=0, compactions=0, migrated=0, evicted=0, pageReleases=0,
                 shrinks=0, validated=0, pageAllocs=0, failures=0;
    };
    const ShadowCacheVerifyTotals& shadowVerifyTotals() const { return shadowVerify_; }
    uint64_t ownershipChecks() const { return ownershipChecks_; }
    const tak::render::EntryPoolStats& ownershipTotals() const { return ownershipTotals_; }
    // Debug harness: the world-reset epoch bump of a rejoin, without the reset.
    void renderStudyBumpWorldEpoch() { worldEpoch_.fetch_add(1, std::memory_order_release); }
    // Render study (workstream M): toggle Texture Filtering Off <-> Units (the palette
    // form follows: indexed for point-sampled units, baked for bilinear ones).
    void renderStudyToggleFilter() { setTextureFilter(textureFilter_ ? 0 : 1); }
    // Render study (workstream L): a camera shake of the given magnitude (shakemagnitude
    // units) and duration, the path a weapon's shake request takes.
    void renderStudyShake(float magnitude, float seconds) { screenShake_ = true; triggerShake(magnitude, seconds); }
    void renderStudyMotion(unsigned frame) {
        if(!frame || frame%240)return;
        const float direction=(frame/240)%2 ? -1.f : 1.f;
        for(const auto& unit:world_.units())
            if(unit.alive() && unit.type && unit.type->canMove)
                writableWorld().order(unit.id,unit.x.toFloat()+direction*320,unit.z.toFloat()+direction*160,false);
    }
    void renderStudyZoom(float zoom) { mapView_.setZoom(zoom); }
    // Workstream E: the sinusoidal pan rounded to whole world units around the
    // setup view, so at zoom 1 every camera step is a whole-pixel translation
    // (exact shadow-tile translation reuse); `hold` > 1 alternates `hold` moving
    // and `hold` still frames, and every other such cycle runs 0.375 px off the
    // whole-pixel grid (one sub-pixel jump in and out: tiles must be redrawn).
    // And a periodic veterancy cycle on every fifth unit (a material-only change:
    // gold tint, no model swap).
    void renderStudyPanSnap(unsigned frame, unsigned hold) {
        if(!panSnapBase_)panSnapBase_={mapView_.offX(),mapView_.offY()};
        const unsigned moved=hold>1 ? frame/(2*hold)*hold+std::min(frame%(2*hold),hold) : frame;
        const float phase=float(moved)*.012f;
        const float fraction=hold>1 && (frame/(2*hold))%2 ? 0.375f : 0.0f;
        mapView_.setOffset(panSnapBase_->first+std::round(240*std::sin(phase))+fraction,
                           panSnapBase_->second+std::round(160*std::cos(phase))+fraction);
    }
    void renderStudyVeteran(unsigned frame, unsigned period);
    // Render-optimization harness (docs/render-optimization-progress.md). Each
    // is presentation-side or local-harness only and runs in the standalone
    // render study, never in a networked game.
    void renderStudyChurn(unsigned frame, unsigned period);
    std::optional<std::pair<float,float>> panSnapBase_;
    void renderStudyOffset(float x, float z) { mapView_.setOffset(x, z); }
    // The world view at this output size (left of the command panel, above the
    // bottom bar): the region a render capture comparison should cover.
    std::pair<int,int> renderStudyWorldView(int w, int h) const { return {mapViewW(w), std::max(1, h - barH())}; }
    // The zoom actually in force (the map-fills-window clamp can raise a request).
    float renderStudyZoomNow() const { return mapView_.zoom(); }
    void renderWork(size_t& vertices,size_t& calls,size_t& images,size_t& bakes,size_t& bakeVertices,
                    size_t& bakeCalls,size_t& switches,size_t& imageBytes) const {
        vertices=bodyVertexTotal_;calls=0;
        for(const auto& op:drawOps_)calls+=!op.f && !op.u && op.tex && op.count>0;
        images=distantModelCache_.used;bakes=distantModelCache_.refreshed;
        bakeVertices=distantModelCache_.bakeVertices;bakeCalls=distantModelCache_.bakeDraws;
        switches=distantModelCache_.targetSwitches;imageBytes=distantModelCache_.bytes();
    }
#endif

    void advance(float seconds);

    void setTrace(bool on) { trace_ = on; sounds_.setVerbose(on); }

    // Smallest window that still fits the widest build-icon row at full size
    // (icons are 60px on a 66px pitch, centred over the map viewport with a small
    // margin, beside the fixed right-hand panel). Enforced in main() so the icons
    // never have to shrink and the last builder tier is never clipped off.
    int minWindowWidth() const;

    // ---- Animated mouse cursor (retail anims/cursors.gaf) --------------------------
    // Draw the retail cursor on top of everything at native resolution. main() calls
    // this after the (optional) AA downscale, right before present, so it is crisp and
    // unambiguously topmost. Lazily loads the cursor art and hides the OS arrow once.
    // We never restore the OS arrow on teardown: the menu and the game both hide it and
    // draw their own, so restoring it only makes the arrow flash during the next screen's
    // (slow) load; the desktop cursor returns on its own when the window is destroyed.
    // `uiOverlay`: a plain arrow for a panel drawn over the game (the streaming setup).
    void drawCursorOverlay(bool uiOverlay = false);
    // HARDWARE CURSOR is on and the platform took our cursor: the OS draws the pointer
    // (and a stream gets it stamped in by its worker), so nothing is drawn into frames.
    bool hardwareCursorInUse() const;
    void setAudioTap(void* context, void (*tap)(void*, const int16_t*, int, int)) {
        sounds_.setAudioTap(context, tap);
    }
    void suspendAudioOutput() { sounds_.releaseOutput(); }
    void resumeAudioOutput() { sounds_.reopenDevice(); }

private:
    tak::sim::ScenarioScript::TraceSink scenarioTrace_;
    // Which cursor to show this frame, from the current UI/order state and what is under
    // the pointer -- the retail two-level scheme (an armed order beats plain hover).
    // `fightTint` is set when the cursor is the fight-move ('f') Attack glyph, which the
    // caller draws tinted so it reads apart from a real attack order.
    tak::CursorId desiredCursor(bool& fightTint);

    // Plain-hover cursor: classify what is under the world point, mirroring the priority
    // in rightClickOrder() so the pointer previews the order a right-click would issue.
    tak::CursorId hoverCursor(float wx, float wz, bool ignoreAirstrikeWeapons = false);
    bool isCursorAttacker(const UnitR& u) const;
    bool attackerCanHitEnemy(const UnitR& attacker, const UnitR& target,
                             bool ignoreAirstrikeWeapons = false) const;
    // Screen-space sprite hit test: the unit's projected model bounds at its
    // DRAWN position (terrain lift + flyer altitude), floored for tiny units --
    // the same region click-select uses, so the hover cursor and a click always
    // agree. Optional out: squared distance to the sprite centre (tie-breaks).
    bool unitUnderCursor(const UnitR& u, float mx, float my, float* d2 = nullptr);

    // Everything about a model piece that never changes: whether it draws at all,
    // and its primitives' texture names folded to lower case. collect() used to
    // work this out per piece and per primitive on EVERY unit EVERY frame -- two
    // std::string allocations and a case fold each -- and then again for the
    // shadow pass. At ~1500 units that is six figures of throwaway string work a
    // frame for answers that were fixed when the model loaded.
    struct ShadowMaskFrame {
        SDL_Texture* texture=nullptr;
        SDL_FRect uv{0,0,1,1};
    };
    struct PieceMeta {
        bool animated = false;              // any animated texture in this subtree
        std::vector<const size_t*> animationFrames; // stable texture-animation records
        // Parallel to animationFrames: 1 when a primitive textured by that animation
        // has shadow masks, so its frame selects the shadow cutout. 0 = the frame
        // only recolours the body (tak::render::shadowInputsEqual).
        std::vector<uint8_t> animationFrameMasks;
        bool skip = false;                  // ground plate / *off duplicate: draws nothing
        std::vector<std::string> primTex;   // lowercased per primitive ("" = untextured)
        std::vector<const SDL_Rect*> primAtlas; // immutable atlas layout; shared by all colour slots
        std::vector<const std::vector<ShadowMaskFrame>*> primShadowMasks;
        std::vector<bool> primAnimated;
        std::vector<PieceMeta> children;    // 1:1 with Object::children
    };
    // The rules themselves, in ONE place, used both to precompute the tree and to
    // answer for a model that has no cached tree (ghosts, portraits). A second copy
    // of these predicates is how the cache and the live path would silently drift.
    static void pieceMetaFor(const tak::tdo::Object& o, bool isRoot, PieceMeta& m) {
        std::string oname = o.name;
        std::transform(oname.begin(), oname.end(), oname.begin(), ::tolower);
        // Same helper-piece policy for live rendering and editor previews.
        m.skip=skipLiveModelPiece(oname,isRoot);
        m.primTex.clear();
        m.primTex.reserve(o.primitives.size());
        for (const auto& p : o.primitives) {
            std::string n = p.texture;
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            m.primTex.push_back(std::move(n));
        }
    }
    // Precompute the whole tree for a model (once, at registration).
    void buildPieceMeta(const tak::tdo::Object& o, PieceMeta& m, bool isRoot = true);

    // Per-worker scratch: evaluate each piece and unique vertex once, then
    // share the exact rotated coordinates between the body and shadow passes.
    struct PreparedPiece {
        Xform transform;
        bool hidden=false, castsShadow=true, shaded=true;
        std::vector<std::array<float,3>> rotated;
        std::vector<PreparedPiece> children;
    };
    struct Anim;
    void preparePiece(const tak::tdo::Object& object,const PieceMeta& meta,
                      const Xform& parent,const Anim* anim,float cy,float sy,
                      PreparedPiece& out);
    // collect()'s per-primitive transformed vertices, and the shadow collector's.
    struct CollectVertex { SDL_FPoint pos; float px, py, pz, d; bool ok; };
    struct ShadowCollectVertex { SDL_FPoint position; float x, y, z; bool valid; };
    // Scratch of one participant of the render pool (workstream J; thread_locals
    // before): slot 0 belongs to the render thread (the pool's only caller), slot k
    // to pool worker k, which touches it only inside a chunk. Between passes the
    // render thread owns every slot (ThreadPool::participant), so it accounts and
    // trims them without racing a worker. Sized by the largest model a participant
    // built, not by the army.
    struct WorkerScratch {
        std::vector<Tri> tris;                // collector output (body, silhouette)
        std::vector<size_t> order;            // depth-sorted triangle indices
        PreparedPiece prepared;               // piece transforms shared by body and shadow
        std::vector<uint64_t> key;            // the local stage key being compared
        std::vector<std::pair<SDL_Texture*, int>> previousRuns;
        std::vector<ShadowCollectVertex> shadowVerts;
        std::vector<CollectVertex> collectVerts;
        size_t peak = 0;                      // largest use since the last fold (bytes)
        size_t usedBytes() const;
        size_t capacityBytes() const;
        void note() { peak = std::max(peak, usedBytes()); }
        void release();                       // drop every capacity (owner only)
    };
    WorkerScratch& workerScratch();

    struct Visual {
        tak::tdo::Model model;
        PieceMeta meta;   // precomputed once; see collect()
    };
    // THE ONLY WAY TO CREATE A visuals_ ENTRY. An entry whose PieceMeta was left
    // default-constructed reports skip=false for every piece, and skip is what hides
    // the flat ground plate every TAK model carries at its root (AraGP, zonnull,
    // targround...). Draw a unit through such an entry and that plate is emitted --
    // invisible in the body pass, where it lies flat on the terrain it coincides with,
    // and a solid dark rectangle in the SHADOW pass, which composites it with MOD.
    //
    // That shipped: the build-icon path created the entry first via ghostModel, and
    // registerUnit's "if (!visuals_.count(typeId))" then skipped building the tree for
    // good. Any unit whose icon was drawn before its first instance spawned wore a
    // square shadow -- reported on the Beast Handler, which sits in the Zhon conjure
    // menu. Routing every site through here is what stops a fourth one being added.
    tak::tdo::Model* loadVisual(const std::string& key, bool rootIsGround = true) {
        auto it = visuals_.find(key);
        if (it != visuals_.end()) return &it->second.model;
        Visual v{tak::tdo::load(vread("objects3d/" + key + ".3do")), {}};
        // Layout is built once and survives GPU target recreation. Rect pointers
        // remain valid for this view's lifetime, including unordered-map rehashes.
        if (!atlasLaidOut_) buildAtlasLayout();
        buildPieceMeta(v.model.root, v.meta, rootIsGround);   // fixed for the model's life
        return &visuals_.emplace(key, std::move(v)).first->second.model;
    }
    struct EffectAnim;   // defined below; Anim only needs the pointer type
    struct Anim {
        std::unique_ptr<tak::cob::Vm> vm;
        // Points at the shared per-TYPE CobCache.pieceNames (node-stable in cobCache_,
        // which outlives every Anim), not a per-unit copy -- ~25 MB saved at 38k units.
        const std::vector<std::string>* pieceNames = nullptr;
        bool dying = false;
        bool ownerVmStopRequested = false; // native SET26 removal / SET31 owner timer
        bool producing = false;
        bool building = false;   // mobile builder actively working a site (conjure anim)
        bool firing = false;
        bool flying = false;
        tak::RetailFlightAnimationState flightAnimation;
        uint32_t occupancy = 0;  // last native setSFXoccupy notification
        uint32_t moveRate = 0;   // last native speed-tier notification
        float altitude = 0;      // captured flight height above groundY (body/shadow/effects)
        // Split the captured absolute flight height into datum + altitude for
        // projection and shadows. Vertical movement belongs to the sim controller.
        float groundY = 0;
        bool groundInit = false;
        int flyGate = 8;         // static index that this unit's `fly` gates on
        bool hasAim = false;     // has an AimWeapon script (turret/torso target tracking)
        bool hasFlinch = false;  // has a HitByWeapon script (damage flinch)
        bool hasWind = false;    // has a WindChange script (flags/sails)
        bool hasFlightSM = false;// has BeginFlight/BeginLanding: drive the drake's VTOL
                                 // state machine (Create ambients + statics) instead of
                                 // the generic reset()+fly/land flyer path
        bool hasActivate = false;// onOffable + has Activate: watch u.active for door swing
        bool active = true;      // last active/door-open state (onoffable/gate swing edge)
        bool cloakOn = false;    // last requested cloak mode (not temporary invisibility)
        bool hasCloakAnim = false;   // COB defines StartCloaking (araspy, npcheket)
        bool hasTurnDir = false; // has a TurnDirection script (rudder/sail trim,
                                 // turn-in-place lean); 116 of 187 unit COBs define it
        int turnSign = 0;        // last turn-direction sign passed to TurnDirection
        uint32_t windStamp = 0;  // last windGen_ this unit received (0 = never)
        // emit-sfx (piece, sfxType) captured off the worker thread; drained on the
        // main thread after the parallel VM tick (SDL/effects_ are main-thread only).
        std::vector<std::pair<int, int32_t>> pendingSfx;
        // The sim VM stops on death, but the display VM still runs Killed/Dying.
        // These captured damage-flame callbacks need the owner position and
        // retirement tick because their native effect lists are unit-attached.
        std::vector<tak::sim::World::ScriptEmission> pendingPoints;
        std::vector<std::pair<int32_t,int32_t>> pendingSnd; // name index + flags, drained on main
        std::span<const uint8_t> explosionReachability; // shared per-script control-flow map
        std::span<const tak::cob::PieceState> capturedPose; // transient render/effect snapshot
        tak::cob::Vm* effectQueryVm=nullptr;
        int workId = 0;           // site/target the build anim last fired for (one-shot per job)
        // Continuous ambient fire/smoke approximation: draw one looping effect of
        // each kind, kept alive while the emit-loop re-fires
        // (fireT/smokeT = seconds since the last emit of each). Smooth, not per-emit.
        const EffectAnim* fireFx = nullptr;
        const EffectAnim* smokeFx = nullptr;
        float fireT = 1e9f, smokeT = 1e9f;
        int firePiece = -1, smokePiece = -1;   // follow the animated emitting pieces
    };

    // Refresh a cosmetic flame/smoke effect attached to its animated COB piece.
    // Effect classes and lifetimes still need a full native particle comparison.
    void emitSfx(const UnitR& u, Anim& a, int piece, int32_t sfx);
    // A unit script's PLAY_SOUND through the native unit sound host 50def0's gate.
    void playUnitSound(const std::string& name, int32_t flags, float x, float z, bool visible, bool selected);
    void emitPoint(const tak::sim::World::ScriptEmission& event);
    // COB EXPLODE: the piece flies off as a debris chunk (retail icd 0x50dd20)
    // plus the TA-flag extras (SMOKE/FIRE bits, BITMAPn explosion classes).
    bool explodePiece(const UnitR& u, Anim& a, int piece, int32_t flags);
    // Map a packed COB sfx code to the retail effect GAF sequence. (KINGDOMS.icd
    // emitSfx @0x50da20: the 0x100 bit flags the extended emitter family, low bits
    // pick the effect -- 4/5/6 = damage-flame small/med/large from anims/flames.gaf,
    // 1/2 = smoke; code 259 is a native no-op.)
    static const char* sfxAnimFor(int32_t sfx) {
        int low = sfx & 0xFF;
        if (sfx & 0x100) {
            if (low == 6) return "flames:flame large";
            if (low == 5) return "flames:flame medium";
            if (low == 4) return "flames:flame small";
            if (low == 1 || low == 2) return "smoke:smoke01";
            // Code 259 reaches retail 502bd0, which returns without an effect.
            return nullptr;
        }
        return (low == 0 || low == 1) ? "flames:flame large" : nullptr;
    }
    // At max veterancy, a unit with a `veteranmodel` swaps its mesh for the
    // fancier promoted 3DO (same piece structure, so the COB/anim carries over).
    void maybeSwapVeteranModel(const UnitR& u);
    void configureExplosionHierarchy(Anim& a,const tak::tdo::Object& root);
    void maybeSwapCorpseModel(const UnitR& u);

    // Client-side per-unit setup (model + COB animation VM). Takes id+type only (not a
    // Unit/UnitR) so it is callable from either the sim path (spawn) or the render path
    // (cosmeticStep, off front().live) without touching live world_.
    void registerUnit(int id, const tak::sim::UnitType* type);

    // Manifest player `t`'s faction god at its army's centre (once favour fills).
    // One announcement per player, for the god the SIM summoned. Not sim state: it
    // only gates a HUD notice, and a spectator/rejoin that arrives after a summon
    // simply does not announce it.
    std::array<bool, 8> godAnnounced_{};

    int spawn(const std::string& typeId, float x, float z, float heading, int player);

    void loadTextures();

    const tak::cob::PieceState* pieceFor(const Anim* a, const std::string& objName) const;

    // The 3DO model for a type, loading it on demand (a queued build may have
    // no live unit of that type yet).
    const tak::tdo::Model* ghostModel(const std::string& typeId);

    // Draw a translucent, faintly blue ghost of a building where it will be
    // built later (a queued or not-yet-started site).
    // Draw a projectile that ships a real 3DO mesh (FBI `model`): arrows, spears,
    // daggers, boulders, harpoons, the egg bomb -- 39 weapons, every one of which
    // was a generic yellow streak before. Same projection as a ghost, but solid,
    // lifted by the shot's altitude and yawed along its flight.
    void drawShotModel(const std::string& name, int player, float x, float z,
                       float altPx, float facing, const tak::sim::Projectile* native=nullptr);

    void drawGhostAt(const tak::sim::UnitType* type, float x, float z,
                     bool invalid = false);

    // Already projected, depth-sorted model triangles, one draw per texture run.
    void drawModelTriangles() {
        if (tris_.empty()) return;
        triBatch_.clear();SDL_Texture* cur=nullptr;
        auto flush=[&]{
            if(!triBatch_.empty())SDL_RenderGeometry(ren_,cur,triBatch_.data(),int(triBatch_.size()),nullptr,0);
            triBatch_.clear();
        };
        for(const auto& t:tris_) {
            if(t.tex!=cur){flush();cur=t.tex;}
            triBatch_.insert(triBatch_.end(),std::begin(t.v),std::end(t.v));
        }
        flush();
    }

    // Per-unit screen-space geometry, built in parallel each frame (the expensive
    // model projection) so the single render thread only submits draw calls.
    // Built in three stages, each kept with its own exact input key
    // (client/geometrystages.h): LOCAL (piece transforms, collection, depth sort,
    // texture/UV and shadow-mask choice, shadow silhouette -- model-projected,
    // before zoom and anchor), PLACEMENT (local -> screen) and MATERIAL (body
    // colour). A stage is redone only when its own inputs changed, so a camera
    // pan re-places retained local geometry instead of rebuilding it.
    // Fields are grouped by the passes that read them: the per-frame key check
    // and the draw passes first, the retained local output after.
    struct alignas(64) UnitGeom {
        // ---- line 0: written or read for every visible unit every frame ----
        bool reused=false;           // no stage redone this frame
        bool animatedTexture=false;
        bool canFly=false;
        uint8_t stages=0;            // tak::render::StageBit set redone this frame
        int owner=-1;
        // Telemetry only (worker-written, render-thread tallied; zero while off):
        // why the last build missed (tak::render::MissCause), every input category
        // that changed (bit per MissCause), whether the entry changed owner, vector
        // capacity growths, and worker CPU time for the key check and the rebuild.
        uint8_t missCause=0;
        bool ownerChanged=false;
        uint8_t reallocs=0;
        bool shadowKept=false;       // local rebuild left the shadow coverage unchanged
        uint16_t missMask=0;         // bit per MissCause (10 causes)
        // Workstream I: estimated cost of this entry's next build, in units of
        // kGeomCostUnitNs, from what this frame's build redid (render-thread read
        // when planning the next pass's chunks; only a load-balance hint).
        uint16_t nextCost=0;
        uint32_t keyNs=0, buildNs=0;
        tak::render::PlacementKey placementKey{};
        // `revision` = the screen output: bumped whenever any stage is redone (what
        // shadow tile stamps and distant images compare). The stage revisions are
        // bumped only when that stage is redone. All values come from one
        // GameView-wide counter, so no value repeats for any occupant of an entry.
        uint64_t revision=0;
        // This entry's current generational handle (render thread, set at claim):
        // the identity downstream caches key on instead of the unit id.
        tak::render::RenderHandle handle{};
        uint64_t handleKey() const { return (uint64_t(handle.generation)<<32)|handle.index; }
        // ---- line 1-2: the rest of the key check ----
        tak::render::MaterialKey materialKey{};
        std::vector<uint64_t> localKey;          // exact inputs, never a probabilistic hash
        tak::render::LocalKeyLayout localLayout{};
        // ---- draw passes (distant images, shadow atlas, composite, body assembly) ----
        float ax = 0, ay = 0, occY = 0, alt = 0;
        std::vector<SDL_Vertex> verts;                  // transformed, coloured
        std::vector<std::pair<SDL_Texture*, int>> runs; // (texture, vertex count)
        // Workers emit projected shadow geometry alongside the body. The shadow
        // collector skips body-only lighting, texture lookup and depth records.
        // POSITIONS ONLY (8 bytes/vertex), not SDL_Vertex (20). Every shadow vertex
        // carries the same flat grey and an unused texcoord, so storing them per-vertex
        // was 12 bytes of identical data each -- ~8.5 MB a frame of pure duplication at
        // this unit count. Submitted with SDL_RenderGeometryRaw passing ONE colour and
        // ONE texcoord at stride 0, which makes SDL re-read element 0 for every vertex.
        // Verified that stride 0 is accepted and pixel-identical on both the opengl and
        // software backends; the validation lives in SDL_render.c above the backend, so
        // it behaves the same for D3D/Metal too.
        std::vector<SDL_FPoint> shadowVerts;
        std::vector<SDL_Vertex> maskedShadowVerts; // only cutout faces need UVs/textures
        std::vector<std::pair<SDL_Texture*,int>> maskedShadowRuns; // adjacent masks, original order
        std::array<float,4> shadowBounds{}; // min x/y, max x/y of the placed shadow
        SDL_Texture* shadowAtlasPage = nullptr;
        SDL_Rect shadowAtlasSrc{};
        SDL_FRect shadowAtlasDst{};
        // ---- retained local-stage output (read only when a stage is redone) ----
        // Body: depth-sorted local positions (parallel to verts) and base colours
        // (empty = every vertex white, the collector's colour for every textured
        // face). Shadow: local positions parallel to shadowVerts / maskedShadowVerts.
        std::vector<SDL_FPoint> localPos;
        std::vector<SDL_Color> baseColor;
        std::vector<SDL_FPoint> shadowLocal;
        std::vector<SDL_FPoint> maskedShadowLocal;
        // shadowRevision = the local shadow coverage (shadowLocal, maskedShadowLocal,
        // masked UVs and runs): advanced by a local rebuild only when that output
        // actually differs, so a body-only local change (texture frame, palette)
        // leaves it alone. With shadowPlacement it keys the silhouette tile (E).
        uint64_t localRevision=0, placementRevision=0, materialRevision=0, shadowRevision=0;
        // The placement the shadow points were placed with (set by every placement
        // stage). With shadowRevision it is the exact key of a shadow silhouette
        // tile (client/shadowtiles.h). Kept at the end so the draw-pass fields keep
        // their cache lines.
        tak::render::ShadowPlacement shadowPlacement{};
        // Telemetry: the last local rebuild left the shadow inputs unchanged, so the
        // silhouette was not collected again (body-only local change).
        bool shadowSkipped=false;
        // Workstream G: body stages (tak::render::StageBit) whose inputs changed
        // while this unit's distant image was held, so the body (verts, runs) was
        // not regenerated; the shadow stages ran as usual. The body is regenerated
        // before anything reads it (bake, live draw). bodyRun = body stages run this
        // frame, bodyDeferred = the body was left pending this frame.
        uint8_t bodyPending=0, bodyRun=0;
        bool bodyDeferred=false;
        // The silhouette cell this entry's shadow occupies (workstream F,
        // client/shadowatlas.h). Render thread only. Validated against the
        // allocation's recorded owner (handleKey), so an entry that changed owner,
        // went cold or outlived a renderer reset never inherits a cell.
        tak::render::RenderHandle shadowTile{};
#ifndef NDEBUG
        uint32_t verifiedTick=~0u;
#endif
        // geomEntries_ payload contract (client/geometryowners.h). Cold = no key,
        // so the next build cannot reuse anything; capacity stays for reuse.
        // Revisions are kept: they come from one GameView-wide counter, so a value
        // is never repeated by another occupant of this entry.
        void makeCold() {
            nextCost=tak::render::GeometryCostModel::pack(tak::render::GeometryCostModel::kColdNs);
            localKey.clear();localLayout={};owner=-1;reused=false;stages=0;
            bodyPending=bodyRun=0;bodyDeferred=false;
            verts.clear();runs.clear();shadowVerts.clear();
            maskedShadowVerts.clear();maskedShadowRuns.clear();shadowAtlasPage=nullptr;
            localPos.clear();baseColor.clear();shadowLocal.clear();maskedShadowLocal.clear();
        }
        template <class F> void forEachVector(F&& f) const {
            f(localKey);f(verts);f(runs);f(shadowVerts);f(maskedShadowVerts);f(maskedShadowRuns);
            f(localPos);f(baseColor);f(shadowLocal);f(maskedShadowLocal);
        }
        size_t retainedBytes() const {
            size_t bytes=sizeof(*this);
            forEachVector([&](const auto& v){bytes+=v.capacity()*sizeof(v[0]);});
            return bytes;
        }
        void releaseMemory() {
            makeCold();
            std::vector<uint64_t>().swap(localKey);std::vector<SDL_Vertex>().swap(verts);
            std::vector<std::pair<SDL_Texture*,int>>().swap(runs);std::vector<SDL_FPoint>().swap(shadowVerts);
            std::vector<SDL_Vertex>().swap(maskedShadowVerts);
            std::vector<std::pair<SDL_Texture*,int>>().swap(maskedShadowRuns);
            std::vector<SDL_FPoint>().swap(localPos);std::vector<SDL_Color>().swap(baseColor);
            std::vector<SDL_FPoint>().swap(shadowLocal);std::vector<SDL_FPoint>().swap(maskedShadowLocal);
        }
    };
    tak::OpaqueShadowSubmit shadowOpaqueSubmit_;
    tak::GeometrySubmit bodySubmit_;
    tak::GeometrySubmit fogSubmit_;
    tak::FogUpload fogUpload_;
    uint64_t fogMeshRevision_=0;
    tak::DistantModels distantModelCache_;
    std::vector<tak::DistantModels::Item> distantModelItems_;
    std::vector<SDL_Vertex> shadowCompositeBatch_;
    void drawShadowBatch(std::span<const UnitGeom* const> shadows);
    std::optional<bool> batchShadowMasks_;
    void drawUnitShadow(const UnitGeom& g);
    std::vector<const UnitR*> visUnits_;
    // Are projected unit shadows on? The Options toggle, with a dev-only TAK_NOSHADOW
    // override. Gates the BUILD as well as the draw -- skipping only the draw would still
    // pay to emit ~700k vertices nobody looks at.
    //
    // Sampled ONCE per frame on the main thread into shadowsOnFrame_, and the workers
    // read that. They must not read settings_ directly: the Options UI writes that bool
    // from another thread, and "it is only a bool" is not a defence -- concurrent
    // non-atomic access is a data race whatever the type. Writing it before the parallel
    // geometry pass dispatches makes it happens-before every worker read.
    bool shadowsOn() const {
        static const bool envOff = tak::devEnv("TAK_NOSHADOW") != nullptr;
        return !envOff && (!settings_ || settings_->unitShadows);
    }
    bool shadowsOnFrame_ = true;   // this frame's snapshot of the above

    // Airborne flyers' shadows, held back from the shadow pass and drawn between the
    // GROUND bodies and the AIR bodies -- see the shadow pass for why.
    std::vector<const UnitGeom*> airShadows_;
    size_t airShadowOp_ = SIZE_MAX;   // drawOps_ index to drain airShadows_ before

    // Reused scratch vertices for order markers, progress bars and minimap dots.
    std::vector<SDL_Vertex> overlayBatch_;
    // Body pass assembled in parallel: plan offsets serially, scatter the vertex
    // copies across the pool, then replay the draw ops. Keeps depth order exact.
    // Since workstream H only the commands that are not drawn from the retained
    // arena are assembled here.
    std::vector<SDL_Vertex> bodyVerts_;
    size_t bodyVertexTotal_ = 0;   // body vertices drawn this frame (assembled + arena)
    struct FeatureInst;   // defined below; DrawOp only needs the pointer type
    // `layer` puts AIRBORNE units in a pass of their own, after every feature.
    // Retail paints the world as bucketed z rows -- ground units interleaved with
    // tall features -- and then makes a SECOND full sweep over every row for the
    // units whose occupancy is not GROUND (icd: pass 1 gate 0x4fc9f3 requires
    // unit[0x130]&3 == 1, pass 2 at 0x4fcbd3 requires != 1). A flying unit
    // therefore paints over every tree and rock and cannot be occluded by feature
    // clutter. Sorting by z alone, as we did, let any tree with a larger z draw
    // over a flyer at any altitude -- which is what "flying under trees" was.
    struct PaintItem { float z; const UnitR* u; const FeatureInst* f; int layer = 0; };
    std::vector<PaintItem> paintItems_;   // per-frame painter list (capacity reused)
    std::unordered_set<int> targetSet_;   // per-frame attack-target ids (reused)
    // Parsed COB scripts shared per unit type (see registerUnit).
    struct CobCache {
        std::shared_ptr<const tak::cob::File> file;
        std::vector<uint8_t> explosionReachability;
        std::vector<uint8_t> gaitCode;   // cob::gaitCode, shared by the type's VMs
        std::vector<std::string> pieceNames;
        std::vector<tak::sim::RetailModelPiece> modelPieces;
        std::vector<std::vector<std::array<int32_t,3>>> pieceVertices;
    bool hasCloakAnim = false;   // defines StartCloaking (araspy, npcheket)
        bool hasAim = false;      // has an AimWeapon script
        bool hasFlinch = false;   // has a HitByWeapon script
        bool hasWind = false;     // has a WindChange script
        bool hasFlightSM = false; // has BeginFlight/BeginLanding (drake VTOL state machine)
        bool hasActivate = false; // has an Activate script (onOffable door/power toggle)
        bool hasTurnDir = false;  // has a TurnDirection script (turn-in-place / steering trim)
    };
    std::unordered_map<std::string, CobCache> cobCache_;
    // The body pass as ordered commands (workstream H, client/vertexarena.h).
    // A body command (DrawOp with a texture) is `ranges` consecutive entries of
    // bodyRanges_ in painter order; a range is a run of one source -- a unit's
    // body vertices (UnitGeom::verts, identity = entry handle, content =
    // revision) or its distant-image quad (by value). Features, special units
    // and the ground/air shadow boundary stay command boundaries. A command is
    // drawn either from bodyVerts_ (assembled: start = its first vertex there)
    // or from arena ranges [arenaFirst, arenaFirst + arenaRanges) of
    // bodyArenaFirsts_/Counts_ (placement stays in the screen-space vertices:
    // D's arithmetic is the CPU's, bit for bit).
    struct BodySource { const SDL_Vertex* data; uint32_t count, slot, generation; uint64_t revision;
                        uint32_t arena = UINT32_MAX;      // planner index (arena frames)
                        uint32_t assembled = 0; };        // of its vertices copied into assembled commands
    struct BodyRange { uint32_t source, offset, count; };
    // One vertex copy into bodyVerts_ (an assembled command, or a changed arena
    // source staged behind them for the frame's single upload).
    struct CopyTask { const SDL_Vertex* src; uint32_t count, dst; };
    struct DrawOp { const UnitR* u; const FeatureInst* f;
                    SDL_Texture* tex; int start, count;     // seg if u&&f both null
                    uint32_t firstRange = 0, ranges = 0;
                    int32_t arenaFirst = -1; uint32_t arenaRanges = 0;
                    int32_t arenaCommand = -1; int8_t arenaSlot = -1;
                    bool inPass = false;      // N2: planned into a native pass, arena-eligible at any size
                    // N (planned once per frame, planNativePasses): on the first command of
                    // a pass, one past its last command and its prime; on every command of
                    // a pass, its resolved texture (indexed page, or GL name + blend).
                    bool passPremultipliedPrime = false, passPalette = false, premultiplied = false;
                    int16_t page = -1;
                    int32_t passEnd = -1;
                    GLuint spanName = 0;
                    SDL_Texture* passPrime = nullptr; };
    std::vector<BodySource> bodySources_;
    std::vector<BodyRange> bodyRanges_;
    std::vector<GLint> bodyArenaFirsts_;
    std::vector<GLsizei> bodyArenaCounts_;
    std::vector<tak::render::ArenaRange> bodyArenaRanges_;   // one command's ranges (planner input scratch)
    std::vector<SDL_Vertex> bodyArenaGather_;   // fallback / simulated / verify gathers
    tak::render::VertexArenaPlanner bodyArena_;
    tak::render::VertexArenaBuffer bodyArenaBuffers_[tak::render::kArenaSlots];   // current + draining storage
    std::vector<SDL_Vertex> bodyArenaMirror_[tak::render::kArenaSlots];   // TAK_BODY_ARENA_VERIFY: CPU copies of the GL storages
    uint64_t bodyArenaFrame_ = 0;
    bool bodyArenaOn_ = true, bodyArenaVerifyOn_ = false;   // read once per frame (debug switches)
    void releaseBodyArena();
    size_t bodyArenaBudget() const;
    bool bodyArenaPressure() const;
    void verifyBodyArena(const tak::render::ArenaFrameStats& stats);
    void verifyBodyAssembled(size_t stagedVerts);
    // Feature sync: the sim-side state of each visual feature, refreshed only when
    // World::featGeneration() moves. A member rather than a function-static so a new
    // game cannot inherit the previous one's array.
    // hasSim: is there a SIM feature under this visual instance at all? Decorative
    // instances (shoreline waves and the like) have none, and the snapshot's fallback
    // deliberately reports them alive so they keep RENDERING. That must not be confused
    // with "reclaimable" -- see hoverCursor.
    struct FeatSim { int type; bool burning; bool alive; int fx, fz; bool hasSim; uint32_t burnStarted = 0; bool reclaimable = false; bool blocking = false; };
    std::vector<FeatSim> featSimState_;
    uint32_t lastFeatGen_ = UINT32_MAX;   // != any real generation, so the first sync runs

    std::vector<CopyTask> copyTasks_;
    // Workstream I: the renderer's pool passes are planned by estimated cost
    // (client/workschedule.h): chunks of about equal estimated work and only as
    // many woken threads as the estimate pays for. Debug TAK_RENDER_SCHEDULE=
    // uniform restores the pre-I uniform passes (fixed 8-item chunks, every
    // thread, the former thresholds) on the same pool; TAK_SCHEDULE_<PASS>=
    // serialBelowNs,workerNs,chunkNs[,maxThreads] overrides a policy (tuning).
    tak::render::RenderSchedules schedules_;
    bool scheduleUniform_ = false;
    bool schedulesRead_ = false;
    bool scheduleVerifyOn_ = false;             // debug TAK_SCHEDULE_VERIFY (never set in release)
#ifndef NDEBUG
    ScheduleVerifyTotals scheduleVerify_;
    std::atomic<uint64_t> geomBuildCalls_{0};   // buildUnitGeom calls (verify only)
    void verifyPlan(const tak::render::WorkPlan& plan, size_t items) const;
#endif
    void readSchedules();
    // Run a planned pass on pool_ (debug: timed calibration samples).
    void runPlanned(char kind, const tak::render::WorkPlan& plan, const std::function<void(size_t,size_t)>& fn);
    tak::render::WorkPlan geomPlan_, shadowPlan_, copyPlan_;
    std::vector<uint32_t> shadowPrepItems_;   // dirty tiles of the page being rasterized
    // Telemetry: participants and estimates of this frame's planned passes.
    uint64_t schedThreadsGeom_ = 0, schedThreadsShadow_ = 0, schedThreadsCopy_ = 0;
    double schedEstimateGeomNs_ = 0;
    std::vector<DrawOp> drawOps_;
    // TAK_PROF sub-phase timers (main thread). ALL of these are MONOTONIC -- they only
    // ever grow, and takeProf() returns the delta since its last call rather than zeroing
    // them. They used to be reset in takeProf, which silently corrupted the TAK_SPIKES
    // logger: that keeps its own previous totals to get a per-FRAME delta, so the frame
    // after each one-second PROF line subtracted a large stale total from a freshly
    // zeroed counter and printed negative phase times with the difference dumped into
    // the residual. Two consumers at different cadences cannot share a resettable
    // counter; monotonic plus per-consumer previous values is the shape that works.
    double profProjMs_ = 0, profSubmitMs_ = 0;
    double profShadowMs_ = 0;   // the projected-silhouette pass, inside submit
    // uint64_t, not long: these are MONOTONIC for the whole session now (so the spike
    // logger can diff them independently of takeProf), and `long` is 32 bits on Windows.
    // At ~700k shadow vertices a frame that signed counter overflows -- undefined
    // behaviour, not just a wrong number -- about a minute into a game. The increments
    // are unguarded by TAK_PROF, so it would have happened in ordinary play.
    uint64_t profUnits_ = 0;        // visible units accumulated over the sampled frames
#ifndef NDEBUG
    std::atomic<uint64_t> geometryChecks_{0};
    std::atomic<uint64_t> animatedTextureChecks_{0};
    std::atomic<uint64_t> placementChecks_{0}, materialChecks_{0}, monolithicChecks_{0};
    // Workstream E (debug): skipped silhouettes proven equal to a fresh collection
    // (TAK_GEOMETRY_VERIFY), and TAK_SHADOW_CACHE_VERIFY totals: frames whose kept
    // tiles all equalled a complete re-bake, tiles compared, tiles kept (of which
    // body-only and translated), and tiles the pre-E key would have kept.
    std::atomic<uint64_t> shadowSkipChecks_{0};
    ShadowCacheVerifyTotals shadowVerify_;
    DistantVerifyTotals distantVerify_;
    BodyArenaVerifyTotals bodyArenaVerify_;
#endif
    uint64_t profShadowVerts_ = 0;  // shadow vertices copied + submitted, likewise
    // takeProf's own previous values, so it can report per-interval deltas.
    double profProjPrev_ = 0, profSubmitPrev_ = 0, profShadowPrev_ = 0;
    uint64_t profUnitsPrev_ = 0, profShadowVertsPrev_ = 0;
    // "other" broken out: a periodic hitch was traced into this bucket and there
    // was no way to say WHICH of terrain / fog / effects / HUD it was.
    double profTerrainMs_ = 0, profFogMs_ = 0, profFxMs_ = 0, profHudMs_ = 0;
    double profAtlasMs_ = 0, profBodyMs_ = 0;
    // Per-frame renderer telemetry (render thread only; see rendertelemetry.h).
    // Recording is off unless a debug harness enables it, so in normal play each
    // tally is a not-taken branch. rendererGeneration_ names the current render
    // target lifetime; invalidateRenderTargets() bumps it.
    tak::render::FrameStats renderStats_;
    tak::render::GenerationCounter rendererGeneration_;
    void tallyRenderStats();
    // Workstream A: the rest of the frame's telemetry, folded once per frame
    // after the world pass. Cumulative sources (terrain workers, section decode,
    // gpuvram) are turned into per-frame deltas against the previous fold.
    void foldRenderStats();
    void noteFrameCapacities();   // frame start: capacities, to count reallocations
    ThreadPool::Stats poolStats_;
    struct TelemetryCursor {
        tak::TerrainChunks::Stats chunks{};
        MapView::SectionStats sections{};
        uint64_t gpuCreates = 0, gpuDestroys = 0;
        std::array<size_t, 11> capacity{};
    } telemetryCursor_;
  public:
    const tak::render::FrameStats& renderStats() const { return renderStats_; }
    tak::render::Generation rendererGeneration() const { return rendererGeneration_.current(); }
    void profOther(double& terrain, double& fog, double& fx, double& hud) const {
        terrain = profTerrainMs_; fog = profFogMs_; fx = profFxMs_; hud = profHudMs_;
    }
    void profOther2(double& atlas, double& body) const {
        atlas = profAtlasMs_; body = profBodyMs_;
    }
  private:
    std::atomic<int64_t> profSimTicks_{0};        // sim-tick time in raw perf-counter ticks,
                                                  // accumulated by the worker, read/reset on main.
                                                  // Integer atomic -- portable (atomic<double>
                                                  // arithmetic isn't supported by Apple libc++).

    // Texture atlas: every unit texture packed into one big texture per player-
    // colour slot, so a whole model (and a whole crowd of one player) shares a
    // single texture and collapses to a handful of draw calls. Depth-sorted
    // multi-texture models otherwise force ~one draw call per triangle.
    std::unordered_map<std::string, SDL_Rect> atlasRect_;  // name -> content rect
    int atlasW_ = 0, atlasH_ = 0;
    std::vector<SDL_Texture*> atlasTex_;   // per colour slot; nullptr until built
    uint64_t atlasRetryAt_ = 0;            // SDL_GetTicks64 after which a failed atlas create retries
    // Per-sequence authored clocks; geometry workers read the selected frame.
    // Atlas regions are repainted per atlas and per sequence: atlasTracker_ records
    // the frame each animated region of each colour atlas holds (created under
    // rendererGeneration_ and assetGeneration_), so only regions whose frame changed
    // are painted, and a new or reset atlas is painted completely when created.
    tak::render::AnimatedAtlasTracker atlasTracker_;
    // Model texture / atlas layout lifetime (texture reload, layout rebuild).
    tak::render::GenerationCounter assetGeneration_;
    bool atlasLaidOut_ = false;
    std::set<std::string> animatedTex_;
    struct ModelTextureAnimation {
        std::vector<uint16_t> durations;
        bool loop = false;
        size_t frame = 0;
    };
    std::map<std::string, ModelTextureAnimation> modelTextureAnimations_;
    void animateGlowTextures();
    // Animated regions in a fixed order (animatedTex_). The list (rects, frame
    // textures, and the clock each region follows) is built once per asset
    // generation; an update only copies the selected frames into it.
    std::vector<tak::render::AnimatedRegion> animatedRegions_;
    std::vector<const ModelTextureAnimation*> animatedRegionClocks_;
    tak::render::Generation animatedRegionsAsset_ = tak::render::kNoGeneration;
    void collectAnimatedRegions();
    void paintAtlasContents(int slot, tak::render::AtlasPainter& painter, SDL_Texture* from = nullptr);
#ifndef NDEBUG
    void verifyAtlases();          // TAK_ATLAS_VERIFY: compare with a complete repaint
    uint64_t atlasVerifyChecks_ = 0;
    size_t atlasVerifyRegions_ = 0;
public:
    uint64_t atlasVerifyChecks() const { return atlasVerifyChecks_; }
    size_t atlasVerifyRegions() const { return atlasVerifyRegions_; }
private:
#endif
    // Authored palette lookup sheets: each frame has the 20 reachable shade bands (5–24)
    // arranged in a four-column sheet. Shared pages preserve batching across polygon light levels.
    // Workstream M (client/palettesheets.h, paletteindexed.h): the layout is computed once
    // (paletteLayout_) and held in ONE of two forms. Baked: 2048² RGBA pages, each frame
    // expanded into its 20 bands (the pre-M form; every renderer). Indexed: per page a 1x1
    // key texture (the run's texture, so batching is unchanged) and a 512² (index, alpha)
    // page drawn by the indexed program; sheet ids ride in the texture coordinates. The
    // form switches as a whole (paletteMode_): the other form's pages are released, the
    // layout restarts and geometry, distant images and the body arena are rebuilt.
    struct PaletteTextureSource {
        uint8_t bank = 0;                               // paletteBanks_ (palette + shade table)
        std::vector<tak::render::palette::Frame> frames;  // (index, alpha) per texel
    };
    struct PaletteTextureSheet {
        SDL_Texture* texture=nullptr;   // baked page, or the indexed page's key texture
        SDL_FRect uv{};                 // the frame's band-0 rectangle on the 2048² layout page
        uint32_t sheet=tak::render::palette::kNoSheet;   // indexed: sheet id within the page
    };
    struct PalettePage {
        SDL_Texture* texture=nullptr;   // baked page (16 MiB) or key texture (1x1)
        uint32_t sheets=0;              // indexed: sheet ids handed out
    };
    enum class PaletteMode : uint8_t { Unset, Baked, Indexed };
public:
    // Workstream M totals since the session began (render-study requirement checks).
    struct PaletteTotals { uint64_t indexedDraws = 0, spans = 0, fallbacks = 0, switches = 0, uploadBytes = 0,
                                    indexedFrames = 0, bakedFrames = 0,
                                    bakeRuns = 0; };   // indexed runs drawn into distant images
    const PaletteTotals& paletteTotals() const { return paletteTotals_; }
private:
    std::vector<tak::render::palette::Bank> paletteBanks_;
    std::map<std::string,PaletteTextureSource> paletteTextureSources_;
    std::map<std::string,std::vector<PaletteTextureSheet>> paletteTextureSheets_;
    std::vector<PalettePage> palettePages_;
    tak::render::palette::PageLayout paletteLayout_;
    std::set<const PieceMeta*> palettePreparedModels_;
    tak::PaletteIndexedGL paletteIndexed_;
    std::vector<bool> paletteBanksUploaded_;
    PaletteMode paletteMode_ = PaletteMode::Unset;
    bool paletteIndexedFailed_ = false;   // an indexed draw failed: baked for the rest of the session
    bool paletteLinear_ = false;          // indexed program variant in use (experimental)
    uint64_t paletteUploadBytes_ = 0;     // texture bytes uploaded for sheets this frame (telemetry)
    uint64_t paletteSwitches_ = 0, paletteIndexedDraws_ = 0, paletteFallbacks_ = 0;   // per frame
    size_t paletteCpuKey_ = 0, paletteCpuCache_ = 0;   // telemetry: CPU bytes, recounted when the maps change size
    PaletteTotals paletteTotals_;
    void preparePaletteTextures(const PieceMeta& meta);
    void preparePaletteTexture(const std::string& name);
    void updatePaletteMode();
    void releasePaletteSheets();
    size_t paletteGpuBytes() const;
    size_t paletteCpuBytes() const;
    // The indexed program for a run whose texture is an indexed page's key, else
    // nullptr (atlas pages, baked palette pages, other renderers).
    tak::NativeProgram* paletteProgram(SDL_Texture* texture);
    // One model run: SDL_RenderGeometry, or the indexed program for indexed keys
    // (special units, distant-image bakes). Returns SDL's result, or 1 when an
    // indexed run could not be drawn (counted; the form falls back next frame).
    int drawModelRun(SDL_Texture* texture, const SDL_Vertex* vertices, int count, bool countSdl);
    static int drawModelRunThunk(void* self, SDL_Renderer*, SDL_Texture* texture, const SDL_Vertex* vertices, int count);
    // Native passes (workstream N, client/nativepass.h). In the body replay a run of
    // body commands up to the next feature, special unit, air-shadow boundary or a
    // texture that cannot join is drawn natively under ONE priming -- palette runs by
    // the indexed program, the others by an SDL-identical RGBA program -- when the run
    // holds a command that would be drawn natively anyway (an indexed run, an arena
    // command, a batch of kNativeMinVertices or more), so a pass never adds a priming
    // where SDL's own batching drew the run before. Returns one past the last command
    // drawn (== first when no pass opened: the caller draws `first` as usual).
    // Mode (debug TAK_NATIVE_PASS): All (default), Palette (the workstream-M rule:
    // passes start only at an indexed run -- the same-binary pre-N reference), Off
    // (no passes; indexed runs draw one by one with their own priming).
    enum class NativePassMode : uint8_t { Off, Palette, All };
    NativePassMode nativePassMode_ = NativePassMode::All;
    bool nativePassesOn_ = false;    // this frame: mode != Off and the renderer can open passes
    size_t nativeMin_ = 512;         // this frame's native batch threshold (TAK_BODY_ARENA_MIN)
    tak::NativePassGL nativePass_;
    // Draws the pass planned at drawOps_[first] (passEnd > first); returns passEnd, or
    // `first` when the pass was refused (the caller then draws command by command).
    size_t drawNativeSpan(size_t first);
    // One run of body commands from `first`: one past its last command, whether a
    // command in it is drawn natively anyway, whether it holds indexed runs, and the
    // texture that primes it. Resolves each scanned command's texture into its DrawOp.
    struct SpanScan { size_t end = 0; bool nativeAnyway = false, palette = false, primePremultiplied = false;
                      SDL_Texture* prime = nullptr; };
    SpanScan scanNativeSpan(size_t first);
    // Plans the frame's passes ONCE, before H's arena planning (so the arena knows
    // which commands a pass will draw -- N2's DrawOp::inPass) and for the replay
    // (DrawOp::passEnd and the resolved textures: no second scan, no lookups).
    void planNativePasses();
    bool nativePassArena_ = true;    // this frame: N2 on (debug TAK_NATIVE_PASS_ARENA=0 = off)
    // N2's smallest command (vertices) for the arena inside a pass: every command.
    // A 64-vertex minimum measured 7-15% slower draw once passes were planned once
    // (Experiment 16); debug TAK_NATIVE_PASS_ARENA_MIN changes it.
    static constexpr size_t kNativePassArenaMin = 1;
    size_t nativePassArenaMin_ = kNativePassArenaMin;
    int palettePage(SDL_Texture* texture) const;
    // A unit's runs in one native pass (special units): false when the form is baked,
    // no run is indexed or a run texture cannot join -- the caller draws run by run.
    bool drawRunsNative(const std::vector<std::pair<SDL_Texture*, int>>& runs, const SDL_Vertex* vertices);
    // Select a span command's program/texture inside an open pass; counts indexed runs.
    void useSpanTexture(SDL_Texture* texture);
    void useSpanTexture(int page, GLuint name, bool premultiplied);
    struct SpanTexture { SDL_Texture* texture = nullptr; GLuint name = 0; bool premultiplied = false; };
    const SpanTexture& spanTexture(SDL_Texture* texture);   // name 0 = cannot join a native pass
    std::vector<SpanTexture> spanTextures_;   // per frame (body replay)
    uint64_t paletteSpans_ = 0, paletteSpanCommands_ = 0;   // per frame (telemetry): passes holding indexed runs
    uint64_t nativePasses_ = 0, nativePassCommands_ = 0, nativePassRefused_ = 0;   // per frame (telemetry)
public:
    // Workstream N verifier totals (TAK_NATIVE_PASS_VERIFY): passes checked, failures.
    struct NativePassTotals { uint64_t passes = 0, commands = 0, refused = 0, verified = 0, verifyFailures = 0; };
    const NativePassTotals& nativePassTotals() const { return nativePassTotals_; }
private:
    NativePassTotals nativePassTotals_;

    // Per-type on-screen sprite box (offset from the draw anchor, px @ zoom 1), the
    // union over facings of the projected model bounds. Drives click-selection so a
    // click anywhere on the drawn unit (a tall building's roof, a body above its
    // feet) hits it -- computed lazily via unitHitBox(), cached here.
    std::map<std::string, SDL_FRect> hitBoxes_;
    // Per-type MODEL-space extents (world units) for the retail selection ring:
    // its radii come from the model's x/z half-extents and it floats at the model's
    // mid height, none of which survives unitHitBox's projection into screen space.
    struct RingBox { float halfX = 12.0f, halfZ = 12.0f, midY = 12.0f; };
    std::map<std::string, RingBox> ringBoxes_;
    // After a failed GPU allocation, pause the optional bake paths (palette, shadow
    // silhouette and distant-model pages, model icons) for a few seconds instead of
    // retrying next frame: they run per visible unit per frame, so an un-cached failure
    // becomes a driver-flooding allocation storm (seen in 2026-09 on an 8 GB card behind
    // a 7680x2160 desktop as NVKMS allocation errors that starved the compositor). Each
    // falls back to direct drawing meanwhile. The colour atlases do NOT wait on this
    // shared pause -- without one a colour's units are not drawn (see atlasFor). The
    // backoff lives in the shared gpuvram accountant so terrain participates too.
    bool gpuAllocBlocked() const { return gpuvram::blocked(); }
    void noteGpuAllocFail() { gpuvram::noteFail(); }

public:
    // SDL_RENDER_TARGETS_RESET / _DEVICE_RESET: on a driver or device reset, every
    // TEXTUREACCESS_TARGET texture silently loses its pixels while its handle stays
    // valid -- so units baked/skinned into them render fully transparent (they
    // "disappear"), the classic intermittent-vanish some GPUs show under load or on
    // a focus/resolution change. SDL fires this event exactly then; we drop every
    // render-target-backed cache so the pre-pass re-bakes them cleanly next frame.
    // Surface-backed caches (shadows, build FX) keep their pixels and are untouched.
    void invalidateRenderTargets();
    // Session-teardown release of EVERY GPU texture GameView owns (unit frames,
    // colour atlases, palette/shadow pages, GUI/HUD art, icons, shadows, feature art,
    // effects, lightning, fog, fonts). See the dtor comment: the renderer outlives the session.
    void destroyGpuTextures();
private:
    int buildBarAlign_ = 1;     // conjure/build row: 0=left 1=center 2=right (Options)
    float buildBarScale_ = 1.0f;   // extra row scale on top of uiScale_ (Options)
    float chatScale_ = 1.0f;       // chat box scale on top of uiScale_ (Options)
    int chatAlign_ = 1;            // chat box: 0=left 1=centered (Options)
    // TEXTURE FILTERING (Options; client/texturefilter.h). Units and above = retail's
    // BiLinearFilter on the unit and unit-shadow pages; Everything also turns terrain,
    // feature, effect and ground-shadow sprites LINEAR while zoomed in (worldLinear_,
    // re-decided each frame in prepare() and applied only when it flips).
    int textureFilter_ = 0;
    bool worldLinear_ = false;
    void setTextureFilter(int level);
    void updateWorldFilter();
    SDL_ScaleMode unitScaleMode() const;
    SDL_ScaleMode worldScaleMode() const { return worldLinear_ ? SDL_ScaleModeLinear : SDL_ScaleModeNearest; }
    bool smoothArt_ = tak::art::g_smoothArt; // how the current interface was built
    int healthBars_ = 1;        // 0=off 1=damaged-only 2=always (Options)
    uint64_t statsSampleAt_ = 0, statsGpuAt_ = 0;
    tak::proc::Sample statsProcess_;
    double statsCpuPct_ = -1, statsServerCpuPct_ = -1;
    tak::proc::Sample statsServer_;
    tak::proc::GpuSample statsGpu_;
    std::future<tak::proc::GpuSample> statsGpuPending_;
    bool statsPanel_ = true;    // Options: live readout in the dead strip under the minimap
    // Animation stall gate (see animFrame): the sim tick last observed, and when it
    // last CHANGED. Animation freezes when the sim clock stops, so a network stall looks
    // paused instead of leaving units walking on the spot.
    uint32_t animLastTick_ = 0;
    uint64_t animAdvanceMs_ = 0;
    // Max NEW units the client registers (model/COB/anim VM + Create script) per frame,
    // so a mass simultaneous spawn streams in over ~a second instead of freezing one frame.
    static constexpr int kRegistrationsPerFrame = 64;

    // Append two triangles for an axis-aligned quad (uv is ignored when the batch
    // is drawn untextured).
    static void pushQuad(std::vector<SDL_Vertex>& b, float x, float y, float w,
                         float h, SDL_Color c) {
        SDL_Vertex tl{{x, y}, c, {0, 0}}, tr{{x + w, y}, c, {1, 0}},
                   br{{x + w, y + h}, c, {1, 1}}, bl{{x, y + h}, c, {0, 1}};
        b.push_back(tl); b.push_back(tr); b.push_back(br);
        b.push_back(tl); b.push_back(br); b.push_back(bl);
    }
    // Oriented segment: a quad of thickness `th` from (x0,y0) to (x1,y1). The
    // selection ring's dashes are chords at arbitrary angles, so pushQuad's
    // axis-aligned rect can't draw them.
    static void pushSeg(std::vector<SDL_Vertex>& b, float x0, float y0, float x1,
                        float y1, float th, SDL_Color c) {
        float dx = x1 - x0, dy = y1 - y0;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-3f) return;
        float nx = -dy / len * th * 0.5f, ny = dx / len * th * 0.5f;
        SDL_Vertex a{{x0 + nx, y0 + ny}, c, {0, 0}}, d{{x1 + nx, y1 + ny}, c, {1, 0}},
                   e{{x1 - nx, y1 - ny}, c, {1, 1}}, f{{x0 - nx, y0 - ny}, c, {0, 1}};
        b.push_back(a); b.push_back(d); b.push_back(e);
        b.push_back(a); b.push_back(e); b.push_back(f);
    }

    // Shelf-pack every loaded unit texture into a single atlas layout (rects are
    // shared across colour slots -- only the pixels differ). Called once, lazily.
    void buildAtlasLayout();

    // Build (or return cached) the atlas texture for one colour slot by blitting
    // each texture's slot variant into its packed rect. Main thread only (render
    // target), so it must run before the parallel geometry pass.
    SDL_Texture* atlasFor(int slot);


    // Project + transform one unit's model into screen-space, coloured vertex runs.
    // No SDL calls and only reads shared state (models/textures/heightmap/anim), so
    // it is safe to run for many units at once on the worker pool. drawUnit() then
    // just submits g.runs. `scratch` is a reusable per-thread triangle buffer.
    // A monarch (the five hero units).
    static bool isMonarchType(const tak::sim::UnitType* t) {
        if (!t) return false;
        for (int i = 0; i < 5; ++i) if (t->id == tak::sim::kMonarchs[i]) return true;
        return false;
    }
    // Fully-saturated hue wheel -> RGB, hue in [0,1). Drives the disco tint & floor.
    static SDL_Color discoHue(float h) {
        h = h - std::floor(h);
        float r = std::fabs(h * 6.0f - 3.0f) - 1.0f;
        float g = 2.0f - std::fabs(h * 6.0f - 2.0f);
        float b = 2.0f - std::fabs(h * 6.0f - 4.0f);
        auto cl = [](float v) { return Uint8(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
        return SDL_Color{cl(r), cl(g), cl(b), 255};
    }
    // Is this unit a captured target of the active Shift+D emote?
    bool dancing(const UnitR& u) const;
    // ...or of the active Shift+H headbang emote?
    bool headbanging(const UnitR& u) const;

    // The projected silhouette for one unit, into g.shadowVerts. Split out because
    // it is built alongside the body geometry on the worker pool.
    // Local stage: shadow positions before placement (g.shadowLocal and
    // g.maskedShadowLocal; masked UVs/colours into g.maskedShadowVerts). Returns
    // whether the output differs from what the entry held (exact, element by element).
    bool buildUnitShadow(const UnitR& u, UnitGeom& g, const tak::tdo::Object& root,
                         const PieceMeta& meta, const Anim* anim, float facing,
                         std::vector<Tri>& scratch, const PreparedPiece* prepared = nullptr);
    // mayDeferBody (workstream G, the frame's parallel pass only): the body may be
    // left pending when this unit's distant image is shown without a repaint.
    void buildUnitGeom(const UnitR& u, UnitGeom& g, std::vector<Tri>& scratch, bool mayDeferBody=false);
    // Workstream G: the distant-image eligibility of a unit (before the vertex
    // count and zoom rules), from its snapshot and this frame's placement.
    bool specialUnit(const UnitR& u, bool canFly, float occY, float ay) const;
    bool distantEligible(const UnitR& u, bool canFly, float occY, float ay) const;
    // Generates a deferred body now (render thread) and refreshes its distant item.
    void materializeDistantBody(size_t index);
    // Debug (TAK_DISTANT_VERIFY): every deferred body regenerated in a copy must
    // keep the revision, the vertex count and the shadow output of the entry.
    void verifyDistantDeferral(size_t catchup);
    // The lookup inputs, fixed before the parallel pass: prepare()'s clock and the
    // output scale, and whether the early lookup is on (TAK_DISTANT_LOOKUP_LATE=1
    // restores the pre-G order: every body generated, then the lookup).
    uint64_t distantNow_=0;
    float distantScaleX_=1, distantScaleY_=1;
    bool distantEarly_=false;
    bool distantOff_=false;   // TAK_DISTANT_MODELS_OFF, read once per frame (getenv is per call)
    // Everything buildUnitGeom derives from the unit and the frame before the
    // stage keys: what the stages compute from.
    struct UnitGeomInputs {
        const Visual* visual=nullptr;
        const Anim* anim=nullptr;
        SDL_Texture* atlas=nullptr;
        float facing=0;
        bool reuse=true;   // false under TAK_GEOMETRY_REFERENCE: no prepared pieces
        // Workstream E: the local stage is redone for a body-only input (colour atlas,
        // a frame of an unmasked animated texture) and every shadow input is unchanged,
        // so the retained silhouette is kept instead of collected again.
        bool shadowValid=false;
        tak::render::BodyPlacement body;
        tak::render::ShadowPlacement shadow;
        tak::render::BodyMaterial material;
    };
    // Redo the given stages (tak::render::StageBit) of one unit's geometry.
    // Returns whether the local shadow coverage changed (local stage only).
    // bodyStages: the stages whose BODY half runs (workstream G may hold it back
    // while the shadow half of `stages` runs).
    bool runUnitGeomStages(const UnitR& u, UnitGeom& g, uint8_t stages, uint8_t bodyStages,
                           const UnitGeomInputs& in, std::vector<Tri>& scratch);
    // Debug (TAK_GEOMETRY_VERIFY): the former single-pass body transform from the
    // sorted collector output, compared with the staged result.
    void verifyMonolithicBody(const UnitGeom& g, const std::vector<Tri>& sorted,
                              std::span<const size_t> order, const UnitGeomInputs& in);

    void drawUnit(const UnitR& u);

    // Project model triangles relative to the unit anchor: yaw by heading,
    // fixed tilt so models read against TAK's painted top-down terrain.
    // Light direction for projected shadows, as a per-unit-of-height offset in
    // model space. A vertex at height y lands on the ground at
    // (x + kShadowLX*y, 0, z + kShadowLZ*y), which is what makes the silhouette
    // lean away from the unit instead of sitting under it as a disc.
    //
    // Both are a quarter, straight out of the retail projection at 0x4ec250.
    // That routine projects model vertices in 16.16 fixed point and takes a flag
    // deciding which of two shears to apply:
    //
    //     model  (flag 0):  screen = ( x,         -z - y/2 )   <- our kProjY
    //     shadow (flag 1):  screen = ( x + y/4,   -z - y/4 )
    //
    // 0x4ede50 passes 1 immediately before handing the geometry to the shadow
    // rasteriser (0x4eda80, the one that bumps the "( %d shadow )" poly counter
    // at 0x62a800); the normal model path passes 0 at 0x4ecf21. Shifting screen
    // Y by -y/4 against a ground point that sits at -z is the same as pushing
    // the ground point +y/4 in z, so in world terms the light leans the
    // silhouette equally in +x and +z -- down-and-right, as it does in retail.
    static constexpr float kShadowLX = 0.25f;
    static constexpr float kShadowLZ = 0.25f;
    // How dark a shadowed pixel goes, as a multiply of the ground under it.
    // 0.55 * 255; measured off a retail screenshot, identical on all three
    // channels, so retail multiplies rather than blending toward a grey.
    static constexpr Uint8 kShadowLevel = 140;
    std::unordered_map<size_t, std::string> burnNames_;   // feature type -> name, copied under the lock

    // The model's extent under ROTATION, gathered during the same walk that builds
    // the geometry. Spinning a unit about Y sends each vertex (x,z) round a circle
    // of radius hypot(x,z) and leaves y alone, so these three numbers describe the
    // union of the unit's projected bounds over EVERY facing exactly -- see
    // unitHitBox, which used to sample sixteen of them and union the results.
    struct RadialExtent {
        float maxR = 0;               // largest hypot(x,z): the screen-x half-width
        float minY = 1e30f, maxY = -1e30f;   // screen y, over all headings
        bool any = false;
    };
    void collect(std::vector<Tri>& out, SDL_Texture* atlas, const tak::tdo::Object& o,
                 const Xform& parent, const Anim* anim, float heading, int player,
                 bool mirror = false, bool isRoot = true, bool shadow = false,
                 RadialExtent* ext = nullptr, const PieceMeta* meta = nullptr,
                 bool shadowCull = false, const PreparedPiece* prepared = nullptr,
                 bool paletteLighting = false);

    // Walk the piece tree (exactly as collect(), but transform-only) to the named
    // piece and return its model-space origin M = the composed translation. Used to
    // place effects at a weapon's emit piece (QueryWeapon) or a unit's SweetSpot.
    bool pieceModelOrigin(const tak::tdo::Object& o, const Anim* anim,
                          const Xform& parent, const std::string& want, float out[3]) const {
        const tak::cob::PieceState* ps = pieceFor(anim, o.name);
        Xform xf = scriptTransform(parent,o.x,o.y,o.z,ps);
        std::string on = o.name;
        std::transform(on.begin(), on.end(), on.begin(), ::tolower);
        if (on == want) { out[0] = xf.t[0]; out[1] = xf.t[1]; out[2] = xf.t[2]; return true; }
        for (const auto& c : o.children)
            if (pieceModelOrigin(c, anim, xf, want, out)) return true;
        return false;
    }

    // Resolve a model piece to a WORLD effect position (x,z,alt) for THIS unit, matching
    // the model's on-screen projection: a piece's model origin projects to screen offset
    // (rx,-ry) under facing=-heading + the projection (see collect()), which corresponds to placing
    // an effect at (u.x+rx, u.z, altitude+ry). Returns false if the model/piece is absent
    // (caller falls back to the unit centre). pieceName must be lowercase.
    bool pieceWorldFx(const UnitR& u, const Anim& a, const std::string& pieceName,
                      float& outX, float& outZ, float& outAlt);

    void scriptEffectOrigin(const UnitR& u, const Anim& a, int piece,
                            float& x, float& z, float& alt) {
        x = u.x; z = u.z; alt = unitAltById(u.id) * kProjY;
        if (a.pieceNames && piece >= 0 && size_t(piece) < a.pieceNames->size())
            pieceWorldFx(u, a, (*a.pieceNames)[size_t(piece)], x, z, alt);

    }

    void drawRing(float wx, float wz, float r);

    SDL_Renderer* ren_;
    // vfs_ is declared BEFORE mapView_ so its Compositor can borrow it; it is owned
    // here (not a reference) so a multiplayer client can REMOUNT to the room's
    // mods at game start -- move-assigning vfs_ keeps every borrowed
    // pointer (MapView's Compositor) valid because the object itself is reused.
    tak::hpi::Vfs vfs_;          // retail-root read-path (the only way we read files)
    MapView mapView_;
    std::string installRoot_;    // retail install dir (for remounting with the room's mods)
    tak::hpi::ModUse mods_;      // which mod kinds this launch may use (--mods)
    std::string mapPath_;        // VFS path to the map .tnt (start positions, siblings)
    // VFS read helpers -- the engine's only game-file access.
    std::vector<uint8_t> vread(const std::string& p) const { return vfs_.read(p); }
    bool vhas(const std::string& p) const { return vfs_.has(p); }
    tak::tdf::Node vtdf(const std::string& p) const;
    // A map's sibling scenario file (map.tnt -> map.ota/.cob/.tdf/.txt/.crt).
    std::string mapSibling(const char* ext) const;
    // Remount the data set with the room's game mods (its verified package) and
    // this player's own cosmetic mods, and rebuild the gameplay registry, so this
    // client's sim reads the same gameplay data the referee does.
    void remountMods();
    // Fingerprint of the gameplay data THIS client will feed its sim (current mods).
    uint64_t gameDataHash() const { return tak::hpi::gameplayHash(vfs_); }
    bool crusades_ = false; // which balance registry_ currently holds
    std::string side_ = "ara";
    // Retail loading screen. Alive from the start of world setup until the first
    // tick lands, so the plate covers both our own load and the wait on peers.
    std::unique_ptr<tak::LoadScreen> loadScreen_;
    std::string aiSide_ = "tar";   // single-player: the AI opponent's faction
    // Faction name -> wire index (0 ara, 1 tar, 2 ver, 3 zon, 4 cre).
    static uint8_t facIdx(const std::string& s) {
        const char* n[5] = {"ara", "tar", "ver", "zon", "cre"};
        for (uint8_t i = 0; i < 5; ++i) if (s == n[i]) return i;
        return 0;
    }
    tak::sim::TypeRegistry registry_;
    // The simulation, reached through the CONST view world_. A write has to name
    // writableWorld(), so display code that mutates the sim (67868e8f: addFeature wrote
    // world_.nav() and desynced the client from the referee) fails to compile. Who may write,
    // and how the main thread reads it while the worker runs: see liveWorld() below.
    tak::sim::World simWorld_;
    const tak::sim::World& world_ = simWorld_;
    // Hash maps (not std::map): these are looked up per unit per frame in the
    // serial anim loop and the parallel projection, and tree traversals were a
    // measurable slice of the update cost at thousands of units.
    std::unordered_map<std::string, Visual> visuals_;
    tak::render::UnitTypeTable unitType_;   // unit id -> visual name (client/unittypetable.h)
    std::unordered_map<int, Anim> anims_;
    // Summon fade-in: unit id -> age (s) since it was conjured from a building. Purely
    // cosmetic and viewer-only (driven by the sim's non-hashed justBuilt hook); it
    // fades the unit in and sprinkles the faction shimmer for kBirthFxDur. Updated on
    // the main thread each frame, then read-only during the parallel geometry build.
    std::unordered_map<int, float> birthFx_;
    static constexpr float kBirthFxDur = 0.9f;
    float birthProgress(int id) const;
    std::map<std::pair<int,int>,SDL_Texture*> lightningTextures_;
    std::map<std::string, std::vector<SDL_Texture*>> textures_;
    std::map<std::string, std::vector<ShadowMaskFrame>> shadowMasks_;
    struct ShadowMaskPage { SDL_Texture* texture;int width,height,x=0,y=0,rowHeight=0;
        SDL_Texture* coverage=nullptr; };
    std::vector<ShadowMaskPage> shadowMaskPages_;
    std::unordered_map<SDL_Texture*,SDL_Texture*> shadowCoverageTextures_;
    // Bounded per-unit silhouette targets (<= 4 pages, <= 2048 square), discarded
    // with other render targets on device loss. Since workstream F each unit's
    // tile keeps a persistent cell (client/shadowatlas.h); page slots follow the
    // allocator (a released page leaves a null texture until a new page reuses it).
    // Tile stamps (client/shadowtiles.h): the unit's stable entry, its shadow
    // coverage revision and exact shadow placement, page, slot, size and output
    // scale -- kept per cell (indexed by the cell handle's index).
    using ShadowTileStamp = tak::render::ShadowTileStamp;
    struct ShadowSilhouettePage {
        SDL_Texture* texture=nullptr;
        uint64_t emptySinceFrame=0, emptySinceMs=0;   // 0 = holds cells
    };
    std::vector<ShadowSilhouettePage> shadowSilhouettePages_;
    int shadowSilhouetteWidth_=0,shadowSilhouetteHeight_=0;
    bool shadowSilhouetteAtlasActive_ = false;
    std::vector<SDL_FPoint> shadowAtlasOpaqueScratch_;
    tak::render::ShadowAtlasAllocator shadowAtlas_;
    std::vector<ShadowTileStamp> shadowCellStamps_;   // by cell handle index
    std::vector<uint8_t> shadowCellStampValid_;
    // Allocation policy state (presentation clock; see buildShadowSilhouetteAtlas).
    struct ShadowAtlasPolicy {
        uint64_t frame=0, lastCompactFrame=0;
        uint64_t shrinkSinceFrame=0, shrinkSinceMs=0;    // demand fits smaller pages since
        uint64_t sparseSinceFrame=0, sparseSinceMs=0;    // cells fit without the last page since
        int migratePage=-1;                              // page being emptied (bounded migration)
        uint64_t failFrame=0;                            // last frame whose full repack did not fit
    } shadowAtlasPolicy_;
    void resetShadowAtlas();
    // Tile decision scratch (render thread): this frame's stamp per packed unit,
    // the stamp it is compared with (carried across reallocation), the tiles
    // that need the point check, and the tiles that need a cell.
    std::vector<ShadowTileStamp> shadowTileStamps_;
    std::vector<ShadowTileStamp> shadowTileBefore_;
    std::vector<uint8_t> shadowTileHasBefore_;
    std::vector<size_t> shadowTileChecks_;
    std::vector<size_t> shadowTileNeeds_;
    std::unordered_map<SDL_Texture*,std::vector<SDL_Vertex>> shadowAtlasMaskScratch_;
    ShadowMaskFrame addShadowMask(std::span<const uint8_t> rgba,int width,int height);
    std::vector<Tri> tris_;
    std::vector<SDL_Vertex> triBatch_;   // reused per-unit vertex batch
    uint64_t geometryFrame_=0;
    // Per-unit geometry entries, owned by the UNIT (workstream C): a unit keeps
    // its entry across painter-order changes and short absences; destroyed units,
    // reused ids (world epoch), replacement models and type changes get a fresh
    // cold entry. Bounded retention, released memory trimmed. Render thread only;
    // workers write the entries visGeom_ hands them.
    tak::render::StableEntryPool<UnitGeom> geomEntries_;
    // visUnits_[i]'s entry, in painter order: the ordered references the painter
    // passes use. Sorting reorders these pointers, never the ownership.
    std::vector<UnitGeom*> visGeom_;
    // visGeom_[i]->nextCost, copied at claim (that cache line is written there
    // anyway), so the geometry pass is planned from a dense array (workstream I).
    std::vector<uint16_t> visCost_;
    UnitGeom& visGeom(size_t i) { return *visGeom_[i]; }
    const UnitGeom& visGeom(size_t i) const { return *visGeom_[i]; }
    // Geometry revisions from one counter: a rebuilt entry's revision is never
    // seen before, by any entry (downstream caches compare revision + source).
    std::atomic<uint64_t> geometryRevision_{0};
    // World lifetime for unit identities: bumped before the world is reset for a
    // replay/rejoin (unit ids restart at 1); captureFrame stamps it on each Frame.
    std::atomic<uint32_t> worldEpoch_{1};
    void claimGeometryEntries();
#ifndef NDEBUG
    uint64_t ownershipChecks_=0;   // TAK_GEOMETRY_OWNERSHIP_VERIFY frames checked
    tak::render::EntryPoolStats ownershipTotals_{};   // events summed over the run
    void verifyGeometryOwnership();
#endif
    // Frame slot -> index into visUnits_/visGeom_, rebuilt each frame. A flat
    // vector over the compact snapshot, so the later passes index in O(1) instead
    // of hashing. -1 = not in view.
    std::vector<int> geomIndex_;
    int geomSlot(int id) const;
    std::vector<int> selection_;
    std::unordered_set<int> selSet_;   // rebuilt each draw for O(1) membership
    bool dragging_ = false;
    bool buildDrag_ = false;          // shift-drag placing a line of buildings
    bool manaBuildDrag_ = false;      // plain left-drag places lodestones in a box
    float bdX0_ = 0, bdZ0_ = 0;       // build-drag start (world)
    tak::client::areadrag::Anchor bdAnchor_;   // lodestone-box start on the map plane
    // Empty cells between the sites of a shift-drag line: 0 = footprints touching.
    // The wheel changes it mid-drag; it holds for the rest of the game (any building
    // type) and startMpGame resets it, so it never outlives a game or reaches settings.
    int buildLineGap_ = 0;
    float buildLineWheel_ = 0;        // fractional wheel travel not yet a gap step
    float buildLineGapShown_ = 0;     // seconds left on the GAP label near the cursor
    uint32_t buildLineWheelMs_ = 0;   // when a trackpad's fractional travel last stepped it
    // The last line placeBuildLine queued: its builder, the cell its work leaves that
    // builder on, and its sites. A second drag queued before those orders come back
    // from the server chains on from here instead of from the builder's stale queue.
    struct BuildLineTrail {
        struct Site {int x, z, w, h;};   // footprint origin and size, in cells
        int builder = -1;
        int px = 0, pz = 0;
        uint32_t tick = 0;
        std::vector<Site> sites;
    } buildLineTrail_;
    bool bdQueue_ = false;
    bool reclaimDrag_ = false;        // right-drag box: a builder clears the area
    float rdX0_ = 0, rdZ0_ = 0;       // reclaim-drag start (world)
    tak::client::areadrag::Anchor rdAnchor_;   // reclaim-drag start on the map plane
    bool draggingMinimap_ = false;
    tak::client::areadrag::Anchor dragAnchor_; // marquee start on the map plane
    float dragX1_ = 0, dragY1_ = 0;            // marquee pointer end (screen)
    // A drag start's point on today's screen (the camera may have moved since).
    tak::client::areadrag::ScreenPoint dragStartOnScreen(const tak::client::areadrag::Anchor& a) const {
        return tak::client::areadrag::toScreen(a, mapView_.offX(), mapView_.offY(), mapView_.zoom());
    }
    tak::client::areadrag::Anchor dragAnchorAt(float sx, float sy) const {
        return tak::client::areadrag::anchorAt(sx, sy, mapView_.offX(), mapView_.offY(), mapView_.zoom());
    }
    // The pointer has dragged a box big enough to be an area command (see areadrag.h).
    bool areaBoxAt(const tak::client::areadrag::Anchor& a, float sx, float sy) const {
        return tak::client::areadrag::isAreaBox(a, dragStartOnScreen(a), sx, sy, uiScale_);
    }
    char pendingCmd_ = 0;   // armed order awaiting a click: 'f' fight-move,
                            // 'm' move, 'a' attack, 'p' patrol, 'g' guard
    bool paused_ = false;
    bool briefingPaused_ = false;  // reset the render clock after the startup modal
    bool exitMenu_ = false;          // in-game exit overlay (Esc) is open
    bool campaignReplayReturn_ = false; // Escape/return restores retained strategic view
    bool canReturnToMenu_ = false;   // launched from the front-end -> offer MAIN MENU
    std::vector<std::pair<SDL_FRect, std::function<void()>>> exitHots_;   // overlay hit-rects (screen space)
    // ---- Options (local display/input; see viewer/settings.h) ----
    float edgeScrollSpeed_ = 1.0f;
    bool  edgeScrollOn_ = true;
    float uiScale_ = 1.0f;
    tak::Settings* settings_ = nullptr;               // main()'s settings (for the in-game Options)
    std::unique_ptr<tak::OptionsScreen> options_;     // in-game Options overlay
    // The VOICE CHAT page in options_ was opened from Options' own VOICE CHAT row: its
    // BACK returns to Options (with Options' unsaved-changes state), as CONTROLS' does.
    bool voiceFromOptions_ = false, optionsDirtyCarry_ = false;
    std::unique_ptr<tak::HotkeysScreen> hotkeysScreen_;   // opened from Options -> CONTROLS
    tak::Hotkeys hotkeys_;                             // effective bindings (from settings)
    int gameSpeed_ = 0;         // retail game-speed level -9..+10 (+/- keys, (d+10)/10 x); 0 = normal
    // 10^(level/10): +10 = 10x, 0 = 1x, -10 = 0.1x.
    // Game-speed multiplier. Forced to 1x in a networked game: the peers advance
    // the sim in lockstep at a fixed step, so scaling one peer's dt would desync.
    float speedMult() const;
    bool giveUnitsMenu_ = false;
    enum class DiplomacyAction { Give, Mana, Chat, Voice, Volume, Close };
    struct DiplomacyHot { SDL_FRect rect; int player; DiplomacyAction action; };
    std::vector<DiplomacyHot> giveUnitsHots_;
    uint8_t chatRecipients_ = 0xff;
    // Who hears this player's voice: the diplomacy VOICE column, with chat's defaults and
    // rules (everyone; reset each game; sent with every frame, enforced by the server).
    uint8_t voiceRecipients_ = 0xff;
    // How loud each player's voice plays here: the diplomacy VOLUME sliders, 0..100 by
    // slot, 100 by default. Local only (never sent, hashed or recorded); reset each game
    // like the columns above. Only human players other than you get a slider.
    std::array<uint8_t, 8> voiceVolume_{100, 100, 100, 100, 100, 100, 100, 100};
    uint8_t humanSlots_ = 0;      // this game's human players (room slot type 1), by slot
    int voiceVolumeDrag_ = -1;    // the player whose VOLUME slider is being dragged
    bool voiceVolumeSlider(int p) const;   // does player p's row get a VOLUME slider?
    // The diplomacy screen's choices as a new game starts: chat and voice to everyone, the
    // usual mana sharing, every VOLUME at 100, and which slots are human.
    void resetDiplomacy(const tak::net::RoomView& room);
    void setVoiceVolumeAt(const SDL_FRect& track, int p, float mx);
    tak::VoiceEngine* voice_ = nullptr;
    tak::voice::PushToTalk ptt_;
    // A room that carries voice: connected and in a room or game, never a replay or a
    // benchmark run (which never open the microphone).
    bool voiceRoomActive() const;
    // A text field has keyboard focus (chat composer, lobby fields, Options' name, the
    // streaming key...): push to talk is off, and a held key is released.
    bool textEntryActive() const;
    // Push to talk for one key event; true when it consumed the event.
    bool voiceKey(const SDL_Event& e);
    void drawVoiceIndicator(int winW, int winH);
    // The in-game chat box: received lines (fading) and the composer while typing, over
    // the play area at the CHAT SCALE / CHAT POSITION options. Layout: client/chatbox.h.
    void drawGameChat(int winW, int winH);
    template <class Face> void drawGameChatIn(const Face& font, int winW, int winH);
    SDL_Color chatSenderColor(const std::string& who, bool notice) const;
    std::optional<uint8_t> requestedManaSharing_;
    std::array<bool,8> eligibleGiftRecipients();
    std::array<bool,8> giftEligibleLast_{};   // its answer when the worker held the world
    void drawGiveUnitsMenu(int winW, int winH);
    void giveSelectedUnits(int recipient);
    bool showCounts_ = false;   // F4: player names, kills, losses, and score
    float streamBannerBottom_ = 0;        // setStreamBannerBottom
    tak::hudcorner::Column topLeft_;      // this frame's top-left overlays, in draw order
    bool spectating_ = false;   // watching a live net game (no control, no fog)
    std::string playerName_[8];   // net games: display name per player (from lobby)
    std::optional<uint8_t> resultParticipants_; // frozen starting roster; keep defeated/disconnected players
    bool playerAi_[8] = {};       // net games: which players are server-run AI
    bool showHDebug_ = false;   // terrain-height / lift diagnostic overlay (TAK_HDEBUG env)
    float fps_ = 0;             // smoothed render FPS, shown on the F4 overlay
    int winW_ = 0, winH_ = 0;   // last-known window size (for centering/culling)
    tak::net::MpClient* mp_ = nullptr;
    std::vector<tak::net::Command> outbox_;   // local orders to send to the server (commands and Cmd::Order items, FIFO)
    std::optional<ClickBuffer> click_;        // the open click (ClickScope), if any
    // Explicit orders (protocol 267): the next serial (continues above every own
    // order seen in a bundle, so a rejoin never reuses one that activated), and the
    // orders sent but neither seen back nor dropped by the server (OrderWindow).
    uint32_t nextOrderSerial_ = 1;
    tak::net::OrderWindow orderWindow_;
    // Index of the first UNSENT command in outbox_. The send path is rate- and
    // window-limited, so it usually ships a prefix and keeps the rest; erasing that
    // prefix shifted every survivor down, making a full drain O(B^2/b) for B queued
    // commands sent b at a time. Advancing a head costs nothing; outbox_ is
    // compacted when the head passes the halfway mark, so the drain is O(B).
    size_t outboxHead_ = 0;
    uint64_t mpListMs_ = 0, mpFirstListMs_ = 0;   // auto-join: ListGames timing
    uint64_t mpSlowSinceMs_ = 0;                  // when the replay backlog went deep
    // Client-side jitter/receive buffer (ON by default; TAK_NET_DELAY overrides:
    // 0 = off, K = fixed depth, auto/unset = self-sizing). The sim is paced on the
    // wall clock at 30 Hz and kept ~netDelay_ bundles behind the newest received,
    // so brief server->client jitter is covered from the reserve instead of
    // stalling. Costs ~netDelay_*33ms of input latency (auto keeps that minimal).
    int netDelay_ = -2;          // -2 = read TAK_NET_DELAY once; then 0 = off, else depth
    bool netAuto_ = false;       // auto (default): size the buffer to the link
    bool netBufReady_ = false;   // built the initial reserve
    float netAccum_ = 0;         // wall-clock tick accumulator (seconds)
    uint64_t netFrameAt_ = 0; float netFrameMs_ = 0;   // smoothed render-frame interval (S1)
    uint64_t netStepMs_ = 0;     // last mpStep wall time
    long netBenchFrames_ = 0, netBenchStalls_ = 0;   // jitter-buffer stall metric
public:
    long netBenchFrames() const { return netBenchFrames_; }
    long netBenchStalls() const { return netBenchStalls_; }
    void netEnableRttProbe() { if (mp_) mp_->enableRttProbe(); }
    float netRttMs() const { return mp_ ? mp_->rttMs() : 0.0f; }
    int netDelay() const { return netDelay_; }
private:
    bool replayMode_ = false;                     // playing a recorded .takrep
    std::unique_ptr<tak::net::ReplayWriter> replayRec_;   // this game's recording (see beginGameRecording)
    uint32_t replayRecGame_ = 0, replayRecSeed_ = 0;      // which game it records
    tak::net::ReplayHeader replayRecHeader_;             // ... and its header (beginGameRecording)
    uint64_t gameDataHash_ = 0;   // gameDataHash() of the game set up by startMpGame (computed once: it reads every gameplay file)
    uint64_t replayFlushMs_ = 0, replaySyncMs_ = 0;       // last flush to the OS / sync to the disk
    uint64_t replayRecRetryMs_ = 0;   // when to try opening a recording that could not start (0: none pending)
    std::vector<tak::net::ReplayCheck> replayChecks_;   // recorded (tick, hash) trail
    size_t replayCheckAt_ = 0;                    // next checkpoint to compare
#ifndef NDEBUG
    tak::situation::Harvester situation_;         // TAK_SITUATION: cut a .scn out of the recording
public:
    void armSituation(const tak::situation::Request& r, const tak::situation::Meta& m) { situation_.arm(r, m); }
    // TAK_SPEED_AT: scheduled in-game speed changes + SPEEDLOG lines (gameview_net.cpp).
    void speedProbe();
    std::vector<std::pair<uint32_t, int>> speedAt_;
    size_t speedAtNext_ = 0;
    bool speedAtParsed_ = false;
    bool stallDone_ = false;                      // TAK_STALL_AT fired (gameview_net.cpp)
    uint64_t speedLogMs_ = 0;
    tak::PaceLog pace_;                           // TAK_PACELOG: per-frame ticks/backlog record (src/client/pacelog.h)
    bool paceInit_ = false;
    uint32_t paceLastTick_ = 0;
    uint64_t paceLastMs_ = 0;
    uint64_t paceLastCounter_ = 0;   // performance counter at the previous record (presentation columns)
    double paceLastTd_ = -1;         // display time at the previous record
    // TAK_PACELOG worker columns: ns accumulated by whichever thread runs the sim (the worker,
    // or the main thread inline), read as per-frame deltas by the main thread. Lock waits are
    // the render thread's own (every blocking liveWorld(): canPlaceLocked, clearableAt, ...).
    std::atomic<uint64_t> paceWorldNs_{0}, paceCaptureNs_{0}, paceHashNs_{0}, paceJobWallNs_{0}, paceJobCpuNs_{0};
    std::array<uint64_t, 5> paceSeenNs_{};
    mutable uint64_t paceLockWaitNs_ = 0;
    static bool paceOn() {
        static const bool on = [] { const char* v = tak::devEnv("TAK_PACELOG"); return v && *v && !(v[0] == '0' && !v[1]); }();
        return on;
    }
    void lockSimTimed(std::unique_lock<std::mutex>& lk) const;
    // Adds the wall ns of its scope to `acc` (and the thread-CPU ns to `cpu`, if given) when
    // TAK_PACELOG is on.
    struct PaceSpan {
        std::atomic<uint64_t>& acc;
        std::atomic<uint64_t>* cpu;
        bool on;
        std::chrono::steady_clock::time_point t0;
        uint64_t c0 = 0;
        explicit PaceSpan(std::atomic<uint64_t>& a, std::atomic<uint64_t>* c = nullptr) : acc(a), cpu(c), on(paceOn()) {
            if (!on) return;
            t0 = std::chrono::steady_clock::now();
            if (cpu) c0 = threadCpuNs();
        }
        ~PaceSpan() {
            if (!on) return;
            acc += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
            if (cpu) *cpu += threadCpuNs() - c0;
        }
        static uint64_t threadCpuNs() {
#ifdef CLOCK_THREAD_CPUTIME_ID
            timespec ts{};
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
            return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
#else
            return 0;
#endif
        }
    };
    tak::postrail::Writer postrail_;              // TAK_POSTRAIL: this playback's digest trail, for the head to read back
    void openPostrail(const char* path) { postrail_.open(path); }
private:
#endif
    ReplayCheckTracker replayTracker_;            // first state divergence + first position divergence
    // Set by the SIM thread when the result lands; the MAIN thread (pumpReplayRecording)
    // makes the recording so far durable. The net client's recorded bundles and hashes
    // are appended from the main thread, so reading them off-thread would race.
    std::atomic<bool> replayResultSync_{false};
    std::vector<tak::net::Bundle> replayBundles_;
    size_t replayTick_ = 0;
    float replayAccum_ = 0;
    bool mpReadied_ = false, mpStarted_ = false, mpSetupDone_ = false;
    // interactive lobby UI state
    enum class LobbyScreen { Browser, Create, Crusades } lobbyScreen_ = LobbyScreen::Browser;
    std::unique_ptr<tak::CrusadesScreen> crusadesScreen_;
    bool reconnectRequested_ = false;
    bool menuRequested_ = false;   // set by a MAIN MENU action -> main() returns to the front-end
    bool quitRequested_ = false;   // set by the in-game QUIT button -> main() exits the app
    bool singlePlayer_ = false;    // menu single-player: private local game (SP-flavoured lobby)
    bool spSpectate_ = false;      // SP: watch the AIs (host takes no slot)
    bool specAutoSeated_ = false;  // SP spectate: one-shot -- fill every open slot with a random-race AI
    bool externalLobbyMusic_ = false;   // front-end owns the lobby BGM -> suppress ours
    int lbField_ = 0;   // active text field: 1=createName 2=createPass 3=joinPass 4=chat
    float lobbyScale_ = 2.0f;               // lobby fit scale (set in render)
    float lobbyOffX_ = 0, lobbyOffY_ = 0;   // lobby centre offset (logical units; set in render)
    std::string createName_ = "game", createPass_, joinPass_, chatDraft_;
    bool createCrusades_ = false;
    bool createDoubleSight_ = false;
    bool createPowerCodes_ = false;   // POWER CODES (GameOptions::powerCodes)
    tak::sim::PathfindingMode createPathfindingMode_ = tak::sim::PathfindingMode::Retail;
    bool createSpeedUnlock_ = false;
    uint16_t createUnitCap_ = tak::net::GameOptions{}.unitCap;
#ifndef NDEBUG
    bool debugDefeatSent_ = false;
#endif
    // Fog of war, chosen at CREATE time (0 = not explored, 1 = explored,
    // 2 = full vision). It is a room setting like the rest, so it belongs where
    // the room is set up -- the host could previously only change it after the
    // game already existed. Default matches GameOptions::fogExplored.
    uint8_t createFog_ = 1;
    uint8_t createStartMode_ = tak::net::kStartsFixed;   // create dialog: START LOCATIONS (default FIXED, retail)
    bool createMonarchExp_ = false;   // create dialog: Monarch Expendable (default OFF = monarch matters)
    bool createAllowSpectators_ = true;   // create dialog (MP): ALLOW SPECTATORS (default ON, as before the option)
    // Create dialog: MAP SCENARIO (protocol 259). Per map and never saved: it starts OFF
    // and goes back OFF whenever the selected map changes (syncCreateScenario).
    bool createMapScenario_ = false;
    std::string createScenarioFor_;           // mapPath_ the two fields here belong to
    tak::sim::ScenarioInfo createScenario_;   // the selected map's scenario facts (with registry check)
    void syncCreateScenario();
    bool createScenarioOn() const;            // the create choice plays the map's scenario
    // Whether the create screen offers a map's scenario. A placed or custom unit type
    // this install lacks may come from a game mod pack (ticked by the host in the room,
    // or always enabled), so MissingType is offered (with a warning) when this install
    // has a game pack; the server's START refuses it if no applied pack supplies the
    // type. (A Use Only entry never needs a pack: a MAP SCENARIO game leaves out the
    // names no unit answers to.) Call after scanModPacks().
    bool scenarioOffered(const tak::sim::ScenarioInfo& s) const {
        return s.offerable() || (!s.marker && s.status == tak::sim::ScenarioStatus::MissingType &&
                                 s.seatOneArmy() && (!availableGameMods_.empty() || !alwaysGameMods_.empty()));
    }
    // One selectable map plus the attributes the picker can sort by, read once from
    // the map's .ota GlobalHeader (a tiny text file -- no need to decompress the TNT).
    struct MapInfo {
        std::string name, path;
        std::string title;          // what to SHOW: mapTitleFor(name, missionname)
        int players = 0;            // numplayers (else the StartPos count)
        int sizeW = 0, sizeH = 0;   // "size = W x H" (0 if the .ota omits it)
        tak::sim::ScenarioInfo scenario;   // read only when the .ota declares one (or is a Test Map copy)
        int area() const { return sizeW * sizeH; }
    };
    std::vector<MapInfo> mapList_;      // cached, sorted per mapSort_
    // Map titles. A map's name (its .tnt stem, the wire id) is often lowercase: Iron
    // Plague's archive stores "maps/ulasem arena.tnt". Retail shows the title from its
    // translate tables (translate/maps.tdf, ipmaps.tdf: [ulasem arena] English = Ulasem
    // Arena); so do the create screen, the room and the game list. Display only -- the
    // id on the wire and in settings.ini stays the name.
    std::unordered_map<std::string, std::string> mapTitles_;   // lowercase name -> English
    bool mapTitlesLoaded_ = false;
    void loadMapTitles();
    std::string mapTitleFor(const std::string& name, const std::string& missionName = {});
    std::string mapTitle(const std::string& id);   // any room/game map id (random maps too)
    int mapSort_ = 0;                  // 0 = name, 1 = players, 2 = size
    int mapSortDir_ = 1;              // 1 = ascending, -1 = descending
    // Create-screen map picker: a scrollable list box. mapScroll_ is the index of the
    // first visible row; the rest is geometry cached each frame for wheel + scrollbar
    // drag handling in lobbyInput (all in panel-local logical coords).
    int mapScroll_ = 0;
    void saveCreatePreferences();
    bool createPrefApplied_ = false;
    bool mapPrefApplied_ = false;      // adopted settings_->lastMap once this session
    bool mapDrag_ = false;             // dragging the scrollbar thumb
    // Random-map generator ("Generate Random Map" in the picker): the current params,
    // which slider is being dragged (0=doodad 1=mana 2=water, -1=none), and the three
    // slider bar rects for drag hit-testing.
    tak::mapgen::Params genParams_{};
    std::string genPreviewInfo_, genPreviewError_;
    int genPreviewScattered_ = 0;   // v9 neutral spots the preview roll placed
    int genSlider_ = -1;
    SDL_FRect genSliderRect_[6]{};
    void applyGenParams();             // re-encode genParams_ -> mpMapId_
    void setGenSlider(int i, float mx);  // drag a density slider from a panel-x
    SDL_FRect mapListRect_{};          // the list box (rows area) -- wheel target
    SDL_FRect mapThumbRect_{};         // the scrollbar thumb -- drag grab
    int mapVisRows_ = 0, mapTotalRows_ = 0;   // for clamping + thumb drag math
    // Selected-map preview (the .tnt's embedded minimap), rebuilt on selection.
    SDL_Texture* mapPreviewTex_ = nullptr;
    std::string mapPreviewFor_;        // mapPath_ the current preview was built for
    int mapPreviewW_ = 0, mapPreviewH_ = 0;
    std::string mapPreviewDims_;       // "W x H" cell size, shown under the preview
    // The previewed map's start positions (world px, in StartPosN order) and its world
    // size, read along with the preview so the numbered start markers never reparse.
    std::vector<std::pair<float, float>> mapPreviewStarts_;
    float mapPreviewWorldW_ = 0, mapPreviewWorldH_ = 0;
    std::map<std::string, std::vector<uint8_t>> kingdomPals_;  // kingdom -> RGBA palette (256*4)
    std::vector<std::string> availableGameMods_, availableCosmeticMods_;   // mods/game, mods/cosmetic
    // mods/cosmetic/always_enabled/<pack>: loaded in every game (campaigns too), only named.
    std::vector<std::string> alwaysCosmeticMods_;
    // mods/game/always_enabled/<pack>: applied in every non-campaign room this player
    // hosts. Display copy; MpClient::alwaysGameModPacks() is the one a room sends.
    std::vector<std::string> alwaysGameMods_;
    std::vector<std::string> alwaysOnLoaded_;   // the always-enabled cosmetic packs the current game mounted (remountMods)
    bool modPacksScanned_=false;
    std::string alwaysOnNote() const {   // the campaign briefing's line naming them
        std::string note;for(const auto& name:alwaysOnLoaded_)note+=(note.empty()?"Always-on mods: ":", ")+name;
        return note;
    }
    void scanModPacks();   // fills availableGameMods_ / availableCosmeticMods_ / alwaysCosmeticMods_ / alwaysGameMods_ once
    size_t gameModPage_=0, cosmeticModPage_=0;
    std::string mpMapId_;   // set from the launched map basename
    std::string missionStem_;   // campaign mission to host (headless --mpmission)
    // Per-mission unit whitelist from missions/<stem>.tdf (lowercased ids). When set,
    // the human's conjure menu is filtered to it (a UI restriction; empty = anything).
    std::vector<std::string> missionAllowed_;
    std::vector<std::string> missionObjectives_;   // in-game objectives panel lines
    bool showObjectives_ = true;                   // panel visible (toggle with O)
    std::vector<std::pair<std::string, std::string>> chatLog_;
    // In-game chat: press Enter to compose, lines fade after a while. Kept apart
    // from the lobby chatLog_ so the in-game overlay and lobby panel don't mix.
    bool chatTyping_ = false;
    // notice: raised by this client (gold name); who "" = a local system line, drawn
    // without a name in the notice gold (power codes, the poster).
    struct GameChat { std::string who, text; float age = 0; bool notice = false; };
    std::vector<GameChat> gameChat_;
    // ---- power codes (sim/powercodes.h; gameview_powercodes.cpp) ----
    // This match has POWER CODES on and this client plays in it (not a replay, a
    // spectator or a campaign mission).
    bool powerCodesOn() const;
    // A chat line that named a code: the sim codes go out as Cmd::PowerCode, Radar
    // and MakePoster act here. Nothing while the match has POWER CODES off.
    void powerCode(const tak::sim::PowerCodeInput& in);
    // A local system line. While this client's own +CODE line is on its way to the
    // server and back, the lines it raised wait (heldSystemChat_) so they are drawn
    // under the line that caused them; flushSystemChat releases them on the echo, or
    // after kChatEchoWaitMs if the echo never comes.
    void systemChat(const std::string& text) {
        if (chatEchoMs_) heldSystemChat_.push_back(text);
        else gameChat_.push_back({std::string(), text, 0, true});
    }
    void flushSystemChat() {
        chatEchoMs_ = 0;
        chatEcho_.clear();
        for (auto& text : heldSystemChat_) gameChat_.push_back({std::string(), text, 0, true});
        heldSystemChat_.clear();
    }
    static constexpr uint64_t kChatEchoWaitMs = 2000;
    std::string chatEcho_;                     // the +CODE line awaiting its echo
    uint64_t chatEchoMs_ = 0;                  // when it was sent (0 = none awaited)
    std::vector<std::string> heldSystemChat_;
    // The chat composer's Enter: a +CODE line acts when the match has POWER CODES on,
    // and every line is sent as chat.
    void submitChat(const std::string& line);
    // TAK_POWER_SCRIPT="TICK:LINE;..." (debug builds): type these
    // chat lines at those ticks; a ManaMe line selects every own unit first.
    void powerScriptStep();
    bool radarCode_ = false;   // +Radar: the minimap shows every unit (reset each match)
    // +MakePoster / MAKE POSTER: the whole map at native scale, one tile (the window,
    // at most 2048 x 1152) rendered per frame (posterStep, before the frame's own
    // draw), then written as a PNG off the main thread. The view is the local player's (its fog included).
    struct PosterJob {
        int width = 0, height = 0;          // the map in world pixels = the image
        int tileW = 0, tileH = 0, cols = 0, rows = 0, next = 0;
        std::shared_ptr<std::vector<uint8_t>> rgb;
        std::vector<uint8_t> readback;      // one tile's RGBA, reused tile to tile
        std::string path;
        uint64_t startMs = 0;
        double maxTileMs = 0;   // the worst frame cost of one tile (logged)
    };
    std::unique_ptr<PosterJob> poster_;
    bool posterFrame_ = false;   // draw(): the world only, none of the HUD
    // Lines from poster writer threads (they may outlive a match), shown as chat.
    struct PosterNotes { std::mutex m; std::vector<std::string> lines; };
    std::shared_ptr<PosterNotes> posterNotes_ = std::make_shared<PosterNotes>();
    std::string posterFolder() const;
public:
    void startPoster();
    void posterStep(int winW, int winH);   // main loop, before draw(): one tile, if a poster is due
private:
    uint64_t chatLastMs_ = 0;
    struct LobbyHot {
        SDL_FRect rect;
        std::function<void()> action;
        std::function<void(int)> cycle;
        LobbyHot(SDL_FRect r, std::function<void()> a) : rect(r), action(std::move(a)) {}
        LobbyHot(SDL_FRect r, std::function<void(int)> c) : rect(r), cycle(std::move(c)) {}
    };
    std::vector<LobbyHot> lobbyHots_;
    int localPlayer_ = 0;
    // Player-colour slot per player (which colour variant of each unit texture to
    // use); defaults to the player index. Overridable via --color / --aicolor and
    // the in-game picker.
    int colorSlot_[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    // The 10 player-colour RGBs (sampled from a player-coloured logo texture at
    // load; the fallback below is a sensible distinct palette). Used so the HUD
    // and minimap match whatever slot a player renders in.
    SDL_Color playerColors_[10] = {
        {70, 130, 240, 255}, {215, 60, 55, 255}, {70, 185, 90, 255},
        {235, 205, 55, 255}, {225, 225, 225, 255}, {80, 200, 205, 255},
        {160, 95, 205, 255}, {230, 145, 50, 255}, {230, 120, 180, 255},
        {120, 120, 130, 255}};
    bool sampledColors_ = false;
    // RGB a player's units render in (its slot's player colour). The argument is
    // the sim ownership index (Unit::player) -- one player per slot in skirmish.
    int playerColorSlot(int player) const { return player == 8 ? 9 : (player >= 0 && player < 8 ? colorSlot_[player] : 0); }
    SDL_Color playerColor(int player) const;

    // Height-aware 2.5D: the world-pixel lift for a point, from the terrain height
    // under it, so a unit (and its shadow/health bar/selection/effects) sits on
    // the elevation the tile art shows instead of the flat grid cell. Zero at the
    // map's ground level; water and flat ground are unaffected.
    int heightRef_ = -1;                          // map ground level (modal height)
    // Screen-Y lift per terrain height unit (also used by occlusion + picking).
    // RETAIL'S VALUE IS 0.5, and it is not a guess: 0x511140 returns byte +4 of
    // a cell record -- the terrain height -- and its caller at 0x426820 forms
    //     screenY = (z << 4) - cameraY - (height >> 1)
    //     screenX = (x << 4) - cameraX              <- no height term at all
    // Confirmed by driving both under emulation (tools/re/emulift.py). We used
    // 1.1 here, which is 2.2x too much: at height 120 that is 132px of lift
    // against retail's 60, so a unit on raised ground was drawn most of four
    // tiles too far up-screen and appeared to stand on terrain beside it.
    float kHeightScale_ = 0.5f;
    // Screen-X lift per height unit. Zero, and retail agrees: its screen X is
    // (x << 4) - cameraX with no height term (0x426820). Tunable via TAK_HSCALEX
    // for experiments only.
    float kHeightScaleX_ = 0.0f;
    int kOccScan_ = 12;                            // cells to scan south for a wall
    // Height above ground at a world point, bilinearly sampled so lifts ramp
    // smoothly across a slope. Lazily initialises the modal ground reference and
    // reads the tunable scales / debug flag from the environment.
    float heightAbove(float wx, float wz);
    float rawHeight(float wx, float wz);   // unclamped bilinear height (waterline sink)
    // Screen-Y sink for a wading (canhover) or floating (floater) unit standing in
    // water, from FBI `waterline`. Zero on land and for every other unit.
    float waterSink(const tak::sim::UnitType* t, float wx, float wz);
    // Screen-space displacement of a world point's surface from its flat grid cell,
    // baked into the tile art by the tilted 2.5D view: up (Y) AND sideways (X).
    float terrainLift(float wx, float wz) { return heightAbove(wx, wz) * kHeightScale_; }
    // ---- the datum a FLYER holds its altitude above --------------------------
    // Retail does not fly over the per-pixel heightmap. It precomputes, at map
    // load, one byte per 128x128-world-unit sector holding the MAXIMUM terrain
    // height in that sector (floored at sea level), then max-DILATES that grid
    // 3x3 -- so the value a flyer reads is the highest ground within a 384x384
    // window (icd 0x50e740; grid at world+0x19f18, sector index (x>>23,z>>23)).
    // Its navigator then aims at that value + cruisealt (0x4e42aa / 0x524b89) and
    // a servo walks the unit's Y toward it a step per tick rather than snapping.
    //
    // The visible difference is the whole point: over the real heightmap a flyer
    // twitches with every bump it crosses, which is what "the altitude visibly
    // changes with terrain height" was. Over a dilated maximum it climbs once,
    // smoothly, to clear the highest thing anywhere near it, and stays there.
    float flyerGround(float wx, float wz);
    void buildFlyerGround();
    std::vector<uint8_t> flyGround_;         // 3x3-dilated per-sector max height
    int flyGroundW_ = 0, flyGroundH_ = 0;    // sectors
    const void* flyGroundMap_ = nullptr;     // which map it was built for
    float terrainLiftX(float wx, float wz) {
        if(heightRef_>=0 && kHeightScaleX_==0)return kHeightScaleX_;
        return heightAbove(wx, wz) * kHeightScaleX_;
    }
    // The terrain-relief lift is for MOBILE units standing on painted slopes. A
    // A "structure" (building) for render/build purposes = one that can't actually
    // move. NOTE: the FBI `canmove` flag is unreliable -- some buildings (the Keep,
    // arakeep) set canmove=1 with NO velocity -- so key off maxVel, not type->canMove.
    static bool isStructure(const tak::sim::UnitType* t) { return !t || t->maxVel <= tak::sim::Fixed(); }
    // Retail Glide skips both `noshadow` and `floater` types (KINGDOMS.icd
    // 0x4ec8d8 / 0x4ecac6). We deliberately allow floating units to cast the
    // same animated silhouettes as land units. Lodestones also deliberately cast
    // shadows despite authored exclusions. The global Shadows option still applies.
    static bool castsShadow(const tak::sim::UnitType* t) {
        return t && (!t->noShadow || (t->onMana && isStructure(t)));
    }
    // EVERY unit that casts at all casts a projected silhouette -- there is no
    // second kind of shadow in the Glide renderer we target. The shadow is emitted
    // by the model draw itself (0x4ee700 -> 0x4ec720 -> 0x4ec7b0 -> 0x4ede50),
    // which runs the same geometry through the shear at 0x4ec250 and hands it to
    // a rasteriser that bumps the "( %d shadow )" poly counter. Nothing in that
    // chain looks at `shadowart`.
    //
    // The FBI shadow sprites are the SOFTWARE renderer's cheaper answer, blitted
    // from a subsystem (0x4ee310, reached only from 0x511d00) that never calls the
    // model draw. So in Glide the 17 sequences in shadows.gaf go unused for units
    // and the 104 types declaring `shadowart` get silhouettes like everyone else.
    static bool castsBlobShadow(const tak::sim::UnitType* t) { return castsShadow(t); }
    // EVERYTHING on the map lifts onto the terrain relief by the same rule -- mobile
    // units, buildings, AND the feature decals (mana deposits, trees) -- so a mana
    // deposit sits at the height its heightmap claims and a lodestone/units built on
    // it stack right on top instead of the decal being flat while units float above.
    float uLiftY(const tak::sim::Unit& u) { return terrainLift(u.x.toFloat(), u.z.toFloat()); }
    float uLiftX(const tak::sim::Unit& u) { return terrainLiftX(u.x.toFloat(), u.z.toFloat()); }
    // Snapshot overloads. A FLYER rides the coarse dilated datum, not the relief
    // under its nose -- see flyerGround.
    float uLiftY(const UnitR& u) {
        if (u.type && u.type->canFly) return flyerDatum(u) * kHeightScale_;
        return terrainLift(u.x, u.z);
    }
    float uLiftX(const UnitR& u) {
        if (u.type && u.type->canFly) return flyerDatum(u) * kHeightScaleX_;
        return terrainLiftX(u.x, u.z);
    }
    // Screen-space height a flying unit is lifted by its own altitude. ONE
    // definition, because there used to be several that disagreed: the body applied
    // altitude as a model-space translation while unitScreen (which drives picking
    // and the marquee) used alt*0.8 on top of the per-pixel terrain lift rather
    // than the flyer datum -- so a flyer was drawn in one place and clicked in
    // another.
    float altLift(const UnitR& u) {
        if (!u.type || !u.type->canFly) return 0.0f;
        auto it = anims_.find(u.id);
        float alt = it != anims_.end() ? it->second.altitude : u.type->cruiseAlt;
        // kProjY, because that is what the MODEL projection does with it: the
        // renderer lifts a piece by (localY + altitude) * kProjY (the effect
        // anchor uses the same factor). unitScreen had a hand-tuned 0.8 instead,
        // so picking and the drawn unit disagreed by ~6% of the altitude even
        // before the datum change.
        return alt * kProjY;
    }
    // The SMOOTHED datum for this flyer (Anim::groundY), falling back to the raw
    // sector value for a unit with no live anim yet.
    float flyerDatum(const UnitR& u) {
        auto it = anims_.find(u.id);
        if (it != anims_.end() && it->second.groundInit) return it->second.groundY;
        return flyerGround(u.x, u.z);
    }

    // A unit's current render altitude (flyers rise to cruiseAlt; 0 for ground
    // units or units with no live anim). Used to lift a flyer's projectiles/effects
    // so they leave/strike at the altitude the unit is drawn, not the ground.
    float unitAltById(int id) const;

    // Screen position of a unit's drawn body centre, matching the render lift:
    // terrain relief (uLift*) plus, for a flyer, its cruise altitude (the sprite is
    // raised by alt*0.8*zoom, same factor as drawUnit). Used for height-correct
    // marquee/click selection so a lifted or airborne unit is picked where it's SEEN,
    // not at its flat ground cell.
    SDL_FPoint unitScreen(const UnitR& u);

    // ---- Tactical Dots (Options > Graphics; not in retail) --------------------
    // Zoomed far out, units draw as minimap-style dots instead of models. The
    // zoom policy lives in client/tacticaldots.h; dotsFrame_ is the state the LAST
    // drawn frame used, so picking always matches what is on screen.
    bool tacticalDotsOpt_ = false;              // the Options toggle
    int  tacticalDotsZoom_ = tak::tacticaldots::kDefaultPercent;  // the Options slider
    tak::tacticaldots::Switch dotsSwitch_;      // zoom threshold + hysteresis
    bool dotsFrame_ = false;
    // The one rule for the world's effects and shots under Tactical Dots: when the
    // units are dots, no fire, smoke, particles, glows, debris, storms, beams or
    // projectiles draw either (at that zoom a fireball outsizes the dot it hit and
    // buries the map). Drawing only: the sim, audio and every effect list and clock
    // advance in their own update passes, so zooming back in finds each effect
    // exactly where it would have been.
    bool worldFxFrame() const { return !dotsFrame_; }
    // Exactly the minimap's rule for showing a unit: alive, not embarked, and
    // either a spectator, an ally, or standing in a cell the local player sees.
    // Shared by the minimap and the dots so the two can never disagree about
    // what the fog hides.
    bool radarVisible(const UnitR& u) const {
        if (!u.alive() || u.embarked() || !u.type) return false;
        return noFog_ || alliedToLocal(u.player) || cellVisibleR(u.x, u.z);
    }
    // Centre and edge of a unit's dot: its model origin on screen (unitScreen
    // minus the body bias, so terrain lift and flyer altitude are included).
    SDL_FPoint dotCentre(const UnitR& u) {
        SDL_FPoint p = unitScreen(u);
        p.y += 12.0f * mapView_.zoom();
        return p;
    }
    float dotSide(const UnitR& u) const {
        return tak::tacticaldots::dotSide(u.type ? u.type->footX : 1, u.type ? u.type->footZ : 1,
                                          mapView_.zoom(), uiScale_);
    }
    // One batched draw of every radar-visible unit in view; returns dots drawn.
    size_t drawTacticalDots(int mvw, int winH);
#ifndef NDEBUG
    size_t debugDotCount_ = 0;                  // dots in the last drawTacticalDots
    std::vector<int> debugDotIds_;              // ...and whose they were
    std::vector<int> debugHealthBarIds_;        // units given a health bar in the last draw
#endif

    // Per-type on-screen sprite box (offset from the draw anchor, px @ zoom 1),
    // computed once by projecting the model over all facings and cached in hitBoxes_.
    const SDL_FRect& unitHitBox(const tak::sim::UnitType* type);

    // Per-type model-space extents driving the selection ring, cached in ringBoxes_.
    const RingBox& unitRingBox(const tak::sim::UnitType* type);
    // Half-extent of the model's GROUND PLATE -- the flat untextured quad at
    // y = 0 that every TAK unit's root carries (AraGP and friends). It is the
    // unit's shadow: sized per unit (17.6 for a Monarch, 14.4 for a swordsman),
    // which is why a Monarch has a shadow in retail despite declaring no
    // `shadowart` in its FBI. Zero when the model has no such piece.


    // Height-aware picking: invert the render lift so a click on elevated terrain
    // (a wall/plateau top, drawn lifted UP on screen) resolves to the cell whose
    // *lifted* position is under the cursor, not the flat cell the raw screen->world
    // map would give (which lands on the low ground behind the wall). Returns the
    // FRONT-MOST surface (largest z) that projects to the click, like a depth pick.
    void pickWorld(float sx, float sy, float& wx, float& wz);

    // Terrain occlusion: a wall's baked-relief art projects up-and-north over the
    // flat ground behind it, but terrain is painted before units, so a unit on
    // that ground would draw ON the wall. Find the screen-Y of the projected top
    // surface of the tallest wall BETWEEN this unit and the camera (i.e. to its
    // south, larger z). Units are then clipped to above that line and the hidden
    // part re-drawn as a faint silhouette. Returns a huge value when nothing
    // occludes the unit. kHeightScale_/heightRef_ are lazily set by terrainLift.
    float wallOcclusionY(float wx, float wz);
    uint32_t netTick_ = 0;
    uint32_t netStopTick_ = UINT32_MAX;   // see setNetStopTick
#ifndef NDEBUG
    uint32_t autoplayNext_ = 0;    // next sim tick an autoplay order is due
    uint32_t autoplayRng_ = 0;     // per-player stream; seeded from slot + game seed
    // TAK_AUTOPLAY_FORMATION delivery probe (Legion 10,000 phase 0; stderr only, never read by the
    // sim): the tick the formation order was issued, its command count, and how many of them and on
    // which ticks have come back in a bundle (the tick the server applied them).
    uint32_t formIssueTick_ = 0, formFirstTick_ = 0;
    int formPending_ = 0, formLanded_ = 0;
#endif
    uint32_t lastSendTick_ = 0;     // tick the send credit was last accrued for
    int cmdCredit_ = tak::net::kCmdCapPerTick;   // rate: commands we may send now
    int cmdInFlight_ = 0;   // window: sent, not yet seen back in a bundle
    bool cmdCatchUp_ = false;   // replaying history (a spectate, a reconnect's missed ticks): hold orders, ignore acks
    uint32_t cmdReplayEnd_ = 0;  // server-declared tick where that history ends
    // Reconnecting inside the disconnect wait (beginReconnect): the local time it gives
    // up (0 = not reconnecting) and of the next attempt.
    uint64_t reconnectUntilMs_ = 0, reconnectNextMs_ = 0;
    bool linkDropped_ = false;   // TAK_DROP_LINK_AT_TICK fired (debug harness)
    std::string netError_;
    // --- Sim/render decouple: the sim worker thread (Stage B1c) ----------------------
    // The worker runs the heavy sim tick (apply commands/events + world_.tick + captureFrame)
    // off the render thread. The main thread owns the network (mp_): it drains delivered
    // bundles and hands them to the worker via simInbox_ (FIFO == lockstep tick order), and
    // sends the worker's per-tick state hash back to the server from simOutbox_.
    // Off for headless/replay (inline path) unless wantSimThread_ (TAK_SIM_THREAD) forces it on
    // for verification.
    //
    // world_ is mutated ONLY by whoever runs the sim: the worker (under simMutex_) while it
    // runs, otherwise the main thread (match setup before startSimThread, the inline sim path,
    // replay, the debug harness). writableWorld() is that route and asserts it. hud, render
    // and input never write; they turn a click into a Command. They read the world through:
    //   - the published snapshot (front(), framePlayer, features_/featSimState_, ...) -- the
    //     default, and the only choice on a per-frame draw path;
    //   - worldTables(): data fixed once the match is set up, safe without the lock;
    //   - liveWorld() / tryLiveWorld(): anything else live (canPlace, canGiveUnit, the feature
    //     list), under simMutex_ while the worker runs. A blocking liveWorld() is for one-off
    //     clicks; a per-frame check uses tryLiveWorld() and keeps last frame's answer on a miss.
    //
    // A const World& that holds simMutex_ (only while the worker runs; inline, nothing else
    // touches world_). Main thread only: the worker already holds the lock while it ticks.
    class LiveWorld {
    public:
        const tak::sim::World* operator->() const { return w_; }
        const tak::sim::World& operator*() const { return *w_; }
    private:
        friend class GameView;
        LiveWorld(const tak::sim::World& w, std::unique_lock<std::mutex> lk) : w_(&w), lk_(std::move(lk)) {}
        const tak::sim::World* w_;
        std::unique_lock<std::mutex> lk_;
    };
    LiveWorld liveWorld() const {
        assert(std::this_thread::get_id() == mainThreadId_);
        std::unique_lock<std::mutex> lk(simMutex_, std::defer_lock);
        if (useSimThread_) {
#ifndef NDEBUG
            lockSimTimed(lk);
#else
            lk.lock();
#endif
        }
        return LiveWorld(simWorld_, std::move(lk));
    }
    // Never waits: empty while the worker is mid-tick.
    std::optional<LiveWorld> tryLiveWorld() const {
        assert(std::this_thread::get_id() == mainThreadId_);
        std::unique_lock<std::mutex> lk(simMutex_, std::defer_lock);
        if (useSimThread_ && !lk.try_lock()) return std::nullopt;
        return LiveWorld(simWorld_, std::move(lk));
    }
    // No lock, so ONLY for what match setup fixes and the sim never changes afterwards:
    // featureTypes(), numPlayers(), allied() (teams), pathfindingMode(), manaSpots(),
    // buildAllowed(), isWater() (terrain), benchmarkMode()/benchmarkEndTick(), and the atomic
    // featGeneration().
    // Units, features, players' economy, nav and fog are live: snapshot or liveWorld().
    const tak::sim::World& worldTables() const { return simWorld_; }
    // The one non-const route to world_. Legal on the sim worker, and on the main thread
    // only while no worker runs.
    tak::sim::World& writableWorld() {
        assert(!useSimThread_ || std::this_thread::get_id() != mainThreadId_);
        return simWorld_;
    }
    struct SimJob { tak::net::Bundle bundle; uint32_t tick = 0; bool wantHash = false; bool spectator = false; bool wantPos = false; };
    struct HashJob { uint32_t tick = 0; uint64_t hash = 0; uint64_t pos = 0; };
    std::thread::id mainThreadId_ = std::this_thread::get_id();   // set at construction (main thread)
    std::thread simThread_;
    mutable std::mutex simMutex_;       // guards world_ mutation (worker) vs liveWorld() reads
    std::mutex inboxMutex_;
    std::condition_variable inboxCv_;
    std::deque<SimJob> simInbox_;       // main -> worker: bundles to simulate, in tick order
    std::mutex outboxMutex_;
    std::deque<HashJob> simOutbox_;     // worker -> main: {tick, hash} to send to the server
    std::atomic<bool> simQuit_{false};
    // Quit WITHOUT draining: abandon whatever ticks are still queued instead of
    // simulating them all first. Leaving a game mid-replay-catchup can otherwise
    // queue thousands of ticks, and the join waits for every one of them.
    std::atomic<bool> simCancel_{false};
    std::atomic<uint32_t> simProcessedTick_{0};   // last tick the worker finished (backlog/ack)
#ifndef NDEBUG
    int simSlowUs_ = -1;   // TAK_SIM_SLOW_US: sleep this long per sim tick (a slow client, for flow-control checks)
#endif
    bool useSimThread_ = false;         // true while the worker is running for this game
    bool wantSimThread_ = false;        // decided once per game (see mpStep)
    bool simThreadDecided_ = false;
    bool simThreadMode_ = true;         // interactive default ON; the headless harness opts out
    bool benchmarkMode_ = false;        // menu Benchmark: all-AI watch run + staged spawn plan
    float patrolPerfAccum_ = -1.0f;     // developer fixture only; negative disables it
    int benchmarkLevel_ = 0;            // benchmark intensity 1..5 (for the plan + results label)
    // Benchmark metrics: one sample per 5s milestone (the 7 spawn stages + the 40s end).
    struct BenchSample {
        int gameSec = 0, liveUnits = 0;
        double clientCpuPct = -1, serverCpuPct = -1; // 0–100% total CPU capacity; -1 unavailable
        size_t clientRss = 0, serverRss = 0;         // bytes
        size_t gpuBytes = 0;                         // tracked client texture VRAM (gpuvram)
        double gpuPct = -1;                          // whole-GPU utilization %, -1 = unknown
        size_t gpuSysUsed = 0;                       // whole-GPU VRAM used (all processes), bytes
        float fps = 0, simSpeed = 0;
    };
    std::string benchGpuName_;                       // GPU adapter name (captured once)
    size_t benchGpuTotal_ = 0;                       // total GPU VRAM, bytes
    std::vector<BenchSample> benchSamples_;
    tak::proc::Sample benchCliPrev_, benchSrvPrev_;
    uint64_t benchPrevWallMs_ = 0;
    uint32_t benchNextTick_ = 300;      // next milestone tick (300,600,...,1800)
    long localServerPid_ = 0;           // local takserver pid (0 = N/A, e.g. headless)
    bool benchStatsShown_ = false;      // the benchmark stats overlay is up
    SDL_FRect benchDoneRect_{};          // the stats overlay's DONE button (set each render)
    int benchCamLeg_ = -1;              // benchmark flythrough: leg (faction) the camera is on
    float benchLegT_ = 0.0f;            // seconds into the current leg (drives the zoom-in)
    // The worker: pop bundles FIFO, simulate under simMutex_, hand back the state hash.
    void simWorkerLoop();
    void startSimThread();
    void stopSimThread(bool drain);
    // canPlace reads live world_ (units_, which the worker resizes on spawn/death, plus the
    // nav grid), so it can't run lock-free while the worker ticks: liveWorld(). Placement
    // clicks only and rare (a ghost while positioning a building), so the brief
    // wait for the current tick is invisible; uncontended and cheap when inline.
    bool canPlaceLocked(const tak::sim::UnitType* type, float x, float z);
    // The PER-FRAME form for ghosts (drawGhost, build line, mana drag): the verdict is cached per
    // (type, x, z, published tick), a miss does a try_lock (never a blocking wait), and once a
    // try has failed this frame no more are made -- the previous verdict is reused instead. The
    // click path keeps canPlaceLocked's blocking lock, so a placement is never decided on a guess.
    struct PlacePreview {const tak::sim::UnitType* type=nullptr; float x=0,z=0; uint32_t tick=0;
                         bool ok=false, clearable=false, haveClear=false;};
    std::vector<PlacePreview> placePreview_;
    bool placePreviewBusy_ = false;     // a try_lock failed this frame
    bool placePreviewLast_ = true;      // last verdict handed out (the no-data fallback)
    PlacePreview& placePreviewEntry(const tak::sim::UnitType* type, float x, float z);
    bool canPlacePreview(const tak::sim::UnitType* type, float x, float z);
    bool placePreviewClearable(const tak::sim::UnitType* type, float x, float z);
    // A site blocked ONLY by clearable features: collect those features' ids, so the
    // placement can queue reclaims ahead of the build instead of being refused.
    // Returns false when anything else blocks (terrain, a unit, a building, an
    // indestructible feature) -- those are refusals retail makes too and we keep.
    bool clearableAt(const tak::sim::UnitType* type, float x, float z,
                     std::vector<int>& outFeatures);
    // Submit one Build command; the simulation clears the footprint first.
    void issueClearThenBuild(int builderId, const tak::sim::UnitType* type,
                             float x, float z, const std::vector<int>& feats, bool queue);
    // HUD notice setter that is safe to call from the sim worker: the worker's sim events
    // (god summon, scenario/mission messages) defer into a pending slot that the main thread
    // applies in mpStep, so notice_ (a std::string draw() reads every frame) and noticeTimer_
    // are only ever touched on the main thread. Direct on the main thread.
    std::mutex noticeMutex_;
    std::string pendingNotice_;
    float pendingNoticeTimer_ = 0;
    bool pendingNoticeSet_ = false;
    void postNotice(std::string msg, float t);
    void drainPendingNotice();
    // Actual-vs-requested game-speed meter (F4): measured from our own tick advance.
    uint64_t actualSpeedT0_ = 0, actualSpeedTick0_ = 0;
    float actualSpeed_ = 0.0f;
    float actualSpeedReq_ = 0.0f;   // requested speed the average is currently tracking
    uint64_t lastSpecAckMs_ = 0;   // spectator flow-control heartbeat (see mpStep)
    uint32_t progressSentTick_ = 0;   // tick of the last Msg::Progress (WE S3 flow control, see mpStep)
    bool progressSent_ = false;
    uint64_t gameStartMs_ = 0;     // wall time the game/spectate began (stats real-time elapsed)
    bool follow_ = false;
    bool trackSel_ = false;   // T: keep the camera centred on the selection
    bool trace_ = false;
public:
    bool noFog_ = false;
    // A campaign mission's own fog rules (.ota lineofsight / mapping), which
    // override the room's setting for that mission.
    bool missionFullVision_ = false, missionPreMapped_ = false;
    uint32_t shakeSeqSeen_ = 0;   // last mission ScreenShake sequence acted on
    // A top-of-screen HUD notice from the app shell (e.g. the renderer fallback).
    void showNotice(std::string msg, float seconds) { postNotice(std::move(msg), seconds); }
private:
    int keepId_ = -1, aiKeepId_ = -1, builderId_ = -1;
    std::optional<std::pair<float,float>> initialCamera_;
    int playerMonarchId_ = -1, aiMonarchId_ = -1;
    const tak::sim::UnitType* placing_ = nullptr;
    bool shiftBuildPlaced_ = false; // disarm placement on Shift release after queued work
    // An armed order issued with Shift stays armed for the next click, as
    // retail does (0x526620 keeps the mode and flags it; it disarms once
    // Shift is up). Releasing Shift disarms it.
    bool shiftOrderArmed_ = false;
    float mouseX_ = -1, mouseY_ = -1;   // -1 until the first real mouse motion, so
                                        // edge-scroll can't fire from a (0,0) default
                                        // cursor on launch (before the mouse moves)
    // Retail animated mouse cursors (anims/cursors.gaf). Lazily loaded on the first
    // overlay draw; when it takes over, the OS arrow is hidden (restored in the dtor).
    tak::CursorSet cursors_;
    bool cursorsInit_ = false;          // attempted the one-time load yet?
    int  cursorMode_ = -1;              // -1 uninit, 0 software (drawn), 1 hardware (OS-tracked)
    bool hwCursorFailed_ = false;       // hardware cursor rejected once -> stay on software

    // Render-side motion interpolation: glide units between 30Hz sim ticks (see
    // captureFrame / interpPose). Viewer-only, never hashed.
    // Render snapshot RING for the sim/render decouple (WE S4; was a triple buffer). The writer
    // (the sim worker in threaded mode, or captureFrame inline) fills the OLDEST free slot and
    // publishes it (published_); the render pins one slot for a whole frame (beginFrame /
    // presentFrame -> reading_) so a concurrent publish never tears its reads. A slot is free
    // unless it is published_, reading_ or shown_ (the slot the presentation clock showed last
    // frame, kept so the next frame can stay on or after it), so nine of the twelve always are.
    // Twelve, not the eight the WE design named: at 60 Hz a frame that misses a vsync takes
    // 27 ms, five ticks at 8x, on top of a lag target of four, and eight slots then dropped
    // the tick the next frame wanted (measured on the Mac's real display: 50 of 551 frames).
    // Every tick is captured and each Frame's px/pz/ph is exactly the previous tick's pose, so
    // any slot interpolates correctly on its own: the presentation clock picks the slot for the
    // tick it wants to show (presentFrame). front() returns the render's pinned slot.
    static constexpr int kFrameRing = 12;
    Frame frameBuf_[kFrameRing];
    std::mutex frameMutex_;             // guards published_/reading_/shown_/writing_ (brief index swaps only)
    int published_ = 0;                 // newest fully-written slot (writer sets under lock)
    int reading_ = -1;                  // slot the render pinned this frame, or -1
    int shown_ = -1;                    // slot the presentation clock showed last frame, or -1
    int writing_ = -1;                  // slot the writer is filling (its metadata is not stable), or -1
    int renderReadIdx_ = 0;             // render-thread's pinned slot (mirrors reading_)
    // Presentation clock (WE S4, client only, never hashed). The render keeps a display time Td
    // in ticks and advances it at the game speed every frame, servoed to stay lagTarget ticks
    // behind the newest snapshot; the frame shows slot ceil(Td) interpolated by Td - (ceil(Td) - 1).
    // So the game time shown per frame is even even when ticks are published 0, 1 or 2 per frame,
    // and a single slow tick up to lagTarget is absorbed. lagTarget is the p98 of recent publish
    // gaps plus a quarter tick, clamped to 1..4 ticks: about 1.25 at 1x, 2-3 at 8x and 240 Hz, 4 at
    // 60 Hz. The clock runs at the game speed, or at the measured publish rate (0.25 s EMA) when the
    // game cannot hold that speed, so an overloaded game slows smoothly instead of freezing between
    // snapshots. Gaps are measured in ticks at that rate. A start gate holds
    // the first tick until kPresentGate ticks (scaled to the speed) are published, which hides the
    // spawn tick. Only for live net games: a replay captures once per batch, not per tick.
    struct PresentClock {
        bool on = false, started = false, gateOpen = false;
        bool freeze = false, gate = false, snap = false;   // this frame (TAK_PACELOG columns)
        double td = 0, dtd = 0, nominal = 0;   // display time, its advance and nominal step (ticks)
        double rate = 0;                       // measured publish rate, ticks/s (EMA)
        double lagAfter = 0;                   // EMA of newest - Td after the advance
        double lagTarget = 1;
        uint32_t gateTick = 0, seen = 0;
        uint64_t last = 0;                     // performance counter at the previous frame
        std::array<float, 128> gaps{};         // recent publish gaps, ticks
        size_t gapCount = 0, gapNext = 0;
    };
    PresentClock present_;
    static constexpr uint32_t kPresentGate = 15;
    bool presentClockWanted() const;
    // The slot holding `tick` (newest capture of it), skipping the slot being written; -1 if none.
    int ringSlotLocked(uint32_t tick) const;
    // Pin `slot` for this frame and do the per-frame bookkeeping (caller holds frameMutex_).
    void pinFrameLocked(int slot);
    uint32_t captureCounter_ = 0;       // monotonic; each Frame.gen gets a unique value
    // Parallel captureFrame (WE S5b). The per-unit record copy runs on its OWN small pool,
    // never pool_: the render drives pool_ concurrently from the main thread, and two
    // callers in one ThreadPool would interleave their jobs. Created on the first capture
    // large enough to use it. The capturing thread is the only one that mutates world_
    // (the sim worker, or the main thread inline) and it blocks inside parallelFor, so
    // the pool's reads of world_ race nothing. captureSlots_ carries what the serial
    // tail needs (ID table, live list, conjure-site lookup) without touching units_ again.
    static constexpr unsigned kCaptureThreads = 4;
    static constexpr size_t kCaptureMinParallel = 2000;
    // Units per pool job. Whole blocks keep each thread's writes to the per-slot side
    // arrays (captureSlots_, transportFlags_) off the other threads' cache lines.
    static constexpr size_t kCaptureBlock = 64;
    struct CaptureSlot { int32_t id = 0, site = 0; bool typed = false, buildGhosts = false; float x = 0, z = 0; };
    std::unique_ptr<ThreadPool> capturePool_;
    std::vector<CaptureSlot> captureSlots_;
    // Capture-thread scratch for Frame::unitGrid (workstream L): live positions in live
    // order and the grid builder's cell buffer.
    std::vector<float> captureGridX_, captureGridZ_;
    std::vector<uint32_t> captureGridCells_;
    std::vector<uint8_t> transportFlags_;   // captureTransportEffects: per slot, 1 = weapon packet, 2 = flyer
    void captureUnit(Frame& fb, const Frame& pf, size_t slot);
    // perUnit(slot) for every slot of world_.units(): on capturePool_ from kCaptureMinParallel
    // units up, serially below. perUnit may write only its own slot's outputs and read world_.
    // Sole-mutator check: only the thread that ticks world_ may run this (the sim worker, or
    // the main thread inline), and it stays blocked in parallelFor for the whole pass, so the
    // pool reads a world nobody is writing. units_ storage, size and tick must not move.
    template <class F> void forEachUnitSlot(F&& perUnit) {
        const size_t count = world_.units().size();
        if (count < kCaptureMinParallel) {
            for (size_t i = 0; i < count; ++i) perUnit(i);
            return;
        }
        assert(std::this_thread::get_id() == (useSimThread_ ? simThread_.get_id() : mainThreadId_));
        [[maybe_unused]] const auto* const unitsBefore = world_.units().data();
        [[maybe_unused]] const uint32_t tickBefore = world_.tickCount();
        if (!capturePool_) capturePool_ = std::make_unique<ThreadPool>(kCaptureThreads);
        capturePool_->parallelFor((count + kCaptureBlock - 1) / kCaptureBlock, [&](size_t b, size_t e) {
            const size_t end = std::min(count, e * kCaptureBlock);
            for (size_t i = b * kCaptureBlock; i < end; ++i) perUnit(i);
        }, 1);
        assert(world_.units().data() == unitsBefore && world_.units().size() == count &&
               world_.tickCount() == tickBefore);
    }
    const Frame& front() const { return frameBuf_[renderReadIdx_]; }
public:
    // Render thread: pin the newest published buffer for this frame's reads, then release.
    // Call beginFrame() before any front() read and endFrame() once all are done. Public
    // because the top-level render loop drives them around the whole per-frame gameView pass.
    void beginFrame();
    void endFrame();
    // The render loop's beginFrame: advance the presentation clock and pin the slot it shows
    // (live net games); anywhere else it is beginFrame (pins the newest snapshot).
    void presentFrame();
private:
    float interpAlpha_ = 0.0f;          // 0..1 through the current tick interval (per render frame)
    uint32_t lastCosmeticGen_ = 0;      // front().gen last processed by cosmeticStep's per-tick pass
    // Fight-move ('f') reuses the Attack glyph tinted this red-orange, so it reads apart
    // from a real attack order -- for both the order-column button and the mouse cursor.
    static constexpr SDL_Color kFightMoveTint{255, 90, 80, 255};
    SDL_Texture* fogTex_ = nullptr;
    SDL_Texture* miniTex_ = nullptr;
    // Async minimap crunch (see buildMinimap): thread + handoff buffer.
    std::thread miniThread_;
    std::mutex miniMu_;
    std::vector<uint8_t> miniPix_;
    bool miniBuilding_ = false, miniReady_ = false;

    // Screenshot path only: block until terrain chunks and the minimap are done so
    // a capture never shows half-composited scenery. Normal play never waits.
public:
    void finishTerrain();
private:
    // Join the minimap thread and drop its output -- required before the map it
    // reads is swapped (mapView_.reload) and at teardown.
    void resetMinimap();
    SDL_Texture* panelTex_ = nullptr;
    int panelW_ = 0, panelH_ = 0;
    SDL_Texture* botTex_ = nullptr;
    int botW_ = 0, botH_ = 0;
    // Retail GUI-driven HUD: the parsed .gui and, parallel to gui_.gadgets, the
    // loaded state-art textures for each gadget (imgs[0]=normal,1=hover,2=grayed).
    tak::gui::Gui gui_;
    std::vector<std::vector<SDL_Texture*>> guiTex_;
    std::map<std::string, SDL_Texture*> icons_;
    std::map<std::pair<std::string, int>, SDL_Texture*> modelIcons_;  // model-rendered fallback icons
    std::vector<std::pair<SDL_FRect, const tak::sim::UnitType*>> iconRects_;
    SDL_FRect buildMenuRect_{};
    // The top of what the build row draws this frame (its tooltip line, over the
    // TRAINING line); 0 when no row is up. The chat box stays above it.
    float buildRowTop_ = 0;
    bool overBuildMenu(float mx, float my) const;
    // The info bar's multi-selection strip (client/selportraits.h): one hit rect per
    // drawn type portrait (type id) plus the overflow tile (empty id), rebuilt each draw.
    std::vector<std::pair<SDL_FRect, std::string>> selPortraitRects_;
    static constexpr int kMiniSizeBase = 180;
    int miniSize() const { return int(kMiniSizeBase * uiScale_); }   // UI-scale (Options)
    // Right-side UI strip (minimap + command panel). The map view is kept to the
    // left of it so the panel never draws over the world.
    int panelW() const { return miniSize() + int(20 * uiScale_); }   // fallback width (no GUI loaded)

    // Scale from the retail 640x480 GUI space to screen pixels. Authored at 640x480, so
    // the art keeps its aspect and the buttons stay aligned at any scale.
    //
    // The size is FIXED: it follows the UI SCALE option and nothing else. It used to be
    // uiScale_ * winH/700, which meant the command panel -- the shield medallion, the
    // mana orb, every order button -- grew and shrank as the window was resized, so the
    // HUD was a different size in a window than it was fullscreen. The rest of the HUD
    // was already window-independent (miniSize() and barH() are both uiScale_ only), so
    // this was also the one piece that disagreed with the others about how big the
    // interface is.
    //
    // kHudBase is where that old formula landed at 1080p, so a 1080p display sees the
    // same HUD as before; every other display now gets that same size rather than one
    // scaled by its height, and UI SCALE is the single knob that changes it.
    static constexpr float kHudBase = 1080.0f / 700.0f;
    float guiS() const { return uiScale_ * kHudBase; }
    // Width of the right-hand command-panel strip (the retail UnitMenu is 128 wide in
    // 640-space); never narrower than the minimap.
    int cmdPanelW() const;
    int mapViewW(int winW) const { return std::max(64, winW - cmdPanelW()); }

    // Screen rect for a command-panel gadget (x >= 512 in 640-space): the whole
    // command panel is anchored to the bottom-right corner, so the ButtonPanel art
    // and its buttons share one transform and stay aligned at any scale.
    SDL_FRect guiCmdRect(const tak::gui::Gadget& g) const;

    // Screen rect for a bottom-bar gadget (the retail InfoPanel occupies 640-space
    // y 431..480). The whole bar layout is scaled uniformly to our barH()-tall bar and
    // anchored bottom-left, so the unit-info block clusters at the left while the bar
    // chrome stretches to fill the width.
    SDL_FRect guiBarRect(const tak::gui::Gadget& g) const;

    SDL_FRect minimapRect(int winW, int winH) const;

    // Fill the otherwise-black strip between the minimap and the command panel with a
    // live readout (fps, ping, sim speed, unit counts, clock, memory). How much room is
    // there depends on the window height, the map aspect (the minimap is as tall as the
    // map is), and UI SCALE -- so nothing here assumes a fixed number of rows: it
    // measures the gap, fits as many rows from a priority-ordered list as will go, and
    // draws nothing when even one row would not fit. Display only; reads the render
    // snapshot, never the live world. Toggled by Settings::statsPanel.
    void drawStatsPanel(int winW, int winH);
    // Retail's full-screen radar (TAB). It is the SAME radar, drawn at a bigger
    // rect: the setter (icd 0x4f9ce0) flips one bool and recomputes the radar
    // rectangle from the 126x126 corner box to the whole world viewport, then
    // rebuilds the map picture and the blips. The world is still painted
    // underneath, the side and bottom HUD panels stay put, and the simulation
    // keeps running -- the tick driver never reads the flag. Because every use of
    // the radar goes through minimapRect(), enlarging that rect gives the drawing,
    // the camera click and the order click all at once, exactly as it does there.
    bool fsRadar_ = false;

    // Build the minimap WITHOUT stalling the first frame: averaging every block
    // decodes every terrain tile on the map (hundreds of ms cold), so the crunch
    // runs on a background thread and the texture is adopted when it lands. Both
    // call sites poll `if (!miniTex_) buildMinimap();` each frame already, which
    // doubles as the completion poll; the minimap frame just paints empty briefly.
    void buildMinimap();

    void drawMinimap(int winW, int winH);

    // Returns true if the click was inside the minimap (and moved the camera).
    // Map a minimap-space click to world coords; false if outside the minimap.
    bool minimapToWorld(float mx, float my, int winW, int winH, float& wx, float& wz);

    bool minimapClick(float mx, float my, int winW, int winH);

    // Right-click on the minimap: order the selection to that world point.
    bool minimapOrder(float mx, float my, int winW, int winH, bool queue);

    // After an armed order's click: keep it armed while Shift queues.
    void armedOrderIssued(bool queue);
    // Issue the armed order (pendingCmd_) to the selection at world (wx,wz). On the
    // main map (`precise`) 'a' targets an enemy under the cursor and 'g' the friendly
    // under it; from the minimap (coarse) 'a' falls back to attack-move and 'g' to the
    // nearest friendly. Shared by the map click and the minimap click.
    void issueArmedOrder(char cmd, float wx, float wz, bool queue, bool precise);

    // Left-click on the minimap while an order is armed (e.g. F): issue it at the
    // minimap location (F + minimap = fight-move there). Returns true if handled.
    bool minimapArmedOrder(float mx, float my, int winW, int winH);

    struct FeatArt;   // defined just below; FeatureInst only needs the pointer type
    struct FeatureInst {
        SDL_Texture* tex = nullptr;
        const std::vector<SDL_Texture*>* frames = nullptr;
        const FeatArt* art = nullptr;   // per-frame geometry + GAF timing
        SDL_Texture* shadow = nullptr;
        int w = 0, h = 0, xoff = 0, yoff = 0;
        int sw = 0, sh = 0, sxoff = 0, syoff = 0;
        float x = 0, z = 0;
        std::string name;    // lowercase feature key (burn art + burnt-swap lookups)
        const FeatArt* burnArt = nullptr;   // seqnameburn playback (lazy, on ignition)
        const EffectAnim* frontFlame = nullptr;
        const EffectAnim* backFlame = nullptr;
        uint32_t burnStarted = 0; // simulation ignition tick
        uint8_t burnVis = 0; // sim says burning: draw burnArt + emit smoke
        // Snapshotted once per frame in syncBurningFeatures, under simMutex_. The
        // draw loop used to ask world_.featureAliveAt() per feature with no lock,
        // and addFeature() does a push_back that can REALLOCATE the feature vector
        // under that read -- a dangling read from the hot path, every frame.
        uint8_t aliveVis = 1;
        // Footprint, snapshotted alongside aliveVis. Constant per feature, but it lives
        // in the SIM's Feature, and the reclaim cursor needs it every frame to size its
        // pick radius -- so it is copied here rather than read live. See hoverCursor.
        int fx = 1, fz = 1;
        // Does a SIM feature back this instance? Decoration does not, and decoration is
        // still drawn (aliveVis stays 1 for it) -- so picking must test THIS, not
        // aliveVis. Conflating the two made the broom cursor appear over scenery that no
        // click could ever reclaim.
        uint8_t hasSim = 0;
        bool reclaimable = false;
        bool blocking = false;
        int simType = -2;    // last-seen sim FeatType index (-2 = not yet synced)
        int simId = -1;      // cell-derived sim feature id (matches World's ids)
        bool tree = false;   // category=trees (eligible for the wind-sway option)
        bool mana = false;   // category=mana (deposit cluster: kept walkable/buildable)
        bool glowy = false;  // the animated "Sacred Stone" centre -- the actual
                             // buildable spot; category=mana + animating=1. The
                             // static "Standing Stones" (animating=0) are decoration.
    };
    std::vector<FeatureInst> features_;
    tak::FeatureIndex featureIndex_;
    bool featureIndexDirty_=true;
    // Unit broad phase (workstream L). Frame::unitGrid holds the snapshot's index;
    // unitCandidates() answers "which live units may have a lifted anchor in this
    // screen band" (unitpass::*Band, client/unitindex.h) from it (indices into front().live, ascending) and the caller keeps
    // its exact test. The lift a unit can carry is bounded per map (unitLiftBounds).
    // TAK_UNIT_FULL_SCAN=1 (debug) is the same-binary pre-L reference: every query
    // returns the whole list. TAK_UNIT_INDEX_VERIFY=1 compares every indexed pass with
    // a full scan and throws at the first difference.
    std::span<const uint32_t> unitCandidates(const tak::render::ScreenBand& band);
    tak::render::LiftBounds unitLiftBounds();
    std::pair<int, int> unitFootZRange();
    std::pair<const void*, size_t> unitFootKey_{nullptr, 0};
    std::pair<int, int> unitFoot_{1, 1};
#ifndef NDEBUG
    std::vector<int> overlayIdsIndexed_, overlayIdsFull_;   // TAK_UNIT_INDEX_VERIFY scratch
#endif
    std::vector<uint64_t> unitQueryBits_;
    std::vector<uint32_t> unitQuery_;
    std::vector<uint32_t> unitAll_;       // 0, 1, 2, ... (whole-list answers)
    struct LiftBoundsKey { const void* map = nullptr; const void* heights = nullptr; size_t size = 0;
                           int ref = -2; float scale = 0, scaleX = 0; };
    LiftBoundsKey unitLiftKey_;
    tak::render::LiftBounds unitLift_;
    bool unitFullScan_ = false;           // read once per frame
    uint32_t unitGridShownGen_ = 0;       // snapshot whose grid build time was last tallied
#ifndef NDEBUG
    bool unitIndexVerify_ = false;
    UnitIndexVerifyTotals unitIndexVerifyStats_;
#endif
    std::unordered_set<int> featInstIds_;   // sim ids with a visual inst (dynamic adds)
    std::vector<std::pair<float, float>> manaSpots_;   // Sacred Stone deposits

    struct FeatArt {
        SDL_Texture* tex = nullptr;
        SDL_Texture* shadow = nullptr;
        int w = 0, h = 0, xoff = 0, yoff = 0;
        int sw = 0, sh = 0, sxoff = 0, syoff = 0;
        std::vector<SDL_Texture*> frames;   // >1 entries when animating
        // Animated sequences (waves especially) author motion through PER-FRAME
        // size + anchor offsets, and each frame carries a GAF display duration in
        // 30Hz engine ticks. Retail keeps one clock per TYPE (all instances in
        // sync) and loops continuously.
        struct FGeom { int w = 0, h = 0, xoff = 0, yoff = 0; };
        std::vector<SDL_Texture*> shadowFrames;
        std::vector<FGeom> shadowGeom;
        std::vector<uint16_t> shadowDurations;
        bool shadowLoop = false;
        bool bodyLoop = false;
        std::vector<FGeom> fgeom;   // per-frame geometry, parallel to frames
        std::vector<int> tickEnd;   // cumulative end tick per frame
        int totalTicks = 0;         // full loop length in 30Hz ticks
    };
    std::map<std::string, tak::tdf::Node> featureDefs_;
    std::map<std::string, tak::gaf::Palette> featurePals_;
    std::map<std::array<std::string, 6>, FeatArt> featureArt_;

    // Unit ground shadows: the sprites from data/anims/shadows.gaf named by each
    // unit's FBI `shadowart`, recoloured to translucent black. Loaded once.
    struct ShadowTex { SDL_Texture* tex = nullptr; int w = 0, h = 0, xoff = 0, yoff = 0; };
    std::map<std::string, ShadowTex> shadowTex_;
    bool shadowsLoaded_ = false;
    const ShadowTex* shadowFor(const std::string& art);

    void loadFeatureDefs();

    const tak::gaf::Palette* featurePalette(std::string world);

    FeatArt* featureArtFor(const tak::tdf::Node& def, const char* seqKey = "seqname",
                           const char* shadKey = "seqnameshad");
    void swapFeatureArt(FeatureInst& fi, const std::string& name);
    void syncBurningFeatures();

    // Place one feature instance by definition name; returns success.
    bool addFeature(const std::string& rawName, int cx, int cz);

    void loadFeatures();
    // Scatter retail wave sprites along the coast (display only; see the impl).
    void addShorelineWaves();

    // Track numbers for a side from gamedata/sidedata.tdf (falls back to IP).
    std::vector<int> factionMusicTracks(const std::string& side);

    // Every faction's music tracks (deduped), for a SPECTATOR -- who has no side of their
    // own, so their in-game music draws from all factions' playlists (startMusic shuffles).
    std::vector<int> allFactionMusicTracks();

    void loadPanel(const std::string& side);
    void loadInterfaceFonts();
    void reloadInterfaceArt(const tak::Settings& s);

    // Palette for a GUI GAF: the sibling anims/<gaf>.pcx if it exists (per-faction
    // panels ship one), else the global palettes/guipal.pal used by gui.gaf.
    tak::gaf::Palette guiPalette(const std::string& gaf);

    // Load one GAF sequence frame (anims/<gaf>, sequence seq, frame idx) to a texture.
    SDL_Texture* loadGuiFrame(const std::string& gaf, const std::string& seq, int frame);

    // Parse the faction in-game .gui and load every gadget's state art. side is the
    // 3-letter faction ("ara"/"tar"/"ver"/"zon"/"cre"); the file is guis/<side>ingame.gui.
    void loadGui(const std::string& side);

    static constexpr int kBarHBase = 72;
    int barH() const { return int(kBarHBase * uiScale_); }   // UI-scale (Options)

    struct OrderBtn {
        SDL_Texture* frames[3] = {nullptr, nullptr, nullptr};   // normal/hover/armed
        int w = 0, h = 0;
        char cmd = 0;        // 'm','a','p'; 0 = stop (instant)
        const char* label;
    };
    std::vector<OrderBtn> orderBtns_;


    void loadOrderButtons();

    SDL_FRect orderBtnRect(size_t i, int winW) const;

    void drawOrderColumn(int winW, int winH);

    // The front selected unit, if it has more than one weapon (worth a picker).
    const UnitR* multiWeaponSel();

    std::vector<SDL_FRect> weaponRects_;
    // A small row of weapon-select buttons under the order column: retail fires
    // only the active weapon; click (or press W to cycle) to pick another.
    void drawWeaponButtons(int winW, int winH);

    // Issue SetWeapon(slot) for every selected unit that has that slot.
    void selectWeapon(int slot);

    bool weaponButtonClick(float mx, float my);

    // ---- Retail GUI-driven HUD ----------------------------------------------
    // The command panel (right strip) and its order buttons are laid out from the
    // faction .gui (see loadGui). Rects are anchored bottom-right and share the
    // guiCmdRect transform so the ButtonPanel art and its buttons stay aligned.

    int guiIdx(const char* name) const;

    // Leftmost gadget with this name. Several bar gadgets appear twice -- the primary
    // single-unit group (UnitInfo1, x~114) and the second-unit group (UnitInfo2,
    // x~386); the primary is always the left one, the conjure-target the right one.
    int guiIdxLeft(const char* name) const;
    int guiIdxRight(const char* name) const;

    // (gadget index, command char) for each command button to show for the current
    // selection. cmd: 'm'/'a'/'p'/'g' arm pendingCmd_; 's' = Stop (immediate);
    // '1'/'2'/'3' = weapon slot; 'O'/'D'/'H' = stance offensive/defensive/passive;
    // 'K'/'k' = cloak on/off; 'N'/'F' = active on/off (all immediate toggles).
    std::vector<std::pair<int, char>> guiActiveButtons() const;

    std::vector<std::pair<SDL_FRect, char>> guiBtnRects_;
    // Press flash: which command button was last clicked and when, so a push button
    // shows retail's Pressed face for a moment even though we dispatch on mouse-down.
    char guiPressed_ = 0;
    uint32_t guiPressedMs_ = 0;   // hit list, filled by renderGui

    // Draw the command panel chrome + buttons + idle crystal ball. Falls back to the
    // old vertical order column if no .gui loaded.
    void renderGui(int winW, int winH);

    // Returns true if the click hit (and was handled by) a command-panel button.
    bool guiClick(float mx, float my);
    // Retail HUD feedback: every side-panel / build-icon press plays the local
    // faction's click tone (sounds/tone<side>.wav -- the click.hpi overridables).
    // Retail acknowledgment bong: the faction tone. The soundclass "_NN-note"
    // entry names LOOK like chromatic semitones (A=1..G#=12), but whether
    // retail actually pitch-shifts per command is UNPROVEN -- player memory
    // says one unvarying bong, and this data has dead keys (TreeBurn). The
    // pitch stays OFF until the icd playback-rate disassembly proves it;
    // the mixer's rate support is ready if it does. gain 2.0 undoes the /2
    // mixing headroom: retail played tones at native amplitude, and quiet
    // click.hpi replacements vanish at half volume.
    void playClickTone(int semitone = 0);

    // The conjure/build menu for a builder type, filtered by the active mission's unit
    // whitelist (missions/<stem>.tdf) when one is loaded -- so a campaign mission only
    // offers the units it allows. An empty whitelist means no restriction.
    std::vector<std::string> conjureMenu(const std::string& builderType) const;

    // Returns true if a click hit (and was handled by) a conjure/build icon. The icons
    // sit above the info bar (not inside it), so this is hit-tested independently of the
    // bottom-bar region -- placement arms for structures/mobile conjurers, else it
    // trains/unqueues at a building (Ctrl toggles infinite, Shift/Ctrl+Shift = 5/10).
    bool buildIconClick(float mx, float my, bool lmb, bool rmb);

    // Issue a per-unit toggle command (targetId = value) to every selected own unit.
    void issuePerUnit(tak::net::Cmd kind, int value);

    // Draw a thin fill gauge (HP/mana) at a bar gadget's .gui position.
    void drawGauge(const char* name, float frac, SDL_Color c);

    // Retail's Unit Info dialog (guis/unitinfo.gui): portrait + the three mobility
    // stats for the hovered conjure icon, else the first selected unit. Does not
    // pause -- retail's didn't either. See client/gameview_unitinfo.cpp.
    const tak::sim::UnitType* unitInfoSubject() const;
    void toggleUnitInfo();
    void drawUnitInfo(int winW, int winH);
    bool inputUnitInfo(const SDL_Event& e, int winW, int winH);
    const tak::sim::UnitType* unitInfoType_ = nullptr;   // null = dialog closed
    SDL_Texture* unitInfoBg_ = nullptr;
    SDL_Texture* unitInfoOk_[2] = {};
    Font unitInfoLabelFont_, unitInfoValueFont_;
    SDL_Texture* unitInfoIcon_ = nullptr;
    std::string unitInfoIconFor_;      // which type unitInfoIcon_ was baked for
    SDL_FRect unitInfoOkRect_{0, 0, 0, 0};

    // Retail bottom InfoPanel bar: chrome (InfoPanel + EndCap) plus the selected
    // unit's portrait/name/HP/mana at the .gui positions -- only what has data (no
    // empty portrait box or bars, unlike retail). Returns false (so drawPanel
    // keeps its own chrome) when no .gui is loaded. The build menu + mana readout stay
    // in drawPanel and draw on top.
    bool drawGuiInfoBar(int winW, int winH);
    // A multi-selection's per-type portraits, ALL of them: `primary` is the single-unit
    // portrait's full-size spot, where slot 0 (the front unit's type) anchors the strip
    // at the same size as the rest, which extend left into `room` px; a count badge on
    // each (badge sized by `px`). Fills selPortraitRects_ and returns the width used
    // left of the primary's spot. Single selection: draws nothing (the caller draws
    // that portrait), returns 0.
    float drawSelPortraits(SDL_FRect primary, float room, float gap, float px);
    std::vector<tak::selportraits::Group> selTypeGroups() const;   // live selection, by type
    // A multi-selection's bar stats: HP / mana totals over the live selected units, and
    // the name-slot label when the selection is exactly one control squad ("GROUP 3").
    tak::selportraits::Totals selTotals() const;
    std::string selSquadLabel() const;
    static constexpr float kSelPortraitMinScale = 0.6f;   // single-row shrink floor, then two rows
    // A click on that strip: plain = select only that type, Shift = drop it. Consumes
    // any press on a portrait (or the overflow tile) so it never reaches the world.
    bool selPortraitClick(float mx, float my, bool lmb);

    // A one-word description of what the selected unit is doing, for the command
    // panel's HelpText recess.
    const char* unitStatusText(const UnitR* u) const;

    // What a builder is currently conjuring/building, for the info bar (else "").
    std::string conjureTargetName(const UnitR* u) const;

    // A simple filled bar (HP/mana) at explicit pixel coords.
    void drawBar(float x, float y, float w, float h, float frac, SDL_Color c);

    // Returns true if the click hit (and was handled by) the order column.
    bool orderColumnClick(float mx, float my, int winW);

    std::map<std::string, SDL_Texture*> weaponIcons_;
    // Weapon-slot icon: anims/weaponpic/<name>{sb,sbh}.jpg (name lowercased, spaces
    // stripped; sb = normal, sbh = selected/gold), falling back to the default_* pics
    // for weapons that ship no icon. These 32x32 JPGs are the full button (icon +
    // recessed frame), so they replace the empty WPrimaryButton recess.
    SDL_Texture* weaponIcon(const std::string& wname, bool selected);

    SDL_Texture* iconFor(const std::string& typeId);

    // Fallback build icon: render the unit's 3D model into a small cached texture,
    // for buildables that ship no anims/buildpic. Retail never drew a build pic for
    // the Zhon trapdoor spider (zonspide) -- a base-game creature the Crusades
    // balance made buildable -- so without this its slot would be an empty box.
    SDL_Texture* modelIconTex(const std::string& id, int slot, bool canMove);

    // Build controls belong to exactly one selected builder.
    const UnitR* selectedBuilder() const;

    // Keys.TDF-derived hotkeys. Returns true when the key was consumed.
    bool handleKey(SDL_Keycode key, uint16_t mod);

    // Replace the selection with every owned, living unit matching `pred`.
    template <class Pred>
    void selectOwned(Pred pred) {
        if (spectating_) return;   // watch-only
        selection_.clear();
        for (const UnitR* _up : front().live) {
            const UnitR& u = *_up;
            if (u.alive() && u.player == localPlayer_ && u.type && !u.underConstruction &&
                pred(u))
                selection_.push_back(u.id);
        }
        if (!selection_.empty()) voice(selection_.front(), "select");
    }

    // --- control squads (groups / formations); backed by lockstep Cmd::SetSquad -------
    void issueSquad(int unitId, int val);

    // Assign the current selection to squad `num` as a group (sign +1) or formation
    // (sign -1). Assign REPLACES the squad (old members not selected are dropped); SHIFT
    // appends (keeps them, converting the whole number to this type). A unit is in one
    // squad -- setting it here removes it from any other -- and a number is a group XOR a
    // formation, so re-typing a number moves every member onto the new type.
    void assignSquad(int num, int sign, bool append);

    // Recall squad `num` (group or formation): select its members, but NOT any builders in
    // it -- a builder rides in a squad only to feed its products in, not to be commanded
    // with the fighters.
    void recallSquad(int num);
    int lastRecalledSquad_ = 0; // consecutive number presses select, then track

    // Ctrl+Esc: drop the selected units from whatever squad each is in.
    void clearSquad();

    bool onScreen(const UnitR& u) const;

    // Cycle the selection to the next owned unit (single-select stepping).
    void cycleNextUnit();

    // Load the cursors during the LOADING phase. CursorSet::load does the hardware
    // reconstruction itself when the settings call for it; the only thing that matters
    // here is that it happens off the frame path. Without this the first
    // drawCursorOverlay() loads them, and at CURSOR SIZE 8 that is ~105 ms landing on
    // the first rendered frame.
    void warmCursors() {
        if (!cursorsInit_) { cursorsInit_ = true; cursors_.load(ren_, vfs_, settings_); }
    }
    void centerOn(int id);

    // Centre the camera on the average position of the live selected units.
    // Returns false if nothing in the selection is still alive.
    bool centerOnSelection();

    // The player's HUD accent — follows their chosen player colour, not faction.
    SDL_Color factionColor() const { return playerColor(localPlayer_); }

    // Is this player on the local player's team? (Allies share vision, so their
    // units render/appear on the minimap through fog just like your own.)
    bool alliedToLocal(int player) const { return worldTables().allied(player, localPlayer_); }

    // Plain interface text in the chosen UI FONT (client/uitext.h): `px` is the size of
    // one block-font cell pixel (caps 7 * px tall from y), and blockWidth is exactly
    // what blockText advances.
    void blockText(const std::string& s, float x, float y, float px, SDL_Color c) { tak::uiText(ren_, s, x, y, px, c); }
    float blockWidth(const std::string& s, float px) const { return tak::uiTextWidth(s, px); }

    // A top-of-screen status line (network error, notice, pending-order prompt).
    // These print coloured text straight over the terrain, where it can be nearly
    // unreadable against grass/rock -- so back every one with a dark rounded panel
    // first. Returns the y for a following line so stacked messages don't collide.
    // `leftX < 0` centres the text in `winW`; otherwise it's the left edge.
    float hudBanner(const std::string& msg, float y, float scale, SDL_Color col,
                    int winW, float leftX = -1);

    // ==================== interactive multiplayer lobby ====================

    static const char* factionName(int f) {
        static const char* n[5] = {"ARAMON", "TAROS", "VERUNA", "ZHON", "CREON"};
        return n[f % 5];
    }
    // Lobby design size (logical). The lobby always lays out at exactly this size and
    // is scaled to fit + centred in the window (lobbyScale_ / lobbyOffX_/Y_), so it
    // shows fully at any window size or aspect. Hit-tests undo the same transform.
    static constexpr float kLobbyW = 960.0f;
    static constexpr float kLobbyH = 540.0f;
    bool lbHot(const SDL_FRect& r) const;
    // A clickable button: panel + centered label; registers its action. The label is
    // drawn at labelPx (the cell size, uitext.h), or smaller when it would not fit the
    // box -- never larger: a list of names (the room's mod packs) passes the size of the
    // text around it rather than the 2.0 of a caps command button.
    static constexpr float kBtnLabelPx = 2.0f;
    void lbBtn(float x, float y, float w, float h, const std::string& label, bool enabled,
               std::function<void()> action, SDL_Color base = {60, 66, 86, 255},
               float labelPx = kBtnLabelPx);
    // Only selectors accept right-click; ordinary actions stay left-click only.
    void lbCycle(float x, float y, float w, float h, const std::string& label, bool enabled,
                 std::function<void(int)> cycle, float labelPx = kBtnLabelPx) {
        lbBtn(x, y, w, h, label, enabled, {}, {60, 66, 86, 255}, labelPx);
        if (enabled && cycle) lobbyHots_.emplace_back(SDL_FRect{x,y,w,h}, std::move(cycle));
    }
    // A text-input field: label + box; clicking activates it (id != 0).
    void lbField(float x, float y, float w, const std::string& label,
                 const std::string& value, int id);
    void colorSwatch(float x, float y, float s, int color, std::function<void(int)> action);

    void drawLobby(int winW, int winH);

    void drawBrowser(int winW, int winH);

    // Locate an 8-bit-indexed {u32 w, u32 h, w*h bytes} image at TNT header field
    // `field` (little-endian u32 pointers). Returns false (with bounds checks) if the
    // field is absent or malformed. Field 12 = the hi-res overview, 11 = the minimap.
    static bool tntIndexedImage(const std::vector<uint8_t>& d, int field,
                                int& w, int& h, const uint8_t*& data);

    // The map's "kingdom" (from its sibling .ota [GlobalHeader] kingdom=...), which
    // names the preview palette. The .ota carries the preview info too:
    // kingdom=<side>, size=<W x H>, numplayers=<N>, memory=<...>.
    std::string readOta(const std::string& tntPath) const;
    // Value of `key`=... in an .ota (up to ';' or EOL, trimmed). Keeps internal spaces
    // (so "size=9 x 7" -> "9 x 7"); matches only a whole-word key before its '='.
    static std::string otaField(const std::string& ota, const char* keyLower);

    // The 256-colour RGBA palette for a kingdom (palettes/<kingdom>.pcx), cached.
    // Returns nullptr if the kingdom is unknown / its .pcx is missing.
    const std::vector<uint8_t>* kingdomPalette(const std::string& kingdom);

    // Build the selected map's preview texture. Retail (KINGDOMS.icd, MapView gadget
    // at 0x4ae4f0) draws the .tnt's embedded RADARPIC through the map's KINGDOM palette
    // (palettes/<kingdom>.pcx named by the .ota [GlobalHeader] kingdom=) -- that's the
    // pale-parchment/pink-path look, not the terrain palette.pal. We colour the higher-
    // res BIGRADARPIC (overview, field 12; minimap field 11 is the fallback) through
    // that same kingdom palette, so it matches retail's style but crisper. Index 9 is
    // retail's transparent index. Rebuilt only on selection change.
    // Build the picker's map list once: names + paths from the VFS, enriched with
    // each map's player count and size read from its .ota GlobalHeader (a small text
    // file -- numplayers + "size = W x H" -- so no TNT decompression per map).
    void buildMapList(tak::LoadScreen* progress = nullptr);

    // Sort the map list by the active key (name / players / size), tie-broken by name,
    // in the chosen direction.
    void sortMapList();

    // `src` reads a map the VFS does not hold (a room's verified map package).
    void buildMapPreview(const std::string& tntPath, const tak::hpi::Vfs* src = nullptr);
    // Numbered start markers over a preview image drawn at `img` (the whole map):
    // marker k is the map's (k+1)-th start in StartPosN order (the order setupMatch
    // assigns), filled with player colour colorOfStart[k]
    // (the slot rows' palette), or neutral where that is -1 or missing.
    void drawStartMarkers(const SDL_FRect& img, const std::vector<int>& colorOfStart = {});
    // The room's map preview with its start markers, fitted into `box`. `authored`:
    // a scenario that places its own forces, whose starts no slot takes.
    void drawRoomPreview(const SDL_FRect& box, bool authored);
    void buildGenPreview(const std::string& id);   // thumbnail for a "~gen1~" map

    void drawCreate(int winW, int winH);

    void drawRoom(int winW, int winH);

    // The room's map scenario facts, read once per verified package (MAP SCENARIO, 259).
    std::weak_ptr<const tak::net::maps::Package> scenarioLobbyPackage_;
    tak::sim::ScenarioInfo scenarioLobby_;
    // `solo`: the room plays its map's scenario, which may be a one-player objective.
    // `needSeats`: the seats that scenario needs, each of which must be played.
    static bool startValid(const tak::net::RoomView& room, bool solo = false, uint8_t needSeats = 0) {
        int used = 0; bool color[10] = {};
        for (int i = 0; i < tak::net::kMaxSlots; ++i) {
            const auto& s = room.slots[i];
            if (s.type != 1 && s.type != 2) { if (needSeats >> i & 1) return false; continue; }
            ++used;
            if (s.type == 1 && !s.ready) return false;
            if (s.color < 10) { if (color[s.color]) return false; color[s.color] = true; }
        }
        return used >= (solo ? 1 : 2);
    }
    const tak::net::RoomView& mpRoom() const { return mp_->room(); }

    // START LOCATIONS: BY SLOT in the room: the room map's facts the slot table needs to
    // tag a seat that has no start of its own, read once per verified package.
    struct RoomStarts {
        int count = -1;          // start positions; -1 until the map is known
        bool placed = false;     // a Test Map scenario that places units: they decide
    };
    RoomStarts roomStarts();
    std::weak_ptr<const tak::net::maps::Package> roomStartsPackage_;
    RoomStarts roomStarts_;

    // A panel-local logical point, undoing the lobby's fit-scale + centre offset.
    void lobbyMouse(float& mx, float& my) const;
    static bool ptIn(const SDL_FRect& r, float x, float y) {
        return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
    }
    void clampMapScroll();
    // Set the map scroll from the current thumb-drag mouse position: map the cursor's
    // y within the list box to a first-visible-row index.
    void setMapScrollFromThumb();
    void lobbyInput(const SDL_Event& e, int winW, int winH);
    std::string* lbFieldBuf();

    // F4: player names, kills, losses, and score, top-left.
    // F7 diagnostic: tint elevated cells, and for each unit show its cell height,
    // computed lift, its RAW (unlifted) foot position (magenta dot) vs its LIFTED
    // foot position (cyan dot). Lets us see whether a unit that looks "on the wall"
    // is actually on a high heightmap cell or on flat ground beside painted relief.
    void drawHDebug();

    void drawUnitCounts(int winW);


    // The in-mission objectives panel (top-left): the mission's objectives, toggled
    // with the O key. Only shown for campaign missions (missionObjectives_ non-empty).
    void drawObjectivesPanel(int winW, int /*winH*/);

    void drawPanel(int winW, int winH);

    void drawFog();
    std::vector<SDL_Vertex> fogVerts_;
    std::vector<float> fogLift_;      // per-corner heightAbove scratch (see drawFog)
    uint32_t fogTexGen_ = ~0u;        // vis generation last uploaded to fogTex_
    // Fog relief geometry depends on the camera and terrain, not on time.
    // At distant zoom retain transparent cells too, so visibility only updates
    // the texture instead of rebuilding millions of vertices four times/sec.
    bool fogMeshValid_ = false;
    int fogGridX0_=0,fogGridZ0_=0,fogGridX1_=0,fogGridZ1_=0;
    uint32_t fogMeshGeneration_ = 0;
    int fogMeshW_ = 0, fogMeshH_ = 0, fogMeshWinW_ = 0, fogMeshWinH_ = 0;
    float fogMeshX_ = 0, fogMeshY_ = 0, fogMeshZoom_ = 0;

    // Positions along a build-drag line: cell-snapped footprints, buildLineGap_ apart
    // (client/buildline.h).
    std::vector<std::pair<float, float>> buildLinePositions(
        float x0, float z0, float x1, float z1) const;

    // Queue a whole line of the current building from a shift-drag.
    bool placeBuildLine(float x0, float z0, float x1, float z1);
    bool placeManaBuildBox(float x0,float z0,float x1,float z1,bool queue);

    void drawGhost();



    void voice(int unitId, const std::string& event);

    // --- GAF/TAF impact effects (gamedata/explosions -> data/anims/*.taf) ------
    // Legacy status-shot presentation, pending native status flight integration.
    // Ordinary shots, lightning and flames already use simulation projectiles.
    struct BeamFx {
        float x1 = 0, z1 = 0, x2 = 0, z2 = 0;
        float alt1 = 0, alt2 = 0;
        std::optional<std::array<int32_t,3>> fromPosition, position;
        std::string model;
        std::string sprite;
        int player = 0;
        // Cosmetic travel time; this does not delay the simulation's hit.
        float life = 0.16f;
        uint8_t inner[3] = {255, 255, 255};
        uint8_t middle[3] = {200, 230, 255};
        uint8_t outer[3] = {120, 170, 255};
        bool lightning = false;   // jagged bolt vs a clean beam
        float age = 0;
    };
    std::vector<BeamFx> beams_;
    // Live wandering storms by id, so a new one can announce itself and a
    // vanished one can leave its dissipation art behind.
    struct StormTrack { float x = 0, z = 0; const tak::sim::Weapon* w = nullptr; };
    std::unordered_map<int, StormTrack> stormsSeen_;
    struct EFrame { SDL_Texture* tex = nullptr; int w = 0, h = 0, ax = 0, ay = 0, ticks = 2; uint8_t encoding = 0; };
    struct EffectAnim { std::vector<EFrame> frames; std::vector<uint16_t> durations; bool loop=false; };
    std::vector<std::string> explosionClassOrder_; // numeric COB classes follow authored order
    std::map<std::string, std::vector<std::string>> explosionClasses_;  // class -> anim names
    std::map<std::string, EffectAnim> effectAnims_;                     // anim name -> frames
    struct NimbusEffect { const EffectAnim* anim = nullptr; uint32_t started = 0; };
    std::unordered_map<int, NimbusEffect> nimbusEffects_;
    std::map<std::string, std::string> factionNimbus_;
    bool factionNimbusLoaded_ = false;
    bool explosionsLoaded_ = false;
    struct EffectInst {
        const EffectAnim* anim = nullptr;
        float x = 0, z = 0, age = 0;
        float delay = 0;   // seconds before it starts playing
        float dur = 0;     // seconds for one playthrough (0 => use kEffectFps)
        int loops = 1;     // how many times to repeat (ground fire loops)
        float alt = 0;     // extra screen lift (impact on an airborne target)
        bool authoredTiming = false;
        uint32_t started = 0;
        std::optional<std::array<int32_t,3>> worldPosition = std::nullopt;
    };
    std::vector<EffectInst> effects_;
    struct ExplosionGlow {
        float x,z,alt; uint32_t started; unsigned kind;
        std::optional<std::array<int32_t,3>> worldPosition;
    };
    std::vector<ExplosionGlow> explosionGlows_;
    tak::RetailFlightAnimationQueue flightAnimationQueue_; // guarded by hitQueueMutex_
    tak::WeaponAnimationQueue weaponAnimationQueue_; // guarded by hitQueueMutex_
    std::deque<tak::sim::World::TransportFx> transportEffectQueue_; // guarded by hitQueueMutex_
    struct FeatureSmokeEmission {
        int id=0;
        std::string type;
        std::array<int32_t,3> position{};
        uint32_t age=0;
        uint64_t activationSequence=0;
        bool emit=true;
    };
    struct FeatureSmokeOwner {
        uint32_t burnStarted=0;
        uint64_t activationSequence=0;
    };
    struct SmokeTick {
        uint32_t tick=0;
        int32_t windX=0,windZ=0;
        std::vector<tak::sim::World::ScriptEmission> emissions;
        std::vector<int> removedOwners;
        std::vector<FeatureSmokeEmission> features;
        std::vector<int> removedFeatures;
    };
    struct SmokeSprite { tak::RetailSmokeParticle particle; const EffectAnim* art=nullptr; };
    std::deque<SmokeTick> smokeTickQueue_; // guarded by hitQueueMutex_
    std::map<int,FeatureSmokeOwner> featureSmokeOwners_;
    std::map<int,std::vector<SmokeSprite>> featureSmokeSprites_;
    std::set<int> smokeOwners_; // simulation-thread emission owners
    uint32_t smokeCaptureTick_=0;
    std::map<int,std::vector<SmokeSprite>> smokeSprites_;
    std::map<int,std::vector<tak::RetailPointParticle>> pointParticles_;
    struct DamageFlameSprite {
        std::array<int32_t,3> position{};
        const EffectAnim* art=nullptr;
        tak::sim::RetailEffectClock clock;
        int life=15;
    };
    std::map<int,std::vector<DamageFlameSprite>> damageFlames_;
    std::array<std::vector<const EffectAnim*>,3> damageFlameClasses_;
    bool damageFlameClassesLoaded_=false;
    void loadDamageFlameClasses();
    uint32_t smokeTick_=0,smokeRandom_=1;
    void consumeSmokeTicks();
    static constexpr float kEffectFps = 20.0f;

    // Per-loop playback length of an effect instance, in seconds.
    static float effLoopLen(const EffectInst& e) {
        return e.dur > 0 ? e.dur : float(e.anim->frames.size()) / kEffectFps;
    }
    // Play a named effect anim at (x,z), optionally delayed / stretched / looped.
    // `alt` lifts it on screen (impact on an airborne target).
    void spawnEffectAnim(const std::string& anim, float x, float z,
                         float delay = 0, float dur = 0, int loops = 1, float alt = 0,
                         bool authoredTiming = false,
                         std::optional<std::array<int32_t,3>> worldPosition = {},
                         std::optional<uint32_t> started = {}) {
        const EffectAnim* ea = effectFor(anim);
        if (ea) effects_.push_back({ea, x, z, 0.0f, delay, dur, loops, alt,
                                   authoredTiming, started.value_or(front().gameTick), worldPosition});
    }

    void loadExplosionClasses();
    // Route a resolved weapon hit through its authored land/water explosion class,
    // Missing classes/art remain silent, as in the native impact dispatcher.
    void spawnWeaponImpact(const tak::sim::Weapon& weapon, float x, float z, float alt = 0,
                           std::optional<std::array<int32_t,3>> position = {}, bool struckUnit = false);
    // Load a named effect animation from its TAF/GAF (truecolor _4444 preferred).
    const EffectAnim* effectFor(const std::string& animName);
    // Play the named explosion class (a random variant) at (x,z). Returns false
    // if the class/art is unavailable; absence does not request substitute art.
    bool spawnEffect(const std::string& cls, float x, float z, float alt = 0);
    void updateEffects(float dt);
    // A shockwave ring: `sprites` copies of an effect anim arranged around a
    // circle that expands from the centre to maxR over `dur` (TAK radiusart).
    struct RingFx {
        const EffectAnim* anim = nullptr;
        float x = 0, z = 0, age = 0, delay = 0, dur = 1.2f, maxR = 120;
        int sprites = 24;
    };
    // WindChange delivery follows the published simulation generation.
    float windHeading_ = 0, windSpeed_ = 0;
    uint32_t windGen_ = 0;
    // Camera shake (weapon shakemagnitude/shakeduration on heavy impacts).
    float shakeTime_ = 0, shakeDur_ = 0, shakeMag_ = 0;
    bool screenShake_ = false;   // Options: SCREEN SHAKE (default off)
    void triggerShake(float mag, float dur);

    std::vector<RingFx> rings_;
    void spawnRing(const std::string& anim, float x, float z, float delay,
                   float dur, int sprites, float maxR);
    void updateRings(float dt);
    void drawRings();

    void drawEffects();
    // Frame a nimbus shows at the current tick (frames.size() once it has played out),
    // and the lifetime rule drawEffects applies, for frames that skip drawing it.
    size_t nimbusFrame(const NimbusEffect& effect) const;
    void pruneNimbusEffects();

    // Persistent per-unit ambient fire/smoke (emit-sfx). One looping flame/smoke per
    // unit, cycled off the continuous animClock so it flows smoothly (no restart/gaps
    // like re-spawned one-shots), kept alive while the unit's emit-loop keeps firing.
    void drawUnitFx();

    // Procedural particle for impact explosions, flame, and blood spray.
    struct Particle {
        float x = 0, z = 0, vx = 0, vz = 0, alt = 0, valt = 0;
        float life = 0, maxLife = 1, size = 3;
        Uint8 r = 255, g = 200, b = 80;
        int kind = 0;   // 0 spark/blood (gravity), 1 smoke (rises, fades)
    };
    std::vector<Particle> particles_;
    struct DetachedPiece {
        tak::tdo::Object model;
        std::vector<std::string> names;
        std::vector<tak::cob::PieceState> poses;
        tak::RetailDebrisMotion motion;
        std::vector<tak::RetailBloodParticle> blood;
        uint32_t tick=0;
        int player=0;
    };
    void buildDetachedShadow(const DetachedPiece& debris, float zoom);
    void drawDetachedShadow();
    std::vector<SDL_FPoint> detachedShadowVerts_;
    std::vector<Tri> detachedMaskedShadows_;
    std::vector<DetachedPiece> detachedPieces_;
    bool debrisLavaWorld_=false,debrisNoSeaTrigger_=false;
    std::deque<tak::RetailBloodParticle> bloodStains_;
    // Spawn a burst of `n` particles at (x,z) with a colour and speed spread.
    void spawnBurst(float x, float z, int n, Uint8 r, Uint8 g, Uint8 b,
                    float spread, float sizeMax, int kind, float baseAlt = 0);
    void updateParticles(float dt);
    void drawParticles();
    std::vector<SDL_Vertex> partBatch_;   // reused particle-quad batch

    SoundBank sounds_;
    // Worker pool for the per-unit VM ticks and the renderer's parallel passes:
    // one thread per CPU this process may use (debug TAK_RENDER_POOL_THREADS=N
    // overrides it, e.g. 1 for a serial reference run).
    static unsigned renderPoolThreads() {
        if (const char* v = tak::devEnv("TAK_RENDER_POOL_THREADS"); v && std::atoi(v) > 0)
            return unsigned(std::atoi(v));
        return ThreadPool::defaultThreads();
    }
    ThreadPool pool_{renderPoolThreads()};
    // One scratch per pool participant (see WorkerScratch); cache-line separated.
    std::vector<std::unique_ptr<WorkerScratch>> workerScratch_ = [this] {
        std::vector<std::unique_ptr<WorkerScratch>> slots(pool_.threads());
        for (auto& slot : slots) slot = std::make_unique<WorkerScratch>();
        return slots;
    }();
#ifndef NDEBUG
    std::thread::id renderThread_ = std::this_thread::get_id();   // owner of scratch slot 0
#endif
    // Retained renderer buffers trimmed after sustained low demand (workstream J,
    // client/cpubudget.h). Render thread only. noteNeed() records a frame's peak use
    // of a buffer that is refilled more than once per frame, before each refill.
    enum class Retained : uint8_t {
        BodyVerts, PaintItems, DrawOps, CopyTasks, DistantItems, BodySources, BodyRanges,
        ArenaFirsts, ArenaCounts, ArenaRanges, VisUnits, VisGeom, VisCost, GeomIndex, AirShadows,
        ShadowPrepItems, ShadowOpaque, ShadowComposite, ShadowMask, ShadowTileStamps,
        ShadowTileBefore, ShadowTileHasBefore, ShadowTileChecks, ShadowTileNeeds, ShadowCellStamps,
        ShadowCellStampValid, ModelTris, ModelTriBatch, Count
    };
    struct RetainedSlot {
        tak::render::TrimTracker tracker;
        size_t peak = 0;
        uint64_t lastTrimFrame = 0; size_t trimmedTo = 0;   // TAK_CPU_TRIM_VERIFY
    };
    // TAK_CPU_TRIM_VERIFY (debug): every trimmed buffer kept its contents byte for
    // byte, no buffer was trimmed twice within one policy window, regrowths after a
    // trim are counted; retained bytes = the renderer's CPU buffers + caches.
    struct CpuTrimVerifyTotals {
        uint64_t frames=0, trims=0, workerTrims=0, released=0, contentChecks=0, regrowths=0;
        uint64_t peakRetained=0, finalRetained=0;
    };
    CpuTrimVerifyTotals cpuTrimVerify_{};
    std::array<RetainedSlot, size_t(Retained::Count)> retained_{};
    std::vector<tak::render::TrimTracker> workerTrim_ = std::vector<tak::render::TrimTracker>(pool_.threads());
    tak::render::TrimPolicy trimPolicy_ = [] {
        tak::render::TrimPolicy p;
        // Debug: TAK_CPU_TRIM_MS=N / TAK_CPU_TRIM_FRAMES=N shorten the window (tests).
        if (const char* v = tak::devEnv("TAK_CPU_TRIM_MS")) p.windowMs = uint64_t(std::max(1, std::atoi(v)));
        if (const char* v = tak::devEnv("TAK_CPU_TRIM_FRAMES")) p.windowFrames = uint32_t(std::max(2, std::atoi(v)));
        if (const char* v = tak::devEnv("TAK_CPU_TRIM_FLOOR_KIB")) p.floorBytes = size_t(std::max(0, std::atoi(v))) << 10;
        return p;
    }();
    uint64_t trimFrame_ = 0;
    uint64_t trimsThisFrame_ = 0, trimBytesThisFrame_ = 0;   // telemetry, folded per frame
    size_t cpuPeakBytes_ = 0;
    void noteNeed(Retained b, size_t bytes) {
        auto& slot = retained_[size_t(b)];
        slot.peak = std::max(slot.peak, bytes);
    }
    template <class V> void noteNeed(Retained b, const V& v) { noteNeed(b, v.size() * sizeof(v[0])); }
    // Frame end (render thread, no pass running): trim what sustained low demand left
    // oversized -- the retained buffers above and the worker scratch slots.
    void trimRetainedBuffers();
    std::vector<std::pair<int,tak::cob::Vm*>> explosionVmTick_;
    std::vector<tak::cob::Vm*> vmTick_;     // scratch list for the parallel pass
    SoundClasses soundClasses_;
    uint32_t salt_ = 0;
    uint64_t resultStartedMs_ = 0;   // main-thread wall clock, independent of sim speed/pause
    std::atomic<int> outcome_{0};   // 0 = playing, 1 = victory, -1 = defeat (worker writes, main reads)
    // TAK_FAKE_DESYNC=TICK: report a wrong hash from this tick on (see reportedHash).
    // Debug-only -- devEnv reads no environment at all in a release build, so this is
    // constant 0 there and the branch folds away.
    uint32_t fakeDesyncTick_ = [] {
        const char* e = tak::devEnv("TAK_FAKE_DESYNC");
        return e ? uint32_t(std::strtoul(e, nullptr, 0)) : 0u;
    }();
    // Legion gait pacing instruments (gaitpace.h). TAK_GAIT_OFF=1 holds every pace at 1
    // (the before picture, same sim); TAK_GAIT_LOG=path writes one CSV row per ground
    // mover per tick: position, covered distance, maximum, GET 29, refusal and pace. Debug-only:
    // devEnv/devFlag read nothing in a release build.
    const bool gaitPaceOff_ = tak::devFlag("TAK_GAIT_OFF");
    std::FILE* gaitLog_ = [] {
        const char* path = tak::devEnv("TAK_GAIT_LOG");
        std::FILE* log = path ? std::fopen(path, "w") : nullptr;
        if (log) std::fprintf(log, "tick,id,type,x,z,dist,max,get29,refused,gait\n");
        return log;
    }();
    bool sawTeam_[tak::sim::kMaxPlayers] = {};   // teams that have ever fielded a unit
    // Dev-only N-player free-for-all / teams harness (TAK_FFA=N[,teams]); the
    // real lobby (multiplayer M3) replaces it. When >0, an AI Controller drives
    // every player, not just player 1.
#ifndef NDEBUG
    int ffaPlayers_ = 0;
    bool amphib_ = false;
    int amphibPhase_ = 0, amphibSquad_ = 0, transportId_ = -1;
    float amphibLandX_ = 0, amphibLandZ_ = 0, amphibSeaX_ = 0, amphibSeaZ_ = 0;
#endif
    Font hudFont_, bigFont_, statFont_, scoreboardFont_;
    std::unordered_map<std::string, SDL_Texture*> scoreboardLogos_;
    std::string scoreboardSides_[8];

    std::string notice_;
    float noticeTimer_ = 0;
    float animClock_ = 0;
};
