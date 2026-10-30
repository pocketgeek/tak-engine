# TAK Engine 0.9.12

0.9.12 follows 0.9.11. The fog of war now looks and works as in the original
game: units see by the same sight the game plays by, ground seen before lies
under your side's rolling mist, and radar marks enemies on the minimap only.
Our AI levels attack only what their side has seen and scout the map for the
rest. Big late games keep their speed: in the reported game, eight Absurd AIs
on a 64x64 random map, 0.9.11 fell to -2 at about 2,700 units, and 0.9.12 held
+9 to +10 to 3,160. Combat follows the original more closely: every weapon
but melee aims only at buildings within its range, flyers dogfight flyers, an
attack aims a moment after the attacker stops (so Kirenna fires after a walk
far more often), and a shot stopped in front of a castle's long face counts as
blocked. Builders help and repair as in the original: they walk up until they
touch the unit, keep working when pushed, and never repair a unit that walks.
Streaming keeps to YouTube's bitrate, Options, Voice Chat and Controls save as
you change them, and SETTINGS, BENCHMARK and LOAD REPLAY have a BACK button.

**Compatibility first.** The AI's sight-only targets, the combat fixes and the
new lockstep state check change the game state the players compare, so 0.9.12
uses network protocol **305** (replay format **15**, unchanged). **0.9.12 does
not play with 0.9.11 or any earlier version, and does not play their
replays.**

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
  overlay is rebuilt only where a unit's sight changed. At 3,000 units the
  overlay and minimap draw in about 0.05 ms a frame instead of 0.17, and the
  old pass's 0.15 ms a tick is gone (the review's measurement).

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

## Weapons aim only at buildings in range

Until now the engine let every non-arcing weapon count a building as in range
up to its footprint plus 24 pixels further (so short-range attackers would not
grind its edge). The original has no such allowance: its range test belongs
to the weapon's class, and every class but melee measures from centre to
centre (`KINGDOMS.icd` `0x530580`; the arcing class tests its arc first, the
dropped-bomb class calls it directly; melee tests the two footprints). Run in
the original code, all seventeen classes answer the same as that rule against
a 7x12 building, and its attack order walks to a ring of the weapon's range
round the target's centre. Now the engine does the same for every weapon:

- **Shots that die at their range** (a line-of-sight bolt or lightning strike
  and a guided shot fly one weapon range and then vanish; a flame reaches only
  its range) had a dead band. In a reported Crusades game a Death Totem
  (lightning, range 550) took a Taros lodestone 584 pixels away as its target
  and fired all game long, and every bolt died about 540 pixels out without a
  hit (the original first counts that lodestone in range at 585). Towers and
  totems no longer target a building past their range, and mobile units
  ordered onto a building (Crossbowmen, Shamans, Wisps, Fire Demons, Fire
  Wagons, mages, monarchs and dragons on such a weapon) walk to their range
  itself and hit, where they used to stop up to the allowance further out (on
  a 7x12 Barracks, 120 pixels) and, but for a flame's splash, hit nothing.
- **Everything else** that is not melee or arcing: bombs dropped from flyers,
  remote spells (earthquakes, hail and ice storms, fire waves, rings of fire,
  area mind control and freeze), wandering storms, and the freeze, stone and
  individual mind-control beams. Their shots land at the target, so with the
  allowance they hit a building from up to it past their range, which the
  original never does. A Lokken's Fire Wave (range 120) was cast at a
  Barracks 239 pixels off, a Weather Witch's Tornado (150) from 270, and a
  Lich's Death Aura (60) from 179. They now walk to their range, and on Hold
  Position they no longer target a building past it. These weapons are all
  on mobile units: 38 of them in the standard roster, 37 with Crusades.

Arcing shots (arrows, cannons, catapults: their own arc test since 0.9.2) and
melee are unchanged. 40 unit types carry a shot that dies at its range in
the standard roster (34 mobile, 6 buildings), 38 with Crusades.

