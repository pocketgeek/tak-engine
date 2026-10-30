# TAK Engine 0.9.12

0.9.12 follows 0.9.11. The fog of war now looks and works as in the original
game: units see by the same sight the game plays by, ground seen before lies
under your side's rolling mist, and radar marks enemies on the minimap only.
Our AI levels attack only what their side has seen and scout the map for the
rest. Big late games keep their speed: in the reported game, eight Absurd AIs
on a 64x64 random map, 0.9.11 fell to -2 at about 2,700 units, and 0.9.12 held
+9 to +10 to 3,160. Streaming keeps to YouTube's bitrate, Options, Voice Chat
and Controls save as you change them, and SETTINGS, BENCHMARK and LOAD REPLAY
have a BACK button.

**Compatibility first.** The AI's sight-only targets and the new lockstep
state check change the game state the players compare, so 0.9.12 uses network
protocol **305** (replay format **15**, unchanged). **0.9.12 does not play
with 0.9.11 or any earlier version, and does not play their replays.**

- **Everyone updates together.** A 0.9.12 player cannot join a 0.9.11 server,
  and a 0.9.11 player cannot join a 0.9.12 server: the connection is refused
  with a *protocol version mismatch* message. Update your game and tell the
  people you play with to update theirs.
- **Old replays:** a replay recorded by 0.9.11 or earlier is refused with the
  version message. To keep watching them, keep your 0.9.11 install (0.9.0 to
  0.9.11 stay on the GitHub releases page); versions before 0.9.0 are no
  longer offered for download and can be built from their git tags (see the
  [development guide](development.md)).
- **Players:** nothing to do. Settings, your data folder choice, key bindings,
  saved `.kmp` maps, generated-map recipes and mod packs carry over. A
  streaming bitrate saved above YouTube's figure for your stream is lowered to
  it, with a note (below).
