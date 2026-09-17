# Porting retail's movement layer

Design for replacing our movement/pathfinding with retail's, warts included.
Written 2026-09-16 after the report that units stack and drive into obstacles.
Every address below was read from `KINGDOMS.icd`; the RNG was additionally run
under emulation. Anything NOT established is called out as such -- the brief was
to assume nothing, so unproven things are marked rather than filled in.

## The shape of it: retail has two layers, we built one and a half

**Layer 1, the route.** The boundary tracer, `0x4139d0`-`0x416xxx`, ported in
`src/sim/pathsearch.*`. Routes around TERRAIN. Knows nothing about units.

**Layer 2, the step.** The mover `0x4dfd70` and the cell raters
`0x5086xx`-`0x5094xx`. Rates the cells around a unit 0..7 INCLUDING units, and
steers on that. We have none of it.

We also added a bounded A* that retail does not have. So our movement is layer 1
plus an invention, with layer 2 missing entirely -- which is the whole bug:
nothing in our engine makes a moving unit solid, and nothing routes around one.

## Layer 2 in detail

### The rating, `0x509020`

Returns a passability rating where HIGHER IS BETTER. Confirmed against an
independent second implementation at `0x404ff9`.

    7   open ground                     (initial value, 0x50904d)
    6   terrain flag clear              (+0xd bit 0x80, at 0x509348)
    4   slope tests                     (0x509318 / 0x509322)
    1   cell held by a MOVING unit      (0x50917c)
    0   cell held by a PARKED unit      -- blocked

Cell record: word `+8` is the occupant id; `0xffff` empty; `0xfffe` marks a tail
cell of a multi-cell body, whose owner is found by walking back with the bytes at
`+0xa`/`+0xb` and the map stride `[0x62d55c]+0x19e98`. Byte `+6` is the height
the slope test reads.

Units: table `[0x62d55c]+0x19edc`, stride `0x140`, count `+0x19ec0`. Flag word at
unit `+0x13c`: bit 5 (`0x20`) "a unit occupies this cell", bit 17 (`0x20000`)
"it is moving". Every exit reports through `0x509390`.

A parked body is a WALL. A moving body is passable at 1/7 the rating of open
ground. Body-blocking a bridge is a real tactic and falls out for free.

### Corner clearance, `0x508e50`

Rates a cell together with its orthogonal neighbours and demands `> 4` and
`>= 6`. A unit will not cut the diagonal between two bodies. `0x508cd0` is the
same test against the second rater `0x5088f0`. `0x508e20` initialises the working
set: clears `0x121` dwords at `0x6405d4`, stores its two parameters at
`0x6405d0`/`0x6405cc`.

### The local window, `0x4dfd70`

Per mover, per step, fills a buffer at `ebp-0x1ec` with the ratings over the
unit's footprint window, dispatching on footprint size (`<= 5`, `<= 11`, at
`0x4e0260`) between a batched fill `0x508db0` and a per-cell `0x508cd0`, writing
each through `0x4dfe40`. Called from `0x4bfa50` <- `0x4c1950` <- `0x525850`
(the main per-frame update).

The hard "may I stand here" test is `0x507d10` (nine call sites).

## Scheduling: what makes hard solidity survivable

`sim.h` claimed retail affords solidity through "continuous short-hop
replanning, randomised repath delays and an age-weighted per-player path
budget". Resolved one at a time:

### Randomised repath delay -- REAL, and this is the jank to copy

In the service worker around `0x4e5274`:

    r1 = rand(10); r2 = rand(10)
    deadline = lastRepath + (r1 + r2 + 20) * typeByte[+0x249]
    if gameTime >= deadline: enqueue a fresh path request

`gameTime` is `[0x62d55c]+0x19f44`, `lastRepath` is unit `+0x110`, and the
per-type scale byte is `[[unit+8]+0xb4]+0x249`. A second site at `0x4e5230`
uses the same shape with `+0xa` instead of `+0x14`, i.e. a different constant
for a different situation.

So the delay is `rand(10)+rand(10)+20` type-ticks: range [20,38], TRIANGULAR
about 29, not uniform -- two dice, not one. That distribution is the wart. It is
what keeps a crowd from all repathing on the same frame, and copying a uniform
`rand(20..38)` instead would change the clumping behaviour.

`rand(n)` is the GAME's RNG at `0x535cc0` (seed `0x64186c`, 460 callers), NOT
the CRT `rand()` at `0x5d4444`. Verified under emulation: returns `[0, n-1]`,
returns 0 for `n < 2`, and advances the global seed each draw.

    rand(0)=0  rand(1)=0  rand(2)=1  rand(10)=5  rand(100)=15   (seed 12345)
    20 chained rand(10) from seed 1:
      7 9 3 8 0 2 4 8 3 9 0 5 2 2 7 3 7 9 0 2

