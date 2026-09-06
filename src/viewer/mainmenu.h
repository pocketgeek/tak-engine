#pragma once

// The retail Total Annihilation: Kingdoms front-end: the three-door main menu.
// Layout is data-driven from guis/mainmenu.gui (background, door + button rects,
// GAF art); each door plays its Bink (.bik) hover video (idle -> hover-in -> loop
// -> hover-out) when FFmpeg is available, falling back to the GAF door art. Returns
// the player's choice so the app can enter single-player, multiplayer, etc.

#include <SDL.h>

#include <string>

#include "hpi/hpi.h"

namespace tak {

class MainMenu {
public:
    enum class Choice { None, SinglePlayer, Campaign, Multiplayer, Options, Exit };

    // ren: the app renderer. vfs: the mounted retail root (for guis/anims). install:
    // the retail install dir on disk (the door .bik videos are loose files under
    // Movies/Gui/, not in the VFS).
    MainMenu(SDL_Renderer* ren, const hpi::Vfs& vfs, std::string install);
    ~MainMenu();
    MainMenu(const MainMenu&) = delete;
    MainMenu& operator=(const MainMenu&) = delete;

    // Run the front-end loop until the user picks a door/button or closes the
    // window. If shotPath is non-empty, render a single frame there and return None
    // (headless screenshot for tests).
    Choice run(const std::string& shotPath = "");

private:
    struct Impl;
    Impl* d_;
};

}  // namespace tak