- **Server operators:** update `takserver` to 0.9.12 when you announce the
  release; until you do, 0.9.12 players cannot use your server. A server whose
  game has no human player left (AIs watched by spectators) now skips the
  lockstep check nobody compares; nothing else about running a server changed
  (see [public-server.md](public-server.md#versions)).

## Fog of war as in the original game

Through 0.9.11 the fog you saw was the engine's own: a separate display pass
that ray-marched sight lines on 16-pixel squares, let walls cast fog shadows,
and cleared fog wherever radar reached. The game itself, and the AI, already
played by the original's sight (one sight plane per player, on 32-pixel
squares), so what you saw and what the game counted as seen could differ.

The original keeps **one sight plane per player** that both its game logic and
its screen read (`KINGDOMS.icd`: the view counts at `0x4c6800`, LosOn at
`0x4f7210`, the minimap at `0x4f93a0` / `0x4f9970`, the Glide fog at
`0x497a40` -> `0x498270`; read and compared, the engine copies none of the
original's code or data). 0.9.12 shows exactly that plane:

- **Three kinds of ground.** Black where your side has never looked; mist
  where it has seen the ground but does not see it now; clear where your or
  your allies' units see now. The mist is tinted with your side's `FogColor`
  (`gamedata/sidedata.tdf`: white for Aramon, brown for Taros, pale blue for
  Veruna and Creon, warm white for Zhon), and it rolls and drifts slowly, as
  the original's RollingFog and WavyFog do.
- **Sight is the game's sight.** On flat ground a unit sees about its sight
  distance; from high ground it sees further over lower ground (up to twice
  as far), and looking uphill less. Its edge follows the 32-pixel squares,
  not a smooth circle.
- **Seen units.** Another player's unit is drawn, can be clicked, and gets a
  health bar, a Tactical Dot or a Hidden Units dot only when one corner of its
  body is on clear ground, as in the original, so a large unit shows a little
  before its centre is in sight.
- **Radar marks the minimap only.** An enemy within radar range of one of your
  units is a dot on the minimap; radar no longer clears the mist or shows the
  unit in the main view. The minimap shows the same plane, square by square.
- **Less work.** The separate display pass and its worker thread are gone; the
  overlay is rebuilt only where a unit's sight changed. The fog work measured
  the fog's cost at about 1.22 ms a frame before and 0.37 after at 3,000 units.

The user guide has a new **Fog of war** section, and its briefing and
disconnect pictures were retaken for the new fog.

## Our AI levels attack only what they have seen (beyond the original game)

In 0.9.11 our six levels (Defensive to Absurdly Defensive) picked strike
targets from every enemy unit, seen or not, as the original's AI does. By the
user's decision they now know only what a player in their seat would, and
explore the map like a human when nothing they know is worth attacking. The
**Retail AI** (level 6) is unchanged and still weighs unseen units as the
original does.

- **What they know.** Enemy units their own and their allies' units see
  (radar included), units shooting at theirs, and buildings remembered where
  they were seen until that spot is in sight again without them; a moving
  unit is remembered for about 25 seconds. Only a target in sight is attacked
  by name; a remembered or only-heard one gets an attack-move to where it was.
- **The room's rules, as a player has them.** With **Not Explored** they know
  only the ground their side has explored, with **Explored** the whole map's
  ground and deposits but no enemy outside their sight, and with **Full
  Vision** every unit. With **Fixed** or **By Slot** start locations they know
  whose start is whose; with **Random** only the map's start spots.
- **Lodestones on known ground.** They build lodestones only on deposits they
  know, and judge a deposit taken by what they have seen or remember there.
  When the game refuses a build because an enemy lodestone they cannot see
  holds the deposit, they learn it is there (as a player sees the red
  placement ghost) and go elsewhere; a builder that sees its deposit taken on
  the way is re-sited.
- **Scouting.** With nothing known they can reach and hurt, armies, raids and
  scouting parties explore: unchecked enemy starts first, then map sectors
  weighed by time out of sight, distance and deposits. Absurd scouts from the
  start every 20 s, Hard every 40 s, Normal every 60 s, Easy from minute 4
  every 150 s, the defensive levels only while far ahead of what they know.
  A scout killed on its trip backs that target off (2 minutes, doubling),
  later scouts avoid where scouts fell, and a target where scouts died gets a
  larger party. A fleet sails on its own when the army's target is inland.
- **Strength from what they have seen.** An enemy whose home they have not
  looked at in five minutes counts as at least as strong as their own army,
  so a Defensive AI that has seen nothing never thinks itself far ahead.
- **Beaten bases are looked in on again.** A beaten player's base, an enemy's
  or an ally's, goes back on the scouting list once it has been out of sight
  five minutes, in case someone has moved in; a base where an enemy building
  is known is attacked instead.

Measured in all-AI games (Inner Circle unless named, Explored, game minutes):
0.9.11; the sight-only targets alone (`894d23d4`); and the 0.9.12 knowledge
and scouting (`4b374c14`, before the beaten-base rule). Setups in the
[validation report](release-0.9.12-validation.md).

| Game | 0.9.11 | Sight only | 0.9.12 |
| --- | --- | --- | --- |
| Absurd against Absurd, seeds 1-6, mean length | 27.2 | 21.2 | 17.7 |
| ... scouts lost in the six games | | 320 | 112 |
| Hard against Normal, seeds 1-6, mean length | | 14.0 | 13.0 |
| Easy against Easy, seeds 1-6, mean length | | 12.5 | 15.0 |
| Defensive against Normal, seeds 1-3 | | 2 of 3 end (12.6, 9.5) | 2 of 3 end (15.1, 19.3) |
| 8 Absurd on the reported 64x64 map, seeds 1-3 (60-minute limit) | | 1 of 3 ends (56.8) | 2 of 3 end (31.5, 27.6) |
| ... scouts lost in the three games | | 1,006 | 394 |
| Absurd against Absurd, **Not Explored**, seeds 1-3 | | | 21.6 (Explored: 13.0) |

## Big late games keep their speed

Reported: eight Absurd AIs on a 64x64 random map at +10 slowed to below +0 late
in the game. Four costs grew with the map or the unit count, in the server's
and every client's simulation alike:

- **The AI's lodestone search.** An Absurd builder's search radius is the whole
  map, and every deposit in it (2,860 on that map) got the full placement
  test: 300-840 ms in one tick. Deposits are now tried nearest first and the
  first usable one wins, which is the same choice (all 41,359 command bundles
  of a 23-minute Absurd game are byte-identical).
- **Possible map splits.** A new building whose edge touched separate open
  areas threw away the whole map's connectivity labels (90-150 ms each on a
  2,048 x 2,048 grid, 16 to 24 times a minute late in the game); in the
  reported replay 164 of 200 were no split at all. The pieces are now flooded
  locally in turns, so the work is the smaller side's, with the old full
  relabel only past an eighth of the grid. Answers are unchanged.
- **Sacred sites and Legion passages.** A lodestone's mana bonus scanned all
  of the map's 2,863 sacred sites per footprint cell every tick (9-12% of a
  late tick), and Legion's narrow-passage lookups walked a tree of every
  opening (7-8%). Both now read only what lies nearby, with the same answers.
