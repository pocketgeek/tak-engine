#pragma once

// The build profiles of OUR AI levels (Defensive, Easy, Normal, Hard, Absurd,
// Absurdly Defensive): per-unit weight and limit, built into the engine.
//
// These levels never read a game's ai/default.txt. Each has its own built-in table
// (builtinProfile), which a GAME MOD or the room's MAP PACKAGE may adjust with a
// per-level file in its ai/ folder -- loose in a game-mod pack folder
// (mods/game/<pack>/ai/n.txt), at ai/ inside an archive in a game mod, or at ai/
// in the map's .kmp (it travels in the room's map package, net/mappackage.h):
//
//   ai/d.txt  Defensive          ai/ad.txt  Absurdly Defensive
//   ai/e.txt  Easy               ai/n.txt   Normal
//   ai/h.txt  Hard               ai/a.txt   Absurd
//
// A game mod's file wins over the map's (the map's is read only when no game mod
// supplies that level's file). A file of that name anywhere else (a pack's root,
// another folder) is not read, and the base install and cosmetic packs can never
// supply one. A file uses the line format below and overrides only the entries it
// names; every other unit keeps the built-in values (`limit <unit> 0` keeps the
// level from building that unit). Only the matching level reads its file.
// Classic (retail's own AI, shown to players as DEFAULT) is not built in: it reads
// the game's ai/default.txt (ai/classicprofile.h).
//
// Line format (the one loadProfile has always read): `weight <unit> <n>` or
// `limit <unit> <n>`, whitespace-separated, unit names case-insensitive; a line
// starting with '/' and anything that is not one of the two keywords is skipped,
// and a later line for the same unit and keyword wins.
//
// Server-side only (an AI input, never part of the gameplay-data hash).

#include <string_view>

#include "ai/ai.h"
#include "hpi/hpi.h"

namespace tak::ai {

// A level's built-in profile; Classic (and anything out of range) has an empty one.
const Profile& builtinProfile(Difficulty d);

// The file that adjusts a level ("ai/n.txt" for Normal), or nullptr for Classic.
const char* levelProfileFile(Difficulty d);

// Apply profile lines to `prof`: each `weight`/`limit` line sets that unit's entry.
void applyProfileText(std::string_view text, Profile& prof);

// A level's effective profile: builtinProfile(d), with the level's file applied on
// top -- from `gameModFiles` (the room's verified game-mod package) if it has one,
// else from `mapFiles` (the room's verified map package). Both are Vfs::Files keyed
// by lowercased path, or null for none. `source`, when given, receives the path of
// the file applied ("" for the built-in table) and `fromMap` whether it was the
// map package's.
Profile levelProfile(Difficulty d, const tak::hpi::Vfs::Files* gameModFiles,
                     const tak::hpi::Vfs::Files* mapFiles = nullptr, std::string* source = nullptr,
                     bool* fromMap = nullptr);

} // namespace tak::ai
