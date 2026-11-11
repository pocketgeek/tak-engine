# Porting retail's movement layer

This file is the developer reference for the Retail pathfinding mode (the
default): how retail's movement, search, scheduling and occupancy routines in
`KINGDOMS.icd` work, how `src/sim` ports each of them, and the native
comparisons (`tools/re/*.py`) and tests that check the port. The other mode,
[Legion](legion-pathfinding.md), has its own reference. Retail movement and
order behaviour outside the search layer is in
[retail-engine.md](retail-engine.md): factory output release and PARK
(GetBuilt), reclaim reach (`0x406250`), and help-build and guard follow
(HelpBuild `0x405cf0`, Follow_Ground `0x407f90`, VTOL_Follow `0x418860`).

The retail audits come first; then each ported piece with its native
evidence, from the composed World behaviour down to the isolated kernels and
the capture tooling; retail's two-layer movement design closes the file. Test
counts are the sizes of the native comparison runs.

## Retail audit: entity pools grow as needed

Retail's pool is maxunits, which `499590` clamps to 20..500 (options default
250). Sizing the production pools below to the engine's unit cap (2,000 by
default) would make `416430` pay 7 x 2,000 = 14,000 work to walk one player's
pool against a 12,000 budget a tick, slower admission than at any retail
setting, so a pool starts at retail's size and grows only as needed.

The rule (`World::entityPoolCapacity`): a player's pool starts at the maxunits
`499590` would store for the unit cap -- the cap itself for every retail cap
(20..500), 500 for the engine's 1,000 and 2,000 lobby caps (what `499590`
does with them), 20 for a cap below 20, and the options default 250 (`4c7c7a`)
when uncapped, which is no retail setting. It gains a slot only when a unit is
created into a full pool (no CRT draw, as `5121d8` makes none when full), and
a raised cap grows an allocated pool to the new start size. A pool is
therefore never larger than retail's for the cap until a player actually holds
more records at once than retail allows; normal games scan exactly retail's
slots (500 a player at the default cap, 3,500 work, not 14,000).

Native evidence: `check_entity_slots.py` also emulates `499590` (thiscall,
stores `+0xc`) for every cap 1..2,100 plus 4,000, 65,535 and 2^31 - 1 and
requires `World::entityPoolCapacity` to match each (2,103 caps; 250 -> 250,
500 -> 500, 2,000 -> 500). `check_crowd_arrival.py --world-tick` takes
`--unit-cap` (default 2,000) and gets the native pool size by running `499590`
on the capture's options object; `retail_replay_probe --crowd-world` sets the
same cap on World. Three Hunters for 300 ticks, all fields match, at unit cap
250 (slots 2, 86, 24 of 250), 500 (5, 172, 49 of 500), 2,000 (5, 172, 49 of
500, identical to retail's 500), 20 (0, 7, 2 of 20) and uncapped (250).
`check_entity_slots.py` (160 allocations), `check_scheduler.py` (106
fixtures), `emuscheduler.py`, `check_navigation_retry.py` (8,192) and
`fuzz_world_search.py --cases 500 --seed 12` pass.

Regression (`retail_trace --selftest`, `productionPoolCapacity`): pool size
for caps 20, 250, 500, 1,000, 2,000, uncapped and 5; a raised cap grows an
allocated pool to 500, not 2,000; at cap 2,000, 500 live units fill the pool
with one draw each and the 501st takes slot 500 without one. Sizing the pool to
the engine cap fails it (pool 1,000 at cap 1,000). The navigation checkpoint
worlds are uncapped, so their pools are 250.

The fire sweep's budget (`tickFireSweep`, `capacity / 30 + 1`) reads the
engine cap, not the pool; it is a separate retail reading.

## Retail audit: production entity pools for path admission

`416430` admits path searches by walking each player's allocated entity
slots, holes included, at 7 work a slot; the slot cursor wraps at the pool's
last slot and stamps the wrap tick, which weights the next initialization
(`retailInitialPathWeight`). World addresses units by global IDs that are
never reused, so `PathService` cannot use IDs as slots: a pool per player
indexed by ID would hold every other player's units as holes, its capacity
would drift with the match, and the scan order would be ID order (the
three-Hunter `check_crowd_arrival.py --world-tick` run then diverges at tick
15). World therefore keeps retail's per-player slot pools.

What retail does (static reading, then emulation of the routines alone):

- **`511570`, pools at game start.** Capacity is maxunits
  (`[[0x62d558]+8]+0xc`, stored at `+0x14e7c`); there are `10 x capacity + 1`
  records of `0x138` bytes, record 0 a sentinel, and record `i` has ID `i`.
  The ten player records are sorted (`5118c0`: by `+4` in game mode 3, else
  by address) and the `r`th gets records `r x capacity + 1 ..
  (r + 1) x capacity` at `+0x74/+0x78`. Emulated for maxunits 20, 250 and 500
  in modes 1 and 3: player `p` gets `p x cap + 1 .. (p + 1) x cap` in mode 1,
  the reverse order under the mode-3 sort; a slot's offset within its own
  pool is all `416430` uses.
- **`499590`, setting maxunits,** clamps to 20..500 (`0x5f0400/0x5f0404`;
  emulated 0, 10, 20, 250, 500, 1000, 2000 -> 20, 20, 20, 250, 500, 500,
  500). The options file default is 250 (`4c7c7a push 0xfa`).
- **`5121d8`, placing a new unit,** takes the CRT-ranked free slot of its
  owner's pool, one `rand()`, none when the pool is full (ported as
  `retailAllocateSlot`; `check_entity_slots.py`, 160 fixtures).
- **`512ae0`, retirement,** frees the record: `512e23` clears `+0x130` bit 24
  and `512e52` drops the allocated count, right after `512ee0` places the
  corpse feature. A dying unit holds its slot through its death animation,
  not through the corpse's life, which is a map feature.

What World does:

- `World` owns an `EntitySlotTable` (slot -> unit per player, unit -> slot)
  and binds it to `PathService`. `spawn` places every unit with
  `retailAllocateSlot` from the match CRT stream (`crtRand`, origin
  `0x5121dd`). Capacity follows the grow-as-needed rule above; an uncapped
  World (tests, tools) starts at retail's default 250.
- A record is held while the unit is alive or `deadFor <
  corpseAnimationTicks()` (and below `kRetiredTicks`): where World places the
  corpse, which is where retail retires. Freed slots are reclaimed when the
  pool is next searched, so a retired slot is reused at the next allocation.
- An ownership change (`giveUnit`, `scenarioTransfer`, `captureUnit`, the
  mission `capture` verb and `s` command) cancels the unit's request and
  gives it a new record in the new owner's pool, as retail's transfer
  allocates a new object. `requestPath` repairs a unit whose owner a tool
  edited directly.
- `PathService` maps `(player, slot)` through the table: scheduler capacity is
  the pool size, a hole is a slot with no request. Fixed fixture pools
  (`setEntityPool`, restored captures) take precedence; a standalone
  service with neither uses unit IDs as slots.
- `stateHash` folds the CRT stream, the pool sizes and every unit's slot.
- `retail_replay_probe --crowd-world` does not override the pool. It takes
  the native CRT seed, spawns through production, and reports the capacity
  and each Hunter's slot. `check_crowd_arrival.py --world-tick` builds the
  native pool at that capacity (`--unit-cap`, see the grow-as-needed audit
  above) and places each Hunter by running `512162..512255` (`5121d8`)
  itself, then requires World's slots to match before ticking.

Deliberate differences: retail refuses a creation when the pool is full;
World's cap counts live units, so a pool full of dying records, or an
uncapped World past 250, grows by one slot instead (no draw). Unit caps of
1,000 and 2,000 are not retail settings (maxunits clamps to 500). Unit IDs
are global, so per-unit processing order is `units_` order, not retail's
record order.

Regression (`retail_trace --selftest`, no fixture pools): per-owner ranked
slots, the shared CRT stream and capture reallocation; retirement frees a
record for reuse while a dying unit keeps its own, and a full pool grows
without a draw; seed 2631 places units 1, 2, 3 in slots 65, 60, 248, so at
budget 300 (43 scans a tick) unit 2 is admitted first, on tick 2, and the
three deliver in slot order; a cursor restored to slot 249 wraps on its first
scan and stamps that tick. With an ID-addressed pool the admission case fails
(units admitted in ID order on tick 1) and the wrap case throws (no slot 249).
The retry-ladder fixture allows up to eight ticks to admit, since its unit can
sit anywhere in 250 slots.

Native comparisons: `check_crowd_arrival.py --world-tick` on production pools, three Hunters for
300 ticks (native CRT seed `0xbf28309a` puts them in slots 21, 686, 194 of
2,000) and 450 Hunters for 1,800 ticks, all fields match; the composed 450 x
1,800 run (fixture pool) matches. `check_entity_slots.py` (160),
`emuscheduler.py`, `check_scheduler.py` (4,072 gates, 106 fixtures),
`fuzz_world_search.py --cases 500 --seed 12`, `check_ground_owner.py` (3,651
checks), `check_mobile_placement.py` (6,000), `check_navigation_retry.py`
(8,192), `check_search_movement.py` (unit 625 alone, and with 590, 592, 627),
and `check_gate_search_movement.py --missions` for the four gates, `--pair`,
`--pair --pair-close` and `--return-trip --rounds 2400` all match.
(`--return-trip` at the default 1,200 rounds stops short of arrival.)
battle-field-2x60's mopup `trace_cells` ratchet is 183,665.

## Retail audit: who holds an overlapped ground cell

Retail keeps the ground word by events, not from positions: the first body to
stamp a cell keeps it. With two identical bodies stamped 1 then 2, native
`507d10` lets body 1 step east and refuses body 2. A position-derived rule
(the last eligible unit in vector order holds an overlapped cell) makes the
opposite decisions, so World keeps the word by the same events.

What the routines do (static reading, all owner kinds checked by emulation):

- **`5066f0`, placing a body.** It relinks the body into its 8-cell sector
  list if the sector changed (`506650`). Off the map (any footprint edge
  reaching `W`/`H`) it only links it to the off-map sector. Then, for every
  footprint cell (a structure: yard cells under its open/closed mask; a
  mobile: only in mode 1), it looks at the holder. If the holder is a live
  pool entity and its player is active with kind 3 (remote), the newcomer
  overwrites it. Otherwise a live holder keeps the cell, the holder gains
  `+0x130` bit `0x400000` and the newcomer `0x800000` (`506863..5068b8`,
  `5069de..506a33`). A free, dead or out-of-pool cell goes to the newcomer.
- **`506aa0`, a body leaving its cells.** It clears only the cells it holds,
  then its `0x800000`. If it had `0x400000`, it calls `5065b0` over its old
  rectangle. That is `5097f0`, which walks sectors column by column from one
  before the rectangle to one after (each list newest link first: `506630`
  pushes at the head), and calls `5062d0` on every body whose rectangle
  overlaps. `5062d0` acts only on a body with `0x800000`: it clears the bit
  and claims every cell that has come free, by the same rule. A cell it
  already holds counts as a live holder, so it marks itself both ways.
- **`5066a0`, removal** (`512ae0`): `506aa0`, then unlink from the sector.
- **`51b3b0`, a cell or mode change** (takeoff, landing, cargo): `506aa0`,
  then `5066f0`. The ground step (`4dafd2`/`4db001`) does the same.
- **`507c70`, a yard state change:** set `0x800000` and call `5062d0`, which
  claims the cells the new state stamps and frees the ones it no longer
  stamps.
- The yard mask: open stamps bit 2, closed bit 4. The `4c0f59` decode gives
  `o S w f` both, `c C` closed only, `O` open only, and `. y Y` neither. The
  shipped yardmaps use only `. c C o S w`, which World's existing rule
  covers.
- The flags `0x400000`/`0x800000` are read nowhere else in the binary.

Owner kinds 1 and 2 are this machine's human and AI. Kind 3, a remote player,
is overwritten. In a World match every seat is simulated locally, so no seat
is kind 3, and that branch is not ported.

The port (`World::groundOwner_`, `Unit::ground`) does exactly the above.
`groundStamp` is `5066f0`, `groundUnstamp` is `506aa0`, `groundRemove` is
`5066a0`, `groundRestamp` is `5062d0` and `groundRestampRect` is `5097f0`.
The sector of a body is its position `>> 23`, as retail's. The events are:

- creation (`spawn`, and a production site after its exact placement);
- the accepted ground step (`commitGroundStep`);
- landing and taking off;
- boarding (removal: World keeps cargo off the word) and unloading;
- death and every retirement;
- the yard state change (COB SET 18);
- a new terrain (all bodies stamped again in unit order).

`searchBodyRect` gives a cell to the ground word's holder when that holder
covers it, and otherwise to the last covering unit in vector order. When every
event is stamped the holder always covers, so the fallback serves only bodies
moved outside the events (test fixtures that set positions directly) and
bodies retail keeps off the word at the map's last row and column, which World
still counts as occupants. The search plane's full build keeps the same body
on every cell, so placement (`mobilePlacement`, `507d10`), landing
(`509400`), live grades (`cellScore`) and search grades read one holder.

Validation:

- `tools/re/check_ground_owner.py` runs the unmodified `5066f0`, `506aa0` and
  `5066a0`, with real sector lists, through 400 random event sequences on a
  24x24 map: placements, one-cell moves, lifts and deaths of up to 11 bodies
  crowded round one spot, owner kinds 1 and 2. After each of 14,403 events it
  compares every cell's native holder with World's (`retail_replay_probe
  --ground-owner`, the same events through World's handlers). 0 mismatches,
  over 23,249 overlapped cell samples. The vector-order rule disagrees
  after 4,962 events. A stateless rule (the covering body placed first wins)
  disagrees after 631, so the sector-ordered restamp is needed. A control
  with owner kind 3 disagrees after 1,374 of 3,651 events, which confirms
  the harness sees the kind.
- The audit's isolated `5066f0` + `507d10` case gives, for kinds 1 and 2,
  holder 1 and step-east `[1, 0]` when 1 is stamped first, and holder 2 and
  `[0, 1]` when 2 is first. Kind 3 gives the reverse.
- CTest `pathblock` checks that the first body stamped keeps an overlapped
  cell; a leaving holder's cells go to the newest-linked refused body;
  refused bodies are restamped in sector order; search grades take the
  holder. Under the vector-order rule three of them fail. The newest-link
  case agrees with the vector-order rule and refutes the stateless one.
- `retail_trace --selftest`'s gate-policy fixture places a body in a gate's
  passage before the closed gate. In retail the body can only be there if
  it arrived while the passage was open (`507ae0` refuses to close a gate on
  a held cell), so it holds the cell; a closed gate spawned first would hold
  it instead and hide the body from the policy.
- Also matching: `check_mobile_placement.py` (6,000),
  `check_landing_overlap.py` (61,082 `509400` calls),
  `fuzz_world_search.py --cases 500 --seed 12`, `check_world_grade.py` raw and `--live` (6,000 each, a fresh
  probe-37 fixture of capture `5cea791f1448cd7a`), `check_ground_commit.py`
  (12,000), `check_search_movement.py` (unit 625 alone, then 3, 3 and 4
  concurrent requesters, 600 rounds), `check_gate_search_movement.py
  --missions` for all four gates plain, `--pair`, `--pair --pair-close` and
  `--return-trip` (20 runs), and `check_crowd_arrival.py` (3 Hunters for 300
  World ticks, 450 Hunters for 1,800 rounds). These start without overlapping
  footprints, so they check that nothing else depends on the holder rule.
- The `--mpai` Inner Circle seed 1 game has no overlapping footprints, so its
  hash does not exercise the rule; games where bodies overlap (spawns,
  unloads, landings on unexplored sites) do.

Not ported, or unchanged:

- Owner kind 3 (overwrite), as above.
- Retail leaves a body whose footprint reaches the last map row or column
  off the word; World still counts it as an occupant through the fallback.
- `5139f6 -> 507cd0` (a structure's yard restamp at an unidentified
  initialisation) is not ported.
- Cargo: World keeps it off the word (boarding removes it); `4dadd2` commits
  it through `51b3b0`, whose stamp in attached state is unverified (see the
  transport audit).
- Death: World removes a body at death; retail keeps a dying body stamped
  (as a dead id, which reads as free) until `512ae0`, and only then hands its
  cells to the bodies it refused.

## Retail audit: a pending request survives displacement

A re-request of the same controller and goal keeps a queued or running search
however far the unit has moved since it asked, including when the retry ladder
(`World::tickNavigationMovement`, the crowded/traffic/random branches)
re-asks a unit that has been pushed several cells.

Retail's retry at `4e5268` is `push 1; call 4e4f50`. `4e4f50(1)` tests the
navigator's pending bit (`+0x114 & 2`, `4e4f5e..4e4f68`) and, when it is set,
returns at `4e5026` without touching the request, the per-player count
`0x634674`, the outcome bits or the worker. Nothing in it reads the unit's
position: `416430` reads the start only when it admits a request (World's
`refreshSearchRequest`). Only `setDestination` `4e54e0` cancels (`415f30`)
before resubmitting, which World expresses as a new controller or destination.

Emulating `4e4f50(1)` on a pending navigator after 0, 2 and 9 cells of drift,
with and without the worker attached (`OBJ+0x58/+0x64`), leaves the worker,
`+0x114 = 2` and `0x634674 = 1` unchanged in all six cases.

`PathService::request` has no displacement test. A re-request for the same
controller and destination never restarts; a changed controller or
destination cancels. For a rectangle or ring controller the destination is the shape:
World projects the anchor cell from the unit's position (`navigationCell`,
`navigationPoint`), so that cell is not compared. An unadmitted request takes
the new start, heading and costs for hosts that pass no refresh. CTest:
`pathsearch` (circle, rectangle and ring after 2- and 9-cell drift, plus
changed destination) and `retail_trace --selftest` (the production retry
ladder after 0/1/2/9 cells of drift, plus a changed mission point), both
without a fixture entity pool. With a one-cell displacement limit the 2- and
9-cell cases fail (two admissions, the worker retired); the port admits once
and delivers.

Native comparisons: `check_scheduler.py` (4,072 admission gates, 106
fixtures), `check_navigation_retry.py` (8,192 retry decisions),
`check_navigator_reset.py` (4,352), `check_destination_setup.py` (320 World
submissions), `fuzz_world_search.py --cases 500 --seed 12`, and
`check_crowd_arrival.py` with three Hunters for 300 World ticks and 450
Hunters for 1,800 rounds all match.

## Retail audit: flyer landing site and airborne occupancy (all modes)

> Flyers do land on each other in retail, and the port matches it. Landed flyers are in ground word `+0`:
> mode 1 stamps them at `50697f..506a3d`, re-stamped by `51b3b0`/`4dafd2`
> when the mode changes, as `searchBodyRect` does. A descending flyer stays in
> mode 2 until `4da750`, so the only place it appears is word `+2`. There a
> shared cell's owner is redrawn every tick (`506c40`), and eight or more
> overlapping flyers leave `0xffff`. `509400` passes both, and it passes
> unexplored sites. `tools/re/check_landing_overlap.py` replays landings
> natively (`509400`, `5066f0`, `507050`, `506c40`) and checks the port on
> every call: 61,082 calls agree. Over 512 scenarios, 366 end with overlapping
> landed flyers. Of the 2,794 overlapping pairs, 1,317 came from a flyer
> accepted during another's descent (904 of those via `0xffff`), and 1,477
> came from unexplored sites. On those sites a flyer was accepted on top of an
> already landed one 343 times. Retail therefore stays as it is. Legion alone
> refuses landed and descending flyers' footprints; see
> [legion-pathfinding.md](legion-pathfinding.md#scope).
>
> **Air traffic.** World keeps retail's air-traffic counter (unit +0x12a):
> VTOL_Standby and VTOL_LandIfCan step a crowded flyer out of a hovering or
> descending stack before it searches for a site
> ([retail-engine.md](retail-engine.md), "Air traffic"), so twelve flyers sent
> to one spot land apart. `509400` and its port are independent of it.

Flight is not handled separately per pathfinding mode, so the landing check is
world-level flight behaviour in **every mode** (owner's decision: port it
exactly). Retail has no "no shared landing spot" rule: no overlap test against
grounded or descending flyers and no recheck during descent that restarts the
landing, and World has neither.

### `509400`: the landing-site predicate

`509400(unit, point)` is called only from the landing search/mission `416cd0`,
at `41708e` (the current position) and at `41721f` (each candidate). It is
`retailLandingSiteFree` (`src/sim/retailairgrid.h`), and
`World::flightLandingFree` supplies World's planes:

- **Footprint origin.** `(x - fx<<19 + 0x80000) >> 20` is kept as int16, and
  likewise for z. The site is refused when `x0<0`, `z0<0`, `x0+fx>=W` or
  `z0+fz>=H`.
- **Unexplored sites need no checks.** The coarse exploration word is at
  `((z0>>1)+(fx>>2))*(W>>1) + (fx>>2)+(x0>>1)`. Retail uses footprintX on both
  axes. If the lander's player bit (`1<<unit+0xfd`) is clear, the site is
  accepted and no terrain or occupancy test runs. World reads
  `navigationExplored_`, the same plane the search grades use.
- **Feature word `+8`.** `0xffff` passes. An index below `0xfffa` fails if it
  is out of range (count `+0x19ec0`) or if its feature has blocking bit `0x20`
  (`+0x13c`). A tail `0xfffe` resolves its anchor through `+0xa*W + +0xb`;
  the site passes if the anchor holds a marker value, otherwise the anchor's
  blocking bit decides. Any other value from `0xfffa` to `0xfffd` fails.
- **Structure yard flag `+0xd` bit `0x10`.** Any flagged cell fails. `5066f0`
  stamps the flag at `5068ca` for every yard byte with bit 0 set, which the
  `4c0f59` table gives to `C c O o S w f y Y` (every character except `.`).
  `506b39` clears it. The flag is set whether a gate is open or closed. World
  takes it from the live structures' yardmaps, and an unparsed (empty) yardmap
  counts as a repeated blocking character.
- **Ground word `+0`.** A live entity other than the lander fails. `5066f0`
  stamps mobile units in mode 1 and the yard cells of structures (open gate
  cells excepted). World keeps the word (`groundOwner_`), and
  `searchBodyRect` reports its holder.
- **Airborne word `+2`.** A live entity other than the lander fails; `0`,
  `0xffff` and stale dead owners pass. This is the persistent grid described
  below. **Any airborne flyer overhead blocks**, not only one that is
  descending.
- **Heights.** Let `lowest = sea - maxwaterdepth` (`+0x192`). For `canfly`
  types (`+0x260` bit `0x800`) that are not `floater` (`0x200000`), `lowest`
  is raised to `sea`. A cell fails if `low (+6) < lowest`, if
  `high (+5) > sea - minwaterdepth` (`+0x194`), or if
  `high - low > maxslope` (`+0x23c`). Flyers therefore cannot land on water.
  World derives the quad extrema from the corner lattice (`50ed60`). The
  `landMaxWaterDepth`/`landMinWaterDepth`/`landMaxSlope` type words take the
  movement class's raw values, or else the FBI keys, with retail's constructor
  defaults 10000/-10000/255 (`4dfb10`, `4dfc40`).

### The persistent airborne occupant grid

Retail's grid persists across ticks, and so does World's
(`World::airOccupancy_`, `RetailAirOccupancy`):

- **Tick placement.** `51d3e0` runs the per-unit update loop first. It then
  clears every live `canfly` unit, in player/entity order, with
  `507050(unit,1)` (`51da6a..51db00`), and inserts every live `canfly` unit
  with `506c40` (`51db2a..51dbbe`). Projectiles and the next tick's missions,
  including `509400`, read the result. World rebuilds immediately before
  projectiles, the same position relative to the unit loop, with the RNG
  ordering the air traces verify.
- **Clear, `507050(unit,1)`.** This zeroes the footprint recorded at insertion
  (`+0x126/+0x128`, with the current size `+0x78`) and resets the overlap
  count `+0x117`. Units that are not live are skipped. A flyer whose live bit
  is cleared without a removal therefore leaves stale ids behind.
- **Insert, `506c40`.** This runs only for mode 2 (airborne) footprints that
  lie strictly inside the map; otherwise it records origin -9999. When a cell
  owner is invalid or not live, or its list has overflowed, the cell becomes
  the `0xffff` sentinel and the inserter's list overflows. Otherwise it builds
  reciprocal lists with `519bd0`/`519c10`, collects at most seven candidates
  whose current footprint (`+0x74`) covers the cell, and picks one with
  `535cc0`. An eighth candidate sets the sentinel and overflows every list
  involved.
- **Retirement, `512ae0 -> 5066a0 -> 507050(unit,0)`.** Each cell owned by the
  retiring flyer goes to a random overlapping list member whose recorded
  footprint covers that cell (`535cc0` at `50725e`, which draws RNG), or
  becomes `0` if there is none. Sentinel cells are cleared. The retiring
  flyer's id is then removed from the other lists with `519c70`, which
  replaces the entry with the last one and decrements the count even when the
  id is absent. A flyer whose list overflowed is removed from every live
  `canfly` unit's list. World retires a body from its occupancy planes when it
  dies, not at the end of the death animation, so World runs this at the
  death transition. A dead flyer reached through any other removal path is
  removed at the next rebuild.

### Validation

- `tools/re/check_landing_site.py` sets up random native map, exploration,
  feature, pool and type memory and calls the original `509400` for 8,192
  sites. The sites cover unexplored sites, map edges, feature tails and
  markers, yard flags, self, dead and out-of-pool owners, `0xffff`, water and
  depth/slope limits. All 8,192 decisions match (1,055 accepted, 7,137
  refused).
- `tools/re/check_air_occupancy.py` runs retail's own `507050`/`506c40` tick
  sequence over 256 persistent scenarios (5,735 ticks). The bodies cluster,
  wander, take off and land, and cross map edges. Retirements go through
  `507050(unit,0)`, and some deaths only clear the live bit, leaving stale
  ids. Every grid word, the draw count and the RNG state match after every
  tick: 7,292 draws, 1,256 retirements, 663 silent deaths and 582 sentinel
  cells.
- `check_air_collision_grid` (1,024) passes on the class.
  `retail_visual_test`'s cached-grid check compares the persistent
  clear/insert cycle with fresh builds over 200 frames. Also passing: `check_landing_search` (1,024), `check_landing_mission` (2,048),
  `check_flight_motion` (4,000), `check_flight_patrol` (4,000),
  `check_hover_attack` (4,096), and the air traces
  `check_air_flight_motion_trace` (600, plus 128 unload ticks),
  `check_air_unload_heightstep` (480), `check_air_map_height_scan` (480) and
  `check_air_unload_integrated_trace` (500).
- `retail_motion` checks grounded and simultaneous flyers under the retail
  rule, and that an airborne flyer overhead blocks an explored site, the
  blocked flyer lands clear of it, and a ground unit entering the site does
  not interrupt a descent that has already begun.

No flyer lands or retires while overlapping another flyer in the first minute
of the `--mpai` Inner Circle seed 1 game, so that harness does not exercise
this; games where flyers land or die while overlapping other flyers do.

`509400` has no movement-class test, so `retailgap`'s VTOL_standby case does
not use a cell that is unwalkable in the flyer's movement-class nav grid. arafly has no movementclass and no maxslope key, so its slope word is the
default 255 (`4dfcc8..4dfcdb` passes the constructor's `0xff` as the key
default, and `4c0eb4` stores it at type `+0x23c`). The original routine,
emulated with the test site's quads (lows 116..163, highs 163..213, spread 56,
sea 58, explored), reports **landable**: it refuses only when maxslope is 55.
The case parks the flyer over open water on Lake Lokken, which retail does
refuse to a non-floater flyer, and requires it to land on a dry footprint and
stay there.

Residual approximations:

- World removes dying units from occupancy at death, whereas retail keeps a
  dying flyer in both words until `512ae0`. Most flyer retirements are 1 tick
  later.
- World ids are never reused, so a stale id can never resolve to a new unit
  that occupies the same pool slot.
- Retail's ground word is not last-stamp-wins: a body stamped over a live
  holder leaves it the cell (`5066f0`, owner kinds 1 and 2), and only a remote
  player's (kind 3) holder is overwritten. See "Retail audit: who holds an
  overlapped ground cell" above.

## Retail audit: search, scheduler and budget classes

A fidelity audit of the Retail-mode search layer (scheduler `416430`, init
`415170`, tracer `4146e0`, cost search `4142c0`, reconstruction `414450`,
grades `4139d0`/`413c80`, goal controllers, delivery `4e4ea0`). The existing
fixed-fixture comparisons have randomized and adversarial counterparts.
### Path-budget class: every player, not "humans"

The scheduler's 5x class byte, player `+0xe3` (`[game+0x24e7+p*0x110]`), is
set to 1 by the player initializer `4f6290` for **every** player it creates
(`4f6379 mov byte [esi+0xe3],1`). Its only other writer is `4d2a12`, reached
only when the session-mode object `[game+0x175dc]` reports 1, which is a
campaign mission (`4a5633..4a5673` read `Mission`/`Campaign`; skirmish is
mode 2 via `QuickStart.tdf`/`Map`, multiplayer mode 3). There it copies
byte 1 of the mission's `Player%i` entry for players 1..9, and the mission
parser sets that byte only for `strategic` (`4c83fb`); `passive` and
unspecified entries leave 0. Player 0, the human, keeps the initializer's 1.
The other stores to `+0xe3` in the image (`44febb`..`45086e`) are
`vsnprintf` overflow terminators in unrelated buffers.

Ground truth: every saved retail capture (four skirmish save directories,
human type 1 and AI type 2) has the byte set for all ten slots.

The byte is therefore not "seated human". Because the quantum is
`(budget/divisor)/weights`, all-ones and all-fives differ only by truncation,
but they do differ: at the default 12000, 7 pending players get retail's
`(12000/35)*5 = 1710` each, not 1714, and 9 get 1330, not 1333. Campaign
missions differ materially: passive players draw one share against the
human's and strategic players' five. `World::pathBudgetClassMask_` holds the
class: `setupMatch` sets every bit and `setupMission` keeps only the human and
`strategic` slots. Only `PathfindingMode::Retail` applies it; Legion's Retail
fallback keeps an unweighted split. `retail_trace_test --selftest` checks the
classes per mode. The captured-replay probe restores the captured byte (all
ones).

### Randomized differential tests

- `tools/re/fuzz_search_worker.py` drives the real scheduler/init/tracer/cost
  search/retries/reconstruction lifecycle against `RetailSearchWorker` on
  generated maps (6x6 to 111x95): mazes, one-cell and diagonal-only gaps,
  combs, spirals, slope/traffic/road bands and noise, sealed goals, goals inside
  walls, blocked or off-map starts, start == goal, budgets 1..12000 and forced
  node-cap exhaustion. 6,095 lifecycles and 767,049 scheduler ticks match
  (every cell flag/direction, ordered grade query, work remainder, retry, event
  and delivered waypoint), including 78 routes clipped at 64 points. Ten cases
  whose retail run did not finish within 5,000 ticks were skipped.
