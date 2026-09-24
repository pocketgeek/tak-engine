# Porting retail's movement layer

## Current status: scoped pathfinding work complete (protocol 179)

The requested scope is retail-compatible surface navigation and valid map
occupancy, including mountain boundaries, ramps, boats, and both standard and
Crusades balance inputs. The implemented navigation layer passes the retail
comparisons below. This is a tested scope statement, not proof of identical
behavior on every possible map or a claim of whole-game retail parity. AI
strategy may differ, as agreed. Terrain-art occlusion and renderer ordering are
separate from whether a unit can occupy a map position.

### Protocol 179: honor nonblocking map features

The shared movement obstacle overlay now blocks map features only when their
shipped definition sets `blocking=1` (while preserving the clear Sacred Stone
centre). Decorative wave art such as Lake Lokken's `TarWave05` is explicitly
`blocking=0`; it no longer closes water-route cells. Before this correction,
Vertrans could be physically placed after unloading while its water navigation
grid still marked the same footprint blocked. The data-backed `navblock` test
checks that Lake Lokken's TarWave05 cells stay out of the shared overlay, and
the real-profile Vertrans/Araarch pickup-and-unload test checks that the carrier
footprint remains passable in its water grid through the post-release coast.

This is an overlay correction based on the retail feature definitions and map
behavior, not a claim of full live-map route parity. Protocol 179 separates the
changed deterministic movement rule from protocol 178's transport simulation.

### Protocol 168: AI, standing orders, and flying construction

Skirmish planning now scales production and army reserves with actual income,
checks whether unit types can use nearby terrain and reach enemy territory, and
limits raiding commitments so a heavy wave can accumulate. Hard and Absurd expand
farther with non-monarch builders. Passive builds defenses and an army, clears
factory exits with local Move orders, and gives its units defensive standing
orders from birth; it emits no attack commands. Naval selection requires usable
water, with reachable coastal bombardment positions allowed as objectives.
These are read-only placement/connectivity queries and ordinary commands; no
path search, surface movement, or occupancy rule was changed for this work.

Unit creation retains the type's default move/fire standing orders in both
balances. Changing stance retires an automatic engagement immediately while
preserving explicit player attacks. The Absurd multiplier now also covers manual
and automatic corpse reclaim, alongside recurring and feature-reclaim income.
New games center the local player's monarch at zoom 1.25 after viewport sizing.

Live flying conjurers now choose nearby hover destinations using the observed
`41ef00` angle/radius perturbations and the existing flight controller. Both
placed and queued production use the active site's position. The render hook
keeps StartBuilding active during these movements; the shipped Zhon script
continues its wing flap and arm gesture. Regression checks cover movement around
the site, working distance, facing, and repeated wing/arm poses in both balances.
This ports the missing hover behavior, not the entire retail construction mission
scheduler or a claim of identical frame-by-frame trajectories.

Cross-faction testing exposed a shared heading-query bug. Retail COB GET 27
returns the raw native heading (`50ceb0`, case at `50d255`); the simulation
returned zero and the renderer returned port-coordinate heading. Aramon's Keep
uses `32768 - GET 27` to counter-rotate its build pad, while the Smithy relies on
its birth orientation. Both hosts now report native heading, and placed builds
retain the existing retail birth-orientation rule instead of forcing pi. No
factory-specific rotation or occupancy exemption was added. The renderer now
uses building birth heading too, including effects, aiming and selection bounds,
instead of its former fixed-facing workaround (`4ee620`/`4ee9a0` apply the
entity pose to the native visual model). Real construction
and output tests cover Keep, Smithy, Taros factory, and Veruna factory in both
balances. Direct emulation checked GET 27 at eight headings including wrap edges.

Validation: full Debug and Release builds; Debug CTest 36/36 and Release 28/28;
10,030 native integer-rotation comparisons; unchanged cross-compiler golden hash
`8adc4762a852fadd`. Two seed-1, five-faction Absurd multiplayer runs on Ulasem
Arena reached tick 1800 with hash `bb8607f55683c3e8` and `err=none`. Ten 900-second
AI scenarios covered all five factions and both balance modes; Passive issued no
attacks, including Aramon and Creon after the factory fixes. ARM cross-compilation
was unavailable because the target headers were missing.


### Protocol 167: placed flying conjurers keep their build distance

The follow-up screenshot was a placed construction order after a restart, not
queued production. That path already turned the monarch toward the site. The
actual approach mismatch was that flying builders used the ground footprint
rectangle and stopped roughly 40 map units away. Thirsha's altitude then lifted
her body far above the nearby target on screen, making her correct map heading
look nearly perpendicular to the screen-space line between the two units.

Flying placed-build orders now approach a hover point at the builder's nominal
FBI `builddistance` from the site centre, facing inward. Ground approach remains
rectangle-based. The point is computed when the order becomes active, including
when the builder starts inside its working distance. For `zonhunt`, this is 100
units. Captured retail flying construction (`41ef00`, unit 1229 in the early and
late saves) works approximately 100 units from its target; its orbit routine
chooses `builddistance - random(8) + random(8)`. This change uses that nominal
working distance; it does **not** add retail's periodic randomized orbit or
claim complete flying-construction parity. No model-axis rotation was changed.

The original eight cardinal/balance distance regressions failed before the
change. `conjure_test` now checks all eight compass directions and two valid
close placements in both balances, including accepted placement, distance,
correct heading, and stopped movement while construction remains active.
The existing queued-production and real-COB arm-animation checks also pass.

A Debug-only rendered reproduction is available on Ulasem Arena:
`TAK_CONJURE_TEST=1 TAK_SHOT_MS=500 build-dbg/takclient game "ulasem arena" --data assets/game --crusades --testbuild --time 6 --shot /tmp/tak-conjure.png`.
It disables edge scrolling and advances the animation with the initial test
simulation so a headless capture includes the settled airborne conjure pose.
The apparent vertical separation due to altitude remains intentional.

Both complete Debug/Release builds pass, with 34 Debug and 27 Release CTests.
The data-backed conjure tests pass in both binaries. GCC/Clang retain golden
`8adc4762a852fadd` (ARM target headers unavailable). Two seed-1 multiplayer
runs agree at tick 1,800 with hash `a6c1f4ca7b0bc550` and `err=none`.

### Protocol 166: queued conjurers face their output

Queued mobile production created its site but never called the facing logic
used by placed construction. A stationary Zhon monarch could retain her previous
heading throughout the conjure animation, appearing approximately a quarter-turn
off target. The production regression reproduces this for both ground and
flying mobile builders (14,325 BAM of error in the displaced-output fixture).

Stationary mobile producers now turn toward the actual production site's
position at their existing pivot rate. This also handles an output position
moved by the exit-placement search. Structures retain their fixed heading, and
an active movement order retains control of a moving producer's facing.
Because this changes simulated headings, the network protocol is 166.

`production_test` checks the formerly missing ground/flying queue-facing path.
`conjure_test` additionally loads the actual `zonhunt` and `zonter` definitions
in both balances, starts at four cardinal headings, and checks correct facing
and unchanged position while the output remains under construction. Its real
COB tests still verify that the conjuring arm gesture runs while active.
Both full builds pass, with 34 Debug and 27 Release CTests. GCC/Clang retain
golden `8adc4762a852fadd` (ARM headers unavailable). Two seed-1 multiplayer
runs agree at tick 1,800 with hash `a6c1f4ca7b0bc550` and no errors.

### Resolved: dense Hunter crowd walking animation

The reported case was approximately 450 Zhon Hunters (`zonter`) on Ulasem Arena
with Crusades balance, walking in place near a shared destination after other
members arrived. The movement controller and the animation input have different
retail contracts: collision refusal retains controller speed for later attempts,
but original COB GET 29 (`50ceb0` -> `4dc100`) returns **zero** when mover flag
`0x04` indicates repeated refusal. It also returns zero for an attached unit.
The client previously exposed retained speed without this refusal check.

The render snapshot now captures repeated ground refusal, reports zero animation
speed while refused/attached, and uses that value for GET 29 and the fallback
walk/MoveRate paths. Successful movement clears refusal and allows animation to
resume. This is shared by ground movers, including boats. Controller speed,
orders, searches and simulation hashes were unchanged by this animation fix,
which retained protocol 165.
The rest of the preexisting animation-percentage normalization is unchanged.

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
whole crowd clears or reproduce the user's exact initial arrangement.

The animation contract is checked separately: `check_movement_animation.py`
compares 120 controlled cases with the original GET 29 in each build, including
first/repeated refusal, attachment and four headings. The real shipped Hunter
`MoveWatcher` script polls GET 29 every 100 ms and gates walking above 5%.
`cobanim_test --hunter-movement <zonter.cob>` verifies that it stops after repeated
refusal and resumes without restarting the script. Both client builds and the
34 Debug / 27 Release CTests pass. No game asset is added to the repository.

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
| Mission integration | Queued/replaced moves, gate traversal, ring/PARK arrival and rectangle/build approaches; protocol 164's 64 PARK cases and protocol 165's 48 rectangle cases pass |
| Deterministic integration | Full Debug/Release builds; 34/27 CTests; 123 Python tests; GCC/Clang golden hash; two seed-1 multiplayer runs agree at tick 1,800 (`a6c1f4ca7b0bc550`) |

The older tick-10071 replay discrepancy is resolved. Probe format 36 omitted
the explored-map masks, viewer and per-unit sight caches. World consequently
assigned permissive unexplored search grades where retail already knew the
terrain, changing the route and later RNG consumption. Format 37 restores
those initial inputs from captured memory; no later movement results are fed
back into World. A regression checks owner masks, viewer, sight caches and
rejection of missing mask memory. The controlled movement checker accepts
formats 34, 36 and 37 without duplicating height or exploration payloads.

With that correction, native tick-body replay and World match for 317 natural
production boundaries (3,718 gameplay and 754 CRT draws) and 314 accelerated
factory-completion boundaries (4,013 gameplay and 627 CRT draws), in both
builds. Comparisons include unit motion, construction, resources, factory queues
and squad state. The importer fix does not change production protocol 165.
The outer live renderer still consumes CRT draws absent from tick-body replay;
whole live-capture RNG/allocation parity is outside this navigation completion.

Reproducible checks live in `tools/re/check_world_grade.py`,
`check_world_search.py --terrain`, `check_world_height.py`,
`check_search_movement.py`, `check_gate_search_movement.py`,
`check_build_reach.py` and `check_factory_completion.py`.
The sections below are chronological implementation evidence; old statements
of remaining work describe their then-current protocol, not this status.

## Protocol 165: rectangular build approaches through placement handoff

`--missions --rectangle` now executes original MobileBuild dispatch and World’s
production `prepareBuildApproach` host through installation, search, movement,
perimeter arrival and the next dispatch’s placement handoff. The comparison
stops at original `405824`, before placement/allocation, rather than substituting
a successful construction result. The initial mission is supplied directly.
The mover’s `MaybeBuilding` script notification and out-of-reach UI notice are
explicit host boundaries; the gate’s actual script, occupancy and policy still
advance and are compared. This proves navigation through that handoff, not full
construction or command-decoder parity.

The initial successful approach already matched movement and ordered grade
queries, but exposed missing mission state. Ground build approaches now retain
the MobileBuild definition flags, own controller identities, receive search
notifications, wait on `0x700`, and consume the triggering events before handing
off. Rectangle controller retirement preserves the same search timestamps as
other ground controllers. The production tick and probe use the same approach
host, so the diagnostic does not duplicate its transition logic.

The blocked-perimeter case found a functional retry bug. Delivery of a partial
route overwrites the stored steering endpoint; using that endpoint as the next
build destination caused `requestPath` to cancel as already in the target cell.
Rectangle retries now obtain the destination from their controller again.
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

Both full builds and CTest suites pass (34 Debug, 27 Release), along with 122
Python tests. GCC/Clang retain golden `8adc4762a852fadd`; ARM target headers
remain unavailable. Two seed-1 multiplayer runs finish tick 1,800 without errors
and with identical hash `a6c1f4ca7b0bc550`.

The older full-scene replay discrepancy described below was subsequently
resolved by restoring exploration inputs; see the current status above.


## Protocol 164: complete ground PARK sequences

`check_gate_search_movement.py --missions --park` constructs a native PARK
mission (kind 33) and the corresponding World order, then advances actual
mission dispatch, ring installation, asynchronous search, movement, arrival,
mission retirement and standby. `--park-target-loss-at` supplies event 8;
`--park-follow-at` appends a point move while PARK is still active. Both are
controlled input events, not AI or command-decoder parity claims. The initial
PARK order is constructed by the fixture; factory creation is separately
covered by the production construction regression.

The ring target exposed missing authored gate inputs: the old gate fixture
needed only its occupancy rectangle, but PARK also reads world position and
the type footprint. Supplying the same position/footprint as World resolves
the apparent initial steering mismatch. No ring-direction rule changed.
The comparison did reveal missing production mission flags: original `4d6c40`
retains `0x200` on a PARK mission with a live target. Factory-created World
PARK orders now retain that flag too. The construction regression asserts it.

All 64 cases pass: four gate targets × two balances × two builds × four
sequences (ordinary completion, loss before initial dispatch, loss at tick 40,
and a queued point move at tick 40). Each completion/loss run lasts 1,200
updates, and queued moves last 1,800: 86,400 exact movement/mission comparisons,
plus ordered grade queries and gate script/occupancy state. Tests require ring
arrival and standby, or activation and arrival of the following point move.
They reject event times after PARK has already completed; linking behind an
already-created standby mission is not equivalent to queueing a move through
the game's command path.

The older natural-production full replay was also retried, then regenerated
with the then-current version-36 importer. It diverged at tick 10071 before
being usable as full construction evidence: ground mover 56 differs in heading,
and the gameplay RNG trace differs. Builder 281's scoped motion matches through
its approach-to-build transition at tick 10095, but this does not certify the
full rectangle mission/controller sequence. The focused protocol-165 check
above subsequently covers navigation through the placement handoff; broader
AI/render parity remains outside this task.

Both full builds pass, with 34 Debug / 27 Release CTests and 122 Python tests.
The strengthened factory-PARK regression passes in both rebuilt test binaries.
GCC/Clang retain determinism golden `8adc4762a852fadd` (ARM target headers are
unavailable). Two seed-1 multiplayer runs reach tick 1,800 with `err=none`
and matching hash `51197539af4bd9d3`.


## Protocol 163: ring and build-area arrival

The completion audit found that rings and construction rectangles still took
an early braking return. Original `4e5150` detaches all three controller shapes:
circle, ring and rectangle. Their satisfaction handlers share the zero-return
virtual query that selects detachment. `tools/re/check_goal_completion.py`
executes those handlers and navigator cleanup, checking both old and recent
search timestamps, retained stored points and notifications `0x100 | 0x400`.

World now applies that cleanup to satisfied ring and rectangle goals and lets
stored waypoints advance before braking. Completed rectangles cannot enqueue
another search while waiting for construction dispatch. The existing build
mission handoff remains responsible for creating the construction site. World
regressions cover all three shapes, old/recent timestamps, endpoint consumption
and suppression of retry requests and RNG draws. Flying construction approaches
retain their separate movement handling.

Validation: all targets rebuilt in Debug and Release; CTest passes 34/34 and
27/27, and Python passes 122 tests. Both builds pass native goal queries and
8,192 PARK cases each, plus the 87-mover 160-update regression in both balances.
Two seed-1 multiplayer runs finish tick 1,800 without errors at identical hash
`51197539af4bd9d3`. GCC/Clang determinism checks retain golden
`8adc4762a852fadd`; ARM checks are skipped because target headers are unavailable.

## Concurrent captured land, hover and boat movers (protocol 162 tooling)

`check_search_movement.py --also-unit ID` accepts multiple independently moving
requesters. All selected units move in allocation order before one shared native
worker update; unselected captured bodies remain stationary. Ordered grade-query
traces are now asserted, alongside every movement/navigator state. The boat
exploration viewer is a single explicit shared fixture input, chosen from the
lowest selected slot's owner, rather than changing with the current requester.

The 32-case matrix includes single movers, same-owner boats, mixed movement
classes, different-player boats and all 87 captured surface movers. Every case
passes in Debug and Release with standard and Crusades data. All 87 movers
also pass 1,200 updates at budget 503 (417,600 unit comparisons), and 500 updates
at the normal 12,000 budget (174,000 comparisons), across both builds/balances.
The combined fixture spans six owners and seven movement classes. This closes
the mixed-player/mixed-boat composition gap noted in the older gate section.
It does not assert that every request receives a route under the small budget,
or reproduce full AI, mission dispatch, combat or exploration-update cadence.


## Changed destinations and concurrent ground requests (protocol 162)

`--replace-goal X Z` extends replacement checks to different destinations.
The six scenarios change goals to either side beyond the gate, or back to the
starting side, at ticks 16, 20, 21, 120, 400 and 650. All 96 gate/balance/build
cases pass for 1,800 ticks each, with exact ordered grade-query traces as well
as movement, missions and gate state. The unchanged-destination timestamp fix
also handles these route changes; no additional simulation change was needed.

`--missions --pair --rounds 2400` now advances two independently moving 4×4
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

The wider search exposed another authored-fixture omission: World installs
retail projected boundary markers during feature setup, while the native map
had left the right and bottom margins empty. The fixture now invokes original
`50eef0` with matching dimensions and scenario inputs before cache initialization.
The previous movement snapshots could agree despite unequal boundary grades;
ordered query assertions caught the difference. A separate-route destination
was also moved north of every gate's wall so it is actually clear for Creon's
larger footprint. These are fixture corrections, not navigation rule changes.

This covers two same-owner, same-class ground requesters, including both
successful movement and congestion-driven goal expansion. Different-player
and mixed movement-class/boat composition was subsequently checked in the
captured multi-requester matrix above.
All 32 pair/congestion gate/balance/build cases pass for 2,400 ticks each
(153,600 per-unit comparisons), alongside 128 single, queued and changed-goal
regression cases using the corrected map boundary. Both probe builds and full
CTest suites pass (34 Debug, 27 Release); Python passes 122 tests. The protocol-162
multiplayer/determinism evidence below still applies: no simulation code changed.
Simulation protocol remains 162; this follow-up changes diagnostic tooling.

## Protocol 162: navigator timing for queued and replacement ground moves

Ground controller detachment now preserves a very recent navigator admission
stamp and otherwise clears it to zero. Removing an already detached mission
keeps the stamp unchanged, including when it crosses the six-tick boundary
between arrival and mission removal.
The previous unconditional `-1` sentinel suppressed the new leg's retry cadence.
An independently evolving queued gate traversal exposed the first difference at
tick 648: native `402dc2` drew the next mission's delay (bound 5), then `4e5482`
drew a retry check (bound 120); World skipped the second draw. Positions still
matched at that boundary, but the RNG difference later changed search timing.

`check_gate_search_movement.py --missions --return-trip --rounds 2400` starts
with two point missions, the second returning to the original side of the gate.
World uses normal queued orders; native initial missions are constructed and
linked as the supplied queue. Original dispatch and removal then run without
later queue or movement-state injection. All four gates, both balances and both
builds pass (16 cases, 38,400 ticks), including ordered grade queries, outbound
and return traversal, final gate closure and standby.

A replacement move also preserves a recent stamp instead of installing `-1`.
Native queue removal at tick 16 retains an admission stamp of 15; re-admission
must wait until tick 30. The previous World reset admitted the replacement too
early. `--replace-at` compares original queue clearing and a new point mission
against normal World order replacement, with the same destination supplied at
an identical tick. Tests cover ticks 15, 16, 20, 21, 120 and 650, exercising both
sides of the six-tick preservation boundary, active movement and arrival.

A separate native retirement probe detaches at tick 20 with stamp 15, then removes
the mission at tick 21: the stamp stays 15. World regression cases cover live
versus already detached controllers with stamps at and inside the expiry boundary.
The combined matrix passes 112 cases: 96 same-destination replacements and
16 queued round trips, with both balances and builds. After the detached-removal
boundary correction, the targeted Aramon/Creon queued and arrival-replacement
matrix passes all 16 cases alongside the regression cases and complete test suites.
Changed-destination replacement and multiple moving requesters still need
composed coverage.

Final all-target builds pass in Debug and Release; CTest passes 34/34 and 27/27,
Python passes 122 tests, and deterministic math remains `8adc4762a852fadd`
(ARM headers unavailable). Both final seed-1 multiplayer runs reach tick 1,800
with `f5f2f31665d46aca` and `err=none`.

## Protocol 161: point-move dispatch through arrival and standby

Plain `World::order` moves now retain the native point-mission flags
`0x3000400`; ground standby initialization retains `0x5012000`. Previously both
started with zero flags. The native constructor (`4d6c40`) initializes definition
flags and removes the target/point-presence bits when those inputs are absent.
This change is scoped to ordinary point moves and ground standby; other mission
constructors are not inferred from these observations.

`check_gate_search_movement.py --missions` now advances original `4d8450` and
World's production ground mission dispatcher before each mover update. World
starts through `World::order`. The native side constructs the same initial
point mission and controller; subsequent dispatch, search, movement, arrival,
move removal and standby evolve independently. Comparisons include position,
velocity/orientation, movement modes/timestamps, search pending state, RNG,
mission kind/stage/wait mask/deadline/pending/flags/radius, unit events and
controller presence, together with complete gate script/animation/occupancy
state. Completion requires crossing, arrival, a closed gate and ground standby.
All 32 gate/balance/build/dispatcher-mode cases pass (38,400 compared ticks),
including exact ordered grade-query traces. This adds ordinary single-move/standby
composition. Later sections above cover queued/replaced orders, multiple
moving requesters and ring/rectangle arrival cleanup. Full ring/build mission
composition remains outside that gate comparison.

The expanded comparison exposed an authored-fixture error: the copied native
player retained its human priority byte despite being changed to an AI player.
At budget 503 this gave native 500 work units versus World's 503. Resetting that
initial priority to ordinary AI makes the inputs equal; no scheduler change was
needed. Diagnostic mode now also compares every ordered grade query, including
its tick, coordinates and value, instead of merely retaining the traces.

Both complete builds succeed; Debug/Release CTest pass 34/34 and 27/27, Python
passes 122 tests, and deterministic math retains `8adc4762a852fadd` (ARM headers
unavailable). Two seed-1 multiplayer runs reach tick 1,800 with
`f5f2f31665d46aca` and `err=none`; the mission flags now contribute to hashed
state.

## Protocol 160: point arrival and composed gate traversal

Ordinary point-goal arrival now detaches its navigation controller while
continuing to advance stored route points during braking. The native sequence
at `4e5150`/`4e5186` emits pending bits `0x100` and `0x400`, cancels the search
worker, and clears the controller without discarding those points. Old admission
stamps clear; stamps newer than tick minus six remain. A detached controller
cannot start another route retry. Previously World returned early at arrival,
freezing the stored point state; the composed gate oracle caught this at tick
659. Park-goal behavior is unchanged. The self-test covers old/recent stamps,
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
boundaries, not a claim of complete retail match simulation. Multiple moving
requesters, full mission-dispatch composition and exploration cadence remain
separate coverage gaps.

All Debug and Release targets build; CTest passes 34/34 and 27/27, and Python
passes 122 tests. Cross-compiler deterministic math retains `8adc4762a852fadd`
(ARM toolchains lack headers). Two seeded multiplayer runs reach tick 1,800
with hash `38dcf6a11f7ca2a4` and `err=none`. The captured surface-movement
regression passes all 174 cases (87 ground/hover/boat units × two balances),
comparing 27,840 independently evolved movement/search updates. The standalone
gate lifecycle also still matches 2,600 native boundaries after fixture reuse.

## Live native gate fixture for composed traversal

`native_gate.py` supplies the live gate used by the moving-requester
comparison. It executes original active-bit changes (`51e4d0`), immediate
notifications, COB threads and integer piece animation, yard transition checks
(`507c70`/`507ae0`), occupancy restamping (`5062d0`) and cache-refresh dispatch.
No movement-class cache is installed in this component fixture yet. Script-name
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
establishes the live gate component needed by the native movement harness; it
does not itself compare a moving requester's traversal against retail; the
protocol-160 composed harness above adds that comparison.

The test-only follow-up rebuilds both configurations and passes 34/34 Debug,
27/27 Release CTests and 122 Python tests. Production remains protocol 159.

## Protocol 159: automatic AI gates and owner-sensitive search

Player state now retains and hashes automatic-gate capability. Normal computer
slots enable it in client/server setup and recorded-match reconstruction;
strategic campaign players enable it in mission setup. Human players default to
manual gates. Retail's hidden human unlock/AutoGates UI is not implemented.

The World gate controller uses the verified `40a020` proximity rule: an allocated
same-owner mobile body within the footprint plus one-cell border opens the
gate when moving, or holds it open when stopped over a gate-passage cell.
No qualifying body requests closure. Commands use `setActive`, so scripts still
control animation, yard opening, occupancy vetoes and eventual closure.
The controller runs once per simulation tick before unit updates. This is an
intentional AI scheduling difference, consistent with this engine's independent
skirmish AI; retail planner scan timing is not claimed.