METHOD NOTE, because it cost a wrong conclusion: searching for callers of the
CRT `rand()` found none in the movement chain, and this was briefly written up
as "retail has no randomised repath". The game has its own RNG. Absence of the
CRT one proves nothing.

### Per-player budget -- REAL, per PLAYER, weight 5. Ours is per REQUEST.

`0x634674` is a 10-dword table, one slot per player, holding the number of that
player's units currently queued for a path. `0x4e4f50` increments a player's
slot when a unit enters the queue and decrements it when it leaves, guarded by
flag bit 2 of `+0x114`; the player index is `[[unit+8]+0xb8]+0xeb`. The whole
table is cleared once per frame by `0x4e5080`, reached from the path-service
tick `0x4e6060` <- `0x525850`.

The split, at `0x416430`/`0x4164fa`: walk the 10 slots, keep players that are
active (`[+0x2404]` non-null), of type `[+0x24ee]` in {1,2,3}, with
`[+0x24ef] != 0xa`, and with a non-zero pending count. Sort them into two
classes by byte `[+0x24e7]`, then

    perShare = (budget / arg) / (normal + 5*special)
    share    = special ? perShare*5 : perShare

with the frame budget at `[esi+0x225]` and the running total at `[esi+0x165]`.

CONFIRMED UNDER EMULATION. Synthetic game state, ten player slots, budget 1000:

    2 normal                -> 500 / 500
    normal + special        -> 166 / 830          (830 = 166*5)
    special + normal        -> 830 / 166          (order-independent)
    2 normal + 2 special    -> 83 / 415 / 83 / 415
                               denom 2+5*2=12, 1000/12=83, special 83*5=415

THE COUNT IS PLAYERS, NOT REQUESTS -- and this is the decisive result. Vary the
pending count and the share does not move:

    two normal, each pending=1   -> 500 / 500
    two normal, each pending=50  -> 500 / 500
    pending 1 vs pending 99      -> 500 / 500

A player with 99 queued units gets exactly the same frame budget as a player
with one. The scheduler increments its bucket ONCE PER PLAYER that has any
pending work; `0x634674[p]` is only ever tested `> 0`, never summed. That is
per-player fairness regardless of army size, and it is the behaviour to copy.