- `tools/re/fuzz_world_search.py` (runner mode `--world-search2`) drives the
  production World request -> PathService -> delivery path with random start
  and goal cells, headings, turn rates around the 200/1000 boundaries, road
  multipliers around 81920, water multipliers, 10 footprints up to 4x4,
  boats, walled rooms, exploration masks, moving requesters and the
  `--terrain` cache built by the original routines. Seven seeds, 10,031
  searches, 386,000 ticks: zero mismatches (and the fixed
  `check_world_search.py`, with and without `--terrain`, with the native
  player byte authored as retail's 1).
- The other randomized checks (scheduler, grades, goals, trace, attempt,
  route, init, weight, cost search) run under several seeds.

Two harness boundaries are excluded rather than "fixed": in the
non-terrain World fixture a cached grade 2 makes `4139d0` call the live
placement query `4db640`, which the native side answers from an empty map
(-1 everywhere), so cached grade 2 is only compared in terrain mode and
`check_world_grade.py`; and bypassed admission hands the navigator to any
allocated slot, which picks the terrain fixture's blocker.

### Same-cell destinations

When the goal is in the requester's own footprint cell, Retail mode submits
the search as `4e54e0` does; see "Same-cell destinations submit (Retail mode)" below.
`fuzz_world_search.py --same-cell` is the differential.

### Grade planes, the pending count and phase-2 composition

- The tracer's reverse march does not run over a recentering local-window
  grade system. Every grade plane is
  map-sized: `4e098e` passes `[game+0x19e98]`/`[game+0x19e9c]` to `4dfdd0`,
  which stores width/height at `+0x340/+0x344` and allocates the packed plane;
  `4139d0` indexes it as `width*(z>>3)+x`. `+4/+6` of the grade object are the
  footprint used by the unexplored-cell cross around the search start
  (`413a16..413a46`), which is what looks like a local window in a harness.
- `0x634674` is not cleared per frame: `4e5080` runs only when the search
  singleton is created (`4e6088`) or destroyed (`4e60c0`). It is a live count of
  queued navigators, incremented/decremented by `4e4f50` and decremented on
  delivery (`4e4f17`), exactly like `PathService::pendingByPlayer_`.
- Phase-2 composition (`416668..4166a6`): phase 2 runs only while the heap is
  non-empty and `+0x191 < +0xec`; each pop charges 10, arrival 50 more, and the
  fan is `2 + (retry > 0)` after the first pop; otherwise `4166f1` retries up to
  three times and then runs the 9x9 diagnostic scan. `RetailCostSearch::runSlice`
  and `RetailSearchWorker::dispatch` match this and the fuzzers above exercise it.

## Scope of the port

The scope is retail-compatible surface navigation and valid map occupancy,
including mountain boundaries, ramps, boats, and both standard and Crusades
balance inputs. The navigation layer passes the retail comparisons below. This
is a tested scope statement, not proof of identical behavior on every possible
map or a claim of whole-game retail parity. AI strategy may differ, by
agreement. Terrain-art occlusion and renderer ordering are separate from
whether a unit can occupy a map position.

### Retail group pacing

Retail mode (PathfindingMode 0) paces groups with retail's native system.
Legion keeps the engine's own `squad < 0` pacing in `World::tick` (slowest
`baseSpeed`, 48 px catch-up, 140 px rejoin), none of whose constants appear in
the icd. Ported, static reading (details below the mapping):

- `51b890` -> `World::tickRetailGroups` (`src/sim/sim.cpp`): per player and
  group 1..99, counts, footprint-area sums and integer centres for ground,
  boat and flyer members, "all" and "moving" (`0x1000000` without
  `0x4000000`); ground/boat speed = minimum terrain-scaled TYPE maximum over
  members on any `0x1000000` mission (`51bdbc` tests only `0x1000000`;
  `0x4000000` excludes members from the moving sums only);
  centres of an emptied class are retained; navigator formation bit from the
  current mission's `0x2000000`; straggler pass `51c3bf..51c5fb`
  (`retailGroupStragglerSpeed`).
- `51c700` / `51ce40` / `51d1e0` / `535e90` -> `retailGroupCentre`,
  `retailGroupRadius`, `retailGroupOutOfSlot`, `retailOctDistance`
  (`src/sim/retailgroup.h`). The "slot" is the class centre, not a per-member
  slot: ground/boat use their own class (moving subset when asked and
  non-empty, optionally counting the member itself; the ground self-inclusive
  form copies x into y, `51cdcd`); a flyer uses the nearer surface group plus
  itself. The test is `(octdist/16)^2/4 > area * level-scale` (levels 1..5 =
  1/4,1/2,1,2,4; minimum 2; flyers doubled).
- `4d95f0` cap -> `retailGroupSpeedCap` + `World::retailGroupLimit`; the
  capped value replaces the speed-mode-scaled maximum in `steerGround` and
  `brakeGround` (`retailGroundPitchCap` still applies). Owner kinds are not
  distinguished (all players treated as `+0xea` 1/2).
- `402b00` head (two group checks) and `402880` Move_Ground_Formation ->
  `World::tickGroundMission` host + `retailGroundFormation`
  (`src/sim/retailmission.h`). Registry flags (descriptor `+0x11`):
  Move_Ground `0x3000400`, Move_Ground_Formation `0x11000400`, built with no
  unit/goal so `0x11000000` -- no `0x2000000`, so a re-forming member is not
  paced. Check 1: `51d1e0(unit,1,3)`; a member nearer the goal than the moving
  centre sets `0x8000000`, and on a repeat draws `rand(4)==0` to call `50c480`
  (remove itself from the moving aggregate) and `4d6a50(unit,0)` (cancel its
  orders); otherwise level -2 (walk to the moving centre, circle radius 4).
  Check 2: `51d1e0(unit,0,4)` -> level 4 (cancel the controller, wait). The
  formation mission retires (5) once back in slot; stages 0..3 as `402880`;
  the stage-3 target scan (`40296c`) runs only for a non-zero response mode
  (plain moves have mode 0; ported, see below).
- Groups: the console's `CreateSquad N` (`48e170` -> `522170` -> `50b4f0` at
  `5221ce`) rebuilds group N from the selection; a plain selection is never
  grouped, and issuing orders creates no group. Record `+8` is the FORMATION
  flag, written only by `50b700`: `CreateSquad N` clears it (a plain group),
  `CreateSquad N f` sets it (a formation). There is one record per number, so
  either replaces the other, and there is no append form. `51b890` aggregates
  and activates (`+0x2c`) only `+8` groups with members, and every reader of
  group pacing (`51c700`, `51d1e0`, `4d95f0`, the moving aggregate) needs the
  group active (`50b800`): a plain group behaves as no group at all
  (emulated, E). Ours: a `+N` Ctrl group is plain group N and never paced; a
  `-N` formation is group N with `+8` (`retailGroupSlot`).
- Computer players: the retail AI's squads are the player's group records
  (`50b4f0` sets `unit+0xc8`, its set-active `50b700` sets `+8`). A strike
  squad is a formation while it attacks (state 2) and a VTOL squad while it
  has units (S, the thinks `40e180` / `40eb40`), so their members are paced
  as a human formation's are, the speed cap included (owner kind 2, below).
  The server's classic AI tells the sims those squads with `Cmd::AiGroup`
  (`ClassicBrain::publishGroups`, `World::setAiGroup`, `Unit::aiGroup`).

Also ported: the changed flag (record `+0xb0`, set by
`50b7a0`/`50bbc0`/`50bfe0` when the value differs) and its notification
`51c612..51c6e1` as `retailGroupNotify` (the World does not consume the
navigator bit it sets, `4e60f0` `nav+0x114|8`); `50c480` as
`retailGroupLeave`; the `402b00` head as `retailGroundGroupCheck`; the same
head in Patrol (`403600`, checks at `40361c`/`4037e9`, pushes at `4037ad`/`40383b`,
the same structure; E: `check_retail_group.py` `patrol`, 2,000
heads against `retailGroundGroupCheck`, all agree), so
grouped patrols re-form too. Owner `+0xea` 3 uses navigator `vt+0x3c`
instead of the cap (zero for ground navigators), but kind 3 is a REMOTE
player, whose units its own machine moves: a computer player is kind 2 on the
machine that runs it (the battle room `49d067`, the mission loader `4a5524`,
the campaign start `4d298f`, S), and `4d978f` caps kinds 1 and 2
(`probe_formation_speed.py`, E). Lockstep runs every seat as its own machine
does, so every owner, computer players included, takes the cap
(`retail_group_test` `aiSquads`).

**Differential check.** `tools/re/check_retail_group.py` loads randomized
group records into the emulated binary through retail's own setters
(`50b840`..`50c400`) and compares with the engine's helpers, via a small
driver (`tools/re/retail_group_driver.cpp`) built from `retailgroup.h` and
`retailmission.h`: 400 `51b890` ticks (records, navigator formation bits,
straggler slowdown, change notifications incl. `game+0x3070` bit 0 and owner
kind), 4,000 `51c700`/`51ce40`/`51d1e0` queries, 2,000 `402b00` heads
(reform/wait/cancel, `0x8000000`, RNG draws, the `50c480` record update)
and 2,000 `402880` calls over all four stages (return code, stage, stop /
goal reset / centre / sleep). All PASS.

**Straggler convergence.** `402880` stage 1 writes the new centre to the
mission point (`+0x22`), and `4d4da0` resets the controller to it. The World
host therefore retargets `goal.missionTarget` as well as `goal.x/z`, because
the controller reset (`resetGroundSegment`, `retainGroundRoute`) reads
`missionTarget`; otherwise every later cycle steers back to the first cycle's
centre. `retail_group_test` `straggler` completes (tick 3,703) with the
straggler re-forming and its mission retiring.

Move_Ground_Formation's stage-3 target scan (`40296c`, attack via
`4d8370`/`4de530`) is ported (`retail-engine.md`, "The hosts"). It runs only
when the parent carries a response mode; human moves and fight-moves have
mode 0. The other construction sites are Attack_Chase (`403e78`),
Attack_Melee (`4048e1`), Suppress (`404ead`, ported) and Follow_Ground
(`4080a0`, ported, mode 0). The formation goal's y terms are zero in the `402b00` comparison
(the helper takes them; the host passes 0). Mission flags of orders the
ground host does not model read as 0, but a walker's guard and attack on a
unit carry their rows' flags (below). The classic AI's
strike and VTOL squads are group members (`Unit::aiGroup`); the restored
retail AI of the save-replay probes (`players_[].retailAi`) is not. The human UI's click offsets
are retail's `0x5214b0` (`retail-engine.md` "Group move spread"): flyers keep theirs, ground units take the point; ours keeps a
`+-60 px` ground offset by choice. Ctrl+N group assignment
is `522170` (`50b4f0` at `5221ce`), and a plain selection is never grouped.

**Flyers in a group.** The mission registry
row puts the handler at `+4` and the name at `+0x15`. Read that way:
- VTOL_Move: flags `0x1000402`, handler `417c20`;
- VTOL_Move_Formation: flags `0x1000402`, handler `417750`.

Neither carries `0x2000000`, so no flyer is ever paced. `51b890` computes no
flyer speed either. VTOL_Move's head `417e02..418052` matches `402b00`
instruction for instruction:
- `51d1e0(unit,1,3)`, then a goal-versus-centre comparison on the three
  16.16 components;
- the `0x8000000` flag, then `rand(4)==0` -> `50c480` + `4d6a50(unit,0)`;
- otherwise VTOL_Move_Formation at level -2. Failing that, `51d1e0(unit,0,4)`
  -> level 4.

For a flyer, `51c700` is the nearer surface group's centre, with the flyer
counted in. The radius doubles.

VTOL_Move_Formation differs from the ground mission in several ways:
- Both levels fly. There is no stop for level > 0.
- Stage 0 takes the centre (`51c700(unit, flags>>26 & 1, level<=0)`) and calls
  `416c50`.
- Stage 1 snaps the point to the footprint grid, installs a 16 px point
  controller (`4e40e0`, `4e4540(0x10)`), sleeps `rand(10)+5` and returns 0,
  so every wake re-takes the centre.
- It retires once back inside the level's radius.

Ported as `retailFlightFormation` and as the head in `World::tickFlightPatrol`,
which reuses `retailGroundGroupCheck` (with y = 0, as on the ground).
`retailMissionFlags` reports flight move and patrol missions, so flyers
enter the moving aggregate as `0x1000000` missions do.

`check_retail_group.py` emulates both natively:
- `vtolcheck` (2,000 heads, 3-D positions with flyer altitudes): all agree,
  including reform, wait, cancel and the `50c480` record update;
- `vtolform` (2,000 calls, every stage and level): all agree on result,
  stage, centre, calls and the 16 px radius.

In `legion_world_test mixedformation` (Retail move), flyers average 511 px
from the ground centroid (872 without the flyer formation). Five of the eight
end more than 300 px from the group: the cancel path drops their orders on the
way, as in retail.

**Every flyer mission's group check.**
The `51d1e0` call sites by mission (registry rows read from `.rdata`, S):

| mission (handler) | site | check | out of the slot |
|---|---|---|---|
| VTOL_Move (`417c20`) | `417e02` | two, as `402b00` | VTOL_Move_Formation -2 / 4, or cancel |
| VTOL_Patrol (`419db0`) | `419f9b` | two, as `402b00` | the same |
| AirToGround, AirStrike, AirToGroundHover | `41c19a`, `41b7a4`, `41dafa` | `(unit, 1, 5)` | VTOL_Move_Formation -4, or leave (8) |
| AirToAir (`41ca50`), VTOL_Evade (`41e3d0`), VTOL_SeekAttack (`4192f0`) | `41ccde`, `41e55a`, `4194f9` | `(unit, 1, 5)` | the same |
| VTOL_Follow (`418860`), VTOL_SeekGuard (`4197d0`) | `418a7b`, `4199b2` | `(unit, 1, 4)` | VTOL_Move_Formation -3, or let go (2) |
| VTOL_Standby (`417350`) | `417527` | `(unit, 1, 2)` | VTOL_Move_FormationStandby -1, 0 |
| VTOL_LandIfCan (`416cd0`) | `416eda` | `(unit, 1, 3)` | VTOL_Move_FormationStandby -2, 5 |
| Standby (`407770`) | `407779` | `(unit, 1, 2)` | Move_Ground_FormationStandby -1, mode -1, 0 |

All are ported and emulated:
- `check_retail_group.py` `vtolpatrol` (2,000 VTOL_Patrol heads against
  `retailGroundGroupCheck`, stopped at `41a200` as VTOL_Move's at `418052`),
  `standby` (2,400 heads of the three standbys, each run from its entry past
  its exits, against `retailStandbyGroupCheck`) and `vtolform` (with the
  FormationStandby flags);
- `check_air_to_air.py` (AirToAir and VTOL_Evade), `check_air_seek_attack.py`
  (VTOL_SeekAttack) and `check_air_traffic.py` (VTOL_SeekGuard): the whole
  handlers with the check answered in or out of the slot, flagged or not, and
  the next mission a group mover or not. AirToAir, VTOL_Evade and
  VTOL_SeekAttack are AirToGround's check (`retailAirGroupCheck`); VTOL_Follow
  and VTOL_SeekGuard share `retailGuardGroupCheck` (flagged and out again,
  `rand(4) == 0` runs `50c480` and `4d6a50(unit, 0)`; in the slot `rand(10)`
  clears the flag).

`41e55f` is VTOL_Evade's call (not VTOL_SeekAttack's); VTOL_SeekAttack's is
`4194fe`.

VTOL_Patrol: a formation pushed by its check returns 0, so the patrol starts
again at stage 0 when it retires. Stage 0's `4d6b80` walks the list from the
head and finds the leg already flagged `0x4000`, so no second anchor goes in
and the patrol goes on along the same leg (`retail_group_test`
`flyerPatrol`).

The FormationStandby branch of `417750` (`41797e..417ad1`): back in its slot,
a mission with `0x4000000` stays while its group is active (`50b800`) and
has a moving member of any class (`50c140`, `50bd20`, `50b900`). It scans
(`4d8370`, then `4d7de0(unit, found, 0, 0, 1, 0)`: an attack clears the wait
mask and the stage, 3), else it loiters: a 64 px point controller
`(rand(20) + rand(10) + 10) x 16` px off along `heading - 0x800 +
rand(0x1000)`, waiting on `0x700`, 0. Otherwise it retires (5). Ported in
`retailFlightFormation`; `vtolform` checks result, stage, calls, the point
and the wait mask (E). Only VTOL_Standby and VTOL_LandIfCan push it (`4d4bf0`
of `60549c` at `417572` and `416f25`; S). VTOL_LandIfCan returns 5 after the
push, so the landing ends and the default VTOL_Standby follows the
formation; a standby waits under it (0).

The formations as the checks build them (`4d7750` with no target and no
point: the ctor clears `0x200` and `0x400`, `4d6d66`/`4d6d6f`):
VTOL_Move_Formation `0x1000002`, VTOL_Move_FormationStandby `0x5000002`,
Move_Ground_FormationStandby `0x15000000` (`check_air_point_mission.py`, E).

A flyer's move is VTOL_Move (`0x1000402`, row `5ebcd3`), not Move_Ground's
`0x3000400`: a click, a fight-move leg, a rally step and the walk back after a
response attack (`4186f5`, `4de530` action 2). Without `0x2000000` its
navigator never gets the formation bit, as in retail. The classic AI's
`firstMission` reports it (and VTOL_Follow's `0x1000200` for a flyer's guard);
no AI reader looks at these bits. A flyer's guard and AirToAir count in the
moving aggregate (`retailMissionFlags`).

**Follow_Ground's group check.**
Follow_Ground (`407f90`) runs VTOL_Follow's check at the head of every
dispatch, after its exits (ward gone or dying: 5; `+0xa8`: 7; a flyer: 8) and
only for a unit with a mover (`40800c..40810e`): `51d1e0(unit, 1, 4)`;
flagged and out again, `rand(4) == 0` runs `50c480` and `4d6a50(unit, 0)`
(2); otherwise out of the slot it flags the mission and pushes
Move_Ground_Formation (row `604d70`) at level -3 with response mode 0 (0);
in the slot `rand(10)` clears the flag. Emulated: `check_retail_group.py`
`follow` (4,000 heads against `retailGuardGroupCheck`, E). Ported in
`World::tickGuardHelp` (`pushGuardFormation`). A dispatch there is a look:
the look's sleep (`15 + rand(15)`, `4087d2`) or the ward's event 0x10. World
does not wake the look on the ring controller's events (0x700);
a guard that is no builder takes the looks' sleep only while it is in a
group record (its look is then only the check).

The rows (`.rdata`, flags at `+0x11`, S): Follow_Ground `0x3000200`,
Attack_Chase and Attack_Melee `0x1000280`. `retailOrderFlags` gives the
orders these flags (`World::retailGroundUnitMission`), so a grouped walker
guarding or attacking a unit is a moving member of its group (`51b890`'s
`0x1000000`) and a guard is paced (`0x2000000`); `retail_group_test`
`groundGuard`.

The transport missions too (rows read the same way, S): a
passenger's Move_Seek_Pickup (`5eb886`) and its carrier's Ground_Pickup
(`5eb967`) or VTOL_Pickup (`5ebd05`) are `0x1000200`, Ground_Unload
(`5eb980`, whose circle approach World keeps as its own order) and
VTOL_Unload (`5ebd1e`) `0x1000400`, so a formation's member walking to board,
its carrier going to fetch it, or a carrier going to unload is a moving
member. None of them runs a group check (no `51d1e0` call in their handlers).
`retailOrderFlags` gives them; `retail_group_test` `transports`. Flagless:
Facing (`5eba2f`,
`0x1000000`), which Follow_Ground pushes (`408757`) and World does not
model as a mission.

Not ported, the ground attacks' check, which this World runs inside its
combat code rather than as retail handlers: Attack_Chase (`403dce`:
`51d1e0(unit, 1, 5)`, `rand(3)`, `rand(15)`, Move_Ground_Formation at -4
with response mode 1 for an automatic attack with a leash, -1 for one
without, 0 for an order: `403e39..403e56`) and Attack_Melee (`404837`: the
same with `rand(2)`) (S). Their head runs on every dispatch, and World has
no dispatch cadence for these missions: Attack_Chase's approach waits on
its controller's events in sub-states World does not model (the same gap
as its retarget `404075`).

**The attacks' single check.**
SUPPRESS (`404e12`) and the flyer attacks AirToGround (`41c19f`), AirStrike
(`41b7a4`) and AirToGroundHover (`41daff`) run one check at their head, on
every handler call: `51d1e0(unit, 1, 4)` for SUPPRESS, `(unit, 1, 5)` for the
flyers. Out of the slot on a mission already flagged `0x8000000`,
`rand(3) == 0` (SUPPRESS) or `rand(2) == 0` (flyers) gives the order up (8),
`50c480` first unless the next mission moves with the group. Otherwise out of
the slot it flags the mission and pushes Move_Ground_Formation at level -3
(response mode 0) or VTOL_Move_Formation at level -4 and returns 0, so the
attack starts again at stage 0 when the formation retires; in the slot,
`rand(15)` / `rand(25)` clears the flag. Emulated (`check_retail_group.py`
suppress mode, `check_air_attack_run.py`, `check_air_traffic.py`) and ported
as `retailAttackGroupCheck`. These missions are group movers (SUPPRESS
`0x1000410`, AirToGround and AirToGroundHover `0x1000200`, AirStrike
`0x1000600`): `retailMissionFlags` reports them, SUPPRESS's approach leg as
SUPPRESS, so they count in the moving aggregate of a formation. The
formation leaves the attack's weapon slots as they are, and a SUPPRESS walker
fires at its point on the way back; docs/retail-engine.md ("SUPPRESS port")
has the details. Legion has no group record. Standby's check
(`407779`) has a Legion counterpart of our own: at the
same standby wakes, with the same `51d1e0` tolerance measured from the
member's own Legion slot, an idle ground member of a formation (Alt+N, or an
AI squad the AI flags) walks back to that slot, a member moved on its own
included (docs/legion-pathfinding.md, "Formation re-form").

Retail's group system, read statically and partly emulated:

- **Groups** are per-player records (99 groups × 0xc4 bytes) at `player+0x84`.
  A unit's group is `unit+0xc8`, assigned by `50b4f0`. That routine is called
  for unit creation and death, save restore, the AI squad code (`40ac53..40feae`)
  and the selection-to-group handler at `5221ce`. Record `+8` is the formation
  flag (`50b700`),
  `+0x2c` is "active" (`50b7a0`/`50b800`), `+0x58` is the ground
  group speed (`50bbc0`/`50bc20`), `+0x84` is the boat group speed
  (`50bfe0`/`50c040`), `+0xa4` is the centre (`50c340`) and `+0xb0` is a changed
  flag (`50c440`/`50c400`).
- **Passengers stay members.** The member filter
  (`51ba44..51ba8f`) tests alive (`0x1000000`), not dying (`0x1000`),
  finished (`+0x108`), a mover (`+8`) and the record, never the carrier
  `+0xa8` (E: `check_retail_group.py` carried, 400 ticks with passengers that
  have `+0xa8` and every `+0x114` bit set). A passenger therefore counts in
  "all" at its own `+0x68`, the attach point its mover commits from the
  carrier (`4dad30`). Its mission aboard is BeCarried: the attach (`51b4f0`
  -> `51b5a0`, `51b847..51b876`) calls `4d49f0` for an owner of kind 1 or 2
  whose carrier is no airbase (`isairbase`, type `+0x260 & 0x200`, which no
  shipped unit sets), which cancels the queue but for the flag-4 missions
  (`4d6a50(unit, 0)`) and puts `BECARRIED` in front (`4d7750`) (E:
  `check_retail_group.py` becarried). BeCarried's row (`5eb69f`) has handler
  `4024a0` and flags `0x24` (S): neither moving nor paced. The handler
  returns 5 once `+0xa8` is clear, else stage 0 calls `519b70(unit, 3)` and
  stage 1 sleeps 10; it never runs a group check (E). The unit update runs
  the dispatcher (`51de30` -> `4d8450`) for a carried unit too, with no
  `+0xa8` test on the way (S), so BeCarried is all a passenger runs: it
  neither pushes a formation move nor is paced. `tickRetailGroups` counts an
  embarked unit with BeCarried's flags (`kRetailBeCarriedFlags`) and runs no
  missions aboard. Test: `retail_group_test` carried.
- **Per-tick aggregation** happens in `51b890`, called from `526389` before
  `51d3e0` (movers). It runs for players whose `+0xea` is 1, 2 or 3. For each
  formation group (`+8`), it walks alive, completed units with a mover. Navigator
  `vt+0x30` sets formation bit `nav+0x114&0x10` exactly when the unit's current
  mission has flag `0x2000000`; ordinary point moves (`0x3000400`) have it. The
  routine sums centres per class: flyer (`type+0x260&0x800`), boat (`0x80000`
  with minimum depth > 0) and ground. Non-flyers on missions with `0x1000000`
  (but not `0x4000000`) contribute their terrain-scaled TYPE maximum
  (`type+0x162` × road/water multiplier) to a per-class minimum. The routine
  then writes centre, active flag and the two speeds (`51bfd6..51c39a`).
  Next comes a straggler pass (`51c3bf..51c5fb`). Each formation member that
  `51d1e0(unit,1,4)` reports out of its slot, with a type maximum below 1.5×
  the group speed, multiplies that speed by `0xaa7e/65536` (≈2/3). Several
  stragglers compound the reduction, so the group slows down; retail does not
  let the straggler sprint. When the changed flag is set
  (`51c612..51c6e1`, game `+0x3070&1`, owner kind 1/2), every member's
  navigator gets `vt+0x38`, which sets `+0x114|8`.
- **Slot geometry**: `51d1e0` compares the unit's distance from its slot
  (`51c700`) against a radius from `51ce40`, scaled by level 1/2/4/5 and doubled
  for flyers. The `Move_Ground_Formation` mission (handler `402880`) steers
  units to their `51c700` slots. It is constructed by name inside the
  `Move_Ground` handler (`402c99`) and in `4037ad`, `403e78`, `4048e1`,
  `404ead` and `4080a0`; the group checks above are what trigger it. This is
  retail's re-forming mechanism.
- **Mover cap** in `4d95f0` (`4d976a..4d9a2a`): it applies when navigator
  `vt+0x34` (formation bit) is set, owner `[0]` is nonzero, owner `+0xea` is
  1 or 2, and the group is active. Let F be `+0x84` for boats, otherwise
  `+0x58`. Owner kind 3 instead uses navigator `vt+0x3c`, which is 0 for ground
  navigators (`4e60d0`). For F > 0, the floor is
  `ftol(0.25 × terrain-scaled type max)`. If F stays below the mode-scaled
  terrain type max, the unit's maximum becomes
  `(ftol(ownT/typeT × 65536) × F) >> 16`. Here ownT is the terrain-scaled
  `unit+0x12b` and typeT is the terrain-scaled `type+0x162`. Notably the
  speed-mode factor (`0xaac0`/`0x553f`) is not applied to the capped value.
  `tools/re/probe_formation_speed.py` runs the original routine with a
  hook at `4d9a2d`. It matches this written rule in 20,000 randomized cases
  (3,543 capped), covering owner kinds, inactive groups, boats, road/water and
  speed modes.

How retail's pacing differs from the engine's own (Legion) rule: `51b890`
aggregates only a record with `+8` set, so a Ctrl group (retail's plain
"CreateSquad N") is never paced; only a formation is (E). Retail uses
terrain-scaled type speeds, not individual base speeds, with a 25% floor. It
slows the group for stragglers instead of speeding up the straggler. It
re-forms through slot standby missions. A faithful port needs `51b890`,
`51c700`/`51ce40` and the `402880` mission together; the mover cap alone would
drop the catch-up rule without retail's slowdown. The AI's group order
`50b5d0` gives every member the same point. The human UI's per-unit click
offsets are `0x5214b0`: only flyers keep an offset there (`retail-engine.md`
"Group move spread"); our `±60` px ground offset in `gameview_hud.cpp` is a
design choice.

### Whole-World Retail scenarios against native retail

`check_crowd_arrival.py --world-tick` drives World only through its public
entry points: `World::order` (queued legs with `queue=true`) and a full
`World::tick` in Retail mode, instead of the probe's hand-assembled mission /
mover / worker calls. The native side runs, per unit and in retail's order
(`0x51e1e5..0x51e21b`): active dispatcher `0x4d8450`, queued dispatcher
`0x4d85e0`, mover `0x4dc800`, height `0x51b2a0`; then scheduler `0x416430`
once per tick (`0x5263aa` runs units before `0x526411 -> 0x4f6ca0`). New
options: `--blocker X Z` (a Hunter ordered to its own position: a parked
obstacle), `--mover X Z GX GZ` (opposing traffic), `--then GX GZ` (a queued
second leg) and `--dump-map` (native 2x2 passability, for choosing routes).

Every scenario matches on all 24 movement/mission fields on every tick
(Ulasem capture, Crusades unless noted, 12,000 work budget):

| scenario | units | ticks | settled |
| --- | --- | --- | --- |
| single unit routed around cliffs, through arrival | 1 | 900 | 1/1 |
| group, 3-cell spacing | 10 | 600 | (en route) |
| group of 10 through the walled north-east passage | 10 | 1,500 | 10/10 |
| same, Standard balance, touching footprints | 10 | 1,200 | 10/10 |
| 60 touching through the same passage | 60 | 1,500 | 38/60 |
| 20 touching, one destination | 20 | 1,800 | 18/20 |
| destination occupied by a parked unit | 1+1 | 1,200 | stops 70 px short |
| parked unit directly on the route | 1+1 | 600 | detours, arrives |
| head-on pair | 1+1 | 600 | 2/2 |
| 8 north vs 6 south, crossing | 8+6 | 1,200 | 14/14 |
| 16 through the choke vs 4 opposing | 16+4 | 1,500 | 13/20 |
| 5 out and back on a queued leg | 5 | 1,200 | 5/5 |

The World side removes unit scripts (also from the id index), disables
wind and the player bookkeeping clock `0x411a90`: the native loop runs none of
those, yet all three draw from the shared stream. Statically, retail runs them
after the scheduler (`0x4f6c70` calls `0x416430` at `0x4f6ca0`, then
`0x4121c0` at `0x4f6da2`), which is World's order too; their interleaving with
movement draws is not covered by this comparison. Also not covered: the per-unit
updater's other calls (`0x401160`, `0x429d90`, weapons), combat, formations
(squads), AI orders and a seeded whole-game start, and no live run of the
retail executable on the same layouts.

### Retail audit: special terrain and domains

- **Gates and yards.** All controlled gate oracles pass (`check_gate_grade`
  1,800, `check_gate_activation` 3,136, `check_gate_active_bit` 512,
  `check_gate_capability` 216+36, `check_gate_owner` 10,800,
  `check_yard_transition` 192), as do `check_gate_lifecycle` for all four gates
  in both balances (8 × 2,600 boundaries). The composed traversal matrix
  `check_gate_search_movement.py --missions` passes 64/64 cases: four gates ×
  two balances × point, return trip, pair, close convoy, PARK, rectangle,
  replacement at tick 16 and changed-goal replacement at tick 400, so
  animated-gate traversal is verified.
- **Nonblocking features have binary evidence.** Loader
  `493920` parses `blocking` at `494108..49412d` into feature flag `+0x13c` bit
  `0x20`, default 0. Every movement consumer of a feature cell gates on that
  bit before the `0x20000` clearability test: raw grade `5088f0`
  (`508a7b`, `508ade`), the raters `508660` (`508739`, `5087a6`) and `509020`
  (`5090fd`, `50916a`), live grades `4dd780` (`4dd97f`, `4dd9f3`) and `4de530`
  (`4de6db`, `4de744`), and site placement `404d70` (`404ff9`..`40541c`). A
  `blocking=0` feature therefore never obstructs retail movement, matching
  `World::mapFeatureGrade` (`blocking ? (clearable ? 1 : 0) : 7`) and the
  overlay rule ("Nonblocking map features" below).
- **Standard zombie corpse.** `check_corpse_features.py --balance standard
  --synthesize-missing-corpses` closes the `tarzom_dead` gap. Retail resolves
  corpse names through `494480` → `493920`, which parses a startup TDF database
  the capture did not keep, so the harness appends a native feature record
  cloned from a loaded record with identical placement inputs (footprint
  `+0xb0/+0xb2`, height `+0x138`, sacredsite `+0x134`, flag bits read by
  `495360`/`496380`/`512ee0` and no replacement chain), renamed to the source
  definition. Only display pointers stay the donor's: a field-access trace of
  all 507 captured placements, whose replacements also run `496380`, reads
  only those inputs, decomposeTime (`+0x130`, also set from the source) and
  the model pointers (`+0x110`, `+0x11c`, passed to the display constructor
  `4ee290`). Late
  capture, standard: 507 placements (248 accepted, two more than Crusades,
  i.e. the TARZOM corpses), 507 retirements and 5,100 changed cells all match.
- **Shoreline routes with live exploration.** `check_surface_unload_map_route.py
  --native-exploration` runs retail's own sight producer: original
  `50e740`/`50ea58` builds the coarse height pairs and `4c6c00` runs for the
  carrier after every native step, as the sight batch `4f6a60` does. Each
  step's exploration plane must equal World's. The native sight cache starts
  from World's cached footprint because the fixture teleports the carrier.
  With `--native-map-grades --native-live-unload --terrain-scan-after 1`,
  Lake Lokken passes for all nine releasing carriers (Vertrans, VerScout,
  VerMan, Creiron, Arawar, Crester, NpcBotl, NpcRixx, VerHarp) and Sea Dragon
  Spine Arawar, in both balances (20 traces; for example Vertrans: 2,217 steps,
  550 scan deadlines, 1,800 newly revealed cells). Cairbray Coast Landing,
  whose forward scan reaches cells World explores during the trace (step
  129), matches every step of a 4,000-step trace with the native producer.
  Lake Lokken Aratrans with the live scan likewise matches 4,000 steps. Neither
  of those two releases its passenger within the trace, because the fixture
  disables the path service after the first route.
