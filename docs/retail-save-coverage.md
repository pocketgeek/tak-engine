# Retail save comparison coverage

The eight-player `test2` snapshots on Valysia City broaden the existing
single-save replay. Files and runtime captures remain in ignored
`assets/tmp/retail-traces/saves/`, keyed by the first 16 SHA-256 digits.

| Save | Key | Tick | Units | Players with units |
| --- | --- | ---: | ---: | ---: |
| test2-early | 3470a6da9f8ed7b2 | 959 | 18 | 8 |
| test2-mid | b204953d33be47e6 | 14468 | 116 | 8 |
| test2-late | 5cea791f1448cd7a | 37447 | 169 | 8 |

All three pass `tools/re/check_save_state.py`: 303 script records (16 threads
each), 423 order records, 188 movers, 138 cell goals and six rectangle goals
agree with executed retail loader blocks. These checks validate decoded fields,
not complete save restoration or simulation parity.

## Early runtime capture

`reload-crt-state-02.json` captures boundaries 959 through 964. The first attempt
stopped safely at the main menu; runtime preflight now defers an absent entity
table until the matching simulation boundary. Malformed nonempty tables still
fail validation.

The generated `probe-initial` fixture has matching initial compared state, but
the first movement difference is at tick 960: ZONHUNT 1229 has an active
`VTOL_MobileBuild` mission that the importer does not restore. ARAKING 3601 also
has two unsupported ground `Patrol` orders. Gameplay RNG and CRT sequences
diverge in the first tick. This fixture is a failing coverage case, not a
passing regression.

Tracing the executable handler at `0x41ef00` for unit 1229 through tick 963
confirms an active flying-construction loop, rather than a flight approach:
stage 5 (`0x41f652`) receives the timer event and, when owner flags intersect
`0x0c`, draws `random(50)`. A nonzero result advances directly to stage 6 in
the same dispatch. A zero result (or absent flags) sets stage 4 for controller
repositioning. Stage 6 (`0x41f685`) performs construction work, emits builder
and site effects when work succeeds, and returns to stage 5 with a one-tick
deadline and wait bits `0x0a` while unfinished. Completion advances to stage 7.
The four traced transitions match the live fixture. The missing bound-50 draw
at tick 960 is therefore attributable to this omitted mission, independently
of later patrol differences. The existing ground construction stage-3 handler
cannot restore this state by merely reusing its stage number.

Offline retail tick-body replay checks five transitions, with its first reported
difference at tick 964: a missing CRT call at return address 5163096. Outer
frame execution is excluded by this mode. Resolve or isolate that fixture
limitation before using longer replay prefixes as engine-parity evidence.

Mid and late saves additionally contain transport, formation movement, pursuit,
and (late) melee missions. Their decoded presence does not establish support.

## Mid runtime capture

`reload-crt-state-01.json` captures boundaries 14468 through 14473. All five
offline retail tick transitions pass the live-capture comparison, including
the checked random streams. The engine fixture `probe-initial` matches its
initial compared state, then diverges in unit 572's X position at tick 14469.
The first gameplay RNG difference is draw index six (retail bound 10 versus
port bound 5); the first CRT difference is draw index eight.

The importer reports 72 unsupported ground patrol orders, nine carried-unit
orders, three formation moves, one pursuit order, one flying-builder order,
two flight moves, two flight patrols, nine factory-build orders, and one ground
move whose stage/runtime state is unsupported. These are observed order
instances, not a count of independent engine bugs.

## Late runtime capture

`reload-crt-state-01.json` captures boundaries 37447 through 37452. The engine
probe stops during initial feature restoration: cell 24850 changed from
`verhut01` to `verhut01a`. Relative to the early capture, eight feature cells
changed type and eight new feature cells appeared (431 versus 439 total).
The existing rejection remains enabled; silently retaining original map
features would invalidate subsequent comparisons.

Offline retail replay matches two transitions, then stops at tick 37450 on an
unmapped external instruction at `0x7be8fb70`, returning to `0x53b002`.
This is a missing emulator dependency, not evidence of an engine mismatch.

## Active flying construction (protocol 130, probe 31)

The active stage-5/6 work loop is now restored separately from ground jobs.
It preserves the flight controller, velocity, construction target, mission
events and owner flags. Construction does not hold the flyer stationary.
Reaching the flight point posts arrival/release events while velocity decays,
instead of stopping position updates immediately.

