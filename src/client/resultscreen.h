#pragma once

// The post-mission result screen: VICTORY or DEFEAT, then the player's next step.
// A self-contained modal (own pump + cursor), drawn with the shared block font, shown
// after a campaign mission resolves. See docs/campaign-design.md.

#include <SDL.h>

#include <string>

#include "hpi/hpi.h"

namespace tak {

class MenuMusic;
struct Settings;

enum class ResultChoice { Menu, Retry, Next };

class ResultScreen {
public:
    // Show VICTORY (victory=true) or DEFEAT for mission `title`. `hasNext` enables the
    // NEXT MISSION button (victory + a mission after this one). Returns the player's
    // choice. `settings` supplies the cursor scale; `music` keeps the front-end track
    // looping.
    static ResultChoice run(SDL_Renderer* ren, const hpi::Vfs& vfs, bool victory,
                            const std::string& title, bool hasNext,
                            Settings* settings = nullptr, MenuMusic* music = nullptr);
};

}  // namespace tak
