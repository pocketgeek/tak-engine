# TAK Engine 0.8.11

0.8.11 follows 0.8.10. It adds **Default**, a seventh computer-player level: the
DEFAULT AI, the 1999 game's own AI, driven by `ai/default.txt`, for skirmish, multiplayer and
campaign opponents. Long build menus now wrap into extra rows on small
screens, and the blue production bar over factories is gone. A DEFAULT AI seat is
a new value on the wire, so the network protocol moves to **290** (replays stay
at format **14**): **0.8.11 does not play with 0.8.10 or any earlier version,
and does not play their replays.** Servers and every player must update
together. The existing AI levels and the game itself play exactly as in 0.8.10,
but campaign missions now always play **DEFAULT AI** opponents.

## The DEFAULT AI

- **A new AI level, DEFAULT.** In a skirmish or multiplayer room the host can
  now set an AI seat to **DEFAULT**, after Absurdly Defensive (a new AI seat
  still starts on Normal). It is named for `ai/default.txt`, the original
  profile that drives it, and is a port of
  the original game's computer player, routine by routine: what it builds,
  where it puts it, which units guard its bases, when a wave leaves and what it
  marches on all follow the 1999 game. Each ported routine was checked against
  the original program by running both on the same inputs (see the
  [acceptance ledger](classic-ai-acceptance.md)).
- **It reads the map's AI profile**, as the original did: the file the map
  names (`ai/default.txt` on every shipped skirmish map), falling back to
  `ai/default.txt`. Its weights and limits switch units on or off and cap how
  many of each it may have; the AI picks at random among what its builders can
  make, favouring what it is short of.
- **It plays like the original, quirks included.** It builds and expands to
  new bases on sacred sites, defends its bases with patrols, and sends attack
  waves. Its waves simply march on their target. It always knows where your
  units are, even out of its sight, and prefers your lodestones and mana
  buildings as targets. Its Monarch builds only while its base is short of
  builders and runs for a safe spot when badly hurt instead of fighting back.
- **It has no difficulty setting and no income bonus**, and it is **weaker than
  Normal**: in our tests it loses to a Normal AI within 8 to 18 game-minutes.
  It builds a few lodestones at a time and often runs short of mana. A **Taros**
  DEFAULT AI often grows slowly, because its Monarch keeps choosing expensive
  Caged Demons.
- **The disconnect takeover is still Normal.** When a dropped player's time runs
  out, a Normal AI takes the seat, as in 0.8.10.

