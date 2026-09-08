#pragma once

// A release build of takclient honours NO environment variables and no command-line
// options beyond --data and --version: the game is configured through the menu and the
// Options screen, never through hidden switches. Every TAK_* dev/test/diagnostic hook
// in the viewer reads its environment through devEnv(), which compiles to a constant
// nullptr in a release build (NDEBUG) -- so those hooks simply vanish from the shipped
// binary while staying available in debug builds. Use this instead of std::getenv.

#include <cstdlib>

namespace tak {

inline const char* devEnv(const char* name) {
#ifdef NDEBUG
    (void)name;
    return nullptr;
#else
    return std::getenv(name);
#endif
}

}  // namespace tak