**Hold Position.** A unit on Hold Position, whoever owns it (a player's or an
AI's), no longer engages a building beyond its selected weapon's plain range,
as in the original. Before, the allowance let it take and hit a building up
to 120 pixels further out; a Weather Witch (Tornado, 150) or a Sea Dragon
(Tsunami, 450) on Hold Position now leaves a building past that range alone,
as it already left a unit there.

## An attack aims a moment after the attacker stops (Kirenna fires after a walk)

Kirenna (the Veruna Mage) walked up to a target and then stood without
firing: her aim script waits until her walk script has settled, and the
engine started her aim on the very tick she stopped, before her walk script
had seen the stop. The original's attack order (`Attack_Chase`, `0x403bb0`)
stops the walk when the attacker reaches range, waits one tick, then gives the
weapon its target, so the aim starts three ticks after the stop; and the
original tells a unit's own script which way it is turning
(`TurnDirection`, from `0x4d9593`), where the engine told only the
on-screen animation. Both now work as in the original (emulated end to end
with Kirenna's own script, the original's weapon, mission and movement
code):

- **The aim waits for the stop.** A ground unit that walks to attack (not
  melee) aims two ticks after its first tick in reach; one already in reach
  aims at once. It turns to face the target from the tick after.
- **Scripts hear their unit turn.** `TurnDirection` now reaches the unit's
  own script, which plays by the game's state, for every unit whose script
  has it, so other units' timing shifts slightly too.

Kirenna ordered at a target from 664 pixels now stops, aims two ticks later
and fires; over a grid of 144 order timings and distances she fires in 58
(12 before), and in the attack sweep her orders that walked more than 40
pixels fire in 26 of 52 (9 before). Like the original she still stalls in
some cases: the original, emulated over a similar grid with its approach
forced into range, fires in 36 of 144.

## Flyers dogfight flyers

A flyer attacking another flyer used to hang still beside it or hover round
it. The original gives any flyer whose target can fly the **AirToAir**
mission (`0x41ca50`, chosen at `0x4de530`), hover attacker or not: every
7 to 21 ticks it chases a point short of the target's led aim point, swings
out to its own side when threatened, now and then breaks off into straight
lines and hard turns (`VTOL_Evade`, `0x41e3d0`) and comes back, and flies back
onto the map if it has left it. Its weapons fire whenever the target is in
range. The engine now runs the same mission, its moving steering point and
its break-off (each checked against the original code on 12,000 to 20,000
emulated cases, all equal):

- **The Aerial Juggernaut** closes in so its anti-air arrows (450) reach; it
  used to stop at its cannons' 500 and drop cannonballs that miss flyers.
  Its arrows now hit 27 to 31 times a minute, none before.
- **The other weapons pick their own targets**, as in the original
  (`0x41d5ae`, `0x41c308`): attacking a flyer or a ground unit, a Juggernaut
  or Dirigible turns its unselected weapons automatic, so the Juggernaut's
  cannons bomb the ground while its arrows chase the flyer.
- **Dropped bombs wait for the script and the facing**, as in the original
  (`0x52fe30`, `0x52fff0`): an Iron Beak releases an egg only when its script
  is ready and its body faces the target, a Dirigible or Juggernaut when its
  script is ready. A bomb has no upward speed, so an egg hits a flyer below
  the Beak and falls past one at its own height, as in the original.

Over all 19 armed flyers against flyers (720 sweep cases), cases that fired
four or more shots without damage fell from 51 to 38 and damage rose 3.6%;
the Juggernaut's fell from 13 to 0.

## A long building is hit through the ground it stands on

A shot that ends on terrain or scenery near its target counts as **blocked**
when it falls short of the target's body; enough blocked shots make an
attacker on Manoeuvre walk to a clearer line, and the fire sweep drop the
target. The engine judged "at the body" by a circle the size of the
building's longest side, so in front of a castle's long face (a Taros Castle
is 8x20 squares) a ridge or a standing stone inside that circle swallowed
every shot uncounted: an archer fired into the crest for ever. Emulated in
the original's own projectile code (`0x52a4d0` and its cell lookup), a shot
reaches a body only through the cells it stands on: a ridge one or two cells
before the face stops the shot on the ground with no hit, 40 and 75 pixels
from a 4x12 body's centre. The engine now uses the footprint rectangle (within
the old circle) for a building that is not square, both for counting blocked
shots and for testing a clear line. Every unit and square building behaves as
before.

A Mage Archer on Manoeuvre facing a Taros Castle's long face behind a ridge
now walks 41 pixels (48 with Legion) and hits it seven times; before, it
fired eleven shots into the ridge and never moved.

## Builders help and repair as in the original game

A builder told to help build or repair walked to one point south of the
building (a repairer to where the unit stood when ordered) and worked from its
build distance; it stopped working whenever it was pushed out of that
distance, a helper gave up after 8 seconds without getting 20 pixels closer,
and a repairer healed a unit while it walked past. The original's ground
HelpBuild (`0x405cf0`) and RepairUnit (`0x407190`), run stage by stage in
emulation, do it differently, and the engine now does the same:

- **They walk up until they touch.** Both install the rectangle controller a
  build site and a reclaim use: the builder walks to the nearest cell where
  its footprint touches the unit's, on the side it comes from, so several
  helpers spread round a building. The work starts on arrival; if no route
  gets there, it starts anyway when the builder is within its build distance
  of the unit, and otherwise a help ends ("can't get there"; no unit-chatter
  voice here, as before). There is no timer: a helper held up in a crowd
  keeps trying.
