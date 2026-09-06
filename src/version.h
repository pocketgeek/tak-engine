#pragma once

// Single source of truth for the engine's RELEASE version. The value is injected
// by CMake -- project(tak-engine VERSION x.y.z) sets PROJECT_VERSION, which the
// build passes down as the TAK_VERSION macro. The fallback keeps non-CMake/tool
// builds compiling.
//
// This is the human-facing release version. It is deliberately SEPARATE from
// net/protocol.h's kNetVersion, which gates multiplayer WIRE compatibility --
// a release bump must never force a lockstep-incompatible protocol change.
#ifndef TAK_VERSION
#define TAK_VERSION "0.0.0-dev"
#endif

namespace tak {
inline constexpr const char* kVersion = TAK_VERSION;
}
