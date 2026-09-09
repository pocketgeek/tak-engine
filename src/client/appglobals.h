#pragma once

// File-scope globals shared between main.cpp and GameView (client/gameview.h).
// Kept here (as C++17 inline variables -> one definition across TUs) so both the
// app shell and the extracted GameView translation units see the same instance.

// Isometric tilt factor for the world renderer (default 0.72). Read by GameView's
// draw path; written once at startup by the debug `--tilt <f>` CLI flag.
inline float gTilt = 0.72f;
