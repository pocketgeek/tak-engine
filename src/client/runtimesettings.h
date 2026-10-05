// runtimesettings -- push Settings into the runtime globals that render code reads.
//
// A few display options cannot be read from Settings at the point they are used: the
// door-video path has no settings pointer, MainMenu::playIntro is static, and the art
// helpers run deep inside texture creation. Those read globals instead (art::g_smoothArt,
// art::g_cursorFactor, video::g_deblock), and something has to keep the globals in step
// with the Settings the user is editing.
//
// That "something" is this one function, called from app startup and from every
// OptionsScreen onChange -- rather than each host remembering to poke each global. The
// per-host version of this is how SMOOTH MOVIES shipped ignoring its own toggle until a
// restart: the flag was refreshed at startup and menu entry, so changing the option and
// replaying the intro in the same session used the stale value. One function means a new
// option is wired once and reaches every host, including the DEFAULTS button (which goes
// through the same onChange).

#pragma once

#include "client/artscale.h"
#include "client/settings.h"
#include "client/videofilter.h"

namespace tak {

inline void applyRuntimeSettings(const Settings& s) {
    // Active interface owners reload their art only when smoothArt changes. New
    // screens also build with these values, so smoothing reaches every screen.
    art::setSmoothArt(s.smoothArt);
    art::setCursorFactor(s.cursorScale);
    // Deblocking IS live: it runs per decoded frame, so this reaches the next clip.
    video::setDeblock(s.videoDeblock);
}

}  // namespace tak