This corrects two things. `docs/retail-engine.md` (2026-09-12) describes the
scheduler as "counting pending REQUESTS into two buckets" with a "per-request
quantum" -- it is per player, and the quantum is per player. And our own
`pathsearch.cpp` splits the budget across REQUESTS ("a flagged request is worth
five ordinary ones"), which is the wrong axis: ours lets one player with 500
units starve another, retail's cannot. The port must move the weighting to the
player axis.

NOT ESTABLISHED: no AGE weighting exists. The weight is the player-class byte
`[+0x24e7]`. What that byte MEANS is still inference, not fact: the only other
consumers are `0x40fe00`, where `type==2 && [+0xe3]!=0` gates a SOUND
(`0x50cd10` id `0x21`), and `0x409b9a`, where it gates a relationship test
against the ally table at `+0xac[playerId]`. Sound playback is local-only, so
the evidence points at "the local player", i.e. the human at this machine gets
5x the path budget for responsiveness. THAT IS A GUESS AND IS MARKED AS ONE.
No store to the field was found anywhere in the image -- it is filled wholesale
from lobby/save data -- so confirming it needs a different method than a search
for writes.

### Continuous short-hop replanning -- NOT ESTABLISHED

The deadline re-enqueue above is a replanning cadence, and it is the only one
found. Whether retail also shortens the ROUTE (plans to a near waypoint rather
than the goal) was not determined. Do not build on it.

## Consequences for our code

1. **Cell ratings gain units.** Parked 0, moving 1, open 7, with the slope and
   flag clamps. Occupancy must record MOVING units, which ours deliberately does
   not: `sim.h` records only stationary ones, and the separation relaxation
   "resolves overlap after the fact and cannot stop anyone" -- which is exactly
   why they stack.
2. **A local rating window per mover per step**, with the corner-clearance test.
   This is the piece that makes a unit go AROUND rather than into.
3. **Repath on the randomised deadline**, triangular, per unit type.
4. **Move the 5x budget weighting from requests to players.**
5. **Delete the A*.** It is not in retail. It exists because our layer 1 alone
   handles crowds badly -- which is layer 2's job, so once layer 2 lands the A*
   is compensating for a bug that no longer exists.

## Data types

Ratings are small integers 0..7; cell ids and occupant ids are `uint16` with
sentinels at `0xfffa`..`0xffff`; the player table is 10 `int32`; the repath
deadline is a game-time comparison in ticks. None of this wants floating point,
which suits the fixed-point sim.

## Order of work

The A* removal REGRESSES crowd behaviour until layer 2 lands, because the A* is
currently papering over the missing layer. It is still the right thing to do
first -- keeping it would mean building layer 2 against a planner that hides the
very behaviour being ported -- but it should land with that regression stated,
not discovered.

Every step changes hashed sim state, so the `--mpai` baseline moves and the
remote sweep has to be re-run at the end.

## Open questions, all blocking a faithful port

RESOLVED:

- The budget formula and the 5x weighting -- confirmed under emulation, above.
- The count is PLAYERS, not requests -- confirmed under emulation, above.
- `[esi+0x225]` is the frame work budget. Already documented in
  `retail-engine.md`: `0x4252e0` computes it as `base(+0x221) * clamp(pct, 5,
  1000) / 100`, i.e. pathfinding effort is a user-facing quality slider.
- `[+0x24ef]` is the player's own id; the `!= 0xa` test skips the unassigned /
  neutral slot. Same field the enqueue reads as `[[unit+8]+0xb8]+0xeb`.
- `[+0x24ee]` is the player type, and the scheduler serves types 1, 2 and 3.
  `0x40fe00` shows type 2 taking a local-only sound path.

STILL OPEN, and not to be guessed:

- What player byte `[+0x24e7]` actually is. Evidence points to "local player";
  see above for why that is inference. It decides who gets 5x, so it matters.
- What type byte `[+0x249]` is -- the per-unit-type repath scale. The repath
  cadence is meaningless without it.
- Whether the ROUTE is shortened as well as the timing randomised
  ("short-hop replanning"). Only the timing was found.
RESOLVED 2026-09-16 (second pass):

- **The divisor is 1.** `0x416430` has exactly one caller, `0x4f6ca0`, and it is
  a literal `push 1`. So `budget / arg` is a no-op in the shipped game and the
  quantum is simply `+0x225 / (A + 5*B)`. The call is also guarded by
  `[ebp+0xc]+1 == [ebp+8]`, i.e. it runs on the last iteration of the caller's
  loop, once per frame.

- **`UnitType+0x249` is not new.** `retail-engine.md` already has it: the mover
  accumulates a per-type value from it into navigator `+0x30` on each refusal --
  the clamp-and-slow ported in `080d288`. The tracer also computes `50 / +0x249`
  at `0x414588`. NOTE A DIVERGENCE: our port of the clamp-and-slow hardcodes
  0.5x then 0.4x rather than deriving from this per-type byte, so our refusal
  behaviour is type-independent where retail's is not. Its FBI key name is still
  unknown and no store to it exists anywhere in the image.

- **`+0x24e7`: the evidence is now three consumers, all local-player feedback,
  and all pairing it with player type == 2.** `0x40fe00` gates a sound
  (`0x50cd10` id `0x21`); `0x401e6d` gates a float threshold warning against
  `[+0x10c]+8`; `0x409b9a` gates an ally-table test. Sound and warnings are
  local-only, so "the local human player gets 5x the path budget" is the reading.
  IT REMAINS INFERENCE. No store to the field exists in the image -- it is
  filled from lobby/save data -- so this cannot be settled by reading code, and
  it is NOT to be written into the port as fact.

STILL OPEN:

- Whether the ROUTE is shortened as well as the timing randomised
  ("short-hop replanning"). Only the timing has been found.
- The FBI key behind `UnitType+0x249`.
- Confirmation (not inference) of what `+0x24e7` is.

## What is ready to build, and what is blocked

READY -- fully specified, depends on none of the open questions:

  * the 0..7 cell rating including units (parked 0, moving 1, open 7, slope and
    flag clamps at 6 and 4);
  * moving-unit occupancy, so a moving body is solid-but-cheap rather than
    absent;
  * the corner-clearance test (`> 4` and `>= 6` on the orthogonal neighbours);
  * the per-unit local rating window rebuilt each step.

That set is the whole reported bug -- stacking and refusing to go around -- and
it can be ported now.

BLOCKED on the open questions:

  * the repath cadence, which needs the meaning of `UnitType+0x249` to scale
    correctly (the shape `(rand(10)+rand(10)+20) * scale` is known);
  * moving the 5x budget weighting to the player axis, which needs to know which
    players are the 5x class.


## IMPLEMENTED (2026-09-16/17): the movement layer is retail's

Everything below is in the tree, each piece against its address. The A*, the
string-pull, the exemption wrapper, lineOpen, the pixel collision model
(bodyPenetration, the 0.75px touch slack, the already-inside skip), the axis
slides, the sideways-dodge stuck watchdog, and the no-headway abandon-and-rescue
apparatus are all DELETED -- every one was ours, and every one existed to
compensate for some other missing retail piece.

**Grades** (dynamically verified, tools/re/emupath.py: the two map raters return
empty 6 / parked 0 / moving 1 regardless of heading, so the same-way rule
provably lives in the live query, not the map):

    search scorer (0x4db640 semantics): terrain 0/6/7 per cell; a body grades 2
      (blocked) unless under way, not slower, heading within 90 deg -> 5
      (kCellSameWay, marked kScore5 -- the crowding evidence)
    mover probe: ONE cell -- the projected position's own cell (the start-cell
      check at 0x41472e runs through the same single-cell query). The
      footprint-swept variants we had were stricter than retail and manufactured
      multi-minute mutual wedges retail cannot express.
    placement keeps the footprint loop (0x507d10): spawning is strict, moving is
      per-cell. The visible price is retail's own: bodies and terrain corners
      clip by up to half a footprint.