Search now supplies that same player capability and actual gate-owner probes
to `retailSearchGrade`. Owned passage grade 3 becomes grade 4 only for capable
requesters with square footprints on retries 0/1. Enemy gates, manual capability,
solid frames, rectangular footprints and later retries retain native behavior.
`check_gate_owner.py` compares 10,800 production World results with original
`4139d0`, including original eligibility and map probing. Visibility and the
initial cached grade are fixture inputs. Both Debug and Release pass.

`check_gate_activation.py` now compares the production World controller with
all 3,136 original proximity decisions, rather than only observing the binary.
Both builds pass. The authored fixture marks unallocated bodies dead, matching
World's allocation/liveness representation rather than merely setting zero HP.
All eight shipped gate/balance combinations pass manual activation, occupancy
veto/clearance, automatic approach, stopped-passage holding and automatic closure.
The asset-backed test now also runs full `World::tick` traversal in both
directions for all eight combinations (16 crossings). Hard map blockers extend
from the gate to both map edges, making the passage the only crossing. Each leg
must complete a fresh route search before entering a passage, enter only while
the yard is open, arrive on the opposite side, and leave the gate closed. The
test uses normal order dispatch, asynchronous search (budget 503), movement,
automatic gate control and the shipped script; it does not inject positions
after each leg starts. Debug and Release pass. CMake registers this as
`gate_scripts` whenever `TAK_TEST_DATA` is supplied. A first open-map fixture
was rejected because the mover correctly went around the gate, proving no
passage traversal. This end-to-end check establishes World behavior; the
protocol-160 harness above adds native tick-by-tick composed traversal.

The test-only traversal follow-up passes the full 34-test Debug and 27-test
Release suites; the final completed-search assertion also passes the targeted
Debug data-backed test and the Release gate runner. Production remains protocol
159, so the multiplayer evidence below still applies.

All targets rebuild in Debug and Release. CTest passes 33/33 and 27/27;
Python passes 122 tests. The deterministic-math cross-compiler check retains
`8adc4762a852fadd` (ARM headers unavailable). Boat 3014 still matches 160
composed updates in each balance, including seven route deliveries.
Both seed-1 multiplayer runs reach tick 1,800 with hash `38dcf6a11f7ca2a4`
and `err=none`; the hash changes because gate capability now enters player state.

## Protocol 158: gate activation drives the script

`World::setActive` now immediately notifies a gate's `Activate`/`Deactivate`
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
asset-backed checks. This fixes manual activation; protocol 159 subsequently
connects automatic eligibility and the owner-sensitive path-grade adapter.

All targets rebuild in both configurations; CTest passes 33/33 Debug and 27/27
Release, and Python passes 122 tests. Cross-compiler determinism retains
`8adc4762a852fadd` (ARM headers unavailable). Both seed-1 multiplayer runs
reach tick 1,800 with hash `85a79a3af3d81a79` and `err=none`.

## Protocol 157: refresh search caches after yard transitions

An accepted COB yard-state write now refreshes every existing movement-class
search plane over the yard and its clearance border. Previously live placement
observed the open yard, but cached route search could retain its closed grade.
The refresh also runs for accepted writes of the existing state, matching
`507c70`; rejected transitions leave the cache unchanged.

`check_yard_transition.py` executes original transition and occupancy validation
for 192 combinations of prior state, requested value, yard bits and occupant.
It observes the ordered restamp/refresh callbacks and validates their arguments.
CTest exercises the production script host and cache through closed/open,
occupied refusal, successful closure, and an accepted same-state assignment.
Debug and Release rebuild all targets and pass 33/33 and 27/27 CTests; both
pass the 1,800-case raw gate oracle. Python passes 122 tests. Cross-compiler
determinism remains `8adc4762a852fadd` (ARM target headers unavailable).
Both seed-1 multiplayer runs reach tick 1,800 with hash `85a79a3af3d81a79`
and `err=none`.

At this stage, gate activation was a separate prerequisite: `setActive` changed
the unit flag while activation callbacks were only sent for factory production.
Protocol 158 connects the gate command to the native immediate-notification
behavior. Automatic gate eligibility/owner adaptation is still outstanding.

## Protocol 156: raw gate passage grades

The type loader now retains the source FBI `gate` low bit. Raw search grading
recognizes closed `c`/`C` passage cells on those types and returns special grade
3 instead of ordinary stationary-body grade 0. Solid frames stay blocked, open
passages use terrain grades, and forbidden terrain still blocks a passage.
`check_gate_grade.py` compares 1,800 authored rectangle queries against original
`5088f0`, with body allocation and stamped yard cells as controlled inputs.
CTest includes representative frame, passage, open-yard and water-depth cases.
All targets rebuild in Debug and Release, and both builds pass all 1,800 native
rectangle comparisons. The authored fixture explicitly uses a stationary type,
matching the native body without a mover. This does not yet implement the gate
capability/owner adapter or automatic opening/closing.

CTest passes 33/33 Debug and 27/27 Release, and Python passes 122 tests.
Cross-compiler determinism retains `8adc4762a852fadd`; ARM is skipped because
target headers are unavailable. Boat 3014 also matches 160 composed updates
under each balance, including seven route deliveries and 141 moving updates
after its first delivery.
Both seed-1 multiplayer runs reach tick 1,800 with hash `85a79a3af3d81a79`
and `err=none`.

## Protocol 155: separate inactive routes from consumed endpoints

Native `4e51de` selects an immediate retry when fewer than two points remain;
it does not test the navigator's active bit. An empty delivery can disable a
two-point route without consuming its endpoint. World previously used
`navigationExhausted` for both facts and immediately re-requested this route.

Orders now retain `navigationConsumed` separately, and hash it as simulation
state. Replacing a route/direct segment clears consumption; consuming the last
endpoint sets it; empty delivery preserves it. Point advancement also applies
to stored inactive routes, matching `4e5189`. The retry ladder uses consumption
or repeated refusal for its immediate branch. The diagnostic reports this
production state directly instead of maintaining a separate consumption flag.
The retry oracle now exercises active/inactive navigators with one/two points;
new CTest cases distinguish inactive stored routes, consumed routes and repeated
refusal. New requests also clear all four previous route-outcome bits, as
original `4e4f50` does; an already-pending request retains them. The retry oracle
now executes that original submission routine and compares the resulting flags,
and the composed comparison includes all four outcome bits.

All targets rebuild in Debug and Release. Both builds pass the expanded
8,192-case native retry comparison, including outcome flags. Crusades unit
2805 now matches all 160 composed updates and all 529 ordered grade queries:
85 moving ticks, 38 moving while pending, five deliveries and 79 moving ticks
after the first delivery. Standard Release also passes all 160 updates.
CTest passes 33/33 Debug and 27/27 Release, Python passes 122 tests,
and cross-compiler determinism retains `8adc4762a852fadd` (ARM skipped for
unavailable target headers). Both seed-1 multiplayer runs reach tick 1,800
with hash `85a79a3af3d81a79` and `err=none`.

The stricter composed matrix passes all 174 cases: 87 surface units under
both balances, 160 updates each (27,840 state comparisons), including all four
route-outcome bits. Crusades totals: 12,660 moving ticks, 3,489 moving while
pending, 161 deliveries and 10,407 moving ticks after first delivery. Standard
totals: 12,659, 3,542, 161 and 10,353 respectively. Each balance has 2,137
refusal ticks. These tests still control exploration and omit mission dispatch,
scripts, formation and other moving requesters; they do not prove those parts.

## Gate-specific native observations

The late captured scene contains no unit whose type has `+0x264:0x40000000`.
Passing its surface-movement matrix therefore does not cover special gate
grades. The native type loader at `4c0afb..4c0b20` reads the source FBI `gate`
property (low bit) into that flag. Shipped `arangate`, `tarngate`, `verngate`
and `cregate` definitions provide concrete gate fixtures; their `c` yard cells
must be distinguished from the solid frame. Protocol 156 retains this property
and distinguishes these passages in raw search grades.

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
or the AI scan cadence, and do not yet prove World integration.

The yard parser maps `c` to `0x2d` and `C` to `0x35`. Placement at
`506416..506448` tags a cell with map byte `+0xd:0x20` only when the type is a
gate and `(yardByte & 6)==4`, covering both forms of gate passage and excluding
the solid `o` frame. Sixteen direct original `5088f0` comparisons across gate
flag, passage flag, hard terrain and road inputs confirm that only a gate
passage on passable terrain produces grade 3; solid frames and hard terrain
remain grade 0. Protocol 156 adds this branch to World's raw search adapter.

The older general grade oracle substitutes capability and verifies owner/probe
ordering. Protocol 159 adds World integration comparisons with native capability
and map probing, plus shipped-script opening/closing tests. Composed traversal
through an animated gate remains unverified.

## Protocol 154: include all occupied surface bodies in cache aging

`prepareSearchGrade` no longer excludes unfinished units or landed aircraft.
It uses the same grounded-aircraft condition as live occupancy. In the captured
comparison, unfinished unit 1410 occupies the footprint affecting `(426,95)`;
unfinished landed aircraft 1548 affects `(216,359)`. Retail ages both interiors
to blocked grade 0, while the exclusions left World with clearance grade 4.
Correcting only the first exclusion exposed the second. CTest now checks aging
for completed/unfinished surface units, completed/unfinished landed aircraft,
and exclusion of airborne aircraft. Unit 625 (Crusades Debug), unit 590
(standard Release) and unfinished unit 1410 (Crusades Debug) now match all 160
composed states. Their ordered grade traces match exactly: 46,161, 19,263 and
509 queries respectively. Unit 625 moves on all 160 ticks, including 156 while
search is pending, receives two deliveries and moves on 91 ticks after its
first delivery. This includes actual route-following and refusal behavior.

All targets rebuild in Debug and Release. CTest passes 33/33 and 27/27; Python
passes 122 tests. Cross-compiler determinism retains `8adc4762a852fadd` (ARM
skipped for unavailable target headers). Both fresh seed-1 multiplayer runs
reach tick 1,800 with hash `e08af67ac41a8e19` and `err=none`.
The full 87-subject/two-balance comparison is in progress.
That matrix has exposed another mismatch in Crusades unit 585 at update 138:
physical state still agrees, but retail delivers a new route while World keeps
the previous route and search pending. Its second search used different initial
weights (retail 20 versus World 1.5): the harness reduced retail's pool bounds
to two slots without changing its captured scan limit. Different post-completion
cursor positions then changed the next wrap timestamp. The harness now retains
the captured pool bounds on both sides and verifies the native scan limit agrees.
With those corrected inputs, Crusades unit 585 passes all 160 states and all
1,382 ordered grade queries, including two route deliveries and 134 moving
ticks after the first delivery. Both diagnostic binaries are rebuilt; the full
matrix has been restarted with consistent pool/scan inputs. Earlier composed
counts above describe the old controlled pool setup and must not be represented
as full captured-pool validation.
The corrected Crusades sweep passes 86/87 subjects. Unit 2805 first differs at
update 123 only in the pending-search bit; physical state, RNG, mission flags
and stored route agree. Both workers complete an empty route at tick 37569;
World requests again during the next movement update while native remains
inactive. The 529 native grade queries form an identical prefix of World's
725 queries. Investigate inactive-route handling in the movement retry ladder,
which currently treats all `navigationExhausted` orders as immediate re-asks.
Its transition is under investigation. The standard
sweep is still running. Eight supplemental cases (units 625, 580, 3014 and 3538,
both balances, destination offset -256/-512) all pass 160 updates.

The composed diagnostic separately tracks endpoint consumption during movement.
An empty search delivery disables navigation without consuming the stored
points. Boats 3251 and 3724 now pass 160 comparisons with that reporting fix.
Reports include movement, movement while pending, actual native delivery count,
movement after delivery, and refusal ticks; stationary cases no longer imply
coverage of route-following motion.

## Protocol 153: preserve mission precision when submitting searches

World's search submission converted fixed-point mission destinations to float
and back. A captured WATER3 boat's destination Z was `342884351`, one raw step
below a footprint boundary; float rounding moved it into the next cell. The
initial controller distance was consequently 579 instead of retail's 572, and
World delivered its partial route one update late despite 2,870 preceding grade
queries matching. Submission now uses the original fixed-point mission target
for both the goal cell and stored destination.

The formerly failing boat 3014 now matches all 160 composed movement/search
updates in Crusades Debug and standard Release. CTest adds 18 land/water cases
across 2/3/5-cell footprints and raw offsets -1/0/+1 at boundaries, checking
the delivered destination and endpoint. Full Debug and Release builds pass;
CTest passes 33/33 Debug and 27/27 Release, the Python suite passes 122 tests,
and the cross-compiler determinism check retains golden `8adc4762a852fadd`
(ARM skipped for unavailable target headers). Both seed-1 multiplayer runs
reach tick 1,800 with hash `e08af67ac41a8e19` and `err=none`.

The 13-subject, two-balance composed matrix reports 22/26 passes. The four
remaining reports are boats 3251/3724 in both balances: an empty delivery
disables navigation while retaining the two stored points, but the diagnostic
currently reports every inactive route as one consumed endpoint. Correct that
diagnostic distinction before treating these as simulation failures. The
expanded 87-subject matrix is in progress; Crusades land unit 625 has a real
delivery-timing mismatch. Its first differing grade query is origin `(426,95)`,
native 0 versus World 4, also seen in standard unit 590's internal trace.
The cell falls inside unit 1410's aging footprint: native origin `(429,97)`,
footprint 2x2, grade timestamp 35663 and flags `0xe9290921`. World restores
the same coordinates/timestamp but skips it in `prepareSearchGrade` because
`underConstruction` is true. The native preparation includes this body. This
exclusion is corrected by protocol 154, together with landed-aircraft aging.

## Protocol 152 follow-up: composed movement/search comparison (in progress)

`tools/re/check_search_movement.py` now compares a captured surface mover with
the original movement, height, request admission, asynchronous worker and route
delivery routines running together. World runs its production adapters in the
same movement-before-worker order. Both sides evolve their own positions,
occupancy and routes; later native outputs are not injected into World. Other
units remain stationary, exploration is fixed, formation is disabled and the
mission dispatcher/scripts/combat do not advance. This is not full-game parity.

The fixture selects initial grade caches by the chosen balance's movement
class. Reusing a capture's representative IDs without rebinding was incorrect:
unit 590 changes from GROUND5 to GROUND4 in standard balance. With the correct
initial cache, its 160 reported movement/search states match in Debug and
Release. Ordered internal grade traces still differ and require investigation.
The diagnostic also now represents a consumed route as its one remaining
endpoint; boat 2530 passes 160 updates in standard Debug and Crusades Release.

A substantive boat mismatch was found: unit 3014 delivered a partial route at
update 11 in retail while World continued searching. It reproduced with the
corrected cache setup; protocol 153 fixes its destination rounding. Earlier
diagnostics found 2,870 identical ordered grade queries before the searches
diverged. Do not interpret the passing isolated
or composed cases as complete pathfinding parity. Multiple moving requesters,
full mission dispatch, exploration production and special blockers still need
composed coverage. These harness changes do not change the network protocol.

## Protocol 152: conditional route retention on controller reset

Ground/water mission resets now follow original `4e54e0` when an existing route
has multiple points. Retail retains and activates routes with at least three
points only when twice the endpoint-to-goal distance is strictly less than the
unit-to-goal distance, using its signed wrapped fixed-point arithmetic and
truncated double-precision lengths. An exact half-distance tie does not retain
the route. The previous World callback unconditionally kept multi-point routes.

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
execution, physical movement, absent-goal setup and full dispatcher parity remain
outside this oracle.

Validation: all targets rebuild in Debug and Release. Full CTest passes 33/33
and 27/27; the subsequently added route-reset regressions also pass in both
builds. Both builds retain the 320-case destination comparison, and the Python
suite passes 122 tests. GCC/Clang O0/O2/O3 agree on golden `8adc4762a852fadd`;
ARM checks remain skipped for missing target headers.
Both seed-1 multiplayer runs reach tick 1,800 with hash `e08af67ac41a8e19`
and `err=none`.

## Protocol 151: align direct navigator endpoints

Extending the destination comparison to the complete initial two-point segment
exposed another integration mismatch. World retained the fractional mission
point as its steering endpoint. Retail retains that point in the mission but
`4e2500`/`4e2820` convert it to a footprint origin and back to an aligned endpoint
for the navigator. For a 1x1 click just below `(256,320)`, retail's endpoint is
`(248,312)`; World's endpoint was still the fractional click.

Direct ground/water segments now retain the original point in `missionTarget`
and align their endpoint independently by footprint width and depth. Controller
resets use the same construction, so a previous partial-route corner cannot
replace the requested point. The segment's start remains the unit's integer
world position. Queued orders retain their raw point until activated.

`check_destination_setup.py --binary` now checks mission coordinates, goal
origins, segment anchors and endpoints in 320 World comparisons per build.
Half exercise ordinary installation and half replace an existing endpoint before
resetting the controller. Native reset runs original `4d4da0` a second time,
including clearing the old controller and installing its replacement; the World
side invokes its actual ground-mission dispatcher for the reset. Native scheduler
submission/cancellation remain boundary substitutes. This check does not run the
search worker or physical movement, and does not certify longer-route retention.

Validation: all targets rebuild in Debug and Release, and both 320-case World
comparisons pass. The complete CTest runs exposed one return-mission assertion
that equated the mission point and navigator endpoint; it now checks the retained
`(256,256)` mission and aligned `(264,264)` 1x1 endpoint separately, preserving
its parent-state and radius assertions. That test passes on rerun in both builds;
all other tests passed in the full runs (33 Debug / 27 Release total). The Python
suite passes 122 tests. GCC/Clang O0/O2/O3 agree on golden `8adc4762a852fadd`;
ARM checks remain skipped for missing target headers.
Both seed-1 multiplayer runs reach tick 1,800 with hash `e08af67ac41a8e19`
and `err=none`.

Remaining reset boundary: a native three-point route from `(104,104)` to old
endpoint `(168,104)`, with new goal `(424,104)`, becomes the direct two-point
route. With old endpoint `(328,104)`, the same setup retains all three points.
This follows the endpoint-distance branch at `4e5563..4e55e9`. World's reset
callback currently skips direct-segment reconstruction whenever the active leg
contains multiple segments; protocol 152 above subsequently compares and corrects
that unconditional retention. This is separate from the verified two-point cases.

## Protocol 150: preserve blocked move destinations

`World::order` no longer replaces a blocked destination with a nearby walkable
cell. The old outward scan used a square maximum footprint and picked the first
fit in its scan order, changing the mission and search goal before retail's
search/radius logic could run. In the executable comparison, a 1x1 land click
just below `(256,320)` was changed to `(184,248)` by World; retail retained the
original point. The fix applies to active and queued ground/water orders.

The destination oracle now runs original ordinary-move validation `4de530`
(command 2), name lookup `4d4bf0`, submission `4d78a0`, constructor `4d6c40`, and
queue installation `4d77f0`, followed by the existing controller/navigator chain.
It uses a controlled sorted definition table resolving Move_Ground to kind 28,
zero definition flags, an enabled player, and no target entity or region.
Clear and blocked-region cases cover eight footprints, boats/land, and fractional
coordinates on either side of independent-axis quantization boundaries.

The `--binary` option compares 160 native command cases with actual World orders,
checking retained mission coordinates and footprint origins. It reproduced the
old relocation before the fix. The separate native-only checks retain coverage
of 128 supplied-goal navigator setups and 160 direct constructor/controller
installations. CTest additionally checks active/queued blocked goals for four
footprints on land and water. Screen picking, packet decoding, physical movement,
and complete mission/search integration are not certified by this boundary check;
the World comparison does not yet assert initial navigator endpoint quantization.

Validation: all targets rebuilt in Debug and Release. Both destination comparisons
pass; CTest passes 33/33 Debug and 27/27 Release, and the Python suite passes 122
tests. GCC/Clang O0/O2/O3 agree on golden `8adc4762a852fadd`; ARM checks remain
skipped for missing target headers.
Both seed-1 multiplayer runs reach tick 1,800 with hash `e08af67ac41a8e19`
and `err=none`.

## Protocol 149: terrain-cache integration and current heading at cost handoff

The composed oracle's `--terrain` mode now builds cached grades from height
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

This exposed a runtime mismatch when a unit turns between search initialization
and phase-2 seeding. Original `415cb0` reads its current heading then; the port
retained the initialization heading. The cost handoff now refreshes only the
heading, retaining that attempt's start and cost profile. The previous failure
was a moving 6x2 land unit at tick 18: cache contents and grade answers matched,
but the cost search began examining neighbors in a different order. The native
comparison now passes, and CTest checks that handoff samples the changed heading
once across different suspension budgets.

Both builds pass 1,440 terrain searches (720 land, 720 boat), covering 86,948
ticks: full exploration 34,879, hidden 20,973, alternating exploration 31,096.
The original executable performs 428 live placement queries within these runs.
The earlier 80 worker lifecycles also pass (3,010 ticks). Emulator setup now
keeps exploration separate from map records and uses the correct cdecl cleanup
for `atexit`; these corrections affect diagnostics only.

All targets rebuilt in Debug and Release. CTest passes 33/33 and 27/27;
the Python harness suite passes 122 tests. GCC/Clang O0/O2/O3 agree on golden
`8adc4762a852fadd`; ARM checks remain skipped for missing target headers.
The earlier controlled-grade corpus also passes 1,920 searches / 89,996 ticks.
Both seed-1 headless multiplayer runs reach tick 1,800 with hash
`e08af67ac41a8e19` and `err=none`.

Remaining composed boundaries include physical movement and mission dispatch,
multiple simultaneous requesters, feature/special-yard occupancy, and exploration
production. Positions, headings and movement flags are still authored inputs;
the fixture uses synthetic ground/water profiles rather than source rosters.
Legacy click-goal snapping was still unverified at this stage; protocol 150
resolves the ordinary world-coordinate command boundary.

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

These initial checks established the constructor/controller/navigator boundary.
The subsequent protocol-150 comparison above exercises ordinary move validation
in `4de530` (command 2, branch `4df0b9`) and generic submission in `4d78a0`, closing
the gap at the world-coordinate command boundary used by `World::order`.

## Protocol 148 follow-up: exploration and admission in composed searches

The World search comparison now optionally leaves original `4139d0` and
`413c80` running. Both sides receive the same authored cached grades and coarse
exploration words; World calls its actual `searchGrade` adapter. Cases cover
fully explored, fully hidden, and alternating player-0/player-1 explored
regions, plus the previous raw-grade control. They exercise unknown-terrain
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
five grade layouts. Full CTest passes 33/33 Debug and 27/27 Release; the final
probe-only extension also passes both `retail_trace` tests. All 122 Python
tests pass. All targets were rebuilt in both configurations. GCC/Clang
O0/O2/O3 retain `8adc4762a852fadd` (ARM headers unavailable); two seed-1,
1,800-tick multiplayer runs retain `e08af67ac41a8e19`, with no errors.

Grid preparation still receives authored cached grades. Live grade-2 refresh,
special grade-3 yards, the exploration producer, physical movement and mission
dispatch remain outside this composed comparison. No new runtime behavior or
network protocol is introduced by this follow-up.

## Protocol 148: rectangular search coordinates and composed delivery

`check_world_search.py` now compares the actual World request, PathService
scheduler/search and World delivery against original `416430`, its original
search/reconstruction, and original `4e4ea0`. It compares every tick's ordered
grade-query digest, pending status, mission events, navigator activation,
outcome flags and waypoint list. Requesters stay stationary; grades, grid
preparation, controller lookup/admission and the mission event sink are
controlled. Movement, mission dispatch and live terrain preparation do not run
in this comparison. The exact-cell goal is independently supplied on both sides.

The first mismatch was a 2x3 mover receiving X waypoints 8px too far east.
World now converts request starts/goals, refreshed starts, goal acceptance and
delivered points with each axis's footprint. PathService carries separate X/Z
center-to-origin offsets through initialization, queries and reconstruction.
Retail `4146a5` and `4146b5` use the corresponding footprint axis when producing
world waypoints. Cost initialization also uses footprint X, as observed at
`4152ec`, `415396` and `415956`, rather than the larger footprint dimension.
Rectangular perimeter regression cases now include unequal axis offsets.

A boxed-in start exposed a second mismatch: World set the navigator's partial
flag from the mission failure notification. PathService now delivers only the
actual route flag, while retaining failure statistics and the `0x2000` mission
notification. Retail's empty result can notify failure, emit delivery event
`0x200`, disable navigation, and still leave the partial-route flag clear.
This distinction is covered by a PathService regression as well as the native
comparison. The prior delivery-only fixture now supplies per-axis waypoint
coordinates, rather than the old square conversion.

Both builds pass 240 composed searches (120 land, 120 boat), totaling 9,843
ticks each: eight square/rectangular footprints, three scheduler budgets, and
five grade layouts covering direct travel, detours, a disconnected destination,
mixed grades and a boxed-in start. The 2,048 delivery-only cases also pass with
the corrected axis conversion. These are synthetic movement profiles, not
additional standard/Crusades source-roster comparisons.

All targets rebuilt in Debug and Release; CTest passes 33/33 and 27/27, and all
122 Python tests pass. GCC/Clang O0/O2/O3 agree on `8adc4762a852fadd`; ARM
headers remain unavailable. Two seed-1, 1,800-tick multiplayer runs agree on
`e08af67ac41a8e19` with no errors.

