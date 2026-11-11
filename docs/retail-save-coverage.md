# Retail save comparison coverage

Retail save fixtures compare the port against retail from a restored mid-game
state. The eight-player `test2` snapshots on Valysia City broaden the
single-save factory replay (which remains a regression case; these fixtures
supplement it). Files and runtime captures live in ignored
`assets/tmp/retail-traces/saves/`, keyed by the first 16 SHA-256 digits.

`retailGroundPatrol` drives ground patrols in the World (`src/sim/sim.cpp`), and
the commander's threat response (`0x40c890` / `0x40d370`) is ported in the
Retail AI's brain (`src/ai/classicbrain.cpp`, `src/ai/classicdefence.h`,
`src/ai/classicproduction.h`). The fixture replay counts below are the last
recorded comparison; it has not been extended past the tick-994 commander
response.

| Save | Key | Tick | Units | Players with units |
| --- | --- | ---: | ---: | ---: |
| test2-early | 3470a6da9f8ed7b2 | 959 | 18 | 8 |
| test2-mid | b204953d33be47e6 | 14468 | 116 | 8 |
| test2-late | 5cea791f1448cd7a | 37447 | 169 | 8 |

All three pass `tools/re/check_save_state.py`: 303 script records (16 threads
each), 423 order records, 188 movers, 138 cell goals and six rectangle goals
agree with executed retail loader blocks. These checks validate decoded fields,
not complete save restoration or simulation parity.

Mid and late saves additionally contain transport, formation movement, pursuit,
and (late) melee missions. Their decoded presence does not establish support.

## Early runtime capture

`reload-crt-state-02.json` captures boundaries 959 through 964. Runtime
preflight defers an absent entity table until the matching simulation
boundary; malformed nonempty tables fail validation.

At tick 960 ZONHUNT 1229 has an active `VTOL_MobileBuild` mission and ARAKING
3601 has two saved ground `Patrol` orders. Without the flying-construction
mission the first movement, gameplay RNG and CRT sequences diverge in the
first tick.

### Active flying construction (`0x41ef00`)

Tracing the executable handler at `0x41ef00` for unit 1229 through tick 963
shows an active flying-construction loop, rather than a flight approach:
stage 5 (`0x41f652`) receives the timer event and, when owner flags intersect
`0x0c`, draws `random(50)`. A nonzero result advances directly to stage 6 in
the same dispatch. A zero result (or absent flags) sets stage 4 for controller
repositioning. Stage 6 (`0x41f685`) performs construction work, emits builder
and site effects when work succeeds, and returns to stage 5 with a one-tick
deadline and wait bits `0x0a` while unfinished. Completion advances to stage 7.
The four traced transitions match the live fixture, and the bound-50 draw at
tick 960 belongs to this mission, independently of later patrol differences.
The ground construction stage-3 handler cannot restore this state by reusing
its stage number.

The port restores the active stage-5/6 work loop separately from ground jobs.
It preserves the flight controller, velocity, construction target, mission
events and owner flags. Construction does not hold the flyer stationary.
Reaching the flight point posts arrival/release events while velocity decays,
instead of stopping position updates immediately. Unit 1229's motion,
including altitude, matches across all five early-save transitions, with all
gameplay RNG draws; flying mission stage/waits/deadline/events/flags, site
work and HP, and resource pools match through tick 964.

Owner flags change during the loop: at tick 984 retail changes them from
`0x09180126` to `0x09180122` at instruction `0x4db62b`. This is the
movement-rate classification update, including a script callback. At tick 985
the flying builder consequently takes stage 5 -> 4 -> 6, skipping the bound-50
draw and constructing a new orbit controller. Capturing owner flags only at
initialization is not enough for this transition.

The stage-4 calculation at `0x41f580` draws two bounds `0x2492` for its heading
offset and then two bounds 8 for its radius, builds a point around the site,
and installs a point controller with the selected heading. The port restores
the movement-rate thresholds and construction radius: flying construction
updates its movement-rate bits after movement, invoking the `MoveRate` script
callback on transitions, and stage 4 selects a new orbit with the retail draw
order, installs its heading constraint, and clears pending controller events.
Orbit completion is an explicit unsupported stage in the fixture importer.

### Live ground-build restoration

Although ARAKING 3601's saved orders are patrols, its live initial primary
handler is `0x405560` (ground construction), stage 0, advancing to stage 1 at
tick 960. The live mission takes precedence when diagnosing a fixture; saved
order labels alone do not identify the active runtime behavior.

The importer restores a captured stage-zero ground-build mission using its
live type and coordinates, even when the saved order list differs. It requires
the observed initial scheduling state (no wait/pending events, unset deadline,
one build) and rejects other variants instead of guessing. With it, all
units' compared movement fields and gameplay RNG match the five live
transitions, as do construction work, HP, pools, flying mission state and 30
GetBuilt mission comparisons.

