#include "client/gameview.h"

// Out-of-line GameView method definitions (the game room's CHANGE MAP panel): a panel over
// the room with the create screen's map list (EXISTING MAP) and generator (RANDOM MAP),
// whose PICK asks the server to change the room's map (MpClient::setRoomMap).

    // ---- not built yet ----
    // Each room action reports kNotImplemented, so a TAK_ROOM_SCRIPT map: step fails at
    // once; the panel never opens, so its drawing and input hooks do nothing.
    void GameView::openMapPicker() { roomActError_ = kNotImplemented; }
    void GameView::closeMapPicker() { picker_.open = false; }
    void GameView::drawMapPicker(int, int) {}
    bool GameView::pickerInput(const SDL_Event&) { return false; }
    void GameView::pickerSelect(const std::string&) { roomActError_ = kNotImplemented; }
    void GameView::pickerPick() { roomActError_ = kNotImplemented; }
    void GameView::pickerStep() {}