Remaining boundaries include moving-requester admission/refresh, live terrain
and exploration preparation composed with the worker, subsequent movement and
mission dispatch, shoreline orders, and special yards. The earlier square
approximation in legacy goal snapping/other navigation helpers has not been
validated or removed by this change.

## Protocol 147: navigator route delivery

The production path-result callback is now `World::deliverSearchRoute`, called
at the same end-of-tick boundary as before. Original `4e4ea0` shows that an
empty result clears navigator activity and emits event `0x200` when its
controller does not accept the unit's current position. World previously
retained an active old route and emitted no event. It now marks navigation
exhausted and notifies an unsatisfied ground movement mission, preserving the
owning order and queue until the dispatcher handles that event. Accepted goals
do not receive a failure event. The same circle/ring acceptance helper is used
by normal movement and this empty-delivery branch.

New `check_route_delivery.py` compares 2,048 independently authored deliveries
against original `4e4ea0` and its pending-request removal. It varies active and
inactive old routes, goal acceptance, outcome flags, footprints, and route
sizes 0, 2, 3, 63, 64, 65 and 128. World matches activation, mission events,
scan invalidation, unchanged admission stamp, outcome flags, and retained
waypoints in both builds. The pre-fix comparison fails with World active/no
event versus retail inactive/event 512. CTest also checks queue preservation
and subsequent route reactivation for accepted and unaccepted goals.

This checks the delivery adapter with supplied routes, not the full search
producing those routes. Native goal acceptance and the mission notification
sink are controlled inputs; mission dispatch does not run. Request removal is
observed on the native side but World queue removal remains in PathService,
outside this direct callback test. Other mission-controller kinds, shoreline
orders, rectangular search-coordinate conversion, and composed worker-to-mover
execution remain to be verified. The retail search emits either no points or
at least two points; one-point delivery is outside this corpus.

All targets rebuilt in Debug and Release; CTest passes 33/33 and 27/27, and all
122 Python tests pass. GCC/Clang O0/O2/O3 retain `8adc4762a852fadd`; ARM headers
remain unavailable. Two seed-1, 1,800-tick multiplayer runs agree on
`e08af67ac41a8e19` with no errors.

## Protocol 146: search grades consume owner exploration

`World::searchGrade` now calls the verified `retailSearchVisible` adapter with
the simulation-owned exploration words and the requesting unit's player.
It previously supplied an always-visible callback. Both land and boat searches
now apply retail's grade-5 unknown-terrain rule, including the cross-shaped
exemption around the start footprint. This uses the same shared-team explored
state as boat movement, without reading renderer visibility or the captured
observer override.

`check_search_grade.py` adds 4,096 cases through the actual World adapter,
compared against original `4139d0` calling original `413c80`. Cases vary
rectangular footprints, start/query coordinates, supported owners 0–7, cached
grades, retries, and empty/full/owner/other-player/random exploration masks.
These compose visibility and cached-grade dispatch; live grade-2 refresh and
special-yard probes remain covered only by the existing isolated dispatcher
oracle. This does not establish complete route delivery or mission parity.
The existing 6,000 dispatch, 3,000 visibility, 3,000 occupancy aging and 3,000
preparation cases also pass in both builds. Four World regressions run in CTest.

Two synthetic blocked-goal fixtures now explicitly use long sight and upright
terrain height so their obstacles are known. Their prior synthetic types had
no support model and retained height zero below the terrain; simply increasing
their sight distance did not reveal the wall. Retry spacing, drift limits and
early failure notification assertions remain unchanged.

All targets rebuilt in Debug and Release. CTest passes 33/33 Debug and 27/27
Release; all 122
Python tests pass. GCC/Clang O0/O2/O3 retain golden `8adc4762a852fadd` (ARM
headers unavailable). Two seed-1, 1,800-tick multiplayer runs agree on
`e08af67ac41a8e19`, with no errors. Full worker-to-navigator delivery, shoreline
orders, special yards and mission completion still need composed comparison.

## Protocol 145: boat scans and composed navigator comparisons

World now runs the verified forward/neighbor scan for boats, including speed
modes and scan intervals, and applies boat-specific corner pruning. Placement
queries use each axis's actual footprint size. Live navigation reads the
owner's simulation-owned, shared-team exploration; it never reads local UI
visibility. This represents authoritative owner-view movement in lockstep.
Retail's observer-specific remote prediction context remains a separate
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
those counts are not equivalent to admitted/delivered worker jobs. This closes
the earlier boat-scan mismatch, not full search-to-destination parity.

The captured plane contains 57,600 fully explored words (`0xffff`), so passing
its owner-view variant alone does not establish partial-exploration behavior.
The harness also supports explicit `hidden` and `player-stripes` initial maps.
With player-stripes and `--owner-view`, both rosters pass another 13,920 updates.
These runs observe 794 speed-mode-1 and 646 speed-mode-2 updates each; their
WATER4 motion differs from the fully explored run and still matches native.
Standard consumes 149 points and observes 2,904 exhausted routes; Crusades
consumes 150 and observes 2,899. These are authored visibility inputs supplied
identically to both implementations, not reconstructed capture history.

Live regressions cover stronger shoreline slowdown, moderate unexplored-water
slowdown, their scan intervals, and clearing slowdown after an airborne allied
spotter reveals the route. The owner differs from the local viewer in this
fixture. The spotter must actually be airborne; a landed aircraft correctly
remains a forward footprint obstacle.

World also now selects the boat cost profile instead of always passing the
land flag. The same `floater && minWaterDepth > 0` gate travels with the cost
profile into initial heuristic weight selection at each search attempt.
`check_cost_search.py` verifies 240 profiles through the production World
adapter as well as the standalone calculator, varying floater, minimum depth,
and maximum depth independently. Both builds pass, along with the 16 kernel
fixtures (5,446 pops) and 28 slice fixtures (1,401 boundaries). The separate
native weight oracle passes 1,152 age/load/retry/wrap combinations per build.
Full boat worker admission, route delivery, mission completion and shoreline
orders still require composed comparison.

Validation: all targets rebuilt in both configurations; CTest 33/33 Debug and
27/27 Release, all 122 Python tests, and GCC/Clang golden `8adc4762a852fadd`
pass (ARM toolchain still lacks target headers). Two seed-1 Valysia City
multiplayer runs reach tick 1800 with `2c3759baeb4e050c`, `err=none`.

## Protocol 144: simulation-owned exploration producer

World now maintains a persistent exploration word plane and a cached sight
footprint per unit. The producer runs before the first movement tick and after
each completed simulation tick, including on headless referees. Team members
receive the same revealed cells, independently of the local renderer's viewer
or asynchronous fog pass. Exploration survives unit loss and is cleared on
terrain/replay reset. Both the plane and cached footprints enter `stateHash`.
This supplies the state needed by navigation; **boat scanning is not yet
enabled in World**, and search queries still use their documented omniscient
visibility callback.

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
  `sightheight` field. TypeRegistry now obtains this input from source 3DOs.

`check_exploration.py` compares 1,024 terrain planes, 8,192 footprint changes,
8,192 complete sight updates, and 2,048 model trees against native execution.
All 19,456 cases pass in Debug and Release. Terrain execution bypasses the
unrelated sector-grid prefix after establishing the original stack frame;
the tested height-construction region executes unchanged. Footprint and
sight tests check ordered cell changes and retained cache state, including
zero sight height, negative coordinates, sea clamping, update thresholds,
removal, and disabled exploration. World regressions separately prove
viewer-independent hashes, ally masks, persistent reveal, and replay reset.

This is not yet a captured full-producer replay. Retail calls its sight batch
after a frame's simulation batch (52654e), whereas World schedules a fixed
phase each tick for lockstep. The captured initial exploration plane and cached
sight state still need restoration in the composed navigator harness. The
selected-human visibility bit used by retail's boat query, and source position
inputs for all sight-producing unit kinds, still need integration validation.
These boundaries remain explicit rather than treating the local display fog
or a freshly empty plane as captured exploration history.

All targets rebuild in Debug/Release; CTest passes 33/33 and 27/27 and all 122
Python tests pass. The cross-compiler golden now includes exploration's
binary32 slope and admitted cells: GCC/Clang agree on `8adc4762a852fadd`
(ARM legs remain unavailable because target headers are missing).
Two seed-1 Valysia City multiplayer runs reach tick 1800 with
`0eac71da492fb700`, `err=none`.

## Protocol 143 follow-up: verified boat scan rules

`retailGroundScan` now ports 4dba80..4dbd19 with placement/visibility callbacks.
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
distance gate. World uses the shared scan helper for its land/hover scans;
**boat scans remain disabled in World pending explored-map integration**.
Supplying an always-visible callback would not reproduce retail.

`check_ground_scan.py` runs the complete original scanner with coincident
navigator points and independently supplied grades/visibility. It compares
both resulting modes, deadline, every placement query (including raw XYZ),
and ordered visibility cell reads. All 10,000 cases pass in Debug and Release,
including the 160px boundary, grade 4 versus below 4, hidden/out-of-bounds
cells, fractional headings/positions, negative sight limits and tick overflow.
The extended `check_ground_corners.py` passes 8,192 land/boat decisions in both
builds with all three produced speed modes and turn-rate/distance boundaries.
These are controlled-input comparisons, not complete World boat navigation.

Further visibility tracing identifies +0x19ef4 as the persistent `Mapping`
save plane (5110d0 loads it). Native 4c6800 increments/decrements the owner's
current-view byte counts but only ORs exploration bits into this word plane
(4c6a08..4c6a1f); removing sight does not erase exploration. Its inputs include
the cached unit sight center, sight distance, sight height, and the two-byte
coarse terrain-height plane at game +0x19f08. The owner slot comes from the
sight object's player +0xeb. The selected-human bit used by the boat query
still requires a multiplayer ownership audit.

Next producer boundaries: 4c6c00 copies unit XYZ, raises sight Y to at least
sea+1, and calls 4c6a70. That routine updates the sight footprint when a 32px
cell changes or sight height differs by more than 5, removing old counts and
adding the new footprint. 4c6800 tests a radius and a terrain-height-dependent
distance bound, with the closest cells admitted separately. Neither this
producer nor its coarse terrain input is ported yet; local renderer fog is
not a substitute.

Validation after this refactor: all Debug/Release targets rebuilt; CTest
33/33 and 27/27, all 122 Python tests, and GCC/Clang determinism checks pass.
Two seed-1 multiplayer runs retain `e9eba3acf6d3f911` at tick 1800 with
`err=none`. Both balance-mode composed navigator comparisons still stop at
the documented unit-1402 boat scan deadline mismatch.

## Protocol 143: navigator integration in progress

`World::tickNavigationMovement` now owns the production movement stage. Retry
decisions run before the local scan and steering. The constrained retry branch
tests movement/speed mode bits, independently of repeated body refusal. The
boat gate is `floater && minWaterDepth > 0`: native type +0x192 is maximum
water depth and +0x194 is minimum, as verified by the MOVEINFO copy at
4c0e85..4c0e94 and the independently loaded source class records. Earlier
notes identifying +0x194 as maximum depth were incorrect.

`check_navigation_retry.py` compares complete native 4e5150 branching with
production World requests, including final RNG state and ordered draw bounds.
8,192 cases pass in both Debug and Release, with minimum/maximum depth varied
independently, all outcome bits and movement/speed modes, and refusal states.
Controller acceptance is held false and search submission is observed at its
boundary; this does not verify worker delivery or mission dispatch.

A navigator that consumes its last partial-route point now brakes and retries
while retaining its owning mission (`Order::navigationExhausted`, hashed).
The old disconnected-terrain watchdog no longer silently drops that goal and
starts the next queued command. Regressions cover retained search ownership,
queued orders, partial-endpoint braking/retry, and replacement order reset.
Floating hoverers with nonpositive minimum depth now take the ordinary neighbor
scan; each such scan clears both movement and speed mode.

The new `check_ground_navigation_sequence.py` leaves native navigator ticking,
point advancement, and scanning active. Both sides start with authored partial
routes and advance independently; controller acceptance and formation remain
controlled inputs, and no search worker runs. **This comparison currently
fails**, so integrated navigation parity is not established. Correcting the
hover scan moves the first late-standard mismatch from unit 1323 to boat 1402
at tick 37448: motion still matches, but World has no boat scan deadline.
Crusades reaches the same boat/deadline mismatch; retail's next scan tick is
37452 standard and 37450 Crusades, reflecting their different speed inputs.

Remaining scanner work is traced at 4dba80: boats probe in 16px forward steps
to `(footprintX+1)*16 + sightDistance`, query placement and coarse visibility,
and select speed mode 1 or 2 with a two- or three-half-cell scan interval.
They then run the neighbor scan and boat-specific corner pruning. The
visibility test reads game +0x19ef4 using the player bit at +0x306f; that byte
is initialized from the `Human Player` setting and is also reassigned by
player-selection handlers. Its multiplayer semantics need further tracing;
World's display-only `vis_` cannot be used as a deterministic substitute.
Shoreline routes and both-roster composed navigation remain unverified.

Validation: all targets rebuilt in Debug/Release; CTest passes 33/33 and 27/27,
respectively; all 122 Python harness tests pass. GCC/Clang determinism checks
retain `ab1ef54ae324bd0e` (ARM checks skipped for missing target headers).
Two seed-1 Valysia City multiplayer runs reach tick 1800 with
`e9eba3acf6d3f911`, `err=none`. These checks validate reproducibility and
regressions, not the still-failing composed retail navigation comparison.

## Protocol 142: composed active ground travel

Production movement now calls `World::steerGround` for segment aiming, heading,
acceleration/braking, pitch/speed limits and quantized displacement. Commitment
and the post-mover surface phase remain in their original order. Native 4d91b0
and 4da4d4..4da53a scale the unsigned moving turn rate by stored road/water
multipliers, with road priority. World previously used the raw rate for both
turning and stopping-distance geometry. The first captured comparison exposed
an 1800-BAM heading difference on a road; scaling fixes both uses.

The active adapter also converts native direction explicitly for coincident
waypoints and uses a signed-word turn request. Native heading zero is a half-turn
in World's heading convention; general `bamDiff` instead chose the opposite
sign for an exact half-turn. Both active steering and stopped facing now use
`retailTurnRequest`, where an exact half-turn is -32768. Live road and coincident
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

The new motion input audit also found a harness error: `balance_inputs.py` read
all FBI sections into one key map, allowing a weapon's `turnrate` to overwrite
the UNITINFO value (VERMAGE 180 versus 2300). It now reads only UNITINFO and has
regressions for section isolation, nested fields and invalid input. This was
a comparator-input defect, not evidence to change the engine's unit parser.

Formation pacing, mode producers, controller/route progression and retry
integration still need full sequence coverage. The pre-existing component
reachability shortcuts can retire a movement leg where retail keeps searching;
these remain visible in the production tick and are not exercised by controlled
segments. Shoreline routes, long-range boat attack admission, special yards,
visibility and TARCAN clock-dependent pitch remain unfinished.

Protocol 142 validation: all Debug/Release targets build, with 33/33 Debug and
27/27 Release CTests passing. All 122 Python tests pass. The 16,000-case active
steering oracle and expanded 16,000-case inactive/facing oracle pass in both
builds. Final captured active sequences pass in standard Debug and Crusades
Release. The cross-compiler math golden is unchanged (`ab1ef54ae324bd0e`;
ARM skipped for unavailable target headers). Two seed-1 Valysia City multiplayer
runs finish at tick 1800, `err=none`, hash `cbfc8ef605e2eb31`. The dedicated
movement checks, rather than that small smoke match, establish the changed
steering coverage.

## Protocol 141: stopping sequences and combat movement

`check_ground_stop_sequence.py` now runs original 4dc800 and 51b2a0 repeatedly
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

Ground combat and nearby guard units now commit their coasting displacement.
An explicit handled flag prevents a second movement update if coasting crosses
an attack-range boundary. A stopped attacker supplies a facing target to the
same braking helper. The pivot uses the terrain-scaled unsigned 16-bit rate and
starts on the tick speed reaches zero. Native parser 4bfde8..4bfdf4 defaults
`turninplacerate` to zero; the loader and UnitType now retain that default rather
than substituting moving turn rate. The synthetic brake oracle covers facing
and rate boundaries in 16,000 cases (COB notifications are a sink there; the
captured sequence oracle executes them). Live land/boat regressions cover
combat coasting, pivot start, and crossing attack range without moving twice.

Remaining movement gaps include complete active routes, terrain-scaled moving
turns, formation limits and controller integration. A separate combat adapter
gap surfaced while testing boats: direct ranged line-of-fire currently queries
the land navigation grid, so submerged terrain can spuriously keep a boat's
attack controller moving. The dedicated stopping regressions use short-range
attacks to control the inactive-navigation input; they do not prove that
long-range boat attack admission is correct. TARCAN clock/pitch coupling and
special yard/visibility handling also remain open.

Protocol 141 validation: all targets build in Debug and Release; 33/33 Debug
and 27/27 Release CTests pass, as do 119 Python tests and the 16,000-case
native braking/facing oracle in both builds. The cross-compiler math golden
remains `ab1ef54ae324bd0e` (ARM skipped for missing target headers). Two seed-1
Valysia City multiplayer runs finish at tick 1800 with `err=none`, both hash
`cbfc8ef605e2eb31`. That smoke scenario does not exercise every changed combat
path; the dedicated land/boat regressions provide that targeted coverage.

## Protocol 140: inactive navigation braking

Idle, wait, wait-for-attack, build-rectangle arrival and ground-mission arrival
now pass their remaining speed through movement commitment while braking.
Previously these branches reduced speed without moving. The shared production
helper applies stored road/water flags, pitch and speed-mode caps, native heading
quantization, and the existing collision/refusal adapter. Surface updates still
run after movement. Corner scanning now uses the same stored terrain flags.

`check_ground_brake.py` executes original 4d9ad0 with an inactive navigator and
formation limits disabled, comparing 16,000 controlled inputs against
`World::brakeGround`. Inputs cover pitch boundaries, all speed modes, road/water
priority, fractional speeds and braking to zero. Pending facing is disabled.
Displacements stay within the initial footprint cell: this check proves braking
and displacement composition, not collision sequences or full navigation.
Production tick regressions separately check land and boat stop commands coast
to rest and retain their surface heights. These synthetic cases are independent
of balance settings; captured sequential braking in both rosters remains needed.
Combat holding, formation limits, pending-facing behavior and complete route
sequences remain open; protocol 140 does not establish full stopping parity.

Validation: all targets rebuilt in Debug and Release; 33/33 Debug and 27/27
Release CTests pass. The native brake oracle passes in both builds. The
cross-compiler math check retains golden `ab1ef54ae324bd0e` (ARM skipped for
missing target headers). Two seed-1 Valysia City multiplayer runs finish at
tick 1800 without errors, both hash `cbfc8ef605e2eb31`. That smoke scenario does
not demonstrate the changed stopping paths; the dedicated tests above do.

## Active scope: pathfinding parity (2026-09-19)

The user explicitly confirmed that our AI will differ from retail. Completion
means matching pathfinding and movement for identical orders, terrain,
occupancy, navigation state and movement-owned random inputs. AI strategy,
construction/economy, rendering and whole-game random-call ordering are not
parity requirements. Their differences must not block movement comparisons.
The tick-994 commander response is outside this scope. Both the standard and
Crusades balance settings are explicit requirements (user confirmation).
Verification must identify the selected roster; success in one mode cannot
stand in for the other.

Compare the actual World adapters as well as the isolated search kernels:
terrain/feature grading, body occupancy and traffic, controller geometry,
admission and scheduling, route delivery, steering, collision/refusal and
repath cadence. Use native routine execution and captured initial states with
controlled external inputs. Label those comparisons as movement comparisons,
not uninterrupted whole-game replay. Historical sections below include the
broader investigation and superseded claims; inspect current code before
treating an older gap as outstanding or an older result as completion.

Current adapter audit: raw search grading now consumes the verified map feature
plane and actual rectangular unit footprints, including building yards and
landed flyers. It bypasses the legacy shared obstacle overlay for loaded maps.
The live steering query (`cellScore`) now uses the same footprint and map
feature interpretation with its distinct native slope/body grading rules.
Feature creation, replacement and removal now update that shared plane and
active search caches. The initial feature maps match all three captures.
Production corpse/statue placement and retirement now update the same plane.
The standard zombie corpse needs a native fixture with its definition loaded;
special grade-3 yard blockers, simulation visibility, directional ramp
trajectories and complete controller/collision integration still require work. Boats are in scope:
shared surface queries cover water classes, but complete sailing routes,
shoreline maneuvering and boat traffic require dedicated verification.

### Ground pitch and speed limits (2026-09-19, protocol 139)

The combined movement audit found that pitch is simulation state, not merely
rendering: original `4d95f0` clamps the speed maximum using signed unit pitch.
The earlier height-only scope omitted this ramp behavior. World now retains
ground pitch/roll from the same four support samples, applies the quantized
pitch limit to active movement, and hashes both angles. Ground speed mode
(mover bits 8..10) is also retained and applies the original reduced-speed
factors before the pitch limit. Mode-transition producers still need integration
coverage; testing the numeric factors does not establish those transitions.

Support pitch compares the two pair-averaged heights; roll compares corners
0 and 1. Their denominators use integer model spans scaled by FBI pitchscale
and bankscale, with truncation before native angle rounding. Native parsing
at `4c09a3` supplies bankscale 0.5 and ground pitchscale 0.5 by default (flyer
pitchscale defaults to zero). The loader now uses those defaults and retail's
fixed-number conversion. Probe format 36 restores initial surface angles;
older fixtures remain accepted but do not restore that initial state.

`check_ground_height.py` now compares Y, pitch and roll on 15,000 original
height-dispatch calls. `check_ground_speed.py` compares 16,000 original speed
integrations and resulting X/Z velocities, exercising pitch-bucket boundaries,
terrain multipliers, road priority and all speed-mode values. Formation
controllers are absent from that speed test. Debug and Release both pass.
The World adapter additionally passes 12,000 height/pitch/roll queries in each
balance mode across the late capture's 95 surface units, including boats;
Crusades runs in Release. A live ramp regression verifies the stored pitch,
then checks its effect on actual speed and displacement on the next step.

The broader tests exposed a stale reference in the post-movement surface
update when a builder mission creates a site and reallocates the unit vector.
That final phase now resolves its owner by ID. The existing queued-builder
regression exercises the affected path. All targets build; 33 Debug and 27
Release CTests plus 119 Python tests pass. GCC/Clang retain math golden
`ab1ef54ae324bd0e` (ARM headers unavailable). Two seeded 1800-tick multiplayer
runs match at `cbfc8ef605e2eb31`, `err=none`.

TARCAN's clock remains an explicit unresolved trajectory input. A native
controlled test using its shipped 3DO, scales 2.4, and a seven-height-unit
per-cell slope gives pitch -2002 at clock phase 0 and -2059 at phase 26:
different speed-limit buckets. This does not by itself prove different
trajectories (current speed/acceleration also matter), but it disproves using
watermultiplier 1.0 alone to dismiss phase coupling. World currently uses
simulation tick for bobbing; complete TARCAN comparisons must account for
the native clock input and the resulting speed limits.

Further audit findings: inactive navigation brakes through `4d95f0` and still
produces velocity for commitment, while World has paths that only reduce
speed and hold position. Arrival braking and corner-scan branches also retain
center-depth sampling. Those adapters, formation constraints, and complete
steering/height/collision/repath sequences need comparison and correction.
The yard/visibility and standard zombie corpse fixture gaps remain open.

### Movement commitment (2026-09-19, protocol 138)

Actual surface movement now calls `World::commitGroundStep`, which uses the
verified `mobilePlacement` query when crossing footprint cells on a loaded
map. It no longer uses square `NavGrid::fits` and `cellFree` for that decision.
The same-cell bypass retains the current refusal state, as retail does.
Successful crossings clear refusal and update occupancy; refused steps clamp
each axis using its actual footprint dimension and cap speed at base/2 or
base/5. Refusal speed now uses the stored road/water flags with road priority.
The occupancy rebuild also stamps rectangular footprints. Legacy terrain-only
harnesses without a feature plane retain their older placement fallback.

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

All Debug/Release targets build and all 33/27 CTests pass; 119 Python tests
pass. GCC/Clang retain golden `ab1ef54ae324bd0e` (ARM headers unavailable).
Two seeded Valysia City multiplayer runs reach tick 1800 with the same
`2f8a5c4befc60891` checksum and `err=none`.

These checks establish movement commitment for the tested displacement
sequences. They do not establish the steering decisions that produce those
displacements, attached movement, or complete route/height/search-cache
integration. Special yard/visibility handling and the standard zombie corpse
fixture remain open. Complete controlled ramp and shoreline routes are still
required before declaring pathfinding parity.

### Surface height in World (2026-09-19, protocol 137)

Surface units now store ground Y and the last proposed movement tick. The type
loader reads upright and the exact 3DO root selection polygon, mirroring its
support points as retail does. Each unit updates height after its mover,
including movement-loop early exits; the next tick tests that stored integer
height against sea level. Active-leg speed, acceleration and braking select the
stored road/water multiplier with road priority, instead of resampling center
depth. Height and movement stamp are hashed. Probe format 35 restores initial
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
turn-cost multipliers. The later pitch audit above shows why that does not
settle all bobbing/movement coupling. Protocol 139 adds pitch/roll simulation
state; rendering remains separate. This does not establish all TARCAN parity.

