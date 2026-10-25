# Factory output: the Catapult stuck in the Barracks door (protocol 255)

## Report and reproduction

Reported against 0.7.28-42 (`8d7139f2`): "the aramon barracks doesn't seem to spit
out its units far enough and when I built a catapult it got stuck in the entrance
and I had to kill it so other stuff could build". The recording is a Legion-mode,
Crusades-balance game on a generated map; it replays exactly on that build.

Barracks unit 12 (ARAKEEP, 7x12, birth heading 3721) finished a Catapult
(ARAPULT, 4x4) at tick 7283, which set off for its exit point 207 px south. At tick
7328 the Barracks placed its next site, a Knight (ARAKNIGH, 3x3), on its pad 65 px
south of its centre while the Catapult was only 38 px past the pad: the Knight's
footprint rows 273..275 overlapped the Catapult's 275..278. On a loaded map the
mover commits a step only through the native rectangular test
(`commitGroundStep` -> `mobilePlacement`, 507d10), and every step either body could
take still overlapped the other. The Catapult stood 39 px out of the door from tick
7358 until the player killed it, the finished Knight (7921) stood on the pad, and
nothing else could be placed there. The game's other Barracks released their 90-odd
outputs normally, the ground units each ordered 190..520 px out from the pad.

Cause: `canPlaceProduction` admitted ground outputs with `canPlace`'s centre radius,
which is sized for the NEW unit alone (3x3: 36 px). Any output larger than the next
one and slower to clear the pad than the factory's next readiness could be born on.
The flat-terrain tests never showed it: terrain-only harnesses commit moves with a
nav-grid/centre-cell test that lets an overlapped body walk out, so the overlap
unwound by itself there.

## What retail does

* **Site admission.** The BUILD mission (`UNITMISSIONCODE_BUILD`, handler 0x401c20)
  stage 2 (0x401d40..0x401ff7) runs QueryBuildInfo, takes the piece position
  (0x4dd250), forms the footprint origin `(pos - footX*8 + 8) >> 4` (our
  `footprintOrigin`) and calls 0x507d10(output type, self 0, origin, factory
  `+0x130 & 3`, allowMoving 0). Creation gives every unit low bits 1
  (0x511b12..0x511b2d), so a factory always takes the mobile footprint loop: map
  features, any live entity on any cell (self 0 excludes nobody; allowMoving 0
  means moving bodies refuse too), corner-height depth and slope limits. A refusal
  sleeps the mission `rand(15)+7` ticks (0x535cc0, 0x4d6a10), sets mission flag 2 and
  retries stage 2.
* **The factory's own cells.** Yard symbols decode at 0x4c0f59 to masks: `.` 0,
  `o` 0x2f, `c` 0x2d, `O` 0x2b, `C` 0x35, `w` 0x37, `f` 0x6f, `S` 0x8f, `Y` 0x31,
  `y` 0x29 (mobile types: 0x29 everywhere). 0x5062d0 stamps the factory into a cell
  when `mask & (yard open ? 2 : 4)`: `o` always, `c` only while the yard is closed.
  Every shipped ground factory's pad lies on its `c` door rows when the building
  stands on its cell grid anywhere in its buildangle range: the Keeps and the
  Taros/Veruna halls counter-rotate the pad (`32768 - GET 27`); Creon's Academy and
  Smithy turn it with the building, 12 / 8 px sideways at the extremes, still on `c`.
* **Release.** GetBuilt (0x402220) at completion, for a structure builder only
  (`+0x130` bit 0x2000000), copies the factory's queued `QMove`/`QPatrol` orders
  (0x4de530, 0x4d78a0); with none copied it issues `PARK` (0x604ae8) around the
  factory, padding `(rand(6)+rand(3)+3)*16` (`+6` for canfly types). The ring radius
  is `footX*16 + padding + half the factory's footprint diagonal` (407ca0, already in
  `retailpark.h`): for the Barracks 191..303 px from its centre for a 2x2,
  223..335 px for a 4x4, against a door edge 96 px out. A mobile builder's output
  gets nothing.
* **BUGGER_OFF** (COB 19) is never acted on: 0x50d50e stores bit 3 of `+0x12f`,
  read only by GET 19 (0x50d1fd) and save/load (0x513528). A body in the door just
  holds the next site until it moves.

## Change

* `canPlaceProduction`: a scripted producer's non-flying output is admitted by
  `mobilePlacement(type, self 0, origin, allowMoving false)`, the 507d10 host the
  mover commits with (new `World::mobilePlacement(const UnitType&, self, ...)`
  overload). Terrain-only harnesses use the terrain grid and the body plane.
  `canPlace`'s radius remains for flyers and unscripted producers. This also
  replaces the radius test the water branch kept against other units.
