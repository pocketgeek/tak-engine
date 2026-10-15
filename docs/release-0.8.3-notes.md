# TAK Engine 0.8.3

0.8.3 follows 0.8.2. It adds three match options (Allow Spectators, Map
Scenario and By Slot start locations), numbered start markers on the map
previews, a Blocked cells tool and a game data folder setting in the map
editor, retail self-repair, retail air combat, a stronger and fairer AI, the
2026-10-14 review of Legion movement and three retail path-search fixes. The
simulation changed in both pathfinding modes, so the network protocol moves
from 257 to **266**: **0.8.3 cannot play with 0.8.2 or earlier, and cannot play
their replays.** A server and all of its players must update to 0.8.3 together.
Unlike 0.8.2, Retail pathfinding does **not** play exactly as before: self-repair,
air combat and path search all change Retail games too.

## Match setup

- **Allow Spectators** (multiplayer, protocol 258). A host can now refuse
  watchers. It is on by default, so rooms behave as before until a host turns
  it off. With it off, the game browser shows a red **No watch** button instead
  of **Watch**, and the server refuses every request to watch, including from a
  player who left or whose connection dropped (a dropped player still gets their
  own slot back). The host may change it in the room, by clicking it, until the
  game starts. See [Spectating](user-guide.md#spectating).
- **Map Scenario** (skirmish and multiplayer, protocol 259). A button under the
  map preview plays the map's own scenario (its placed armies, trigger rules and
  Use Only list) instead of a skirmish. A gold **S** in the map list marks maps
  with a playable scenario. The button is greyed, with the reason, for a map
  without one or whose scenario gives player 1 no army. It starts **off**, also
  in multiplayer (the original game ticked *Use map script* automatically), turns
  off again when another map is picked, and is not saved. The lines under it say
  what turning it on changes; while it is on, Start Locations are fixed and
  Monarch Expendable does not apply. In the room, every seat the scenario needs
  is tagged **NEEDED BY SCENARIO** and START waits until they are filled; a start
  the server refuses now shows its reason (**CANNOT START: …**). See
  [Scenario games](user-guide.md#scenario-games).
- **Start Locations: By Slot** (protocol 260). A third choice beside Fixed and
  Random: slot N always starts at the map's start position N, whichever slots are
  empty. Fixed is unchanged and still the default, and still follows the
  original game, which gives the k-th occupied slot the k-th position (a player
  in slot 1 and an AI in slot 8 start at positions 1 and 2; checked by
  emulating the original's skirmish and multiplayer start routines). See
  [Match options](user-guide.md#match-options).
- **Start markers on the map preview.** The create screen's preview numbers
  every start position, on shipped, downloaded, Cartographer and generated maps
  (the generator's white dots are replaced). The lobby now shows the map too, at
  the top of the right-hand column, and colours each marker for the player who
  will start there, following the slots, colours and start mode as they change.
  Random start locations, a benchmark and a scenario that places its own armies
  keep the markers neutral.

## Game

- **Units repair themselves at the original game's rate** (protocol 261). A
  damaged, finished unit or building now regains `maxdamage × healtime /
  buildtime` hit points a second, as the original does, instead of one hit
  point per `healtime` seconds. Most mobile units return to full health in about
  four minutes and many buildings in about eight. Under the old rule Aramon
  buildings repaired themselves far too slowly (Crusades Keep 0.32 HP/s against
  the original's 49.08, Barracks 0.80 against 35.73); unfinished units no longer
  repair themselves. The port was checked against the original's own repair
  routine, run in emulation: 4,008 units and 37,560 calls gave the same hit
  points and remainders.
- **Air combat follows the original game** (protocols 262 and 263).
  - A flyer the game holds still (waiting, ambushing, guarding, or a flying
    builder at its work) now has no speed, so shooters stop aiming where it
    would have been had it kept flying. In the flyer combat test a Lokken's Fire
    Ball had missed a Gryphon held in range 47 times out of 47; it now connects
    11 or 12 times of 12.
  - Flyers without hover attack (the Gryphon, Dirigible and Fallen Angel among
    them) fly **attack runs** over ground targets as in the original, instead
    of hovering in range. Their shots land along the flight path, so some
    miss, and they are harder to hit while they fly.
  - **Area damage is three-dimensional.** A blast hurts every unit by its
    distance from the blast point to the unit's box, height included, and never
    the unit that fired it; a weapon with an area of effect of 16 or less that
    strikes a unit hurts only that unit. A Catapult shell bursting on the ground
    under a Gryphon 150 pixels up no longer damages it (1,063 before, 0 now).
    Most melee weapons have an area of effect, so melee hits nearby units too,
    allies included, as in the original.
  - A weapon now faces the point it leads its target to, not the target's
    centre, and hover attackers circle and head for their target's aim point.
- **Death cries are the game's own choice.** Every unit's script draws from its
  own random stream, so the original's rare and alternative death sounds play
  at their authored odds (before, the first draw was the same for every unit).
  The cry is chosen by the simulation, so every player, spectator and replay
  hears the same one.
- **Spectators see every side's health bars,** as well as build-progress bars
  and self-destruct countdowns; before, they saw only those of the first
  player's side.

## Computer players

- **Teams share mana deposits fairly.** Every difficulty reserves the deposit
  that one of its own or an ally's builders is walking to (the reservation now
  uses the building site, not the builder's approach point, so it works), and
  leaves the free deposits around an ally's start to that ally while the ally is
  claiming them. Once a deposit has gone unclaimed for two minutes, only the
  other ally whose start is nearest may take it. A builder that gets no nearer to
  its deposit for a minute gives the reservation up, and its own AI sends it to
  other work. Over 13 test games on *4x4 team*, reservations held for three
  minutes or more on an ally's home deposit fell from 101 to 2.
- **Short of mana, Normal, Easy and Defensive build a lodestone,** as the
  original's AI does: when the store is under 30% full and spending outruns
  income, on a free deposit within the original's base radius (560 pixels for
  the Monarch, 800 for other builders). In eight-Normal games on *4x4 team*
  (seed 1, 900 s), every AI used to stay on one lodestone all game; with this
  change four of the eight ended with two to four.
- **More builders where the original allows them.** Normal grows from its
  Monarch and one builder to the Monarch and up to four as its income rises.
  Every builder counts against the cap, priests included, and no builder type
  passes its limit in the game's `ai/default.txt`. Zhon, which has no factory
  buildings, counts its Beast Handlers, Tamers, Lords and Shamans as producers
  and builders, so it no longer stalls on one deposit; on Hard, Absurd and
  Defensive they take the same production raise as other sides' factories.
- **Idle builders build income beside a full store.** A Normal or Defensive AI
  whose store is 90% full keeps building economy instead of leaving its builders
  idle.

## Legion

The six items of the 2026-10-14 review of Legion movement (protocol 264,
Legion only):

- **Long routes are walked.** Route values no longer run out about 13,100 steps
  from the destination, where a unit used to stand as if trapped.
- **A unit's progress is measured on one scale at a time,** so its "stalled"
  clock no longer runs while it walks freely.
- **Mixed sizes settle.** A small unit among settled large ones of its own order
  no longer waits for ever.
- **Slots by arrival.** Through a narrow entrance into the destination, the
  units that come through first take the deepest places, so they no longer
  settle in the doorway and shut the rest out.
- **Passage lanes.** A formation keeps its side-to-side order through a short
  passage instead of crossing over inside it.
- **One slot-search budget a tick,** shared round robin by every destination,
  so a big click no longer causes a long tick.

Over the 131 Legion scenario fixtures that existed before the review (gate
start offsets, summed), arrivals rose from 11,151 to 11,304, units left behind
fell from 5,117 to 4,971, orders never completed from 336 to 326 and lane swaps
from 571 to 432 (per file in the gap fixtures, 2,723 to 596 hundredths), for
0.68% less Legion work. Some fixtures lost ground; each is listed in
[the Legion notes](legion-pathfinding.md#the-2026-10-14-review-protocol-264)
and was accepted.

## Path search (all modes)

Three fixes make the engine's path search follow the original more closely
(protocols 265 and 266), each checked by emulating the original's routines:

- Each player's path requests are admitted from an entity pool scanned slot by
  slot, holes included, as the original's scheduler does. The pool starts at the
  original's unit limit for the unit cap (the cap itself from 20 to 500, 500 for
  the 1,000 and 2,000 caps) and grows only when it is full, so a default game
  scans 500 slots a player, not 2,000.
- Where two units' footprints overlap, the cell stays with the unit that held it
  rather than going to whichever unit happened to be processed last.
- A unit that asks again for a path it is still waiting for keeps its place in
  the queue, however far it has moved.

## Map editor (Cartographer)

- **Blocked cells.** A new **BLOCKED** tool paints and erases the map's hard
  blockers with 1- to 16-cell brushes, strokes and rectangles; **View → Show
  blocked cells** (**B**) shows blocked cells in red and roads in blue. Blockers
  stop units in Retail and Legion. The original editor had no such tool. See
  [Blocked cells](cartographer-guide.md#blocked-cells).
- **Game data folder.** The game's **Options** and Cartographer's **View →
  Preferences** both gain a **Game data folder** row to change the install
  folder after the first run; the two share the setting, and **RESTART NOW**
  applies it (Cartographer asks to save first and reopens the map). See
  [Game data folder](user-guide.md#game-data-folder).
- **Preferences** also gains borderless fullscreen (**F11**), edge scrolling
  (off by default) and UI scale. Every browser list has a scrollbar.
- **Fixes.** Choosing STARTS no longer crashes. A new blank Veruna or Taros map
  is no longer paved with road. A ninth start position is refused (a match seats
  eight). A feature selection that lost a member can be dragged again. Features
  the world does not define are shown as red markers, and the Units browser
  lists only real unit types. **Open failed** names the missing map.
- A new [Cartographer user guide](cartographer-guide.md) explains every tool,
  with 27 pictures and a first-map walkthrough.

## For contributors

- **Tests no longer touch the player's profile.** On 2026-10-14 a `ctest -L
  quick` run in a Debug build rewrote the real `settings.ini`. Every test now runs
  with its own scratch `XDG_DATA_HOME`, `XDG_CONFIG_HOME` and `HOME`, a fixture
  fails any test that writes outside them, and `tak::prefPath` replaces
  `SDL_GetPrefPath` (a lint test enforces it). Shipped Release programs are
  unchanged.
- New tests include `cartographer_placement_test` (every browser item placed,
  undone, saved and reopened through the editor's real input path, plus a UI
  smoke test), `server_spectators_test.py`, `server_map_scenario_test.py`,
  `scenario_info_test`, `start_positions_test`, `start_markers_test`,
  `spectator_health_bars`, `creon_death_sound`, `flyer_combat_test`'s air cases
  and the Legion world tests `legion_longroute`, `legion_progressclock`,
  `legion_mixedsettle`, `legion_farroom`, `legion_passagelanes`,
  `legion_handout` and `legion_entryplanes`. The scenario baseline was retaken
  for protocols 263 to 266; by the user's ruling every key that moved was
  accepted (`legion_check check --cumulative` passes, 31,753 keys).
- The `--mpai` Inner Circle seed 1 hashes are now: Retail 300 s
  `7535cb08ee787b75`, 60 s `2c39430636a3dbda`; Legion 300 s `c707e1a8063bc0c7`,
  60 s `532539f51d70840b`.

## Compatibility

Protocol **266** (0.8.2: 257), replay format **13** (0.8.2: 12), generator
version **8**, campaign payload **4** and Crusades SQL schema **9**. Checked in
source against `v0.8.2`: `kNetVersion` in `src/net/protocol.h`, `kReplayFormat`
in `src/net/replayhdr.h` (format 13 records the Map Scenario option), the map
generator's `formatVer` (`src/tnt/mapgen.h`, no change under `src/tnt/`), the
campaign payload's `kVersion` (`src/net/crusades.h`, unchanged) and
`kSchemaVersion` (`src/server/crusades/store.cpp`, unchanged).

- **Multiplayer.** A server rejects a client whose protocol differs from its
  own, so **0.8.3 clients cannot join 0.8.2 or older servers, and 0.8.2 or
  older clients cannot join a 0.8.3 server**. Update the server and every
  player together.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.8.3 does not play replays recorded by 0.8.2 or earlier, and
  they do not play 0.8.3 replays. Keep an old copy of the engine to watch old
  recordings. Crusades battle replays follow the same rule.
- **Maps and settings.** Generated-map recipes and saved `.kmp` maps carry over.
  A 0.8.2 settings file reads the same in 0.8.3. New keys:
  `gameCreate.allowSpectators`, and `gameCreate.randomStarts` now takes 2 for
  By Slot (0 and 1 keep their meaning). A By Slot choice saved by 0.8.3 is read
  as Fixed by 0.8.2.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.8.3-validation.md).
