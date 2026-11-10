#include "util/virtualpath.h"
#include "client/mapfilter.h"
#include "client/gameview.h"
#include "net/matchconfig.h"
#include "client/startmarkers.h"
#include <cctype>
#include <set>
#include <cstring>
#include <cmath>
#include "util/u8path.h"

// Out-of-line GameView method definitions (lobby concern), split from the
// class body in gameview.h so editing a body recompiles only this translation
// unit. Trivial getters, ctors, static, template, constexpr and default-arg
// methods stay inline in the header. Grouping is by name heuristic.

    bool GameView::inLobbyPhase() const {
        if (!mp_ || mpSetupDone_) return false;
        auto s = mp_->state();
        return s == tak::net::MpClient::State::Connecting ||
               s == tak::net::MpClient::State::Lobby ||
               s == tak::net::MpClient::State::InRoom ||
               s == tak::net::MpClient::State::Done;
    }

    int GameView::geomSlot(int id) const {
        const int slot=front().slot(id);
        return slot>=0 && size_t(slot)<geomIndex_.size() ? geomIndex_[size_t(slot)] : -1;
    }

namespace {
std::string pathfindingLabel(tak::sim::PathfindingMode mode) {
    std::string name=tak::sim::pathfindingModeName(mode);
    for(char& c:name)c=char(std::toupper(static_cast<unsigned char>(c)));
    return "PATHFINDING: "+name;
}
// START LOCATIONS as the create screen and the room show it (GameOptions::randomStarts).
std::string startLocationsLabel(uint8_t mode) {
    switch (tak::net::normalizeStartMode(mode)) {
    case tak::net::kStartsRandom: return "START LOCATIONS: RANDOM";
    case tak::net::kStartsBySlot: return "START LOCATIONS: BY SLOT";
    default: return "START LOCATIONS: FIXED";
    }
}
const char* const kFogName[3] = {"NOT EXPLORED", "EXPLORED", "FULL VISION"};
// The create screen's option column draws every label at one size: the largest at
// which every caption any row can show (every state of every row, single player and
// multiplayer) fits `maxW`. The column then reads as one list, no label needs its own
// shrink, and cycling a row never resizes its text. Measured in the current UI FONT.
float createOptionPx(float maxW) {
    static const char* const kCaptions[] = {
        "CRUSADES: ON", "CRUSADES: OFF", "DOUBLE SIGHT/RADAR: ON", "DOUBLE SIGHT/RADAR: OFF",
        "UNIT CAP: 250", "UNIT CAP: 500", "UNIT CAP: 1000", "UNIT CAP: 2000",
        "MONARCH EXPENDABLE: SCENARIO", "MONARCH EXPENDABLE: ON", "MONARCH EXPENDABLE: OFF",
        "ALLOW SPEED CHANGE: ON", "ALLOW SPEED CHANGE: OFF", "POWER CODES: ON", "POWER CODES: OFF",
        "SPECTATE (WATCH AIS): ON", "SPECTATE (WATCH AIS): OFF",
        "ALLOW SPECTATORS: ON", "ALLOW SPECTATORS: OFF"};
    float px = tak::uiTextFitPx(pathfindingLabel(tak::sim::PathfindingMode::Retail), 2.0f, maxW);
    px = tak::uiTextFitPx(pathfindingLabel(tak::sim::PathfindingMode::Legion), px, maxW);
    for (uint8_t mode = 0; mode < 3; ++mode) px = tak::uiTextFitPx(startLocationsLabel(mode), px, maxW);
    for (const char* fog : kFogName) px = tak::uiTextFitPx(std::string("FOG OF WAR: ") + fog, px, maxW);
    for (const char* caption : kCaptions) px = tak::uiTextFitPx(caption, px, maxW);
    return std::floor(px * 20) / 20;   // a 0.05 step, so rounding never re-overflows
}
}  // namespace

    void GameView::drawLobby(int winW, int winH) {
        pollGenPreview();   // a random map's preview rolled on its worker
        lobbyHots_.clear();
        // Map-picker geometry is only live while the create screen is shown; clear it
        // so a stale thumb/list rect can't grab clicks or wheel on the other screens.
        mapListRect_ = mapThumbRect_ = chatRect_ = SDL_FRect{0, 0, 0, 0};
        for (auto& rect : genSliderRect_) rect = {};
        // absorb any new chat
        if (mp_) for (auto& m : mp_->takeChat()) chatLog_.push_back(m);
        // The ground + centred panel frame are painted by the caller (the viewport is
        // already offset to this panel); RenderClear would ignore the viewport.
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        float cx = winW / 2.0f;
        // Titled as the door that opens it (PlayComputer's and PlayPlayer's captions).
        const char* title = modsScreen_ && mp_ && mp_->state() == tak::net::MpClient::State::InRoom ? "MODS"
                          : singlePlayer_ ? "PLAY THE MACHINE" : "PLAY AN OPPONENT";
        blockText(title, cx - blockWidth(title, 2.6f) / 2, 24, 2.6f, {210, 200, 150, 255});
        if (!mp_) return;
        auto st = mp_->state();
        if (st == tak::net::MpClient::State::Connecting) {
            blockText("CONNECTING TO SERVER...", cx - 120, winH / 2.0f, 2.0f, {200, 200, 210, 255});
            return;
        }
        if (st == tak::net::MpClient::State::Done) {
            // Connection over (server closed / rejected us / timed out): say WHY --
            // MpClient preserves the server's Reject reason (e.g. "protocol version
            // mismatch (server 28 / client 29)") -- and give the player a way OUT.
            float my = winH / 2.0f - 40;
            std::string why = !lanJoinError_.empty() ? lanJoinError_ : lanHostGone() ? std::string("the host closed the game")
                            : mp_->error().empty() ? std::string("the server closed the connection")
                                                   : mp_->error();
            const char* head = !lanJoinError_.empty() ? "CANNOT JOIN"
                             : why == "the host closed the game" ? "GAME CLOSED" : "DISCONNECTED";
            blockText(head, cx - blockWidth(head, 2.6f) / 2, my, 2.6f, {255, 130, 110, 255});
            // Long reasons (version-mismatch text) wrap onto two centred lines.
            auto rows = tak::uiTextWrap(why, 1.8f, 600);
            const std::string l1 = rows[0];
            std::string l2;
            for (size_t k = 1; k < rows.size(); ++k) l2 += (k > 1 ? " " : "") + rows[k];
            l2 = tak::uiTextFit(l2, 1.8f, 600);
            blockText(l1, cx - blockWidth(l1, 1.8f) / 2, my + 34, 1.8f, {215, 218, 228, 255});
            if (!l2.empty())
                blockText(l2, cx - blockWidth(l2, 1.8f) / 2, my + 56, 1.8f, {215, 218, 228, 255});
            // A LAN game's BACK returns to the CONNECT screen (main reopens it).
            lbBtn(cx - 90, my + (l2.empty() ? 78.0f : 96.0f), 180, 32, mp_->lanServer() ? "BACK" : "BACK TO MENU", true,
                  [this] { menuRequested_ = true; });
            return;
        }
        // Someone else's LOCAL SERVER: into its open game, never the game list.
        if (st == tak::net::MpClient::State::Lobby && mp_->lanServer() && !lanHost_) {
            lanJoinStep();
            if (mp_->state() == tak::net::MpClient::State::Lobby) {
                const char* note = lanWaitingStart_ ? "WAITING FOR THE GAME TO START..." : "JOINING THE GAME...";
                blockText(note, cx - blockWidth(note, 2.0f) / 2, winH / 2.0f, 2.0f, {200, 200, 210, 255});
            }
            return;
        }
        if (st != tak::net::MpClient::State::InRoom) modsScreen_ = false;   // left the room: back to it next time
        if (st != tak::net::MpClient::State::InRoom) lanLeavePrompt_ = false;
        if (st == tak::net::MpClient::State::InRoom) {
            if (modsScreen_) drawModsScreen(winW, winH); else drawRoom(winW, winH);
            if (lanLeavePrompt_) drawLanLeavePrompt(winW, winH);
        }
        else if (lobbyScreen_ == LobbyScreen::Create) drawCreate(winW, winH);
        else drawBrowser(winW, winH);
    }

    // The other humans seated in the room: who a LOCAL SERVER host's LEAVE turns out.
    int GameView::lanOthersInRoom() const {
        const auto& room = mp_->room();
        int n = 0;
        for (int i = 0; i < tak::net::kMaxSlots; ++i) n += i != room.mySlot && room.slots[i].type == 1;
        return n;
    }

    void GameView::leaveRoom() {
        mp_->leaveGame();
        lobbyScreen_ = singlePlayer_ || lanHost_ ? LobbyScreen::Create : LobbyScreen::Browser;
        spSpectate_ = false;
        mpReadied_ = false; mpStarted_ = false; specAutoSeated_ = false;
        lanLeavePrompt_ = false;
    }

    // LEAVE WILL CLOSE THE GAME FOR N PLAYERS: YES leaves (the server closes the room for
    // everyone), NO stays. Over the room, which takes no clicks while it is open.
    void GameView::drawLanLeavePrompt(int winW, int winH) {
        lobbyHots_.clear();
        SDL_FRect dim{0, 0, float(winW), float(winH)};
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 160); SDL_RenderFillRectF(ren_, &dim);
        const float pw = 520, ph = 150, px = (winW - pw) / 2, py = (winH - ph) / 2;
        SDL_FRect panel{px, py, pw, ph};
        SDL_SetRenderDrawColor(ren_, 28, 30, 40, 250); SDL_RenderFillRectF(ren_, &panel);
        SDL_SetRenderDrawColor(ren_, 190, 160, 110, 255); SDL_RenderDrawRectF(ren_, &panel);
        const int n = lanOthersInRoom();
        const std::string line = "LEAVE WILL CLOSE THE GAME FOR " + std::to_string(n) + (n == 1 ? " PLAYER." : " PLAYERS.");
        blockText(line, px + (pw - blockWidth(line, 1.8f)) / 2, py + 30, 1.8f, {225, 215, 170, 255});
        blockText("LEAVE?", px + (pw - blockWidth("LEAVE?", 1.8f)) / 2, py + 58, 1.8f, {225, 215, 170, 255});
        lbBtn(px + pw / 2 - 140, py + ph - 50, 120, 30, "YES", true, [this] { leaveRoom(); });
        lbBtn(px + pw / 2 + 20, py + ph - 50, 120, 30, "NO", true, [this] { lanLeavePrompt_ = false; });
    }

    void GameView::lanJoinStep() {
        const uint64_t now = SDL_GetTicks64();
        auto fail = [&](std::string why) { lanJoinError_ = std::move(why); mp_->disconnect(lanJoinError_); };
        if (lanJoinSent_) {   // asked: in the room by now, or the server said why not
            if (!mp_->joinError().empty()) fail(mp_->joinError());
            return;
        }
        if (!lanJoinAtMs_) lanJoinAtMs_ = now;
        if (now - mpListMs_ > 1000) { mp_->listGames(); mpListMs_ = now; }
        bool started = false;
        lanWaitingStart_ = false;
        for (const auto& g : mp_->games()) {
            if (lanSpectate_) {   // watching: a started game now, a waiting one from its start
                if (!g.running) { lanWaitingStart_ = true; continue; }
                mp_->spectate(g.id, joinPass_);
                lanJoinSent_ = true;
                return;
            }
            if (g.running) { started = true; continue; }
            mp_->joinGame(g.id, joinPass_);
            lanJoinSent_ = true;
            return;
        }
        if (lanWaitingStart_) return;
        if (started) fail("the game on this server has already started -- SPECTATE to watch it");
        else if (now - lanJoinAtMs_ > 5000) fail("the host has not opened a game on this server yet");
    }

    void GameView::drawBrowser(int winW, int winH) {
        (void)winH;
        // keep the list fresh without needing a manual refresh
        if (SDL_GetTicks64() - mpListMs_ > 1000) { mp_->listGames(); mpListMs_ = SDL_GetTicks64(); }
        float x = 60, y = 80, w = winW - 120.0f;
        blockText("GAMES", x, y, 2.2f, {200, 205, 220, 255});
        lbBtn(x + w - 200, y - 6, 95, 26, "REFRESH", true, [this] { mp_->listGames(); });
        lbBtn(x + w - 100, y - 6, 100, 26, "CREATE", true,
              [this] { lobbyScreen_ = LobbyScreen::Create; mapListScanned_ = false; });
        y += 34;
        // column header
        blockText("NAME", x + 8, y, 1.6f, {130, 135, 150, 255});
        blockText("MAP", x + 220, y, 1.6f, {130, 135, 150, 255});
        blockText("PLAYERS", x + w - 200, y, 1.6f, {130, 135, 150, 255});
        y += 20;
        const auto& games = mp_->games();
        if (games.empty())
            blockText("NO GAMES -- CREATE ONE, OR REFRESH", x + 8, y + 10, 1.8f, {150, 150, 160, 255});
        for (const auto& g : games) {
            SDL_FRect row{x, y, w, 30};
            bool hot = lbHot(row) && !g.running;
            SDL_SetRenderDrawColor(ren_, hot ? 46 : 30, hot ? 52 : 34, hot ? 72 : 44, 255);
            SDL_RenderFillRectF(ren_, &row);
            blockText(g.name, x + 8, y + 8, 1.8f, {225, 228, 236, 255});
            blockText(mapTitle(g.mapId), x + 220, y + 8, 1.6f, {180, 185, 195, 255});
            char pc[32]; std::snprintf(pc, sizeof pc, "%d/%d", g.players, g.capacity);
            blockText(pc, x + w - 200, y + 8, 1.8f, {200, 205, 215, 255});
            if (g.passworded)
                blockText("LOCK", x + w - (g.running ? 232 : 120), y + 8, 1.6f, {230, 200, 120, 255});
            if (g.running) {
                // A running game can't be joined, but it can be watched live -- unless
                // its host turned ALLOW SPECTATORS off. Then the button says so (muted
                // red) and a click still asks: the server is the authority, and its
                // refusal puts the reason on the CANNOT JOIN line below.
                blockText("LIVE", x + w - 158, y + 8, 1.6f, {230, 140, 120, 255});
                lbBtn(x + w - 96, y + 3, 92, 24, g.allowSpectators ? "WATCH" : "NO WATCH", true,
                      [this, id = g.id] { mp_->spectate(id, joinPass_); },
                      g.allowSpectators ? SDL_Color{60, 66, 86, 255} : SDL_Color{86, 52, 52, 255});
            } else {
                lobbyHots_.push_back({row, [this, id = g.id] { mp_->joinGame(id, joinPass_); }});
            }
            y += 34;
        }
        // Why the server turned the last JOIN / WATCH away (wrong password, game full,
        // spectators not allowed...) -- until the next request clears it.
        if (!mp_->joinError().empty()) {
            // The server's reason in its own case (as the CONNECT dialog shows server text).
            const std::string why = "CANNOT JOIN: " + mp_->joinError();
            blockText(tak::uiTextFit(why, 1.6f, winW - 2 * x), x, winH - 98.0f, 1.6f, {255, 130, 110, 255});
        }
        // password entry for locked games
        lbField(x, winH - 70.0f, 200, "PASSWORD (FOR LOCKED GAMES)", joinPass_, 3);
        lbBtn(winW - 140.0f, winH - 40.0f, 120, 30, "BACK", true, [this] { menuRequested_ = true; });
    }

    void GameView::buildMapList(tak::LoadScreen* progress) {
        if (progress) progress->step("Finding maps",70);
        resetMinimap(); mapView_.quiesce(); stopPreviewReaders();
        vfs_.refreshMapCache(tak::hpi::userMapRoot());
        mapList_.clear();
        mapListScanned_ = true;
        mapTitlesLoaded_ = false;   // the install may have changed: read the tables again
        auto maps=tak::hpi::listMaps(vfs_);
        // A per-user MapCache copy ("name [16 hex]": a map received from a server, or a
        // Cartographer Test Map snapshot) is left out while an installed map has the same
        // name: those are older or other versions of a map the player has. A copy of a map
        // the player does not have stays. Joining and Test Map pick the copy by its exact
        // name, so they are unaffected.
        {
            const auto lower = [](std::string t) {
                for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
                return t;
            };
            const auto cacheBase = [](const std::string& n) -> std::string {
                if (n.size() > 19 && n.compare(n.size() - 19, 2, " [") == 0 && n.back() == ']' &&
                    std::all_of(n.end() - 17, n.end() - 1, [](char c) { return std::isxdigit((unsigned char)c) != 0; }))
                    return n.substr(0, n.size() - 19);
                return {};
            };
            std::set<std::string> installed;
            for (const auto& [name, path] : maps)
                if (cacheBase(name).empty()) installed.insert(lower(name));
            std::erase_if(maps, [&](const auto& entry) {
                const std::string base = cacheBase(entry.first);
                return !base.empty() && installed.count(lower(base));
            });
        }
        size_t completed=0;
        uint64_t lastProgress=0;
        for (const auto& [name, path] : maps) {
            MapInfo mi; mi.name = name; mi.path = path;
            std::string missionName;
            std::string otaPath = tak::vpath::replaceExtension(path,".ota");
            if (vfs_.has(otaPath)) {
                try {
                    auto b = vfs_.read(otaPath);
                    auto root = tak::tdf::parseText(std::string(b.begin(), b.end()), otaPath);
                    const auto* gh = root.child("globalheader");
                    if (gh) {
                        missionName = gh->valueOr("missionname", "");
                        mi.players = int(gh->numberOr("numplayers", 0));
                        std::string sz = gh->valueOr("size", "");   // e.g. "16 x 16"
                        int a = 0, c = 0;
                        if (std::sscanf(sz.c_str(), "%d x %d", &a, &c) == 2) { mi.sizeW = a; mi.sizeH = c; }
                    }
                    // MAP SCENARIO: only a map that declares a scenario (or a Test Map copy)
                    // can have one, so the CRT of every other map is never read here.
                    if ((gh && gh->numberOr("hasscenario", 0) != 0) || root.child("takplaytest"))
                        mi.scenario = mapScenarioInfo(path);
                } catch (const std::exception&) {}
            }
            if (mi.players == 0)   // .ota had no numplayers: count the start positions
                mi.players = int(tak::sim::parseStartPositions(vfs_, path).size());
            mi.title = mapTitleFor(name, missionName);
            mapList_.push_back(std::move(mi));
            ++completed;
            if (progress && (completed==maps.size() || SDL_GetTicks64()-lastProgress>=100)) {
                progress->step("Reading maps "+std::to_string(completed)+" / "+std::to_string(maps.size()),
                               70+int(28*completed/maps.size()));
                lastProgress=SDL_GetTicks64();
            }
        }
        sortMapList();
    }

    void GameView::loadMapTitles() {
        mapTitlesLoaded_ = true;
        mapTitles_.clear();
        for (const char* path : {"translate/maps.tdf", "translate/ipmaps.tdf"}) {
            if (!vfs_.has(path)) continue;
            try {
                const auto b = vfs_.read(path);
                const auto root = tak::tdf::parseText(std::string(b.begin(), b.end()), path);
                for (const auto& [key, node] : root.children) {
                    const std::string* en = node.value("english");
                    if (!en || en->empty()) continue;
                    std::string k = key;
                    std::transform(k.begin(), k.end(), k.begin(), ::tolower);
                    mapTitles_.emplace(std::move(k), *en);
                }
            } catch (const std::exception&) {}
        }
    }

    // The title of a map file: retail's translated title when the tables name it (only
    // when it is the same words -- a case change, never a different map's title), else
    // the .ota's missionname under the same rule, else the name as the file spells it.
    // A received map's cached copy ("ulasem arena [1e9dfbbdc674b352]") keeps its tag.
    std::string GameView::mapTitleFor(const std::string& name, const std::string& missionName) {
        if (!mapTitlesLoaded_) loadMapTitles();
        auto lower = [](std::string t) { std::transform(t.begin(), t.end(), t.begin(), ::tolower); return t; };
        std::string base = name, tag;
        if (name.size() > 19 && name.compare(name.size() - 19, 2, " [") == 0 && name.back() == ']' &&
            std::all_of(name.end() - 17, name.end() - 1, [](char c) { return std::isxdigit((unsigned char)c) != 0; })) {
            base = name.substr(0, name.size() - 19); tag = name.substr(name.size() - 19);
        }
        const std::string key = lower(base);
        if (auto it = mapTitles_.find(key); it != mapTitles_.end() && lower(it->second) == key) return it->second + tag;
        if (!missionName.empty() && lower(missionName) == key) return missionName + tag;
        return name;
    }

    // What to SHOW for a map id. A generated map's id is a "~gen1~<hex>" recipe -- the
    // whole parameter block in hex -- which is unreadable in a lobby row: it shows the
    // label mapgen provides ("Random 16x16 4P Aramon"). A real map's id is its name: the
    // map list's title for it when the list is built, else the translate tables' title.
    std::string GameView::mapTitle(const std::string& id) {
        if (tak::mapgen::isGeneratedMapId(id)) return tak::mapgen::friendlyLabel(tak::mapgen::decodeMapId(id));
        for (const MapInfo& m : mapList_)
            if (m.name == id) return m.title.empty() ? m.name : m.title;
        return mapTitleFor(id);
    }

    void GameView::sortMapList() {
        int dir = mapSortDir_, key = mapSort_;
        auto lname = [](const std::string& s) {
            std::string t = s; std::transform(t.begin(), t.end(), t.begin(), ::tolower); return t;
        };
        std::stable_sort(mapList_.begin(), mapList_.end(),
                         [&](const MapInfo& a, const MapInfo& b) {
            long long c = 0;
            if (key == 1) c = a.players - b.players;
            else if (key == 2) c = a.area() - b.area();
            if (c == 0) {   // tiebreak (and the whole order for NAME) by name A->Z
                std::string na = lname(a.name), nb = lname(b.name);
                if (na != nb) return dir > 0 ? na < nb : na > nb;
                return false;
            }
            return dir > 0 ? c < 0 : c > 0;
        });
    }

    void GameView::buildMapPreview(const std::string& tntPath, const tak::hpi::Vfs* src) {
        const bool gen = tak::mapgen::isGeneratedMapId(tntPath);
        ++genSeq_;   // any newer preview supersedes a roll still running
        genPreviewPending_ = false;
        mapPreviewFor_ = tntPath;
        // Another random map keeps the last one's picture until its own is rolled.
        if (!(gen && mapPreviewIsGen_ && mapPreviewTex_)) {
            if (mapPreviewTex_) { gpuvram::destroy(mapPreviewTex_); mapPreviewTex_ = nullptr; }
            mapPreviewW_ = mapPreviewH_ = 0;
            mapPreviewDims_.clear();
            mapPreviewStarts_.clear();
            mapPreviewWorldW_ = mapPreviewWorldH_ = 0;
        }
        mapPreviewIsGen_ = gen;
        if (tntPath.empty()) return;
        genPreviewError_.clear();
        if (gen) {
            // Done with old rolls: drop them (a running one finishes on its own and
            // its result is ignored; its future would block here, so it stays listed --
            // it still reads vfs_, and stopPreviewReaders waits for it like the newest).
            std::erase_if(genJobs_, [](auto& job) {
                return job.second.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            });
            genJobs_.emplace_back(genSeq_, std::async(std::launch::async, [this, id = tntPath] {
                return rollGenPreview(id, vfs_);
            }));
            genPreviewPending_ = true;
            return;
        }
        const tak::hpi::Vfs& from = src ? *src : vfs_;
        std::vector<uint8_t> d;
        try { d = from.read(tntPath); } catch (...) { return; }
        if (d.size() < 52) return;
        auto u32 = [&](size_t o) { return uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8)
                 | (uint32_t(d[o + 2]) << 16) | (uint32_t(d[o + 3]) << 24); };
        // The radar pictures cover the whole map (16 px cells), so a start's world
        // position scales straight onto the drawn image.
        mapPreviewStarts_ = tak::sim::parseStartPositions(from, tntPath);
        mapPreviewWorldW_ = float(u32(4)) * 16; mapPreviewWorldH_ = float(u32(8)) * 16;
        int w = 0, h = 0; const uint8_t* idx = nullptr;
        if (!tntIndexedImage(d, 12, w, h, idx) && !tntIndexedImage(d, 11, w, h, idx)) return;
        std::string ota;
        if (!src) ota = readOta(tntPath);
        else if (auto o = src->tryRead(tak::vpath::replaceExtension(tntPath, ".ota"))) ota.assign(o->begin(), o->end());
        const std::vector<uint8_t>* pal = kingdomPalette(otaField(ota, "kingdom"));
        if (!pal) pal = kingdomPalette("aramon");   // maps without a kingdom get a default
        if (!pal) return;
        std::vector<uint8_t> rgba(size_t(w) * h * 4);
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            uint8_t p = idx[i];
            if (p == 9) { rgba[i * 4 + 3] = 0; continue; }   // retail's transparent index
            const uint8_t* c = &(*pal)[size_t(p) * 4];
            rgba[i * 4 + 0] = c[0]; rgba[i * 4 + 1] = c[1]; rgba[i * 4 + 2] = c[2]; rgba[i * 4 + 3] = 255;
        }
        mapPreviewTex_ = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!mapPreviewTex_) return;
        SDL_SetTextureBlendMode(mapPreviewTex_, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(mapPreviewTex_, nullptr, rgba.data(), w * 4);
        SDL_SetTextureScaleMode(mapPreviewTex_, SDL_ScaleModeLinear);
        mapPreviewW_ = w; mapPreviewH_ = h;
        // Info line under the preview, retail-style: "<size>  <N> PLAYER" (memory
        // dropped). The .ota supplies size ("9 x 7", already in 32-cell units) and
        // numplayers; derive them from the tnt / start positions if the .ota omits them.
        std::string size = otaField(ota, "size");
        if (size.empty()) size = std::to_string(u32(4) / 32) + " x " + std::to_string(u32(8) / 32);
        std::string players = otaField(ota, "numplayers");
        if (players.empty() && !mapPreviewStarts_.empty())
            players = std::to_string(mapPreviewStarts_.size());
        mapPreviewDims_ = size + (players.empty() ? "" : "   " + players + " PLAYER");
    }

    GameView::GenPreviewOut GameView::rollGenPreview(const std::string& id, const tak::hpi::Vfs& vfs) {
        // On a worker: roll the map from the seed and paint a downsampled height/water
        // map with feature dots; pollGenPreview uploads it. Nothing here touches the view.
        GenPreviewOut out;
        out.id = id;
        tak::mapgen::Params gp = tak::mapgen::decodeMapId(id);
        tak::mapgen::Result g;
        try { g = tak::mapgen::generate(gp, vfs); }
        catch (const std::exception& e) { out.error = e.what(); return out; }
        const tak::tnt::Map& m = g.map;
        const int W = m.width, H = m.height, sea = m.seaLevel;
        if (W <= 0 || H <= 0 || m.heights.size() < size_t(W) * H) return out;
        // As many texels as the largest create preview (288), so it is never stretched.
        const int cap = 288, mx = std::max(W, H);
        const int TW = std::max(1, W * cap / mx), TH = std::max(1, H * cap / mx);
        // World-flavoured land tint (loosely matches each world's ground section art).
        static const uint8_t landRGB[tak::mapgen::kMapTypes][3] = {
            {74, 118, 58}, {112, 84, 54}, {200, 172, 148}, {46, 92, 46}, {34, 80, 60}};
        const uint8_t* lc = landRGB[gp.mapType % tak::mapgen::kMapTypes];
        std::vector<uint8_t> rgba(size_t(TW) * TH * 4, 255);
        auto put = [&](int tx, int ty, uint8_t r, uint8_t gg, uint8_t b) {
            if (tx < 0 || ty < 0 || tx >= TW || ty >= TH) return;
            uint8_t* px = &rgba[(size_t(ty) * TW + tx) * 4];
            px[0] = r; px[1] = gg; px[2] = b; px[3] = 255;
        };
        // Base: height-shaded water (deeper -> darker) / land (brighter with elevation).
        for (int ty = 0; ty < TH; ++ty)
            for (int tx = 0; tx < TW; ++tx) {
                int h = m.heights[size_t(ty * H / TH) * W + (tx * W / TW)];
                if (h < sea) {
                    // Dark teal, matching the retail sea art the game actually draws.
                    int d = std::clamp((sea - h) * 2, 0, 40);
                    put(tx, ty, uint8_t(14 - d / 8), uint8_t(72 - d / 2), uint8_t(74 - d / 2));
                } else {
                    int e = std::clamp((h - sea) / 2, 0, 70);
                    put(tx, ty, uint8_t(std::min(255, lc[0] + e)),
                                uint8_t(std::min(255, lc[1] + e)), uint8_t(std::min(255, lc[2] + e)));
                }
            }
        // Feature dots -- sparse, so plot every one lest the downsample drop it. A
        // Sacred Stone (the mana spot) gets a bold 3x3 gold marker; Standing Stones
        // (ruins) are muted; rocks grey; trees dark green.
        for (int cz = 0; cz < H; ++cz)
            for (int cx = 0; cx < W; ++cx) {
                uint16_t fi = m.features[size_t(cz) * W + cx];
                if (fi == 0xFFFF || fi >= m.featureNames.size()) continue;
                const std::string& nm = m.featureNames[fi];
                int tx = cx * TW / W, ty = cz * TH / H;
                if (tak::hpi::MountSet::key(nm).find("mana") != std::string::npos) {          // sacred spot: gold
                    for (int dy = 0; dy <= 2; ++dy)
                        for (int dx = 0; dx <= 2; ++dx) put(tx + dx, ty + dy, 250, 224, 82);
                } else if (nm.find("Henge") != std::string::npos) put(tx, ty, 176, 162, 132);  // ruins: stone
                else if (nm.find("Rock") != std::string::npos) put(tx, ty, 150, 148, 140);      // rock: grey
                else put(tx, ty, 28, 66, 28);                                                   // tree: dark green
            }
        // Start positions: drawn over the thumbnail as numbered markers (drawStartMarkers),
        // in world px like a shipped map's -- the same cell -> px setupMatch applies.
        for (auto& [sx, sz] : g.starts) out.starts.push_back({float(sx * 16), float(sz * 16)});
        out.worldW = float(W) * 16; out.worldH = float(H) * 16;
        out.rgba = std::move(rgba); out.w = TW; out.h = TH;
        char dims[48];
        std::snprintf(dims, sizeof dims, "%d x %d   %d PLAYER", W / 32, H / 32, int(gp.players));
        out.dims = dims;
        out.map = std::make_shared<const tak::tnt::Map>(std::move(g.map));
        return out;
    }

    void GameView::stopPreviewReaders() {
        bigMap_.reset();   // ~MapView joins its section and chunk workers
        for (auto& job : genJobs_) job.second.wait();
    }

    void GameView::pollGenPreview() {
        for (auto it = genJobs_.begin(); it != genJobs_.end();) {
            if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++it; continue; }
            GenPreviewOut out = it->second.get();
            const bool newest = it->first == genSeq_;
            it = genJobs_.erase(it);
            if (!newest) continue;
            genPreviewPending_ = false;
            if (mapPreviewTex_) { gpuvram::destroy(mapPreviewTex_); mapPreviewTex_ = nullptr; }
            mapPreviewW_ = mapPreviewH_ = 0;
            mapPreviewStarts_ = std::move(out.starts);
            mapPreviewWorldW_ = out.worldW; mapPreviewWorldH_ = out.worldH;
            mapPreviewDims_ = std::move(out.dims);
            genPreviewError_ = std::move(out.error);
            if (out.map) { genPreviewMap_ = std::move(out.map); genPreviewMapId_ = out.id; }
            if (out.w <= 0) continue;
            mapPreviewTex_ = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, out.w, out.h);
            if (!mapPreviewTex_) continue;
            SDL_SetTextureBlendMode(mapPreviewTex_, SDL_BLENDMODE_BLEND);
            SDL_UpdateTexture(mapPreviewTex_, nullptr, out.rgba.data(), out.w * 4);
            SDL_SetTextureScaleMode(mapPreviewTex_, SDL_ScaleModeNearest);
            mapPreviewW_ = out.w; mapPreviewH_ = out.h;
        }
    }

    void GameView::applyGenParams() {
        genParams_.formatVer = tak::mapgen::Params{}.formatVer;
        genParams_ = tak::mapgen::sanitize(genParams_);   // grows a side the layout cannot fit
        auto previous = tak::mapgen::decodeMapId(mpMapId_);
        previous.name = genParams_.name;
        const bool reusePreview = mapPreviewFor_ == mapPath_ &&
            tak::mapgen::isGeneratedMapId(mpMapId_) &&
            tak::mapgen::encodeMapId(previous) == tak::mapgen::encodeMapId(genParams_);
        mpMapId_ = tak::mapgen::encodeMapId(genParams_);
        if (reusePreview) mapPreviewFor_ = mpMapId_;
        mapPath_ = mpMapId_;   // generated: the id IS the path (findMap returns it as-is)
    }

    void GameView::setGenSlider(int i, float mx) {
        const SDL_FRect& r = genSliderRect_[i];
        if (r.w <= 0) return;
        float t = std::clamp((mx - r.x) / r.w, 0.0f, 1.0f);
        uint8_t v = uint8_t(t * 255.0f + 0.5f);
        switch (i) {
            case 0: genParams_.treeDensity = v; break;
            case 1: genParams_.rockDensity = v; break;
            case 2: genParams_.manaDensity = v; break;
            case 3: genParams_.waterDensity = v; break;
            case 4: genParams_.reliefDensity = v; break;
            case 6: genParams_.plateauDensity = v; break;
            case 7: genParams_.mountainDensity = v; break;
            case 8: genParams_.townDensity = v; break;
            default:   // SCATTERED MANA snaps to its five levels
                genParams_.manaScatter = uint8_t(t * float(tak::mapgen::kScatterLevels - 1) + 0.5f); break;
        }
    }

    void GameView::saveCreatePreferences() {
        if (!settings_ || !createPrefApplied_ || lobbyScreen_ != LobbyScreen::Create ||
            (mp_ && mp_->state() != tak::net::MpClient::State::Lobby)) return;
        auto pref = settings_->gameCreate;
        pref.crusades = createCrusades_;
        pref.doubleSight = createDoubleSight_;
        pref.pathfindingMode = createPathfindingMode_;
        pref.speedUnlock = createSpeedUnlock_;
        pref.monarchExpendable = createMonarchExp_;
        pref.randomStarts = createStartMode_;
        pref.allowSpectators = createAllowSpectators_;
        pref.powerCodes = createPowerCodes_;
        pref.unitCap = createUnitCap_;
        pref.fog = createFog_;
        if (!lanHost_) pref.name = createName_;   // a LOCAL SERVER game is always named for its host
        pref.generated = tak::mapgen::isGeneratedMapId(mpMapId_);
        pref.generator = tak::mapgen::encodeMapId(genParams_);
        pref.mapSort = mapSort_; pref.mapSortDir = mapSortDir_;
        if (pref == settings_->gameCreate) return;
        settings_->gameCreate = std::move(pref);
        saveSettings(*settings_);
    }

    void GameView::syncCreateScenario() {
        // MAP SCENARIO belongs to one map: every way the selection changes (a list click,
        // the remembered map, the generator, setMpMapId) lands here and starts it OFF.
        if (createScenarioFor_ == mapPath_) return;
        createScenarioFor_ = mapPath_;
        createMapScenario_ = false;
        createScenario_ = {};
        if (!mapPath_.empty() && !tak::mapgen::isGeneratedMapId(mapPath_))
            createScenario_ = mapScenarioInfo(mapPath_);
    }

    void GameView::scanModPacks() {
        if(!modPacksScanned_){
            const auto root=tak::u8path(installRoot_);
            availableGameMods_=mods_.game?tak::hpi::modPacks(root,tak::hpi::ModKind::Game):std::vector<std::string>{};
            availableCosmeticMods_=mods_.cosmetic?tak::hpi::modPacks(root,tak::hpi::ModKind::Cosmetic):std::vector<std::string>{};
            alwaysCosmeticMods_=mods_.cosmetic?tak::hpi::alwaysEnabledPacks(root,tak::hpi::ModKind::Cosmetic):std::vector<std::string>{};
            alwaysGameMods_=mods_.game?tak::hpi::alwaysEnabledPacks(root,tak::hpi::ModKind::Game):std::vector<std::string>{};
            modPacksScanned_=true;
        }
    }

    bool GameView::createScenarioOn() const {
        if (createScenarioFor_ != mapPath_ || tak::mapgen::isGeneratedMapId(mpMapId_)) return false;
        // An offerable map plays its scenario when chosen; a Test Map copy always does.
        return createScenario_.active(createMapScenario_ && scenarioOffered(createScenario_));
    }

    void GameView::drawCreate(int winW, int winH) {
        (void)winW; (void)winH;
        if (!createPrefApplied_ && settings_) {
            const auto& pref = settings_->gameCreate;
            createCrusades_ = pref.crusades;
            createDoubleSight_ = pref.doubleSight;
            createPathfindingMode_ = pref.pathfindingMode;
            createSpeedUnlock_ = pref.speedUnlock;
            createMonarchExp_ = pref.monarchExpendable;
            createStartMode_ = uint8_t(std::clamp(pref.randomStarts, 0, 2));
            createAllowSpectators_ = pref.allowSpectators;
            createPowerCodes_ = pref.powerCodes;
            spSpectate_ = false;  // Opt in for each skirmish; never restore from settings.
            createUnitCap_ = pref.unitCap == 250 || pref.unitCap == 500 || pref.unitCap == 1000 || pref.unitCap == 2000
                ? uint16_t(pref.unitCap) : 2000;
            createFog_ = uint8_t(pref.fog);
            if (!lanHost_) createName_ = pref.name.substr(0, 16);   // a LOCAL SERVER game keeps its host's name
            mapSort_ = pref.mapSort; mapSortDir_ = pref.mapSortDir;
            if (!mapList_.empty()) sortMapList();
            if (tak::mapgen::isGeneratedMapId(pref.generator)) {
                try {
                    genParams_ = tak::mapgen::decodeMapId(pref.generator);
                    if (pref.generated) { applyGenParams(); mapPrefApplied_ = true; }
                } catch (const std::exception&) { genParams_ = {}; }
            }
            createPrefApplied_ = true;
        }
        scanModPacks();   // scenarioOffered: may a game pack supply a missing unit type?
        syncCreateScenario();
        // While the map's scenario is on, it owns how the game ends and where each seat's
        // forces start: those two rows read SCENARIO / FIXED and are not clickable (retail
        // greyed the same boxes while Use Map Script was ticked). The saved choices stay.
        const bool scenarioOn = createScenarioOn();
        // Only the page title is over the columns, which start just under it: the first
        // row level with the map list's sort buttons. Multiplayer puts the name and
        // password fields and the MAP line above its option buttons.
        float x = kCreateMargin, y = 60;   // the left margin = the right one
        // A private single-player game needs no name or password.
        if (!singlePlayer_) {
            lbField(x, y, 260, "GAME NAME", createName_, 1); y += 44;
            lbField(x, y, 260, "PASSWORD (OPTIONAL)", createPass_, 2); y += 44;
        }
        if (!singlePlayer_) { blockText(std::string("MAP: ") + mapTitle(mpMapId_), x, y, 1.8f, {180, 185, 195, 255}); y += 28; }
        // Option rows: the map column's rhythm (24 high, 6 apart, as TYPE / LAYOUT /
        // SIZE). Multiplayer's ten, under the name and password fields, end at 470, clear
        // of the BACK/CREATE footer. One column: every row kOptW wide (clear of the map
        // list) and every label at optPx (createOptionPx), whatever it says.
        const float kRow = 30, kRowH = 24;
        constexpr float kOptW = 300;
        const float optPx = createOptionPx(kOptW - 12);
        auto option = [&](const std::string& label, bool enabled, std::function<void(int)> cycle) {
            lbCycle(x, y, kOptW, kRowH, label, enabled, std::move(cycle), optPx);
            y += kRow;
        };
        option(createCrusades_ ? "CRUSADES: ON" : "CRUSADES: OFF", true,
               [this](int) { createCrusades_ = !createCrusades_; });
        option(createDoubleSight_ ? "DOUBLE SIGHT/RADAR: ON" : "DOUBLE SIGHT/RADAR: OFF", true,
               [this](int) { createDoubleSight_ = !createDoubleSight_; });
        option(pathfindingLabel(createPathfindingMode_), true,
               [this](int direction) { createPathfindingMode_=tak::sim::cyclePathfindingMode(createPathfindingMode_,direction); });
        option("UNIT CAP: " + std::to_string(createUnitCap_),true,[this](int direction) {
            static constexpr uint16_t limits[]={250,500,1000,2000};
            auto at=std::find(std::begin(limits),std::end(limits),createUnitCap_);
            int index=at==std::end(limits) ? 0 : int(at-std::begin(limits));
            createUnitCap_=limits[(index+direction+4)%4];
        });
        // When OFF, losing your Monarch loses the game (retail commander rule); ON
        // makes the Monarch just another unit.
        option(scenarioOn ? "MONARCH EXPENDABLE: SCENARIO"
               : createMonarchExp_ ? "MONARCH EXPENDABLE: ON"
                                   : "MONARCH EXPENDABLE: OFF", !scenarioOn,
               [this](int) { createMonarchExp_ = !createMonarchExp_; });
        option(createSpeedUnlock_ ? "ALLOW SPEED CHANGE: ON" : "ALLOW SPEED CHANGE: OFF", true,
               [this](int) { createSpeedUnlock_ = !createSpeedUnlock_; });
        // POWER CODES (retail's battle-room box): the +codes typed in the chat box work
        // for every player of this game. Off by default, as in retail. A single-player
        // spectator plays no army: OFF, greyed, like FOG OF WAR below -- the saved choice
        // (createPowerCodes_) is left alone and comes back when SPECTATE is turned off.
        const bool spectating = singlePlayer_ && spSpectate_;
        option(createPowerCodes_ && !spectating ? "POWER CODES: ON" : "POWER CODES: OFF", !spectating,
               [this](int) { createPowerCodes_ = !createPowerCodes_; });
        // SP only: spectate mode -- you take no slot and just watch the AIs fight.
        // Seat AIs in the slots below, then START.
        if (singlePlayer_) {
            option(spSpectate_ ? "SPECTATE (WATCH AIS): ON" : "SPECTATE (WATCH AIS): OFF", true,
                   [this](int) { spSpectate_ = !spSpectate_; });
        } else {
            // MP only: may others WATCH this game once it runs? The server enforces it;
            // the host can still flip it in the room until START.
            option(createAllowSpectators_ ? "ALLOW SPECTATORS: ON" : "ALLOW SPECTATORS: OFF", true,
                   [this](int) { createAllowSpectators_ = !createAllowSpectators_; });
        }
        // Fog of war: a room rule, so it is picked here and not only after the
        // room exists. Display-only (never hashed) -- every peer applies the same
        // rule, so it stays fair. The room displays this choice as information.
        // A single-player spectator sees everything: FULL VISION, greyed. The saved
        // choice (createFog_) is left alone and comes back when SPECTATE is turned off.
        option(std::string("FOG OF WAR: ") + kFogName[spectating ? 2 : std::min<int>(createFog_, 2)], !spectating,
               [this](int direction) { createFog_ = uint8_t((createFog_ + direction + 3) % 3); });
        // Start Locations: also a room rule, picked here like fog so it is set before
        // the room exists. FIXED = the seated slots take the map's starts in order and
        // empty slots are skipped, as in retail; RANDOM = the starts are shuffled for
        // the match; BY SLOT (not retail) = slot N always takes the map's Nth start.
        // A MAP SCENARIO plays on fixed starts.
        option(startLocationsLabel(scenarioOn ? tak::net::kStartsFixed : createStartMode_), !scenarioOn,
               [this](int direction) { createStartMode_ = uint8_t((createStartMode_ + direction + 3) % 3); });
        // Footer buttons, symmetric: BACK at the bottom-left and CREATE at the
        // bottom-right, both inset by `x` from their side and at the same height.
        // BROWSER (MP only) sits just left of CREATE.
        const float by = kLobbyH - 40, bw = 120;
        lbBtn(kLobbyW - x - bw, by, bw, 30, "CREATE", !createName_.empty() && genPreviewError_.empty(), [this] {
            tak::net::GameOptions o; o.crusades = createCrusades_ ? 1 : 0;
            // Game mods are on in every room (multiplayer works like single player): the
            // host ticks mods/game packs in the room, and they and the always-enabled
            // ones are sent to every player. The server turns this off for missions.
            o.gameMods = 1;
            mp_->setGameModsAllowed(mods_.game);
            mp_->setGameModPacks(settings_ ? settings_->gameModPacks : std::vector<std::string>{});
            o.doubleSight = createDoubleSight_ ? 1 : 0;
            o.pathfindingMode = createPathfindingMode_;
            o.speedUnlock = createSpeedUnlock_ ? 1 : 0;
            o.unitCap = createUnitCap_;
            o.monarchExpendable = createMonarchExp_ ? 1 : 0;
            o.powerCodes = createPowerCodes_ && !(singlePlayer_ && spSpectate_) ? 1 : 0;
            o.fogExplored = singlePlayer_ && spSpectate_ ? 2 : std::min<uint8_t>(createFog_, 2);
            // MAP SCENARIO: an offerable map's own scenario, when chosen. A Test Map copy
            // plays its scenario through its opt-in and sends 0 (the server checks the
            // verified package either way). Seats and rule regions follow fixed starts.
            o.mapScenario = !tak::mapgen::isGeneratedMapId(mpMapId_) && createScenarioFor_ == mapPath_ &&
                scenarioOffered(createScenario_) && createMapScenario_ ? 1 : 0;
            o.randomStarts = createScenarioOn() ? tak::net::kStartsFixed : createStartMode_;
            // A private single-player room is never listed, so the rule means nothing
            // there; leave it at the default rather than a remembered MP choice.
            o.allowSpectators = singlePlayer_ || createAllowSpectators_ ? 1 : 0;
            // SP spectate: create as a spectator (no slot) so every slot can be an AI;
            // the game-start path flags spectating_ + noFog_ from mp_->isSpectator().
            mp_->createGame(createName_, createPass_, mpMapId_, o, mpCapacity(o.mapScenario != 0),
                            singlePlayer_ && spSpectate_, singlePlayer_);
            specAutoSeated_ = false;   // arm the one-shot auto-seat for this new room
            // A map that cannot build sends nothing: stay here, where the reason is shown.
            lobbyScreen_ = singlePlayer_ || !mp_->createError().empty() ? LobbyScreen::Create : LobbyScreen::Browser;
        });
        if (!mp_->createError().empty()) {
            const std::string why = "CANNOT CREATE: " + mp_->createError();
            blockText(tak::uiTextFit(why, 1.3f, kLobbyW - 2 * x - 2 * bw - 152.0f), x + bw + 20, by + 9, 1.3f, {255, 130, 110, 255});
        }
        // BACK: multiplayer returns to the game browser it came from; a private
        // single-player game has no browser, so it returns to the menu.
        lbBtn(x, by, bw, 30, "BACK", true, [this] {
            if (singlePlayer_ || lanHost_) menuRequested_ = true;
            else lobbyScreen_ = LobbyScreen::Browser;
        });

        // Map picker (right column): a scrollable list box. Selecting sets both the
        // wire id (mpMapId_ = bare .tnt stem) and mapPath_, so mpCapacity() recomputes
        // the chosen map's start-position count.
        if (mapList_.empty() && !mapListScanned_) buildMapList();
        // Adopt the remembered map once (persisted across launches), and scroll to it.
        if (!mapPrefApplied_ && settings_ && !settings_->lastMap.empty()) {
            for (int i = 0; i < int(mapList_.size()); ++i)
                if (mapList_[size_t(i)].name == settings_->lastMap) {
                    mpMapId_ = mapList_[size_t(i)].name;
                    mapPath_ = mapList_[size_t(i)].path;
                    mapScroll_ = std::max(0, i - 3);
                    break;
                }
            mapPrefApplied_ = true;
            syncCreateScenario();
        }
        // hy: where a heading line would be; the columns have none, so the sort buttons
        // (hy + 20) sit level with the first option row (y 60).
        float lx = kCreateMargin + 320, hy = 40;   // 20 right of the 300-wide option column
        // Toggle between the map list and the random-map generator.
        bool gen = tak::mapgen::isGeneratedMapId(mpMapId_);

      if (!gen) {
        // Sort buttons: NAME / PLAYERS / SIZE. Clicking sets the sort key; clicking the
        // active one flips ascending/descending. The active key shows an up/down arrow.
        {
            const char* keys[3] = {"NAME", "PLAYERS", "SIZE"};
            const float bw = 86, bh = 20, sy = hy + 20;
            // FILTER: a text field right of the sort buttons, as tall as they are, to the
            // list's right edge. A dim FILTER until something is typed.
            {
                const float fx = lx + 3 * (bw + 6), fw = mapListW() - 3 * (bw + 6);
                const SDL_FRect r{fx, sy, fw, bh};
                const bool active = lbField_ == 6;
                SDL_SetRenderDrawColor(ren_, 24, 26, 34, 255); SDL_RenderFillRectF(ren_, &r);
                SDL_SetRenderDrawColor(ren_, active ? 200 : 90, active ? 210 : 100, active ? 130 : 130, 255);
                SDL_RenderDrawRectF(ren_, &r);
                if (mapFilter_.empty() && !active)
                    blockText("FILTER", fx + 6, sy + 5, 1.5f, {110, 116, 132, 255});
                else
                    blockText(tak::uiTextFitStart(mapFilter_ + (active ? "_" : ""), 1.5f, fw - 12), fx + 6, sy + 5, 1.5f,
                              {225, 230, 240, 255});
                lobbyHots_.push_back({r, [this] { lbField_ = 6; SDL_StartTextInput(); }});
            }
            for (int k = 0; k < 3; ++k) {
                float bx = lx + k * (bw + 6);
                SDL_FRect b{bx, sy, bw, bh};
                bool active = mapSort_ == k;
                SDL_SetRenderDrawColor(ren_, active ? 56 : 34, active ? 74 : 38, active ? 96 : 50, 255);
                SDL_RenderFillRectF(ren_, &b);
                SDL_SetRenderDrawColor(ren_, 90, 100, 130, 255); SDL_RenderDrawRectF(ren_, &b);
                const SDL_Color color = active ? SDL_Color{215, 230, 245, 255}
                                               : SDL_Color{170, 178, 195, 255};
                blockText(keys[k], bx + 6, sy + 5, 1.5f, color);
                if (active) {
                    const float ax = bx + bw - 10, ay = sy + bh / 2;
                    const float direction = mapSortDir_ > 0 ? -1.0f : 1.0f;
                    const SDL_Vertex arrow[] = {
                        {{ax, ay + direction * 3}, color, {}},
                        {{ax - 4, ay - direction * 3}, color, {}},
                        {{ax + 4, ay - direction * 3}, color, {}}
                    };
                    SDL_RenderGeometry(ren_, nullptr, arrow, 3, nullptr, 0);
                }
                lobbyHots_.push_back({b, [this, k](int) {
                    if (mapSort_ == k) mapSortDir_ = -mapSortDir_;   // toggle direction
                    else { mapSort_ = k; mapSortDir_ = 1; }
                    sortMapList();
                    mapScroll_ = 0;
                }});
            }
        }
        // The list reaches down to y 480, above the BACK/CREATE footer.
        const float boxX = lx, boxY = hy + 46, boxW = mapListW(), boxH = 480 - boxY, rowH = 24, sbW = 12;
        // The rows on show: every map, or those whose title or name holds the FILTER text
        // (case-insensitive).
        std::vector<int> shown;
        {
            const std::string want = tak::mapfilter::lower(mapFilter_);
            for (int i = 0; i < int(mapList_.size()); ++i)
                if (tak::mapfilter::matches(mapList_[size_t(i)].title, mapList_[size_t(i)].name, want)) shown.push_back(i);
        }
        const int total = int(shown.size());
        mapVisRows_ = int(boxH / rowH);
        mapTotalRows_ = total;
        clampMapScroll();
        mapListRect_ = {boxX, boxY, boxW, boxH};
        // box background + border
        SDL_FRect box{boxX, boxY, boxW, boxH};
        SDL_SetRenderDrawColor(ren_, 24, 26, 34, 255); SDL_RenderFillRectF(ren_, &box);
        SDL_SetRenderDrawColor(ren_, 70, 76, 96, 255); SDL_RenderDrawRectF(ren_, &box);
        const float rowW = boxW - sbW - 4;
        const float infoW = 108;                    // right-side "S 2P 8x8" column
        const float nameW = rowW - infoW - 12;      // the name column, at px 2.0
        for (int r = 0; r < mapVisRows_; ++r) {
            const int at = mapScroll_ + r;
            if (at >= total) break;
            const MapInfo& m = mapList_[size_t(shown[size_t(at)])];
            const std::string& nm = m.name;
            const std::string& pth = m.path;
            bool sel = (nm == mpMapId_);
            SDL_FRect row{boxX + 2, boxY + r * rowH, rowW, rowH};
            bool hot = lbHot(row);
            SDL_Color rc = sel ? SDL_Color{44, 78, 44, 255}
                         : hot ? SDL_Color{40, 46, 62, 255} : SDL_Color{24, 26, 34, 255};
            SDL_SetRenderDrawColor(ren_, rc.r, rc.g, rc.b, 255); SDL_RenderFillRectF(ren_, &row);
            const std::string& title = m.title.empty() ? nm : m.title;
            std::string label = tak::uiTextFit(title, 2.0f, nameW, false);
            // Keep part of a generated map's recipe suffix visible even when a
            // long title needs shortening, so duplicate titles can be told apart.
            if (label != title && title.size() > 17 &&
                title[title.size()-17] == '-' &&
                std::all_of(title.end()-16,title.end(),[](char c) {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                })) {
                const std::string tail = "~" + title.substr(title.size()-8);
                label = tak::uiTextFit(title, 2.0f, nameW - blockWidth(tail, 2.0f), false) + tail;
            }
            blockText(label,
                      boxX + 8, row.y + (rowH - 14) / 2, 2.0f,
                      sel ? SDL_Color{200, 240, 200, 255} : SDL_Color{220, 225, 235, 255});
            // Right-aligned player count + size, e.g. "4P 16x16".
            char info[24];
            if (m.sizeW > 0) std::snprintf(info, sizeof info, "%dP %dx%d", m.players, m.sizeW, m.sizeH);
            else std::snprintf(info, sizeof info, "%dP", m.players);
            const float infoX = boxX + rowW - blockWidth(info, 1.5f) - 6;
            blockText(info, infoX, row.y + (rowH - 11) / 2, 1.5f,
                      sel ? SDL_Color{170, 210, 170, 255} : SDL_Color{150, 160, 178, 255});
            // MAP SCENARIO: a gold S marks a map with a playable scenario (or a Test Map copy).
            if (scenarioOffered(m.scenario) || (m.scenario.marker && m.scenario.runnable()))
                blockText("S", infoX - 14, row.y + (rowH - 11) / 2, 1.5f, {230, 200, 120, 255});
            lobbyHots_.push_back({row, [this, nm, pth] {
                mpMapId_ = nm; mapPath_ = pth;
                if (settings_) { settings_->lastMap = nm; saveSettings(*settings_); }  // remember it
            }});
        }
        // Scrollbar: track + a proportional, draggable thumb (also wheel-scrollable).
        if (total > mapVisRows_) {
            float trackX = boxX + boxW - sbW;
            SDL_FRect track{trackX, boxY, sbW, boxH};
            SDL_SetRenderDrawColor(ren_, 30, 32, 42, 255); SDL_RenderFillRectF(ren_, &track);
            float thumbH = std::max(24.0f, boxH * mapVisRows_ / total);
            float thumbY = boxY + (boxH - thumbH) * mapScroll_ / float(total - mapVisRows_);
            mapThumbRect_ = {trackX, thumbY, sbW, thumbH};
            SDL_SetRenderDrawColor(ren_, mapDrag_ ? 130 : 90, mapDrag_ ? 150 : 100,
                                   mapDrag_ ? 190 : 130, 255);
            SDL_RenderFillRectF(ren_, &mapThumbRect_);
        } else {
            mapThumbRect_ = {0, 0, 0, 0};
        }

      } else {
        // ---- random-map params panel (replaces the list) --------------------------
        static const char* kTypeName[tak::mapgen::kMapTypes] = {"ARAMON", "TAROS", "VERUNA", "ZHON", "CREON"};
        // One rhythm for the whole panel: every control is kRowH tall and every gap
        // is kGap. The rows used to step by 30, 30, 36 and then 44, so the buttons
        // sat tighter than the sliders and the run of sliders drifted out of line
        // with everything above it.
        const float kRowH = 24, kGap = 6, kBtnW = 300;
        const float kSliderH = 30;   // label (11) + 5 + bar (14)
        const float kHalfW = (kBtnW - kGap) / 2;
        // Where the list's sort buttons sit, level with the first option row.
        float px = lx, py = hy + 20;
        lbField(px, py, kBtnW, "MAP NAME (OPTIONAL)", genParams_.name, 5); py += 44;
        lbCycle(px, py, kBtnW, kRowH, std::string("TYPE:  ") + kTypeName[genParams_.mapType % tak::mapgen::kMapTypes],
              true, [this](int direction) { genParams_.mapType = uint8_t((genParams_.mapType + direction + tak::mapgen::kMapTypes) % tak::mapgen::kMapTypes);
                             if(genParams_.layout>=tak::mapgen::Maze) genParams_.layout=tak::mapgen::themedLayout(genParams_.mapType);
                             applyGenParams(); }); py += kRowH + kGap;
        lbCycle(px, py, kBtnW, kRowH, std::string("LAYOUT:  ") + tak::mapgen::layoutName(genParams_.layout),
              true, [this](int direction) { const int index=genParams_.layout>=tak::mapgen::Maze?3:genParams_.layout;
                             const int next=(index+direction+4)%4;
                             genParams_.layout=next==3?tak::mapgen::themedLayout(genParams_.mapType):uint8_t(next);
                             applyGenParams(); }); py += kRowH + kGap;
        char pl[32]; std::snprintf(pl, sizeof pl, "PLAYERS:  %d", int(genParams_.players));
        lbCycle(px, py, kBtnW, kRowH, pl, true, [this](int direction) {
            genParams_.players = uint8_t(2 + (genParams_.players - 2 + direction + 7) % 7);
            applyGenParams();
        }); py += kRowH + kGap;
        // WIDTH and HEIGHT, in sections (32 cells), each on its own: a step offers only the
        // sizes the layout can build with the other side as it is (sanitize keeps both).
        const auto sizeCycle = [this](bool height, int direction) {
            const int cur = (height ? genParams_.heightCells : genParams_.widthCells) / 32;
            std::vector<int> sizes;
            for (int size : tak::mapgen::kSizeSteps) {
                auto candidate = genParams_;
                (height ? candidate.heightCells : candidate.widthCells) = uint16_t(size * 32);
                const auto kept = tak::mapgen::sanitize(candidate);
                if (kept.widthCells == candidate.widthCells && kept.heightCells == candidate.heightCells)
                    sizes.push_back(size);
            }
            if (sizes.empty()) return;
            const auto at = std::find(sizes.begin(), sizes.end(), cur);
            const int index = at == sizes.end() ? (direction > 0 ? -1 : 0) : int(at - sizes.begin());
            const int next = sizes[size_t((index + direction + int(sizes.size())) % int(sizes.size()))];
            (height ? genParams_.heightCells : genParams_.widthCells) = uint16_t(next * 32);
            applyGenParams();
        };
        char wl[32]; std::snprintf(wl, sizeof wl, "WIDTH:  %d", genParams_.widthCells / 32);
        lbCycle(px, py, kHalfW, kRowH, wl, true, [sizeCycle](int direction) { sizeCycle(false, direction); });
        char hl[32]; std::snprintf(hl, sizeof hl, "HEIGHT:  %d", genParams_.heightCells / 32);
        lbCycle(px + kHalfW + kGap, py, kHalfW, kRowH, hl, true, [sizeCycle](int direction) { sizeCycle(true, direction); });
        py += kRowH + kGap * 2;   // a wider break between the pickers and the sliders
        // Nine sliders in two half-width columns; each level reads at the right end of
        // its label line, where the bar's column ends.
        int column = 0;
        auto slider = [&](int idx, const char* label, uint8_t val, bool enabled = true, const char* off = "AUTO") {
            const float bx = px + column * (kHalfW + kGap), by = py + 16, bw = kHalfW, bh = 14;
            genSliderRect_[idx] = enabled ? SDL_FRect{bx, by, bw, bh} : SDL_FRect{};
            SDL_FRect bar{bx, by, bw, bh};
            SDL_SetRenderDrawColor(ren_, 30, 34, 46, 255); SDL_RenderFillRectF(ren_, &bar);
            float t = enabled ? val / 255.0f : 0.0f;
            SDL_FRect fill{bx, by, bw * t, bh};
            SDL_SetRenderDrawColor(ren_, 70, 120, 90, 255); SDL_RenderFillRectF(ren_, &fill);
            SDL_FRect handle{bx + bw * t - 3, by - 2, 6, bh + 4};
            SDL_SetRenderDrawColor(ren_, 200, 220, 200, 255); if (enabled) SDL_RenderFillRectF(ren_, &handle);
            SDL_SetRenderDrawColor(ren_, 70, 76, 96, 255); SDL_RenderDrawRectF(ren_, &bar);
            static const char* kScatterShort[tak::mapgen::kScatterLevels] = {"NONE", "MIN", "MED", "HIGH", "MAX"};
            const std::string level = !enabled ? off : idx==2 ? "+" + std::to_string(tak::mapgen::extraManaRounds(val)) + "/P" :
                idx==5 ? kScatterShort[std::min<int>(genParams_.manaScatter, tak::mapgen::kScatterLevels - 1)] :
                val == 0 ? "NONE" : val < 86 ? "LOW" : val < 171 ? "MED" : "HIGH";
            const float levelW = blockWidth(level, 1.3f);
            // The label shrinks, if it must, to leave the level its room on the line.
            blockText(label, bx, py + 2, tak::uiTextFitPx(label, 1.3f, bw - levelW - 8), {180, 185, 195, 255});
            blockText(level, bx + bw - levelW, py + 2, 1.3f, {160, 165, 180, 255});
            if (++column == 2) { column = 0; py += kSliderH + kGap; }
        };
        const uint8_t world = genParams_.mapType, layout = genParams_.layout;
        slider(0, "FORESTS", genParams_.treeDensity);
        slider(1, "ROCKS", genParams_.rockDensity);
        slider(2, "EXTRA MANA", genParams_.manaDensity);
        // Neutral spots scattered by AREA, apart from the per-player extra spots (a
        // level, not a count: the same level puts four times as many on a map twice as
        // wide). Five notches, NONE..MAX; MAX = a six-spot home's density.
        slider(5, "SCATTERED MANA", uint8_t(genParams_.manaScatter * 255 / (tak::mapgen::kScatterLevels - 1)));
        slider(3, "WATER", genParams_.waterDensity, !tak::mapgen::automaticWater(layout));
        slider(4, layout==tak::mapgen::Highlands?"HIGHLANDS":"HILLS", genParams_.reliefDensity, layout!=tak::mapgen::Maze);
        // v10 retail kits. Maze walls and Creon highlands are the layout's own high
        // ground; Veruna ships no mountain kit.
        slider(6, "PLATEAUS", genParams_.plateauDensity, !tak::mapgen::automaticPlateaus(layout) &&
               tak::mapgen::kitAvailable(world, tak::mapgen::KitPlateaus));
        slider(7, "MOUNTAINS", genParams_.mountainDensity, tak::mapgen::kitAvailable(world, tak::mapgen::KitMountains), "NONE");
        slider(8, world==tak::mapgen::Zhon?"VILLAGES":"TOWNS", genParams_.townDensity,
               tak::mapgen::kitAvailable(world, tak::mapgen::KitTowns), "NONE");
        // The seed button shares the last row with TOWNS.
        py += 8;
        px += kHalfW + kGap;
        lbBtn(px, py, kHalfW, kRowH, "RE-ROLL SEED", true, [this] {
            genParams_.seed = genParams_.seed * 6364136223846793005ULL + 1442695040888963407ULL;
            applyGenParams();
        });
        px -= kHalfW + kGap;
      }

        // Preview panel, right of the list / params column. A real map shows its
        // embedded minimap; a generated map shows a live thumbnail rolled from its
        // seed (both go through buildMapPreview -> mapPreviewTex_).
        // Against the right margin; the map list widens to 12 short of it. Beside the
        // generator's narrower panel the preview grows into the free space: as wide as
        // the gap from the 300-wide panel (plus 20) to the margin, top level with the
        // panel's, and as tall as leaves room for the size line and button under it
        // above the footer at 500.
        float pvW = 192, pvy = hy + 46;
        if (gen) {
            pvy = hy + 20;
            pvW = std::min(kLobbyW - kCreateMargin - (lx + 320), kLobbyH - 48 - pvy - 58);
        }
        const float pvH = pvW, pvx = kLobbyW - kCreateMargin - pvW;
        SDL_FRect pbox{pvx, pvy, pvW, pvH};
        SDL_SetRenderDrawColor(ren_, 18, 20, 28, 255); SDL_RenderFillRectF(ren_, &pbox);
        if (mapPreviewFor_ != mapPath_) buildMapPreview(mapPath_);
        if (mapPreviewTex_ && mapPreviewW_ > 0) {
            float sc = std::min(pvW / float(mapPreviewW_), pvH / float(mapPreviewH_));
            float iw = mapPreviewW_ * sc, ih = mapPreviewH_ * sc;
            SDL_FRect dst{pvx + (pvW - iw) / 2, pvy + (pvH - ih) / 2, iw, ih};
            SDL_RenderCopyF(ren_, mapPreviewTex_, nullptr, &dst);
            drawStartMarkers(dst);   // no slots yet: the room tints them
        } else if (gen && !genPreviewError_.empty()) {
            // A recipe the retail data cannot build: the reason in the box.
            for (const auto& [line, y, color] : {std::tuple{"MAP UNAVAILABLE", -16.0f, SDL_Color{180, 185, 195, 255}},
                                                 std::tuple{"CHECK RETAIL DATA", 4.0f, SDL_Color{220, 140, 120, 255}}})
                blockText(line, pvx + (pvW - blockWidth(line, 1.4f)) / 2, pvy + pvH / 2 + y, 1.4f, color);
        } else {
            const char* none = gen ? "GENERATING" : "NO PREVIEW";
            blockText(none, pvx + (pvW - blockWidth(none, 1.6f)) / 2, pvy + pvH / 2 - 7, 1.6f,
                      {120, 125, 140, 255});
        }
        SDL_SetRenderDrawColor(ren_, 70, 76, 96, 255); SDL_RenderDrawRectF(ren_, &pbox);
        if (gen && genPreviewPending_ && mapPreviewTex_) {   // the last map's picture, until the new one is rolled
            const char* note = "GENERATING";
            const SDL_FRect band{pvx, pvy + pvH / 2 - 14, pvW, 28};
            SDL_SetRenderDrawColor(ren_, 12, 14, 20, 200); SDL_RenderFillRectF(ren_, &band);
            blockText(note, pvx + (pvW - blockWidth(note, 1.6f)) / 2, pvy + pvH / 2 - 7, 1.6f, {200, 205, 220, 255});
        }
        if (mapPreviewTex_ && mapPreviewW_ > 0 && !genPreviewPending_) {
            lobbyHots_.push_back({pbox, [this] { openBigMap(); }});   // full window
            drawClickHint(pbox);
        }
        if (!mapPreviewDims_.empty())
            blockText(mapPreviewDims_, pvx, pvy + pvH + 8, 1.6f, {160, 165, 180, 255});
        // Keep the generator action below the preview and map description.
        lbCycle(pvx, pvy + pvH + 32, pvW, 26, gen ? "PICK AN EXISTING MAP" : "GENERATE RANDOM MAP", true,
              [this, gen](int) {
                  if (gen) { mpMapId_.clear(); mapPath_.clear(); }  // drop back to the list
                  else applyGenParams();                           // encode the current gen params
              });
        // MAP SCENARIO (protocol 259): play the map's own scenario -- its placed armies,
        // trigger rules and Use Only list -- instead of a skirmish. Off for every map until
        // chosen; greyed, with the reason, when the map has no playable scenario. The lines
        // under it say what turning it on changes (dim while it is off).
        if (!gen && !mapPath_.empty()) {
            const float y0 = pvy + pvH + 68;
            const bool testCopy = createScenario_.marker && createScenario_.runnable();
            // A Test Map copy of a map whose only scenario content is a Use Only list
            // (hasscenario stays 0) plays no scenario, but its opt-in still applies the list.
            const bool testUseOnly = createScenario_.marker && !createScenario_.hasScenario &&
                                     createScenario_.useOnlyTypes >= 0;
            const bool offered = scenarioOffered(createScenario_);
            lbCycle(pvx, y0, pvW, 26, (createMapScenario_ && offered) || testCopy ? "MAP SCENARIO: ON" : "MAP SCENARIO: OFF",
                    offered, [this](int) { createMapScenario_ = !createMapScenario_; });
            std::vector<std::pair<std::string, SDL_Color>> lines;
            const SDL_Color grey{150, 155, 165, 255}, dim{130, 135, 150, 255}, normal{205, 210, 225, 255},
                            amber{235, 205, 120, 255};
            if (testCopy) {
                lines.push_back({"TEST COPY: ALWAYS ON", grey});
                for (const auto& l : tak::sim::scenarioSummary(createScenario_, true))
                    lines.push_back({l.text, l.warning ? amber : normal});
            } else if (testUseOnly) {
                lines.push_back({"TEST COPY: USE ONLY LIST", grey});
                lines.push_back({tak::sim::scenarioUseOnlyLine(createScenario_, true), normal});
            } else if (!offered) {
                for (const auto& l : tak::sim::scenarioReason(createScenario_)) lines.push_back({l, grey});
            } else {
                // Offered only because a game mod pack may supply a missing unit type.
                if (!createScenario_.offerable()) {
                    // The pack's own name (its capitals under MODERN), cut to the column.
                    lines.push_back({tak::uiTextFit("PACK NEEDED: " + createScenario_.detail, 1.2f, 190), amber});
                }
                for (const auto& l : tak::sim::scenarioSummary(createScenario_, true))
                    lines.push_back({l.text, !createMapScenario_ ? dim : l.warning ? amber : normal});
            }
            // Four lines at a 16 pitch, or a fifth (a test copy's or the pack warning on
            // top of a full summary) at 14, all clear of the CREATE footer at 500.
            const float pitch = lines.size() > 4 ? 14.0f : 16.0f;
            for (size_t i = 0; i < lines.size() && i < 5; ++i)
                blockText(lines[i].first, pvx, y0 + 32 + float(i) * pitch, 1.2f, lines[i].second);
        }
    }

    // The mods screen (the room's MODS... button): COSMETIC and GAME side by side over the
    // whole panel, each with its FILTER and a scrolling list of tick boxes, the
    // always-enabled packs first, locked on. COSMETIC packs are this player's own (never
    // sent, never changing the game); GAME packs only the skirmish host ticks, and a guest
    // sees the host's choice. At most 64 packs of a kind, its always-enabled ones included.
    // Changes apply as they are ticked; DONE (or Esc) goes back to the room.
    void GameView::drawModsScreen(int winW, int winH) {
        (void)winH;
        const auto& room = mp_->room();
        const bool host = room.hostId == mp_->myClientId();
        scanModPacks();
        const bool gameOn = room.mission.empty() && room.opts.gameMods;
        const auto& alwaysGame = mp_->alwaysGameModPacks();
        // (The page title, MODS, is the lobby's own.)
        constexpr float kTop = 60, kRowH = 22, kListTop = 112, kListBottom = 482, kPx = 1.5f;
        const float colW = (winW - 2 * kCreateMargin - 16) / 2;
        // One column: heading, count, FILTER, then the list box.
        auto column = [&](int k, const char* heading, const std::vector<std::string>& available,
                          const std::vector<std::string>& selected, const std::vector<std::string>& always,
                          std::string& filter, int field, bool editable, const char* note,
                          std::function<void(std::vector<std::string>)> apply) {
            const float cx = kCreateMargin + float(k) * (colW + 16);
            blockText(heading, cx, kTop, 1.8f, {205, 210, 225, 255});
            const auto key = [](const std::string& n) { return tak::hpi::MountSet::key(n); };
            const auto isAlways = [&](const std::string& n) {
                return std::any_of(always.begin(), always.end(), [&](const std::string& a) { return key(a) == key(n); });
            };
            std::vector<std::string> names = available;
            for (const auto& n : selected)
                if (std::find(names.begin(), names.end(), n) == names.end() && !isAlways(n)) names.push_back(n);
            std::sort(names.begin(), names.end());
            size_t enabled = always.size();
            for (const auto& n : selected) if (!isAlways(n)) ++enabled;
            const std::string count = std::to_string(enabled) + " / 64 ON";
            blockText(count, cx + colW - blockWidth(count, 1.4f), kTop + 4, 1.4f, {160, 170, 185, 255});
            // FILTER.
            const SDL_FRect fr{cx, kTop + 24, colW, kRowH};
            const bool active = lbField_ == field;
            SDL_SetRenderDrawColor(ren_, 24, 26, 34, 255); SDL_RenderFillRectF(ren_, &fr);
            SDL_SetRenderDrawColor(ren_, active ? 200 : 90, active ? 210 : 100, active ? 130 : 130, 255); SDL_RenderDrawRectF(ren_, &fr);
            if (filter.empty() && !active) blockText("FILTER", fr.x + 6, fr.y + 5, kPx, {110, 116, 132, 255});
            else blockText(tak::uiTextFitStart(filter + (active ? "_" : ""), kPx, fr.w - 12), fr.x + 6, fr.y + 5, kPx, {225, 230, 240, 255});
            lobbyHots_.push_back({fr, [this, field] { lbField_ = field; SDL_StartTextInput(); }});
            const std::string want = tak::mapfilter::lower(filter);
            // The rows: the always-enabled packs (locked), then the tickable ones.
            struct Row { std::string name; bool locked; };
            std::vector<Row> rows;
            for (const auto& a : always) if (tak::mapfilter::matches(a, "", want)) rows.push_back({a, true});
            for (const auto& n : names) if (tak::mapfilter::matches(n, "", want)) rows.push_back({n, false});
            const SDL_FRect box{cx, kListTop, colW, kListBottom - kListTop};
            modListRect_[k] = box;
            SDL_SetRenderDrawColor(ren_, 24, 26, 34, 255); SDL_RenderFillRectF(ren_, &box);
            SDL_SetRenderDrawColor(ren_, 70, 76, 96, 255); SDL_RenderDrawRectF(ren_, &box);
            if (note) { blockText(note, cx + 8, kListTop + 8, 1.4f, {150, 155, 165, 255}); return; }
            if (rows.empty()) {
                blockText(names.empty() && always.empty() ? (k ? "NO PACKS IN MODS/GAME" : "NO PACKS IN MODS/COSMETIC")
                                                          : "NO PACK MATCHES THE FILTER",
                          cx + 8, kListTop + 8, 1.4f, {150, 155, 165, 255});
                return;
            }
            const int visible = int((box.h - 4) / kRowH);
            modScroll_[k] = std::clamp(modScroll_[k], 0, std::max(0, int(rows.size()) - visible));
            const float sbW = int(rows.size()) > visible ? 10.0f : 0.0f;
            for (int r = 0; r < visible && modScroll_[k] + r < int(rows.size()); ++r) {
                const Row& row = rows[size_t(modScroll_[k] + r)];
                const SDL_FRect rr{cx + 2, kListTop + 2 + float(r) * kRowH, colW - 4 - sbW, kRowH - 2};
                const bool on = row.locked || std::find(selected.begin(), selected.end(), row.name) != selected.end();
                const bool live = editable && !row.locked;
                if (live) {
                    const std::string name = row.name;
                    lbBtn(rr.x, rr.y, rr.w, rr.h, "", true, [selected, name, on, apply, n = always.size()] {
                        auto selection = selected;
                        if (on) selection.erase(std::remove(selection.begin(), selection.end(), name), selection.end());
                        else if (selection.size() + n + 1 <= 64) selection.push_back(name);
                        apply(std::move(selection));
                    }, {40, 44, 58, 255}, kPx);
                }
                SDL_FRect check{rr.x + 8, rr.y + 5, 11, 11};
                SDL_SetRenderDrawColor(ren_, live ? 190 : 120, live ? 205 : 128, live ? 220 : 140, 255);
                SDL_RenderDrawRectF(ren_, &check);
                if (on) { check.x += 3; check.y += 3; check.w -= 6; check.h -= 6; SDL_RenderFillRectF(ren_, &check); }
                blockText(tak::uiTextFit(row.name, kPx, rr.w - 120), rr.x + 28, rr.y + 4, kPx,
                          live ? SDL_Color{225, 230, 240, 255} : SDL_Color{170, 200, 170, 255});
                if (row.locked) {
                    const char* tag = "ALWAYS ON";
                    blockText(tag, rr.x + rr.w - blockWidth(tag, 1.2f) - 8, rr.y + 5, 1.2f, {150, 185, 150, 255});
                }
            }
            if (sbW > 0) {   // where the visible rows sit in the list
                const float th = std::max(20.0f, box.h * float(visible) / float(rows.size()));
                const float ty = box.y + (box.h - th) * float(modScroll_[k]) / float(int(rows.size()) - visible);
                SDL_FRect thumb{box.x + box.w - sbW - 1, ty, sbW, th};
                SDL_SetRenderDrawColor(ren_, 90, 100, 130, 255); SDL_RenderFillRectF(ren_, &thumb);
            }
        };
        column(0, "COSMETIC MODS", availableCosmeticMods_, settings_ ? settings_->cosmeticModPacks : std::vector<std::string>{},
               alwaysCosmeticMods_, cosmeticModFilter_, 7, room.mission.empty(), nullptr,
               [this](std::vector<std::string> selection) {
                   if (!settings_) return;
                   settings_->cosmeticModPacks = std::move(selection); saveSettings(*settings_);
               });
        std::string guestNote;
        if (!gameOn) guestNote = "THE HOST TURNED GAME MODS OFF";
        else if (!host) {
            std::string names = mp_->modPackage() ? mp_->modPackage()->mapPath : "";
            guestNote = "THE HOST CHOOSES: " + (names.empty() ? std::string(mp_->modPackage() ? "NONE" : "WAITING FOR THE HOST") : names);
            guestNote = tak::uiTextFit(guestNote, 1.4f, colW - 16);
        }
        column(1, "GAME MODS", availableGameMods_, mp_->gameModPacks(), host ? alwaysGame : std::vector<std::string>{},
               gameModFilter_, 8, host && gameOn && !mp_->modBusy(), guestNote.empty() ? nullptr : guestNote.c_str(),
               [this, alwaysGame](std::vector<std::string> selection) {
                   mp_->setGameModPacks(selection);
                   if (settings_) {
                       // A saved tick hidden by a same-named always-enabled pack is kept (it
                       // applies again once that pack leaves always_enabled/).
                       for (const auto& n : settings_->gameModPacks)
                           if (std::find(selection.begin(), selection.end(), n) == selection.end() &&
                               std::any_of(alwaysGame.begin(), alwaysGame.end(), [&](const std::string& a) {
                                   return tak::hpi::MountSet::key(a) == tak::hpi::MountSet::key(n); }))
                               selection.push_back(n);
                       settings_->gameModPacks = std::move(selection); saveSettings(*settings_);
                   }
               });
        if (const auto status = mp_->roomModStatus(); gameOn && !status.empty())
            blockText(tak::uiTextFit(status, 1.3f, 600), kCreateMargin, kLobbyH - 32, 1.3f, {205, 190, 135, 255});
        lbBtn(kLobbyW - kCreateMargin - 120, kLobbyH - 40, 120, 30, "DONE", true, [this] { modsScreen_ = false; lbField_ = 0; SDL_StopTextInput(); });
    }

    void GameView::drawRoom(int winW, int winH) {
        const auto& room = mp_->room();
        // The slot table starts just under the page title (multiplayer: under the room's
        // name). The map's name heads the right column, above its preview.
        float x = 40, y = 54;
        if (!singlePlayer_) { blockText(room.name, x, y, 2.2f, {210, 210, 220, 255}); y += 34; }
        // Left-aligned with the preview under it; a long name runs on past the right edge.
        blockText("MAP  " + mapTitle(room.mapId), winW - 300.0f, 62, 1.8f, {180, 185, 195, 255});
        bool host = (room.hostId == mp_->myClientId());
        // MAP SCENARIO (259): the room plays its map's scenario when the option is on, or
        // for a Test Map copy, read from the verified package (the host's own until then).
        // The seats that scenario needs are tagged below and must be played to START.
        if (scenarioLobbyPackage_.lock() != mp_->mapPackage()) {
            scenarioLobbyPackage_ = mp_->mapPackage();
            // With this client's registry, so the Use Only count leaves out the names no
            // unit answers to, as setupMatch does (the create screen counts the same way).
            scenarioLobby_ = mp_->mapPackage() ? tak::net::maps::scenarioInfo(*mp_->mapPackage(), &registry_) : tak::sim::ScenarioInfo{};
        }
        const bool fileMap = room.mission.empty() && !tak::mapgen::isGeneratedMapId(room.mapId);
        const bool scenarioOn = fileMap && !room.opts.stressTest && !room.opts.benchmark && mp_->mapPackage() &&
                                scenarioLobby_.active(room.opts.mapScenario != 0);
        const uint8_t needSeats = scenarioOn ? scenarioLobby_.requiredSeats() : 0;
        // The room's own MAP SCENARIO line reads ON from the option alone (a joiner sees it
        // before the package is verified); the rule cells that follow it agree with it.
        const bool scenarioShown = fileMap && (room.opts.mapScenario != 0 || scenarioOn);
        // The room's map, with its starts numbered and coloured by who opens there,
        // heads the right column; the mod packs and the chat sit below it.
        const bool roomMap = room.mission.empty();
        // Single player has no chat under the mods, so its map takes the column's whole
        // width; a multiplayer room keeps the space for its chat.
        const float kRoomMap = singlePlayer_ ? 280 : 140, mapDrop = roomMap ? kRoomMap + 5 : 0;
        float modBottom=112+mapDrop;
        {
            // MODS, in the right column under the map. ALWAYS ON: the always-enabled
            // packs (mods/<kind>/always_enabled/<pack>), named and locked, never ticked.
            // The cosmetic ones load in every game (campaigns included).
            // COSMETIC: every player ticks their own mods/cosmetic packs (not in a
            // mission room); they are never sent anywhere and cannot change the game.
            // GAME: only the host of a skirmish room ticks mods/game packs; they and the
            // host's always-enabled game packs are sent to every player, and everyone
            // else sees the host's choice. A room is OFF only when a 0.8.12 or 0.8.13 host chose so.
            scanModPacks();
            const bool gameRoom=room.mission.empty();
            const bool gameOn=gameRoom && room.opts.gameMods;
            const auto& alwaysGame=mp_->alwaysGameModPacks();
            // A summary under the map: what is always on and what is ticked, each line as
            // many names as fit then +N, and a MODS button to the mods screen
            // (drawModsScreen), where both lists use the whole panel. The game mods' status
            // (transfer, error, VERIFIED) stays here.
            float py=87+mapDrop;
            // A label and its names, wrapping onto further lines down the column, pushing
            // what follows down; once the lines `after` it (one each, then the MODS button
            // and the status) would run out of room -- the panel's foot in single player,
            // the chat's room in multiplayer -- the last line ends with +N.
            const float bottom=singlePlayer_ ? float(winH)-24 : float(winH)-72-140;
            const auto namesLine=[&](const std::string& label,const std::vector<std::string>& names,SDL_Color c,int after) {
                constexpr float kLineW=280,kPx=1.3f,kLineH=18;
                const float reserve=float(after)*kLineH+4+28+20;
                const int maxLines=std::max(1,int((bottom-py-reserve)/kLineH));
                std::string line=label;
                bool lineHasName=false;
                int lines=1;
                for(size_t i=0;i<names.size();++i){
                    const std::string piece=(lineHasName?", ":"")+names[i];
                    const bool last=i+1==names.size();
                    // On the last line allowed, keep room for " +N" unless this is the last name.
                    const float need=blockWidth(line+piece+(lines==maxLines && !last?" +99":""),kPx);
                    if(need<=kLineW || !lineHasName){
                        line+=lineHasName||line==label?piece:names[i];
                        lineHasName=true;
                        if(blockWidth(line,kPx)>kLineW)line=tak::uiTextFit(line,kPx,kLineW);
                        continue;
                    }
                    if(lines==maxLines){line+=" +"+std::to_string(names.size()-i);break;}
                    blockText(line,winW-296.0f,py,kPx,c);py+=kLineH;
                    line="  "+names[i];lineHasName=true;++lines;
                }
                blockText(line,winW-296.0f,py,kPx,c);py+=kLineH;
            };
            const auto splitNames=[](const std::string& joined) {
                std::vector<std::string> out;
                size_t at=0;
                while(at<joined.size()){
                    size_t comma=joined.find(',',at);
                    std::string n=joined.substr(at,comma==std::string::npos?std::string::npos:comma-at);
                    while(!n.empty() && n.front()==' ')n.erase(n.begin());
                    if(!n.empty())out.push_back(n);
                    if(comma==std::string::npos)break;
                    at=comma+1;
                }
                return out;
            };
            blockText("MODS",winW-296.0f,py,1.6f,{205,210,225,255});py+=22;
            std::vector<std::string> always=alwaysCosmeticMods_;
            if(gameOn && host)always.insert(always.end(),alwaysGame.begin(),alwaysGame.end());
            if(!always.empty())namesLine("ALWAYS ON: ",always,{170,200,170,255},gameRoom?2:0);
            if(gameRoom) {
                std::vector<std::string> cosmetic;
                if(settings_)for(const auto& n:settings_->cosmeticModPacks)
                    if(std::find(availableCosmeticMods_.begin(),availableCosmeticMods_.end(),n)!=availableCosmeticMods_.end())cosmetic.push_back(n);
                if(cosmetic.empty())blockText("COSMETIC: NONE",winW-296.0f,py,1.3f,{150,155,165,255}),py+=18;
                else namesLine("COSMETIC: ",cosmetic,{200,205,215,255},1);
                if(!gameOn)blockText("GAME: OFF (HOST)",winW-296.0f,py,1.3f,{150,155,165,255}),py+=18;
                else if(host){
                    if(mp_->gameModPacks().empty())blockText("GAME: NONE",winW-296.0f,py,1.3f,{150,155,165,255}),py+=18;
                    else namesLine("GAME: ",mp_->gameModPacks(),{200,205,215,255},0);
                } else {
                    const std::string names=mp_->modPackage()?mp_->modPackage()->mapPath:"";
                    if(names.empty())blockText(mp_->modPackage()?"GAME (HOST): NONE":"GAME (HOST): WAITING FOR THE HOST",winW-296.0f,py,1.3f,{180,190,205,255}),py+=18;
                    else namesLine("GAME (HOST): ",splitNames(names),{180,190,205,255},0);
                }
                py+=4;
                lbBtn(winW-300.0f,py,280,22,"MODS...",true,[this]{modsScreen_=true;lbField_=0;SDL_StopTextInput();},
                      {60,66,86,255},1.6f);
                py+=28;
                if(const auto status=mp_->roomModStatus();gameOn && !status.empty()){
                    blockText(tak::uiTextFit(status,1.2f,280),winW-296.0f,py,1.2f,{205,190,135,255});py+=16;}
            }
            modBottom=py+4;
        }
        // SP spectate ("watch the AIs"): the instant the host lands in the room, fill
        // every open capacity slot with an AI -- each on its own team/colour and a
        // RANDOM race -- so the watcher gets a full free-for-all with no hand-seating.
        // Closed slots (no start position on this map) are skipped. One-shot
        // (specAutoSeated_), so the host can still tweak/close slots afterwards.
        if (singlePlayer_ && spSpectate_ && host && room.mySlot < 0 && !specAutoSeated_) {
            std::mt19937 rng(uint32_t(SDL_GetTicks64()) ^ (room.id * 2654435761u));
            for (int i = 0; i < tak::net::kMaxSlots; ++i)
                if (room.slots[i].type == 0)   // open capacity slot
                    mp_->setSlot(i, 2, uint8_t(rng() % 5), uint8_t(i), uint8_t(i), 1, aiLevelEnv());
            specAutoSeated_ = true;
        }
        // START LOCATIONS: BY SLOT gives slot N the map's StartPos N. A seat past the map's
        // last start opens at a fallback, worked out by the sim's own rule and tagged under
        // its name. Seats are normally capped at the start count, so this shows only on a
        // map with fewer than two starts or a scenario map with more seats than starts. A
        // Test Map scenario that places units opens its seats at them, so it gets no tags.
        std::vector<int> bySlotSpot;
        int startCount = -1;
        if (tak::net::normalizeStartMode(room.opts.randomStarts) == tak::net::kStartsBySlot &&
            room.mission.empty() && !room.opts.benchmark) {
            const RoomStarts starts = roomStarts();
            if (starts.count >= 0 && (!starts.placed || room.opts.stressTest)) {
                startCount = starts.count;
                bySlotSpot = tak::sim::startSpotsFor(tak::net::matchConfigFor(room.opts, room.slots, 0).slots,
                                                     size_t(startCount), tak::sim::StartMode::BySlot);
            }
        }
        // slot table
        const char* typeName[4] = {"OPEN", "HUMAN", "AI", "CLOSED"};
        for (int i = 0; i < tak::net::kMaxSlots; ++i) {
            const auto& s = room.slots[i];
            bool mine = (i == room.mySlot);
            SDL_FRect row{x, y, winW - 360.0f, 30};
            SDL_SetRenderDrawColor(ren_, mine ? 40 : 26, mine ? 48 : 30, mine ? 66 : 40, 255);
            SDL_RenderFillRectF(ren_, &row);
            char sn[12]; std::snprintf(sn, sizeof sn, "%d", i + 1);
            blockText(sn, x + 8, y + 8, 1.8f, {150, 155, 170, 255});
            // type (host may cycle open<->closed on empty slots)
            SDL_Color tcol = s.type == 1 ? SDL_Color{200, 230, 200, 255}
                           : s.type == 2 ? SDL_Color{230, 220, 150, 255}
                                         : SDL_Color{130, 135, 150, 255};
            // Each column's text fits its column (x+34 type, x+100 name, x+178 level,
            // x+260 faction), shrinking rather than running into the next.
            auto cell = [&](const std::string& t, float cx, float cy, float px, float colW, SDL_Color c) {
                const float fpx = tak::uiTextFitPx(t, px, colW * 0.97f, px * 0.7f);
                blockText(tak::uiTextFit(t, fpx, colW), cx, cy + (px - fpx) * 3.5f, fpx, c);
            };
            cell(typeName[s.type % 4], x + 34, y + 8, 1.6f, 56, tcol);
            if (host && s.type != 1) {
                // Host cycles an empty slot OPEN -> AI -> CLOSED. AI opponents are
                // seated (and run) on the server; this is how you set up multi-AI
                // games (including single-player vs several AIs).
                SDL_FRect tb{x + 34, y + 6, 60, 18};
                lobbyHots_.push_back({tb, [this, i, t = s.type](int direction) {
                    uint8_t nt = direction > 0 ? (t == 0 ? 2 : t == 2 ? 3 : 0)
                                               : (t == 0 ? 3 : t == 3 ? 2 : 0);
                    const auto& s2 = mpRoom().slots[i];
                    mp_->setSlot(i, nt, s2.faction, s2.color, s2.team, 0, s2.aiLevel); }});
            }
            // Single player has no READY column, so the side / colour / team columns move up
            // to the MOVE HERE / KICK column (row.w - 94) and the name takes the room.
            const float wide = singlePlayer_ ? 72.0f : 0.0f;
            if (s.type == 1) cell(s.name, x + 100, y + 8, 1.8f, 154 + wide, {225, 228, 236, 255});
            else if ((s.type == 0 || s.type == 3) && (needSeats >> i & 1))
                blockText("NEEDED BY SCENARIO", x + 100, y + 9, 1.4f, {235, 205, 120, 255});
            else if (s.type == 2) {
                cell(s.name.empty() ? "Computer" : s.name, x + 100, y + 8, 1.6f, 74 + wide, {210, 200, 150, 255});
                // AI difficulty (host cycles PASSIVE -> EASY -> NORMAL -> HARD -> ABSURD ->
                // DEFENSIVE -> ABSURDLY DEFENSIVE -> RETAIL: kCycle, which is not the wire
                // aiLevel order that diffName and diffCol follow). RETAIL is level 6, internally Classic: the 1999 game's own AI, driven by its
                // ai/default.txt; a new AI seat still starts NORMAL.
                constexpr int kLevels = tak::ai::kAiLevels;
                static const char* diffName[kLevels] = {"DEFENSIVE", "EASY", "NORMAL", "HARD", "ABSURD",
                                                        "ABSURDLY DEFENSIVE", "RETAIL", "PASSIVE"};
                static const SDL_Color diffCol[kLevels] = {
                    {150, 180, 210, 255},   // defensive (calm blue)
                    {150, 200, 150, 255},   // easy     (green)
                    {210, 200, 150, 255},   // normal   (neutral gold)
                    {225, 150, 140, 255},   // hard     (red)
                    {230, 120, 220, 255},   // absurd   (magenta)
                    {165, 140, 245, 255},   // absurdly defensive (violet)
                    {205, 165, 105, 255},   // retail (Classic, bronze): the retail AI's port
                    {170, 195, 175, 255}};  // passive (pale green): Easy that stays home
                static constexpr uint8_t kCycle[kLevels] = {7, 1, 2, 3, 4, 0, 5, 6};
                const uint8_t lvl = uint8_t(s.aiLevel % kLevels);
                // Two words that overrun the 82 px column stack in two smaller lines.
                if (const char* gap = std::strchr(diffName[lvl], ' ')) {
                    blockText(std::string(diffName[lvl], gap), x + 178 + wide, y + 3, 1.1f, diffCol[lvl]);
                    blockText(gap + 1, x + 178 + wide, y + 13, 1.1f, diffCol[lvl]);
                } else
                    blockText(diffName[lvl], x + 178 + wide, y + 9, 1.4f, diffCol[lvl]);
                if (host) { SDL_FRect db{x + 176 + wide, y + 2, 82, 26};
                    lobbyHots_.push_back({db, [this, i](int direction) { const auto& s2 = mpRoom().slots[i];
                        const int at = int(std::find(kCycle, kCycle + kLevels, s2.aiLevel) - kCycle);
                        mp_->setSlot(i, s2.type, s2.faction, s2.color, s2.team, s2.ready,
                                     kCycle[(at + direction + kLevels) % kLevels]); }}); }
            }
            if (i >= startCount && size_t(i) < bySlotSpot.size() && bySlotSpot[size_t(i)] >= 0) {
                const int spot = bySlotSpot[size_t(i)];
                blockText("NO START " + std::to_string(i + 1) + ": " +
                          (spot < startCount ? "USES START " + std::to_string(spot + 1) : std::string("NEAR MAP CENTRE")),
                          x + 100, y + 21.5f, 1.1f, {235, 205, 120, 255});
            }
            // faction / color / team edit: your own row, or (host) any AI row.
            bool canEdit = (mine || (host && s.type == 2));
            cell(factionName(s.faction), x + 260 + wide, y + 8, 1.6f, 94, {200, 205, 215, 255});
            if (canEdit) { SDL_FRect fb{x + 260 + wide, y + 6, 90, 18};
                lobbyHots_.push_back({fb, [this, i](int direction) { const auto& s2 = mpRoom().slots[i];
                    mp_->setSlot(i, s2.type, (s2.faction + direction + 5) % 5, s2.color, s2.team, s2.ready, s2.aiLevel); }}); }
            colorSwatch(x + 360 + wide, y + 5, 20, s.color, canEdit ? std::function<void(int)>([this, i](int direction) {
                // Cycle to the next colour NOT already held by another used slot --
                // landing on a taken colour just blocked READY, which was a trap.
                const auto& r2 = mpRoom();
                const auto& s2 = r2.slots[i];
                uint8_t next = s2.color;
                for (int step = 1; step <= 10; ++step) {
                    uint8_t cand = uint8_t((s2.color + direction * step + 10) % 10);
                    bool taken = false;
                    for (int k = 0; k < tak::net::kMaxSlots; ++k)
                        if (k != i && (r2.slots[k].type == 1 || r2.slots[k].type == 2) &&
                            r2.slots[k].color == cand) { taken = true; break; }
                    if (!taken) { next = cand; break; }
                }
                mp_->setSlot(i, s2.type, s2.faction, next, s2.team, s2.ready, s2.aiLevel); }) : nullptr);
            char tm[8]; std::snprintf(tm, sizeof tm, "T%d", s.team + 1);
            blockText(tm, x + 392 + wide, y + 8, 1.8f, {200, 205, 215, 255});
            if (canEdit) { SDL_FRect teb{x + 392 + wide, y + 6, 34, 18};
                lobbyHots_.push_back({teb, [this, i](int direction) { const auto& s2 = mpRoom().slots[i];
                    mp_->setSlot(i, s2.type, s2.faction, s2.color, uint8_t((s2.team + direction + tak::net::kMaxSlots) % tak::net::kMaxSlots), s2.ready, s2.aiLevel); }}); }
            if (s.type == 1 && !singlePlayer_) {   // SP: the player is always ready, no column
                SDL_Color rc = s.ready ? SDL_Color{130, 230, 140, 255} : SDL_Color{120, 125, 135, 255};
                blockText(s.ready ? "READY" : "NOT READY", x + 440, y + 8, 1.6f, rc);
            }
            // host kick button for other humans
            if (host && s.type == 1 && !mine) {
                lbBtn(x + row.w - 54, y + 3, 50, 22, "KICK", true, [this, i] { mp_->kick(i); });
            }
            // Any seated player may take an OPEN seat (Msg::MoveSeat): the seat is the
            // start under BY SLOT, and the order under FIXED. Open seats are always
            // inside the map's capacity (the server closes the rest). Not in a campaign
            // mission, whose seats are given.
            if (room.mission.empty() && room.mySlot >= 0 && s.type == 0)
                lbBtn(x + row.w - 94, y + 3, 90, 22, "MOVE HERE", true, [this, i] { mp_->moveSeat(i); });
            y += 30;   // slot row pitch: 8 rows must leave room for the option toggles below
        }
        y += 10;
        // controls
        float bx = x;
        if (singlePlayer_) {
            // SP: no READY button -- the player is always ready. Keep the slot marked
            // ready (once, self-limiting) so the host's START enables.
            if (room.mySlot >= 0 && !room.slots[room.mySlot].ready) {
                const auto& s = room.slots[room.mySlot];
                mp_->setSlot(room.mySlot, 1, s.faction, s.color, s.team, 1, s.aiLevel);
            }
        } else {
            bool iAmReady = room.mySlot >= 0 && room.slots[room.mySlot].ready;
            lbBtn(x, y, 130, 30, iAmReady ? "UNREADY" : "READY", room.mySlot >= 0, [this, iAmReady] {
                const auto& s = mpRoom().slots[mpRoom().mySlot];
                mp_->setSlot(mpRoom().mySlot, 1, s.faction, s.color, s.team, iAmReady ? 0 : 1, s.aiLevel); });
            bx = x + 142;
        }
        // A scenario that places its own armies opens nobody at the map's starts.
        if (roomMap) drawRoomPreview({winW - 300.0f, 84, kRoomMap, kRoomMap}, scenarioOn && scenarioLobby_.placed > 0);
        // A room playing its map's scenario may start solo, once every seat the scenario
        // needs is played; ordinary skirmishes need two. A multiplayer room (not single
        // player's private one) also needs two human players: AI alone is single player.
        int humans = 0;
        for (const auto& s : room.slots) humans += s.type == 1;
        const bool needHuman = !singlePlayer_ && humans < 2;
        bool canStart = host && startValid(room, scenarioOn, needSeats) && room.mapsReady && !mp_->modPending() &&
            (!room.opts.gameMods || bool(mp_->modPackage())) && !needHuman;
        // The server takes a moment to build the game: show the click landed.
        if (mp_->startPending() || mp_->starting()) lbBtn(bx, y, 130, 30, "STARTING...", true, nullptr, {70, 110, 70, 255});
        else lbBtn(bx, y, 130, 30, "START", canStart, [this] { mp_->startGame(); }, {70, 110, 70, 255});
        lbBtn(bx + 142, y, 120, 30, "LEAVE", true, [this] {
            // A LOCAL SERVER's host leaving closes the game for everyone: ask first.
            if (lanHost_ && lanOthersInRoom() > 0) { lanLeavePrompt_ = true; return; }
            leaveRoom();
        });
        // Room readiness also includes game mods and other players. A
        // completed local map is not progress to display while packs change;
        // their status is already shown beside the pack list.
        // The status beside the buttons stops short of the map column (winW - 300),
        // shrunk before it is cut, as the refusal line below is.
        const auto status = [&](const std::string& text) {
            const float room = std::max(40.0f, winW - 312.0f - (bx + 280));
            const float px = tak::uiTextFitPx(text, 1.3f, room, 0.9f);
            blockText(tak::uiTextFit(text, px, room), bx + 280, y + 9, px, {235, 205, 120, 255});
        };
        const auto& mapStatus = mp_->mapStatus();
        if (!room.mapsReady && !mp_->modPending() && !mp_->modBusy() &&
            mapStatus != "MAP READY" && mapStatus != "MAP VERIFIED")
            status(mapStatus.empty() ? "WAITING FOR MAP VERIFICATION" : mapStatus);
        else if (host && needHuman && !mp_->startPending() && !mp_->starting())
            status("NEEDS 2+ HUMAN PLAYERS");
        // Why the server refused the last START (a needed seat, setupMatch's refusal...).
        if (!mp_->startError().empty()) {
            // Shrunk before it is cut: a refusal that names a unit and the way out (a
            // scenario unit CRUSADES leaves out) should be read whole.
            const std::string why = "CANNOT START: " + mp_->startError();
            const float px = tak::uiTextFitPx(why, 1.1f, winW - 360.0f, 0.8f);
            blockText(tak::uiTextFit(why, px, winW - 360.0f), x, y + 33, px, {255, 130, 110, 255});
        }
        // Creation settings are read-only here and come from the room, not
        // this client's saved preferences. Keep every rule visible with 8 slots.
        y += 44;
        const SDL_Color infoColor{205,210,225,255};
        // Six rows of two columns at a 16.5 pitch (POWER CODES and MAP SCENARIO are the
        // sixth), above a MAP SCENARIO's details (from y+99) or a generated map's recipe
        // (from y+98).
        constexpr float kInfoPitch = 16.5f;
        auto info = [&](int row, int column, const std::string& label) {
            blockText(label,x+column*300.0f,y+row*kInfoPitch,1.6f,infoColor);
        };
        auto onOff = [](bool value) { return value ? "ON" : "OFF"; };
        info(0,0,std::string("CRUSADES: ")+onOff(room.opts.crusades));
        info(0,1,std::string("DOUBLE SIGHT/RADAR: ")+onOff(room.opts.doubleSight));
        info(1,0,"UNIT CAP: "+std::to_string(room.opts.unitCap));
        info(1,1,scenarioShown ? std::string("MONARCH EXPENDABLE: SCENARIO")
                            : std::string("MONARCH EXPENDABLE: ")+onOff(room.opts.monarchExpendable));
        info(2,0,std::string("ALLOW SPEED CHANGE: ")+onOff(room.opts.speedUnlock));
        static const char* fogNames[]={"NOT EXPLORED","EXPLORED","FULL VISION"};
        info(2,1,std::string("FOG OF WAR: ")+fogNames[std::min<int>(room.opts.fogExplored,2)]);
        info(3,0,startLocationsLabel(room.opts.randomStarts));
        info(4,0,pathfindingLabel(room.opts.pathfindingMode));
        if(room.mission.empty())
            info(5,0,std::string("POWER CODES: ")+onOff(room.opts.powerCodes));
        // ALLOW SPECTATORS is the one room rule the host may still flip here: it only
        // decides who may WATCH once the game runs, so no player has planned around it.
        // The server adopts it until START and refuses Spectate while it is off.
        // For the host it lights up under the pointer (the button hover colour), the one
        // hint that this line of the read-only list is a toggle.
        if(!singlePlayer_) {
            const std::string label=std::string("ALLOW SPECTATORS: ")+onOff(room.opts.allowSpectators);
            const SDL_FRect hit{x+300.0f,y+4*kInfoPitch-2,blockWidth(label,1.6f),18};
            blockText(label,hit.x,y+4*kInfoPitch,1.6f,host && lbHot(hit) ? SDL_Color{180,200,240,255} : infoColor);
            if(host) lobbyHots_.push_back({hit,[this](int) {
                auto o=mpRoom().opts; o.allowSpectators=o.allowSpectators?0:1; mp_->setGameOptions(o); }});
        }
        if(singlePlayer_)
            info(3,1,std::string("SPECTATE (WATCH AIS): ")+onOff(mp_->isSpectator()));
        else {
            const auto& games=mp_->games();
            const auto game=std::find_if(games.begin(),games.end(),[&](const auto& g){return g.id==room.id;});
            // Show whether access is protected, never the password itself.
            if(game!=games.end() || host)
                info(3,1,std::string("PASSWORD: ")+((game!=games.end()?game->passworded:!createPass_.empty())?"SET":"NONE"));
        }
        // MAP SCENARIO, under the grid for a file map (the rows a generated map's recipe
        // uses): the rule, and when on, what it changes and the seats it needs.
        if(fileMap) {
            const SDL_Color amber{235,205,120,255};
            const bool testCopy=mp_->mapPackage() && scenarioLobby_.marker && scenarioLobby_.runnable();
            const bool wanted=room.opts.mapScenario!=0 || testCopy;
            // A Test Map copy whose only scenario content is a Use Only list: no scenario,
            // but the copy's opt-in applies the list (not in a stress or benchmark room).
            const bool testUseOnly=mp_->mapPackage() && scenarioLobby_.marker && !scenarioLobby_.hasScenario &&
                                   scenarioLobby_.useOnlyTypes>=0 && !room.opts.stressTest && !room.opts.benchmark;
            // The rule itself is a grid line like the others: the right column's last, under
            // SPECTATE (single player) or ALLOW SPECTATORS (multiplayer).
            info(singlePlayer_ ? 4 : 5,1,testCopy ? "MAP SCENARIO: ON (TEST MAP)" : wanted ? "MAP SCENARIO: ON" : "MAP SCENARIO: OFF");
            // Its details, in reading order across two columns, in the three rows between
            // the grid's last row (it ends near y+94) and the canvas edge.
            int detail=0;
            auto rule=[&](const std::string& text,bool warning=false) {
                if(detail>=6)return;
                blockText(text,x+float(detail%2)*300.0f,y+99+float(detail/2)*12.5f,1.2f,warning ? amber : infoColor);
                ++detail;
            };
            if(testUseOnly && !wanted) rule("TEST MAP: "+tak::sim::scenarioUseOnlyLine(scenarioLobby_,false));
            else if(wanted && !mp_->mapPackage()) rule("CHECKING SCENARIO...");
            else if(wanted && !scenarioOn && !room.opts.stressTest && !room.opts.benchmark) {
                const auto why=tak::sim::scenarioReason(scenarioLobby_);
                rule(why.empty() ? std::string("NO SCENARIO TO PLAY") : why.front(),true);
            } else if(scenarioOn) {
                const auto lines=tak::sim::scenarioSummary(scenarioLobby_,false);   // placed, seats, use only, endings
                bool missing=false;
                for(int i=0;i<tak::net::kMaxSlots;++i)
                    if((needSeats>>i & 1) && room.slots[i].type!=1 && room.slots[i].type!=2) missing=true;
                if(lines.size()>0) rule(lines[0].text);
                if(lines.size()>1) rule(lines[1].text,missing);
                if(lines.size()>2) rule(lines[2].text);
                if(lines.size()>3) rule(lines[3].text,lines[3].warning);
                // Placements replace Monarchs, so a seated player the scenario gives no
                // units starts with nothing.
                uint8_t empty=0;
                for(int i=0;i<tak::net::kMaxSlots;++i)
                    if((room.slots[i].type==1 || room.slots[i].type==2) && !(scenarioLobby_.placedSeats>>i & 1)) empty|=uint8_t(1u<<i);
                if(scenarioLobby_.placed>0 && empty) rule("NO PLACED UNITS: PLAYER "+tak::sim::seatList(empty),true);
            }
        }
        if(tak::mapgen::isGeneratedMapId(room.mapId)) {
            try {
                const auto gp=tak::mapgen::decodeMapId(room.mapId);
                const auto level=[](uint8_t v){return v==0?"NONE":v<86?"LOW":v<171?"MED":"HIGH";};
                std::string scatter=tak::mapgen::scatterName(gp.manaScatter);
                for(char& ch:scatter)ch=char(std::toupper(static_cast<unsigned char>(ch)));
                static const char* mapTypes[]={"ARAMON","TAROS","VERUNA","ZHON","CREON"};
                const std::string shape=std::string("MAP TYPE: ")+mapTypes[gp.mapType%tak::mapgen::kMapTypes]+
                    "  LAYOUT: "+tak::mapgen::layoutName(gp.layout)+"  SIZE: "+
                    std::to_string(gp.widthCells/32)+" X "+std::to_string(gp.heightCells/32)+
                    "  PLAYERS: "+std::to_string(gp.players);
                const std::string terrain=std::string("FORESTS: ")+level(gp.treeDensity)+
                    "  ROCKS: "+level(gp.rockDensity)+"  MANA: "+level(gp.manaDensity)+
                    "  WATER: "+(tak::mapgen::automaticWater(gp.layout)?"AUTO":level(gp.waterDensity))+
                    "  HILLS: "+level(gp.reliefDensity);
                blockText(shape,x,y+98,1.1f,infoColor);
                blockText(terrain,x,y+112,1.1f,infoColor);
                blockText("MAP SEED: "+std::to_string(gp.seed)+"  SCATTERED MANA: "+scatter,x,y+126,1.1f,infoColor);
                const auto kit=[&](uint8_t v,uint8_t which){return tak::mapgen::kitAvailable(gp.mapType,which)?level(v):"NONE";};
                blockText(std::string("PLATEAUS: ")+(tak::mapgen::automaticPlateaus(gp.layout)?"AUTO":kit(gp.plateauDensity,tak::mapgen::KitPlateaus))+
                          "  MOUNTAINS: "+kit(gp.mountainDensity,tak::mapgen::KitMountains)+
                          (gp.mapType==tak::mapgen::Zhon?"  VILLAGES: ":"  TOWNS: ")+kit(gp.townDensity,tak::mapgen::KitTowns),x,y+140,1.1f,infoColor);
            } catch(const std::exception&) {} // Invalid map IDs cannot start.
        }
        // chat panel on the right (multiplayer only -- there's no one to chat with in SP)
        if (!singlePlayer_) {
            // The message box runs from under the mods down to the line the text field and
            // SEND share.
            float chx = winW - 300.0f, chy = modBottom, chw = 280;
            constexpr float kLineH = 16, kSb = 8;
            const float inputY = winH - 52.0f;
            SDL_SetRenderDrawColor(ren_, 22, 24, 32, 255);
            SDL_FRect cp{chx, chy, chw, std::max(40.0f, inputY - 6 - chy)}; SDL_RenderFillRectF(ren_, &cp);
            chatRect_ = cp;
            // Every message wrapped to the box (measured, so it fits in either UI FONT),
            // oldest first; chatScroll_ is how many rows up from the newest the view sits
            // (the mouse wheel over the box), with a scroll bar once they overflow.
            std::vector<std::string> rows;
            for (const auto& m : chatLog_)
                for (auto& r : tak::uiTextWrap(m.first + ": " + m.second, 1.4f, chw - 16 - kSb)) rows.push_back(std::move(r));
            const int visible = std::max(1, int((cp.h - 8) / kLineH));
            const int total = int(rows.size());
            chatScroll_ = std::clamp(chatScroll_, 0, std::max(0, total - visible));
            const int last = total - 1 - chatScroll_;   // the bottom row on show
            float ly = cp.y + 4 + float(visible - 1) * kLineH;
            for (int i = last; i >= 0 && i > last - visible; --i, ly -= kLineH)
                blockText(rows[size_t(i)], chx + 8, ly, 1.4f, {200, 205, 215, 255});
            if (total > visible) {
                const float th = std::max(16.0f, cp.h * float(visible) / float(total));
                const float ty = cp.y + (cp.h - th) * float(total - visible - chatScroll_) / float(total - visible);
                SDL_FRect thumb{cp.x + cp.w - kSb - 2, ty, kSb, th};
                SDL_SetRenderDrawColor(ren_, 90, 100, 130, 255); SDL_RenderFillRectF(ren_, &thumb);
            }
            // The text field (no label): Enter or SEND sends.
            {
                const SDL_FRect r{chx, inputY, chw - 70, 24};
                const bool active = lbField_ == 4;
                SDL_SetRenderDrawColor(ren_, 24, 26, 34, 255); SDL_RenderFillRectF(ren_, &r);
                SDL_SetRenderDrawColor(ren_, active ? 200 : 90, active ? 210 : 100, active ? 130 : 130, 255); SDL_RenderDrawRectF(ren_, &r);
                const float vpx = tak::modernUiText() ? 1.6f : 1.8f;
                const std::string shown = tak::uiTextFitStart(chatDraft_ + (active ? "_" : ""), vpx, r.w - 12);
                blockText(shown, r.x + 6, r.y + (r.h - 7 * vpx - (tak::modernUiText() ? tak::uiTextDescent(vpx) : 0)) / 2, vpx,
                          {225, 230, 240, 255});
                lobbyHots_.push_back({r, [this] { lbField_ = 4; SDL_StartTextInput(); }});
            }
            lbBtn(chx + chw - 62, inputY, 56, 24, "SEND", !chatDraft_.empty(), [this] {
                mp_->chat(chatDraft_); chatDraft_.clear(); chatScroll_ = 0; });
        }
    }

    GameView::RoomStarts GameView::roomStarts() {
        const auto& room = mp_->room();
        // A generated map has a start for each player its recipe (the map id) names.
        if (tak::mapgen::isGeneratedMapId(room.mapId)) {
            try { return {int(tak::mapgen::decodeMapId(room.mapId).players), false}; }
            catch (const std::exception&) { return {}; }   // invalid ids cannot start
        }
        // A file map: read its verified package (the host's own until then) once.
        const auto& package = mp_->mapPackage();
        if (!package || !package->files) return {};
        if (roomStartsPackage_.lock() != package) {
            roomStartsPackage_ = package;
            roomStarts_ = {};
            try {
                tak::hpi::Vfs view; view.setMapFiles(package->files);
                roomStarts_.count = int(tak::sim::parseStartPositions(view, package->mapPath).size());
                if (tak::net::maps::authoredScenario(*package))
                    if (const auto crt = view.tryRead(tak::vpath::replaceExtension(package->mapPath, ".crt")))
                        roomStarts_.placed = !tak::crt::parse(*crt).units.empty();
            } catch (const std::exception&) {}
        }
        return roomStarts_;
    }

    void GameView::drawStartMarkers(const SDL_FRect& img, const std::vector<int>& colorOfStart, float maxRadius) {
        if (mapPreviewStarts_.empty() || img.w <= 0 || img.h <= 0) return;
        // Sized from the preview (the full-window map view allows larger), so the create preview (192 to 288 px) and the room's smaller
        // one both read: a black rim and a white ring keep it legible on any terrain.
        const float r = std::clamp(std::min(img.w, img.h) * 0.06f, 7.0f, maxRadius);
        auto disc = [&](float cx, float cy, float rad, SDL_Color c) {
            constexpr int kSeg = 20;
            SDL_Vertex v[kSeg + 1];
            int ix[kSeg * 3];
            v[0] = {{cx, cy}, c, {0, 0}};
            for (int k = 0; k < kSeg; ++k) {
                const float a = float(k) * 6.2831853f / kSeg;
                v[k + 1] = {{cx + rad * std::cos(a), cy + rad * std::sin(a)}, c, {0, 0}};
                ix[k * 3] = 0; ix[k * 3 + 1] = k + 1; ix[k * 3 + 2] = (k + 1) % kSeg + 1;
            }
            SDL_RenderGeometry(ren_, nullptr, v, kSeg + 1, ix, kSeg * 3);
        };
        for (size_t k = 0; k < mapPreviewStarts_.size(); ++k) {
            const auto p = tak::startmarkers::place(mapPreviewStarts_[k], mapPreviewWorldW_, mapPreviewWorldH_,
                                                    img.x, img.y, img.w, img.h, r + 2);
            const int color = k < colorOfStart.size() ? colorOfStart[k] : -1;
            // Neutral (nobody, or nobody knows yet) is a dark disc: the slot palette has
            // both a white and a grey, so neither can stand for "no one".
            const SDL_Color fill = color >= 0 ? playerColors_[color % 10] : SDL_Color{34, 36, 46, 255};
            disc(p.x, p.y, r + 2, {0, 0, 0, 255});
            disc(p.x, p.y, r + 1, {235, 235, 240, 255});
            disc(p.x, p.y, r, fill);
            const std::string n = std::to_string(k + 1);
            const float px = r * (n.size() > 1 ? 0.15f : 0.19f);
            const bool light = fill.r * 299 + fill.g * 587 + fill.b * 114 > 150000;
            blockText(n, p.x - blockWidth(n, px) / 2 + px / 2, p.y - 3.5f * px, px,
                      light ? SDL_Color{16, 16, 20, 255} : SDL_Color{250, 250, 252, 255});
        }
    }

    void GameView::drawRoomPreview(const SDL_FRect& box, bool authored) {
        const auto& room = mpRoom();
        // A verified map package is the map every peer plays (a joiner may not have it,
        // or may hold a different map under the same name); otherwise the room's map is
        // in the VFS, or it is a generated id. Rebuilt only when the map changes.
        const auto pkg = mp_->mapPackage();
        const bool gen = tak::mapgen::isGeneratedMapId(room.mapId);
        const std::string key = "room:" + room.mapId + "#" + (pkg && !gen ? pkg->digest : std::string());
        if (mapPreviewFor_ != key) {
            if (pkg && !gen && pkg->files) {
                tak::hpi::Vfs view; view.setMapFiles(pkg->files);
                buildMapPreview(pkg->mapPath, &view);
            } else {
                buildMapPreview(gen ? room.mapId : tak::hpi::findMap(vfs_, room.mapId));
            }
            mapPreviewFor_ = key;
        }
        SDL_SetRenderDrawColor(ren_, 18, 20, 28, 255); SDL_RenderFillRectF(ren_, &box);
        if (mapPreviewTex_ && mapPreviewW_ > 0) {
            const float sc = std::min(box.w / float(mapPreviewW_), box.h / float(mapPreviewH_));
            const float iw = mapPreviewW_ * sc, ih = mapPreviewH_ * sc;
            const SDL_FRect dst{box.x + (box.w - iw) / 2, box.y + (box.h - ih) / 2, iw, ih};
            SDL_RenderCopyF(ren_, mapPreviewTex_, nullptr, &dst);
            // Each start in the colour of the slot that opens there. An authored scenario
            // places its own forces, so its starts belong to nobody.
            std::vector<int> colors;
            if (!authored)
                for (int slot : tak::startmarkers::occupants(room.opts, room.slots, mapPreviewStarts_.size()))
                    colors.push_back(slot >= 0 ? room.slots[slot].color : -1);
            drawStartMarkers(dst, colors);
            roomPreviewColors_ = std::move(colors);
            if (!genPreviewPending_) {
                lobbyHots_.push_back({box, [this] { openBigMap(); }});   // the map, full window
                drawClickHint(box);
            }
        }
        SDL_SetRenderDrawColor(ren_, 70, 76, 96, 255); SDL_RenderDrawRectF(ren_, &box);
    }

    // Under the pointer, a preview that opens the full-window map says so: CLICK on a
    // dark band across its middle.
    void GameView::drawClickHint(const SDL_FRect& box) {
        if (!lbHot(box)) return;
        const char* note = "CLICK";
        const SDL_FRect band{box.x, box.y + box.h / 2 - 12, box.w, 24};
        SDL_SetRenderDrawColor(ren_, 12, 14, 20, 190); SDL_RenderFillRectF(ren_, &band);
        blockText(note, box.x + (box.w - blockWidth(note, 1.6f)) / 2, band.y + 6, 1.6f, {225, 230, 240, 255});
    }

    void GameView::openBigMap() {
        // The map of the preview on show: the room's (its verified package if it has one,
        // as drawRoomPreview reads it) or the create screen's choice. A downloaded map
        // reads through a view holding its archive's files; a generated one is rolled
        // again from its id, through the base data only (as the game builds it).
        const bool room = mapPreviewFor_.rfind("room:", 0) == 0;
        std::string path = room ? mpRoom().mapId : mapPath_;
        if (path.empty()) return;
        static const char* kWorld[tak::mapgen::kMapTypes] = {"aramon", "taros", "veruna", "zhon", "creon"};
        try {
            if (tak::mapgen::isGeneratedMapId(path)) {
                const auto params = tak::mapgen::decodeMapId(path);
                auto view = std::make_unique<tak::hpi::Vfs>(&vfs_, true);
                // The preview's roll when it is this map (it always is once the preview shows).
                auto map = genPreviewMap_ && genPreviewMapId_ == path ? *genPreviewMap_
                                                                      : tak::mapgen::generate(params, *view).map;
                bigMap_ = std::make_unique<BigMapView>(ren_, std::move(view), std::move(map),
                                                       kWorld[params.mapType % tak::mapgen::kMapTypes]);
            } else {
                auto view = std::make_unique<tak::hpi::Vfs>(&vfs_);
                const auto pkg = room ? mp_->mapPackage() : nullptr;
                if (pkg && pkg->files) { path = pkg->mapPath; view->setMapFiles(pkg->files); }
                else {
                    if (room) path = tak::hpi::findMap(vfs_, path);
                    if (const auto cached = vfs_.cachedMap(path)) { path = cached->first; view->setMapFiles(cached->second); }
                }
                std::string ota;
                if (auto o = view->tryRead(tak::vpath::replaceExtension(path, ".ota"))) ota.assign(o->begin(), o->end());
                std::string world = otaField(ota, "kingdom");
                std::transform(world.begin(), world.end(), world.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                bigMap_ = std::make_unique<BigMapView>(ren_, std::move(view), path, world);
            }
            bigMapFor_ = mapPreviewFor_;
            lbField_ = 0; SDL_StopTextInput();
        } catch (const std::exception&) {
            bigMap_.reset();   // an unreadable map keeps the small preview only
        }
    }

    void GameView::drawBigMap(int winW, int winH) {
        // Another map or another screen than the one it was opened from: close.
        if (bigMapFor_ != mapPreviewFor_) { bigMap_.reset(); return; }
        // Only BACK is live (the lobby drew its own buttons underneath).
        lobbyHots_.clear();
        std::fill(std::begin(genSliderRect_), std::end(genSliderRect_), SDL_FRect{}); mapThumbRect_ = {};
        const float s = lobbyScale_, lw = winW / s, lh = winH / s;
        SDL_SetRenderDrawColor(ren_, 8, 9, 13, 255);
        const SDL_FRect all{0, 0, float(winW), float(winH)};
        SDL_RenderFillRectF(ren_, &all);
        // The map fills the window above a footer as tall as the lobby's (BACK at 40 from
        // the bottom), with the lobby's 16 margin round it.
        const SDL_FRect area{kCreateMargin * s, kCreateMargin * s, (lw - 2 * kCreateMargin) * s,
                             (lh - kCreateMargin - 50) * s};
        const SDL_FRect r = bigMap_->draw(area);
        if (r.w > 0 && mapPreviewWorldW_ > 0) {
            // The starts as the preview shows them, in the room's colours there.
            const SDL_FRect img{r.x, r.y, mapPreviewWorldW_ * r.w / bigMap_->worldW(),
                                mapPreviewWorldH_ * r.h / bigMap_->worldH()};
            drawStartMarkers(img, bigMapFor_.rfind("room:", 0) == 0 ? roomPreviewColors_ : std::vector<int>{}, 16 * s);
        }
        // The footer in the lobby's units, from the window's corner: lobbyMouse reads
        // the pointer the same way while this is open.
        lobbyOffX_ = lobbyOffY_ = 0;
        SDL_RenderSetScale(ren_, s, s);
        lbBtn(kCreateMargin, lh - 40, 120, 30, "BACK", true, [this] { bigMap_.reset(); });
        const std::string title = mapTitle(bigMapFor_.rfind("room:", 0) == 0 ? mpRoom().mapId : mpMapId_);
        const std::string fitted = tak::uiTextFit(title, 1.8f, lw - 2 * (kCreateMargin + 140), false);
        blockText(fitted, (lw - blockWidth(fitted, 1.8f)) / 2, lh - 32, 1.8f, {200, 205, 220, 255});
        SDL_RenderSetScale(ren_, 1.0f, 1.0f);
    }

    void GameView::lobbyMouse(float& mx, float& my) const {
        mx = mouseX_ / lobbyScale_ - lobbyOffX_;
        my = mouseY_ / lobbyScale_ - lobbyOffY_;
    }

    void GameView::lobbyInput(const SDL_Event& e, int winW, int winH) {
        (void)winW; (void)winH;
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            if (genSlider_ >= 0 || lbField_ == 5) applyGenParams();
            genSlider_ = -1; mapDrag_ = false;
            saveCreatePreferences();
        }
        // The full-window map view: BACK (its only button) or Esc closes it; nothing
        // else reaches the lobby under it.
        if (bigMap_) {
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) bigMap_.reset();
            else if (e.type == SDL_MOUSEMOTION) { mouseX_ = float(e.motion.x); mouseY_ = float(e.motion.y); }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                mouseX_ = float(e.button.x); mouseY_ = float(e.button.y);
                float mx, my; lobbyMouse(mx, my);
                for (auto& hot : lobbyHots_)
                    if (ptIn(hot.rect, mx, my) && hot.action) { hot.action(); break; }
            }
            return;
        }
        // The mods screen: Esc (no field being typed in) goes back to the room, as DONE.
        if (modsScreen_ && e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && !lbField_) {
            modsScreen_ = false;
            return;
        }
        if (e.type == SDL_MOUSEMOTION) {
            mouseX_ = float(e.motion.x); mouseY_ = float(e.motion.y);
            if (mapDrag_) setMapScrollFromThumb();   // dragging the map scrollbar
            if (genSlider_ >= 0) { float mx, my; lobbyMouse(mx, my); setGenSlider(genSlider_, mx); }
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            if (genSlider_ >= 0) applyGenParams();
            mapDrag_ = false; genSlider_ = -1;
            saveCreatePreferences();
        } else if (e.type == SDL_MOUSEWHEEL) {
            float mx, my; lobbyMouse(mx, my);
            if (ptIn(mapListRect_, mx, my)) {   // scroll the map list under the cursor
                mapScroll_ -= e.wheel.y;
                clampMapScroll();
            }
            if (ptIn(chatRect_, mx, my)) chatScroll_ = std::max(0, chatScroll_ + e.wheel.y);   // up: older rows
            for (int k = 0; k < 2; ++k)   // the mods screen's two lists (clamped as they draw)
                if (modsScreen_ && ptIn(modListRect_[k], mx, my)) modScroll_[k] = std::max(0, modScroll_[k] - e.wheel.y);
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)) {
            mouseX_ = float(e.button.x); mouseY_ = float(e.button.y);
            const bool forward = e.button.button == SDL_BUTTON_LEFT;
            if (forward) { if (lbField_ == 5) applyGenParams(); lbField_ = 0; SDL_StopTextInput(); }
            float mx, my; lobbyMouse(mx, my);
            if (forward && ptIn(mapThumbRect_, mx, my)) { mapDrag_ = true; return; }   // grab the thumb
            for (int gi = 0; gi < 9; ++gi)   // grab a density slider
                if (forward && ptIn(genSliderRect_[gi], mx, my)) { genSlider_ = gi; setGenSlider(gi, mx); return; }
            for (auto& hot : lobbyHots_)
                if (ptIn(hot.rect, mx, my)) {
                    if (hot.cycle) hot.cycle(forward ? 1 : -1);
                    else if (forward && hot.action) hot.action();
                    saveCreatePreferences();
                    break;
                }
        } else if (e.type == SDL_TEXTINPUT && lbField_) {
            std::string* f = lbFieldBuf();
            // A typed game name is 16 at most (the game list and the LAN rows show it whole);
            // only a LOCAL SERVER's own "<name>'s Game" runs past it.
            const size_t cap = lbField_ == 1 ? 16 : 24;
            if (f && f->size() < cap) {
                if (lbField_ == 1) {
                    for (unsigned char c : std::string(e.text.text)) if (f->size() < cap) *f += char(c);
                } else if (lbField_ == 5) {
                    for (unsigned char c : std::string(e.text.text))
                        if (f->size() < 24 && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_')) *f += char(c);
                } else *f += e.text.text;
                if (lbField_ == 6) mapScroll_ = 0;   // a new filter starts at the top
                if (lbField_ == 7) modScroll_[0] = 0;
                if (lbField_ == 8) modScroll_[1] = 0;
            }
            saveCreatePreferences();
        } else if (e.type == SDL_KEYDOWN && lbField_) {
            if (e.key.keysym.sym == SDLK_BACKSPACE) {
                std::string* f = lbFieldBuf();
                if (f && !f->empty()) f->pop_back();
                if (lbField_ == 6) mapScroll_ = 0;
                if (lbField_ == 7) modScroll_[0] = 0;
                if (lbField_ == 8) modScroll_[1] = 0;
            }
            else if (e.key.keysym.sym == SDLK_RETURN) {
                if (lbField_ == 4 && !chatDraft_.empty()) { mp_->chat(chatDraft_); chatDraft_.clear(); }
                if (lbField_ == 5) applyGenParams();
                lbField_ = 0; SDL_StopTextInput();
            } else if (e.key.keysym.sym == SDLK_ESCAPE) { if (lbField_ == 5) applyGenParams(); lbField_ = 0; SDL_StopTextInput(); }
            saveCreatePreferences();
        }
    }

    void GameView::lbBtn(float x, float y, float w, float h, const std::string& label, bool enabled,
               std::function<void()> action, SDL_Color base, float labelPx) {
        SDL_FRect r{x, y, w, h};
        bool hot = enabled && lbHot(r);
        SDL_Color c = enabled ? (hot ? SDL_Color{90, 110, 150, 255} : base)
                              : SDL_Color{40, 42, 50, 255};
        SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
        SDL_RenderFillRectF(ren_, &r);
        SDL_SetRenderDrawColor(ren_, hot ? 180 : 90, hot ? 200 : 100, hot ? 240 : 130, 255);
        SDL_RenderDrawRectF(ren_, &r);
        // Shrink the label if it would overflow the button (keeps long captions like
        // "START LOCATIONS: BY SLOT" inside their box), measured, with 6 px clear each side.
        // Height too: a label with descenders (a mod pack's "Big Battles") keeps its ink
        // 2 px inside the border, and centres caps plus descenders; caps-only labels
        // centre their caps as always.
        float px = tak::uiTextFitPx(label, labelPx, w - 12.0f);
        if (const float ink = 7 * px + tak::uiTextInkDescent(label, px); ink > h - 4)
            px *= (h - 4) / ink;
        float tw = blockWidth(label, px);
        blockText(label, x + (w - tw) / 2, y + (h - 7 * px - tak::uiTextInkDescent(label, px)) / 2, px,
                  enabled ? SDL_Color{225, 230, 240, 255} : SDL_Color{110, 115, 125, 255});
        if (enabled && action) lobbyHots_.push_back({r, std::move(action)});
    }

    bool GameView::tntIndexedImage(const std::vector<uint8_t>& d, int field,
                                int& w, int& h, const uint8_t*& data) {
        auto u32 = [&](size_t o) -> uint32_t {
            return uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8) | (uint32_t(d[o + 2]) << 16)
                 | (uint32_t(d[o + 3]) << 24);
        };
        size_t hoff = size_t(field) * 4;
        if (hoff + 4 > d.size()) return false;
        size_t p = u32(hoff);
        if (p == 0 || p + 8 > d.size()) return false;
        uint32_t iw = u32(p), ih = u32(p + 4);
        if (iw == 0 || ih == 0 || iw > 4096 || ih > 4096) return false;
        if (p + 8 + size_t(iw) * ih > d.size()) return false;
        w = int(iw); h = int(ih); data = &d[p + 8];
        return true;
    }

    std::string GameView::otaField(const std::string& ota, const char* keyLower) {
        std::string low = ota;
        for (char& c : low) c = char(std::tolower((unsigned char)c));
        std::string k = keyLower;
        for (size_t pos = 0; (pos = low.find(k, pos)) != std::string::npos; pos += k.size()) {
            if (pos > 0 && (std::isalnum((unsigned char)low[pos - 1]) || low[pos - 1] == '_')) continue;
            size_t e = pos + k.size();
            while (e < low.size() && (low[e] == ' ' || low[e] == '\t')) ++e;
            if (e >= low.size() || low[e] != '=') continue;
            size_t a = e + 1, end = ota.find_first_of(";\r\n", a);
            if (end == std::string::npos) end = ota.size();
            std::string v = ota.substr(a, end - a);
            size_t s = v.find_first_not_of(" \t"), t = v.find_last_not_of(" \t");
            return s == std::string::npos ? std::string() : v.substr(s, t - s + 1);
        }
        return {};
    }