- **Controlled ramp routes.** `check_gate_search_movement.py --missions
  --ramp` gives both sides the same 64×64 relief beyond the gate. A lane at
  x=24..39 climbs 10 height units per cell for five rows (above GROUND4/5's
  soft slope 7, so grade 4) and then 5 per cell for three rows. Cliffs climbing
  22 per cell (above the hard limit 15) flank it. Native cells carry the
  heights and quad extrema, and `4e01d0` builds the cached grades from them;
  World reads the same bytes through `TAK_GATE_HEIGHTS`. All 24 cases pass:
  four gates × two balances × point (2,400 ticks), return trip downhill (4,000)
  and pair (3,000). Each case climbs to height 64–65 through 46–91 distinct
  pitches, with position, height, pitch/roll, speed, missions, search state,
  RNG and gate state compared every tick. Adding 3 to one ramp row on the
  World side only fails at tick 441 (height and pitch), so the comparison is
  sensitive to the relief.
- **Also matching.** `check_ground_scan` (10,000),
  `check_ground_corners` (8,192), `check_world_search.py --terrain
  --exploration all --motion both` (1,440 searches, 86,948 ticks, including
  ramps and cliffs), `check_exploration` (19,456), `check_landing_search`
  (1,024), `check_landing_mission` (2,048), `check_flight_motion` (4,000),
  `check_flight_patrol` (4,000), `check_hover_attack` (4,096),
  `check_air_collision_grid` (1,024), `check_animation_flight_attitude`
  (8,192), and the air traces `check_air_flight_motion_trace` (600 and 128
  unload ticks), `check_air_unload_heightstep` (480), `check_air_map_height_scan`
  (480) and `check_air_unload_integrated_trace` (500).

Related:

- **Embarked units' sight**: see "Retail audit: embarked cargo sight" below.
- **VTOL landing-site predicate `509400`**: see "Retail audit: flyer landing
  site and airborne occupancy" above. For every shipped flyer the water and
  slope limits are effectively unlimited: maxwaterdepth is 0 or the default
  10000, minwaterdepth is -10000 and maxslope is 255, as the captured types
  confirm; only the `canfly` non-`floater` raise to `sea` bites.
- **TARCAN bobbing clock.** `51af48` reads `53ff20`: elapsed
  `QueryPerformanceCounter` seconds × 1000 × `[[0x641a48]+0x48]` / 1000.
  Both captures store 30 there, so retail's phase advances 30 per wall-clock
  second, the nominal tick rate. World supplies the simulation tick. That rate
  matches at normal speed, but retail's phase is wall-clock, unsynchronized
  between peers, and cannot be reproduced exactly.

### Same-cell destinations submit (Retail mode)

Retail's `setDestination` `4e54e0` cancels the old work (`415f30`), installs
the controller (`4e4de0`), clears the navigator's active bit and, whenever a
controller exists, marks the navigator pending (`4e5540 push 1; call 4e4f50`).
There is no distance or same-cell test. When the scheduler `416430` reaches
such a request, `415170` accepts the start immediately: it charges 500 work
(`retailtrace.h`, `Result::Complete, 500, 0x1000`) and notifies 0x1000; `4e4ea0`
delivers no points, so the navigator becomes inactive, the stored points are
kept, and 0x200 is raised only when the controller does not accept the unit
(`4e4ead..4e4ed4`). In Retail mode (`PathfindingMode::Retail`)
`World::requestPath` submits such requests; Legion's Retail fallback cancels
them.

What follows, established by emulating the dispatch path:

- Per-unit order is `4d8450`, `4d85e0`, mover `4dc800`, height `51b2a0`
  (`51e1e5..51e21b`). The scheduler runs afterwards (`5263aa` -> `51d3e0`, then
  `526411` -> `4f6c70` -> `416430` at `4f6ca0`). `4dc800` calls the navigator
  tick (vtable +8 = `4e5150`) unconditionally at `4dc869`.
- `4e5150` checks the controller before anything else (`4e5154..4e5186`). A
  satisfied controller notifies 0x100. Circle, ring and rectangle controllers
  return 0 from vtable +0x2c, so `4e5186` calls `4e54e0(0)`. That detaches the
  controller and its `4e4f50(0)` removes the pending request (0x400).
- A circle controller compares footprint origins with the rounded radius
  (`retailCircleRadiusSquared(4)` is 0 cells). Two points in the same
  footprint cell share the origin, so a same-cell circle goal is always
  accepted. The submission is therefore withdrawn in the same unit update,
  before `416430` runs. No work is charged, and the Move_Ground mission
  (`retailGroundMove` stage 2) retires on 0x100 with unchanged timing.
- The submission reaches the scheduler only when the controller does not
  accept the unit: for example a ring whose inner radius excludes the unit's
  cell, or the retry ladder (`4e5268`) or a delayed admission (`4e54a0`) moving
  the start onto the goal cell. In that case the 500-work charge, 0x1000, the
  inactive navigator and the 0x200 failure apply. 0x200 is in the Move_Ground
  wait mask 0x2700, so the mission widens its radius by `footX*32` and
  re-dispatches (`retailmission.h` stage 2). World ports each of these steps.

Evidence:

- `fuzz_world_search.py build/retail_trace_test --cases 1500 --seed 11 --same-cell`
  compares World request -> PathService -> delivery against `416430`/`415170`/
  `4e4ea0` for every tick. Same-cell goals run by default (`--skip-same-cell`
  excludes them; 58 of 1,500 cases at seed 11 are same-cell). 1,500 searches
  over 55,153 ticks, 0 mismatches.
- `check_crowd_arrival.py --world-tick` (full `World::tick` against native
  `4d8450`/`4d85e0`/`4dc800`/`51b2a0`/`416430`). Two cases match on every field
  and every tick: Hunters ordered 0, 4 and 7 px inside their own cell (`--blocker`,
  `--mover 2048 1616 2052 1620`, `--mover 2608 2608 2615 2615`), and 10 Hunters
  converging on a same-cell blocker. `TAK_PATHLOG` shows World queueing these
  requests and withdrawing them on arrival.
- `retail_trace_test --selftest` (ctest `retail_trace`): a Retail same-cell
  request is pending; it completes in its admitting tick with exactly one
  0x1000 for its controller, no points, an inactive navigator, the stored
  point retained, no 0x200, and the admission stamp written. A full
  `World::order` + `World::tick` same-cell move retires on the same tick, and
  nothing is left pending.

Accepted same-cell submissions are withdrawn before the scheduler runs, so
they leave crowdbench and navigation-checkpoint hashes alone; Retail hashes
depend on the rule only where an unaccepted same-cell submission reaches
`416430`, such as a ring goal or a retry or admission that lands on the goal
cell.

### Nonblocking map features

The shared movement obstacle overlay blocks map features only when their
shipped definition sets `blocking=1` (while preserving the clear Sacred Stone
centre). Decorative wave art such as Lake Lokken's `TarWave05` is explicitly
`blocking=0` and does not close water-route cells, so a Vertrans placed after
unloading is never on a footprint its water navigation grid marks blocked.
The data-backed `navblock` test
checks that Lake Lokken's TarWave05 cells stay out of the shared overlay, and
the real-profile Vertrans/Araarch pickup-and-unload test checks that the carrier
footprint remains passable in its water grid through the post-release coast.

This overlay rule follows the retail feature definitions and map behavior; it
is not a claim of full live-map route parity.

### AI queries, standing orders, and flying construction

The skirmish AI's terrain-use, reachability and naval-water checks are
read-only placement/connectivity queries that issue ordinary commands; they
change no path search, surface movement, or occupancy rule.

Unit creation retains the type's default move/fire standing orders in both
balances. Changing stance retires an automatic engagement immediately while
preserving explicit player attacks.

Live flying conjurers choose nearby hover destinations using the observed
`41ef00` angle/radius perturbations and the flight controller. Both
placed and queued production use the active site's position. The render hook
keeps StartBuilding active during these movements; the shipped Zhon script
continues its wing flap and arm gesture. Regression checks cover movement around
the site, working distance, facing, and repeated wing/arm poses in both balances.
This ports the hover behavior, not the entire retail construction mission
scheduler or a claim of identical frame-by-frame trajectories.

Retail COB GET 27 returns the raw native heading (`50ceb0`, case at `50d255`),
and both the simulation and the renderer report native heading for it.
Aramon's Keep uses `32768 - GET 27` to counter-rotate its build pad, while the
Smithy relies on its birth orientation. Placed builds keep the retail
birth-orientation rule (not a forced pi); there is no factory-specific
rotation or occupancy exemption. The renderer uses building birth heading too,
including effects, aiming and selection bounds (`4ee620`/`4ee9a0` apply the
entity pose to the native visual model). Real construction and output tests
cover Keep, Smithy, Taros factory, and Veruna factory in both balances. Direct
emulation checks GET 27 at eight headings including wrap edges, and 10,030
native integer-rotation comparisons match.

### Placed flying conjurers keep their build distance

Flying builders do not use the ground footprint rectangle (that would stop them
roughly 40 map units away, with altitude lifting the builder far above the
target on screen). Flying placed-build orders approach a hover point at the
builder's nominal FBI `builddistance` from the site centre, facing inward.
Ground approach is rectangle-based. The point is computed when the order becomes active, including
when the builder starts inside its working distance. For `zonhunt`, this is 100
units. Captured retail flying construction (`41ef00`, unit 1229 in the early and
late saves) works approximately 100 units from its target; its orbit routine
chooses `builddistance - random(8) + random(8)`; the hover destinations above
apply that orbit (see "Flying-builder orbit and movement rate" below).

`conjure_test` checks all eight compass directions and two valid close
placements in both balances, including accepted placement, distance, correct
heading, and stopped movement while construction remains active, plus the
queued-production and real-COB arm-animation checks.

A Debug-only rendered reproduction is available on Ulasem Arena:
`TAK_CONJURE_TEST=1 TAK_SHOT_MS=500 build-dbg/takclient game "ulasem arena" --data assets/game --crusades --testbuild --time 6 --shot /tmp/tak-conjure.png`.
It disables edge scrolling and advances the animation with the initial test
simulation so a headless capture includes the settled airborne conjure pose.
The apparent vertical separation due to altitude is intentional.

### Queued conjurers face their output

Queued mobile production uses the same facing logic as placed construction:
stationary mobile producers turn toward the actual production site's position
at their pivot rate, including an output position moved by the exit-placement
search. Structures keep their fixed heading, and an active movement order keeps
control of a moving producer's facing. Without this a stationary Zhon monarch
keeps her previous heading through the conjure animation (14,325 BAM of error
in the displaced-output fixture).

`production_test` checks the ground/flying queue-facing path. `conjure_test`
loads the actual `zonhunt` and `zonter` definitions in both balances, starts at
four cardinal headings, and checks correct facing and unchanged position while
the output remains under construction. Its real COB tests verify that the
conjuring arm gesture runs while active.

### Dense Hunter crowd walking animation

The case: approximately 450 Zhon Hunters (`zonter`) on Ulasem Arena with
Crusades balance, near a shared destination after other members arrived. The
movement controller and the animation input have different retail contracts:
collision refusal retains controller speed for later attempts, but original COB
GET 29 (`50ceb0` -> `4dc100`) returns **zero** when mover flag `0x04`
indicates repeated refusal. It also returns zero for an attached unit.

The render snapshot captures repeated ground refusal, reports zero animation
speed while refused/attached, and uses that value for GET 29 and the fallback
walk/MoveRate paths, so refused units do not walk in place. Successful movement
clears refusal and allows animation to resume. This is shared by ground movers,
including boats. It is display only: controller speed, orders, searches and
simulation hashes do not depend on it.

`check_crowd_arrival.py` constructs 450 Hunters using the captured native Hunter
type/mover and Ulasem terrain/features. Original bodies are removed; initial
positions and one destination are identical on both sides. Each implementation
advances its own mission dispatch, occupancy, search worker, movement, height
and RNG; no resulting native state is fed into World. Combat, formation, AI
strategy and animation scripts are excluded from this movement comparison.
Two Crusades cases pass 1,800 ticks each (60 simulated seconds), at the normal
12,000-work-unit budget: footprints initially touching, and one-cell gaps.
All 1,620,000 unit snapshots match across 24 movement/mission fields. The cases
include 87/110 units stationary with repeated refusal within 384 pixels of the
destination after another unit arrived. These deliberately congested tests
settle 103/117 units within the measured interval; they do not assert that the
whole crowd clears or reproduce one particular initial arrangement.

The animation contract is checked separately: `check_movement_animation.py`
compares 120 controlled cases with the original GET 29 in each build, including
first/repeated refusal, attachment and four headings. The real shipped Hunter
`MoveWatcher` script polls GET 29 every 100 ms and gates walking above 5%.
`cobanim_test --hunter-movement <zonter.cob>` verifies that it stops after repeated
refusal and resumes without restarting the script. No game asset is in the
repository.

Legion (not retail) adds its own stand on top of the refusal bit
(`World::updateLegionStill`): a ground body without NET headway over 8 ticks
reads GET 29 = 0 and requests no `TurnDirection`, and a body walking or
turning to and fro is held by its navigator; see docs/legion-pathfinding.md.

Example reproduction (local retail capture/assets required):

```sh
python3 tools/re/check_crowd_arrival.py \
  assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/reload-crt-state-01.json \
  --save assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/test.tak \
  --balance crusades --count 450 --rounds 1800 --spacing 3 \
  --output /tmp/tak-crowd-arrival
python3 tools/re/check_movement_animation.py
```

| Requirement | Completion evidence |
| --- | --- |
| Valid footprints, mountain boundaries (including north of steep terrain), ramps, land and water movement classes | 240,000 raw/live grade comparisons across Debug/Release and both balances |
| Composed terrain search and route delivery | 1,440 searches totaling 86,948 ticks per build; land/boat, cliffs, ramps, occupancy and full/hidden/striped exploration; ordered queries, notifications, flags and navigator points match |
| Surface position and pose, including boats | 15,000 native height/pitch/roll comparisons per build, including floaters, hover, support and bobbing |
| Shared worker and moving occupancy | Captured 87-unit mixed land/hover/water comparisons across both builds and balances, including 1,200-tick runs with a constrained worker budget and 500-tick runs at the normal budget |
| Mission integration | Queued/replaced moves, gate traversal, ring/PARK arrival and rectangle/build approaches; the 64 PARK cases and 48 rectangle cases pass |
| Deterministic integration | Debug/Release CTests; the Python tests under `tools/re`; GCC/Clang golden hash; seed-1 multiplayer runs agree |

Probe format 37 carries the explored-map masks, viewer and per-unit sight
caches, restored from captured memory as initial inputs; no later movement
results are fed back into World. Without them World assigns permissive
unexplored search grades where retail already knows the terrain, changing the
route and later RNG consumption (the tick-10071 replay divergence of format
36). A regression checks owner masks, viewer, sight caches and rejection of
missing mask memory. The controlled movement checker accepts formats 34, 36
and 37 without duplicating height or exploration payloads.

Native tick-body replay and World match for 317 natural
production boundaries (3,718 gameplay and 754 CRT draws) and 314 accelerated
factory-completion boundaries (4,013 gameplay and 627 CRT draws), in both
builds. Comparisons include unit motion, construction, resources, factory queues
and squad state. The outer live renderer consumes CRT draws absent from
tick-body replay; whole live-capture RNG/allocation parity is outside the scope
of the navigation port.

Reproducible checks live in `tools/re/check_world_grade.py`,
`check_world_search.py --terrain`, `check_world_height.py`,
`check_search_movement.py`, `check_gate_search_movement.py`,
`check_build_reach.py` and `check_factory_completion.py`. The sections below
document each ported piece and its native evidence.

## Rectangular build approaches through placement handoff

`--missions --rectangle` executes original MobileBuild dispatch and World’s
production `prepareBuildApproach` host through installation, search, movement,
perimeter arrival and the next dispatch’s placement handoff. The comparison
stops at original `405824`, before placement/allocation, rather than substituting
a successful construction result. The initial mission is supplied directly.
The mover’s `MaybeBuilding` script notification and out-of-reach UI notice are
explicit host boundaries; the gate’s actual script, occupancy and policy still
advance and are compared. This proves navigation through that handoff, not full
construction or command-decoder parity.

Ground build approaches retain the MobileBuild definition flags, own
controller identities, receive search notifications, wait on `0x700`, and
consume the triggering events before handing off. Rectangle controller
retirement preserves the same search timestamps as other ground controllers.
The production tick and probe use the same approach host, so the diagnostic
does not duplicate its transition logic.

Delivery of a partial route overwrites the stored steering endpoint, so
rectangle retries obtain the destination from their controller again (the
endpoint would make `requestPath` cancel as already in the target cell).
Empty deliveries notify unsatisfied rectangles of failure. If the remaining
edge-to-edge distance exceeds build reach, the mission is retired and enters
standby; otherwise it proceeds to placement, as retail does.

All 48 gate/balance/build cases pass (28,512 compared updates), covering normal
arrival, an unreachable perimeter, and the same blocked approach with enough
build reach to proceed. Each run compares full movement and mission state,
RNG, ordered grade queries, gate state, and the exact handoff/abort tick.
`check_build_reach.py` additionally checks 8,192 original-handler decisions per
build, concentrated around the reach threshold with fractional coordinates and
varying footprints. World regressions verify controller setup, partial-route
retries, failure notifications, both reach outcomes and retained recent stamps.

## Complete ground PARK sequences

`check_gate_search_movement.py --missions --park` constructs a native PARK
mission (kind 33) and the corresponding World order, then advances actual
mission dispatch, ring installation, asynchronous search, movement, arrival,
mission retirement and standby. `--park-target-loss-at` supplies event 8;
`--park-follow-at` appends a point move while PARK is still active. Both are
controlled input events, not AI or command-decoder parity claims. The initial
PARK order is constructed by the fixture; factory creation is separately
covered by the production construction regression.

PARK reads the gate fixture's world position and type footprint as well as
its occupancy rectangle, so the fixture supplies the same position/footprint
as World. Original `4d6c40` retains `0x200` on a PARK mission with a live
target, and factory-created World PARK orders retain that flag too; the
construction regression asserts it.

All 64 cases pass: four gate targets × two balances × two builds × four
sequences (ordinary completion, loss before initial dispatch, loss at tick 40,
and a queued point move at tick 40). Each completion/loss run lasts 1,200
updates, and queued moves last 1,800: 86,400 exact movement/mission comparisons,
plus ordered grade queries and gate script/occupancy state. Tests require ring
arrival and standby, or activation and arrival of the following point move.
They reject event times after PARK has already completed; linking behind an
already-created standby mission is not equivalent to queueing a move through
the game's command path.

The rectangle check above covers navigation through the placement handoff;
broader AI/render parity is outside the navigation port.

## Ring and build-area arrival

Original `4e5150` detaches all three controller shapes:
circle, ring and rectangle. Their satisfaction handlers share the zero-return
virtual query that selects detachment. `tools/re/check_goal_completion.py`
executes those handlers and navigator cleanup, checking both old and recent
search timestamps, retained stored points and notifications `0x100 | 0x400`.

World applies that cleanup to satisfied ring and rectangle goals and lets
stored waypoints advance before braking (no early braking return). Completed rectangles cannot enqueue
another search while waiting for construction dispatch. The build
mission handoff creates the construction site. World
regressions cover all three shapes, old/recent timestamps, endpoint consumption
and suppression of retry requests and RNG draws. Flying construction approaches
have their separate movement handling. Native goal queries and 8,192 PARK
cases match, plus the 87-mover 160-update regression in both balances.

## Concurrent captured land, hover and boat movers

`check_search_movement.py --also-unit ID` accepts multiple independently moving
requesters. All selected units move in allocation order before one shared native
worker update; unselected captured bodies remain stationary. Ordered grade-query
traces are asserted, alongside every movement/navigator state. The boat
exploration viewer is a single explicit shared fixture input, chosen from the
lowest selected slot's owner, rather than changing with the current requester.

The 32-case matrix includes single movers, same-owner boats, mixed movement
classes, different-player boats and all 87 captured surface movers. Every case
passes in Debug and Release with standard and Crusades data. All 87 movers
also pass 1,200 updates at budget 503 (417,600 unit comparisons), and 500 updates
at the normal 12,000 budget (174,000 comparisons), across both builds/balances.
The combined fixture spans six owners and seven movement classes, covering
mixed-player and mixed-boat composition. It does not assert that every request receives a route under the small budget,
or reproduce full AI, mission dispatch, combat or exploration-update cadence.

## Changed destinations and concurrent ground requests

`--replace-goal X Z` extends replacement checks to different destinations.
The six scenarios change goals to either side beyond the gate, or back to the
starting side, at ticks 16, 20, 21, 120, 400 and 650. All 96 gate/balance/build
cases pass for 1,800 ticks each, with exact ordered grade-query traces as well
as movement, missions and gate state. The admission-stamp rule of the next
section handles these route changes too.

`--missions --pair --rounds 2400` advances two independently moving 4×4
VERPULT requesters against one shared scheduler and movement-class cache.
Each unit's mission, movement and height update precedes the single shared
worker step, matching the production ordering. Requests must overlap and both
must receive native route deliveries. The leader crosses the gate; the second
unit follows a separate route to a clear destination on the approach side.
`--pair-close` instead starts a crowded convoy aimed through the same gate.
Native retries widen the follower's accepted goal radius, and its mission
finishes short of the requested point. The test requires that expanded-radius
arrival event and matches the entire trajectory, rather than forcing a more
successful result than retail produces. Depending on the gate, the follower
can stop before or after the passage.

World installs retail's projected boundary markers during feature setup, so
the native fixture invokes original `50eef0` with matching dimensions and
scenario inputs before cache initialization; movement snapshots can agree
despite unequal boundary grades, and the ordered query assertions catch that.
The separate-route destination lies north of every gate's wall so it is clear
for Creon's larger footprint.

This covers two same-owner, same-class ground requesters, including both
successful movement and congestion-driven goal expansion; different-player
and mixed movement-class/boat composition is in the captured multi-requester
matrix above. All 32 pair/congestion gate/balance/build cases pass for 2,400
ticks each (153,600 per-unit comparisons), alongside 128 single, queued and
changed-goal regression cases.

## Navigator timing for queued and replacement ground moves

Ground controller detachment preserves a very recent navigator admission
stamp and otherwise clears it to zero. Removing an already detached mission
keeps the stamp unchanged, including when it crosses the six-tick boundary
between arrival and mission removal. An unconditional `-1` sentinel would
suppress the new leg's retry cadence: in a queued gate traversal native
`402dc2` draws the next mission's delay (bound 5) at tick 648, then `4e5482`
draws a retry check (bound 120), and that second draw later sets search
timing.

`check_gate_search_movement.py --missions --return-trip --rounds 2400` starts
with two point missions, the second returning to the original side of the gate.
World uses normal queued orders; native initial missions are constructed and
linked as the supplied queue. Original dispatch and removal then run without
later queue or movement-state injection. All four gates, both balances and both
builds pass (16 cases, 38,400 ticks), including ordered grade queries, outbound
and return traversal, final gate closure and standby.

A replacement move also preserves a recent stamp instead of installing `-1`.
Native queue removal at tick 16 retains an admission stamp of 15; re-admission
must wait until tick 30. `--replace-at` compares original queue clearing and a new point mission
against normal World order replacement, with the same destination supplied at
an identical tick. Tests cover ticks 15, 16, 20, 21, 120 and 650, exercising both
sides of the six-tick preservation boundary, active movement and arrival.

A separate native retirement probe detaches at tick 20 with stamp 15, then removes
the mission at tick 21: the stamp stays 15. World regression cases cover live
versus already detached controllers with stamps at and inside the expiry boundary.
The combined matrix passes 112 cases: 96 same-destination replacements and
16 queued round trips, with both balances and builds; the Aramon/Creon queued
and arrival-replacement matrix passes all 16 cases. Changed-destination
replacement and multiple moving requesters are covered in the sections above.

## Point-move dispatch through arrival and standby

Plain `World::order` moves retain the native point-mission flags
`0x3000400`; ground standby initialization retains `0x5012000`. The mission
flags are hashed state. The native constructor (`4d6c40`) initializes definition
flags and removes the target/point-presence bits when those inputs are absent.
These two are taken from these observations; other missions' flags come from
their own registry rows (see "Retail group pacing").

`check_gate_search_movement.py --missions` advances original `4d8450` and
World's production ground mission dispatcher before each mover update. World
starts through `World::order`. The native side constructs the same initial
point mission and controller; subsequent dispatch, search, movement, arrival,
move removal and standby evolve independently. Comparisons include position,
velocity/orientation, movement modes/timestamps, search pending state, RNG,
mission kind/stage/wait mask/deadline/pending/flags/radius, unit events and
controller presence, together with complete gate script/animation/occupancy
state. Completion requires crossing, arrival, a closed gate and ground standby.
All 32 gate/balance/build/dispatcher-mode cases pass (38,400 compared ticks),
including exact ordered grade-query traces. This is ordinary single-move/standby
composition; the sections above cover queued/replaced orders, multiple
moving requesters and ring/rectangle arrival cleanup. Full ring/build mission
composition is outside that gate comparison.

