# TAK Engine 0.9.13

0.9.13 follows 0.9.12. Melee units can no longer hit flyers in the air, and
every "not against air" weapon judges a flyer by whether it is flying right
now, so a landed flyer is a ground target for everyone. Melee units reach the
building they attack, each from its own place round it, so a second Spirit
Wolf on a Lodestone and wolves sent at a Keep bite instead of standing out of
reach. Flyers step out of a crowd instead of sitting on top of each other, on
moves, attacks, fight-moves, patrols and flying builders' jobs, and a flying
guard whose charge dies looks for another. Mind control, stone and freeze fly
to their target as a real shot and work when it strikes, with the original's
chances; they never take a Monarch, a building or a unit still being built,
and do no damage. Units attack enemy buildings still being built. With Legion
pathfinding, flyers keep the army's pace only in a formation (Alt+number), not
in a group. And a performance pass makes very large views, long build queues,
auras, the mouse pointer and big maps cheaper without changing a single game:
the same lockstep checks, AI orders and random draws as before.

**Compatibility first.** The flying status spells, the melee and air-traffic
rules and the new targets change the game state the players compare, so
0.9.13 uses network protocol **307** (replay format **15**, unchanged;
protocol 306 was never released). **0.9.13 does not play with 0.9.12 or any
earlier version, and does not play their replays.**

- **Everyone updates together.** A 0.9.13 player cannot join a 0.9.12 server,
  and a 0.9.12 player cannot join a 0.9.13 server: the connection is refused
  with a *protocol version mismatch* message. Update your game and tell the
  people you play with to update theirs.
- **Old replays:** a replay recorded by 0.9.12 or earlier is refused with the
  version message. To keep watching them, keep your 0.9.12 install (0.9.0 to
  0.9.12 stay on the GitHub releases page); versions before 0.9.0 are no
  longer offered for download and can be built from their git tags (see the
  [development guide](development.md)).
- **Players:** nothing to do. Settings, your data folder choice, key bindings,
  saved `.kmp` maps, generated-map recipes and mod packs carry over.