**Cadences** (all traced, all scaled by UnitType+0x249 = clamp(8/bestSpeed,1,255)):
    blocked re-request: 120 ticks fixed (0x4e545b)          [was already ported]
    stale route, goal crowded:  rand(10)+rand(10)+10  (0x4e5226)
    stale route, normal:        rand(10)+rand(10)+20  (0x4e5284)
    failed route:               rand(8)+rand(8)+30    (0x4e535d)
Triangular on purpose -- two dice desynchronise a crowd's re-asks. Drawn from a
dedicated minstd stream (World::pathRand), deterministic per peer.

**Failure = best-effort** (0x415170): a failed search reconstructs the walk to
its closest approach and the unit takes it; the ordered point stays appended, so
the order survives, the unit presses at the crowd's edge under the 0.5x/0.4x
refusal caps (two-stage, keyed on the refusal streak -- navigator 0x100/0x200),
and the failed-route cadence re-asks from closer as the pack tightens. That loop
IS retail's convergence; there is no settle order and no give-up. The
64-waypoint navigator cap (0x4e4ea0) bounds each installed leg and re-anchors,
which is what tames the tracer's outline-hugging wander.

**Budget**: split per PLAYER with the 5x class (0x4164fa; emulated), the class
mapped to seated humans (the only lockstep-safe reading of +0x24e7; wired as
World::setHumanPlayers, empty mask until the lobby passes it).

