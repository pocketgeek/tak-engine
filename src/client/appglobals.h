#pragma once

// File-scope globals shared between main.cpp and GameView (client/gameview.h).
// Kept here (as C++17 inline variables -> one definition across TUs) so both the
// app shell and the extracted GameView translation units see the same instance.

#include <string>

// Isometric tilt factor for the world renderer (default 0.72). Read by GameView's
// draw path; written once at startup by the debug `--tilt <f>` CLI flag.
inline float gTilt = 0.72f;

// The resolved retail install folder. The VFS covers everything inside the .hpi
// archives, but the movies are LOOSE files under <install>/Movies -- so anything
// that plays one (the front end, the loading screen) needs the path itself.
inline std::string gInstallRoot;