See [Default](user-guide.md#default) in the user guide. Developer documents
and the source call this level **Classic** (`ai::Difficulty::Classic`,
`classic-ai-plan.md`); only its player-visible name is DEFAULT.

## AI profiles

- **Our AI levels carry their own build profiles.** Defensive, Easy, Normal,
  Hard, Absurd and Absurdly Defensive no longer read the game's
  `ai/default.txt`: each has a built-in weight and limit per unit, today the
  values of the original default profile they were tuned on, so they play
  exactly as before.
- **Game mods and maps can change them, one level at a time.** A game pack with
  `ai/d.txt`, `ai/e.txt`, `ai/n.txt`, `ai/h.txt`, `ai/a.txt` or `ai/ad.txt` (in
  the pack's `ai` folder, loose or inside an archive in the pack) adjusts that
  level only (`ai/n.txt`: Normal); units the file does not name keep the
  built-in values, and `limit <unit> 0` stops the level building one. A map's
  own archive in `Maps/` (a `.kmp` map, for example) may carry the same files in
  its `ai` folder; when a game pack has the
  same file, the game pack's wins. A file of that name directly in the pack
  folder is not read. The files travel with the host's game packs, or with the
  map, in multiplayer; cosmetic packs and the install cannot supply them. See
  [AI profiles in game mods](user-guide.md#ai-profiles-in-game-mods).

## Campaigns

- **Campaign opponents are always the DEFAULT AI**, the original game's own AI. There
  is no choice of level: missions no longer play the Normal AI they had in
  0.8.10.
- **DEFAULT AI opponents play a mission as the 1999 game did**: each uses the AI
  profile the mission names (30 of the shipped missions name their own, the
  rest fall back to `ai/default.txt`) and the mission's timed mass attack, when
  every armed mobile unit marches on you.

See [Campaigns](user-guide.md#campaigns).

## DEFAULT AI profile files

- **The DEFAULT AI reads the room's own profile file.** The file the map names
  (`ai/<name>.txt`, else `ai/default.txt`) comes from a ticked game pack that
  carries one, else from the map's own archive in `Maps/` (the file travels in
  the map's package, so a server without that map gets it too), else from the
  install: a game pack's file wins over the map's. Profiles in other maps'
  archives never apply.

## Interface

- **Long build menus wrap into rows.** A builder whose build menu does not fit
  beside the command panel (a long menu, a small window, a large UI Scale or
  Build Menu Scale) used to draw one row of icons under the command panel and
  over the mana orb. The icons now wrap into as few extra rows as the menu
  needs, stacked upward in menu order and balanced (13 icons make rows of 7
  and 6). Every row follows the **Build Menu** alignment. The icons keep their
  size; only a Build Menu Scale too big for the window is lowered, just enough
  to fit. A menu that fits one row looks as before, except that a right-aligned
  row now ends at the command panel instead of under it.
- **The tooltip and TRAINING line stay clear of the panel.** The hover tooltip
  of the last icons and the TRAINING line no longer hang over the mana orb, and
  the chat box rises above the extra rows.
- **No blue production bar.** A factory or a mobile conjurer producing a unit
  drew a blue progress bar over itself as well as the new unit's health bar,
  showing the same progress twice. The original game never drew a bar over the
  producer, so it is gone: the unit being made shows its own health bar (under
  the **Health Bars** option), and the information panel shows the progress.

See [Building](user-guide.md#building).

## For contributors

- The Classic level (shown to players as **DEFAULT**: the lobby's level name,
  the user documents and the server log's `DEFAULT AI profile` line; `aitool`
  takes `default`, and still `classic`) lives in `src/ai/classic*.{h,cpp}`
  (`ai::ClassicBrain`, one per Classic seat, run by the server). The design and phases are in
  [classic-ai-plan.md](classic-ai-plan.md); every ported routine, its emulation
  oracle and the planted bug it was shown to catch are in
  [classic-ai-acceptance.md](classic-ai-acceptance.md). The oracles are opt-in:
  configure with `-DTAK_CLASSIC_ORACLES=ON` (and `TAK_CLASSIC_PRINTWEIGHTS` for
  the PrintWeights check). New game tests: `classic_ai`, `classic_ai_data`,
  `classic_ai_bases`, `classic_ai_raid`, `classic_ai_u13`, `classic_ai_waves`,
  `classic_ai_duels`, `classic_ai_expansion`.
- Protocol 290: AI level 6 (`SlotInfo.aiLevel`), which a 289 peer would have
  clamped to Normal. `CreateGame` is unchanged: mission rooms always play
  Classic opponents. A Classic seat gets exactly a Normal seat's
  match config, so the simulation is 289's. `replay_test` refuses protocol-289
  replays.
- `tools/check-detmath.sh` now also guards the Classic brain's sources.
- Our levels' tables are `src/ai/builtinprofile.{h,cpp}` (`ai::builtinProfile`,
  `ai::levelProfile`); the server builds each room's per-level profiles from
  them, the room's game-mod package and then its map package. New tests:
  `ai_profile`, `ai_profile_data` (the built-in Normal table still equals the
  install's `ai/default.txt`; a map package carries `ai/d.txt` ... `ai/ad.txt`;
  game mod over map) and `ai_mod_profile` (a game pack's `ai/n.txt`, through the
  real mod transfer, or a `.kmp` map's, through the map package, reaches the
  server's Normal AI, and only it; the game pack's wins; misplaced `n.txt` files
  are not read). The headless harness takes `TAK_AI_LEVELS`
  (`1,2`: one level per seated AI) for mixed-level games.
- Retail AI comparison harness under `tools/re` (`retail_ai_harness.py`,
  `retail_ai_poll.py`, `tak_ai_run.py`, `compare_ai.py`) and a debug-only
  `TAK_ROSTER_DUMP` on `takserver`; optional developer tooling, not a gate.
  Finding: retail's `-AutoStop` reseeds both RNGs every tick (see
  `docs/retail-engine.md`).
- New tests: `buildrow_test` (icon count x window x UI scale x build scale x
  alignment); `build_controls` narrows the window until a menu wraps, then
  hovers and clicks a bottom-row icon.
- The server looks up the Classic profile file in the room's layers: game
  mods, else the map package, else the base data (`ai/` files only). A map package may carry the map's own
  `ai/<aiprofile>.txt` / `ai/default.txt` when the map's archive does
  (`maps::build`, checked on decode); new tests `classic_ai_map_profile` (a game
  pack's and a map archive's `ai/default.txt` reach the server's Classic AI, the
  game pack's winning when both have one; a
  mission plays Classic with no settings) and more `ai_profile_data` cases.
- A campaign mission's opponents are Classic: `takmission08_dh` at 300 s gives
  `5c35c83f41cbae5f`.
- The `--mpai` Inner Circle seed 1 hashes are unchanged from 0.8.10: at 60 s
  Retail `b353410003e98dd5`, Legion `0825aeb9e8139203`; at 300 s Retail
  `a3d953803a064191`, Legion `ce5c4ba7562e53b9`. A Classic seat
  (`TAK_AI_LEVEL=6`) gives `14849c00bc6e2a19` at 60 s, repeatably.

## Known limits

All of 0.8.10's known limits carry over:

- **Legion units still stand and wait more than Retail's**, in crowds, at
  corners and in rings, though they usually arrive sooner. This is cosmetic.
- **Legion attackers through a narrow gate deal more damage than Retail's**:
  in the test layouts from about 1.2 to 2.2 times as much, with more units in
  reach.
- **Power codes go through the game simulation like orders**, so their effects
  are part of every player's game (and of replays). The original applied a
  code only on the computer of the player who typed it. The codes that act on
  the typer (+ATM, +ManaMe, +LOS, +Mapping, +NowISee) still act only for that
  player; +DoubleShot and +HalfShot change everyone's damage.
- **Fog of war: Full Vision does not switch line of sight off at the start.**
  Your units do not attack what they cannot see until **+LOS** or **+NowISee**
  is typed.
- **UI Font: Classic is close to, but not pixel-identical with, 0.8.7's
  interface.**
- **The campaign book's chapter numeral** still touches the bottom of
  "APTER" in CHAPTER, in both UI fonts, as in the original game (its lettering
  boxes overlap).
- **Legion bridges are still a lottery at about 100 units**: two armies meeting
  at a narrow, tapering bridge can jam in its mouth and take far longer than
  Retail to cross.
- **A stall on the server's side is charged to the players**, and a
  reconnecting player who hitches while catching up is charged as any other
  hold (the disconnect wait).
- A stationary unit choosing its own targets can keep ignoring an enemy it once
  found blocked. The Legion 10,000-unit workloads still struggle with
  congestion and the largest run over the tick budget; in mixed-10000 the
  flyers hover over a slow ground army. Moving the game window between a
  Retina and an ordinary display does not rescale UI Scale. In the remote test
  sweep, Retail `h-cramped` ends early because the idle seat starts with only
  its Monarch.

New with the DEFAULT AI:

- **A DEFAULT AI game is not a replay of a 1999 game.** Its decisions follow the
  original's rules, but the world they act in is this engine's (economy, build
  times, sight, pathfinding and combat), its orders take effect a tick later,
  and it draws its own random numbers. So games go differently from the
  original's even where every decision rule matches.
- **It weighs targets against this game's unit cap** (2,000 per player by
  default), not the original's smaller one, and it has not been tested in games
  that reach the unit cap.
- **Weak and slow-growing**, as described above; a Taros DEFAULT AI in
  particular can fall far behind.
- **In campaigns, its start positions come from the mission map's start
  markers**, as on a skirmish map; this is inferred, not read from the original
  mission loader.

## Compatibility

Protocol **290** (was 289), replay format **14**, generator version **9**,
campaign payload **4** and Crusades SQL schema **9**. Checked in source against
`v0.8.10`: `kNetVersion` in `src/net/protocol.h` moved from 289 to 290;
`kReplayFormat` in `src/net/replayhdr.h`, the map generator's `formatVer`
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` (`src/net/crusades.h`)
and `kSchemaVersion` (`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** A server accepts only a client whose protocol equals its own,
  so **0.8.11 cannot play with 0.8.10 or any earlier client or server**, in
  either direction. Update the server and every player together.
- **Replays.** Playback requires the recording's protocol to equal the engine's,
  so 0.8.11 does not play replays recorded by 0.8.10 or earlier (they still
  write format 14, but under protocol 289 or lower), and 0.8.10 does not play
  0.8.11's. Keep the version that recorded a replay to watch it. Crusades battle
  replays follow the same rule.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes,
  `mods/` packs, `ModCache/` downloads and 0.8.10 settings files carry over
  unchanged. 0.8.11 adds no setting.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.8.11-validation.md).