**Measured profile** (crowdbench): opposing columns 29-30/32 (head-on files
wedge a few -- retail's documented behaviour), chokepoint 22-24/24, group order
32/32, open field 24/24, serpentine 12/12 at travel x6.65 (the naked tracer's
maze wander, no A* hiding it). retailgap: a walker routes AROUND a parked body
and arrives; head-on never tunnels; a 24-unit convergence packs 21+ into
retail's own crowding ring (50/+0x249 cells) with nobody abandoning.

## Still open, explicitly

- **Retail runtime ground truth.** Grades, formulas and mechanisms are traced
  and emulated, but no side-by-side run against retail itself (wine) has
  measured convergence pace or wedge frequency. The quantitative bars in
  retailgap/crowdbench are our fixtures around traced invariants, not retail
  measurements.
- **+0x24e7 = "human"** remains the inference (all its other consumers are
  local-feedback paths; a local reading would desync a lockstep sim).
- The +0x36 refusal-flag cadence variant (0x4e52d3, rand(8)-based) and the
  crowded-success type gates (+0x260 bit 0x80000, +0x194 > 0) are traced but
  not ported -- the flags they key on have no exact analogue yet.
- kScore5's producer in the MAP fills was never found (the raters emit no 5);
  the live-query reading makes it same-way traffic, which is what we mark.


## ATTEMPTED AND REVERTED (2026-09-17): the full cadence ladder

The remaining traced pieces -- the refusal-flag cadence variant (0x4e52d3) and
the crowded-success type gates -- were RE'd to completion and then implemented,
and the implementation was REVERTED on measurement. The record, so the next
attempt starts where this one stopped:

**What the RE established, and stands:**
- The complete per-frame ladder (dice rolled FRESH each check; nothing stores a
  deadline):
      bit0 (crowded goal):  floater-on-water 2d10+10 (0x4e5226);
                            no refusal state 2d10+20 (0x4e5284);
                            refusal state    2d8+10  (0x4e52d3)
      bit1 (traffic):       floater-on-water 2d8+30  (0x4e535d);
                            no refusal state 2d8+60  (0x4e53bc);
                            refusal state    2d8+30  (0x4e540a)
      neither, not-failed:  elapsed >= 120 AND rand(120)==0 per frame -- a
                            geometric tail, not a timer (0x4e545b)
- The type gates decoded: +0x260 bit 19 is the FBI key `floater` (parse at
  0x4c03e0-0x4c0403) and +0x194 is the movement class's `maxwaterdepth`
  (moveinfo copy block at 0x4c0e8x). The fast crowd cadences are for
  water-capable floaters.
- Bit 2 of +0x134 is set when the search object's +0x40 counter is positive
  (0x4144bf), and the tolerance walk is SKIPPED then -- so bits 0/1 from the
  walk mark only clean reconstructions.

**What is NOT established, and why the implementation lost to measurement:**
reconstruction ENTRY also sets bit0 / bit1 on its own conditions (`or 1` at
0x41448b, `or ecx`=2 at 0x4144aa, keyed on search fields +0x50 and the branch at
0x414476) -- and those conditions were never read. Mapping them by guess
(failed-with-progress ~ bit1) produced a ladder that stranded long hauls or
over-asked, and crowdbench regressed against the shipped state on every
scenario (chokepoint 16/24 vs 22-24, group order 27/32 vs 32/32, open field
18/24 vs 24/24, serpentine 10/12 vs 12/12). The shipped stored-deadline
approximation is not retail's mechanism either, but it is measurably CLOSER in
observables, and "not better, not worse" cuts both ways.

**To finish this properly:** read the 0x414476/0x41448b/0x4144aa entry
conditions and the +0x110 stamp discipline (who re-stamps, and when), or
emulate 0x414450 with planted search states to enumerate the bit table the way
emupath.py enumerated the grades. The WIP implementation is preserved as a
patch in the session scratchpad (ladder-wip.patch) and included: the per-frame
ladder, the traffic flag through PathService, the failed-quiet branch, and a
cadence_test with two behavioural checks (failed-quiet, geometric-tail) that
passed against the ladder but presume the unverified bit semantics.


## FINISHED (2026-09-17): the ladder is in, on emulated bit semantics

The reverted attempt above is superseded. The missing reads were done, and the
one that mattered was done the way emupath.py settled the grades -- by running
the binary's own code against planted state:

- **The bit table, emulated** (0x414450 called with planted search objects):
  the FAILURE report (arg != 0, reached from the -1 branch of the step runner
  at 0x415b53) converts the search's accumulators to navigator bits --
  {+0x4c=1} -> bit0, {+0x50=1} -> bit1, {both} -> bit0 alone, {neither} -> no
  bits. The delivery invocation (arg == 0) sets bit2 iff the stored step code
  (+0x40, written at 0x415b22) is positive, then walks breadcrumbs.
- **The accumulators are set at VISIT time** (0x414951-0x4149ab, read in
  phase): every score-5 cell the march touches latches +0x4c (within
  50/+0x249 of the goal) or +0x50 (elsewhere). The reconstruction walk
  recomputes the same pair over breadcrumbs for delivered routes.
- **0x415170 is the search INITIALISER**, not the failure path (an old note
  mislabelled it): it clears the accumulators and navigator bits 0-3 per
  request, and computes per-type march parameters -- including a floater-gated
  one (+0xbc: 0x30 normally, 0x140 for water-capable floaters) and three
  roadmultiplier-scaled ones. Those parameters are NOT yet ported (they bound
  march legs; ours uses the visit-limit form).
- **The consequence that fixed the pacing**: a failed search with no crowd
  evidence reports NO bits, and the plain branch (elapsed >= 120 then a
  1-in-120 roll per frame, 0x4e545b) governs it -- so long hauls that fail at
  the visit limit chain their way across the map, and a unit against a sealed
  wall gently probes it a few times a minute for ever. "Failure means quiet",
  which the first attempt shipped, was wrong and measurably stranded units.

What is in the tree now: visit-time accumulators feeding the failure report
(priority bit0), walk-time flags feeding delivered routes, the full flag-keyed
dice table rolled fresh per frame, the floater/maxwaterdepth gates, and the
plain geometric tail -- with our goalStuck retry engine, the periodic re-anchor
sweep, and the pathRetryAt_ backoff all deleted as duplicates of it.
cadence_test pins the two ends (gentle probing at a sealed wall, re-anchoring
on a long march). Crowdbench against the pre-ladder build: opposing columns
32/32 (was 29-30), serpentine 12/12, group order 31/32, chokepoint and open
field medians identical with slightly slower tails -- the tails now wait on
retail's own 2d8+60 traffic dice rather than our uniform invention.

Still open: the 0x415170 march parameters (+0xbc/+0xc0/+0xc4/+0xc8), bit3's
setter (nothing found; only the clear sites), and the retail-under-wine ground
truth for pacing.

## RE (2026-09-17): bit3 found, and +0xbc is not what its name said

Chasing the two items left open above. One resolved cleanly; the other turned
out to be the tip of an unported search phase.

### bit3's setter -- FOUND, and it is a delivered-route detour flag

`0x414642  or ebx, 8`, in the DELIVERY walk of 0x414450 (arg==0, the
successful-reconstruction path). It is gated at 0x414635 by

    [ebp-0xc]  (waypoints walked back from the goal)  >  [ebp-0x14]  (the
    Manhattan distance start->goal, computed at 0x414516)

-- a detour-ratio test: the route wandered more cells than the straight line.
When it fires it also CLEARS bits 0 and 1 (0x414654 / 0x414660). Via the
service ladder's `test [+0x134], 0xc` at 0x4e545b, a set bit3 (like bit2)
SUPPRESSES the plain re-ask branch. So retail stops re-asking a delivered route
once it is "settled enough" -- either a clean valid delivery (bit2, requires
+0x40 > 0) or a detoured one (bit3).

Confirmed the delivery bit conditions under emulation (0x414450 arg==0):
+0x40=0 -> no bits, +0x40>0 -> bit2. A clean tracer arrival stores +0x40=0
(0x4146e0 returns 0 on arrival, 0x415b22), so a normal DIRECT success sets
neither bit2 nor bit3 and DOES fall into the plain branch -- consistent with
the shipped v83, where the plain 1-in-120 tail governs any route with no
crowd/traffic marks. bit2/bit3 suppression applies only to the +0x40>0 and
detour cases, which the shipped ladder does not distinguish. That is a known
minor divergence, not a regression: it means a few settled routes re-ask on the
tail where retail would stay quiet, at the cost of a handful of extra searches.

### +0xbc/c0/c4/c8 are per-grade QUEUE-SEARCH costs, not march bounds

The prior note called these "march parameters that bound march legs". That was
wrong. 0x414160 selects among them BY CELL GRADE:

    grade 5 -> +0xc8      grade 4 -> +0xbc
    grade 7 -> +0xc4      grade 6 -> +0xc0   (default)

and 0x413e70 adds the selected cost into a node total that 0x416a30 inserts into
an OPEN LIST. That is a cost-ordered (best-first) queue search, driven by
0x4142c0 -- and it is a SEPARATE PHASE from the boundary tracer we ported. The
scheduler's per-request phase field (+0x5c) runs: 0 -> 0x415170 (initialise the
costs), 1 -> 0x415b10 -> 0x4146e0 (the geometric tracer, the part we have), 2 ->
0x4142c0 (the cost queue). 0x415170 -- long mislabelled "the failure path" --
is the phase-0 initialiser; it also sets the floater gate (+0xbc = 0x30
normally, 0x140 for a water-capable floater: +0x260 bit19 `floater` AND +0x194
`maxwaterdepth` > 0) and three roadmultiplier-scaled costs (+0x172 x the rdata
doubles 0x5ebc08 / 0x5eba78 / 0x5ebc10).

