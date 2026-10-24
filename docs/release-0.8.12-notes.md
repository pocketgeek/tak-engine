# TAK Engine 0.8.12

0.8.12 follows 0.8.11. It brings an **AI profile editor** to the Cartographer,
lets you make an **AI mod without a map**, and has our six AI levels read **one
file, `ai/takengine.txt`**, instead of one file each. Each map now plays its own
AI files, the lobby calls the original game's AI **RETAIL**, campaigns always
allow game speed changes, and an armed flyer's Shift-queued patrol now laps.

**Compatibility first.** The network protocol moves to **293** (replays stay
at format **14**): **0.8.12 does not play with 0.8.11 or any earlier version,
and does not play their replays.** Servers and every player must update
together. Keep 0.8.11 to watch replays it recorded. A game mod or map made for
0.8.11 that carries `ai/d.txt`, `ai/e.txt`, `ai/n.txt`, `ai/h.txt`, `ai/a.txt`
or `ai/ad.txt` no longer changes our AI levels: those files are not read any
more (see [For modders](#for-modders)).

## AI profiles in the Cartographer

- **A window for a map's AI profiles.** **Scenario → AI Profiles...** (**A**)
  edits the files that tell computer players what to build, instead of writing
  them by hand. It has two tabs:
  - **DEFAULT AI**, the profile the Retail AI reads: the map's own profile
    (`ai/<name>.txt`, which the map names) or the standard `ai/default.txt`. Set
    a chance and a limit per unit, and **GROUP RULES** change a whole category of
    units at once (move a rule up or down: order matters).
  - **TAK ENGINE AI**, the one file all six of our levels read
    (`ai/takengine.txt`). **PREVIEW LEVEL** picks which level's built-in values
    and limits in play the table shows.
- **Presets** (**No flyers**, **No ships**, **Clear all**), a **text view** of
  the file itself, and **CHECK**, which reads the files with the game's own code,
  so what the window shows is what the game will do.
- **Save as a game mod.** **SAVE AS GAME MOD...** writes the files into a game
  pack (`mods/game/<pack>/ai/`) so they apply on every map while the pack is
  ticked; **OPEN FROM GAME MOD...** edits a pack's files (the pack list scrolls).
  A file it replaces is backed up outside the pack, and the pack is written
  all at once or not at all. A Retail profile saved into a pack as
  `ai/default.txt` gets a header line naming that file.
- **Maps keep their `ai/` files.** Opening and saving a map, Test Map, crash
  recovery and Undo all keep the map's AI files; in 0.8.11 the Cartographer
  dropped them when it saved a map again. An installed map opened by name keeps
  the files in its own archive.

See [AI profiles](cartographer-guide.md#ai-profiles) in the Cartographer guide
and [Making AI profiles](user-guide.md#making-ai-profiles) in the user guide.

## AI mods without a map

- **File → New AI mod...** (**Ctrl+Shift+N**) and **File → Open AI mod...**
  (**Ctrl+Shift+O**) open the AI Profiles window with a game pack as the only
  document, no map at all.
- **TEST IN GAME...** saves the pack (asking first), lets you pick one of your
  installed maps, and opens the game's private test lobby on it with that pack
  ticked for that run only. Your own pack ticks stay as they were.

See [Making an AI mod without a map](cartographer-guide.md#making-an-ai-mod-without-a-map).

## AI levels and their files

- **One file for our six levels.** Defensive, Easy, Normal, Hard, Absurd and
  Absurdly Defensive all read `ai/takengine.txt` from a ticked game mod (which
  wins) or the map's own archive, never from the install. Each level applies it
  on top of its own built-in profile; with no such file each level plays its
  built-in profile.
- **Honestly:** the six built-in unit tables hold the same values today, so
  `ai/takengine.txt` sets the same weights and limits for every level. The
  levels still differ in how they play and in how they treat a limit: Easy
  halves it, Hard and Absurd raise it, and Defensive and Absurdly Defensive
  ignore limits (except a limit of 0, which stops any level building that
  unit).
- **The Retail AI is unchanged:** it reads the map's own profile, else
  `ai/default.txt` (a ticked game mod, then the map, then the install). It never
  reads `ai/takengine.txt`.
- **Each map plays its own AI files.** When two installed maps carried an AI
  file of the same name, only one of them sent it to the game (and a ticked game
  mod's copy could keep a map's out); now each map's files come from its own
  archive.

See [AI profiles in game mods](user-guide.md#ai-profiles-in-game-mods).

## Lobby

- **AI level 6 is shown as RETAIL** (it was DEFAULT in 0.8.11): the original
  1999 game's own computer player. The user documents call it the **Retail AI**.
  It is separate from **Retail** pathfinding. See
  [Retail AI](user-guide.md#retail-ai).

## Campaigns

- **Game speed can always be changed** in a campaign mission with **+** and
  **−**.
- **The order hint** (for example **ATTACK: CLICK TARGET...**) now sits below
  the objectives panel instead of over it.

## Flyers

- **A Shift-queued patrol of an armed flyer or a flying builder laps.** "Move to
  A, then Shift-patrol B" sent such a flyer to B, where it hovered until its next
  order (a player's replay showed Zhon Drakes stuck like that for about four
  minutes). It now patrols A ↔ B, as ground units and unarmed flyers always did.
  See [Queueing orders with Shift](user-guide.md#queueing-orders-with-shift).

## For modders

- **`ai/takengine.txt` replaces the six level files.** Put it in a game pack's
  `ai` folder (loose or in an archive in the pack) or in a `.kmp` map's own
  `ai` folder. Lines are `weight <unit> <n>` and `limit <unit> <n>`; units the
  file does not name keep each level's built-in values. The old `ai/d.txt`,
  `ai/e.txt`, `ai/n.txt`, `ai/h.txt`, `ai/a.txt` and `ai/ad.txt` are not read
  at all (there is no fallback). The Retail AI still reads `ai/default.txt` or
  the map's own `ai/<name>.txt`.
- **MERGE INTO TAK ENGINE...** In the Cartographer, a map or pack that still has
  old level files shows a notice and this button: it lists them in level order
  (files with the same settings merge as one; when they differ you pick one,
  Normal first), writes the chosen file's settings into `ai/takengine.txt`, and
  can delete the old files. The old files are kept byte for byte otherwise.
- **An AI mod needs no map**: File → New AI mod / Open AI mod, then TEST IN
  GAME. The game's new **`--game-mod <pack>`** option (only together with
  `--play-map`) is what TEST IN GAME uses to tick exactly that pack for one run.
  See [Command line](user-guide.md#command-line).

## For contributors

- Protocol 291: a map package's AI files come from the map's own archive
  (`Vfs::readFromMapArchive`); simulation unchanged. Protocol 292: a
  Shift-queued patrol of an armed or builder flyer closes its loop when the leg
  starts (`World::openFlightPatrol`); the `movement_orders` test (`queuedFlyerPatrols`).
  Protocol 293: our levels read `ai::kEngineProfileFile` (`ai/takengine.txt`)
  over `builtinProfile`; a map package carries it and refuses the old per-level
  names; simulation unchanged. Details in
  [multiplayer-design.md](multiplayer-design.md).
- The Cartographer's AI model is `src/cartographer/aiprofiles.{h,cpp}`
  (SDL-free, verified through `ai::ClassicProfile::execute` and
  `ai::applyProfileText`); the window is `aiprofileswindow.cpp`. New tests:
  `cartographer_aiprofile`, `cartographer_aiprofile_ui`, `_mod_ui`,
  `_extras_ui`, `_packs_ui`, `_merge_ui`, `cartographer_aimod_ui`,
  `cartographer_ai_profile_e2e`; `ai_profile` and `ai_mod_profile` cover
  `ai/takengine.txt`. Design and phases in
  [cartographer-improvements-progress.md](cartographer-improvements-progress.md).
- `aitool` accepts `retail` for level 6 (`default` and `classic` still work).
- The `--mpai` Inner Circle seed 1 hashes are unchanged from 0.8.11: at 60 s
  Retail `b353410003e98dd5`, Legion `0825aeb9e8139203`; at 300 s Retail
  `a3d953803a064191`, Legion `ce5c4ba7562e53b9`; a Retail AI seat
  (`TAK_AI_LEVEL=6`) `14849c00bc6e2a19` at 60 s.
  <!-- TODO-LEAD: confirm against the release validation's --mpai results. -->

## Known limits

All of 0.8.11's known limits carry over:

- **Legion units still stand and wait more than Retail's**, in crowds, at
  corners and in rings, though they usually arrive sooner. This is cosmetic.
- **Legion attackers through a narrow gate deal more damage than Retail's**:
  in the test layouts from about 1.2 to 2.2 times as much.
- **Power codes go through the game simulation like orders**, so their effects
  are part of every player's game (and of replays); the original applied a code
  only on the typer's computer. +DoubleShot and +HalfShot change everyone's
  damage.
- **Fog of war: Full Vision does not switch line of sight off at the start.**
  Your units do not attack what they cannot see until **+LOS** or **+NowISee**
  is typed.
- **UI Font: Classic is close to, but not pixel-identical with, 0.8.7's
  interface.**
- **The campaign book's chapter numeral** still touches the bottom of "APTER"
  in CHAPTER, as in the original game.
- **Legion bridges are still a lottery at about 100 units.**
- **A stall on the server's side is charged to the players**, and a
  reconnecting player who hitches while catching up is charged as any other
  hold.
- A stationary unit choosing its own targets can keep ignoring an enemy it once
  found blocked. The Legion 10,000-unit workloads still struggle with
  congestion and the largest run over the tick budget; in mixed-10000 the
  flyers hover over a slow ground army. Moving the game window between a
  Retina and an ordinary display does not rescale UI Scale. In the remote test
  sweep, Retail `h-cramped` ends early because the idle seat starts with only
  its Monarch.
- **The Retail AI** is not a replay of a 1999 game (this engine's world, orders
  a tick later, its own random numbers), weighs targets against this game's
  unit cap and has not been tested at it, is weaker than Normal (a Taros Retail
  AI can fall far behind), and in campaigns takes its start positions from the
  mission map's start markers (inferred).

New in 0.8.12:

- **Our six levels share identical built-in tables**, so `ai/takengine.txt`
  sets the same unit weights and limits for all of them (they still differ in
  behaviour and limit scaling, above).
- **TEST IN GAME has not yet been tried by hand on Windows or macOS.**

## Compatibility

Protocol **293** (was 290), replay format **14**, generator version **9**,
campaign payload **4** and Crusades SQL schema **9**. Checked in source against
`v0.8.11`: `kNetVersion` in `src/net/protocol.h` moved from 290 to 293 (291 map
package AI files, 292 queued flyer patrols, 293 `ai/takengine.txt`);
`kReplayFormat` in `src/net/replayhdr.h`, the map generator's `formatVer`
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` (`src/net/crusades.h`)
and `kSchemaVersion` (`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** A server accepts only a client whose protocol equals its own,
  so **0.8.12 cannot play with 0.8.11 or any earlier client or server**, in
  either direction. Update the server and every player together.
- **Replays.** Playback requires the recording's protocol to equal the engine's,
  so 0.8.12 does not play replays recorded by 0.8.11 or earlier (they still
  write format 14, but under protocol 290 or lower), and 0.8.11 does not play
  0.8.12's. Keep the version that recorded a replay to watch it. Crusades battle
  replays follow the same rule.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes,
  `mods/` packs, `ModCache/` downloads and 0.8.11 settings files carry over.
  0.8.12 adds no setting. A pack's or map's old `ai/d.txt` ... `ai/ad.txt` are
  ignored; open it in the Cartographer and use **MERGE INTO TAK ENGINE...**.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.8.12-validation.md).
