#include "client/gameview.h"
#include "net/matchconfig.h"
#include <cmath>
#include <cstring>
#include <random>

// Out-of-line GameView method definitions (the game room): the left column -- the room's
// name, the slot table, READY / START / LEAVE and the rules -- and the room's per-frame
// reactions. The right column is drawRoomColumn (gameview_lobby.cpp); the CHANGE MAP
// panel is gameview_mappicker.cpp.

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

    GameView::RoomFrame GameView::roomFrame(int winW) {
        const auto& room = mp_->room();
        RoomFrame f;
        // The slot table starts just under the page title (multiplayer: under the room's
        // name). The map's name heads the right column, above its preview.
        if (!singlePlayer_) f.y += 34;
        // The right column (map, mods, chat) and the slot table's width, both from kRoomColW.
        f.colX = winW - kRoomEdge - kRoomColW;
        f.rowW = f.colX - kRoomEdge - f.x;
        f.host = (room.hostId == mp_->myClientId());
        // MAP SCENARIO (259): the room plays its map's scenario when the option is on, or
        // for a Test Map copy, read from the verified package (the host's own until then).
        // The seats that scenario needs are tagged in the slot table and must be played to START.
        if (scenarioLobbyPackage_.lock() != mp_->mapPackage()) {
            scenarioLobbyPackage_ = mp_->mapPackage();
            // With this client's registry, so the Use Only count leaves out the names no
            // unit answers to, as setupMatch does (the create screen counts the same way).
            scenarioLobby_ = mp_->mapPackage() ? tak::net::maps::scenarioInfo(*mp_->mapPackage(), &registry_) : tak::sim::ScenarioInfo{};
        }
        f.roomMap = room.mission.empty();
        f.fileMap = room.mission.empty() && !tak::mapgen::isGeneratedMapId(room.mapId);
        f.scenarioOn = f.fileMap && !room.opts.stressTest && !room.opts.benchmark && mp_->mapPackage() &&
                       scenarioLobby_.active(room.opts.mapScenario != 0);
        f.needSeats = f.scenarioOn ? scenarioLobby_.requiredSeats() : 0;
        // The room's own MAP SCENARIO line reads ON from the option alone (a joiner sees it
        // before the package is verified); the rule cells that follow it agree with it.
        f.scenarioShown = f.fileMap && (room.opts.mapScenario != 0 || f.scenarioOn);
        // A multiplayer room (not single player's private one) needs two human players:
        // AI alone is single player.
        int humans = 0;
        for (const auto& s : room.slots) humans += s.type == 1;
        f.needHuman = !singlePlayer_ && humans < 2;
        return f;
    }

    // A room playing its map's scenario may start solo, once every seat the scenario needs
    // is played; ordinary skirmishes need two.
    bool GameView::roomCanStart(const RoomFrame& f) const {
        const auto& room = mp_->room();
        return f.host && startValid(room, f.scenarioOn, f.needSeats) && room.mapsReady && !mp_->modPending() &&
               (!room.opts.gameMods || bool(mp_->modPackage())) && !f.needHuman;
    }

    void GameView::roomStart() { mp_->startGame(); }

    // The host's slot type cell (drawRoomSlots): an empty seat becomes OPEN, AI or CLOSED,
    // keeping its side, colour and team.
    void GameView::roomSetSlotType(int slot, uint8_t type) {
        const auto& room = mpRoom();
        if (room.hostId != mp_->myClientId()) { roomActError_ = "only the host changes a seat"; return; }
        if (slot < 0 || slot >= tak::net::kMaxSlots || room.slots[slot].type == 1) {
            roomActError_ = "not an empty seat"; return;
        }
        const auto& s = room.slots[slot];
        mp_->setSlot(slot, type, s.faction, s.color, s.team, 0, s.aiLevel);
    }

    void GameView::roomStep() {
        const auto& room = mp_->room();
        const bool host = (room.hostId == mp_->myClientId());
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
        // SP: no READY button -- the player is always ready. Keep the slot marked
        // ready (once, self-limiting) so the host's START enables.
        if (singlePlayer_ && room.mySlot >= 0 && !room.slots[room.mySlot].ready) {
            const auto& s = room.slots[room.mySlot];
            mp_->setSlot(room.mySlot, 1, s.faction, s.color, s.team, 1, s.aiLevel);
        }
        pickerStep();   // the CHANGE MAP panel's answer (gameview_mappicker.cpp)
    }

    void GameView::drawRoom(int winW, int winH) {
        const auto& room = mp_->room();
        const RoomFrame f = roomFrame(winW);
        const float x = f.x, rowW = f.rowW, colX = f.colX;
        const bool host = f.host, needHuman = f.needHuman;
        if (!singlePlayer_) blockText(room.name, x, 54, 2.2f, {210, 210, 220, 255});
        drawRoomColumn(f, winH);   // the map, its preview, the mods and the chat
        float y = f.y;
        drawRoomSlots(f, y);
        y += 10;
        // controls (single player has no READY button: the player is always ready, roomStep)
        float bx = x;
        if (!singlePlayer_) {
            bool iAmReady = room.mySlot >= 0 && room.slots[room.mySlot].ready;
            lbBtn(x, y, 130, 30, iAmReady ? "UNREADY" : "READY", room.mySlot >= 0, [this, iAmReady] {
                const auto& s = mpRoom().slots[mpRoom().mySlot];
                mp_->setSlot(mpRoom().mySlot, 1, s.faction, s.color, s.team, iAmReady ? 0 : 1, s.aiLevel); });
            bx = x + 142;
        }
        bool canStart = roomCanStart(f);
        // The server takes a moment to build the game: show the click landed.
        if (mp_->startPending() || mp_->starting()) lbBtn(bx, y, 130, 30, "STARTING...", true, nullptr, {70, 110, 70, 255});
        else lbBtn(bx, y, 130, 30, "START", canStart, [this] { roomStart(); }, {70, 110, 70, 255});
        lbBtn(bx + 142, y, 120, 30, "LEAVE", true, [this] {
            // A LOCAL SERVER's host leaving closes the game for everyone: ask first.
            if (lanHost_ && lanOthersInRoom() > 0) { lanLeavePrompt_ = true; return; }
            leaveRoom();
        });
        // Room readiness also includes game mods and other players. A
        // completed local map is not progress to display while packs change;
        // their status is already shown beside the pack list.
        // The status beside the buttons stops short of the map column, shrunk before
        // it is cut, as the refusal line below is; too long for one line even then, it
        // takes two.
        const auto status = [&](const std::string& text) {
            const float room = std::max(40.0f, colX - 12 - (bx + 280));
            constexpr SDL_Color amber{235, 205, 120, 255};
            const float px = tak::uiTextFitPx(text, 1.3f, room, 1.1f);
            if (blockWidth(text, px) <= room) { blockText(text, bx + 280, y + 9, px, amber); return; }
            auto rows = tak::uiTextWrap(text, 1.1f, room);
            for (size_t k = 2; k < rows.size(); ++k) rows[1] += " " + rows[k];
            blockText(rows[0], bx + 280, y + 3, 1.1f, amber);
            if (rows.size() > 1) blockText(tak::uiTextFit(rows[1], 1.1f, room), bx + 280, y + 16, 1.1f, amber);
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
            const float px = tak::uiTextFitPx(why, 1.1f, rowW, 0.8f);
            blockText(tak::uiTextFit(why, px, rowW), x, y + 33, px, {255, 130, 110, 255});
        }
        // Creation settings are read-only here and come from the room, not
        // this client's saved preferences. Keep every rule visible with 8 slots.
        y += 44;
        drawRoomRules(f, y);
    }

    void GameView::drawRoomSlots(const RoomFrame& f, float& y) {
        const auto& room = mp_->room();
        const float x = f.x, rowW = f.rowW;
        const bool host = f.host;
        const uint8_t needSeats = f.needSeats;
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
        // Each column's start in the row: type, name, AI level, side, colour, team, READY.
        constexpr float kType = 28, kName = 92, kLevel = 156, kSide = 232, kColour = 302, kTeam = 330, kReady = 372;
        for (int i = 0; i < tak::net::kMaxSlots; ++i) {
            const auto& s = room.slots[i];
            bool mine = (i == room.mySlot);
            SDL_FRect row{x, y, rowW, 30};
            SDL_SetRenderDrawColor(ren_, mine ? 40 : 26, mine ? 48 : 30, mine ? 66 : 40, 255);
            SDL_RenderFillRectF(ren_, &row);
            char sn[12]; std::snprintf(sn, sizeof sn, "%d", i + 1);
            blockText(sn, x + 8, y + 8, 1.8f, {150, 155, 170, 255});
            // type (host may cycle open<->closed on empty slots)
            SDL_Color tcol = s.type == 1 ? SDL_Color{200, 230, 200, 255}
                           : s.type == 2 ? SDL_Color{230, 220, 150, 255}
                                         : SDL_Color{130, 135, 150, 255};
            // Each column's text fits its column, shrinking rather than running into the next.
            auto cell = [&](const std::string& t, float cx, float cy, float px, float colW, SDL_Color c) {
                const float fpx = tak::uiTextFitPx(t, px, colW * 0.97f, px * 0.7f);
                blockText(tak::uiTextFit(t, fpx, colW), cx, cy + (px - fpx) * 3.5f, fpx, c);
            };
            cell(typeName[s.type % 4], x + kType, y + 8, 1.6f, 56, tcol);
            if (host && s.type != 1) {
                // Host cycles an empty slot OPEN -> AI -> CLOSED. AI opponents are
                // seated (and run) on the server; this is how you set up multi-AI
                // games (including single-player vs several AIs).
                SDL_FRect tb{x + kType, y + 6, 60, 18};
                lobbyHots_.push_back({tb, [this, i, t = s.type](int direction) {
                    roomSetSlotType(i, direction > 0 ? (t == 0 ? 2 : t == 2 ? 3 : 0)
                                                     : (t == 0 ? 3 : t == 3 ? 2 : 0)); }});
            }
            // Single player has no READY column, so the side / colour / team columns move up
            // to the MOVE HERE / KICK column (row.w - 94) and the name takes the room.
            const float wide = singlePlayer_ ? row.w - 98 - (kTeam + 34) : 0.0f;
            if (s.type == 1) cell(s.name, x + kName, y + 8, 1.8f, kSide - kName - 4 + wide, {225, 228, 236, 255});
            else if ((s.type == 0 || s.type == 3) && (needSeats >> i & 1))
                cell("NEEDED BY SCENARIO", x + kName, y + 9, 1.4f, kSide - kName - 4 + wide, {235, 205, 120, 255});
            else if (s.type == 2) {
                cell(s.name.empty() ? "Computer" : s.name, x + kName, y + 8, 1.6f, kLevel - kName - 4 + wide, {210, 200, 150, 255});
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
                // Two words that overrun the column stack in two smaller lines.
                constexpr float kLevelW = kSide - kLevel - 2;
                if (const char* gap = std::strchr(diffName[lvl], ' ')) {
                    cell(std::string(diffName[lvl], gap), x + kLevel + wide, y + 3, 1.1f, kLevelW, diffCol[lvl]);
                    cell(gap + 1, x + kLevel + wide, y + 13, 1.1f, kLevelW, diffCol[lvl]);
                } else
                    cell(diffName[lvl], x + kLevel + wide, y + 9, 1.4f, kLevelW, diffCol[lvl]);
                if (host) { SDL_FRect db{x + kLevel - 2 + wide, y + 2, kLevelW + 2, 26};
                    lobbyHots_.push_back({db, [this, i](int direction) { const auto& s2 = mpRoom().slots[i];
                        const int at = int(std::find(kCycle, kCycle + kLevels, s2.aiLevel) - kCycle);
                        mp_->setSlot(i, s2.type, s2.faction, s2.color, s2.team, s2.ready,
                                     kCycle[(at + direction + kLevels) % kLevels]); }}); }
            }
            if (i >= startCount && size_t(i) < bySlotSpot.size() && bySlotSpot[size_t(i)] >= 0) {
                const int spot = bySlotSpot[size_t(i)];
                blockText("NO START " + std::to_string(i + 1) + ": " +
                          (spot < startCount ? "USES START " + std::to_string(spot + 1) : std::string("NEAR MAP CENTRE")),
                          x + kName, y + 21.5f, 1.1f, {235, 205, 120, 255});
            }
            // faction / color / team edit: your own row, or (host) any AI row.
            bool canEdit = (mine || (host && s.type == 2));
            cell(factionName(s.faction), x + kSide + wide, y + 8, 1.6f, kColour - kSide - 4, {200, 205, 215, 255});
            if (canEdit) { SDL_FRect fb{x + kSide + wide, y + 6, kColour - kSide - 4, 18};
                lobbyHots_.push_back({fb, [this, i](int direction) { const auto& s2 = mpRoom().slots[i];
                    mp_->setSlot(i, s2.type, (s2.faction + direction + 5) % 5, s2.color, s2.team, s2.ready, s2.aiLevel); }}); }
            colorSwatch(x + kColour + wide, y + 5, 20, s.color, canEdit ? std::function<void(int)>([this, i](int direction) {
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
            blockText(tm, x + kTeam + wide, y + 8, 1.8f, {200, 205, 215, 255});
            if (canEdit) { SDL_FRect teb{x + kTeam + wide, y + 6, 34, 18};
                lobbyHots_.push_back({teb, [this, i](int direction) { const auto& s2 = mpRoom().slots[i];
                    mp_->setSlot(i, s2.type, s2.faction, s2.color, uint8_t((s2.team + direction + tak::net::kMaxSlots) % tak::net::kMaxSlots), s2.ready, s2.aiLevel); }}); }
            if (s.type == 1 && !singlePlayer_) {   // SP: the player is always ready, no column
                SDL_Color rc = s.ready ? SDL_Color{130, 230, 140, 255} : SDL_Color{120, 125, 135, 255};
                cell(s.ready ? "READY" : "NOT READY", x + kReady, y + 8, 1.6f, row.w - 58 - kReady, rc);
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
    }

    void GameView::drawRoomRules(const RoomFrame& f, float y) {
        const auto& room = mp_->room();
        const float x = f.x, rowW = f.rowW;
        const bool host = f.host, fileMap = f.fileMap, scenarioOn = f.scenarioOn, scenarioShown = f.scenarioShown;
        const uint8_t needSeats = f.needSeats;
        const SDL_Color infoColor{205,210,225,255};
        // Six rows of two columns at a 16.5 pitch (POWER CODES and MAP SCENARIO are the
        // sixth), above a MAP SCENARIO's details (from y+99), across the slot table's
        // width: a label too long for its column shrinks. A generated map's recipe is
        // on the preview's hover box and the full-window map instead.
        constexpr float kInfoPitch = 16.5f;
        const float infoColW = std::floor(rowW / 2);
        auto info = [&](int row, int column, const std::string& label) {
            const float px = tak::uiTextFitPx(label, 1.6f, infoColW - 8, 1.2f);
            blockText(tak::uiTextFit(label, px, infoColW - 8), x+column*infoColW, y+row*kInfoPitch+(1.6f-px)*3.5f, px, infoColor);
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
            const SDL_FRect hit{x+infoColW,y+4*kInfoPitch-2,blockWidth(label,1.6f),18};
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
                blockText(tak::uiTextFit(text,1.2f,infoColW-8),x+float(detail%2)*infoColW,y+99+float(detail/2)*12.5f,1.2f,warning ? amber : infoColor);
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
    }

    // ---- room editing: the host's edits in the room, not built yet ----
    // Each room action reports kNotImplemented, so a TAK_ROOM_SCRIPT verb wired to it fails
    // at once; the input and drawing hooks do nothing.
    void GameView::rememberRoomPrefs(RoomField) {}
    void GameView::rememberRoomSort() {}
    void GameView::roomFieldCommit(int) { roomActError_ = kNotImplemented; }
    void GameView::roomFieldBlur() {}
    bool GameView::roomInput(const SDL_Event&) { return false; }
    void GameView::roomRuleCommit(RoomField, int) { roomActError_ = kNotImplemented; }
    void GameView::roomScenarioClick() { roomActError_ = kNotImplemented; }
    void GameView::roomWatch() { roomActError_ = kNotImplemented; }
    void GameView::roomSitHere(int) { roomActError_ = kNotImplemented; }
    void GameView::drawRoomFlash(RoomField, const SDL_FRect&) {}