THIS REOPENS THE A* QUESTION. We deleted our bounded A* (613e426) on the finding
"retail has no A*, no open list, no cost-to-goal". That finding was about the
TRACER (0x4146e0), and it holds for the tracer. But 0x4142c0 IS an open-list
cost-ordered search, and the scheduler reaches it at phase 2. What is NOT yet
established: whether phase 2 runs on every search or only when the tracer's
phase-1 result leaves the queue non-empty (the guards at 0x41665f-0x4166a6 test
+0x14/+0x18 and a node budget +0xec), and how its result composes with the
tracer's route. Until that is pinned, neither the cost port nor a claim about it
is safe -- and half-porting it is the exact mistake the reverted cadence
attempt already paid for.

### NOT ported, deliberately

No sim change. bit3's exact trigger is known but its faithful modelling needs
the +0x40>0 / bit2 outcome pinned across both search phases; the cost-queue
port needs the phase-2 execution conditions read first. The disciplined next
step is to emulate the phase machine (0x416430 driving a planted request across
phases 0/1/2) the way emupath.py enumerated the grades and the bit table --
enumerate when phase 2 fires and what +0x40 it leaves -- BEFORE porting either.

## RE (2026-09-17): the phase machine resolved -- and phase 2 IS the route

The composition, read end to end from the scheduler's phase field (+0x5c) and
the two tracer-outcome setups. No sim change: this is the map the eventual port
needs, and it corrects a conclusion we had already committed.

**The three phases** (scheduler 0x416430, per-request +0x5c):
- **0 -> 0x415170**: initialise. Builds the per-grade cost table (+0xbc/c0/c4/c8),
  the 8 direction costs (+0x70..0x8c), the node budget (+0xec), and clears the
  navigator outcome bits. Advances to phase 1.
- **1 -> 0x415b10 -> 0x4146e0**: the boundary tracer we ported. Its step result
  lands in +0x40: -2 keeps phase 1 (more steps), 0 = arrived, -1 = failed.
- **2 -> 0x4142c0**: a BEST-FIRST COST SEARCH. Pops the min-cost node off a heap
  (0x416a30), returns when the popped cell carries the goal bit (0x4143fc test
  al,4), else closes it (or al,2) and expands neighbours -- each neighbour's cost
  from 0x413e70, the per-grade table selected at 0x414160 (grade 5->+0xc8,
  4->+0xbc, 7->+0xc4, 6->+0xc0). Runs only while the heap is non-empty and under
  +0xec nodes (guards at 0x416668-0x416680).

**The composition, which is the point:**
- Tracer ARRIVES (result 0, 0x415b6b): seeds the heap with the start node and
  leaves phase at 2. Phase 2's cost search then finds the route. THE TRACER
  DOES NOT PRODUCE THE ROUTE ON SUCCESS -- it is a fast reachability probe that,
  on reaching the goal, hands off to the cost-optimal search.