`probe-flying130-final` and `probe-flying130-release` match unit 1229's motion,
including altitude, across all five early-save transitions. All gameplay RNG
draws match. The construction comparison additionally checks flying mission
stage/waits/deadline/events/flags, site work and HP, and resource pools; those
checks pass through tick 964. The full scenario still diverges in unit 3601's
movement at tick 960, and two outer-frame CRT draws remain unmatched at 964.
Although its saved orders are patrols, the live initial primary handler is
`0x405560` (ground construction), stage 0, advancing to stage 1 at tick 960.
The live mission therefore takes precedence when diagnosing this failure;
the saved patrol labels alone do not identify the active runtime behavior.

Orbit repositioning and completion stages remain explicit unsupported guards.
This implements the observed active work loop, not the entire flying-builder
lifecycle. Owner flags are restored from the initial boundary; later changes
outside this verified prefix still need comparison.

## Next work

Use the validated mid fixture and the early prefix to isolate mission support,
extending flying construction and adding ground patrol restoration. Restore
changed and newly created features with their relevant state before claiming
the late fixture is initialized correctly. Resolve the late external call and
early CRT omission separately. Keep the existing factory replay as a regression
case; these broader fixtures supplement it rather than replace its checks.

## Live ground-build restoration

The importer now restores a captured stage-zero ground-build mission using its
live type and coordinates, even when the saved order list differs. It requires
the observed initial scheduling state (no wait/pending events, unset deadline,
one build) and rejects other variants instead of guessing.

`probe-live-build01` and `probe-live-build-release01` produce identical traces.
All units' compared movement fields and gameplay RNG now match the five live
transitions. Construction work, HP, pools, flying mission state and 30 GetBuilt
mission comparisons pass as well. The final two outer-frame CRT calls remain
unmatched; this is not full-state parity. All 118 Python tests pass.

A 100-tick port-only extension reaches tick 993, then explicitly stops at 994
on the unsupported retail AI nearby-threat response. That extension has not
been compared against retail and is not a matching-prefix claim.

The subsequent offline comparison matches movement and gameplay RNG through
tick 984 (25 transitions), then finds unit 1229's X-position difference at 985.
This comparison excludes CRT equality and does not claim complete state parity.
The preserved extended mission/flag traces show retail changing owner flags
from `0x09180126` to `0x09180122` at tick 984, at instruction `0x4db62b`.
This is the movement-rate classification update, including a script callback.
At tick 985 the flying builder consequently takes stage 5 -> 4 -> 6, skipping
the bound-50 draw and constructing a new orbit controller. Capturing owner
flags only at initialization is insufficient for this transition.

The stage-4 calculation at `0x41f580` draws two bounds `0x2492` for its heading
offset and then two bounds 8 for its radius, builds a point around the site,
and installs a point controller with the selected heading. Implement this with
the movement-rate flag update before pursuing the later AI threat-response
guard. The previous port-only run reached that guard after already diverging.

## Flying-builder orbit (protocol 131, probe 32)

Probe 32 restores the movement-rate thresholds and construction radius. Flying
construction now updates its movement-rate bits after movement, invoking the
`MoveRate` script callback on transitions. Stage 4 selects a new orbit with the
retail draw order, installs its heading constraint, and clears pending controller
events. Completion remains an explicit unsupported stage.

The reusable `tools/re/check_replay_fixture.py` compares unmodified fixtures
against native tick bodies without supplying later state. Both builds pass
34 early-save transitions through tick 993: compared unit motion, flying-builder
mission state, construction work/HP, resource pools, 321 gameplay and 393 CRT
draws. This does not include outer-frame rendering or all AI/script internals.
The 35th transition now stops on the AI nearby-threat-response guard at tick
994 after a verified prefix. Keep that guard until the response is implemented.

## Commander threat response at tick 994

The native owner-7 base, slot 1, contains only commander 3601. Its threat query
returns true. The constructor planner `0x40c890` checks commander state and
category-4 membership; with no such members it cancels the construction order
whose flags include bit 8 (`0x40ca0e`) and invokes `0x40d370` with threat=true.
The preserved `ai-threat-orders994-01.json` records the resulting queue:
two kind-28 ground moves with flags `0x03000401`, followed by a kind-34 patrol
with flags `0x03001411`. There is no entity target on these orders. At tick 995,
the first move reaches stage 2 while the remaining two stay at stage 0.

The destination planner includes radius/angle draws, map bounds, ground
placement checks (`0x507d10`), a minimum separation of 100 world units between
patrol points, and additional threat-related candidate selection. Reusing a
direct attack command would not reproduce this observed queue. Implement the
planner and queued ground patrol semantics together; the existing guard remains.