All targets build in Debug/Release. The runtime validation includes 33/27
CTests and 119 Python tests. GCC/Clang retain math golden `ab1ef54ae324bd0e`
(ARM headers unavailable). Two seeded 1800-tick multiplayer runs agree on
`2f8a5c4befc60891`, `err=none`; the new ground state intentionally changes the
protocol-136 checksum.

The movement-commitment gap identified here is addressed by protocol 138 above.
Complete controlled ramps, shorelines and routes remain required, along with
the special yard/visibility and standard zombie corpse gaps.

### Surface-height kernels (2026-09-19, before runtime integration)

`retailheight.h` now implements retail's integer bilinear height sampler,
upright/hover clamp, floater waterline, rotated model-support height, and
tilting-hover bobbing with an explicit clock input.
`check_ground_height.py` compares 15,000 cases against original executable
execution; Debug and Release both pass. It includes 3,000 floater cases.
At this stage the kernels were not connected to World; protocol 137 above
adds that integration. Kernel checks alone do not establish complete movement.

Native per-unit tracing establishes that ground Y updates after the mover,
and feeds the next tick's water flag. The old World substituted center-cell
water depth and had no stored surface Y. Protocol 137 adds model support loading,
ground-Y state/import/hash, per-unit height update timing, and terrain-speed
selection from the resulting flags. TARCAN's special hover bobbing uses a
wall-clock input; the chosen deterministic adapter is documented above. The
calculation is verified with a controlled clock. Both shipped TARCAN rosters
have road/water multipliers 1.0, so its water flag does not alter speed or
turn-cost multipliers. At this stage pitch/roll were omitted; protocol 139
extends the kernel and identifies their additional movement coupling.

All targets build in Debug/Release, with 33/27 CTests passing. GCC/Clang retain
golden `ab1ef54ae324bd0e` (ARM headers unavailable). Two seeded 1800-tick
multiplayer runs retain `e37fa8921cb30c29`, `err=none`; these kernels have not
changed World behavior yet.

At this stage movement commitment still used legacy square `g.fits`/`cellFree`
checks. Protocol 138 connects rectangular placement and checks sequential
commitments. Passing those checks is not evidence for complete routes.

### Corpse and statue navigation (2026-09-19, protocol 136)

Dead-unit records now own installed corpse footprints in the shared feature
plane. Placement uses the original unit origin plus corpseadjustx/z, with the
selected feature's dimensions. Frozen/stone deaths install immediately;
ordinary wrecks install at the project's existing death-animation completion.
The old structure footprint is released separately. Reclaim, resurrection and
decomposition remove the installed feature footprint. Replacing a corpse retires
its owner, so later cleanup of that old unit cannot erase the replacement.
The old body cache is refreshed before any corpse offset moves the record.
Ownership and active corpse lifetime/work state participate in the checksum.

`check_corpse_features.py` executes original 512ee0 corpse/stone/frozen dispatch,
495360 feature installation and 496380 removal, then compares every changed
feature cell after each event with the production World helpers. Placement
refusals and prior-feature removals are preserved, not filtered from results.
The emulator retains freed heap backing pages instead of entering captured OS
heap locks; feature logic itself executes natively. This test supplies feature
events and excludes combat/death-animation timing. Ordinary death still uses
the existing 120-tick animation threshold, not a newly verified retail timer.

All 169 late-capture units in all three modes pass: 507 placement attempts,
246 accepted, followed by 507 retirement operations and 5,084 changed cells.
Both Debug and Release pass. The set includes the captured boat types. Runtime
regressions cover differing unit/wreck dimensions, offsets, mobile statues,
indestructible refusal, replacement ownership, decomposition, ordered reclaim,
and active search-cache updates.

Controlled standard-mode setup correctly stops at `tarzom_dead`: standard
TARZOM names that corpse, but Crusades TARZOM has no ordinary corpse and the
captured native feature table never loaded its definition. The source feature
exists in data.hpi, is nonblocking and 2x2. The port loads it through the standard
registry, but this native corpus cannot yet establish its placement parity.
The comparator does not substitute a different feature or silently skip it.
An independent source comparison of corpse/stone/frozen names and offsets for
all 77 captured unit types found only this TARZOM difference between standard
assets and the Crusades capture. The broader 30,000-query standard live-grade
comparison still passes.

All 33 Debug and 27 Release CTests, plus 119 Python tests, pass. GCC/Clang retain
determinism golden `ab1ef54ae324bd0e` (ARM headers unavailable). Two seeded
Valysia City multiplayer runs reach tick 1800 with hash `e37fa8921cb30c29` and
`err=none`. These results do not establish full route or terrain-transition
parity.

### Mutable feature footprints (2026-09-19, protocol 135)

The 30,000-query mid-capture live check exposed one feature discrepancy in
both balance modes: the frozen statue at anchor (294,49), queried by unit
786 at center (295,50), was absent from the restored World. The importer had
ignored new feature anchors. More generally, the navigation plane retained
original map footprints when tracked features changed shape.

Initial map loading and runtime installation now share footprint overlap and
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

The production corpse/statue gap identified here is addressed by protocol 136
above; restored maps alone were not used as evidence for that lifecycle.

Native `0x512ee0` dispatch was also checked on 256 corpse/stone/frozen calls
from the late capture, intercepting feature installation without changing its
arguments. The anchor is the original unit cell origin plus FBI corpse offsets,
independent of the replacement feature's footprint. This is placement-dispatch
evidence only; installation timing and retirement still need native tests.

Validation after the active-cache fix: all 33 Debug and 27 Release CTests pass,
including feature mutation/cache regressions; 119 Python tests pass. The latest
30,000-query live comparison and exhaustive late feature-plane comparison pass.
GCC/Clang determinism retains golden `ab1ef54ae324bd0e`; ARM toolchain headers
remain unavailable. Two seeded Valysia City multiplayer runs reach tick 1800
with hash `819cdedba72d7709`, `err=none`. These smoke tests cover determinism,
not complete movement parity.

### Stored terrain flags (2026-09-19, protocol 134)

Traffic and path costs now use stored road/water bits rather than recomputing
them during each speed query. The importer restores them and the state hash
includes them. This resolves the under-construction requester 845's stale-flag
case against occupant 3737. Runtime road flags update at the movement phase;
water still uses the existing depth approximation. Native water flags compare
unit integer Y with sea level, so ground height and transition timing remain
unverified and must be addressed with complete movement trajectories.

### Captured balance selection (2026-09-19, probe 34)

The initial saved-movement fixture always called `setupRegistry(...,false)`.
Native unit loading selects `unitsCB` when global byte `0x641144` is nonzero
(`0x5174e0`, called at `0x4bf540`). All three existing early/mid/late captures
have that byte set. Probe 34 records the captured selection in the header,
loads the matching registry, and reports it in output metadata. The direct
grade comparator rejects pre-34 fixtures and mismatched balance settings.
Both values are covered by the importer test, but this is not yet full
standard-mode movement verification.

This resolves nine of the ten mid-capture live-grade mismatches: captured
ARAPULT/VERPULT use the shipped Crusades GROUND5 class (slope 30), while the
old fixture loaded standard GROUND4 (slope 15). The class parser and inheritance
were correct. No global siege-unit slope override was introduced.

Protocol 134 resolves the remaining stored-terrain-flag discrepancy; see above.
Native `0x421ae0` reads mover+36 bits 0x800/0x1000. `0x4dc800` updates
those bits at writes 0x4dc822 and 0x4dc844. Water uses integer unit Y relative
to sea level, which still constrains the remaining trajectory work.

### Live movement grades (2026-09-19, protocol 133, probe 33)

The new `check_world_grade.py --live` comparison executes native `0x4db640`
against actual World `cellScore` at the same footprint center. All terrain
and feature checks precede the sorted distinct-body checks, matching the
native wrapper. Live terrain rejection is -1; an ordinary structure yields
0; unacceptable traffic yields 2. Acceptable traffic retains the terrain
grade instead of the former invented grade 5. Live slope penalties remain
on roads, unlike raw search terrain grading. Grade 4 remains passable at
the normal movement threshold, though it ranks below grades 6 and 7.

Probe 33 restores every flyer's captured low ground/air mode. Active flying
builders previously defaulted to landed when restored without a VTOL standby
mission, producing a false ground occupant. Probe 33 retains earlier format
support; earlier fixtures cannot establish airborne occupancy parity.

On the early captured map, all 30,000 live queries match exactly, including
5,000 ramp candidates and 5,000 cells north of steep terrain. There are zero
step-acceptance differences at threshold 4. The raw comparison with 6,000
controlled requester/age inputs still passes after the shared feature-query
refactor. These checks observe possible steps at an initial state; they do
not yet compare a unit's entire uphill/downhill trajectory.

Debug and Release each pass the 30,000-query live comparison. All 33 Debug
and 27 Release CTests and 118 Python tests pass. GCC/Clang retain golden
`ab1ef54ae324bd0e` (ARM headers unavailable). Both seeded multiplayer runs
finish 1,800 ticks without errors and hash `9a61db9d0fe15fea`.


The later tick-14468 capture exposes 10/6,000 remaining live-grade differences
(6 change step acceptance). Nine involve Catapult/Trebuchet units 3737/831;
unit 3737's native type has hard slopes 30/30 and depth limits 10/-10000,
while its mover points at a native grid with slope bytes 30/15/30/15. Check
retail class selection and unit-versus-grid limit inheritance against the
current registry before changing either. The other occupied-cell discrepancy
uses requester 845 at center (97,289): port grade 2, native 6. This later-state
failure is unresolved; early-capture success does not prove all-class parity.

### Raw grade integration (2026-09-19, protocol 132)

World's raw rectangle host now respects projected boundary markers, resolves
feature footprint tails, assigns grade 1 to clearable blocking features, and
uses actual body footprints/yard cells for occupancy. A rectangle snapshot
keeps full cache refreshes from repeatedly scanning every unit for every cell.
The snapshot is derived from current units for each refresh, not persisted or
restored from native grade results.

The fixture importer now restores mover+28 for landed flyers from initial
captured memory. Previously those units lacked `mover_hex`, so the importer
substituted the capture tick and erased their occupancy age. In the early
capture unit 383 has stamp 877, not tick 959. This accounted for the remaining
23 occupied-cell differences after the feature/body adapter correction.

The fixed importer and integrated host pass 30,000 stratified raw rectangle
queries against native `0x5088f0`, including 5,000 each for occupied, featured,
steep, north-of-steep, arbitrary and ramp-candidate cells. Each category mixes
single-cell and actual unit footprints. This is initial-state raw grading
coverage, not directional movement, dynamic obstacle lifecycle, or full-game
parity. The failing counts below describe the pre-fix audit.

Both Debug and Release pass the 30,000-query comparison. All 33 Debug and
27 Release CTests and 118 Python tests pass. An additional 6,000 queries
with controlled requester IDs and occupancy-age thresholds also match.
GCC/Clang retain deterministic
golden `ab1ef54ae324bd0e`; ARM target headers remain unavailable. Two seeded
Valysia City multiplayer runs finish 1,800 ticks with `err=none` and identical
hash `9a61db9d0fe15fea`. Optimized full Debug CTests take about 34 seconds
(the initial per-cell unit scan took 98 seconds).

### Raw World grade audit and ramp coverage

`tools/re/check_world_grade.py` now compares native `0x5088f0` with the
restored World's raw rectangle host without executing AI or simulation ticks.
The early tick-959 capture, seed `0x5088f0`, 6,000 queries (1,000 per category,
alternating single-cell and unit footprints within every category) produces:

| Query cells | Matching | Retail accepts |
| --- | ---: | ---: |
| Occupied | 677/1000 | 68 |
| Features | 858/1000 | 67 |
| Steep (quad spread >30) | 999/1000 | 65 |
| 1–4 cells north of steep terrain | 1000/1000 | 327 |
| Arbitrary | 991/1000 | 544 |
| Ramp candidate (quad spread 1–30) | 996/1000 | 626 |

These categories can overlap obstacles. A ramp candidate is a height-spread
sample, not proof of an entire traversable ramp. These are placement grades,
not yet directional uphill/downhill controller or route comparisons. The
479 mismatches were the pre-fix failure; the integration above resolves them
on the tested initial capture.

Native feature classification needs more than the blocking bit: blocking
features with flag `0x20000` yield grade 1. The loader initially sets that bit
for destructible features (`0x494178..0x4941b3`) but subsequently clears it
when the reachable replacement-feature chain includes an indestructible
blocking feature (`0x4945bb..0x494854`, links at feature offsets `0x12c` and
`0x12e`, confirmed as `featuredead` and `featureburnt`). Therefore simply treating every destructible feature as grade 1
would be incorrect. The loader now follows both replacement links with cycle detection and
stores the clearability classification. Both Debug and Release map checks
match this flag on every named feature cell in the 230,400-cell captured map.
The integrated raw grade adapter consumes it together with actual
body footprints, preserving building blocking. Native data confirms destructible Valysia buildings can
lack this bit while trees and some walls have it.

## Flying-builder orbit and movement rate (2026-09-19, protocol 131)

Flying construction now updates movement-rate flags and the `MoveRate` script
callback, then repositions its orbit with retail's four ordered random draws.
Probe 32 restores the two movement thresholds and build radius. Installing the
new controller clears its pending event bits. Completion is still guarded.

Both builds pass 34 early-save tick-body transitions through 993 with the new
generic `check_replay_fixture.py`: compared unit motion, flying-builder mission
state, construction work/HP, resource pools, 321 gameplay and 393 CRT draws.
This excludes outer-frame rendering and is not full-state parity. Retail's
next occupied base update is owner 7, slot 1, containing unit 3601; its native
threat query returns true at tick 994. The port still guards that response.

All 33 Debug and 27 Release CTests and 118 Python tests pass; GCC/Clang retain
golden `ab1ef54ae324bd0e` (ARM headers unavailable). Both seed-1 multiplayer
runs finish 1800 ticks without errors and hash `cd13ea7591179314`. Original
factory and natural-production fixtures retain their 314/317-tick prefixes.

## Active flying construction (2026-09-19, protocol 130)

Probe 31 restores active flying construction separately from ground jobs.
The stage-5/6 loop consumes the bound-50 draw, works on its site and preserves
flight movement. Point arrival posts mission events while velocity decays.
Orbit repositioning and completion remain explicit unsupported guards.

Both builds produce identical early-save traces. Unit 1229's motion, including
altitude, and its flying construction mission state match five transitions;
site work/HP and resource pools also match. All gameplay draws agree. The full
scenario still differs in unit 3601's movement: its live primary mission is
ground construction, despite patrol orders in the save. Two outer-frame CRT
draws remain outside the tick-body replay. See [save coverage](retail-save-coverage.md).

All 33 Debug and 27 Release CTests and 118 Python tests pass. The GCC/Clang
golden remains `ab1ef54ae324bd0e` (ARM headers unavailable). Two seed-1
multiplayer runs reach tick 1800 without errors and hash `cd13ea7591179314`.
The existing factory replay passes 314 accelerated boundaries (4,013 gameplay,
627 CRT draws) and 317 natural boundaries (3,718 gameplay, 754 CRT draws).

## Partial path endpoints (2026-09-19, protocol 128)

Ground missions retain their requested destination separately from delivered
navigator points. Partial routes no longer append that destination as an extra
corner. The extra corner had changed unit 248's pruning and steering before it
reached the partial endpoint. Its complete navigator route is now compared from
tick 10277 onward.

Both builds match **311 boundaries through 10380**: accelerated production uses
**3,980 gameplay / 621 CRT draws**, and natural production **3,666 / 744**.
All 33 Debug and 27 Release CTests pass, and the GCC/Clang math golden remains
`ab1ef54ae324bd0e`. Both 1,800-tick multiplayer runs agree on
`a8504979381fa229`, without errors. Unit 146 diverges in motion at tick 10381;
its route already differs when delivered at tick 10122. This route was outside
the earlier selected-route comparisons, so matching motion does not establish
complete navigator-state parity.

## Strike reachability and cached flight sectors (2026-09-19, protocol 127)

Strike members now run the original rectangle-connectivity rule on their cached
navigation plane. Two wall followers try to rejoin the direct L-shaped route;
long failed traversals terminate the query, while short failures retry source
perimeter cells in reverse order. Exhaustive flood fill is not equivalent.
`check_rectangle_reachability.py` matches 20,000 synthetic cases in each build,
covering both grade modes, bounded traversal, rectangular bodies and map edges.
Unreachable-member withdrawal remains an explicit guard.

Flight point-controller arrival posts both arrival and release events for move
and patrol missions. Flight terrain uses the center sector cached at the last
footprint relocation, rather than recalculating a sector from the current
footprint origin. This corrects an early descent after a patrol turn. Probe 30
restores initial sector links from the captured entity pool and hashes that
state. Refused factory-yard requests at map boundaries also send their script
event, as occupied-yard refusals already did.

Both builds match **265 boundaries through 10334**, with accelerated production
(**3,319 gameplay / 543 CRT draws**) and natural production (**2,990 / 640**).
All 33 Debug and 27 Release CTests and 116 Python tests pass; GCC/Clang's math
golden remains `ab1ef54ae324bd0e` (ARM headers unavailable). The next divergence
is unit 248's partial navigator endpoint and resulting turn at tick 10335.
Both seed-1 multiplayer runs finish 1,800 ticks without errors and hash
`a8504979381fa229`.

## Scout patrol replanning (2026-09-19, protocol 126)

Quiet-base unarmed scouts now choose patrol points with retail's ordered radius,
angle and separation draws. Their initial flight-move mission snaps to the body
center, starts the flight script immediately, and hands off to a patrol that
appends its return anchor. The comparison now checks the scout's entire mission
queue, including flags, pending events, deadlines and destinations.

Both builds match **161 ticks through 10230**, with accelerated production
(**1,915 gameplay / 347 CRT draws**) and natural production (**1,842 / 409**).
All 33 Debug and 27 Release CTests and 115 Python tests pass. GCC/Clang agree on
golden `ab1ef54ae324bd0e`; ARM target headers remain unavailable. Both seed-1
multiplayer runs reach 1,800 ticks with hash `a8504979381fa229` and no errors.
The next guard is strike-member rectangle reachability at tick 10231.

## Construction routes, flight terrain, retry outcomes and factory yards (2026-09-19, protocols 123–125)

Completed mobile construction now refreshes occupancy and cached search grades.
Build-rectangle paths retain their accepted perimeter endpoint instead of
appending the rectangle's nominal destination. Flight terrain sampling uses the
footprint-origin sector and the maximum corner height, including the water floor.
Navigator delivery retains the detour outcome, and partial/detour routes suppress
the ordinary 120-tick retry draw as in the original.

The factory comparison now checks active thread headers for both the Keep and
the airborne parrot. This exposed two World host omissions: yard transitions
must wait for bodies occupying cells blocked in the requested state, and factory
activation runs a zero-elapsed notification after the regular script update.
The old host closed the occupied yard immediately and eventually produced VERMER
too early. Yard checks distinguish upper/lowercase map cells; tests exercise
blocked requests and subsequent success after the occupant leaves. Dynamic yard
navigation stamping and BUGGER_OFF displacement still need fuller integration.

Both builds match **140 ticks through 10209** with accelerated
production (**1,409 gameplay / 309 CRT draws**) and natural production
(**1,595 gameplay / 365 CRT draws**). It checks all restored unit motion,
construction, resources, factory queues and AI squads, plus selected mission,
route and script-thread state. Tick 10210 next enters the explicitly unsupported
base combat-member replanning branch. This is still partial scene coverage.
All 33 Debug and 27 Release CTests, 115 Python tests and 1,024 factory flag oracle
cases pass. GCC/Clang determinism agrees on `ab1ef54ae324bd0e`; ARM cross-builds
remain unavailable because target headers are missing. Two protocol-125 seed-1
multiplayer runs finish 1,800 ticks without errors, both hashing
`a8504979381fa229`.

## Strike mission replacement and airborne script notifications (2026-09-19, protocol 122)

The restored strike squad now refreshes its ground movement mission at tick
10132. Its old navigator route survives mission replacement; the new controller
starts on the next unit update and requests the new destination. Mission targets
are stored separately from route endpoints, so delivered paths retain the
original grid waypoints. The comparison checks unit 927's mission fields and
complete navigator route after this replacement. Target selection, combat,
withdrawal and multi-base retreat selection remain explicit unsupported branches.

Airborne movers now send the original `setSFXoccupy(5)` notification when their
occupancy changes, including the immediate zero-elapsed script update. Omitting
that update delayed the parrot's flight animation and first changed gameplay RNG
at tick 10147. The harness now compares its active script-thread headers from
the first boundary. Landed and water occupancy transitions remain unported.
Probe format 29 restores initial script occupancy alongside the existing unit
flags; it does not infer that state from later frames.

Both builds match **90 ticks through 10159** for accelerated factory production
(**962 gameplay and 207 CRT draws**) and natural production (**952 gameplay and
243 CRT draws**). Checks include unit motion, construction, resources, factory
queues and AI squad state. All 33 Debug and 27 Release CTests and 115 Python tests
pass. This remains a partial restored scene, not full-game parity.

## Occupied base factory planning (2026-09-19, protocol 121)

Restored bases now issue production orders using their captured ordered build
menus, availability limits and signed priority/preference weights. The factory
admission helper matches 8,192 original cases covering allocation thresholds,
already-queued production, cost-dependent deferral and RNG order. The World host
also handles healthy builders retaining active construction, busy combat units
deferring replanning, base growth thresholds and redistribution attempts with no
other occupied base. Combat replanning, threats and actual squad transfers remain
explicit guards, not no-op replacements.

The accelerated factory scene matches **60 original simulation ticks through
10129**, including unit motion/construction/resources, factory queues, AI squad
state, **747 gameplay and 165 CRT draws**. This includes base 1 ordering VERMER
at 10102 and an unsuccessful redistribution search at 10118. All state comes
from the initial boundary; the executable generates the extended tick sequence.
Natural production also matches 60 ticks in both builds, with 734 gameplay and
189 CRT draws. The next occupied squad guard is strike slot 21 at tick 10132.

At 10109, an external native allocation required two additional offline hosts:
fixed `GlobalAlloc` and `GlobalLock`. They use the existing bounded allocator,
honor zero initialization and reject movable flags or unknown handles. No game
routine is bypassed. Three regressions cover that boundary, bringing the Python
suite to 115 tests. All 33 Debug and 27 Release CTests pass.

## Unanchored base centres and planner predicates (2026-09-19, protocol 120)

The original unanchored base centre averages every live squad member, including
mobile units. Protocol 119 incorrectly restricted this fallback to structures.
The corrected accumulator preserves signed high-word coordinates, unsigned
32-bit summation and truncating signed division. A World regression distinguishes
the two outcomes; 4,096 original-executable centroid cases pass in both builds.

The occupied base planner remains guarded at tick 10102. Its category predicates
are now isolated and checked against 16,384 original-executable cases covering
all eight categories, invalid categories, construction capability, mobile state,
commander exclusions and mission flags. These predicates are preparation for
the planner port, not evidence that occupied planning is integrated.

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
hash; no future planner output is imported. Five decoder regressions bring the
Python suite to 112 passing tests. Both scene checks below also pass with format 28.

The controlled factory scene still matches 32 boundaries through 10101,
including compared AI state and 535 gameplay/117 CRT draws. Natural production
matches 30 boundaries with 518 gameplay/125 CRT draws. All 33 Debug and 27 Release
CTests and 107 Python tests pass. The determinism guard and available x86
GCC/Clang builds agree on `ab1ef54ae324bd0e`; ARM cross builds lack target headers.
Two seed-1 multiplayer runs each finish 1,800 ticks with four units, no errors
and hash `b85e7cb90db1dd16`.

## New-unit AI assignment (2026-09-19, protocol 119)

Restored AI managers assign eligible new mobile units to the nearest occupied
base slot (1..20), falling back to slot 2, and apply the original offensive
standing orders. Probe format 27 restores the signed base-anchor references
from the initial capture's two anchor tables. Bases without a valid anchor
used their building-member centre (corrected in protocol 120 above). Selection preserves the original separate
fixed-point square truncations, signed comparison and first-slot tie rule;
4,096 randomized original-executable cases pass in Debug and Release.
Older captures explicitly reject selection requiring absent anchor data.

Before unit updates, active squads refresh their completed-mobile presence
flag and mark themselves dirty only when that flag changes. This was exposed
by expanding the scene oracle to compare squad membership, countdowns,
deadlines, active/dirty flags and nine saved parameters at every boundary.
A World regression covers anchor selection, standing orders, next-tick
presence publication and anchor hashing; two decoder tests validate signed
anchor references and malformed vectors.

The controlled factory variant matches 32 original simulation ticks through
10101, including all unit motion/construction/resources and compared squad
state, 535 gameplay draws and 117 CRT draws. The last two ticks extend beyond
the recorded window: only initial captured state is supplied to either
engine, and the original executable generates each subsequent boundary.
Occupied squad planning is still guarded at 10102. Full planner behavior,
per-unit AI maintenance and outer-frame rendering remain incomplete.