- Tracer FAILS (result -1, 0x415be2): builds the best-effort breadcrumb route
  via 0x4e4ea0 and stops. Heap stays empty, so phase 2's guard skips it.
- Phase 2 skipped / over budget (0x4166f1): retry up to 3, then relocate the
  goal to a nearby passable cell via a widening 0x4139d0 scan.

**What this means for our port.** We ported the tracer (phase 1) and use ITS
route directly on success. Retail uses the cost search's route on success and
the tracer's breadcrumbs only on failure. So our successful routes are the
tracer's greedier, outline-hugging ones where retail's are cost-optimal. This
also corrects docs/retail-engine.md's "retail's tracer is a bug algorithm that
meanders" -- true of the tracer in isolation, but the tracer is not the route
producer when the goal is reachable, which is the common case. Our serpentine
x6.7 is the tracer alone; retail's cost search would be far straighter there.

**Why not ported yet.** A faithful port is a whole best-first cost search plus
the phase orchestration plus the bounded-budget fallback -- and it changes
hashed sim state, so a wrong reconstruction is a desync, not a visible bug. The
structure is read but NOT yet observed producing a route end to end. Porting on
the read alone is the "half-understood exact mechanism" the reverted cadence
attempt (e19929e) already paid for. The honest next step is a driver harness
that runs a planted request through init -> tracer -> seed -> cost search ->
route extraction on a small grid and OBSERVES the route (the way emupath.py
observed the grades and the bit table), confirming the cost model and the
composition before a line of it lands in the sim. That is a dedicated effort,
not a tail-end one.


## PORTED (2026-09-17): phase 2, the Dijkstra route producer

The harness the previous section called for was built (tools/re/emuphase.py) and
it did the job: it constructs retail's real search object (0x415f80), runs init
(0x415170), and drives the phase machine -- the tracer ARRIVES and phase 2
(0x4142c0) EXPANDS NODES -- observed live. From that run the EXACT cost model
was read off the initialised object, and phase 2 was confirmed to be Dijkstra.

**The cost model, emulated (not inferred):**
  step cost   cardinal 16, diagonal 23         (+0x90 table)
  turn cost   0/80/120/160/200 by |heading turn| (+0x70 table)
  grade cost  open(6) 24, road(7) 8, slope(4) 48, traffic(5) 80
  turn pen.   136 within a 3-cell window; floater-on-water scales all ~5.3x
  node budget map cells / 10                    (0x415c47)
  priority    g(parent) + step + turn + grade   -- NO heuristic (0x413ef5)

**The port** (PathSearch::buildDijkstraRoute): a min-heap Dijkstra with these
exact costs, tie-broken by cell index for determinism, bounded by the node
budget. It is the route producer when the tracer reaches the goal (retail's
composition: tracer = reachability probe, Dijkstra = the route); the tracer's
breadcrumb route stays the failure/over-budget fallback. Wired at all four
tracer-arrival sites.

**Measured.** Runs (651 calls / 136 within-budget completions across crowdbench),
deterministic (Ulasem Arena hash reproducible across runs), all 14 suites pass.
The effect is SMALL and that is a finding, not a defect: where the tracer
arrives its routes were already near-straight, so the Dijkstra's turn-minimising
route often coincides; the hard cases (serpentine) are tracer FAILURES that fall
back to breadcrumbs in retail too, so x6.78 there is faithful, not a gap our
port should close. Crowdbench travel ratios rise a hair (chokepoint x1.17->1.24,
open x1.02->1.04) -- the direct, predictable consequence of retail's turn cost
(80..200) dwarfing its step cost (16..23): the search minimises TURNS, not
distance, exactly as the emulated costs dictate.

**Honest scope.** The cost model and the Dijkstra algorithm are emulation-exact.
What could NOT be observed end to end is a complete retail ROUTE: the tracer's
reverse-march runs over a recentering local-window grade system, and driving it
to a produced route needs that subsystem replicated (the harness gets the tracer
to arrive and phase 2 to expand nodes, then hits a node-pool plumbing wall). So
route fidelity is by CONSTRUCTION -- a correct Dijkstra over the exact cost model
-- rather than by observation of retail's output. Determinism makes this
lockstep-safe regardless; the residual is whether retail's tie-breaks match ours
in exact-cost ties, which is cosmetic.


## VALIDATED (2026-09-17): the Dijkstra route matches retail's, observed