The fixture's native player carries the priority byte of the slot it
represents (at budget 503 a mismatched byte gives native 500 work units
against World's 503). Diagnostic mode compares every ordered grade query,
including its tick, coordinates and value.

## Point arrival and composed gate traversal

Ordinary point-goal arrival detaches its navigation controller while
continuing to advance stored route points during braking. The native sequence
at `4e5150`/`4e5186` emits pending bits `0x100` and `0x400`, cancels the search
worker, and clears the controller without discarding those points. Old admission
stamps clear; stamps newer than tick minus six remain. A detached controller
cannot start another route retry. An early return at arrival would freeze the
stored point state (the composed gate oracle sees that at tick 659). The
self-test covers old/recent stamps,
stored endpoint consumption and refusal without retry or RNG consumption.

`check_gate_search_movement.py` compares independently evolving native and World
runs in an authored corridor, with a captured 4×4 VERPULT mover and local gate
assets. Hard blockers make the gate the only crossing. Original gate decisions,
active-bit changes, COB execution, yard validation/restamping, cache refresh,
movement, height and asynchronous search run together for 1,200 ticks per case.
Checks include movement/controller state, RNG, script statics/threads, piece
animation and footprint occupancy, plus successful crossing, arrival and closure.
All four gates under both balances pass in Debug and Release (16 cases,
19,200 compared ticks).

Gate command construction and dispatch are checked observation boundaries;
emitted actions enter the original active-bit setter. Gate policy uses World's
chosen cadence. Formation is disabled, and other AI, mission dispatch, combat
and rendering are not advanced. This is a composed movement proof within those
boundaries, not a claim of complete retail match simulation; exploration
cadence is not covered. The captured surface-movement regression passes all
174 cases (87 ground/hover/boat units × two balances), comparing 27,840
independently evolved movement/search updates.

## Live native gate fixture for composed traversal

`native_gate.py` supplies the live gate used by the moving-requester
comparison. It executes original active-bit changes (`51e4d0`), immediate
notifications, COB threads and integer piece animation, yard transition checks
(`507c70`/`507ae0`), occupancy restamping (`5062d0`) and cache-refresh dispatch.
No movement-class cache is installed in this component fixture. Script-name
lookup, simple unit queries, rendering and sound are explicit host boundaries;
the gate's script, yard validation and occupancy writes are not substituted.
The local install supplies the FBI and COB inputs. Its authored flat map has
empty feature records, and nonoverlapping allocations reserve the complete
game-field block before the map-cell buffer.

`check_gate_lifecycle.py` compares 2,600 per-tick boundaries for each of four
shipped gates under both balances, in both Debug and Release. All 16 cases pass
(41,600 boundaries): active/yard state, RNG, every script static and thread word,
integer animation state and pose, and every gate-footprint cell's occupant.
Inputs include duplicate activation commands and reversals before animation
finishes. The World side uses `setActive` and the production script host. This
establishes the live gate component used by the native movement harness; the
composed harness above compares a moving requester's traversal.

## Automatic AI gates and owner-sensitive search

Player state retains and hashes automatic-gate capability. Normal computer
slots enable it in client/server setup and recorded-match reconstruction;
strategic campaign players enable it in mission setup. Human players default to
manual gates. Retail's hidden human unlock/AutoGates UI is not implemented.

The World gate controller uses the verified `40a020` proximity rule: an allocated
same-owner mobile body within the footprint plus one-cell border opens the
gate when moving, or holds it open when stopped over a gate-passage cell.
No qualifying body requests closure. Commands use `setActive`, so scripts
control animation, yard opening, occupancy vetoes and eventual closure.
The controller runs once per simulation tick before unit updates. This is an
intentional AI scheduling difference, consistent with this engine's independent
skirmish AI; retail planner scan timing is not claimed.

Search supplies that same player capability and actual gate-owner probes
to `retailSearchGrade`. Owned passage grade 3 becomes grade 4 only for capable
requesters with square footprints on retries 0/1. Enemy gates, manual capability,
solid frames, rectangular footprints and later retries retain native behavior.
`check_gate_owner.py` compares 10,800 production World results with original
`4139d0`, including original eligibility and map probing. Visibility and the
initial cached grade are fixture inputs. Both Debug and Release pass.

`check_gate_activation.py` compares the production World controller with
all 3,136 original proximity decisions. The authored fixture marks unallocated bodies dead, matching
World's allocation/liveness representation rather than merely setting zero HP.
All eight shipped gate/balance combinations pass manual activation, occupancy
veto/clearance, automatic approach, stopped-passage holding and automatic closure.
The asset-backed test also runs full `World::tick` traversal in both
directions for all eight combinations (16 crossings). Hard map blockers extend
from the gate to both map edges, making the passage the only crossing. Each leg
must complete a fresh route search before entering a passage, enter only while
the yard is open, arrive on the opposite side, and leave the gate closed. The
test uses normal order dispatch, asynchronous search (budget 503), movement,
automatic gate control and the shipped script; it does not inject positions
after each leg starts. Debug and Release pass. CMake registers this as
`gate_scripts` whenever `TAK_TEST_DATA` is supplied. (On an open map the
mover correctly goes around the gate, so the blockers are what force passage
traversal.) This end-to-end check establishes World behavior; the harness
above adds native tick-by-tick composed traversal. Boat 3014 matches 160
composed updates in each balance, including seven route deliveries.

## Gate activation drives the script

`World::setActive` immediately notifies a gate's `Activate`/`Deactivate`
script on an active-state edge and updates the script host's activation value.
Unchanged state emits no notification. This matches the bit-0 branch of native
`51e4d0`; `check_gate_active_bit.py` verifies 512 flag/target combinations and
the immediate-notification arguments at the script boundary.

The command regression uses the shared lockstep `applyCommand` entry point:
wrong-owner commands do nothing, owned state changes execute the script and
update path grades, and duplicate commands do not restart callbacks.
`retail_script_test --gates assets/game` also executes all eight shipped gate
type/balance combinations through repeated activation cycles. It verifies that
an occupied passage prevents closure and that the original script closes after
the occupant leaves, without another command. Debug and Release pass these
asset-backed checks. Automatic eligibility and the owner-sensitive
path-grade adapter are in the section above.

## Refresh search caches after yard transitions

An accepted COB yard-state write refreshes every existing movement-class
search plane over the yard and its clearance border, so cached route search
never keeps a closed grade where live placement sees the open yard. The refresh also runs for accepted writes of the existing state, matching
`507c70`; rejected transitions leave the cache unchanged.

`check_yard_transition.py` executes original transition and occupancy validation
for 192 combinations of prior state, requested value, yard bits and occupant.
It observes the ordered restamp/refresh callbacks and validates their arguments.
CTest exercises the production script host and cache through closed/open,
occupied refusal, successful closure, and an accepted same-state assignment,
and the 1,800-case raw gate oracle passes.

## Raw gate passage grades

The type loader retains the source FBI `gate` low bit. Raw search grading
recognizes closed `c`/`C` passage cells on those types and returns special grade
3 instead of ordinary stationary-body grade 0. Solid frames stay blocked, open
passages use terrain grades, and forbidden terrain still blocks a passage.
`check_gate_grade.py` compares 1,800 authored rectangle queries against original
`5088f0`, with body allocation and stamped yard cells as controlled inputs.
CTest includes representative frame, passage, open-yard and water-depth cases.
All targets rebuild in Debug and Release, and both builds pass all 1,800 native
rectangle comparisons. The authored fixture explicitly uses a stationary type,
matching the native body without a mover. The gate capability/owner adapter
and automatic opening/closing are in "Automatic AI gates and owner-sensitive
search" above.

## Separate inactive routes from consumed endpoints

Native `4e51de` selects an immediate retry when fewer than two points remain;
it does not test the navigator's active bit. An empty delivery can disable a
two-point route without consuming its endpoint, so `navigationExhausted`
cannot stand for both facts (it would re-request this route at once).

Orders retain `navigationConsumed` separately, and hash it as simulation
state. Replacing a route/direct segment clears consumption; consuming the last
endpoint sets it; empty delivery preserves it. Point advancement also applies
to stored inactive routes, matching `4e5189`. The retry ladder uses consumption
or repeated refusal for its immediate branch. The diagnostic reports this
production state directly instead of maintaining a separate consumption flag.
The retry oracle exercises active/inactive navigators with one/two points;
CTest cases distinguish inactive stored routes, consumed routes and repeated
refusal. New requests clear all four previous route-outcome bits, as
original `4e4f50` does; an already-pending request retains them. The retry oracle
executes that original submission routine and compares the resulting flags,
and the composed comparison includes all four outcome bits.

The 8,192-case native retry comparison matches, including outcome flags.
Crusades unit 2805 matches all 160 composed updates and all 529 ordered grade
queries: 85 moving ticks, 38 moving while pending, five deliveries and 79
moving ticks after the first delivery; standard balance also passes all 160
updates.

The composed matrix passes all 174 cases: 87 surface units under
both balances, 160 updates each (27,840 state comparisons), including all four
route-outcome bits. Crusades totals: 12,660 moving ticks, 3,489 moving while
pending, 161 deliveries and 10,407 moving ticks after first delivery. Standard
totals: 12,659, 3,542, 161 and 10,353 respectively. Each balance has 2,137
refusal ticks. These tests control exploration and omit mission dispatch,
scripts, formation and other moving requesters; the gate harnesses below cover
those.

## Gate-specific native observations

The late captured scene contains no unit whose type has `+0x264:0x40000000`.
Passing its surface-movement matrix therefore does not cover special gate
grades. The native type loader at `4c0afb..4c0b20` reads the source FBI `gate`
property (low bit) into that flag. Shipped `arangate`, `tarngate`, `verngate`
and `cregate` definitions provide concrete gate fixtures; their `c` yard cells
must be distinguished from the solid frame. World retains this property and
distinguishes these passages in raw search grades ("Raw gate passage grades").

Native `409fe0` was executed for 360 combinations of its two configuration
bytes, player enabled/type state and stored capability byte. It returns 1 when
both configuration bytes are nonzero and the player is enabled with type 1;
otherwise it returns the stored AI-object byte at `+0x1a5`. The configuration
inputs are `[[0x62d558]+4]+4` and `[[0x62d558]+0]+9`. Their user-facing meanings
are partly identified: the command table at `605c74` maps `AutoGates` to
`425a00`, which toggles the second byte. The AI constructor at
`41013c..410169` initializes the stored capability to 1 for an enabled type-2
player and 0 otherwise. The first configuration byte is a hidden-command unlock:
the command table at `605a4c` maps `Kingdoms` to `425b30`. That handler requires
exactly five matching tokens, sets the byte on success, and clears it on any
failure. The same byte gates the tilde console branch at `528ed4`. It must not
be equated with “human player” or the AutoGates setting alone.

`check_gate_capability.py` executes the original eligibility routine for 216
combinations, including noncanonical nonzero bytes. It also executes the
command's comparisons/configuration writes followed by eligibility for 36
sequences. Only token lookup is supplied by the fixture; original strings are
read from the local binary's comparison sites. All cases pass.

`check_gate_activation.py` executes original `40a020` and map lookup for 3,136
controlled cases. Its scan covers the gate footprint plus a one-cell border.
Only allocated same-owner bodies with a mover qualify. Nonzero mover speed
triggers opening; a stopped body also qualifies if its footprint overlaps any
map cell carrying the gate-passage bit. An already-active gate closes when no
body qualifies; unchanged desired state emits no order. The tests observe the
order builder/dispatcher boundary, including the exact gate target and order
arguments. They do not execute downstream COB animation, yard closure checks,
or the AI scan cadence; World integration is checked by the World comparison
in "Automatic AI gates and owner-sensitive search".

The yard parser maps `c` to `0x2d` and `C` to `0x35`. Placement at
`506416..506448` tags a cell with map byte `+0xd:0x20` only when the type is a
gate and `(yardByte & 6)==4`, covering both forms of gate passage and excluding
the solid `o` frame. Sixteen direct original `5088f0` comparisons across gate
flag, passage flag, hard terrain and road inputs confirm that only a gate
passage on passable terrain produces grade 3; solid frames and hard terrain
remain grade 0. World's raw search adapter has this branch.

The general grade oracle substitutes capability and verifies owner/probe
ordering. The World integration comparisons with native capability and map
probing, the shipped-script opening/closing tests and the composed traversal
through an animated gate are in the sections above.

## Include all occupied surface bodies in cache aging

`prepareSearchGrade` includes unfinished units and landed aircraft, with the
same grounded-aircraft condition as live occupancy. In the captured
comparison, unfinished unit 1410 occupies the footprint affecting `(426,95)`;
unfinished landed aircraft 1548 affects `(216,359)`. Retail ages both
interiors to blocked grade 0 (excluding them would leave clearance grade 4).
CTest checks aging for completed/unfinished surface units, completed/unfinished
landed aircraft, and exclusion of airborne aircraft. Unit 625 (Crusades), unit
590 (standard) and unfinished unit 1410 (Crusades) match all 160 composed
states. Their ordered grade traces match exactly: 46,161, 19,263 and
509 queries respectively. Unit 625 moves on all 160 ticks, including 156 while
search is pending, receives two deliveries and moves on 91 ticks after its
first delivery. This includes actual route-following and refusal behavior.

The composed comparison runs with the captured pool bounds on both sides and
verifies that the native scan limit agrees (reduced pool bounds with the
captured scan limit change initial weights, retail 20 against 1.5, and the
next wrap timestamp). Crusades unit 585 passes all 160 states and all 1,382
ordered grade queries, including two route deliveries and 134 moving ticks
after the first delivery. Unit 2805, whose workers complete an empty route at
tick 37569, depends on the inactive-route rule of the section above: native
stays inactive and does not re-ask. Eight supplemental cases (units 625, 580,
3014 and 3538, both balances, destination offset -256/-512) all pass 160
updates.

The composed diagnostic separately tracks endpoint consumption during movement.
An empty search delivery disables navigation without consuming the stored
points, so an inactive route is not reported as one consumed endpoint; boats
3251 and 3724 pass 160 comparisons. Reports include movement, movement while
pending, actual native delivery count, movement after delivery, and refusal
ticks, so stationary cases do not imply coverage of route-following motion.
The cache-aging case above is unit 1410's footprint: native origin `(429,97)`,
footprint 2x2, grade timestamp 35663 and flags `0xe9290921`.

## Preserve mission precision when submitting searches

World's search submission uses the original fixed-point mission target for
both the goal cell and stored destination, with no float round trip. A
captured WATER3 boat's destination Z is `342884351`, one raw step below a
footprint boundary; float rounding would move it into the next cell, making
the initial controller distance 579 instead of retail's 572 and delivering the
partial route one update late after 2,870 matching grade queries.

Boat 3014 matches all 160 composed movement/search updates in both balances.
CTest has 18 land/water cases across 2/3/5-cell footprints and raw offsets
-1/0/+1 at boundaries, checking the delivered destination and endpoint.

## Composed movement/search comparison

`tools/re/check_search_movement.py` compares a captured surface mover with
the original movement, height, request admission, asynchronous worker and route
delivery routines running together. World runs its production adapters in the
same movement-before-worker order. Both sides evolve their own positions,
occupancy and routes; later native outputs are not injected into World. Other
units remain stationary, exploration is fixed, formation is disabled and the
mission dispatcher/scripts/combat do not advance. This is not full-game parity.

The fixture selects initial grade caches by the chosen balance's movement
class, rebinding the capture's representative IDs: unit 590 changes from
GROUND5 to GROUND4 in standard balance. The diagnostic represents a consumed
route as its one remaining endpoint; boat 2530 passes 160 updates in both
balances. Boat 3014's partial route at update 11 depends on the fixed-point
destination of the section above. Passing isolated or composed cases are not
complete pathfinding parity; the sections above add multiple moving
requesters, mission dispatch and gates.

## Conditional route retention on controller reset

Ground/water mission resets follow original `4e54e0` when an existing route
has multiple points. Retail retains and activates routes with at least three
points only when twice the endpoint-to-goal distance is strictly less than the
unit-to-goal distance, using its signed wrapped fixed-point arithmetic and
truncated double-precision lengths. An exact half-distance tie does not retain
the route.

Otherwise the mission flags and previous activation select a fresh direct
segment or an inactive navigator whose stored points survive. In particular,
`0x400000` can preserve points without activating them, and `0x10000000` forces a
direct segment only when the previous navigator was inactive. A nearby existing
endpoint can reactivate a stored route regardless of its previous activation.
When a direct segment replaces corners, World defers erasing those corners until
mission dispatch returns, preserving the dispatcher's mission reference and the
later queued commands.

`check_navigator_reset.py` compares actual World ground-mission resets against
original `4e2500`/`4e54e0`, with scheduler cancellation/submission and notification
substitutes. Both builds pass 4,352 activation/point-list comparisons: 2/3/4/64
points, four mission flag combinations, active/inactive navigators, land/boats,
rectangular footprints, one-raw-unit half-distance boundaries and 512 randomized
full signed-coordinate cases. Every World case also verifies an untouched queued
command. CTest retains representative replacement, retention, tie and flag cases
for both land and water. This checks reset decisions and route storage; search
execution, physical movement, absent-goal setup and full dispatcher parity are
outside this oracle.

## Align direct navigator endpoints

Retail retains the fractional mission point in the mission, but
`4e2500`/`4e2820` convert it to a footprint origin and back to an aligned
endpoint for the navigator. For a 1x1 click just below `(256,320)`, retail's
endpoint is `(248,312)`, not the fractional click.

Direct ground/water segments retain the original point in `missionTarget`
and align their endpoint independently by footprint width and depth. Controller
resets use the same construction, so a previous partial-route corner cannot
replace the requested point. The segment's start remains the unit's integer
world position. Queued orders retain their raw point until activated.

`check_destination_setup.py --binary` checks mission coordinates, goal
origins, segment anchors and endpoints in 320 World comparisons per build.
Half exercise ordinary installation and half replace an existing endpoint before
resetting the controller. Native reset runs original `4d4da0` a second time,
including clearing the old controller and installing its replacement; the World
side invokes its actual ground-mission dispatcher for the reset. Native scheduler
submission/cancellation remain boundary substitutes. This check does not run the
search worker or physical movement; longer-route retention is the section
above. A return-mission CTest assertion checks the retained `(256,256)`
mission and the aligned `(264,264)` 1x1 endpoint separately.

A native three-point route from `(104,104)` to old endpoint `(168,104)`, with
new goal `(424,104)`, becomes the direct two-point route. With old endpoint
`(328,104)`, the same setup retains all three points. This follows the
endpoint-distance branch at `4e5563..4e55e9`, which "Conditional route
retention on controller reset" above ports.

## Preserve blocked move destinations

`World::order` keeps a blocked destination as given: it does not replace it
with a nearby walkable cell, on active and queued ground/water orders.
Retail retains the original point and lets its search/radius logic deal with
it; an outward scan for a fit would change the mission and search goal first
(a 1x1 land click just below `(256,320)` would become `(184,248)`).

The destination oracle runs original ordinary-move validation `4de530`
(command 2), name lookup `4d4bf0`, submission `4d78a0`, constructor `4d6c40`, and
queue installation `4d77f0`, followed by the existing controller/navigator chain.
It uses a controlled sorted definition table resolving Move_Ground to kind 28,
zero definition flags, an enabled player, and no target entity or region.
Clear and blocked-region cases cover eight footprints, boats/land, and fractional
coordinates on either side of independent-axis quantization boundaries.

The `--binary` option compares 160 native command cases with actual World orders,
checking retained mission coordinates and footprint origins. The separate
native-only checks retain coverage
of 128 supplied-goal navigator setups and 160 direct constructor/controller
installations. CTest additionally checks active/queued blocked goals for four
footprints on land and water. Screen picking, packet decoding, physical movement,
and complete mission/search integration are not certified by this boundary
check; navigator endpoint quantization is checked in the section above.

## Terrain-cache integration and current heading at cost handoff

The composed oracle's `--terrain` mode builds cached grades from height
and occupancy inputs on both sides. World runs its terrain-grid construction,
cache refresh/preparation/cleanup, cached dispatch and live placement adapter.
Retail runs original `4e01d0`, `508cd0`/`5088f0`, `4e1ee0`, `4e2060`, `4139d0`,
`413c80` and `4db640`/`507fb0` along with the original search and delivery.
Each tick additionally compares the complete cached-grade plane's digest.
No native grade result is injected into World.

Both caches are warmed through their preparation paths at tick zero. Inputs
include a second ordinary mobile body, so later preparation ages occupancy
and distant grade-2 queries can invoke live placement. Layouts cover flat
terrain, a cliff with a gap, an unbroken cliff, a ramp and a rough height patch;
water cases use the same heights with water-depth limits and sea level.
Native map-cell extrema are independently formed from the supplied height
quads; the retail map loader itself does not run in this fixture.

When a unit turns between search initialization and phase-2 seeding, original
`415cb0` reads its current heading then, so the cost handoff refreshes only
the heading, retaining that attempt's start and cost profile. (With the
initialization heading a moving 6x2 land unit at tick 18 has matching cache
contents and grade answers, but its cost search examines neighbors in a
different order.) CTest checks that handoff samples the changed heading once
across different suspension budgets.

Both builds pass 1,440 terrain searches (720 land, 720 boat), covering 86,948
ticks: full exploration 34,879, hidden 20,973, alternating exploration 31,096.
The original executable performs 428 live placement queries within these runs.
The 80 worker lifecycles also pass (3,010 ticks). Emulator setup keeps
exploration separate from map records and uses the cdecl cleanup for
`atexit`. The controlled-grade corpus passes 1,920 searches / 89,996 ticks.

In this fixture positions, headings and movement flags are authored inputs,
with synthetic ground/water profiles rather than source rosters; physical
movement, mission dispatch, several requesters, special yards and the
exploration producer are covered by the sections above.

### Destination setup boundary

`tools/re/check_destination_setup.py` runs original `4e54e0` with the real
goal-coordinate getter `4e2820`. Across 128 fresh-route cases (eight footprints,
land/boat, four goal origins, clear/fully blocked cached planes), the navigator
keeps the supplied origin and constructs its endpoint using each footprint axis
independently. Cancellation, request submission and mission notification are
recorded substitutes; this check does not run a worker or a mission constructor.
An additional 160 cases run the original mission constructor `4d6c40`, goal
reset `4d4da0`, controller constructor `4e2500`, installer `4d4d40`, and navigator
setup together. The constructor preserves supplied fixed-point coordinates in
the mission. Controller creation converts each axis to a footprint origin as
`(coordinateRaw - footprint*8*65536 + 8*65536) >> 20`, without searching for a
walkable cell. Fixtures cover one raw unit below/on/above cell boundaries,
including opposite rounding directions on rectangular axes, for boats and land
units on clear and fully blocked cached planes. Both the retained mission
coordinates and the resulting initial segment are checked. Mission-definition
flags are controlled as zero; scheduler cancellation/submission are substituted.

These checks establish the constructor/controller/navigator boundary. The
comparison in "Preserve blocked move destinations" above exercises ordinary
move validation in `4de530` (command 2, branch `4df0b9`) and generic
submission in `4d78a0`, the world-coordinate command boundary used by
`World::order`.

## Exploration and admission in composed searches

The World search comparison optionally leaves original `4139d0` and
`413c80` running. Both sides receive the same authored cached grades and coarse
exploration words; World calls its actual `searchGrade` adapter. Cases cover
fully explored, fully hidden, and alternating player-0/player-1 explored
regions, plus a raw-grade control. They exercise unknown-terrain
grading throughout the search rather than only in an isolated query.

Original navigator admission `4e54a0` also runs by default. World calls the
production admission and request-refresh methods extracted unchanged from its
tick callbacks. Every tick compares the admission stamp as well as the ordered
grade queries, pending status, mission events, navigator activation, outcome
flags and delivered route. Moving cases supply authored positions before and
after admission, plus changing headings and road/water movement flags; both
sides independently refresh the source position, heading and costs when they
initialize an attempt. The movement simulation itself does not produce these
inputs and is not covered by this test.

Both builds pass 1,920 searches (960 land, 960 boat), covering 89,996 ticks:
480 raw-grade searches (26,117 ticks), 480 fully explored (27,108), 480 hidden
(14,584), and 480 alternating exploration (22,187). Each group includes
stationary and changing requesters, eight footprint shapes, three budgets and
five grade layouts.

Grid preparation here receives authored cached grades. Live grade-2 refresh,
special grade-3 yards, the exploration producer, physical movement and mission
dispatch are outside this composed comparison.

## Rectangular search coordinates and composed delivery

`check_world_search.py` compares the actual World request, PathService
scheduler/search and World delivery against original `416430`, its original
search/reconstruction, and original `4e4ea0`. It compares every tick's ordered
grade-query digest, pending status, mission events, navigator activation,
outcome flags and waypoint list. Requesters stay stationary; grades, grid
preparation, controller lookup/admission and the mission event sink are
controlled. Movement, mission dispatch and live terrain preparation do not run
in this comparison. The exact-cell goal is independently supplied on both sides.

World converts request starts/goals, refreshed starts, goal acceptance and
delivered points with each axis's footprint (a square conversion puts a 2x3
mover's X waypoints 8px too far east). PathService carries separate X/Z
center-to-origin offsets through initialization, queries and reconstruction.
Retail `4146a5` and `4146b5` use the corresponding footprint axis when producing
world waypoints. Cost initialization also uses footprint X, as observed at
`4152ec`, `415396` and `415956`, rather than the larger footprint dimension.
Rectangular perimeter regression cases include unequal axis offsets.

The navigator's partial flag is not taken from the mission failure
notification: PathService delivers only the actual route flag, while
retaining failure statistics and the `0x2000` mission notification. Retail's empty result can notify failure, emit delivery event
`0x200`, disable navigation, and still leave the partial-route flag clear.
This distinction is covered by a PathService regression as well as the native
comparison (a boxed-in start). The delivery-only fixture supplies per-axis
waypoint coordinates.

Both builds pass 240 composed searches (120 land, 120 boat), totaling 9,843
ticks each: eight square/rectangular footprints, three scheduler budgets, and
five grade layouts covering direct travel, detours, a disconnected destination,
mixed grades and a boxed-in start. The 2,048 delivery-only cases also pass with
the per-axis conversion. These are synthetic movement profiles, not
additional standard/Crusades source-roster comparisons.

## Navigator route delivery

The production path-result callback is `World::deliverSearchRoute`, called at
the end-of-tick boundary. Original `4e4ea0` shows that an empty result clears
navigator activity and emits event `0x200` when its controller does not accept
the unit's current position. World marks navigation exhausted and notifies an unsatisfied ground movement mission, preserving the
owning order and queue until the dispatcher handles that event. Accepted goals
do not receive a failure event. The same circle/ring acceptance helper is used
by normal movement and this empty-delivery branch.

`check_route_delivery.py` compares 2,048 independently authored deliveries
against original `4e4ea0` and its pending-request removal. It varies active and
inactive old routes, goal acceptance, outcome flags, footprints, and route
sizes 0, 2, 3, 63, 64, 65 and 128. World matches activation, mission events,
scan invalidation, unchanged admission stamp, outcome flags, and retained
waypoints (retail: inactive, event 512). CTest also checks queue preservation
and subsequent route reactivation for accepted and unaccepted goals.

This checks the delivery adapter with supplied routes, not the full search
producing those routes. Native goal acceptance and the mission notification
sink are controlled inputs; mission dispatch does not run. Request removal is
observed on the native side but World queue removal remains in PathService,
outside this direct callback test. The retail search emits either no points or
at least two points; one-point delivery is outside this corpus.

## Search grades consume owner exploration

`World::searchGrade` calls the verified `retailSearchVisible` adapter with
the simulation-owned exploration words and the requesting unit's player (not
an always-visible callback). Both land and boat searches apply retail's grade-5 unknown-terrain rule, including the cross-shaped
exemption around the start footprint. This uses the same shared-team explored
state as boat movement, without reading renderer visibility or the captured
observer override.

`check_search_grade.py` adds 4,096 cases through the actual World adapter,
compared against original `4139d0` calling original `413c80`. Cases vary
rectangular footprints, start/query coordinates, supported owners 0–7, cached
grades, retries, and empty/full/owner/other-player/random exploration masks.
These compose visibility and cached-grade dispatch; live grade-2 refresh and
special-yard probes are covered by the isolated dispatcher oracle. The 6,000
dispatch, 3,000 visibility, 3,000 occupancy aging and 3,000 preparation cases
also pass. Four World regressions run in CTest.

Two synthetic blocked-goal fixtures use long sight and upright terrain height
so their obstacles are known: a synthetic type with no support model sits at
height zero below the terrain, and sight distance alone does not reveal the
wall to it.

## Boat scans and composed navigator comparisons

World runs the verified forward/neighbor scan for boats, including speed
modes and scan intervals, and applies boat-specific corner pruning. Placement
queries use each axis's actual footprint size. Live navigation reads the
owner's simulation-owned, shared-team exploration; it never reads local UI
visibility. This represents authoritative owner-view movement in lockstep.
Retail's observer-specific remote prediction context is a separate,
unverified boundary.

Probe format 37 restores the captured exploration word plane, sight caches,
and selected-player query context. No later retail result is injected into
World. `check_ground_navigation_sequence.py` compares independently evolving
routes, position, speed, heading, pitch/roll, height, terrain/refusal state,
point count, movement/speed modes, scan deadline and game RNG. Search submission
is intercepted; workers do not run or deliver routes. Controller acceptance
stays false and formation is disabled. Exploration stays at the supplied
initial plane because neither side executes the sight-update phase here.

Both balance modes pass 13,920 updates over 160 rounds and 87 surface movers
(standard Debug, Crusades Release). Each includes 1,440 boat updates across
WATER3/4/5, with 977 changed boat positions and 579 boat refusal observations.
Each consumes 150 route points; standard has 2,950 exhausted-route observations,
Crusades 2,934. Observed native submission calls are 7,537 and 7,518 respectively;
those counts are not equivalent to admitted/delivered worker jobs. This covers
the boat scan, not full search-to-destination parity.

The captured plane contains 57,600 fully explored words (`0xffff`), so passing
its owner-view variant alone does not establish partial-exploration behavior.
The harness also supports explicit `hidden` and `player-stripes` initial maps.
With player-stripes and `--owner-view`, both rosters pass another 13,920 updates.
These runs observe 794 speed-mode-1 and 646 speed-mode-2 updates each; their
WATER4 motion differs from the fully explored run and matches native.
Standard consumes 149 points and observes 2,904 exhausted routes; Crusades
consumes 150 and observes 2,899. These are authored visibility inputs supplied
identically to both implementations, not reconstructed capture history.

Live regressions cover stronger shoreline slowdown, moderate unexplored-water
slowdown, their scan intervals, and clearing slowdown after an airborne allied
spotter reveals the route. The owner differs from the local viewer in this
fixture. The spotter must actually be airborne; a landed aircraft correctly
remains a forward footprint obstacle.

World selects the boat cost profile for boats (not always the land flag). The same `floater && minWaterDepth > 0` gate travels with the cost
profile into initial heuristic weight selection at each search attempt.
`check_cost_search.py` verifies 240 profiles through the production World
adapter as well as the standalone calculator, varying floater, minimum depth,
and maximum depth independently. Both builds pass, along with the 16 kernel
fixtures (5,446 pops) and 28 slice fixtures (1,401 boundaries). The separate
native weight oracle passes 1,152 age/load/retry/wrap combinations per build.
Boat worker admission, route delivery and mission completion are composed in
the sections above; shoreline routes are in "Retail audit: special terrain
and domains".

## Simulation-owned exploration producer

World maintains a persistent exploration word plane and a cached sight
footprint per unit. The producer runs before the first movement tick and after
each completed simulation tick, including on headless referees. Team members
receive the same revealed cells, independently of the local renderer's viewer
or asynchronous fog pass. Exploration survives unit loss and is cleared on
terrain/replay reset. Both the plane and cached footprints enter `stateHash`.
This is the state boat scanning and search grades read.

`retailexploration.h` ports four native computations:

- 50ea58..50ed53 constructs the projected 32px terrain-height pairs from the
  original 16px map height samples, then blends their extrema and clamps to sea.
- 4c6800 admits footprint cells using distance and eye-height bounds. The
  closest cells bypass the height test. Sight changes affect current-view
  reference counts, while exploration only ORs persistent owner bits.
- 4c6c00/4c6a70 clamp sight Y to sea+1 and retain the old footprint until its
  cell changes or eye height changes by more than five. Signed cell division
  truncates toward zero, unlike the boat query's arithmetic shift.
- 546f40 computes the default model's top, including object offsets and the
  native recursive zero floor. Type +0x14c is the integer high word of the
  fixed-point model top at +0x14a; sight uses its low byte. It is not an FBI
  `sightheight` field. TypeRegistry obtains this input from source 3DOs.

`check_exploration.py` compares 1,024 terrain planes, 8,192 footprint changes,
8,192 complete sight updates, and 2,048 model trees against native execution.
All 19,456 cases pass in Debug and Release. Terrain execution bypasses the
unrelated sector-grid prefix after establishing the original stack frame;
the tested height-construction region executes unchanged. Footprint and
sight tests check ordered cell changes and retained cache state, including
zero sight height, negative coordinates, sea clamping, update thresholds,
removal, and disabled exploration. World regressions separately prove
viewer-independent hashes, ally masks, persistent reveal, and replay reset.

Retail calls its sight batch after a frame's simulation batch (52654e),
whereas World schedules a fixed phase each tick for lockstep. Probe format 37
restores the captured initial exploration plane and cached sight state for
the composed navigator harness; the local display fog and a freshly empty
plane are never used as captured exploration history. The cross-compiler
golden (`tools/check-determinism.sh`) includes exploration's binary32 slope and
admitted cells.

## Verified boat scan rules

`retailGroundScan` ports 4dba80..4dbd19 with placement/visibility callbacks.
It resets both modes, schedules the base scan, walks the boat's forward probes
in exact 16px quantized steps, applies slowdown/deadline extensions, then scans
the eight neighboring footprints in original order. A grade below 4 through
the probe at counter 160 selects speed mode 2; grade 4, a later obstacle, or
unexplored/out-of-bounds space selects mode 1. The counter is measured before
each forward step, so its 160 boundary queries the point 176px ahead.
The original stored height is retained throughout these probes.

`retailBoatScanVisible` implements the projected 32px cell and player-bit test.
`retailPruneGroundCorner` also includes the boat branches: multiply the first
angle by five and allow the extra clear-speed-mode shortcut after the shared
distance gate. World uses the shared scan helper for land, hover and boat
scans, with the simulation-owned exploration as the visibility input (an
always-visible callback would not reproduce retail).

`check_ground_scan.py` runs the complete original scanner with coincident
navigator points and independently supplied grades/visibility. It compares
both resulting modes, deadline, every placement query (including raw XYZ),
and ordered visibility cell reads. All 10,000 cases pass in Debug and Release,
including the 160px boundary, grade 4 versus below 4, hidden/out-of-bounds
cells, fractional headings/positions, negative sight limits and tick overflow.
The extended `check_ground_corners.py` passes 8,192 land/boat decisions in both
builds with all three produced speed modes and turn-rate/distance boundaries.
These are controlled-input comparisons; World boat navigation is composed in
"Boat scans and composed navigator comparisons".

Visibility tracing identifies +0x19ef4 as the persistent `Mapping`
save plane (5110d0 loads it). Native 4c6800 increments/decrements the owner's
current-view byte counts but only ORs exploration bits into this word plane
(4c6a08..4c6a1f); removing sight does not erase exploration. Its inputs include
the cached unit sight center, sight distance, sight height, and the two-byte
coarse terrain-height plane at game +0x19f08. The owner slot comes from the
sight object's player +0xeb. World's boat query uses the owner's shared-team
exploration rather than retail's selected-human bit.

The producer: 4c6c00 copies unit XYZ, raises sight Y to at least sea+1, and
calls 4c6a70. That routine updates the sight footprint when a 32px cell
changes or sight height differs by more than 5, removing old counts and
adding the new footprint. 4c6800 tests a radius and a terrain-height-dependent
distance bound, with the closest cells admitted separately. World ports it
and its coarse terrain input ("Simulation-owned exploration producer").

## Navigator integration

`World::tickNavigationMovement` owns the production movement stage. Retry
decisions run before the local scan and steering. The constrained retry branch
tests movement/speed mode bits, independently of repeated body refusal. The
boat gate is `floater && minWaterDepth > 0`: native type +0x192 is maximum
water depth and +0x194 is minimum, as verified by the MOVEINFO copy at
4c0e85..4c0e94 and the independently loaded source class records (+0x194 is
not the maximum depth).

`check_navigation_retry.py` compares complete native 4e5150 branching with
production World requests, including final RNG state and ordered draw bounds.
8,192 cases pass in both Debug and Release, with minimum/maximum depth varied
independently, all outcome bits and movement/speed modes, and refusal states.
Controller acceptance is held false and search submission is observed at its
boundary; this does not verify worker delivery or mission dispatch.

A navigator that consumes its last partial-route point brakes and retries
while retaining its owning mission (`Order::navigationExhausted`, hashed); a
goal on disconnected terrain is not silently dropped in favour of the next
queued command. Regressions cover retained search ownership, queued orders,
partial-endpoint braking/retry, and replacement order reset. Floating
hoverers with nonpositive minimum depth take the ordinary neighbor scan; each
such scan clears both movement and speed mode.

`check_ground_navigation_sequence.py` leaves native navigator ticking, point
advancement, and scanning active. Both sides start with authored partial
routes and advance independently; controller acceptance and formation are
controlled inputs, and no search worker runs. Boat 1402's scan deadline at
tick 37448 is the sensitive case: retail's next scan tick is 37452 standard
and 37450 Crusades, reflecting their different speed inputs.

The boat scanner at 4dba80 probes in 16px forward steps to
`(footprintX+1)*16 + sightDistance`, queries placement and coarse visibility,
and selects speed mode 1 or 2 with a two- or three-half-cell scan interval.
It then runs the neighbor scan and boat-specific corner pruning. The
visibility test reads game +0x19ef4 using the player bit at +0x306f; that byte
is initialized from the `Human Player` setting and is also reassigned by
player-selection handlers. World uses the deterministic owner exploration
instead; display visibility is never a substitute.

## Composed active ground travel

Production movement calls `World::steerGround` for segment aiming, heading,
acceleration/braking, pitch/speed limits and quantized displacement. Commitment
and the post-mover surface phase keep their original order. Native 4d91b0
and 4da4d4..4da53a scale the unsigned moving turn rate by stored road/water
multipliers, with road priority, for both turning and stopping-distance
geometry (the raw rate gives an 1800-BAM heading difference on a road).

The active adapter converts native direction explicitly for coincident
waypoints and uses a signed-word turn request. Native heading zero is a
half-turn in World's heading convention, and general `bamDiff` picks the
opposite sign for an exact half-turn, so both active steering and stopped
facing use `retailTurnRequest`, where an exact half-turn is -32768. Live road and coincident
waypoint/half-turn regressions cover these cases.