- **Server operators:** update `takserver` to 0.9.13 when you announce the
  release; until you do, 0.9.13 players cannot use your server. Nothing else
  about running a server changed (see
  [public-server.md](public-server.md#versions)).

## Melee never reaches a flyer in the air

A Titan used to walk under a slow or hovering flyer, such as a Gryphon, and
hammer it. In the original, a melee weapon only ever reaches a unit on the
ground (`KINGDOMS.icd`: the melee range test `52b980`, Attack_Melee's exit
for a target in the air or carried, `404778..4047ee`, and the melee tests of
the target picker, its keep test and the hit response, `412620`, `40fcb3`,
`409db4`; read and emulated, the engine copies none of the original's code or
data). 0.9.13 does the same:

- A Titan, Swordsman, Troll or any other unit fighting with a melee weapon
  drops an order to attack a flyer in the air (or a unit that boards a
  transport), does not go after one on its own and does not answer one that
  attacks it.
- A flyer that has landed is fair game: melee units attack it, ordered or
  not, also when they pass one on a move, and stop when it takes off.
- A melee blow's splash still never reaches a flyer at its flying height
  (the Titan's Hammer reaches 15 pixels up; shipped flyers cruise 50 to 200).

**"Not against air" weapons judge the flyer's height, not its type.** Every
`noairweapon` / `toairweapon` test in the original reads whether the target is
in the air right now. The engine tested whether its unit type could fly, so
the Knight, the Grenadier and the wolves could never attack a landed flyer.
They now do, and a ranged attack on a unit that boards a transport ends, as a
melee one does. The Retail AI sees landed flyers as ground units too.

## Melee units reach the building they attack

The owner's 0.9.12 replay: a second Spirit Wolf on an Aramon building stood
behind the first for good and never bit. Melee attackers headed for a
building's centre; the original's Attack_Melee walks each to its own free
place on a rectangle round the building's footprint (`404690` -> `4d4e80`),
corners included, and its range test lines both footprints up as `52b980`
does. 0.9.13 ports both, in both pathfinding modes (with Legion the leg to the
building is handed to the native navigator).

In the new `melee_building_test` fixtures (target topped up so the fight goes
on): on the code before, Legion's second wolf on a lodestone never bit and
four wolves on a Keep never touched it; twenty Swordsmen round a Keep bit with
11 of 20. Now every wolf bites in every fixture, in both modes, and 16 of the
20 Swordsmen bite (about 150 hits). Wolves at a Barracks or any other long
building (every factory and gate) used to stand out of reach on its short
sides; they bite now.

## Flyers step out of a crowd

The same replay had grouped Drakes piling onto one spot for the whole fight.
The original keeps an air-traffic count per flyer (unit `+0x12a`), stepped
after each occupancy rebuild; at 5 its flyer missions push a short step-out
(`4214b0`) ahead of themselves, toward the least crowded side, and then go on
with what they were doing. The engine kept no such count, so stacks never
broke up. 0.9.13 ports it into every flyer mission that has it:

- **Attacks and moves.** Drakes and other flyers attacking one target or
  chasing one enemy flyer settle apart; groups sent to one point and flyers
  coming in to land settle apart too (twelve flyers sent to one spot now land
  apart with Retail pathfinding as well).
- **Fight-moves and patrols.** Flyers on a fight-move fly a move's course and
  keep their places in the group, as on an ordinary move, instead of flying
  the whole way stacked and piling onto the clicked point; armed flyers on
  patrol no longer lap as one stack. A factory's flyers follow their rally the
  same way, and a Shift-patrol given to a patrolling flyer joins its loop at
  the end, as on the ground.
- **Hovering attackers** (AirToGroundHover, `41d820`, ported whole): a Drake
  comes in to a point beside its target, takes a new point round a target that
  stands still after a wasted shot, gives up an attack it keeps being crowded
  out of, and goes back to its formation when it strays too far from it; one
  that loses its target circles the spot and looks for another.
- **Flying builders** (Flying Builders, Dark and Darkest Priests, Thirsha)
  building, helping or repairing on one job step aside when crowded, stop work
  for a moment and come back to the job.
- **A flying guard whose charge dies** (VTOL_SeekGuard, ported whole) circles
  where its charge last stood and guards the first friendly unit on the
  ground, or building, that comes within its sight. It used to stand down at
  once.
- **Bombing passes** (AirStrike, ported whole) for flyers with a `dropped`
  weapon: mods only, no unit of the game has one.

`tools/re/check_air_traffic.py` matches every port against the original under
emulation. With Legion pathfinding a formation's flyers keep their own lanes
over the ground (Legion's design) and do not step out of them.

## Mind control, stone and freeze fly

The Harpy's and Mind Mage's charm, the Basilisk's and Acolyte's Turn To Stone
and the Mechanic's, Chief Engineer's and Neo Dragon's freeze took their
target the moment they were cast, while the ball of energy was still drawn
flying towards it. In the original these are real shots (`52d8d0`, `52db40`,
`52dda0`) that act on the unit they strike, through the original's own
callbacks (`52da10`, `52dca0`, `52df00`, which the Area Mind Control and Area
Freeze use too). 0.9.13 ports them:

- **The shot.** The spell works when its ball strikes, on the first unit it
  strikes: one that steps into its path, or the one behind a target that died,
  moved off or changed sides meanwhile; the ball is aimed once and never
  steered. A charm in flight is lost if its caster dies; a stone or freeze
  shot still lands.
- **The chance.** Each can fail: a unit without veteran rank is taken 80
  times in 100, one of rank 4 or more 99 times, by one draw on the original's
  random stream (the charm used the game's).
- **What it takes.** Never a Monarch, a building, a unit still being built or
  one of the caster's own; an ally's unit that the ball strikes is taken (the
  original spares only the caster's own), and the Area Mind Control and Area
  Freeze take allies' units in their area too. Casters still never pick an
  ally as a target.
- **No damage.** Stone and freeze used to deal their weapon's damage (to
  immune units too); none of the three deals damage now (in Crusades the
  freeze weapons used to hit immune units and buildings for 450).
- **No charmed buildings.** A charm no longer takes a wall, a Trebuchet or a
  Mortar (and in Crusades a Bomb Sprinkler, Gatling Crossbow or Prismatic
  Mirror); the original charms only units that move.
- **Target choice.** A stone or freeze caster no longer passes over a
  paralyzed enemy, and no longer picks one still being built.
- **What you see.** The spell's explosion shows on the unit it takes, and
  only when it takes; the hit sound plays where the shot strikes.

## Units attack enemy buildings still being built

A building still being conjured, and a unit still being built at a factory,
were invisible to every automatic target choice: idle, patrolling and moving
units stood or walked past an enemy castle going up beside them (in the
owner's replay three Spirit Wolves idled beside an unfinished Aramon castle).
The original's target list (`411700`) and picker (`412620`) take such a
target, scored ten times worse than a finished one. 0.9.13 does the same: a
unit takes one after any finished enemy near enough. A guard keeps to the
unit it guards but shoots one in range, and stone, freeze and charm casters
still leave it alone. Three more places follow the original: a paralyzed
enemy is no longer passed over for a healthy one, the random choice among
several targets lands on the same one as in the original, and a computer
player's caster no longer switches to stone, freeze or charm against a
building still being built. The replay's situation (real Crusades types) now
destroys the site in both pathfinding modes.

## Legion: flyers keep the army's pace only in a formation

By the owner's ruling, only a formation's (Alt+number) flyers keep station
over the ground army; a group's (Ctrl+number) fly their own orders at their
own speed, as a plain selection's do, and so take the air-traffic step-out
like any other free flyer. In the owner's replay a Ctrl+1 group's Drakes,
fight-moving with wolves, goblins and harpies, were held to the ground's pace
for almost the whole trip and then sent toward the map's west edge.

A formation's flyers still land in lanes just in front of the ground, but
only when every lane lies on the map with room to its edge; otherwise the wing
lands where it was sent, as in the original (the old rule clamped the lanes
onto the edge, where two Drakes were pinned and the wing crawled at a quarter
of its speed). In `legion_flyers_test ctrlfree` (a Ctrl+1 group sent 2,900
pixels west to a point 288 pixels from the edge) every flyer now reaches its
own point by tick 806; before, only 3 of 6 came within 48 pixels of it.

## Faster with big armies, the same game

A performance pass (twelve changes and two new measurements, branch
`perf-followup`; the full evidence is in
[perf-followup-2026-10.md](perf-followup-2026-10.md)) changed how much work
the game does, not what it does. Every change is exact. Against its base
(0.9.12) the seven `--mpai` gates, the cross-compiler golden, all 145
scenario fixtures, the navigation checkpoints, four recorded big games
(1,200 to 10,400 units, every checkpoint to tick 18,000), the campaign smoke
games and the AI command streams were identical, and the screen showed the
same pixels except for unit shadows in very large views (below). On main,
with the gameplay fixes, the seven gates read the same before the merge (as
recorded by `b4e83a49`) and after it (the
[validation report](release-0.9.13-validation.md)).
Measured on an Intel Core Ultra 9 275HX, base against the combined branch,
swapped pairs:

| What | Before -> after |
| --- | --- |
| A 7680x2160 view of 10,000 units, frame | 31.5 -> 18.4 ms |
| Densest shadow view at 7680x2160, frame time | 0.54 of before |
| Frames over 16.7 ms, a view at the edge of the old shadow limit | 52% -> 15% |
| The unit under the mouse pointer, 10,000 units | 0.33-0.48 -> 0.013-0.019 ms a frame |
| Long build queues' ghost buildings, frame | 0.07-0.34 of before |
| Hidden Units: Fade, whole frame (forest, dense forest, keeps) | 0.96 / 0.89 / 0.93 of before |
| A 3,200-unit battle with 1,200 Aramon Smiths (auras), tick | -16.6% |
| A marching army's sight updates, tick | -7.8% (Retail), -5.0% (Legion) |
| Exploration's fixed cost, 1-million-cell map | 0.18 -> 0.05 ms a tick |
| First minute of an 8-AI game on a large retail map, tick | -38% |
| Idle tick on a map with 11,600 scenery features | -18% (burning and fading alone) |
| Legion building refresh, 2,000 buildings | 0.27 of before |
| Late game, 8 Absurd AIs, 4,000 units, tick | -3.1% |
| Recorded 10,000-unit games, tick | -0.4% to -1.9% |

- **Unit shadows** have room for up to 16 shadow pages instead of 4. A view
  whose units need more than four pages used to drop the whole frame to slow
  per-triangle shadows; it now keeps the cached silhouettes every smaller
  view already uses. **This is the one visible change, accepted by the
  owner:** in such views (5,000-10,000 units at 7680x2160) about 5-6% of the
  picture differs, almost all of it lighter, because the per-triangle path
  darkened a model's overlapping triangles twice. Views that fit four pages
  draw exactly as before.
- **The pointer** asks only the units near it (a grid of the drawn units)
  instead of testing every unit each frame.
- **Build ghosts** keep their model geometry between frames and skip ghosts
  off screen; **Hidden Units: Fade** finds each covering object once a frame.
- **Auras** fold a big world's stat auras per recipient, tile by tile.
- **Sight and exploration** move a unit's counted sight by the squares that
  change, and keep the explored map's block summaries instead of rebuilding
  them every pass.
- **Burning and fading scenery** is kept in its own list instead of found by
  walking every feature each tick.
- **Our AI levels' raises** gather the raisable bodies once a think (per call
  6 to 75 times cheaper with 64-256 casters; no measured game issued a raise,
  so it is not a whole-game gain).
- **Legion** reads buildings through a spatial index on maps with 64 or more,
  finds the routes a change touches through a route index, and groups very
  many changed rectangles with bins. Recorded games rarely reach these paths.
- **Measurements:** `simperf` reports complete ticks (with the lockstep check
  due every 30th tick) beside the core ticks, and the render benchmark has 14
  interactive cases (pointer, selection, ghosts, Hidden Units, shadows).

## For modders and mapmakers

No file format changed. A flyer's `dropped` weapon now flies the original's
AirStrike bombing passes. `noairweapon` and `toairweapon` now test whether the
target is flying at that moment, as in the original.

## For contributors

- Sim (status flights, protocol 306): `Weapon::losStatus` names the three
  Line-of-Sight status classes; they launch as straight shots and fly in
  `tickStraightProjectiles`; `applyHit` runs the original's callbacks for
  them and the two area spells (`World::retailStatusRoll` draws the CRT
  stream); `retailSelectTarget` has the picker's filters. New
  `los_status_flight_test`, `tools/re/probe_los_status.py`.
- Sim (melee rectangle): `World::installMeleeApproach` installs Attack_Melee's
  rectangle on the attack leg; Legion leaves such a leg to the native
  navigator; `meleeInRange` snaps to footprint origins. New
  `melee_building_test` and the `melee-*.scn` fixtures.
- Sim (air traffic): `src/sim/retailairtraffic.h`, the hashed counter and the
  step-out front order; AirToGroundHover, VTOL_SeekGuard and AirStrike in
  `src/sim/retailairattack.h` / `retailairseek.h`; a shot's outcome posted to
  the shooter's mission; `OrderShape::FlyerOffsets` valid for `AttackMove`.
  `ClassicBrain` reads a step-out as its own mission; our AI levels' `idle`,
  `waveFree` and the stalled-builder watch look past a leading step-out. New
  `air_traffic_test`, `tools/re/check_air_traffic.py`.
- Sim (construction left): `World::rebuildVisibleLists` keeps enemy sites on
  the visible lists; the picker's x10 is for construction left, not paralysis;
  `ClassicBrain` takes the World's list as it is. `tools/re/probe_site_targets.py`,
  `tools/re/check_fire_sweep_picker.py`; `fire_sweep_sites`.
- Sim (air flags, melee and air): `canAttackTarget`, the picker, its keep
  test, the hit response, the fire-at-will scorer and the slot gates read
  `flightGroundMode`. New `melee_air_test`, `tools/re/check_melee_air.py`.
- Legion (flight stations): only an Alt+N formation's flyers hold a station;
  landing lanes only when they all fit with a lane's room to the map's edge.
  New `legion_flyers_test ctrlfree` and `formationedge`.
- Performance: `src/sim/legion_cluster.h`, `legion_routebins.h`,
  `retailviewcounts.h`, `client/unitindex.h`, `client/shadowatlas.h`
  (`kShadowMaxPages` 16) and the benches and tests listed in
  [perf-followup-2026-10.md](perf-followup-2026-10.md); every item is one
  `--no-ff` merge on `perf-followup` (revert one with `git revert -m 1`).
- `kNetVersion` 307 in `src/net/protocol.h`; `tools/replay_test.cpp` pins it.

## Known limits

New with 0.9.13:

- **A melee attacker drops its attack on the tick a flyer takes off**; the
  original drops it at Attack_Melee's next wake, a few ticks later. The
  weapon cannot swing meanwhile in either.
- **Legion's melee ring round a moving unit has no corner places**, so at
  most four attackers stand round a 2x2 unit (the building rectangle above
  has them).
- **The air traffic's group check** is ported for hovering attackers only;
  AirToGround, AirToAir, the seek, the flying guard's seek and AirStrike fly
  as if in formation there.
- **Flying builders keep the engine's own approach**: a builder that stepped
  out comes back by the engine's approach to the job, not from the start of
  the original's job.
- **A flying guard's search** takes the lowest unit number within its
  128-pixel sector; the original walks each sector's own unit list, whose
  order the engine does not keep.
- **AirStrike** aims at the target unit where the original aims at its
  position at that moment; AirToAir still uses a `dropped` weapon against a
  flyer.
- **A step-out that ends** hands over to standby and landing a tick later
  than the original does.
- **Shots that end without an impact**, storms, pending area effects and
  death blasts tell the shooter nothing (the original's outcome post covers
  only the impacts the engine has).
- **Legion formations' flyers do not step out of their lanes** (Legion's
  design); their standby and landing still do, so a formation's flyers take
  longer to land (in `legion_flyerfront` the last landing waits 900 ticks
  instead of 150).
- **A Legion formation sent near the map's edge**: when the lanes do not fit,
  every flyer lands round the click among the arriving ground, which then
  finishes a fight-move later (2,068 ticks instead of 1,889 in
  `formationedge`).
- **Status shots and other shots in one tick**: the engine flies its flames
  and straight shots (the status spells among them) before its other shots,
  where the original runs one list in creation order, so in a tick with a
  status roll and another shot's damage spread the two draws can come in the
  other order. A charmed unit still keeps its unit number (as in 0.9.12).
- **Enemies with construction left:** a site at 0 HP or below, which the
  original's scans still take, exists only in restored saves and is skipped.
  The picker's paralyzer-immunity test is not ported.
- **Performance:**
  - Under a slow continuous pan across a very large view, the shadow cache
    redraws every tile every frame, and the worst frames are about 4% slower
    than before; the extra shadow pages use 32 to 144 MiB more video memory.
  - The AI raise speed-up has no real-game evidence (no measured game issued
    a raise), and the Legion items rarely run in recorded games.
  - Under a 384 MiB video-memory cap at 7680x2160 the renderer can crash in
    `GeometrySubmit::passDrawBuffer`; it does so on 0.9.12 too and was not
    caused by this work.
  - In the combined build, COB scripts read 5% slower on one 2-hour Legion
    replay with the same instruction count, and peak memory there is 1.7 MB
    higher; neither was traced. Every recorded game's whole tick is faster.
  - All timings were taken on an Intel Core Ultra 9 275HX.
- **0.9.12's limits still apply:** see the
  [0.9.12 notes](release-0.9.12-notes.md#known-limits), and through them the
  earlier notes; 0.9.12's "Our AI levels raise only beside an idle caster"
  holds (the faster search gives the same answers).

## Compatibility

Protocol **307** (was 305; 306 was never released), replay format **15** and
generator version **9**, all unchanged except the protocol. Checked in source
against `v0.9.12`: `kNetVersion` in `src/net/protocol.h` and `kReplayFormat`
in `src/net/replayhdr.h`.

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.13 clients and servers play only with each other**.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.9.13 plays only 0.9.13 replays; a 0.9.12 replay (format 15,
  protocol 305) is refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes, pack
  folders and settings files carry over unchanged.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.9.13-validation.md).