The candidate trace at tick 994 records radius 560 and base centre raw
`(93847552, 4063232, 263716864)`. With no threat-target candidates, the first
patrol point takes four attempts and the second takes one; each attempt draws
radius/2, radius, then 65536. The first three ground-placement calls return 0,
then the next two return 1. Commander 3601 has a 2x2 footprint, maximum depth
20 and maximum slope 30; sea level is 58. The rejected footprints respectively
contain a slope of 38, `0xfffe` feature-footprint cells, and zero-height ground
58 below sea level. These are distinct reasons to retain the placement check,
not random retries that can be replaced by a fixed draw count. The accepted
points are the destinations recorded in the resulting queue.

The empty-target-list sampler is now shared in `src/sim/retailaipatrol.h` and
used by the existing flight patrol planner. `tools/re/check_ai_patrol_points.py`
executes native `0x40d7ad..0x40db3f` with only the placement host substituted:
4,000 deterministic cases agree in Debug and Release on accepted points,
exhaustion, placement-call counts, draw counts and final RNG state. Cases cover
small radii, map-edge rejection and up to 20 rejected placement queries.
This verifies sampling, not terrain placement or the complete commander response.

The ground placement host must preserve `0x507d10` semantics rather than call
the general building-placement API: it excludes footprints ending exactly at
the last map edge, resolves feature-footprint references, checks blocking feature
flags and live entity occupancy (excluding the querying entity), then applies
minimum/maximum corner-height water limits and the dry/wet slope limit. Roads
do not bypass those slope checks. The nearby-threat guard remains until this
host and the queued ground mission behavior are implemented.

After sharing the sampler, the 34-transition early fixture still passes both
builds and the older natural-production fixture passes 317 boundaries with
3,718 gameplay and 754 CRT draws. Protocol and fixture versions stay 131/32;
this extraction preserves the existing simulation behavior.
All 33 Debug and 27 Release CTests and 118 Python tests pass. The compiler
determinism check retains golden `ab1ef54ae324bd0e` (ARM toolchains unavailable);
two seed-1 multiplayer runs finish at tick 1800 with hash `cd13ea7591179314`
and no reported errors.

The captured mission-definition table resolves kind 28 to `0x402b00` and
kind 34 to `0x403600`. The latter's four stage entries are `0x4038bc`,
`0x4038da`, `0x4038eb` and `0x403914`. After its pre-stage diversion checks,
it initializes the patrol queue, resets acceptance expansion, installs a goal
with expansion plus 4, then handles controller events. Event bits `0x700`
reset to stage 1 and return dispatcher result 6 (queue rotation); bit `0x2000`
adds footprint width times 32 to expansion and retries stage 2 after one tick.
World integration and host behavior remain outstanding; stage-core verification
is recorded below.

## Ground placement and patrol cores

`retailMobilePlacement` in `src/sim/retailplacement.h` now implements the mobile
mode of `0x507d10`, including map-edge exclusion, feature-anchor resolution,
entity eligibility, water depth and dry/wet slope limits. The oracle
`tools/re/check_mobile_placement.py` executes the complete original function
without host substitutions: 6,000 cases (2,666 accepted) match in both builds.
Special feature markers `0xfffa..0xfffd`, including the road marker, reject
placement; an indirect `0xfffe` anchor whose feature is itself a sentinel
skips the blocking-feature check. This is different from navigation grading.

`retailGroundPatrol` in `src/sim/retailmission.h` owns stage advancement,
controller-event retries, queue-rotation requests and timer/random behavior.
`tools/re/check_ground_patrol.py` compares 5,000 cases in each build against
native `0x403600`, with diversions and assistance disabled and controlled hosts
for queue initialization, controller creation and combat order selection.
It covers all four stages, invalid stages, absent movers, controller event
combinations, radius wrap, the flag-clearing draw and tick overflow.

These cores are not yet wired into the World commander planner; they do not
extend the verified 34-tick replay prefix. Integration needs an accurate
placement feature plane: `setupMatch` adds only reclaimable/flamable non-mana
features to `features_`, while the navigation obstacle overlay also blocks
some nonblocking decorative features. Neither collection alone reproduces the
placement routine's feature flags. Preserve the static feature placement data
and combine it with dynamic feature and entity state, then compare that host
against captured map queries before removing the nearby-threat guard.

Ground patrol queue initialization (`0x4d6b80`) scans all queued orders for
flag `0x4000`, appends a return patrol at the unit's current position if absent,
and sets that flag on the current order. Combat polling (`0x4d8370`) first
requires an armed unit with unit flags masked by `0xc0000` equal to `0x80000`.
The assistance branch that follows combat is still a separate unported host.

## Recovery and captured placement checks