## Mobile site birth in dispatch order (2026-09-19, protocol 118)

A builder arriving at its construction perimeter now allocates the site during
its own unit update. Deferring allocation until after the unit loop let other
builders consume CRT particle draws first and selected the wrong entity slot.
After allocation the loop reacquires references and restores unit-slot order,
so a higher-slot newborn receives its first update that same tick.

Restored mobile sites use captured build costs/max HP, begin at zero HP with
all work remaining, execute Create immediately, and enter GetBuilt. Their
builder starts the two-argument StartBuilding script, turns before waiting
for build stance, then uses the verified construction-work kernel. COB stance
writes publish the original mission wake event. Factory and mobile creation
share initial site setup.

The controlled fast factory now matches 28 Debug/Release boundaries through
10097, with 513 gameplay and 93 CRT draws. `check_factory_completion.py
--natural-production --ticks 30` leaves the type catalogue unchanged and
matches all 30 original simulation boundaries: unit identities/motion,
construction/resources, 518 gameplay and 125 CRT draws. The comparison runs
without outer rendering in either engine; it is not live-frame parity.
The accelerated case next reaches the guarded AI assignment for its completed
output at 10098. Initial save restore and outer rendering remain partial.
All 33 Debug and 27 Release tests pass; seed-1 multiplayer runs reach tick
1800 with hash `b85e7cb90db1dd16`, four units and no errors.

## Feature footprint origins (2026-09-19, protocol 117)

Map feature records identify the footprint's top-left cell. Their world centre
is `(cell * 16 + footprint * 8)` on each axis (original `4931e0`). The port
previously treated the record as the centre cell, shifting every multi-cell
obstacle up and left. Registration, rendering and restored presence now agree
on the origin; replacement preserves it, and reclaim frees the same cells.
The renderer looks up tracked features by stable cell ID rather than centre.

A 2x2 ZonTree201 regression checks the blocked cells, clear neighbours, world
centre and reclaim cleanup. The initial-presence importer (probe format 26)
reads only the captured initial feature plane and validates its catalogue;
five synthetic tests cover origin markers, absent data and malformed inputs.
Changed saved feature types remain explicitly unsupported.

The controlled factory comparison now passes 25 ticks through 10094 in Debug
and Release: all unit motion/construction/resources, 292 gameplay draws and
86 CRT draws. The next difference is mobile-site allocation at 10095: the
port defers it past construction-particle draws, selecting a different slot.
The unmodified fixture still matches 518 gameplay draws and restored motion;
its outer-renderer CRT gap remains. All 33 Debug and 27 Release tests pass
(the combat/leash fixtures now use clear flat ground), as do 105 Python tests
and the cross-compiler golden `ab1ef54ae324bd0e` (ARM toolchain unavailable).
Two seed-1 multiplayer runs finish tick 1800 with hash `a5ebe2af61a70e90`,
four units and no errors.

## Factory completion and ground parking (2026-09-19, protocol 116)

Restored factories now complete ordinary ground outputs, emit their final
construction particles, notify StopBuilding/Deactivate, and clear the factory
target. GetBuilt installs the original randomized PARK mission when there is
no rally queue. PARK requests an annular pathfinding goal, preserves navigator
retry timing and steers toward the ring midpoint. Its stages, target loss,
retry draws and requested radii match 8,192 executable cases; ring steering
matches another 4,096 cases in addition to the existing goal-query checks.
Flying outputs and rally-order copying remain explicitly guarded.

Factory Create runs immediately with zero elapsed time before work payment,
so even an output completed on its birth tick sees its unfinished state.
StartBuilding receives the original argument-stack layout. Script activation
reads the script activation flag; BUILD_PERCENT_LEFT follows the original
1..100 mapping (8,198 executable cases). Restored construction units retain
their captured maximum HP after completion for script health and regeneration.
This matters for VERPULT: the captured maximum is 1,289 while the loaded asset
default is 2,289, which otherwise starts damage-smoke RNG on a full-health unit.

`check_factory_completion.py` changes only the initial VERPULT cost and inverse
build time in both engines. Debug and Release match 21 tick boundaries through
10090: every unit's motion, construction work/HP, resources, 266 gameplay draws
and 76 CRT draws. Both executions omit outer rendering; this is a controlled
variant, not a whole-scene/live-capture parity claim. At 10091 the port refuses
a footprint crossing that the original accepts; factory exit placement remains
under investigation. Running the port farther reaches the explicit unassigned
AI-unit selection guard at 10098. The unmodified 30-tick fixture retains all
518 gameplay draws and restored movement matches, with its renderer CRT gap
unchanged. Two seed-1 multiplayer runs finish tick 1800 with identical hash
`bbd325687d10850b`, four units and no errors.

## Mobile construction completion (2026-09-19, protocol 115)

Completed mobile-builder jobs now apply the verified completion eligibility
kernel, notify `StopBuilding` immediately, release the builder, and retire the
site's waiting mission in unit-slot order. Immediate COB notifications run the
whole script scheduler with zero elapsed time, matching original thread and
piece behavior for both captured builders over 31 boundaries.

Probe format 25 also restores inactive navigator search stamps and the active
builder's remaining 61 queued builds (115 queued builds total). Activating a
queued build cancels and replaces its path request while preserving the old
search stamp for the random retry cadence.

`check_mobile_completion.py` changes only initial remaining work to exercise
completion of site 389 or 700 in both engines. Both variants match all 47 unit
motion states, construction progress/HP, waiting missions, resource pools and
all gameplay/CRT draws for that tick: 29/9 draws for 389 and 28/9 for 700.
This is a controlled executable comparison, not an event observed in the
unmodified live trace. The original trace retains all 518 matched gameplay
draws and restored movement tracks; its first missing CRT call remains the
outer renderer at tick 10074. First-five-tick construction comparisons pass.

Both full builds pass 33 Debug / 27 Release CTests, including completion and
finished-site death regression coverage. The Python suite passes 100 tests;
the detmath guard and available cross-compiler golden checks pass
(`ab1ef54ae324bd0e`; ARM builds remain unavailable). Factory output completion,
special attachment/activation/death completion hosts, and final unconjure
removal remain explicit unsupported paths.
Two seed-1 multiplayer runs finish tick 1800 with identical hash
`bbd325687d10850b`, four units and no errors.

## Unfinished construction mission host (2026-09-19, protocol 114)

Probe format 24 restores each site's GetBuilt waiting mission, including its
builder, stage, wait mask, deadline and pending events. New factory sites start
that mission when created. World consumes construction events in slot order,
postpones abandonment decay for 30 ticks after work, and applies the original
half-unit unconjure step only when the waiting deadline expires. Restored sites
with these missions bypass legacy per-tick orphan decay. Mission state is hashed.

The waiting handler matches 4,096 original-executable cases. The executable
comparison checks all 11 GetBuilt mission observations in the first five ticks,
including newly allocated unit 806, as well as remaining work, HP and resource
pools. Debug and Release produce 666 identical replay events. All 518 gameplay
RNG calls and restored movement tracks remain matched; the first CRT gap is
still call 44 in the outer renderer. Full builds pass 33 Debug / 27 Release
CTests, 100 Python tests, the detmath guard and the available cross-compiler
golden checks (`ab1ef54ae324bd0e`). Completion and final unconjure-removal
notifications remain guarded, pending their host integration. Two seed-1
multiplayer runs finish tick 1800 with identical hash `bbd325687d10850b`,
four units and no errors.

The `429990` completion eligibility/flag kernel separately matches 8,192
original-executable cases. Alliance, attachment, activation and network
notifications are observed callback hosts in this comparison; their full
effects and completed-mission scheduling still need integration.

## Visual-quality CRT decision (2026-09-19)

`client/retailquality.h` reproduces the fixed-percentage and adaptive visual
refresh decision (`4ec7cb..4ec879`). Adaptive smoothing advances once per
selected unit, not once per frame or simulation tick. Above the FPS threshold
it consumes no random value; at or below the threshold it samples the shared
CRT stream. Debug, Release and Clang pass 8,192 original-executable comparisons
of decision, smoothing, probability, call site and final random seed.
The helper is not yet integrated into the renderer or restored World.

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

Both full builds pass 33 Debug / 27 Release CTests and 97 Python tests.
The shared simulation has not changed since the protocol-113 validation below.

## Restored construction and ranked allocation (2026-09-19, protocol 113)

Probe format 23 restores initial active construction missions, float resource
accounting, construction emitters and per-player entity pools. World advances
emitters and construction in retail slot order. Factory births use the verified
ranked-free-slot allocator and immediately participate in construction; a site
with zero HP remains alive while unfinished. The new factory demand contributes
to the next tick's allocation, and factory payment precedes later builders.
Initial catalogue geometry and quality settings supply fresh emitter profiles.
Their arithmetic matches 4,096 original-executable cases and all 47 initial
captured emitters. Later changes in renderer quality are not yet modeled.

The initial-state replay now naturally creates unit **806 at tick 10074**.
Its first **44 CRT calls** match tick, call site and seed. The next missing call
is the outer renderer's `4ec858`, after tick 10074; no captured seeds, later
unit IDs or draw counts are injected. The later allocation at tick 10095 still
differs (retail 190, port 385). All **518 gameplay RNG calls** and all restored
movement tracks continue to match. Debug and Release produce 666 identical
replay events after metadata. `check_replay_construction.py` additionally
compares remaining work, HP and both resource pools with executable tick replay
at all five boundaries before that renderer gap, including newborn 806.

All 32 Debug / 26 Release CTests and 97 Python tests pass. The detmath guard and
available cross-compiler checks agree on `ab1ef54ae324bd0e` (ARM headers remain
unavailable). Two seed-1 multiplayer runs finish tick 1800 with four units,
no errors and identical hash `bbd325687d10850b`.
Construction completion notifications, retired-slot reuse, pool
exhaustion and outer-frame render cadence remain explicit unsupported paths;
these results do not establish whole-game parity. Earlier sections describe
historical integration stages and are superseded by this section where noted.

## Sacred-site income and saved type economy (2026-09-19, protocol 111)

World now reads feature `sacredsite` multipliers and applies the original
footprint coverage rule to units with uppercase `S` yard cells. Map feature
coordinates are footprint origins, not centres. A partially covered deposit
provides no income; a fully covered 1.5/2-strength stone scales the lodestone's
base production. The original x-then-z scan, including its last-site rule for
mixed coverage, matches 1,024 original-executable cases in Debug and Release.