- **Once working, distance no longer matters**: a builder pushed aside keeps
  working where it stands.
- **No repair is done to a unit that moves** (walks or turns). The repair
  waits and re-aims at the unit each look; every look while it moves counts
  1 (3 when the builder is within reach), and past 10 the repair is given up
  ("repairs unsuccessful"): a few seconds of walking, less when the builder
  keeps up. A still unit is never given up on, even one the builder cannot
  reach; during the approach, one that has moved to another cell is re-aimed
  at on the next look (every 7 to 21 ticks).
- **A patrolling builder's leash** is its own distance from where it left its
  route, its sight distance (the original's 199 pixels continue, 200 end); the
  engine measured the target's distance instead.
- **An assist queued behind other orders** starts when its turn comes; it
  used to latch at once and could drop the orders before it.
- **A helper that finishes a unit inside a factory walks out of the factory**
  (not in the original, which leaves it where it touched the unit, in the
  yard): the 0.9.8 report of builders standing in a factory's door, which the
  guard ring already answers for guards. In the door test (every shipped
  ground factory, both pathfinding modes and balances, 232 cases) all outputs
  finish and no unit is left in a factory; without the step-out, 84 assist
  cases left the helper standing in the yard.

Flying helpers and repairers keep 0.9.7's VTOL_HelpBuild and VTOL_RepairUnit.
A building never repairs (the original's RepairUnit ends at once without a
mover; the Keep's `canmove=1` let the engine accept the order).

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
- Sim (range): `Weapon::centreReach` / `Weapon::structurePad` (a pad only
  for a melee approach); no structure pad in `slotInRange`, `combatReach`,
  the per-slot fire gate, `repositionForShot`, the flyer/structure hold and
  Legion's `reachPx` / `reachFloat`. New `shot_range_test` (Retail and
  Legion, a case per weapon class); `structure_combat_reach` places its
  target inside the range. `tools/re/check_inrange_classes.py` runs every
  weapon class's range test in the original code.
- Sim (attack timing): `Order::attackChased` / `attackAimTick` (hashed with
  a tag) hold a walking, non-melee attacker's aim until two ticks after its
  first tick in reach, and its facing pivot one tick longer (`tickCombat`);
  `World::notifyTurnDirection` (called before MoveRate) runs `TurnDirection`
  in the unit's hashed script VM; `brakeGround` reports the idle pivot's zero
  first (`UnitScript::turnInPlace`). New `attack_stop_test` (Retail and
  Legion); `tools/re/probe_attack_chase_stop.py`.
- Sim (AirToAir): `src/sim/retailairtoair.h` (ports of `0x41ca50`,
  `0x41e3d0`, `0x41d780` and the moving controller), `World::tickAirToAir`,
  `Order::airToAir` (hashed with a tag), `airtoairdistance` for units and
  weapons; a Dropped bomb's release in `tickScriptWeapon` waits for SET 22
  and, for a hover attacker, its facing. New `air_to_air_test`, AirToAir
  modes in `retail_motion_test`, `tools/re/check_air_to_air.py`.
- Sim (blocked shots): `nearBody` (the footprint rectangle within the old
  circle for a non-square body) in `World::noteShotImpact`, and
  `World::chordClear` now takes the target. New castle case in
  `blocked_shot_test`; `tools/re/probe_long_footprint_blocked.py`.
- `Order` stays five cache lines (320 bytes): `attackChased` fills the pad
  beside `chaseLeashed`, `repairLooks` (RepairUnit's give-up count) the pad
  after `chaseOrigin`; a ground help or repair leg hashes its stages with a
  tag once started.
- Sim (help and repair): `World::prepareHelpApproach` / `tickGroundHelp`
  (HelpBuild `0x405cf0`) and `prepareRepairApproach` /
  `installRepairApproach` / `tickGroundRepair` (RepairUnit `0x407190`) run
  on the leg's `mission` with its `buildRectangle`; a ground help leg is no
  longer a Move (`assist` latches `buildSiteId` once the leg is current,
  `cancelBuilds` drops it). `World::moveRateMoving`; `constructionWork` split
  out of `tickConstruction` (MobileBuild's own site work and its watchdog are
  unchanged); `leaveFactoryYard` and `pointOutside` (split out of
  `clearBuildSite`, same point). `World::repair` refuses a building. Legion:
  Repair is an exact rectangle kind like Build and Reclaim, a help leg is
  Build, no member is keyed by its target any more. `docs/retail-engine.md`
  "Helping and repairing on the ground"; probe
  `~/tak-tmp/retail-checks/repair-approach/probe_repair_help.py` (outside
  the repo).
- Tests (help and repair): `helpbuild_test`'s approach cases (spread helpers,
  touching reach, a 14 s hold, a bumped helper and repairer, failed
  approaches in and out of reach, a walking repair target, the patrol leash,
  a queued assist; Retail and Legion): 36 checks, 25 fail on the old code.
  `builder_automation` (a departing ally now walks off; patrol eligibility
  gets time to walk up), `production` (repairers start touching their
  targets) and `retail_script` (the repair controller driven by a Repair
  order; work stops when the target moves) follow the new rules.
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
  `tools/navigation_checkpoints.json` lines; for the combat fixes, 352
  `hash@` keys of 16 attack and chase fixtures again, two new work-counter
  entries and 39 licensed keys in attackring-struct-60 (built around the old
  structure allowance) and fight-retarget (details in the
  [validation report](release-0.9.12-validation.md)).
- `--mpai` Inner Circle seed 1, `TAK_SPEED=80`: every gate hash changes
  (the hash layout, the combat fixes, and for the Legion and both Crusades
  300 s games also the AI's new targets and scouting); new values in the
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
- **A building just past a tower's range is no longer shot** even where a bolt
  launched from the muzzle could have clipped its edge (a Stronghold at a
  lodestone 620 pixels off did): the original does not target it either.
- **A unit whose range is shorter than it can stand from a building's
  centre never hits it**, as in the original: a Lich (Death Aura, 60) or an
  Injured Thirsha (Wind Wave, 60) ordered onto most buildings keeps moving at
  it and never casts (a Lich stood 88 pixels from a Barracks' centre). The
  original's attack order keeps trying narrower rings and random spots within
  range for as long as it lasts; before, the allowance let them hit from
  there.
- **A Fallen Angel's Death Sword** (range 140) now strikes a building only from
  140 pixels, as in the original. On its attack runs over a lodestone it
  missed every time in a test (before, it struck once or three times in
  80 seconds from further out); bigger buildings are still hit.
- **A crowd attacking a small building stands closer**, at its range from
  the building's centre: 60 gunners (range 96) at a 4x4 house in a Legion
  test all get a shot and do more damage, but shuffle more in the tighter
  ring; with Retail pathfinding they all get a shot too, but kill the house
  later than with the old allowance.
- **Hold Position units no longer engage buildings beyond their selected
  weapon's plain range**, whoever owns them, as in the original. A Sea Dragon
  on Hold Position with its Fire Breath (525) selected is acquired by a
  building 472 pixels off but never fires at it (it did not fire at a unit
  there either; not yet diagnosed).
- **Kirenna still stalls sometimes** after walking up to a target, as the
  original does: when her walk script has not settled by the time she aims
  (for example against a castle from 848 or 1,148 pixels, or a lodestone from
  704 or 1,004), she stands until she moves again. The original's attacker
  also keeps its weapons on automatic while it walks, so its fire-at-will aim
  settles her walk early and she fires far more often there (129 to 133 of
  141 emulated approaches); the engine does not port that part, which would
  change what every unit shoots at on the way to an attack target.
- **The attack timing rides the front order.** A route rebuilt in the middle
  of an attack drops it, so an arrival right after such a rebuild aims at once
  (or, in a later chase of the same order, waits its two ticks again).
- **Builder, repair and idle automatic-weapon pivots** tell a unit's script
  only when the turn changes direction, not the original's zero-then-turn
  every tick; only scripts that read their turn state while pivoting notice.
- **An Iron Beak hardly hits a flyer at its own height**, as in the original:
  its eggs have no upward speed and fall past. It hits flyers below it.
- **A dogfight's retargeting** uses the engine's own target scan, not the
  original's, so a flyer that finds its target again keeps its attack where
  the original restarts it; the group check, the air-traffic step-out and
  the lost-target search of the original's AirToAir are not ported (the
  attack ends instead, as AirToGround's already does).
- **Blocked shots against a long building** count short misses on open ground
  in front of its long face as blocked too, as they already did for a square
  building (a Taros Beak bombing a castle's side gives up after three eggs
  instead of four; it hit nothing either way). The reposition's minimum
  distance and the navigation grid's sight radius still use a building's
  longest side.
- **Helpers and repairers start working on arrival**: the original first
  turns to face the unit and waits for the builder's stance, which the
  engine does not port (as before).
- **A repair whose unit is healed to full by someone else ends at once**; the
  original's repairer walks on until it arrives.
- **A helper whose site is walled off** can walk round the wall for a long
  time before its approach fails (41 seconds behind a three-cell wall in a
  test), as a builder going to a walled-off site does: the same search.
- **Give-ups are silent**: the original's "can't get there" and "repairs
  unsuccessful" voices have no channel in the engine.
- **A patrolling builder repairs the nearest damaged unit** it sees; the
  original picks one at random (unchanged).
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