### Replay prefix

The reusable `tools/re/check_replay_fixture.py` compares unmodified fixtures
against native tick bodies without supplying later state. The early fixture
matches 34 transitions through tick 993 in both builds: compared unit motion,
flying-builder mission state, construction work/HP, resource pools, 321
gameplay and 393 CRT draws. This does not include outer-frame rendering or all
AI/script internals: outer-frame execution is excluded, and two outer-frame CRT
calls at tick 964 (the first returning to address 5163096) stay unmatched, so
this is not full-state parity. The
natural-production fixture passes 317 boundaries with 3,718 gameplay and 754
CRT draws. The 35th transition is the commander's nearby-threat response at
tick 994.

## Mid runtime capture

`reload-crt-state-01.json` captures boundaries 14468 through 14473. All five
offline retail tick transitions pass the live-capture comparison, including
the checked random streams. The importer reports 72 unsupported ground patrol
orders, nine carried-unit orders, three formation moves, one pursuit order,
one flying-builder order, two flight moves, two flight patrols, nine
factory-build orders, and one ground move whose stage/runtime state is
unsupported. These are observed order instances, not a count of independent
engine bugs.

## Late runtime capture

`reload-crt-state-01.json` captures boundaries 37447 through 37452. Relative to
the early capture, eight feature cells changed type (cell 24850 changed from
`verhut01` to `verhut01a`) and eight new feature cells appeared (431 versus
439 total). The fixture importer rejects changed features rather than silently
retaining the original map features, which would invalidate later comparisons;
changed and newly created features must be restored with their state before
the late fixture counts as initialized.

Offline retail replay matches two transitions, then stops at tick 37450 on an
unmapped external instruction at `0x7be8fb70`, returning to `0x53b002`.
This is a missing emulator dependency, not evidence of an engine mismatch.

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
patrol points, and additional threat-related candidate selection. A direct
attack command would not reproduce this queue; the planner and queued ground
patrol semantics go together.

The candidate trace at tick 994 records radius 560 and base centre raw
`(93847552, 4063232, 263716864)`. With no threat-target candidates, the first
patrol point takes four attempts and the second takes one; each attempt draws
radius/2, radius, then 65536. The first three ground-placement calls return 0,
then the next two return 1. Commander 3601 has a 2x2 footprint, maximum depth
20 and maximum slope 30; sea level is 58. The rejected footprints respectively
contain a slope of 38, `0xfffe` feature-footprint cells, and zero-height ground
58 below sea level. These are distinct reasons to keep the placement check,
not random retries that a fixed draw count could replace. The accepted points
are the destinations recorded in the resulting queue.

The response's first ground move is the base centre, not a sampled patrol
point: `0x40dc03..0x40dc1e` issues the centre with the computed radius; the
following call issues the first sampled point, and `0x40dc7b` appends the second
sampled point as a patrol. The captured destinations are respectively
`(93847552,263716864)`, `(108471616,288114608)` and `(90731912,295350504)`.
Cancellation through `0x4d6a50(unit,0)` preserves queued missions with flag
bit 4, removes the others and invokes their teardown handler (`0x4d6da0`).
It is not an unconditional queue clear, nor the World's `cancelBuilds` alone,
which does not reset `retailBuild`.

The empty-target-list sampler is shared in `src/sim/retailaipatrol.h` and used
by the flight patrol planner as well. `tools/re/check_ai_patrol_points.py`
executes native `0x40d7ad..0x40db3f` with only the placement host substituted:
4,000 deterministic cases agree in Debug and Release on accepted points,
exhaustion, placement-call counts, draw counts and final RNG state. Cases cover
small radii, map-edge rejection and up to 20 rejected placement queries.
This verifies sampling, not terrain placement or the complete commander response.

## Ground placement and patrol cores

The ground placement host preserves `0x507d10` semantics rather than calling
the general building-placement API: it excludes footprints ending exactly at
the last map edge, resolves feature-footprint references, checks blocking
feature flags and live entity occupancy (excluding the querying entity), then
applies minimum/maximum corner-height water limits and the dry/wet slope limit.
Roads do not bypass those slope checks.

`retailMobilePlacement` in `src/sim/retailplacement.h` implements the mobile
mode of `0x507d10`, including map-edge exclusion, feature-anchor resolution,
entity eligibility, water depth and dry/wet slope limits. The oracle
`tools/re/check_mobile_placement.py` executes the complete original function
without host substitutions: 6,000 cases (2,666 accepted) match in both builds.
Special feature markers `0xfffa..0xfffd`, including the road marker, reject
placement; an indirect `0xfffe` anchor whose feature is itself a sentinel
skips the blocking-feature check. This is different from navigation grading.