Probe format 19 additionally restores initial type income/storage from the
catalogue. These values can differ from asset defaults (the captured VERMAGE
produces 15 rather than the file's 10). They are validated, hashed and used by
the economy. Resource snapshots are now included in diagnostic frame output.
The restored scene now produces the retail aggregates 47 and 31 mana/second
for players 0 and 1. All 518 gameplay draws and all restored movement tracks
still match; Debug and Release emit 549 identical events after metadata.
All 32 Debug / 26 Release CTests and 91 Python checks pass, along with the
detmath guard and the available cross-compiler golden checks
(`ab1ef54ae324bd0e`; ARM headers unavailable).
Two final seed-1 multiplayer runs reach tick 1800 with identical hash
`bbd325687d10850b`, four units and no errors. Construction-state decoding now
has six additional rejection/restore tests (97 Python tests total); its initial
snapshot contains jobs 338->389 and 827->700 and 47 empty emitters. That decoder
is preparatory and is not yet consumed by the World probe.
The current resource pool still follows the legacy World host; exact float
payment/allocation, active construction jobs, particle emitters and CRT-based
unit allocation remain integration work. The independent verified kernels
below do not by themselves close those gaps.

## Build-priority cache integration (2026-09-19, protocol 110)

Probe format 18 restores initial catalogue order, desired composition, cached
priorities, resource pools and thirty income/use samples. The decoder validates
owned/completed counts against the initial unit records and rejects missing,
corrupt or nonfinite state. World refreshes scores using current live unit and
construction counts. Income and spending advance the history; the pressure
calculation excludes the newest sample, as retail does. Cached scores, inputs
and resource history participate in the state hash. The economy host still
uses World's production/consumption rules: this is not yet full retail economy
or availability-classification parity.

All **518 gameplay RNG calls** in `reload-crt-state-01` now match tick, bound and
seed. All 22 restored ground movers, flight 821 and builder 281 still match all
31 boundaries. Debug and Release produce 549 identical events after metadata.
Construction/allocation state remains incomplete: matching gameplay draws and
restored movement does not prove complete scene equality.

The resource-history oracle passes 4,096 original-executable cases, including
sample rolling and strict pressure thresholds; the score oracle still passes
8,192 cases. Debug/Release and Clang agree. The 32 Debug / 26 Release CTests
and 90 Python tests pass. The detmath guard and available cross-compiler golden
checks pass (`ab1ef54ae324bd0e`; ARM headers remain unavailable).

Allocation follow-up: original tick execution confirms that builders 338 and
827 emit paired construction particles for sites 389 and 700 before the first
factory allocation. Those active stage-three jobs are not restored by the
movement importer. The `429af0` construction/unconjure accounting kernel now
passes 8,192 original-executable comparisons, with completion/death callbacks
controlled. It records requested use before affordability scaling and uses a
separate remaining-work fraction. It is not yet connected to those jobs.
The aggregate resource accounting (`51d837`) and end-of-tick clamp (`401270`)
now pass another 8,192 original comparisons: current storage is float, lifetime
produced/excess totals are double, the tick multiplier is decimal `0.03333333`,
and clamping occurs after construction work. Capacity overrides suppress income.
Unit eligibility, deposit multipliers and demand aggregation still need a World
host; passing these arithmetic checks does not establish economy integration.

The construction particle kernel (`4f1430`, `4f1310`, `4f12d0`) passes 4,096
original comparisons of admission, final CRT seed, position, vertical speed,
and expiry. Allocation and animation are controlled hosts; original CRT and
trig execute. The second random draw wraps a 32-bit left shift before signed
division, so a draw of 16384 produces speed 65536, not 196608. Slots expire on
the update after crossing the vertical bound. These particles are not yet wired
into World. Initial emitters for builders 338/827 and site 389 have capacity 5;
site 700 has capacity 40; all four lists are initially empty.
The remaining CRT work also includes render-cadence consumers; no later seeds,
captured draw counts or forced allocation identities are injected.

## Restored AI schedules and mobile scripts (2026-09-19, protocol 109)

World now restores the retail AI assignment countdown, all 100 squad slots,
deadlines, kinds, parameters and membership from the initial capture (probe
format 17). The dispatcher visits slots 0 through 98, refreshes membership once
per dispatch and preserves signed countdown and unsigned deadline behavior.
The scheduler matches 4,096 original-executable cases with controlled planner
callbacks. Complete empty base, strike, backup, VTOL and reserve handlers match
another 4,096 cases. Debug, Release and Clang agree. Restored World execution
rejects occupied planning and assignments that need unsupported selection;
it does not silently treat occupied groups as empty. Per-unit AI maintenance
at `40f330` remains unported. The normal command-emitting skirmish AI is separate.

Simulation COB execution now includes mobile and nonfactory units. Their saved
threads are restored and execute before unit movement, consuming the shared
gameplay RNG. All script state participates in the lockstep hash. The full
retail-data regression caught an unsupported PLAY_SOUND opcode; the interpreter
now preserves its stack/result behavior while World leaves audio to the client.
The controlled interpreter oracle passes 315 programs / 6,554 boundaries,
including sound return values. All 202 shipped unit scripts complete 10,000
updates through the lifecycle smoke harness. This does not establish parity
for every host query, movement/weapon notification or effect callback.

The save replay still matches all 22 ground movers, flight 821 and builder 281
at all 31 captured movement boundaries. Debug and Release produce 349 identical
events after metadata. The first 260 gameplay random inputs match; the next
bound mismatch is at tick 10090 inside build-priority refresh (`410b6a`, bound
40). Matching bounds alone can hide an earlier call-site difference. Factory
output still has ID 928 instead of 806; CRT consumers and allocation remain
unresolved. No later-frame state or random seeds are injected.

The isolated `410810` build-priority scorer now matches 8,192 original-executable
cases in Debug, Release and Clang, including every random bound, order and final
seed. The oracle controls availability classification and explicitly selects
53-bit x87 precision. It covers current/completed counts, desired composition,
cost penalties and resource pressure. It is **not yet connected to World**:
the importer and host must first supply accurate construction counts, catalogue
ordering and resource history. The verified scorer alone does not close the
full-replay gap.

Validation: all 31 Debug and 25 Release CTest cases pass, as do 85 Python tests.
The detmath guard passes and GCC/Clang O0/O2/O3 still produce the golden
`ab1ef54ae324bd0e`; ARM target headers are unavailable. Two seed-1 multiplayer
runs completed 1,800 ticks with no errors and matching `bbd325687d10850b` hashes.

## Ground responses, controller reset and corner pruning (2026-09-19, protocol 108)

The saved-game probe now restores signed ground-response modes, return positions,
move/fire orders and stage-three resumption. World can suspend a ground move for
an auxiliary attack, retain its original position and install the bounded return
leg before resuming. The mission kernel passes 4,096 controlled comparisons with
the original handler, including positive-mode leash checks and negative-mode
return geometry. Target scoring, retaliation eligibility and auxiliary combat
still use the current World host; this does not establish retail combat parity.

Replacing a ground controller now cancels its old search **and queues the new
one**, preserving the admission stamp when retail does. The former implementation
cancelled the old request without replacing it. Original-handler execution from
the captured state confirms the pending request and preserved stamp. Regression
tests require the replacement request and reject stale completions.

Clear-land steering scans now prune corners before selecting the steering aim.
They check eight neighboring footprints, the 32/144-pixel distance gates,
distances to the two segment lines and turn-angle/rate comparisons. Removing a
corner repeats the scan, and queued commands beyond the current leg are not
look-ahead points. Blocked neighboring footprints keep the corner. The complete
original land-scan routine, with controlled terrain and navigator callbacks,
matches 8,192 cases in Debug, Release and a Clang build. Floater-specific
look-ahead is still outside this integration.

Player bookkeeping now preserves its seven-tick clock and bound-30 draw for
human and AI players, after unit/search/production updates and before wind.
This is separate from AI planning. World continues answering availability/count
queries directly rather than retaining retail's delayed caches. The clock passes
4,096 original-executable cases including unsigned wrap and rebuild selection.
Probe format 16 restores these clocks from the initial memory snapshot, validates
their player owners and never borrows later-frame pointers or seeds.

In the CRT-state save fixture, **all 22 restored ground movers, flight 821 and
builder 281 match all 31 captured movement boundaries**. Debug and Release
produce 320 identical events after metadata; the first 212 gameplay random
calls match. The next missing call is AI group planning at tick 10084
(`40b332`, bound 30). Its group is empty in the initial snapshot and its
deadline is 10084; the owner runs a separate group scheduler, periodic group
assignment and unit maintenance. Those operations remain unported, so no
synthetic draw has been inserted to conceal the gap. The factory output still
has ID 928 instead of 806 because shared CRT consumers and slot allocation
remain unresolved. No later capture state is injected.

Validation: all 30 Debug and 24 Release CTest cases pass, along with 80 Python
tests. World regressions cover attack/resume, player clock hashing, replacement
searches and blocked/clear corner handling. The GCC/Clang O0/O2/O3 detmath golden
remains `ab1ef54ae324bd0e`; ARM target headers are unavailable. This golden checks
detmath, while the separate corner oracle checks the new geometry across compilers.
Two seed-1 Ulasem Arena multiplayer runs completed 1,800 ticks without errors
and both produced `6e6035e201bad8bf`.

## Factory scripts, VTOL landing and unit initialization (2026-09-19, protocol 107)

Factories with QueryBuildInfo now execute their COB threads in World. The
interpreter preserves the sixteen saved thread slots, separate CALL children,
integer sleeps, signal masks, statics and piece motion. Restored factory threads
run before production in each unit's update. Readiness gates creation, and
QueryBuildInfo selects the animated model piece used for the output position.
3DO origins retain their original integer precision; hierarchical rotations
round after each plane, and the loader's X/Z mirroring is applied explicitly.
COB and 3DO files now participate in the multiplayer gameplay-data hash and are
excluded from cosmetic-only overrides.

The controlled original-executable checks pass 312 thread programs / 6,654
boundaries, 6,000 motion updates, 100 complete saved Keep script updates and
10,030 integer rotations. The restored Keep signals ready on update five.
The full World replay now creates its first factory output at tick 10074 with
exact raw coordinates (531124978, 4196444, 479025836), heading 32768 and base
speed 50540. Spawn now consumes the heading-range and variation-phase draws
before randomized speed, including the zero-width heading draw. Explicit spawn
headings still override the drawn heading after those calls. The output's
identity remains 928 rather than 806: the shared CRT consumers and allocation
stream remain unresolved. Production work/payment/completion and yard-occupancy
handshakes remain partial. All eleven shipped factory scripts also pass 10,000
updates each across Create/Activate/StartBuilding/StopBuilding/Deactivate.
Probe format 14 additionally restores activation, readiness, yard-open and
bugger-off values from the initial entity pool; those values are not in the
saved VM blob. All 1,024 synthetic flag combinations match the original unit
getters. A counterfactual initially-ready factory creates its output on the
first update instead of waiting for a script signal that may already have
happened. Invalid flag values and malformed script hex are rejected.

World now runs the ordinary VTOL_Standby and VTOL_LandIfCan stages, with saved
ground/air mode, altitude, timers and events restored by probe format 13.
Landing searches belong to mission transitions instead of running every idle
tick. The controlled original-handler comparisons pass 1,024 standby cases,
1,024 candidate/orbit searches and 2,048 landing mission cases. These isolate
host inputs; boundary recovery, diversions, the full retail landability
predicate and display/weapon notifications remain outside this integration.

Debug and Release produce identical replay events after metadata and match the
first 129 gameplay random calls. Restored flight 821 and builder 281 still
match all 31 boundaries; restored ground 53 first differs at tick 10086.
The next missing draw is unit 927's Move_Ground timer at tick 10075
(402dc2, bound 5). Its response mode is -1, which the importer deliberately
rejects: stage two invokes target selection and retaliation, and stage three
handles returning from an auxiliary attack. Executing the captured handler
with its initial state and an advanced deadline confirms the no-target timer
path, but does not justify treating this mode as a plain move. No later
capture state is injected.

Validation: 30 Debug / 24 Release CTest cases pass, including a synthetic factory
that proves the script's readiness tick and subpixel build point control World.
All 79 Python tests pass. Clang-built script, piece-rotation and landing/standby
kernels also match the original-executable fixtures. The existing GCC/Clang
O0/O2/O3 detmath golden remains
ab1ef54ae324bd0e (ARM target headers are unavailable); this checks detmath, not
the full World across compilers.
Two seed-1 Ulasem Arena runs reached 1,800 ticks without errors and matched
8dff35899f8a8389. Those runs cover the new simulation state in the multiplayer hash;
they do not certify full retail replay parity.


## Navigator admission and script snapshots (2026-09-19, protocol 105)

The first extra gameplay-RNG call in the CRT capture belonged to navigator 927.
Retail stamped its pending request at admission on tick 10071; the port waited
until delivery and kept rolling against a zero stamp. `retailAdmitNavigator`
now implements `4e54a0`: require a pending request, compare the current unsigned
tick with stamp+15, and stamp immediately when selected. Selection can exhaust
the scheduler budget before initialization; resumed work does not re-admit.
Delivery and repeated-refusal requests preserve that stamp. This also corrects
the blocked-unit retry test: 120 searches per minute, not 1,800, with no random
retry delay in the repeated-refusal branch.

The admission oracle matches 4,072 original-executable cases, including overflow
and absent requests; the scheduler still matches 106 scenarios / 1,233 boundaries.
Debug and Release replay traces are identical and match the first 84 gameplay
random calls. The next missing call is VTOL_Standby for unit 308 at tick 10073
(`41769e`, bound 7). Factory output is still absent at tick 10074; flight 821
and builder 281 match all 31 captured boundaries, and ground 97 first differs
at tick 10083. No later capture state is injected.

The save decoder now associates `ScriptN` by the unit's saved index, not the
script blob's metadata (which retains the previous BANK selection). Keep 540
is saved index 37 and owns Script37. Script decoding preserves all sixteen
0xa4-byte thread records, clears each transient +20 pointer as the original
loader does, and preserves the header trailer. Static/piece counts are accepted
only from the owning COB. All 47 saved script headers/threads match executed
`56dc90..56dcd5`. This is decoder support, not execution of restored scripts in
World: factory readiness, piece transforms and shared CRT allocation remain
unfinished.

Validation: all targets rebuilt in both configurations; 29 Debug and 23 Release
CTest cases pass; 44 Python tests pass. The determinism guard and GCC/Clang
O0/O2/O3 agree on `ab1ef54ae324bd0e`; ARM headers are unavailable.
Two seed-1 Ulasem Arena multiplayer runs completed 1,800 ticks with no errors
and the same hash, `0fa4764a6a5f4037`.

## Wind, stationary guards and factory output (2026-09-19, protocol 104)

Wind now belongs to World, with its range loaded from the map, its state hashed,
and its renderer/COB delivery carried by the render snapshot. `retailwind.h`
implements `525170`: the strict deadline comparison, shared-RNG speed/bearing
choices, quantized direction vector, and CRT-generated interval. The comparison
is `abs(z) > x`, including signed X. The two branches wait 3..8 or 5..14 seconds.
The live engine uses a separate deterministic wind interval stream. Retail's
other CRT consumers remain unported, so this is not a claim of full CRT call
order or long-run wind deadline parity.

Stationary Guard_NoMove missions now own their attack-acquisition timer instead
of using the generic unit-ID acquisition cadence. Armed guards scan then sleep
7..21 ticks; unarmed guards sleep 150 ticks without consuming game RNG. Firing
still updates each tick. The existing target-selection model is retained.
`check_wind.py` and `check_stationary_guard.py` each match 4,000 original-executable
fixtures under GCC and Clang, including final RNG and mission state.

Probe format 11 restores wind only when the same initial boundary contains its
fields and CRT seed. Older captures explicitly omit wind rather than borrow
another recording's state. The runtime recorder now includes the wind fields.
The CRT capture (`reload-crt-state-01`, 31 frames) restores all four stationary
guards and advances the matching gameplay-RNG prefix to 67 calls. The next
difference is an extra path retry in the port at 10072. Flight 821 and builder
281 continue to match every captured frame; ground 97 still differs at 10083.

**Correction to the previous diagnosis:** the retail-only unit at 10074 is a
new Catapult (`VERPULT`) created by Keep 540, not a unit death. Its ID is 549 in
the runtime-only recording and 806 in the separate CRT recording. These are
different executions and their state must not be combined.

Factory output now exists as an unfinished unit while mana and work accumulate.
Completion releases that same unit and applies rally orders; it does not spawn
a second unit. Cancellation releases the site to decay, dead output is not
resurrected by construction, and the admitted site can finish at the unit cap.
Progress, the site attachment, construction state and queued types are hashed.
The price/duration tests now measure completed output separately from site
creation and still enforce the full price and build duration.

Exact factory replay remains incomplete: BuildingBuild stage 1 waits for COB
readiness (`4d4b90`, unit byte +12f bit 0), then stage 2 calls QueryBuildInfo and
creates/attaches the output (`401d40..401ff7`). Keep's Activate/RequestState/Go
chain opens the yard and sets COB unit value 5. The save contains its running
script threads: reader `56dc00` checks a 0xa48-byte header, four bytes per static,
and 0x6c bytes per piece; its sixteen thread records are 0xa4 bytes each.
Restoring these threads, the piece-derived spawn point, and the shared CRT
allocation stream is still required. The live engine's exit placement and
initial readiness continue to use its existing approximation.

Validation: all 29 Debug / 23 Release CTests and 43 capture/import Python tests
pass. Two seed-1 headless multiplayer runs reach tick 1800 with hash
`cc115a96915ae594` and no reported errors. The deterministic math golden agrees
across GCC/Clang; ARM target headers remain unavailable.

## Live flight movement and patrol (2026-09-19, protocol 103)

World now stores and hashes absolute flight altitude, three-dimensional velocity,
navigator destination/velocity/heading, and positional flight controllers.
Active flight uses the verified `retailflight.h` velocity calculation
(`4da803` through `4dad1a`) and navigator update (`524af0`). The previous
horizontal flight ramp is removed. Cruise height follows the maximum terrain
height in the surrounding 3x3 group of 128-pixel sectors.

Unarmed positional patrols use the retail mission dispatcher, randomized
overshoot/radius construction, timer waits and arrival events. Arrival signals
the mission while retaining movement on that tick; subsequent dispatch rotates
the patrol and replaces its controller. The repeated random expressions in
`41a47e..41a613` deliberately consume separate draws.

The version-10 diagnostic importer restores saved patrol mission/controller and
velocity state, with altitude from the initial capture boundary. Only initial
state is restored: no future positions or random draws are supplied. Private
`probe-flight-live-04` and `probe-flight-release-02` match unit 821's position
**including Y**, heading and speed for all 61 captured frames. Builder 281 also
matches all 61. The first 26 shared RNG draws now match; the next missing draw
is the world wind update (`52519e`, bound 4975) at tick 10070. The full scene
first differs when retail creates unit 549 at 10074; the importer does not yet
restore that factory production state. Ground unit 97
still first differs in heading at 10083.

Validation: 40,000 velocity and 40,000 navigator fixtures match the original
executable under GCC Release and Clang `-O2` with contraction disabled.
`check_flight_patrol.py` adds 4,000 original-handler comparisons of patrol
points, radii and final RNG state. Motion regressions cover live ascent,
absolute terrain height, hashed momentum, arrival boundaries and patrol
rotation. All 29 Debug and 23 Release CTests pass, as do 20 save/import/emulator
Python tests. The deterministic math golden hash agrees across GCC and Clang;
ARM cross-builds remain unavailable because target headers are missing. Two
seed-1 headless multiplayer runs reach tick 1800 without reported errors and
produce the same hash, `4b3c9e61f076a827`.

This is still a partial simulation replay. Armed/builder patrol diversions,
VTOL standby/landing, flight collision and some terrain-dependent speed flags
remain unported. The renderer still uses its existing visual altitude handling.
Combat, production and wind state are not fully restored. The full-memory
capture used to validate the isolated mover has a different RNG seed from the
runtime replay; their state is never combined.

## Simulation-first investigation (2026-09-18)

**Shared cached search integration (protocol 102):** PathService now uses the
verified singleton scheduler and resumable worker together. Allocated player
slots, including holes, consume the seven-unit scan cost. Cache preparation,
retry and finish follow that single worker's lifetime; cancellation also handles
an admission that exhausted its budget before initialization. World maintains
footprint-specific cached grade planes, ages bodies at the verified thresholds,
refreshes old footprints when bodies move and updates terrain changes in place.
Death and embarkation retire the body's search/cache contribution. Navigation
class replacement clears caches before their grid pointers become invalid.

The version-8 diagnostic importer restores initial entity traversal cursors,
body timestamps, route outcomes and captured packed grade planes. It now queues
restored requests **after** installing their mission radius; the old order
restoration queued searches with the default radius. Only initial state is
restored. No future routes, grades or RNG draws are injected.

Controller replacement reanchors direct two-point segments and expires old
route stamps. Route delivery clears the obstacle-scan deadline (`4e4f39`),
including empty deliveries, so the next mover update refreshes steering mode.
Ground arrival applies inactive braking on the arrival tick,
before the mission is retired. Regressions cover these transitions and cache
ownership through admission, cancellation and successive searches.

Private `probe-cached-singleton-08` matches all 21 restored ground movers through
10082; the next mismatch is unit 97's heading on 10083, after a different
randomized search retry installs a new route. Builder 281 still
matches all 61 frames. This remains a partial replay: flying unit 821 differs
on 10070 and its **VTOL_Patrol** handler `419db0` owns the first missing RNG
draw (`41a45c`, bound 10). Full Standby targeting, queued builder/production
state, flight behavior, raw terrain grading and simulation visibility are
still incomplete. Generated caches use the existing terrain walkability model;
restoring an initial captured cache does not certify cache generation.

Validation: all 29 Debug and 23 Release CTests pass, as do the standalone
Release pathblock and retailgap tests. The executable scheduler, whole-worker,
cached-footprint/body and grade/preparation oracles pass. Debug and Release
produce identical movement/RNG replay output (excluding diagnostic source-line
metadata). GCC/Clang determinism checks agree on the existing golden hash;
ARM checks remain unavailable without the target headers/libraries.

The protocol-101 and earlier results below are historical checkpoints.

Normal `queueBuild` commands now use the rectangular approach too, delay site
creation until arrival, and compute each queued approach when it becomes
active. A replacement build cancels the previous construction assignment.
The build ghost uses the site coordinates independently of the approach point.
The version-9 diagnostic importer restores builder 281's 54 queued stage-zero
MobileBuild orders without skipping unsupported predecessors. The private
`probe-queued-builder-01` keeps the same movement comparison as the cache
checkpoint above. The 63 orders on builders 338/827 remain unsupported because
their active stage-three construction state has not been restored. This queue
support does not certify production or script state.

**Standby and builder approach follow-up (protocol 101):** World now dispatches
the verified Standby stage/timer core in the per-unit update, interleaved with
movement. `check_standby.py` checks 512 real-handler fixtures with controlled
host callbacks. The host still uses World's existing target acquisition;
retail diversion, weapon initialization and complete target selection are not
certified. The version-6 partial importer restores ten captured Standby states
and explicitly disables unsupported idle handlers instead of initializing them
as Standby. These states participate in the lockstep hash.

Saved controller kind 6 is a rectangular perimeter, not a circle or ring.
Its bounds match the actual `4e3632..4e364a` loader. `RetailRectGoal` now also
implements the initial navigation point (`4e38e0`), verified in 2,000 cases.
PathService accepts rectangle goals and enumerates every perimeter cell;
regressions cover an obstructed initial destination with a reachable alternate
edge, including footprint-coordinate offsets. World preserves the owning build
order through route replacement, emits arrival on the perimeter and defers
placement without an extra movement step into the site. Inactive-navigator
braking follows the uncapped branch at `4d9aec`. This is **approach restoration,
not a full MobileBuild port**: subsequent construction still uses the existing
World host and queued retail builder missions are not restored.

Private `probe-builder-approach-04` restores builder 281's approach. Position,
heading, speed and base speed match all 61 frames, through tick 10129. The
follow-up fixed a double update: construction pivoted the builder, then the
ordinary mover pivoted/moved it again. Construction now holds the mover once
in range, uses the verified direction kernel, and starts the first pivot on
the deferred placement dispatch. A regression checks one pivot per tick and
stationary placement. The full unfiltered comparison first differs at the unsupported flying
unit 821 on 10070; the restored ground comparison still first differs at
Hunter 56 on 10071. The first 24 RNG calls match tick, bound and seed; the
next missing call is `41a45c`, bound 10. Matching these three fields alone does
not certify caller identity; restoring the builder also restores its formerly
missing `4e5482` draw within that prefix.

The route discrepancy has a concrete grade-input cause. Use the diagnostic
runner's optional `--grades-unit ID` to export its **initial** full search plane
in footprint-origin coordinates. `grade-dump-56-01.jsonl` shows World grades 5
at (172,73) and (171,72), while both captured cached planes grade them 6.
Unit 296 occupies that area and has mover `+28 == 10066`, only three ticks
before the snapshot. The real raw cached query ignores this fresh body;
World's live query treats it as traffic and introduces the earlier corner.
Additional terrain-border differences also exist. No later frame, captured
route, or replacement grade was injected into the simulation to hide them.

`retailgrade.h` now additionally ports `508cd0`'s ordered footprint/border
queries and `508b74..508bbe`'s ordinary-body timestamp grading.
`check_cached_footprint.py` verifies 3,000 ordered-border fixtures and 3,000
actual raw-rectangle body cases. Those helpers remain unconnected to World:
the shared cache lifecycle, raw terrain grading, simulation visibility, and
singleton scheduler must be integrated together. Replacing the live query
with an arbitrary traffic exemption would not reproduce that state machine.

Validation: all 29 Debug and 23 Release CTests pass; seven movement-importer
tests pass, and the determinism guard plus available GCC/Clang optimization
variants agree on `ab1ef54ae324bd0e`. ARM builds were skipped because the target
headers/libraries were unavailable. These checks do not establish retail replay
parity. The earlier protocol-100 results below are historical baselines.

**Search runs after movement (protocol 100):** Hunter 56's first mismatch was
caused by installing a newly completed route before the mover ran. Retail's
outer tick calls the unit update `51d3e0` at `5263aa`; that update dispatches
missions (`51e1e5`) and moves units (`51e215`). Only afterward does `526411`
call `4f6c70`, whose scheduler call is `4f6ca0 -> 416430`. World now runs its
search work and notification drain after the unit loop. A newly admitted
search therefore also reads the position and occupancy after movement.

The synthetic regression failed before this change: enabling a background
search changed the very first step compared with the same mover with search
disabled. It now passes and verifies that crossing a footprint-cell boundary
before admission changes the new route's anchor to the cell just entered.
The existing early-failure/controller-cancellation tests still pass.

In `probe-post-movement-search-release-01`, all 21 restored normal ground
missions match position, heading and speed on tick 10070. The next scoped
mismatch is Hunter 56's heading at 10071 (9301 versus retail 9300). Its new
route still differs: the first corner is (2784,1200) in the port and
(2688,1104) in retail, from the same (2896,1312) anchor. Both segments are
diagonal but have different lengths; the one-BAM difference is not evidence
of an angle-kernel bug when their steering inputs differ. Hunter 53 still
matches through 10085 and first differs at 10086.

The full comparison remains unfiltered: it now first differs at unit 281,
tick 10070 (x_raw 230876774 versus 230802475). This ZONHAND has MobileBuild
orders, which the partial importer does not restore. The report additionally
names `first_restored_ground_mismatch` so an unsupported builder does not hide
the remaining ground-order divergence. That scoped comparison still rejects
missing units and differing timelines; it does not replace the full check.

The second RNG draw is identified as unit 49's **Standby** handler `407770`,
stage 1, deadline 10070. Its `40786e` call draws `rand(7)`, followed by a
7-tick base delay and wait bit `0x20`; the real handler also performs diversion
and target/order checks. It is not a spare random draw to insert into the
ground handler. Running this captured mission in isolation stops at `41265a`
on uncaptured address `1a85f390`, before the RNG call (private evidence
`mission49-standby-01.json`). Standby behavior and complete per-unit dispatch
ordering remain unported here.

Validation: all targets build in Debug and Release; 29/29 Debug and 23/23
Release CTests pass. The changed pathblock and retailgap executables also pass
in Release. Six importer/comparison tests pass. Debug and Release produce
identical 751-record capture traces. Neither complete-state parity nor exact
route reconstruction in the restored World is established.


**World ground-order integration (protocol 99, resumed after crash):** plain
issued ground moves now own dispatcher state, an expanding radius and a unique
controller token on the goal order. Intermediate waypoints do not own missions;
route replacement preserves the goal state and the later command queue. World
dispatches these missions before search work, then drains search notifications
into the matching controller for the next dispatch. A reset cancels the old
search without deleting its existing navigator segment. Arrival sets event
`0x100`; the dispatcher retires that goal and initializes the next queued move.
The new mission fields and controller sequence participate in the lockstep hash.

The radius constructor (`4e2500`) matches 1,013 executable fixtures, including
signed overflow; the existing 3,000 controller-query fixtures still match.
World regressions cover captured-style failure/reset, route and queue retention,
arrival/next-goal dispatch, hashing, and an actual early search failure that
cancels the unfinished cost search on the following tick before delivery.

Failure-expanded goal circles change crowd stopping distances. The old fixed
arrival assertions conflated traffic scoring tolerance with mission completion.
The group tests now retain each mission's radius and check completion within
that circle (allowing cell quantization), while retaining collision checks.
All 16 repeatedly commanded units complete within their circles. The 24-unit
army has 19 units within the old 144px ring; all 24 reach their expanded
circles. The navigator-only cadence fixture explicitly disables the mission
handler so it continues to test retry behavior independently of goal completion.

The partial restore format now imports supported captured primary ground
mission stages, events, masks, deadlines, flags and radii. In private evidence
`probe-world-missions-01`, 21 missions are restored; response-mode -1 remains
unsupported. Hunter 53's position, heading and speed match through tick 10085
(16 steps), first differing at 10086 instead of 10070. The overall first motion
mismatch is now Hunter 56 at tick 10070: x_raw 190265069 versus retail 190262243.
The first RNG draw now matches; draw 2 still differs (port bound 5, retail 7).
These are initial-state restores, not later-frame corrections.

Validation: 29/29 Debug and 23/23 Release CTests pass; the additional World
early-notification regression passes in both builds. Five partial-importer
unit tests pass, as do all 147 captured ground-core oracle cases. The changed
pathblock and retailgap executables also pass directly in Release (they are not
registered in that build's CTest set). Debug and Release produce identical capture traces, including
all compared frames and RNG records (`probe-world-missions-release-01`).

This is **normal ground-core integration, not whole-game parity**. World still
uses insertion-order traversal and a ground-dispatch prepass rather than the
retail per-player entity-slot sequence with other handlers interleaved. Terrain
diversion, other response modes, other mission kinds, complete search-state
restoration and the singleton retail scheduler remain outside this integration.


**Primary dispatcher and normal ground-order core:** `retailmission.h` now
ports `4d8450` event filtering, unsigned deadline expiry, handler result
transitions, queue callbacks, random delay and the 100-handler limit. The
queue head and unit eligibility are rechecked after each handler. The queue
host still owns controller lifetime and idle-order creation. This is distinct
from the campaign `MissionScript` system.

`check_mission_dispatch.py` matches the executable in 327 controlled-handler
fixtures / 8,665 handler calls, including all return codes, queue rotation,
deadline wrap, stage-byte overflow and loop-limit cleanup. This isolates the
dispatcher; queue destruction and handlers are controlled callbacks on both
sides. `retailPlainGroundMove` additionally ports the normal response-mode-zero
states 0–2 of `402b00`, after the diversion checks. It preserves the `rand(10)`
flag check, `rand(5)+5` polling, and footprint-scaled radius growth on navigation
failure. Stage 3 explicitly requires a different handler.

`check_ground_missions.py` compares this C++ core and dispatcher with actual
captured retail handlers: 147 cases / 259 handler calls over 21 units match
stage, mask, deadline, pending events, flags, radius, unit events, RNG seed and
bounds, controller-reset count and active-search ownership. Inputs include the
captured state and explicitly counterfactual event, deadline, starting-stage
and 0x08000000 flag changes. Unit 927 has response mode -1 and is reported as
unsupported rather than silently treated as mode zero. Private evidence:
`ground-mission-core-02.json`. These isolated per-unit runs are not whole-tick
RNG ordering or complete-state parity.

**Early search notifications:** `PathService` now accepts an optional nonzero
controller identity and exposes `takeNotifications()`. Phase-1 notifications
are retained immediately even when phase 2 is still searching. Re-requests from
the same controller preserve progress; a different controller starts a fresh
search even at identical coordinates. Cancellation clears undelivered events.
Consumers must drain events and match their controller identity before the
next mission dispatch. Legacy requests with identity zero remain route-only.
Regression tests cover the pre-completion event, replacement and cancellation,
including the real search worker feeding the ground mission reset and thereby
cancelling its unfinished search before route delivery.

Both executable comparisons pass in Debug and Release. All targets rebuild;
29/29 Debug and 23/23 Release CTests pass, including the combined worker/order
regression. Release captured evidence is `ground-mission-core-release-01.json`.

**Earlier component-only boundary (superseded by the integration above):**
these checks originally left World on legacy requests and protocol 98. They
establish the isolated core, not the remaining diversion handlers, response
modes, singleton scheduler or per-player traversal.

**Order/search ownership fix (protocol 98):** explicit move, wait, ambush,
build, reclaim and repair replacements now retire the previous search before
installing their orders. `dropLeg` also cancels its departing request and resets
the route stamp, allowing the next leg to request its own route. Queuing an
order behind the current leg preserves the current search; cadence re-requests
within the same order still preserve search progress.

Before the fix, two executable World regressions failed: replacing a move with
a wait or ambush left a pending search whose delivery erased both the new
command and a queued move. Both now pass. Another regression retires an
unreachable leg while a low-budget search is unfinished, verifies cancellation,
and verifies that the next leg requests its own search. These are gameplay
ownership checks, not proof that World implements the retail mission dispatcher.

Validation: all targets rebuilt in Debug and Release; 28/28 Debug and 22/22
Release CTests pass, as do the expanded pathblock regressions and four
emureload allocation tests. GCC and Clang at O0/O2/O3 agree on the deterministic
math hash `ab1ef54ae324bd0e`; ARM builds were skipped for missing build support.
Two fresh seed-1 headless Ulasem Arena runs reach tick 1800 without errors and
agree on state hash `5db003d24943d6f9`. This smoke test has four units and does
not replace the Hunter-group capture comparison.

`emureload.py --mission` now reports individual dispatcher handler entries and
returns: consumed events, stages, controller/search pointers, deadline/mask and
RNG call indices. In `mission53-lifecycle-01.json`, the handler stages are
2 -> 0 -> 1. Stage 2 consumes 0x2000 and returns reset; stage 0 replaces the
controller and cancels the search; stage 1 schedules `rand(5)+5` ticks and
returns advance to state 2 with wait mask 0x2701. Counterfactual runs in
`mission53-event-isolation-01.json` independently remove or set the pending
event: zero or 0x1000 dispatches no handler and preserves the active search,
whereas 0x2000 or 0x3000 reproduces the reset. These runs start from the baseline
seed, not the real per-unit handler-entry seed after preceding units execute.
Hunter 40 follows the same state sequence but preserves Hunter 53's active
search (`mission40-lifecycle-01.json`): cancellation is scoped to the replaced
controller. Hunter 53's captured navigator bytes change only at the controller
pointer; its existing route is retained during this restart.

The post-fix partial restore (`probe-order-lifecycle-01`) still first differs
at Hunter 53, tick 10070, and at the first game RNG call. The importer still
does not restore or execute these mission events; the ownership fix must not
be presented as resolving that discrepancy. The older runtime-state capture
also lacks the game-memory snapshot required for full tick-body emulation;
isolated mission execution remains a bounded diagnostic on this fixture.

Renderer replay is no longer the prerequisite for movement work. No comparison
has established different pathfinding algorithms across retail's three rendering
backends. Rendering and entity-slot allocation do share CRT random-stream usage,
which matters to whole-process replay, but that does not establish a cause for
the movement failures. The OS/Vulkan work below is diagnostic infrastructure,
not movement parity.

The current partial C++ restore was compared again with
`reload-runtime-state-03.json`; results are in the ignored
`probe-direct-current-01` directory beside that capture. Initial compared motion
fields match. At tick 10070, Hunter 53 remains at x_raw 188641432 in the port,
while retail moves to 188567463. The port replaces the restored direct segment
with a newly completed detour and sets speed to zero; retail retains its route
and speed 128461. The first RNG call also differs: retail uses bound 5 at caller
`402dc2`, while the port uses bound 120, starting from the same game seed.

An independent invocation of the actual retail mission dispatcher `4d8450`
on captured Hunter 53 reproduces that first bound-5 call and clears the active
search pointer from 367411384 to zero, replacing the movement controller.
`mission53-direct-refocus-01.json` records no emulation error or unmapped access.
This isolated execution uses substitute allocations and is not a complete-state
match. It supports investigating mission processing/search cancellation before
route delivery, without executing a renderer.

The partial importer currently retains the request created by `world.order()`
when captured route flags contain bit 2. World runs its path service before
movement and does not restore the retail mission queue/controller lifecycle.
Do not treat the resulting route replacement as proof of a search-kernel defect,
or fix the comparison by unconditionally discarding pending searches. The next
integration must distinguish restore limitations from live order handling and
implement the verified cancellation/notification lifecycle.

## OS and Vulkan return observations (2026-09-18)

**Recorder recovery update:** live software-probe capture was disabled again when the
concurrent Wine forced-debugger-kill test restored the probe byte but the
fixture exited 4 instead of 42. Byte restoration alone is insufficient evidence
of recovery. Wine exception logging subsequently identified a surviving
hardware-breakpoint trap (`EXCEPTION_SINGLE_STEP` at a reserved probe site).
`probe_guard.py` now snapshots debug registers before installing any probes
and restores them per thread during external recovery. Both the ELF kill test
and concurrent Wine kill test pass with the final guard validation, including
verified `cc` removal and exit 42 (`/tmp/tak-cleanup-hardware-guard01.json`,
`/tmp/tak-wine-probe-threads-recovery08.json`). Combined capture is re-enabled.
This reproduces the fixture failure; it does not prove the cause of the earlier
retail exit. The third guarded
attempt (`next-guarded-returns-03.json`) reported tick 10237 twice at the correct
instruction address, despite recording an intervening native entry at 10238.
It restored its probes and detached immediately. Observation processing has
now moved completely out of Python breakpoint callbacks into the outer stop
event loop. A concurrent actual-Wine-dispatcher fixture passes 803 matched
calls in this mode (`/tmp/tak-wine-native-loop-threads01.json`).

The new recorder then completed `next-loop-returns-01.json`: boundaries
10303–10309, normal verified probe cleanup and detach, and a strictly validated
call tree containing 1,141 syscalls, 126 Unix calls and 49 callbacks. All
callbacks have captured syscall parents; maximum nesting depth is five. This
clears the capture-order blocker, not the callback-replay or C++ parity work.
Historical successful isolated recovery results below predate the concurrent
fixture.

`next-syscall-state-01.json` captured 1,822 completed OS calls and 41 clock
results. `next-native-returns-01.json` starts at tick 11,727 and captures 1,256
OS calls, 14 Vulkan bridge calls and 30 clocks. Both detached successfully.

Wine i386's normal syscall exit at `f7cb7471` and Unix bridge exit at
`f7cb7576` both read the saved return address at syscall-frame `+8`. A single
hardware **read watchpoint** on that field observes both paths without code
patches, retaining the tick/game-RNG/CRT breakpoints. The frame is obtained
from the verified simulation TEB (`3e2000`) at `+218`; it was `77fc80` in this
process. Completed-call records contain bounded parameter/pointee samples,
not a reconstructed OS or GPU checkpoint. Replay must identify the API and
copy only its defined output fields.

`emurender.py` now supports recorded cursor, monitor, display-mode and Vulkan
format-capability outputs. Cursor writes only the eight-byte POINT. Monitor
replay preserves name-buffer padding; display-mode replay requires the device
name to alias a preceding captured monitor output. Vulkan format replay checks
the entire output extension chain before writing and preserves relocated
`sType`/`pNext` inputs. `emuvulkan.py` adds the observed image-format, create,
memory-requirements, bind and destroy API contracts. Resource handles are
recorded external outputs; no GPU work or worker concurrency is reproduced.
`--recorded-unmap` is diagnostic: it checks recorded calls/results but retains
inert pages, so shared-view lifetime/aliasing is still unverified.

The recorder now explicitly captures bounded Vulkan image queue-family and
view-format arrays as well as `pNext` chains. `next-native-returns-02.json`
cleared that dependency; replay then reached an uncaptured newly mapped Wine
view at tick 11,848. `--map-view-return` now captures successful
`NtMapViewOfSection` outputs and returned bytes, bounded to 32 MiB per view and
128 MiB per capture, rejecting overlap with emulator scratch memory.
`next-mapped-view-01.json` contains six such outputs (786,432 bytes total).

`emu-mapped-view01.json` matches nine tick boundaries (11,936–11,944), then
stops on cursor-query order at tick 11,945: the partial render harness omitted
the intervening calls from `58a050` and `5b7385`. Its two render batches also
assumed fixed cadence; captured batch sizes change from five to two.
`--whole-frame` now offers continuation from the captured stack through the
actual outer loop instead of manually sequencing render fragments. Its native
dependencies remain subject to strict recorded-input checks.

Mapped-view replay restores observed bytes and defined outputs; it does not
reconstruct shared alias coherence, view lifetime, or pre-call in/out values.
These are fixture improvements, not evidence of end-to-end engine parity.
The C++ gameplay integration remains at version 97; the grade helpers below
remain unwired.

The full-loop probe `emu-whole-frame03.json` reaches `NtUserDispatchMessage`
(`7b4e093c`) at tick 11,940 after consuming all three cursor queries for that
frame. The recorded message is `WM_PAINT`. The local Wine implementation at
`win32u.so!NtUserDispatchMessage+218` enters `KeUserModeCallback` with callback
ID 4 and a 44-byte payload; returning the recorded syscall result alone would
omit that callback's execution. The return-only capture cannot reconstruct it.

`emuwindow.py` supports the observed DPI/window rectangle queries and bounded
scalar/message inputs. It rejects unsupported message payloads; it does not
substitute for dispatch callbacks or kernel queue side effects. Synthetic
tests check relocated rectangle output, output bounds, DPI validation, and
exact translated-message input matching.

The optional `capture_reload.py --user-callbacks ENTRY RETURN` observes
`KiUserCallbackDispatcher` and `NtCallbackReturn` and uses software probes at
both native return sites to include nested calls. The four temporary Wine
software probes are installed **after** the baseline memory capture, then
removed on detach. Callback payloads are bounded at 64 KiB each and 16 MiB
total. `native_order` records callback/OS/Unix observations in one stream.
`next-user-callbacks-01.json` failed the initial signature-length preflight and
detached without capturing; that check has been corrected.
`next-user-callbacks-02.json` captured ticks 12,012–12,022 and 41 balanced
callback entries/returns, including nested messages. `--native-entries`
adds the two dispatcher entry probes, preserving pre-call arguments and
explicit callback parentage instead of inferring parents from return order.

`callbacktrace.py` validates every entry/return pair, arguments, chronological
nesting and event coverage. It rejects `next-native-entries-01.json`: entries
at native-order 224 and 493 lack matching observations before later calls,
and ordinal 624 duplicates a return. The trace must not be silently repaired
or used as proof of parity. The recorder now disables displaced stepping and
uses `scheduler-locking step`, with probes restricted to the captured thread,
to test whether software breakpoint step-over races caused the omissions.
`next-native-entries-02.json` also fails validation (an orphan duplicate return
at native-order 12), despite completing and detaching at tick 12,198. The
thread-stepping change did not solve it. Static inspection shows a separate
Wine dispatcher context-restoration path as well as the normal return path;
the shared exit is not a sufficient API completion marker.

With `--native-entries`, the recorder now follows the active call's actual
user-code return address using the fourth hardware breakpoint. The four
software probes observe the two dispatcher entries and callback entry/return;
they no longer observe shared exits. Nested calls move the hardware probe to
the child and restore the parent on completion. Each completion identifies
its entry and validates the expected user stack. Repeated dispatcher entries
on the same still-active user stack are explicitly recorded as restarts,
validated against that original entry, rather than silently discarded.
The `next-user-returns-01.json` attempt **crashed GDB itself** after capturing
the baseline at tick 12,268; no report was written. Log:
`/tmp/tak-user-returns01.log`. Retail PID 1899727 remained stopped and detached,
with four software breakpoint bytes still installed. A separate static GDB
recovery restored and verified the original first bytes at `7bf02170`,
`7befd134`, `f7cb72e4`, and `f7cb751c`, then detached. Retail exited when
resumed with SIGCONT; the running session was not recovered.

Both Wine software-probe modes were initially rejected before attachment,
pending isolated debugger tests and external probe recovery.
The exact internal GDB fault and retail's final exit cause are not yet proven;
the attempt involved dynamically replacing hardware breakpoints from Python
breakpoint callbacks. Earlier captures and the on-disk save remain available.

An isolated 32-bit assembly reproducer now exists at
`tools/re/check_native_return_probe.py`. It reserves three hardware slots,
uses a software dispatcher-entry probe and changes the fourth hardware slot
to follow nested user returns. On GDB 17.2-2.fc44, `--mode callback` reproduces
the debugger SIGSEGV without retail (`/tmp/tak-isolated-return-callback01.log`).
Both `--mode loop` and `--mode deferred` complete 400 entries and 400 returns,
nesting depth four, with zero open calls and the fixture's expected exit 42.
The deferred result is `/tmp/tak-isolated-return-deferred01.log`.

The recorder now queues probe installation/replacement in its Python
breakpoint callbacks, returns to the outer `continue` loop, and only then
creates or deletes breakpoints. This also moves initial software probe
installation out of the baseline callback.

`check_probe_cleanup.py` adds an independently launched ELF target with
normal, Python-exception and forced-debugger-kill cases. The kill case verifies
an actual `cc` byte through `/proc/PID/mem`, stops the disposable target and
kills GDB before cleanup. A separate debugger restores the instruction prefix;
the target subsequently exits with its expected code 42.
`check_wine_return_probe.py` runs a separate PE fixture under the same Proton
Wine binary in a fresh private prefix. It exercises 800 nested dispatcher
entries/returns, including Windows API callbacks and performance-counter
queries. Normal, exception and forced-kill cases pass. This is a synthetic
fixture, not a successful retail native-dispatch trace.

The recorder and fault tests now share `probe_guard.py`: original prefixes
are persisted before probe installation, recovery checks process start time
and every prefix before writing, and normal cleanup marks verified restoration.
Malformed records and overlapping probes are rejected before attachment.
Combined callback/native-entry capture is re-enabled with this supervisor.
The stopped-target forced-kill test does not establish recovery from every
possible asynchronous debugger failure. Native module addresses must be
rediscovered after each restart.

`next-guarded-returns-01.json` exercised the guarded recorder on reloaded PID
2358271. It captured boundaries 10166–10172, then timed out while observing
window messages at tick 10173. All four original probe prefixes were verified
restored, GDB detached, and retail remained readable. The trace is rejected:
Unix outputs were sampled through a parameter slot overwritten by Wine's
return address, callback return observations include duplicates, and a native
return-stack mismatch was recorded. Pending debugger actions also allowed
capture to continue after that error. The recorder now uses entry-saved Unix
arguments, stops immediately on errors, and returns callback observations to
the explicit outer continuation loop. These changes still need a fresh live
trace; the rejected capture is not replay/parity evidence.

`next-guarded-returns-02.json` stopped immediately after baseline tick 10173
with a nonsequential-tick error and one native entry. Normal probe restoration
and detach succeeded. Actual-PC assertions and detailed tick/stack errors now
distinguish debugger event attribution from simulation discontinuity.
The isolated fixture now actively exercises the three reserved hardware
probes. Its Wine `--native-dispatcher` mode traces the real NT dispatcher,
with a second thread issuing competing calls and main-thread-scoped probes:
803 entries and returns match, all 300 reserved-probe hits occur, and exit 42
is preserved (`/tmp/tak-wine-native-dispatch-threads01.json`). This has not
reproduced retail's nonsequential-tick failure.

Callback replay is still required; the nine-boundary partial-render result
is not full-loop or C++ parity. Current targeted tests: 21 capture, 19 native
replay, three recovery validation and three call-tree validation tests pass. No gameplay changes were made
during this native-fixture work.

## Cached passability ports and shared-buffer replay (2026-09-18)

`retailgrade.h` now reproduces four additional retail routines, verified by
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

These helpers are **not yet connected to World's grade source**. Raw footprint
grading, grid sharing/invalidation, and the retail mover timestamp still need
integration. The timestamp is mover `+28`, written by `4e1e60` and `5066f0`;
it is distinct from mover `+30`. `4e1e60` refreshes initialized grids when the
old stamp predates their recent threshold, excluding their current requester.

The synchronized `next-shared-clock-state-02.json` capture adds Wine shared
texture buffers (`/memfd:wine-mapping`), not just NVIDIA mappings. Shared bytes
total 320,712,704; the recorder's checked limit is 384 MiB. The earlier failure
at `02b0d2a8` was valid Glide code writing an omitted shared texture buffer at
`20cb0000`, not a corrupted return address. Replay now reaches two recorded
clock inputs in order. Native GPU destruction (`winevulkan!vkDestroyImage`)
and display queries (`user32!MonitorFromPoint`) still require external support.
Skipping destruction is an explicitly marked diagnostic option, not parity.

The forced render also emitted an extra CRT draw at tick 11,527. Omitting it
misses two expected draws, so neither variation is exact. The replay now stops
at the first incorrect CRT call and executes caller-side camera/visibility
setup before attempting a frame; work on actual frame cadence continues.

## Gameplay search phases and navigator origin (2026-09-18, version 97)

`PathService` now dispatches `RetailSearchWorker` instead of `PathSearch`'s
legacy tracer/Dijkstra fallback. It supplies heading (converted by a half
turn), type/terrain-adjusted costs, traffic radius and footprint-origin
coordinates, translating returned cells back to the existing World interface.
Initialization/retry refreshes the current unit position, heading and costs.
Point-goal queries use the circle controller with the ordinary four-pixel
tolerance. Initialization's 500-work charge is included in service accounting.

This is **phase integration, not scheduler or whole-game parity**. Admission
still uses the existing 12-worker wrapper. Grid preparation is still the live
World grade source, without retail's cached directional planes/retry refresh.
General mission/controller selection, notification semantics, heavy-slope
cost selection and slot-driven singleton scheduling remain outstanding.

The World route installer now treats the first returned point as the segment
origin, and steers toward the second point. Retail `4e5150` checks arrival at
point 1; `4e5100` supplies the origin/end/look-ahead triple. Treating point 0
as a destination made replans turn back toward the start cell. `replaceLeg`
preserves an explicitly supplied first segment origin. Empty completion also
preserves its actual failure classification rather than forcing failure.

The integration tests exposed a separate benchmark placement mismatch:
`snapSpawn` checked a center-indexed footprint but always emitted `cell*16+8`.
Even footprints require `cell*16`. It now uses `footprintWaypoint`, and initial
cell lookup uses `footprintCell`. The benchmark terrain check passes again.
All **28 Debug and 22 Release CTests pass** with network version **97**.

### Renderer replay progress and limits

`next-device-state-01.json` captured 76,447,744 bytes of shared NVIDIA CPU
mappings in 150 bounded chunks, in addition to game/native image memory.
Single-unit rendering now returns. Scene traversal also returns and executes
six real CRT draws when diagnostic thread-wakeup-success and released-key
substitutions are enabled. This is not replay of the GPU worker or whole-frame
cadence. The native exports were identified in the live process:
`7befd754` = `NtAlertThreadByThreadId`, `7b4e0d8c` =
`NtUserGetAsyncKeyState`, `7befd3f4` = `NtQueryPerformanceCounter`.

`next-clock-state-01.json` starts at tick 11,327 and contains 11 boundaries,
49 CRT calls and 31 completed external performance-counter results. The
recorder observes the counter's RET-8 site using the hardware slot otherwise
used for steering calls (baseline route/mover state remains captured).
`emurender.CapturedClock` feeds those external inputs in strict tick/caller
order and fails on exhaustion or mismatched output arguments. It does not
replace simulation state or invent RNG draws. The diagnostic GDT restores FS
to the captured TEB and sets flat 32-bit code/data/stack segments.

The full captured-tick renderer attempt matches four tick boundaries, then
fails in native graphics execution at tick 11,332 before consuming any clock
input (`emu-clock-ticks04.json`). Its bounded native instruction history is
retained for diagnosis. Renderer/native worker execution and World replay
are still incomplete; the earlier claim of an uncapturable NVIDIA buffer was
too strong—the CPU mappings were readable, and the remaining dependency is
broader native execution state.

## Native baseline, allocation and goal controllers (2026-09-18)

The expanded recorder completed `next-native-state-01.json` at tick 10,722,
with 11 frame boundaries and 46 CRT calls. `--next-tick` captures without a
reload; `--native-memory` adds readable private PE image mappings, including
writable globals, at the same stopped boundary as game memory. This baseline
contains 580,767,744 game-memory bytes and 38,785,024 native-image bytes.
The save comparison differs because this is a later live state, not a reload.

The renderer diagnostic now passes the previously missing mutable D3D9
interface. It stops in native `msvcrt.dll` at `7b82a38d`, copying into address
`ca09a100`, whose live mapping is shared `/dev/nvidia0` memory. These shared
device mappings are excluded from the recorder. `emu-render-native01.json`
records that dependency before any observed CRT call. Executing captured
native code alone therefore does not establish rendering or CRT ordering
parity; device behavior and render cadence remain unresolved.

`retailrng.h` now implements the separate CRT generator and selection of the
ranked free entity slot. `check_entity_slots.py` executes the retail allocation
fragment and real CRT instructions for 160 fixtures. It compares both slot and
updated seed, including full pools (no draw), holes and irrelevant flag bits.
Slot lifetime and the shared render/allocation CRT stream still need a World
adapter; this helper alone does not change gameplay identity.

`retailgoal.h` ports acceptance, distance and ordered cell enumeration for
circle, rectangle and ring controllers. `check_goal_queries.py` compares all
three operations in 3,000 executable fixtures, using the real vector code and
varying entity footprints. Rectangles accept only their perimeter and use a
16:6 distance metric. Circles enumerate only their center. Rings use world
distance for the inner radius and preserve all 16 ordered samples, including
duplicates. These controller components are not yet connected to World.

The remaining gameplay integration is still substantive: retail slot
allocation/lifetime, mission/controller selection and notification handling,
retry grid preparation, and replacing the legacy path service with the
verified worker. No new gameplay/network version is claimed by these changes.

## Initialized search worker connected to scheduler (2026-09-18)

`RetailSearchAttempt::initialize` now ports the controller-facing part of
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

This closes the missing search lifecycle, but **does not connect the worker to
World yet**. The current World still has global sequential unit identities,
flattened order/waypoint queues and its legacy grade source. Retail slot
identity, controller goal geometry/notifications and retry grid semantics
must be supplied by that adapter; synthesizing those from global unit IDs or
an exact-point goal for every order would not preserve the checked behavior.
No gameplay/network version change was made by this worker addition.

The native renderer diagnostic also advanced using 7,766,016 bytes of live
read-only D3D9 module pages (ignored `d3d9-readonly-module.json`). Entity 17
now executes the actual D3D9 entry past the previous missing interface table,
but stops at `799e1ed0` reading writable module global `79d90bd0`; the following
instructions dereference that interface and jump through vtable offset 0x0c.
That mutable native interface was not captured at the baseline tick. Current
live state was inspected only to identify the mapping, not inserted into the
old replay. `emu-render-unit05.json` records the new dependency, still before
any CRT draw. Native runtime state and render cadence remain unverified.

## Reachability, handoff and route reconstruction verified (2026-09-18)

Network version **96** corrects the gameplay direction selector: retail's
`415040` default-hint threshold is **24:10**, not 2:1. For example, delta
(12,5) stays diagonal; (13,5) becomes cardinal. All 6,561 vectors in [-40,40]
match executable retail. This bounded fix is active in `PathSearch`.

The separate `retailtrace.h` port now includes:

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
Debug and Release clients/servers were rebuilt; 28 Debug and 22 Release CTests
pass. The new search attempt is **not yet the gameplay service**. Remaining
adapter work includes controller goals/notifications, retry grid preparation,
per-player entity-slot identity and singleton scheduling. Direction correction
alone does not establish that sticking/stacking is resolved.

The renderer dependency probe also advanced. Live module exports identify
`7bf0c390` as `ntdll!RtlAllocateHeap`, and `797ce770` as
`d3d9!DebugSetMute` (a bare RET in this installed build). The bounded allocation
substitute and verified no-op get entity 17 as far as Glide `02b1aa8d`, where
it reads the uncaptured D3D9 interface table at `79e3f7a8`. No CRT draw has
occurred by this point. `probe_captured_render.py` reproduces this diagnostic;
`emu-render-unit03.json` and `04.json` remain with the ignored captures.
Native D3D9 execution and outer render cadence still block full RNG replay.
These diagnostic substitutes are not a validated graphics model or proof of
RNG parity; another identical reload does not resolve them.

## Origin-relative traffic flags restored in gameplay (2026-09-18)

Network version **95** corrects two traffic-reporting errors in the existing
gameplay path service. Retail's `0x414951` and `0x414563` compare traffic cells
with search +30/+32, the **start**, not the destination. Once the nearby-traffic
bit is set, another traffic cell sets the other-traffic bit even if it is also
nearby. `check_route_traffic.py` executes both retail branches on five fixtures,
including the strict radius boundary and repeated nearby traffic.

The port previously compared against the goal, and successful legacy Dijkstra
routes did not report traffic at all. `PathSearch` now measures from the start,
preserves traffic grades during cost search, and accumulates the delivery flags
in backtracking order. These flags select the mover's re-request cadence.
Seven assertions in the new corridor regressions failed before the correction
and pass afterward. The public `goalCrowded` field retains its legacy name but
now documents that it means traffic near the origin.

Debug and Release clients/servers were rebuilt. All **27 Debug** and **21
Release** CTest checks pass (`/tmp/tak-traffic-{debug,release}-tests.log`);
the 29 Python unit tests also pass. Scheduler executable comparisons pass
against both build configurations.

This is a bounded correction to the current service, not an integration of the
new scheduler or a claim that crowding/stacking is solved. The legacy tracer,
cost route selection and phase timing still differ from retail.

## Scheduler admission and cost-slice ports (2026-09-18)

`src/sim/retailscheduler.h` now implements the singleton admission/accounting
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
The existing 240 cost profiles and 5,446 individual-pop comparisons still pass.

The exhausted-heap executable test in `emuscheduler.py` also corrects the older
relocation claim below: the scheduler retries three times (four initializations
total), queries a fixed 9x9 neighborhood, then calls `4e4ea0(0,0)` to clear the
route and completes the request. That branch does not relocate the goal or
seed another search. The neighborhood is centered on search +30/+32 (the
start). It is not a widening destination scan.

These components are **not yet connected to the gameplay PathService**. They
do not change the network version or fix the remaining movement behavior by
themselves. Integration still needs retail phase-0 initialization, tracer
handoff (including positive partial-distance results), reconstruction and
controller notifications, retry-driven grid preparation, and real per-player
entity-slot identity. Using the legacy trace outcome or global unit-ID order
as substitutes would lose the verified behavior. Whole-game replay remains
separately blocked on renderer/native dependencies and render cadence.

## Live renderer caller confirmed (2026-09-18)

`crt-live-stack03.jsonl` captured eight calls at `0x4ec858` and detached at
its event limit. All eight were at simulation tick 10274. The recorded frame
chain confirms this path (call sites shown, rather than return addresses):

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
cannot be inferred from simulation ticks alone. A fixed one-render-per-batch
substitution has not been validated. Entity traversal in `4fc5b0` uses render
row bins built from the entity list and camera position, with flag and visual
object checks before calling `4ee700`; unit-ID order is not a substitute.

The narrower offline diagnostic `emu-render-unit02.json` calls `4ee700` for
the first eligible captured entity (17), with the same single-thread critical
section substitution used by the tick checker. It enters Glide and stops
before any CRT call, at uncaptured native code `0x7bf0c390`, return address
`0x02bad9b6`. Disassembly of the captured Glide code shows an allocation-shaped
three-argument import call there. This is a diagnostic dependency result, not
a render-order or parity test. No graphics calls or RNG draws were fabricated.

The current offline replay blocker is native renderer/OS behavior plus actual
outer-frame cadence. Another identical save reload alone does not supply an
executable model of those dependencies. Full replay needs those dependencies
modeled and validated, or a separately validated extraction of the RNG-relevant
render work and its scheduling. The engine's weighted search integration and
whole-game RNG ordering remain unfinished.

## CRT batch boundary and renderer dependency (2026-09-18)

`reload-crt-state-01` succeeded: **31 boundaries, 518 gameplay RNG calls and 210
CRT calls**. The boundary registers show five-tick batches: EDI=5 and ESI counts
4,3,2,1,0. The first missing CRT draws occur after tick 10074 at return address
`0x4ec858`; 85 of the 210 observed CRT calls come from this site. Other callers
are particle spawning (`0x4f1467`, `0x4f14a2`), `0x525248`, and entity-slot
selection (`0x5121dd`).

The offline checker now executes the actual batch epilogue as well as the tick
body. That does **not** supply the missing `0x4ec858` calls: they belong to the
outer renderer path. Attempting `0x4fbb90(1,1)` reaches `glide3x.dll` (captured
module base `0x02b00000`) and faults at `0x02b0a2e5` on an uncaptured read at
`0x79e3f874`. The render attempt stops and reports this dependency; it does not
replace those draws with a fabricated count or reset the RNG to later captures.
A bounded live stack trace of the render caller returned no events during its
20-second observation window. Further live inspection needs that path running.

The ground-mover constructor at `0x4dc6f6` masks flags with `0xe001` before
setting bit 0, preserving allocator contents in the upper three bits. The
checker compares initialized low 13 flag bits for newly created units, and
reports that exclusion alongside the existing +2c allocator-byte exclusion.
This is a semantic comparison with explicit limits, not byte-for-byte state
equality or a C++ engine parity claim. The weighted kernel remains unintegrated.

## Failed-route correction and executable tick replay (2026-09-18)

Network version **94** fixes an engine route-reconstruction error: `buildRouteTo`
appended the requested goal even when reconstructing failure at `bestCell`.
That added an untraced segment through the separating obstacle; a boxed-in start
also received a fabricated straight segment. Reconstruction now emits only
backtracked points. Three new checks failed before the fix and pass afterward:
reachable failure endpoint, no segment across a sealed wall, and no route when
boxed in. All 26 debug and 20 release CTest checks pass.

`reload-game-memory-01` captured 61 frames, 527 gameplay RNG calls, 1,251 steering
events and 507,596,800 bytes of bounded initial private memory. This allows the
previously blocked idle mission to execute. `emureload.py` maps contiguous page
ranges, implements bounded allocation/reallocation for known extents, and rejects
unknown original reallocation sizes rather than guessing how much to copy.

`check_captured_tick.py` executes the retail tick body (`0x526351..0x526515`)
from that single initial state. **25 consecutive ticks and 279 gameplay RNG calls
match**, including unit identities, raw positions/headings, ground speeds and
selected movement/search fields. This validates a limited executable replay,
**not the C++ engine**. It assumes released keys, single-threaded critical
sections and one-tick batch arguments; presentation/OS work outside that body
is omitted. Newly allocated mover +2c bytes are excluded because construction
can leave allocator contents there. No future positions or routes are injected.

The CRT RNG is **not visual-only**: unit creation at `0x5121d8` uses `rand()` to
select a free entity slot. Assumed CRT seeds produced different new IDs at tick
10074. The actual initial thread record is present in the capture and can be
identified by TEB self-pointer, stack bounds, TLS index 23 and matching thread
ID. Restoring that record reproduces new unit 991. At tick 10095 the replay's
next new ID is 77 versus retail 499, while gameplay RNG calls still match.
The missing CRT-call ordering (potentially outside the tick body) and batch
context must be measured before claiming continued parity.

The recorder's `--crt` option uses the fourth hardware breakpoint to observe
simulation-thread CRT calls, captures its seed at each boundary, and includes
tick-batch registers. It requires `--game-memory`, checks the executable bytes,
bounds event counts and detaches in `finally`. All 29 Python tests pass. The
verified weighted search kernel is still not integrated into PathService.

## Scheduler and mission capture (2026-09-18)

`reload-runtime-state-03` succeeded: **61 frames, 535 RNG calls, 1,251 steering
events**, with the visibility, occupancy and type-prefix inputs absent from
runtime capture 01. Arithmetic, RNG continuity and recorded steering checks
pass. No further engine behavior change or whole-game parity is claimed here.

`check_captured_search.py` resumes the captured phase-2 heap in both retail
emulation and `RetailCostSearch`: **625 pops, 854 grade queries in identical
order, every heap entry after each pop, and all 409,600 final cell flags and
directions agree**. This is a frozen-world kernel comparison, not delivery
timing or validation of the port's terrain grader. It uses the real binary's
grade queries against the captured world. The suspended-state importer rebuilds
open-cell node references from heap order; allocation indices do not break ties.

Crucially, the actual first-tick mission dispatcher **cancels Hunter 53's active
search** when its 0x2000 event resets the move controller. Running `0x4d8450`
from this capture clears the singleton's active entity. For Hunters 40 and 53,
using each handler's recorded entry RNG seed reproduces all next-frame mission
bytes except the newly allocated controller pointer. These are isolated handler
checks; seeding each entry does not prove whole-game RNG ordering. Hunter 56's
post-dispatch pending mask additionally receives 0x2000 later in the live tick.
Completing Hunter 53's frozen search and delivering its route would therefore
be an incorrect restore strategy.

`emureload.py` retains actual lazy container initialization and replaces cdecl
exit-callback registration alongside allocation/free; executing process shutdown
is outside this harness. The next known missing input is idle mission 49's
script state (read at `0x41265a`). The recorder's optional `--game-memory` captures
readable private mappings below `0x60000000` in 1 MiB compressed chunks, bounded
to 768 MiB total, at the same initial stopped tick. This covers heap dependencies
and writable globals together; shared/high Wine mappings and OS state remain
excluded. Emulator reads in uncaptured portions of mapped pages are not yet
audited, so successful execution alone does not certify complete state.

**Additional correction:** the old phase-2 Dijkstra claim was wrong.
`emuphase.py` supplied a passability grade for the controller's goal-distance
virtual method and reversed start/goal. It now executes the real cell-goal
controller with separate navigator storage. Retail's distance is
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
neighbor fan, trace-marked cells, and the increasing heuristic. It is **not yet
connected to PathService**: hooking it into the existing synchronous Dijkstra
call would still give incorrect delivery timing and omit retail retries.

`reload-runtime-state-01` also succeeded: 61 frames, 942 RNG calls and 1,251
steering events. Arithmetic and the entire RNG chain pass. Hunter 40 and 53's
initial live mission pending mask is **0x3000**, versus saved **0x1000**. Their
extra 0x2000 event wakes state 2 before its deadline; the dispatcher can reset
and advance through states within the same tick. Hunter 56 instead reaches
its deadline on the first tick. These observations explain the missing move
mission RNG activity without fabricated draws.

Continuing the captured search also requires the coarse per-player visibility
plane used by `0x413c80`, map/occupancy records and relevant type prefixes.
Those were absent from runtime capture 01. The bounded recorder now includes
them, plus the mission definition records, and preflight reads succeed. Type
prefixes and recognized controllers are explicitly partial; this is not a full
save-state importer or a whole-game parity claim.

`reload-path-state-02` succeeded: 61 frames (ticks 10069–10129), 543 RNG
calls and 1,251 steering events. The complete RNG chain and every recorded
steering calculation pass `check_motion.py`; this verifies arithmetic and
capture continuity, **not full simulation parity**. The partial replay still
diverges on its first RNG call and on MobileBuild/flying Patrol movement.

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
at `0x4f6c9e` passes divisor 1. The current 12-search pool, once-per-tick slices,
custom completion charges and non-resumable cost search remain deviations.

The first RNG call is Hunter 40's Move_Ground handler at `0x402dbd`, followed by
idle unit 49 and moving Hunters 53 and 56. Saved Move_Ground state is 2; the
observed call comes from state 1. The controller at `0x4d8450` can run several
state transitions in one tick, consumes unit/mission event masks, and schedules
deadline events. These transitions must be reproduced, not replaced with dummy
RNG draws. Initial live mission records and cached navigation grade planes were
not included in capture 02, so exact resumption is not yet possible from it.

The recorder's `--runtime-state` option adds the initial entity slot pool,
bounded mission chains and recognized ground controllers, cached ground grade
planes, and active mission heads/events each tick. Unknown controller classes
are explicitly marked. It preflights these reads while stopped before arming;
the running fixture yielded 163 missions and three grade planes. Synthetic
tests cover cyclic chains, oversized planes and unknown controllers. All 21
Python decoder/recorder tests pass. No engine behavior changes are claimed by
these diagnostic additions.

## Captured route state and ground movement corrections (2026-09-17)

**Exact game parity is not established.** Network version is **93**. The local
`reload-steering-02` capture contains the navigator points before the first tick,
the movement modes, and 69 steering events. Live x87 control is **0x027f**
(53-bit precision, nearest rounding). The partial replay now restores those
initial points, modes, refusal bits and local-scan deadlines, rather than
starting every navigator cold. It never injects future captured routes.

The first captured movement tick now agrees in raw position, heading and speed
for **all 32 Hunters** and 45 of 47 units overall. The two exceptions are the
unrestored MobileBuild and flying Patrol missions. Hunter 40's earlier detour
was already present before the first tick. Subsequent Hunter differences in
this short recording coincide with route deliveries missing from the partial
restore: unit 56 first, then 67, 81 and 97. This is evidence about this fixture,
not certification of all ground units, orders or map conditions.

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
refreshed on mover `+30`'s deadline by checking eight neighboring footprints.
Floater look-ahead and clear-ground corner pruning are still incomplete.
Repeated refusal (mover `+36` bit 0x4) bypasses random retry cadence and requests
a route immediately, leaving an already-pending search alone.

Removed the projected-cliff-wall heuristic: it blocked flat cells north of
cliffs which the captured retail Hunter traversed. Actual slope/depth and map
hard blockers remain. Match setup now uses retail's default 12,000 work budget;
the former 1,500 performance setting changed route-delivery timing.

Whole-game RNG ordering still diverges on the first call: retail executes a
mission-controller `rand(5)` at `0x402dbd`, which the partial importer does not
schedule. Search scheduling, initial unfinished searches, full mission queues,
saved features, script/production, combat/economy, allocation and remaining
mover state still need restoration or implementation. The 12-search pool and
custom scheduler are deviations, not parity-preserving optimizations.

Latest crowd measurements (`/tmp/tak-refusal-crowd.log`) have final area counts
31/32 opposing, 24/24 choke, 32/32 one-point, 24/24 open, and 11/12 maze.
Immediate retries substantially increase search work. The repeated-click
16-unit collision/arrival regression passes, but the crowd results still expose
unresolved arrival/performance issues; do not describe this as finished.

Validation after these changes: **26/26 debug and 20/20 release tests pass**,
plus 16 Python tests, the executable arithmetic comparisons above, and the
traffic/local-scan emulations. The old cadence test asserted gentle random
retry after repeated refusal; its expectation was corrected using the branch
at `0x4e51d4`, and now also verifies no random-tail draw is consumed.

The next capture tool supports `--steering --path-state --ticks 60`: initial
singleton fields, bounded compressed scratch buffers, per-player counts and
iteration state, mover/navigator records, route outcomes and RNG caller
registers. The first attempt (`reload-path-state-01`) timed out without a
reload and detached successfully; it is **not a usable capture**. A fresh
reload was required at that point; capture 02 and the scheduler comparison above
now supersede that blocker.

The sections below retain earlier investigation results and are superseded by
the observations above where they conflict.

## Earlier route-segment steering implementation

Ground routes now retain each segment's start with its endpoint in `Order`.
`replaceLeg` creates those pairs within the current command; consuming a route
node no longer loses the preceding point, and queued commands do not become
look-ahead points for one another. The mover uses `retailSteeringPoint` to
derive its aim, with raw fixed-point differences. Flying steering is unchanged.
Segment state is hashed, and network version is **92**.

Navigator getter `0x4e5100` returns the first N route points, repeating the final
point when fewer than N remain. The new aim calculation implements
`0x4d9bc8..0x4d9ce9`, including truncating the normalization division and
shifting signed products. Comparisons cover 627 boundary/random inputs at both
53-bit and 64-bit x87 precision. Unicorn's default 24-bit precision produces
different square-root rounding; the comparison sets precision explicitly.
This validates arithmetic under those two settings, not the live control word.

World uses movement mode 0 (80-pixel threshold) for these segments; the helper
also implements the 16-pixel threshold for other retail modes. Route selection,
node-advancement timing and mode transitions are not fully restored or ported.
In `probe-segment-01`, initial movement fields still match, but the first
mismatch remains Hunter 40 at tick 10,070. This does not fix that trajectory
or whole-game RNG ordering by itself.

All 26 debug and 20 release tests pass, including an actual mover regression
distinguishing segment steering from direct endpoint steering. Crowdbench
spot/area counts are 30/32 and 32/32 opposing, 18/24 and 24/24 choke,
32/32 and 31/32 one-point group, 21/24 and 24/24 open, and 12/12 and 12/12
maze (travel 7.18). The one-point final-area count regressed by one, so these
results do not establish an overall crowd improvement or retail parity.

## First-turn steering isolation

`tools/re/analyze_saved_steering.py` executes retail's `0x53612a` angle
routine for each active Hunter's saved cell goal, then applies the verified
2,500 BAM/tick turn limit. In the recorded first tick, 13 of 21 headings agree
with this direct-goal counterfactual. Hunter 40 would turn from 64,728 to
64,978; the capture instead turns to 62,228. The discrepancy therefore survives
using retail angle arithmetic: replacing CORDIC alone cannot resolve it.
This check assumes an unmodified turn rate and does not certify route state.
The local report is `steering-first-tick.json` alongside the saved fixture.

The mover obtains two route points through virtual slot +0c with argument 2
at `0x4d9bbb..0x4d9bc5`. At `0x4d9bc8..0x4d9ce9`, it adjusts the second
point along the segment toward the first before deriving the requested heading
at `0x4d9cf3`. The adjustment uses a distance threshold of 80 pixels for
movement flag mode 0 and 16 for the other modes, and is capped by segment
length. The former single-front-order steering did not reproduce this calculation.
The captured route points themselves are not in the existing frame recorder;
the Hunter 40 result alone does not establish which points retail selected.

The save decoder now exports every copied mover field by runtime offset,
including both three-component vectors and the additional 16-bit fields.
All 38 mover records match execution of `0x4dcae9..0x4dcb9e` under three
different initial flag patterns. The loader preserves high flag bits and
does not write the runtime movement tick at +2c. These fields remain explicitly
opaque where their meaning has not been established; they are not silently
interpreted as waypoints or injected into the simulation.

## Movement replay probe and arithmetic corrections

`retail_replay_probe` now loads the retail map/type data and the verified raw
unit positions, headings, base/current speeds, footprint identities, tick, and
captured RNG state. It rebuilds ground move orders from the decoded cell-goal
controller. `tools/re/probe_saved_movement.py` prepares its input and reports
the first motion and RNG mismatches against the bounded reload sequence.
Generated inputs, traces and reports stay outside version control.

**This is an explicitly partial diagnostic restore.** It records missing
mission scheduling, unsupported orders, script/production state, saved feature
changes, combat/economy state, movement timers/flags, and allocator state.
Routes start cold. A matching initial movement frame does not mean the whole
world has been restored. The fixture includes 118 MobileBuild orders and other
non-movement missions; these cannot be silently discarded in a full importer.

```
python3 -B tools/re/probe_saved_movement.py assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/test.tak assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/reload-sequence-01.json --runner build-dbg/retail_replay_probe --retail-root assets/game --output assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/probe-new
```

The probe exposed two independently verified movement arithmetic defects:

- **Turn braking mixed BAM with radians.** The old expression converted turn
  rate to radians/sec but left the angle difference in BAM, inflating the
  turning distance by roughly 10,430. `retailTurnTravel` now computes
  `speed_raw * angle_bam / rate_bam`, including retail's zero-rate behavior.
  All 60 comparison cases match execution of `0x4da53a..0x4da56e`. The broader
  braking predicate and the choice of distance/path points remain port code;
  this does not certify the entire braking routine.
- **Ground displacement used different trig quantization.** Retail
  `0x5360bf/0x5360f3` use 512 Q13 sine samples, binning with a +32 BAM bias,
  and add 4096 before shifting the product by 13. The ground movement path
  negates the result after rounding (`0x4dab0d/0x4dab2b`). A mathematical
  constexpr generator supplies the samples; no retail table bytes are copied
  into the source. All 8,192 signed-magnitude/bin-boundary comparisons match
  executed retail routines. Flying movement retains its existing calculation.

Retail heading zero points along negative Z, whereas World uses positive Z.
The probe converts by half a turn at import/export; it does not change the
engine's internal convention. This is confirmed by the retail displacement
call path and the captured positions. The original click also differs from
the controller's navigation point: the shared click (2180,596) becomes
(2176,592) for the Hunters' footprint. All 22 controller goals match execution
of `0x4e2820` in `check_save_state.py`.

In `probe-04/comparison.json`, every initial compared movement field matches.
Hunter 17's first horizontal movement step then matches exactly: raw displacement
(-54769,-91372), heading 5692, speed 106521. The first remaining movement
divergence is Hunter 40 at tick 10,070; its steering differs. The first RNG
divergence is also explicit: retail calls rand(5) at tick 10,070 while the
partial port restore first calls rand(8) at tick 10,112 (36 total port calls
versus 529 retail calls). This is not evidence to insert dummy draws: the
mission scheduling and restored state still need porting.

Validation: 26 debug and 20 release tests pass, including a movement regression
that a distant quarter-turn goal does not brake the unit to a stop. Both builds
pass the executable arithmetic comparisons. Crowdbench reaches every goal area;
exact-spot counts are 31/32 opposing columns, 20/24 choke, 32/32 one-point group,
19/24 open field, 12/12 maze. The maze travel ratio remains high at 7.24.
Network version is **91** because the arithmetic changes simulation outcomes.
Full retail trajectory and whole-game RNG ordering parity remain outstanding.

## Saved retail baseline (2026-09-17)

The user saved the current retail game as `SavedGames/test.tak`. A byte-for-byte
copy is preserved outside version control at
`assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/test.tak`, alongside a provenance
manifest and decoded-section inventory. The original was not modified.
The save is 156,303 bytes with SHA-256
`0d8e4c03c052a3c469f4b1d69b2f1e35df25b4747fb6351d66bbd197bbf11caa`.

`tools/re/inspect_save.py` recognizes this HAPI BANK save and decodes its eight
SQSH sections using method-1 LZSS. All eight compressed checksums and decoded
sizes match. Its string table identifies **Ulasem Arena**, resolving the earlier
phonetic map names, and the decoded data contains `zonter` records and named
orders such as `Move_Ground` and `Standby`. String-table entries alone do not
establish associations between keys and state values.

```
python3 -B tools/re/inspect_save_test.py
python3 -B tools/re/inspect_save.py assets/tmp/retail-traces/saves/0d8e4c03c052a3c4/test.tak
```

The inspector now also decodes all ten directory nodes, including integer,
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
of aligning them silently. Manual pause was 28 ticks late, so
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
the captured boundary is therefore required for replaying that particular run.
The save-only export still leaves RNG null.

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

Still outstanding: complete mission/controller semantics, script and other
simulation state, importing the restored baseline into the port, port-side
trace export, and resolving the first trajectory divergence. `World::setRngObserver`
now exposes tick, bound, before/after seed, result, and original C++ consumer
location through the fire/burn/path wrappers. A 600-tick paired movement test
verifies that tracing leaves every state hash unchanged; replay reset also
preserves the observer without consuming an extra draw. This remains
a state exporter and retail reference capture, not a runnable port replay.

## Live retail findings (2026-09-17)

Read-only observation of retail under Faugus/Proton with Glide now works.
The user identified the map as "Ulem arena" and the scenario as a group of
Zhon Hunters moving around terrain, with **several destination clicks**.
The shipped Hunter definition is `zonter`, not the flying monarch `zonhunt`.
The map name and selected group were user reports, not decoded from memory.

`tools/re/livesample.py` now reads the actual entity table at
`G+0x14e84..G+0x14e88`, stride `0x138`. The previous sampler incorrectly used
the separate occupancy/rater table and guessed coordinate offsets. Three live
code regions (traffic query, hard placement, movement commit) match our local
reference byte for byte; this does not establish whole-executable identity.
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

The previous floor-to-pixels, divide-by-16, subtract-half-footprint convention
disagreed in 16,646 observations. `src/sim/footprint.h` now supplies the conversion
for occupancy, hard placement, road tests, navigation endpoints, waypoints,
connectivity lookups and production/unload placement. Even-footprint navigation
anchors are at `cell*16`; odd footprints retain `cell*16+8`. The compiled helper,
not just a Python transcription, matches all 40,005 captured observations.

The trace also shows multiple per-unit base speeds for the same type. Creation
at `0x51228f..0x5122b8` multiplies the type speed by `0x512430`'s result before
storing entity `+0x12b`. The multiplier is `trunc((900+rand(201))*65536/1000)`.
All 201 multipliers were executed in retail and compared with the compiled port.
Units now retain their individual base speed for movement, formation pacing,
refusal caps and the traffic floor; it participates in the lockstep hash.
The FBI fixed-point reader `0x5431f0` also truncates instead of rounding: Hunter
nominal speed is 117964, not 117965. Movement data now uses that conversion.

Further refusal-path checking found a missing behavior and an earlier RE error:
`0x4db010` clamps a refused position inside the old footprint's +/-8px cell
window, with a one-raw-unit inset. The first-refusal branch `0x4db06f` snaps
coordinates outside +/-4px to the corresponding inset edge. Rejecting the
whole movement vector loses this tangential motion. Both branches now have
compiled-vs-executable comparison coverage (210 cases over five footprints).
The two speed caps divide terrain-adjusted individual speed by **2 and 5**;
previous descriptions of the second cap as 0.4 were wrong. Refusal bits persist
through same-origin movement and clear on an accepted origin change.
Goal-area satisfaction is checked independently of intermediate route points,
so a blocked waypoint cannot trap a unit that already reached its leg endpoint.
The existing goal radii and square navigation approximation are not certified
retail equivalents.

The previous independent burn/fire/path streams have been replaced with a
shared simulation stream, including creation speed draws. The recurrence,
seed initializer (`0x535d30`) and no-draw behavior for bounds below 2 are
verified against the executable; 3,500 chained calls compare both outputs and
successor states. Match setup uses the shared match seed; replay resets it.
This is **not whole-game RNG ordering parity**. Retail has 460 direct calls to
`0x535cc0`; their complete dynamic order is not mapped. Scenario and AI RNGs
remain separate and their retail relationship is unverified. The seed passed
by retail match initialization still needs live verification. Sharing our
existing consumers does not establish matching retail consumer order.

Run the compiled-vs-retail comparisons (requires local retail and Unicorn):

```
python3 -B tools/re/check_motion.py build-dbg/retail_motion_test assets/tmp/retail-traces/terrain-20260917-01.jsonl
```

A new 16-unit terrain regression changes destinations three times, checks every
footprint throughout 3,600 ticks and requires all 16 final arrivals. It initially
failed (10/16); with refusal clamping and independent goal completion it passes
16/16. The six-unit queued rally regression likewise improved from 4/6 to 6/6.
These are port regressions, not side-by-side retail trajectory comparisons.
Network version is 90 because movement, RNG and state hashes change.

Validation after these changes: all 26 debug tests and 20 release tests pass
(the final debug retail-data test was rerun after fixing its storm fixture).
The storm test now places its caster and crowd on open terrain in weapon range;
it had been failing before any storm was cast, testing map LoS instead of damage.
Factory output shares no footprint cells across 5,400 ticks. Crowdbench area
arrivals are 32/32 columns, 24/24 chokepoint, 32/32 single-point group, 24/24 open
field and 12/12 maze. Exact-spot counts are respectively 30/32, 19/24, 32/32,
19/24 and 12/12. Those remaining spot misses must not be hidden by the area metric.

These are local movement observations, not a single-order route/arrival test.
The trace includes other units, not just the selected group. Tick-before/after
guards reject reads spanning a tick but do not make snapshots atomic. Observed
tick deltas are 2 (1,568 times) and 5 (once); the original capture footer's
`tick_gaps` assumes increments of one and must not be interpreted as missed
simulation frames. The recorder now reports a delta histogram instead. Schema 2 also records
nominal type speeds and RNG state before/after the read; RNG snapshots cannot
identify individual callers or prove draw ordering.
Four synthetic-memory sampler tests pass, covering layout, signed coordinates,
changed ticks, mismatched IDs and invalid table extents.

Second live capture: `assets/tmp/retail-traces/terrain-20260917-02.jsonl`, schema 2,
1,322 frames / 31,635 unit observations. The compiled origin helper matches all
31,635; the old convention disagrees in 11,451. Every recorded individual speed
is attainable from its recorded nominal speed under the verified multiplier.
Hunter nominal speed is confirmed live as 117964. Eight Hunters moved and were
stationary at the end; two observed Hunters remained at their starting positions.
One destination click was requested for this capture; command events/coordinates
were not decoded, so it is not yet an identical-input trajectory comparison.
RNG changed during 10 otherwise same-tick snapshots, further confirming that
polling states cannot establish atomic RNG draw order.

`tools/re/trace_rng.py` now supplies a bounded **hardware-breakpoint** recorder
for call ordering. It pauses/slows the game; it never patches executable bytes
and detaches at its call limit or 20-second soft timeout. Wine's SIGUSR1 is passed
through normally. The successful local capture is
`assets/tmp/retail-traces/rng-20260917-02.jsonl`: 512 consecutive calls from 15
call sites during ticks 9105..9152, on the game thread. Each seed successor
matches the next call's observed seed, so this captured interval has no missing
state-advancing draws. Repath caller `0x4e547d` accounts for 201 calls; mission
caller `0x402dbd` accounts for 142 (`rand(5)+5` before `0x4d6a10`). The other 169
calls come from 13 additional sites. These are not a demonstrated match to the
port's order of consumers: that mapping is still outstanding.

Reproduce arithmetic and sequence verification with:

```
python3 -B tools/re/check_motion.py build-dbg/retail_motion_test assets/tmp/retail-traces/terrain-20260917-02.jsonl --rng-trace assets/tmp/retail-traces/rng-20260917-02.jsonl
```

## Current correction: placement is separate from traffic scoring (2026-09-17)

The earlier "IMPLEMENTED" section's claim that a same-way traffic grade permits
overlapping movement is superseded. It omitted the movement commit path:

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

The sim now keeps the two decisions distinct. Routing scores the footprint,
including qualifying same-way traffic; committing a new footprint requires it
to be free of all other bodies. Occupancy is updated after **each mover**, so
later movers cannot claim space using a stale beginning-of-tick snapshot.
Without the corresponding routing change, adding collision alone creates
routes the mover cannot follow; that combination was tested and rejected.

Two independent integration errors are also corrected: `requestPath` no longer
cancels all trips shorter than three cells (even a two-cell trip can cross a
wall), and attack/guard legs now use the route service. Their intermediate
waypoints retain the target and order flags, while target tracking updates the
leg endpoint rather than overwriting its next waypoint. Target loss cancels the
leg's pending route and resumes queued commands. Terrain refusals now advance
the same two-stage refusal state as unit refusals.

Validation: new short-obstacle, chase/guard, queued-command, footprint-edge,
same-tick reservation and refusal-state regressions in `pathblock_test`; zero
overlapping pairs in the existing 24-unit factory test, plus exclusive footprint
ownership checked throughout its 5,400-tick production run. All 19 release and
25 debug tests pass. Two seeded 60-second Ulasem Arena network smoke tests finish
at tick 1,800 with hash `195541517b19a6b0`, no synchronization error. The math
determinism check agrees across GCC/Clang at O0/O2/O3; the ARM toolchain legs were
unavailable. Crowdbench reaches the
destination **area** for 32/32 opposing units, 24/24 chokepoint units, 32/32
group-order units, 24/24 open-field units and 12/12 maze units. Exact-point
convergence is slower in tight crowds: the chokepoint's spot count is 17/24 and
open-field spot count 16/24 within the existing time limits. Do not present the
area count as exact-point arrival or as a measured retail timing comparison.

These are observed mechanisms and regression results, not proof of complete
retail equivalence. The port still uses its existing square footprint / cell
centre conventions, tracer and heap tie-breaking, and does not reproduce every
branch of retail's movement clamp or look-ahead. Live retail comparison on the
same Ulasem Arena orders remains needed to certify identical behaviour.

The sections below retain the research history; the correction above takes
precedence over their conflicting claims about overlap and single-cell movement.

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
returns 0 for `n < 2` **without advancing the seed**; other calls advance it.

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
  0x4c03e0-0x4c0403) and +0x194 is the movement class's `minwaterdepth`
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
dice table rolled fresh per frame, the floater/minwaterdepth gates, and the
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
`minwaterdepth` > 0) and three roadmultiplier-scaled costs (+0x172 x the rdata
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
- Phase 2 skipped / over budget (0x4166f1): retry up to 3, then clear the route.
  The former relocation claim was disproved by the executable exhausted-heap
  test described above; its fixed neighborhood scan does not change the goal.

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