`check_ground_travel_sequence.py` executes original 4dc800 plus 51b2a0 against
production World steering, commitment, terrain flags and surface updates.
Authored segments switch at fixed times and never depend on future native
results. Initial state/assets are supplied independently; positions, speed,
heading, pitch, height, refusal state and occupancy evolve independently over
160 rounds. Navigator segment delivery and movement mode are explicit inputs;
scan scheduling, formation constraints, route generation and automatic route
advancement are outside this comparison. This proves travel under supplied
segments, not complete order-to-destination parity.

The late capture passes 13,920 updates per balance mode (standard Debug,
Crusades Release), across 87 unattached surface movers. Each run has 11,597
changed positions, 3,685 refusal-state observations and 1,215 height changes.
Its 1,440 boat updates include 1,099 changed positions and 556 refusal-state
observations across WATER3/4/5. Attached WATER2 units remain excluded. Ground
classes differ with roster selection (GROUND4 standard, GROUND5 Crusades).
The synthetic `check_ground_travel.py` runs original complete active steering
with controlled navigator inputs and a COB notification sink, covering zero
and extreme rates, road/water priority, speed modes, pitch and degenerate
segments. Its comparisons exercise the actual production steering adapter.

`balance_inputs.py` reads only the UNITINFO section of an FBI (a weapon's
`turnrate` must not overwrite the unit's: VERMAGE 180 versus 2300) and has
regressions for section isolation, nested fields and invalid input.

The 16,000-case active steering oracle and the 16,000-case inactive/facing
oracle pass, and the captured active sequences pass in both balances. TARCAN
clock-dependent pitch is a known difference (see "Ground pitch and speed
limits").

## Stopping sequences and combat movement

`check_ground_stop_sequence.py` runs original 4dc800 and 51b2a0 repeatedly
against production World terrain flags, braking, commitment and surface height.
Both sides independently retain position, occupancy, speed, pitch/roll, refusal
state and last-movement tick. Assets are loaded independently; optional standard
or Crusades selection supplies source FBI motion values to native type inputs.
Individual maximum speed remains the identical captured initial input rather
than pretending the captured Crusades match is a freshly created standard game.
The navigator is held inactive and formation limits disabled. Every 16 rounds,
an identical speed input restarts coasting. The optional facing target remains
fixed. Mission dispatch, combat, route selection and independent COB ticks are outside
this oracle; original movement notifications still execute, with the captured CRT
thread resolved and an explicit bobbing clock. No resulting native state enters
World.

The late capture passes 13,920 updates in each roster with facing disabled and
another 13,920 per roster with facing enabled. Each run includes 1,440 boat
updates (WATER3/4/5); attached WATER2 units are excluded. Facing sequences include
2,023 changed X/Z positions, 2,431 refusal-state observations and 47 height
changes. The standard facing run contains 818 changed boat positions and 738
boat refusal-state observations. GROUND2 and HOVER2 stop immediately because
their shipped brake rate exceeds their initial speed; their zero-displacement
results are expected, not evidence of travel coverage. These are composed
stopping sequences, not proof of complete sailing routes or shoreline maneuvering.

Ground combat and nearby guard units commit their coasting displacement.
An explicit handled flag prevents a second movement update if coasting crosses
an attack-range boundary. A stopped attacker supplies a facing target to the
same braking helper. The pivot uses the terrain-scaled unsigned 16-bit rate and
starts on the tick speed reaches zero. Native parser 4bfde8..4bfdf4 defaults
`turninplacerate` to zero; the loader and UnitType keep that default rather
than substituting moving turn rate. The synthetic brake oracle covers facing
and rate boundaries in 16,000 cases (COB notifications are a sink there; the
captured sequence oracle executes them). Live land/boat regressions cover
combat coasting, pivot start, and crossing attack range without moving twice.
The stopping regressions use short-range attacks to control the
inactive-navigation input; they do not cover long-range boat attack admission.

## Inactive navigation braking

Idle, wait, wait-for-attack, build-rectangle arrival and ground-mission arrival
pass their remaining speed through movement commitment while braking (they do
not only reduce speed in place). The shared production helper applies stored
road/water flags, pitch and speed-mode caps, native heading quantization, and
the collision/refusal adapter. Surface updates run after movement. Corner
scanning uses the same stored terrain flags.

`check_ground_brake.py` executes original 4d9ad0 with an inactive navigator and
formation limits disabled, comparing 16,000 controlled inputs against
`World::brakeGround`. Inputs cover pitch boundaries, all speed modes, road/water
priority, fractional speeds and braking to zero. Pending facing is disabled.
Displacements stay within the initial footprint cell: this check proves braking
and displacement composition, not collision sequences or full navigation.
Production tick regressions separately check land and boat stop commands coast
to rest and retain their surface heights. These synthetic cases are independent
of balance settings; captured sequential braking in both rosters is in the
section above.

## Pathfinding parity: what is compared

Our AI differs from retail by agreement. Parity means matching pathfinding and movement for identical orders, terrain,
occupancy, navigation state and movement-owned random inputs. AI strategy,
construction/economy, rendering and whole-game random-call ordering are not
parity requirements. Their differences must not block movement comparisons.
The tick-994 commander response is outside this scope. Both the standard and
Crusades balance settings are explicit requirements. Verification must
identify the selected roster; success in one mode cannot stand in for the
other.

Compare the actual World adapters as well as the isolated search kernels:
terrain/feature grading, body occupancy and traffic, controller geometry,
admission and scheduling, route delivery, steering, collision/refusal and
repath cadence. Use native routine execution and captured initial states with
controlled external inputs. Label those comparisons as movement comparisons,
not uninterrupted whole-game replay.

Adapters: raw search grading consumes the verified map feature plane and
actual rectangular unit footprints, including building yards and landed
flyers; it bypasses the shared obstacle overlay for loaded maps. The live
steering query (`cellScore`) uses the same footprint and map feature
interpretation with its distinct native slope/body grading rules. Feature
creation, replacement and removal update that shared plane and active search
caches; production corpse/statue placement and retirement update the same
plane. The initial feature maps match all three captures. Boats are in scope:
shared surface queries cover water classes, and sailing routes, shoreline
maneuvering and boat traffic have the dedicated checks in the sections above.

### Ground pitch and speed limits

Pitch is simulation state, not merely rendering: original `4d95f0` clamps the
speed maximum using signed unit pitch. World retains ground pitch/roll from
the same four support samples, applies the quantized pitch limit to active
movement, and hashes both angles. Ground speed mode (mover bits 8..10) is also
retained and applies the original reduced-speed factors before the pitch
limit. The numeric factors are tested; the mode-transition producers are
covered by the scan and navigation sequence comparisons.

Support pitch compares the two pair-averaged heights; roll compares corners
0 and 1. Their denominators use integer model spans scaled by FBI pitchscale
and bankscale, with truncation before native angle rounding. Native parsing
at `4c09a3` supplies bankscale 0.5 and ground pitchscale 0.5 by default (flyer
pitchscale defaults to zero). The loader uses those defaults and retail's
fixed-number conversion. Probe format 36 restores initial surface angles;
older fixture formats are accepted but do not restore that initial state.

`check_ground_height.py` compares Y, pitch and roll on 15,000 original
height-dispatch calls. `check_ground_speed.py` compares 16,000 original speed
integrations and resulting X/Z velocities, exercising pitch-bucket boundaries,
terrain multipliers, road priority and all speed-mode values. Formation
controllers are absent from that speed test. Debug and Release both pass.
The World adapter additionally passes 12,000 height/pitch/roll queries in each
balance mode across the late capture's 95 surface units, including boats;
Crusades runs in Release. A live ramp regression verifies the stored pitch,
then checks its effect on actual speed and displacement on the next step.

The post-movement surface update resolves its owner by ID, because a builder
mission that creates a site can reallocate the unit vector; the queued-builder
regression exercises that path.

TARCAN's clock is an explicit unresolved trajectory input. A native
controlled test using its shipped 3DO, scales 2.4, and a seven-height-unit
per-cell slope gives pitch -2002 at clock phase 0 and -2059 at phase 26:
different speed-limit buckets. This does not by itself prove different
trajectories (current speed/acceleration also matter), but it disproves using
watermultiplier 1.0 alone to dismiss phase coupling. World uses the
simulation tick for bobbing; complete TARCAN comparisons must account for
the native clock input and the resulting speed limits.

Inactive navigation brakes through `4d95f0` and produces velocity for
commitment ("Inactive navigation braking").

### Movement commitment

Actual surface movement calls `World::commitGroundStep`, which uses the
verified `mobilePlacement` query (not a square `NavGrid::fits`/`cellFree`
test) when crossing footprint cells on a loaded map.
The same-cell bypass retains the current refusal state, as retail does.
Successful crossings clear refusal and update occupancy; refused steps clamp
each axis using its actual footprint dimension and cap speed at base/2 or
base/5. Refusal speed uses the stored road/water flags with road priority.
The occupancy rebuild stamps rectangular footprints. Terrain-only harnesses
without a feature plane use a placement fallback.

`check_ground_commit.py` supplies displacement sequences to original `4dad30`
and the production World helper. Native placement, refusal, clamping and
occupancy writes run without substitutions. Each implementation keeps its
own resulting state between steps; no native positions or placement answers
are fed back into World. It compares X/Z, speed and refusal state after every
step. Attached units are excluded because their native commitment follows a
host transform rather than the free surface-movement branch.

The mid capture passes 2,000 steps across 58 unattached surface movers, with
257 steps retaining refusal state and 1,236 accepted cell crossings. The late
capture passes 12,000 steps across 87 movers in each balance mode: standard
has 1,944 refusal-state steps and 7,366 crossings; Crusades has 1,928 and 7,369.
Each late run includes 1,241 boat steps across WATER3/4/5. Standard source inputs
come from shipped FBI/MOVEINFO assets; Crusades also passes in Release.
This corpus's WATER2 boat is attached and is not part of this commitment test.

Regressions cover a 2x4 unit passing an obstacle outside its real width,
first/repeated refusal clamps and speed caps, and within-cell movement
preserving refusal. The test terrain is flat at height zero so projected map
boundary markers do not accidentally replace the intended obstacle case.

These checks establish movement commitment for the tested displacement
sequences, not the steering decisions that produce them or attached movement.

### Surface height in World

Surface units store ground Y and the last proposed movement tick. The type
loader reads upright and the exact 3DO root selection polygon, mirroring its
support points as retail does. Each unit updates height after its mover,
including movement-loop early exits; the next tick tests that stored integer
height against sea level. Active-leg speed, acceleration and braking select the
stored road/water multiplier with road priority (no center-depth resampling).
Height and movement stamp are hashed. Probe format 35 restores initial
Y, movement stamp and variation phase; frame output includes surface Y.

`check_world_height.py` compares the production helper with original `51b2a0`.
World loads terrain, FBI flags/waterline and model support coordinates directly
from assets; native height outputs/support points are never imported. The mid
capture passes 12,000 queries across 67 surface units. The late capture passes
12,000 queries in each balance mode across 95 surface units. Each late run
includes 1,260 boat queries across WATER2/3/4/5. Controlled standard inputs are
read from shipped standard FBI/MOVEINFO definitions. Crusades also passes in
Release. These are height-adapter comparisons, not complete route replays.

Runtime regressions distinguish walker, hoverer and boat height/water flags
over the same submerged terrain. A walking ramp regression crosses sea level,
checks integer height throughout traversal, and verifies that the water flag
uses the previous step's height. The format-35 producer/importer also runs on
the mid capture; whole-game AI/random divergence remains outside this scope.

Hover bobbing uses simulation tick as an explicit deterministic clock input.
It does not reproduce retail's wall-clock animation phase. The captured sets
do not contain TARCAN, but its mathematical corner adjustment has an independent
controlled-clock oracle below. Both shipped TARCAN rosters have road/water
multipliers 1.0, so phase-dependent water flags do not change those speed or
turn-cost multipliers. The pitch section above shows why that does not
settle all bobbing/movement coupling; pitch/roll are simulation state and
rendering is separate. This does not establish all TARCAN parity.

### Surface-height kernels

`retailheight.h` implements retail's integer bilinear height sampler,
upright/hover clamp, floater waterline, rotated model-support height, and
tilting-hover bobbing with an explicit clock input.
`check_ground_height.py` compares 15,000 cases against original executable
execution; Debug and Release both pass. It includes 3,000 floater cases.
The kernels are wired into World as described in the section above.

Native per-unit tracing establishes that ground Y updates after the mover,
and feeds the next tick's water flag; World keeps a stored surface Y rather
than center-cell water depth. TARCAN's special hover bobbing uses a
wall-clock input; the deterministic adapter is documented above. The
calculation is verified with a controlled clock. Both shipped TARCAN rosters
have road/water multipliers 1.0, so its water flag does not alter speed or
turn-cost multipliers. The kernel includes pitch/roll ("Ground pitch and
speed limits").

### Corpse and statue navigation

Dead-unit records own installed corpse footprints in the shared feature
plane. Placement uses the original unit origin plus corpseadjustx/z, with the
selected feature's dimensions. Frozen/stone deaths install immediately;
ordinary wrecks install at death-animation completion.
The structure footprint is released separately. Reclaim, resurrection and
decomposition remove the installed feature footprint. Replacing a corpse retires
its owner, so later cleanup of that old unit cannot erase the replacement.
The body cache is refreshed before any corpse offset moves the record.
Ownership and active corpse lifetime/work state participate in the checksum.

`check_corpse_features.py` executes original 512ee0 corpse/stone/frozen dispatch,
495360 feature installation and 496380 removal, then compares every changed
feature cell after each event with the production World helpers. Placement
refusals and prior-feature removals are preserved, not filtered from results.
The emulator retains freed heap backing pages instead of entering captured OS
heap locks; feature logic itself executes natively. This test supplies feature
events and excludes combat/death-animation timing.

All 169 late-capture units in all three modes pass: 507 placement attempts,
246 accepted, followed by 507 retirement operations and 5,084 changed cells.
Both Debug and Release pass. The set includes the captured boat types. Runtime
regressions cover differing unit/wreck dimensions, offsets, mobile statues,
indestructible refusal, replacement ownership, decomposition, ordered reclaim,
and active search-cache updates.

Standard TARZOM names a `tarzom_dead` corpse, but Crusades TARZOM has no
ordinary corpse and the captured native feature table never loaded its
definition. The source feature exists in data.hpi, is nonblocking and 2x2, and
the port loads it through the standard registry. `--synthesize-missing-corpses`
supplies the native record (see "Retail audit: special terrain and domains");
the comparator never substitutes a different feature or silently skips it. A
source comparison of corpse/stone/frozen names and offsets for all 77 captured
unit types finds only this TARZOM difference between standard assets and the
Crusades capture.

### Mutable feature footprints

The navigation plane follows tracked features when they change shape: the
importer restores new feature anchors (for example the frozen statue at anchor
(294,49), queried by unit 786 at center (295,50), in both balance modes).

Initial map loading and runtime installation share footprint overlap and
indestructible-obstacle rules. `addFeature`, `swapFeature` and reclaim update
the plane; replacements free the old footprint before placing the new one.
Active search caches refresh even when the legacy obstacle bit does not change.
The plane is included in the state checksum. Initial map registration avoids
placing raw tracked features a second time over the resolved map plane.

The fixture restores feature identities/names through production placement,
including additions and changed types; it never imports native cell grades.
`check_map_features.py --fixture` compares every resulting cell, clearability
flag and footprint reference. All 230,400 cells match at ticks 959, 14468 and
37447 (plus 229,441 interior terrain quad lows). The mid fixture adds two
tracked features; the late fixture adds eight and replaces eight. Synthetic
regressions cover shrinking/expanding footprints, removal, replacement graph
cycles, indestructible refusals, partial removal before refusal, and active
cache updates.

At ticks 14468 and 37447, 30,000 live and 30,000 raw search queries match in
each balance mode. Standard-mode checks explicitly substitute the shipped
standard FBI/MOVEINFO inputs into both implementations; the original captures
are Crusades. This is controlled-state evidence, not fresh standard gameplay
captures or full trajectory parity. Release also passes the mid live check.
The middle capture includes two boats (WATER3/WATER5); the late capture includes
ten (WATER2 through WATER5). The comparator reports results per movement class
so boat coverage is visible separately from land units. In the late standard
raw check all 3,154 boat queries match, including 409 accepted queries.

Native `0x512ee0` dispatch is also checked on 256 corpse/stone/frozen calls
from the late capture, intercepting feature installation without changing its
arguments. The anchor is the original unit cell origin plus FBI corpse offsets,
independent of the replacement feature's footprint; installation and
retirement are checked by `check_corpse_features.py` above.

### Stored terrain flags

Traffic and path costs use stored road/water bits rather than recomputing
them during each speed query. The importer restores them and the state hash
includes them (the under-construction requester 845 against occupant 3737 is
the case that needs them). Road flags update at the movement phase; native
water flags compare unit integer Y with sea level, which World's stored
surface Y reproduces ("Surface height in World").

### Captured balance selection (probe 34)

Native unit loading selects `unitsCB` when global byte `0x641144` is nonzero
(`0x5174e0`, called at `0x4bf540`). All three existing early/mid/late captures
have that byte set. Probe 34 records the captured selection in the header,
loads the matching registry, and reports it in output metadata. The direct
grade comparator rejects pre-34 fixtures and mismatched balance settings.
Both values are covered by the importer test.

Captured ARAPULT/VERPULT use the shipped Crusades GROUND5 class (slope 30), not
standard GROUND4 (slope 15); the class parser and inheritance handle this, and
there is no global siege-unit slope override.

Native `0x421ae0` reads mover+36 bits 0x800/0x1000. `0x4dc800` updates
those bits at writes 0x4dc822 and 0x4dc844. Water uses integer unit Y relative
to sea level.

### Live movement grades (probe 33)

The `check_world_grade.py --live` comparison executes native `0x4db640`
against actual World `cellScore` at the same footprint center. All terrain
and feature checks precede the sorted distinct-body checks, matching the
native wrapper. Live terrain rejection is -1; an ordinary structure yields
0; unacceptable traffic yields 2. Acceptable traffic retains the terrain
grade. Live slope penalties remain
on roads, unlike raw search terrain grading. Grade 4 remains passable at
the normal movement threshold, though it ranks below grades 6 and 7.

Probe 33 restores every flyer's captured low ground/air mode, so an active
flying builder restored without a VTOL standby mission is not taken for a
landed ground occupant. Earlier probe formats are accepted but cannot
establish airborne occupancy parity.

On the early captured map, all 30,000 live queries match exactly, including
5,000 ramp candidates and 5,000 cells north of steep terrain. There are zero
step-acceptance differences at threshold 4. The raw comparison with 6,000
controlled requester/age inputs also passes. These checks observe possible
steps at an initial state; whole uphill/downhill trajectories are the ramp
routes in "Retail audit: special terrain and domains".

In the tick-14468 capture, Catapult/Trebuchet unit 3737's native type has hard
slopes 30/30 and depth limits 10/-10000, while its mover points at a native
grid with slope bytes 30/15/30/15: the balance selection above (Crusades
GROUND5) is what makes its grades match. Requester 845 at center (97,289)
needs the stored terrain flags.

### Raw grade integration

World's raw rectangle host respects projected boundary markers, resolves
feature footprint tails, assigns grade 1 to clearable blocking features, and
uses actual body footprints/yard cells for occupancy. A rectangle snapshot
keeps full cache refreshes from repeatedly scanning every unit for every cell.
The snapshot is derived from current units for each refresh, not persisted or
restored from native grade results.

The fixture importer restores mover+28 for landed flyers from initial
captured memory rather than substituting the capture tick, which would erase
their occupancy age (in the early capture unit 383 has stamp 877, not tick
959).

The importer and integrated host pass 30,000 stratified raw rectangle
queries against native `0x5088f0`, including 5,000 each for occupied, featured,
steep, north-of-steep, arbitrary and ramp-candidate cells. Each category mixes
single-cell and actual unit footprints. This is initial-state raw grading
coverage, not directional movement, dynamic obstacle lifecycle, or full-game
parity. An additional 6,000 queries with controlled requester IDs and
occupancy-age thresholds also match.

### Raw World grade audit and ramp coverage

`tools/re/check_world_grade.py` compares native `0x5088f0` with the
restored World's raw rectangle host without executing AI or simulation ticks.
The early tick-959 capture, seed `0x5088f0`, draws its queries by category
(occupied, features, steep cells with quad spread >30, 1–4 cells north of
steep terrain, arbitrary, and ramp candidates with quad spread 1–30),
alternating single-cell and unit footprints within every category. Retail
accepts roughly 68, 67, 65, 327, 544 and 626 of each 1,000 respectively.
These categories can overlap obstacles. A ramp candidate is a height-spread
sample, not proof of an entire traversable ramp; these are placement grades,
not directional uphill/downhill controller or route comparisons.

Native feature classification needs more than the blocking bit: blocking
features with flag `0x20000` yield grade 1. The loader initially sets that bit
for destructible features (`0x494178..0x4941b3`) but subsequently clears it
when the reachable replacement-feature chain includes an indestructible
blocking feature (`0x4945bb..0x494854`, links at feature offsets `0x12c` and
`0x12e`, confirmed as `featuredead` and `featureburnt`). Treating every
destructible feature as grade 1 is therefore wrong. The loader follows both
replacement links with cycle detection and
stores the clearability classification. Both Debug and Release map checks
match this flag on every named feature cell in the 230,400-cell captured map.
The integrated raw grade adapter consumes it together with actual
body footprints, preserving building blocking. Native data confirms destructible Valysia buildings can
lack this bit while trees and some walls have it.

## Flying-builder orbit and movement rate

Flying construction updates movement-rate flags and the `MoveRate` script
callback, then repositions its orbit with retail's four ordered random draws.
Probe 32 restores the two movement thresholds and build radius. Installing the
new controller clears its pending event bits.

`check_replay_fixture.py` replays the early save's tick bodies (34 transitions
through tick 993) and compares unit motion, flying-builder mission state,
construction work/HP, resource pools, 321 gameplay and 393 CRT draws. This
excludes outer-frame rendering and is not full-state parity. At tick 994
retail's next occupied base update is owner 7, slot 1, containing unit 3601,
whose native threat query returns true.

## Active flying construction

Probe 31 restores active flying construction separately from ground jobs.
The stage-5/6 loop consumes the bound-50 draw, works on its site and preserves
flight movement. Point arrival posts mission events while velocity decays.

Unit 1229's motion, including altitude, and its flying construction mission
state match the early-save transitions; site work/HP and resource pools also
match, and all gameplay draws agree. Unit 3601's live primary mission is
ground construction, despite patrol orders in the save. Two outer-frame CRT
draws are outside the tick-body replay. See [save coverage](retail-save-coverage.md).

## Partial path endpoints

Ground missions retain their requested destination separately from delivered
navigator points. Partial routes do not append that destination as an extra
corner (it would change unit 248's pruning and steering before it reaches the
partial endpoint). Its complete navigator route is compared from tick 10277
onward. Unit 146's route at tick 10122 is the boundary of the selected-route
comparisons: matching motion alone does not establish navigator-state parity.

## Strike reachability and cached flight sectors

Strike members run the original rectangle-connectivity rule on their cached
navigation plane. Two wall followers try to rejoin the direct L-shaped route;
long failed traversals terminate the query, while short failures retry source
perimeter cells in reverse order. Exhaustive flood fill is not equivalent.
`check_rectangle_reachability.py` matches 20,000 synthetic cases in each build,
covering both grade modes, bounded traversal, rectangular bodies and map edges.

Flight point-controller arrival posts both arrival and release events for move
and patrol missions. Flight terrain uses the center sector cached at the last
footprint relocation, rather than recalculating a sector from the current
footprint origin (which would descend early after a patrol turn). Probe 30
restores initial sector links from the captured entity pool and hashes that
state. Refused factory-yard requests at map boundaries send their script
event, as occupied-yard refusals do.

## Scout patrol replanning

Quiet-base unarmed scouts choose patrol points with retail's ordered radius,
angle and separation draws. Their initial flight-move mission snaps to the body
center, starts the flight script immediately, and hands off to a patrol that
appends its return anchor. The comparison checks the scout's entire mission
queue, including flags, pending events, deadlines and destinations.

## Construction routes, flight terrain, retry outcomes and factory yards

Completed mobile construction refreshes occupancy and cached search grades.
Build-rectangle paths retain their accepted perimeter endpoint instead of
appending the rectangle's nominal destination. Flight terrain sampling uses the
footprint-origin sector and the maximum corner height, including the water floor.
Navigator delivery retains the detour outcome, and partial/detour routes suppress
the ordinary 120-tick retry draw as in the original.

The factory comparison checks active thread headers for both the Keep and
the airborne parrot. Yard transitions wait for bodies occupying cells blocked
in the requested state (closing an occupied yard at once would produce VERMER
too early), and factory activation runs a zero-elapsed notification after the
regular script update. Yard checks distinguish upper/lowercase map cells;
tests exercise blocked requests and subsequent success after the occupant
leaves. The 1,024-case factory flag oracle matches.

## Strike mission replacement and airborne script notifications

The restored strike squad refreshes its ground movement mission at tick
10132. Its old navigator route survives mission replacement; the new controller
starts on the next unit update and requests the new destination. Mission targets
are stored separately from route endpoints, so delivered paths retain the
original grid waypoints. The comparison checks unit 927's mission fields and
complete navigator route after this replacement.

Airborne movers send the original `setSFXoccupy(5)` notification when their
occupancy changes, including the immediate zero-elapsed script update (without
it the parrot's flight animation lags and gameplay RNG differs from tick
10147). The harness compares its active script-thread headers from the first
boundary. Probe format 29 restores initial script occupancy alongside the unit
flags; it does not infer that state from later frames.

## Occupied base factory planning

Restored bases issue production orders using their captured ordered build
menus, availability limits and signed priority/preference weights. The factory
admission helper matches 8,192 original cases covering allocation thresholds,
already-queued production, cost-dependent deferral and RNG order. The World host
also handles healthy builders retaining active construction, busy combat units
deferring replanning, base growth thresholds and redistribution attempts with no
other occupied base. In the accelerated factory scene base 1 orders VERMER at
tick 10102 and makes an unsuccessful redistribution search at 10118; all state
comes from the initial boundary and the executable generates the extended
tick sequence.

The emulator hosts fixed `GlobalAlloc` and `GlobalLock` (a native allocation at
tick 10109) with the bounded allocator: they honor zero initialization and
reject movable flags or unknown handles. No game routine is bypassed; three
Python regressions cover that boundary.

## Unanchored base centres and planner predicates

The original unanchored base centre averages every live squad member, including
mobile units (not only structures). The accumulator preserves signed high-word coordinates, unsigned
32-bit summation and truncating signed division. A World regression distinguishes
the two outcomes; 4,096 original-executable centroid cases pass in both builds.

The planner's category predicates are isolated and checked against 16,384
original-executable cases covering all eight categories, invalid categories,
construction capability, mobile state, commander exclusions and mission flags.

The deterministic integer base-radius helper matches 7,168 original x87
calculations with the game's 53-bit control word. Weighted build selection
matches another 4,096 cases, including every RNG bound, final seed, special-type
filter and rejection when the selected type belongs to another faction.
Both checks pass in Debug and Release. The traced occupied-base update issues
a factory build order; treating it as clock/RNG-only would miss that behavior.
The availability/weight query matches 8,192 additional original cases, including
signed cached priorities and per-type population limits. Probe format 28 restores
ordered menus (including duplicates), faction markers, special-type flags and
owner preference bytes from the initial capture. Those inputs enter the state
hash; no future planner output is imported. Five decoder regressions cover
the format.

## New-unit AI assignment

Restored AI managers assign eligible new mobile units to the nearest occupied
base slot (1..20), falling back to slot 2, and apply the original offensive
standing orders. Probe format 27 restores the signed base-anchor references
from the initial capture's two anchor tables. Bases without a valid anchor use
the unanchored centre of the section above. Selection preserves the original separate
fixed-point square truncations, signed comparison and first-slot tie rule;
4,096 randomized original-executable cases pass in Debug and Release.
Captures without anchor data reject a selection that needs it.

Before unit updates, active squads refresh their completed-mobile presence
flag and mark themselves dirty only when that flag changes. The scene oracle
compares squad membership, countdowns, deadlines, active/dirty flags and nine
saved parameters at every boundary.
A World regression covers anchor selection, standing orders, next-tick
presence publication and anchor hashing; two decoder tests validate signed
anchor references and malformed vectors. Only initial captured state is
supplied to either engine; the original executable generates each subsequent
boundary.

## Mobile site birth in dispatch order

A builder arriving at its construction perimeter allocates the site during
its own unit update (after the unit loop, other builders would consume CRT
particle draws first and the wrong entity slot would be selected). After
allocation the loop reacquires references and restores unit-slot order,
so a higher-slot newborn receives its first update that same tick.

Restored mobile sites use captured build costs/max HP, begin at zero HP with
all work remaining, execute Create immediately, and enter GetBuilt. Their
builder starts the two-argument StartBuilding script, turns before waiting
for build stance, then uses the verified construction-work kernel. COB stance
writes publish the original mission wake event. Factory and mobile creation
share initial site setup.

`check_factory_completion.py --natural-production` leaves the type catalogue
unchanged and compares unit identities/motion, construction/resources and
gameplay/CRT draws at every original simulation boundary. The comparison runs
without outer rendering in either engine; it is not live-frame parity.

## Feature footprint origins

Map feature records identify the footprint's top-left cell. Their world centre
is `(cell * 16 + footprint * 8)` on each axis (original `4931e0`); taking the
record as the centre cell would shift every multi-cell obstacle up and left.
Registration, rendering and restored presence agree on the origin;
replacement preserves it, and reclaim frees the same cells.
The renderer looks up tracked features by stable cell ID rather than centre.

A 2x2 ZonTree201 regression checks the blocked cells, clear neighbours, world
centre and reclaim cleanup. The initial-presence importer (probe format 26)
reads only the captured initial feature plane and validates its catalogue;
five synthetic tests cover origin markers, absent data and malformed inputs.
Changed saved feature types are restored by the mutable-footprint fixture
above.

## Factory completion and ground parking

Restored factories complete ordinary ground outputs, emit their final
construction particles, notify StopBuilding/Deactivate, and clear the factory
target. GetBuilt installs the original randomized PARK mission when there is
no rally queue. PARK requests an annular pathfinding goal, preserves navigator
retry timing and steers toward the ring midpoint. Its stages, target loss,
retry draws and requested radii match 8,192 executable cases; ring steering
matches another 4,096 cases in addition to the goal-query checks.

Factory Create runs immediately with zero elapsed time before work payment,
so even an output completed on its birth tick sees its unfinished state.
StartBuilding receives the original argument-stack layout. Script activation
reads the script activation flag; BUILD_PERCENT_LEFT follows the original
1..100 mapping (8,198 executable cases). Restored construction units retain
their captured maximum HP after completion for script health and regeneration.
This matters for VERPULT: the captured maximum is 1,289 while the loaded asset
default is 2,289, which otherwise starts damage-smoke RNG on a full-health unit.

`check_factory_completion.py` changes only the initial VERPULT cost and inverse
build time in both engines and compares every unit's motion, construction
work/HP, resources and gameplay/CRT draws at each boundary. Both executions
omit outer rendering; this is a controlled variant, not a whole-scene/live-
capture parity claim.

## Mobile construction completion

Completed mobile-builder jobs apply the verified completion eligibility
kernel, notify `StopBuilding` immediately, release the builder, and retire the
site's waiting mission in unit-slot order. Immediate COB notifications run the
whole script scheduler with zero elapsed time, matching original thread and
piece behavior for both captured builders over 31 boundaries.

Probe format 25 restores inactive navigator search stamps and the active
builder's remaining 61 queued builds (115 queued builds total). Activating a
queued build cancels and replaces its path request while preserving the old
search stamp for the random retry cadence.

`check_mobile_completion.py` changes only initial remaining work to exercise
completion of site 389 or 700 in both engines. Both variants match all 47 unit
motion states, construction progress/HP, waiting missions, resource pools and
all gameplay/CRT draws for that tick: 29/9 draws for 389 and 28/9 for 700.
This is a controlled executable comparison, not an event observed in the
unmodified live trace, whose first missing CRT call is the outer renderer at
tick 10074. CTest covers completion and finished-site death.

## Unfinished construction mission host

Probe format 24 restores each site's GetBuilt waiting mission, including its
builder, stage, wait mask, deadline and pending events. New factory sites start
that mission when created. World consumes construction events in slot order,
postpones abandonment decay for 30 ticks after work, and applies the original
half-unit unconjure step only when the waiting deadline expires. Sites with
these missions have no separate per-tick orphan decay. Mission state is hashed.

The waiting handler matches 4,096 original-executable cases. The executable
comparison checks all 11 GetBuilt mission observations in the first five ticks,
including newly allocated unit 806, as well as remaining work, HP and resource
pools.

The `429990` completion eligibility/flag kernel separately matches 8,192
original-executable cases. Alliance, attachment, activation and network
notifications are observed callback hosts in this comparison.

## Visual-quality CRT decision

`client/retailquality.h` reproduces the fixed-percentage and adaptive visual
refresh decision (`4ec7cb..4ec879`). Adaptive smoothing advances once per
selected unit, not once per frame or simulation tick. Above the FPS threshold
it consumes no random value; at or below the threshold it samples the shared
CRT stream. Debug, Release and Clang pass 8,192 original-executable comparisons
of decision, smoothing, probability, call site and final random seed.
The helper is used by `retail_visual_test`; it is not part of the renderer
or World (the decision draws from the outer frame's CRT stream).

`check_render_quality_prefix.py` executes original visibility selection,
row binning and quality arithmetic after the first five emulated ticks. With
the captured camera retained, it selects 24 units and moves the smoothing
counter from 60 to 36. The last five units (56, 259, 53, 146, 282) produce exactly
the five recorded `4ec858` calls after tick 10074, with matching seeds. This
is a conditional diagnostic, not full renderer replay: camera input, drawing
side effects and outer-frame timing are excluded. Attempting the enclosing
camera update reaches an uncaptured native call at `7b9ab390`, returning to
`48c65b`. The fixture contains no recorded cursor/clock inputs. Its renderer
calls occur at irregular ticks (10074, 10079, 10080, 10081, 10091), so a fixed
render-per-tick or render-per-batch substitution is not justified.

## Restored construction and ranked allocation

Probe format 23 restores initial active construction missions, float resource
accounting, construction emitters and per-player entity pools. World advances
emitters and construction in retail slot order. Factory births use the verified
ranked-free-slot allocator and immediately participate in construction; a site
with zero HP remains alive while unfinished. The new factory demand contributes
to the next tick's allocation, and factory payment precedes later builders.
Initial catalogue geometry and quality settings supply fresh emitter profiles.
Their arithmetic matches 4,096 original-executable cases and all 47 initial
captured emitters. Changes in renderer quality during play are not modeled.

The initial-state replay creates unit **806 at tick 10074**. Its first **44
CRT calls** match tick, call site and seed; the next CRT call is the outer
renderer's `4ec858`, after tick 10074. No captured seeds, later unit IDs or
draw counts are injected. All **518 gameplay RNG calls** and all restored
movement tracks match. `check_replay_construction.py` compares remaining
work, HP and both resource pools with executable tick replay at all five
boundaries before that renderer call, including newborn 806. Outer-frame
render cadence is outside the replay, so these results do not establish
whole-game parity.

## Sacred-site income and saved type economy

World reads feature `sacredsite` multipliers and applies the original
footprint coverage rule to units with uppercase `S` yard cells. Map feature
coordinates are footprint origins, not centres. A partially covered deposit
provides no income; a fully covered 1.5/2-strength stone scales the lodestone's
base production. The original x-then-z scan, including its last-site rule for
mixed coverage, matches 1,024 original-executable cases in Debug and Release.

Probe format 19 restores initial type income/storage from the catalogue.
These values can differ from asset defaults (the captured VERMAGE produces 15
rather than the file's 10). They are validated, hashed and used by the economy.
Resource snapshots are included in diagnostic frame output. The restored scene
produces the retail aggregates 47 and 31 mana/second for players 0 and 1.
Construction-state decoding has six rejection/restore tests; its initial
snapshot contains jobs 338->389 and 827->700 and 47 empty emitters.

## Build-priority cache integration

Probe format 18 restores initial catalogue order, desired composition, cached
priorities, resource pools and thirty income/use samples. The decoder validates
owned/completed counts against the initial unit records and rejects missing,
corrupt or nonfinite state. World refreshes scores using current live unit and
construction counts. Income and spending advance the history; the pressure
calculation excludes the newest sample, as retail does. Cached scores, inputs
and resource history participate in the state hash.

All **518 gameplay RNG calls** in `reload-crt-state-01` match tick, bound and
seed, and all 22 restored ground movers, flight 821 and builder 281 match all
31 boundaries. Matching gameplay draws and restored movement do not by
themselves prove complete scene equality.

The resource-history oracle passes 4,096 original-executable cases, including
sample rolling and strict pressure thresholds; the score oracle passes 8,192
cases.

Original tick execution shows builders 338 and 827 emitting paired
construction particles for sites 389 and 700 before the first factory
allocation. The `429af0` construction/unconjure accounting kernel passes
8,192 original-executable comparisons, with completion/death callbacks
controlled. It records requested use before affordability scaling and uses a
separate remaining-work fraction. The aggregate resource accounting
(`51d837`) and end-of-tick clamp (`401270`) pass another 8,192 original
comparisons: current storage is float, lifetime produced/excess totals are
double, the tick multiplier is decimal `0.03333333`, and clamping occurs after
construction work. Capacity overrides suppress income.

The construction particle kernel (`4f1430`, `4f1310`, `4f12d0`) passes 4,096
original comparisons of admission, final CRT seed, position, vertical speed,
and expiry. Allocation and animation are controlled hosts; original CRT and
trig execute. The second random draw wraps a 32-bit left shift before signed
division, so a draw of 16384 produces speed 65536, not 196608. Slots expire on
the update after crossing the vertical bound. World runs them
(`retailconstructionparticles.h`). Initial emitters for builders 338/827 and
site 389 have capacity 5; site 700 has capacity 40; all four lists are
initially empty. Render-cadence CRT consumers are outside the World; no later
seeds, captured draw counts or forced allocation identities are injected.

## Restored AI schedules and mobile scripts

World restores the retail AI assignment countdown, all 100 squad slots,
deadlines, kinds, parameters and membership from the initial capture (probe
format 17). The dispatcher visits slots 0 through 98, refreshes membership once
per dispatch and preserves signed countdown and unsigned deadline behavior.
The scheduler matches 4,096 original-executable cases with controlled planner
callbacks. Complete empty base, strike, backup, VTOL and reserve handlers match
another 4,096 cases. Debug, Release and Clang agree. Restored World execution
never silently treats occupied groups as empty. The normal command-emitting
skirmish AI is separate.

Simulation COB execution includes mobile and nonfactory units. Their saved
threads are restored and execute before unit movement, consuming the shared
gameplay RNG. All script state participates in the lockstep hash. The
interpreter preserves PLAY_SOUND's stack/result behavior while World leaves
audio to the client.
The controlled interpreter oracle passes 315 programs / 6,554 boundaries,
including sound return values. All 202 shipped unit scripts complete 10,000
updates through the lifecycle smoke harness. This does not establish parity
for every host query, movement/weapon notification or effect callback.

The build-priority refresh draws at `410b6a` (bound 40); matching bounds alone
can hide an earlier call-site difference, so the replay compares call sites
too.

The isolated `410810` build-priority scorer matches 8,192 original-executable
cases in Debug, Release and Clang, including every random bound, order and final
seed. The oracle controls availability classification and explicitly selects
53-bit x87 precision. It covers current/completed counts, desired composition,
cost penalties and resource pressure; the World host supplies construction
counts, catalogue ordering and resource history ("Build-priority cache
integration").

## Ground responses, controller reset and corner pruning

The saved-game probe restores signed ground-response modes, return positions,
move/fire orders and stage-three resumption. World can suspend a ground move for
an auxiliary attack, retain its original position and install the bounded return
leg before resuming. The mission kernel passes 4,096 controlled comparisons with
the original handler, including positive-mode leash checks and negative-mode
return geometry. Target scoring, retaliation eligibility and auxiliary combat
are World's combat host; this kernel does not establish retail combat parity.

Replacing a ground controller cancels its old search **and queues the new
one**, preserving the admission stamp when retail does. Original-handler execution from
the captured state confirms the pending request and preserved stamp. Regression
tests require the replacement request and reject stale completions.

Clear-land steering scans prune corners before selecting the steering aim.
They check eight neighboring footprints, the 32/144-pixel distance gates,
distances to the two segment lines and turn-angle/rate comparisons. Removing a
corner repeats the scan, and queued commands beyond the current leg are not
look-ahead points. Blocked neighboring footprints keep the corner. The complete
original land-scan routine, with controlled terrain and navigator callbacks,
matches 8,192 cases in Debug, Release and a Clang build. The boat branch is in
"Verified boat scan rules".

Player bookkeeping preserves its seven-tick clock and bound-30 draw for
human and AI players, after unit/search/production updates and before wind.
This is separate from AI planning. World answers availability/count queries
directly rather than retaining retail's delayed caches. The clock passes
4,096 original-executable cases including unsigned wrap and rebuild selection.
Probe format 16 restores these clocks from the initial memory snapshot, validates
their player owners and never borrows later-frame pointers or seeds.

AI group planning draws at `40b332` (bound 30, tick 10084 in the CRT-state
save fixture, whose group is empty in the initial snapshot with deadline
10084); the owner runs a separate group scheduler, periodic group assignment
and unit maintenance. World regressions cover attack/resume, player clock
hashing, replacement searches and blocked/clear corner handling; the corner
oracle checks the geometry across compilers.

## Factory scripts, VTOL landing and unit initialization

Factories with QueryBuildInfo execute their COB threads in World. The
interpreter preserves the sixteen saved thread slots, separate CALL children,
integer sleeps, signal masks, statics and piece motion. Restored factory threads
run before production in each unit's update. Readiness gates creation, and
QueryBuildInfo selects the animated model piece used for the output position.
3DO origins retain their original integer precision; hierarchical rotations
round after each plane, and the loader's X/Z mirroring is applied explicitly.
COB and 3DO files participate in the multiplayer gameplay-data hash and are
excluded from cosmetic-only overrides.

The controlled original-executable checks pass 312 thread programs / 6,654
boundaries, 6,000 motion updates, 100 complete saved Keep script updates and
10,030 integer rotations. The restored Keep signals ready on update five.
The World replay creates its first factory output at tick 10074 with
exact raw coordinates (531124978, 4196444, 479025836), heading 32768 and base
speed 50540. Spawn consumes the heading-range and variation-phase draws
before randomized speed, including the zero-width heading draw. Explicit spawn
headings override the drawn heading after those calls. All eleven shipped
factory scripts pass 10,000 updates each across
Create/Activate/StartBuilding/StopBuilding/Deactivate.
Probe format 14 restores activation, readiness, yard-open and
bugger-off values from the initial entity pool; those values are not in the
saved VM blob. All 1,024 synthetic flag combinations match the original unit
getters. A counterfactual initially-ready factory creates its output on the
first update instead of waiting for a script signal that may already have
happened. Invalid flag values and malformed script hex are rejected.

World runs the ordinary VTOL_Standby and VTOL_LandIfCan stages, with saved
ground/air mode, altitude, timers and events restored by probe format 13.
Landing searches belong to mission transitions instead of running every idle
tick. The controlled original-handler comparisons pass 1,024 standby cases,
1,024 candidate/orbit searches and 2,048 landing mission cases. These isolate
host inputs; the landing predicate is "Retail audit: flyer landing site and
airborne occupancy".

Unit 927's Move_Ground timer draws at tick 10075 (402dc2, bound 5) with
response mode -1: stage two invokes target selection and retaliation, and
stage three handles returning from an auxiliary attack, so this mode is not a
plain move ("Ground responses, controller reset and corner pruning"). A
synthetic factory CTest shows the script's readiness tick and subpixel build
point control World. Clang-built script, piece-rotation and landing/standby
kernels also match the original-executable fixtures.

## Navigator admission and script snapshots

Retail stamps a pending request at admission (navigator 927 on tick 10071 in
the CRT capture), not at delivery. `retailAdmitNavigator` implements `4e54a0`: require a pending request, compare the current unsigned
tick with stamp+15, and stamp immediately when selected. Selection can exhaust
the scheduler budget before initialization; resumed work does not re-admit.
Delivery and repeated-refusal requests preserve that stamp. A blocked unit
retries 120 searches per minute, with no random retry delay in the
repeated-refusal branch.

The admission oracle matches 4,072 original-executable cases, including overflow
and absent requests; the scheduler matches 106 scenarios / 1,233 boundaries.
VTOL_Standby draws at `41769e` (bound 7; unit 308 at tick 10073).

The save decoder associates `ScriptN` by the unit's saved index, not the
script blob's metadata (which retains the previous BANK selection). Keep 540
is saved index 37 and owns Script37. Script decoding preserves all sixteen
0xa4-byte thread records, clears each transient +20 pointer as the original
loader does, and preserves the header trailer. Static/piece counts are accepted
only from the owning COB. All 47 saved script headers/threads match executed
`56dc90..56dcd5`. Restored scripts execute in World as described in "Factory
scripts, VTOL landing and unit initialization".

## Wind, stationary guards and factory output

Wind belongs to World, with its range loaded from the map, its state hashed,
and its renderer/COB delivery carried by the render snapshot. `retailwind.h`
implements `525170`: the strict deadline comparison, shared-RNG speed/bearing
choices, quantized direction vector, and CRT-generated interval. The comparison
is `abs(z) > x`, including signed X. The two branches wait 3..8 or 5..14 seconds.
The live engine uses a separate deterministic wind interval stream. Retail's
render-side CRT consumers are not ported, so this is not a claim of full CRT
call order or long-run wind deadline parity.

Stationary Guard_NoMove missions own their attack-acquisition timer instead
of a generic unit-ID acquisition cadence. Armed guards scan then sleep
7..21 ticks; unarmed guards sleep 150 ticks without consuming game RNG. Firing
updates each tick.
`check_wind.py` and `check_stationary_guard.py` each match 4,000 original-executable
fixtures under GCC and Clang, including final RNG and mission state.

Probe format 11 restores wind only when the same initial boundary contains its
fields and CRT seed; captures without them omit wind rather than borrow
another recording's state. The runtime recorder includes the wind fields.
The CRT capture (`reload-crt-state-01`, 31 frames) restores all four
stationary guards.

The unit that appears at tick 10074 in the captures is a new Catapult
(`VERPULT`) created by Keep 540. Its ID is 549 in the runtime-only recording
and 806 in the separate CRT recording; these are different executions and
their state must never be combined.

Factory output exists as an unfinished unit while mana and work accumulate.
Completion releases that same unit and applies rally orders; it does not spawn
a second unit. Cancellation releases the site to decay, dead output is not
resurrected by construction, and the admitted site can finish at the unit cap.
Progress, the site attachment, construction state and queued types are hashed.
The price/duration tests measure completed output separately from site
creation and enforce the full price and build duration.

BuildingBuild stage 1 waits for COB readiness (`4d4b90`, unit byte +12f bit 0), then stage 2 calls QueryBuildInfo and
creates/attaches the output (`401d40..401ff7`). Keep's Activate/RequestState/Go
chain opens the yard and sets COB unit value 5. The save contains its running
script threads: reader `56dc00` checks a 0xa48-byte header, four bytes per static,
and 0x6c bytes per piece; its sixteen thread records are 0xa4 bytes each.
World restores these threads and the piece-derived spawn point ("Factory
scripts, VTOL landing and unit initialization").

## Live flight movement and patrol

World stores and hashes absolute flight altitude, three-dimensional velocity,
navigator destination/velocity/heading, and positional flight controllers.
Active flight uses the verified `retailflight.h` velocity calculation
(`4da803` through `4dad1a`) and navigator update (`524af0`). Cruise height follows the maximum terrain
height in the surrounding 3x3 group of 128-pixel sectors.

Unarmed positional patrols use the retail mission dispatcher, randomized
overshoot/radius construction, timer waits and arrival events. Arrival signals
the mission while retaining movement on that tick; subsequent dispatch rotates
the patrol and replaces its controller. The repeated random expressions in
`41a47e..41a613` deliberately consume separate draws.

The diagnostic importer (format 10) restores saved patrol mission/controller
and velocity state, with altitude from the initial capture boundary. Only
initial state is restored: no future positions or random draws are supplied.
Unit 821's position **including Y**, heading and speed match all 61 captured
frames, as does builder 281. The world wind update draws at `52519e` (bound
4975).

40,000 velocity and 40,000 navigator fixtures match the original executable
under GCC Release and Clang `-O2` with contraction disabled.
`check_flight_patrol.py` adds 4,000 original-handler comparisons of patrol
points, radii and final RNG state. Motion regressions cover live ascent,
absolute terrain height, hashed momentum, arrival boundaries and patrol
rotation. The full-memory capture used to validate the isolated mover has a
different RNG seed from the runtime replay; their state is never combined.

## Simulation-first investigation

**Shared cached search integration:** PathService uses the
verified singleton scheduler and resumable worker together. Allocated player
slots, including holes, consume the seven-unit scan cost. Cache preparation,
retry and finish follow that single worker's lifetime; cancellation also handles
an admission that exhausted its budget before initialization. World maintains
footprint-specific cached grade planes, ages bodies at the verified thresholds,
refreshes old footprints when bodies move and updates terrain changes in place.
Death and embarkation retire the body's search/cache contribution. Navigation
class replacement clears caches before their grid pointers become invalid.

The diagnostic importer (format 8) restores initial entity traversal cursors,
body timestamps, route outcomes and captured packed grade planes. It queues
restored requests **after** installing their mission radius (queueing first
would search with the default radius). Only initial state is restored. No
future routes, grades or RNG draws are injected.

Controller replacement reanchors direct two-point segments and expires old
route stamps. Route delivery clears the obstacle-scan deadline (`4e4f39`),
including empty deliveries, so the next mover update refreshes steering mode.
Ground arrival applies inactive braking on the arrival tick,
before the mission is retired. Regressions cover these transitions and cache
ownership through admission, cancellation and successive searches.

The **VTOL_Patrol** handler `419db0` draws at `41a45c` (bound 10). Restoring
an initial captured cache does not certify cache generation; the terrain-cache
comparison does ("Terrain-cache integration and current heading at cost
handoff"). The executable scheduler, whole-worker, cached-footprint/body and
grade/preparation oracles pass.

Normal `queueBuild` commands use the rectangular approach too, delay site
creation until arrival, and compute each queued approach when it becomes
active. A replacement build cancels the previous construction assignment.
The build ghost uses the site coordinates independently of the approach point.
The diagnostic importer (format 9) restores builder 281's 54 queued stage-zero
MobileBuild orders without skipping unsupported predecessors.

**Standby and builder approach:** World dispatches the verified Standby
stage/timer core in the per-unit update, interleaved with movement.
`check_standby.py` checks 512 real-handler fixtures with controlled host
callbacks; target acquisition is World's own, so retail diversion, weapon
initialization and complete target selection are not certified by it. The
partial importer (format 6) restores ten captured Standby states and disables
unsupported idle handlers instead of initializing them as Standby. These
states participate in the lockstep hash.

Saved controller kind 6 is a rectangular perimeter, not a circle or ring.
Its bounds match the actual `4e3632..4e364a` loader. `RetailRectGoal` also
implements the initial navigation point (`4e38e0`), verified in 2,000 cases.
PathService accepts rectangle goals and enumerates every perimeter cell;
regressions cover an obstructed initial destination with a reachable alternate
edge, including footprint-coordinate offsets. World preserves the owning build
order through route replacement, emits arrival on the perimeter and defers
placement without an extra movement step into the site. Inactive-navigator
braking follows the uncapped branch at `4d9aec`; the construction that follows
is "Rectangular build approaches through placement handoff".

Builder 281's restored approach matches position, heading, speed and base
speed in all 61 frames, through tick 10129. Construction holds the mover once
in range (no second pivot/move by the ordinary mover), uses the verified
direction kernel, and starts the first pivot on the deferred placement
dispatch; a regression checks one pivot per tick and stationary placement.
Matching tick, bound and seed alone does not certify caller identity, so the
replay compares call sites; the builder owns a `4e5482` draw.

The diagnostic runner's optional `--grades-unit ID` exports a unit's
**initial** full search plane in footprint-origin coordinates. For Hunter 56
both captured cached planes grade (172,73) and (171,72) 6: unit 296 occupies
that area with mover `+28 == 10066`, three ticks before the snapshot, and the
raw cached query ignores such a fresh body, where a live traffic query would
grade it 5 and introduce an earlier corner. No later frame, captured route,
or replacement grade is injected into the simulation.

`retailgrade.h` ports `508cd0`'s ordered footprint/border queries and
`508b74..508bbe`'s ordinary-body timestamp grading. `check_cached_footprint.py`
verifies 3,000 ordered-border fixtures and 3,000 actual raw-rectangle body
cases. The shared cache lifecycle, raw terrain grading, simulation visibility
and singleton scheduler work together; an arbitrary traffic exemption in the
live query would not reproduce that state machine.

**Search runs after movement:** retail's outer tick calls the unit update `51d3e0` at `5263aa`; that update dispatches
missions (`51e1e5`) and moves units (`51e215`). Only afterward does `526411`
call `4f6c70`, whose scheduler call is `4f6ca0 -> 416430`. World runs its
search work and notification drain after the unit loop, so a newly admitted
search reads the position and occupancy after movement (installing a newly
completed route before the mover runs changes the very first step).

A synthetic regression checks that enabling a background search leaves the
first step equal to the same mover with search disabled, and that crossing a
footprint-cell boundary before admission changes the new route's anchor to
the cell just entered.

From the same (2896,1312) anchor, Hunter 56's first corner in retail is
(2688,1104). A one-BAM heading difference between two diagonal segments of
different lengths is not evidence of an angle-kernel bug when their steering
inputs differ. The comparison report names `first_restored_ground_mismatch`
so an unsupported unit (for example a ZONHAND with MobileBuild orders) does
not hide a ground-order divergence; that scoped comparison still rejects
missing units and differing timelines and does not replace the full check.

Unit 49's **Standby** handler `407770`, stage 1, deadline 10070, draws
`rand(7)` at `40786e`, followed by a 7-tick base delay and wait bit `0x20`;
the real handler also performs diversion and target/order checks. Running
this captured mission in isolation stops at `41265a` on uncaptured address
`1a85f390`, before the RNG call.

**World ground-order integration:** plain issued ground moves own dispatcher
state, an expanding radius and a unique
controller token on the goal order. Intermediate waypoints do not own missions;
route replacement preserves the goal state and the later command queue. World
dispatches these missions before search work, then drains search notifications
into the matching controller for the next dispatch. A reset cancels the old
search without deleting its existing navigator segment. Arrival sets event
`0x100`; the dispatcher retires that goal and initializes the next queued move.
The mission fields and controller sequence participate in the lockstep hash.

The radius constructor (`4e2500`) matches 1,013 executable fixtures, including
signed overflow; the 3,000 controller-query fixtures match.
World regressions cover captured-style failure/reset, route and queue retention,
arrival/next-goal dispatch, hashing, and an actual early search failure that
cancels the unfinished cost search on the following tick before delivery.

Failure-expanded goal circles change crowd stopping distances, so traffic
scoring tolerance is not mission completion. The group tests retain each
mission's radius and check completion within that circle (allowing cell
quantization), with collision checks. All 16 repeatedly commanded units
complete within their circles, and all 24 units of the 24-unit army reach
their expanded circles (19 are within a fixed 144px ring). The
navigator-only cadence fixture disables the mission handler so it tests retry
behavior independently of goal completion.

The partial restore format imports supported captured primary ground mission
stages, events, masks, deadlines, flags and radii (21 missions in the
Hunter-group capture). These are initial-state restores, not later-frame
corrections. The 147 captured ground-core oracle cases and the World
early-notification regression pass.

**Primary dispatcher and normal ground-order core:** `retailmission.h` ports `4d8450` event filtering, unsigned deadline expiry, handler result
transitions, queue callbacks, random delay and the 100-handler limit. The
queue head and unit eligibility are rechecked after each handler. The queue
host owns controller lifetime and idle-order creation. This is distinct
from the campaign `MissionScript` system.

`check_mission_dispatch.py` matches the executable in 327 controlled-handler
fixtures / 8,665 handler calls, including all return codes, queue rotation,
deadline wrap, stage-byte overflow and loop-limit cleanup. This isolates the
dispatcher; queue destruction and handlers are controlled callbacks on both
sides. `retailPlainGroundMove` ports the normal response-mode-zero
states 0–2 of `402b00`, after the diversion checks. It preserves the `rand(10)`
flag check, `rand(5)+5` polling, and footprint-scaled radius growth on navigation
failure. Stage 3 is the response handler ("Ground responses, controller reset
and corner pruning").

`check_ground_missions.py` compares this C++ core and dispatcher with actual
captured retail handlers: 147 cases / 259 handler calls over 21 units match
stage, mask, deadline, pending events, flags, radius, unit events, RNG seed and
bounds, controller-reset count and active-search ownership. Inputs include the
captured state and explicitly counterfactual event, deadline, starting-stage
and 0x08000000 flag changes. Unit 927 has response mode -1 and is never
treated as mode zero. These isolated per-unit runs are not whole-tick RNG
ordering or complete-state parity.

**Early search notifications:** `PathService` accepts an optional nonzero
controller identity and exposes `takeNotifications()`. Phase-1 notifications
are retained immediately even when phase 2 is still searching. Re-requests from
the same controller preserve progress; a different controller starts a fresh
search even at identical coordinates. Cancellation clears undelivered events.
Consumers must drain events and match their controller identity before the
next mission dispatch. Requests with identity zero are route-only.
Regression tests cover the pre-completion event, replacement and cancellation,
including the real search worker feeding the ground mission reset and thereby
cancelling its unfinished search before route delivery.

**Order/search ownership:** explicit move, wait, ambush, build, reclaim and
repair replacements retire the previous search before installing their
orders. `dropLeg` also cancels its departing request and resets
the route stamp, allowing the next leg to request its own route. Queuing an
order behind the current leg preserves the current search; cadence re-requests
within the same order preserve search progress.

Two World regressions replace a move with a wait or ambush and check that no
pending search's delivery erases the new command or a queued move. Another
retires an unreachable leg while a low-budget search is unfinished, verifies
cancellation, and verifies that the next leg requests its own search. These
are gameplay ownership checks; the dispatcher itself is checked above.

`emureload.py --mission` reports individual dispatcher handler entries and
returns: consumed events, stages, controller/search pointers, deadline/mask and
RNG call indices. In `mission53-lifecycle-01.json`, the handler stages are
2 -> 0 -> 1. Stage 2 consumes 0x2000 and returns reset; stage 0 replaces the
controller and cancels the search; stage 1 schedules `rand(5)+5` ticks and
returns advance to state 2 with wait mask 0x2701. Counterfactual runs
independently remove or set the pending event: zero or 0x1000 dispatches no handler and preserves the active search,
whereas 0x2000 or 0x3000 reproduces the reset. These runs start from the baseline
seed, not the real per-unit handler-entry seed after preceding units execute.
Hunter 40 follows the same state sequence but preserves Hunter 53's active
search: cancellation is scoped to the replaced controller. Hunter 53's
captured navigator bytes change only at the controller pointer; its existing
route is retained during this restart. The runtime-state capture lacks the
game-memory snapshot required for full tick-body emulation, so isolated
mission execution is a bounded diagnostic on that fixture.

Renderer replay is not a prerequisite for movement work: no comparison
establishes different pathfinding algorithms across retail's three rendering
backends. Rendering and entity-slot allocation do share CRT random-stream
usage, which matters to whole-process replay.

At tick 10070 in `reload-runtime-state-03.json`, retail keeps Hunter 53's
route and speed 128461, moving it to x_raw 188567463, and its first RNG call
is bound 5 at caller `402dc2`. Invoking the actual retail mission dispatcher
`4d8450` on captured Hunter 53 reproduces that bound-5 call and clears the
active search pointer (367411384 to zero), replacing the movement controller,
with no emulation error or unmapped access (substitute allocations, so not a
complete-state match). Mission processing cancels the search before route
delivery; a pending search is never discarded unconditionally.

## OS and Vulkan return observations

These are tools for recording a live retail process under Wine and replaying
its native calls in the emulator (`tools/re`); they are diagnostic
infrastructure for renderer and whole-process replay, not movement parity.

**Recorder and probe recovery.** `probe_guard.py` persists the original
prefixes before any probe is installed, snapshots the debug registers and
restores them per thread during external recovery (a surviving hardware
breakpoint traps as `EXCEPTION_SINGLE_STEP` at a reserved probe site, so byte
restoration alone is not recovery). Recovery checks process start time and
every prefix before writing, and normal cleanup marks verified restoration;
malformed records and overlapping probes are rejected before attachment.
Observation processing runs in the outer stop-event loop, never inside Python
breakpoint callbacks: the callbacks queue probe installation/replacement and
the loop creates or deletes breakpoints (dynamically replacing hardware
breakpoints from a callback crashes GDB 17.2-2.fc44, which the isolated
32-bit reproducer `check_native_return_probe.py --mode callback` shows without
retail; `--mode loop` and `--mode deferred` complete 400 entries and 400
returns, nesting depth four, exit 42). `check_probe_cleanup.py` (an ELF target
with normal, Python-exception and forced-debugger-kill cases, verifying an
actual `cc` byte through `/proc/PID/mem`) and `check_wine_return_probe.py` (a
PE fixture under the Proton Wine binary in a fresh private prefix, 800 nested
dispatcher entries/returns with Windows API callbacks and performance-counter
queries; `--native-dispatcher` traces the real NT dispatcher with a competing
second thread, 803 entries and returns, all 300 reserved-probe hits) exercise
recovery. The stopped-target forced-kill test does not establish recovery
from every possible asynchronous debugger failure. Native module addresses
must be rediscovered after each restart. A recorded call tree (for example
boundaries 10303–10309: 1,141 syscalls, 126 Unix calls and 49 callbacks, all
with captured syscall parents, maximum nesting depth five) is validated
strictly.

**Syscall and Unix-bridge returns.** Wine i386's normal syscall exit at
`f7cb7471` and Unix bridge exit at `f7cb7576` both read the saved return
address at syscall-frame `+8`. A single hardware **read watchpoint** on that
field observes both paths without code patches, retaining the
tick/game-RNG/CRT breakpoints. The frame is obtained from the simulation TEB
(`3e2000`) at `+218`. Completed-call records contain bounded parameter/pointee
samples, not a reconstructed OS or GPU checkpoint; replay must identify the
API and copy only its defined output fields. Unix outputs are sampled through
entry-saved arguments (Wine overwrites the parameter slot with its return
address), and the recorder stops immediately on errors.

**Replay of recorded outputs.** `emurender.py` supports recorded cursor,
monitor, display-mode and Vulkan format-capability outputs. Cursor writes only
the eight-byte POINT. Monitor replay preserves name-buffer padding;
display-mode replay requires the device name to alias a preceding captured
monitor output. Vulkan format replay checks the entire output extension chain
before writing and preserves relocated `sType`/`pNext` inputs. `emuvulkan.py`
adds the observed image-format, create, memory-requirements, bind and destroy
API contracts. Resource handles are recorded external outputs; no GPU work or
worker concurrency is reproduced. `--recorded-unmap` checks recorded
calls/results but retains inert pages, so shared-view lifetime/aliasing is
unverified. The recorder captures bounded Vulkan image queue-family and
view-format arrays and `pNext` chains. `--map-view-return` captures successful
`NtMapViewOfSection` outputs and returned bytes, bounded to 32 MiB per view and
128 MiB per capture, rejecting overlap with emulator scratch memory.
Mapped-view replay restores observed bytes and defined outputs; it does not
reconstruct shared alias coherence, view lifetime, or pre-call in/out values.
Render batch sizes vary (five, then two), so a fixed render cadence is wrong;
`--whole-frame` continues from the captured stack through the actual outer
loop instead of manually sequencing render fragments, with strict
recorded-input checks on its native dependencies.

**Window messages and user callbacks.** `NtUserDispatchMessage` (`7b4e093c`)
dispatching `WM_PAINT` enters `KeUserModeCallback` with callback ID 4 and a
44-byte payload (`win32u.so!NtUserDispatchMessage+218`), so returning the
recorded syscall result alone would omit that callback's execution.
`emuwindow.py` supports the observed DPI/window rectangle queries and bounded
scalar/message inputs; it rejects unsupported message payloads and does not
substitute for dispatch callbacks or kernel queue side effects. Synthetic
tests check relocated rectangle output, output bounds, DPI validation, and
exact translated-message input matching.

`capture_reload.py --user-callbacks ENTRY RETURN` observes
`KiUserCallbackDispatcher` and `NtCallbackReturn` and uses software probes at
both native return sites to include nested calls. The four temporary Wine
software probes are installed **after** the baseline memory capture, then
removed on detach. Callback payloads are bounded at 64 KiB each and 16 MiB
total. `native_order` records callback/OS/Unix observations in one stream.
`--native-entries` adds the two dispatcher entry probes, preserving pre-call
arguments and explicit callback parentage. The shared dispatcher exit is not
a sufficient API completion marker (Wine has a separate dispatcher
context-restoration path), so with `--native-entries` the recorder follows the
active call's actual user-code return address with the fourth hardware
breakpoint: nested calls move it to the child and restore the parent on
completion, each completion identifies its entry and validates the expected
user stack, and repeated dispatcher entries on the same still-active user
stack are recorded as restarts validated against the original entry.
`callbacktrace.py` validates every entry/return pair, arguments,
chronological nesting and event coverage; a trace that fails it is never
repaired or used as parity evidence.

## Cached passability ports and shared-buffer replay

`retailgrade.h` reproduces four retail routines, verified by
`tools/re/check_search_grade.py` against the executable in Debug and Release:

- `4139d0`: 6,000 cached/live grade cases, including ordered blocker probes.
  Its direction argument is unused. Unseen-cell and nearby-traffic exemptions
  are crosses: both coordinates must fall outside their respective intervals
  before the unseen/live fallback applies. Retries 2 and 3 retain cached grades.
- `413c80`: 3,000 visibility cases, including queried coarse-plane coordinates.
- `4e0300`: 3,000 aged occupancy planes and ordered writes. Borders cap grades
  at 4; interiors cap them at 2, or 0 for stale occupancy. The independent
  fourth bit survives. Interior and border clipping are independent.
- `4e1ee0`/`4e2060`: 3,000 preparation/cleanup cases. Thresholds are tick minus
  10 and 150, clamped at zero; timestamp intervals are unsigned and half-open.
  Last retry refreshes all allocated mover bodies in slot order, including a
  second refresh of the requester. Cleanup restores requester occupancy.

World's grade source uses these helpers ("Simulation-first investigation",
shared cached search integration). The mover timestamp is mover `+28`, written by `4e1e60` and `5066f0`;
it is distinct from mover `+30`. `4e1e60` refreshes initialized grids when the
old stamp predates their recent threshold, excluding their current requester.

Captures include Wine shared texture buffers (`/memfd:wine-mapping`) as well as
NVIDIA mappings (320,712,704 shared bytes in one capture; the recorder's
checked limit is 384 MiB): Glide code at `02b0d2a8` writes a shared texture
buffer at `20cb0000`. Native GPU destruction (`winevulkan!vkDestroyImage`) and
display queries (`user32!MonitorFromPoint`) need external support; skipping
destruction is an explicitly marked diagnostic option, not parity. The replay
stops at the first incorrect CRT call and executes caller-side
camera/visibility setup before attempting a frame; a forced render does not
reproduce retail's CRT draws, so exact frame cadence is not reproduced.

## Gameplay search phases and navigator origin

`PathService` dispatches `RetailSearchWorker`. It supplies heading (converted by a half
turn), type/terrain-adjusted costs, traffic radius and footprint-origin
coordinates, translating returned cells back to the existing World interface.
Initialization/retry refreshes the current unit position, heading and costs.
Point-goal queries use the circle controller with the ordinary four-pixel
tolerance. Initialization's 500-work charge is included in service accounting.
Scheduling is the singleton scheduler and grid preparation is the cached
directional planes of the sections above.

The World route installer treats the first returned point as the segment
origin, and steers toward the second point. Retail `4e5150` checks arrival at
point 1; `4e5100` supplies the origin/end/look-ahead triple (treating point 0
as a destination would turn replans back toward the start cell). `replaceLeg`
preserves an explicitly supplied first segment origin. Empty completion
preserves its actual failure classification rather than forcing failure.

`snapSpawn` emits `footprintWaypoint` for its center-indexed footprint (even
footprints need `cell*16`, not `cell*16+8`), and initial cell lookup uses
`footprintCell`.

### Renderer replay progress and limits

Captures can include shared NVIDIA CPU mappings (76,447,744 bytes in 150
bounded chunks in one), in addition to game/native image memory. Single-unit
rendering returns, and scene traversal returns and executes six real CRT
draws when diagnostic thread-wakeup-success and released-key substitutions
are enabled. This is not replay of the GPU worker or whole-frame
cadence. The native exports were identified in the live process:
`7befd754` = `NtAlertThreadByThreadId`, `7b4e0d8c` =
`NtUserGetAsyncKeyState`, `7befd3f4` = `NtQueryPerformanceCounter`.

The recorder observes the performance counter's RET-8 site using the hardware slot otherwise
used for steering calls (baseline route/mover state remains captured).
`emurender.CapturedClock` feeds those external inputs in strict tick/caller
order and fails on exhaustion or mismatched output arguments. It does not
replace simulation state or invent RNG draws. The diagnostic GDT restores FS
to the captured TEB and sets flat 32-bit code/data/stack segments.

A full captured-tick renderer replay matches the first tick boundaries and
then stops in native graphics execution, keeping its bounded native
instruction history for diagnosis. The NVIDIA CPU mappings are readable; the
dependency is broader native execution state.

## Native baseline, allocation and goal controllers

`capture_reload.py --next-tick` captures without a reload; `--native-memory` adds readable private PE image mappings, including
writable globals, at the same stopped boundary as game memory (for example
580,767,744 game-memory bytes and 38,785,024 native-image bytes at tick
10,722, a later live state rather than a reload).

The renderer diagnostic can stop in native `msvcrt.dll` at `7b82a38d`,
copying into shared `/dev/nvidia0` memory (`ca09a100`) that is excluded unless
captured. Executing captured native code alone does not establish rendering
or CRT ordering parity.

`retailrng.h` implements the separate CRT generator and selection of the
ranked free entity slot. `check_entity_slots.py` executes the retail allocation
fragment and real CRT instructions for 160 fixtures. It compares both slot and
updated seed, including full pools (no draw), holes and irrelevant flag bits.
World's slot pools use it ("Retail audit: production entity pools for path
admission").

`retailgoal.h` ports acceptance, distance and ordered cell enumeration for
circle, rectangle and ring controllers. `check_goal_queries.py` compares all
three operations in 3,000 executable fixtures, using the real vector code and
varying entity footprints. Rectangles accept only their perimeter and use a
16:6 distance metric. Circles enumerate only their center. Rings use world
distance for the inner radius and preserve all 16 ordered samples, including
duplicates. World's missions use these controllers.

## Initialized search worker connected to scheduler

`RetailSearchAttempt::initialize` ports the controller-facing part of
`415170`. `RetailSearchWorker::dispatch` connects initialization, reachability,
cost search and retries to `RetailSearchScheduler`. The world adapter supplies
current type/heading/weight parameters, grid preparation, ordered goal cells,
controller acceptance/distance and directional grade queries.

`check_search_init.py` executes 96 initialization fixtures against retail.
Grid preparation precedes controller enumeration; retry 3 selects the final
preparation mode. Goal cells are marked and the nearest squared-distance goal
is selected, retaining the first on ties. An empty list retains the previous
selected goal, matching the object reuse behavior. Cell flags are cleared but
directions survive reuse. Controller acceptance precedes distance evaluation
and can complete initialization with an empty route. Out-of-map starts report
failure after distance evaluation. The scheduler's 500-work charge applies to
early completion too.

`check_search_worker.py` executes the real `416430` scheduler with real
initialization, trace, cost expansion, reconstruction and retry instructions:
**80 complete lifecycles and 3,010 tick boundaries match** the connected C++
components. Comparisons include budget remainder, active state, retry count,
every cell's flags/direction, ordered grade queries, grid/notification/delivery
events, route flags and world waypoints. Four budgets exercise suspension
before initialization and within searches. Forced node-cap fixtures exercise
all three retries and the final 81-cell diagnostic scan. The scan calls both
grade and controller acceptance for every cell before clearing the route.
No search phase is stubbed in this comparison; world grid preparation and
controller lookup/delivery sinks are controlled. Multi-player admission has
its separate scheduler comparison. Debug and Release pass these checks and
the `retail_trace` CTest regressions.

In World the adapter supplies retail slot identity (the per-player pools),
controller goal geometry/notifications and retry grid semantics; global unit
IDs or an exact-point goal for every order would not preserve the checked
behavior.

The renderer's D3D9 entry reads writable module global `79d90bd0` at
`799e1ed0` and jumps through vtable offset 0x0c of that interface, so the
mutable native interface must be captured at the baseline tick (read-only
D3D9 module pages alone are not enough).

## Reachability, handoff and route reconstruction

Retail's direction selector `415040` uses a default-hint threshold of
**24:10**, not 2:1: delta (12,5) stays diagonal; (13,5) becomes cardinal. All
6,561 vectors in [-40,40] match executable retail.

The `retailtrace.h` port includes:

- `4146e0` reachability: 96 fixtures and 1,559 suspension boundaries match
  every state field, cell flag/direction and ordered grade query. Initialization
  scans its 7x7 neighborhood x-first. Octant steps leave directions alone;
  cardinal and twin-trace steps overwrite them. Cursor B moves opposite its
  stored direction; copying cursor A's convention is incorrect.
- `414450` reconstruction: 80 controlled parent chains match world waypoints
  and traffic/partial/detour flags, including the 64-corner ring. Reconstruction
  includes the start and retains the 64 corners nearest it when overflowing.
  A detour exceeding Manhattan distance sets bit 8 and clears traffic bits.
- `415b10` handoff plus actual scheduler cost slices: 156 attempts and 4,330
  boundaries cover all
  four retry indices and three budgets. Direct result -1 emits start/end and
  costs another 30; zero seeds goal search; positive progress seeds a partial
  cost search. No progress clears the route. Parent cells and ordered query
  directions survive the handoff. Cost exhaustion is exposed as a retry request.
- `4161b0` initial weight: 1,152 cases match weight and wrap-timestamp updates,
  including tick wraparound, retry age, pending load and heavy-floater base.

Checks: `check_trace_search.py`, `check_route_search.py`,
`check_search_attempt.py`, and `check_search_weight.py`, using
`retail_trace_test`. The CTest `retail_trace` regression needs no retail binary.
It covers direct delivery, traffic precedence, cost handoff, retail's diagonal
corridor corner, ring overflow, malformed chains and initialization weight.

Live module exports identify `7bf0c390` as `ntdll!RtlAllocateHeap`, and
`797ce770` as `d3d9!DebugSetMute` (a bare RET in the installed build). With a
bounded allocation substitute and that no-op, rendering entity 17 reaches
Glide `02b1aa8d`, which reads the D3D9 interface table at `79e3f7a8`, before
any CRT draw. `probe_captured_render.py` reproduces this diagnostic. These
substitutes are not a validated graphics model or proof of RNG parity.

## Origin-relative traffic flags

Retail's `0x414951` and `0x414563` compare traffic cells with search +30/+32,
the **start**, not the destination. Once the nearby-traffic
bit is set, another traffic cell sets the other-traffic bit even if it is also
nearby. `check_route_traffic.py` executes both retail branches on five fixtures,
including the strict radius boundary and repeated nearby traffic.

The port measures from the start, preserves traffic grades during cost
search, and accumulates the delivery flags in backtracking order. These flags
select the mover's re-request cadence. The corridor regressions check it. The
`goalCrowded` field (`pathsearch.h`) keeps its name but means traffic near the
origin.

## Scheduler admission and cost-slice ports

`src/sim/retailscheduler.h` implements the singleton admission/accounting
layer of `0x416430`. `tools/re/check_scheduler.py` compares the C++ implementation
with executable scheduling using controlled phase workloads: **106 fixtures,
1,233 tick boundaries, and every init/contour/completion event match**. Checks
include ten-player rotation, priority weighting, integer division/truncation,
empty-slot scans, per-player scan limits, slot wrap ticks, active-search budget
borrowing, and suspension between admission and initialization. Active-entity
validation is separate from pending-controller lookup, as in retail.

`RetailCostSearch::runSlice` implements the scheduler's phase-2 expansion loop.
**28 executable fixtures and 1,401 slice boundaries match**, including work,
neighbor fan, heap contents and every cell flag/direction. The node cap is
checked only at slice entry; expansions cost ten each, and arrival adds fifty
before reconstruction. A one-unit quantum can spend ten. A slice can exceed
its node cap and still arrive; enforcing the cap after each pop is incorrect.
The 240 cost profiles and 5,446 individual-pop comparisons pass.

The exhausted-heap executable test in `emuscheduler.py` shows that the
scheduler does not relocate the goal: it retries three times (four initializations
total), queries a fixed 9x9 neighborhood, then calls `4e4ea0(0,0)` to clear the
route and completes the request. That branch does not relocate the goal or
seed another search. The neighborhood is centered on search +30/+32 (the
start). It is not a widening destination scan.

PathService runs these components together with phase-0 initialization,
tracer handoff (including positive partial-distance results),
reconstruction and controller notifications, retry-driven grid preparation
and per-player entity-slot identity; a legacy trace outcome or global unit-ID
order would lose the verified behavior.

## Live renderer caller

A live stack capture of eight calls at `0x4ec858`, all at simulation tick
10274, confirms this path (call sites shown, rather than return addresses):

```
526f61 -> 4fbb90
4fbc26 -> 4fc5b0(1)
4fca1f -> 4ee700(entity)
4ee74d -> 4ec720(entity->visual)
4ec774 -> 4ec7b0
4ec853 -> CRT rand
```

Outer-frame stack arguments differ between the first five and last three
events while the simulation tick stays unchanged. Render cadence therefore
cannot be inferred from simulation ticks alone, and a fixed
one-render-per-batch substitution is not valid. Entity traversal in `4fc5b0` uses render
row bins built from the entity list and camera position, with flag and visual
object checks before calling `4ee700`; unit-ID order is not a substitute.

Calling `4ee700` offline for the first eligible captured entity (17), with
the tick checker's single-thread critical section substitution, enters Glide
and reaches native code `0x7bf0c390` (return address `0x02bad9b6`, an
allocation-shaped three-argument import call) before any CRT call. No
graphics calls or RNG draws are fabricated.

Offline whole-game replay therefore depends on native renderer/OS behavior
and actual outer-frame cadence; full replay needs those dependencies modeled
and validated, or a separately validated extraction of the RNG-relevant
render work and its scheduling.

## CRT batch boundary and renderer dependency

`reload-crt-state-01` has **31 boundaries, 518 gameplay RNG calls and 210
CRT calls**. The boundary registers show five-tick batches: EDI=5 and ESI counts
4,3,2,1,0. The first missing CRT draws occur after tick 10074 at return address
`0x4ec858`; 85 of the 210 observed CRT calls come from this site. Other callers
are particle spawning (`0x4f1467`, `0x4f14a2`), `0x525248`, and entity-slot
selection (`0x5121dd`).

The offline checker executes the actual batch epilogue as well as the tick
body. The `0x4ec858` calls belong to the outer renderer path: `0x4fbb90(1,1)`
reaches `glide3x.dll` (captured module base `0x02b00000`), which reads
`0x79e3f874` at `0x02b0a2e5`. The render attempt stops and reports such a
dependency; it never replaces those draws with a fabricated count or resets
the RNG to later captures.

The ground-mover constructor at `0x4dc6f6` masks flags with `0xe001` before
setting bit 0, preserving allocator contents in the upper three bits. The
checker compares initialized low 13 flag bits for newly created units, and
reports that exclusion alongside the existing +2c allocator-byte exclusion.
This is a semantic comparison with explicit limits, not byte-for-byte state
equality or a C++ engine parity claim.

## Failed-route reconstruction and executable tick replay

Route reconstruction at a failure emits only backtracked points: it never
appends the requested goal when reconstructing failure at `bestCell` (that
would add an untraced segment through the separating obstacle, or a straight
segment out of a boxed-in start). Checks: reachable failure endpoint, no
segment across a sealed wall, and no route when boxed in.

`reload-game-memory-01` has 61 frames, 527 gameplay RNG calls, 1,251 steering
events and 507,596,800 bytes of bounded initial private memory, enough for the
idle mission to execute. `emureload.py` maps contiguous page
ranges, implements bounded allocation/reallocation for known extents, and rejects
unknown original reallocation sizes rather than guessing how much to copy.

`check_captured_tick.py` executes the retail tick body (`0x526351..0x526515`)
from that single initial state; **25 consecutive ticks and 279 gameplay RNG
calls match** the capture, including unit identities, raw positions/headings, ground speeds and
selected movement/search fields. This validates a limited executable replay,
**not the C++ engine**. It assumes released keys, single-threaded critical
sections and one-tick batch arguments; presentation/OS work outside that body
is omitted. Newly allocated mover +2c bytes are excluded because construction
can leave allocator contents there. No future positions or routes are injected.

The CRT RNG is **not visual-only**: unit creation at `0x5121d8` uses `rand()` to
select a free entity slot, so an assumed CRT seed gives different new IDs.
The actual initial thread record is present in the capture and is identified
by TEB self-pointer, stack bounds, TLS index 23 and matching thread ID;
restoring it reproduces new unit 991. Later allocations also depend on
CRT-call ordering outside the tick body (render) and batch context.

The recorder's `--crt` option uses the fourth hardware breakpoint to observe
simulation-thread CRT calls, captures its seed at each boundary, and includes
tick-batch registers. It requires `--game-memory`, checks the executable bytes,
bounds event counts and detaches in `finally`.

## Scheduler and mission capture

`reload-runtime-state-03` has **61 frames, 535 RNG calls, 1,251 steering
events**, with the visibility, occupancy and type-prefix inputs.
Arithmetic, RNG continuity and recorded steering checks pass.

`check_captured_search.py` resumes the captured phase-2 heap in both retail
emulation and `RetailCostSearch`: **625 pops, 854 grade queries in identical
order, every heap entry after each pop, and all 409,600 final cell flags and
directions agree**. This is a frozen-world kernel comparison, not delivery
timing or validation of the port's terrain grader. It uses the real binary's
grade queries against the captured world. The suspended-state importer rebuilds
open-cell node references from heap order; allocation indices do not break ties.

The actual first-tick mission dispatcher **cancels Hunter 53's active
search** when its 0x2000 event resets the move controller. Running `0x4d8450`
from this capture clears the singleton's active entity. For Hunters 40 and 53,
using each handler's recorded entry RNG seed reproduces all next-frame mission
bytes except the newly allocated controller pointer. These are isolated handler
checks; seeding each entry does not prove whole-game RNG ordering. Hunter 56's
post-dispatch pending mask additionally receives 0x2000 later in the live tick.
Completing Hunter 53's frozen search and delivering its route is therefore an
incorrect restore strategy.

`emureload.py` retains actual lazy container initialization and replaces cdecl
exit-callback registration alongside allocation/free; executing process shutdown
is outside this harness. Idle mission 49 reads script state at `0x41265a`.
The recorder's optional `--game-memory` captures
readable private mappings below `0x60000000` in 1 MiB compressed chunks, bounded
to 768 MiB total, at the same initial stopped tick. This covers heap dependencies
and writable globals together; shared/high Wine mappings and OS state remain
excluded. Emulator reads in uncaptured portions of mapped pages are not
audited, so successful execution alone does not certify complete state.

**Phase-2 is not a plain Dijkstra.** `emuphase.py` executes the real
cell-goal controller with separate navigator storage (not a passability grade
in place of the goal-distance virtual method, and with start/goal the right
way round). Retail's distance is
`max(0, 18*max(abs(dx),abs(dz)) + 7*min(abs(dx),abs(dz)) - tolerance)`.
Heap priority includes that distance multiplied by a changing 16.16 weight.
The initial weight depends on scheduler backlog/age/retries; after 100 pops it
increases by 655 per pop, capped at 50. The first-attempt node limit is cells/20,
not cells/10, and the scheduler checks it outside its inner pop loop.

`src/sim/retailcost.h` contains the replacement resumable kernel and cost-profile
initialization. `check_cost_search.py` verifies **5,446 pops across 16 fixtures**,
comparing every cell flag/direction and every heap entry after each pop, plus
**240 cost profiles** against executable initialization. The kernel preserves
root reuse, strict heap ties, decrease-key handling, the heading-relative
neighbor fan, trace-marked cells, and the increasing heuristic. PathService
runs it resumably through the scheduler (a synchronous call would give
incorrect delivery timing and omit retail retries).

In `reload-runtime-state-01` (61 frames, 942 RNG calls, 1,251 steering
events; arithmetic and the entire RNG chain pass), Hunter 40 and 53's
initial live mission pending mask is **0x3000**, versus saved **0x1000**. Their
extra 0x2000 event wakes state 2 before its deadline; the dispatcher can reset
and advance through states within the same tick. Hunter 56 instead reaches
its deadline on the first tick. These observations explain the missing move
mission RNG activity without fabricated draws.

Continuing the captured search also requires the coarse per-player visibility
plane used by `0x413c80`, map/occupancy records and relevant type prefixes;
the bounded recorder includes them, plus the mission definition records, with
preflight reads. Type prefixes and recognized controllers are explicitly
partial; this is not a full save-state importer or a whole-game parity claim.

In `reload-path-state-02` (61 frames, ticks 10069–10129, 543 RNG calls and
1,251 steering events) the complete RNG chain and every recorded steering
calculation pass `check_motion.py`; this verifies arithmetic and capture
continuity, **not full simulation parity**.

`analyze_path_capture.py` validates/decompresses bounded search buffers, maps
captured entity addresses to unit IDs, and reports scheduler state, route
changes and RNG attribution. Initially Hunter 53 is the active phase-2 search,
with 109 queued heap nodes and 389 processed nodes. Its owner's remaining share
is negative while the other player's is positive; the global remainder is zero.

`emuscheduler.py` executes `0x416430` with synthetic entity slots and controlled
search phases. It verifies that retail has one active search, resumes it before
new admissions, and permits it to spend the global budget beyond its player's
share. Admission advances the player cursor, then that player's entity cursor
(including empty slots, 7 work each). Initialization costs 500 and can overshoot
a small budget. Player shares are computed once per call, with priority weight
five and pending counts used only as a nonzero eligibility test. The call site
at `0x4f6c9e` passes divisor 1. `RetailSearchScheduler` follows this: one
active resumable search, no fixed search pool.

The first RNG call is Hunter 40's Move_Ground handler at `0x402dbd`, followed by
idle unit 49 and moving Hunters 53 and 56. Saved Move_Ground state is 2; the
observed call comes from state 1. The controller at `0x4d8450` can run several
state transitions in one tick, consumes unit/mission event masks, and schedules
deadline events. These transitions must be reproduced, not replaced with dummy
RNG draws. Capture 02 has no initial live mission records or cached
navigation grade planes, so exact resumption needs a `--runtime-state`
capture.

The recorder's `--runtime-state` option adds the initial entity slot pool,
bounded mission chains and recognized ground controllers, cached ground grade
planes, and active mission heads/events each tick. Unknown controller classes
are explicitly marked. It preflights these reads while stopped before arming;
the running fixture yielded 163 missions and three grade planes. Synthetic
tests cover cyclic chains, oversized planes and unknown controllers.

## Captured route state and ground movement

**Exact game parity is not established.** The local `reload-steering-02`
capture contains the navigator points before the first tick, the movement
modes, and 69 steering events. Live x87 control is **0x027f** (53-bit
precision, nearest rounding). The partial replay restores those initial
points, modes, refusal bits and local-scan deadlines, rather than starting
every navigator cold. It never injects future captured routes.

The first captured movement tick agrees in raw position, heading and speed for
**all 32 Hunters**. Hunter 40's detour is already present before the first
tick. This is evidence about this fixture, not certification of all ground
units, orders or map conditions.

Implemented and checked against executable arithmetic:

- Rounded BAM direction conversion: 10,049 boundary/random cases.
- Segment steering: 1,254 cases across two x87 precision settings; all 69
  recorded aims and requested headings also agree.
- Three-point acceleration/braking: 1,600 cases at the observed precision.
  Retail limits acceleration and braking to one third of the individual
  maximum when within 80 pixels of the next point. Turn braking uses a separate
  adjusted point and an **unwrapped** unsigned retail heading difference;
  stopping distance uses the third point, repeating the last if absent.
- Traffic also uses an unwrapped heading comparison. Wrap and quarter-turn
  boundaries are checked by `emutraffic.py` and the actual World regression.

Ground navigator advancement uses the integer-coordinate five-pixel circle
at `0x4e5192`, continuing movement on the same tick. Land steering mode is
refreshed on mover `+30`'s deadline by checking eight neighboring footprints
(corner pruning and the boat look-ahead are in "Ground responses, controller
reset and corner pruning" and "Verified boat scan rules"). Repeated refusal
(mover `+36` bit 0x4) bypasses random retry cadence and requests a route
immediately, leaving an already-pending search alone, with no random-tail
draw (the branch at `0x4e51d4`).

There is no projected-cliff-wall heuristic: flat cells north of cliffs are
traversable, as the captured retail Hunter shows; actual slope/depth and map
hard blockers apply. Match setup uses retail's default 12,000 work budget
(a smaller budget changes route-delivery timing). Immediate retries
substantially increase search work.

Retail's first RNG call in the capture is a mission-controller `rand(5)` at
`0x402dbd`.

The capture tool supports `--steering --path-state --ticks 60`: initial
singleton fields, bounded compressed scratch buffers, per-player counts and
iteration state, mover/navigator records, route outcomes and RNG caller
registers.

## Route-segment steering

Ground routes retain each segment's start with its endpoint in `Order`.
`replaceLeg` creates those pairs within the current command; consuming a route
node keeps the preceding point, and queued commands do not become look-ahead
points for one another. The mover uses `retailSteeringPoint` to derive its
aim, with raw fixed-point differences. Segment state is hashed.

Navigator getter `0x4e5100` returns the first N route points, repeating the final
point when fewer than N remain. The aim calculation implements
`0x4d9bc8..0x4d9ce9`, including truncating the normalization division and
shifting signed products. Comparisons cover 627 boundary/random inputs at both
53-bit and 64-bit x87 precision. Unicorn's default 24-bit precision produces
different square-root rounding; the comparison sets precision explicitly.
This validates arithmetic under those two settings, not the live control word.

The helper implements the 80-pixel threshold of movement mode 0 and the
16-pixel threshold of the other retail modes. A mover regression
distinguishes segment steering from direct endpoint steering.

## First-turn steering isolation

`tools/re/analyze_saved_steering.py` executes retail's `0x53612a` angle
routine for each active Hunter's saved cell goal, then applies the verified
2,500 BAM/tick turn limit. In the recorded first tick, 13 of 21 headings agree
with this direct-goal counterfactual. Hunter 40 would turn from 64,728 to
64,978; the capture instead turns to 62,228. The discrepancy therefore survives
using retail angle arithmetic: replacing CORDIC alone cannot resolve it.
This check assumes an unmodified turn rate and does not certify route state.

The mover obtains two route points through virtual slot +0c with argument 2
at `0x4d9bbb..0x4d9bc5`. At `0x4d9bc8..0x4d9ce9`, it adjusts the second
point along the segment toward the first before deriving the requested heading
at `0x4d9cf3`. The adjustment uses a distance threshold of 80 pixels for
movement flag mode 0 and 16 for the other modes, and is capped by segment
length; steering at the front order alone does not reproduce it.

The save decoder exports every copied mover field by runtime offset,
including both three-component vectors and the additional 16-bit fields.
All 38 mover records match execution of `0x4dcae9..0x4dcb9e` under three
different initial flag patterns. The loader preserves high flag bits and
does not write the runtime movement tick at +2c. Fields stay explicitly
opaque where their meaning has not been established; they are not silently
interpreted as waypoints or injected into the simulation.

## Movement replay probe and arithmetic corrections

`retail_replay_probe` loads the retail map/type data and the verified raw
unit positions, headings, base/current speeds, footprint identities, tick, and
captured RNG state. It rebuilds ground move orders from the decoded cell-goal
controller. `tools/re/probe_saved_movement.py` prepares its input and reports
the first motion and RNG mismatches against the bounded reload sequence.
Generated inputs, traces and reports stay outside version control.

**This is an explicitly partial diagnostic restore**, extended by the probe
formats described above; it reports what it does not restore rather than
discarding it. A matching initial movement frame does not mean the whole
world has been restored. The fixture includes 118 MobileBuild orders and other
non-movement missions; these cannot be silently discarded in a full importer.

```
python3 -B tools/re/probe_saved_movement.py assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/test.tak assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/reload-sequence-01.json --runner build-dbg/retail_replay_probe --retail-root assets/game --output assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/probe-new
```

Two movement arithmetic details, each verified against the executable:

- **Turn braking works in BAM.** `retailTurnTravel` computes
  `speed_raw * angle_bam / rate_bam`, including retail's zero-rate behavior
  (mixing a radians/sec rate with a BAM angle would inflate the turning
  distance by roughly 10,430). All 60 comparison cases match execution of
  `0x4da53a..0x4da56e`; the whole braking routine is covered by
  "Inactive navigation braking".
- **Ground displacement trig quantization.** Retail
  `0x5360bf/0x5360f3` use 512 Q13 sine samples, binning with a +32 BAM bias,
  and add 4096 before shifting the product by 13. The ground movement path
  negates the result after rounding (`0x4dab0d/0x4dab2b`). A mathematical
  constexpr generator supplies the samples; no retail table bytes are copied
  into the source. All 8,192 signed-magnitude/bin-boundary comparisons match
  executed retail routines. Flying movement uses the flight velocity
  calculation ("Live flight movement and patrol").

Retail heading zero points along negative Z, whereas World uses positive Z.
The probe converts by half a turn at import/export; it does not change the
engine's internal convention. This is confirmed by the retail displacement
call path and the captured positions. The original click also differs from
the controller's navigation point: the shared click (2180,596) becomes
(2176,592) for the Hunters' footprint. All 22 controller goals match execution
of `0x4e2820` in `check_save_state.py`.

Hunter 17's first horizontal movement step matches exactly: raw displacement
(-54769,-91372), heading 5692, speed 106521. Retail's first RNG call at tick
10,070 is rand(5); dummy draws are never inserted to match a count. A movement
regression checks that a distant quarter-turn goal does not brake the unit to
a stop.

## Saved retail baseline

The reference save is a retail `SavedGames/test.tak`. A byte-for-byte copy is
kept outside version control at
`assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/test.tak`, alongside a provenance
manifest and decoded-section inventory.
The save is 156,303 bytes with SHA-256
`0d8e4c03c052a3c469f4b1d69b2f1e35df25b4747fb6351d66bbd197bbf11caa`.

`tools/re/inspect_save.py` recognizes this HAPI BANK save and decodes its eight
SQSH sections using method-1 LZSS. All eight compressed checksums and decoded
sizes match. Its string table identifies **Ulasem Arena**, and the decoded data contains `zonter` records and named
orders such as `Move_Ground` and `Standby`. String-table entries alone do not
establish associations between keys and state values.

```
python3 -B tools/re/inspect_save_test.py
python3 -B tools/re/inspect_save.py assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/test.tak
```

The inspector also decodes all ten directory nodes, including integer,
double, string, and binary entries. Every node is consumed exactly, and every
blob reference agrees with its offset in the uncompressed node. Blob metadata
is preserved without assigning unverified meaning, except for the unnamed
unit record indices established by the retail save/load routines.

`tools/re/decode_save_state.py` exports a **partial movement-state baseline**:

```
python3 -B tools/re/decode_save_state.py assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/test.tak
```

The local result is retained as `movement-state.json` beside the preserved save.
It contains 47 units (32 Hunters), their IDs, players, raw positions, headings,
footprint sizes/origins, individual base speeds, movement speed/refusal fields,
and 160 associated order names and blob references. The saved simulation tick
is **10,069**, independently agreeing with the Summary value. All 47 saved
footprint origins agree with the retail coordinate formula.

Field provenance: unit writer `0x513b34..0x513e79` and reader
`0x513330..0x513944` serialize 235-byte version-44 records. Their stack buffers
have different bases, `EBP-0xf8` and `EBP-0xf4`. The movement reader
`0x4dcaa0..0x4dcba2` handles the 44-byte `uXXXXmob` records. GameTime is loaded
to `G+0x19f34` by `0x4f7750..0x4f775f`; the tick at `G+0x19f44` therefore
comes from blob offset 16. Unit field offsets are documented in the decoder.

The exporter additionally decodes the common 82-byte mission payload for all
160 orders. Owner IDs match their unit records. `tools/re/check_save_state.py`
executes retail's field-copy block `0x4d7107..0x4d718f` and compares every
exported runtime field for all 160 records. Runtime-offset labels deliberately
avoid assigning unverified meanings to mission-specific fields. The loader
resolves mission type from its name rather than trusting the saved type byte.
The 22 `Move_Ground` orders use controller kind 4: its 16-byte blob restores
runtime `+08/+0c/+10` through `0x4e25f9..0x4e2608`. The first saved word is
ignored by retail and must not be treated as a live pointer.

### Exact reload capture

`tools/re/capture_save_baseline.py` compares two equal read-only observations
with the save. It rejects a moving process and reports differing ticks instead
of aligning them silently. A manual pause lands about 28 ticks late, so
`tools/re/capture_reload.py` uses a bounded hardware breakpoint at `0x526351`,
immediately before the simulation counter increments. It verifies the tick
entry code and existing movement/RNG code ranges, captures with all threads
stopped, then detaches. It never patches retail code or data.

Two successful captures are retained beside the save:

- `reload-first-tick-01.json`: tick 10,069, 47 units, RNG state 390784116.
- `reload-sequence-01.json`: tick 10,069 through 10,129 inclusive, 61 frame
  boundaries, 529 ordered RNG calls from 24 call sites, initial RNG 342647657.

Both starting captures match all saved unit positions, headings, footprints,
players, and individual base speeds. Runtime refusal deadlines for units 17
and 40 are zero after reload despite nonzero saved values. The loader restores
only the low 13 movement flag bits; upper bits differ across reloads. Reports
retain those differences rather than treating the saved blob as the complete
restored object.

The initial RNG states differ between reloads. Retail `0x527060` seeds from
the sum of the two words returned by `QueryPerformanceCounter`, via
`0x535d30`. Its fixed-1234 branches are guarded by `0x524f60`, which checks
`-AutoStop`; they do not establish a normal-game fixed seed. The live RNG at
the captured boundary is therefore required for replaying that particular run;
the save-only export leaves RNG null.

```
python3 -B tools/re/check_save_state.py assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/test.tak
python3 -B tools/re/check_motion.py build-dbg/retail_motion_test --reload-sequence assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/reload-sequence-01.json
```

The combined check passes: every RNG successor links to the next observed
seed, the initial/final seeds match frame boundaries, all 61 frame seeds occur
in sequence, 2,958 unit footprint observations match the compiled helper, and
2,409 positive base-speed observations are attainable from their nominal speed.
These are arithmetic/capture checks, **not a port trajectory comparison**.
Hardware breakpoints perturb wall-clock timing; the trace describes that
observed run. The RNG sites include mission scheduling, repathing, and unit
creation; copying the generator recurrence alone does not reproduce their order.

`World::setRngObserver` exposes tick, bound, before/after seed, result, and
original C++ consumer location through the fire/burn/path wrappers. A
600-tick paired movement test verifies that tracing leaves every state hash
unchanged; replay reset preserves the observer without consuming an extra
draw.

## Live retail findings

Read-only observation of retail under Faugus/Proton with Glide works. The
reference scenario is Ulasem Arena with a group of Zhon Hunters moving around
terrain, with **several destination clicks**. The shipped Hunter definition is
`zonter`, not the flying monarch `zonhunt`.

`tools/re/livesample.py` reads the actual entity table at
`G+0x14e84..G+0x14e88`, stride `0x138` (not the separate occupancy/rater
table). Three live code regions (traffic query, hard placement, movement
commit) match our local reference byte for byte; this does not establish whole-executable identity.
The 60-second capture is retained locally, outside version control, at
`assets/tmp/retail-traces/terrain-20260917-01.jsonl`.

Reproduce the analysis with:

```
python3 -B tools/re/analyze_live.py assets/tmp/retail-traces/terrain-20260917-01.jsonl
```

Across 1,570 snapshots and 40,005 unit observations, retail's footprint origin
matches this formula without a single mismatch (apply independently to x/z):

```
origin = floor((position_raw - (footprint_size - 1)*8*65536) / (16*65536))
```

A floor-to-pixels, divide-by-16, subtract-half-footprint convention disagrees
in 16,646 observations. `src/sim/footprint.h` supplies the conversion
for occupancy, hard placement, road tests, navigation endpoints, waypoints,
connectivity lookups and production/unload placement. Even-footprint navigation
anchors are at `cell*16`; odd footprints retain `cell*16+8`. The compiled helper,
not just a Python transcription, matches all 40,005 captured observations.

The trace also shows multiple per-unit base speeds for the same type. Creation
at `0x51228f..0x5122b8` multiplies the type speed by `0x512430`'s result before
storing entity `+0x12b`. The multiplier is `trunc((900+rand(201))*65536/1000)`.
All 201 multipliers were executed in retail and compared with the compiled port.
Units retain their individual base speed for movement, formation pacing,
refusal caps and the traffic floor; it participates in the lockstep hash.
The FBI fixed-point reader `0x5431f0` truncates instead of rounding: Hunter
nominal speed is 117964, not 117965. Movement data uses that conversion.

`0x4db010` clamps a refused position inside the old footprint's +/-8px cell
window, with a one-raw-unit inset. The first-refusal branch `0x4db06f` snaps
coordinates outside +/-4px to the corresponding inset edge. Rejecting the
whole movement vector would lose this tangential motion. Both branches have
compiled-vs-executable comparison coverage (210 cases over five footprints).
The two speed caps divide terrain-adjusted individual speed by **2 and 5**
(not 0.4 for the second). Refusal bits persist through same-origin movement
and clear on an accepted origin change. Goal-area satisfaction is checked
independently of intermediate route points, so a blocked waypoint cannot trap
a unit that already reached its leg endpoint.

Burn, fire and path draws all use one shared simulation stream, including
creation speed draws. The recurrence,
seed initializer (`0x535d30`) and no-draw behavior for bounds below 2 are
verified against the executable; 3,500 chained calls compare both outputs and
successor states. Match setup uses the shared match seed; replay resets it.
This is **not whole-game RNG ordering parity**. Retail has 460 direct calls to
`0x535cc0`; their complete dynamic order is not mapped. Scenario and AI RNGs
are separate and their retail relationship is unverified, as is the seed
retail match initialization passes. A shared stream does not by itself
establish retail's consumer order.

Run the compiled-vs-retail comparisons (requires local retail and Unicorn):

```
python3 -B tools/re/check_motion.py build-dbg/retail_motion_test assets/tmp/retail-traces/terrain-20260917-01.jsonl
```

A 16-unit terrain regression changes destinations three times, checks every
footprint throughout 3,600 ticks and requires all 16 final arrivals (refusal
clamping and independent goal completion are what make all 16 arrive); the
six-unit queued rally regression requires 6/6. These are port regressions, not
side-by-side retail trajectory comparisons. Factory output shares no
footprint cells across 5,400 ticks. Crowdbench reports exact-spot counts
alongside area arrivals, so spot misses are not hidden by the area metric.

These are local movement observations, not a single-order route/arrival test.
The trace includes other units, not just the selected group. Tick-before/after
guards reject reads spanning a tick but do not make snapshots atomic. Observed
tick deltas are 2 (1,568 times) and 5 (once); the original capture footer's
`tick_gaps` assumes increments of one and must not be interpreted as missed
simulation frames; the recorder reports a delta histogram. Schema 2 also records
nominal type speeds and RNG state before/after the read; RNG snapshots cannot
identify individual callers or prove draw ordering.
Four synthetic-memory sampler tests pass, covering layout, signed coordinates,
changed ticks, mismatched IDs and invalid table extents.

Second live capture: `assets/tmp/retail-traces/terrain-20260917-02.jsonl`, schema 2,
1,322 frames / 31,635 unit observations. The compiled origin helper matches all
31,635; the half-footprint convention disagrees in 11,451. Every recorded
individual speed is attainable from its recorded nominal speed under the
verified multiplier. Hunter nominal speed is confirmed live as 117964. Command
events/coordinates are not decoded, so it is not an identical-input trajectory
comparison. RNG changed during 10 otherwise same-tick snapshots: polling
states cannot establish atomic RNG draw order.

`tools/re/trace_rng.py` supplies a bounded **hardware-breakpoint** recorder
for call ordering. It pauses/slows the game; it never patches executable bytes
and detaches at its call limit or 20-second soft timeout. Wine's SIGUSR1 is passed
through normally. The successful local capture is
`assets/tmp/retail-traces/rng-20260917-02.jsonl`: 512 consecutive calls from 15
call sites during ticks 9105..9152, on the game thread. Each seed successor
matches the next call's observed seed, so this captured interval has no missing
state-advancing draws. Repath caller `0x4e547d` accounts for 201 calls; mission
caller `0x402dbd` accounts for 142 (`rand(5)+5` before `0x4d6a10`). The other 169
calls come from 13 additional sites. They are not mapped one to one to the
port's consumers.

Reproduce arithmetic and sequence verification with:

```
python3 -B tools/re/check_motion.py build-dbg/retail_motion_test assets/tmp/retail-traces/terrain-20260917-02.jsonl --rng-trace assets/tmp/retail-traces/rng-20260917-02.jsonl
```

## Placement is separate from traffic scoring

A same-way traffic grade does not permit overlapping movement; the movement
commit path decides placement:

- `0x4daf1c..0x4daf5d` compares the prospective footprint origin with the current
  one. Movement within that origin updates the position directly.
- A changed origin, for the active player classes gated at `0x4daf60`, reaches
  `0x4daf8f`, which calls `0x507d10` with its final
  argument **zero**. That is a hard footprint placement check, separate from
  the soft query used by navigation. On refusal, movement is clamped and slowed;
  on acceptance, the old occupancy is removed and the new one installed.
- `tools/re/emutraffic.py` executes that placement routine without hooks. A
  2x2 candidate overlapping an occupied cell returns false for both parked and
  moving occupants, including when the occupied cell is the rectangle's far
  corner. Adjacent non-overlapping rectangles return true.
- The same harness executes the live traffic predicate with only its terrain /
  candidate-list provider stubbed. It confirms an omitted comparison: the
  occupant must travel at least **75% of the querying unit's terrain-adjusted
  base speed**, as well as at least its current speed. `0x421ae0` applies road
  before water; `0x4db77d..0x4db7ac` multiplies by `0xc000` and compares both
  limits. Boundary cases one fixed-point unit below and at the limit are checked.

The sim keeps the two decisions distinct. Routing scores the footprint,
including qualifying same-way traffic; committing a new footprint requires it
to be free of all other bodies. Occupancy is updated after **each mover**, so
later movers cannot claim space using a stale beginning-of-tick snapshot.
Collision without the matching routing creates routes the mover cannot follow.

`requestPath` does not cancel short trips (even a two-cell trip can cross a
wall), and attack/guard legs use the route service. Their intermediate
waypoints retain the target and order flags, while target tracking updates the
leg endpoint rather than overwriting its next waypoint. Target loss cancels the
leg's pending route and resumes queued commands. Terrain refusals advance the
same two-stage refusal state as unit refusals.

`pathblock_test` has short-obstacle, chase/guard, queued-command,
footprint-edge, same-tick reservation and refusal-state regressions; the
24-unit factory test checks exclusive footprint ownership throughout its
5,400-tick production run. Crowdbench's area count is not exact-point arrival
or a measured retail timing comparison.

## Retail's movement design: route and step

Every address below was read from `KINGDOMS.icd`; the RNG and the routines
marked emulated were also run under emulation. What is not established is
called out as such.

Retail moves units with two layers. **Layer 1, the route**, is the search
(`0x4139d0`..`0x416xxx`: initialiser, boundary tracer, cost search,
reconstruction; see the phase machine below), which routes around terrain and
features using the cached grade planes. **Layer 2, the step**, is the mover's
per-step placement and traffic queries, which make bodies solid and make a
unit go around another rather than into it. Retail has no separate A* or
string-pull; the cost search below is part of layer 1.

### The map rater, `0x509020`

Returns a passability rating where HIGHER IS BETTER, confirmed against an
independent second implementation at `0x404ff9`:

    7   open ground                     (initial value, 0x50904d)
    6   terrain flag clear              (+0xd bit 0x80, at 0x509348)
    4   slope tests                     (0x509318 / 0x509322)
    1   clearable blocking feature      (0x50917c)
    0   blocking feature                -- blocked

Cell word `+8` is the cell's feature index; `0xffff` empty; `0xfffe` marks a
tail cell of a multi-cell feature, whose anchor is found by walking back with
the bytes at `+0xa`/`+0xb` and the map stride `[0x62d55c]+0x19e98`. Byte `+6`
is the height the slope test reads. Features: table `[0x62d55c]+0x19edc`,
stride `0x140`, count `+0x19ec0`; flag word `+0x13c` bit 5 (`0x20`) blocking,
bit 17 (`0x20000`) clearable (see "Raw World grade audit and ramp coverage"
for how the loader derives it). Every exit reports through `0x509390`.
Bodies are held in the separate ground word `+0` ("Retail audit: who holds an
overlapped ground cell").

### Corner clearance, `0x508e50`

Rates a cell together with its orthogonal neighbours and demands `> 4` and
`>= 6`, so a route does not cut the diagonal between two obstacles.
`0x508cd0` is the same test against the second rater `0x5088f0`. `0x508e20`
initialises the working set: clears `0x121` dwords at `0x6405d4`, stores its
two parameters at `0x6405d0`/`0x6405cc`.

### The footprint fill, `0x4dfd70`

Fills a buffer at `ebp-0x1ec` with the ratings over the unit's footprint,
dispatching on footprint size (`<= 5`, `<= 11`, at `0x4e0260`) between a
batched fill `0x508db0` and a per-cell `0x508cd0`, writing each through
`0x4dfe40`. Called from `0x4bfa50` <- `0x4c1950` <- `0x525850` (the main
per-frame update). The grade planes it feeds are map-sized, not a recentering
window (see "Grade planes, the pending count and phase-2 composition").

The hard "may I stand here" test is `0x507d10` (nine call sites); see
"Placement is separate from traffic scoring" above.

### The game RNG

`rand(n)` is the GAME's RNG at `0x535cc0` (seed `0x64186c`, 460 callers), NOT
the CRT `rand()` at `0x5d4444`; the movement chain has no CRT `rand()`
callers. Verified under emulation: returns `[0, n-1]`, returns 0 for `n < 2`
**without advancing the seed**; other calls advance it.

    rand(0)=0  rand(1)=0  rand(2)=1  rand(10)=5  rand(100)=15   (seed 12345)
    20 chained rand(10) from seed 1:
      7 9 3 8 0 2 4 8 3 9 0 5 2 2 7 3 7 9 0 2

### Per-player budget: per PLAYER, weight 5

`0x634674` is a 10-dword table, one slot per player, holding the number of that
player's units currently queued for a path. `0x4e4f50` increments a player's
slot when a unit enters the queue and decrements it when it leaves, guarded by
flag bit 2 of `+0x114`; the player index is `[[unit+8]+0xb8]+0xeb`. It is a
live count, not cleared per frame (see "Grade planes, the pending count and
phase-2 composition").

The split, at `0x416430`/`0x4164fa`: walk the 10 slots, keep players that are
active (`[+0x2404]` non-null), of type `[+0x24ee]` in {1,2,3}, with
`[+0x24ef] != 0xa`, and with a non-zero pending count. Sort them into two
classes by byte `[+0x24e7]`, then

    perShare = (budget / arg) / (normal + 5*special)
    share    = special ? perShare*5 : perShare

with the frame budget at `[esi+0x225]` and the running total at `[esi+0x165]`.
The weighted total is formed as `edi + (without + 4*edi)` = `without*1 +
with*5` (`0x416500 lea ecx,[ecx+edi*4]`, `0x416507 add edi,ecx`) and divided
at `0x416515 idiv edi`.

Confirmed under emulation, synthetic game state, ten player slots, budget 1000:

    2 normal                -> 500 / 500
    normal + special        -> 166 / 830          (830 = 166*5)
    special + normal        -> 830 / 166          (order-independent)
    2 normal + 2 special    -> 83 / 415 / 83 / 415
                               denom 2+5*2=12, 1000/12=83, special 83*5=415

The count is PLAYERS, not requests: varying the pending count does not move
the share.

    two normal, each pending=1   -> 500 / 500
    two normal, each pending=50  -> 500 / 500
    pending 1 vs pending 99      -> 500 / 500

A player with 99 queued units gets exactly the same frame budget as a player
with one; `0x634674[p]` is only ever tested `> 0`, never summed. No age
weighting exists in the split.

- **The divisor is 1.** `0x416430` has exactly one caller, `0x4f6ca0`, and it
  is a literal `push 1`, so the quantum is `+0x225 / (A + 5*B)`. The call is
  guarded by `[ebp+0xc]+1 == [ebp+8]`, i.e. it runs on the last iteration of
  the caller's loop, once per frame.
- `[esi+0x225]` is the frame work budget: `0x4252e0` computes it as
  `base(+0x221) * clamp(pct, 5, 1000) / 100`, a user-facing quality setting.
- `[+0x24ef]` is the player's own id; the `!= 0xa` test skips the unassigned
  / neutral slot. Same field the enqueue reads as `[[unit+8]+0xb8]+0xeb`.
- `[+0x24ee]` is the player type; the scheduler serves types 1, 2 and 3.
- `[+0x24e7]` is the budget class byte; its writers are in "Path-budget class:
  every player, not humans" above. Its other consumers all pair it with
  player type 2: `0x40fe00` gates a sound (`0x50cd10` id `0x21`), `0x401e6d`
  gates a float threshold warning against `[+0x10c]+8`, and `0x409b9a` gates
  an ally-table test against `+0xac[playerId]`.

### `UnitType+0x249`

The mover accumulates a per-type value from it into navigator `+0x30` on each
refusal, and the tracer computes `50 / +0x249` at `0x414588` (the crowding
ring around the goal). Its value is `clamp(8/bestSpeed, 1, 255)`; no store to
it exists in the image and its FBI key name is unknown. The two refusal speed
caps divide terrain-adjusted individual speed by 2 and 5.

### The re-request cadence ladder

The service worker re-asks for a route on per-frame dice rolled FRESH at each
check (nothing stores a deadline), keyed on the navigator outcome bits and
scaled by `UnitType+0x249`:

    bit0 (crowded goal):  floater-on-water 2d10+10 (0x4e5226);
                          no refusal state 2d10+20 (0x4e5284);
                          refusal state    2d8+10  (0x4e52d3)
    bit1 (traffic):       floater-on-water 2d8+30  (0x4e535d);
                          no refusal state 2d8+60  (0x4e53bc);
                          refusal state    2d8+30  (0x4e540a)
    neither, not settled: elapsed >= 120 AND rand(120)==0 per frame -- a
                          geometric tail, not a timer (0x4e545b)
    blocked re-request:   120 ticks fixed (0x4e545b)

Two dice give a triangular distribution (for example `rand(10)+rand(10)+20`
ranges over [20,38] about 29), which keeps a crowd from all re-asking on the
same frame; a uniform draw would change the clumping. `gameTime` is
`[0x62d55c]+0x19f44`, `lastRepath` is unit `+0x110`, and the per-type scale
byte is `[[unit+8]+0xb4]+0x249`. The "floater-on-water" gate is type `+0x260`
bit 19, the FBI key `floater` (parse at `0x4c03e0..0x4c0403`), with `+0x194`,
the movement class's `minwaterdepth` (moveinfo copy block at `0x4c0e8x`),
above zero.

**The outcome bits, emulated** (`0x414450` called with planted search
objects): the FAILURE report (arg != 0, reached from the -1 branch of the step
runner at `0x415b53`) converts the search's accumulators to navigator bits --
{+0x4c=1} -> bit0, {+0x50=1} -> bit1, {both} -> bit0 alone, {neither} -> no
bits. The delivery invocation (arg == 0) sets bit2 iff the stored step code
(+0x40, written at `0x415b22`) is positive, then walks breadcrumbs; the
tolerance walk is skipped then (`0x4144bf`), so bits 0/1 from the walk mark
only clean reconstructions. The accumulators are set at VISIT time
(`0x414951..0x4149ab`): every score-5 cell the march touches latches +0x4c
(within 50/+0x249 of the goal) or +0x50 (elsewhere); the reconstruction walk
recomputes the same pair over breadcrumbs for delivered routes.

**bit3** is set at `0x414642 or ebx, 8`, in the delivery walk of `0x414450`,
gated at `0x414635` by `[ebp-0xc]` (waypoints walked back from the goal) >
`[ebp-0x14]` (the Manhattan distance start->goal, computed at `0x414516`): a
detour-ratio test. It also clears bits 0 and 1 (`0x414654` / `0x414660`).
The ladder's `test [+0x134], 0xc` at `0x4e545b` makes bit2 or bit3 suppress
the plain re-ask branch. A clean tracer arrival stores +0x40=0 (`0x4146e0`
returns 0 on arrival, `0x415b22`), so a normal direct success sets neither and
falls into the plain branch.

A failed search with no crowd evidence reports NO bits, so the plain branch
governs it: long hauls that fail at the node limit chain their way across the
map, and a unit against a sealed wall probes it a few times a minute.

World implements the whole ladder: visit-time accumulators feeding the failure
report (priority bit0), walk-time flags feeding delivered routes, the full
flag-keyed dice table rolled fresh per frame, the floater/minwaterdepth gates,
and the plain geometric tail suppressed by the route's settled/detour flags
(`Unit::routeFailed`, `Unit::routeDetour`). There is no separate periodic
re-anchor sweep or backoff. `cadence_test` pins the two ends (gentle probing at
a sealed wall, re-anchoring on a long march).

### The phase machine and the cost search

The scheduler `0x416430` advances each request through its phase field
(`+0x5c`):

- **0 -> `0x415170`**: the search INITIALISER. Builds the per-grade cost table
  (`+0xbc/c0/c4/c8`), the 8 direction costs (`+0x70..0x8c`), the node budget
  (`+0xec`), and clears the accumulators and navigator outcome bits 0-3. It
  sets the floater gate (`+0xbc` = 0x30 normally, 0x140 for a water-capable
  floater) and three roadmultiplier-scaled costs (`+0x172` x the rdata
  doubles `0x5ebc08` / `0x5eba78` / `0x5ebc10`). Advances to phase 1.
- **1 -> `0x415b10` -> `0x4146e0`**: the boundary tracer. Its step result
  lands in `+0x40`: -2 keeps phase 1 (more steps), 0 = arrived, -1 = failed.
- **2 -> `0x4142c0`**: a best-first cost search. Pops the min-priority node
  off a heap (`0x416a30`), returns when the popped cell carries the goal bit
  (`0x4143fc test al,4`), else closes it (`or al,2`) and expands neighbours,
  each neighbour's cost from `0x413e70` with the per-grade cost selected at
  `0x414160` (grade 5 -> +0xc8, 4 -> +0xbc, 7 -> +0xc4, 6 -> +0xc0).

The composition:

- Tracer ARRIVES (result 0, `0x415b6b`): seeds the heap with the start node
  and leaves phase at 2; the cost search produces the route. The tracer is a
  reachability probe, not the route producer, when the goal is reachable.
- Tracer FAILS (result -1, `0x415be2`): builds the best-effort breadcrumb
  route to its closest approach via `0x4e4ea0` and stops; the heap stays
  empty, so phase 2's guard skips it.
- Phase 2 over budget or exhausted (`0x4166f1`): retry up to 3, then the 9x9
  diagnostic scan and a cleared route ("Scheduler admission and cost-slice
  ports").

The 64-waypoint navigator cap (`0x4e4ea0`) bounds each installed leg.

**The cost model, emulated** (`tools/re/emuphase.py` constructs retail's real
search object `0x415f80`, runs init `0x415170` and drives the phase machine
through to a reconstructed route):

    step cost   cardinal 16, diagonal 23           (+0x90 table)
    turn cost   0/80/120/160/200 by |heading turn|  (+0x70 table)
    grade cost  open(6) 24, road(7) 8, slope(4) 48, traffic(5) 80
    turn pen.   136 within a 3-cell window; floater-on-water scales all ~5.3x

Priority adds the controller's goal distance times a changing 16.16 weight,
and the first-attempt node limit is cells/20 (see "Scheduler and mission
capture"). Retail's turn cost (80..200) dwarfs its step cost (16..23), so the
search minimises turns more than distance. The cost function (`0x413e70`)
grades only the destination cell, so a diagonal can round a wall corner.
Observed on a wall detour (start (20,10), goal (1,10), wall x=10 z=0..15),
retail routes (20,10)->(14,16)->(10,16)->(4,10)->(1,10), rising over the wall
on diagonals, at cost 1084.

**The heap (`0x416a30`) is a plain binary min-heap with no secondary key.**
Its sift-down takes the current node at `[ebp+8]`, its children at `2i+1` /
`2i+2`, compares a single integer key at `node+0xc`, and stops on `jge` (a
child equal to its parent does not rise; between two equal children the LEFT
one wins). Among equal-priority nodes retail's pop order is a product of
insertion and sift order. `RetailCostSearch` (`src/sim/retailcost.h`)
reproduces root reuse, strict heap ties and decrease-key handling, verified
pop by pop ("Scheduler and mission capture").

`emuphase.py`'s traps: work budget `+0x165` must be nonzero or the march takes
zero steps; the allocators (`0x4eb9e0` malloc, `0x4eba00` free, `0x5ba3d0`
alloc) are all CDECL -- shim them to clean ZERO args or the stack corrupts
across the node-pool grow.

### Not established

- **Short-hop replanning.** The deadline re-ask above is the only replanning
  cadence found; whether retail also plans to a near waypoint rather than the
  goal was not determined.
- **Retail-under-wine pacing.** Grades, formulas and the ladder's branch logic
  are traced and emulated, but convergence pace, wedge frequency and the
  wall-clock feel of the geometric re-ask tail have not been measured against
  a running retail process; the crowdbench and `retailgap` bars are fixtures
  around traced invariants, not retail measurements.

## Retail audit: embarked cargo sight

**The sight pass includes transport cargo, at the attach point, with the
passenger's own sight**, in every pathfinding mode.

Binary evidence:

- The sight batch `4f6a60..4f6ac0` calls `4c6c00` for every pool unit that has
  `+0x130` bit `0x1000000`, lacks the dying bit `0x1000`, and has a zero build
  fraction. It has no attachment test.
- The pool mover loop (`51f2b7..51f2dc`) runs `4dc800` for every unit with a
  mover. `4dc800` ends with `4dad30` (`4dc958`). The attached branch of `4dad30`
  (`4dad3e..4dadd2`) works as follows:
  - It takes the host's attach point from `4dd250(out, host, (int8)+0x112)`. That
    point is the host's `+0x68` position plus the `4dd0f0` piece offset.
    `4dd0f0` returns zero when the piece is negative, out of range, or has no
    drawable; otherwise it is `retailPieceOrigin`, already verified by
    `check_piece_origin`.
  - For a floater (type `+0x260` bit `0x80000`), it raises y to
    `(waterline(+0x248)*0xffff + sea) << 16`, which wraps to `(sea-waterline)<<16`.
  - `51b3b0` stores the result as the cargo's own `+0x68`.
- Both transport pickup handlers attach with piece -1. `408d00` (GROUND_PICKUP)
  and `41add7` (VTOL_PICKUP) call `51b4f0(passenger, carrier, -1, 0, 1)`, and
  `51b5a0` copies packet byte 5 to `+0x112` at `51b6b0`.
- The only other piece source is COB ATTACH_UNIT (`50e04f`). A byte scan of all
  204 shipped scripts finds no `0x10083000` opcode. Transport cargo therefore
  sits exactly at the carrier position, at the carrier's height.
- The build mission (`401f78`) also attaches with a piece, its build pad, but
  units under construction are excluded from sight. World has no factory-pad
  attachment, so that path is not modelled.

Port:

- `retailAttachPoint` (`4dd250`) and `retailAttachedPosition` (the `4dad30`
  attached branch) live in `src/sim/retailtransport.h`.
- World's per-unit cargo follow step commits x, groundY and z from them,
  with piece -1.
- `updateNavigationExploration` includes `embarked()` units.
- The area-reclaim visibility pass (`World` reclaim area) and the display-only
  effect visibility (`gameview_impl.cpp`) include cargo footprints too, so all
  three readers agree on which footprints are live.
- The commit runs in unit order inside the main unit loop, as retail's runs in
  pool order. Cargo earlier in the pool therefore sees the carrier's previous
  position, just as in retail.

Mode decision: **world-level, all modes.** Exploration is shared simulation
state (`navigationExplored_` is hashed and feeds search grades in every mode),
and `updateNavigationExploration` has no mode branch. No mode document claims a
different exploration rule, so retail fidelity applies.

Verification:

- `check_cargo_attach_position.py` runs the native `4dad30` attached
  segment (stopped at `4dadd7`, after the commit) for 20,000 randomized hosts,
  heights, floater flags, waterlines, sea levels and pieces. It compares each
  result with World's helpers through `transport_test --attach-position`. All
  20,000 match: 4,917 floater clamps and 5,021 non-negative pieces on a host
  without a drawable.
- `check_surface_unload_map_route.py --native-exploration` also runs the
  passenger's native `4dad30` commit every step, in World's pool order relative
  to the carrier. It requires the committed `+0x68` to equal World's cargo
  position, then runs `4c6c00` for the passenger as well as the carrier.
  - Lake Lokken Vertrans/Araarch: 2,216 commits and 1,800 revealed cells match
    through release. Araarch's sight (180) is shorter than every carrier's, so
    it reveals nothing new there.
  - With `--cargo-sight 600`, the passenger reveals 4,027 cells. The changed
    exploration alters the route itself (release at step 1,549 instead of
    2,217), and native and World match on every step.
  - Skipping `embarked()` cargo fails at step 1 (native reveals column 102/103
    cells).
- ctest `transport` has a `cargoSight` case covering cargo position and
  height on a flying carrier, the extra exploration, and the floater-clamp edge
  cases.

The navigation checkpoint and crowdbench scenarios carry no cargo; only
worlds with loaded transports hash cargo exploration and cargo `groundY`
while aboard.

Residual differences:

- At boarding, World relocates the passenger to the carrier immediately.
  Retail's `51b5a0` does not move it, and its next `4dad30` does. Cargo earlier
  in the pool than its carrier therefore explores from the carrier one tick
  early in World.
- `51b3b0` also re-stamps the cargo's cells (`506aa0`/`5066f0`) when its cell or
  state bits change. The harness observes only the committed point, and World
  keeps cargo out of the occupancy tables. Whether retail stamps attached state
  0 is unverified.
- `4dadfc..4dae6e` copies the host orientation (`4dd370`) and mover velocity to
  the cargo. World keeps cargo speed at zero; nothing here reads it.

## Emulator early-stop guard

`emu.Icd.call` raises `EmulationIncomplete` when unicorn stops with EIP not at
the return magic, saying whether the budget (a 5 s wall-clock `timeout`) ran
out or the run was stopped early, so a long native call under CPU load cannot
pass a check while half-run.

- Callers that deliberately stop inside a call set `allow_early_stop=True` on
  the instance or the call. Eight scripts use `emu_stop` hooks and opt in on
  their instance: `check_ai_patrol_points`,
  `check_captured_tick`, `check_cost_search`, `check_entity_slots`,
  `check_search_attempt`, `probe_campaign_chapter_art`, `probe_campaign_score`
  and `probe_monarch_alarm`.
- `emuphase.py` and `emupath.py` insert their own directory at the front of
  `sys.path` (not a fixed checkout path), so a harness in a worktree imports
  that worktree's `emu.py`.