`retailGroundPatrol` in `src/sim/retailmission.h` owns stage advancement,
controller-event retries, queue-rotation requests and timer/random behavior.
The captured mission-definition table resolves kind 28 to `0x402b00` and
kind 34 to `0x403600`. The latter's four stage entries are `0x4038bc`,
`0x4038da`, `0x4038eb` and `0x403914`. After its pre-stage diversion checks,
it initializes the patrol queue, resets acceptance expansion, installs a goal
with expansion plus 4, then handles controller events. Event bits `0x700`
reset to stage 1 and return dispatcher result 6 (queue rotation); bit `0x2000`
adds footprint width times 32 to expansion and retries stage 2 after one tick.
`tools/re/check_ground_patrol.py` compares 5,000 cases in each build against
native `0x403600`, with diversions and assistance disabled and controlled hosts
for queue initialization, controller creation and combat order selection.
It covers all four stages, invalid stages, absent movers, controller event
combinations, radius wrap, the flag-clearing draw and tick overflow.

Ground patrol queue initialization (`0x4d6b80`) scans all queued orders for
flag `0x4000`, appends a return patrol at the unit's current position if absent,
and sets that flag on the current order. Combat polling (`0x4d8370`) first
requires an armed unit with unit flags masked by `0xc0000` equal to `0x80000`.
The assistance branch that follows combat is a separate host.

### Captured placement checks

`tools/re/check_captured_placement.py` compares `0x507d10` with the placement
core using initial captured cells, feature flags, entity slots and mobile type
limits. It samples occupied cells, feature cells, arbitrary positions and map
edges with both moving-occupant policies. The runner's sparse input includes
the queried footprint and referenced feature anchors; missing cells fail rather
than defaulting to empty. This is a captured-input core check, not verification
of the World placement host or map loader.
All 1,000 queries per capture pass: 338 accepted at tick 959, 288 at tick
14468 and 253 at tick 37447, covering 9, 73 and 106 mobile units respectively.

The placement plane is not the TNT feature markers copied verbatim. In the
early Valysia City capture, the TNT contains 24,853 road markers (`0xfffb`),
but the runtime feature plane contains none. Those original road cells become
21,599 empty cells, 3,188 footprint references and 66 `0xfffd` markers. Across
the full runtime map there are 3,543 `0xfffd` markers and 8,212 footprint
references. All 479-by-479 interior terrain quads agree exactly with the
runtime high/low corner heights, using the same down-right four-sample rule
as `NavGrid`.

## World placement host

`World::setMapPlacementFeatures` keeps a separate feature plane from map
loading, including non-reclaimable decorations and mana features; `setupMatch`
puts only reclaimable/flamable non-mana features in `features_`, and the
navigation obstacle overlay also blocks some nonblocking decorative features,
so neither of those reproduces the placement routine's feature flags. Retail
`0x50f789` turns roads into a separate cell flag and installs `0xfffc` directly;
`0x495360` installs named features in scan order, removes intersecting removable
features, and writes footprint back-references. Removal (`0x496380`) preserves
non-tail markers, and a failed later removal does not undo earlier removals.

`0x50eef0` installs `0xfffd` along the final two columns and the projected top
and bottom edges. The bottom scan marks the preceding row before retesting;
it is not a symmetric rectangular margin. The optional water-blocking branch
reads the scenario's `lavaworld` flag (`+0xd39`, parsed at `0x4c8330`).
`retailMapBoundary` matches all cells in 300 synthetic maps in both builds.
The World loader uses the ordinary, non-lava mode; lava scenarios are not
wired to the water-blocking branch.

`check_map_features.py` passes all 230,400 feature cells, all footprint
back-references, all height samples and 229,441 interior low-height quads in
the initial early capture. It feeds only the retail install/map name to World,
not captured cells. The final row/column do not have complete terrain quads;
placement rejects them before reading heights. The five tracked features the
early fixture importer removes are already absent from this reconstructed
plane: retail's installation overlap removes them.

`World::mobilePlacement` uses this plane, tracked feature liveness/blocking,
current unit footprints, landed-flight state and building yards; the Retail
AI's patrol-point sampler calls it (`src/ai/classicbrain.cpp`). The diagnostic
runner's `--placement-queries` option tests this host after restoring the
initial World fixture. `check_captured_placement.py --world-fixture ...`
matches 10,000 early queries in Debug (3,318 accepted) and 1,000 in Release
(338 accepted). The five recorded tick-994 patrol footprints return exactly
`0,0,0,1,1` against the initial World. Dynamic feature geometry/replacements,
later-save restoration, lava scenarios and occupancy transitions beyond this
initial fixture are not covered by these checks.
