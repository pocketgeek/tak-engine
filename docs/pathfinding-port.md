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
