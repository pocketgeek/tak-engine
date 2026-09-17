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
- What the divisor argument to `0x416430` is at the call site. Emulation shows
  it simply divides the budget; the value passed in the real game is unknown.