* `tickProduction`: a refused scripted site sets `Unit::productionRetryTick` to
  `now + 7 + gameRand(15)` (hashed when set, cleared when a site is created).
* Exit orders are unchanged: the Retail exit step and Legion's Exit mission.
  Retail's GetBuilt release (rally, else PARK) was implemented and measured, then
  left out. Its ring parks the army in a band 0..190 px in front of the factory,
  which is where the skirmish AI's site search (`canPlace`, bodies included) looks
  for its next building: naval_ai's Taros opening on Varro Passage could no longer
  site its Hell Gate and produced no hover unit (2 of 146 cases). It also gives no
  longer distances than the exit step (24 soldiers: PARK 213..320 px from the
  factory centre, exit step 154..385). Shipping it needs the AI half of GetBuilt
  first (0x50b4f0, the AI unit assignment) or a MobileBuild-style site test that
  ignores mobile units (405824). Legion's PARK routing also left 3 of 24 crowded
  units pressing outside their ring without re-rolling it.

**Update, protocol 295 (0.9.1):** the exit step is gone for structures. A Creon
Navy Yard on a pond sent its Submersible 316 px into another body of water,
where it sat on the pad and held the yard for 3m46s. Factories now release with
GetBuilt's rally-else-PARK, kept to cells the output can reach (see
`retail-engine.md`, "A factory's output: rally, else PARK"). The siting
conflict above did not recur: `naval_ai`, `production_exit` and the quick suite
pass with it.

## Every shipped ground factory

Largest ground output, then three of the smallest (and the reverse order), at both
ends and the middle of the buildangle range, flat loaded map, both modes and
balances. Ticks are from the large unit's completion until it is clear of the
factory's rectangle.

| Factory | Footprint | Pad (px south) | Large / small | Clear after (Retail / Legion) |
| --- | --- | ---: | --- | --- |
| ARACASTL Keep | 7x14 | 87.5 | ARACAN 4x4 / ARABROAD | 98 / 97 |
| ARAKEEP Barracks | 7x12 | 65 | ARAPULT 4x4 / ARASWORD, ARAKNIGH | 75 / 74 |
| CREACAD Academy | 9x9 | 64.6 | CREBEAS / CRESHOC | 20 / 21 |
| CRESMIT Smithy | 7x8 | 59.9 | CREFIRE / CREAUTO | 22 / 22 |
| TARCASTL | 8x20 | 127.8 | TARBLACK / TARZOM | 18 / 18 |
| TARDUNG | 8x18 | 115 | TARFIRE / TARARCH | 46 / 46 |
| TARHELL | 10x14 | 77.5 | TARMAGE 4x4 / TARDEMON | 33 / 33 |
| VERCASTL | 8x20 | 130.6 | VERCEN / VERBERS | 40 / 40 |
| VERKEEP | 5x14 | 77.4 | VERPULT 4x4 / VERSWORD | 84 / 84 |

On `origin/main` every one of them births the next site over its large output
(Retail and Legion); with a loaded feature plane the Barracks Catapult never leaves
the yard in Retail mode, and in Legion the Barracks stops after two units, the
Catapult and the Knight locked together, exactly as reported.

## Validation

`production_exit` (data-backed, 268 cases: the 40 earlier ones plus 228 new)
passes; the same source against `origin/main` fails 131 (Retail 52 large-first and
27 large-last, Legion 50 and 2), six of them outright stalls. `production_test`
(both modes) needed one fixture change: its scripted factory had no yardmap, so its
pad lay on solid cells; it now carries the Barracks' door rows and opens its yard
before reporting ready, as the shipped scripts do. The recording's own commands on
the fixed build (the hash parts by the tick-6570 checkpoint, at the first refused
site's retry draw): the Barracks places the Knight at 7358, after the Catapult has
cleared, the Catapult reaches its exit point at 7584, and production continues
(Knight 7951, Archer site 7972).

Optimized Debug ctest 551/551. `--mpai` Inner Circle 300 s, seed 1, twice per mode:
Retail `95b05c8290b55317`, Legion `21055e66adec40d6`, identical to protocol 254.
Neither AI game ever has a site refused (a retry draws the hashed gameplay RNG), so
the change is inert there; the production_exit hashes show where it is not.
`tools/check-detmath.sh` passes and `tools/check-determinism.sh` agrees on
`dcef618cd2e4d558` across GCC/Clang O0..O3 (aarch64 legs skipped, no headers).
