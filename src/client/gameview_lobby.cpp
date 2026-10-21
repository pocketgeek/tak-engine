#include "util/virtualpath.h"
#include "client/gameview.h"
#include "client/crusadesscreen.h"
#include "net/matchconfig.h"
#include "client/startmarkers.h"
#include <cctype>
#include <cstring>
#include <cmath>

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
}  // namespace

    void GameView::openCrusades(uint32_t territory) {
        if (!mp_ || singlePlayer_) return;
        if (!crusadesScreen_) crusadesScreen_ = std::make_unique<tak::CrusadesScreen>(ren_, vfs_, *mp_);
        crusadesScreen_->selectTerritory(territory);
        lobbyScreen_ = LobbyScreen::Crusades;
        lbField_ = 0; SDL_StopTextInput();
    }

    uint32_t GameView::crusadesSelectedTerritory() const {
        return crusadesScreen_ ? crusadesScreen_->selectedTerritory() : 0;
    }

    std::string GameView::takeCrusadesReplayPath() {
        return crusadesScreen_ ? crusadesScreen_->takeReplayPath() : std::string{};
    }
    void GameView::setCrusadesReplayError(const std::string& error) {
        if (crusadesScreen_) crusadesScreen_->setReplayError(error);
    }

    void GameView::drawLobby(int winW, int winH) {
        lobbyHots_.clear();
        // Map-picker geometry is only live while the create screen is shown; clear it
        // so a stale thumb/list rect can't grab clicks or wheel on the other screens.
        mapListRect_ = mapThumbRect_ = SDL_FRect{0, 0, 0, 0};
        for (auto& rect : genSliderRect_) rect = {};
        // absorb any new chat
        if (mp_) for (auto& m : mp_->takeChat()) chatLog_.push_back(m);
        // The ground + centred panel frame are painted by the caller (the viewport is
        // already offset to this panel); RenderClear would ignore the viewport.
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        if (mp_ && lobbyScreen_ == LobbyScreen::Crusades && crusadesScreen_ &&
            mp_->state() != tak::net::MpClient::State::InRoom) {
            crusadesScreen_->update();
            crusadesScreen_->draw(winW, winH);
            return;
        }
        float cx = winW / 2.0f;
        const char* title = singlePlayer_ ? "SINGLE PLAYER VS AI" : "TA:KINGDOMS  MULTIPLAYER";
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
            blockText("DISCONNECTED", cx - blockWidth("DISCONNECTED", 2.6f) / 2, my,
                      2.6f, {255, 130, 110, 255});
            std::string why = mp_->error().empty() ? std::string("the server closed the connection")
                                                   : mp_->error();
            // Long reasons (version-mismatch text) wrap onto two centred lines.
            auto rows = tak::uiTextWrap(why, 1.8f, 600);
            const std::string l1 = rows[0];
            std::string l2;
            for (size_t k = 1; k < rows.size(); ++k) l2 += (k > 1 ? " " : "") + rows[k];
            l2 = tak::uiTextFit(l2, 1.8f, 600);
            blockText(l1, cx - blockWidth(l1, 1.8f) / 2, my + 34, 1.8f, {215, 218, 228, 255});
            if (!l2.empty())
                blockText(l2, cx - blockWidth(l2, 1.8f) / 2, my + 56, 1.8f, {215, 218, 228, 255});
            lbBtn(cx - 90, my + (l2.empty() ? 78.0f : 96.0f), 180, 32, "BACK TO MENU", true,
                  [this] { menuRequested_ = true; });
            return;
        }
        if (st == tak::net::MpClient::State::InRoom) drawRoom(winW, winH);
        else if (lobbyScreen_ == LobbyScreen::Create) drawCreate(winW, winH);
        else drawBrowser(winW, winH);
    }

    void GameView::drawBrowser(int winW, int winH) {
        (void)winH;
        // keep the list fresh without needing a manual refresh
        if (SDL_GetTicks64() - mpListMs_ > 1000) { mp_->listGames(); mpListMs_ = SDL_GetTicks64(); }
        float x = 60, y = 80, w = winW - 120.0f;
        blockText("GAMES", x, y, 2.2f, {200, 205, 220, 255});
        lbBtn(x + w - 200, y - 6, 95, 26, "REFRESH", true, [this] { mp_->listGames(); });
        lbBtn(x + w - 100, y - 6, 100, 26, "CREATE", true,
              [this] { lobbyScreen_ = LobbyScreen::Create; });
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
        resetMinimap(); mapView_.quiesce();
        vfs_.refreshMapCache(installRoot_);
        mapList_.clear();
        mapTitlesLoaded_ = false;   // the install may have changed: read the tables again
        const auto maps=tak::hpi::listMaps(vfs_);
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
        if (!missionName.empty() && lower(missionName) == lower(name)) return missionName;
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
        if (mapPreviewTex_) { gpuvram::destroy(mapPreviewTex_); mapPreviewTex_ = nullptr; }
        mapPreviewFor_ = tntPath;
        mapPreviewW_ = mapPreviewH_ = 0;
        mapPreviewDims_.clear();
        mapPreviewStarts_.clear();
        mapPreviewWorldW_ = mapPreviewWorldH_ = 0;
        if (tntPath.empty()) return;
        genPreviewInfo_.clear(); genPreviewError_.clear(); genPreviewScattered_ = 0;
        if (tak::mapgen::isGeneratedMapId(tntPath)) {
            try { buildGenPreview(tntPath); }
            catch (const std::exception& e) { genPreviewError_ = e.what(); }
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

    void GameView::buildGenPreview(const std::string& id) {
        // Roll the map from the seed (cheap integer-only work) and paint a downsampled
        // height/water map with feature + start markers, so the sliders preview live.
        tak::mapgen::Params gp = tak::mapgen::decodeMapId(id);
        tak::mapgen::Result g = tak::mapgen::generate(gp, vfs_);
        const tak::tnt::Map& m = g.map;
        const int W = m.width, H = m.height, sea = m.seaLevel;
        if (W <= 0 || H <= 0 || m.heights.size() < size_t(W) * H) return;
        const int cap = 192, mx = std::max(W, H);
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
        // Sacred Stone (the mana spot) gets a bold 2x2 gold marker; Standing Stones
        // (ruins) are muted; rocks grey; trees dark green.
        int manaSpots=0;
        for (int cz = 0; cz < H; ++cz)
            for (int cx = 0; cx < W; ++cx) {
                uint16_t fi = m.features[size_t(cz) * W + cx];
                if (fi == 0xFFFF || fi >= m.featureNames.size()) continue;
                const std::string& nm = m.featureNames[fi];
                int tx = cx * TW / W, ty = cz * TH / H;
                if (tak::hpi::MountSet::key(nm).find("mana") != std::string::npos) {          // sacred spot: gold
                    ++manaSpots;
                    for (int dy = 0; dy <= 1; ++dy)
                        for (int dx = 0; dx <= 1; ++dx) put(tx + dx, ty + dy, 250, 224, 82);
                } else if (nm.find("Henge") != std::string::npos) put(tx, ty, 176, 162, 132);  // ruins: stone
                else if (nm.find("Rock") != std::string::npos) put(tx, ty, 150, 148, 140);      // rock: grey
                else put(tx, ty, 28, 66, 28);                                                   // tree: dark green
            }
        // Start positions: drawn over the thumbnail as numbered markers (drawStartMarkers),
        // in world px like a shipped map's -- the same cell -> px setupMatch applies.
        for (auto& [sx, sz] : g.starts) mapPreviewStarts_.push_back({float(sx * 16), float(sz * 16)});
        mapPreviewWorldW_ = float(W) * 16; mapPreviewWorldH_ = float(H) * 16;
        mapPreviewTex_ = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, TW, TH);
        if (!mapPreviewTex_) return;
        SDL_SetTextureBlendMode(mapPreviewTex_, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(mapPreviewTex_, nullptr, rgba.data(), TW * 4);
        SDL_SetTextureScaleMode(mapPreviewTex_, SDL_ScaleModeNearest);
        mapPreviewW_ = TW; mapPreviewH_ = TH;
        char dims[48];
        std::snprintf(dims, sizeof dims, "%d x %d   %d PLAYER", W / 32, H / 32, int(gp.players));
        mapPreviewDims_ = dims;
        if (gp.formatVer >= 3)
            genPreviewInfo_ = "WATER: " + std::to_string(g.waterPercent) + "%  MANA: " + std::to_string(manaSpots);
        genPreviewScattered_ = g.scatteredMana;

    }

    void GameView::applyGenParams() {
        genParams_.formatVer = tak::mapgen::Params{}.formatVer;
        genParams_ = tak::mapgen::sanitize(genParams_);
        // The menu offers square maps; accommodate the layout's minimum size.
        genParams_.widthCells = genParams_.heightCells =
            std::max(genParams_.widthCells, genParams_.heightCells);
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
        if (!singlePlayer_) pref.gameMods = createGameMods_;   // SP is always on: keep the MP choice
        pref.name = createName_;
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
            const auto root=std::filesystem::u8path(installRoot_);
            availableGameMods_=mods_.game?tak::hpi::modPacks(root,tak::hpi::ModKind::Game):std::vector<std::string>{};
            availableCosmeticMods_=mods_.cosmetic?tak::hpi::modPacks(root,tak::hpi::ModKind::Cosmetic):std::vector<std::string>{};
            alwaysOnMods_=mods_.cosmetic?tak::hpi::alwaysOnMods(root):std::vector<std::string>{};
            modPacksScanned_=true;
        }
    }

    bool GameView::createScenarioOn() const {
        if (createScenarioFor_ != mapPath_ || tak::mapgen::isGeneratedMapId(mpMapId_)) return false;
        // An offerable map plays its scenario when chosen; a Test Map copy always does.
        return createScenario_.active(createMapScenario_ && scenarioOffered(createScenario_, createGameModsOn()));
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
            createFog_ = uint8_t(pref.fog); createGameMods_ = pref.gameMods;
            createName_ = pref.name;
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
        float x = 80, y = 70;
        blockText(singlePlayer_ ? "SINGLE PLAYER VS AI" : "CREATE GAME", x, y, 2.2f,
                  {200, 205, 220, 255}); y += 34;
        // A private single-player game needs no name or password.
        if (!singlePlayer_) {
            lbField(x, y, 260, "GAME NAME", createName_, 1); y += 44;
            lbField(x, y, 260, "PASSWORD (OPTIONAL)", createPass_, 2); y += 44;
        }
        blockText(std::string("MAP: ") + mapTitle(mpMapId_), x, y, 1.8f, {180, 185, 195, 255}); y += 28;
        // Option rows (26 high at a 27 pitch): multiplayer's eleven rows, under the name
        // and password fields, are 24 high at a 25 pitch to stay clear of the BACK/CREATE
        // footer.
        const float kRow = singlePlayer_ ? 27 : 25, kRowH = singlePlayer_ ? 26 : 24;
        lbCycle(x, y, 170, kRowH, createCrusades_ ? "CRUSADES: ON" : "CRUSADES: OFF", true,
              [this](int) { createCrusades_ = !createCrusades_; }); y += kRow;
        lbCycle(x, y, 240, kRowH, createDoubleSight_ ? "DOUBLE SIGHT/RADAR: ON" : "DOUBLE SIGHT/RADAR: OFF", true,
              [this](int) { createDoubleSight_ = !createDoubleSight_; }); y += kRow;
        lbCycle(x, y, 280, kRowH, pathfindingLabel(createPathfindingMode_), true,
              [this](int direction) { createPathfindingMode_=tak::sim::cyclePathfindingMode(createPathfindingMode_,direction); }); y += kRow;
        lbCycle(x,y,240,kRowH,"UNIT CAP: " + std::to_string(createUnitCap_),true,[this](int direction) {
            static constexpr uint16_t limits[]={250,500,1000,2000};
            auto at=std::find(std::begin(limits),std::end(limits),createUnitCap_);
            int index=at==std::end(limits) ? 0 : int(at-std::begin(limits));
            createUnitCap_=limits[(index+direction+4)%4];
        }); y+=kRow;
        // When OFF, losing your Monarch loses the game (retail commander rule); ON
        // makes the Monarch just another unit.
        lbCycle(x, y, 240, kRowH, scenarioOn ? "MONARCH EXPENDABLE: SCENARIO"
                             : createMonarchExp_ ? "MONARCH EXPENDABLE: ON"
                                                 : "MONARCH EXPENDABLE: OFF", !scenarioOn,
              [this](int) { createMonarchExp_ = !createMonarchExp_; }); y += kRow;
        lbCycle(x, y, 240, kRowH, createSpeedUnlock_ ? "ALLOW SPEED CHANGE: ON"
                                              : "ALLOW SPEED CHANGE: OFF", true,
              [this](int) { createSpeedUnlock_ = !createSpeedUnlock_; }); y += kRow;
        // POWER CODES (retail's battle-room box): the +codes typed in the chat box work
        // for every player of this game. Off by default, as in retail.
        lbCycle(x, y, 240, kRowH, createPowerCodes_ ? "POWER CODES: ON" : "POWER CODES: OFF", true,
              [this](int) { createPowerCodes_ = !createPowerCodes_; }); y += kRow;
        // SP only: spectate mode -- you take no slot and just watch the AIs fight.
        // Seat AIs in the slots below, then START.
        if (singlePlayer_) {
            lbCycle(x, y, 240, kRowH, spSpectate_ ? "SPECTATE (WATCH AIS): ON"
                                             : "SPECTATE (WATCH AIS): OFF", true,
                  [this](int) { spSpectate_ = !spSpectate_; }); y += kRow;
        } else {
            // MP only: may others WATCH this game once it runs? The server enforces it;
            // the host can still flip it in the room until START.
            lbCycle(x, y, 240, kRowH, createAllowSpectators_ ? "ALLOW SPECTATORS: ON"
                                                        : "ALLOW SPECTATORS: OFF", true,
                  [this](int) { createAllowSpectators_ = !createAllowSpectators_; }); y += kRow;
        }
        // Fog of war: a room rule, so it is picked here and not only after the
        // room exists. Display-only (never hashed) -- every peer applies the same
        // rule, so it stays fair. The room displays this choice as information.
        {
            static const char* kFogName[3] = {"NOT EXPLORED", "EXPLORED", "FULL VISION"};
            lbCycle(x, y, 240, kRowH,
                  std::string("FOG OF WAR: ") + kFogName[std::min<int>(createFog_, 2)], true,
                  [this](int direction) { createFog_ = uint8_t((createFog_ + direction + 3) % 3); });
            y += kRow;
        }
        // Start Locations: also a room rule, picked here like fog so it is set before
        // the room exists. FIXED = the seated slots take the map's starts in order and
        // empty slots are skipped, as in retail; RANDOM = the starts are shuffled for
        // the match; BY SLOT (not retail) = slot N always takes the map's Nth start.
        // A MAP SCENARIO plays on fixed starts.
        lbCycle(x, y, 240, kRowH, startLocationsLabel(scenarioOn ? tak::net::kStartsFixed : createStartMode_), !scenarioOn,
              [this](int direction) { createStartMode_ = uint8_t((createStartMode_ + direction + 3) % 3); }); y += kRow;
        // GAME MODS (multiplayer only): lets the host tick mods/game packs in the room,
        // which are then sent to every player. Single player always has them;
        // cosmetic mods need no switch (every player picks their own in the room).
        if (!singlePlayer_) {
            lbCycle(x, y, 240, kRowH, createGameMods_ ? "GAME MODS: ON" : "GAME MODS: OFF", true,
                  [this](int) { createGameMods_ = !createGameMods_; }); y += 44;
        }
        // Footer buttons, symmetric: BACK at the bottom-left and CREATE at the
        // bottom-right, both inset by `x` from their side and at the same height.
        // BROWSER (MP only) sits just left of CREATE.
        const float by = kLobbyH - 40, bw = 120;
        lbBtn(kLobbyW - x - bw, by, bw, 30, "CREATE", !createName_.empty() && genPreviewError_.empty(), [this] {
            tak::net::GameOptions o; o.crusades = createCrusades_ ? 1 : 0;
            o.gameMods = createGameModsOn() ? 1 : 0;
            if(settings_)mp_->setGameModPacks(mods_.game?settings_->gameModPacks:std::vector<std::string>{});
            o.doubleSight = createDoubleSight_ ? 1 : 0;
            o.pathfindingMode = createPathfindingMode_;
            o.speedUnlock = createSpeedUnlock_ ? 1 : 0;
            o.unitCap = createUnitCap_;
            o.monarchExpendable = createMonarchExp_ ? 1 : 0;
            o.powerCodes = createPowerCodes_ ? 1 : 0;
            o.fogExplored = std::min<uint8_t>(createFog_, 2);
            // MAP SCENARIO: an offerable map's own scenario, when chosen. A Test Map copy
            // plays its scenario through its opt-in and sends 0 (the server checks the
            // verified package either way). Seats and rule regions follow fixed starts.
            o.mapScenario = !tak::mapgen::isGeneratedMapId(mpMapId_) && createScenarioFor_ == mapPath_ &&
                scenarioOffered(createScenario_, createGameModsOn()) && createMapScenario_ ? 1 : 0;
            o.randomStarts = createScenarioOn() ? tak::net::kStartsFixed : createStartMode_;
            // A private single-player room is never listed, so the rule means nothing
            // there; leave it at the default rather than a remembered MP choice.
            o.allowSpectators = singlePlayer_ || createAllowSpectators_ ? 1 : 0;
            // SP spectate: create as a spectator (no slot) so every slot can be an AI;
            // the game-start path flags spectating_ + noFog_ from mp_->isSpectator().
            mp_->createGame(createName_, createPass_, mpMapId_, o, mpCapacity(o.mapScenario != 0),
                            singlePlayer_ && spSpectate_, singlePlayer_);
            specAutoSeated_ = false;   // arm the one-shot auto-seat for this new room
            lobbyScreen_ = singlePlayer_ ? LobbyScreen::Create : LobbyScreen::Browser;
        });
        if (!singlePlayer_)   // a private single-player game has no browser to go back to
            lbBtn(kLobbyW - x - bw - 122, by, 110, 30, "BROWSER", true,
                  [this] { lobbyScreen_ = LobbyScreen::Browser; });
        lbBtn(x, by, bw, 30, "BACK", true, [this] { menuRequested_ = true; });

        // Map picker (right column): a scrollable list box. Selecting sets both the
        // wire id (mpMapId_ = bare .tnt stem) and mapPath_, so mpCapacity() recomputes
        // the chosen map's start-position count.
        if (mapList_.empty()) buildMapList();
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
        float lx = 400, hy = 90;
        blockText("SELECT MAP", lx, hy, 1.8f, {200, 205, 220, 255});
        // Toggle between the map list and the random-map generator.
        bool gen = tak::mapgen::isGeneratedMapId(mpMapId_);

      if (!gen) {
        // Sort buttons: NAME / PLAYERS / SIZE. Clicking sets the sort key; clicking the
        // active one flips ascending/descending. The active key shows an up/down arrow.
        {
            const char* keys[3] = {"NAME", "PLAYERS", "SIZE"};
            const float bw = 86, bh = 20, sy = hy + 20;
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
        const float boxX = lx, boxY = hy + 46, boxW = 340, boxH = 344, rowH = 24, sbW = 12;
        const int total = int(mapList_.size());
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
            int i = mapScroll_ + r;
            if (i >= total) break;
            const MapInfo& m = mapList_[size_t(i)];
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
            if (scenarioOffered(m.scenario, createGameModsOn()) || (m.scenario.marker && m.scenario.runnable()))
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
        static const int kSizes[] = {8, 12, 16, 20, 24, 32, 40, 48, 56, 64};   // section-units (x32 cells) per side
        // One rhythm for the whole panel: every control is kRowH tall and every gap
        // is kGap. The rows used to step by 30, 30, 36 and then 44, so the buttons
        // sat tighter than the sliders and the run of sliders drifted out of line
        // with everything above it.
        const float kRowH = 24, kGap = 6, kBtnW = 300;
        const float kSliderH = 30;   // label (11) + 5 + bar (14)
        const float kHalfW = (kBtnW - kGap) / 2;
        float px = lx, py = hy + 46;   // aligns with the map list's box top
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
        int curU = genParams_.widthCells / 32;
        char szl[48]; std::snprintf(szl, sizeof szl, "SIZE:  %d x %d", curU, curU);
        // SIZE and PLAYERS share a row, so the six sliders and the seed button
        // still end above the BACK/CREATE footer band of the 540-high canvas.
        lbCycle(px, py, kHalfW, kRowH, szl, true, [this](int direction) {
            const int u = genParams_.widthCells / 32;
            std::vector<int> sizes;
            for (int size : kSizes) {
                auto candidate = genParams_;
                candidate.widthCells = candidate.heightCells = uint16_t(size * 32);
                candidate = tak::mapgen::sanitize(candidate);
                if (candidate.widthCells == size * 32 && candidate.heightCells == size * 32)
                    sizes.push_back(size);
            }
            if (sizes.empty()) return;
            auto at = std::find(sizes.begin(), sizes.end(), u);
            int index = at == sizes.end() ? 0 : int(at - sizes.begin());
            int next = sizes[(index + direction + int(sizes.size())) % int(sizes.size())];
            genParams_.widthCells = genParams_.heightCells = uint16_t(next * 32);
            applyGenParams();
        });
        char pl[32]; std::snprintf(pl, sizeof pl, "PLAYERS:  %d", int(genParams_.players));
        lbCycle(px + kHalfW + kGap, py, kHalfW, kRowH, pl, true, [this](int direction) {
            genParams_.players = uint8_t(2 + (genParams_.players - 2 + direction + 7) % 7);
            applyGenParams();
        }); py += kRowH + kGap * 2;   // a wider break between the pickers and the sliders
        auto slider = [&](int idx, const char* label, uint8_t val, bool enabled = true) {
            blockText(label, px, py, 1.6f, {180, 185, 195, 255});
            float bx = px, by = py + 16, bw = kBtnW, bh = 14;
            genSliderRect_[idx] = enabled ? SDL_FRect{bx, by, bw, bh} : SDL_FRect{};
            SDL_FRect bar{bx, by, bw, bh};
            SDL_SetRenderDrawColor(ren_, 30, 34, 46, 255); SDL_RenderFillRectF(ren_, &bar);
            float t = enabled ? val / 255.0f : 0.0f;
            SDL_FRect fill{bx, by, bw * t, bh};
            SDL_SetRenderDrawColor(ren_, 70, 120, 90, 255); SDL_RenderFillRectF(ren_, &fill);
            SDL_FRect handle{bx + bw * t - 3, by - 2, 6, bh + 4};
            SDL_SetRenderDrawColor(ren_, 200, 220, 200, 255); if (enabled) SDL_RenderFillRectF(ren_, &handle);
            SDL_SetRenderDrawColor(ren_, 70, 76, 96, 255); SDL_RenderDrawRectF(ren_, &bar);
            // Short level names: a longer one would run into the preview column.
            static const char* kScatterShort[tak::mapgen::kScatterLevels] = {"NONE", "MIN", "MED", "HIGH", "MAX"};
            const std::string level = idx==2 ? "+" + std::to_string(tak::mapgen::extraManaRounds(val)) + "/P" :
                idx==5 ? kScatterShort[std::min<int>(genParams_.manaScatter, tak::mapgen::kScatterLevels - 1)] :
                !enabled ? "AUTO" : val == 0 ? "NONE" : val < 86 ? "LOW" : val < 171 ? "MED" : "HIGH";
            blockText(level, bx + bw + 8, py + 14, 1.5f, {160, 165, 180, 255});
            py += kSliderH + kGap;
        };
        slider(0, "FORESTS", genParams_.treeDensity);
        slider(1, "ROCK CLUSTERS", genParams_.rockDensity);
        slider(2, "EXTRA MANA SPOTS", genParams_.manaDensity);
        // Neutral spots scattered by AREA, apart from the per-player extra spots above
        // (a level, not a count: the same level puts four times as many on a map twice
        // as wide). Five notches, NONE..MAX; MAX = a six-spot home's density.
        slider(5, "SCATTERED MANA", uint8_t(genParams_.manaScatter * 255 / (tak::mapgen::kScatterLevels - 1)));
        slider(3, "WATER AMOUNT", genParams_.waterDensity, !tak::mapgen::automaticWater(genParams_.layout));
        slider(4, genParams_.layout==tak::mapgen::Highlands?"HIGHLAND AMOUNT":"AUTHORED HILLS", genParams_.reliefDensity, genParams_.layout!=tak::mapgen::Maze);
        lbBtn(px, py, 110, kRowH, "RE-ROLL SEED", true, [this] {
            genParams_.seed = genParams_.seed * 6364136223846793005ULL + 1442695040888963407ULL;
            applyGenParams();
        });
      }

        // Preview panel, right of the list / params column. A real map shows its
        // embedded minimap; a generated map shows a live thumbnail rolled from its
        // seed (both go through buildMapPreview -> mapPreviewTex_).
        const float pvx = lx + 352, pvy = hy + 46, pvW = 192, pvH = 192;
        blockText("PREVIEW", pvx, hy, 1.8f, {200, 205, 220, 255});
        SDL_FRect pbox{pvx, pvy, pvW, pvH};
        SDL_SetRenderDrawColor(ren_, 18, 20, 28, 255); SDL_RenderFillRectF(ren_, &pbox);
        if (mapPreviewFor_ != mapPath_) buildMapPreview(mapPath_);
        if (mapPreviewTex_ && mapPreviewW_ > 0) {
            float sc = std::min(pvW / float(mapPreviewW_), pvH / float(mapPreviewH_));
            float iw = mapPreviewW_ * sc, ih = mapPreviewH_ * sc;
            SDL_FRect dst{pvx + (pvW - iw) / 2, pvy + (pvH - ih) / 2, iw, ih};
            SDL_RenderCopyF(ren_, mapPreviewTex_, nullptr, &dst);
            drawStartMarkers(dst);   // no slots yet: the room tints them
        } else {
            blockText(gen ? "GENERATING" : "NO PREVIEW", pvx + 34, pvy + pvH / 2 - 7, 1.6f,
                      {120, 125, 140, 255});
        }
        SDL_SetRenderDrawColor(ren_, 70, 76, 96, 255); SDL_RenderDrawRectF(ren_, &pbox);
        if (!mapPreviewDims_.empty())
            blockText(mapPreviewDims_, pvx, pvy + pvH + 8, 1.6f, {160, 165, 180, 255});
        if (gen) {
            blockText(genPreviewError_.empty() ? genPreviewInfo_ : "MAP UNAVAILABLE", pvx, pvy + pvH + 28,
                      1.4f, {180, 185, 195, 255});
            if (!genPreviewError_.empty())
                blockText("CHECK RETAIL DATA", pvx, pvy + pvH + 46, 1.4f, {220, 140, 120, 255});
            blockText("3 HOME MANA SPOTS EACH", pvx, pvy + pvH + 106, 1.3f, {160, 175, 160, 255});
            blockText("EXTRAS NEED FREE SPACE", pvx, pvy + pvH + 124, 1.3f, {160, 175, 160, 255});
            if (genParams_.manaScatter)
                blockText("SCATTERED: " + std::to_string(genPreviewScattered_) + " NEUTRAL", pvx, pvy + pvH + 160,
                          1.3f, {160, 175, 160, 255});
            if (genParams_.layout == tak::mapgen::Islands || genParams_.layout == tak::mapgen::Ports)
                blockText("SIZE RESERVES SEA LANES", pvx, pvy + pvH + 142, 1.3f, {160, 175, 160, 255});
        }
        // Keep the generator action below the preview and map description.
        lbCycle(pvx, pvy + pvH + (gen ? 68 : 32), pvW, 26, gen ? "PICK AN EXISTING MAP" : "GENERATE RANDOM MAP", true,
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
            const bool offered = scenarioOffered(createScenario_, createGameModsOn());
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

    void GameView::drawRoom(int winW, int winH) {
        const auto& room = mp_->room();
        const bool campaign = mp_->campaignRoom();
        float x = 40, y = 78;
        if (!singlePlayer_) blockText(room.name, x, y, 2.2f, {210, 210, 220, 255});
        blockText(std::string("MAP  ") + mapTitle(room.mapId), x, y - 24, 1.8f, {180, 185, 195, 255});
        y += 34;
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
        const bool scenarioShown = fileMap && !campaign && (room.opts.mapScenario != 0 || scenarioOn);
        // The room's map, with its starts numbered and coloured by who opens there,
        // heads the right column; the mod packs and the chat sit below it.
        const bool roomMap = !campaign && room.mission.empty();
        const float kRoomMap = 140, mapDrop = roomMap ? kRoomMap + 5 : 0;
        float modBottom=112+mapDrop;
        {
            // MODS, in the right column under the map. ALWAYS ON: the mods/cosmetic/*.hpi
            // archives every game loads (campaigns included); named, never ticked.
            // COSMETIC: every player ticks their own mods/cosmetic packs (not in a
            // campaign room); they are never sent anywhere and cannot change the game.
            // GAME: only the host of a skirmish room with GAME MODS on ticks mods/game
            // packs, which are sent to every player; everyone else sees the host's choice.
            scanModPacks();
            const bool gameRoom=!campaign && room.mission.empty();
            const bool gameOn=gameRoom && room.opts.gameMods;
            const size_t perPage=gameOn && host?3:5;
            float py=87+mapDrop;
            constexpr float kPackPx=1.3f;   // the column's list text (ALWAYS ON, HOST:)
            auto packList=[&](const std::vector<std::string>& available,const std::vector<std::string>& selected,
                              size_t& page,bool busy,const char* empty,const char* pageLabel,
                              std::function<void(std::vector<std::string>)> apply) {
                auto names=available;for(const auto& n:selected)if(std::find(names.begin(),names.end(),n)==names.end())names.push_back(n);
                std::sort(names.begin(),names.end());
                const size_t pages=std::max<size_t>(1,(names.size()+perPage-1)/perPage);page=std::min(page,pages-1);
                if(names.empty()){blockText(empty,winW-296.0f,py,1.3f,{150,155,165,255});py+=22;}
                for(size_t i=page*perPage;i<std::min(names.size(),page*perPage+perPage);++i){
                    const auto name=names[i];const bool enabled=std::find(selected.begin(),selected.end(),name)!=selected.end();
                    // A pack's name at the size of the column's other lines (ALWAYS ON, the
                    // joiner's HOST: list), centered clear of the checkbox.
                    const std::string label=tak::uiTextFit(name,kPackPx,220);
                    lbBtn(winW-300.0f,py,280,20,label,!busy,[selected,name,enabled,apply]{
                        auto selection=selected;
                        if(enabled)selection.erase(std::remove(selection.begin(),selection.end(),name),selection.end());else if(selection.size()<64)selection.push_back(name);
                        apply(std::move(selection));
                    },{60,66,86,255},kPackPx);
                    SDL_FRect checkBox{winW-291.0f,py+4.5f,11,11};
                    SDL_SetRenderDrawColor(ren_,190,205,220,255);
                    SDL_RenderDrawRectF(ren_,&checkBox);
                    if(enabled){checkBox.x+=3;checkBox.y+=3;checkBox.w-=6;checkBox.h-=6;SDL_RenderFillRectF(ren_,&checkBox);}
                    py+=22;
                }
                if(pages>1){lbCycle(winW-300.0f,py,280,20,std::string(pageLabel)+" "+std::to_string(page+1)+" / "+std::to_string(pages),true,[&page,pages](int d){page=(page+pages+d)%pages;},kPackPx);py+=22;}
            };
            // COSMETIC MODS heads the always-on archives and the ticked packs alike (a
            // campaign room shows the heading only when it has always-on archives to list).
            if(gameRoom || !alwaysOnMods_.empty()){blockText("COSMETIC MODS",winW-296.0f,py,1.6f,{205,210,225,255});py+=22;}
            if(!alwaysOnMods_.empty()) {
                // As many names as fit on one line (280 px), then "+N".
                constexpr float kLineW=280;
                std::string line="ALWAYS ON: ";
                const size_t n=alwaysOnMods_.size();size_t shown=0;
                for(;shown<n;++shown){
                    const std::string next=(shown?", ":"")+alwaysOnMods_[shown];
                    if(blockWidth(line+next+(shown+1<n?" +99":""),1.3f)>kLineW)break;
                    line+=next;
                }
                if(!shown){line=tak::uiTextFit(line+alwaysOnMods_[0],1.3f,kLineW-(n>1?blockWidth(" +99",1.3f):0));shown=1;}
                if(shown<n)line+=" +"+std::to_string(n-shown);
                blockText(line,winW-296.0f,py,1.3f,{170,200,170,255});py+=20;
            }
            if(gameRoom) {
                packList(availableCosmeticMods_,settings_?settings_->cosmeticModPacks:std::vector<std::string>{},cosmeticModPage_,false,
                         "NO PACKS IN MODS/COSMETIC","COSMETIC PAGE",[this](std::vector<std::string> selection){
                    if(!settings_)return;
                    settings_->cosmeticModPacks=std::move(selection);saveSettings(*settings_);
                });
                py+=4;
                blockText(gameOn?"GAME MODS":"GAME MODS: OFF",winW-296.0f,py,1.6f,{205,210,225,255});py+=22;
                if(gameOn && host) {
                    packList(availableGameMods_,mp_->gameModPacks(),gameModPage_,mp_->modBusy(),"NO PACKS IN MODS/GAME","GAME PAGE",
                             [this](std::vector<std::string> selection){
                        mp_->setGameModPacks(selection);
                        if(settings_){settings_->gameModPacks=std::move(selection);saveSettings(*settings_);}
                    });
                } else if(gameOn) {
                    // The host's packs, as named by the verified package (read-only here).
                    std::string names=mp_->modPackage()?mp_->modPackage()->mapPath:"";
                    if(names.empty())names=mp_->modPackage()?"NONE SELECTED":"WAITING FOR THE HOST";
                    blockText(tak::uiTextFit("HOST: "+names,1.3f,280),winW-296.0f,py,1.3f,{180,190,205,255});py+=22;
                }
                if(gameOn){const auto status=tak::uiTextFit(mp_->modStatus(),1.2f,280);blockText(status,winW-296.0f,py+3,1.2f,{205,190,135,255});py+=23;}
            }
            modBottom=py+8;
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
            char sn[8]; std::snprintf(sn, sizeof sn, "%d", i + 1);
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
            if (host && !campaign && s.type != 1) {
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
            if (s.type == 1) cell(s.name, x + 100, y + 8, 1.8f, 154, {225, 228, 236, 255});
            else if ((s.type == 0 || s.type == 3) && (needSeats >> i & 1))
                blockText("NEEDED BY SCENARIO", x + 100, y + 9, 1.4f, {235, 205, 120, 255});
            else if (s.type == 2) {
                cell(s.name.empty() ? "Computer" : s.name, x + 100, y + 8, 1.6f, 74, {210, 200, 150, 255});
                // AI difficulty (host cycles DEFENSIVE -> EASY -> NORMAL -> HARD -> ABSURD
                // -> ABSURDLY DEFENSIVE, the wire aiLevel order).
                constexpr int kLevels = tak::ai::kAiLevels;
                static const char* diffName[kLevels] = {"DEFENSIVE", "EASY", "NORMAL", "HARD", "ABSURD",
                                                        "ABSURDLY DEFENSIVE"};
                static const SDL_Color diffCol[kLevels] = {
                    {150, 180, 210, 255},   // passive  (calm blue)
                    {150, 200, 150, 255},   // easy     (green)
                    {210, 200, 150, 255},   // normal   (neutral gold)
                    {225, 150, 140, 255},   // hard     (red)
                    {230, 120, 220, 255},   // absurd   (magenta)
                    {165, 140, 245, 255}};  // absurdly defensive (violet)
                const uint8_t lvl = uint8_t(s.aiLevel % kLevels);
                // Two words that overrun the 82 px column stack in two smaller lines.
                if (const char* gap = std::strchr(diffName[lvl], ' ')) {
                    blockText(std::string(diffName[lvl], gap), x + 178, y + 3, 1.1f, diffCol[lvl]);
                    blockText(gap + 1, x + 178, y + 13, 1.1f, diffCol[lvl]);
                } else
                    blockText(diffName[lvl], x + 178, y + 9, 1.4f, diffCol[lvl]);
                if (host && !campaign) { SDL_FRect db{x + 176, y + 2, 82, 26};
                    lobbyHots_.push_back({db, [this, i](int direction) { const auto& s2 = mpRoom().slots[i];
                        mp_->setSlot(i, s2.type, s2.faction, s2.color, s2.team, s2.ready,
                                     uint8_t((s2.aiLevel + direction + kLevels) % kLevels)); }}); }
            }
            if (i >= startCount && size_t(i) < bySlotSpot.size() && bySlotSpot[size_t(i)] >= 0) {
                const int spot = bySlotSpot[size_t(i)];
                blockText("NO START " + std::to_string(i + 1) + ": " +
                          (spot < startCount ? "USES START " + std::to_string(spot + 1) : std::string("NEAR MAP CENTRE")),
                          x + 100, y + 21.5f, 1.1f, {235, 205, 120, 255});
            }
            // faction / color / team edit: your own row, or (host) any AI row.
            bool canEdit = !campaign && (mine || (host && s.type == 2));
            cell(factionName(s.faction), x + 260, y + 8, 1.6f, 94, {200, 205, 215, 255});
            if (canEdit) { SDL_FRect fb{x + 260, y + 6, 90, 18};
                lobbyHots_.push_back({fb, [this, i](int direction) { const auto& s2 = mpRoom().slots[i];
                    mp_->setSlot(i, s2.type, (s2.faction + direction + 5) % 5, s2.color, s2.team, s2.ready, s2.aiLevel); }}); }
            colorSwatch(x + 360, y + 5, 20, s.color, canEdit ? std::function<void(int)>([this, i](int direction) {
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
            blockText(tm, x + 392, y + 8, 1.8f, {200, 205, 215, 255});
            if (canEdit) { SDL_FRect teb{x + 392, y + 6, 34, 18};
                lobbyHots_.push_back({teb, [this, i](int direction) { const auto& s2 = mpRoom().slots[i];
                    mp_->setSlot(i, s2.type, s2.faction, s2.color, uint8_t((s2.team + direction + tak::net::kMaxSlots) % tak::net::kMaxSlots), s2.ready, s2.aiLevel); }}); }
            if (s.type == 1 && !singlePlayer_) {   // SP: the player is always ready, no column
                SDL_Color rc = s.ready ? SDL_Color{130, 230, 140, 255} : SDL_Color{120, 125, 135, 255};
                blockText(s.ready ? "READY" : "NOT READY", x + 440, y + 8, 1.6f, rc);
            }
            // host kick button for other humans
            if (host && !campaign && s.type == 1 && !mine) {
                lbBtn(x + row.w - 54, y + 3, 50, 22, "KICK", true, [this, i] { mp_->kick(i); });
            }
            // Any seated player may take an OPEN seat (Msg::MoveSeat): the seat is the
            // start under BY SLOT, and the order under FIXED. Open seats are always
            // inside the map's capacity (the server closes the rest). Not in a campaign
            // mission or a Crusades battle, whose seats are given.
            if (!campaign && room.mission.empty() && room.mySlot >= 0 && s.type == 0)
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
        // needs is played; ordinary skirmishes need two.
        bool canStart = host && startValid(room, scenarioOn, needSeats) && room.mapsReady && !mp_->modPending() &&
            (!room.opts.gameMods || bool(mp_->modPackage()));
        lbBtn(bx, y, 130, 30, "START", canStart, [this] { mp_->startGame(); },
              {70, 110, 70, 255});
        lbBtn(bx + 142, y, 120, 30, "LEAVE", true, [this] {
            const bool campaign = mp_->campaignRoom();
            mp_->leaveGame();
            if (campaign) openCrusades(crusadesSelectedTerritory());
            else lobbyScreen_ = singlePlayer_ ? LobbyScreen::Create : LobbyScreen::Browser;
            spSpectate_ = false;
            mpReadied_ = false; mpStarted_ = false; specAutoSeated_ = false; });
        // Room readiness also includes game mods and other players. A
        // completed local map is not progress to display while packs change;
        // their status is already shown beside the pack list.
        const auto& mapStatus = mp_->mapStatus();
        if (!room.mapsReady && !mp_->modPending() && !mp_->modBusy() &&
            mapStatus != "MAP READY" && mapStatus != "MAP VERIFIED")
            blockText(mapStatus.empty() ? "WAITING FOR MAP VERIFICATION" : mapStatus,
                bx + 280, y + 9, 1.3f, {235, 205, 120, 255});
        // Why the server refused the last START (a needed seat, setupMatch's refusal...).
        if (!mp_->startError().empty()) {
            const std::string why = "CANNOT START: " + mp_->startError();
            blockText(tak::uiTextFit(why, 1.1f, winW - 360.0f), x, y + 33, 1.1f, {255, 130, 110, 255});
        }
        // Creation settings are read-only here and come from the room, not
        // this client's saved preferences. Keep every rule visible with 8 slots.
        y += 44;
        const SDL_Color infoColor{205,210,225,255};
        // Six rows of two columns at a 16.5 pitch (POWER CODES is the sixth), above the
        // MAP SCENARIO rules (from y+99) or a generated map's recipe (from y+98).
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
        if(!campaign && room.mission.empty())
            info(5,0,std::string("POWER CODES: ")+onOff(room.opts.powerCodes));
        // ALLOW SPECTATORS is the one room rule the host may still flip here: it only
        // decides who may WATCH once the game runs, so no player has planned around it.
        // The server adopts it until START and refuses Spectate while it is off.
        // For the host it lights up under the pointer (the button hover colour), the one
        // hint that this line of the read-only list is a toggle.
        if(!singlePlayer_ && !campaign) {
            const std::string label=std::string("ALLOW SPECTATORS: ")+onOff(room.opts.allowSpectators);
            const SDL_FRect hit{x+300.0f,y+4*kInfoPitch-2,blockWidth(label,1.6f),18};
            blockText(label,hit.x,y+4*kInfoPitch,1.6f,host && lbHot(hit) ? SDL_Color{180,200,240,255} : infoColor);
            if(host) lobbyHots_.push_back({hit,[this](int) {
                auto o=mpRoom().opts; o.allowSpectators=o.allowSpectators?0:1; mp_->setGameOptions(o); }});
        }
        if(singlePlayer_)
            info(3,1,std::string("SPECTATE (WATCH AIS): ")+onOff(mp_->isSpectator()));
        else if(!campaign) {
            const auto& games=mp_->games();
            const auto game=std::find_if(games.begin(),games.end(),[&](const auto& g){return g.id==room.id;});
            // Show whether access is protected, never the password itself.
            if(game!=games.end() || host)
                info(3,1,std::string("PASSWORD: ")+((game!=games.end()?game->passworded:!createPass_.empty())?"SET":"NONE"));
        }
        // MAP SCENARIO, under the grid for a file map (the rows a generated map's recipe
        // uses): the rule, and when on, what it changes and the seats it needs. Not in a
        // Crusades battle room, which never plays one (the server keeps it 0 there).
        if(fileMap && !campaign) {
            const SDL_Color amber{235,205,120,255};
            const bool testCopy=mp_->mapPackage() && scenarioLobby_.marker && scenarioLobby_.runnable();
            const bool wanted=room.opts.mapScenario!=0 || testCopy;
            // A Test Map copy whose only scenario content is a Use Only list: no scenario,
            // but the copy's opt-in applies the list (not in a stress or benchmark room).
            const bool testUseOnly=mp_->mapPackage() && scenarioLobby_.marker && !scenarioLobby_.hasScenario &&
                                   scenarioLobby_.useOnlyTypes>=0 && !room.opts.stressTest && !room.opts.benchmark;
            // Three rows between the grid's last row (it ends near y+94) and the canvas edge.
            auto rule=[&](int row,int column,const std::string& text,bool warning=false) {
                blockText(text,x+column*300.0f,y+99+row*12.5f,1.2f,warning ? amber : infoColor);
            };
            rule(0,0,testCopy ? "MAP SCENARIO: ON (TEST MAP)" : wanted ? "MAP SCENARIO: ON" : "MAP SCENARIO: OFF");
            if(testUseOnly && !wanted) rule(0,1,"TEST MAP: "+tak::sim::scenarioUseOnlyLine(scenarioLobby_,false));
            else if(wanted && !mp_->mapPackage()) rule(0,1,"CHECKING SCENARIO...");
            else if(wanted && !scenarioOn && !room.opts.stressTest && !room.opts.benchmark) {
                const auto why=tak::sim::scenarioReason(scenarioLobby_);
                rule(0,1,why.empty() ? std::string("NO SCENARIO TO PLAY") : why.front(),true);
            } else if(scenarioOn) {
                const auto lines=tak::sim::scenarioSummary(scenarioLobby_,false);   // placed, seats, use only, endings
                bool missing=false;
                for(int i=0;i<tak::net::kMaxSlots;++i)
                    if((needSeats>>i & 1) && room.slots[i].type!=1 && room.slots[i].type!=2) missing=true;
                if(lines.size()>0) rule(0,1,lines[0].text);
                if(lines.size()>1) rule(1,0,lines[1].text,missing);
                if(lines.size()>2) rule(1,1,lines[2].text);
                if(lines.size()>3) rule(2,0,lines[3].text,lines[3].warning);
                // Placements replace Monarchs, so a seated player the scenario gives no
                // units starts with nothing.
                uint8_t empty=0;
                for(int i=0;i<tak::net::kMaxSlots;++i)
                    if((room.slots[i].type==1 || room.slots[i].type==2) && !(scenarioLobby_.placedSeats>>i & 1)) empty|=uint8_t(1u<<i);
                if(scenarioLobby_.placed>0 && empty) rule(2,1,"NO PLACED UNITS: PLAYER "+tak::sim::seatList(empty),true);
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
            } catch(const std::exception&) {} // Invalid map IDs cannot start.
        }
        // chat panel on the right (multiplayer only -- there's no one to chat with in SP)
        if (!singlePlayer_) {
            float chx = winW - 300.0f, chy = modBottom, chw = 280;
            SDL_SetRenderDrawColor(ren_, 22, 24, 32, 255);
            SDL_FRect cp{chx, chy, chw, std::max(50.0f,winH - chy - 72.0f)}; SDL_RenderFillRectF(ren_, &cp);
            blockText("CHAT", chx + 8, chy + 6, 1.8f, {160, 165, 180, 255});
            // Newest at the bottom; each message wraps to the panel's width (measured,
            // so it fits in either UI FONT), and the oldest rows go off the top.
            float ly = chy + cp.h - 20;
            for (auto it = chatLog_.rbegin(); it != chatLog_.rend() && ly > chy + 26; ++it) {
                const auto rows = tak::uiTextWrap(it->first + ": " + it->second, 1.4f, chw - 16);
                for (auto r = rows.rbegin(); r != rows.rend() && ly > chy + 26; ++r) {
                    blockText(*r, chx + 8, ly, 1.4f, {200, 205, 215, 255});
                    ly -= 16;
                }
            }
            lbField(chx, winH - 66.0f, chw - 70, "SAY", chatDraft_, 4);
            lbBtn(chx + chw - 62, winH - 52.0f, 56, 24, "SEND", !chatDraft_.empty(), [this] {
                mp_->chat(chatDraft_); chatDraft_.clear(); });
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

    void GameView::drawStartMarkers(const SDL_FRect& img, const std::vector<int>& colorOfStart) {
        if (mapPreviewStarts_.empty() || img.w <= 0 || img.h <= 0) return;
        // Sized from the preview, so the 192 px create preview and the room's smaller
        // one both read: a black rim and a white ring keep it legible on any terrain.
        const float r = std::clamp(std::min(img.w, img.h) * 0.06f, 7.0f, 11.0f);
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
        }
        SDL_SetRenderDrawColor(ren_, 70, 76, 96, 255); SDL_RenderDrawRectF(ren_, &box);
    }

    void GameView::lobbyMouse(float& mx, float& my) const {
        mx = mouseX_ / lobbyScale_ - lobbyOffX_;
        my = mouseY_ / lobbyScale_ - lobbyOffY_;
    }

    void GameView::lobbyInput(const SDL_Event& e, int winW, int winH) {
        (void)winW; (void)winH;
        if (lobbyScreen_ == LobbyScreen::Crusades && crusadesScreen_ && mp_ &&
            mp_->state() != tak::net::MpClient::State::InRoom) {
            if (e.type == SDL_MOUSEMOTION) { mouseX_ = float(e.motion.x); mouseY_ = float(e.motion.y); }
            if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) { mouseX_ = float(e.button.x); mouseY_ = float(e.button.y); }
            float mx, my; lobbyMouse(mx, my);
            const auto action = crusadesScreen_->input(e, int(mx), int(my));
            if (action == tak::CrusadesScreen::Action::Back) {
                lobbyScreen_ = LobbyScreen::Browser; mp_->subscribeCampaign(""); SDL_StopTextInput();
            } else if (action == tak::CrusadesScreen::Action::Reconnect) {
                reconnectRequested_ = true; menuRequested_ = true; SDL_StopTextInput();
            }
            return;
        }
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            if (genSlider_ >= 0 || lbField_ == 5) applyGenParams();
            genSlider_ = -1; mapDrag_ = false;
            saveCreatePreferences();
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
        } else if (e.type == SDL_MOUSEBUTTONDOWN &&
                   (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)) {
            mouseX_ = float(e.button.x); mouseY_ = float(e.button.y);
            const bool forward = e.button.button == SDL_BUTTON_LEFT;
            if (forward) { if (lbField_ == 5) applyGenParams(); lbField_ = 0; SDL_StopTextInput(); }
            float mx, my; lobbyMouse(mx, my);
            if (forward && ptIn(mapThumbRect_, mx, my)) { mapDrag_ = true; return; }   // grab the thumb
            for (int gi = 0; gi < 6; ++gi)   // grab a density slider
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
            if (f && f->size() < 24) {
                if (lbField_ == 5) {
                    for (unsigned char c : std::string(e.text.text))
                        if (f->size() < 24 && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_')) *f += char(c);
                } else *f += e.text.text;
            }
            saveCreatePreferences();
        } else if (e.type == SDL_KEYDOWN && lbField_) {
            if (e.key.keysym.sym == SDLK_BACKSPACE) { std::string* f = lbFieldBuf(); if (f && !f->empty()) f->pop_back(); }
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