- **The lockstep state check** cost 33 ms per check at 2,500 units on that
  map, mostly folding the per-cell planes a byte at a time. It now folds a
  word at a time on four lanes, in fixed chunks spread over the simulation's
  worker threads (the value does not depend on how many): about 5 ms. A
  server whose game has no human player left skips it, except for a recorded
  replay's checkpoints. Every hash value changes, but the simulation does not.

On that game (8 Absurd AIs, Legion, Crusades, random starts, double sight,
+10, 30 game minutes, Intel Core Ultra 9 275HX):

| | 0.9.11 | 0.9.12 |
| --- | --- | --- |
| Units at the end | 2,621 | 3,162 |
| Slowest speed shown | -2 (minute 24, about 2,700 units) | +9 |
| Server ticks over 100 ms / over 250 ms | 861 / 219 | 0 / 0 |
| Longest AI think after the first minute | 929 ms | 7 ms |
| Wall time for 30 game minutes (900 s at +10) | 1,085 s | 909 s |

The two games differ after the opening (the AI changed), so the unit counts
differ. The 0.9.12 run is the code before the fog display change (client
only) and the beaten-base rule.

## Streaming keeps to YouTube's bitrate

Above YouTube's bitrate for a stream's size, frame rate and codec, YouTube can
accept the connection and never start the broadcast while the panel shows
**LIVE**. YouTube publishes a minimum and a recommended bitrate per row of its
live encoder table and no maximum, so the recommended figure, the top of its
range, is now the limit (`src/video/youtubebitrate.h`; for example 17,000
Kbps for 1080p at 60 fps in H.264).

- **BITRATE** steps stop at it; a saved higher value is lowered and saved, and
  the bottom line says *BITRATE LOWERED TO YOUTUBE'S N KBPS MAXIMUM*. Other
  destinations than YouTube keep the full range.
- **RECOMMENDED** beside the bitrate fills in YouTube's figure.
- **UPLOAD CAN'T KEEP UP - LOWER THE BITRATE** shows on the panel and the
  corner badge when the encoder keeps waiting to hand its packets to the
  connection, not merely when a still scene needs fewer bits.

The row is the first at or above the picture's shorter side, so a 7680 x 2160
window streamed at MAX 3840 (3840 x 1080) is a 1080p stream.

## Settings save as you change them

**Options**, **Voice Chat** and **Controls** no longer have a SAVE button: a
toggle, a choice or a rebound key is saved at once, a slider when you let it
go, and the player name when its box loses focus. **DEFAULTS** now asks first
(RESET TO DEFAULTS? YES / NO), and switches the sound back to the system's
default device at once instead of after a restart. The Controls hint (CLICK TO
REBIND / RIGHT-CLICK TO CLEAR) has a band of its own instead of covering the
last row, where a right-click used to unbind a key by accident. The Options
and Voice Chat pictures in the user guide were retaken.

## A BACK button on three menu panels

**SETTINGS**, **BENCHMARK** and **LOAD REPLAY** draw a **BACK** button beside
"Esc - Back" that does what Esc does. An empty replay list keeps its message
on a row of its own.

## For modders and mapmakers

No file format changed. A side's `FogColor` in `gamedata/sidedata.tdf` now
tints its mist, as in the original. A unit's `sightdistance`,
`radardistance` and `sightheight` decide what the fog shows, as they already
decided what the game counted as seen.

## For contributors

- Client (no hashed state): `client/retailfog.h` and `GameView::captureFog`
  build the local view from `World::viewCounts` and the Mapping plane (merged
  with allies); `UnitR::losSeen` (LosOn's four projected corners) and
  `UnitR::mapSeen` (minimap); `GameView::drawFog` is retail's Glide overlay
  (ARGB4444, 19/5/5/3 blend, RollingFog / WavyFog, `FogColor`), uploaded per
  changed block (`client/fogupload.h`). The World's display fog (`vis_`,
  `visGather` / `visCompute`, `TAK_FOG_EYE`, the worker, `cellVisible`,
  `revealTerrain`, `prepareInitialVisibility`, `setVisPlayer`) is gone;
  `World::viewDirty` (unhashed) marks the squares to rebuild. Details in
  `docs/height-and-fog.md` and `docs/retail-engine.md`.