The harness was driven all the way to a produced route. The wall that stopped it
before was a cdecl bug in the emulation shims (the allocators clean their own
args in the harness but retail's are caller-cleaned -- double-cleaning corrupted
the stack across the node-pool grow). Fixed, the search completes: the tracer
arrives, phase 2 expands nodes, reaches the goal, and 0x414450 reconstructs the
route into the request handle. OBSERVED, on an open diagonal: a straight route.
On a wall detour (start (20,10), goal (1,10), wall x=10 z=0..15): retail routes
(20,10)->(14,16)->(10,16)->(4,10)->(1,10), rising over the wall on diagonals.

Compared to our port on the same grid: our Dijkstra reaches the goal at cost
1084 -- byte-for-byte the cost of the route retail produced (scored under our
own cost model). So the COST MODEL IS EXACT and the search finds a genuine
minimum. Our route's corners differ from retail's (both cost 1084): a tie among
equal-cost routes, broken by the heap order, which is cosmetic -- matching it
would mean replicating retail's exact heap (0x416a30) for no behavioural gain.

Two things the validation corrected in the port (kNetVersion 85):
  - The corner-cut guard (stepLegal) is REMOVED from the Dijkstra. Retail's cost
    function (0x413e70) grades only the destination cell, so a diagonal rounds a
    wall corner; keeping the guard forced wider cardinal detours our search then
    rated 1720 against retail's 1084. Crowdbench improved with it gone
    (chokepoint 23->24/24, work 6.3M->2.0M) -- straighter, cheaper, retail-shaped.
  - Confirmed the earlier "wrong" routes were the tracer FALLBACK, not the
    Dijkstra: with retail's mapcells/10 node budget the search only reaches the
    goal on SHORT trips (~20% in crowdbench); longer trips exceed the budget and
    fall back to the tracer route in both retail and our port. So the tracer
    routing we already had is retail-faithful for the majority (long-trip) case,
    and the Dijkstra refines the short-trip minority.

The harness (tools/re/emuphase.py) drives the full lifecycle and is the tool to
reach for when a route needs observing. Its traps, for the next session:
work budget +0x165 must be nonzero or the march takes zero steps; the grade
query is a local window anchored at the goal (put the goal near the origin with
large +4/+6 window offsets to grade the whole map); the allocators (0x4eb9e0
malloc, 0x4eba00 free, 0x5ba3d0 alloc) are all CDECL -- shim them to clean ZERO
args or the stack corrupts across the node-pool grow.

## RE residuals resolved (2026-09-17)

Three loose ends from the phase-2 validation, chased to ground under emulation
and static disassembly.

**The heap (0x416a30) is a plain binary min-heap with NO secondary key.** Its
sift-down takes the current node at `[ebp+8]`, its children at `2i+1` / `2i+2`,
compares a single integer key at `node+0xc`, and stops on `jge` (a child equal
to its parent does not rise; between two equal children the LEFT one wins). There
is no tie-break field beyond that key -- among equal-priority nodes retail's pop
order is whatever the heap's array positions happen to give, a pure artefact of
insertion + sift order. Our port instead breaks equal-cost ties by cell index
(`a.cell > b.cell` in buildDijkstraRoute). That is a *deterministic substitute*,
not a match: it yields an equal-cost (optimal) route, but not necessarily retail's
particular one. Matching retail's exact corner sequence would mean reproducing
this heap's push (sift-up) and pop (this sift-down) AND retail's neighbour
insertion order verbatim, and then it would still only re-select among routes of
identical cost -- no behavioural or determinism gain (our peers already agree
with each other; phase 2 only fires on the ~20% short-trip case). Left as a
documented cosmetic residual, now with the exact heap shape on record.

**+0x24e7 is the human/privileged-player flag; humans get a 5x path-budget
share (CONFIRMED, was inferred).** The scheduler's budget split (0x4164a0-
0x416517) walks the player table, and for each active slot reads the byte at
`player+0x24e7`: nonzero increments the "with" count (`edi`), zero increments the
"without" count (`[ebp-4]`). It then forms the weighted total
`edi + (without + 4*edi)` = `without*1 + with*5` (0x416500 `lea ecx,[ecx+edi*4]`
then 0x416507 `add edi,ecx`) and divides the frame's search budget by it
(0x416515 `idiv edi`). So a flagged player draws 5 shares to an unflagged
player's 1 -- exactly the 5x our World::setHumanPlayers applies. (The adjacent
byte `player+0x24ef == 0xa` gates a slot out entirely; 0xa = 10 is the same
sentinel the player loop caps at.) The port is byte-faithful here.

**Retail-under-wine pacing: not ground-truthed (environment-limited).** The
cadence ladder (docs above) was reversed by EMULATING 0x414450/0x4e51f3 with
planted states, which fixes the branch logic exactly but not the wall-clock feel
of the geometric re-ask tail. Confirming that against a live retail process would
need KINGDOMS.icd running under wine with the RNG + frame counter instrumented --
outside this repo's harness (which emulates routines, it does not run the game).
Recorded as a known gap, not a discrepancy: nothing observed contradicts the
port; the tail's timing constants are simply unverified against a running binary.