After the interrupted session, a full Debug rebuild and all 33 Debug CTests
pass. The 6,000 synthetic placement cases and 5,000 patrol cases still match
retail. The early `probe-orbit131` fixture reproduces its 34-transition prefix
with 321 gameplay and 393 CRT draws. The tick-994 guard remains in place.

`tools/re/check_captured_placement.py` compares `0x507d10` with the placement
core using initial captured cells, feature flags, entity slots and mobile type
limits. It samples occupied cells, feature cells, arbitrary positions and map
edges with both moving-occupant policies. The runner's sparse input includes
the queried footprint and referenced feature anchors; missing cells fail rather
than defaulting to empty. This is a captured-input core check, not verification
of the World placement host or map loader.
All 1,000 queries per capture pass: 338 accepted at tick 959, 288 at tick
14468 and 253 at tick 37447, covering 9, 73 and 106 mobile units respectively.

Do not initialize that host by copying TNT feature markers verbatim. In the
early Valysia City capture, the TNT contains 24,853 road markers (`0xfffb`),
but the runtime feature plane contains none. Those original road cells become
21,599 empty cells, 3,188 footprint references and 66 `0xfffd` markers. Across
the full runtime map there are 3,543 `0xfffd` markers and 8,212 footprint
references. Reconstruct the loader's marker transformation and feature
footprints before integrating the host. By contrast, all 479-by-479 interior
terrain quads agree exactly with the runtime high/low corner heights, using
the same down-right four-sample rule already used by `NavGrid`.

## World placement host

`World::setMapPlacementFeatures` now preserves a separate feature plane from
map loading, including non-reclaimable decorations and mana features. Retail
`0x50f789` turns roads into a separate cell flag and installs `0xfffc` directly;
`0x495360` installs named features in scan order, removes intersecting removable
features, and writes footprint back-references. Removal (`0x496380`) preserves
non-tail markers, and a failed later removal does not undo earlier removals.

`0x50eef0` installs `0xfffd` along the final two columns and the projected top
and bottom edges. The bottom scan marks the preceding row before retesting;
it is not a symmetric rectangular margin. The optional water-blocking branch
reads the scenario's `lavaworld` flag (`+0xd39`, parsed at `0x4c8330`).
`retailMapBoundary` matches all cells in 300 synthetic maps in both builds.
The World loader currently uses the ordinary, non-lava mode; lava scenario
wiring remains outstanding.

`check_map_features.py` passes all 230,400 feature cells, all footprint
back-references, all height samples and 229,441 interior low-height quads in
the initial early capture. It feeds only the retail install/map name to World,
not captured cells. The final row/column do not have complete terrain quads;
placement rejects them before reading heights. The five tracked features
removed by the existing early fixture importer were already absent from this
reconstructed initial plane: the older tracked-feature collection did not
model installation overlap correctly.

`World::mobilePlacement` uses this plane, tracked feature liveness/blocking,
current unit footprints, landed-flight state and building yards. The diagnostic
runner's `--placement-queries` option tests this host after restoring the
initial World fixture. `check_captured_placement.py --world-fixture ...`
matches 10,000 early queries in Debug (3,318 accepted). The five recorded
tick-994 patrol footprints also return exactly `0,0,0,1,1` against the initial
World; this does not yet test the complete commander response or advance replay.
Dynamic feature geometry/replacements, later-save restoration, lava scenarios
and occupancy transitions beyond this initial fixture still need coverage.

All 33 Debug and 27 Release CTests and 118 Python tests pass after these changes.
The Release World host also matches 1,000 early queries (338 accepted).
GCC and Clang at O0/O2/O3 retain golden `ab1ef54ae324bd0e`; the ARM builds
cannot run here because target libc/headers are unavailable. The early
replay still verifies 34 transitions, 321 gameplay draws and 393 CRT draws.
The host is not yet called by the commander planner, so the tick-994 guard
remains and protocol/probe versions remain 131/32. Next connect the verified
placement host to construction cancellation, return-waypoint creation and
queued ground patrol handling, then extend the replay comparison from tick 959.

The response's first ground move is the base centre, not a sampled patrol
point: `0x40dc03..0x40dc1e` issues the centre with the computed radius; the
following call issues the first sampled point, and `0x40dc7b` appends the second
sampled point as a patrol. The captured destinations are respectively
`(93847552,263716864)`, `(108471616,288114608)` and `(90731912,295350504)`.
Cancellation through `0x4d6a50(unit,0)` preserves queued missions with flag
bit 4, removes the others and invokes their teardown handler (`0x4d6da0`).
Do not replace it with an unconditional queue clear or the existing
`cancelBuilds` alone, which does not reset `retailBuild`.
