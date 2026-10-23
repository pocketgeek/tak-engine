#pragma once

// The build profiles of OUR AI levels (Defensive, Easy, Normal, Hard, Absurd,
// Absurdly Defensive): per-unit weight and limit, built into the engine.
//
// These levels never read a game's ai/default.txt. Each has its own built-in table
// (builtinProfile), which a GAME MOD may adjust with a per-level file in the ai/
// folder of its game-mod layer -- loose in the pack folder
// (mods/game/<pack>/ai/n_ai.txt) or at ai/ inside an archive in it:
//
//   ai/d_ai.txt  Defensive          ai/ad_ai.txt  Absurdly Defensive
//   ai/e_ai.txt  Easy               ai/n_ai.txt   Normal
//   ai/h_ai.txt  Hard               ai/a_ai.txt   Absurd
//
// A file of that name anywhere else (a pack's root, another folder) is not read.
// A file uses the line format below and overrides only the entries it names;
// every other unit keeps the built-in values (`limit <unit> 0` keeps the level
// from building that unit). Only the matching level reads its file, and the
// base install, map packages and cosmetic packs can never supply one.
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

// The game-mod file that adjusts a level ("ai/n_ai.txt" for Normal), or nullptr
// for Classic.
const char* levelProfileFile(Difficulty d);

// Apply profile lines to `prof`: each `weight`/`limit` line sets that unit's entry.
void applyProfileText(std::string_view text, Profile& prof);

// A level's effective profile: builtinProfile(d), with the level's file from
// `gameModFiles` (the room's verified game-mod package: Vfs::Files keyed by
// lowercased path, or null for no game mods) applied on top. `source`, when
// given, receives the path of the file applied, or "" for the built-in table.
Profile levelProfile(Difficulty d, const tak::hpi::Vfs::Files* gameModFiles,
                     std::string* source = nullptr);

} // namespace tak::ai