- AI (levels 0-5): `Controller::knowledge_` (`knowsGround`, `inView`, `sees`,
  `Known::sighted`), `depositTaken_` with learning from a refused lodestone
  site, `ai::enemyStartSpots`, the exploration and scouting state
  (`exploreLevelFor`, start and sector targets, loss back-off, vacated
  bases), `TAK_AI_EXPLORE` (debug) counters. The lodestone search's nearest-
  first order is in `placeSite`. `classicwaves.h` (Retail AI) is untouched.
- Sim: `src/sim/statefold.h` (`WordMix`, chunked xxHash64-style lanes) for
  `World::stateHash`; `resolveSplit` and `World::compStats` for local nav
  splits (`TAK_COMPCHECK` in debug builds checks every carried labelling);
  bucketed `setSacredSites`; flat per-line opening records for `passageOf`.
- Server: `TickInput::clientHashes` / `replayTrail` (the referee hash only
  when a human slot or a replay trail reads it).
- Client UI: `client/promptbox.h` (shared YES / NO box), the options
  autosave, the Controls hint band, `client/uploadwatch.h`,
  `video/youtubebitrate.h`.
- Net: `kNetVersion` 304 -> 305, no new command, replay format 15, generator
  9. `tools/replay_test.cpp` pins 305 and refuses 304.
- Tests: new `state_hash_test` (pinned fold values, serial equals pooled; in
  the three CI workflows), `options_autosave_test` (22 of 38 checks fail on
  the old code), `stream_bitrate_test`, the `retail_fog` harness test; AI
  knowledge and scouting cases in `ai_behavior_test`; `navincr_test` split
  cases; `room_worker_test` hash-skip cases; `fogupload_test` cover checks;
  many tests moved onto the sight plane.
- Baselines retaken for the new hash layout: the 2,193 `hash@` keys of
  `tools/scenarios/baseline.json` (1,229 Retail, 964 Legion) and all 48
  `tools/navigation_checkpoints.json` lines.
- `--mpai` Inner Circle seed 1, `TAK_SPEED=80`: every gate hash changes
  (the hash layout, and for the Legion and both Crusades 300 s games also
  the AI's new targets and scouting); new values in the
  [validation report](release-0.9.12-validation.md). The cross-compiler
  golden (`check-determinism`) is unchanged, `dcef618cd2e4d558`.

## Known limits

New with 0.9.12:

- **An all-AI game may not end** when a last enemy unit stays hidden from
  every AI, since our levels no longer target what they have not seen.
- **On huge crowded maps, lone scouts sent to beaten bases rarely arrive**, so
  a base someone has moved into can go unnoticed for a long time.
- **Not Explored games run longer** than in 0.9.11, as the AI no longer knows
  every deposit and must find them.
- **The AI learns a hidden enemy lodestone from a refused build order**, as a
  player learns it from the red placement ghost.
- **+10 is still limited by the plain simulation cost** on very large games:
  past an estimated 3,500 to 4,000 units it cannot hold +10, and a spectating
  client on a slow core may fall behind first.
- **Human allies share main-view sight.** The original shares its main view
  only with computer allies; 0.9.12 shares it with every ally (a design
  choice).
- **The minimap's explored shade** is half-opaque black; the original remaps
  each pixel through a shade table.
- **Minimap dots** sit at a unit's ground position, not its projected height
  (as before).
- **YouTube's figures are its recommended bitrates**, because YouTube
  publishes no maximum; an ultrawide window picks its row by height.
- **The speed measurements** were made on an Intel Core Ultra 9 275HX, not on
  the reporting player's Ryzen.
- **0.9.11's limits still apply:** see the
  [0.9.11 notes](release-0.9.11-notes.md#known-limits), and through them the
  earlier notes; 0.9.11's "AI goes after everything" is replaced by the
  sight-only targets above.

## Compatibility

Protocol **305** (was 304), replay format **15** and generator version **9**,
all unchanged except the protocol. Checked in source against `v0.9.11`:
`kNetVersion` in `src/net/protocol.h` and `kReplayFormat` in
`src/net/replayhdr.h`.

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.12 clients and servers play only with each other**.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.9.12 plays only 0.9.12 replays; a 0.9.11 replay (format 15,
  protocol 304) is refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes, pack
  folders and settings files carry over unchanged (a streaming bitrate above
  YouTube's figure is lowered).

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.9.12-validation.md).
