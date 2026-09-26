# Transport and animation audit — 2026-09-21

This audit uses the locally installed `KINGDOMS.icd` and shipped unit scripts.
No retail executable bytes or assets are included in the repository.

Animation parity means accurate retail behavior: callback order, selected pose
and state, transitions, motion and relative placement, and playback/lifecycle
timing. It does not require pixel-identical rendering. Visual captures are
useful for checking representative behavior; pixel comparisons are diagnostic,
not a completion gate.

## Current completion status

The original air/sea transport and all-animation goal remains incomplete. The
sections below record successive findings; later corrections supersede earlier
implementation descriptions. Current open gates are:

Latest profile checks narrow several of those gates. All ten shipped water
transports now pass Lake Lokken's map-backed route, native grade, and joined
shore-release trace in both balances and Release, Debug, and optimized Debug.
The three shipped flying transports now pass native unload-through-release
traces with their selected FBI profiles and live height scans in both balances
and all three builds; those use a controlled ridge and direct circle rather than
a shipped map route. Native pickup reaches reciprocal attachment and
`BeCarried` completion for all three air carriers. Retail's code-30
  `Move_Seek_Pickup` auto-routes a passenger only for a `GROUND_PICKUP` carrier;
  with a `VTOL_Pickup` carrier it waits while the flyer pursues. The code-30
  GROUND2 auto-seek is therefore a ground-carrier behavior. A separate Lake
  Lokken probe moves Araarch along a validated shoreline while ZONROC tracks and
  boards it. A corrected queue fixture uses ordinary `Move_Ground` code 27,
  then appends `Move_Seek_Pickup` code 30 with retail's `0x20000` flag. Araarch
  begins moving. The O2 trace now runs both calls in retail's per-unit update:
  active dispatcher `0x4d8450`, then queued dispatcher `0x4d85e0`. At tick 4,
  the original `VTOL_Pickup` retires while Araarch still has code 27 active;
  queued code 30 is then dispatched, returns 8, and is removed by the queued
  dispatcher. Its controller field is null and there is no cargo attachment;
  the fixture reports one pending path request whose owner is not isolated.
  The trace stops before code 27 completes. The command path now preserves
  Shift-load and Shift-unload queueing: `Cmd::Load` carries the flag, and
  `World::loadInto(..., true)` appends passenger/carrier orders; `Cmd::Unload`
  carries the flag, and `World::unloadAt(..., true)` appends the carrier's
  approach and transfer. The transport integration test verifies serialization,
  preserved routes, boarding, and disembarkation after queued movement completes.
  This does not yet compare retail when both units have coordinated queued legs;
  the diagnostic above has an already-active carrier pickup. Reproduce that
  native diagnostic with
  `PYTHONPATH=tools/re python3 tools/re/probe_air_pickup_native_ground_move_seek.py --hpitool build-o2/hpitool --max-ticks 9000`.
The callback-bearing flyer
pose sweep covers all 26 shipped pairs, LIFBIRD now has a separate idle/fly
ambient-pose trace, CREAERI has a native flight-to-ground animation and
model-pose trace, and ZONROC's accepted-site `BeginLanding` now joins the
mission callback through its wing pose and model transform. These remain
controlled headless traces, not proof of
every map, order combination, capacity case, or rendered animation.
ZONHUNT's placed-construction root and site anchor match the native projection
in the current fixture; a synchronized capture using the reported map and
camera is still needed to judge the apparent northward separation in the live
view. A new client fixture checks weapon impact class selection, authored
animation lifetime, and fallback particles, but does not synthesize an actual
projectile hit or judge the beam/arrow's rendered appearance.

- Transport: live retail air and sea pickup/unload round trips have both been
  captured. Paired native/World pickup dispatcher traces match for 36 air/sea
  ticks; surface pickup callbacks match for 18 ticks. Paired unload dispatcher
  traces match for 17 air ticks and 19 sea ticks, with route arrival controlled
  at the navigator boundary. A full-map native Lake Lokken pickup now grades
  all 230,400 TNT cells, installs five real waypoints, moves the Vertrans for
  1,728 ticks, and boards the Araarch through native `0x51b4f0`/`0x51b5a0` at
  tick 1,731 using the native global entity-pool resolver, payload arguments,
  and reciprocal attachment links; the carrier mission retires. The fixture
  uses two synthesized pool records and a player row, zero-fills feature bodies,
  and controls visibility, grade-body services, mover services, and the UI
  effect boundary. A separate isolated fixture now executes native code-1
  carrier-pickup and code-2 passenger-order construction/insertion, then one
  `0x4d8450` passenger dispatch. Retail's `0x403430` handler reaches
  `0x4d4da0` and creates a passenger-mission-owned circle controller through
  `0x4e2500`; the navigator SetController slot is a controlled sink. This
  confirms order dispatch and controller setup, but not route search or
  passenger movement. The full-map trace now also stages a real code-2
  passenger order after native boarding and dispatches it through completion.
  Real `0x4d6ad0`/`0x4d6da0`/`0x519950` cleanup clears its queue/reference while
  preserving the cargo links. That remains a post-boarding cleanup check. A
  newer Lake Lokken native Araarch trace uses the actual sorted-registry
  `GROUND_PICKUP` code 21 and `Move_Seek_Pickup` code 30 orders. Its full-map
  route installs five waypoints, moves Araarch to `(3560.8,1840.4)`, 270.7 px
  from the 284 px pickup circle, and raises the native circle-arrival event.
  A continuous paired-dispatch trace runs both real handlers while Araarch
  moves; it calls native `0x51b4f0`/`0x51b5a0` at tick 6,306, retires code 21,
  and replaces passenger code 30 with `BeCarried` code 11. Circle arrival is
  accepted at tick 6,286. The headless fixture supplies a zero-filled per-entity
  `+0xc0` drawable/model-owner pointer and does not run the graphics traversal.
  A separate native trace confirms ordinary ground/VTOL pickup sets the carried
  cargo `+0x130` `0x8000` bit; the carrier's draw traversal skips passenger pose
  refresh and recursive drawable submission while it is set. Full raster output
  remains untested, and worker/player scheduling remains controlled.
  The independent World full-map pickup boards at tick 2,178; these tick counts
  are not compared
  because the search spaces differ. A moving-target pickup now joins the native
  air dispatcher and flight mover while the Araarch follows a host-scripted
  shoreline trajectory. Native code 30 auto-routes Araarch for a GROUND_PICKUP
  carrier, but waits for a VTOL_Pickup carrier. In the corrected queued
  Move_Ground/Move_Seek_Pickup trace, code 30 is actually dispatched from the
  queued list at tick 4, returns 8, and is removed while code 27 remains active;
  the original VTOL order also retires at tick 4. The trace ends before the
  ground move completes, and the existing World `loadInto` path clears prior
  orders, so queued-sequence parity is unverified. Full-map flying route search,
  additional maps, and native feature bodies remain open. The
  separate 70px callback fixture used a synthetic `transportdistance=86`;
  shipped Vertrans uses 300 and its native pickup circle is 284px.
  Eight deterministic boat unload-circle searches,
  five Per Mare Per Terras cases, and one Lake Lokken water-crossing case match
  retail's native reconstructed routes using World-produced grades, including a
  disconnected-water partial route after a World failure. The World air-route end-to-end
  test now includes a distant unload after a cross-water pickup. Sea-mover state
  matches for 1,000 physical steps through transfer, three route-point transitions,
  and the complete post-unload coast to rest. The fixture now enables retail's
  strict mobile-footprint check and supplies each terrain cell's four-corner
  maximum/minimum envelope; its ship-specific hull-height callback remains
  controlled. Paired
  retail/World flight-mover traces now match for 600 ticks on a point flight
  and 470 ticks of a distant VTOL unload-circle approach. A new integrated
  native-dispatcher/mover comparison matches 1,200 ticks from the distant
  unload approach through transfer, mission retirement, step-out arrival and
  the detached flight coast. The comparison normalizes the equivalent `0x500`
  arrival event because World stores it in the mission while retail stores it
  on the unit. These controlled traces pin terrain scanning and initial
  navigator state; they do not establish every live-map grade or flight route.
  The 600-tick point-flight trace matches through controller detach, the final
  mover step, and braking/coast; it closes the one-step arrival gap previously
  seen at tick 505. Ordinary flyers use a direct point controller, not the
  generic surface path-search worker.
  A separate 480-tick VTOL unload comparison now crosses a 100-to-220 terrain
  sector-height step: XYZ, altitude, flight dynamics, controller, mission and
  cargo fields match throughout, and native `0x4dc800` relinks the carrier's
  center sector seven times. Its fixture supplies retail's raw and 3x3-dilated
  sector-height planes and holds terrain rescans, so it does not by itself
  establish live rescan parity or every map's height transition. A companion
  map-backed trace builds those planes with retail `0x50e740` from per-cell
  terrain corners and leaves the native mover scan deadline live. Native and
  World match for 480 ticks, with eight center-sector relinks and six live
  scan-deadline advances. A joined map-backed VTOL unload then carries that
  ridge through arrival, passenger release, PARK and mission retirement: 488
  rows match across Release, Debug and optimized builds, with eight relinks,
  six live scans and only the expected tick-487 arrival-wake sampling
  difference. Additional maps and flight paths remain open.
  A blocked unload retry followed by mission removal and a new destination now
  matches in paired native-air and native-sea traces; World regressions cover
  both carrier classes. Native sea-unload recovery now also resumes the same
  in-range mission through transfer and release after the ten-tick retry. A
  same-trip out-of-range retry now has a composed dispatcher/route/mover trace:
  native and World agree on the remote `(400,160)` retry start, original
  `(500,500)` landing site, route, and circle radius. Their physical mover state
  matches for 1,000 steps, including five route transitions, circle arrival at
  step 317, cargo release at 334, and mission retirement at 335. The integrated
  sea retry fixture now keeps one native mission, carrier, passenger, mover,
  navigator and controller through retry, retail route search and physical
  movement; arrival detaches the controller at step 317, cargo releases at 334,
  and the real mission-removal routine retires it at 335. Scheduler request
  events and World-produced terrain grades are controlled harness inputs. The
  World crowded-shore retry regression now exercises real terrain, routing,
  movement, and attachment. A new asset-backed Lake Lokken World test follows a
  surface carrier from a real water start through route movement and releases
  its passenger at the exact selected shore point while the carrier's full
  footprint remains navigable water. Its paired Lake Lokken route reconstruction
  consumes effective grades produced by World and returns a usable partial
  route after reporting failure. The long Vertrans shore route from `(240,120)`
  to `(240,350)` now also matches retail's reconstructed route in Standard and
  Crusades when the search hook computes grades with native `0x508cd0` over
  TNT-derived Lake Lokken cells. The 260 distinct native query cells produce the
  same one-waypoint route ending inside the 266px unload circle; the 217 cells on the route origin
  line match World's attempt plane exactly. The route query keeps World's grade-5
  unexplored-terrain visibility override as a separate input; the narrow
  off-route sample's remaining differences are all that override. Native feature
  definitions encountered by the route are only the nonblocking `TarWave05`.
  Retail's real navigator setter installs the reconstructed path, then native
  `0x4dc800` and `0x51b2a0` match World for 1,470 consecutive ticks over
  TNT-derived Lake Lokken terrain in both balances; both movers enter the
  authored unload circle. This controlled trace postpones terrain scanning so
  both movers retain matched 0/0 movement modes, and has no live unit-occupancy
  blockers. A separate
  asset-backed `vertrans` route from `(240,120)` to the water target `(240,140)`
  now matches native reconstruction exactly in standard and Crusades: one
  waypoint, 57 native grade queries, and an endpoint inside the 266px unload
  circle. This is a short connected-water route, not a shore unload. Release,
  Debug, and optimized builds also complete a full World pickup-and-unload trip
  on Lake Lokken with the shipped `vertrans` and `araarch` definitions in both
  balances: the ship routes from `(240,120)` to the shore passenger at `(240,350)`
  through a load order, boards, then unloads at that same selected point. Both
  footprints remain physically placeable through a 60-tick coast, and the
  carrier remains passable in its water grid. A separate direct World unload
  from `(240,120)` to shore `(240,350)` also completes in 2,222 ticks in both
  balances. The first grade-plane dump belonged to an abandoned request; the
  completed search starts at `(240,123)` and targets goal cell `(239,349)`. The
  old native cost-search heap-root fault (`0x414367`) was a harness sequencing
  error and is resolved. The map's `TarWave05` feature is `blocking=0`; the
  shared overlay honors that flag instead of treating decorative wave art as an
  obstacle. A separate map-backed release test runs retail's unload dispatcher,
  placement, detach and PARK at the World carrier's completed circle position
  in both balances. A mobile Araarch now blocks that selected Lake Lokken
  shoreline during the joined live-scan mission: native `0x507d10` reads its
  live entity slot and cell occupancy, rejects the blocked footprint, then
  accepts the same site after the blocker walks clear. World releases one tick
  after retail (2253 versus 2252); carrier mover fields match through tick 2200,
  before the controlled retry window, and are not used as a parity gate during
  that retry. For the captured Lake Lokken Vertrans/Araarch route, retail's
  `0x416430` worker now installs a replacement from blocker-adjusted map grades
  on the same navigator/controller while the native unload mission and cargo
  remain active. The paired search packs World's captured grade cache and keeps
  retail's original `0x4139d0` cached/live dispatch intact. In the delayed
  blocker case, all five World waypoints match in Standard and Crusades across
  Release, Debug, and optimized Debug builds. An experimental always-on route
  search case has a separate direct-search discrepancy: the isolated captured
  attempt agrees on its first point but differs by one cell on later points,
  even though every native grade-dispatch result matches the captured plane.
  Replaying that same replacement through the native unload worker while the
  mission and cargo remain active delivers all ten World waypoints. The isolated
  always-on search-state difference remains unexplained. The replay begins at
  the captured World replan boundary and explicitly submits the replacement.
  A continuous collision-to-retry probe now keeps the native unload mission,
  controller, and initial route from `WORLDSEED` through the live blocker.
  Retail reaches the exact World replan position, gets two placement refusals
  from `0x507d10`, and `0x4e5150`/`0x416430` install all five World replacement
  waypoints. The earlier two-point result came from a boundary-seeded fixture
  that created a fresh native route/controller at the World replan position;
  it did not carry the approach history. That fixture also rewound its worker
  clock after enqueueing the retry. With continuous native history and a
  monotonic clock, the collision-driven and captured-bit controls both match.
  No production pathfinding change is justified by the old fixture result.
  Native TNT-backed terrain grading and reconstructed
  routes also match for Aratrans/WATER5 on the same long Lake Lokken shore route
  in Standard and Crusades: 698 distinct native query cells, 906 grade calls,
  one waypoint, and an endpoint inside its 385px unload circle. A separate
  Lake Lokken VerScout/Araarch route matches World across 867 native grade
  queries, then matches native mover state for 1,000 physical steps; the paired
  Vertrans/Araarch mover trace matches for 1,550 steps. Other maps and carrier
  profiles remain open. Per Mare Per Terras now has a second joined
  Vertrans/Araarch case: native TNT-backed grades, active terrain scans, the
  native unload mission, mover, shoreline placement and release all match the
  World trace through step 78. Its 54 distinct queried cells, 58 grade calls,
  15 scan deadlines and 16 placement checks pass Standard and Crusades in
  Release, Debug and optimized builds. Sea Dragon Spine adds a third
  Vertrans/Araarch case: three reconstructed waypoints and the joined live
  mission, terrain scans, placement, and release match through step 314, with
  176 distinct grade cells, 219 grade calls, and 74 scan deadlines; Standard
  and Crusades pass across the same three builds. Other maps and carrier
  profiles remain open. Lake Lokken also has a joined VerScout/Araarch unload:
  867 native route-grade calls, 1,400 matching mover steps, 691 matched live
  scan deadlines, 16 native placement checks and passenger release at step
  1400; its World end-to-end placement/coast fixture also passes. An
  experimental combined live-blocker trace is
  inconclusive because its emulator initially lacked World exploration updates
  and then omitted the blocker from its live-body query and occupancy plane; the
  resulting scan mismatch is a fixture error, not evidence of different gameplay.
  That initial trace did not deliver a replacement route in its combined
  mission context. Both the captured-bit `0x4e5150`/`0x416430` retry and the
  continuous native collision-driven retry now match all five World waypoints.
  The continuous case follows retail from the original map start with its first
  route and controller intact, reaches the same X/Z collision boundary as
  World, and carries that native state through the two refusals. The previous
  two-point result used a newly constructed navigator at the World boundary,
  not the native route history; its worker also ran after a clock rewind. That
  result was a fixture artifact, not evidence of a gameplay mismatch.
- Combat animation: scripted AimWeapon/FireWeapon readiness and delayed SET 23
  release are integrated, with authoritative display aiming and GET 33 turn
  input. AimWeapon, FireWeapon, and TargetCleared now enter the regular script
  scheduler at the native deferred phase. The native mover callback tail and
  display helper also agree on the coincident
  `TurnDirection → MoveRate → setSFXoccupy` order, and the display VM receives
  `BeginFlight` call-ins from the simulation snapshot. An exact-range regression
  now confirms that AimWeapon starts at the native inclusive maximum range and
  remains gated one pixel beyond it. Araarch's native projectile timing now
  matches World when callbacks run in retail order: SET23 at tick 39, release at
  tick 40, identical age-zero position/velocity/angles, tree impact at age 9,
  and clear-target impact at age 11. A separate native pool trace now covers
  insertion, one manager update and compaction per tick, tree impact, and same-
  tick retirement; World matches its launch and each active-tick snapshot.
  That projectile-pool fixture stops before impact damage/effects; a separate
  native impact-body trace is documented below.
  Special weapon cases, broader missing-script behavior, roster-wide engine-
  driven flight callback inputs, and final projected poses remain open. A headless
  native Araarch trace now confirms that losing visibility does not clear an
  already assigned live target in this path; World's explicit-target behavior
  agrees. Native `0x4dc800` callback-request traces cover all 151 movable
  shipped COB/FBI pairs that declare movement callbacks; their six raw patterns
  match the World helper across eight callback inputs. Full COB thread/piece
  timelines pass for ten representatives across ground, hover, water, and
  flying families (1,202 boundaries each), with callback events supplied by a
  controlled schedule rather than routed from every unit's mover. An added
  `check_native_mover_cob_join.py` probe captures native mover requests at the
  script-dispatch boundary and replays callbacks actually declared by four
  shipped COBs through the retail scheduler: Tarblack (ground), Tarlich
  (hover), Tarcship (water), and Aradrag (flying). Each declared callback
  starts and matches local COB thread/piece state; a second unchanged tick
  confirms edge suppression. The stronger
  `probe_native_mover_cob_transitions.py` now attaches those shipped COB VMs to
  the native unit and runs `0x4dc800` through retail's actual script lookup and
  scheduler. Full COB thread and piece state matches World for Tarblack
  (ground), Tarlich (hover), Tarcship (water), and Aradrag (flight) over 8–10
  boundaries in Release, Debug, and optimized builds. Same-target ticks are
  silent; reversal calls `TurnDirection(41)`; stopping calls
  `TurnDirection(0)` and `MoveRate(0)`; water resume calls `MoveRate(1)`; and
  Aradrag occupancy changes `5→4→5`. The full-roster wrapper now runs all 151
  movable callback-bearing COB/FBI pairs through that attached native path:
  1,280 complete VM boundaries match across 97 ground, 10 hover, 18 water, and
  26 flying units. Target, speed, mode/state, sectors and COB GET values are
  controlled, and the mover fixture stops before native position commit
  `0x51b2a0`. A separate native
  flight-mission probe runs BeginFlight producer `0x416c50` and accepted
  BeginLanding path `0x416cd0/0x417089` through retail's real `0x56c5c0` COB
  dispatcher and scheduler; all 2,170 VM state words match World after the next
  tick. Its mission/controller services are controlled; the separate Zonhunt
  pose-to-model geometry check below does not yet provide equivalent roster-wide
  coverage. A 103-tick
  persistent `zonhunt` construction trace also captures
  native mover requests: its only declared movement callback, `setSFXoccupy(5)`,
  fires once on tick 1 in both native and World, including through two retargets,
  arrival and the next hover goal. This verifies the request edge and World
  helper, not COB thread execution or rendered pose. A follow-up joins the
  native movement/callback trace to Zonhunt's display COB at five checkpoints.
  The root piece stays at zero local transform; its site-relative screen anchor
  shifts from 90.50 pixels north at tick 1 to 107.98 at tick 101 as the monarch
  gains altitude and follows the orbital ground path. The projection equations
  remain equivalent; the 0.5-pixel maximum is integer half-height rounding,
  not an independent framebuffer comparison. A new 104-tick display-COB join
  schedules BeginFlight, setSFXoccupy(5), and StartBuilding from the native
  construction-movement callback trace; all script-thread and piece state
  matches retail in Release, Debug, and optimized builds. Zonhunt child-piece
  pose-to-model geometry is checked separately at tick 101; roster-wide pose
  coverage and controlled-camera screen placement remain open. The pending-shot/dead-
  target sequence now joins target retirement to the next common weapon update;
  broader target-loss cases remain open.
- Main-menu doors: the Bink decoder uses the chroma matrix measured from
  retail's Bink DLL. All 348 frames in the 16 door clips pass decoded-frame
  comparison, with worst mean RGB error 2.086/255; native BinkWait cadence is
  33.00 ms against the encoded 33.33 ms interval. A headless native-method
  trace now verifies retail's four door hit rectangles, inclusive edges,
  hover/click state changes, and leave/re-enter timing. The local menu shares
  those rectangles and transitions, covered by `door_animation_test` in all
  three builds. The native-method fixture supplies synthetic Bink/button
  records and does not exercise the outer SDL/Windows event dispatch or compare
  the final menu composite.
- Build icon aspect: all 171 shipped build portraits measured across the three
  containing HPI archives are 63–64 by 47–49 pixels, matching the authored
  4:3 shape within one-pixel variations. The HUD uses a 4:3 icon box and scales
  each texture proportionally, so this path does not stretch the portraits.
  Resolution-by-resolution visual layout remains outside this asset/layout check.
- Cursors: authored frame timing, software/hardware rendering, enemy weapon-range
  feedback, and the native Revive, Load, and FindSite selector gates are covered.
  Native mode 3's Airstrike gates and active weapon selection are measured. The
  UnitDef `+0x264` bit comes from a primary WEAPON1 pointer; WeaponType `+0xc8`
  bit `0x20` comes from FBI `dropped=` and is represented separately from
  `subtype=Dropped`. The HUD maps the active weapon slot and follows native
  minimum-slot aggregation for mixed Airstrike selections. No shipped FBI uses
  `dropped=`, so this primarily covers authored/mod content. Static selector
  and setter-callsite audits found no gameplay return path for Capture, Pickup,
  or Teleport. Their shipped GAF sequences each have one frame, and the native
  updater preserves that frame, so no visible start phase remains to match for
  these modes. The registration audit confirms slot 12 is `cursorload` and
  slot 13 is `cursorunload`; native command tags map LOAD to mode 6/slot 12 and
  UNLOAD to mode 5/slot 13. The earlier mode-5-as-Load label was a test error;
  product mapping was already correct. Live pointer pixels remain un-compared.
  Hourglass is confined
  to the native modal file-picker path. End-of-order waypoint markers now use
  retail's global-tick frame interval (`2 × first GAF delay`) instead of the
  previous fixed three-tick interval. The live software and hardware pointer
  now share retail's native per-sequence frame indices and single countdown;
  switching cursor IDs preserves each sequence's frame instead of restarting.
  A headless probe joins the native setter and updater; it stubs frame-delay
  lookup and does not compare cursor pixels.
- Effects: feature burn art, authored lifetimes, layered flames, shadow clocks
  and tick-owned smoke are integrated. Authored projectile sprites now honor
  each TAF frame's anchor. The native-probed GuidedWeapon XYZ path is now wired
  into World launch/update/collision, lockstep state, and renderer positioning;
  ground- and air-target World regressions and paired native kinematics pass.
  Model-backed ballistic shots now check every native 3D substep for unit and
  environmental collision; an Arabow arrow's native feature-impact dispatch
  matches the World feature-damage regression. Death damage-flame callback and
  creation-dispatch timelines now match native for three scripts. The `tarmage`
  SET31 path is also traced from retail's native death-state builder through
  Killed/Dying dispatch, its timer, and tick-35 owner/list teardown; its
  standalone World timeline omits that outer owner gate, so the
  extra callbacks there do not establish a live mismatch. GameView now retires
  attached display effects at the native owner-cleanup deadlines measured for
  `crefire` SET26 (tick 1) and `tarmage` SET31 (tick 35), rather than retaining
  them to the 120-tick corpse fallback. `tarhel`'s death builder now also
  matches five SFX 260/261 callback rows at tick 0, and its native SET31 owner
  timer/list teardown runs at update 35 in the headless trace. That fixture
  controls SFX node allocation/insertion; other callback-to-removal and
  attached-emitter lifecycles remain open, along with debris details,
  shared cosmetic RNG/tick phase, collision/lifetime
  paths, and representative checks that live effects use the right art family,
  placement and lifetime. Feature smoke now follows retail's newest-first burn
  order and updates each burn's existing particles before that burn emits.
  Headless live-script fixtures cover `verpill`'s detached SFX 264 and
  Kirenna's (`vermage`) water-transition SFX 263 through skipped render ticks
  and owner removal, checking authored art, emission position/tick and lifetime.
  Drake's Fire Breath is a LOS `FlameEmitter` using the `flame` sequence, not a
  ballistic model; shipped `zondrake` passes the 11-profile fire roster in both
  balances. Native/local emission, scan, particle, draw-admission and delayed
  impact probes pass compositionally. No single paired shot with identical
  native/World geometry and random inputs is established.
  The shipped arrow, bolt, harpoon, and spear 3DO selection/pose path also has
  passing native draw and 4,096-case pose checks; all 145 moving shot slots in
  Standard and Crusades resolve their referenced models. The zoomed-out
  silhouette scaling now also covers the veteran spear aliases
  `verspear_10` and `zonterspearvet`; focused checks confirm both retain the
  same readability scaling as their base meshes without changing native model
  choice, pose, collision or lifetime. Focused Araarch draw probes confirm
  `araarrow` follows the native 3DO model path and returns before the yellow-
  streak fallback; straight-shot launch/draw, ballistic draw, pose, and shipped
  weapon/build timeline checks pass. The earlier laser-like appearance report
  has no current reproducible mismatch; it is not an open bug without a clearer
  case. The remaining
  projectile gaps are other effect lifecycles and uncovered collision/lifetime
  families. Native impact probing follows unit damage into retail's health
  fields: a 476-damage Arabow hit moves the fixture from 1000 to 595 HP, and an
  Arapult splash leaves HP 281/673/1000/281 for the center, near enemy,
  out-of-radius enemy, and same-owner unit. Native feature impact increments
  the map cell's damage word by 1250 per hit up to 3750 under a synthetic 5000
  threshold. A fourth impact now executes native removal/replacement and model
  cloning: feature type changes 0→1, damage resets to 0, and the replacement
  cell flag is set. The probe uses synthetic leaf model trees and shimmed C
  heap frees; authored feature rendering and replacement effect callbacks
  remain open.
  The World fix passes cross-build validation; this is not the arrow-to-yellow-
  streak fallback.
  Pixel-identical Glide output is not required.
- Overall animation parity: verify engine-driven callback timelines and final
  rendered behavior across the roster. VM/piece-transform oracle coverage and
  the current smoke viewport fix do not by themselves prove that requirement.

The native ZONROC `BeginFlight` call-in now also has a dedicated script-to-model
pose check. It covers that mission-driven transition separately from the generic
flyer mover callbacks; camera projection and rendered behavior remain open.

Keep deterministic simulation and retail pathfinding intact throughout. Passing
CTest and generic network runs is regression evidence, not proof of these gates.
The latest full CTest sweep before registering the optional ZONROC pose test
passes Release (53/53), Debug (56/56), and optimized Debug (56/56). The new
`native_air_transport_animation_pose` test then passed individually in all three
builds. The weapon impact effect-route client fixture is included in the Debug
suites; the Release configuration builds the changed client source but does not
expose the game-mode CLI needed to run that fixture.

## Transport corrections

The native eligibility function at `519f50` distinguishes three limits:

| FBI field | Meaning | Native type offset |
| --- | --- | --- |
| `transportcapacity` | Passenger count | `242` |
| `transportsizecapacity` | Sum of passenger costs | `244` |
| `transportsize` | Largest individual passenger | `240` |
| `transportedsize` | Passenger cost; zero defaults to final movement footprint area | `246` |

Previously the loader conflated count with size budget and used the carrier's
maximum-size field as passenger cost. Boarding now checks all three limits at
issue and arrival, including competing orders. Surface carriers reject water-only
passengers; flying passengers, prohibited passengers, unfinished units, other
owners, and fully submerged passengers are rejected. The model-top check uses the
full fixed-point model top (`14a`), not its truncated sight-height byte.

Native movement-class `MinWaterDepth` defaults to -10000 (`4dfb10`). A separate
transport eligibility bit preserves that classification without changing the
existing navigation-grid defaults.

### Unmapped native passenger-state gates (2026-09-25)

Retail's `0x519f50` is called during load-order validation (`0x403476`) and by
`GROUND_PICKUP` (`0x4088f4`, `0x408aa9`). It also requires both unit records'
`+0x108` float to equal zero and rejects either record when bit 4 is set at
`+0x114`. Those raw checks have not been tied to a reachable initialized unit
state or a World field, so they do not yet demonstrate a gameplay mismatch.
The 4,096-case capacity probe and native air/sea selection probe pass; mapping
these state gates needs a live record or a traced setter before changing
`World::canLoadInto()`.

Pickup and unloading use the carrier's `transportdistance`, including the ship
and airship differences. The native transfer stages (`408cxx`/`408fxx`,
`41ad4d`, `41b29b`) wait fifteen ticks before attaching or detaching a passenger.
Unloading releases passengers individually and rechecks the landing footprint.
Air pickup creates the reciprocal carrier approach; a passenger on another island
no longer has to walk across water to reach a stationary aircraft. Air unloading
also bypasses ground-component restrictions. Ship approaches retain their water
connectivity checks.

The display plays `mindspin` at the passenger/landing point and
`transportfx:transswirl` at the carrier, as initialized at `4bde75..4bdee6` and
started by the transfer stages. Effects respect visibility.

The movement search and ground-movement math were not replaced. The surrounding transport approach still uses the existing navigator host.
Unloading now checks the exact selected point through native mobile placement
instead of searching nearby cells. Full carrier mission scheduling remains under
audit; transfer-state parity alone does not establish the complete approach trace.

## Animation corrections and coverage

The main-menu door clips also have a separate color oracle. The former Credits
hover clip appeared orange/brown against yellow/gold background art because the
decoder used standard BT.601 chroma coefficients. `BinkVideo` now uses the
coefficient ordering measured from the shipped retail Bink DLL; the original
idle, hover-in, loop, and mouse-out playback states remain in the menu state
machine. The 16 reference-frame comparisons across all four doors pass with mean
RGB error between 1.151 and 2.058/255. This is close color agreement rather than
byte-identical decoding, and does not measure the complete frame-by-frame
transition timing. Detailed method and results are in
[`docs/bink-colors-2026-09-20.md`](bink-colors-2026-09-20.md).

The build-menu portrait concern also has an asset/layout check: all 171 shipped
`anims/buildpic/*.jpg` images in the three HPI archives that contain them are
between 63 and 64 pixels wide and 47 and 49 pixels high. The in-game HUD draws a
4:3 icon rectangle and fits the source texture using the smaller width/height
scale, preserving each portrait's aspect ratio. Generated model thumbnails are
square fallbacks and are fit into the same portrait rectangle without stretching.

Gameplay unit display VMs now use the same integer scheduler and piece controller
that are checked against retail `56c870`, rather than a separate floating-point
animation interpreter. This preserves native 30 Hz sleep quantization, bounded
thread slots, CALL/WAIT ordering, integer move/turn increments and spin ramps.
Render-frame accumulation allows two 60 Hz display updates to advance one native
tick. Mission scripting retains its existing map-command interpreter.

Every unit runs `Create`. The Priest and ambient birds retain their script-owned
controllers instead of having them reset to a manually selected fly/land loop.
This also preserves the Priest's concurrent construction controller. Effect
callbacks are installed before immediate startup notifications execute.

Display queries now use actual construction work remaining, integer health
percentage with the current maximum HP, authoritative yard-open state (including
blocked/closed yards), authoritative mover flags for road/water state, and the
native airborne vertical-speed percentage used by the bird script. Per-unit
cosmetic RNG remains separate from simulation RNG; identical VM semantics do not
mean identical random animation choices across two independent game runs.

Native `416cd0` queues `BeginLanding` as soon as a landing site is accepted,
before installing its final ground-height controller. Transport-capable flyers
also receive `EndTransport` first. The sim now carries that callback edge through
the render snapshot, where the VM starts those scripts in native order and avoids
restarting `BeginLanding` at the later grounded-mode transition. Focused mission
and renderer tests cover one-shot delivery and the transport callback order.

`check_script_state.py` also had a test-harness defect: bytecode exceeding 64 KiB
could overlap its emulated entry table. Separate allocations now cover large
scripts without corruption.

## Repeatable validation

- `python3 tools/re/check_transport_capacity.py`: 4,096 capacity decisions against
  the actual native eligibility routine, with other eligibility inputs held valid.
- `transport_test assets/game`: both balance modes; all 13 standard carriers and
  all 14 Crusades carriers, actual boarding/unloading orders, independent capacity
  limits, competing boarders, transfer delay/interruption, carrier loss, water
  crossings, coastal approaches, whole-footprint reservations, disconnected ponds,
  and superseded path requests.
- `transport_test --surface-unload-map-travel /home/pocket_geek/tak_data 'Lake Lokken' 240 340 240 350`:
  asset-backed World route, shore placement, cargo transfer, exact passenger
  landing, and a navigable-water check for the carrier footprint.
- `python3 tools/re/check_surface_unload_map_route.py build/transport_test --map 'Lake Lokken' --start 240 340 --target 240 350`:
  paired native route reconstruction using World-produced effective grades; the
  World search reports one failure while still supplying the partial route used
  by the successful end-to-end unload.
- `python3 tools/re/check_surface_unload_map_route.py build/transport_test --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' --start 240 120 --target 240 140 --carrier vertrans --passenger araarch` (repeat with `--crusades`):
  actual-profile Vertrans route reconstruction; one waypoint matches exactly,
  with 57 native grade queries and the endpoint inside the 266px unload circle.
- `python3 tools/re/check_surface_unload_map_grades.py --binary build/transport_test --hpitool build/hpitool --retail-root /home/pocket_geek/tak_data`:
  native `0x508cd0` versus World on all 217 Lake Lokken shore-route origin
  cells in Standard and Crusades. Exact match, including grade 4 at `(240,337)`;
  off-route differences are all World grade-5 unexplored-terrain overrides.
- `python3 tools/re/check_surface_unload_map_route.py build/transport_test --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' --start 240 120 --target 240 350 --carrier vertrans --passenger araarch --native-map-grades` (repeat with `--crusades`):
  reconstructs the long shore route using native TNT-backed `0x508cd0` grades
  for all queried cells, retaining World’s grade-5 visibility mask separately.
  The exact one-waypoint route matches and ends inside the 266px unload circle.
- Repeat the preceding command with `--carrier aratrans --passenger araarch`:
  the WATER5 route and native terrain grades also match in Standard and
  Crusades; 698 distinct map cells are checked across 906 grade calls, and the
  endpoint lies inside the 385px unload circle.
- Repeat the preceding command with `--native-map-mover-steps 1470` (and
  `--crusades`): after native `0x4e4ea0` route installation, retail's
  `0x4dc800` mover plus `0x51b2a0` height update match World on every XYZ,
  heading, speed, mode and terrain-flag sample through entry into the unload
  circle. The scan deadline is held beyond this controlled trace, and there are
  no dynamic occupancy blockers.
- Repeat it with `--native-map-mover-steps 2500 --native-live-unload` (and
  `--crusades`): one native `GROUND_UNLOAD` mission, carrier, passenger,
  navigator and circle controller remain joined through native map-grade route
  search, physical movement, circle-arrival wakeup, real `0x507d10` shoreline
  placement, cargo detach and mission retirement. Both balances release Araarch
  at Lake Lokken cell `(240,350)` after 1,488 paired movement steps; all 16
  native passenger-placement checks accept the selected site. As in the World
  movement fixture, the mission-poll and terrain-scan deadlines are held past
  the trace and there are no dynamic occupancy blockers. The first path request
  is serviced at the controlled route-search boundary; repeated asynchronous
  scheduler jobs with live rescans and blockers remain outside this check.
- For each `bin` in `build`, `build-dbg`, `build-o2`, run
  `"$bin/transport_test" --surface-unload-map-travel-type /home/pocket_geek/tak_data 'Lake Lokken' vertrans araarch 240 120 240 350 0` and repeat with final argument `1`:
  full World shore unload in standard and Crusades. It completes at tick 2,222,
  releases at the selected shore point, and keeps both unit footprints valid
  through the 60-tick coast. A single live run with repeated asynchronous path
  requests, terrain rescans and dynamic occupancy active through release remains
  open.
- `ctest --test-dir build -R '^transport_map_roundtrip(_crusades)?$' --output-on-failure`:
  the shipped Vertrans/Araarch Lake Lokken pickup-and-unload trip in standard
  and Crusades balance.
- `python3 tools/re/check_vertical_animation.py`: 4,096 airborne GET 30 results
  against `4dc1f0`, including attachment and refusal.
- `python3 tools/re/check_animation_roster.py assets/extracted/all/scripts
  --output /tmp/animation-roster.json`: all 204 scripts, four controlled query
  profiles and 1,500-tick callback timelines (816 runs). Compares native thread,
  static, piece-motion, RNG and unit-value-write state through construction,
  flight, combat, wind, cloaking and death callbacks. Sound playback and rendered
  pixels are outside this oracle's controlled host.
- `animation_roster_test assets/game`: all 202 loaded unit scripts through
  1,200-tick callback timelines using the renderer-facing VM at 60 Hz, comparing
  its exported poses and visibility with the integer reference.
- Existing animation/conjuring tests use the new display interpreter.
- `ctest --test-dir build-dbg -R '^script_transient$' --output-on-failure`
  (also `build-o2`): the live `verpill` SFX 264 fixture retains its authored
  asset and emission-time XYZ/tick across two skipped cosmetic ticks and owner
  retirement, then expires at the authored duration. The Release client omits
  the local game/dev command path, so this integration test runs in Debug builds.
- `ctest --test-dir build-dbg -R '^script_transient_263$' --output-on-failure`
  (also `build-o2`): the live `vermage` water transition emits SFX 263 on Lake
  Lokken; the detached `deathmagic:purpledeath` effect retains its authored
  asset and emission-time XYZ/tick across two skipped cosmetic ticks and owner
  retirement, then expires at the authored duration.

These checks provide broad execution coverage, not a claim of pixel-for-pixel
comparison of every camera angle, effect blend, or game-driven callback instant.
Protocol **179** carries the corrected shared navigation overlay, following
protocol **178**'s transport simulation changes and published 0.7.0 (protocol
177).

## Results in this checkout

- Release, Debug and optimized Debug: all targets rebuilt; **50/50 Release and
  52/52 in each Debug configuration** passed (154 passing executions).
- The Lake Lokken surface unload and its paired route reconstruction pass in
  Release, Debug, and optimized Debug. The route search returns two waypoints
  with one World failure; the World carrier follows that partial route, releases
  the passenger at the selected shore point, and keeps its full footprint in
  navigable water.
- The Lake Lokken Vertrans/Araarch pickup-and-unload round trip also passes in
  standard and Crusades balance across Release, Debug, and optimized Debug,
  including 60 post-release ticks, physical placement checks for both units,
  and water-grid passability for the carrier.
- The map-backed retail `GROUND_UNLOAD` integration fixture now carries one
  native mission from its real controller setup through retail route search,
  1,488 paired Lake Lokken mover steps, the native arrival wake, shoreline
  placement, passenger release and mission retirement in both balances. It
  holds the poll/scan deadlines past the controlled approach and does not add
  moving occupancy blockers; those full-scheduler conditions remain open.
- The data-backed `navblock` regression confirms Lake Lokken's shipped
  `TarWave05` decorative cells do not enter the shared movement-obstacle overlay.
- The same-trip sea retry route probe passes for 1,000 physical mover steps;
  retail and World agree on all route points, movement state, cargo release and
  mission retirement. This is a controlled composition of native dispatcher
  and mover fixtures, not a full live scheduler run.
- The native Arabow ballistic probe confirms one environmental-impact dispatch
  with no unit target. The World regression applies 476 damage to the blocking
  feature and leaves the selected unit unharmed. Projectile oracles also pass
  for 512 ballistic launches / 6,912 substeps, 8,192 environment cases, 8,192
  unit-quad cases, 432 straight-shot and 432 lightning timelines, and guided
  launch/motion/collision traces. The native probe intercepts the damage routine,
  so retail damage magnitude itself is not measured here.
- Cursor probes confirm Airstrike's native gates and active-weapon slot; source
  tracing finds no gameplay path returning the registered Capture, Pickup or
  Teleport slots.
- The current paired native/World probes pass for 36 pickup-dispatch ticks,
  18 surface-pickup callback ticks, 17 air-unload ticks and 19 sea-unload
  ticks. Unload arrival is injected at the navigator boundary; these checks do
  not establish physical route movement.
- All 816 native animation timelines passed; all 202 renderer-facing script
  timelines passed.
- Existing native-oracle sweep: 62/62; movement/search sweep: 15/15; Python
  reverse-engineering unit tests: 123/123.
- GCC and Clang at O0/O2/O3 agree on deterministic-math hash
  `8adc4762a852fadd`. ARM cross-builds were skipped because the local target
  headers/toolchain configuration is unavailable.
- Two fresh local client/server runs at 600 ticks agree on
  `b879f25ebe4532bf`, unchanged from the pre-change baseline.
- Headless software-renderer smoke runs completed, including a Zhon monarch
  conjuring a Hunter with both ends' particles visible.
- An isolated O2, single-thread display benchmark with 16,000 moving Aramon
  Archers and 120 frames measured about 0.76 ms/frame for the former interpreter
  and 6.45 ms/frame for the integer interpreter. This is a real cost increase;
  gameplay already distributes animation updates over its worker pool. It is
  not a 16,000-unit whole-game simulation-speed measurement.

## Continued query audit

The previous GET 29 oracle deliberately covered only refusal and attachment with
cardinal velocities and integral percentages. It did not establish correct
normalization for ordinary units. The renderer divided by nominal type speed and
truncated the percentage. Native `4dc100` instead truncates the length of the
horizontal velocity vector, divides by the individual base speed after road/water
multipliers, rounds to nearest, and clamps to 0..100. The snapshot now follows
that calculation, using the flight vector for aircraft and the native ground
step vector for surface movers.

Native GET 33 (`4dc3f0`) returns a signed percentage of the greater of the
terrain-scaled moving/pivot turn rates, clamped to -100..100. It does not return
BAM angles. The old comment and wheel test inferred that interpretation from the
script's +/-910 comparisons. Those branches are unreachable with the native
query. The renderer now normalizes the exact captured heading-word change, and
the script test preserves the retail outcome rather than manufacturing a value
of 2000 to activate the branch.

GET 28/34 read mover flags 12/11 (`50d262`/`50d295`). Reading the terrain again
from the render map can disagree with the mover's supported body height and the
point in the tick at which terrain flags were updated. Both queries now consume
the captured simulation flags.

`check_animation_queries.py` executes 8,192 controlled native cases and compares
16,384 GET 29/33 results: arbitrary velocity vectors, individual speeds, terrain
multipliers (including road priority), unsigned turn-rate words, rounding,
attachment/refusal, and zero denominators. All pass. Renderer snapshot tests also
check individual-speed normalization, road scaling, flight vectors and attachment.
The all-script oracle's turn-query profiles now stay within the native +/-100
range.

## Completion remains unproven

The broad original request still requires stronger evidence in these areas:

- A paired, map-backed retail/World air and sea pickup-to-unload trip, including
  the passenger's queued load order, its own dispatcher/controller/navigation,
  crowded-shore placement, and cancellation/recovery. Native pickup and unload
  phases, World round trips, and retail live round trips have separate coverage;
  the full joined trace is still open. The Lake Lokken pickup fixture begins
  with a synthetic reciprocal passenger-order placeholder and only stages the
  real code-2 completion after boarding; it does not run the passenger's
  pre-boarding approach or movement on that map.
- Engine-driven animation callback inputs and final poses across the roster.
  Native COB transitions now match World for all 151 movable callback-bearing
  unit scripts, but the test controls callback inputs and stops before position
  commit. Native flight mission VM state and Zonhunt's interpolated pose-to-3DO
  geometry are covered separately; engine-produced call-in coverage and
  controlled-camera screen placement remain open across the roster.
- Remaining effect families and their playback, attachment, transition,
  placement, and lifetime behavior. Existing native probes cover representative
  projectiles and attached effects, not every shipped emitter/collision path.
  Pixel-identical output is not required.

These items remain part of the goal, not excluded from its definition of success.

## Movement notification audit

Native `4dc600` computes `setSFXoccupy` from the unit mode, signed integral body
height, sea level, waterline and model top. Its values distinguish shallow water
(1), floating (2), submerged (3), dry ground (4), and airborne (5). In the deeper
partially submerged band it retains the previous value. These conditions have
ordered precedence, including submerged overriding floating. The renderer now
captures this state each simulation tick and notifies on changes; it previously
sent only 5/0 to a subset of flyers and omitted the ground/water transitions.
`check_animation_occupancy.py` verifies 8,320 native states and notification edges.
This is display state; the narrower simulation-side construction notification
host still warrants a separate callback audit.

Native `4db350` computes `MoveRate` tiers 0..3 from speed, applied turn and the
terrain-scaled `moverate1`/`moverate2` thresholds. Turning without translation is
still tier 1. Aircraft use their horizontal velocity length for the threshold
comparison; ground movers use their scalar speed. Refused or attached movers
report zero. Both omitted thresholds default to twice the type's max velocity
(`4bfd8e..4bfdcb`); they are now loaded from the FBI data. The display previously
sent 100/0 for surface vehicles and 5/0 based on airborne state for airships. It
now sends the native tier on changes for every mover. The script-owned ambient
controllers remain running.

The revised GET 33 profiles were rerun across all 204 scripts: all 816 native
callback timelines passed. Release, Debug and optimized Debug also passed all
42 CTest cases each after the query corrections, before these additional
notification changes.

After the notification corrections, all targets were rebuilt in Release, Debug
and optimized Debug and all 126 CTest executions passed again. The native
`check_animation_moverate.py` oracle passed 8,192 ground/air tier and notification
edge cases. Snapshot tests cover terrain scaling, pivoting, repeated refusal,
aircraft vector speed, attachment and landed/airborne occupancy. Deterministic
math still matches `8adc4762a852fadd` across GCC/Clang optimization levels; ARM
remains skipped for missing local target headers. A fresh rendered Zhon conjuring
smoke test shows both monarch and construction-site particles with the new
notifications; this remains a smoke test, not a retail pixel comparison.
Two fresh 600-tick local client/server runs both produced `b879f25ebe4532bf`,
unchanged from the prior baseline. All 123 Python reverse-engineering unit tests
also passed after these additions.

## Air/sea-unload mission traces and passenger dispersal

`probe_transport_unload.py --output /tmp/retail-transport-unload.json` now runs
52 controlled cases through the native `41ae20` handler. It observes the exact
selected cell in both placement calls: the second call changes the final
placement flag, not the position. A successful check counts 15 ticks and enters
stage 4. An obstruction accepted by the second check advances to retry stage 3;
that stage resets the transfer counter, sleeps ten ticks and returns to stage 1.
At six approach attempts it returns dispatcher result 9. A site rejected by both
checks produces blocked chatter and result 8. Those observations identified the old nearby-cell spiral as incorrect. The
transfer/retry stages are now ported, and unloading checks the selected point.

Native stage 4 also gives the released passenger a temporary PARK order. Its
padding is `(random(3)+random(3)+1)*16`, plus half the next passenger's footprint
diagonal (in world units) for each remaining passenger, capped at six passengers.
All 36 combinations of random endpoints, remaining-count boundaries and footprint
shapes in the probe match `retailUnloadParkPadding`. The simulation now installs
that PARK order after air or sea unloading, using the existing retail ground navigator.
This clears the landing point instead of leaving every released passenger still.
The same probe with `--sea` executes native `408d50`: all 52 sea cases also
pass, including the same 36 spacing calculations. Both transport types now use
the verified dispersal helper. Full carrier mission sequencing remains incomplete; the transfer/placement
corrections below do not establish every carrier approach or pickup event.

With passenger dispersal installed, all targets rebuilt in all three build
configurations and all 126 CTest executions passed. The air-crossing test now
checks the actual PARK order, its final-passenger radius, movement away from the
drop position, and cancellation by Stop, in addition to boarding/unloading.

The final air-and-sea version passed all 42 CTest cases in each build (126 total),
including a stronger clearance check: the air passenger moves far enough that
its entire original footprint is free. Both native probes passed again (104
observations total, including 72 C++ spacing comparisons). Two local network runs
after the initial air-dispersal change retained hash `b879f25ebe4532bf`; these runs
are a general lockstep regression check, not a transport-heavy network scenario.


## Exact landing points and interrupted transfers

`retailUnloadTransfer` now matches both native handlers' stages 2 and 3, including
counter updates, stage changes, retry delays, placement-call ordering, effect
starts, rejection and retry-limit results. The probe compares those returned
states with the C++ helper for both air and sea, in addition to PARK spacing.
The World host uses the loaded map's native mobile-placement query, preserving
its distinction between movable and permanent obstructions. It no longer searches
for a different point or rounds release coordinates to a waypoint. A permanent
obstruction ends the unload order with cargo retained. A movable blocker enters
retry, resets transfer progress, waits ten ticks, then re-enters the approach/check
phase; the following transfer receives its full effect interval.

Tests cover both air and surface carriers with fractional release coordinates,
a building over the selected point, and a unit walking into a partially completed
transfer and later walking away. The existing queue/path tests now request valid
landing terrain: one old fixture clicked water and another clicked a pond-edge
slope, relying on the incorrect spiral to find another location. Multi-passenger
unloading waits for the earlier passenger to clear the same point.

The Crusades siege transport exposed a separate approach defect: it was driving
onto its own drop point, preventing placement. Land carriers now use the native
`transportdistance-34` approach tolerance (408e84), through the existing ground
mission/navigator. All standard and Crusades carrier fixtures pass. Sea and air
approach/step-out scheduling and initial handler dispatch still require a full
mission trace comparison; those are not proven by these transfer-stage checks.


## Air unloading while landed and native range edges

An initially landed aircraft over its drop point exposed a flight-controller
stall: the transfer host was keeping the aircraft grounded. Air unload now issues
`BeginFlight`, runs the flight controller during the beam/retry phases, and keeps
the step-out flight destination separate from the selected passenger destination.
The landed-carrier regression checks takeoff, altitude gain and successful release.

The air probe compares 42 native step-out goals across headings/ranges with
`retailUnloadStepOut`. Both air and surface range checks also compare 4,096 cases
each against the actual handlers. Native distance testing floors the two squared
fixed-point components separately before adding them; keeping a floating-point
squared distance disagreed at range edges. All 8,192 comparisons now pass. The
probes record 94 air and 56 surface stage observations, in addition to range cases.
This still does not establish every pickup or mission-event transition.

## VTOL unload controller after mission retirement

The full native trace exposed a mover-lifetime detail that the dispatcher-only
trace could not show: VTOL_UNLOAD retires after its one-tick empty tail while the
flight point controller remains attached to the mover. The carrier keeps flying
to the step-out radius, receives arrival/release, then coasts on the retained
navigation state after the controller pointer is cleared. World now retains that
controller independently of the unload order and models the detached coast; the
state is included in deterministic hashing and suppresses VTOL standby until the
coast ends. The integrated fixture compares position, heading, speed, velocity,
navigation, controller, cargo, passenger, effects and parked state for 1,200
ticks against the retail dispatcher plus `0x4dc800` mover. All rows match after
normalizing the `0x500` event-storage difference described above. Retail GUI was
not launched for this comparison.

## Standing orders and weapon holstering

GET 46 (`50cee6`) returns the composite standing unit order, not whether the unit
has an attack target. The type loader reads its two bits at `4c005d`; spawn copies
them to the entity at `511c5d`. We now preserve this value separately from the
GUI stance and individual move/fire defaults, including the default value 3.
Both the authoritative script host and renderer consume it; stance changes and
AI stance initialization update it, and deterministic hashes include it.

`check_animation_standing_order.py` compares 2,048 getter/setter/default cases
against native `5198a0` and GET 46/2/3. The loaded `verbers` and `vercrus` scripts
are additionally exercised through repeated offensive/passive/defensive changes:
their weapons remain drawn while idle in either active stance and holster in
passive stance even when an explicit target order exists. This fixes an engine
input error that the controlled script-timeline oracle alone could not detect.

After the standing-order correction, all targets rebuilt in Release, Debug and
optimized Debug; all 42 CTest cases passed in each (126 total). The 123 Python
unit tests and deterministic-math compiler matrix passed again. Both native
unload probes and the new standing-order oracle passed against the rebuilt tools.
Two fresh 600-tick local client/server runs agree on `b2694d13eea05acf`, with no
reported errors. This replaces the earlier baseline because the newly retained
standing-order field is deliberately hashed. These remain general lockstep runs,
not a transport-heavy multiplayer validation.

The next callback review includes combat aiming. Native `52ff09..52ff3d` delivers
three AimWeapon parameters: the two unsigned angle words and the zero-based
weapon slot; `51a853..51a86b` delivers that slot to TargetCleared. The renderer still
substitutes a zero pitch and periodically re-aims on a display timer. The slot 1
substitution for single-weapon aiming/clearing identified here is corrected below.
The remaining angle/timing choices have not been validated by the all-script VM
oracle and require complete native angle/event observations. Pickup mission scheduling and
rendered effect fidelity likewise remain open; this document is not a declaration
of complete retail parity.

## Combat callback slots and actual firing events

Single-weapon AimWeapon and TargetCleared now receive slot 0. The native callback
probe `check_animation_callbacks.py` executes 768 packed display AimWeapon events,
all three FireWeapon slots, and six target-clear transitions. It verifies native
argument counts/values and that clearing an already cleared target is silent.
The packed display event carries the high bytes of both angle words; direct
simulation callbacks use the full words. This does not yet settle which path to
use for the port's independent display scheduler.

Firing visuals previously alternated slots cosmetically and always used weapon 0
for muzzle queries, sounds and effects. World now records a per-tick mask of the
slots that actually fired, after mana/reload gating. The render snapshot carries
this display-only event, and each set bit drives its own weapon data, QueryWeapon
and FireWeapon. Simultaneous shots are retained. FireWeapon is also delivered
while moving, allowing the shipped script to combine gait and firing instead of
silently suppressing the event. The old cosmetic slot alternator is removed.
The real Aramon AA tower regression compares those events against each weapon's
reload transitions throughout 900 simulation ticks and passes.

Further native aiming observations identify direct aim `52c4e0` and ballistic
aim `52bdf0` (the latter calls trajectory-angle routine `52bd10`). For a direct
shot at horizontal distance 100, vertical offsets -100/-25/0/25/100 yield pitch
words 8192/2555/0/62981/57344. Thus the renderer's constant zero pitch is only
correct for equal-height source and target points. Correcting this requires the
actual muzzle/target coordinates and the weapon-specific trajectory calculation;
it remains open, together with callback scheduling.

After the firing-slot changes, all targets rebuilt in all three configurations;
all 126 CTest executions passed. The native callback probe passed, and GCC/Clang
deterministic-math checks retain `8adc4762a852fadd` (ARM toolchain unavailable).
Both repeated 600-tick local network runs retain `b2694d13eea05acf` with no
reported errors: firing-slot events are cosmetic and do not alter hashed gameplay.

## Direct/guided aiming geometry and target pieces

`retailDirectAim` matches native `52c4e0`: heading uses the full fixed-point
horizontal vector and native body heading, while pitch uses signed whole-word
height and horizontal distance. `check_animation_aim.py` compares 8,192 angle
pairs, including subpixel boundaries. The probe explicitly sets normal Win32 x87
precision; Unicorn's default single precision otherwise creates spurious one-word
rounding discrepancies.

The renderer now uses this calculation for Line of Sight and Guided weapons.
Their native vtables identify this same aiming routine (including lightning,
fire, freeze, stone and mind-control Line of Sight subclasses). Each uses its
actual animated QueryWeapon origin, an exact unprojected position snapshot,
and the target's SweetSpot. The display VM exposes its integer piece state for
these queries, avoiding a float-pose round trip and camera-projected coordinates.
A cached model hierarchy maps the script pieces to authored offsets and vertices.

Native SweetSpot (`4dd4f0` -> `4dd2a0`) differs from QueryWeapon: it returns the
center of the selected piece's mirrored model-space vertex bounds, whose extrema
start at zero. It applies neither piece offsets nor animation nor body heading.
A complete captured-unit comparison exposed this distinction and corrected an
initial implementation that transformed the vertices. Another 1,024 native
comparisons cover the bounds center, including empty and one-sided geometry.
The existing integer piece-origin transform is unchanged; all 10,030 native
rotation cases still pass. Real `vertower.cob` tests aim above, level and below the source,
checking cannon pitch and the slot-0 aim-ready callback; all pass along with the
202-script display timeline tests.

At this stage ballistic pitch, moving-target lead and callback cadence remained open;
the ballistic elevation implementation and checks are recorded below.
The direct calculation deliberately does not substitute for a ballistic arc.
Captured-unit geometry comparisons and rendered visual comparisons are being
used to check the integration beyond the isolated angle/bounds calculations;
passing those alone will not establish complete aiming fidelity.

The full captured geometry check now covers ten unit types: Hunter, Lodestones,
Beast Handler, Zhon monarch, Keep, Veruna weapon tower (`verat`),
ship (`verpar`), Veruna monarch and Warrior. All their captured piece origins and
bounds match. The initial monarch and ship failures exposed native `4dd18f`'s
addition of body roll/pitch as well as heading to the root transform. The shared
piece-origin helper now accepts those angles; default-zero calls retain their
prior behavior. The renderer captures existing surface pitch/roll for this query.

The captured Zhon monarch passes all 47 piece origins/bounds and the ship all 26
when supplied their native attitude. This validates the transform, not the port's
aircraft attitude generator: the current display still eases bank/pitch using
heuristic scales, and flight body angles are not authoritative snapshot fields.
Replacing that heuristic and delivering the same attitude to drawing and muzzle
queries remain required animation work.

After the captured-attitude correction, all targets rebuilt in Release, Debug and
optimized Debug; all 126 CTest executions passed. The deterministic-math matrix
retains `8adc4762a852fadd`. An initial fire-test render smoke completed but its
camera missed the combat fixture, so its screenshot is not visual validation.

The next aircraft-attitude trace is native `4da620`: it retains mover components
`+14/+18/+1c` with fixed factor `0xf333`, adds an input vector, rotates the
horizontal pair by body heading, and computes body roll/pitch using per-type
scales and world gravity. This differs fundamentally from the current client's
frame-time-based turn/climb easing. The complete caller/input trace and native
oracle remain to be implemented before replacing that heuristic.

Two fresh local 600-tick network runs after these changes agree on
`b2694d13eea05acf` without reported errors. The projectile-fixture render smoke
also completed, but at the available viewport size its camera still excluded the
units; neither screenshot establishes rendered aiming parity. The native geometry
comparisons and real-script pitch tests are the current evidence for this change.

## Aircraft attitude kernel

`retailFlightAttitude` now matches 8,192 native `4da620` updates, including the
three retained acceleration components, both body angles, arbitrary heading,
positive/negative per-type scales and varying gravity. The retained state decays
by `0xf333/65536` before adding the velocity delta. Both native angle calculations
use the rotated lateral component; pitch does not use vertical speed. The full
flight-velocity oracle now checks the caller's attitude input against its actual
before/after velocity delta instead of discarding that argument silently.

At this stage the kernel was not yet wired into gameplay. Integration needed map gravity,
retained mover state, landed-mode decay and matching body-angle delivery to both
rendering and muzzle queries. The subsequent integration is recorded below; this isolated oracle alone did not
establish replacement of the frame-time-based aircraft attitude heuristic.

The caller check passes all 4,000 velocity updates and 4,000 navigation cases.
The legacy map-loader branch at `50f136` sets its destination to world+`19e64`,
and `50f240` writes fixed gravity `0x1fdb` at destination+`68` (world+`19ecc`).
That value is 8155, matching the preserved retail capture. Thus reading the OTA
`gravity=112` directly would be wrong for the kernel's raw per-tick input; the
other loader branch must still be checked before generalizing this default.

## Aircraft attitude integrated

Further map-loader inspection shows the other supported branches converge on
`50f21b`/`50f240`, so the raw gravity used here is the shared initialized value
8155. `4da750` applies the zero-input decay once on transition to landed, and
returns without changing attitude when the mode is unchanged. It does not ease
landed units flat every frame.

World now retains the three acceleration components per flyer, updates them
from the actual flight velocity delta after heading changes, and publishes native
body roll/pitch. Landing performs the single transition update. The retained
state joins the deterministic checksum; groundPitch/groundRoll were already hashed.
The display consumes these same angles for its body attitude and muzzle queries;
the frame-time turn/climb heuristic and its history fields have been removed.
The real Zhon monarch regression checks that flight produces both retained input
and nonzero body attitude in both balance modes. All flyer combat and transport
roster cases pass with the integrated controller.

The render matrix's composition relative to the root piece still needs a full
native geometry comparison; supplying the correct angles alone does not establish
that every rendered vertex matches retail. Ballistic aiming, lead, callback timing,
and pickup mission sequencing also remain open.

The integrated version rebuilt all targets in all three configurations and passed
all 126 CTest executions. The 8,192-update attitude oracle, 4,000 velocity/input
cases and 4,000 navigation cases passed against rebuilt tools. The compiler
matrix retains deterministic-math hash `8adc4762a852fadd`; ARM remains unavailable.
The general network smoke has no significant flyer population and therefore does
not substitute for a flight-heavy lockstep scenario.

## Ballistic animation elevation

`client/retailaim.h` implements the display-side trajectory elevation from native
`52bd10`. The native oracle compares 8,192 float-input cases, both arc preferences,
vertical offsets, varying gravity/scales, and unreachable/vertical-shot sentinels.
All match. `52bdf0` converts the unreachable sentinel to zero for AimWeapon; the
renderer now follows that behavior and supplies the calculated elevation for
Ballistic weapons instead of unconditional zero pitch.

Weapon speed follows the native loader's quantization: `530f21` multiplies the
FBI value by 65536/30, truncates, then `531ccb` divides that raw speed by the
ceiling of its 16-unit substep count. The aim routine multiplies the stored speed
by that count before the float conversion. The renderer preserves this sequence.
Real `vermort.cob` tests verify low/high arc barrel angles and slot-0 aim readiness;
all pass alongside the 202-script display timelines.

This is an animation aiming correction, not a replacement of the projectile
simulation. Moving-target lead and engine callback timing remain under audit,
as do full rendered root-transform parity and transport pickup sequencing.

After ballistic elevation integration, all three build configurations rebuilt
successfully and all 126 CTest executions passed. The 8,192-case ballistic oracle
also passed against the rebuilt optimized tool. This change is confined to display
math/callback arguments and tests; it does not change authoritative projectile or
pathfinding state.

## Moving-target animation lead

`retailAimLead` matches native `51aa50` in 8,192 controlled complete target-point
queries, with the SweetSpot sink held constant. Cases cover all three velocity
components, shooter/target height differences, no-lead flags and zero weapon
speed. Retail measures the three-dimensional distance from the shooter center to
SweetSpot, truncates flight time to fixed point, applies `0xcccc`, and independently
multiplies the target mover's velocity components. The renderer now snapshots
those components and supplies this led target to direct/guided/ballistic aiming.
Stationary structures and no-lead weapons retain the current target point.
The existing authoritative projectile simulation is unchanged by this display fix.

The shared display speed conversion retains the native substep quantization used
by both trajectory pitch and lead. Engine callback cadence and readiness handling,
rendered root-transform parity, and transport pickup mission sequencing remain
open; matching aim geometry does not establish those behaviors.

After lead integration, all targets rebuilt in all three configurations and all
126 CTest executions passed. The native lead and ballistic oracles passed again.
No authoritative gameplay fields or pathfinding calculations changed in this step.

## Weapon readiness handshake: native controller evidence

`tools/re/check_animation_readiness.py` exercises native `50d450` setters and
`52fff0` readiness gates. It substitutes only the angle-calculation sink; mana
cost, reload checks, tolerance comparisons, mover-heading checks, and flag
updates execute in the retail binary. The 4,096 setter cases verify SET 21 clears
bits 3–7, SET 22 sets bit 3, and SET 23 independently sets bit 4, for each of the
three weapon slots. Each setter raises the unit script-state event.

The 8,192 gate cases cover wrapped headings/pitches, tolerance boundaries, mana,
reload, script acknowledgement, ground movers and aircraft. Retail compares
heading change against the weapon tolerance and pitch against half that value.
An angle outside tolerance decrements the three-bit refresh counter when nonzero.
Missing script acknowledgement alone does not decrement it. Reload and insufficient
mana return before geometry evaluation. A successful gate retains the new angles;
a failed gate restores the previous ones. Applicable movers additionally require
body heading within at least 512 BAM of the desired heading.

These are controller observations, not an integrated fix: the display still uses
its existing fixed refresh interval, and authoritative firing is not yet gated
by the complete script handshake. Full aim-start/refresh transitions and the
simulation/display ownership of readiness remain outstanding. This step changes
only the native regression probe and this audit, so existing binaries remain
current; no additional gameplay or pathfinding changes were made.

## Display aim controller integration

The display's 0.33-second aim timer has now been removed. Each of its three
weapon slots owns a `RetailAimState`, connected to the display VM's SET 21/22/23
callbacks before Create. State remains local to that animation's worker; the
main thread consumes it after the pool joins. Switching weapons aims the selected
slot, while independent weapons retain separate handshakes. Reload values are
copied into the render snapshot, avoiding reads from the live simulation.

The readiness probe now compares the C++ helper against 4,096 native SET cases,
4,096 full aim-start calls and 8,192 readiness calls. The start probe substitutes
valid target/range predicates and geometry but executes the native start decision,
flag update and script notification. All 16,384 cases match. The actual Watch Tower
COB also verifies one uninterrupted long turn, seven out-of-tolerance refresh
steps, another completed turn, TargetCleared's reset, and immediate reacquisition.

This integrates the handshake into display aiming; it does not yet establish
complete weapon-controller parity. Target eligibility/range checks still come from
our order/snapshot logic, body-facing checks use the display target point, and
mana availability follows the current simulation's cost model. Authoritative
firing still does not wait for the COB aim acknowledgement. These boundaries must
be reconciled when the native combat controller is integrated into the simulation.
Transport pickup sequencing and rendered transform/effect comparison remain open.

After display integration, all targets rebuilt successfully in Release, Debug,
and optimized Debug, and all 126 CTest executions passed. The native handshake
comparison also passed against the rebuilt optimized tool. The live client logged
a successful AimWeapon notification in the firing fixture. Screenshot smoke runs
so far do not establish rendered parity with retail; their camera framing must be
fixed before they can serve as visual evidence. No authoritative simulation or
pathfinding calculations changed in this integration.

## Pickup range and transfer-stage observations

Pickup now uses the same native fixed-point distance comparison as unloading:
`408992..4089b9` (surface) and `41a9f8..41aa1f` (air) independently floor each
squared horizontal component. Previously both the passenger boarding check and
the reciprocal air-carrier check used a floating-point circle. New World tests
cover the observable boundary: a passenger 150 units along one axis and half a
unit along the other begins boarding a distance-150 carrier without approaching.

`tools/re/probe_transport_pickup.py` executes both complete native handlers with
controlled eligibility and external sinks. Its 2,048 boundary cases take the
in-range branch, signal the passenger to stop, clear the transfer counters and
sleep one tick. Another 4,096 cases cover transfer-stage retry counts, signed
passenger speed, beam progress and completion; both attachment calls also match
the observed five-argument contract.

The probe exposes remaining timing differences. Native pickup waits for positive
passenger speed to reach zero; each moving check clears beam progress, increments
a retry counter and sleeps six ticks. Four retries abort air pickup; ten abort
surface pickup. A stopped passenger starts the sound and both effects once, then
waits fifteen one-tick intervals before advancing to attachment. The transfer
stage does not repeat the approach range check. Our current passenger-owned
countdown still zeros speed immediately and rechecks range, so this range fix
does not claim complete pickup mission parity. Carrier-owned selection, passenger
stop events, retries, air-carrier step-out motion and attachment-stage timing must
be integrated together next.

The pickup range change rebuilt all targets in all three configurations; all 126
CTest executions passed. GCC/Clang O0/O2/O3 retained detmath golden
`8adc4762a852fadd` (ARM legs skipped for missing target headers). Two local
600-tick client/server runs retained `b2694d13eea05acf`. Those general multiplayer
runs do not exercise the full pickup mission; the fractional boarding regression
is covered by the focused World tests and native handler observations above.

## Pickup transfer integration

`retailPickupTransfer` now implements the verified moving-passenger retry and
beam-delay stage. Its C++ outputs match all 4,096 native transfer cases. The
pickup probe additionally compares 1,024 native air departure coordinates against
the shared step-out helper, across headings and transport distances; all match.
The existing 2,048 fractional boundary observations and attachment sinks pass.

World pickup explicitly initializes its mission state instead of inheriting the
unload stage. Entering range cancels the passenger route, starts braking through
the existing ground mover, and waits one tick. The transfer phase respects native
six-tick moving retries and fifteen one-tick beam intervals. Once committed, it
no longer restarts the range check. Air carriers continue their departure flight
controller throughout this phase instead of freezing. Waiting air passengers are
serialized against the carrier's current pickup request; replacing that queue
ends their outstanding request. Queue-head inspection avoids a full queue scan
for every waiting passenger on every tick.

World regressions cover gradual braking, delayed effect start, reboarding after
PARK dispersal, fractional range, departure-controller activation, serial air
pickup, completion after leaving the approach radius, and cancellation. All
standard/Crusades carrier roster boarding/unloading checks pass.

This integrates the transfer phase, not the entire native mission system. The
phase is still hosted on the passenger order, rather than the carrier's native
pickup mission. Native carrier selection among multiple nearby passengers,
surface-carrier serialization, complete approach/event dispatch, and retry-abort
queue ownership remain unverified. The original animation audit also still needs
authoritative combat readiness and rendered transform/effect comparison.

The final transfer implementation rebuilt all targets in Release, Debug and
optimized Debug, and passed all 126 CTest executions. The native pickup probe
passed against the rebuilt Release tool. The determinism guard and GCC/Clang
O0/O2/O3 golden checks passed (`8adc4762a852fadd`; ARM headers unavailable), and
two final 600-tick local client/server runs agreed on `b2694d13eea05acf`. The
multiplayer fixture remains general smoke coverage, not a transport-heavy trace.

## Native renderer transforms: piece motion and body attitude

The firing screenshot fixture now disables edge scrolling/follow and frames its
units using the actual viewport. The dummy pointer at the window edge had been
moving the camera during delayed captures. Captures now visibly include the
Watch Tower and Thirsha; these are client smoke captures, not retail pixel matches.

A new native renderer oracle executes `5ad090` and `4eea20`, including the actual
vertex writes. It confirmed three additional conversion errors: script roll had
the wrong sign, X/Z script translations had the wrong signs, and body roll had the
wrong sign. Retail's model loader mirrors authored X/Z before rendering. Expressing
its transforms in our authored basis requires negating all three piece angles and
X/Z moves; Y moves retain their sign. Body pitch and roll both negate before the
piece tree, with the existing negative heading outside it.

`scriptTransform` and `modelBodyTransform` now share this conversion across unit
geometry, shadows, effect anchors and the model viewer. Body attitude is read
from the snapshot directly for ground units as well as flyers; the former had
previously been omitted, and shadows/effect anchors had omitted body attitude.
The historical model-rendering notes and CLAUDE orientation rule were updated to
point to this verified convention.

`check_model_transform.py` matches 4,096 randomized native vertex cases (piece
translation/rotation, body attitude and heading), with maximum coordinate error
0.000153 world units. `check_captured_model.py` additionally compares all 2,374
vertices through the full 293-piece hierarchies of ten captured retail units:
zonter, zonlode, zonhand, zonhunt, verkeep, verat, verlode, verpar, vermage and
versword. All pass; maximum captured-model coordinate error is below 0.000023
world units. The tolerances accommodate float matrix rounding and retail's final
fixed-point vertex rounding. A new CTest guards native translation/rotation
axis directions without needing game assets.

These checks establish the shared model-coordinate transform, including animated
hierarchies; they do not establish final pixel/rasterization, texture, effect
lifetime, visibility, or every engine callback timeline. Combat readiness in the
simulation and the outstanding transport mission handling remain open. This
rendering correction does not change simulation movement or pathfinding state.

All targets rebuilt in all three configurations after the renderer correction.
All 129 CTest executions passed (43 per configuration), and the 4,096-case native
renderer comparison passed against each configuration's rebuilt tool. The final
client smoke capture `/tmp/model-render-thirsha.png` shows the units in frame and
completed without runtime errors. Native model geometry is verified as described
above; the smoke image does not replace retail pixel comparison.

### Script effect attachment follow-up

Script flame/smoke attachments now resolve their animated piece on each draw,
including parent transforms, body attitude, heading and projected flight altitude.
EXPLODE stand-in particles and bitmap effects now start at the animated piece
rather than the unit center plus a static model-Y offset. Removed the separate
static-height lookup. The native EMIT_SFX entry at 0x50da20 calls the model update
before reading the transformed emitter origin for extended effects; nonextended
beam effects instead read transformed vertex endpoints and remain unported.

This is an attachment correction, not full particle parity. The existing
one-effect-per-kind lifetime approximation, effect mapping, debris stand-ins,
and native vertex-ended beam classes still require comparison and replacement.
The shared transform has the native oracle coverage described above. An initial
client smoke run exited cleanly but used an invalid fixture type, so its image
does not validate emitter placement. All three configurations rebuilt successfully
and all 129 CTest executions passed after this attachment correction.

The corrected `zonfire` fixture produced
`/tmp/effect-anchor-sacred-fire.png`, visibly showing the Sacred Fire's flame and
smoke at its model. This confirms the draw path executes; it is not a retail
pixel comparison or a moving-piece attachment oracle.

### Surface pickup serialization

GROUND_PICKUP (0x408860) validates a reciprocal Move_Seek_Pickup passenger
mission and advances one selected passenger through its transfer stages. The
previous World implementation only installed reciprocal carrier orders for air
pickup, allowing several surface passengers to complete their beams together.
`loadInto` now queues reciprocal surface carrier missions too. Only the active
passenger transfers; other surface passengers may continue approaching, then
wait within range. A surface carrier brakes during the committed transfer
instead of entering the air departure controller. Replacing the carrier queue
ends waiting passenger requests for both carrier classes.

New World regressions cover sequential boarding and cancellation with both a
land carrier and a water-domain carrier at a real coast, with the path service
enabled. Capacity tests now wait for sequential transfers while asserting that
the size budget is never exceeded. They retain the separate count/size checks.
The native pickup probe still passes 4,096 transfer states, 2,048 fractional
boundaries, attachment sinks, and 1,024 air departure goals. Cross-compiler
simulation determinism passes with golden `8adc4762a852fadd` (ARM variants skipped
for unavailable target headers). Two 600-tick local network runs agree at
`b2694d13eea05acf`; this generic fixture is not transport-heavy parity evidence.

This closes the simultaneous surface-transfer gap. Transfer state still lives
on passenger orders, and native selection of nearby alternative passengers,
full approach/event dispatch, and retry-abort ownership remain unfinished.

All targets rebuilt in Release, Debug and optimized Debug after the surface
queue change. The final three CTest suites passed all 129 executions, including
the standard/Crusades carrier roster. No movement/pathfinding algorithm changed.

### Nearby pickup selection

Native GROUND_PICKUP and VTOL_PICKUP do not always finish the first distant
request before serving nearby passengers. In approach stage 1, when the selected
passenger is outside transfer range, both handlers scan the owner's unit array
for live eligible passengers whose active Move_Seek_Pickup refers back to this
carrier. They collect the in-range candidates and call random(candidate count),
then push a pickup ahead of the interrupted mission.

`check_transport_selection.py` executes both native handlers for 512 randomized
16-unit lists, controlling only allocation, queue sinks, eligibility and random
results. It checks active/dying flags, reciprocal mission identity/target,
geometry filtering, candidate array order, random bound and selected target.
The current selected passenger remains valid and out of range in these cases;
this probe does not cover dispatcher scheduling or no-candidate approach timing.

World now considers eligible nearby requests before continuing a distant pickup,
orders candidates by unit id rather than command order, and uses the shared game
RNG to choose one. The selected request moves ahead of the distant mission.
Existing path work is canceled when that mission is interrupted. New World tests
for both air and surface carriers verify canceled-request exclusion, RNG bound
and index, preservation of the distant mission, and completion of the nearby
passenger first. No movement/pathfinding algorithm changed.

Remaining transport work includes carrier ownership of transfer state, full
approach/event dispatch and its polling cadence, retry-abort ownership, and
end-to-end native mission timelines. Candidate selection is now implemented;
that does not establish the complete carrier controller.

Selection validation: all targets rebuilt in all three configurations and all
129 CTest executions passed. The existing native pickup transfer probe also
passes. Cross-compiler determinism retains golden `8adc4762a852fadd`; two local
600-tick network runs retain matching `b2694d13eea05acf` hashes. As before, those
network runs are general lockstep checks, not a transport-specific native oracle.

### Carrier-owned pickup stages and initialization timing

Pickup initialization, beam progress, retry count and sleep deadlines now live
on the carrier's GROUND_PICKUP/VTOL_PICKUP order. Passenger load orders only
approach surface carriers or brake for the carrier's stop request. Carrier
completion attaches the passenger and removes the completed pickup mission;
beam progress no longer depends on ticking a passenger-owned transfer machine.
The renderer now observes carrier pickup state for mindspin/transswirl effects.

Both native handlers wait one tick after initialization, then another after
entering range. The old passenger-hosted code omitted the first wait. The updated
`probe_transport_pickup.py` drives native stages 0 through 3 with their sleep
results and compares each tick with `transport_test --pickup-timeline`. For
stationary, initially in-range passengers, both air and sea now match through
attachment on tick 18: initialization at tick 1, entering transfer at tick 2,
fifteen effect waits at ticks 3 through 17, attachment at tick 18. The existing
4,096 transfer-state, attachment, range and air-departure comparisons still pass.
This timeline controls controller/FX sinks and does not prove distant-approach,
landing-recovery or complete event-dispatch parity.

World regressions check carrier-owned counters and exact attachment timing with
both carrier-first and passenger-first update order. Moving-passenger retries
belong to the carrier as well. Retry exhaustion still cleans both local requests
to avoid an orphaned load order; the native counterpart's subsequent abort/event
response remains a separate verification item. Existing cancellation, capacity,
coastal serialization, nearby-selection and roster tests remain in place.

All targets rebuilt in Release, Debug and optimized Debug; all 129 CTest
executions passed. Native pickup state/timeline comparisons pass against all
three binaries, and the 512 native selection lists still pass. Cross-compiler
simulation determinism retains `8adc4762a852fadd` (ARM target headers unavailable);
two 600-tick local network checks agree at `b2694d13eea05acf`. Full transport
mission scheduling and animation/combat/effect parity remain open as described
above; no pathfinding algorithm was changed.

### Preserve pickup departure destination during movement

The pickup mission correctly constructed retail's departure point, but
`tickFlightMovement` classified every pickup as pursuit and replaced that point's
X/Z with the passenger location on each movement update. Pickup stage 2 now
preserves its departure destination, as unloading already does; stage 1 still
tracks the passenger. An integration regression checks the native-oracle-backed
departure coordinates after multiple actual flight updates, in addition to the
existing controller flags/radius checks. This fixes the movement integration gap
that the standalone departure-coordinate oracle could not detect.

`check_transport_selection.py` now also executes 60 no-candidate approach cases,
including the actual native sleep routine. Surface pickup requests a navigator
radius of transportdistance minus 16, sets event mask 0x709, and waits 15 ticks.
Air pickup installs a passenger pursuit controller with radius transportdistance
minus 1, sets mask 0x729, and waits random(6)+6 ticks. Those observations establish
the next approach-host requirements; this turn does not claim their complete
integration or event-response parity.

Departure integration validation: all targets rebuilt in all three configurations;
all 129 CTest executions passed, including the new persistent-destination check.
The native pickup state/timeline probe passes, as do cross-compiler determinism
(golden `8adc4762a852fadd`, ARM headers unavailable) and two local network runs
matching `b2694d13eea05acf`. These do not replace the remaining approach/event and
animation parity work. No pathfinding algorithm changed.

### Pickup approach polling and wake dispatch

The carrier approach now uses the native polling masks and delays: 0x709/15 ticks
for surface carriers, 0x729/random(6)+6 for flyers. `retailPickupApproachWait` is
compared directly with all 60 native approach observations through the rebuilt
C++ test executable. Pickup consumes matching unit/pending events and timer
expiry before re-entering its handler. During an approach wait, ground movement
continues and air pursuit advances persistently; transfer waits retain their
separate braking/departure behavior. Using ordinary nonpersistent air movement
while waiting could otherwise consume the pickup order on reaching its point.

World regressions verify both carrier classes: a new nearby passenger is not
selected until a polling deadline or a matching movement event wakes the mission;
movement continues during that wait, deadlines remain stable between polls, and
the consumed event is cleared. The event test injects the event explicitly. It
proves pickup dispatch, not that every native controller event is generated at
the correct moment. Native controller radii, approach navigator integration,
failure-event responses and air pursuit vertical behavior still require work.
Existing exact stationary-transfer and selection probes continue to pass.

Polling validation: all targets rebuilt in Release, Debug and optimized Debug,
and all 129 CTest executions passed. Native selection/approach and pickup
state/timeline probes pass. Cross-compiler determinism retains
`8adc4762a852fadd` (ARM target headers unavailable), and two local 600-tick
network runs agree at `b2694d13eea05acf`. Controller-event generation and the
other completion gates above remain open.

### Air pickup pursuit geometry and generated arrival

Air pickup now installs the native persistent passenger pursuit goal: flags
0x11, strict horizontal radius transportdistance minus 1, and the passenger's
world position/height, capped at height 511. Flight movement preserves this
pursuit height rather than replacing it with cruise height; the existing native
navigation rule still uses cruise height when farther than 160 units away.
The transfer departure controller remains distinct and keeps its own point.

`check_transport_pursuit.py` executes retail's pursuit constructor (4e3f70),
radius setter (4e4540), target getter (4e41d0, including its native coordinate
query), and arrival predicate (4e43d0). Only reference bookkeeping is substituted.
All 4,096 randomized target/height/radius/position cases match the C++ helper,
including one-fixed-bit offsets at the strict arrival boundary and the height
clamp. These tests cover geometry, not a complete native moving-world timeline.

Native 524c10 posts 0x100 after controller acceptance; the pursuit controller's
4e41b0 reports persistence while its target exists. Our flight update now posts
that event into the pickup mission without deleting the pursuit order. A World
regression puts an active carrier inside the controller radius before its poll
expires, lets the actual flight update produce the event, and verifies transfer
on the next tick followed by boarding. It also checks that nearby navigation
uses the passenger's height. Unlike the earlier polling test, this test does not
inject an arrival event. Surface approach controllers, failure/recovery and full
mission traces remain open.

Pursuit validation: all targets rebuilt in all three configurations; all 129
CTest executions passed. The new 4,096-case native pursuit oracle passes against
all three binaries. Pickup transfer/timeline and selection/polling probes still
pass. Cross-compiler determinism retains `8adc4762a852fadd` (ARM target headers
unavailable); two local network checks agree at `b2694d13eea05acf`. The full
transport and animation goal remains incomplete under the gates above.

### Surface pickup failure response

The native approach gate at 4089fa..408a19 aborts GROUND_PICKUP only when its
passenger is outside transfer range, has exactly zero speed, and either the
carrier has no mover or a 0x200 controller-failure event was received. Nonzero
speed, including a negative raw value, bypasses that abort. VTOL_PICKUP has no
corresponding gate. The in-range branch precedes failure handling and still
starts transfer even if the same wake includes 0x200.

`retailPickupApproachAborted` is now used by the carrier host. The native
selection probe compares 100 air/surface, mover-presence, event and raw-speed
combinations directly with the C++ helper. World regressions check moving versus
stationary passengers, both carrier classes, in-range precedence and consumption
of the failure event. On abort, the host cancels outstanding local path work and
cleans the matching passenger request, consistent with its existing retry-abort
cleanup. Complete native counterpart cleanup remains unverified.

These World tests inject 0x200. This closes the handler-response gap, not the
remaining surface navigator/controller event-generation and radius integration.

Failure-response validation: all targets rebuilt in Release, Debug and optimized
Debug; all 129 CTest executions passed. The native selection/approach probe now
also passes its 100 failure combinations. Cross-compiler determinism retains
`8adc4762a852fadd` (ARM target headers unavailable), and two local 600-tick network
checks agree at `b2694d13eea05acf`. The broader completion gates remain open.

### Surface pickup navigator integration

Surface carrier pickup now uses the existing circle-goal navigator and search
service with radius transportdistance minus 16. Each pickup is a distinct order
leg. Installing route waypoints preserves load/pickup identity on the leg and
retains the carrier mission, timers, retry state and controller on its final
waypoint. Passenger transfer checks read that final mission rather than a
waypoint's default state. Transfer/removal retires the whole active route while
preserving subsequent pickup requests.

Circle arrival/detachment, empty-route failure and search notifications now feed
the pickup mission's pending events. Route retries use its retained mission point
and controller identity; incomplete routes retain their native endpoint rather
than appending an unreachable passenger point. Polling replaces the approach
controller using the existing route-retention predicate. A nearby-passenger
interruption resets the interrupted pickup to initialization and discards its
old approach waypoints before installing the temporary pickup. The core search,
steering, collision and waypoint algorithms are unchanged.

New World regressions exercise a water-domain ship routing around a peninsula
to board two queued shore passengers, preserving pickup identity through every
observed route waypoint. They check the native approach radius, navigator-produced
arrival/detachment before a poll deadline, and actual empty-route failure from a
boxed-in ship. The latter then verifies the failure reaches the abort handler
when the passenger stops. Arrival and failure events in these tests are generated
by the navigator rather than injected. These integration tests complement the
native component oracles; they are not full native-world timeline comparisons.

All targets rebuilt in Release, Debug and optimized Debug; all 129 final CTest
executions passed, including the expanded transport tests and carrier roster.
Native pickup state/timeline and selection/approach/failure probes pass.
Cross-compiler determinism retains `8adc4762a852fadd` (ARM target headers unavailable),
and two local 600-tick network runs agree at `b2694d13eea05acf`. Transport mission
trace coverage and the remaining animation gates are still open.

### Passenger mission identification and native scheduling

The actual `Move_Seek_Pickup` handler is **403430**, identified from the mission
registration descriptor (handler at 5eb88a, name pointer at 5eb89b). The earlier
scratch investigation of 407f90 was examining the following GUARD descriptor.
Its rejection of a flying target is therefore not evidence that a passenger
cannot seek an air transport. No implementation change should be based on that
misidentification.

`tools/re/probe_transport_passenger.py` now executes 403430 and the real native
sleep routine, with controlled eligibility, mission-name lookup and navigator
installation sinks. All 49 cases pass. Observations:

- Matching attached-carrier and requested-carrier pointers return completion (5),
  before eligibility checks; even two null pointers take this equality branch.
  Attachment to another carrier returns abort (8).
- The passenger scans the carrier's linked mission chain for either Ground_Pickup
  or VTOL_Pickup. The first such mission chooses the approach behavior; this
  lookup itself does not require that pickup's target be this passenger.
- Stage 0 copies the carrier's full fixed-point position into the passenger
  mission. A surface pickup installs a circle controller of transportdistance
  minus 16 and waits on 0x789 for 30 ticks. Air pickup detaches any passenger
  controller and waits on 0x89 for 30 ticks.
- At stage 1, event 0x80 takes precedence: detach, clear retries, return 1.
  Otherwise any 0x700 navigator event detaches and waits on 0x89 for 30 ticks,
  returning 2. Without those events it sets stage 0, sleeps five ticks and
  returns 4.
- Stage 2 increments the retry count and sleeps 30 ticks, returning 2 through
  count five. Count six and later abort with 8. Unknown stages return 7.

These observations expose a remaining implementation gap: World's passenger
load branch currently brakes or directly approaches according to range/carrier
state, without this native passenger controller, event mask or retry schedule.
The new probe is an executable native specification, not a port-comparison test
and not evidence that this gap is closed. Next integration must preserve routed
pickup legs while adding passenger scheduling and the native transfer stop event.
No simulation or rendering code changed in this investigation.

### Passenger scheduling core and dispatcher regression

`retailPassengerPickup` now implements the verified stage response independently
of host eligibility, carrier mission-chain lookup and controller ownership. The
native probe compares all resulting stage/counter/mask/deadline changes and
approach/detach calls against the C++ core in 4,096 additional cases. Cases include
retained masks and deadlines, combined events, signed counter comparisons and
32-bit tick/counter wraparound. All match in each of the three build variants.

A normal transport regression also runs this handler through the existing retail
mission dispatcher for both carrier classes. It verifies initial approach,
30-tick polling followed by the five-tick restart, stop-event wakeup and immediate
entry into the attachment wait, and abort after the sixth retry. The native
carrier handlers set passenger unit event 0x80 at 4089cf and 41aa33; this is the
same event consumed by the passenger handler before navigator events.

This core is not yet called from World's load-order branch. Runtime integration
must replace that branch's direct movement with the passenger's circle controller
and route/event lifecycle, and deliver the carrier's stop event through the
mission scheduler. Existing carrier-owned braking is not proof of that behavior.

Scheduling-core validation: all targets rebuilt in Release, Debug and optimized
Debug; all 129 CTest executions passed. Cross-compiler determinism remains
`8adc4762a852fadd` (ARM target headers unavailable). Both local 600-tick network
runs agree at `b2694d13eea05acf`. These are regression checks; the new helper is
not yet in the live World's transport path, so they do not prove its integration.

### Live passenger scheduler and navigation integration

World's load-order branch now calls the verified passenger scheduler. It selects
the active leg's final mission, checks the carrier's pickup queue, installs the
surface passenger circle controller (transportdistance minus 16), or detaches it
for an air pickup. Carrier transfer entry posts unit event 0x80; the passenger
consumes it through the native mask and enters its independent attachment wait.
Its wait/retry counter is distinct from the carrier's moving-passenger beam retry.

Passenger route waypoints retain load/target identity and the final mission.
Arrival, detachment and route failure now feed the passenger transport mission,
just as for carrier pickup. Retrying preserves eligible routes and the existing
navigator admission behavior. Detaching or aborting retires the entire active leg,
not only its first waypoint. Core path search and steering algorithms are unchanged.

World checks now assert passenger masks, deadlines and controller radius, both
unit update orders, separate passenger/carrier counters, and an actual passenger
route around water to a nearly stationary carrier. The latter exposed two old
fixture assumptions: a passenger may retain its route during stage-0's five-tick
restart wait; and the default synthetic brake took 61 ticks to stop, exceeding
the native carrier's ten six-tick moving-passenger retries. The successful boarding
fixture uses a faster brake; dedicated retry-exhaustion tests remain in place.
The fractional-edge surface case now checks transfer admission rather than
assuming the passenger never begins its smaller-radius approach.

Native probes still pass: 49 passenger precondition cases, 4,096 passenger
scheduler comparisons, 4,096 carrier transfer states, 2,048 range boundaries,
1,024 departure goals, the stationary tick-18 boarding timeline, 512 selection
rosters, 60 approach controller/wait cases and 100 failure cases. These establish
component behavior and selected integrated cases; complete native-world paired
passenger/carrier traces and recovery/cancellation dispatch remain open.

Live integration validation: all targets rebuilt in Release, Debug and optimized
Debug; final CTest runs pass all 129 executions, including the added routed
passenger regression. Cross-compiler golden remains `8adc4762a852fadd` (ARM
headers unavailable), and both local 600-tick network runs match
`b2694d13eea05acf`. Generic network checks are not a transport-heavy parity trace.

### Native cancellation ownership and passenger wake timing

`tools/re/probe_transport_cleanup.py` executes native queue removal (4d6ad0),
mission destruction (4d6da0) and target-reference unlinking (519950). Twenty cases
cover pickup/passenger masks, head versus queued removal and presence of an
owned navigator. Allocator release, controller detachment and owner animation
notification are controlled sinks; the queue and reference operations are real.
The reciprocal passenger mission and its unit events remain byte-for-byte intact.
Only the retiring mission's target reference is unlinked. No immediate passenger
order deletion or synthetic passenger wake is issued in these cases.

The same probe then runs native 4d8450 with the real passenger handler 403430
after removing the carrier pickup. A sleeping air passenger with mask 0x89 stays
pending until a subscribed event or its deadline; an unrelated 0x100 navigator
event does not wake it. At the wake the missing carrier pickup is detected and
the native dispatcher removes the passenger mission.

World now follows that ownership: carrier initialization/approach/transfer aborts
retire only their own pickup leg, and passenger eligibility/queue validation runs
after event/deadline admission. Its active controller can continue while it sleeps.
World regressions cover cancellation at tick 31, early event-8 cancellation, an
ignored air-arrival event, and eventual cleanup after native carrier failure.
Retry-exhaustion checks allow the passenger's own subsequent wait to expire.

This closes the directly observed immediate-counterpart-deletion discrepancy.
Carrier-side validation admission, queued interruption/recovery and complete
paired native/world movement timelines still need verification; the ownership
probe does not emulate navigator destruction or owner animation callbacks.

Cancellation validation: all targets rebuilt in Release, Debug and optimized
Debug; all 129 CTest executions pass. Native cleanup, passenger scheduling and
carrier transfer/timeline probes pass. Cross-compiler golden remains
`8adc4762a852fadd` (ARM headers unavailable); both local 600-tick network runs
agree at `b2694d13eea05acf`. Overall transport/animation completion remains open.

### Authoritative aim integration audit and native post-fire lifecycle

Current-source inspection confirms that World creates a RetailScriptState for
all scripted units, but its ScriptHost does not implement weapon SET 21/22/23,
its combat path never invokes AimWeapon, and tryFire uses range/reload/heading
checks without the native script acknowledgement. This is a real remaining
simulation gap, not merely a missing display test. The display-side RetailAimState
also currently survives FireWeapon without an explicit native post-shot reset.

New `tools/re/probe_animation_fire.py` executes native projectile creation
530220, with controlled allocation, target lookup and the weapon fire virtual.
All 4,096 cases pass, including arbitrary existing flags, simulated synchronous
script SET 21/22/23 inside the fire virtual, and allocation failure. The native
routine initializes projectile owner/creation tick, calls the fire virtual, then
clears weapon flags with 0xff0f and deducts mana. Thus it clears the aim countdown
and bit 16 after the callback, while preserving slot bits and bit 8. Allocation
failure invokes no fire callback and preserves both flags and mana. This ordering
matters: clearing flags before the callback would retain a callback's SET 23 bit,
which the native routine clears afterward.

The probe covers this projectile-creation base routine; it does not establish
that every weapon subclass uses it. The other firing paths must be identified
before applying its flag retirement universally. No runtime code changed in this
investigation. Authoritative aim integration and display post-fire behavior remain
open; the new executable evidence specifies one required part of that integration.

### Common weapon update: animation starts before projectile creation

The caller of 530220 is the common per-unit weapon update **52ae90**, not a
weapon-specific virtual. It loops the three weapon records (selected slot or all
slots), decrements a nonzero reload, calls aim-start 52fe30 and aim-readiness
52fff0, and invokes 530140 when ready. **530140 issues FireWeapon; it does not
itself create the projectile.** The loop separately checks flag 16 (script SET
23), then calls 530220 to create it. The post-creation flag retirement established
above therefore belongs to this common pipeline rather than only a presumed
projectile subclass.

New `tools/re/probe_weapon_update.py` runs 52ae90 with controlled aim/fire/create
sinks and real slot selection/countdown/branching. All 4,096 cases match expected
selected/all-slot updates, absent weapons, reload boundaries, aim admission,
FireWeapon delivery and independent SET-23 creation. An explicit three-update
sequence verifies FireWeapon with no projectile, a waiting update, then projectile
creation after delayed SET 23 with no second FireWeapon callback and a false aim
readiness result. This is permitted after aim-start admits the update.

This refines the authoritative implementation requirement substantially: adding
only an AimWeapon-ready check before current World's `fire()` would still fire
too early. The simulation must own AimWeapon and FireWeapon script callbacks,
SET 21/22/23 state, and delayed creation; the display must animate the callback
rather than infer it solely from `firedWeapons` (currently an actual-projectile
notification). Native reload/randomization also belongs to the callback phase.
The probe does not yet verify complete COB timing or replace the live combat
pipeline. No gameplay behavior changed in this investigation.

### Shared weapon update core

Moved the already-verified RetailAimState from the client-only header into
`src/sim/retailweapon.h`; existing client code includes that shared definition.
Added its post-creation flag retirement and the common per-slot update sequence:
reload countdown, aim admission, ready-triggered firing callback, then an
independent SET-23 creation check and combat activity deadline. The host owns
slot selection, script callbacks, successful-allocation retirement, mana and
weapon fallback. This helper does not assume callback delivery creates a shot.

The native update probe now compares 4,096 complete callback traces and final
reload/aim/deadline states against the C++ helper. Trace encoding uses 64 bits
so all twelve possible callbacks remain represented; an initial stale optimized
build still using the earlier 32-bit test adapter was rebuilt before validation.
A regular CTest regression verifies a callback beginning reload, one waiting
update, delayed SET 23 creating exactly one projectile, and no duplicate next tick.

The shared helper is not yet used by World's combat loop. Required next integration
includes script-owned weapon state and SET handling, native query/aim geometry,
callback delivery, delayed creation and separate renderer animation notifications.
The current direct fire path remains an open completion gate.

Shared-core validation: all targets rebuilt in Release, Debug and optimized
Debug; all 129 CTest executions passed. Each build matches the 4,096 native
weapon-update cases. The existing 16,384 aim-state comparisons and 4,096 native
creation cases pass. Cross-compiler golden remains `8adc4762a852fadd` (ARM target
headers unavailable). These establish the helper, not live combat integration.

### Authoritative weapon handshake storage and COB host

Unit now owns three native aim records (heading, pitch and flags), initialized
with the zero-based slot identifiers. The simulation ScriptHost implements SET
21/22/23 for valid weapon slots using the shared verified masks and posts unit
mission event 4. The record belongs to the simulated unit, not to the display
animation instance. Both full lockstep state and per-unit desync diagnostics
include all three records' angles and flags.

The simulation-script regression executes a Create script that sets each opcode
on a different slot, checks the actual live Unit records and owner wake, and
individually changes every slot's heading, pitch and acknowledgement to verify
that each affects the state hash. This provides the state and host integration
needed by the common weapon controller; World's combat loop still needs to drive
AimWeapon/FireWeapon and delayed SET-23 creation, with native geometry and timing.
No claim of completed firing integration is made by storing these acknowledgements.

Weapon-host validation: all targets rebuilt in Release, Debug and optimized
Debug; all 129 CTest executions pass, including real simulation-host SET and hash
coverage. Native aim-state and common weapon-update comparisons pass. Detmath
cross-compiler golden remains `8adc4762a852fadd` (ARM headers unavailable).
Both local 600-tick network runs match **89687750e7d24fcb**. This match hash changed
intentionally because full Unit aim records now participate in stateHash; it is
not expected to equal the earlier hash that omitted those records. The live
combat integration and the broader original transport/animation gates remain open.

### Simulation-owned weapon point queries

Type loading now retains the script-piece hierarchy for every scripted model,
not just production buildings with QueryBuildInfo, and caches each piece's native
mirrored, zero-inclusive bounds center for SweetSpot. The existing production
hierarchy storage is shared rather than introducing a second copy of the tree.

`World::queryUnitScriptPoint` executes QueryWeapon or SweetSpot in the unit's
RetailScriptState. Weapon origins use live piece moves/turns, body pitch/roll and
heading through the already oracle-verified integer transform. SweetSpot uses
model-space bounds without piece offsets or body rotation, as the native query
does. Both add the simulated world position and the appropriate ground/flight
height; invalid pieces fall back to that world position.

World regressions exercise slot-to-piece output from the actual COB query,
known piece coordinates, SweetSpot's independence from body attitude, flight
altitude and invalid-piece fallback. This makes point queries available to the
future authoritative aim controller; it does not itself connect the combat loop
to AimWeapon or delayed FireWeapon/SET-23 projectile creation.

Point-query validation: all targets rebuilt in Release, Debug and optimized
Debug; all 129 CTest executions passed. The existing native capture transform
check matches all 47 animated origins and 47 SweetSpot bounds for unit 308
(Thirsha); the new World regressions establish query-host integration separately.
Detmath cross-compiler golden remains `8adc4762a852fadd` (ARM headers unavailable).
Both local 600-tick network runs match `89687750e7d24fcb`. Aim geometry/controller
integration and the overall transport/animation completion gates remain open.

### Deterministic shared ballistic geometry

Moved aim speed, target lead and ballistic elevation helpers into
`src/sim/retailaim.h`; the client header now includes that shared implementation.
Ballistic elevation uses a fixed-operation double-precision `detmath::atan`
instead of host libm atan. It reduces the argument to magnitude at most tan(pi/8)
and evaluates a 32-term series; double rounding dominates its sub-3e-27 series
truncation error. Existing float atan2 behavior is unchanged.

Cross-compiler golden coverage now includes exact double result bits for 40,001
small/medium arguments, both signs across the finite binary64 exponent range,
and 4,096 ballistic elevations. The atan accuracy check permits at most 5e-16
absolute difference from the host reference. The libm-call guard explicitly
includes the new shared geometry header. The standalone visual test now links
tak-formats because the deterministic function is implemented there.

This removes the host-transcendental obstacle to using the geometry in combat.
Live combat still needs to call it with simulation-owned weapon/target points,
drive script callbacks and defer projectile creation until SET 23.

Shared-geometry validation: all targets rebuilt in Release, Debug and optimized
Debug; all 129 CTest executions pass. Each build matches all 8,192 native
ballistic elevations; the 8,192 native moving-target lead cases also pass. The
expanded cross-compiler math corpus agrees at **dcef618cd2e4d558** (ARM headers
unavailable); the changed golden reflects added double-atan/ballistic coverage.
Both local 600-tick network runs retain `89687750e7d24fcb`. Overall completion
remains open because live combat still uses the earlier firing pipeline.

### World aim solution assembly

`World::queryWeaponAim` now combines simulation-owned weapon and SweetSpot
queries with the shared deterministic geometry. It returns the requested heading,
pitch and led target point (also needed by body alignment). Ground target velocity
uses the retail ground step; airborne velocity includes vertical motion. No-lead
weapons and structure targets suppress lead. Ballistic weapons select the preferred
arc and map the native unreachable sentinel to the caller's zero-pitch fallback.
Invalid unit/target/slot queries return no solution.

World regressions check cardinal heading, no-lead, the exact fixed-point lead
factor for a moving flyer, structure suppression, both ballistic arcs, unreachable
pitch and invalid references. These verify assembly from live simulation state;
component math has separate native oracles. The method is available to the
forthcoming firing controller but is not yet called by World's combat loop.
Full callback timing, deferred projectile creation, renderer notification changes
and complete transport recovery remain open completion requirements.

World aim validation: all targets rebuilt in Release, Debug and optimized Debug;
all 129 CTest executions pass. Native checks match 8,192 direct/guided angle pairs,
1,024 SweetSpot bounds, 8,192 ballistic elevations and 8,192 moving-target lead
points. Cross-compiler math golden remains `dcef618cd2e4d558` (ARM headers
unavailable); both local 600-tick network runs match `89687750e7d24fcb`.
These verify the available aim solution; live combat adoption remains incomplete.

### FireWeapon callback reload and owner events

The native 530140 callback phase starts reload before invoking the script. It
truncates nominal reload times 0.2 to a spread, consumes one CRT random draw,
applies the scaled signed spread and stores the result as an unsigned word.
Thus even nominal reloads 0..4 consume a draw despite having zero spread, and
large reloads wrap at the word boundary. Added shared `retailWeaponReload` and
`retailWeaponFireEvent` helpers for this phase.

`tools/re/check_weapon_fire.py` executes 530140 with controlled CRT output and
script/display sinks, checking native reload and owner event values, the unchanged
aim flags, and the sequence random -> FireWeapon(slot) -> display notification.
The event depends on native weapon/type/selection flags and is posted before the
script callback. Cases cover random field combinations and reload/roll boundaries.

This is a separate phase from projectile creation and mana spending. World's
existing `fire()` still starts a fixed reload when creating the projectile;
replacing it must use the callback calculation and move display notification to
that earlier phase. The existing simulation CRT stream can supply draws, but
complete native CRT consumption order (which also includes rendering) remains
outside this isolated routine comparison.

Callback-phase validation: all targets rebuilt in Release, Debug and optimized
Debug; all three binaries match 4,132 native reload/event/callback cases and all
129 CTest executions pass. Cross-compiler math golden remains
`dcef618cd2e4d558` (ARM headers unavailable). Both local 600-tick network runs
match `89687750e7d24fcb`. Callback reload/event helpers are verified but have not
yet replaced World's live projectile-time reload path.

### Live script-timed weapon release

World now adopts the verified aim handshake and callback reload calculation for
weapons whose unit scripts provide both AimWeapon and FireWeapon. AimWeapon runs
with heading, pitch and slot; readiness requires SET 22, reload/mana availability
and the mover-facing gate. FireWeapon starts the jittered reload and a distinct
`fireAnimations` event. SET 23 can arrive later; that update creates the projectile
and spends mana. `firedWeapons` remains the actual projectile event. Renderer
FireWeapon delivery follows the earlier callback event, while projectile effects
follow the later release. A synthetic delayed script verifies both waits, one CRT
draw, no duplicate callback/shot, and mana spending at release. The multiweapon
regression now checks animation slots against reload starts and independently
observes actual projectile slots.

Live integration exposed two host mismatches. Kraken's body-facing gate compared
against an offset SweetSpot even though the mover was facing the entity center;
it now uses the attack controller's requested facing. Landed Fallen Angels and
Thirsha also needed BeginFlight when entering an attack: their attack scripts
skip SET 23 when their airborne static remains false. Attack-target transitions
now deliver that notification, including an in-range target requiring no travel.
All existing flyer and naval acquisition/Move Fight/direct-order cases pass in
the optimized build after these fixes. No path-search algorithms were changed.

This is not yet complete native weapon-controller parity. The legacy path remains
for unit definitions with no authoritative weapon script; special weapon event
flags, lower-slot mana fallback, and broader target-retirement/visibility
interactions during delayed release still need investigation. Selected-slot
reload countdown is now matched by the native 52ae90 update probe and the live
weapon-switching regression below.
The live changes also consume additional simulation CRT draws, intentionally
changing match hashes.

Live integration validation: all targets rebuilt in Release, Debug and optimized
Debug; all 129 CTest executions pass. Native comparisons pass 16,384 aim/SET
cases, 4,132 callback reload/event cases, 4,096 common weapon-update traces plus
the delayed acknowledgement sequence, and 4,096 projectile creation cases.
Cross-compiler math golden remains `dcef618cd2e4d558` (ARM target headers
unavailable). Two local 600-tick Crusades multiplayer runs match
`63a377b7995dbe2b`. This intentionally differs from the pre-integration match hash.

### Authoritative display weapon callbacks

The display no longer solves a second aim handshake from interpolated unit/model
poses. World records ordered AimWeapon, FireWeapon and TargetCleared events for
each update, and the render snapshot delivers each event once, in that order.
Angles use native display-packet precision (high byte retained, low byte cleared),
while simulation aiming retains the full word. A target switch preserves clear
before re-aim; projectile retirement may request another aim during reload, just
as the native weapon state does. The delayed-script regression now checks that
sequence, retarget ordering, slot identity, angle precision and event expiry.

The native callback oracle now executes both 4ea4f0 encoding and 4ea5c0 decoding:
3,072 random full-word angle pairs/slots match the shared event representation.
All extracted scripts with AimWeapon also provide FireWeapon. Every extracted
walk/walk_legs/tread script has MoveWatcher, MeleeControl or DemonControl; their
Create-owned controllers already read the live movement queries. Removed the
obsolete manual gait fallback and its VM resets, and the display-only aim state
and geometry query helper. Death/corpse VM handling is separate and unchanged.

Display-event validation: all targets rebuilt in Release, Debug and optimized
Debug; all 129 CTest executions pass. Native callback checks pass the 768 decoder
cases, three fire slots, six clear transitions and 3,072 sender/decoder round
trips. Cross-compiler math remains `dcef618cd2e4d558` (ARM target headers unavailable).
Both local 600-tick network checks retain `63a377b7995dbe2b`, confirming this
transient display-event change did not alter the authoritative match state.

### Simulation movement queries and fire-breath follow-up

World's ScriptHost now supplies GET 28/34 terrain flags and GET 29 horizontal
speed/GET 30 airborne vertical speed using the native-verified helpers already
used by the renderer. Previously these returned zero in authoritative scripts,
even while their display counterparts walked or flew. The regression executes
real query bytecode against ordinary, road, water, blocked, airborne and carried
unit states, including signed descent and terrain-adjusted maximum speed. GET 33
was still open at that stage; the completed GET 33 host integration is recorded
in the later update below. The complete movement-callback timeline remains open.

The user explicitly confirmed projectile effects include fire, particularly the
Drake. Its shipped weapon is Line of Sight / fire, emittime 30, velocity 500.
The current renderer still creates a one-shot line of generic flame sprites;
this is not the native flame emitter. Native RTTI identifies LineOfSightFire
vtable 5f3780 and FlameEmitter 5f37b4. Initializer 52d290 installs the emitter;
52d360 updates the weapon, including first-tick collision scanning, a refreshed
QueryWeapon muzzle origin, particle emission and post-emission draining.

Added `tools/re/probe_flame_emission.py`: 4,096 actual 52d360 updates with collision
already resolved and external services controlled establish that the active
interval emits one particle per update, excludes the initialization tick and end
tick, follows the current muzzle, and consumes three CRT draws for signed per-axis
velocity spread. The base particle velocity is (stored endpoint-current muzzle)
divided by stored flight duration. Per component n, the adjustment is
trunc(trunc(2*n/20)*roll/32768)-trunc(n/20). Dead owners and expired emit intervals
do not emit. This establishes an emission contract, not damage/collision,
particle lifetime/rendering, or an integrated replacement; those remain next.

Movement-host validation: all targets rebuilt in all three configurations; all
129 CTest executions pass. Native movement query probes pass 16,384 horizontal/
turn outputs and 4,096 airborne vertical outputs. Math golden remains
`dcef618cd2e4d558` (ARM headers unavailable). Both local 600-tick network runs
match `a36943a2855745d9`; the changed hash is expected because authoritative
scripts now observe actual motion and may follow different branches/random draws.

### Flame particle lifetime and frame selection

Added `src/client/retailflame.h` with the observed per-axis emission velocity and
FlameParticle state/update/frame rules. Native constructor 4f1790 copies position,
velocity, remaining/total lifetime and effect kind. Update 4f16a0 decrements
remaining first, expires at <=0 without moving, and otherwise adds all three
velocity components with 32-bit wrapping. Renderer 4f16e0 selects
(age * sequence-frame-count) / total-lifetime, using integer arithmetic rather
than a global animation frame rate. It projects signed whole-word x and z-y/2.
Kind 1 chooses BlueFire, kind 2 DieselFlame, and other kinds flame; resource setup
at 4bdb2d..4bdb87 establishes those names.

`check_flame_particle.py` executes constructor, updater and render-frame selection
against 4,096 randomized particles, including wrapping positions/velocities,
last-tick expiry, varying lifetime/frame count and all sequence branches. Viewport
admission and the final draw are controlled sinks. `probe_flame_emission.py` now
compares the shared C++ velocity helper with all 1,318 emitting cases in its
4,096-update native corpus. These helpers are not yet connected to the renderer;
the required next integration must also establish the weapon's first-tick ray
scan/flight-duration calculation and collision/damage timing. Replacing only the
one-shot sprite line while guessing those inputs would not establish parity.

Particle-helper validation: all targets rebuilt in Release, Debug and optimized
Debug. The focused retail_visual regression and both native flame probes pass
against all three binaries. No live simulation or render behavior changed in
this helper-only step; the preceding complete 129-test/network result remains
the latest full integration baseline.

### Flame pre-scan and delayed impact timing

Added shared `retailFlameScan` and `tools/re/check_flame_scan.py`. The native
initialization branch of 52d360 visits a range-limited series of full substeps,
retains the collision-clipped endpoint, restores the advancing front to its
muzzle origin, and clears the impact flag. Its outer budget is the unsigned
wrapped `(range << 16) / (substepSpeed * substepCount)`. Collision in outer
iteration n retains lifetime n+1; an unobstructed ray retains budget+1, including
lifetime 1 for zero budget. The probe compares every visited position through a
rolling checksum, final endpoint, lifetime, clipping mutation, restored origin
and cleared flag across 4,096 randomized scans, including wrapped coordinates.

`probe_flame_impact.py` runs 64 complete native timelines with a controlled
obstacle. The initialization scan does not dispatch damage. Subsequent updates
advance the actual front by up to substepCount steps until collision, then call
529c10 exactly once and set the hit flag. Emission continues until the end of
emittime even after that impact. Therefore World's current immediate applyHit for
fire Line of Sight is a confirmed timing mismatch, not merely a visual issue.
The probe establishes impact dispatch timing; the collision geometry and damage
application themselves remain controlled sinks.

Initializer 52c540 sets normal start time to game tick+1; Fire's 52d290 then
replaces expiry with start+emittime and attaches FlameEmitter. Thus an ordinary
new fire shot is initialized on the next tick, and emission starts one update
after that. Its direction is computed from native body heading + stored weapon
aim heading, stored aim pitch and per-substep speed (sine/cosine helpers 5360bf/
5360f3). Source is the current QueryWeapon piece. These initialization inputs and
the existing projectile collision machinery must be connected before replacing
both the instant-hit fire branch and the one-shot client sprite approximation.

Scan validation: all targets rebuilt in all three configurations; the focused
retail_visual regression and all 4,096 native scan comparisons pass for each.
The 64 native impact timelines also pass. This remains preparatory code and
verification; no live flame behavior changed in this step.

### Projectile/model point collision prerequisite

The existing World projectile loop is a 2D swept radius against its selected
target only. That cannot reproduce the flame's native obstruction scan. Native
52a4d0 classifies the occupied map cell, including other bodies and terrain;
its unit geometry predicate 51f340 first admits Y in [unit base,base+modelTop],
then reverses the first four selection-primitive vertices, rotates their X/Z by
native body heading, adds unit position, truncates each world coordinate to a
signed whole word, and applies strict winding-dependent polygon edges (549090).
Points exactly on a selection-quad edge are excluded; base/top Y are inclusive.

Added `retailProjectileInUnit` in `src/sim/retailprojectile.h` and a native oracle
`check_projectile_unit_collision.py`. All 8,192 randomized native height/rotated-
quad cases match, covering boundaries, winding reversal and signed coordinate
wrapping. Type loading now retains the model's mirrored selection quad for all
unit types, independently of the narrower ground-support eligibility rules.
`World::projectilePointInUnit` supplies current body/flight height, heading and
model data to the verified predicate. World tests cover interior, edge, above-
body and flying-altitude cases. Existing pathfinding support data is unchanged.

This query is ready for the flame collision host but is not yet used by the live
projectile loop. Full 52a4d0 admission remains to connect: primary/secondary cell
occupants, owner/player exclusions, feature height, terrain/water and weapon flags,
and its optional designated-target proximity branch. The initial ray and moving
front must share that host before the live fire branch can be replaced faithfully.

Point-query integration validation: all targets rebuilt in Release, Debug and
optimized Debug; all 129 CTest executions pass. The 8,192-case native predicate
comparison passes, math golden remains `dcef618cd2e4d558` (ARM headers unavailable),
and both local 600-tick network runs retain `a36943a2855745d9`. The new model data
and query do not yet change live projectile collision or damage.

### Projectile environmental collision prerequisite

Added the environmental predicate from 52a4d0, with 8,192 native comparisons
covering direct and anchored multi-cell features, absent/invalid feature types,
terrain minimum, water boundary, flag bypasses, and signed bounce velocity.
Feature contact is inclusive at cell minimum plus feature height, and precedes
terrain bounce. Ground contact uses the signed whole-word projectile Y and the
cell minimum (not interpolated terrain). Ground bounce negates velocity divided
by four, truncating toward zero, and immediately returns without a water test.
Water contact is strictly below sea level. Native weapon flags 0x800, 0x1000 and
0x2000 load from unitsonly, groundbounce and waterweapon respectively. The extra
water bypass at game+175dc -> +d3d is not yet identified; its policy remains an
explicit input rather than a guessed game setting.

The default retail_visual regression checks feature-before-bounce precedence,
units-only bypass, exact water surface versus fractional-below-surface contact,
and that water bypass does not bypass solid ground. Native feature loader
4939ef reads TDF height through 543190 and 493a0e retains its low byte at +138.
Current map/feature simulation metadata does not yet retain that field. This
predicate is preparatory and does not change live flame collision or damage.

Environmental predicate validation: all targets rebuilt in Release, Debug and
optimized Debug. The focused retail_visual regression and all 8,192 native
comparison cases pass in each configuration. `git diff --check` passes.

### Map-backed projectile environmental query

The remaining water bypass is now identified: native 4c832b reads the OTA
`nosealeveltrigger` key (default zero), and 4c8345 stores it at +d3d. Match setup
retains this setting; terrain reload resets it. `World::projectileEnvironment`
uses it alongside native signed fixed-point cell lookup (50e660), the cell
minimum and the resolved feature height. Its result preserves the native
0=clear/bounce, 1=outside-map, 2=impact distinction.

Feature TDF height is now retained as a byte in both initial map placement types
and lifecycle feature types. Replacement/corpse installation propagates that
height, and placement-type reuse compares it. This does not change placement or
pathfinding rules. World regressions cover feature anchors and tails, elevated
terrain corners, exact feature tops, feature-before-bounce precedence, units-only,
water fractions, map water bypass, and negative/far map boundaries.

The new query is not yet called from the live fire loop. Primary and secondary
unit occupancy admission and designated-target proximity must still be combined
with it before replacing instant fire damage and the client sprite line.

Next collision-host evidence: LineOfSightFire vtable 5f3780 slot +14 resolves to
42a6b0 (false), so primary occupants must pass 51f340's model-quad predicate;
fire cannot use the primary-cell geometry bypass. Secondary occupants instead
use the inclusive [unit Y + modelBottom, unit Y + modelTop] bounds. The current
`occ_` grid excludes flyers and structures and is not a drop-in replacement for
these two native cell ownership fields.

Map-query validation: rebuilt all targets in Release, Debug and optimized Debug;
all 129 CTest executions pass. The 8,192 native environmental comparisons pass
against each configuration. Cross-compiler math remains dcef618cd2e4d558
(ARM checks skipped for unavailable target headers). Both 600-tick local network
runs retain a36943a2855745d9. `git diff --check` passes. This remains collision-host
preparation rather than a live flame or overall animation parity completion.

### Surface and airborne projectile collision host

Added `World::projectileCollision`, composing the existing surface body/yard
lookup, native selection-quad predicate, secondary airborne height admission,
optional interception proximity, and environmental query in native order.
Same-player bodies are excluded (the native check is owner equality, not team
alliance). Out-of-map returns 1 before proximity. Interception returns impact
without a hit unit; direct body contact reports its unit ID. World regressions
cover same-owner exclusion, primary geometry, secondary height-only admission,
exact aircraft top and proximity precedence. This query remains to be connected
to the live projectile update; it does not yet replace immediate fire damage.

The earlier proposed model-bottom prerequisite was unnecessary: native type
initialization at 4c1419 stores zero into +13e. The secondary body interval is
therefore [unit Y, unit Y + modelTop]. LineOfSightFire still uses the strict
primary selection quad (its virtual +14 returns false).

Native secondary occupancy is a dedicated airborne footprint grid, rebuilt in
player/entity order (51da76/51db36, clear 507050 then insert 506c40). The insertion
builds reciprocal overlap lists, selects a cell owner with bounded random 535cc0,
and installs the ffff no-owner sentinel when more than seven cell candidates are
encountered. Lists themselves can overflow too, affecting later cells. It is not
a nearest-aircraft query or a last-writer grid. Boundary admission requires the
whole footprint to end strictly before the map width/height, and only mode 2
is inserted. The new `retailAirCollisionGrid` matches 1,024 native scenarios,
including dense eight-plus-aircraft piles, sparse partial overlaps, grounded
bodies, map edges and controlled random draw counts. `World::projectileAirGrid`
supplies flight modes/footprints in player/entity order and uses the existing
verified game RNG. It is not called automatically yet, so this step consumes no
new gameplay randomness.

The optional proximity predicate matches 8,192 native cases. Native 52a519
subtracts fixed-point coordinates with wrapping, squares each signed delta,
retains each product's high word separately, wraps their sum, then compares
signed against the wrapped square of the unsigned-word radius. Exact radius is
excluded. This optional pointer describes another projectile (its position is
at +4), not the ordinary targeted unit (+68); the collision host takes an
explicit optional position rather than incorrectly inferring it from targetId.

This advances collision-host integration but does not prove full native primary
ownership timing, complete combat scheduling or rendered flame parity. Those
remain completion gates alongside the original transport and animation scope.

Flame integration must select the native fire subclasses explicitly: shipped
Line-of-Sight subtypes fire, bluefire and dieselflame cover aradrag, credrag,
crefire, cresage, tardrag, tarknigh, tarmage, tarspout, verdrag, zondrag and
zondrake. `Weapon::beam` currently means every Line-of-Sight weapon, including
ordinary arrows/cannonballs, lightning and status effects, so it cannot be used
as the flame-class discriminator. The existing instant-hit comments/branch are
still wrong for moving fire and ordinary Line-of-Sight projectiles; replacing
that branch remains required work, not a completed collision-query result.

Collision-host validation: all targets rebuilt in all three configurations and
all 129 CTest executions pass. The 1,024 airborne-grid and 8,192 interception
oracles pass against Release, Debug and optimized Debug. Existing 8,192-case
unit-geometry and environmental oracles also pass against optimized Debug.
Cross-compiler math remains dcef618cd2e4d558 (ARM headers unavailable); both local
600-tick network runs retain a36943a2855745d9. `git diff --check` passes.

### Live flame integration

The fire, bluefire and dieselflame subclasses now create an authoritative
three-dimensional flame shot instead of applying immediate damage. The launch
uses QueryWeapon and current weapon/body aiming angles with native quantized
substep velocity. At start tick it performs the verified full ray scan without
impact or emission. Subsequent active ticks advance the front in collision
substeps, dispatch one impact, and continue emitting from the current muzzle
until the emission deadline. Existing particles update before the new particle
is appended, retaining the native age-zero first frame. Owner death drains the
emitter; embarking after release does not cancel the existing shot. Particles
also drain after the emission deadline. World reset clears the new state, and
its positions, clocks, velocities, collision state and particles enter the
lockstep hash. Particle spread consumes the authoritative CRT stream with the
three native draw origins.

The airborne collision grid is now built before projectile updates, using the
native bounded game-RNG choices for overlapping aircraft. It is skipped when
there are no airborne bodies. Surface path search/steering math is unchanged.
Synthetic featureless terrain now provides its minimum-height environmental
query too; absence of a feature plane no longer means outside-map.

The render snapshot carries the live particles. Rendering uses signed whole-word
positions, native half-height projection and lifetime-scaled sequence frames,
with distinct flame, BlueFire and DieselFlame assets. Removed the old static line
of delayed flame sprites and the generic muzzle burst for these subclasses.
A debug-only TAK_SHOT_FLAME capture gate waits for actual particle draw calls,
avoiding an arbitrary wall-clock screenshot between breath pulses.

Focused World tests check no damage at launch/pre-scan, delayed front arrival,
exactly one impact, tree obstruction, current-muzzle emission, persistence after
embarking, and draining after owner death. Live attack-order tests exercise all
11 shipped flame weapons in both balance modes; all 22 emit their correct
particle kind and damage a stationary enemy. The same 22 first-impact scenario
hashes agree across Release, Debug and optimized Debug. Native scan (4,096),
particle (4,096), emission (4,096 with 1,318 emitted-velocity comparisons) and
controlled impact-timeline (64) probes still pass.

Inspected client captures /tmp/flame-live-zondrake.png,
/tmp/flame-live-credrag.png and /tmp/flame-live-crefire.png show ordinary fire,
blue fire and diesel flame respectively, attached to their emitting weapons.
These are rendered smoke checks, not retail pixel comparisons. Full native
world-phase/primary-occupancy timing, the generic damage/effect dispatcher,
sound/blending parity and the remaining Line-of-Sight subclasses are still
outside this step's proof. The original transport/all-animation goal remains
incomplete; this section supersedes the earlier statement that live fire still
uses immediate damage and the temporary sprite line.

Final live-flame validation: rebuilt all targets in Release, Debug and optimized
Debug after the embarkation regression. All 129 CTest executions pass, and all
22 live flame scenario hashes agree across these final builds. Math golden stays
dcef618cd2e4d558 (ARM target headers unavailable). Both final 600-tick local
client/server runs retain a36943a2855745d9. `git diff --check` passes. These
network runs are general regression coverage; the 22 live flame scenarios provide
the flame-specific cross-build state comparison.

### Ordinary straight-projectile update evidence

`tools/re/probe_straight_projectile.py` executes native 52c6d0 with controlled
collision, impact, trail and destruction sinks. Its 432 timelines establish:
no movement before or on the activation tick; body heading added on activation;
wrapped three-axis motion and 16-bit model rotation on every collision substep;
collision result 1 does not stop flight; result 2 dispatches one impact and
returns immediately; expiry excludes movement on the deadline tick. A shot
without a trail is removed on the update after impact. A trail-bearing shot
continues draining until its emitter reports empty. Dead or pending-death
owners cancel before/on activation, but do not cancel a released ordinary shot.
The probe checks positions, angles and impact state after every update as well
as the ordered external calls. Collision geometry and damage magnitudes remain
outside this probe; it does not yet compare a live World ordinary-shot port.

RTTI/vtable inspection also confirms that the ordinary class must not be applied
to every non-flame Line of Sight weapon. The observed dispatch is:

| Class | Vtable | Update | Draw | Initialize |
| --- | --- | --- | --- | --- |
| LineOfSightWeapon | 5f39f4 | 52c6d0 | 52c8c0 | 52c540 |
| LineOfSightLightning | 5f374c | 52ccc0 | 52ccf0 | 52cbf0 |
| LineOfSightFire | 5f3780 | 52d360 | 52d710 | 52d290 |
| LineOfSightTurnToStone | 5f3a5c | 52db40 | 52c8c0 | 52c540 |
| LineOfSightMindControl | 5f3a90 | 52d8d0 | 52c8c0 | 52c540 |
| LineOfSightTurnToFrozen | 5f3a28 | 52dda0 | 52c8c0 | 52c540 |

Lightning and the three status classes have distinct updates. The status classes
share initialization/drawing with ordinary shots, which is not proof they share
flight or impact semantics. Next integration must retain these distinctions.
The existing ordinary-LOS immediate-damage branch is still unfinished work.

`probe_straight_projectile_launch.py` now exercises native 52c540 over 4,096
launches, including vertical/backward pitches and fractional muzzle coordinates.
It establishes QueryWeapon slot masking, muzzle position, native trig argument
wiring, relative model heading/negated pitch, veteran model selection, creation
tick, and the range-derived expiry. The horizontal component falls back to full
speed only when exactly zero. Range times speed wraps before signed division;
the quotient is shifted by 16, the per-tick speed added with wrapping, and the
result divided unsigned by per-tick speed. Thus this is not a floating-point
distance/speed lifetime. Launch normally starts at creation tick plus one;
weapon flag 40000 adds buildup ticks only if the owner has the corresponding
animation. The probe checks that conditional animation-start dispatch too.
Native trig is queried separately to verify wiring; independent trig accuracy
remains covered by the earlier motion probes.

`probe_straight_projectile_render.py` executes 52c8c0 over 4,096 draws. Drawing
is gated by activation, viewport admission, map bounds, and the selected player's
visibility bit (or explored byte in the alternative mode). Visibility addresses
use the projected cell `(x >> 5, (z - (y >> 1)) >> 5)`. Signed whole-word
coordinates, including negative values and fractional fixed-point inputs, are
covered. Dispatch order is shadow sprite, model, foreground sprite. Shadows
use the terrain-height query; foreground sprites use the shot's actual height.
Models receive the shot's actual position and all three stored angles. This
contradicts substituting the legacy renderer's estimated arc/current-unit height.
Frame selection and rasterization are controlled sinks, so the probe does not
prove animation frame clocks, blending, or pixel parity.

Weapon fields +54/+58 in these routines are foreground/shadow animation handles,
not sounds: update calls 537390/5373d0 start/advance them, and draw calls 536400
to obtain their frames. The optional buildup call to 537390 is also animation
startup. The straight-shot timeline probe currently leaves both handles empty;
the render probe supplies frame selection separately. Live integration still
needs those animation clocks and any associated trail emitter, in addition to
the now-observed launch/update/render sequencing. Both new probes pass, and
`git diff --check` passes; no engine source changed in this evidence step.

### Live ordinary straight-shot integration

Ordinary Line of Sight weapons (empty subtype) now launch authoritative 3D
projectiles instead of applying instant damage. Specialized lightning, flame,
stone, freeze and mind-control classes retain their own paths. QueryWeapon and
the accepted script aim supply muzzle/velocity; launch and expiry follow the
observed fixed-point calculation. Activation adds current owner heading without
movement. Subsequent ticks advance wrapped XYZ and model angles per substep,
using the same unit/aircraft/environment collision host as flames. One collision
dispatches impact; the projectile is retained until the following update. Owner
death after activation does not cancel flight. All new flight state participates
in the lockstep hash and is cleared with the existing projectile collection.

Loader inspection at 53103c..53107b confirms spinpitch/spinheading/spinroll are
read as integers directly into the three 16-bit increments. These are now applied
per substep; the previous renderer's per-second interpretation is not used for
ordinary shots. The snapshot retains actual XYZ and angles. Ordinary shot art
and models now use actual height instead of an estimated arc or the source's
current height. Shadows, models and foreground sprites can coexist. Authored
frame durations are retained by the effect loader and used for these sprites;
native 537390/5373d0/5367e0 reads each duration as a word and advances at least
once per tick when duration is zero. The old impact-triggered fake beam is
excluded for ordinary shots.

Live testing exposed missing terrain Y for structures without a moving support
controller. Their simulation base had remained zero, launching Stronghold and
Trapdoor Spider shots underground on elevated maps. Structures now receive
terrain height in fixed-point coordinates. Native birth 511f50 copies a complete
XYZ placement position; this port's public spawn API only accepts X/Z and must
supply Y. This does not change surface path search/steering or X/Z positions.
Full native structure placement height on uneven terrain remains a separate
parity check; flat raised-terrain firing is now exercised by the live tests.

Focused World coverage verifies delayed damage, no activation movement, wrapped
rotation, tree obstruction, flight after shooter death, and single impact with
next-tick removal. Shipped attack-order coverage adds 17 ordinary-shot scenarios
across both balance modes (Crusades changes the Centaur's class), including
ground units, structures and the Harpoon Ship on water. A paralyzer hit is
checked as incapacitation rather than health loss. Together with 22 flame
scenarios, all 39 first-hit state hashes agree across Release, Debug and optimized
Debug. All targets rebuilt and all 129 CTest executions pass. Cross-compiler
math retains dcef618cd2e4d558, with unavailable ARM headers skipped. Both local
600-tick network runs agree on 9955322ef77531a1 after the deliberate simulation
changes. `git diff --check` passes.

Viewed /tmp/straight-tarhel.png and /tmp/straight-verarch.png show a traveling
authored fireball and a modeled arrow. These are smoke captures, not retail
pixel comparisons. Open ordinary-shot work includes secondary/trail emitters,
veteran art switching, conditional buildup for modded definitions, full native
visibility/projection integration, sprite loop/blend semantics and native model
draw parity. Native artless classes with nimbus/light effects need their actual
emitter rather than invented fallback sprites. The generic damage dispatcher
and other projectile classes remain outside this step's proof. This section
supersedes the earlier statement that live ordinary LOS still deals instant
damage; the original transport/all-animation goal remains incomplete.

### Lightning and nimbus follow-up evidence

The `--lightning` mode of `probe_straight_projectile.py` executes 52ccc0 rather
than 52c6d0. All 432 timelines pass: this subclass checks owner alive/pending
death on every update, then delegates to the ordinary moving-shot updater. It
therefore has delayed flight/collision, not the current live instant-damage
shortcut. In contrast to ordinary shots, owner death after activation cancels
lightning too. Its initializer 52cbf0 delegates to ordinary launch and optionally
creates a named effect object at shot+a4 by matching the class's effect name.
The observed allocation of that object is lightning-specific; it must not be
assumed to exist for every ordinary shot. The shared smoke hook 530730 is a
different mechanism, gated by weapon flag 10000 and a cadence timer.

`probe_lightning_render.py` executes native 52ccf0 in the no-named-emitter path.
All 4,096 cases pass. From activation onward it refreshes the caster's current
QueryWeapon muzzle on every draw, before viewport/fog tests. Either endpoint
may satisfy viewport admission, and either endpoint independently may satisfy
visibility. The fallback bolt drawer 52b400 receives the current projectile
position and the freshly queried muzzle, plus the class's bolt parameters.
This proves that the visual grows with the moving collision front and follows
the caster; it is not an impact-triggered full-length bolt with fixed endpoints.
The probe substitutes the final bolt drawer, so segment jitter, thickness,
colors, named effect tessellation and rasterization still need verification.

Nimbus clarification: weapon parsing at 53121f..53124d maps `nimbus & 1` to
flag 40000. The already-probed initializer uses that flag for caster animation
startup and conditional buildup delay. This is not evidence for a larger,
translucent copy of the projectile sprite. The current legacy renderer still
used that unsupported interpretation; it is removed in the later correction below. Sidedata supplies
the faction-specific names nimbus_aramon/taros/veruna/zhon. Thus the prior
"artless classes with nimbus" note should not be read as proof of a missing
projectile particle emitter: caster animation, named lightning emitter and
lightmap are distinct paths. All findings here are evidence for the next live
integration; lightning damage/render behavior has not yet been changed.

### Named hardware-effect intensity plane and lightning source

Added shared integer helpers in `sim/retailhweffect.h` for native 4f46b0's
existing-particle update, intensity-plane diffusion and particle stamping.
Particles use 24.8 X/Y/intensity and wrapped addition. After movement, particles
are removed for nonpositive intensity or strict interior-bound failures. The
plane diffuses in descending row order, in place, with the source rows shifted
when rise is enabled. Border bytes remain untouched. New emission belongs after
diffusion and before stamping; the helpers keep these stages separate. Stamping
overwrites the texel in list order, even when the last particle is dimmer. The
native emitter-alive result follows particle count, not nonzero texture bytes.

`check_effect_field.py` executes 4f46b0 with a native linked list and no emission
sources, substituting only deallocation and graphics-device access. All 4,096
cases match the C++ particle positions/counts and every intensity byte. Coverage
includes fixed-point overflow, off-plane pruning, rise/non-rise diffusion,
boundary preservation, zero surviving particles and byte-truncated intensities.
The native doubly linked list and alive return are also checked independently.

The named "line lightning" source is distinct from the fallback screen-space
bolt drawer. Its virtual generator 4f3f20 writes its line directly into the
intensity texture, then supplies an origin particle. It sorts endpoints by X,
writes endpoint intensities, and calls recursive midpoint routine 4f3df0. Each
midpoint consumes a CRT draw, jitters along the minor axis, writes only inside
strict texture bounds, and reduces spread down to one. The second half follows
the first half; random consumption order is observable. Fade changes the sorted
end's intensity to zero. These operations are now implemented by
`retailEffectLightning` rather than an invented polyline/jitter approximation.

`check_effect_lightning.py` executes the native generator and midpoint routine
with a controlled CRT stream. All 2,048 cases match every texel and the final
random state/draw count, covering horizontal/vertical/diagonal and reversed
endpoints, fractional coordinates, fading and clipped midpoints. Native source
output is checked for original emitter position and zero X/Y particle velocity.
The native virtual slots are readiness 4f4f40, generation 4f3f20 and loading
4f5180; scheduling/definition loading, palette construction, GPU mapping/blend
and integration into live lightning shots remain required work. The shipped
effect definitions are read-only local reference material, not copied assets.

These are verified effect components, not a claim that live lightning is fixed.
The live ordinary/flame integration remains as described above, and the overall
transport/all-animation goal is still incomplete.
All targets rebuilt in all three configurations. Both native comparisons pass
against Release, Debug and optimized Debug (6,144 cases per configuration), as
does the existing retail_visual CTest. `git diff --check` passes. No live-world
behavior changed in this component step, so the previous broad gameplay/network
validation was not repeated.

### Full named-lightning emitter cadence and repeated definitions

`RetailLightningEffect` now composes advancement, diffusion, emission and stamping
in native order. The 4f4f40 source clock decrements with signed wrapping; a
nonpositive result emits and resets to the authored rate. Cloning at 4f3d10
resets the clock to zero, so its first eligible update can emit immediately.
Emission visits sources round-robin, at most once each per update. Reaching the
particle cap preserves the next-source cursor and stops visiting clocks; disabled
emission also leaves those clocks unchanged. Existing particles and the texture
still advance while emission is disabled. New line sources draw their polyline
and append an origin particle with the effect's initial intensity/decay before
the final stamping pass.

`check_lightning_emitter.py` runs real 4f46b0, 4f4f40, 4f3f20, 4f3df0 and list
insertion 4f5300. Only allocation/free, the controlled CRT stream and graphics
device calls are substituted. Its 512 timelines (16,384 updates) match source
clocks, capacity/cursor behavior, particle positions/counts, alive return, texture
hashes, CRT state and draw count after every update. Cases include no sources,
zero capacity, caps smaller than the source count, rate zero/negative values,
intermittent emission and final drain periods. Earlier isolated field/line
oracles establish full texel equality for those stages.

Definition inspection exposed a prerequisite loader bug: TDF stored repeated
section names only in a map, overwriting all but the last emitter or palette
ramp. `tdf::Node::orderedChildren()` now provides every parsed section in source
order, retaining independent nodes for superseded definitions. Existing `child`,
`children` and unique-name `childOrder` behavior remains last-definition-wins,
so current callers keep their established lookup semantics. The regression test
covers interleaved duplicate emitters, case folding, duplicate top-level sections
and copying the parsed tree after destroying the original. Native effect loading
can now consume repeated emitters/ramps without losing authored entries.

These shared components still need definition/palette loading and connection to
live shots/render snapshots. This step does not claim live lightning parity or
completion of the transport/all-animation goal.
Validation: all targets rebuilt in Release, Debug and optimized Debug; all 129
CTest executions pass. The complete 512-timeline native emitter comparison passes
against each build, and all 39 existing live projectile scenario hashes agree
across the three configurations. `git diff --check` passes.

### Named-effect palette conversion and definition loading

`retailEffectColorRamp` implements native 4f40f0's per-channel signed integer
increments, byte accumulation and explicit final endpoint. Equal/reversed ramp
bounds perform no writes; single-color entries are separate. Overlapping ramp
definitions apply in file order. This differs from evaluating a floating-point
interpolation independently for every color entry.

`retailEffectPalette` implements 4f4ed0's ARGB4444 conversion. RGB comes from the
high nibble of each palette channel. Alpha comes from intensity index times 255
divided by the effect's whole initial intensity, with native masking/truncation;
overbright entries are not clamped. The palette's own fourth byte does not supply
opacity. `check_effect_palette.py` runs both native routines without substituted
arithmetic. All 4,096 cases match the full raw palette and all 256 packed entries,
including overlaps, untouched bins, small per-step color differences, no-op
ramps, byte accumulation and signed intensity divisors.

`retailhweffectdata.h` loads line-lightning definitions through the lossless TDF
section API. It retains every emitter and palette entry, converts coordinates
and intensities to 24.8, reads source cadence/fading, preserves anchors and the
translucent flag, and prepares a fresh zeroed effect state. Source clone clocks
start at zero; ticking a clone does not mutate its reusable definition. Invalid
palette bounds, zero opacity divisors, invalid texture/endpoint coordinates or
unsupported source kinds are rejected explicitly rather than silently dropping
data. This loader currently handles line-lightning sources, not burst/spray.

All eight definitions in the shipped effects.tdf load successfully, retaining
respectively 2/3/2/3/2/4/1/3 sources for lightning, fire, bluelight, blueblocker,
yellow lightning, creon lightbeam, creon lightning and creon paralyzer. A focused
regression checks distinct repeated emitters, overlapping ramps, packed color
values and independent clone clocks. All targets rebuilt in all three
configurations; the focused CTest, shipped definition reads and 4,096-case
palette oracle pass against each. `git diff --check` passes.

Inspection of 52cfb1..52d177 shows named lightning maps its texture onto a rotated
four-vertex rectangle with screen-space length and authored texture-height
thickness. It uses native rounded direction/rotation and padded GPU texture UVs.
Clipping, vertex construction, blending and live World/render integration remain
open; the new loader/palette components alone do not establish rendered parity.

### Named-lightning textured quad comparison

`retailLightningQuad` now reproduces native 52ce47..52d171's endpoint sorting,
500-pixel clipping margins, integer clipping ratios, rounded direction and
corner rotation, authored half-height, vertex order and padded-texture UVs.
The native signed squared-distance overflow case also retains the observed
zero low-word length. `check_lightning_quad.py` executes the native draw path
and captures the submitted vertices and device state. All 4,096 cases match
vertex and UV float bits against Release, Debug and optimized Debug. Device
state confirms SRCALPHA / INVSRCALPHA blending rather than additive blending.

The probe explicitly sets x87 control word 0x027f, matching the precision used
by the existing native model-transform comparisons. The emulator default
24-bit precision had introduced a one-pixel corner discrepancy near a rounding
boundary. The straight-shot launch probe now sets the same control word; all
4,096 native launch comparisons still pass.

All targets rebuilt in all three configurations and each focused retail_visual
CTest passes. These remain independently verified rendering components: live
named-lightning World state, snapshots and texture submission are not yet wired
to them. This does not establish complete projectile or animation parity.

### Weapon registry connection for named lightning

The registry now reads `gamedata/effects/*.tdf` once, preserving ordered
definitions and binding LOS/lightning weapons to immutable named effect
definitions. Weapon subtype and effect name are retained separately from the
legacy visual-family heuristic. Registry inspection finds 15 named lightning
weapons in each balance mode; every one resolves, including the distinct Creon
lightbeam, lightning and paralyzer definitions. The regression checks both
balance rosters and verifies that resolved effects retain their emitters.

Named effect TDFs now participate in `gameplayHash` and are excluded from
cosmetic overrides. Their emission schedules will consume the simulation RNG,
so allowing peers to use different definitions would be unsafe when the live
update is connected. A temporary isolated VFS check confirms that changing
start intensity changes the gameplay hash. No navigation or projectile update
behavior changes in this registry step; live emitter updates and rendering
remain open.

Validation: all targets rebuilt in Release, Debug and optimized Debug; all 129
CTest executions pass. All 39 existing live projectile scenario hashes agree
across the three builds. `git diff --check` passes.

### Live named-lightning trajectory, emitter and rendering

LOS/lightning now uses the verified quantized 3D launch and collision-substep
path rather than immediate target damage. Its named emitter is cloned at birth,
advances before movement, stops emitting on collision/range expiry, and drains
existing particles before deletion. Unlike ordinary released shots, lightning
cancels when its caster dies, including the zero-HP pending-death interval.
Emitter clocks, cursor, particles and intensity pixels participate in the world
checksum; recursive line generation consumes the shared CRT stream at the
native origin. Frame snapshots retain the effect state and current muzzle.

The renderer expands the verified ARGB4444 palette, uploads a padded texture,
and uses the native textured-quad geometry with normal alpha blending.
Foreground art precedes the beam; ordinary shot shadows/models do not run for
this class. Textures are reused by dimensions and destroyed with the game view.
The former instantaneous impact-generated lightning beam is disabled for these
shots. This connects the earlier independent components to actual gameplay.

Synthetic cases cover delayed damage, collision, no repeated damage while
draining, pending caster death and ordinary shots surviving fully processed
caster death. The initial synthetic fixture incorrectly launched at height zero
below its terrain; explicit above-ground muzzle/target positions corrected the
fixture. Its later death check exposed and fixed the pending-death cancellation
gap. Live combat checks cover all 13 primary-lightning units in each balance
mode. The two additional lightning weapons in secondary slots still require
selected-slot integration coverage; registry resolution alone is not that proof.

All targets rebuilt in Release, Debug and optimized Debug. All 129 CTest
executions pass; all 65 flame/ordinary/lightning scenario hashes agree across
builds. The 432 native lightning timeline cases still pass. Cross-compiler math
checks retain golden dcef618cd2e4d558, and two 600-tick local network runs agree
on 9955322ef77531a1. These network runs are general regression coverage rather
than a forced lightning encounter.

Open rendering/phase gates include exact projected visibility admission, the
native draw-time QueryWeapon scheduling versus our simulation-time muzzle
snapshot, fallback lightning without a named definition, nimbus/buildup and
final paired retail visual comparison. No pathfinding math changed.

Live render smoke captures were inspected for Creon Prism and the Zhon monarch.
The Prism shows its broad authored beam; the monarch's first emitted tick shows
two thin jittered white lines extending to the advancing head. The screenshot
harness previously kept simulating while waiting for terrain uploads, which could
miss short-lived effects. `TAK_PROJECTILE_CAPTURE=<minimum shot age>` now pauses
on the selected active named effect before capture; it is debug-only. Final
capture-harness changes were rebuilt in all three configurations. These captures
prove the live texture submission path runs, not retail screenshot parity.

### Pending-death checks across fire and ordinary LOS shots

The native 52d360 fire update checks both the owner-alive bit and pending-death
bit before scanning, moving the damage front or emitting. For a dead/dying owner
it updates existing particles only, deleting the shot when the emitter reports
empty. The flame emission oracle now covers all four relevant flag combinations
and both nonempty/empty drain results. All 4,096 cases pass, including absence
of emission/CRT draws during pending death and deletion only after draining.

The live flame update now requires positive owner HP as well as the alive state.
The earlier test only compared particle counts, allowing a fresh particle to
replace an expiring one without failing. It now compares every expected surviving
particle's position, velocity and remaining lifetime after one drain update.
This applies to the shared fire, bluefire and dieselflame classes, including Drake
fire. Ordinary LOS shots also reject pending-death owners before/on activation,
while activated ordinary shots still survive owner death. Both activation
boundaries have explicit regressions and the 432-case native ordinary timeline
probe passes.

Validation: all targets rebuilt in all three configurations and all 129 CTest
executions pass. The 65 live projectile scenario hashes match across builds.
The expanded emission oracle passes against all three builds, and the 64 native
fire impact timelines still pass. GCC/Clang math checks retain golden
dcef618cd2e4d558 (ARM checks remain skipped for missing target headers). Two
600-tick network runs retain matching hash 9955322ef77531a1. `git diff --check`
passes. This closes the identified owner-state gap, not the remaining full
transport/animation parity gates.

### Interrupted pickup resumption and bounded failure recovery

The cleanup probe now executes native queue insertion (4d7750/4d7640) as well
as removal (4d6ad0), rather than replacing insertion with a queue sink. Across
160 stage/mask/mission-flag combinations, ordinary inserted missions clear the
interrupted request's stage and wait mask, preserve its deadline, counters and
other payload, inherit the relevant queue flag, and restore the same request
after the temporary child is removed. The preserving insertion flags retain
stage/wait as well. Existing target-reference and sleeping-passenger cancellation
checks still pass. This establishes the queue contract; navigation and mission
handlers remain outside this particular probe.

The live opportunistic-pickup test now runs the path service and continues past
boarding the nearby passenger. Both air and surface carriers resume their
distant request, reach transfer and board that passenger while retaining cargo.
A separate slow-braking surface fixture reaches transfer but exhausts the ten
native retries before it stops; cancellation cleans up both requests, preserves
existing cargo, and a newly issued pickup succeeds after the passenger stops.
The original fixture's default braking took almost 60 ticks, so treating eventual
boarding as unconditional was an incorrect test expectation. The success fixture
uses explicit 0.125 px/tick² braking; the slow case remains covered as a bounded
failure/recovery case. No production movement/pathfinding behavior was changed.

All targets rebuilt in Release, Debug and optimized Debug; both transport CTests
pass in each configuration. The native pickup probe still passes 4,096 transfer
states, 2,048 fractional boundaries, 1,024 air departure goals and full stationary
air/sea initialization-to-attachment timelines. `git diff --check` passes. Full
paired native distant/coastal/blocked navigation traces remain an open gate;
these queue observations and live world checks are complementary evidence, not
a replacement for that wider comparison.

### Secondary lightning slots and selected reload clocks

Live lightning coverage now selects every named-lightning weapon through
`World::setWeapon`, keeps each unit's complete authored weapon/script definition,
and verifies that the resulting shot retains the selected slot and matching
weapon pointer. This adds the Taros witch's slot 1 Thunderbolt and Creon chief's
slot 2 Long Term Stun in both balance modes: all 15 named lightning weapons now
emit and reach a target through the live combat/script path. This covers fixed
selection for those weapons, not every possible mid-callback switching case.

The native common weapon loop skips unselected and absent slots before reload
countdown. Our live `tickCombat` instead decremented all three timers, allowing
switched-away weapons to reload in the background. It now clocks only the
selected present slot for switchers, or all present slots for independent
multi-weapon units. Live regressions switch 1 -> 2 -> 0 and check preserved and
resumed countdowns in both modes. The native update probe adds a successive
selection sequence, alongside its 4,096 randomized trace/state comparisons,
confirming the same freeze/resume behavior independently of aim admission.
No pathfinding or weapon-range calculation changed.

Validation: all targets rebuilt in all three configurations and all 129 CTest
executions pass. All 69 projectile scenario hashes agree across builds. The
expanded native weapon-update oracle passes against each build. GCC/Clang math
checks retain golden dcef618cd2e4d558 (ARM target headers remain unavailable),
and both 600-tick network runs retain hash 9955322ef77531a1. `git diff --check`
passes. Other callback scheduling, target-loss and visual parity gates remain
open as listed above.

### Missing weapon callbacks do not authorize immediate fire

`tickScriptWeapon` previously abandoned script timing when either AimWeapon or
FireWeapon was absent, falling through to immediate legacy firing. The native
lookup/start path does not synthesize SET 22 or SET 23 on lookup failure. The
new `probe_missing_weapon_callbacks.py` executes that lookup/start path, aim
start and FireWeapon dispatch across 1,536 empty/unrelated-roster, slot and flag
cases. Missing aim leaves the handshake unacknowledged; missing fire still
starts reload but does not acknowledge projectile creation.

Scripted units now retain the handshake even when a callback is missing. Live
regressions cover absent aim, absent fire and both absent, asserting no shot or
mana charge and the correct reload/callback distinction. Units without any
authoritative script still use the existing legacy path; this change does not
claim that broader missing-script behavior has been matched to retail.

A registry inventory finds 141 armed definitions in each balance mode, with
only TARKAM missing these callbacks. Its shipped script has Create, movement,
MeleeControl, SweetSpot and death callbacks, but neither AimWeapon nor FireWeapon.
The native common weapon update calls the same aim/readiness/fire routines
directly; readiness requires the script acknowledgement. Live TARKAM checks
in both balances confirm it no longer invents periodic Bite damage and still
damages nearby enemies through its authored EXPLODEAS on death.

Validation: all targets rebuilt in Release, Debug and optimized Debug; all 129
CTest executions pass. The 69 projectile scenario hashes agree across builds.
The native absent-aim probe also executes the real readiness gate with matching
angles, zero reload and sufficient mana, confirming the missing acknowledgement
alone prevents readiness. GCC/Clang math checks retain dcef618cd2e4d558 (ARM
checks still lack target headers), and two 600-tick network runs agree on
9955322ef77531a1. `git diff --check` passes.

### Target-clear callback selection

Native 51a7f0 always retires a present target reference, including ground-point
targets, but only dispatches TargetCleared for the selected slot unless the unit
uses all-slot mode. An already-empty reference is silent. Clearing alone does
not modify aim acknowledgement flags; any reset comes from the script callback.
The callback probe now covers 96 type/selection/slot/reference combinations and
checks both reference retirement and unchanged aim flags.

Our `clearScriptWeaponTarget` dispatched the callback to every slot of a
switcher. It now dispatches only the selected slot, preserving inactive slots'
script/aim state. Live retargeting regressions compare switchers with independent
three-weapon units and verify that inactive acknowledgements are not reset.
This fixes callback admission; full delayed fire/retarget and slot-switch
timelines still require separate comparison. No navigation math changed.

Validation: all targets rebuilt in Release, Debug and optimized Debug; all 129
CTest executions pass. All 69 projectile scenario hashes agree across builds,
and the expanded native callback/packet probe passes against each. GCC/Clang
math checks retain dcef618cd2e4d558 (ARM target headers remain unavailable). Both
600-tick network runs agree on 9955322ef77531a1. `git diff --check` passes.

### Delayed acknowledgements across interruption and reselection

The native common-update probe now follows nine multi-update sequences (three
slots times target loss, deselection and explicit cancellation), executing the
real SET dispatcher and target retirement with controlled admission/readiness
sinks. A late SET 23 is retained while admission fails or the slot is deselected.
Re-admission/reselection creates one projectile even when readiness is false,
without another FireWeapon callback. Target retirement alone preserves the
acknowledgement; a subsequent explicit SET 21 cancels it.

Live synthetic scripts now exercise the same policy through real AimWeapon and
FireWeapon sleeps: Stop followed by retargeting, deselection followed by
reselection, and TargetCleared issuing SET 21 after a pending acknowledgement.
They verify no premature shot or mana charge during interruption, one release
and one mana charge on resumption, no duplicate callback, and no repeated shot
on the next tick. Passive behavior is selected with `World::setStance`; changing
only the fixture's legacy stance field left Fire At Will enabled and incorrectly
allowed auto-acquisition in the initial test. No production fix was needed.

These cases establish delayed acknowledgement retention/cancellation around
explicit order and selection changes. Actual target-death, range/visibility-loss
admission timing and full native mission/script phase comparison remain open.

Validation: all targets rebuilt in all three configurations. The live script
regression passes in all three; focused retail_visual checks also pass in Debug
and optimized Debug. The expanded 4,096-case update oracle and nine interruption
timelines pass against all three builds. `git diff --check` passes. No production
code or network state format changed in this step.

### Pending-death target rejection and immediate retirement

Native 51a9a0 rejects an invalid, dead or pending-death unit reference and calls
51a7f0 during that same lookup. The new target-lookup probe executes both
routines across 768 selection, reference-kind, bounds and alive/dying flag
combinations. It also confirms that unselected slots and ground-point references
do not use unit lookup, that invalid unit references trigger TargetCleared
immediately, and that lookup/retirement itself preserves aim flags.

The live combat path previously checked only `alive()` and deferred its clear
callback until the tick after dropping the order. It now rejects pending-death
targets and clears the established script target before retiring the attack
order. `queryWeaponAim` likewise rejects those targets. World maps pending death
through its existing HP/death-sweep rule, preserving the zero-HP unfinished
retail construction-site exception. No movement/pathfinding calculation changed.

Live regressions cover both pending death and already-retired targets, with and
without a TargetCleared callback that issues SET 21. A pending shot cannot fire
at the lost target or spend mana; target-clear is delivered in that same update
and is not repeated next tick. Cancellation remains script-owned: absent the
callback, the acknowledgement remains available for a later valid target. This
complements the explicit Stop/reselection timelines recorded above; it does not
establish every range/visibility or whole-frame native scheduling case.

Validation: all targets rebuilt in all three configurations. The full suite
passes 129 executions; the subsequently expanded missing-clear-callback cases
also pass in all three script-test binaries. All 69 projectile scenario hashes
agree across builds. The native lookup probe passes, GCC/Clang math checks
retain dcef618cd2e4d558 (ARM target headers remain unavailable), and both
600-tick network runs agree on 9955322ef77531a1. `git diff --check` passes.

### Remove unsupported projectile nimbus glow

The legacy sprite renderer no longer draws a translucent, double-size duplicate
of a projectile when its weapon has `nimbus`. The native initializer uses this
flag for faction caster animation and a conditional buildup delay, as recorded
above; it does not justify that extra projectile sprite. The Weapon field
comment now describes that meaning. This correction changes only rendering;
caster effect integration and conditional launch timing remain open.

The native launch probe again passes all 4,096 cases, including the conditional
delay. This is evidence for the native behavior, not a claim that the live
caster effect or delay is implemented.

Validation: all targets rebuilt in Release, Debug and optimized Debug. The
retail_visual, shadow and model_transform checks pass in all three builds
(nine executions). `git diff --check` passes. No simulation logic changed.

### Nimbus attachment and authored animation lifetime

`probe_effect_animation_clock.py` executes real 537390 startup, 5373d0 single
updates, 537430 batched updates and 5367e0 duration lookup with synthetic
metadata. All 131,072 updates agree on frame index, remaining duration,
looping, expiration, startup bounds and return value. The batched routine holds
single-frame animations indefinitely; the single-update routine expires a
non-looping one-frame animation after its duration. These paths are not
interchangeable. Positive authored durations are covered; malformed zero or
negative duration behavior is not claimed.

The unit update at 51df57..51df6b uses the single-update routine for its nimbus
state at display-host +1a4, gated by the animation pointer at +1ac. The draw
branch at 4eccea..4ecd3a reads the owner's body position at unit +68, truncates
coordinates to signed whole pixels, and projects x-cameraX,
z-(height>>1)-cameraY. It draws the current animation frame with three zero
render options. `probe_nimbus_projection.py` executes that branch with only the
frame lookup and final renderer replaced by argument-recording sinks: all
4,096 active/inactive, fractional and negative-coordinate cases pass.

This establishes the attachment and lifetime policy needed by the pending live
caster effect integration; it does not yet add that effect or its launch delay.
No production behavior changed in this step. Both new probes pass and
`git diff --check` passes.

### Live caster nimbus for ordinary and lightning shots

The display now reads faction nimbus names from sidedata (both name and
nameprefix, since units use prefixes such as ZON), starts/restarts a per-caster
instance on projectile creation, follows the captured whole-pixel body position,
and expires using authored frame durations and simulation ticks. This covers the
ordinary initializer shared by ordinary LOS and lightning shots; it does not
pretend that every weapon subclass uses that initializer.

Additional native loader inspection at 4c2d59..4c2d87 establishes that the faction
loader explicitly clears the loaded nimbus animation's loop byte. The shipped
Zhon TAF has a loop flag of one, but runtime nimbus is forced to play once. This
resolves the file/runtime distinction left open by the generic clock probe.

The screenshot harness now steps display animations during projectile capture,
so short-lived firing events are consumed before pausing. An initial capture
exposed the name/prefix mismatch, which was corrected before accepting the
rendered result. Conditional projectile buildup delay and exact raster/blending
and frame-phase parity remain open; this is live caster-art integration only.

Validation: all three configurations rebuilt successfully. The native clock
(131,072 updates) and projection (4,096 cases) probes pass, as do the nine
retail_visual/shadow/model_transform checks across builds. Final optimized-Debug
capture at tick 28 reports one nimbus instance; `/tmp/nimbus-live-zonhunt-final.png`
was visually inspected and shows the purple ring around the caster, with the
lightning extending toward the target. No gameplay or pathfinding logic changed.
The capture initially consumed an old pinned snapshot; projectile capture now
pins each newly published snapshot and runs cosmetics before pausing. The image
is a live integration check, not a paired retail raster comparison.

### Conditional ordinary/lightning buildup delay

Ordinary and lightning projectile creation now delays activation only when the
weapon requests nimbus and the faction has a resolvable named nimbus animation.
The loader retains the native integer buildup count: seconds multiplied by 30
and truncated toward zero (0.25 seconds is seven ticks). The new native
`probe_weapon_buildup_ticks.py` executes the actual loader multiply and x87 cast
for 4,106 inputs; the existing 4,096-case launch probe covers the conditional
addition and resulting expiry. The fire/flame initializer is separate and is
not given this ordinary-projectile delay.

`gaf::factionNimbus` shares faction name/prefix and art-availability resolution
between TypeRegistry, display and gameplayHash. The checksum includes resolved
availability, not pixels: valid recolors remain cosmetic, while missing or
invalid art cannot silently change projectile timing between peers. A synthetic
GAF/TDF regression verifies absent/present/invalid art, both faction aliases and
pixel-only edits. Initial fixture failures were its line-packed TDF assignments
and an attempted corruption of an already-zero header byte; both were corrected.

Eight direct World firing/flight cases cover ordinary/lightning crossed with
nimbus on/off and art available/unavailable. They verify activation tick, no
motion during buildup/activation, and motion afterward. An initial attempt to
reach these cases through a minimal AI/movement fixture never fired; the test
now uses the existing direct-fire probe to isolate the requested launch behavior.
The shipped Zhon registry resolves nimbus in both balances. No pathfinding or
movement math changed. Other weapon-class buildup, exact caster raster/blending
and whole-frame callback phase parity still require the remaining audit.

Validation: all targets rebuilt in Release, Debug and optimized Debug. All 44
CTest cases pass in each configuration after the new fixture corrections; the
43 other cases passed in the full sweeps and the corrected nimbus case was
rerun separately. All 69 projectile scenario hashes agree across builds.
GCC/Clang O0/O2/O3 retain math golden dcef618cd2e4d558; ARM checks skip for
missing target headers. The two 600-tick network runs agree on 9955322ef77531a1.
Both native buildup/launch probes pass, and `git diff --check` passes.

### Remote and wandering caster nimbus

`probe_remote_wandering_nimbus.py` executes the complete remote initializer at
52e890 with aim-point resolution controlled, plus the wandering initializer's
activation tail at 52faa0. All 8,192 branch cases agree on nimbus flag/art
admission, restart arguments and wrapped activation delay (including tick wrap
and negative delay inputs). Remote initialization copies the caster body origin
and faction art reference. The wandering geometry prefix and both classes'
subsequent update/damage timelines are explicitly outside this probe.

The live caster renderer now starts nimbus for Remote and Wandering weapons too,
without requiring positive projectile velocity. It still does not infer this
behavior for other unverified initializers. A debug-only nimbus capture option
pins each simulation snapshot and freezes at a requested effect age, allowing
non-lightning spells to be inspected. Optimized-Debug captures of tarlich Death
Aura (tick 212) and tarwitch Tornado (tick 149), both at age four, show the Taros
ring attached to the caster. Images `/tmp/nimbus-tarlich.png` and
`/tmp/nimbus-tarwitch.png` were inspected. They establish live attachment, not
paired retail raster or complete spell timeline parity.

All three builds rebuilt successfully; nimbus, retail_visual, shadow and
model_transform pass in all three (12 checks). The new native probe passes and
`git diff --check` passes. No simulation or pathfinding logic changed in this
step. Remote/wandering timing remains a separate open integration gate.

### Wandering startup, active and ending phase evidence

The current live storm loop moves and varies during `arm`, then decrements its
remaining lifetime and applies damage. Native 52fb10 contradicts that model:

- Before activation, there is no motion, animation clock update or damage. A
  dead or pending-death caster disposes the storm in this phase only.
- At activation, start art is initialized without motion or damage. If absent,
  the active-loop initializer runs immediately, also without a motion step.
- While start art runs, its authored clock advances without movement. Its
  expiration enters the active loop; the active duration begins at that point.
- Active frames advance the loop animation, integrate wrapped XYZ velocity once
  per substep, vary when due and apply damage. On the expiry tick they still
  damage before starting end art (or being disposed if no end art).
- End art advances and the storm still moves, but it no longer damages. The
  frame that expires end art disposes it without another motion step.
- Caster death after activation does not cancel these later phases.

`probe_wandering_update.py` runs the real update and authored animation clocks
across 4,096 phase/death/art/expiry/substep combinations. Disposal, damage and
active initialization are recording sinks; variation is scheduled in the future.
`probe_wandering_activation.py` separately runs real 52f6c0: 4,096 cases verify
loop-art startup, duration/variation deadlines relative to activation, and XYZ
velocity initialized from two controlled floating-point samples. Only the random
sampler is substituted in that probe; real animation and conversion code runs.
All cases pass, including wrapped positions/deadlines. Neither probe establishes
native RNG or launch-geometry equivalence.

This is actionable evidence for replacing live storm phase scheduling, not a
claim that the current live storm implementation now matches it. No production
code changed in this step; the existing binaries remain current.

### Live wandering phase scheduler and authored frame clocks

Storms now have explicit Waiting, Starting, Active and Ending phases. Waiting
uses the native conditional nimbus buildup deadline and cancels on caster death.
Starting holds position until its authored animation completes. Active-loop
startup sets duration and variation deadlines relative to that transition;
active frames move before varying and damaging, including the expiry frame.
Ending retains movement without damage and retires on animation expiration.
Caster death after activation does not cancel the remaining phases. Start/end
art are one-shot and loop art repeats, matching loader overrides at
52f853/52f893/52f8d8. Missing optional start/end art transitions directly.

The shared RetailEffectClock uses the native single-tick unsigned frame-duration
rules. GAF decoding now retains a separate raw low-word duration alongside the
legacy cosmetic clamped duration, so simulation does not inherit that clamp.
TypeRegistry resolves storm animation timing; gameplayHash includes named storm
frame counts/durations and availability while allowing pixel-only recolors.
The checksum fixture verifies a 501-tick authored frame and cosmetic pixel edits.

The renderer reads the authoritative phase/frame. It hides waiting storms and
keeps end art attached to the moving storm, removing the detached stationary
end effect. Storm phase, clock and deadlines are hashed, along with its owner
reference and launch direction. No navigation or unit movement math changed.
The existing storm launch geometry, planar velocity/clamping and burn-RNG
variation remain approximations and are explicitly not claimed as ported here.

Four direct World timelines cover optional start/end art, no startup movement
or damage, post-animation duration, active damage through expiry, ending motion
without damage, final retirement and survival after caster death. A separate
case verifies death cancellation during waiting. The shared C++ frame clock
matches 1,024 native 64-update timelines in every build; the generic native probe
also retains its 131,072 single/batched clock checks. The native wandering update
and activation probes each pass 4,096 cases.

All 44 CTests pass in Release, Debug and optimized Debug (132 executions), and
all 69 projectile scenario hashes agree. All targets rebuilt in all three.
GCC/Clang O0/O2/O3 retain dcef618cd2e4d558; ARM checks skip for missing headers.
Both 600-tick network runs agree on 9955322ef77531a1. `git diff --check` passes.

A debug storm-phase capture was added and the projectile fixture now sets the
victim's actual hold-fire order rather than only its legacy stance field.
Captures center the requested storm phase so travel cannot carry it out of the
viewport. The final ending capture `/tmp/storm-phase-ending-framed.png` (tick
593, phase Ending, frame zero) was inspected and shows authored end art at the
live storm position. This is a render integration check, not native pixel parity.
The capture-only changes were rebuilt in all three configurations afterward.

### Private wandering sampler and aim-derived seed

Native 52f780 uses a per-storm sampler, not the shared game/burn RNG. The live
storm now owns and hashes that state, initialized from the accepted aim words
(heading in the low word, pitch in the high word). The sampler preserves native
32-bit arithmetic, zero-state substitution, and its unsigned floating-point
scale. Unlike the general game RNG, it does not correct wrapped negative
intermediates; even a zero span consumes a draw. Variation uses two draws in
X/Z order, subtracts signed amplitudes, and truncates to int64 before keeping
the low 32-bit fixed-point result.

`check_wandering_random.py` executes the actual sampler, stores its x87 result
as double via a synthetic caller, and compares 8,192 states/results against the
shared C++ implementation in all three builds. It includes zero spans, signed
spans, zero and unsigned boundary seeds. It also executes the real initializer's
seed-packing tail for 8,192 aim pairs. All pass with the native 53-bit x87 mode.

A live regression fires two identically aimed storms into one world and checks
that their independent seeds, offsets and trajectories remain equal through
repeated variations, without the prior fire-spread RNG draws. This removes
cross-storm/shared-RNG interference; it does not establish complete trajectory
parity. Direction normalization, launch geometry, velocity quantization and
planar clamping still use the existing approximation and require their own port.
The signed amplitudes now follow the native direction convention, but still
inherit that approximate normalized direction.

Validation: all targets rebuilt in Release, Debug and optimized Debug; all 44
CTest cases pass in each (132 executions). All 69 projectile scenario hashes
agree across builds. GCC/Clang math goldens retain dcef618cd2e4d558 (ARM target
headers remain unavailable), and repeated 600-tick network runs agree on
9955322ef77531a1. Native sampler/seed comparisons pass in all three builds;
`git diff --check` passes. No unit navigation/pathfinding calculation changed.

### Native wandering launch geometry and wrapped motion

The live launch path now uses retailStormLaunch, checked against the complete
native 52f970 initializer across 8,192 cases in every build. Normalization uses
signed whole-word source/aim coordinates and wrapped squared distance. X crosses
an authored float boundary while Z remains double; base velocity and signed
variation amplitudes preserve that asymmetry. The 32-pixel launch offset is
computed before the terrain query. Invalid/overflowed normalization follows the
native integer-conversion result rather than inventing a direction.

World retains the native quantized base/current velocity, signed float variation
amplitudes and substep count, and hashes all of them. Variation uses the private
sampler with those amplitudes. Active/ending integration wraps fixed-point XYZ
substeps and no longer clamps storms to map bounds or substitutes a default
velocity. A live 960-pixel/sec fixture checks the two-substep case and crossing
beyond the map edge. The existing lifecycle and independent-stream tests pass
with the new representation.

The launch oracle controls only terrain lookup; real base initialization runs.
It compares position, velocity and exact amplitude bits and checks terrain-query
coordinates, returned height, packed aim seed and timestamps. The earlier remote
probe incorrectly described 530710 as aim-point resolution: it only writes the
creation timestamp. That stub was removed; the remote probe now executes and
checks the real timestamp initializer too. The stored aim point is copied by the
common factory from its weapon record before class initialization.

Full factory/aim-point phase equivalence remains open: World currently queries
its target point when firing. Also, native 52fd00 refreshes the storm's terrain
height during drawing before projected visibility and sprite submission. Exact
render projection/blending and the implications of that draw-time height write
are not established by this launch/motion port. These checks do not prove every
storm collision/damage or full-frame script interaction.

Validation: all targets rebuilt in Release, Debug and optimized Debug, all 44
CTest cases pass in each (132 executions), and all 69 projectile scenario hashes
agree. Native launch comparisons pass in every build. GCC/Clang math goldens
retain dcef618cd2e4d558 (ARM headers unavailable), and both 600-tick network runs
agree on 9955322ef77531a1. `git diff --check` passes. No navigation code changed.

### Native wandering draw projection

`probe_wandering_projection.py` executes full native 52fd00 with controlled
terrain, visibility, frame and raster sinks. All 4,096 cases pass, covering the
unsigned activation gate, signed whole-coordinate projection, odd/negative
heights, and terrain-Y refresh before visibility rejection. The probe does not
establish the visibility function or pixel blending internals.

The client now uses the existing native terrain-height helper and projects
whole X/Z with integer half-height for storm sprites, replacing the unrelated
smooth terrain-lift interpolation. Height refresh stays local to drawing; no
render-time mutation enters deterministic state. The implications of retail's
stored Y write for other consumers remain open, as do exact projected fog and
blending. No simulation or navigation code changed in this correction.

Validation: all targets built in Release, Debug and optimized Debug. Release
retail_visual, transport_roster, animation_roster and flyer_combat pass (4/4).
The optimized Debug ending-phase capture at tick 593/frame 0 was inspected at
`/tmp/storm-native-projection.png`; this is a rendering smoke check, not a paired
retail pixel comparison. Native projection cases and `git diff --check` pass.

### Unclamped renderer effect durations

The client effect loader now retains `retailDelayTicks`, the decoded native
16-bit duration, instead of narrowing the already clamped legacy `delayTicks`.
Nimbus and projectile-art frame selection therefore no longer shortens authored
durations above 300 ticks. Zero still occupies one update in these single-clock
paths. The native clock probe retains its original 1,024 short-duration cases
and adds 256 single-update edge-duration timelines (0, 300, 301, 32767, 32768,
65535). All 147,456 native updates pass; the shared C++ clock agrees on all
1,280 single-update timelines. Long-duration batch updates have different
signed behavior and are not established by these added cases; the live nimbus
and storm paths use single-update semantics.

All targets rebuilt in all three build directories. Release nimbus and
retail_visual tests pass and `git diff --check` passes. This establishes duration
preservation, not complete nimbus phase/blend parity. Empty-frame handling,
exact visibility and paired retail frame captures remain to be audited.

### Preserve blank effect frames

The effect texture loader discarded frames with zero width or height, while
GAF decoding and simulation animation timing retained them. It now retains
one texture entry per authored frame, using a transparent 1x1 backing texture
for blank frames while preserving their original dimensions, anchors and
clock duration. This prevents later frame indices from shifting relative to
storm simulation and preserves blank-frame time in nimbus/projectile art.

The nimbus fixture verifies that a zero-width frame remains decoded, retains
its 501-tick duration and does not disable faction art availability. This is a
synthetic edge-case check; it does not establish how often shipped effects use
blank frames. Release retail_visual, nimbus and flyer_combat pass. All targets
rebuilt in Release, Debug and optimized Debug. The ordinary storm ending-frame
capture at tick 593 was inspected (`/tmp/storm-blank-frame-preservation.png`);
it is a smoke check, not a blank-frame or paired retail pixel test.
`git diff --check` passes. No simulation/pathfinding changes.

Sequence-selection parity remains open: the display loader still permits a
first-sequence fallback while nimbus availability and storm timing require a
matching sequence name. Native lookup behavior and shipped sequence names need
checking before changing that policy globally.

### Native exact sequence lookup

Native 537550 scans sequence pointers in order and invokes the real CRT
case-insensitive string comparison. `probe_animation_lookup.py` executes it
without substitutions for 2,048 mixed-case, duplicate, absent and empty-name
queries, plus a null-container check. First exact match wins; a missing name
returns null, with no first-sequence fallback. The wandering loader calls this
through 4bd420.

The display effect loader now applies that exact case-insensitive selection,
including explicit file:sequence requests, and no longer substitutes unrelated
first-sequence art. File suffix resolution remains unchanged and is outside
this native name-lookup proof. Full shipped-effect inventory and raster/blend
parity remain open.

All three builds rebuilt. Release retail_visual, nimbus and flyer_combat pass;
all native lookup cases and `git diff --check` pass. Optimized Debug storm
capture still reaches ending frame 0 at tick 593 with visible authored art
(`/tmp/storm-exact-sequence.png`, inspected). This is a smoke check, not full
roster visual proof. No simulation or pathfinding code changed.

### Installed weapon and common effect sequence inventory

Added `effect_inventory <retail-root>` as a reproducible inspection tool. It
collects weapon sprite, shot-art, wandering phase, radius-art and projectile
shadow requests from both balance registries, plus faction nimbus and explicit
common script smoke/flame/build requests. Against `assets/game`, all 60 unique
requests resolve to exact sequence names. None requires the removed unrelated
first-sequence fallback. This verifies installed-data availability for these
categories, not all explosion definitions, script timing, rasterization or
transport behavior. Faction nimbus requests are drawn from the existing
availability resolver, so this inventory does not independently establish
missing-nimbus behavior (covered separately by nimbus_test).

All targets built in all three configurations; the Release inventory reports
60/60 and `git diff --check` passes. No runtime behavior changed this turn.

### Pickup interruption and recovery

`probe_transport_invalidation.py` executes both native pickup handlers across
all four stages with a null passenger, inactive passenger flag, terminal flag,
or cancellation event 8. All 32 cases abort with result 8 and failure chatter
before dispatch, without changing mission bytes. Only chatter is a sink; no
navigation, transfer or attachment call is reached. This establishes admission
ordering, not the full mission scheduler's handling of the return code.

Added live World cases for air/surface carriers interrupted in stage 2 by
passenger Stop or death. All four cases enter transfer, never attach the
interrupted passenger, and successfully board a replacement passenger after
cleanup. The whole transport_test passes in Release, Debug and optimized Debug;
its targets were rebuilt in all three. The existing native pickup probe also
passes 4,096 transfer states, fractional boundaries/departure goals and the
stationary in-range full pickup timelines. `git diff --check` passes.

No production behavior changed: current cleanup passed these checks. Full
native distant/coastal/blocked approach traces and broader reciprocal mission
resumption remain open; these interruption cases do not close those gates.

### Unload Stop and replacement destination

Added six live World unload interruption cases: air and surface carriers stopped
1, 7 or 14 ticks after issuing an in-range unload. Each fixture boards through
the real pickup path, checks cargo is still attached before Stop, waits beyond
the old transfer deadline, and verifies cargo remains aboard. A replacement
unload releases the passenger at the new destination with an empty cargo list;
the cancelled destination is not reused. All transport_test cases pass in
Release, Debug and optimized Debug after rebuilding those test targets.

Native unload probes also pass 94 air and 56 surface cases, transfer/retry
states, 36 PARK radii per class and 4,096 range comparisons per invocation.
`probe_transport_unload_retry_cancel.py` now pairs the air retry path through
the native dispatcher and real mission-removal routine; the remaining approach
and route scheduler limits are recorded below. No production changes were
needed. `git diff --check` passes.

### Blocked unload retry cancellation and reissue

`probe_transport_unload_retry_cancel.py` executes native `0x41ae20` through
`0x4d8450` with a controlled moving-blocker result: strict placement fails,
relaxed placement succeeds, and the dispatcher advances the transfer mission
to retry stage 3. It then calls the real `0x4d6ad0` remover and verifies that
the mission leaves the carrier queue while the passenger remains attached in
both the carrier cargo list and passenger transport reference. A fresh unload
mission at a different destination completes once and places the passenger at
that exact new point (native completion tick 20 in this controlled fixture).

The paired World regression in `transport_test` creates the same blocked
stage-3 state, issues Stop, confirms cargo remains attached, and then reissues
the unload at a distinct point. Both air and surface carriers release cargo
there without retaining the old mission or destination. `transport_test` and
the new probe pass. The native fixture controls placement and flight arrival at
their host boundaries, so it verifies mission cleanup/reissue state and exact
release destination, not retail collision geometry or full routed movement.
There is no evidence for a production mismatch or fix from this case.

`probe_transport_surface_unload_retry_cancel.py` now covers the native sea
counterpart: retail `GROUND_UNLOAD` reaches retry stage 3 after strict placement
fails and the relaxed moving-blocker check succeeds. The probe then executes
the real `0x4d6ad0` mission remover and checks that the carrier queue is empty
while both carrier cargo and passenger transport references remain reciprocal.
A fresh sea-unload mission at a distinct exact point releases that passenger on
native tick 20 and retires on tick 21. The probe checks the existing World Stop
and replacement-destination assertions using `build-o2/transport_test`; no
shared target was rebuilt. The native fixture keeps the replacement point in
transfer range, so this does not establish its full sea route. Resuming the same
interrupted native sea-unload trip is still unverified; the mixed parent/child
dispatcher fixture faulted before producing reliable evidence and is not used
to close this gate.

### Transport effect durations corrected

Transport visuals previously stretched mindspin and transswirl to 0.5 seconds.
Native creation 421e10 starts the authored clock; updater 421ca0 performs one
5373d0 update per tick and removes the node only when the animation finishes.
The loader at 4bdea8/4bdee6 explicitly disables looping on both animations.
`probe_transport_effect_clock.py` executes real creation/update/clock routines
with allocation/free sinks across 256 timelines, including zero-duration
frames, and verifies frame indices and exact expiry.

Transport effect instances now select frames and expire from simulation tick
age using authored durations, independent of wall-clock animation age and of
the cargo transfer's fifteen-tick interval. The installed inventory finds
mindspin has 14 frames/28 ticks and transswirl 11 frames/22 ticks. Both resolve
exactly; the expanded inventory now resolves 62/62 requests. Other generic
effect timing policies are unchanged pending their separate native checks.

All client configurations rebuilt. Release transport, transport_roster,
retail_visual and nimbus pass, as do the native lifecycle probe and diff check.
Exact spawn phase under skipped render frames, 3D projection and visibility/
blend parity remain open; this change establishes authored playback duration,
not a paired complete transport-render timeline.

### Captured transport effect positions

Native 421e10 copies the effect's full fixed-point XYZ into a detached list
node. The lifecycle probe now varies those coordinates and changes the source
point after creation, checking that every update leaves the effect at its
original position. Native draw 421d00 projects signed whole X/Z and half the
signed whole Y. `probe_transport_effect_projection.py` checks 4,096 draws with
visibility and raster sinks, including negative/fractional coordinates.

The client now captures authoritative worldPosition for carrier transswirl and
pickup-passenger mindspin, and draws those effects with native whole-coordinate
projection. This removes smooth terrain-lift interpolation and the generic
unit-altitude offset from these two placements. Unload destination mindspin
still uses its existing ground placement until the mission point's Y source is
established; full effect event capture under skipped display frames is also
open. Exact visibility and blending remain outside this projection probe.

The native lifecycle/projection probes and Release transport, transport_roster,
retail_visual and nimbus tests pass. All client builds rebuilt. No simulation
or navigation code changed. There is no paired full-frame retail capture for
this correction yet.

### Unload effect height source established

Expanded the native unload probe with six independently varied mission-point
heights per carrier class, including negative and fractional fixed-point Y.
Both handlers pass the full mission destination XYZ to mindspin and the full
carrier XYZ to transswirl. They do not replace the mission Y with a terrain
lookup or the passenger height at transfer start. The resulting 100 air and
62 surface observations pass, including existing transfer/retry, PARK and range
comparisons. Probe output is in /tmp/unload-height-{air,sea}.json.

This changes the next implementation step: retaining the original destination
Y is required. The current World unload order keeps only X/Z; substituting a
fresh draw-time terrain sample would not establish retail behavior. Native
command issuance and destination-height construction must be traced before
adding the field. No production code changed in this investigation. The landing
effect placement gate remains open; the carrier/pickup projection corrections
from the preceding section remain valid. `git diff --check` passes.

### Mission destination retention upstream of unload

`probe_mission_destination.py` executes native 4d6c40 with descriptor lookup and
unit-reference bookkeeping controlled. Across 2,048 cases, the constructor
copies supplied fixed-point XYZ to mission+22 exactly, including arbitrary Y;
a null destination produces zero XYZ. Changing the caller's point afterward
does not change the stored destination. Generic insertion 4d78a0 passes its
point argument through to this constructor. There is no constructor terrain
lookup to emulate.

This narrows the missing work to command point production/serialization,
rather than mission initialization or unload transfer. Static inspection also
shows the text-order parser 513fb0 has coordinate branches initializing Y to
zero before insertion; a universal terrain-height default would therefore be
unjustified. The click/network unload path still needs its own trace before
choosing the public API default and adding retained order height. No runtime
change was made from this incomplete upstream evidence. The goal remains open.

Validation: the 2,048-case native constructor probe and `git diff --check` pass.

### Clicked destination height producer located

The cursor updater at 5285af calls 510da0 with the map cursor coordinates and
writes its full XYZ result to game+17614. Point-command entry 527e20 passes that
stored point to dispatcher 5214b0, which forwards a destination pointer toward
mission insertion. Native 510da0 ray-picks the terrain and clamps sampled height
to at least the map sea-level byte. This distinguishes clicked commands from
text-order branches that explicitly construct Y=0.

`probe_clicked_destination.py` executes the complete picker with controlled
constant terrain-height queries. All 2,048 cases pass for terrain heights
-1..255, sea levels 0..255 and clamped X. The returned Y is
max(terrain, sea)*65536. This establishes flat terrain/water height; sloped
ray-picking geometry and end-to-end command dispatch are not yet proved by it.

The production integration point is now identified: both HUD unload issuance
paths create Cmd::Unload, matchsetup.cpp dispatches only c.x/c.z to unloadAt,
and the order retains only X/Z. Preserve the clicked height through this path
and distinguish non-click callers before changing landing-effect rendering.
No production code changed this turn. Native picker probe and diff check pass.

### Retained unload destination height integrated

Both clicked unload issuance paths now compute destination Y with the shared
native terrain sampler, clamped to the map sea-level byte. Cmd::Unload uses its
otherwise unused targetId word for signed 16.16 Y; the existing 35-byte command
codec, replay and server relay preserve it without a packet-size change.
The current unreleased protocol remains 178. Command dispatch supplies that Y
to unloadAt; direct coordinate-only callers retain a zero default, matching
explicit zero-height scripted points rather than silently resampling them.

Orders retain transportY alongside X/Z, copy it through route replacement,
and include it in the unload-order state checksum. The destination mindspin
now receives this full XYZ for the verified transient-effect projection.
Native placement/navigation calculations still use their existing X/Z inputs;
passenger ground support is still computed when detaching. No pathfinding
algorithm changed.

New live tests cover signed/fractional Y through the wire codec, command
application, retained approach queue, checksum sensitivity and direct-call
zero defaults, for both air and surface carriers. All targets rebuilt in
Release, Debug and optimized Debug; all 44 CTests pass in each. All 69
flame/straight/lightning scenario hashes match across builds. GCC/Clang math
checks retain golden dcef618cd2e4d558 (ARM target headers unavailable).

This closes the missing retained-height field, not full click-picking parity:
the existing screen-to-world X/Z conversion and native sloped ray-picking still
need comparison. Exact event delivery with skipped display frames and paired
transport render captures also remain open.

Repeated 600-tick network runs also agree on 9955322ef77531a1. These are general
lockstep regression runs; the explicit unload-height wire/dispatch behavior is
covered by the targeted transport cases above. Final diff check passes.

### Retain transport effects while hidden

The native transport-effect draw probe now alternates viewport and visibility
admission, checking that rejected draws leave the transient node/list unchanged
and that later visible draws render it again. This complements native creation
and independent clock expiry: fog is a draw gate, not a creation gate.

The client no longer discards mindspin/transswirl at spawn when their world
cell is hidden. It retains their authored clock and captured position, with
visibility checked at draw time. Generic effect drawing now also honors noFog_
consistently with other effect passes. Normal fog still suppresses drawing;
this change does not expose hidden units or target selection.

The 4,096 native projection cases and alternating admission sequence pass.
Release transport, transport_roster and retail_visual pass. All three client
builds rebuilt; diff check passes. Exact native projected fog-grid mapping,
end-to-end reveal captures and event delivery across skipped frames remain open.
No simulation or pathfinding code changed.

### Native projected visibility grid measured

Extended `probe_transport_effect_projection.py` to execute 421d00's alternate
visibility branch (configuration object +15 nonzero) against a real synthetic
player grid, without a visibility-helper sink. All 4,096 cases pass: the selected
player is game+306f, player records stride 0x110 from game+2404, and the branch
indexes `(wholeX>>5, (wholeZ-(wholeY>>1))>>5)`. Out-of-bounds cells reject draws;
any nonzero grid byte admits them. The existing 4,096 projection and alternating
helper-branch admission checks still pass.

Current client fog snapshots are the World's unprojected 16-pixel navigation
cells, with 0 hidden/1 explored/2 visible. Therefore replacing cellVisibleR with
this native indexing/nonzero test against the existing buffer would be wrong
and could reveal explored-only effects. Native grid production and the meaning
of the configuration switch must be established before integrating this
visibility branch. This is stronger evidence for the remaining representation
gap, not proof of full fog/render parity. No production code changed.

Validation: expanded native probe and `git diff --check` pass.

### Alternate effect visibility helper verified

`probe_effect_visibility.py` executes 4223f0 without substitutions against a
synthetic native 16-bit grid. All 8,192 cases pass: the same projected 32-pixel
coordinates and bounds apply, but this branch reads game+19ef4 and tests the
selected player's bit (game+306f), rather than a per-player nonzero byte.
Thus both branches use projected coordinates; only the backing masks differ.

Static save/load routines 511070/5110d0 serialize this shared mask as
Mapping/Data. That persistence is evidence about storage, not sufficient proof
that the mask is explored-only or current visibility. The configuration switch
and grid update paths still need tracing before selecting an equivalent client
representation. Existing ground-cell ==2 checks were not replaced on an
unverified assumption. No production changes this turn.

Validation: all 8,192 native helper cases and `git diff --check` pass.

### Current visibility versus explored mapping distinguished

`probe_effect_visibility_updates.py` executes native 4c6800 with two overlapping
small revealers. All 256 lifecycles pass: enabling increments the per-player
byte grid, duplicate enabling is idempotent, a second revealer increments to
two, and removal decrements back to zero. The shared 16-bit mapping grid ORs
the selected player bit on reveal and retains it after both revealers leave.
Existing unrelated mapping bits are preserved. The tested 3x3 footprint takes
the native near-center branch; distant LOS and height-dependent stamp geometry
are explicitly outside this probe.

Together with the draw probes, this establishes that the nonzero configuration
branch reads current visibility counts, and the alternate helper branch reads
retained explored mapping bits. The remaining representation gap is projected
grid construction, not ambiguity over those masks. Do not substitute the
existing 16-pixel unprojected fog snapshot directly for either native buffer.
No production code changed. Native lifecycle probe and diff check pass.

### Revealer initialization and movement measured

`probe_visibility_revealer_init.py` executes native 4c66b0 without substitutions
for 2,048 cases. It copies raw unit XYZ, takes sight radius from type+226 and
eye height from type+14c, initializes cached cells to -1, and routes allied
revealers to the local player's record according to the native alliance table.
It does not project the input position during construction.

The overlap/update probe now also runs actual 4c6a70 refreshes: 512 moves remove
the old small stamp, install the new stamp and cache raw whole X/Z divided by
32, with unit whole Y plus authored eye height stored separately. Thus the
phrase 'projected grid' in prior notes describes effect lookup coordinates;
it must not be taken to mean every revealer center is preprojected the same
way. Full distant stamp geometry still needs verification before a port.
All constructor, overlap and refresh cases pass, as does diff check. No runtime
code changed in this investigation.

### Full sight-stamp admission measured

`probe_visibility_stamp.py` executes full native 4c6800 without routine
substitutions against randomized synthetic height-pair maps. All 512 cases
pass, including centers outside map boundaries, sight radii 1..256, eye offsets
1..255 and cached eye heights -20..400. The footprint clips a square of radius
trunc(sight/16); within it, squared distance <=2 is always admitted. Other cells
must also lie within that radius circle and satisfy nonnegative height
clearance and a scaled-distance threshold. Clearance uses cached eye height
minus the smaller of the cell's two precomputed bytes. Scale is the float-rounded
sight/(eyeOffset*32). Both current counts and retained player bits match the
observed admission set exactly.

This establishes the distant-stamp consumer, not production of the height-pair
map at game+19f08. Its terrain/occlusion construction remains necessary before
porting this into effect visibility. Replacing it with direct raw terrain
samples or the existing line-of-sight ray marcher would be unproven. No runtime
code changed. Native stamp probe and diff check pass.

### Visibility height-pair producer verified

`probe_visibility_height_pairs.py` executes the native accumulation
phase 50ea58..50ecc8 with only allocation/free and the phase exit controlled.
All 256 randomized/constant terrain maps match an independent model of the
observed output. Inputs are terrain cell heights from the native 14-byte cell
array; output dimensions are half the terrain dimensions, with allocation
rounded to eight entries and each pair initialized to (0,255).

The constructor walks columns and rows, projects each source row using half
its height, accumulates upper/lower height bytes into adjacent output columns,
and interpolates at projected cell boundaries. Both prior and newly selected
output cells participate in that accumulation; simply taking min/max of each
2x2 terrain block would be incorrect. The probe checks every output byte,
including allocation padding. Earlier map loading and feature-cell preparation
are outside its phase boundary.

This supplies evidence for the intermediate height pairs, before the final
weighted smoothing and sea-height clamp. The complete producer is already
implemented in `retailExplorationHeights` and compared through 50ed53 by
`check_exploration.py`; the earlier statement that this producer still needed
porting was incorrect. The live client still uses its existing
fog representation: integrating the native producer, revealer lifecycle and
lookup together remains necessary before claiming effect visibility parity.
No production code changed. Native producer probe and diff check pass.

### Existing sight helpers revalidated; fire included in the audit

Re-ran `check_exploration.py` against `build-o2/retail_motion_test`: all 1,024
complete height-map cases, 8,192 sight-footprint cases, 8,192 cached sight-update
cases and 2,048 model-top cases match the native routines. Navigation already
uses these shared helpers. Effect-display integration should reuse them while
preserving navigation exploration; temporary revealers, render-frame capture
and native projected lookup remain open integration work.

The projectile scope includes flame weapons such as Drake fire. Re-ran the
native fire emission and scan comparisons: all 4,096 emission updates and 4,096
scan cases pass, including owner-state gates, moving muzzle position, spread,
collision clipping and retained flight duration. These checks do not establish
final pixel blending or complete visibility/lifecycle parity.

### Temporary sight retention measured

`probe_visibility_expiry.py` executes complete native 4c6750 and 4c6b30,
including their real 4c6800 count updates, without routine substitutions.
All 256 randomized lifecycles pass. Each creates 21 overlapping revealers:
20 transfer their active footprint into the temporary array without changing
counts; the overflow revealer removes its footprint immediately. The source
active flag clears in both cases. Expiry removes counts only when the unsigned
current tick is greater than the stored deadline, not at equality. Surviving
entries compact in their original order. Explored player bits remain set.
The test uses near-center footprints to isolate lifecycle from distant LOS.

Static caller inspection finds the direct call in unit-removal routine 512ae0:
the unit's owner ID is compared with game+306f; matching ownership requests a
60-tick retention, while other ownership removes sight immediately. This is
caller evidence, not yet an emulated whole unit-removal trace. It also means
allied revealers must not automatically inherit the local owner's death delay.
The live display-grid integration remains open; these observations define its
required removal behavior. No simulation or renderer behavior changed here.

### Transport effects captured at mission time

Pickup/unload transfer callbacks now append cosmetic `World::TransportFx`
events containing the callback tick and copied passenger/landing and carrier
XYZ. This replaces render-snapshot polling of transportTicks. The callback runs
before subsequent mover updates, so the captured position no longer comes from
a later rendered pose. Events do not participate in the lockstep checksum.

The client queues events after every world tick, including replay catch-up and
multiple simulation substeps. Rendering consumes only events at or before its
pinned snapshot tick, preserves the original start tick, and lets authored frame
expiry discard effects that have already finished. A bounded queue retains up
to 4,096 transfer events. The two sprite instances keep their captured XYZ and
do not follow moving units. This closes the identified skipped-snapshot start
and position gap; native visibility and blend parity remain separate gates.

`transportEffectEvents` exercises air and sea pickup/unload, one event per
transfer, exact destination XYZ (including fractional Y), callback timestamps,
value retention across later movement, and next-tick event clearing.

Validation: all targets rebuilt in Release, Debug and optimized Debug; all 44
CTests pass in each. Native transport-effect clock probe passes 256 timelines.
GCC/Clang determinism remains `dcef618cd2e4d558` (ARM skipped for missing target
headers); two 600-tick network runs both retain `9955322ef77531a1`. Logs are
`/tmp/transport-event-{build*,tests*,determinism,network}.log`. No pathfinding,
mission movement, transport timing or authoritative checksum behavior changed.

### Native current-sight container implemented and compared

`client/retaileffectvisibility.h` now provides display-owned byte counts using
shared native footprint admission, transfer into the 20-entry temporary array,
strict unsigned expiry, stable survivor compaction, and projected whole-XYZ
lookup. No unit scheduling or navigation state is owned by this helper.
`probe_visibility_expiry.py <retail_visual_test>` compares every byte and retained
entry count after all 53 steps in each of 256 native scenarios against this C++
container. Release, Debug and optimized Debug comparisons pass.

`probe_transport_effect_projection.py <retail_visual_test>` additionally compares
4,096 actual native current-grid draw decisions with the C++ projected lookup;
all pass. This includes negative/fractional coordinates, height lift, out-of-map
rejection, and nonzero byte counts. The existing native terrain/sight admission
comparisons remain the basis for the shared footprint helper.

This is the verified display-grid implementation, not completed live integration:
unit activation/removal/ownership transitions must feed it on the simulation
thread and counts must enter the pinned render snapshot before transport drawing
switches to this lookup. In particular, the native local-owner removal delay
must not be inferred solely from disappearing rendered units.

### Current sight integrated for captured transport effects

The client now updates the display grid after each simulation tick, including
replay catch-up, using each eligible unit's authoritative cached footprint.
It removes boarded/ineligible revealers immediately, installs unloaded units,
tracks changed footprint geometry and ownership, retains local-owner death
stamps for 60 ticks, and removes allied deaths without that delay. Repeated dead
snapshots do not extend expiry. Replay tick rollback or a viewer change rebuilds
the grid. The counts are copied into the pinned render frame; transport effects
with captured XYZ use native projected lookup at draw time. Spectators retain
full vision. Navigation, targeting and the older terrain-fog display are unchanged.

The shared display tracker has regression coverage for overlapping units,
unchanged snapshots, movement, boarding/unloading, alliance/ownership loss,
local/allied death and the exact expiry boundary. Client and visual-test targets
rebuilt in all three configurations; visual, transport and transport-roster
CTests pass in each. Native lifecycle and 4,096 projected admission comparisons
still pass. Two 600-tick network runs retain `9955322ef77531a1`.

Remaining fidelity boundary: the live integration observes the port's end-of-tick
unit life state. Whole retail unit-removal/Killed callback scheduling remains
unproven; the 60-tick native removal caller has only static caller evidence.
Other effect classes still need their own admission paths checked before using
this transport lookup. The alternate explored-mask configuration branch is not
wired by this change.

Rendered smoke: automatic `--mpai` startup with fog enabled completes a five-second
run and writes `/tmp/effect-sight-match.png`, inspected as an in-game frame.
An earlier three-second run only reached the lobby and is not in-game evidence.
This smoke exercises client startup/snapshotting, not a paired retail transport
capture or proof of transport pixels at a fog boundary.

### Flame visibility differs from per-particle fog

`probe_flame_render.py` executes 52d710, the real FlameEmitter draw 532340,
particle draw 4f16e0 and rectangle test 540150. Only endpoint viewport admission,
renderer-state calls and final raster submission are sinks; projected current
visibility reads are real. All 4,096 randomized cases pass. Stream admission is
(any endpoint passes viewport) AND (any endpoint passes projected current sight).
Those two endpoints need not be the same. Once admitted, each particle tests
only its projected center against the inclusive viewport rectangle, without an
individual fog test. Kind selection and whole-coordinate camera projection also
execute in the real particle draw. Optional shot art is absent in this probe;
renderer state/blending remain excluded.

The two admission endpoints are the retained muzzle at projectile+10 and the
captured aim point at +28. The emission update refreshes +10 through QueryWeapon
while active; it stops refreshing after the stream ends or its owner dies, while
existing particles drain. +28 is distinct from the clipped flame endpoint at
+98 and must not be reconstructed from that collision endpoint.

This exposes a live renderer mismatch: it flattens all flame particles into one
list and fog-tests each particle's ground X/Z. The next integration must retain
stream grouping plus muzzle and captured aim XYZ, use endpoint admission, then
perform particle viewport clipping. Simply applying the transport lookup to
each particle would still be wrong. No runtime behavior changed in this probe.

### Fire-stream admission integrated

Flame snapshots now retain each stream instead of flattening all particles. The
stream carries its current muzzle and captured aim point independently from its
moving damage front and collision-clipped endpoint. Scripted fire copies the
last aiming solution; direct fire reuses its existing aim query. Muzzle refresh
uses the existing QueryWeapon result during active emission, then freezes while
particles drain after expiry or owner death. These additional fields are display
geometry only and do not alter damage, random draws or the checksum.

Drawing now gates the stream by either endpoint in the viewport and either
endpoint in projected current sight. It then clips each particle's projected
center to the viewport without applying per-particle fog. Native endpoint
projection halves raw fixed Y before extracting whole Z; native particle
projection extracts whole coordinates first. Separate shared helpers preserve
that rounding difference. Camera/zoom adapts those projected points to the
client viewport.

The native flame draw probe now executes real endpoint viewport routine 48c870
as well as the real emitter, particle draw, current sight reads and rectangle
test. Only renderer-state calls and raster submission remain substituted.
All 4,096 full draw decisions match the C++ helpers in Release, Debug and
optimized Debug. Live flame regression checks now assert captured aim retention,
active muzzle refresh, and frozen endpoints while the dead owner's particles
drain. `/tmp/drake-stream-admission.png` is an inspected live Drake/fire capture;
it is not a paired retail pixel comparison. Whole-frame aim callback phase,
optional shot art and exact blend behavior remain open.

Validation: all targets rebuilt in all three configurations and all 44 CTests
pass in each. All 69 live flame/straight/lightning scenario hashes agree across
builds. The 4,096 native flame particle comparisons pass; GCC/Clang determinism
retains `dcef618cd2e4d558` (ARM headers unavailable), and both 600-tick network
runs retain `9955322ef77531a1`. Logs: `/tmp/flame-admission-*.log`.

### Truecolor sprite blend-state selection measured

`probe_sprite_blend.py` executes full 4fac00 for 1,536 synthetic truecolor draws:
formats 4 and 5, every runtime frame+0b value, and call flags 0/1/255. Renderer
acquisition, state setters and final primitive submission are sinks. Format 4
sets render state 27 to 1 and state 19 to 5; state 20 is 6 when frame+0b is 255,
otherwise 2. It restores states 19/20 to 5/6 after submission. This branch's
choice is independent of the draw call's special flag. Format 5 selects renderer
modes [8,5] only when the call flag is nonzero and frame+0b is not 255, otherwise
[8,4]. These mode numbers still need tracing through the renderer implementation.

All cases pass, including one submitted quad per call. This establishes state
selection, not pixel blending arithmetic, device-mode interpretation, or the
loader/caller that produces frame+0b. The current effect loader always selects
SDL additive blending. Replacing that globally or by effect-name heuristics
would not be justified by this probe: the runtime flag's provenance and each
loaded frame's format need to be retained first. No rendering behavior changed.

### Authored 4444 blend flags preserved and applied

`probe_animation_frame_flags.py` executes complete native 537490 with only its
file read substituted. All 1,024 synthetic banks pass across every flag byte and
subframe counts 0/1/2/255: relocation reads byte 10 as a count and preserves byte
11 independently for both parent and child frames. The frame flag is authored
bank data, not a high-byte subframe count or disposable junk.

The decoder now reads those bytes separately and retains encoding/blendFlag in
`gaf::Frame`. This replaces the old high-count fallback, which explicitly called
FF00 “junk” in cannonball/hurricane/dust frames and discarded its meaning.
Flagged composites now retain their actual low-byte child count. Synthetic tests
cover single frames with flags 0/1/128/254/255 and a flagged one-child composite.

Effect textures with encoding 4 and flag 255 now use source-alpha/inverse-source-
alpha blending; other 4444 frames retain source-alpha additive blending, matching
the measured 4fac00 state selection. Format-5 renderer mode interpretation and
indexed-format state remain separate open work; this change does not guess them.

The shipped exact-sequence inventory still resolves 62/62 requests and identifies
222 alpha-blended versus 424 additive 4444 frames. Alpha cases include cannblg,
cannbmed, cannbsm, all three hurricane phases, blue/green/purple/white ring_fx
variants and tsunamiexplode. Sampled flame, mindspin and zhonbuild frames retain
flag 0 and additive blending. `/tmp/sprite-blend-storm.png` is an inspected live
storm end-phase capture, not a paired retail pixel comparison.

Validation: all targets rebuilt and all 44 CTests pass in Release, Debug and
optimized Debug. All 69 projectile scenario hashes agree across configurations;
two 600-tick network runs retain `9955322ef77531a1`. Native bank relocation and
sprite-state probes pass. Logs: `/tmp/sprite-blend-*.log`.

### Composite draw behavior and current effect inventory

The native sprite-state probe now includes 72 real recursive composite draws,
combining format-4/5 children, flags 0/1/255, initial special flag and caller
override. All pass. Each child submits independently in authored order. A child
with a nonzero flag enables the special-call flag for itself and later siblings
unless the override is set; format-4 children retain their independent blend
selection. Flattening these into one image cannot generally preserve that draw
behavior. Pixel arithmetic remains outside the renderer sinks.

`effect_inventory` now checks raw frame headers and reports composite counts and
mixed child format/flag combinations for the exact sequence selected from each
bank. The current 62/62 resolved requests contain zero composite frames, so this
representation gap does not affect that audited effect set. Explosion-definition
coverage and other animation banks remain outside this inventory. This evidence
prioritizes remaining format-5/indexed renderer modes and explosion coverage;
it does not establish universal composite parity. The optimized inventory target
rebuilt, native probe passed, and diff check passed. No runtime change this turn.

### Glide reference correction and explosion inventory (2026-09-22)

The user reaffirmed Glide as the reference. Existing retail-engine notes already
require this. The sprite probe stops at generic renderer state requests; it does
not establish Glide output pixels or backend blend translation. Treat the prior
alpha/additive labels as provisional until that translation is executed.

Located Glide state handler 5b7fe0: its dispatch maps states 19, 20 and 27 to
5b8231, 5b8247 and 5b8273 respectively. These cache values in the device and
invalidate its cached state; the eventual draw-time application still needs
tracing. No runtime blend change was made based on this partial evidence.

Expanded effect_inventory to include winning VFS explosion definitions:
52 variants, no differing bank/sequence names, 109/109 total unique requests
resolved. Format-4 flags divide into 441 flag-255 frames and 1024 other frames.
No composite frames occur in this inventory. This checks asset availability and
metadata only, not final Glide pixel parity. Optimized inventory target built
and executed successfully.

### Glide blend translation executed (2026-09-22)

Added `tools/re/probe_glide_blend.py`: executes the real state setter 5b7fe0
and deferred state application 5b7e10, with only the exported Glide API calls
replaced by recording sinks. All 512 cases pass (256 flags, textured/untextured
state). Format-4 requests from the existing sprite probe map to
`grAlphaBlendFunction(SRC_ALPHA, ONE, SRC_ALPHA, ONE)` except flag 255, which
uses ONE_MINUS_SRC_ALPHA for both destination factors. The function pointer
at device+f8 is resolved from `_grAlphaBlendFunction@16`; enum meanings checked
against the [Glide source header](https://sources.debian.org/src/glide/2002.04.10ds1-25/glide3x/h5/glide3/src/glide.h).
This confirms the current SDL effect RGB blend choice for format 4 at the
backend API boundary. Texture conversion, sampling, output-alpha behavior and
final framebuffer pixels remain separate checks; this is not full pixel parity.
The existing 1536 sprite and 72 composite cases also pass. No runtime change
was required by this finding; no simulation/pathfinding code changed.

### Glide indexed and 1555 modes; default effect correction (2026-09-22)

Located the Glide vtable at 5f67c0: mode setter +50 is 5b77b0, textured
quad +48 is 5b78f0, primitive submission +64 is 5b79a0. Expanded
probe_glide_blend.py executes mode selection and quad preparation with the real
state setter, sinking texture lookup and final primitive submission. 512 cases
confirm mode 4 disables blending and preserves vertex color; mode 5 applies
0x80ffffff vertex color and source-alpha/inverse-source-alpha blending.
Expanded probe_sprite_blend.py runs 1024 indexed draws through real 4fac00:
normal calls select mode 4, special calls mode 5, explicit-alpha calls without
the special flag select mode 6. Mode 6 pixel behavior remains to verify.

The winning effect inventory contains 1465 format-4 frames and 99 indexed
frames, no format-5 frames. Indexed sequences include the transport swirl,
smoke, legacy flames and projectile shadow art. Corrected effectFor's default:
only format 4 without flag FF uses additive blending; other frames use SDL
coverage blending to retain transparent pixels. This fixes the incorrect
additive default for indexed transport effects. Native special-call half-alpha
and explicit-alpha handling remain separate work; default coverage does not
claim equivalence for those modes or texture filtering. Release, Debug and
optimized Debug client rebuilds started; final build results pending.

All three client rebuilds completed successfully. The optimized retail_visual
and nimbus tests pass, as do both expanded native blend probes and diff checks.
No sim/net/AI code changed. A paired retail/port screenshot is still needed to
verify final appearance rather than just draw-state selection.

### Glide explicit-opacity mode verified (2026-09-22)

Expanded the native Glide quad probe to all 256 input alpha values in mode 6,
in addition to modes 4/5: 768 quad cases pass. Mode 6 preserves caller alpha,
forces RGB white, and uses source-alpha/inverse-source-alpha blending. Mode 5
instead forces alpha 128 regardless of caller input. This closes the backend
mode-6 uncertainty recorded above, but does not implement caller-specific
opacity in our effect draws. Existing transport/nimbus draw probes submit
special=0, override=0; the flame-particle probe submits special=1, override=0.
Thus transport uses the corrected default path, whereas a non-format-4 flame
fallback would require half opacity. Current winning flame/bluefire/dieselflame
frames are format 4 and ignore that special flag. Script smoke/flame attachment
callers still need their complete native lifecycle and draw arguments traced.
No engine change or build was needed for this additional probe coverage.

### Extended script emission dispatch executed (2026-09-22)

Added probe_script_sfx_dispatch.py: 9216 calls execute 50da20 with real smoke
wrappers; only visibility, model refresh and final creation are sinks. Tested
1024 random body/piece coordinate combinations across codes 257..265, including
32-bit overflow and invisible admission. Visible emissions refresh the model
once, capture X=bodyX+pieceX, Y=bodyY+pieceY, Z=bodyZ-pieceZ; invisible emissions
neither refresh nor create effects. Final creation receives the captured origin.

Codes 257/258/265 reach 502660 via distinct smoke-wrapper flags. Code 259 calls
502bd0 with a second endpoint at the captured X/Z and sea-level Y, parameters
8/7; it is not the smoke path. Codes 260/261/262 call damage-flame creation with
kinds 0/1/2. Codes 263/264 enqueue transient effects using game+174d4/+174d8.
Current sfxAnimFor incorrectly maps 259 to smoke and omits 265 and 263/264.
The continuously attached one-effect-per-kind presentation is also insufficient:
complete particle allocation/update/render lifecycle and emission-time capture
must replace it. No partial runtime remapping was made before those paths are
traced, since mapping 265 to generic smoke alone would still discard its flags.

### Native smoke particle lifecycle (2026-09-22)

Added probe_smoke_particle.py. Real 4f1cf0 constructor, 4f1bd0 update and 4f1c60
render execute with only RNG and final sprite draw substituted. 46400 updates
across 512 complete lifetimes pass, including coordinate overflow, frame
countdowns, expiry and bank selection. Each tick adds windX*8 and windZ*8 to
raw X/Z, and gravity*4 to raw Y. Initial countdown is the requested period;
when it reaches zero, reset to period/2 + rand15*(period/2)/32768 and increment
the frame; remove when frame equals the chosen limit. Draw uses whole-coordinate
projection, native viewport admission and special=1, override=0. For indexed
art this requests Glide mode 5 (half alpha); format-4 ignores that call flag.

Emission 4f1d40 limits each owner's smoke list (default capacity 10), defaults
period to 8 and chooses frame limit 2+rand15*(frameCount-3)/32768. These creation
rules are statically traced, not yet exercised by this lifecycle probe. Owner
list vtable 5f03a4 calls update 497300 and draw 4972c0; expiry unlinks nodes.

The bank initialization at 4bdb80..4bdbf6 identifies the three smoke flags:
257 uses bigsmoke:bigsmoke, 258 uses smoke:smoke01, 265 uses steam:steam. Added
bigsmoke and steam to the effect inventory. Runtime still needs these mappings,
per-emission state, capacity, tick integration and correct RNG ownership; the
existing persistent smoke renderer has not yet been replaced.

### Smoke creation verified and update port compared (2026-09-22)

probe_smoke_emission.py executes real 4f1d40 list admission, particle construction
and insertion, substituting only allocation, RNG and animation-count lookup.
1280 cases pass across remaining capacities, batch sizes, zero/default periods,
all smoke flags and RNG boundaries. It verifies a full list consumes no RNG or
frame lookup, inserted list order, exact captured origins and randomized frame
limits. Constructor/list-creation defaults remain outside this probe.

Added client/retailsmoke.h with display-only particle update logic. The native
smoke lifecycle probe now compares the compiled retail_visual_test --smoke-step
output for all 46400 updates, including random draw counts. Every coordinate,
countdown, frame, alive result and draw count matches. The optimized test target
built and retail_visual CTest passed; diff check passed.

This helper is not yet wired into GameView. Remaining integration must capture
emissions at the proper script/tick phase, preserve owner list capacity/order,
use world wind/gravity without touching authoritative gameplay RNG, and apply
native draw flags for the selected animation format. Current client binaries
still use the old persistent smoke presentation; this is not a claimed visual fix.

### Smoke integration event-source audit (2026-09-22)

Inspection before wiring the helper found a required timing change: World::
ScriptHost::effect in sim.cpp currently discards every effect opcode. The
separate client COB VM records only piece/code pairs in pendingSfx; tickAnims
advances that VM with realDt*animSpeed and later drains those pairs after all
piece motion has advanced. Consequently this source cannot prove emission-time
piece positions or tick-accurate ordering, even with a correct particle helper.

Integration should capture cosmetic EMIT_SFX events from the authoritative
script host at the instruction: tick, owner, code and transformed origin using
the existing retailPieceOrigin path. Publish them after every world tick, as
transport effects already do, without drawing or consuming cosmetic RNG in the
script host. The client can then maintain smoke lists and update each tick with
published wind, while suppressing duplicate smoke from its display VM. Need to
verify visibility admission and owner-removal behavior too. This is outstanding
implementation, not a completion claim; no engine behavior changed this turn.

### Authoritative script emission capture implemented (2026-09-22)

World::ScriptHost now captures extended EMIT_SFX at the instruction using the
current RetailScriptState piece pose and authoritative body position/attitude.
ScriptEmission retains tick, owner/player, piece, code and fixed-point world
origin. It consumes no RNG and is excluded from stateHash; events clear on the
next tick and replay reset. Nonextended effects still require two vertex
endpoints and are intentionally not represented as a one-point event.

Added a simulation fixture emitting twice with a piece movement between the
instructions, checking distinct captured origins and hash isolation. All-target
Release, Debug and optimized Debug builds started; validation is pending.
The client has not yet consumed these events or replaced its smoke renderer.

### Script emission capture validation (2026-09-22)

All-target Release, Debug and optimized Debug builds completed successfully.
The new instruction-time emission fixture passed in the optimized binary,
including distinct origins across an intervening piece movement and checksum
isolation. Release and optimized Debug each pass all 44 CTests. The math
cross-compiler check retains golden dcef618cd2e4d558; ARM checks remain skipped
because target libc/headers are unavailable. First 600-tick network run retains
9955322ef77531a1. Debug suite and second network run were still running at this
entry; their terminal results must be checked before reporting complete validation.

### Smoke client integration (2026-09-22)

The pending Debug suite completed: all 44 tests pass (137 seconds). Both network
runs retain 9955322ef77531a1. Thus the simulation event capture passed all three
build/test configurations before client integration started.

Client now publishes a smoke packet after each world tick with that tick's wind
and admitted script emissions; packets survive skipped render snapshots. The
render thread maintains ordered per-owner particles, advances the verified
helper once per queued tick, applies the ten-particle capacity, chooses the
correct large/small/steam banks and randomized frame limit, and draws captured
world positions. Indexed smoke gets half alpha; format-4 retains its authored
blend. Duplicate 257/258/265 emissions from the separate display VM are suppressed.
Cosmetic random draws use a private CRT stream, never gameplay RNG.

Remaining fidelity limitations: admission currently uses post-tick owner
visibility; owner removal reads the published unit snapshot; smoke updates occur
before the packet's new emissions, but the full native per-owner scheduling
phase has not been verified. Initial startup emissions before the first normal
tick and exact native cosmetic RNG interleaving need checking. Damage flames,
beam code 259, and transient codes 263/264 are still separate outstanding work.
This is a runtime smoke improvement, not proof of full smoke/effects parity.
Three client rebuilds are in progress; rendered smoke validation remains pending.

All three client rebuilds completed successfully. Optimized retail_script and
retail_visual checks pass after integration, and diff check passes. A dummy-SDL
Drake startup run is still in progress; it is only a startup check, not a smoke
pixel-parity comparison.

### Smoke owner-removal scheduling correction (2026-09-22)

The first integration read owner life from the newest render snapshot while
replaying older tick packets. That made skipped frames delete older particles
prematurely and changed cosmetic random consumption. Replaced that dependency
with simulation-thread owner tracking: each packet records removals for that
tick, and the renderer applies them while consuming the same tick. This removes
the latest-snapshot dependency; exact native death-callback phase remains open.

The earlier startup command did not exit because --time advances the scene but
is not a non-headless lifetime limit. Stopped that specific test process and
reran with --shot. The corrected run exited successfully and its image was
inspected: valid Drake game scene, no smoke visible. It proves startup/render
execution, not smoke pixels. Three client builds for owner scheduling are running.

### Smoke-specific rendered fixture (2026-09-22)

All three owner-scheduling client builds succeeded. Inspected the shipped
Aramon Keep SmokeControl script: below 66 percent health it emits 257/258 from
one of eight authored damage pieces. Added debug TAK_SMOKE_TEST fixture to
spawn a one-third-health Keep and center the camera. Its screenshot gate waits
for at least two actual draws from the new smoke sprite list. This exercises
the real script/event/client pipeline rather than injecting renderer particles.
Fixture builds and execution are pending; it is not yet rendered evidence.

All three fixture builds succeeded. The damaged-Keep capture exited successfully
through the gate requiring at least two new smoke draws; inspected
/tmp/retail-smoke-client.png shows the Keep with small gray smoke patches over
its roof/entrance region. This establishes actual script-to-render execution,
not pixel equivalence to a paired retail Glide capture. Native smoke creation
(1280 cases) and compiled/native update comparison (46400 steps) still pass.

### Damage-flame lifecycle observed (2026-09-22)

Added probe_damage_flame.py: real 4f2660 constructor, 4f25d0 update, 4f2600 draw,
537390/5373d0 authored clock and viewport helper execute with only final sprite
submission replaced. 1024 lifetimes / 14588 draws pass with varied authored
durations (including zero), loop flags and signed world coordinates. Position
never moves after emission. Updates decrement a 15-tick lifetime before advancing
the animation; tick 15 removes the particle without advancing the clock. Earlier
animation completion also removes it. Draw submits special=1, override=0.

Static creation trace: per-owner list default capacity 40 (502da0), admission
4f26a0 chooses an animation from the requested size class with one CRT draw.
The loader enumerates gamedata/damageflames/*.tdf. Winning shipped definition
is data.hpi!gamedata/damageflames/damageflames.tdf: smallflame, mediumflame,
largeflame each reference the matching flames GAF sequence. Runtime still uses
its old persistent damage-flame approximation; integration must preserve class
selection, capacity, authored loop metadata, tick phase and draw flags.

### Damage-flame creation and loop metadata (2026-09-22)

Added probe_damage_flame_emission.py: 240 native batches verify capacity 40,
variant choice at CRT endpoints, one RNG draw per accepted emission (none when
full), linked-list order, captured positions, initial authored frame duration
and lifetime 15. Allocation and RNG alone are substituted; construction and
animation initialization execute. Expanded lifecycle probe to all 256 loop
bytes: native single-tick clock treats any nonzero byte as looping.

GAF Sequence now preserves header byte 2 as loopFlag instead of discarding it.
Extended decoding fixture checks independent sequence/frame metadata values
including 128, 254 and 255. All-target builds are running in all three directories.
The damage-flame renderer is not yet wired; this metadata is needed for its
native authored clock. No simulation or pathfinding behavior changed here.

### Damage-flame runtime integration (2026-09-22)

Metadata builds completed successfully in all three configurations. Client effect
animations now retain authored durations and loop byte. Damage-flame classes
load from VFS gamedata/damageflames TDF definitions, preserving ordered variants
per class. Tick packets include codes 260..262; renderer keeps per-owner lists
with capacity 40, random variant selection, instruction-time fixed positions,
authored RetailEffectClock and the native 15-tick lifetime cap. Existing owner
removal packets also clear damage flames. Indexed flames use half-alpha drawing;
format-4 flames keep their native blend choice. Display-VM duplicates are suppressed.

The damaged-Keep fixture now uses one-fifth health; optional
TAK_DAMAGE_FLAME_TEST requires an actual new damage-flame draw before screenshot
capture. Rebuilds and the fixture capture are pending. Remaining integration
limits include exact native owner scheduling/visibility phase and cosmetic RNG
interleaving, as already recorded for smoke. No pathfinding code changed.

Native damage-flame tests and the shared C++ clock comparison passed (1280
64-update timelines). Initial integration builds found a string_view-to-string
conversion error in the new class loader; fixed the explicit conversion and
restarted the now-terminal builds. Replacement build sessions are running;
no rendered integration result is claimed yet.

### Damage-flame render validation and indexed palette correction (2026-09-22)

All integration builds succeeded. retail_script, retail_visual and nimbus pass
in all three configurations. Damaged-Keep capture with both TAK_SMOKE_TEST and
TAK_DAMAGE_FLAME_TEST gates completed and was inspected: actual new smoke and
flame draws are present. This is end-to-end execution evidence, not retail pixel
parity.

Tracing effect-bank palette input exposed a separate mismatch. Native 4bd980
loads fx.pcx through 4bc9b0 and stores it at game+17410 (4bd9b7); the damage-flame
loader passes that palette to 4bcf70 at 4f296d. Indexed small smoke uses the same
palette at 4bdbb9. Our effectFor instead used aramon_features.pcx. Corrected it
to load palettes/fx.pcx directly; truecolor TAF decoding remains independent of
that palette. VFS confirms fx.pcx is supplied by data.hpi. Palette-correction
client rebuilds are running; updated rendered capture remains pending.

All three palette-correction client builds succeeded. The same gated Keep
fixture completed and /tmp/damage-flame-fx-palette.png was inspected after the
correction. Final comparison with retail Glide is still outstanding.

### Previously omitted script transients connected (2026-09-22)

Native initialization 4bdf28..4bdf9a resolves game+174d4 to
deathmagic:purpledeath and game+174d8 to pillaroflight:pillaroflight, then forces
both animation loop bytes to zero. These are the already-probed EMIT_SFX codes
263 and 264, which call the same 421e10 transient queue used by transport art.

Client tick packets now retain these events and spawn authored one-shot effects
at the captured instruction-time position/tick, using the existing transient
visibility and clock path. They do not join per-owner particle lists and are
not removed with that owner. Post-tick visibility admission is still a known
integration limitation. Expanded the effect inventory with both exact names.
Client builds and expanded inventory run are pending. Native emission dispatch
still passes all 9216 cases after this integration.

All three client builds succeeded. Expanded inventory resolves 113/113 requests;
optimized retail_script, retail_visual and nimbus tests pass. Native transient
clock probe passes 256 timelines. No dedicated live capture of codes 263/264
has yet been made, so that rendered end-to-end verification remains open.

### Script code 259 is a native no-op (2026-09-22)

Following 502bd0 beyond the earlier dispatch sink shows it immediately returns
with four arguments popped. Thus the constructed sea-level endpoint does not
produce a beam or smoke in this retail binary. Earlier notes calling code 259
an outstanding beam effect overstated the dispatch evidence.

Removed the 502bd0 sink from probe_script_sfx_dispatch.py so all 9216 cases now
execute that native handler; 259 creates no effect and the probe passes.
Removed the client's erroneous code-259 smoke mapping. Other nonextended beam
codes still have actual creation paths and remain outstanding. Three client
rebuilds are running; simulation/pathfinding code was not changed.

### Script beam factory routing (2026-09-22)

`tools/re/probe_script_beam_creation.py` executes the native `502a70`
wrapper and `502580` factory, substituting only allocation, geometry
initialization and manager insertion. All 1,024 randomized endpoint cases
pass, including allocation failure. Successful calls allocate 68 bytes,
initialize the separate beam class with both endpoints, mode 1 and argument
6 or 7, then insert it into the effects manager with priority argument 7.
Static inspection of `504420` shows the fourth initializer argument feeds
lifetime setup and reciprocal motion scaling: earlier notes treating 6/7 as
colors are not supported. Geometry initialization, particle updates and the
Glide draw path remain unverified by this new probe. The current flame
stand-ins for nonextended script effects 0/1 still need replacement after
that verification.

The latest code-259 correction rebuilt `takclient` successfully in `build`,
`build-dbg` and `build-o2` (all three build logs end in successful linking).
The 9,216-case extended script dispatch probe was rerun and passed.

### Script beam particle motion and expiry (2026-09-22)

`tools/re/probe_script_beam_particle.py` runs native `5042a0` and `504400`
without substitutions. All 32,768 updates pass: each coordinate adds its
velocity with 32-bit wrap; a modulo-period counter advances a modulo-frame
counter; expiry is strictly signed `currentTick > deadline` (the particle
survives the deadline tick). Randomized initial positions, velocities, frame
counts, periods and deadlines exercise these rules independently of drawing.
This verifies the behavioral model in the probe, not a C++ implementation.

Static tracing also narrows the remaining work: `504520` creates sprite
particles using animation bank `game+174b4`, and `503420` draws them through
`5042e0` / `536e90`. Thus “beam” here names the native emitter path; it does
not establish that the result is a solid geometric line. Resolve this bank
and follow its Glide sprite submission before choosing replacement art.

### Script beam sprite projection and visibility (2026-09-22)

`tools/re/probe_script_beam_draw.py` executes `5042e0`, substituting only
animation-frame lookup and final sprite submission. All 2,048 randomized
cases pass. The screen point is whole-coordinate X and Z minus half Y,
then camera offsets. Admission uses the height-projected 32-pixel cell,
rejects out-of-bounds cells, and selects either the local player's sight
byte or explored-player bit according to the native configuration branch.
The selected animation frame and projected point reach `536e90`.
This is a different final sprite entry point from the already-tested
`4fac00` path; Glide blending equivalence is still unproven here.

The examined initialization interval `4bdb00..4bdf30` did not identify the
source of `game+174b4`. Do not infer a flame animation, missing asset, or
unused effect from this negative result; initialization remains to trace.

### Script sprite backend correction (2026-09-22)

`tools/re/probe_script_sprite_backend.py` executes `536e90` with real
surface accessors and clipping, substituting only its final CPU blitters.
All 768 cases pass: encoding zero reaches `549f4c`, every nonzero encoding
reaches `54a077`, and fully clipped sprites produce no blit. This routine
does not submit a Glide primitive. Consequently the previous proposed
“Glide blending” check for this specific entry point was based on an
unproven backend assumption. Its software behavior must not be presented
as Glide parity. Establish whether this legacy emitter path is reached
in actual Glide gameplay, and how its destination surface is presented,
before implementing a replacement based on it. The unresolved animation
bank initialization is also still a gate; no runtime effect was changed
on the strength of this probe.

### Shipped script effect call sites (2026-09-22)

`tools/re/inventory_script_sfx.py` decodes instruction boundaries using the
project COB opcode metadata and scans every archived COB version in the
retail root. The current install contains 268 scripts and 2,386 EMIT_SFX
sites. Of these, 1,152 directly follow PUSH_CONST: code 2 occurs 938 times,
3 occurs 149 times, 4 occurs 40 times and 5 occurs 25 times. No direct
literal site requests 0 or 1. This does not prove those codes unreachable:
1,234 sites compute or fetch their arguments, and the inventory includes
superseded archive versions rather than only winning VFS files.

This changes investigation priority: follow the heavily used 2..5 owner
emitter (`502aa0`) before investing further in the unproven 0/1 legacy
sprite path. It does not justify deleting support for computed 0/1 calls.

### Common script effects 2–5: point particles (2026-09-22)

Tracing `502aa0` reaches owner+178's emitter and `4f19c0`, unlike the
legacy 0/1 sprite path. The emitter has capacity 100. Its draw method
`502f40` projects accepted particles with `4f1910`, builds colored vertices,
and submits primitive type 1 through the renderer's +64 method. Follow
that method in Glide to establish final point rasterization.

`tools/re/probe_script_point_particle.py` executes native construction
`4f1960` and update `4f1890`, substituting terrain sampling only. All
25,089 tested updates pass. Position moves before terrain sampling; a
terrain sample at or above sea level kills the particle before advancing
its color clock. Surviving particles use six native color stages, each
lasting the supplied 8 or 16 ticks. The probe reads palette values from
the user's binary rather than embedding binary data. These are behavior
observations, not yet a C++ implementation or full Glide rendering proof.
Emission direction, randomized origin, model vertex capture and native
terrain-sampler parity remain required for integration.

### Glide point preparation (2026-09-22)

`tools/re/probe_glide_points.py` executes the actual Glide primitive method
`5b79a0`, including recursive batching, and sinks only its draw-array API.
Across 17,664 points, packed colors are unchanged, X/Y gain half a pixel,
and submissions split into batches of at most 64 vertices. Primitive type
1 becomes Glide draw-array kind 0. This establishes the Glide route for
the point-particle emitter, unlike the older script-sprite path. State was
already applied in the fixture; blend state, actual pixel coverage and
point size are not proven by this probe. No texture or flame animation is
involved in these point submissions.

### Point-particle emission rules (2026-09-22)

`tools/re/probe_script_point_emission.py` executes `4f19c0`, its constructor
and list insertion with only allocation/RNG substituted. 576 cases verify
capacity 100, no RNG draws when full, three RNG draws per accepted particle,
independent whole-coordinate jitter `rand*7/32768-3`, half-unit speed for
axis-aligned vectors, supplied 8/16 color cadence, and insertion order.
Four diagonal cases additionally match integer-length and quantized
reciprocal normalization: floor(sqrt(sum(delta²))), reciprocal
floor(2^32/(2*length)), then signed product shifted by 16. These diagonal
checks are samples, not an exhaustive proof for all vector magnitudes;
zero-length and overflowing vectors remain to characterize. The probe
is behavioral evidence for the pending point-emitter implementation.

### Compiled point-particle state implementation (2026-09-22)

Added `src/client/retailpointparticle.h` with display-only wrapped motion,
terrain-before-clock termination, six stages and supplied color cadence.
`retail_visual_test --point-step` exposes the compiled behavior to
`probe_script_point_particle.py`. All 25,260 compiled updates match native
positions, stage, countdown and survival, including 171 terrain-termination
cases. The optimized debug `retail_visual` CTest passes and the test target
rebuilt successfully in all three build directories. This helper is not
yet wired into live rendering; emission capture, terrain sampling and point
drawing integration remain outstanding. No simulation state changed.

### Point-emission model endpoints (2026-09-22)

`tools/re/probe_script_point_dispatch.py` executes `50da20` with model refresh
replacing deliberately stale vertex data. All 4,096 cases pass: codes 2/3
use the first transformed vertex as origin and the second as destination;
4/5 reverse them. Even codes select cadence 16, odd codes cadence 8.
Body translation adds X/Y and subtracts model Z with 32-bit wrap. Invisible
owners cause neither model refresh nor emission. Eight piece indices and
randomized body/vertex coordinates exercise addressing and overflow.

Integration cannot reuse `retailPieceOrigin` alone: the current production
model stores hierarchy offsets but not the two authored vertices, and
point emission includes the selected piece's own rotation. Add and verify
vertex transformation before capturing authoritative emission endpoints.

### Rendered endpoint transforms differ from piece-origin queries (2026-09-22)

The native source of the endpoint array is the floating-point renderer
transform `4eea20`, not the rounded integer piece-origin query. Extended
`check_model_transform.py` to exercise two vertices per native piece,
matching the point-emitter input shape. All 8,192 vertices across 4,096
randomized two-vertex pieces meet the existing visual tolerance against
`model_transform_test`; maximum coordinate error is 0.00014499 world units.
This is not bit-exact 16.16 equivalence and covers one piece, not arbitrary
hierarchies. Capture should use the rendered-pose transform semantics,
rather than applying the integer origin helper to authored vertices.

### Recursive rendered model hierarchy (2026-09-22)

Added `tools/re/check_model_hierarchy.py`, executing native body setup and
recursive `4eea20` with no substitutions. The existing client transform
matches 5,115 vertices across 1,024 generated hierarchies, checking every
depth from one through eight, within 0.00005337 world units maximum error.
The fixtures vary body orientation, each piece's rotation/movement, model
offsets and authored vertex. This supports reuse of the client hierarchy
transform for point-emission endpoints; it is visual-tolerance evidence,
not bit-identical fixed-point equivalence. Live instruction-time pose
capture and endpoint emission remain to implement.

### Reusable emission-pose transform (2026-09-22)

Extracted the verified transform from the test harness into
`modelEmissionPoint` in `src/client/modelmath.h`, accepting a compact
root-to-leaf pose chain, authored vertex and body orientation. The test
harness now calls this production helper. Both native comparisons still
pass: 5,115 hierarchical vertices and 8,192 two-vertex-piece outputs, with
unchanged maximum errors. This establishes the display-side consumer for
instruction-time poses without introducing floating-point operations into
gameplay simulation. Live event capture/rendering remains pending.
Client rebuilds were launched in all three build directories; completion
must be checked before reporting them as rebuilt.

### Instruction-time point pose capture implemented (2026-09-22)

Added shared raw `cob::EmissionPose` data and retained the first two authored
vertices in each production-model piece. `ScriptHost::effect` now captures
codes 2–5 with body position/orientation, both vertices and an immutable
root-to-leaf pose chain at the instruction. Pieces with fewer than two
vertices do not emit. All transform math remains client-side; capture uses
no RNG and the event vector remains outside stateHash. Added script fixture
checks for two emissions separated by immediate piece movement, independent
body/vertex capture and hash isolation. These new checks are not yet run:
all-target builds in build-o2, build and build-dbg are still in progress.
Live particle queue consumption/drawing remains outstanding.

### Degenerate point-emission endpoints (2026-09-22)

Extended the native emission probe with coincident endpoints. Retail still
allocates one particle, consumes three origin-jitter draws and produces
zero velocity on all axes. A client implementation must retain stationary
particles for this case rather than dropping them or dividing by zero.
The three all-target pose-capture builds were re-polled and are still live;
no completion or test success is inferred from intermediate object builds.

### Pose-capture rebuild and regression validation (2026-09-22)

All-target builds completed successfully in build, build-o2 and build-dbg.
The new instruction-time pose-capture regression passes in all three.
All 44 CTests pass in Release and optimized debug; the full Debug suite is
still running. Cross-compiler determinism retains golden
`dcef618cd2e4d558`; ARM builds remain skipped for unavailable target headers.
The two-run local referee/client harness has been started and remains live;
its result must be checked before claiming network validation. These
results validate capture plumbing, not completed point rendering.

### Capture validation complete; compiled emission helper (2026-09-22)

The full Debug suite finished: all 44 tests pass in all three configurations.
Both local referee/client runs retained `9955322ef77531a1`, matching the
previous baseline. Added `RetailPointParticle::emit` and the compiled
`--point-emit` comparison. 1,024 arbitrary-direction and coincident-endpoint
cases match native positions and velocities exactly within the sampled
coordinate ranges. The optimized test target rebuilt; Release and Debug
test-target rebuilds are pending verification. The new helper is not yet
connected to the live client queue/draw path. Large overflowing vectors
remain outside this sampled emission comparison.

### Live point-particle queue consumption (2026-09-22)

The client now admits captured effects 2–5 to its tick queue, transforms
both instruction-time vertices, reverses endpoints for 4/5, and creates
particles with the verified jitter/velocity helper. Per-owner capacity is
100. Tick updates use the existing retail terrain sampler; owner removal
and replay rollback clear particles along with the other owner effects.
The private cosmetic RNG is shared with the existing smoke/flame effects.
This queue integration is being rebuilt in all three client configurations.
Drawing is still missing, so this is not yet a visible completed effect.
Native color stages and final drawing must be reproduced through behavioral
verification; do not embed an extracted binary table as an engine asset.

### Owner emitter through Glide (2026-09-22)

All three point-queue client rebuilds completed successfully. Added
`probe_script_point_glide.py`, running native `502f40` through real Glide
`5b79a0`, with native projection/viewport clipping and final API capture.
Lists of 1, 64, 65 and 100 particles preserve admitted order and packed
colors, including draws spanning multiple batches. State application is
still substituted, so this does not establish blend/pixel equivalence.
The shipped fx.pcx palette has no exact matches for the examined native
point-color stages; substituting nearby palette entries would be a visual
approximation and was not implemented. Drawing integration remains open.

### Endpoint conversion overflow correction (2026-09-22)

Review found that converting a transformed endpoint directly to int32_t
could invoke undefined behavior outside that range. Native `4eebab..4eebc8`
rounds to signed 64-bit with x87 and keeps the low 32 bits. Added
`modelEmissionFixed` with that wrap behavior and zero low word for invalid
64-bit conversions. The live queue uses it; transform tests now cover
32-bit wrap, half-even ties and nonfinite input. Client and transform-test
rebuilds are running in all three configurations; new assertions still
require verification. Final particle drawing remains unimplemented.

### Point drawing connected (2026-09-22)

The client now draws live point particles with height-projected viewport
clipping and one-pixel SDL point primitives. Color behavior is expressed
as six RGBA stages observed by executing native construction for each stage;
no binary table or extracted asset is loaded/copied into the engine. This
supersedes the earlier assumption that palette-asset matching was required
to reproduce these functional color outputs. Native-to-Glide probes already
show that the packed output colors survive point submission unchanged.

All three endpoint-rounding rebuilds and model-transform tests passed.
New point-drawing client/test rebuilds are running in all three directories.
Live screenshot validation and paired Glide pixel coverage remain pending;
SDL's point rasterization must not yet be claimed pixel-identical to Glide.

### Live point screenshot fixture (2026-09-22)

All three point-draw builds completed. Added debug-only `TAK_POINT_TEST`:
it finds a deep-water stretch, spawns a Veruna transport, orders it along
the water and frames the camera. Screenshot readiness requires at least
three point particles actually drawn, preventing a blank screenshot from
passing as evidence. The fixture initially used a nonexistent World::move;
corrected to World::order after the compiler caught it. All three client
fixture builds are now running. The fixture has not yet been executed.

### First live point fixture result (2026-09-22)

All three fixture builds succeeded. Ulasem Arena correctly rejected the
fixture because it lacks the required deep-water stretch. Crab Cove found
water and ran, but the 50-second bounded run timed out without reaching
three drawn points; no screenshot was produced. This is an unresolved
integration failure, not a visual pass. The Veruna transport WakeControl
emits code 3 only while static 0 is set by a positive MoveRate callback;
trace movement, callback delivery, captured events and viewport admission
to isolate the failing stage before changing rendering assumptions.

### Live wake failure: movement callback split (2026-09-21 investigation continued)

Source tracing identifies a concrete gap in the new capture integration:
`GameView::tickAnimations` delivers MoveRate and setSFXoccupy to the display
VM for all mover classes; `World::notifyFlightOccupancy` delivers MoveRate
only for units with a flying retail-build job. There is no general ground/
sea MoveRate delivery in the simulation VM. Veruna transport WakeControl
requires the static set by MoveRate, so simulation-only capture cannot
replace those display-VM emissions as currently implemented. This explains
a missing prerequisite for the failed live fixture; the test itself did
not log movement and should still verify it on rerun.

Do not declare the capture path complete. Resolve the callback ownership
split, including emission-time display pose and duplicate suppression,
before claiming ground/sea point effects work. Adding gameplay-VM callbacks
can change hashed script state/RNG and requires explicit determinism and
pathfinding regression validation; a display-side fix must preserve the
actual emission-time pose rather than using the latest end-of-frame pose.

### Ground/sea display-VM point capture implemented (2026-09-22)

Ground/sea effects 2–5 now capture their raw pose chain inside the display
VM's emit callback, before later instructions can alter the piece. The
main-thread drain supplies the stable frame body pose and calls the shared
point emitter. Simulation point events for nonflyers are filtered to avoid
duplicates; flyers retain simulation capture. This fixes the identified
movement-callback ownership gap without changing gameplay script callbacks.
Client builds are running, but heading conversion was corrected from port
to retail after launch; rebuild again if needed to ensure that correction
is included. Live fixture rerun is pending. Display emission cadence remains
frame-driven, and display-owner death cleanup still needs review.

### Live wake capture passes after display-VM fix (2026-09-22)

All three final client builds succeeded, including port-to-retail heading
conversion and dead/missing-owner point cleanup. Re-ran the Crab Cove
TAK_POINT_TEST fixture with the same gated screenshot command; it exited
successfully and produced `/tmp/point-water-live.png`. Inspected the image:
the moving Veruna transport has visible cyan point particles beside and
behind it. Screenshot readiness required at least three actual point draws.
This verifies the live path that previously timed out, not paired retail
pixel parity. Native comparisons still pass for 25,260 compiled updates
and 1,024 compiled emissions. Display-VM timing remains frame-driven;
full native phase/visibility parity and paired Glide captures remain open.

### Retained passenger retry sequence (2026-09-22)

Extended `probe_transport_passenger.py` to retain native mission memory
through approach, blocked-event detachment and all retry waits for both
air and sea carriers. Five successive 30-tick deadlines are preserved,
the sixth retry returns failure, and completed attachment still wins even
with the exhausted mission retained. The existing 4,096 compiled scheduler
comparisons pass as well. This sequence manually installs returned stages
and supplies navigator events; it does not close the full native navigator/
dispatcher or coastal mission-timeline gates.

### Blocked unload followed by a new destination (2026-09-22)

Extended the existing World exact-site fixture for both air and surface
carriers: after a permanently obstructed unload fails with cargo retained,
a fresh clear-site order must complete and preserve its fractional exact
coordinates. The complete transport test passes in all three rebuilt test
configurations. Native unload probes also pass: 100 air and 62 sea cases,
plus range and PARK-spacing comparisons. The new World recovery assertion
is integration evidence; it is not a paired native navigator timeline and
does not close the broad coastal/failure/resumption parity gate.

### Retail process prepared for broader captures (2026-09-22)

Confirmed no retail process or reusable transport save/capture was present.
Started the user-owned Kingdoms.exe through installed Proton 10.0 in a new
isolated `/tmp/tak-retail-parity` prefix. After creating the required prefix
root, launch remained live and Kingdoms.exe processes were observed. This
is only process-start evidence: game/menu readiness and active Glide backend
have not yet been confirmed. The bounded process handle is still live; do
not relaunch it without revalidating that handle/process state. Next work
is inspecting the window/backend and preparing a retail transport scenario.

### Retail title screen confirmed through Wayland (2026-09-22)

Revalidated the existing Proton process (PID 2429535), without relaunching.
X11 root capture via ffmpeg was black; a compositor capture with Spectacle
produced `/tmp/retail-wayland-screen.png`, inspected and showing the retail
v4.0BB title screen. Therefore the earlier black capture is not evidence
of a game startup failure. No Glide module was present in the process maps
at this title-screen check; in-game renderer selection remains unverified.
GDB inspection detached cleanly and the original retail process remains live.

Re-ran `probe_script_point_glide.py`: projection, clipping, packed colors
and multi-batch ordering pass. Re-ran `probe_transport_passenger.py` with
`build-o2/transport_test`: 49 native cases, 4,096 native/port scheduler
comparisons and retained air/sea retry sequences pass. These routine checks
do not replace the outstanding in-game transport/animation captures.

### Live retail input/renderer setup remains unresolved (2026-09-22)

The existing process remains live; do not restart it based on capture timeouts.
XTest clicks at both screenshot and native X11 coordinates did not produce
a verified menu transition. The X11 desktop is 7680x2160, so reduced preview
coordinates must not be passed directly to input. A temporary Win32 helper
run using Proton `runinprefix` enumerated a visible Kingdoms window reporting
a zero-sized client rectangle. The helper and source are under `/tmp` only.
Proton `run` was not a reliable way to run a second helper in the active
prefix; `runinprefix` did return helper output. ChooseRenderer.exe under
`runinprefix` exited with status zero but no renderer dialog or persisted
RendererType/RendererName registry value was verified. A subsequent compositor
capture showed the desktop instead of retail; retail PID 2429535 remained
live. No evidence yet proves Glide initialization or successful game input.
This is a live-capture setup issue, not evidence of a transport/animation
implementation failure. No engine code or gameplay state was changed here.

### EXPLODE dispatch verified; current debris remains a stand-in (2026-09-22)

Added and passed `tools/re/probe_script_explode.py`: 4,096 native calls to
50dd20, sinking only bounded random draws, piece-position lookup and downstream
creation. Non-BITMAPONLY dispatch creates one piece-debris descriptor, with
three random draws bounded 40/10/40, horizontal velocities (20-draw)*4096
and vertical draw*16384. Descriptor field +32 is 900; its downstream unit
and lifetime interpretation are not established by this dispatch probe.
BITMAPONLY suppresses the descriptor and all three draws. Low flag mappings
are checked; uninitialized higher descriptor bits are deliberately excluded.

Bits 0x100 through 0x10000 independently request nine effect classes, in
ascending order, at the looked-up piece position. Class 4 alone is gated by
settings+0x18 -> +0x11. Sound indices are 0/1/2/2 for the first four classes,
then -1. A zero gate does not suppress other requested classes.

Current `GameView::explodePiece` still draws three generic particles, uses
end-of-frame piece pose, treats smoke/fire flags as immediate bursts, and
collapses only bits 0x100..0x2000 to one generic explosion. These are confirmed
remaining mismatches, not completed parity. Next dependencies are native
492910 debris construction/update/render and 492fd0 class resolution before
replacing those approximations. No runtime behavior changed in this probe.

### Explosion variant selection corrected (2026-09-22)

Native 492fd0 selects floor(CRTdraw * variantCount / 32768), including a
draw for single-variant classes. Added `probe_explosion_variant.py`; all
4,096 cases pass across nine synthetic classes and counts 1..127, including
random-range boundary values. Class loading itself is outside this probe.
Changed GameView::spawnEffect from salt-counter modulo cycling to that
selection formula using the existing private cosmetic CRT stream. This
preserves simulation state/RNG and fixes the verified selection mismatch;
it does not reproduce global retail RNG interleaving or resolve the remaining
EXPLODE class dispatch/debris geometry gaps.

All three takclient builds completed successfully (build, build-o2, build-dbg),
logs `/tmp/explosion-variant-{release,o2,debug}.log`. Targeted retail_visual
and model_transform CTests pass. No live takclient was present during relink.

### Native single-piece debris construction verified (2026-09-22)

Added `probe_debris_creation.py`, executing 492910 with only free-slot lookup
and allocation replaced. All 1,024 synthetic single-piece cases pass. Retail
allocates a detached geometry instance, retains the model and body position,
copies current piece translations and rotations, removes authored offsets
from the copied translation, preserves copied visibility and hides the source
piece. The copied piece and debris descriptor receive the bounds midpoint
as rotation center, truncating signed division toward zero.

Coverage uses equal authored/transformed vertex arrays to isolate construction,
and varies offsets, script moves, rotations, vertices and body coordinates.
It excludes the 0x40 child-subtree branch, allocation failures, optional
script-driven blood effects and debris update/render. Those remain necessary
before replacing generic particles with detached animated geometry. No engine
code changed in this step; previously completed builds remain current.

### Debris child hierarchy and failure visibility verified (2026-09-22)

Extended `probe_debris_creation.py` through the native 0x40 branch, including
492770 counting, 4927c0 hierarchy copy and 4928e0 source hiding. With the flag,
child siblings detach along with the root; without it only the root detaches.
The root's own sibling is excluded in both cases. Copied pieces retain their
visible flags; the corresponding original pieces are hidden. Native pointers
to children, siblings and predecessor/parent links are checked.

Also tested no available debris slot and failed allocation. Both return without
hiding any source piece. All new cases and the prior 1,024 single-piece cases
pass. Current Vm host hides the root before invoking onExplode, so visibility
ownership must move together with the eventual debris allocation/capture
integration; changing visibility alone now would leave the generic renderer
inconsistent. No runtime edits made here. The native update/draw entry region
is 492410..492770 (draw eventually calls 4ee700); motion remains next to trace.

### Native debris update verified (2026-09-22)

Added `probe_debris_update.py`: 4,096 native 492420 calls pass, sinking only
terrain sampling, removal and impact-effect dispatch. Lifetime decrements first;
zero after decrement removes the object before all other checks. Initial 900 is
a count of updates, not milliseconds. Zero initially wraps instead of expiring.
Water checks current integer Y; terrain checks current integer Y plus arithmetic
shifted vertical velocity, at the pre-move X/Z. Removal precedes movement.
Survivors add wrapped fixed velocities and 16-bit spins (descriptor +0xc,+0x10,
+0x8 map to piece X/Y/Z). Gravity subtracts only with descriptor flag 8.
Impact flag 16 requests class 0 on terrain or 9/10 on water depending on the
weather fields; another weather field suppresses water effects but not removal.

Updated the misleading runtime comment that claimed the existing 0.9-second
generic particle lifetime was retail behavior. Runtime remains deliberately
unchanged until detached geometry/pose, collision, flags and lifetime can be
integrated together. Optional attached emitter updates and final Glide rendering
are outside this probe. This step requires no new binary for behavior changes.

### Compiled debris motion component matches native updates (2026-09-22)

Added display-only `src/client/retaildebris.h` with RetailDebrisMotion. It
implements expiry-first ordering, water/terrain collision, impact class/sound
results, wrapped fixed movement, mapped 16-bit spin and conditional gravity.
No gameplay integration yet: this is the motion component for the forthcoming
detached-geometry path, not a replacement claim for current generic particles.

Added retail_visual_test --debris-step and extended the native update probe
to compare 4,096 native outputs directly against compiled C++ state, impact
outputs and terrain-query counts. Comparisons pass in build, build-o2 and
build-dbg. Rebuilt retail_visual_test and takclient in all three configurations;
all completed successfully. Targeted retail_visual CTest passes. Remaining
integration includes emission-time geometry capture, allocation/visibility
ownership, hierarchy rendering, attached effects, and final Glide comparison.

### EXPLODE origin uses instruction-time piece pose (2026-09-22)

The display callback now snapshots raw retail VM piece translations/rotations
inside EXPLODE, converting with the same factors as the normal VM export. The
main-thread effect drain resolves the origin through that immutable snapshot
instead of the later end-of-frame VM pose. This removes later-instruction
movement/turn contamination from existing explosion origins and prepares pose
data for detached geometry. Unit body pose still comes from the stable display
frame; callback visibility remains post-VM-hide and must be corrected when
actual debris allocation/visibility ownership is integrated. Generic particles
are still present; this change does not claim detached-mesh parity.

All three takclient builds succeeded, with logs /tmp/debris-pose-{o2,release,debug}.log.
Targeted retail_script, retail_visual and model_transform CTests pass. These
checks are regressions, not a paired live EXPLODE visual comparison.

### Debris draw admission and model setup verified (2026-09-22)

Added `probe_debris_draw.py`: 2,048 native 492590 cases pass with only viewport
admission and final 4ee700 model submission replaced. Height-projected cells
use integer X/32 and (integer Z - (integer Y >> 1))/32, with bounds checks.
The selected mode independently tests sight bytes or the local player's
exploration bit; the probe deliberately varies these independently and checks
other-player bits do not admit the local view. Viewport rejection precedes
submission.

For admitted debris, native sets a temporary unit's world position, type and
color from detached state, points it at the detached view, and maps the copied
piece rotations into temporary body angles (piece Z/Y/X -> body +7c/+7e/+80).
The view links back to that temporary unit for model submission. This proves
setup and admission, not final vertex transforms, texturing, shadows or Glide
pixels. Next integration dependency is the detached model's pivot/vertex
transform through 4ee700/4ee620. No runtime changes in this step.

### Detached refresh transform verified; pivot interpretation corrected (2026-09-22)

Added `check_debris_transform.py`: full native 4ee620 refresh executes without
hooks for 1,024 synthetic detached pieces / 2,048 vertices. It matches compiled
model_transform_test with maximum coordinate error 0.00008392 world units.
Inputs use zero effective authored offset, captured piece move/turn, and the
same angles mapped into temporary body state as observed at debris submission.
Both body and piece rotations participate in this observed path.

Correction to prior wording: the constructor's stored bounds midpoint is NOT
proven to be a rotation pivot, and this full-refresh experiment disproves that
interpretation for this path. 4ee920 resets it before transforming. Running
each case with zero versus randomized stored centers gives identical vertex
output. Do not implement an extra rotate-about-midpoint operation based on the
earlier constructor-only observation.

Coverage remains single-piece synthetic transforms with authored arrays and
normal refresh state. Child transforms, complete live draw/Glide coverage,
geometry ownership and final runtime integration remain outstanding.

### Detached child transforms verified (2026-09-22)

Added `check_debris_hierarchy.py`: 1,024 synthetic child chains execute full
native 4ee620 refresh without hooks, with detached root authored offset
canceled and root rotation also supplied as temporary body angles. All 5,115
vertices at depths 1..8 match compiled model_transform_test --chain within
0.00005149 world units. This extends the single-piece transform evidence;
it does not prove sibling rendering, visibility traversal, textures or shadows.

Runtime integration can reuse the existing model transform math. It still
needs detached model/subtree ownership, pose capture before visibility changes,
source visibility conditional on successful allocation, tick draining, impact
effects and geometry submission. In particular collect currently returns for
a hidden parent; that behavior needs comparison with native traversal before
using it for selectively detached children. No runtime change in this step.

### Hidden-parent traversal corrected (2026-09-22)

Static native inspection shows per-piece visibility in the flat body geometry
loop (4ed3a8..4ed3d6, skip advances one 56-byte piece at 4eda3c) and projected
shadow loop (4eda90..4edacd). Neither rejects a subtree because its parent
visibility bit is clear. This also agrees with constructor behavior hiding
only a detached root unless subtree flag 0x40 is set.

Changed GameView::collect to suppress a hidden piece's own primitives while
continuing its transform and child traversal. Previously its early return
hid all descendants. The shared collector applies the correction to body and
shadow geometry. This is a runtime correction based on static native loop
evidence; no paired live screenshot has verified its visual result yet.

All three client builds succeeded (/tmp/piece-visibility-{o2,release,debug}.log);
retail_script, retail_visual and model_transform CTests pass. Detached geometry
integration remains outstanding.

### Production collector hidden-parent fixture passes (2026-09-22)

Added debug-only TAK_PIECE_VISIBILITY_TEST in fireTest. It calls the actual
GameView::collect with a synthetic parent/child mesh, parent movement/rotation
and visible child. In both body and shadow passes, hiding the parent must
produce exactly the same child vertex positions as removing only the parent's
primitives. Showing both must produce more geometry; hiding both must produce
none. This would fail the previous early-return implementation.

Ran the fixture through build-o2/takclient on Ulasem Arena with dummy SDL,
--firetest --nofog --time 1 --shot /tmp/piece-visibility-fixture.png. It exited
zero and printed the collector PASS in /tmp/piece-visibility-fixture.log.
The screenshot is only the harness completion artifact: assertions concern
collected body/shadow vertices, not a rendered model/pixel comparison. All
three client builds completed successfully (/tmp/piece-fixture-*.log).

### Numeric EXPLODE class dispatch integrated (2026-09-22)

Replaced the single generic small/medium selection with independent dispatch
for bits 0x100..0x10000 through the first nine authored explosion classes. The
installed explosions.tdf explicitly documents that these entries have fixed
script-number order. Retained authored class order during loading, and clear
class/order caches before reload to avoid duplicated variants after texture
recreation. Selection uses the previously verified cosmetic CRT formula.

TAK_EXPLOSION_CLASSES_TEST runs the production loader and explodePiece: each
of nine individual flags must add one effect, and the combined mask adds nine.
The fixture passes on Ulasem Arena with build-o2 dummy SDL, exits zero and
writes /tmp/explosion-classes-fixture.png; log /tmp/explosion-classes-fixture.log.
This checks dispatch/art availability, not paired visual timing. All three
client builds succeed (/tmp/explosion-classes-*.log); retail_script and
retail_visual CTests pass.

Remaining distinctions: native class 4 respects its blood setting; this client
has no corresponding blood-disable option and currently permits it. Native
class sound indices, exact sprite clock, generic debris and immediate smoke/fire
burst approximations are still pending. No simulation/pathfinding changes.

### Explosion classes use authored tick timing (2026-09-22)

Native manager 492320 advances active explosion sprites via 5373d0 once per
update. Changed spawnEffect class instances from generic 20 FPS real-time age
to existing authored per-frame durations and simulation-tick start time. This
applies to class-based impact/death/script explosions. The native clock probe
passes 147,456 updates and 1,280 compiled 64-update timelines. Production
class fixture now also asserts authored timing and correct start tick; it
passes (/tmp/explosion-clock-fixture.log). All three clients and visual test
binaries rebuild successfully; retail_visual and debris update comparison pass.

Correction: the last 491fd0 argument previously labeled sound is a timed
visual/light index. Update 492320 and draw 492070 use it for a visual envelope;
there is no sound dispatch in those branches. Renamed RetailDebrisMotion's
result field and probe labels to light. The earlier proposed sound-index
integration must not be followed. Exact dynamic-light rendering remains open.
Authored clock integration does not establish exact emission/update phase or
paired retail pixel output.

### Explosion manager lifecycle verified (2026-09-22)

Added probe_explosion_lifecycle.py, executing native 492320 with no routine
substitutions and real 537390/5373d0 authored clocks. Synthetic sprite/light
metadata exercises 256 cases, zero-duration frames, varied completion order
and forced timestamp wraparound. All 5,166 manager updates pass. Sprite and
light remain independently active; the light deadline is inclusive, and the
manager recycles an object only after both are inactive.

This supports existing authored sprite expiry but exposes an integration
requirement for future lights: retain them independently after the sprite
finishes. Current EffectInst lifetime covers only sprite output and must not
be reused unchanged for the light envelope. No runtime changes in this step.

### Explosion-light envelope draw observed (2026-09-22)

Added probe_explosion_light.py: 4,096 native manager draw calls pass for
synthetic envelopes with binary-exact time fractions. Only viewport admission
and final 4916d0 raster submission are sinks. The glow uses a linearly
interpolated diameter, truncation before horizontal halving, a second
truncation after multiplying by 0.75 before vertical halving, projected
integer X/Z-Y/2 camera coordinates, and alpha 196.

An initial arbitrary-denominator sweep found an x87 precision boundary:
case 669 produced a vertical radius one below the ordinary Python-double
formula. The committed comparison deliberately limits denominators to powers
of two; it does not prove arbitrary-duration rounding. Preserve this open
precision issue for a runtime envelope helper. Native 4916d0 queries renderer
capability 4 and has a hardware branch; final Glide submission/gradient
geometry remains to verify. No runtime light implementation added yet.

### Hardware explosion glow mesh verified (2026-09-22)

Added probe_explosion_glow_geometry.py. Full native 4916d0 hardware branch
constructs the mesh; renderer acquisition, capability/state calls and final
mesh submission are sinks. All 1,024 cases pass: primitive kind 6, fourteen
vertices forming a closed twelve-triangle elliptical fan, white RGB throughout,
requested alpha at the center and zero alpha around the edge. Diametrically
opposite vertices are symmetric and the perimeter lies on the ellipse within
float tolerance. State requests are (1,0), (27,1), (19,5), (20,6).

This is hardware geometry boundary evidence, not final Glide API/state or
pixel coverage. The authored envelope's arbitrary-denominator rounding and
actual backend blend-state translation remain open before runtime glow
integration. No engine edits or rebuild needed for this probe-only step.

### Explosion glow reaches native Glide API correctly (2026-09-22)

Added probe_explosion_glide.py: native 4916d0 glow creation now runs through
real 5b7fe0 state translation, 5b7e10 application and 5b79a0 vertex preparation.
Only renderer acquisition/capability and exported Glide calls are sinks. All
1,024 cases preserve fourteen fan vertices, center/perimeter colors, positions
and symmetry. The final primitive is Glide triangle-fan kind 5. Blend factors
are (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) for both color/alpha API pairs, not additive.
This establishes the backend mapping for SDL alpha blending. Final driver
raster coverage remains outside emulation; no runtime glow was added here.

### Compiled glow mesh matches native geometry (2026-09-22)

Added client/retailglow.h, constructing the fourteen-vertex elliptical fan
with white RGB, center alpha and transparent perimeter. Added the visual test
CLI --glow-mesh and a direct compiled comparison to the hardware geometry
probe. All 14,336 vertices/colors match in all three rebuilt test binaries;
maximum coordinate discrepancy is 0.00012574 pixels. retail_visual CTest
passes. No asset/binary tables are embedded; the helper expresses the observed
fan construction.

This helper is not yet called by GameView. Per-class envelope behavior,
clock/lifetime ownership, emission placement and geometry submission remain
for runtime integration. No client relink is needed until it uses this helper.

### Glow envelope rounding resolved for observed retail precision (2026-09-22)

Revalidated live retail PID 2429535 and read its main-thread floating-point
state with GDB, then detached. Control word is 0x027f: 53-bit precision,
round-to-nearest. Updated the envelope probe to use that observed mode and
retail's divide-then-multiply-then-add operation order. Arbitrary durations
1..127 now pass all 4,096 cases; the earlier binary-exact-only restriction
is removed. This is main-thread title-screen precision evidence, not a new
in-game Glide capture.

Added retailGlowRadii to retailglow.h with explicit intermediate double stores
to avoid reassociation/FMA. Native integer radii match exactly in all 4,096
compiled cases in all three rebuilt visual-test configurations. Helpers remain
unconnected to GameView pending per-class envelope inputs and independent
lifetime/render ownership. No client behavior changes in this step.

### Script explosion glows connected to runtime (2026-09-22)

Observed unmodified native class outputs at successive times, including removal
via 492320: class 0 shrinks diameter 64 to 8 over 15 updates; classes 1/2
shrink 128 to 16 / 200 to 32 over 22 updates. Class 3 is not a valid glow
index (the fourth script explosion maps to glow 2). Added these behavior
parameters as RetailGlowEnvelope.

Script explosion classes 0..3 now create independent ExplosionGlow instances.
Update retains the inclusive final tick; draw submits the verified fan as
twelve SDL triangles with ordinary alpha blending before effect sprites.
Glows survive sprite expiry independently. They currently use the existing
script-effect origin/projection and cellVisibleR gate; native height-projected
visibility, exact tick phase and paired pixels remain open. Other producers
of native glows (weapon explosions/debris impacts) are not yet connected.

All three clients and visual tests rebuild successfully (/tmp/glow-live-*.log).
Native Glide and compiled envelope probes pass; targeted script/visual/model
CTests pass. The existing class fixture exits zero and writes its screenshot;
it does not gate on visible glow pixels, so live glow appearance is still
unverified. Detached model debris remains unfinished.

### Live three-size glow capture inspected (2026-09-22)

Added TAK_GLOW_TEST and debug draw-count screenshot gating. The first attempt
timed out because startup consumed the initial short lifetimes; the debug
fixture now repeats after all three expire. A first capture also exposed an
over-wide layout, corrected to fit all sizes at zoom 1.5. Final Ulasem Arena
dummy-SDL run exits zero and /tmp/glow-visible.png was inspected: three white
elliptical glows of increasing size are visible over terrain. This verifies
the runtime geometry submission path, not paired retail pixel equivalence.

Final client builds succeeded in build, build-o2 and build-dbg, logs
/tmp/glow-layout-{release,o2,debug}.log. The debug-only repeat/camera fixture
does not change normal effect lifetimes. No live takclient remains from it.

### Built-in glow class regression (2026-09-22)

Added probe_explosion_glow_classes.py, which leaves retail's class records
untouched and executes manager update plus drawing for all three valid light
classes. Only viewport admission and final geometry submission are sinks.
The compiled retail_visual_test --glow-class path uses the production envelope
and radius helpers. All 234 samples match in build, build-o2 and build-dbg,
including the inclusive expiry tick and unsigned timestamp wrap. The optimized
retail_visual CTest also passes. This closes the earlier synthetic-parameter
coverage gap; it does not establish full scene pixel equivalence or finish
the remaining detached debris and transport timeline work.

### Remove unsupported immediate debris bursts (2026-09-22)

The client EXPLODE callback still generated immediate generic smoke/fire bursts
for low flags 8/16, even with BITMAPONLY. Native dispatch instead passes these
flags into the detached debris descriptor; immediate class effects use the nine
high class bits. Removed those bursts. The existing runtime class fixture now
checks BITMAPONLY with smoke, fire, and both produces no particles or sprites.
Native dispatch passes 4096 cases; the optimized live fixture exits zero with
its class checks passing (/tmp/debris-flags-fixture.log). All three clients
rebuilt successfully (/tmp/debris-flags-{o2,release,debug}.log).
This removes invented effects; detached mesh and attached emitters are still
missing and must be implemented before claiming complete death animations.

### Compiled debris launch behavior (2026-09-22)

RetailDebrisMotion now exposes script launch initialization with the observed
three bounded random draws, fixed velocities, 900-update lifetime and script-to-
debris flag conversion. BITMAPONLY consumes no draws. The native dispatch probe
now compares these fields directly with retail_visual_test --debris-launch:
4096 cases pass in all three builds. The existing 4096-case native motion update
comparison also passes in build-o2. This helper is not yet connected to model
ownership or runtime drawing; generic debris remains until those are integrated.

### Preserve pre-hide visibility in EXPLODE capture (2026-09-22)

Both display VM interpreters now invoke onExplode before hiding the source
piece. Native constructor observations copy the visible pose before changing
source visibility; the previous callback order captured an already-hidden
root for future debris rendering. Added the asset-independent explode_capture
CTest for both interpreters, ordinary EXPLODE and BITMAPONLY, callback delivery,
captured visibility and post-instruction source visibility. Native construction
probe still passes 1024 creations plus subtree/allocation cases. All targets
rebuilt in all three directories; targeted animation/transport CTests pass.
This does not yet implement allocation-dependent hiding or detached geometry.

### Detached geometry ownership helper (2026-09-22)

Added retailDebrisModel to retain an independent copy of the selected object's
vertices and materials, remove its authored root offset, and copy descendants
only for subtree detachment. model_transform_test now checks that modifying or
destroying the source children/materials cannot change the detached geometry,
while child offsets and deeper descendants survive subtree copying. All three
model_transform CTests pass; the optimized native hierarchy comparison passes
5115 vertices with maximum error 0.00005149 world units. Runtime ownership,
motion, drawing, emitters and allocation-dependent hiding still need joining;
the helper alone does not change visible debris.

### Detached model runtime connected (2026-09-22)

EXPLODE now retains the selected model geometry and captured poses instead of
creating three generic colored chunks. The display-owned list admits up to 100
pieces, initializes observed launch motion, uses the unit's absolute world
position, and advances fixed motion/collision once per elapsed game tick. Impact
class sprites use absolute positions and the impact tick. Drawing uses copied
materials/poses, the observed piece-angle body transform and height projection.

TAK_DEBRIS_TEST exercises production creation, deletes the source visual, and
checks owned geometry/no generic particles. The live optimized dummy-SDL run
passes; /tmp/debris-runtime.png was inspected and shows its detached triangle.
All three clients rebuild successfully. Native 4096 motion updates and 2048
transformed vertices still match; targeted optimized CTests pass.

Remaining limitations are explicit: source hiding is still unconditional after
the callback (including full allocation), subtree source hiding is incomplete,
attached smoke/fire/blood are absent, impact glow is absent, water weather flags
currently default false, per-frame snapshots do not recover every instruction's
body position, and debris is drawn in the effects pass without retail scene
sorting. Cosmetic random-stream interleaving is not exact.
These must be resolved before treating detached death animation as complete.

### Debris impact glow connected (2026-09-22)

Ground-impact light results now create a glow independently of sprite loading,
anchored to the same absolute position and impact tick. ExplosionGlow supports
absolute coordinates with effect visibility and native integer height projection.
The runtime debris fixture now forces one ground collision and asserts removal,
class-0 glow, exact position and start tick. It passes in the optimized client
(/tmp/debris-impact-fixture.log); all three clients rebuild. Native motion (4096)
and built-in glow timeline (234) comparisons also pass. The screenshot occurs
after this short glow expires, so it is not evidence of impact-glow pixels.
This closes the impact-glow wiring omission, not the other debris limitations.

### Correction: legacy debris smoke/fire flags (2026-09-22)

Earlier notes assumed the low SMOKE/FIRE script flags required attached smoke
and flame emitters. Tracing the complete 492910 constructor, 492420 updater and
492590 draw dispatch contradicts that assumption: descriptor flags are retained,
but do not create those emitters. The optional attached particle system is gated
by blood settings and a separate script query. Expanded native constructor
coverage to every low six-bit descriptor combination across 1024 cases; all
preserve flags and have no attached emitter when blood is disabled. Existing
subtree and allocation-failure checks still pass. Corrected the runtime comment.
Do not add smoke/fire solely from these legacy flag names. Script-driven blood
and the remaining rendering/visibility limitations still need investigation.

### Native QueryBlood construction gate (2026-09-22)

Extended probe_debris_creation.py through the optional blood branch, sinking
only the script query and particle allocation/construction/start calls. The
constructor requests QueryBlood with a minus-one output sentinel. With blood
enabled, output zero, one and 37 all allocate/start the emitter; minus one does
not. Disabling blood skips the query entirely. The observed particle constructor
arguments are capacity 100, unit blood-color address and kind 3; start receives
100 and the unit body position, not the detached piece origin. All eight gate
cases pass alongside the existing 1024 geometry cases. This observes dispatch
only: actual particle initialization, motion, drawing and script invocation
semantics remain to be matched before integrating blood into the client.

### Native blood emission initialization (2026-09-22)

Added probe_debris_blood_emission.py for 4f2130 with only CRT draws and list-node
allocation replaced. Native trigonometry, particle construction and insertion
execute. Across 256 capacity/request combinations, 3759 particles confirm
capacity clamping, four random draws per particle, three-color selection, exact
body-origin position, initial stage zero and upward velocity. Existing-count
cases use a synthetic count to isolate admission, not a complete existing list.
This differs from wake particles; don't reuse their six-stage cyan behavior.
Horizontal trig outputs execute but are not yet compared with a compiled port;
updates, rendering and runtime blood integration remain open.

### Blood particle motion and contact (2026-09-22)

Added RetailBloodParticle and probe_debris_blood_update.py. Native 4f1e50 moves
first, applies gravity, then samples terrain. Ground contact removes the moving
particle; contact above sea level requests a stain after resampling terrain and
replacing only the integer half of Y (fraction retained). At/below sea level it
removes the particle without a stain. No timer or wake-color stage drives this
update. All 4096 native/compiled cases match in all three visual-test builds,
including state, alive result, stain position and terrain-query count. Native
terrain and stain creation are sinks; stain lifecycle/rendering and runtime
blood integration remain open. This is a display helper, not a sim change.

### Blood stain capacity behavior (2026-09-22)

Added probe_blood_stain_capacity.py, executing native 4f2500 plus its point
constructor with only node allocation replaced. 1250 successive insertions
across capacities 1, 2, 3, 17 and 100 preserve exact position/color, list links
and count, and evict oldest marks first when full. This demonstrates bounded
retention under insertion, not an elapsed-time lifetime; the owning system's
update and render dispatch still require verification. The first probe run
needed signed conversion of the packed color for the emulator's call ABI;
the corrected probe passes. No runtime stain implementation added yet.

### Blood stains persist until capacity eviction (2026-09-22)

Native 4f3020 initializes the global blood-mark system with capacity 2048 and
vtable update 4f34f0. That update traverses marks without changing their state.
Expanded the capacity probe to 2048 and repeated 1000 native updates per tested
capacity, checking system, list and node bytes unchanged. All 3305 insertions
and 6000 updates pass, including overflow at the retail capacity. Blood marks
therefore have no elapsed-time fade in this update path; retain up to 2048 and
evict oldest on insertion. Draw dispatch is 4f3440 (point geometry), still to
be tested through the Glide renderer boundary before runtime integration.

### Blood points and stains at Glide submission (2026-09-22)

Expanded probe_script_point_glide.py to native moving-blood draw 4f3330 and
stain draw 4f3440, alongside owner points. All three paths pass counts 1, 64,
65, 100 and 2048 through the real Glide point preparation method. Fractional
world coordinates confirm integer truncation before height/camera projection;
viewport clipping, packed colors, half-pixel offset and submission order match.
Renderer acquisition/state setters and final Glide API are sinks, so this is
geometry/color submission evidence, not framebuffer pixel or blend-state proof.
Runtime blood emission and persistent stain storage are still not integrated.

### Compiled blood launch matches native trig (2026-09-22)

RetailBloodParticle::emit now uses the existing observed retail scaled sine/
cosine helpers for its horizontal velocity, plus the four-draw palette/angle/
radial/vertical initialization. Extended the emission probe to compare the
compiled result directly: all 3759 particles match all position/velocity words,
packed colors and draw counts in build, build-o2 and build-dbg. This resolves
the earlier unverified horizontal launch component. Runtime QueryBlood capture,
emitter ownership, stain storage and drawing remain to be connected.

### QueryBlood script semantics and palette gap (2026-09-22)

Inspected shipped data.hpi/scripts/zonhunt.cob in a temporary file. QueryBlood
uses a random draw and writes 2..6 to local argument zero. The display VM's
native query adapter exposes this through lastLocals(), not call()'s return.
Added cobanim_test --blood-query and ran 64 calls on that shipped script with
argument sentinel -1; all pass. Querying must preserve this script execution
and its random draw, not infer blood from bodyType or callback presence alone.
The current UnitType loader retains only bloodcolor1; the native emitter selects
among three packed blood colors, so palette loading also needs completion.
Runtime hookup remains pending these details and emitter ownership handling.

### Three authored blood colors loaded (2026-09-22)

UnitType now retains all three bloodcolor fields as packed display colors, while
the existing first-color RGB field remains available to current hit effects.
Missing components/colors fall back to the existing first-color convention;
native missing-field defaults have not been verified. Added retailgap_test
--blood-palette against the installed Hunter definition, passing in all three
builds. All targets rebuilt in build, build-o2 and build-dbg. Five targeted
Debug CTests pass including retailgap and both transport tests. Determinism
golden remains dcef618cd2e4d558 (ARM toolchain header legs skipped). Two local
Debug server/client Ulasem Arena 10-second mpai runs with seed 1 both end at
83a52a301ae12a8b; logs /tmp/blood-net-{server,client}-{0,1}.log. Runtime blood
emitter hookup remains pending; no movement/pathfinding logic changed.

### Blood runtime attached to detached pieces (2026-09-22)

Accepted detached pieces now query the source display VM with sentinel -1,
create 100 verified blood particles for non-minus-one output, retain them with
the piece, and update them after surviving debris motion/collision. Ground
stains use a 2048-entry oldest-first queue. Moving particles and stains draw
packed-color points with integer height projection and effect visibility.
The runtime fixture uses a zero-output synthetic QueryBlood, checks 100 attached
particles, then forces ground removal. Optimized fixture exits zero; all three
clients rebuild; five targeted optimized CTests and native particle update pass.

This is initial integration, not full parity: QueryBlood currently runs during
main-thread event drain rather than precisely at the EXPLODE instruction,
cosmetic random-stream interleaving differs, blood setting gating is not wired,
and point drawing is in the effects pass rather than verified retail scene order.
The fixture removes particles before capture, so its screenshot does not prove
blood pixels. Live blood/stain captures and full source-hiding behavior remain.

### Live blood particle capture inspected (2026-09-22)

Added TAK_BLOOD_TEST as a variant of the production debris fixture that retains
its particles for rendering instead of forcing immediate collision. The Debug
dummy-SDL run exits zero; /tmp/blood-live.png was inspected and shows many small
red blood points spreading below the detached triangle. All three clients were
rebuilt. This verifies visible runtime particles, not paired Glide pixel parity
or persistent-stain pixels. Settings inspection confirms the client currently
has no blood toggle, so there is no existing user preference to wire; the native
blood-disabled mode remains outside the exposed options. Exact query timing,
scene order and remaining detached/source visibility work are still open.

### Subtree source hiding at EXPLODE (2026-09-22)

Display VMs now receive model-derived COB descendant indices at registration.
Both interpreters hide descendants for flag 0x40 after the pre-hide callback;
BITMAPONLY suppresses hiding, and root siblings remain attached. This happens
at instruction time, so later SHOW instructions retain their normal ordering.
Extended explode_capture tests cover all combinations in both interpreters.
Native construction/subtree probes pass, as do five targeted CTests in each
build; all targets rebuilt in all three directories. Full-capacity/allocation
failure still hides sources incorrectly, and metadata for veteran-model swaps
has not been revalidated. These remaining cases prevent claiming full parity.

### Refresh detachment hierarchy on veteran model swap (2026-09-22)

Factored display model-to-COB hierarchy configuration into one method, used at
registration and after a successful veteran-model swap. The piece-visibility
runtime fixture now replaces an arm/hand hierarchy with sibling arm/hand nodes,
then executes subtree EXPLODE and verifies the newly separate hand stays visible.
The Debug fixture exits zero (/tmp/debris-modelswap-fixture.log); all three
clients rebuild. This tests production hierarchy replacement, not every shipped
veteran mesh or corpse script sequence. Allocation-dependent source hiding and
the broader scene/timing parity requirements remain open.

### Correct debris collision terrain sampling (2026-09-22)

Native debris calls 511260, not blood's 511170 interpolated terrain sampler.
Added retailDebrisTerrainHeight: truncate integer world coordinates toward zero
when dividing by cell size, then average the cell's minimum and maximum corner
heights. Switched only client detached-piece collision to this helper. New
probe_debris_height.py executes both native lookup and sampling without sinks;
4096 sloped-cell, negative-coordinate and edge samples match in all three builds.
All clients/visual tests rebuilt, and the Debug debris/blood runtime fixture
still passes. This is cosmetic collision only; navigation/pathfinding unchanged.

### Map-backed debris splash selection (2026-09-22)

Native scenario loading at 4c831c/4c8330 identifies +d39 as lavaworld; the
previously verified +d3d field is nosealeveltrigger. GameView now reads both
OTA GlobalHeader fields and passes them to debris motion instead of constant
false values. This selects numeric splash class 10 for lava, 9 for ordinary
water, and suppresses water impact effects when nosealeveltrigger is enabled.
All clients rebuilt. Existing 4096 native/compiled update cases cover these
branches and pass; the ordinary-map runtime fixture still passes. A live lava
map screenshot and explicit OTA loading fixture remain unverified.

### Runtime splash dispatch fixture and class-order caveat (2026-09-22)

Extended TAK_DEBRIS_TEST through all four lava/suppression combinations. It
checks debris removal, sprite count, class-9/10 variant identity, absolute
impact position/tick and absence of a water-impact glow. Debug runtime fixture
passes; all three clients rebuild (/tmp/debris-splash-*.log).

Asset inspection exposes a caveat to earlier wording: the installed V3Rocket
explosions.tdf lists lava explosion and lightning explosion at authored indices
9 and 10, not ordinary/lava splash by name. Native update definitely requests
numeric 9/10; native class-loader ordering must be verified before asserting
which artwork retail displays or changing this mapping. Do not substitute
named water effects based only on semantic expectations. The runtime fixture
checks current numeric dispatch, not native loader equivalence.

### Native class enumeration order (2026-09-22)

Traced 492ce0: it enumerates parsed top-level children, appends each class to the
global numeric array, then enumerates that class's variants. No special remap of
numeric classes 9/10 appears in this loop. Added an unhooked native enumeration
probe covering 8070 randomized child selections, empty classes and out-of-range
termination. Selections preserve the parsed child-vector order, not name order.
This resolves the enumeration layer only: archive file enumeration and original
TDF parser insertion order still need verification before claiming which class
names occupy 9/10 in a complete retail load. No runtime mapping changed here.

### Native TDF parser preserves authored explosion order (2026-09-22)

Added probe_explosion_tdf_order.py, executing full native 5427f0 text parsing
with only memory allocation/free/string-duplication sinks. 995 synthetic class
nodes across shuffled files retain authored order. Parsing the installed
V3Rocket explosion file (temporary /tmp/retail-explosions.tdf) produces 31
classes, with lava explosion and lightning explosion at indices 9/10. Thus the
unexpected names are present after native parsing, not introduced by our parser.
The first exploratory run reached a CRT heap import; adding its allocator sink
allowed the complete parser to execute. Full archive enumeration across all
explosion definition files remains the final ordering layer to verify.

### Installed explosion inventory closes multi-file ambiguity (2026-09-22)

Inspected installed archive listings: data.hpi and V3Rocket.hpi contain the same
gamedata/explosions/explosions.tdf path, with no additional explosion definitions
in the other installed archives. Mounted VFS resolution selects V3Rocket. Added
definition paths and numeric class order to effect_inventory; its rebuilt
optimized run confirms one definition, 31 classes, 52 variants and no differing
bank/sequence names (/tmp/explosion-order-inventory.log). Combined with native
parser/enumerator observations, class 9/10 names in this installation should
remain lava explosion/lightning explosion; do not replace them with named water
classes based on expected appearance. This inventory doesn't establish parity
for other asset sets or substitute for a final live retail splash capture.

### Full post-integration regression sweep (2026-09-22)

All 45 CTests pass in build (17.06 s), build-o2 (17.75 s), and build-dbg
(133.92 s), logs /tmp/transport-animation-sweep-{release,o2,debug}.log.
Reran native passenger scheduling (49 cases and 4096 compiled comparisons),
pickup (4096 transfer states, fractional boundaries and stationary timelines),
air unload (100 cases), sea unload (62 cases), range (4096 each) and PARK radii
(36 each). All pass. The first unload invocation omitted its required output
argument and was corrected before either unload run. Blood initialization,
motion, stain retention and all three Glide point submission paths also pass.

These results validate accumulated changes, not completion of the goal. Full
distant/coastal transport traces, allocation-dependent visibility, exact effect
query/body timing, scene ordering/shadows, persistent-stain captures and paired
retail visual comparisons remain required. No running test processes remain.

### Attached blood lifetime follows detached-piece removal (2026-09-22)

Added probe_debris_emitter_lifecycle.py, executing native debris update and
removal with attached-emitter virtual methods, vector cleanup and terrain as
boundary sinks. Eight attached/absent cases cover expiry, water, ground and
survival. Removal destroys the emitter before any further particle update and
clears the debris slot; survivors update the emitter after moving. All pass.
This supports current per-piece blood ownership and update order: particles
must not survive their debris owner. The emitter destructor's internal memory
cleanup is not emulated here; previously created global stains are independent.

### Live blood-to-stain transition verified (2026-09-22)

The four-second software-renderer capture completed successfully at
/tmp/blood-stains-live.png. Added a debug-only assertion to TAK_BLOOD_TEST's
production update path to distinguish airborne points from retained stains:
when the last attached particle disappears, exactly 100 global stains must
exist. Rebuilt Debug and repeated the run; /tmp/blood-stains-counted.log
confirms 100 stains and zero airborne particles, with screenshot
/tmp/blood-stains-counted.png. The capture shows red ground marks on the dry
terrain. Reran probe_blood_stain_capacity.py: 3305 native insertions and 6000
updates pass, including oldest-first eviction at capacity 2048. This closes
the ambiguous airborne-versus-ground classification in the local capture;
it does not establish paired live Glide pixel parity or scene ordering.

### Native debris admission now included in creation probe (2026-09-22)

Removed the free-slot lookup substitution from probe_debris_creation.py.
Native 4923b0 now executes inside every creation case. Independently exercised
a single vacancy at each of 100 indices, a full pool, and an entirely empty
pool: admission returns the first free slot, or failure for the full pool,
without mutating the table. Creation assertions check actual slot installation
and unchanged tables after failed allocation. All previous 1024 creation,
subtree, failure and blood-gate cases pass with this stronger boundary;
log /tmp/debris-native-admission.log.

The client visibility mismatch remains open. Both VM interpreters currently
hide immediately after the callback, while the client callback only queues
creation until after the parallel animation pass. A deferred visibility
restoration cannot safely fix this: subsequent script SHOW/HIDE commands must
retain their ordering. Admission must be available at the EXPLODE instruction,
without making shared-pool acceptance depend on worker scheduling. No engine
behavior changed in this probe update.

### VM supports instruction-time debris rejection (2026-09-22)

Changed Vm::onExplode to return an acceptance boolean. Both legacy and native
interpreter adapters now hide the piece/subtree only after acceptance; the
callback still sees the original pose. Hosts without a callback retain prior
behavior. Expanded explode_capture to 24 acceptance/flag/later-command cases
per interpreter (48 total): rejection leaves pieces visible, and subsequent
SHOW or HIDE retains script ordering independently of acceptance.

The client callback explicitly still returns true after queuing its event.
This is the VM-side prerequisite, not the completed client capacity fix;
instruction-time shared-pool admission remains to be integrated. All targets
were rebuilt in the three configurations. Debug transport, transport_roster,
cobanim and explode_capture pass; the same four tests pass in Release and
optimized builds. Cross-compiler determinism remains
dcef618cd2e4d558; ARM legs skip for unavailable target headers.

### Synchronous explosion query prerequisite checked (2026-09-22)

Inspected native dispatch wrapper 492cc0: it forwards directly to 492910 using
the global debris manager. There is no deferred creation queue at that boundary.
Added an explode_capture case in each interpreter which calls QueryBlood from
inside the accepted explosion callback, resumes the outer script through SHOW,
then rejects a second explosion. Both queries return their argument output,
both outer scripts resume, and the second source remains visible. Debug test
passes (/tmp/explode-reentrant-test.log). This verifies that nested query
execution itself does not prevent instruction-time client creation. Shared
client pool access during the parallel VM pass still requires integration;
the new test does not claim that current queued client effects are corrected.

### Client EXPLODE now creates synchronously (2026-09-22)

Removed pendingExplode. The installed client callback captures the current
piece poses, creates debris immediately, and returns actual acceptance to the
VM. QueryBlood now runs during that callback for accepted debris. An exhausted
100-piece pool returns false before querying blood; independent sprite-class
bits still dispatch. The live debris fixture now explicitly checks admission
into an empty pool and rejection at 100 retained pieces.

VMs whose script code contains the EXPLODE opcode are conservatively ticked
on the main thread in ascending unit ID order; other VMs retain the worker
pool. Script-start notifications already execute on the main thread. This
avoids worker scheduling determining admission and preserves subsequent
SHOW/HIDE ordering. The scan may conservatively match an operand word, which
only affects scheduling. Body positions still come from the pinned frame,
and unit-ID ordering is stable but not verified against native global order.

Performance is an explicit remaining check: 192 of 204 extracted installed
scripts contain the opcode, so this conservative classification serializes
most display VMs. A more selective implementation must account for active
thread control flow and script calls. Do not treat this as performance-validated
or the full transport/animation goal as complete.

All three clients rebuilt successfully; the four targeted animation/transport
tests pass in all three builds. The initial live invocation omitted --shot,
passed its assertions but hit the 45-second wrapper timeout. Repeated with
--shot /tmp/debris-sync-live.png: clean exit and screenshot produced, with
the production debris fixture passing (/tmp/debris-sync-live-shot.log).

### 16,000-VM scheduling measurement (2026-09-22)

Added optional cobanim_test --schedule-bench <script.cob>. It constructs 16,000
display VMs from a shared real script, starts Create/StartMoving, supplies moving
unit queries, advances 120 frames (30 warmup), and alternates two serial and
two worker-pool trials. Final piece visibility/transforms and active thread PCs
must agree. This is an animation microbenchmark, not a whole-game sim-speed or
rendering benchmark; it does not spawn gameplay units or bypass unit caps.

Optimized Hunter run (/tmp/animation-schedule-bench.log): serial median
15.576/15.762 ms and p95 22.313/22.604 ms; parallel median 3.477/3.421 ms and
p95 5.406/4.220 ms. All four final-state signatures agree (b2d032fe57c73b83).
This demonstrates a material regression from classifying by the entire file.
The synchronous creation fix is not performance-complete. Next integration
must conservatively classify reachability from active thread PCs, including
branches and START/CALL targets, so dormant Killed code does not serialize
unrelated movement loops. Unknown/malformed control flow must fall back to
serial execution rather than risk a shared effect callback on a worker.

### Active-path scheduling implementation (2026-09-22)

Added a shared per-file conservative control-flow map. Reverse reachability
starts at EXPLODE and unsafe instructions/targets, follows conditional and
unconditional branches, and includes START_SCRIPT/CALL_SCRIPT destinations.
RETURN terminates a path. Operand words are skipped by actual instruction
length, so an opcode-valued constant does not itself force serialization.
Vm::mayReachExplosion checks active native thread PCs, or legacy threads and
their return stacks, without allocating a per-frame PC vector. The client
uses this map instead of whole-file opcode presence. Unknown paths remain
serial; active safe loops can use workers despite dormant Killed code.

Expanded the optional 16,000-VM benchmark with selective scheduling trials,
and added graph and active-thread transition tests to explode_capture. All
three full rebuilds are in progress; validation and updated performance
numbers remain pending. The determinism check passes at dcef618cd2e4d558
(ARM legs skipped for missing headers). An early invocation of the Debug
test occurred before its rebuilt binary linked, so it is not evidence for
the new reachability cases; rerun after the build completes.

Validation completed: all targets rebuilt in all three configurations, and
transport, transport_roster, cobanim and explode_capture pass in each. The
live debris fixture exits cleanly with /tmp/debris-selective-live.png and
passing capacity/geometry assertions (/tmp/debris-selective-live.log).

Optimized 16,000-Hunter comparison (/tmp/animation-selective-bench.log):
selective medians 4.977/4.805 ms, p95 6.279/5.960 ms, versus serial median
about 15.7 ms and unrestricted parallel about 3.4 ms. All six final animation
signatures remain b2d032fe57c73b83. Classification removes most of the measured
regression while retaining synchronous admission; it still costs roughly
1.4–1.6 ms over unrestricted parallel in this isolated moving-Hunter workload.
These numbers do not establish mixed-roster or full-match performance, nor
the native ordering of simultaneous explosions across different units.

### Scheduler safety across installed animation roster (2026-09-22)

Expanded animation_roster_test to check pre-tick classification against every
executed EXPLODE callback. Notifications remain explicitly main-thread work.
In addition to each script's existing 1200-tick callback timeline, six death
timelines cover severity 1/100/1000 and damage types 0/1, with 240 subsequent
ticks to exercise delayed child scripts. Optimized run: 202 scripts, 3261
explosion callbacks, 1638 serial and 774042 parallel frame classifications,
zero failures (/tmp/explosion-roster.log). This is exercised-path coverage,
not exhaustive proof for all possible script inputs or native timing.

Release and optimized full suites pass all 45 tests (8.88 and 9.20 seconds).
Logs /tmp/explosion-full-{release,o2}-tests.log. The Debug full suite is still
running; its completion must be checked before claiming all-three full passes.

Debug subsequently completed successfully: all 45 tests pass, log
/tmp/explosion-full-debug-tests.log. Native debris creation/admission and
4096 compiled launch comparisons also pass. The first dispatch comparison
named a nonexistent executable and failed before compiled comparison; reran
with build-o2/retail_visual_test successfully. No test process remains pending
from this sweep.

### Pickup handler creates a live native pursuit controller (2026-09-22)

Removed pursuit-constructor and radius-setter substitutions from
check_transport_selection.py. Entry observers record arguments without
replacing those native routines; only reference bookkeeping is substituted.
The air pickup handler now executes the native controller constructor and
radius setter together. Inspect its resulting flags/radius and invoke the
native target getter through 16 target moves for each air approach case.

PASS: 512 air/sea candidate rosters, 60 approach/controller/wait cases, 480
moving-target positions (including the 511-height clamp), and 100
failure/mover/speed combinations. The same controller follows subsequent
target positions without reconstruction. Log:
/tmp/transport-native-controller-integration.log. This closes a substituted
mission-to-controller boundary, not the full native distant/coastal/blocked
navigation timeline; controller installation and ground approach remain sinks.
No engine changes or rebuilds were necessary for this probe enhancement.

### Ground pickup approach constructor executes natively (2026-09-22)

Removed the ground-approach substitution from check_transport_selection.py.
Native 4d4da0 now executes 4e2500 and its base constructor; only allocation
and final controller installation remain controlled. The 30 ground polling
cases verify mission ownership, target cell, world radius (transport distance
minus 16) and rounded squared cell radius on the resulting controller.

Added 16 odd/even-footprint and fractional/negative-coordinate cases. Native
ground circles snapshot the target: changing the passenger position leaves
the controller unchanged; requesting another approach reconstructs the circle
at the new target. This differs from the live passenger reference used by air
pursuit. Existing World pickup code stores missionTarget at its polling step,
consistent with this observed distinction. All prior air/selection/failure
cases still pass. Log /tmp/transport-native-ground-controller.log. This does
not yet exercise full controller installation, navigation and dispatcher
timelines together. Engine behavior and binaries are unchanged this turn.

### Native controller installation order and pending flags (2026-09-22)

Added probe_transport_controller_install.py, executing 4d4d40 with only
navigator virtual installation and old-controller destruction as sinks.
2048 combinations of mover availability, old/new controller presence and
random pending flags pass. With a mover, replacement detaches the old
controller, destroys it, stores the new pointer and installs it; a non-null
replacement clears exactly pending bits 0x3700. Null removal preserves those
flags. Without a mover, the mission controller and pending flags are unchanged.
Log /tmp/transport-controller-install.log. This observes the installation
boundary, not navigation progress or destruction internals.

Review follow-up: surface carrier/passenger approach installation explicitly
clears mission.pending bits 0x3700. Air pickup's advanceApproach updates its
flight goal without an equivalent clear at the polling installation point.
Determine with an injected-pending-event World regression whether this leaves
stale events; do not clear on every pursuit position refresh, since native
refresh and controller replacement are distinct operations. No engine change
has yet been made for this potential discrepancy.

### Air pickup clears stale events on pursuit installation (2026-09-22)

Added a World regression injecting pending movement bits plus an unrelated
bit before the first outside-range approach poll. Before the fix, the surface
case passes and the air case fails (/tmp/pickup-pending-before.log). Added
the native 0x3700 clear at air pickup's polling installation point. A separate
assertion verifies per-tick pursuit position refresh does not clear newly
pending bits between polls. This changes mission event bookkeeping, not the
path search or steering algorithms.

All targets rebuilt in build, build-o2 and build-dbg. Transport, transport_roster
and retail_mission tests pass in each; cross-compiler golden remains
dcef618cd2e4d558 (ARM legs skip for missing target headers). Repeated local
multiplayer validation is running; check /tmp/pickup-pending-network.log and
its per-run client/server logs before claiming that check complete.

Both local ten-second multiplayer runs completed successfully with hash
83a52a301ae12a8b, matching each other and the prior equivalent smoke fixture.
The harness stopped both servers after their clients exited. This checks
rebuild consistency and ordinary lockstep repeatability; the injected transport
event regression provides coverage for the specific behavior change.

### Air unload and transfer-departure event clearing (2026-09-22)

Native air unload calls controller installation in both outside-range approach
and in-range departure stages (41b1ff / 41b1a1). Added a World regression that
boards cargo, drifts outside an already-issued unload destination, then
injects pending bits at approach and departure polls. Both cases failed before
the fix (/tmp/unload-pending-before.log). Clear pending movement bits at those
controller replacements. Added the equivalent clear and regression for air
pickup's transfer-departure controller. Ordinary position refresh still
preserves pending events.

All targets rebuilt in all three configurations; transport, transport_roster
and retail_mission tests pass in each. Native installation and 100 air-unload
cases pass, including 4096 range comparisons and 36 PARK radius checks.
Cross-compiler golden remains dcef618cd2e4d558 (ARM header-dependent legs skip).
Repeated local multiplayer verification is running with logs under
/tmp/unload-pending-{network,client-0,client-1,server-0,server-1}.log.

Further dispatcher audit is required: unload currently waits on its deadline
directly, unlike pickup's combined pending/unit-event wake check. Test whether
an actual flight-controller arrival wakes unload before the polling deadline.

Repeated multiplayer verification completed: both ten-second runs produced
83a52a301ae12a8b. The harness terminated both servers after client completion.

### Air unload arrival wakes transfer before the polling timer (2026-09-22)

Added a World regression which moves an approaching carrier within its flight
controller's arrival radius, then lets the real movement update generate the
event. Before the fix, neither the arrival notification nor next-tick transfer
assertion passed (/tmp/unload-arrival-before.log).

Added probe_transport_flight_arrival.py. Native point/pursuit construction,
queries, 524af0 navigation update and 4e4de0 detachment execute together;
reference bookkeeping and event delivery are boundary sinks. At arrival, a
point controller emits 0x100 then 0x400 and detaches, while a passenger pursuit
emits only 0x100 and remains installed. Both cases pass. Initial scratch setup
needed a valid terrain-height pointer and global game pointer before running
these native queries; the checked-in probe supplies both.

The client-independent simulation now posts both completion events and releases
transport point goals on arrival. Unload waits use the same pending/unit-event
and timer admission logic as pickup, so arrival wakes stage 1 before its timer.
The World regression now passes (/tmp/unload-arrival-after.log). This does not
establish full post-detachment inertial motion or native route timing parity;
those remain part of the complete transport trace audit.

All targets rebuilt. Release and optimized suites pass all 45 tests; Debug is
still running. Cross-compiler golden remains dcef618cd2e4d558 (ARM headers
unavailable), and both repeated local multiplayer runs yield 83a52a301ae12a8b.
Logs /tmp/unload-arrival-{release,o2,debug}-tests.log and
/tmp/unload-arrival-network.log. Check the Debug completion before claiming
all-three suite success.

Debug subsequently completed with all 45 tests passing. No build, test or
multiplayer process remains pending from this validation batch.

### Detached air transport retains flight integration (2026-09-22)

Extended the native flight-arrival probe: after point-controller detachment,
32 navigator updates preserve the stored destination, drift and heading;
the native output getter returns those retained values. Running the actual
flight velocity routine with that detached navigator still changes velocity.
Only orientation/banking callbacks are added as sinks for this velocity check.
Log /tmp/transport-detached-flight.log. This confirms controller detachment
does not itself zero navigation or stop flight physics.

The previous transport completion change reset flightGoal and returned, while
subsequent transport updates skipped movement without a goal. Extracted
tickFlightBody from controller handling. Transport arrival now updates its
navigation state, detaches, then integrates the body; subsequent pickup/unload
waits without a controller integrate from retained navigation without creating
a new goal. Added a World regression for retained-target movement during
transfer. Full builds are running; test and multiplayer validation remain
pending for this change. Cross-compiler determinism passes at
dcef618cd2e4d558 with the usual missing-header ARM skips.

All three rebuilds completed. The new retained-navigation movement regression
passes, as do the Release and optimized full suites (45 tests each). Native
comparisons pass 4000 flight velocity/attitude-input cases and 4000 navigation
cases (/tmp/detached-flight-native-motion.log). Debug full-suite and repeated
local multiplayer runs remain in progress; their logs are
/tmp/detached-flight-debug-tests.log and /tmp/detached-flight-network.log.

Both completed: Debug passes all 45 tests in 68.63 seconds; the two ten-second
local multiplayer runs both yield 83a52a301ae12a8b. The harness stopped both
servers. This validates the integration change and regression behavior, not
complete native route or visual parity.

### Full flight-arrival boundary path coverage (2026-09-22)

Extended probe_transport_flight_arrival.py through native construction,
navigation, notification and retention at one 16.16 quantum inside, exactly
on, and outside radii 16/116/149/500. Both X/Z directions, both signs and
three heights cover 288 cases across point and pursuit controllers. Arrival
is strictly inside the horizontal radius, independent of altitude. Arriving
point controllers post 0x100/0x400 and detach; arriving pursuits post 0x100
and stay installed. Outside/boundary cases emit no events and retain their
controller. All pass against the compiled shared radius predicate using the
existing pickup-pursuit CLI. This checks the shared predicate for both native
constructor variants; it does not run a compiled full navigator through that
CLI. Logs /tmp/transport-flight-arrival-{boundaries,release,debug}.log.
No engine behavior or binaries changed for this extension.

### Legacy script codes 0/1 use vertex-ended emitters (2026-09-22)

Re-ran archived literal inventory: 268 script versions, 2386 EMIT_SFX sites,
1152 direct literals, all codes 2/3/4/5. No direct literal code 0/1 use appears;
computed runtime values are not excluded by this inventory. Log
/tmp/legacy-sfx-inventory.log.

Extended probe_script_sfx_dispatch.py with 2048 legacy code 0/1 cases through
native 50da20 and 502a70, sinking final manager creation at 502580. Visible
emissions refresh the model and pass the first two transformed piece vertices,
with wrapped body X/Y addition and Z subtraction, count 1, kinds 6/7 and
group 7. Invisible emissions neither refresh nor create. All cases and the
existing 9216 extended-code cases pass (/tmp/legacy-sfx-native-dispatch.log).
Current sfxAnimFor's generic large-flame fallback does not represent this
two-endpoint dispatch. Keep that mismatch open until the 502580 object's
initialization, update and draw are traced; no speculative remapping was made.

### Legacy emitter initialization and duration correction (2026-09-22)

Added probe_legacy_emitter_init.py: 1024 native 504420 initialization cases,
with only the initial virtual spawn operation substituted. Both endpoints are
preserved, the deadline is current tick plus 6/7, endpoint stepping uses the
truncated 16.16 reciprocal of that duration, and initialization calls spawning
immediately. All pass (/tmp/legacy-emitter-init.log).

Correction to the preceding dispatch entry: its fourth argument 6/7 is a
duration, not an emitter kind. Updated the dispatch probe terminology and
reran it successfully. The actual particle generation, lifetime and draw
remain unverified; one initial spawn-method call does not imply one particle.
No runtime remapping or binary rebuild was performed in this step.

### Legacy emitter spawning and lifetime (2026-09-22)

Added probe_legacy_emitter_lifetime.py. Native initializer, spawn operation,
vector insertion/copy, update, readiness and removal all execute. Only memory
allocation/free and animation-frame-count lookup are sinks, using a synthetic
eight-frame resource. Six timelines (durations 6/7 at three starting ticks)
yield 192 verified particle snapshots.

Initialization reserves duration+1 particle slots and spawns one particle.
Readiness admits another spawn per tick through the inclusive deadline. Each
particle starts at the first endpoint, retains the second, advances by the
stored 16.16 endpoint step, and cycles frames using the native stored modulus
(frame-count minus one). All share the emitter deadline and are removed when
the current tick exceeds it. Allocation does not recur in these timelines.
Log /tmp/legacy-emitter-lifetime.log. This adds combined spawning/update/removal
coverage to the earlier individual particle probes. The earlier draw and
backend entries already establish 5042e0 projection and software blitting
through 536e90; those should not be retraced or mistaken for Glide submission.
Remaining gates are the game+174b4 animation resource and whether/how this
software path is presented during actual Glide gameplay. Runtime generic-flame
fallback remains unchanged.

### Runtime emission inventory and live renderer check (2026-09-22)

The animation roster now counts EMIT_SFX callbacks in the normal 1200-tick
display timeline and six death scenarios per script. All 202 scripts pass in
optimized, Release and Debug builds with identical counts:
2=14633, 3=9521, 4=809, 5=448, 257=992, 258=136, 260=1411,
261=715, 262=326, 263=8, 264=885, 265=681. No code 0/1 occurred
in these exercised scenarios. This narrows priority; it does not prove those
computed codes unreachable in all gameplay. Logs are
/tmp/runtime-sfx-inventory.log and /tmp/runtime-sfx-{release,debug}.log.
Only the roster test was rebuilt; no engine behavior changed.

Revalidated the existing Proton retail process (PID 2429535), raised its
Kingdoms window, and captured /tmp/retail-parity-raised.png. It is still at
the retail 4.0BB title screen. Its current module mappings contain DirectDraw
and WineD3D but no Glide-named module. This is not an in-game renderer check
and does not establish which gameplay renderer is configured. Full paired
Glide gameplay captures remain outstanding.

### Proton UI input recovery (2026-09-22)

After raising the existing window with wmctrl, the Win32 helper launched via
Proton runinprefix reports the correct 640x480 client size. PostMessage mouse
events in client coordinates work: (320,300) enters campaign, Escape skips
the introductory video, (140,375) cancels campaign selection, and title
(120,300) opens the skirmish lobby. Captures:
/tmp/retail-parity-after-escape.png and /tmp/retail-parity-skirmish.png.
The XTest desktop-coordinate approach still did not trigger the menu.
Scratch helper /tmp/tak-retail-input.c/.exe adds Escape to the earlier helper.

Clicking lobby (562,431) was followed by process exit: PID 2429535 and all
Kingdoms processes disappeared. The launch log contained no reason. This
does not establish a crash cause or renderer selection. Relaunched the same
isolated prefix with PROTON_LOG=1, PROTON_LOG_DIR=/tmp and
SteamGameId=tak-parity; stdout log /tmp/tak-retail-parity-relaunch.log and
Proton log /tmp/steam-tak-parity.log. In-game Glide capture remains unverified.

### Match-start crash captured under Proton (2026-09-22)

Repeated the lobby start action with Proton logging. Retail terminates with
an unhandled access violation at EIP 0x4b0. Its stack contains consecutive
display-mode triples (width, height, 16-bit depth), including 1920x1440,
2048x1152, 2048x1536 and 2560x1080. No Glide module appears in the crash
module list. Saved /tmp/retail-match-start-crash.log. This suggests display
mode enumeration corruption but does not yet prove its origin.

Next experiment launches through Proton explorer.exe
/desktop=TAKParity,1024x768. The input helper must also be launched through
explorer.exe on that desktop. It successfully opens the skirmish lobby and
starts the transition. PID 2971767 remains alive after the point where the
fullscreen run exited, with no unhandled exception recorded yet. The capture
/tmp/retail-desktop-loaded.png is blank except the desktop background, and
no Glide module is mapped. This is an ongoing experiment, not a successful
gameplay capture or proof the crash is fixed. Launch stdout is
/tmp/tak-retail-desktop.log; Proton log /tmp/steam-tak-parity.log.

### Explicit Glide selection in capture prefix (2026-09-22)

The virtual-desktop run remained alive but repeatedly reported incomplete
WineD3D OpenGL framebuffers and never mapped Glide. Stopped only the two
isolated retail game processes (2971767/2971753). ChooseRenderer.exe exits
while the game is running; once it was stopped the chooser opened normally.
Win32 child-control inspection shows four devices: Glide 3.x, two Direct3D
entries and Software MMX. Saved the selected Glide entry using its OK button.
The resulting authoritative prefix user.reg now contains
HKCU/Software/Cavedog Entertainment/Kingdoms/RendererOptions with
RendererType=4, RendererPrefs=0x1000e and the Voodoo Banshee/Voodoo3/Velocity
renderer name. Before this operation no RendererOptions key was present.
Scratch control inspector is /tmp/retail-chooser-inspect.c/.exe.
This verifies configuration, not successful in-game Glide presentation;
the next launch must still verify the loaded module and rendered frame.

### Glide gameplay module confirmed (2026-09-22)

Launched normal Proton fullscreen after saving RendererType=4. Current
retail PID 2973087 reaches gameplay without the former startup crash.
/proc/2973087/maps confirms assets/game/glide3x.dll and the prefix d3d9.dll
loaded; /tmp/steam-tak-glide.log records native Glide module loading.
Capture /tmp/glide-start.png shows the in-game HUD and minimap. Clicking
(606,32) selects Elsin and displays his construction icons and mana storage;
/tmp/glide-monarch.png records this. The world view remains black. Double
click and right-click on that minimap point did not visibly recenter its
yellow camera rectangle; /tmp/glide-right-center.png records latest state.
Thus backend activation and responsive gameplay UI are confirmed, but no
usable terrain/unit image yet. The initial assumption that the black view
was solely unexplored terrain is unconfirmed. No parity claim follows.
Launch log /tmp/tak-retail-glide.log; live exec session 3035.

### Usable live Glide gameplay capture (2026-09-22)

Resolved the black viewport as a camera-position issue in this run. Win32
SetCursorPos plus mouse_event provides actual cursor input, unlike posted
WM_MOUSEMOVE alone. Setting the cursor to client (1,200) edge-scrolls left
from the unexplored area into Elsin's starting area. Capture
/tmp/glide-edge-scroll.png shows terrain, Elsin, selection ring and a
model-shaped shadow with Glide3x still mapped in PID 2973087. Moved cursor
to (300,200) afterward to stop scrolling; additional capture
/tmp/glide-stable-reference.png and module list
/tmp/glide-reference-modules.log. The screenshot validates usable retail
Glide presentation, not animation parity with our client.

Scratch helper /tmp/retail-cursor.c/.exe takes client x y and optional click,
uses ClientToScreen, SetCursorPos, and a 500ms mouse_event left press. Right
variant /tmp/retail-cursor-right.exe is available. Neither minimap press
visibly centered the camera in this experiment; edge scrolling did.
Current map is Abnar's Terrace, human Aramon versus Taros AI, default
non-Crusades settings. No engine code changed.

### First live Glide construction sequence (2026-09-22)

In the live Abnar's Terrace match, selected Elsin's barracks icon at client
(350,405), then placed the site at (170,190) using the physical-cursor helper.
/tmp/glide-construction-1.png captures early Intangible Mass construction.
Twelve subsequent full-screen frames are stored in
/tmp/glide-barracks-sequence/frame-00.png through frame-11.png, with monotonic
capture start/end timestamps in frames.tsv (about 13 seconds total).
Inspected first and last frames: the initially opaque yellow-green animated
construction surface becomes more transparent and reveals the barracks
texture, with yellow sparkles distributed over its silhouette and around
Elsin. Elsin stands to the site's right, oriented toward it, with sword raised.
The sequence is a live Glide reference; no synchronized client comparison
or frame/tick alignment has yet been performed. Spectacle captures take
roughly 0.8 seconds plus 0.3-second spacing, so this is unsuitable for exact
per-tick timing claims. Existing TAK_CONJURE_TEST is specifically Zhon
monarch-to-Hunter, so it cannot serve directly as the paired Aramon fixture.

### Configurable client conjuring fixture, integration still failing (2026-09-22)

TAK_CONJURE_TEST now accepts optional TAK_CONJURE_BUILDER and
TAK_CONJURE_TARGET names, validates them, and searches nearby legal placement
cells for an explicit target. Original Zhon defaults remain. All three
client builds succeed; git diff --check passes. No simulation code changed.
However araking/arakeep captures on Abnar's Terrace (10/20 seconds) and
Ulasem Arena (10 seconds) show Elsin standing without a construction site.
The added canPlace search alone did not resolve this: placement acceptance
does not establish builder access or successful mission execution. Latest
/tmp/client-aramon-legal.png and .log, build logs
/tmp/conjure-site-{build,build-dbg,build-o2}.log. Treat the configurable
fixture as unfinished until the failed queue/approach is diagnosed. Do not
use these images as evidence of a missing production construction effect.

### Aramon fixture queue admission verified (2026-09-22)

Added diagnostic output confirming queueBuild accepts araking/arakeep
(orders=1). Configurable builder now reuses the matching local starting unit
when available, avoiding the original hard-coded ground spawn. Latest run
uses araking #1 at (592,720), barracks site (720,720) on Abnar's Terrace.
Even from that starting position, 20-second capture remains Standby with no
site visible: /tmp/conjure-start.png and .log. Therefore the initial invalid
spawn hypothesis is insufficient; next inspect build approach events and
order removal in the simulation. Do not claim the fixture fixed. All three
clients rebuilt successfully; no simulation edits. Logs
/tmp/conjure-start-build.log and /tmp/conjure-start-{build,build-o2}.log.

### Ground construction approach exposes placement conflict (2026-09-22)

Extended existing --trace output under TAK_CONJURE_TEST with orders, mission
events/stage/wait masks and target footprint. Trace on Abnar's Terrace:
Elsin moves from (592,720) toward (656,720), reaches (648.4,720.2), and
the accepted barracks order disappears between ticks 31 and 47. No site
is allocated. Barracks footprint is 7x12. Logs /tmp/conjure-trace.log and
/tmp/conjure-footprint.log. This disproves the prior impression that Elsin
never moved: the late screenshot showed him stopped after approach.

World::canPlace currently rejects any live unit within
8*max(footX,footZ)+12 of the target center. For this barracks that is 108
pixels, larger than the approach's distance from the center (~72 pixels).
startBuild reruns canPlace after approach, so the builder's own position
can veto its build. This is a concrete conflict between the rectangular
approach and circular placement predicate, not an animation defect. Need
a focused regression and retail building-placement evidence before changing
this shared sim predicate. Existing check_mobile_placement.py explicitly
excludes building placement and cannot establish the required semantics.
All three client targets rebuilt; diff whitespace check passes. No shared
simulation change yet.

### Flat-map barracks regression isolates side-dependent rejection (2026-09-22)

Added an end-to-end conjure_test regression with real araking/arakeep types,
flat 128x128 terrain and four starting sides, for both balance modes. All
eight orders are accepted. North/south approaches allocate a construction
site; east/west approaches fail in both modes (four expected regression
failures). This isolates the failure from map obstacles, client rendering
and fixture spawn placement. /tmp/barracks-regression.log; optimized test
built successfully. The test is intentionally red pending the simulation
fix; the suite must not be reported fully passing in this state.

Existing native check_build_reach.py stops at 405824 before placement and
allocation. That boundary is the next investigation point for resolving
the builder-versus-building placement semantics without altering movement.

### Native building occupancy boundary verified (2026-09-22)

Traced MobileBuild beyond 405824: it calls 507d10 with self ID zero, then
retries with moving-entity acceptance enabled if placement fails. For a
structure (type+24a zero), 507d10 calls building placement 507400.
Added probe_building_occupancy.py executing both routines with no substituted
routines. Synthetic flat cells, footprints 7x12/12x7/1x1/3x5, per-cell
masks 0/2/4/6/14, six inside/outside occupancy locations and moving/allow
combinations yield 480 passing cases. Outside cells never block; inside
cells block only for occupancy-tested mask bits 2/4, with the moving-unit
exception. Log /tmp/native-building-occupancy.log.

This establishes native footprint-scoped occupancy rather than our circular
exclusion. It does not yet verify authored yardmap mask decoding, complete
terrain restrictions, unit-to-cell population, or the full retry timeline.
The existing flat-map construction regression remains red until the shared
simulation placement check is corrected and rebuilt/tested.

### Rectangular building placement correction validated (2026-09-22)

World::canPlace now uses existing searchBodyRect occupancy for structures,
skipping '.' yard cells, instead of the largest-dimension circular unit
exclusion. Mobile placement remains unchanged. This reuses the existing
body-to-cell eligibility and updates; no routing or movement algorithm changed.
The four-side barracks regression now passes in both balance modes. Added
checks that a builder beside the short side is accepted while a swordsman
inside the footprint blocks placement. Client capture
/tmp/conjure-placement-fixed.png now shows Elsin actively conjuring a
barracks, resolving the previously failed fixture. It is not yet a matched
retail frame: viewpoint, scale and build progress differ.

All targets rebuilt in optimized, Release and Debug. All 45 CTests pass in
each (9.27/8.99/70.11 seconds). Optimized build was refreshed after the last
regression additions and conjure_test rerun successfully. Native occupancy
probe still passes 480 cases. Deterministic math hash dcef618cd2e4d558; ARM
leg skipped for missing target headers. Two local multiplayer runs match
83a52a301ae12a8b. Logs /tmp/building-placement-*; network harness reused
/tmp/detached-flight-network.py so its per-client/server files were overwritten.

Remaining placement fidelity: synthetic native mask coverage does not verify
all authored yard symbols against native mask decoding or complete placement
retry/clearance behavior. This correction addresses the demonstrated circle
versus footprint defect, not those remaining semantics. Full transport and
animation parity work remains open.

### Construction capture snapshot corrected; visible sparkle gap (2026-09-22)

TAK_CONJURE_TEST fast-forward now uses the same beginFrame/cosmeticStep/
animFrame/endFrame path as projectile captures, pinning each freshly published
snapshot rather than animating against a stale front buffer. All three client
targets rebuilt; /tmp/conjure-snapshot.png verifies the active Aramon fixture.
This capture still has sparse anchor sparkles, unlike the live retail Glide
barracks sequence. Inspection finds sprinkleBuildFx explicitly draws one TAF
cluster at the unit anchor and discards footprint dimensions, justified by a
comment claiming retail uses one cluster. Live retail evidence contradicts
that claim for the building effect; do not restore an arbitrary tiled grid.

The simulation already owns verified RetailConstructionEmitter particles
(Unit::constructionEmitter, retailconstructionparticles.h), updates them at
sim.cpp:8090 and hashes them. No constructionEmitter reference exists in
the client. Next inspect native draw 4f13xx and publish/render these particles
with their verified projection and animation behavior. Existing simulation
particles provide the appropriate source rather than invented random visuals.

### Construction particle draw projection and partition (2026-09-22)

Added probe_construction_draw.py: 2048 cases execute native 4f1540/4f15f0
list traversal and projection. Renderer state acquisition/reset, sprite
submission 536400 and submission-result handling are sinks. The first draw
accepts relative Z<0; the second accepts Z>=0 (zero included). Coordinates
add particle offsets to owner position with 32-bit wrap, extract signed
integer words, and project X-cameraX, Z-(Y>>1)-cameraZ. Sprite submission
receives each particle's own animation state and three zero options.
Log /tmp/construction-draw-probe.log passes. This establishes two partitions
and position behavior, but not their scene call order, animation frame
initialization/clock, final Glide state, or pixel parity. Those remain needed
before replacing the anchor-only client sparkle with the sim particle list.

### Construction particle animation clock composed with lifecycle (2026-09-22)

Added probe_construction_animation.py. Executes 4f1310 startup and 4f12d0
updates through real 537390/5373d0 and frame-duration lookup, without routine
substitutions. Synthetic 12x2-tick, variable-delay and zero-delay resources,
looping/nonlooping and rising/falling variants yield 780 passing snapshots.
Each particle initializes at frame zero, with its own authored duration
countdown, then advances once per live particle update. Existing generic
clock probe covers the clock itself; this new probe confirms construction
actually uses it in startup/update. Log /tmp/construction-animation-probe.log.

Client anchor fallback currently uses shared animClock_*15, so replacing only
positions is insufficient. Particle identity/age or animation clock must be
carried to the snapshot to preserve independent start phases. Simulation
RetailConstructionParticle currently retains position/speed/ceiling only.
Scene call order and final sprite backend still require verification.

### Construction draw probe correction and scene call sites (2026-09-22)

Corrected probe_construction_draw.py: 536400 is a one-argument current-frame
resource lookup, not sprite submission. Actual submission is 4fac00 with six
arguments. The previous sinks collectively consumed the right stack size but
assigned the arguments to the wrong boundary. Now 536400 executes natively
against twelve synthetic frame pointers; only 4fac00 is the sprite sink. All
2048 projection/partition cases pass and now check the correct selected frame
resource too (/tmp/construction-draw-corrected.log). Previous descriptions of
536400 as submission are superseded.

Static caller inspection locates 4f1540 at 4ecce5 and 4f15f0 at 4ed168, both
using the render object's +174 emitter. These bracket the model-rendering
section; the first follows a shadow call at 4eccd6. Full surrounding native
scene execution is not yet captured, so this is call-site evidence only.
4fac00 handles composite sprites and renderer-specific submission; final
blend/texture state remains a separate gate. No engine behavior changed.

### Construction draw composed through sprite state selection (2026-09-22)

Added probe_construction_sprite.py composing native particle traversal,
projection, per-particle frame lookup and full 4fac00. Only renderer state
acquisition/setters and final primitive submission are sinks. A code-entry
observer records the real submission arguments without replacing the call.
2048 cases pass with synthetic 42x45, anchor (19,21), unflagged encoding-4
frames: exactly one four-vertex quad per admitted particle, state19=5,
state20=2 and state27=1. Log /tmp/construction-sprite-probe.log. This
composes the previously separate draw and sprite-state probes; it is not
final Glide API/pixel verification and uses synthetic frame metadata.
Existing effectFor already supports this additive blend selection. The
remaining visible integration is particle snapshots, independent clock
phase and placement around the model; no engine change made this step.

### Construction particle snapshot wiring (2026-09-22, validation running)

Added cosmetic displayAge to RetailConstructionParticle, initialized to zero
and incremented only when its live update advances position. It is excluded
from stateHash; particle motion, admission, expiry and CRT consumption are
unchanged. UnitR now copies the particle vector at snapshot publication and
clears it when no emitter exists. Added lifecycle checks that age advances
to one and resets to zero when a freed slot receives a new particle. No draw
replacement yet; this is the data prerequisite for independent animation.

All-target builds plus CTest launched in three directories (header affects
shared code). Logs /tmp/construction-snapshot-{o2,build,build-dbg}-build.log
and corresponding -tests.log. Exec sessions 96914/59292/88395 respectively
were live when this entry was written. Determinism session 17454 logs to
/tmp/construction-snapshot-determinism.log. Revalidate completion before
running any executable or claiming validation. No screenshot change expected
until the particle draw passes are added.

### Initial construction particle draw integration; ordinary-match gate found

Snapshot all-target builds and all45 CTests pass in optimized/Release/Debug
(9.90/9.52/71.54s), deterministic math hash unchanged. Added particle draw
lambda around model vertex runs, using owner-relative projected offsets,
signed-Z partition, authored EffectAnim delays/loop and per-particle age.
Anchor fallback remains when the particle vector is empty. All three clients
rebuilt after draw edit; logs /tmp/construction-draw-{client-build,build,build-o2}.log.

Capture /tmp/construction-particles-first.png still shows the old sparse
effect. Inspection identifies why: World::spawn initializes constructionEmitter
only when retailAllocation_ and the player's buildCache exist. Ordinary
match setup does not populate this path. Thus draw integration is compiled
but not demonstrated by this capture; no visible-fix claim. Need ordinary
match emitter initialization/emission without inventing gameplay RNG changes,
and emitter-presence distinction so empty active emitters do not flash the
anchor fallback. Animation evaluation currently replays age ticks per draw;
optimize/cache this before calling it complete. Native draw/clock probes remain
the behavioral reference. Network repeat check for the cosmetic-age header
change is still outstanding.

### Construction emitter geometry initialization observed

`tools/re/probe_construction_emitter_setup.py` executes native sizing slice
4ee377..4ee46f without substitutions: 216 cases pass. Radius derives from
half the model X/Z extent: radial length normally, smaller half-extent when
owner flag byte +133 has bit2 set. Height is upperY minus lowerY. Capacity
starts at the signed integer highword of radius, divided by four when type
byte +24a equals1. The enabled quality reduction divides capacity by
min(6,(target-current)/2), clamping to at least1, only when current+1<target.
The probe includes zero, fractional and rectangular dimensions, both radius
branches, three mobility-byte values and six quality combinations.

This establishes sizing arithmetic with synthetic bounds; it does not yet
establish model-bound provenance, the meaning of the special owner flag,
outer allocation lifecycle or ordinary-match emission cadence. No simulation
or renderer behavior changed in this step. Next integration must resolve
those inputs and keep cosmetic RNG independent of gameplay unless native
shared ordering is deliberately reproduced and verified.

### Empty emitter fallback corrected

UnitR now snapshots emitter presence separately from its particle vector.
Both construction-site and builder legacy anchor fallbacks require no emitter,
so a valid but empty emitter cannot flash the legacy cluster. All three
clients rebuilt successfully: /tmp/construction-emitter-presence-{dbg,release,o2}.log.
This is client-only and does not alter hashed state or emission cadence.
Ordinary matches still require emitter initialization AND emission integration:
the current World::emitConstruction callers are within restored retail-site
resource branches. Existing modelTop helper observes native 546f40 with a zero
floor per sibling list; client ring bounds deliberately omit ground geometry
and are therefore not an established substitute for native emitter bounds.

### Emitter horizontal bounds provenance correction

Native type initialization 4c1410..4c1494 establishes X/Z bounds as plus/minus
8 times footprint X/Z in fixed point; bottom Y is zero. With 48d070 mode query
false, model top Y is preserved. `probe_construction_type_bounds.py` executes
that slice with only the mode query substituted: 36 footprint combinations
pass. This corrects the earlier description of horizontal extents as model
geometry: emitter sizing reads type bounds, whose initialization is footprint
based. Type +14a is separately written by 546f40 model-top loading (observed
call site 4fd19c). Later modifications/legacy mode still need qualification.
The nearby 546fa0 general model bounds helper is used for other purposes;
its existence does not prove construction emitters consume its output.

### Construction sprite frame sampling bounded by frame count

Replaced per-particle per-draw replay of every age tick with client-only
retailEffectFrame: sum effective authored delays (zero occupies one tick),
reduce looping age modulo total duration, then locate the frame. Non-looping
expiration and empty resources remain invisible. Runtime cost now depends on
resource frame count, not particle age. retail_visual compares against the
single-tick clock for 2.4 million states spanning empty, zero-delay, variable,
long-delay, looping and non-looping resources. Debug, Release and optimized
client/test targets rebuilt; retail_visual passes in each. Native construction
clock probe rerun passes 780 snapshots. No shared simulation changes.
Ordinary-match emitter creation/emission and the broader visual/transport
parity work remain open.

### Pending multiplayer check completed; ordinary emission hook identified

Repeated current Debug client/server local multiplayer harness completes twice
with hash 83a52a301ae12a8b; /tmp/construction-clock-network.log. This closes the
pending repeatability check after construction particle displayAge/snapshot
changes, not full native construction or transport parity. The prior broader
check_construction_emitter_profile.py also reruns successfully (4096 cases);
its structure flag input corresponds to owner 0x02000000 and its movement
byte is bmcode, already documented in decode_player_cache.py. Avoid treating
canMove as interchangeable: registry combines bmcode with canmove and does
not retain the original byte.

Ordinary placed construction admits work after the mana check in
World::tickConstruction; queued production admits work in tickProduction
before its early progress return. A future cosmetic work event must be emitted
at those admission points, including the completing tick. Snapshot conjuring
alone is insufficient: it persists on mana-starved ticks and disappears on
completion. This rules out using that boolean alone for faithful emission.

### Ordinary construction work event plumbing (validation pending)

Added cumulative falling/rising constructionEmissions counters to Unit and
UnitR, copied at snapshot publication and intentionally excluded from stateHash.
World::emitConstruction increments them before the optional restored emitter
check. Ordinary placed construction calls it for builder and site after work
admission; ordinary queued production calls it for the site before its early
progress return, including instant completion. Existing restored emitter
callers now publish the same counters. No cosmetic RNG is consumed when the
optional emitter is absent. Renderer consumption remains to be implemented.

conjure_test adds funded work-event assertions for each barracks approach and
checks that changing the cosmetic counter leaves stateHash unchanged.
All-target builds launched in build/build-dbg/build-o2, logs
/tmp/construction-events-{build,build-dbg,build-o2}.log, live exec sessions
34211/75006/96766 respectively. At this entry builds are still running;
re-poll those handles, do not restart blindly. Tests, final build results and
network checks remain pending. Counters retain multiplicity across skipped
snapshots but do not retain individual event timestamps; any renderer catch-up
implementation must account for that limitation rather than claim exact
particle age for skipped ticks.

### Construction event validation progress

All-target builds 34211/75006/96766 finished successfully. Release and optimized
CTest suites pass all45 (6.19/6.58s); direct optimized conjure_test against retail
assets passes including 24 new work-event/hash assertions across balances and
approaches. Determinism math guard/golden passes dcef618cd2e4d558; ARM compiler
variants skipped for unavailable target headers. Logs use
/tmp/construction-events-{build,build-o2}-tests.log,
/tmp/construction-events-conjure.log and /tmp/construction-events-determinism.log.
Debug full CTest remains live session46372; local network repeat harness remains
live session14542, logs /tmp/construction-events-build-dbg-tests.log and
/tmp/construction-events-network.log. Poll before starting any replacement.
Renderer event consumption still open. Current event checks do not yet exercise
mana starvation or exact final-tick counts; add those with the renderer hookup.

### Construction event boundary validation complete

Debug full CTest session46372 completed: all45 pass, 51.99s. Network session14542
completed: both hashes83a52a301ae12a8b. Extended actual World barracks fixture
with a zero-income copy of the builder type to isolate mana admission. All
four approach directions in both balance modes prove no events during a
zero-mana tick, and exactly one worker/site event on the tick that finishes
construction (site health set one fixed-point unit below maximum). The fixture
also checks completion actually occurs. All three conjure_test builds/runs
pass; /tmp/construction-event-boundaries-{test,build-test,build-dbg-test}.log.
Only test code changed in this step; the all-target runtime builds remain
current. No pending tool processes from this validation. Renderer hookup and
queued production-specific boundary coverage still remain.

### Ordinary client construction particles connected (build/capture pending)

GameView now maintains per-unit cosmetic ConstructionFx emitters, consumes
published work counters once per snapshot generation, advances surviving
particles by gameTick delta, and draws them through the existing native
projection/frame path. Restored authoritative emitters take precedence.
Ordinary sizing uses footprint half-extents and modelTop with full quality;
structure classification currently uses isStructure and nonstructures capacity/4.
Each cosmetic emitter has a private CRT-form stream seeded from unit id, so
no gameplay RNG calls change. Removed construction anchor fallback; reclaim
fallback remains. Dead/absent/restored units retire cosmetic map entries.

Known incomplete details: skipped snapshots bunch newly observed events at
current tick (counters lack timestamps); raw bmcode is not retained, so exotic
mobility codes may need classification correction; initial late snapshots can
replay old cumulative counts; adaptive quality is not integrated; authored
side/resource mapping and paired Glide capture remain to verify. Do not claim
full particle parity. Cleanup unused conjuring local and now-dangling conjure
comments in drawUnit (compiler reports unused-variable warning).

Client builds running sessions37647/40484/92784 for Debug/Release/optimized,
logs /tmp/construction-client-events-{build-dbg,build,build-o2}.log. Poll handles
before replacement builds. No visual capture/test has yet validated this hookup.

### Ordinary construction visual hookup demonstrated

Initial client builds37647/40484/92784 finished successfully. Headless fixture
captures /tmp/construction-client-events.png (araking/arakeep,20s) and
/tmp/construction-client-zhon.png (zonhunt/zonter,8s) execute successfully and
were visually inspected: distributed construction sprites appear around both
builder and site; purple sprites are visible around Thirsha herself. These
prove ordinary-match draw hookup, not matched Glide pixels, cadence or density.
Both captures are fixture zoom3 and not a synchronized retail comparison.

Removed unused conjuring local and dangling construction-fallback comments.
All three cleanup client rebuilds finish successfully, logs
/tmp/construction-cleanup-{build-dbg,build,build-o2}.log. No live build/capture
handles remain from this step. Known skipped-snapshot/timestamp, initial
counter replay, bmcode classification and quality limitations above remain.

### Replace snapshot counter catch-up with tick-owned cosmetic particles

Removed ConstructionFx client map and its per-snapshot catch-up. Ordinary
World::emitConstruction now lazily creates a separate cosmetic emitter with
its private CRT-form stream, emits at the actual admission tick, and advances
it alongside the restored emitter at the existing per-unit tick phase.
Snapshot publication copies its current particles (restored emitter takes
precedence). This eliminates initial cumulative replay and skipped-snapshot
bunching without storing a bounded event history. Cosmetic emitter and random
state are not included in stateHash or used by gameplay. Counters remain for
work-event diagnostic coverage. Full-quality footprint/modelTop sizing and
isStructure classification remain as before; raw bmcode fidelity still open.

All-target rebuilds running: build80359, Debug44219, optimized33637;
/tmp/construction-tick-owned-{build,build-dbg,build-o2}.log. Poll existing
handles before replacement. Runtime validation and stronger tests (cosmetic
emitter hash independence and age progression without rendering) required
next. Existing captures demonstrate prior hookup, not this updated timing.

### Tick-owned cosmetic construction validation

All-target builds80359/44219/33637 finish successfully. Extended conjure_test
passes in all3 builds: headless work creates the emitter, removing its contents
and changing its private RNG leaves stateHash unchanged, and a mana-starved
headless tick advances positions/lifetimes/displayAge exactly once without new
emissions. Prior starvation/completion checks still pass. Logs
/tmp/construction-tick-test-{build,build-dbg,build-o2}-run.log. Math determinism
passes dcef618cd2e4d558 (same unavailable ARM variants skipped).
New /tmp/construction-tick-owned-zhon.png capture completes and was inspected:
particles remain visible around Thirsha and the conjured Hunter with tick-owned
updates. This remains a client-only capture, not synchronized Glide parity.

Full CTests now running sessions16598(build),40838(Debug),74555(optimized),
logs /tmp/construction-tick-owned-{build,build-dbg,build-o2}-tests.log.
Network repeat check still session59901, /tmp/construction-tick-owned-network.log.
Poll these existing handles next. No build or capture processes remain live.

### Tick-owned suites complete; construction mobility classification corrected

All45 tests pass in Release/Debug/optimized (6.13/49.93/6.55s); repeated local
network hashes83a52a301ae12a8b. Prior pending sessions all terminated normally.
Now retain authored bmcode byte as UnitType::buildMovementCode, solely for
construction visuals: code0 uses the smaller horizontal half-extent, all other
codes radial length; only code1 divides particle capacity by4. Existing
canMove/maxVel/isStructure movement rules remain untouched. This removes the
previous unsupported assumption that every nonstructure has native code1.

All-target builds launched for this header change: sessions58829(build),
98812(Debug),95938(optimized), logs /tmp/construction-mobility-{build,build-dbg,build-o2}.log.
Poll before replacing. Tests/build completion pending. Also identified next
asset-mapping audit: both current builder draw and reclaim fallback map Creon
to zhonbuild even though inventory requests creonbuild. Verify actual authored
faction effect definitions before changing that mapping; no correction yet.

### Creon construction art corrected from mounted faction data

hpitool where resolves gamedata/sidedata.tdf to IPData.hpi. SIDE7 explicitly
sets buildsparklygaf/buildsparklyanim and resurrect equivalents to creonbuild;
SIDE3 sets zhonbuild. Archive includes creonbuild_1555.taf and _4444.taf.
Corrected both ordinary construction particle draw and existing sparkle
fallback mapping from Creon->zhonbuild to Creon->creonbuild. Other mappings
agree with the authored prefix definitions. General arbitrary sidedata
bank/sequence support is still hardcoded and remains a fidelity limitation.
Effect inventory completes successfully (/tmp/construction-creon-inventory.log).

Mobility all-target builds58829/98812/95938 complete successfully; all3 clients
rebuilt after art correction (/tmp/construction-creon-{build,build-dbg,build-o2}.log).
Release/optimized all45 CTests pass7.46/7.35s. Debug CTest remains live98446,
/tmp/construction-mobility-build-dbg-tests.log. No other build handles remain.
No Creon gameplay screenshot captured yet. Native emitter/adaptive quality
oracle remains the existing4096-case check; full synchronized Glide parity
and broader transport/animation requirements remain open.

### Creon runtime capture and mobility Debug suite completed

Debug CTest98446 completes all45 successfully (52.92s). Creon fixture
cresage->crebarn at12s produces /tmp/construction-creon-capture.png; inspected
blue faction particles at the builder/site, confirming the corrected art loads
and draws. Not a synchronized native capture. /tmp/construction-creon-capture.log
records accepted construction order and successful screenshot.

Removed obsolete sprinkleBuildFx comment claiming native effects are a single
anchored cluster. Current callers are only the two reclaim draws in drawUnit;
those are still legacy fallbacks and need separate emitter verification.
Comment-only client rebuilds10752/86328/47519 complete successfully in all3
builds (/tmp/construction-comment-{build,build-dbg,build-o2}.log).
No pending processes from this step. Broad transport, roster animation and
Glide visual comparison scope is still incomplete.

### Repeated coastal transport lifecycle roster coverage

Extended retailRoster to reuse the same carrier/passenger after the initial
unload: reboard through loadInto, travel back to a second coast destination,
unload, then require empty cargo and passenger on land. This exercises stale
attachment/mission cleanup beyond a single successful journey. All27 carrier
balance combinations (13standard,14Crusades) pass in all3 builds, including
air/sea and authored NPC carriers. Logs /tmp/transport-second-trip-test.log,
/tmp/transport-second-trip-{build,build-dbg}-test.log. No runtime code changed.

Reexecuted native unload probes: 100air and62sea controlled-placement cases,
36PARK radii each, and4096 fixed-point range comparisons per run pass. Outputs
/tmp/transport-current-{air,sea}.{json,log}. These still control placement and
navigation sinks; they do not establish full native coastal/distant/blocked
World timelines. All tool handles from this step completed normally.

### Native transport draw frame lookup no longer substituted

Removed 536400 fixed-frame sink from probe_transport_effect_projection.py.
The native scene draw now reads each effect's animation state and resolves its
actual frame pointer through the native lookup (17 synthetic frame resources).
4096 coordinate/frame cases pass; hidden draws preserve node state;4096 direct
projected fog cases and compiled visibility comparisons pass. Existing native
create/update clock probe rerun passes256 complete timelines. Logs
/tmp/transport-native-frame-{projection,clock}.log. These probes still sink
raster submission and one visibility branch and do not prove rendered Glide
pixels. Only probe code changed; no runtime rebuild required. All handles
completed normally.

### Shared authored frame sampler for transport/explosion draw and expiry

updateEffects and drawEffects now use retailEffectFrame(durations,false,age)
for authored-timing sprites, including transport effects. Both draw and expiry
therefore use the same tested zero-delay/nonloop boundary semantics. The draw
path no longer computes a floating playback loop before replacing that result
with the authored frame. Loader fills durations and frames together from each
retailDelayTicks; inspected that invariant. Non-authored effects unchanged.

All3 client/test targets rebuild successfully; retail_visual passes each,
including2.4million frame-sampler comparisons. Native transport clock rerun
passes256 timelines (/tmp/transport-shared-clock-native.log). A headless client
smoke exits successfully and saves /tmp/transport-shared-clock-effects.png;
the requested explosion fixture did not print its PASS marker under --testbuild,
so this is only client smoke, not verified explosion-fixture coverage.
Build logs /tmp/transport-shared-clock-{build,build-dbg,build-o2}.log.
All sessions complete. Full transport scene/capture parity remains open.

### Live authored effect expiration boundaries verified

Correct fixture entry is --firetest with TAK_EXPLOSION_CLASSES_TEST=1, not
--testbuild. Confirmed PASS for all9 classes and combined flags in the live
client. Extended that fixture to run updateEffects(0) for each spawned authored
resource at age0, duration-1 and duration, using retained event start ticks;
requires survival through the last frame and removal at exact duration.
The enhanced Debug client run passes both assertions and saves
/tmp/authored-expiry.png; log /tmp/authored-expiry.log. This verifies actual
client expiration with loaded retail assets, not rendered pixel equivalence.
All3 client rebuilds complete successfully (/tmp/authored-expiry-{build,build-o2}.log
and /tmp/authored-expiry-build.log for Debug). No pending processes. Only
Debug fixture coverage changed; runtime clock logic remains as prior entry.

### Transport-specific live queue and asset clock fixture

Added Debug-only TAK_TRANSPORT_EFFECT_TEST entry under --firetest. It queues
a transport event four ticks old, drains through cosmeticStep, and requires
two loaded effects with original XYZ and callback tick, authored timing and
no duplication on a second drain. Loaded mindspin/transswirl expiration is
checked at age0, duration-1, duration and duration+20 via updateEffects.
Debug and optimized Debug client runs pass; logs /tmp/transport-live-effect.log
and /tmp/transport-live-effect-o2.log. All3 clients rebuilt successfully; no
runtime behavior changed outside the explicitly requested debug fixture.
Screenshots were saved but no pixel parity claim: fixture asserts queue,
loaded-resource and expiry boundaries, not scene draw placement or native
Glide output. All processes completed normally.

### Repeatable live flame-age capture

Extended existing TAK_PROJECTILE_CAPTURE debug stop to FlameShot streams,
requiring requested shot age and nonempty particles. This makes captures
repeatable for Drake/fire and other flame kinds rather than depending on an
arbitrary end time. With TAK_PROJECTILE_TEST=zondrake and capture8, --firetest
--nofog --time20 stops at tick10, age8, kind0,8particles. Inspected
/tmp/drake-age8.png: flame stream visibly extends from Drake toward target.
Log /tmp/drake-age8.log. All3 clients rebuilt successfully; only debug capture
control changed. Native full flame draw/admission/clipping comparison rerun
passes4096 cases (/tmp/flame-current-render.log). No matched native screenshot
at this age yet, so this does not prove flame appearance/blend equivalence.
All build/capture handles completed normally.

### Retail Glide reference session revalidated and Zhon setup prepared

Observed live Proton retail PID2973087 (revalidate before reuse), responsive
menu and loaded assets/game/glide3x.dll in /proc maps. Previous Abnar match
had ended in defeat; screenshot /tmp/retail-current-scene.png proves terminal
match state, so no assumption of an ongoing reference battle. Clicked bottom
right return and cycled human side Aramon->Taros->Veruna->Zhon with the existing
Proton runinprefix /tmp/retail-cursor.exe helper. Inspected
/tmp/retail-zhon-setup.png: human Zhon versus Taros AI Matt, Abnar's Terrace,
standard balance, setup screen (match not started). Next step is a controlled
Zhon match/capture; no new flame pixel parity evidence yet. No engine changes.

### Live Glide Zhon construction reference captured

Started prepared Zhon match on Abnar's Terrace via posted click562,433.
Thirsha visible; selecting255,160/165 failed, actual click255,180 selected her
(the lower body/ground anchor). Menu icon225,405 then site350,260 starts a
Beast Handler (HUD confirms, not Hunter). Inspected
/tmp/retail-zhon-conjure.png: native hovering Thirsha and site both have purple
particles. This is direct Glide reference evidence for the two-ended effect.
Saved8 further screenshots in /tmp/retail-zhon-conjure-sequence with monotonic
capture bounds in times.tsv. These are unsynchronized wall-time samples,
not per-tick timing/paired geometry proof. Native match remains running in
PID2973087; revalidate scene before assuming construction still active.
No engine edits; Drake flame reference still outstanding.

### Retail Zhon reference continues; build-chain correction

Inspected sequence frame07: Thirsha has moved near the site with raised wings,
while current screenshot shows finished Beast Handler and settled monarch.
No per-tick matching claimed. Selected handler at350,260. First menu icon was
mistakenly interpreted as a higher builder; capture /tmp/retail-lord-working.png
HUD proves it produced a Goblin. Checked actual canbuild entries: zonhand builds
zontrain, zontrain builds zonlord, zonlord builds zondrake (priority6). Issued
handler's last menu icon480,405 then site210,200 to request Beast Tamer; current
result must be checked next rather than assumed. Live native match PID2973087
remains active; no code changes. Drake capture still outstanding.

### Retail Beast Tamer completed; Beast Lord requested

Inspected /tmp/retail-trainer-current.png and progress capture: HUD identifies
Beast Tamer under construction, then /tmp/retail-trainer-ready.png shows it
complete. Selection at (210,200) is verified. The earlier note that the Beast
Lord icon was at (30,357), priority 8, was not reliable: the fresh normalized
640x480 capture shows (30,357) is map, while the bottom palette is around
y=405; both base and Crusades TDFs specify `Priority=9`. The later
/tmp/retail-lord-status.png does confirm a Beast Lord entered construction by
the Tamer, but the intervening icon click has no input log, so its exact
coordinate remains unresolved. Resource waiting was still observed at that
time. No engine edits. Drake flame capture remains outstanding.

### Beast Lord resource wait and current flame arithmetic checks

Inspected /tmp/retail-lord-status.png: Beast Lord under construction by Beast
Tamer, confirming requested type. Later /tmp/retail-lord-progress.png shows
nearly completed Lord but zero stored mana; remaining wait is resource-driven,
not an unaccepted command. Sent6 further VK_ADD increments for setup (previous
requested speed+4; actual displayed final setting still needs confirmation).
Restore normal speed before wall-time visual comparisons.

Current native/compiled flame checks pass4096 particle cases and4096 emission
updates, plus386 native emitted velocity comparisons. Logs
/tmp/flame-reference-{particle,emission}-check.log. Native owner live/dead
emission gates, draining, muzzle origin, per-tick count, random draws and
velocity spread covered; no full shot pixel parity claimed. No runtime edits.
Retail match PID2973087 remains active and should be rechecked next.

### Retail reference setup invalidated by attack; empty-opponent restriction found

Current capture /tmp/retail-lord-ready.png shows finished Beast Lord under
Taros attack; attempted pause but next capture proves defeat screen. Prior
high speed shortened AI arrival too. Returned to lobby, removed AI via player
name click110,92, attempted no-opponent start. Retail refuses with explicit
"There are no opponents in the battle room" dialog (/tmp/retail-no-ai-state.png).
Dismissed dialog500,310 and clicked empty opponent slot110,92 to restore an
opponent; final lobby needs verification next. No ongoing battle to preserve.
Binary local strings confirm ManaMe/ATM/NowISee power-code names but entry
syntax and actual enabled state not yet verified. Need controlled setup
(resource code and/or passive opponent) rather than repeat slow build chain
under accelerated hostile AI. No engine edits or new parity claim.

### Controlled Glide reference restart: pause and resource command provenance

Revalidated retail PID2973087 and lobby, then started fresh Zhon reference match.
Power Codes indicator is lit in `/tmp/retail-reset-lobby.png`. Confirmed VK_PAUSE
19 actually pauses this running match (`/tmp/retail-pause-check.png`), so reference
setup can be preserved between observations. Previous `+atm` appearing in chat
was not sufficient evidence of rejection: inspected native chat handler4b4fb0,
which strips leading '+' and dispatches via428f10; ATM registration605bc8 points
to425d30. That handler refills local player's mana to capacity and accounts for
the added amount. Runtime refill success still needs resource-state evidence.
Plain `atm` lacks the required prefix and is ordinary chat.

Queued Beast Handler at350,260 from Thirsha, then unpaused. Screenshot
`/tmp/retail-controlled-handler.png` proves accepted construction and purple
particles around both owner and site. Started bounded six-refill setup sequence
at five-second intervals; do not assume builder completed until inspecting final
capture `/tmp/retail-handler-funded.png`. No engine changes or new parity claim.

Read-only native process helper `/tmp/retail-mana.exe` resolved world62d55c,
local player+306e, resource pointer+2510+player*110: current stored/capacity
5100/5100 after refill sequence. Handler completion verified in
`/tmp/retail-handler-funded.png`; funded Tamer completion and paused state
verified in `/tmp/retail-funded-tamer-checkpoint.png`. Requested Beast Lord at
130,210 and started another bounded funded interval with pause at its end.

Additional explicit animation gap found during current renderer inspection:
feature ignition in `gameview_render.cpp` still spawns two generic flame effects
instead of authored seqnamefrontflame/seqnamebackflame, plus wall-time smoke
bursts. No native comparison covers this path yet; retain it in full animation
scope rather than treating projectile-flame checks as feature-fire parity.

### Retail Drake reference unit now available

Verified completed Lord in `/tmp/retail-funded-lord-checkpoint.png` and selected
Lord menu in `/tmp/retail-lord-menu-controlled.png`. Drake icon is sixth visible
slot355,405; placed at240,310. `/tmp/retail-funded-drake-checkpoint.png` shows
completed Drake and paused match. Selected250,320; attack-ground A then400,280
was accepted (HUD “Engaging target”). Captured16 unsynchronized wall-time frames
in `/tmp/retail-drake-fire-reference/` with capture bounds in times.tsv. Inspected
frames00/08/15: takeoff and airborne aiming visible, but no flame in those samples.
Do not label this a verified shot capture. Retargeted clear ground420,180 and
started12 further screenshots in `/tmp/retail-drake-clear-fire/`, ending paused;
inspect results next. Native reference remains PID2973087; no engine edits.

Inspected all clear-target samples: frames03 and10 show a sustained visible
pale-yellow flame stream from Drake toward the commanded ground point; frame05
shows a short mouth flame. Thus live retail Glide shot evidence is now obtained,
not just accepted attack/takeoff. Frames01/02/04/06/07/09/11 have no visible flame
at their sampled instant. Frame03/10 screenshots are references, not synchronized
per-tick comparison with `/tmp/drake-age8.png`. Exact blend/color/geometry parity
remains unproven. Match was paused automatically after12 captures; retain this
built Drake scene for additional native-state and shot phase capture.

### Live Glide flame metadata exposes reference memory fallback

Read-only native inspection (`/tmp/retail-flame-meta.exe`) confirms loaded flame
sequence has6 frames, encoding4, flag0, matching authored4444 indices0,2,4,6,8,10
exactly in width/height/anchor. Winning data.hpi asset has12 frames. Native
4bd050 budget branch4bd169 uses signed reported memory at game+8 -> +5ee;
frame stride is1 at >=65 MiB, otherwise2. Live field is0xffffffff (-1), which
falls into half-frame fallback under Proton. Added observation-only
`probe_effect_frame_budget.py`:4105 native cases pass, including boundary and
failure-sentinel cases. It tests budget decision only, not complete bank upload.

Reran `probe_glide_blend.py`:512 blend-state applications and768 textured-mode
cases pass. Loaded format4/flag0 selects source-alpha/destination-one additive
blending, as current SDL effect loader does. Pale native sand versus dark client
terrain and different target distance/shot phase also confound prior visual
comparison. Do not infer a color/spacing bug from these unmatched screenshots,
and do not silently degrade our full-frame animations to reference's failed
memory-report fallback. Need equivalent scene/frame budget for paired pixels.

### Feature ignition and layered flame rendering traced

Native definition loader493f5f..494025 loads seqnamefrontflame and
seqnamebackflame using each configured value as BOTH hardware bank name and
sequence name (4bd050/4bd420), storing at definition+108/+10c. Both overlay
sequences are forced nonlooping; underlying burn sequence+e0 is made looping
when either overlay exists. Ignition494c6a initializes burn state+4 and optional
front+45/back+51 at age0 through537390. Update495e59 advances each active clock;
with overlays, lifecycle completion tests both overlay pointers becoming null,
not a hardcoded five-second overlay duration. This differs from current generic
client overlays/shared burn animation clock; implementation still outstanding.

Located hardware feature draw4fd430. Active overlay frame selection4fd7f1 uses
back clock+51 then front+45; final submission4fd9b8..4fda29 orders shadow, back
flame, body, front flame, all at the same calculated feature anchor. Overlays
use special-call flag1, normal frame blending rules still apply. Added native
`probe_feature_flame_draw.py`:288 combinations verify final submission ordering,
optional layers, common signed anchor, shadow suppression and flame call flags.
Probe substitutes final sprite draw and does not prove upstream positioning,
visibility, feature state updates or GPU pixels. No engine edits this turn.

### Authored feature flame overlays connected to renderer

Replaced ignition's two generic global flame instances with per-feature loaded
seqnamebackflame/seqnamefrontflame animations. Draw order is shadow, back, body,
front at common feature anchor, using existing authored-duration sampler with
nonlooping overlays. Burn body clock now starts at observed ignition tick rather
than global display time; seqnameburn loads all frames even for static features
(animating=0). Cleared overlay pointers on burnt-art replacement. Existing
wall-time smoke/spark stand-ins, native per-feature lifecycle, exact ignition
snapshot timestamp, shadow animation and non-overlay burn completion still need
work. This is not full feature-fire parity.

Release/Debug/optimized takclient rebuilds pass. Debug retail_visual passes.
Live --firetest 5s run logged `burn-vis vertree01 art=1 front=30 back=31`, matching
fixture definition front=mediflame/back=megaflame and demonstrating asset hookup.
Attempted screenshots have the burning tree outside captured viewport; they do
not prove visual output. Logs `/tmp/feature-flames-{loaded,visible}.log`; build
logs `/tmp/feature-flames-{build,release-build,o2-build}.log`. No shared simulation
changes; pathfinding and hashed state unchanged. `git diff --check` passes.

### Feature burn clocks retain simulation ignition time

Added cosmetic Feature::burnStarted, set only on accepted ignition in the sim,
excluded from stateHash. Feature sync carries this timestamp instead of dating
ignition at the first render observation. Burn and overlay clocks use published
simulation tick minus ignition tick; if a nonblocking feature refresh is ahead
of the published scene, age is clamped at zero rather than unsigned underflow.
Repeated ignition cannot reset an active clock. Added featgen tests for delayed
viewing, duplicate ignition and cosmetic timestamp hash independence.

All targets rebuilt in Release, Debug and optimized trees. All45 tests pass in
all3 configurations; logs `/tmp/feature-stamp-{release,debug,o2}-tests.log`.
Determinism guard/math golden passes (dcef618cd2e4d558; unavailable ARM headers
skip cross leg). Two local10s server/client runs retain83a52a301ae12a8b, same as
prior runs. Logs `/tmp/feature-stamp-{determinism,network}.log`.
Remaining feature fire work still includes native burn lifecycle, smoke/sparks,
shadow playback, live paired visual proof and visibility/early disappearance
when a whole burn falls between snapshots. Other transport/animation audit gaps
remain open. This change does not alter pathfinding or hashed gameplay state.

### Burning feature capture verified; native lifecycle mismatch measured

Added debug TAK_FEATURE_FLAME_TEST capture gate: fireTest centers the actual
fixture using viewport dimensions, disables edge scrolling/follow and main waits
for at least two submitted feature-flame layers. All three clients rebuilt.
Debug dummy-renderer capture succeeded at `/tmp/feature-flames-verified.png`;
inspected image shows authored flames around the burning tree, with dragon breath
and other fixture units visible. This verifies ordinary draw hookup, not paired
retail pixels. Prior hardcoded fixture camera placed the tree near/under the HUD.

Added `probe_feature_flame_clock.py`, executing native495e31..495f0f with real
5373d0 clock updates and only final retirement substituted:2236 updates across
256 timelines pass. With overlays, retirement occurs after BOTH finish; without
overlays, when the burn body finishes. Asset durations for fixture VerTree01:
mediflame30x2=60 ticks; megaflame31x2=62 ticks (full-frame banks). Current sim
hardcodes150 ticks, a confirmed mismatch requiring authored lifecycle loading.
Hardware low-memory frame reduction must be accounted for separately when
comparing the live Proton reference. Smoke/spread/replacement were not exercised
by this clock probe. No simulation changes in this turn.

### Burn lifetime change requires gameplay-data protection

Traced beyond the prior clock sink. Native495300 finds the cell from signed
slot coordinates, removes original through496380, then places definition+12e
unlessffff; loader494536 confirms+12e is featureburnt (featuredead is+12c).
New `probe_feature_burn_retirement.py` passes512 cases, observing exact removal/
replacement calls and missing-cell behavior, with terrain operations as sinks.
Native495cce gate confirms every-third-tick branch is smoke emission only;
active burn clocks still update on other ticks through495e31.

Implementation dependency discovered: gameplayHash currently hashes only
seqnameburn presence; feature animation resources remain cosmetic-mountable.
Changing burnLeft to authored duration must also hash the derived duration,
using the SAME asset resolution/timing helper as matchsetup, so duration-changing
reskins are rejected before lockstep while equal-duration art remains cosmetic.
Do not land a sim-only timer change that leaves this gate unprotected. No engine
edits in this turn; authored lifecycle implementation still outstanding.

### Authored feature retirement lifetime implemented and hash-protected

Added shared gaf::FeatureBurnTiming: cached body-bank sequence durations from
feature filename.gaf, cached front/back hardware-effect durations resolved with
existing4444/1555/taf/gaf precedence. Missing burn body disables ignition; when
an overlay resolves, lifetime is max(front,back), otherwise body lifetime.
Zero authored delays occupy one tick. Match setup stores duration in FeatType;
World ignition uses it instead of fixed150. Full-quality timing is deliberately
independent of a client's renderer/memory report. Gameplay hash now incorporates
the same derived duration, preserving equal-duration pixel-only reskins.
Native hardware compaction4bd351 multiplies retained frame delays by stride;
thus frame dropping is not simple halving of playback duration (odd lengths can
round up). No renderer-budget dependence was introduced into lockstep.

Expanded nimbus hash/asset tests: longest overlay, body fallback, missing body,
pixel recolor, shorter overlay changes preserving total lifetime, longer lifetime
changing checksum. Expanded featgen regression proves presence through final
frame and retirement at authored tick. Initial hash-test TDF was malformed on
one line; corrected multiline fixture and reran successfully. Scratch compiled
loader with current retail assets confirms VerTree01=62 ticks.

All targets rebuilt Release/Debug/optimized. All45 tests pass each (29.63s,
106.30s,32.23s respectively under concurrent work). Logs
`/tmp/feature-duration-{release,debug,o2}-tests.log`. Determinism guard/math golden
unchanged dcef618cd2e4d558; ARM missing headers skip. Two multiplayer runs match
83a52a301ae12a8b. `git diff --check` passes. Hashed gameplay intentionally differs
for burning features, protected by the new data hash; navigation code untouched.
Remaining: native retirement-before-spread ordering (ours still spread first),
smoke/random emission parity, ignition/update phase, shadows and paired captures.

### Burn retirement now precedes simultaneous fire spread

Extended native feature clock probe to place spark expiry before/on/after burn
completion, including native suppression flag. All2236 updates across256 timelines
pass: the retiring tick never calls495110. World::tickBurning now retires before
processing spreadIn, matching that precedence. Added actual adjacent-tree tests
with100% spread chance: one-tick burn expires without igniting neighbour; a spark
before two-tick expiry still ignites it. Both cases pass all3 build configurations.

All-target Release/Debug/optimized builds pass. Targeted featgen/nimbus/retail_visual
suites pass in all3; full45 suites passed immediately before this small ordering
change and were not redundantly rerun. Guard/math determinism golden unchanged,
ARM header leg skipped; two local multiplayer runs agree83a52a301ae12a8b.
Logs `/tmp/feature-order-{network,determinism}.log`; build logs
`/tmp/feature-order-{release,debug,o2}-build.log`. Pathfinding unchanged.
Smoke/spark visuals, native spread geometry/RNG, creation/update phase, shadows,
paired Glide captures and remaining transport/animation audit requirements remain.

### Native feature smoke origin/request measured

Added `probe_feature_smoke_origin.py`:2048 cases execute495ce3..495e2e with
feature-position/current-frame inputs and CRT/final-insertion sinks. Existing
emitter bypasses allocation only. Native requests one ordinary smoke particle
(count1,period0 -> default8,small0,steam0). Jitter uses current burn-body frame:
X whole offset=floor(width/4)+floor(rollX*floor(width/2)/32768)-anchorX;
Y whole offset=2*(anchorY-floor(rollY*floor(height/2)/32768)-floor(height/4));
Z unchanged. Coordinate additions wrap16.16; two CRT draws consumed for origin.
Emission gate495a24/495cd8 is global tick%3==0. Prior smoke-emission probe covers
capacity10 and third random draw for particle frame limit in4f1d40.

Current client still emits wall-time generic gray smoke and orange sparks from
feature center; replacement remains outstanding. Existing tick-queued unit smoke
pipeline (captureCombatEvents/consumeSmokeTicks, bigsmoke bank, capacity10,
RetailSmokeParticle) provides reusable mechanics, but feature origin4931e0,
per-feature ownership/retirement and tick-owned emission hookup must be integrated
rather than emitting from draw. No runtime edits this turn.

### Feature smoke base world origin verified

Added `probe_feature_world_origin.py`:2048 native4931e0 cases pass with only
terrain lookup511170 substituted. Input signed cell coordinates and signed
footprint dimensions produce center X/Z=(2*cell+footprint)<<19, wrapped16.16;
Y is terrain height sampled at that center, shifted16. Smoke's burn-frame jitter
then adjusts this base (previous probe), not an arbitrary fraction of tree height.
No renderer terrainLift inversion is needed to construct the world origin.

Inspected existing smoke queue: captureTransportEffects owns per-tick wind and
script emissions; consumeSmokeTicks advances native particle mechanics and uses
bigsmoke for ordinary flag0 smoke. Feature integration must preserve independent
feature ownership (cell ids can overlap unit ids), removal when native feature
emitter retires, body-frame geometry at emission tick, capacity10, and ordering
around the feature. Simply injecting render-time events into unit smoke storage
would leave these unresolved. No engine changes in this turn.

### Feature smoke integration verified

Feature smoke now uses the simulation tick queue, ordinary bigsmoke particles,
native burn-frame origin jitter, global three-tick emission cadence and cap10.
Feature ownership is separate from unit ownership; retirement and ignition
generation changes discard the old feature emitter. Wind and particle lifetime
advance on consumed ticks, not render elapsed time. Drawing occurs beside the
feature body/flame layers. The previous timed gray/orange bursts are removed.

Confirmed all three client build logs terminate in successful linking. Runtime
`TAK_FEATURE_SMOKE_QUEUE_TEST` passes capacity, delayed two-tick motion, duplicate
consumption and overlapping unit/feature owner-ID isolation. Runtime flame capture
now requires both flame layers and a smoke draw before saving;
`/tmp/feature-smoke-live.png` was generated and inspected. These are integration
checks, not paired Glide visual parity. Logs `/tmp/feature-smoke-{queue,live}.log`.
Debug retail_visual, nimbus and featgen all pass (3/3).

Remaining: whole-callback pre/post tick alignment, shared cosmetic RNG ordering,
blank burn-body frame handling, paired Glide captures, and the broader transport
and animation gaps listed above. Current feature loader uses max(1,delayTicks),
without the old 300-tick clamp; blank frames are still discarded. No navigation
or gameplay RNG changes in this integration.

### Preserve feature body blank frames and native durations

Feature loader no longer rejects sequences whose first frame is blank or drops
blank frames inside a sequence. It retains original geometry and a transparent
1x1 backing texture for a blank frame, preserving its clock slot and smoke origin
geometry without drawing pixels. Blank shadow frames are skipped. Feature body
clock now reads retailDelayTicks (native low word, minimum one), matching the
burn-lifetime loader. The previous entry's statement that no 300-tick clamp
remained was incomplete: the old delayTicks field was clamped inside gaf::load.
The renderer now bypasses that field. Texture allocation failures can still drop
frames; this resource-exhaustion behavior remains unresolved.

All three client builds pass. Debug retail_visual/nimbus/featgen pass (3/3);
nimbus includes decoder and timing fixtures for blank frames and delay501, but
does not directly exercise SDL feature texture creation. Real-asset feature
smoke queue runtime passes after rebuild and exits successfully. Logs:
`/tmp/feature-blank-{debug,build,build-o2}-build.log` and
`/tmp/feature-blank-smoke.log`. No shared simulation code changes.

### Native feature shadow frame selection measured

New probe_feature_frame_selection.py executes native selection blocks
4fd93f..4fd9b8 (ordinary features) and4fd7cc..4fd9b8 (instance/burn clocks),
substituting only final frame lookup routines. All32 combinations pass. Animated
ordinary features select independent body and shadow clocks (definition+f0/+fc);
static features select frame zero. Instance branch uses shadow clock+10 when
instance flag4 and shadow option are enabled, body+4 and optional back/front
clocks+51/+45. Shadow option gates shadow lookup only.

Current renderer stores only first shadow frame and draws f.shadow even when
using burnArt, so it does not yet implement these native selection semantics.
This establishes a concrete remaining mismatch, not just an untested possibility.
Probe does not cover clock advancement/loop flags, projection, visibility or GPU
blending; those must not be inferred from lookup calls. No engine changes in
this measurement turn.

### Burn shadow advancement and ignition distinction verified

Extended actual-clock feature probe: all2236 updates/256 timelines pass with
an independent shadow clock comparison. Without overlays, shadow advances; with
either overlay, shadow remains untouched. Extended frame-selection probe with
16 native ignition setups at494c1f..494ca8 (clock initialization sink). Without
overlays it replaces the shadow with seqnameburnshad, or disables shadow when
absent. With overlays it preserves the previous shadow state instead. Thus an
unconditional switch to burnArt shadow would be wrong. Existing selection32
cases still pass. Implementation must retain this distinction and freeze the
prior shadow frame for overlay burns; ordinary shadow animation remains open.
No runtime changes this turn.

### Feature shadow frame playback integrated

FeatArt now owns shadow texture/geometry/duration arrays separately from body
frames. Renderer samples authored shadow duration/loop flag independently, using
each selected frame's geometry. Blank shadows retain clock slots; failed shadow
texture allocation also retains the slot. Teardown destroys the owned shadow
array (legacy shadow pointer remains an alias). Tree sway applies to the selected
shadow geometry. Toggle still gates shadow drawing.

Burns without resolved flame overlays select burnArt shadow at ignition age; no
burn shadow means no shadow. Overlay burns retain original shadow sampled at
burnStarted rather than advancing it. This reconstructs the prior shared clock
from ignition tick; exact native global clock phase and any loader overrides of
authored loop metadata still need verification. It is not yet proof of full
native shadow parity. Ordinary sampling still uses animClock, as body playback
does; that timing relationship also remains to audit.

All3 client builds pass; debug retail_visual/nimbus/featgen pass3/3. Rebuilt
feature fire runtime produces `/tmp/feature-shadow-live.png`, inspected, and
exits normally with no tracked texture leak. This capture checks integration,
not animated-shadow frame correctness or paired Glide visuals. Logs
`/tmp/feature-shadow-{debug,build,build-o2}-build.log` and
`/tmp/feature-shadow-live.log`. No shared sim/pathfinding edits.

### Burn-shadow loop override corrected

Native loader493ea5..493ebf forces seqnameburnshad sequence loop byte to zero.
Extended feature-frame probe executes that block for all256 authored byte values;
all pass. Ordinary body/shadow load assignments493dc2/493e0d do not apply this
override. Renderer now forces burn shadows nonlooping, instead of trusting their
authored loop metadata. All3 client builds pass (`/tmp/feature-shadow-loop-*.log`),
2236 native burn-clock updates pass, and debug retail_visual/nimbus/featgen pass.
Exact shared-clock phase, animated SDL shadow fixture and paired Glide capture
remain outstanding; these checks do not prove them.

### Shared feature playback moved to simulation ticks

Extended native selection probe with tick-pass4959c0..495a12, substituting
clock advancement only. All32 five-type animation masks advance body and shadow
exactly once per active type, independently of visibility. Existing selection,
ignition and loop-override cases still pass. Ordinary feature body/shadow rendering
now samples front().gameTick, rather than accumulated float animClock. This also
uses the same time basis as the ignition timestamp used to freeze overlay-burn
shadows. Body playback now respects sequence loop metadata (including one-frame
animated sequences); static body sequences remain held indefinitely.

All3 client builds pass (`/tmp/feature-clock-{build-dbg,build,build-o2}.log`).
Debug retail_visual/nimbus/featgen pass3/3. Integration fire capture produced
`/tmp/feature-clock-live.png` and exited successfully. Exact native first-tick
phase relative to world publication remains unproven; this change removes
floating-time accumulation, but cannot by itself prove that phase or paired
Glide parity. No simulation/pathfinding changes.

### Ordinary feature loop metadata preservation checked

Extended probe_animation_frame_flags.py: all256 sequence loop bytes survive
native537490 bank relocation across1024 frame/subframe combinations. Extended
probe_animation_lookup.py executes full493700 feature lookup with actual537550
string lookup and only GPU texture preparation4bd840 substituted; all256 loop
bytes remain unchanged, returned sequence and preparation arguments agree.
Existing2048 lookup cases pass. Static inspection of4bd840 shows per-frame
texture preparation rather than sequence loop mutation; the executable probe
does not cover that substituted GPU work. These results support retaining
authored ordinary loop metadata, distinct from the measured burn-shadow
override. No engine edits or rebuild necessary this turn.

### Feature art cache separates playback and shadow variants

Cache previously keyed only file/body sequence, although loading also depends on
shadow sequence, palette world, animation enablement and burn mode. Changed to a
structured six-part key incorporating those inputs. This prevents an earlier
static or standing request from supplying truncated frames, wrong shadows or
wrong shadow-loop policy to a later burn/animated request.

New debug TAK_FEATURE_CACHE_TEST uses real VerTree01 art with definition variants
sharing the body sequence. It verifies distinct static/animated/burn/shadowless
results, nonlooping burn shadow, no shadow frames for shadowless variant, and
identity reuse on repeated identical lookup. Runtime passes and exits normally
with no tracked textures remaining (`/tmp/feature-cache-test.log`). All3 client
builds pass (`/tmp/feature-cache-{build-dbg,build,build-o2}.log`); debug
retail_visual/nimbus/featgen pass3/3. Palette-world discrimination is represented
in the key but not separately exercised by this runtime fixture. No shared sim
changes; broader paired visual and transport requirements remain outstanding.

### Live Glide reference recovered for transport comparisons

Revalidated retail PID2973087: existing Kingdoms.exe remained alive, minimized.
An initial case-sensitive process search missed it; subsequent case-insensitive
inspection corrected that mistake. Proton run of icd exited immediately without
a new persistent game. /proc/2973087/maps confirms installed glide3x.dll mapped.
Scratch Win32 ShowWindow(SW_RESTORE) helper through the existing Proton prefix
restored the original paused Zhon reference match. Screenshot
`/tmp/transport-retail-restored.png` inspected: paused scene retains prior units
and5300/5300mana. No game-state edits or new match were needed.

Current transport tests include peninsula routing, boxed-ship failure, interrupted
pickups and repeated coastal roster trips, but these are our World scenarios.
Existing native probes substitute navigation and do not yet establish complete
retail approach traces for those scenarios. Restored live reference is available
for the next air/sea transport comparison; no new transport parity claim yet.

### Live transport setup input investigation

Existing paused Glide match remains intact. Restored-window client is640x480;
Proton cursor helper reports requested client coordinates correctly. Initial
clicks encountered an active input/order mode: Escape restored HUD text showing
selected Drake/Patrolling, right-click then cleared selection. Brief pause toggles
advance the match and confirm it is responsive. Attempts to select the small
figure at164,180/190 (including PostMessage) have not shown a selected builder;
its identity/selection hit location is not established. No air transport has
yet been built or tested. Current match paused, no selection. Latest inspected
screenshot `/tmp/transport-retail-postselect.png`; Drake now near400,60, ground
figures near195,260 and345,300. Next setup should identify/select a ground builder
rather than assume the old build-chain coordinates still apply.

### Live retail Roc transport and unload captured

Using the existing paused retail Glide match, built a Roc from the selected Beast
Lord, then loaded the nearby ground unit. Retail HUD visibly reported `Carrying 1`.
Ordered the Roc to a separate destination. Captures show the carrier progressing
through `Avoiding air traffic` and `Seeking to land`; later captures show `Carrying
1` cleared and the passenger on the ground near the destination. Sequence frames
are in `/tmp/transport-roc-load/` and `/tmp/transport-roc-unload/`. This is live
retail air load/unload evidence, but capture intervals are wall-time snapshots and
do not establish exact transition ticks, destination landing coordinates, or the
full simulation path. The retail process exited by the final inspection;
`coredumpctl` found no crash dump, cause unknown. No further retail inputs sent.

Ran native transport parity probes against current build-o2 transport_test: pickup
transfer 4096 cases plus complete stationary air/sea timelines pass. Unload native
parity passes100 air and62 sea cases, with4096 fixed-point range cases per mode
and36 PARK radii. Full `build-o2/transport_test` passes, including actual World
coastline crossing, landing on land, water-domain preservation, isolated pond
selection, peninsula routing, blocked ship failure and queued/interrupted trips.
The unload parity wrapper substitutes placement/effects sinks; pickup probes
substitute attachment/navigation dependencies as documented in probe headers.
These native comparisons verify transfer geometry/state logic but not full native
coastal navigation callbacks. Outputs: `/tmp/transport-unload-{air,sea}.json`;
probe stdout in this turn. Sea has no equivalent live Glide capture yet.

### Transport comparison follow-up: sea test map search

Scanned retail maps.hpi for `.tnt`/`.ota` names; candidate water maps include
Lake Lokken, Sea Dragon Spine, Iapur Narrows, Meredoc Keys and Per Mare Per Terras.
A fresh Kingdoms.exe launch stayed on the BTI splash during this check; pressing
Enter did not advance it. No new live sea capture was obtained. The prior live Roc
sequence and native air/sea parity results remain the usable evidence. Interactive
sea loading/unloading and broader animation/projectile visual comparison are still
open.

### Projectile sprite clocks and blend modes use authored metadata (2026-09-22)

Non-straight projectile sprites and their shadows were sampled at fixed 20/12 fps,
ignoring the authored per-frame durations and sequence loop flag. Straight-shot
sprite sampling walked frame durations but always wrapped at the end. All these
paths now use `retailEffectFrame` with the captured projectile age, authored
durations and loop metadata, matching the native animation controller's zero-delay,
startup, loop and expiry rules. This is renderer-only and does not change simulation
state or the lockstep hash. Straight-shot foreground sprites also stopped forcing
every texture to additive: `effectFor`'s per-frame native mode is retained, with a
blend override only for the darkening shadow pass. This fixes format-4 alpha frames
in `cannbmed`, used by the Arassh and Veruna tower straight shots.

The native `537390/5373d0/537430` animation-clock oracle passes 147,456 updates;
the shared C++ helper matches 1,280 native 64-update timelines. The native sprite
blend oracle passes 1,536 format-4 draws, 72 composite draws and 1,024 indexed
draws. Rebuilt Release and optimized Debug clients, then passed all 45 CTests in
both builds, including `animation_roster` and both transport suites. The local
software-renderer capture `/tmp/projectile-authored-clock.png` shows an in-flight
authored fireball; it is a render smoke check, not a paired Glide pixel comparison.

Extended the projectile inventory to parse each referenced 3DO model. Across the
Standard and Crusades registries it scanned 145 moving-projectile weapon slots;
all referenced models parsed with geometry, with zero missing, unreadable or empty
models. The only artless entries are Water Ball and Claws beam slots. Aramon,
Taros and Veruna Archer definitions resolve to the authored `araarrow` mesh, so the
generic yellow-line fallback does not explain the Archer report. Exact paired
projectile pixels and remaining emitter/model details remain open. The wider
animation inventory resolved all 113 distinct requested effect sequences and sprite
banks, including weapon, shadow, explosion, Nimbus and transport art. A live Glide
sea-transport sequence remains open. No retail game was launched for this check.

The same weapon scan found `spinheading` on non-straight 3DO shots (`araham`,
`aragren1`, `aradag` and the Zhon `zonbolo`). Their model path had ignored that
field. It now advances mesh yaw from shot age at the authored 30 Hz rate, following
the same COB-angle conversion already used by spinning shot sprites. A local
`zonlord` capture at `/tmp/projectile-spin-bolo.png` confirms the shot mesh renders;
it is not a paired visual proof of spin phase. This remains display-only.

### Native-only transport regression sweep (2026-09-22)

Per the current instruction, no retail game was launched or controlled. Re-ran the
available native-binary transport probes against the current optimized build:
capacity (4,096 cases), pursuit (4,096), cleanup/cancellation and interruption
resumption (160 chains), controller installation (2,048), effect clock (256),
effect projection/fog (4,096 each), flight arrival (288 boundaries), invalidation
(32), pickup transfer/timeline (4,096 state cases plus complete stationary air/sea
timelines), passenger scheduling (4,096 native/port cases plus retained retries),
unload transfer (100 air cases and 62 sea cases with exact-site/retry/PARK checks),
and pickup selection/approach (512 rosters, 60 controllers, 480 moving-target
positions, 16 ground snapshots, 100 failure combinations). All passed. The full
`build-o2/transport_test` integration suite also passed, covering coast crossings,
landing, blocked placement, queued orders, cancellation and interruption.

This narrows the remaining transport gap to end-to-end native mission timelines
with actual navigator callbacks, especially sea transport; it does not prove those
callbacks or interactive Glide behavior. The fresh probe run provides no new
counterexample requiring a transport code change. Live sea-transport comparison
and paired animation visuals remain open.

### Surface unload approaches use the native circle goal (2026-09-22)

The native `408d50` sea handler sends the exact selected landing point to the
navigator with radius `transportdistance-34`; `408e84` uses the same tolerance for
ground carriers. `probe_transport_unload.py --sea` observes the exact XYZ and 116
radius for a 150-distance carrier. The previous `unloadAt` implementation instead
preselected a reachable water cell inside the full transport range, then issued an
ordinary move. That changed the native goal geometry and endpoint selection.

`World::unloadAt` now creates a separate circle-goal approach for every surface
carrier, centered on the unchanged landing point and with the native radius. The
existing water/ground path service chooses a fitting endpoint reachable from the
carrier, so disconnected ponds are excluded by the route search. The exact unload
order remains queued behind the approach, and air carriers retain their flight-goal
path. This change does not alter the tracer or other pathfinding rules.

The optimized `transport_test` passes with new assertions for the exact circle
center/radius, a real coast crossing, a far-inland unreachable goal request, and an
isolated pond closer than connected sea. Fresh native air/sea unload probes pass (100
air and 62 sea cases), as does the 512-roster pickup-selection oracle. Full carrier
mission callback traces and live sea Glide comparison remain open; the far-inland
test verifies goal submission, not the whole retry/failure timeline.

### Surface unload failure dispatch follows the native callback (2026-09-22)

The native `408d50` stage-1 callback checks the navigator failure event `0x200`
and mover availability before installing wait mask `0x700`. Failure returns `8`,
which cancels the unload approach. The boxed-ship World test observes the real
path-service failure, then verifies the passenger remains aboard and a queued
move survives.

The sea unload probe also compares the stage-1 result, attempt counter, wait mask,
15-tick retry and exact circle-goal call across 71 native cases. The air probe
retains its 100 transfer/range cases. These checks and both transport CTests pass.
This covers a real blocked-start failure and its queue cleanup, not a full native-
versus-World timeline for distant/coastal route arrival, recovery or interruption.
Live sea Glide comparison and paired animation visuals remain open.

### GET 33 turn percentage reaches authoritative unit scripts (2026-09-22)

`World::ScriptHost::get` previously returned zero for GET 33 even though the
renderer and native query probe used the verified signed percentage. The
simulation host now scales moving and pivot turn rates by the active road/water
multiplier and calls the shared native-matched query. COB executes before the
mover each tick, so the input is the signed applied heading delta saved at the
end of the previous tick; the unclamped `TurnDirection` request remains a
separate display input. The authoritative sample participates in the lockstep
hash, while its tick-start scratch heading does not.

The retail-script regression executes real GET 28/29/30/33/34 bytecode for
ground, road, water, blocked, airborne and carried states, checks signed turn
percentages, and verifies that a completed world-mover turn is read by the next
COB update. It also verifies that changing the sampled turn changes the state
hash. `retail_script` passes; the native `check_animation_queries.py` oracle
continues to cover 8,192 GET 29/33 input cases. This closes the missing GET 33
host query, not the remaining full callback-order or rendered animation gates.

### Surface unload callback and dispatcher trace (2026-09-22)

Extended the sea-unload comparison through native primary dispatcher `4d8450`
and real `408d50`, rather than calling the handler once with a preselected event.
The trace covers the initial approach request, an unsubscribed movement event,
the 15-tick poll, an arrival event while still outside transfer range, another
poll, and simultaneous timer/failure delivery. All seven scheduler ticks match
the compiled `RetailMissionState` dispatcher, including retained unit events,
approach count, deadline, stage, and queue removal. A separate no-mover case
also matches.

This exposed and corrected a state discrepancy: the port set wait mask `0x700`
before returning failure, but native checks failure/mover first, leaving the
dispatcher-cleared mask at zero on the removed mission. The helper now matches
that order. `transport_test` and the native dispatcher comparison pass. These
traces validate handler scheduling and callback delivery; they still do not
compare the retail navigator's complete coastal route against the World path.

### Detached model shadows use retail's projected silhouette pass (2026-09-22)

The native detached-model renderer reaches `4ec720` after drawing the debris body;
that helper uses the same model shadow projection path as live units. `drawEffects`
previously drew detached geometry without any shadow. It now collects the detached
piece through the existing retail-matched shadow shear and shadow-mask path, anchors
the silhouette using the debris' absolute height, and gates both collection and
submission on the sampled unit-shadow option.

The production `TAK_DEBRIS_TEST` fixture now asserts that detached geometry emits
shadow triangles when enabled and emits none when disabled. The Debug dummy-SDL
`--firetest` run passes and produced `/tmp/debris-shadow-fixture.png`; optimized,
Debug, and Release `takclient` builds and the `retail_visual` / `animation_roster`
CTests pass.
The native model-call path confirms shadow dispatch, but the capture is not a paired
retail pixel comparison. Debris painter ordering/depth and attached emitters remain
open.

### Pickup missions match native dispatcher timing through boarding (2026-09-22)

Added `probe_transport_pickup_dispatch.py`, which runs native dispatcher `4d8450`
and both surface/flying pickup handlers through 36 controlled timeline rows. The
native and World traces match from request initialization through attachment and
mission removal. `probe_transport_pickup_events.py` separately covers early-arrival
wakeup, surface abort, flyer failure/timer recovery, and interruption followed by
resumption of the original pickup. Both probes and the 160-chain cleanup/resumption
probe pass; `build-o2/transport_test assets/game` plus both transport CTests pass.

The probes control movement-controller generation and arrival, so this closes
mission dispatch and queue timing, not route generation or navigator path parity.
No retail game was launched.

### Surface pickup callbacks reach the native mission dispatcher (2026-09-22)

Added `probe_transport_surface_pickup_callbacks.py`, chaining native ground
pickup circle-goal construction and navigator installation through the actual
arrival (`0x100`) or empty-route failure (`0x200`) callbacks and dispatcher
`4d8450`/handler `408860`. Arrival reaches the transfer stage and matches the
first `--pickup-transfer` tick; failure removes the stopped out-of-range mission
and matches `--pickup-approach-abort`. The new probe, pickup dispatcher/events
probes, and `retail_mission`, `transport`, and `transport_roster` CTests pass.

This closes native sea pickup callback delivery and recovery through the
dispatcher. Native route search/waypoint parity against `World` and live sea
Glide behavior remain open; no production mismatch was found in this trace.

### Scripted retail GUI input through a DPI-aware Win32 bridge (2026-09-22)

Retail can be driven without a slow GUI automation stack. Added
`tools/re/retail_input.c`, a small Win32 helper compiled with MinGW and run
inside the active Proton prefix. It finds the Kingdoms window, reports its
client bounds and cursor, posts menu clicks, sends keys, and uses
`ClientToScreen` plus `SetCursorPos`/`SendInput` (with a legacy mouse-event
fallback) for actual game input.
`SetProcessDPIAware` is required here: the earlier DPI-unaware helper reported
negative client coordinates and clipped the pointer at the display edge.
The helper reports the selected Win32 process ID and HWND, accepts
`--pid <id>` for an explicit game window, and refuses to choose arbitrarily if
multiple visible Kingdoms windows match. This keeps parallel retail references
controllable without sending inputs to the wrong match.

On this workstation, retail reports a 640x480 client on a 7680x2160 Xwayland
desktop. Its 2880x2160 fullscreen surface is centered horizontally, so a
compositor screenshot from Spectacle must be cropped at `(2400,0)-(5280,2160)`
and reduced to 640x480 before it is compared or fed into screenshot-based
automation. XTest clicks did not trigger the retail menu; client-coordinate
`PostMessage` did. In gameplay, posted mouse-move messages are insufficient;
the physical Win32 cursor path is the one used for cursor and edge-scroll input.

Build and drive the currently running game with:

```sh
x86_64-w64-mingw32-gcc -Wall -Wextra -O2 -o /tmp/retail_input.exe tools/re/retail_input.c
env STEAM_COMPAT_DATA_PATH=/tmp/tak-retail-parity \
  STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.local/share/Steam" \
  "$HOME/.local/share/Steam/steamapps/common/Proton 10.0/proton" \
  runinprefix /tmp/retail_input.exe info
```

The helper was tested against the live Glide match: `info` reported the correct
640x480 client and Kingdoms foreground window. From the title, posted clicks at
(120,300) and (562,433) opened the Zhon-versus-Taros skirmish and started the
match; `Ctrl+M` selected and tracked the monarch, exposing her name, selection
ring, and build palette. Pointer movement hovered the authored build icons and
displayed retail tooltips, including Lodestone and Beast Handler costs.
Screenshots were captured with `spectacle -b -n -o /tmp/retail.png` and
inspected after the crop above. This is a repeatable driver and capture path.
`tools/re/retail_capture.py` automates the compositor capture and centered
client crop; it was tested with a saved desktop image and against the live
session.

The direct path also reached a placed-construction scene. With Tirsha selected,
the Beast Handler (`zonhand`) icon and a click on open ground produced retail's
`Conjuring Beast Handler` state and purple sparkles. Screenshots at the start,
+1 s, and +2 s are in `/tmp/retail-site-placed.png`,
`/tmp/retail-conjure-plus1s.png`, and `/tmp/retail-conjure-plus2s.png`; their
normalized 640x480 views end in `-client.png`. A local `TAK_CONJURE_TEST=1`
capture using the same `zonhunt`/`zonhand` pair is at
`/tmp/tak-conjure-zonhand.png` (builder 1920,1616; site 2048,1616).

During a second placed-conjure run, the existing read-only `livesample.py`
observer captured 120 retail frames over four seconds (119 one-tick deltas,
two torn reads rejected). It identified the moving `zonhunt` and the new
`zonhand` by their native type/speed records. At tick 17569 the target was
(3648,62,688) and the Monarch was (3596.5,233,640.8), 69.9 map units away;
by tick 17688 the Monarch was (3547.3,233,664.0), 103.5 units from the target.
The `zonhunt` FBI build distance is 100. This confirms the live retail builder
approaches and orbits at its expected working distance while flying 171 units
above the target's ground height. It does not by itself establish whether the
rendered screen offset matches TAK.

That pair is useful for inspecting each renderer, but is not an apples-to-apples
height/position verdict: the TAK fixture uses zoom 3 and an east-only site
offset, while the retail match used its normal camera and a southeast screen
click. The retail frames confirm the Monarch hovers, casts sparkles, and has a
separate projected ground shadow during conjuring; no new render offset or
simulation correction is justified until the camera, site vector, and capture
tick are matched.

The input path was exercised again from a fresh skirmish on 2026-09-22. A
posted menu click returned from the results screen to the title, another opened
the skirmish lobby, and a third started the match. In gameplay, `Ctrl+M`
selected Tirsha, a real cursor hover at `(224,402)` exposed the retail tooltip
`Beast Handler, cost 1685`, and physical clicks on that icon and the map at
`(300,250)` began construction. Captures at the start, +1 s, and +2 s are
`/tmp/retail-drive-site-placed.png`, `/tmp/retail-drive-plus1s.png`, and
`/tmp/retail-drive-plus2s.png`; the last shows the `Conjuring Beast Handler`
state and live sparkles. A concurrent read-only `livesample.py` run observed 90
frames and 89 one-tick deltas over 2.98 seconds, with no torn samples. This
confirms the direct-control and capture sequence works end to end in the live
game; the screenshots are still not paired to a same-tick TAK capture.

### Cursor roster and authored frame durations follow the shipped GAF (2026-09-22)

The retail loader registers `cursorcapture`, `cursorteleport`, and `cursorpickup`;
they were missing from the local cursor ID/name table. Added the three entries and
confirmed their shipped one-frame art matches `cursornormal`. Cursor frames also
carry authored GAF durations: the old cursor renderer discarded them and advanced
every frame at a fixed 15 fps. Both software and hardware cursor paths now select
frames from cumulative GAF delays on the native 30 Hz basis, using wall time so
cursor animation continues while the simulation is paused.

The new `cursor_test` checks all 21 mapped IDs/names, 2/3/10-tick frame boundaries,
and the extracted sequence roster/pixels. It passes with and without the local art
root. `cursor_roster`, `retail_visual`, and `animation_roster` pass in Release and
Debug; the cursor test and roster CTest also pass in the optimized build. All three
client configurations build successfully. Full per-context cursor selection and
exact native animation start phase remain open; no retail game was launched for
this check.

### Airstrike cursor selector and source flags traced (2026-09-22–23)

Added `probe_cursor_airstrike.py`. It follows reachable selector mode 3 through
the retail eligibility gate and confirms native cursor slot 2 (`cursorairstrike`)
requires UnitDef `+0x264 bit 0x20` and the active weapon's WeaponType `+0xc8 bit
0x20`; clearing the UnitDef bit returns normal slot 19. Native `0x531280` parses
the FBI `dropped` key and sets the WeaponType flag. The UnitDef bit is initialized
from whether the primary WEAPON1 pointer exists at `0x4c1672..0x4c1698`; the
weapon gate reads the currently selected slot through `0x519ac0`. This `dropped=`
key is distinct from `subtype=Dropped`, which selects the local
`Weapon::Kind::Dropped` class. The shipped FBI roster contains no `dropped=`
entries, so the mapping primarily covers authored/mod content. The local model
now preserves the flag by native weapon slot, including the native WEAPON1 gate
and active-slot behavior when local weapon entries are compressed. Active-slot
and native parser probes pass.

The gameplay cursor setter path traced is `0x521cd0 → 0x4dd780`; other setter
sites observed use Normal, Hourglass or the default cursor. The selector exposes
no returns for Capture (slot 4), Pickup (8), or Teleport (9). Its Capture order
mode falls back to Normal, and no alternate gameplay setter callsite for these
registered slots was found in the audited paths. Native registration maps slots
1–21 to Attack, Airstrike, TooFar, Capture, Defend, Repair, Patrol, Pickup,
Teleport, Revive, Reclaim, Unload, Load, Move, Select, FindSite, Red, Green,
Normal, Hourglass and PathIcon; registration alone does not imply an active
selection path. The local HUD applies the native smallest-slot rule: Attack takes
priority over Airstrike, Airstrike takes priority over TooFar, and an all-Airstrike
selection retains Airstrike when no target-specific result wins. Focused unit
tests cover these cases and native weapon-slot mapping.

### Native action-mode 2 Revive, mode-5 Unload, and mode-6 Load gates (2026-09-23)

`tools/re/probe_cursor_action_modes.py` runs the installed `0x4dd780` cursor
selector for action modes 2, 5, and 6. Mode 2 returns slot 10 (`cursorrevive`) for
a `canmove` type (`UnitDef+0x264 bit 0x100`) with either `canresurrect` (bit
`0x1000`) or `cananimate` (bit `0x20000000`), provided the native map-cell and
corpse predicates pass. A movable type without either ability falls back to
slot 14 (`cursormove`); either ability without `canmove` falls back to slot 19
(`cursornormal`). Mode 5 returns slot 13 (`cursorunload`) for `cantransport`
(bit `0x200`) and slot 19 otherwise; mode 6 returns slot 12 (`cursorload`) for
a valid passenger target. The native parser maps the UI tags UNLOAD to mode 5
and LOAD to mode 6. The earlier audit mislabeled mode 5 as Load; local product
mapping was already correct. The parser flag mappings were checked at the
native `0x4c06xx–0x4c07xx` writes.

The focused emulator fixture supplies a visible point cell, stubs the action
eligibility and corpse-cell predicates to true, and executes the selector's
capability branches. It does not establish which live corpse cell passes those
native predicates. Locally, `UnitR` snapshots already carry caster abilities,
corpse phase/type, position and owner, but the HUD has no mode-2/Revive armed
command; the sim currently chooses eligible nearby corpses automatically.
Adding a default hover Revive glyph would therefore ignore a native action-mode
input that the local HUD does not have. Mode 6 has a direct local equivalent:
the armed `l`/Load command and `UnitType::canTransport`; it shows Load only when
at least one selected type can transport, matching the native capability gate.
The `cursorTeleport` sequence is registered, but neither action-mode branch
returns slot 9, and the audited gameplay setter paths do not assign it. Local
types/orders expose no teleport ability or command. Capture mode 13 also falls
back to Normal in the native selector. These slots remain registered placeholders
without a demonstrated live gameplay cursor path.

### Native action-mode 14 FindSite cursor (2026-09-23)

`tools/re/probe_cursor_findsite.py` executes retail selector `0x4dd780` in mode
14. The native branch returns slot 16 (`cursorfindsite`) for a live unit when
`UnitDef+0x132` has its build-options list and `unit+8` has its unit context;
missing either pointer or the live-unit status returns normal slot 19. Retail
arms mode 14 when a build-menu item is selected. Locally, `placing_` is that
armed state, while `registry_.buildable(unit type)` supplies the build-options
equivalent. The HUD now shows FindSite when a selected builder has build options;
the construction ghost continues to show whether the chosen site is legal.
The native fixture and `cursor_test` cover the selector and local mapping.

The final offline caller trace establishes that the `0x4dd780` map-action
selector's complete mode switch (1–14) never returns Capture (slot 4), Pickup
(slot 8), or Teleport (slot 9). Its selected-unit aggregation caller
`0x521cd0` takes the lowest per-unit slot; no direct cursor-setter callsite uses
those three fixed IDs. They are registered legacy assets with no supported
gameplay selection gate in the traced path. Hourglass (slot 20) is set around
the modal file picker at `0x4e7601` and restored to Normal at `0x4e7824` and
`0x4e7849`. No extra HUD branches are justified. These conclusions are from
native static disassembly and Unicorn probes; no retail GUI was launched.

### Flyer height and construction-orbit audit (2026-09-22)

The flyer projection checks found no renderer height correction to make: native
model transforms and airborne animation queries match, and the local render test
tracks the captured flight height through ascent, hover, and landing. The focused
native `41ef00` probe also confirms that placed-construction stage 4 installs an
X/Z point around the site with Y=0, flags `0x60`, and the same angle/radius draw
order already used by the C++ hover goal. Seed-50 probes cover both owner-flag
cases, including the one-in-50 retarget branch. A deterministic `conjure_test`
case now checks the native seed-50 goal and heading through the shared hover-goal
helper used by both construction paths.

The probe runs the retail handler, RNG, direction, trig, and goal setters in the
ICD; it substitutes only the platform allocation, critical-section, and
process-exit services needed by the emulator. Native renderer transform/flight
checks, `retail_visual_test`, and `conjure_test` pass. This does not prove that the
reported in-game northward separation is correct: a paired live native/TAK mission
timeline comparing unit/site positions and flight altitude is still needed. No
retail GUI was launched.

The live 120-frame sample narrows the northward offset: at tick 17688 the Monarch
is 171 world units above the Zonhand target, with `dx=-100.731` and `dz=-24.027`.
Retail's body projection `z-y/2` predicts a `-109.5` world-unit screen-Y delta,
including `-85.5` units from altitude alone; TAK uses the same half-height
projection. This explains the expected direction and much of the distance, so it
does not justify a render correction. The sample is not synchronized to a screenshot
or TAK frame, and does not include camera parameters, navigator goal, or COB piece
pose; exact paired visual proof remains open.

### Full three-configuration regression sweep (2026-09-22)

The complete 46-test CTest suite passes in all configured builds: Release
(`build`), Debug (`build-dbg`), and optimized (`build-o2`). The new native
Airstrike cursor and surface-pickup callback probes also pass, as do Python
byte-compilation, MinGW `-Wall -Wextra` compilation of `retail_input.c`, live
PID-targeted `info` against the paused retail window, and `git diff --check`.

The suite is green, but full parity is not complete: sea navigator route/waypoint
movement and live sea transport remain open; projectile, detached-effect and
feature-fire pixels still lack synchronized Glide comparisons; and several
context-specific cursor branches still lack local mappings.

### PID-targeted live retail control and coastal setup (2026-09-23)

`tools/re/retail_input.c` now enumerates the exact visible Kingdoms window and
accepts `--pid` so automation cannot silently attach to a second game window.
`info` reports the selected process, client rectangle, origin and foreground
state. Lobby controls can use posted client clicks; gameplay uses the actual
Win32 cursor with `SetCursorPos` and `SendInput` for pointer hovers, clicks and
keys. A physical drag command also moves a held button in steps, which reliably
scrolls the retail map list. It was built with MinGW `-Wall -Wextra -Werror`
and exercised through the installed Proton prefix. `retail_capture.py` captures
the compositor and normalizes the 7680x2160 desktop to the 640x480 game client.

The live round-trip opened a fresh skirmish, changed the faction/map, selected
Kienna, read the Sea Fort tooltip, issued waypoints from the retail command
panel, and observed Kienna's changing world coordinates with the read-only
`livesample.py`. The lobby's Map Revealed radio option produced a full-map
minimap. Screenshots from this run are under `/tmp/retail-lobby-drag1.png`,
`/tmp/retail-lake-maprevealed-2.png`, and `/tmp/retail-lake-near-shore.png`.
This gives us a repeatable way to drive and observe retail without relying on
window-title ambiguity or guessed mouse coordinates at desktop scale.

The Lake Lokken attempt did not yet produce a valid Sea Fort placement: retail
kept the red invalid-site X over the tested shoreline points, and enemy units
arrived nearby. No Sea Fort, ship, or transport completed in this test, so it
does not establish sea-transport parity. Native surface-pickup callbacks are
covered through dispatcher recovery by `probe_transport_surface_pickup_callbacks.py`,
but route search, waypoint movement, and a successful live sea-transport cycle
remain open.

### Retail driver discovers its own Proton runtime (2026-09-23)

`tools/re/retail_driver.py` removes the remaining manual runtime selection. It
finds the mapped `KINGDOMS.icd` process, reads that process's Proton executable
and compatibility-prefix environment, compiles `retail_input.c` with strict
MinGW warnings, then lists visible Kingdoms windows. It uses the sole visible
window automatically or accepts an explicit `--window-pid` if several retail
windows are open. For example, `tools/re/retail_driver.py info` reports the
640x480 client, foreground state and Win32 PID; the bridge's existing
`move`, `click`, `drag`, `key`, and `hotkey` actions use actual cursor/key input.

The wrapper was tested against the live retail process: automatic discovery
selected its Proton 10.0 runtime and `/tmp/tak-retail-parity` prefix, and `info`
reported the expected foreground window. An initial attempt through a different
Proton installation failed with a wineserver version mismatch before sending
input; tying the bridge to the running process's mapped runtime avoids that
failure. The result screen was then dismissed with Escape and retail returned to
the title screen. `tools/re/retail_capture.py` continues to normalize the
fullscreen compositor capture to the game's 640x480 client surface.

### Enemy hover range cursor follows the selected retail weapon (2026-09-23)

The HUD now resolves an enemy hover against the selected weapon slot, target
damage categories, airborne restrictions, min/max range, and the represented
weapon-class range callback. A reachable attack shows Attack; a legal weapon
outside its retail envelope shows Too Far. Attack-armed hover reuses the same
result. `cursor_test` covers inclusive maximum range, strict minimum range,
melee footprint reach, airborne targets and ballistic arc rejection. Range
distance now follows retail's per-axis high-dword squares, including fractional
diagonals and the full signed 16.16 coordinate limits. The damage multiplier
uses only FBI `damagecategory`; the broader simulation category list does not
participate in retail cursor eligibility. Remote Effect and Dropped weapon
callbacks remain unknown and keep the conservative Attack cursor.

The complete 46-test suite passed in Release, Debug and optimized builds before
this final narrow cursor correction. After that correction, `takclient` and
`cursor_test` rebuilt in all three configurations, and both the extracted-assets
cursor test and `cursor_roster` CTest passed in each configuration. `git diff --check`
also passes.

### Ballistic projectile draw contract (2026-09-23)

`tools/re/probe_ballistic_projectile_render.py` invokes retail's ballistic draw
callback at `0x52c100` for 4,096 randomized visible shots. All cases pass the
shot's actual XYZ position (`shot+4`), authored model, stored XYZ angles
(`shot+0x34`) and owner to the 3DO renderer. Aramon and Taros Archers use
`type=Ballistic, model=ararrow`, so this contract covers the arrow mesh the
player reported as beam-like.

Before the renderer port, TAK positioned this mesh using only X/Z and invented
symmetric altitude and yaw from the 2D trajectory. That confirmed difference
in drawing inputs motivated the port below. The follow-up probe identified
the normal BallisticWeapon initializer as `0x52be80` (the similarly named
`0x52c3b0` is a different shot path) and exercised it with the native update
`0x52bf90`: 512 launch cases and 6,912 substeps pass, including exact XYZ,
initial angles, truncating gravity, and move-before-collision ordering. The
probe controls muzzle lookup and collision admission, so its motion rows alone
do not validate native impact boundaries. The renderer port uses retail's 3D
muzzle, relative aim, pitch, gravity, substep motion and angle state to draw
ballistic 3DO models. A follow-up now advances the World collision path at every
one of those 3D substeps, including environmental impacts. The native
`0x52bf90`/`0x52a4d0` probe confirms an Arabow arrow contacts a feature top and
dispatches `0x529c10` with no unit target; the World regression applies the
authored 476 damage to the blocking feature and leaves the selected unit
unharmed. 512 native launches, 6,912 substeps and 2,048 launch/tick snapshots
match for ballistic motion; 20 Aramon/Taros Archer aim fixtures also match
native. The dispatch probe intercepts the damage routine, so native damage
magnitude and all terrain/feature impact variants are not yet proved. Matched
live Archer behavior and pixel-level comparison of fire effects remain open.

### Retail driver and live sea-carrier round trip (2026-09-23)

The direct driver now also provides `doubleclick`, sending two physical button
presses in one bridge process so retail's double-click timing is reliable. The
wrapper automatically chose the Proton runtime for the active game, and its
`doubleclick`, `move`, and `click` commands were exercised in the 640x480 client.
`livesample.py` remained read-only throughout.

On Aibel's Seaport, retail accepted a Sea Fort in open water, completed it at
`(2480,57,592)`, and offered a Transport Ship for production; the 4x4 ship
completed as entity 66. We selected the ship's Load command and targeted Kienna
through the minimap. Retail displayed “Carrying 1”; entity 44 snapped to the
carrier's XYZ and its loaded-state flags changed. The Unload command targeted
nearby shore. Within two simulation samples Kienna moved off the carrier to
shore `(2214,38,643)`, its loaded-state flag cleared, and the carrier panel
returned to Mana. Captures are `/tmp/retail-sea-retry-seafort-progress.png`,
`/tmp/retail-sea-loaded.png`, and `/tmp/retail-sea-unload-progress.png`; the
read-only live traces are `/tmp/retail-sea-load-watch.jsonl` and
`/tmp/retail-sea-unload-watch.jsonl`. This confirms one live sea pickup/unload
cycle; it does not cover a full passenger capacity, combat interruption, or
air-transport pickup and unload.

### Air transport and cursor follow-up (2026-09-23)

The offline native air-unload audit found no supported production mismatch:
retail `0x41ae20` uses `transportdistance - 34`, polls with `rand(6)+6`, steps
out at radius 16, then performs the one-tick passenger transfer; local mission
sleep/unload handling matches. Native controller probes with target Y values
from 0 through 300 converge to terrain height plus the unit's flight altitude,
also matching local cruise-height behavior. Existing air-unload,
flight-arrival and arrival-wake tests pass. The paired dispatcher trace is now
covered by `tools/re/probe_transport_air_unload_callbacks.py`: it runs retail's
`0x41ae20` handler through `0x4d8450`, installs the real flight-point
controller, invokes the navigator arrival callback, and compares 17 ticks with
the World fixture. Controller goal/radius, event timing, transfer effects,
passenger release, PARK state, and the native one-tick empty-mission tail now
match in Release, Debug and optimized builds. This probe holds route integration
at the boundary, so it does not establish flight-route parity. The probe and
focused transport CTests pass in all three configurations.

The final cursor correction now matches native per-axis squared-product high
dwords before summing, including fractional diagonals and full signed 16.16
coordinates. Weapon eligibility uses FBI `damagecategory`, not the broader
simulation category list. Focused extracted-asset cursor tests and `cursor_roster`
pass in Release, Debug and optimized builds. At this earlier checkpoint,
`Airstrike` remained unmapped because native checks UnitDef `+0x264` and the
active WeaponType `+0xc8`; the next cursor audit below resolves both sources and
records the local mapping.

### Retail direct text input and live air-carrier setup (2026-09-23)

`tools/re/retail_driver.py` now includes `text ASCII`. The Win32 bridge maps each
printable character through the active keyboard layout and sends real key events
to the foreground Kingdoms window. This avoids repeating one bridge process per
character for chat commands. Strict MinGW compilation (`-Wall -Wextra -Werror`)
and Python byte-compilation pass. In the paused live retail match, `key 13` then
`text +atm` displayed `+atm` in the game chat; submitting it refilled the
observed player mana from 3,944 to its 5,200 capacity. The process sampler stayed
read-only.

The no-AI skirmish lobby rejects a match, and retail also rejects a match where
all occupied player slots share a team. A Zhon-versus-Veruna Crusades setup was
therefore used for the attempted live Roc chain. At speed 2, a Beast Handler
completed but the AI reached and defeated the player before the next tier. A
second run after reducing the lobby speed setting to 1 accepted a Beast Tamer,
but the AI closed on the base before the Beast Lord/Roc stage. Captures include
`/tmp/retail-zon-handler-completion.png`,
`/tmp/retail-zon-tamer2-queued.png`,
`/tmp/retail-zon-tamer2-progress2.png`, and
`/tmp/retail-zon-slowed.png`. These runs verify the direct driver and the Zhon
build-menu steps only; they do not establish an additional live air-carrier
pickup or unload. The earlier live Roc pickup/unload sequence recorded above
remains valid evidence; these failed fresh setups do not supersede it. Exact
live tick/landing-site correlation and air/sea route-search parity remain open.

### Surface unload arrival callback reaches retail's native circle controller (2026-09-23)

Added `tools/re/probe_transport_surface_unload_callbacks.py`. Unlike the earlier
surface-unload dispatcher trace, this runs retail's real `4d8450` / `408d50`
path through circle-controller construction (`4e2500`) and installation, then
calls the real navigator arrival callback (`4e5150`) with a completed endpoint
inside the controller's exact goal. The native constructor stores center cell
`(31,31)` and radius `116` for the `(500,500)` unload site and a 150-range ship;
the native navigator retains its initial `(100,200) -> (504,504)` segment.
Arrival sets the mission's `0x100` event, detaches the controller, and the next
dispatcher tick advances the unload handler into passenger transfer. The local
`retailTransportUnloadApproach` reports the matching completion result; its
`World::tickGroundMission` wrapper consumes that completed approach leg.

The probe controls the completed route endpoint. It validates native goal setup,
arrival notification, and handler handoff, not retail route search or physical
waypoint movement. `transport` and `transport_roster` CTests, native surface
pickup callbacks, native surface-unload dispatcher, native surface-unload
arrival callbacks, and the paired 17-tick air-unload callback trace all pass in
`build-o2`.

### Batched live retail input (2026-09-23)

`tools/re/retail_driver.py sequence PATH` now reads a timed sequence and sends it
through one helper process launched by the exact Proton runtime and prefix of
the running retail process. Sequences support waits, mouse motion and clicks,
keys, hotkeys, and printable text; `sequence -` reads from stdin. This avoids
restarting the helper between each UI action and records a result for every
step. The bridge still targets the enumerated retail window and uses physical
Win32 input for mouse actions.

On the live 640x480 retail client, the original single-click command opened the
single-player lobby. Sequence mode then moved the cursor and clicked the Aibel's
Seaport map row; the captured lobby shows that map selected. Python compilation,
strict MinGW compilation (`-Wall -Wextra -Werror`), and `git diff --check` pass.
This improves reproducibility of live probes; it does not by itself close the
remaining paired transport-route or rendered-animation comparisons.

### Water-domain carrier search fixtures (2026-09-23)

`tools/re/check_cost_search.py` now adds eight floater/boat-profile route fixtures
to the native-vs-World cost-search comparison. They configure a 4x4 floater
with retail's water-movement flag and use a controlled grade plane where grade
6 is navigable water and grade 0 is shore. The layouts include open water,
peninsulas, islands, and a narrow channel. All 2,151 heap pops match retail's
emulated cost-search state and heap ordering. The full run also passes 240
native cost profiles, 240 World cost profiles, two general route fixtures, and
28 resumable-slice boundaries.

Run with `python3 tools/re/check_cost_search.py build-o2/retail_cost_test --cases 2`.
This verifies the water-movement cost profile through routed search;
the grade plane is controlled and does not prove retail's actual map-water
sampling, navigator waypoint movement, or full live carrier mission parity.

`tools/re/check_surface_unload_route.py build-o2/transport_test` compares eight
boat unload-circle searches. Four rotate or mirror a 64x64 L-shaped coast; one
extends it to a 128x128 long coast; two repeat the original coast with 2x2 and
3x3 footprints alongside the 4x4 baseline; one disconnects the destination
water from the carrier's starting pond. Each fixture exports the effective
grade plane from the actual World's unload-circle search, including cached
footprint grades, exploration state, retry state and the mover heading at search
start. Unicorn feeds those grades to retail's circle constructor, reachability
tracer, cost search and route reconstruction. All 37 World waypoints and eight
retail route anchors match in pixel coordinates across these cases (5,775
native grade queries). In the disconnected case World records a search failure
and both implementations return the same one-waypoint partial route to reachable
water. This establishes exact route parity when both searches receive the same
World-produced grades; it does not establish that retail and World classify
every live map tile identically or match mission retry/recovery behavior.

`tools/re/check_surface_unload_map_route.py` adds five Per Mare Per Terras
real-map cases, loading the shipped TNT terrain and feature plane. The 4x4 boat
starts at `(40,120)`, `(38,120)`, `(42,120)`, `(40,118)`, and `(40,122)`, each
routed toward a nearby shore site. Each four-waypoint route matches retail's
native circle constructor, tracer, cost search and reconstruction at World tick
15; the native oracle makes 564, 678, 568, 621, and 528 grade queries. All five
cases pass in Release, Debug and optimized builds. The native oracle consumes
the effective World grades exported at that search boundary, so these prove
route parity on real map geometry conditional on those grades; they are not an
independent retail-versus-World terrain-grade comparison. Attempts on Cairbray
Coast Landing and Lake Lokken have not produced valid native routes in the
current emulation fixture, so other maps remain unverified rather than counted
as matches.

### Live Zhon construction pose reference (2026-09-23)

A retail skirmish was driven with the batched input bridge and process sampling
left read-only. At tick 3358, retail reported Thirsha at
`(581.65,161,3896.93)` and the Beast Handler construction site at
`(656,62,3936)`: about 84 world units apart horizontally, with Thirsha's
reported Y 99 units above the site. The screenshot is
`/tmp/air-transport-handler-after-atm.png`; the corresponding sampler log is
`/tmp/air-transport-handler-build-watch.jsonl`. This is a useful measured pose
for the Zhon flying-builder offset question, but the sample is not an atomic
render/state capture and does not by itself establish the intended visual
anchor.

`TAK_CONJURE_BUILDER_X/Z`, `TAK_CONJURE_SITE_X/Z`, camera zoom and screen-point
overrides were added to the local debug conjure fixture. A new local capture
uses the same Aibel's Seaport map and the sampled builder/site coordinates:
`/tmp/zhonpose-aibel-local-tick78.png`, with its trace in
`/tmp/zhonpose-aibel-tick78.log`. At local tick 77, about 30 ticks after the
site appeared, Thirsha was at `(574.2,129,3894.2)` and the Beast Handler site
at `(656,62,3936)`. The retail sample was about 28 ticks after its site first
appeared. The local simulated pose is therefore 32 world units lower, 7.45
units farther west, and 2.73 units farther north than the sampled retail pose.

This is a useful state comparison, but not yet a renderer verdict: the 640x480
captures use different effective game-view rectangles and camera transforms
(the local HUD/sidebar occupy more of the frame), and the retail sample was not
an atomic render/state capture. The local height difference could affect the
projected screen position, but matching the map landmarks/camera before judging
the reported north offset remains open. No pose correction is justified from
these captures alone.

### Feature smoke viewport admission (2026-09-23)

Retail's smoke draw routine calls its viewport point test before submitting the
sprite; the rectangle includes all four edges. Feature smoke was the one local
smoke path that skipped this test, so a particle just outside the view could
bleed in when its bitmap overlapped the edge. It now projects the particle
center and shares the same inclusive helper as ordinary smoke, point particles,
and damage flames.

Extended `probe_smoke_particle.py` to exercise retail's real draw routine at
each edge and one pixel outside it, then compare the same cases with the
compiled renderer helper. All eight cases agree. Release, Debug and optimized
clients rebuild; `retail_visual` passes in all three, and all 46 CTests pass in
each configuration. This closes center-point admission logic, not pixel-level
smoke compositing parity in Glide.

A bounded Three Jewels retail follow-up reached the Zhon Beast Tamer but the
allied Veruna AI won before a Roc was available. The run sampled no carrier
transitions and does not change the earlier live Roc pickup/unload evidence.
A passive or externally funded setup is still needed for exact live route/state
correlation.

### Drake fire comparison status (2026-09-23)

The behavior comparison is now covered headlessly. Native `52d290` initializes
the ordinary LOS-flame emitter, sets its end tick from `emittime`, and creates a
500-particle emitter. `52d360` scans silently on the start tick, then re-queries
the muzzle and advances the flame front on active ticks; a collision dispatches
one impact, emission stops at the authored end, and remaining particles drain
after expiry or owner death. World follows those boundaries. Native probes pass
4,096 flame-scan cases, 4,096 emission updates, 64 impact timelines, 4,096
straight-projectile launch cases, and 4,132 FireWeapon callback/reload cases.
World `retail_script` and `flyer_combat` tests pass for Zhon Drake fire in both
balance modes. No behavioral mismatch was found, so no production change was
justified.

The native timeline substitutes collision and damage sinks; it does not prove
damage magnitude or full World target acquisition parity. Pixel-synchronized
Glide captures are not a completion gate under the behavior-based acceptance.
No retail GUI was launched.

### Paired sea-unload dispatcher timeline (2026-09-23)

`probe_transport_surface_unload_callbacks.py` now runs the native ground-unload
mission from the actual circle-goal constructor and `4e5150` arrival callback
through cargo release, PARK, and mission retirement. A matching World fixture
injects the same route-arrival event at the boundary; it does not emulate
physical route movement. Comparing all 19 ticks exposed two timing differences:
World started the beam on the arrival tick, and it removed the completed unload
order immediately. World now defers transfer until the following tick and
retains the empty mission for its native one-tick wait. The full dispatcher rows
match in Release, Debug and optimized builds. Together with the air-unload
timeline, this closes the controlled transfer/retirement phase comparison; route
search and movement, retries, recovery and interrupted-trip resumption remain
open.

### Paired surface-pickup callback timeline (2026-09-23)

Extended `probe_transport_surface_pickup_callbacks.py` through the native
dispatcher while the passenger is in range. All 18 stage, transfer-clock,
retry-count and retirement rows match `transport_test --pickup-timeline` in
Release, Debug and optimized builds. Attachment host behavior is checked
separately, and this controlled in-range trace does not cover route generation
or movement. Together with the existing 36-row native air/sea pickup-dispatch
trace, it closes the controlled pickup transfer-clock comparison; live route
generation and interrupted-trip behavior remain separate gates.

### Distant air unload exercises physical flight movement (2026-09-23)

Extended `airAcrossWater()` in `transport_test.cpp`: after the carrier collects
a passenger across the water, it unloads an out-of-range destination at
`(1480,1200)`. The test requires a routed approach order, more than 150 units of
carrier travel, cargo release at the selected site, and the passenger's PARK
order to clear the landing footprint. It passes in Release, Debug and optimized
builds; the full 46-test CTest suite also passes in each configuration. This
proves the World path executes through actual flight movement for this case;
retail-versus-World physical flight trajectory parity remains unverified.

### Native sea-unload route installation and 32 movement steps (2026-09-23)

`check_surface_unload_route.py` now lets retail's real `0x414450` reconstruction
call the real navigator setter at `0x4e4ea0`; it no longer substitutes a route
receiver. The exact circle is created by `0x4e2500`. The World fixture waits for
its delayed path result at tick 15, then exports that request's `0x4139d0` grade
plane and movement profile. Native and World route output matches in all eight
controlled water-corridor variants, including mirrored paths, 2x2/3x3/4x4
footprints, a long map, and an unreachable partial route. For the canonical 4x4
L corridor, native stores six points including the start; World stores the five
following points:

`(160,160) → (176,144) → (528,144) → (592,208) → (592,480) → (656,544)`.

That final point is inside the authored `(640,640)` unload circle. The canonical
World search profile is turn rate 10000, 4x4 footprint, road multiplier 78643,
water multiplier 65536, heading 40960, and heavy-floater mode. The exported
64x64 grade plane contains 3186 grade-5, 750 grade-0, 89 grade-6, and 71 grade-4
cells. Its native circle fields are center `(38,38)`, radius 116, and squared
radius 53; the World mission stores radius 112 and adds the same 4-unit search
tolerance.

The fixture seeds both movers at the delivered segment origin and calls retail
`0x4dc800` plus position commit `0x51b2a0`. Its first run exposed a fixture-state
mismatch: World ran its boat terrain scan and entered movement/speed modes 1/2,
while the minimal native unit had no matching map/body grid and remained in
modes 0/0. Those different scan results changed the steering aim; the resulting
2-BAM heading difference at step 2 was not an `atan2` rounding discrepancy.
The durable movement comparison now holds the scan deadline beyond the tested
window on both sides and compares the same 0/0 movement modes. It asserts raw
position, heading, speed, movement mode, speed mode, and road/water flags for 32
consecutive physical steps; every field matches. The synthetic unit has no COB
VM, so only its unrelated Turn-script dispatch is stubbed. This proves the
delivered sea route feeds 32 matching native/World mover updates under an
explicit shared scan state. Live native terrain scanning (`0x4dba80`), travel
through later route waypoints, and arrival callbacks during physical travel
remain open; route callback/dispatcher state is covered by its separate controlled
trace. No production navigation code changed. The focused route probe and
`ctest --test-dir build-o2 -R '^transport$' --output-on-failure` pass.

### Surface unload first physical waypoint transition (2026-09-23)

Extended `tools/re/check_surface_unload_route.py` to follow the same delivered
L-corridor route for 100 native and World movement ticks and compare route
cursor/controller/event state on every step. The native side still runs the
actual `0x4dc800` mover update and `0x51b2a0` position commit; the observed
vtable callbacks are `0x4e5150` (arrival/cursor check, 100 calls) and
`0x4e50a0` (route-point pop, one call). The first route point is consumed at
physical step 46: the native count drops from six points, including its anchor,
to five; World drops from five queued path points to four. The remaining four
points agree at every subsequent step. World is still targeting `(176,144)` at
step 45; after the pop, both sides target `(528,144)`, followed by
`(592,208)`, `(592,480)`, and `(656,544)`. Position, heading, speed, movement
and speed modes, terrain flags, route points, and active circle controller all
match. Retail keeps the exact `(38,38)` / radius `116` circle controller active;
its mission field `+0x6a` remains `0x1000`, matching World pending state, while
neither side raises the final-arrival `0x100` event at this intermediate
waypoint.

The route used by retail is generated from the effective grade plane exported
from World after its real delayed unload-circle request. The harness seeds a
shared post-delivery state, pins terrain scanning beyond the tested window, and
provides the minimal empty native body-sector index needed by this isolated ship
fixture. It does not compare live native terrain scans, body collisions with
other units, or the full match dispatcher during physical movement. It also
does not physically travel the later route points or reach the final unload
circle; final circle arrival and dispatcher handoff remain covered only by the
separate controlled `probe_transport_surface_unload_callbacks.py` fixture.

The updated eight-variant route probe, including all 100 movement ticks and the
first route-point callback transition, passes with the Release binary in `build`
and the Debug binary in `build-o2`; focused `transport` CTest passes in both.
The first 32 physical steps are included in these 100-step comparisons. No
production navigation code changed.

### Weapon callback admission: range and visibility correction (2026-09-23)

The earlier inference that a pending SET 23 can release after a target moves
out of range was incorrect. Re-reading 52ae90 shows that a failed 52fe30
AimWeapon-start check branches around readiness, FireWeapon, and the later
SET-23 bit check. The common updater therefore retains a pending SET 23 while
the target is out of range and releases it only after range admission resumes.
`probe_weapon_gate_loss.py` executes this native path with the actual target
lookup, target-alive checks, base inclusive 2D range virtual, and callback
order; script-piece geometry, readiness result, display transport and projectile
creation are controlled. It verifies callbacks at exactly 400 px for a 400 px
range and rejects 401 px.

The same probe supplies a zero-filled current-player visibility grid while
keeping the target's native unit-table entry live. 52ae90 still dispatches
AimWeapon and FireWeapon. This establishes that this common update's callback
admission has no direct fog-of-war/LOS predicate after target lookup and range
admission. It does not execute the full unit-update caller or establish whether
an upstream target-acquisition path admits an unseen target.

A World regression places a real terrain blocker on the ranged shot line and
confirms that `tickCombat` withholds AimWeapon/FireWeapon, projectile creation,
and mana expenditure until the obstruction is removed. Separately, native
52a4d0 probes establish environmental projectile collision against feature and
terrain cells, and the 52d360 timing probe establishes delayed impact dispatch.

`tools/re/probe_ballistic_blocked_target.py` now runs the same aimed Arabow
trajectory through native `52bdf0` aim, `52be80` launch, `52bf90` update,
`52a4d0` map collision and `51f340` unit-quad collision. It uses actual native
64x64 cell and unit tables. With a feature at `(448,400)` and an enemy at
`(520,400)`, the arrow hits the blocker at `(452.987,20.647,400)` and calls
`529c10` with a null unit pointer. Removing only the blocker lets the same
trajectory hit the enemy at `(505.974,20.175,400)`. This resolves the collision
choice that was previously unknown: the obstructing feature takes the native
impact path before the unit behind it. The existing World regression uses the
same source, target and blocker arrangement; it applies the Arabow's authored
476 damage to the feature and leaves the target at full health. The focused
`retail_script` CTest passes in `build-dbg`.

The probe intercepts `529c10` at its entry to record the native impact target,
so it does not execute the later native feature/unit damage routines. Exact
native damage magnitude and area falloff therefore remain unverified. This is
the remaining blocker to claiming full World blocked-shot damage parity; the
bounded collision-selection comparison found no mismatch and justifies no
production change.

### Surface unload physical route through transfer and coast (2026-09-23)

`check_surface_unload_route.py --steps 1000` compares all eight boat circle-route
variants and follows the canonical sea corridor for 1,000 paired physical ticks.
The trace uses retail's actual `0x4dc800` mover and `0x51b2a0` commit from a
shared post-search state. It compares position, heading, speed, movement/speed
modes, terrain flags, and active navigator/circle state. The mission and cargo
fields match through transfer and mission release; physical motion continues to
match until the boat stops after the unload. The trace asserts 1,000 native
`0x4e5150` checks and the three observed `0x4e50a0` route-point pops at steps
46, 256, and 353. Native `0x4daf8f` passes the active mover's low mode bits to
`0x507d10`; the fixture now sets bit 0 so the real mobile-footprint loop runs.
It also writes each native map cell's high/low terrain samples from the same
four-corner plane used by World. The missing ship hull-height mesh callback
`0x51ad20` is held at the seeded Y value, so the test isolates route, placement,
and X/Z mover behavior from model-specific waterline interpolation.

The extended trace exposed three World gaps. A polled unload retry must rebuild
its segment from the saved mission site rather than the partial route endpoint;
the circle arrival can precede the end of the transfer, so its mover still has
to brake; and the empty-carrier release tail must preserve residual motion. The
World now re-anchors on the exact requested site during the retry, keeps surface
movement active while the transfer runs, reproduces the native `0x500` release
events, and lets the boat coast after the unload mission leaves its queue. The
synthetic fixture now includes a dry passenger drop cell with water around it,
so World and the native callback exercise the same valid unload site.

The controlled trace shows transfer-route overlap at physical steps 489–495,
circle-controller arrival at step 496, passenger release at step 505, and the
native mission pointer clearing at step 506. Route cursor assertions stop when
that mission is detached; the native navigator retains stale point storage
while both movers coast. The earlier step-561 divergence came from an incomplete
native fixture: its mover low bits selected the non-footprint placement path and
its cell records did not carry the four-corner height envelope. With those inputs
corrected, the actual native mobile-placement routine rejects the same shoreline
step as World, and the two movers match through step 1,000 and stop together.

The test seeds retail from the effective grade plane exported by World, pins
terrain scanning beyond the trace, and holds both movers in scan modes 0/0. It
does not compare independent live-map grade generation, nearby-body collision,
or a complete native match dispatcher on every movement tick. All eight route
variants and the 1,000-step trace pass with the Release transport fixture; the
focused `transport` CTests pass in Release (`build`) and Debug (`build-o2`).

### VTOL unload-circle flight mover (2026-09-23)

check_air_flight_motion_trace.py --unload --steps 470 pairs the World flight
update for a distant air unload with retail's actual point-controller setup,
0x4dc800 mover, heading update, and position commit. The controller uses the
drop point as its center and the native transportdistance-minus-34 radius. All
470 ticks match for position, altitude, heading, speed, 3D velocity, and
navigator outputs, ending immediately before the unload handoff. The generic
point-flight case was later corrected to use retail's default acceptance radius
and extended through arrival and coast; see the following section. The native air-unload
dispatcher trace separately verifies its mission and controller callbacks
through passenger release.

World::unloadAt now keeps this approach controller on the active unload mission
for a distant VTOL drop, so flight movement brakes on the circle before starting
the transfer instead of first reaching the exact point. transport_test checks
the controller goal and radius, then exercises the complete cross-water pickup
and distant unload. Release and Debug focused transport tests pass,
as do both native paired flight traces and the 17-tick unload dispatcher
comparison. After this production change, both builds were rebuilt and their
full 46-test CTest suites passed.

This controlled mover trace pins terrain scanning and does not call the full
native mission dispatcher during each physical flight tick. At tick 471, World
starts the transfer and retargets its navigator; that dispatcher transition is
therefore outside the paired mover window. Live map scanning, nearby-unit
collision, and full route/arrival integration remain open.

### Ordinary point-flight arrival step (2026-09-25)

The corrected native point-flight fixture uses the controller's default 0.5px
acceptance radius and follows the flight through arrival and coast. Retail posts
arrival and detaches the controller before `0x4dc800` commits one final mover
step. World previously erased the move order and skipped that body update; it
now retains the accepted destination with an inactive controller, commits the
same final step, and coasts using the retained-flight path. The paired trace
matches all 600 ticks exactly for position, altitude, heading, speed, velocity,
and navigator state in Release, Debug, and optimized builds. The earlier
128-tick fixture used an artificial 16px radius and ended before arrival, so it
could not catch the mismatch. This is a controlled direct-flight case, not a
map or COB pose sweep.

Reproduce with:

```sh
PYTHONPATH=tools/re python3 tools/re/check_air_flight_motion_trace.py \
  --binary build/transport_test --steps 600
PYTHONPATH=tools/re python3 tools/re/check_air_flight_motion_trace.py \
  --binary build-dbg/transport_test --steps 600
PYTHONPATH=tools/re python3 tools/re/check_air_flight_motion_trace.py \
  --binary build-o2/transport_test --steps 600
```

### Authored projectile sprite anchors (2026-09-23)

The generic `weaponart` draw path now places each selected TAF frame using that
frame's authored `(ax, ay)` offset. Retail's shared sprite backend applies the
same offsets after projecting the projectile anchor; centering by frame size
misplaces authored shots such as the 63x65 FireballB frames anchored at `(31,33)`.
The `retail_visual` test checks the offsets at native and 2x zoom, and native
straight-shot, ballistic-render, flame-render, and frame-budget probes pass.

This fixes sprite placement only. Retail GuidedWeapon steers and integrates X,
Y, and Z, while World's guided path still uses planar movement and a renderer
height approximation. A faithful correction needs the launch point, velocity,
target steering, substep cadence, and collision path to share the 3D state; a
render-only altitude adjustment would leave flight behavior inconsistent.
Pixel-level Glide comparisons for authored projectiles remain open.

### Feature smoke body-frame sampling phase (2026-09-23)

Added `probe_feature_smoke_clock_phase.py`. It executes retail's actual feature
smoke body (`495ce3..495e2e`), uses the real current-frame lookup (`536400`) and
advances the same burn-body clock with native `5373d0` after each sample. Only
world-position lookup, CRT rolls and final particle insertion are controlled.
Across 128 synthetic sequences, 2048 smoke requests use the frame active before
that tick's clock advance; the request origin matches that frame's dimensions
and anchor. This closes the body-frame read-vs-clock-update ordering within the
feature callback.

The client now names this pre-advance age mapping in
`retailFeatureSmokeAge`: for a controlled burn start, elapsed tick one samples
age zero. The `retail_visual` test compares the helper and frame sampler against
the single-step clock across zero/long delays, looping, expiration and tick wrap.
The native probe and focused CTest pass. Ignition's placement relative to the
world tick remains unproven, as does paired Glide capture; this controlled
sequence does not close those parity gates.

### Feature smoke active-list and CRT ordering

Added `probe_feature_smoke_order.py`. It executes native burn-slot allocation
and list movement (`4949c0`/`494a80`) for a synthetic free-list order of 13, 2,
9. The active list becomes 9, 2, 13: `494a80` puts each newly active burn at
the list head, and `4959c0` walks the next-slot link. In the same per-burn loop,
retail updates that feature's existing smoke list (`495a83`) before requesting
its new smoke emission (`495e29`).

The local consumer has different ordering. `consumeSmokeTicks` advances all
existing feature smoke sprites through the ID-keyed `std::map` first, then
emits from the tick's feature events. `captureTransportEffects` creates those
events by walking `World::features()`, whose setup insertion is row-major with
cell-derived ascending IDs. The native callback instead interleaves each
feature's existing-particle update and emission in active-burn order, newest
active burn first. A particle countdown expiring during this pass therefore
changes which later emissions receive each random value; multiple active fires
also need not have the same order in both implementations.

The native per-thread CRT initializer (`5dc3f0`) sets the random state to 1,
which the probe verifies alongside actual `5d4444` draws. Thus the initial seed
is representable, and it matches the client's fresh `smokeRandom_` value. The
full stream is not: retail shares that TLS state with entity allocation and
other effect/rendering consumers, while the client advances a private visual
stream. The controlled no-existing-particles case proves that identical seed
and accepted three-draw emissions assign different draw groups when native
newest-first order is compared with ascending local IDs; it is not a whole-tick
replay. No production change is recommended from this probe alone because
sorting emissions cannot restore the shared CRT call sequence. Treat exact
cross-feature smoke randomness as a documented limitation until that shared
stream and its consumers are modeled. The explicit dispatcher `srand(1234)`
call is in the `-AutoStop` branch; it does not establish a normal-game seed.

### GuidedWeapon XYZ kinematics helper (2026-09-23)

`retailguided.h` ports the native launch/update chain: GuidedWeapon initialization
at `0x52c540`, 3D steering at `0x52e030`, and XYZ substep movement plus collision
at `0x52c6d0`. The Unicorn probe executes those native routines while controlling
only the source muzzle, current target SweetSpot, and collision result. Across 384
launches and 768 updates with moving and vertically offset targets, the helper
matches BAM angles exactly; maximum XYZ position and velocity deltas are 35 and 5
raw 16.16 units. Early and midpoint code-2 collisions verify that movement stops
on the impacted substep and records one impact. Exact antiparallel steering with
native angle refresh enabled faults inside retail at `0x2023bc`; that undefined
edge path is not claimed as matched. `retail_visual_test` and the
focused `retail_visual` CTest pass.

### GuidedWeapon World and renderer integration (2026-09-23)

The live Guided path now launches from the authored QueryWeapon muzzle, uses the
native XYZ velocity and BAM orientation, steers toward the current target
SweetSpot, and integrates/collides on every substep. A lost or embarked target
leaves the existing trajectory unchanged. Nimbus delay, pre-start owner checks,
range lifetime and one-shot impacts are carried through the World path. The
lockstep hash includes owner, slot, target, timing, life/spent state, XYZ
position/velocity and BAM angles. Projectile models, authored sprites and
fallback effects use the simulated world Y rather than an interpolated flight
height.

The `retailgap` World regression confirms a homing shot curves onto a moved
ground target and applies damage with identical paired-World hashes. A second
case preserves the Fire Demon's authored script/muzzle, keeps a copied Zhon
monarch airborne, and verifies launch, vertical tracking, paired hashes and
continued motion after target loss. The fixture caps HP below the 16.16 signed
limit and uses an explicit flight mission to avoid VTOL standby landing.

The native probe still controls the target SweetSpot and collision return; it
matches 384 launches and 768 moving/vertical updates with exact BAM and maximum
raw 16.16 position/velocity deltas `[35, 14, 28, 5, 2, 4]`. Its early and
mid-flight code-2 cases confirm substep stop/impact behavior. This does not yet
prove live retail environmental collision geometry, all target-loss cases, or
paired Glide pixels. The exactly antiparallel angle-refresh path faults in retail
at `0x2023bc`, so that undefined native edge case remains unclaimed.

After integration, both `build` and `build-o2` compile all targets and pass all
46 CTest cases. The focused native Guided, cursor Airstrike and air/sea unload
cancel/reissue probes pass against the rebuilt binaries.

### Same-trip sea-unload transfer retry (2026-09-23)

Extended `probe_transport_surface_unload_retry_cancel.py` with a native
same-mission recovery trace. The real sea-unload dispatcher reaches retry stage
3 after strict placement fails and the relaxed moving-blocker check succeeds.
The probe then makes the selected in-range landing cell available, keeps the
original mission pointer and destination bytes intact, and advances native
dispatch through the ten-tick wait, restarted transfer, passenger release,
PARK, and mission retirement. The passenger releases on tick 30 and the
original mission retires on tick 31. No replacement mission or mission-removal
call is used in this trace.

World's existing `exactLandingSites` regression exercises the same in-range
same-trip retry for both air and surface carriers, including the full restarted
transfer interval and release. The native probe and `transport_test` pass.

The follow-up out-of-range trace moves the carrier to `(400,160)`, well beyond
transfer range of the `(500,500)` landing site. The original mission and
destination remain intact; retail destroys the old controller, requests a route,
and installs a fresh `0x5f28d8` circle controller at `(31,31)` with radius 116.
The paired route/mover fixture starts from that exact remote position and landing
site. Its composed dispatcher and physical traces agree on the route and radius;
the mover matches for 1,000 physical steps, reaching the replacement circle at
step 317, releasing cargo at 334 and retiring the mission at 335. The dispatcher
and mover were separate emulator fixtures at that stage. The later integrated
same-object trace is recorded below. Crowded-shore interactions and live-map
transport tests remain open.

### Airstrike HUD and ballistic environmental collision integration (2026-09-23)

The local FBI loader now retains `dropped=` as the native Airstrike cursor flag,
separate from `subtype=Dropped`, and preserves its original WEAPON1–WEAPON3 slot
when damage-less entries are omitted from the local weapon vector. Armed attack
cursor selection applies the native numeric minimum across selected units:
Attack wins over Airstrike, which wins over TooFar. Unit tests cover active-slot,
WEAPON1 and mixed-selection behavior; the three native cursor probes pass.
Shipped FBI content has no `dropped=` entries, so this currently protects
authored/mod content.

World ballistic projectiles now check the native 3D collision routine at every
substep, including terrain and features, and keep their 2D impact coordinates on
the same XYZ trajectory. The native Arabow feature-hit probe confirms one
environmental impact dispatch with no unit target; the World regression applies
the authored damage to the blocking feature without reaching its selected unit.
Native projectile, cursor and retry-route probes all pass. Release, Debug and
optimized builds pass all 47 CTest cases each (141 passing executions). A
representative review of the complete animation behavior remains open; pixel
identity is not required. No retail game GUI was launched for this verification.

### Movement callbacks, flight events and integrated sea retry (2026-09-23)

Retail `0x4dc800` dispatches `TurnDirection` during the mover, then the common
tail calls `MoveRate` and `setSFXoccupy`. The display path now preserves that
order when all three values change together. `check_movement_callback_order.py`
captures the native sequence `TurnDirection(-135)`, `MoveRate(1)`,
`setSFXoccupy(5)` and compares it with the shared client callback helper; a
second unchanged tick is silent. The helper CTest also checks same-sign turn
changes, reversal and stop.

The follow-up headless profile sweep runs the actual native mover for two ticks
each on shipped ARAARCH, VERTRANS, and VERBALL FBI/COB pairs. It confirms
`TurnDirection(-40) → MoveRate(1) → setSFXoccupy(4)` for ARAARCH,
`TurnDirection(-40) → MoveRate(1) → setSFXoccupy(2)` for VERTRANS, and
`TurnDirection(-135) → MoveRate(3) → setSFXoccupy(5)` for VERBALL; each is
silent on the unchanged second tick. The test verifies the callback methods
declared by each shipped COB and compares each profile's request sequence to
the shared World helper. It captures requests before COB method lookup, so it
does not prove full COB execution or rendered performance across the roster.
The Python probe and focused helper CTest pass. No retail GUI was launched.

A later roster pass extends callback-request capture to all 151 movable shipped
COB/FBI pairs declaring one of those methods. All native mover requests match
the World helper across six raw request patterns and eight relevant inputs.
Full native/local COB thread and piece-state comparisons also pass for ten
representatives across ground, hover, water, and flight (1,202 boundaries per
script). Those ten callbacks come from a controlled schedule rather than each
unit's native mover, so an end-to-end mover-to-COB execution comparison across
all 151 units remains open.

Reproduce with:

```sh
python3 tools/re/check_movement_callback_order.py --binary build-o2/retail_movement_animation_test
ctest --test-dir build-o2 -R '^retail_movement_animation$' --output-on-failure
```

`BeginFlight` call-ins now carry a display-only serial through the render
snapshot. The display VM receives every call-in, including one that occurs while
the flyer is still landed, and avoids duplicating the later airborne-mode
fallback. A simulation test confirms flight-move dispatch increments the serial
without changing lockstep state; the display test covers pre-takeoff delivery,
same-tick takeoff deduplication and multiple call-ins in one snapshot. The
landing probe continues to pass all 2,048 native stage, callback and RNG cases.

The sea retry trace now retains one live native carrier, passenger, mission,
mover, navigator and replacement controller across retry, retail route search,
and physical movement. Against the World route it matches 1,000 steps, including
five route-point transitions, controller detach at step 317, cargo release at
334 and mission retirement at 335. The broader sea-route suite also passes all
connected-water variants and the disconnected partial route. These tests still
inject scheduler request events and use World-produced grade data; they do not
close crowded-shore or live-map interaction coverage.

The full 47-case CTest suites pass in Release, Debug and optimized builds.
After the final test-only edits, the affected `retail_motion` and `retail_visual`
tests also pass in all three configurations. Focused feature-smoke,
movement-animation, native mover-callback and landing-callback probes pass. The
retail game GUI was not launched.

### Behavioral accuracy acceptance bar (2026-09-23)

Animation and transport review targets an accurate representation of retail
behavior, not pixel identity. Completion checks should cover the visible action
and its state transitions: what moves, where it faces, when it starts/stops,
how long it persists, and how it interacts with units, terrain, and missions.
Pixel-difference captures remain useful diagnostic evidence, but a remaining
pixel-only comparison is not an open completion gate. Any visual difference
that changes the apparent action (for example, an arrow that reads as a beam)
remains a behavioral issue to investigate.

### Queued mobile-builder animation transitions (2026-09-23)

The display previously represented every nonempty mobile production queue as
one constant work ID. Consecutive output sites therefore did not retrigger
one-shot `StartBuilding` performances, and a capped queue with no live output
site could animate as if work were underway. `UnitR` now carries
`productionSiteId`, and the display transition keys the performance to that
active site. A shared regression covers no-site waiting, consecutive sites,
stopping while the queue waits, placed builds, and a moving active flyer. With
`zonhand.cob`, it drives the display VM and observes `StartBuilding`,
`StartBuilding`, then `StopBuilding` state writes.

The focused builder-animation test, `conjure_test`, relevant movement/script
tests, and the native `0x41ef00` orbit probe pass. The new builder-animation
case is included in the full 48-test Release, Debug, and optimized suites.

### Crowded-shore unload retry (2026-09-23)

A World test routes both air and sea carriers to a real shoreline landing cell,
then moves a unit into that exact cell at transfer range. It verifies the
passenger stays attached while the carrier moves, the site retries after the
blocker clears, and the passenger releases at the selected point; the sea case
also confirms the carrier stays on navigable water. This exposed a sea-only
stage-1 retry race: after route and unload had been folded into one order, the
approach dispatcher treated the retry wake as a second approach completion and
removed the combined order, stranding cargo. The dispatcher now still allows a
stage-1 retry to re-anchor when out of transfer range, but it only folds an
approach into an unload when a separate unload order remains.

The transport World regression and all 48 CTests pass in Release, Debug, and
optimized builds. Native surface retry/route probes, 71 sea and 100 air unload
cases, and a 500-tick integrated air-unload trace pass. Native fixtures still
control scheduler requests and selected placement/effect callbacks; the World
regression exercises live terrain, body placement, routing, movement, and
attachment. No retail GUI was launched.

### Deferred combat-script callback phase (2026-09-23)

Retail's `52fe30` AimWeapon, `530140` FireWeapon, and `51a7f0` TargetCleared
call the script dispatcher `56c640` with immediate execution disabled. The
dispatcher installs the COB thread; it calls the VM tick routine `56c870` at
start time only when that flag is enabled. `probe_weapon_callback_phase.py`
executes the native `56c640/56c680` path for all three callbacks, checks that
each thread remains at its entry with its arguments intact, then runs the next
regular VM pass. An immediate-mode control confirms the distinction.

World had started these callbacks and also run `state.tick(file,0)` inline in
the same combat update. That could let AimWeapon SET 22, FireWeapon SET 23, or
TargetCleared SET 21 affect readiness, projectile release, or aim cancellation
one update too early. The inline pass is removed; the existing per-unit COB
phase runs the queued callback on the following update. Display events are
still recorded at native callback-start order. The live regression now verifies
that retargeting reports TargetCleared first and starts the replacement aim
after the script phase resets the old handshake; pending release cancellation
also takes effect in that regular phase rather than inside target retirement.

The native callback-phase probe, native range/visibility and weapon-update
probes, and focused Release `retail_script`, `retail_visual`, `naval_combat`,
and `flyer_combat` CTests pass. This closes callback-start scheduling for these
three script entry points; it does not establish the complete native unit
dispatcher phase or remaining range/visibility, special-weapon, and animation
roster behavior. No retail GUI was launched.

### Death-script SFX attachment lifecycle (2026-09-23)

The display VM continues `Killed`/`Dying` after the authoritative script host
stops, so death-time `EMIT_SFX` callbacks need a separate capture path. An
initial all-roster sweep incorrectly counted live `Create` child threads: the
production death edge resets that VM before starting `Killed`/`Dying`. The
corrected sweep follows that reset, clears static slot 0 as GameView does, and
scans each shipped death timeline for 600 ticks. It finds attached damage-flame
codes 260/261/262 (in `tarmage`, `tarhel`, and `crefire`); the latest emission
is at tick 72. It finds no death emissions for smoke codes 257/258/265 or
detached codes 263/264. In particular, `zonfire` 257/262 and `verpill` 264
were live `Create` emissions, not `Killed`/`Dying` effects.

The display VM now captures only death codes 260–262 at the EMIT_SFX
instruction, using the same retail fixed-point piece-origin routine as the
simulation host. These events enter the existing per-owner damage-flame lists,
preserving their 40-particle cap, class selection, authored animation clock,
and 15-tick sprite lifetime. The existing simulation-side transient bridge for
live script emissions of 263/264 remains in place; the death capture does not
claim those codes. `animation_roster_test` reproduces reset-separated callback
ordering, checks all 600 ticks, requires each confirmed flame family, rejects
live-only families in death callbacks, and verifies attached emissions finish
before the 120-tick body handoff.

`probe_death_sfx_lifecycle.py` compares the retail VM's actual `0x50da20` death
SFX dispatch against World for `tarmage`, `tarhel`, and `crefire`, across three
severities and two damage types. All 312 callbacks and associated
`SET_UNIT_VALUE` writes match; native emission ends by tick 72. The probe
controls only the final native creation sink, after retail has run the VM
callback, piece-origin lookup, and SFX dispatcher. A separate native destructor
probe confirms teardown drains its attached list. The new
`probe_native_death_update_edge.py` also runs the actual update chain
`0x5130d0 → 0x512ae0 → 0x4ee560 → 0x497380` with the native removal bit set;
it clears the owner pointer, removal flags, and attached SFX list on that update.
This verifies cleanup relative to the removal bit, not when the script callback
sets it. Callback and creation-dispatch parity for these three flame families
is closed; the timing of the `SET_UNIT_VALUE 31` path is now covered for
`tarmage`. Other attached-emitter families and other SET31-bearing death
scripts remain open. No retail GUI was launched. Reproduce the original two
checks with:

```sh
python3 tools/re/probe_native_death_update_edge.py
PYTHONPATH=tools/re python3 tools/re/probe_death_sfx_lifecycle.py \
  --world-binary build-o2/animation_roster_test
```

World's generic attached-effect fallback survives the HP-death edge through
the existing `kCorpseAnimTicks` body handoff (120 ticks); statue removal
remains immediate. The native SET26/SET31 paths below retire the display-side
owner effects sooner. The sim supplies a raw statue feature ID, while
RenderFrame supplies a boolean; the cleanup guard handles and tests those
representations separately so an ordinary `false` snapshot value is not read
as feature ID zero.

`probe_attached_sfx_teardown.py` verifies that native model teardown synchronously
drains the smoke/damage-flame owner list through its vtable destructor. The
death dispatcher starts `Dying`; `SET_UNIT_VALUE 26` requests teardown on the
next update, while `SET_UNIT_VALUE 31` starts a one-second model timer that
sets the same removal bit when it expires. For example, shipped `araking`
uses value 31 and `araknigh` uses value 26. Native VM traces show `SET_UNIT_VALUE`
31 at tick 0 for `tarmage`/`araking`, 26 at tick 0 for `crefire`, and 26 at tick
18 for `araknigh`.

`probe_native_set31_lifecycle.py` now starts the shipped `tarmage` `Killed` and
`Dying` COB callbacks through retail `0x56c680`, routes `SET_UNIT_VALUE` through
the real `0x50d450` host, and advances the real outer unit updater `0x51d3e0`.
The one-unit fixture supplies the death-status bits that route each pass to
`0x51e380`: the timer is 0.97 after tick 1, 0.01 after tick 33, and zero with
the removal bit set after tick 34. The trace records exactly two real
`0x56c870` calls, both at tick 0 for immediate `Killed` and `Dying` starts.
SET31 sets owner byte `+0x13`; `0x51e380` tests that byte and skips the VM update
for ticks 1–34 while decrementing the timer. Tick 35 consumes the removal bit through
`0x512ae0 → 0x4ee560 → 0x497380`; the owner pointer clears and the attached
one-node SFX list drains. The node is fixture-provided; the real `0x502da0`
effect-creation sink is controlled after retail's `0x50da20` dispatcher. The
probe also controls synthetic game/player/model records, unrelated graphics
and allocator callbacks, the SFX-list update slot, and COB VM shutdown slot
`+0x60`; the COB callback, unit updater, timer, retirement routine, owner
destructor, and list destructor run from retail.

For the same `tarmage` input, World matches all eight tick-0 damage-flame
callbacks, and the standalone timeline harness prints SET31 at tick 0 before
producing 16 more callbacks at ticks 14 and 28. That harness's `onSetUnitValue`
only prints the write; it directly ticks the COB VM and does not run the native
unit/model owner timer. It is useful as a direct-VM script parity trace, but its
later callbacks do not model the render host. GameView mirrors the native
owner stop after SET26 or SET31 and skips subsequent render-frame VM ticks after
that write. It still starts the synchronous `Killed` and `Dying` callbacks on
the death edge. SET26 uses the same stop gate because retail's real `0x50d450` host
sets the native removal bit, and the native update-edge probe proves removal is
consumed on the next owner update. The `--death-render-timeline` fixture shares
the GameView gate: it preserves the eight synchronous tick-0 effects and emits
no later effects, while `--death-sfx-timeline` keeps its direct-VM behavior for
the existing script parity sweep. A second render-host fixture runs shipped
`crefire`: its SET26 at tick 0 stops the two later callbacks that remain in the
direct-VM trace.

This closes the GameView stop condition for the tested SET31 `tarmage` death
timeline and covers the SET26 render-host latch, but does not invoke `0x512610`
to build the death state or run a full `araknigh` callback-to-retirement trace.
The display-side effect list now uses those same tick-35/tick-1 retirement
deadlines. A static audit of the 204 shipped unit COBs found 38 SET31 writes;
each is `(31, 1)` and each script has one such write in `Dying`. Native timer
and owner teardown were run through `tarmage`, while the SET26 path was run
through `crefire`; the other SET31 scripts still need full native runtime
coverage. Repeating SET31 resets the native one-second timer, but the audited
shipped death callbacks do not repeat it. Other attached-emitter families
remain open. No retail GUI was launched. Reproduce with:

```sh
PYTHONPATH=tools/re python3 tools/re/probe_native_set31_lifecycle.py \
  --world-binary build-o2/animation_roster_test
build-o2/retail_visual_test
```

### Live detached script transient lifecycle (2026-09-24)

The headless `TAK_SCRIPT_TRANSIENT_TEST` fixture drives the shipped `verpill`
Create script until it emits SFX 264. It deliberately skips two cosmetic render
ticks, retires the source unit before draining the queued event, and verifies
that the client still creates the authored `pillaroflight` effect at the
captured XYZ and event tick. The detached effect survives through its final
authored tick and expires at the authored duration, independently of its owner.
CTest runs this fixture with SDL's dummy video and audio drivers; it checks
behavior and lifecycle, not framebuffer pixels.

SFX 263's native dispatcher, position capture, projection, visibility, and
shared transient-clock behavior are covered by separate probes. A second
headless fixture now drives Kirenna's real `vermage::MoveControl` water
transition on Lake Lokken, which emits SFX 263. It confirms that the client
retains the authored `deathmagic:purpledeath` asset and emission XYZ/tick across
skipped cosmetic ticks and owner retirement, with expiry at the authored
duration. CTest runs this with SDL's dummy video/audio drivers and
`TAK_SCRIPT_263_TEST=1`; a dry map such as Ulasem Arena cannot trigger the
transition. This checks behavior and lifecycle, not framebuffer pixels.

Run the integrated checks with:

```sh
ctest --test-dir build-dbg -R '^script_transient$' --output-on-failure
ctest --test-dir build-o2 -R '^script_transient$' --output-on-failure
ctest --test-dir build-dbg -R '^script_transient_263$' --output-on-failure
ctest --test-dir build-o2 -R '^script_transient_263$' --output-on-failure
```

### Lake Lokken native sea-route fixture (2026-09-23)

`check_surface_unload_map_route.py` now exercises the shipped 480x480-cell
Lake Lokken map. A 4x4 boat route from `(240,120)` to the circle centered at
`(240,140)` matches retail's reconstructed route at World tick 15: one waypoint
and 84 native grade queries with the synthetic boat profile. With the shipped
`vertrans`/`araarch` profiles, both standard and Crusades match the same single
waypoint at 57 native grade queries; Vertrans's 266px unload circle contains the
endpoint. The Per Mare Per Terras case still matches at
`(40,120)` to `(40,142)`, with four waypoints and 564 grade queries. Both cases
pass with Release, Debug, and optimized World binaries. The Lake Lokken
paired destination is in water, so it checks a connected water crossing rather
than shore cargo placement. The asset-backed route fixture also compares the
long shore approach from `(240,120)` to the unload circle centered at
`(240,350)`. In both balances, the completed search runs at World tick 30 from
`(240,123)` to controller goal cell `(239,349)` and emits the direct route
`(240,123) → (240,337)`. The earlier tick-15 plane belonged to an abandoned
request from `(240,121)`; the checker now selects the completed attempt's grade
plane, heading, weight, and Vertrans profile. World and native reconstruction
match exactly in both balances: native outputs `(3840,1968) → (3840,5392)` and
makes 264 grade queries. The trace's `partialDistance=-1` means retail delivers
this direct route during the first `0x415b10` handoff. The previous fixture
advanced once more into `0x4142c0` without a cost heap; the null root at
`0x414367` was a harness sequencing error, not a route mismatch. The separate
actual-profile World shore unload completes in both balances at tick 2,222.

This exposed an emulation-fixture overlap: the fixed terrain-cell buffer sat
1 MiB before the unit/type/object tables, so a 480x480 map overwrote those
tables. `Phase` now allocates cell planes larger than 1 MiB from its emulated
heap, retaining the fixed address for smaller fixtures that write it directly.
`check_surface_unload_map_grades.py` compares retail's native `0x508cd0`
directly against the World attempt plane over the real route origin. The extended
route checker also reconstructs the same long shore route from native TNT-backed
grades, with the World grade-5 unexplored-cell visibility mask held as a separate
input. The route checker leaves the original `0x4e4ea0` navigator setter in
place and advances the native `0x4dc800` mover and `0x51b2a0` height update over
the same map. It matches World's full movement state for 1,470 ticks in both
balances, and both positions enter the 266px unload circle. For this controlled
trace the scan deadline is postponed equally, leaving movement modes at 0/0;
there are no live occupancy blockers. `check_surface_unload_map_release.py` pairs the completed World
shore unload with retail's real `0x507d10` placement test over the map's
TNT-derived terrain and feature records. In both balances, retail accepts shore
cell `(240,350)` for the 2x2 passenger footprint and releases at `(3848,5608)`
on transfer tick 18. World requests that same release cell; its first observed
position `(3849.11,5607.41)` is after PARK movement has started, with the same
footprint origin `(240,349)`. This checks route-arrival handling, legal coast
placement, detach, and release. The native arrival is delivered at World's
completed carrier position, so movement and transfer are validated in paired
fixtures rather than one uninterrupted native mission with active terrain
rescans and changing occupancy. Reproduce with:

```sh
python3 tools/re/check_surface_unload_map_release.py --binary build/transport_test
python3 tools/re/check_surface_unload_map_route.py build/transport_test \
  --map 'Lake Lokken' --start 240 120 --target 240 350 \
  --carrier vertrans --passenger araarch
python3 tools/re/check_surface_unload_map_route.py build/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier vertrans --passenger araarch \
  --native-map-grades --native-map-mover-steps 1470
python3 tools/re/check_surface_unload_map_route.py build/transport_test \
  --map 'Lake Lokken' --start 240 120 --target 240 350 \
  --carrier vertrans --passenger araarch --crusades
python3 tools/re/check_surface_unload_map_route.py build/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier vertrans --passenger araarch \
  --native-map-grades --native-map-mover-steps 1470 --crusades
for bin in build build-dbg build-o2; do
  python3 tools/re/check_surface_unload_map_route.py "$bin/transport_test" \
    --map 'Lake Lokken' --start 240 120 --target 240 140
  python3 tools/re/check_surface_unload_map_route.py "$bin/transport_test" \
    --map 'Per Mare Per Terras' --start 40 120 --target 40 142
done
```

### Zhon construction-flight arrival and retarget parity (2026-09-24)

`check_zhon_construction_flight_trace.py --persistent` now runs the actual
native `41ef00` dispatcher, point-controller factory and binder, `4dc800`
mover, and construction commit path. The fixture substitutes only OS
allocation/synchronization calls and the unfinished-work seam. Its paired World
fixture preserves an active build order and site while disabling the two COB
threads so the construction mission is the only path-RNG consumer.

The seed-1 trace matches native and World through 103 ticks of position,
velocity, heading, navigation output, orbit goal, and RNG, including native
retargets at ticks 25 and 63. At tick 82, retail detaches its controller and
posts `0x500` but still runs one body step. World previously returned before
that step because the temporary hover order carries a `buildType`; that was
the first north/height offset. `tickConjureHover` now uses the persistent flight
movement path for that temporary flying-build order. World takes the same tick-
82 step, brakes with retail, and selects the same next hover point at tick 101.
The trace stays exact through tick 103. Ground builders use their separate
movement path.

The original 44-tick mover comparisons still pass at seed 50 and
`0xdeadbeef`; the full `conjure_test` passes all 4,477 checks in Release,
Debug, and optimized Debug. Reproduce the persistent and shorter native pairs
with:

```sh
PYTHONPATH=tools/re python3 tools/re/check_zhon_construction_flight_trace.py \
  --binary build-o2/conjure_test --install assets/game --persistent
PYTHONPATH=tools/re python3 tools/re/check_zhon_construction_flight_trace.py \
  --binary build-o2/conjure_test --install assets/game --steps 44
PYTHONPATH=tools/re python3 tools/re/check_zhon_construction_flight_trace.py \
  --binary build-o2/conjure_test --install assets/game --steps 44 --seed 0xdeadbeef
```

This is movement, navigation, and mission parity; it is not a synchronized
render capture. The `zonhunt.cob` oracle still matches across 1,501 boundaries,
including piece/thread/RNG state. The captured-model transform comparison
matches all 2,374 vertices across 293 retail pieces to within 0.000023 world
units. Pixel-level Glide matching is not required, and no retail GUI was
launched for this check.

### DroppedBallistic launch and update oracle (2026-09-24)

The Tarbeak Egg Bomb is a `type=Ballistic, subtype=Dropped` shot with
`weaponvelocity=10` and `cruisealt=200`. Its DroppedBallistic class uses the
shared native 3D updater `0x52bf90`. It reads the weapon type's `+0xd0` field
and loops that many XYZ movement/collision substeps. Native type setup at
`0x531ccb` derives the count from quantized speed as `ceil(rawSpeed / 2^20)`;
the loader at `0x530f21` quantizes FBI speed by `65536/30` and truncates.
Tarbeak's value 10 therefore becomes raw speed 21845 and one substep. Verball's
dropped cannonball at speed 15 becomes 32768 and also uses one. The probe sets
the native field to one and confirms the updater dispatches one collision step.

The custom initializer `0x52c3b0` queries the weapon muzzle with `0x4dd420`,
computes `max(1, floor(sqrt(2 * abs(targetY - muzzleY) / worldGravity)))`,
divides the fixed-point X/Z aim-point delta by that tick count with signed
truncation toward zero, and starts with zero vertical velocity and pitch
`0xc000`.

Before the World correction, dropped bombs instead launched from the unit center
and used a fixed 24-tick fall window. The new
`tools/re/probe_dropped_ballistic.py` executes the native initializer and the
shared update with controlled QueryWeapon/collision sinks. Its 512 varying
height/gravity launch cases and first-update XYZ/velocity/angle states match the
TAK helper (pitch within one BAM unit); negative X/Z cases also check
C-style truncation rather than Python floor division. The probe runs the retail
binary only through headless Unicorn; no retail GUI was launched.

World now launches this as `ballistic3d` from the QueryWeapon muzzle, advances
it with the shared ballistic XYZ/gravity update, and dispatches impact through
the 3D collision path. The renderer routes dropped `ballistic3d` shots through
the native-3D path: projectile meshes use the shot's XYZ position and angles,
including actual Y altitude, while authored sprites use XYZ-derived screen
position and trail direction rather than an invented 2D arc. The synthetic
World regression passed in `build-dbg`: a 100-unit drop takes the expected 40
ticks, hits its ground target for 25 damage exactly once, and retires without an
expiry detonation. The native probe, Python byte-compilation, and `git diff --check`
pass. This validates the launch/update and one-impact behavior; it is
not a pixel-synchronized renderer comparison.

After the World integration, the complete `build-dbg` and `build-o2` CTest
suites each passed all 52 tests. `build-static` and `build-allstatic` each passed
all 36 registered tests, and the native 512-case probe also passed against the
debug and fully static visual-test binaries. The changed client, simulation,
and visual-test targets compile with Clang; its full all-target build stops in
the unrelated `tools/retail_ai_test.cpp` Planner construction test. The Windows
cross-build compiles the changed client source and links the focused retail
tests, but its full client link is blocked by unresolved FFmpeg symbols in that
build configuration.

### Sprite-only BallisticWeapon motion and impact (2026-09-24)

The shipped standard and Crusades weapon inventory includes many ordinary
`BallisticWeapon` shots that have sprite art but no projectile mesh, including
Arapult cannonballs, several faction cannons and mortars, Taros fireballs, and
water balls. Native motion and collision belong to the BallisticWeapon class;
they do not depend on a model pointer. World previously enabled its matching
XYZ path only when `shotModel` was nonempty, leaving these weapons on the flat
projectile path.

World now launches every ordinary non-melee, non-beam BallisticWeapon through
the shared retail XYZ initializer, substep/gravity updater, and map collision.
The renderer already selects a mesh when one is authored and otherwise draws
the weapon sprite at that same 3D position. These shots have no range-based
expiry in retail, so the old distance/velocity timer no longer removes a
pitched shell while it is still in flight. `retail_script_test` covers a
`cannbmed` sprite-only projectile with no mesh: its altitude changes during
flight, its sprite coordinates track the XYZ state, it remains independent of
the flat-shot lifetime, and a feature impact stops it before the selected unit.
The long-running `retailgap` case also verifies a missed Arapult shell still
lands and splashes after the target moves away.

The complete Release CTest suite passes 50/50; complete Debug and optimized
suites each pass 52/52; the static and fully static suites each pass 36/36.
These are behavior and lifecycle checks; no pixel-match comparison or retail
GUI launch was used.

### Projectile model pose and authored asset selection (2026-09-24)

The Archer and bolt appearance audit found that shipped mesh-backed shots use
their authored 3DO models (`araarrow`, `arabolt`, `verhpoon`), while sprite-only
shots such as Arapult cannonballs and Taros fireballs use their authored weapon
art. The standard and Crusades inventories contain no weapon that combines a
projectile model with `weaponart`, so the World mesh path does not omit a shipped
foreground sprite. Retail's ballistic and straight-shot draw probes both pass
the actual XYZ record, all three BAM angles, owner, and selected model to the
native 3DO renderer; World routes those same model shots through the corresponding
3D position and pose.

`check_projectile_model_transform.py` now compares the projectile mesh pose with
retail's native `0x535d50` transform for 4,096 randomized authored vertices and
roll/yaw/pitch combinations. The maximum coordinate error is 0.00002706 world
units. Together with the native draw-dispatch probes and the existing model
transform tests, this closes the mesh orientation and trajectory-pose path for
the authored arrow. No remaining projectile transform or shipped asset-selection
mismatch was demonstrated. The remaining complaint was about the arrow's low-zoom
silhouette, not its selected asset or trajectory; the display readability
correction is recorded below. This audit does not compare final Glide texture
sampling or triangle pixels; those remain outside the behavioral parity gate.

Reproduce the model-pose check with:

```sh
python3 tools/re/check_projectile_model_transform.py build-o2/model_transform_test
```

### Low-zoom arrow and bolt silhouette (2026-09-24)

The headless `TAK_PROJECTILE_TEST=araarch` scene showed that the correct
`araarrow` mesh and heading still collapsed to a narrow line at ordinary map
zoom. The authored head and fletching are present, but their roughly 14-by-3
world-unit model loses those details during projection. `drawShotModel` now
applies a camera-dependent display scale to the elongated projectile models
used for arrows, bolts, harpoons, and spears (`araarrow*`, `arabolt`, `cregatl1`,
`araharp1`, `verbal1*`, `verhpoon`, `verspear`, and `zonterspear`). It expands
the projected cross-section more than length and returns to the authored size
at close zoom. At the normal test zoom of 1.305, the travel-axis scale is 1.5
and the perpendicular scale is 3.375; this is a low-resolution readability
correction, not a claim about retail's exact projectile dimensions. Projectile
coordinates, aim, collision, lifetime, and texture selection are unchanged;
shells, bombs, and magic beams are not scaled.

The in-engine Ulasem Arena screenshot fixture reproduces the normal-zoom shot;
the zoomed crop shows the mesh's tail feathers, shaft, and arrowhead separately.
Release, Debug, and optimized Debug client builds succeed, and the complete
Debug suite passes 52/52. The final cross-section refinement was then rebuilt
in Debug and exercised by the same headless screenshot. This is a readability
approximation for the low-resolution projection, not evidence of retail's exact
projectile size. That screenshot also exposed a no-fog inconsistency: the 3D
projectile path still culled by explored-cell visibility when `--nofog` was set.
It now applies the same no-fog visibility bypass as the straight-projectile
path. The `araarrow` mid-flight capture at `/tmp/arrow-crossscale-3_5.png` and
its enlarged crop at `/tmp/arrow-crossscale-crop.png` show the shaft, point, and
fletching in the headless software renderer. No retail GUI was launched, as
requested.

### Joined native VTOL-unload mission and flight trace (2026-09-24)

`tools/re/probe_transport_air_unload_flight.py` now pairs the native unload
mission dispatcher with the native flight point controller, navigator, and
mover on the same continuous synthetic trip as World's
`--air-unload-flight-trace`. The trace begins with the distant unload command,
observes the dispatcher install the `transportdistance-34` circle goal, follows
the carrier through the physical flight path, and continues through arrival,
cargo/passenger release, and the empty mission tail. The comparison reaches
the released-and-parked terminal state after 488 ticks in Debug, Release, and
optimized Debug. All movement, controller goal, mission stage/timer, cargo,
passenger, effects, and park fields agree. One post-mover sample has only a
pending-wake-field difference: World exposes `0x500` after its movement pass;
retail consumes that wake in the next dispatcher pass before the paired
post-mover snapshot.

This joins mission dispatch and flight integration for a direct synthetic
circle route. It deliberately pins terrain rescans past the trace and uses the
fixture's controlled placement hook. It does not validate native route search,
active terrain rescans, dynamic body collisions, retries/interrupted trips, or
map-backed shoreline placement in this same end-to-end trace; those remain
separate coverage. No retail GUI was launched.

Validation: the joined 488-tick probe, the existing 17-tick native callback
probe, and the 470-tick native flight-mover probe pass against all three build
configurations. The four transport CTests also pass in each configuration
(12/12 total). Reproduce the joined trace with:

```sh
python3 tools/re/probe_transport_air_unload_flight.py --binary build-o2/transport_test --steps 1200
```

### Joined Lake Lokken sea-unload route, movement, and release (2026-09-24)

The `--native-live-unload` mode of
`tools/re/check_surface_unload_map_route.py` now carries the native
`GROUND_UNLOAD` mission through one uninterrupted map-backed trip. On Lake
Lokken, a Vertrans starting at `(240,120)` routes Araarch to the selected shore
cell `(240,350)`. Retail's actual map-grade routine answers the route queries
from the shipped TNT terrain, and native route reconstruction agrees with
World's delivered route. The same native carrier then advances through retail's
`0x4dc800` mover and `0x51b2a0` position update alongside World, receives the
circle-arrival wake once, passes 16 map-backed Araarch placement checks, releases
the passenger at the selected shore footprint, and retires the mission after
its native empty tail. Both movers match for all 1,488 ticks through release.

The integrated trace passes Standard and Crusades with Release, Debug, and
optimized Debug binaries (six runs). A 1,470-step limit reaches the circle but
ends before retail's dispatcher consumes arrival; use at least 1,550 steps to
include the release at physical step 1,488. No retail GUI was launched.

This closes the map-backed shoreline mission/route/movement/placement
integration case. The original trace pins local terrain scans past its window;
the live-scan extension below exercises those scans during the same trip. The
fixture has no moving neighboring blockers. Traffic from other units and
additional maps/carrier profiles remain separate parity coverage. Reproduce
the original pinned-scan Standard case with:

```sh
python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier vertrans --passenger araarch \
  --native-map-grades --native-map-mover-steps 1550 --native-live-unload
```

### Live-scan Lake Lokken sea-unload route (2026-09-24)

The same joined route now has an active terrain-scan variant. The harness seeds
retail's carrier with its shipped sight range and map-sized exploration plane,
and restores the scan-enabled game-options context after each mission dispatch.
World and retail start with the same unexplored plane and scan on the first
physical movement tick. Every movement field remains equal through the native
shoreline release at step 2,217, including 550 live scan deadlines and the
terrain/speed modes those scans select. The native mission wakes once at circle
arrival, passes all 16 map-backed Araarch placement checks, releases the
passenger at `(240,350)`, and retires through its empty tail.

The live-scan case passes Standard and Crusades with Release, Debug, and
optimized Debug binaries (six runs). This verifies live terrain rescans for
this Lake Lokken Vertrans/Araarch unload route. Dynamic collision traffic from
other moving units and retries around newly blocked shore cells are covered by
the following bounded blocker variant; broader traffic, additional maps, and
other carrier profiles remain open. No retail GUI was launched. Reproduce the
Standard run with:

```sh
python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier vertrans --passenger araarch \
  --native-map-grades --native-map-mover-steps 2500 --native-live-unload \
  --terrain-scan-after 1
```

### Moving blocker at the map-backed sea-unload shore (2026-09-24)

The optional `--shore-blocker` trace adds a mobile Araarch at the selected
Lake Lokken landing point after the initial route is delivered. World starts
the unload retry when the carrier reaches the blocked site and orders the
blocker to walk clear. Retail's placement oracle uses the actual `0x507d10`
routine with the TNT cell plane, a live `0x138`-stride entity slot, and matching
cell occupancy IDs. It rejects the site while occupied, retains the original
landing point and attached passenger, then accepts it after the blocker clears.
The live-scan deadlines remain paired through release, and the surface carrier
stays inside the authored water-side unload circle. The retry briefly returns
the unload mission to stage 1; World previously treated that as an inactive
surface route and zeroed the carrier's remaining coast speed. It now keeps the
mover active through every live unload stage, matching retail's brake-down.
Native and World movement fields match on all 2,252 physical steps through the
blocked retry and retail release. Retail releases at step 2,252; World releases
at step 2,253, the one-tick dispatcher/mover phase offset.

The fixture holds the delivered route fixed while it verifies the moving shore
blocker and retry. Dynamic route replacement around changing map occupancy
remains open at this point in the audit; the following section closes that route
search case. The blocker trace passes Standard and Crusades with Release, Debug,
and optimized Debug builds (six runs), including 550 paired live terrain-scan
deadlines. No retail GUI was launched. Reproduce the Standard case with:

```sh
python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier vertrans --passenger araarch \
  --native-map-grades --native-map-mover-steps 2500 --native-live-unload \
  --terrain-scan-after 1 --shore-blocker
```

### Live-blocker replacement route paired with retail search (2026-09-24)

The `--live-route-blocker-steps` Lake Lokken fixture captures a completed World
replan after a Vertrans blocks the shore, then replays the request through
retail's native route search and worker. It packs World's row-major grade cache
into retail's native map buffer, preserving the original `0x4139d0` cached-grade
dispatch, including its grade-2 live-refresh branch. Only the live map query is
supplied by the independent TNT/body oracle. The native circle goal, scheduler
weight, water movement costs, and footprint come from the captured request;
the fixture checks the goal center, radius, start origin, and initial distance.

In the delayed blocker case, the footprint changes 120 locally sampled grades
from open TNT terrain to retail's blocked interior and clearance border. The
grade oracle checks those cells against native `0x508cd0`; the original native
dispatcher returns the same grades as World's captured plane for all 1,545
queries. It reconstructs the same five replacement waypoints. The real
`0x416430` mission worker keeps the carrier, navigator, controller, unload
mission, and attached Araarch through both deliveries, and its replacement also
matches all five World waypoints. World moves the blocker clear and releases
Araarch at the selected shore on physical step 2,350.

An experimental always-on path-service run captures a ten-waypoint replacement.
The isolated direct replay returns the same first point but shifts later points
by one cell, although all 2,131 original-dispatcher grade results match the
captured plane. The resulting native and World final cost planes differ. In
contrast, replaying the same request through the native sea-unload worker
delivers all ten captured World waypoints while the mission and cargo remain
active. The isolated search-state discrepancy is unresolved and is not counted
as direct-search parity. In both fixtures the carrier is not physically stepped
between worker requests, so automatic blocker detection and replan are not yet
one continuous native trace; other maps and carrier profiles also remain open.
Standard and Crusades worker replays pass with Release, Debug, and optimized
Debug binaries (six runs). The four transport CTests pass in each build
(12/12). No retail GUI was launched. Reproduce the Standard case with:

```sh
python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier vertrans --passenger araarch \
  --native-map-grades --live-route-blocker-steps 5000 --native-worker-mission-repath
```

### Per Mare map-backed unload with live terrain scans (2026-09-24)

The route checker now also joins a Vertrans/Araarch unload on Per Mare Per
Terras. Retail's `0x508cd0` grades are computed from the map's TNT height and
feature planes; the queried feature neighborhood contains no unmodeled
blocking feature. Native reconstruction matches World's one-waypoint route
from `(40,120)` to the shore site `(40,142)`, with 54 distinct map-grade cells
across 58 queries. The actual `GROUND_UNLOAD` mission, navigator, carrier,
passenger and mover remain joined through navigator arrival, 16 native
map-backed placement checks, cargo release and mission retirement. With live
terrain scanning enabled on the first movement tick, all 15 scan deadlines and
the full movement state match through release at physical step 78. Standard
and Crusades pass in Release, Debug and optimized builds (six runs). This adds
one real map and carrier profile; the broader map, carrier and dynamic-traffic
gates above remain open. No retail GUI was launched.

Reproduce the Standard and Crusades optimized runs with:

```sh
python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Per Mare Per Terras' \
  --start 40 120 --target 40 142 --carrier vertrans --passenger araarch \
  --native-map-grades --native-map-mover-steps 120 --native-live-unload \
  --terrain-scan-after 1
python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Per Mare Per Terras' \
  --start 40 120 --target 40 142 --carrier vertrans --passenger araarch \
  --native-map-grades --native-map-mover-steps 120 --native-live-unload \
  --terrain-scan-after 1 --crusades
```

### Sea Dragon Spine map-backed unload with live terrain scans (2026-09-24)

The joined Vertrans/Araarch case also passes on Sea Dragon Spine. Independent
retail terrain grades are computed from its TNT height and feature planes, and
native route reconstruction matches World's three-waypoint route from
`(240,95)` to the shore site `(240,120)`: 176 distinct grade cells across 219
queries. The `GROUND_UNLOAD` mission, navigator, carrier, passenger, and mover
remain joined through navigator arrival, 16 map-backed placement checks,
release, and mission retirement. Starting live terrain scanning on the first
movement tick preserves all 74 scan deadlines and movement fields through
release at physical step 314. Standard and Crusades pass in Release, Debug,
and optimized builds (six runs). The additional map does not close broader
map, carrier, or dynamic-traffic coverage. No retail GUI was launched.

Reproduce the Standard and Crusades optimized runs with:

```sh
python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Sea Dragon Spine' \
  --start 240 95 --target 240 120 --carrier vertrans --passenger araarch \
  --native-map-grades --native-map-mover-steps 500 --native-live-unload \
  --terrain-scan-after 1
python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Sea Dragon Spine' \
  --start 240 95 --target 240 120 --carrier vertrans --passenger araarch \
  --native-map-grades --native-map-mover-steps 500 --native-live-unload \
  --terrain-scan-after 1 --crusades
```

### Continuous native mover through the live-blocker boundary (2026-09-24)

The earlier live route-replacement replay begins at the captured replan request,
while its mover is not physically advanced between worker requests. The combined
Lake Lokken diagnostic retained the mission-backed path, advanced retail's
`0x4dc800`/`0x51b2a0` mover, and injected the map-backed blocker, but its first
run did not have complete environment inputs. The initial step-825 difference
came from the emulator's exploration plane missing World updates. After those
updates were supplied, scans matched through step 890. At step 891, World grades
candidate origin `(238,232)` as blocked because its 4x4 footprint overlaps the
map-backed Vertrans at `(238,235)`. The emulator fixture had not registered that
blocker in its live-body query and occupancy plane, so its different scan result
is a fixture omission, not evidence of a retail/gameplay mismatch.

That earlier combined movement-and-worker trace did not produce the native
repeated-refusal bit because it lacked the full live-body inputs. The continuous
native collision and route-history case below supersedes that result and closes
the collision-to-bit gap. No retail GUI was launched.

### Native collision-driven mission-worker retry after the map-backed blocker (2026-09-25)

The `--native-worker-mission-retry` Lake Lokken case begins at the World
repeated-block boundary: after two failed shore placements, World has a
map-backed Vertrans blocker at `(240,292)` and the carrier at `(240,288)`. It
keeps the active Vertrans/Araarch unload mission, navigator, controller, and
cached TNT-plus-occupancy grade plane. The fixture supplies the captured
repeated-refusal bit (`mover+0x36 & 4`) to retail's real `0x4e5150`; that native
retry enqueues through `0x4e4f50`, and `0x416430` delivers the replacement on
the same mission and navigator. The five installed waypoints match World's
replacement route. Retail's original `0x4139d0` dispatcher runs with the cached
map grades; it needs no `0x4db640` live-body refresh because the map cache
already marks the occupied footprint blocked and its clearance cells.

The `--native-worker-mission-collision` variant starts at the original native
`WORLDSEED`, installs the initial route on the live unload mission/controller,
and advances that same carrier through the real `0x4dc800` mover and
`0x507d10` placement check. Retail reaches the World replan X/Z position,
rejects two consecutive footprints, sets mover bit 4, and `0x4e5150` queues the
retry for `0x416430`. The native mover reaches that boundary after 771 updates;
the World fixture installs its replacement at step 1232 because it delays path
service until its terrain-scan window. Both traces are at the same captured
collision position, and retail installs all five World replacement waypoints.
The initial native route is also checked against the captured map route.

The earlier boundary-seeded collision replay started a fresh native
mission/controller at the captured World replan position and built a two-point
straight route there, omitting the trip's initial route and approach state. It
also advanced through the collision at ticks 3–4, then rewound the worker clock
to tick 3 after the retry had been enqueued at tick 4. With the native route
history carried through the collision and the worker clock kept monotonic, the
two-point result disappears. This was a harness artifact; it does not justify
a production pathfinding change. The trace is Standard-only, and other maps
and carrier profiles remain open. No retail GUI was launched.

Reproduce the continuous native collision-driven trace with:

```sh
python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier vertrans --passenger araarch \
  --native-map-grades --live-route-blocker-steps 5000 \
  --native-worker-mission-retry --native-worker-mission-collision
```

Reproduce the captured-bit control with:

```sh
python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier vertrans --passenger araarch \
  --native-map-grades --live-route-blocker-steps 5000 \
  --native-worker-mission-retry
```

The standalone direct-search reconstruction remains one waypoint different.
Both worker replays match the native grade checks to the map-backed blocker
plane, and their endpoints remain inside the same 266px unload circle. No
production change was justified by this trace.

Reproduce the direct-bit control with:

```sh
python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier vertrans --passenger araarch \
  --native-map-grades --live-route-blocker-steps 5000 \
  --native-worker-mission-retry
```

### Araarch projectile release and impact timing (2026-09-24)

The native update order calls the weapon update (`0x52ae90`) before the unit's
COB VM (`0x56c870`). With that order, the VM writes SET23 at tick 39 and the
weapon update releases the projectile at tick 40, matching World's release
tick and age-zero projectile state. The first native and World snapshots match
position `(225,141.25,200)`, velocity `(815300,-80300,0)`, and angles
`(0,16384,64512)`. Against the same placed tree, both hit at age 9; against a
clear target, both hit at age 11. This resolves the reported one-tick gap as a
probe-ordering issue and gives no reason to change production code for this
case. The native collision routine was advanced directly, so the global
projectile-manager's same-tick scheduling is covered by the following pool
trace. The untracked ad-hoc Araarch probe still calls the callbacks in the old
reversed order and is not authoritative for release timing. No retail GUI was
launched.

### Native projectile pool scheduling (2026-09-24)

`tools/re/probe_projectile_manager_timeline.py` creates one Araarch-speed shell
through retail's real projectile pool initialization (`0x52aa20`), slot
allocation (`0x529a90`), aim (`0x52bdf0`), and ballistic initializer
(`0x52be80`). It then calls the global pool manager (`0x52afd0`) once on each
tick. Each tick dispatches one live shell update (`0x52bf90`) and one pool
compaction (`0x52a800`). The native update reaches map collision (`0x52a4d0`):
the shell remains in the pool through ticks 1–8, hits the height-255 tree at
`(448,400)` on tick 9 at `(449.945,20.203,400)`, and the real compactor reduces
the pool count to zero on that same tick. Its XYZ position, velocity, and angle
state at every manager tick match the retail ballistic stepping helper.

The matching World case is in `retail_script_test`. It checks the same launch
state, compares all eight live-tick XYZ/velocity/angle snapshots against the
native trace, then confirms tree damage and projectile removal on tick 9. No
native-versus-World scheduling mismatch was reproduced, so this work does not
justify a production change.

The fixture initializes the manager and allocates the slot through their native
pool entry points, but writes the launch caller's owner/start metadata directly;
it does not yet join common weapon release to pool insertion in one trace. At
impact, the probe records the real collision result and entry into `0x529c10`,
then substitutes the omitted damage/effect side effects by setting the
projectile retirement bit before native compaction. This verifies the real
collision-to-manager-removal schedule, while native impact damage/effects and
the code that normally sets that bit remain unverified. It is headless Unicorn
emulation only; no GUI or Wine launch was used.

### Delayed script release across range and visibility changes (2026-09-24)

The native `probe_weapon_gate_loss.py` check confirms the common weapon update
admits a 400-pixel target, rejects one at 401 pixels, retains SET 23 while the
live target is out of range, and releases the pending shot after range returns.
The World `retail_script_test` now drives the same delayed AimWeapon/FireWeapon
handshake with a live target: after SET 23 becomes pending at 401 pixels, eight
out-of-range updates create no projectile and spend no mana; moving the target
back to the inclusive 400-pixel boundary releases exactly one shot and charges
mana once. The focused `retail_script` CTest passes in Release, Debug, and
optimized Debug. `weapon_hidden_release_test` adds the fog case: while SET 23
is pending out of range, the test switches to a player with no vision, confirms
the target is hidden, then brings it to the inclusive boundary and observes one
shot and one mana charge. Native `probe_weapon_gate_loss.py` independently
confirms a hidden live target releases when range returns; `probe_weapon_target_lookup.py`
confirms a pending-death target is retired without consuming SET 23. Both World
tests pass. The two CTests pass in Release, Debug, and optimized Debug. Broader
target-retirement and visibility combinations remain open. No retail GUI was
launched.

### Pending shot after target death (2026-09-25)

`probe_weapon_gate_loss.py` now joins native target retirement (`0x51a9a0`) to
the following common weapon update (`0x52ae90`). With SET 23 pending, the
retirement step clears the dead target, dispatches one `TargetCleared`, and
preserves the pending bit. The next update sees no target and does not call
readiness, `FireWeapon`, or the projectile sink. The native probe passes, and
`retail_script` plus `weapon_hidden_release` pass in optimized Debug. This
closes the dead-target/pending-release sequence; other target-loss and
visibility combinations remain open. No production change or retail GUI run
was needed.

### Shipped arrow and bolt rendering paths (2026-09-24)

The asset-backed weapon inventory covers the Standard and Crusades registries:
145 moving projectile slots resolve their referenced 3DO models, with no
missing, unreadable, or empty model files. This includes Araarch/Taros arrows
(`araarrow`), the Arasiege Mounted Ballista's large bolt (`arabolt`), the
Veruna Ballista's veteran pair (`verbal1`/`verbal1_vet`), anti-air arrows, and
LOS arrow/bolt weapons. The unique arrow/bolt/harpoon/spear model names all
appear in the low-zoom readable-silhouette path. The four artless slots are the
same two authored beams (`vermage` Water Ball and `zonspide` Claws) in both
balance registries; they are not arrow weapons.

The Glide renderer sends modeled ballistic and straight/LOS shots to
`drawShotModel` and returns before the generic yellow-streak fallback. Native
emulation checks pass for 4,096 ballistic draw dispatches, 4,096 straight-shot
launches, 4,096 straight-shot draw decisions, and 4,096 XYZ model poses; the
largest pose difference is 0.00002706 world units. The draw probes validate
model selection, angles, position, visibility, and draw ordering. A headless
Arasiege image did not expose a clear enough in-flight bolt for a retail/Glide
visual comparison; this work establishes the authored model path and pose, not
a final screenshot comparison. Screenshot pixel identity is not a completion
gate.

Reproduce the focused checks with:

```sh
python3 tools/re/probe_ballistic_projectile_render.py
python3 tools/re/probe_straight_projectile_launch.py
for bin in build build-dbg build-o2; do
  "$bin/effect_inventory" /home/pocket_geek/tak_data --weapons
  python3 tools/re/probe_straight_projectile_render.py "$bin/retail_visual_test"
  python3 tools/re/check_projectile_model_transform.py "$bin/model_transform_test"
done
```

### VTOL unload across a terrain-sector height boundary (2026-09-25)

The headless 480-tick comparison crosses a terrain step from height 100 to 220
at world X 3520. The fixture writes both planes used by retail map setup:
raw sector maxima at record `+0` and the 3x3-dilated maximum at `+1`, which
native `0x524af0` reads for cruise height. The real `0x4dc800` mover relinks
the unit's center-sector pointer at each boundary; all seven observed pointers
match the carrier's position-derived sector. World and native agree on XYZ,
altitude, vertical and horizontal flight state, navigation destination and
velocity, controller, mission, and cargo for all 480 ticks.

The first mismatch in a 500-tick experiment is the already-known arrival
sampling phase at tick 487 (World exposes pending `0x500` after its mover pass;
retail consumes it in dispatcher order). The focused height trace stops before
that event, so the result establishes sector-height movement only. Terrain
rescans are held and map-sector heights are controlled fixture data in this
trace; the companion map-backed flight check below covers the live scan path on
the same ridge. No production change or retail GUI run was needed.

Reproduce with:

```sh
python3 tools/re/check_air_unload_heightstep.py \
  --binary build-o2/transport_test --steps 480
```

### Map-backed VTOL height-sector scans (2026-09-25)

`check_air_map_height_scan.py` fills retail's per-cell height records and calls
the real map-sector initializer `0x50e740`, which builds both the raw and
3x3-dilated planes. It verifies every sector against those cell heights. The
native air-unload trace then keeps the `0x4dc800` local scan deadline active
while the carrier crosses the 100-to-220 ridge; each center-sector pointer is
checked against its current position-derived sector. World and native rows match
for 480 ticks, with eight sector relinks and six scan-deadline advances (ticks
71 through 491). The mover's recurring scan updates its local scan timer; it
does not rebuild the static sector-height grid during flight. This closes the
map-backed height-grid and local scan path for this controlled ridge, not every
live map or carrier route. No production change or retail GUI run was needed.

Reproduce with:

```sh
python3 tools/re/check_air_map_height_scan.py \
  --binary build-o2/transport_test --steps 480
```

### Map-backed air unload through passenger release (2026-09-25)

`probe_transport_air_unload_map_flight.py` joins the live native `VTOL_UNLOAD`
dispatcher, circle controller, carrier mover, and World unload trace over the
same 100-to-220 terrain ridge. Retail's `0x50e740` builds the sector-height
planes from the map cell records before the unload controller is created;
the controller allocation is verified not to overlap the sector table. The
real `0x4dc800` mover keeps its terrain-scan deadline active throughout the
flight. Eight sector relinks and six live scans occur before terminal state.

All 488 rows match through ridge flight, arrival, passenger release, PARK, and
mission retirement in Release, Debug, and optimized builds. The sole sampled
difference is the expected arrival-wake phase at tick 487: World exposes
pending `0x500` after movement, while retail consumes it in dispatcher order.
Cargo release and PARK occur at tick 487; the empty mission retires at tick
488. Placement feasibility and effect/cargo/PARK host side effects remain
controlled fixture seams, and this is one carrier/map profile rather than a
full route and dynamic-traffic matrix. Call-site hooks show that VTOL unload
uses the point/circle controller and flight navigator (`0x4e40e0`, `0x4e4540`,
`0x524af0`); retail does not enter the generic surface path-search worker on
this flight. Generic air waypoint acquisition is a separate open seam. No
production change or retail GUI run was needed.

Reproduce with:

```sh
PYTHONPATH=tools/re python3 tools/re/probe_transport_air_unload_map_flight.py \
  --binary build/transport_test --steps 600
PYTHONPATH=tools/re python3 tools/re/probe_transport_air_unload_map_flight.py \
  --binary build-dbg/transport_test --steps 600
PYTHONPATH=tools/re python3 tools/re/probe_transport_air_unload_map_flight.py \
  --binary build-o2/transport_test --steps 600
```

### Zhon construction-flight occupancy callback edge (2026-09-25)

The 103-tick persistent construction trace uses the native movement dispatcher
with the placed build site and flight controller retained through two orbit
retargets, arrival at tick 82, and the next target at tick 101. `zonhunt.cob`
declares `setSFXoccupy` but not the coincident `TurnDirection` or `MoveRate`
requests. Native and World both issue `setSFXoccupy(5)` once, on tick 1. This
confirms callback-request ordering and the World occupancy helper for this
profile; it does not execute the full native COB thread or compare a rendered
pose, and other builders/flyers remain open.

Reproduce with:

```sh
python3 tools/re/check_zhon_construction_flight_trace.py \
  --binary build-o2/conjure_test --install assets/game --persistent
```

### Zhon monarch script-state timeline (2026-09-24)

The shipped `zonhunt.cob` script now compares against retail over 502 restored
thread, animation, and RNG boundaries. `Create`, `BeginFlight`/occupancy, and
the construction callbacks remain aligned; the build-ready updates occur at
tick 151. The separate height/projection check below found no transform
mismatch. The script-state check passes with Release, Debug, optimized Debug,
and Clang test binaries. No production change is justified by this comparison.

`check_zhon_construction_display_timeline.py` adds a separate 104-row join:
it schedules the display VM callbacks from the native placed-build
`0x41ef00`/`0x4dc800` trace and compares complete COB thread and piece state at
each 30-Hz tick. `BeginFlight`, `setSFXoccupy(5)`, and `StartBuilding` remain
aligned in Release, Debug, and optimized builds. The supported persistent
construction path also matches through arrival and orbit reacquisition for 103
ticks. The nonpersistent synthetic endpoint test stops matching after arrival
because it omits the mission dispatcher that drives the next flight goal; this
does not establish a retail mismatch. Neither comparison renders a live camera
frame.

Reproduce with:

```sh
python3 tools/re/check_script_state.py \
  assets/extracted/all/scripts/zonhunt.cob /tmp/zonhunt-zero.state \
  --binary build-o2/retail_script_test --ticks 501 --notify Create --timeline
python3 tools/re/check_zhon_construction_display_timeline.py \
  --binary build-o2/retail_script_test
```

### Zhon Monarch flight-height projection (2026-09-25)

Retail projects a unit anchor as `x-cameraX, z-(y>>1)-cameraY`. GameView's
flight-height split recombines to the same half-height projection relative to
the terrain datum; there is no Zonhunt-specific build-site position offset.
Native model-transform checks match the captured Zonhunt hierarchy vertices
to under 0.000023 world units. In the available retail sample, the Monarch is
171 units above the site with `dz=-24.027`, predicting a screen offset 109.5
units north; altitude accounts for 85.5 of those units and the remaining
offset follows from the horizontal orbit around the site. These checks explain
the observed northward placement without identifying a code discrepancy.
The native model seam is now exercised end to end through geometry: current
poses emitted by `56d850` at Zonhunt construction tick 101 map by COB/3DO piece
name into the actual shipped `zonhunt.3do`; native `0x4ee620` refreshes all 48
pieces and 346 vertices. Those transformed vertices match the World model
transform helper within 0.00001716 world units. The separate display timeline
compares complete native and World thread/piece state at 104 boundaries. The
remaining animation gate is roster-wide engine-driven call-in coverage across
other live profiles and flying families, plus final projected screen placement
under a controlled camera. Framebuffer/raster comparison remains diagnostic;
pixel identity is not required. No retail GUI was launched.

Reproduce the geometry and display-state checks with:

```sh
PYTHONPATH=tools/re python3 tools/re/check_native_flyer_model_pose.py \
  --binary build-o2/model_transform_test --tick 101
PYTHONPATH=tools/re python3 tools/re/check_zhon_construction_display_timeline.py \
  --binary build-o2/retail_script_test --ticks 103
```

### Native flyer callback-to-3DO pose join (2026-09-25)

The VERBALL check extends the model-pose comparison to a non-Zonhunt flying
unit. It runs retail's `0x4dc800` mover through a controlled flight sequence,
dispatches its real `TurnDirection(-135)`, `MoveRate(3)`, and
`setSFXoccupy(5)` call-ins to the attached shipped COB, and captures the
post-tick wing poses. The wing pieces continue interpolating between native
COB ticks 4 and 5. All 10 complete native COB boundaries match the World
script oracle for the same callback schedule; native `0x4ee620` transforms all
22 shipped `verball.3do` pieces and 133 vertices to within 0.00001149 world
units of the World transform helper. This complements the complete controlled
World snapshot comparison and Zonhunt construction-pose check; it does not
replace either one. The unit targets, speeds, heading and services are
controlled, with no map pathfinding or camera, texture, projection,
framebuffer, or GUI comparison.

Reproduce with:

```sh
PYTHONPATH=tools/re python3 tools/re/check_native_flight_model_pose.py \
  --script-binary build-o2/retail_script_test \
  --model-binary build-o2/model_transform_test
```

### Native death-state dispatch through owner retirement (2026-09-24)

`probe_native_set31_lifecycle.py --native-death-state` enters retail's
`0x512610(unit, 1)` and `0x512860` instead of manually starting the two COB
callbacks. The native dispatcher starts `Killed`, then `Dying`; the real COB
host writes SET31 at tick 0. The native timer expires at tick 34, and the unit
update consumes removal and destroys the attached SFX owner/list at tick 35.
The synthetic model allocator and effect creation sink remain controlled, so
this verifies lifecycle dispatch and retirement, not the rendered corpse.
Release, Debug, optimized Debug, and Clang runs pass.

Reproduce with:

```sh
PYTHONPATH=tools/re python3 tools/re/probe_native_set31_lifecycle.py \
  --native-death-state --world-binary build-o2/animation_roster_test
```

### Attached death-effect retirement deadline (2026-09-25)

The native death-state probe covers both early owner-removal edges that affect
attached damage flames. `crefire` emits one attached flame and writes SET26 at
tick 0; retail's real outer unit updater drains the model owner/list at tick 1.
`tarmage` writes SET31 at tick 0; the native one-second timer expires at tick
34 and owner/list teardown runs at tick 35. GameView records the corresponding
deadline from the display VM's SET_UNIT_VALUE callback and uses it to shorten
the attached-effect retirement schedule, preserving the earliest deadline if
multiple callbacks arrive. A unit without either early-removal edge retains the
existing 120-tick corpse fallback.

A static audit of all 204 shipped unit COBs found 129 SET26 writes, all
`(26,1)` in `Dying`; `crebomb` has two mutually exclusive branches, with its
delayed branch writing only after its sleep loop. A native roster probe starts
each writer's actual Killed/Dying COB handlers, then runs the real owner updater
and list destructor. All 129 cases write SET26; teardown follows exactly one
owner update later, including `crebomb`'s delayed and immediate branches at
146/147 and 0/1. This validates callback execution and owner teardown, not the
full death-state builder for every unit. The same audit found 38 SET31 writes,
all `(31,1)`, one per `Dying` function. All 38 were traced through native
death-state handling: 30 write immediately and eight write after sleeps. The
display deadline remains `+35` from its pinned presentation snapshot; delayed
writes consume one owner update before that snapshot advances, so changing it
to `+34` would retire the display VM early. `tarmage`'s native timer/teardown
path remains the direct tick-0/35 reference case.

The C++ lifecycle helper checks the SET26/SET31 delay and wrap-safe earliest
deadline selection. Native `crefire` and `tarmage` probes and the direct-VM
death-flame comparison pass on the optimized build. The `retail_visual`,
`animation_roster`, `cursor_roster`, and `shadow` tests pass in Release, Debug,
optimized Debug, and Clang configurations; no retail GUI was launched. The
native probes validate owner teardown, while the C++ tests validate deadline
selection; they do not compare a live retail framebuffer. Other attached-effect
families and live rendered lifetimes remain open.

Reproduce the SET26 roster using the locally installed data archives:

```sh
build-o2/hpitool merge /home/pocket_geek/tak_data /tmp/tak-native-scripts
PYTHONPATH=tools/re python3 tools/re/probe_native_set26_roster.py \
  --scripts /tmp/tak-native-scripts/scripts \
  --world-binary build-o2/animation_roster_test --workers 4
```

Reproduce with:

```sh
PYTHONPATH=tools/re python3 tools/re/probe_native_set31_lifecycle.py \
  --native-death-state --script crefire --world-binary build-o2/animation_roster_test
PYTHONPATH=tools/re python3 tools/re/probe_native_set31_lifecycle.py \
  --native-death-state --script crebomb --world-binary build-o2/animation_roster_test
PYTHONPATH=tools/re python3 tools/re/probe_native_set31_lifecycle.py \
  --native-death-state --world-binary build-o2/animation_roster_test
PYTHONPATH=tools/re python3 tools/re/probe_death_sfx_lifecycle.py \
  --world-binary build-o2/animation_roster_test
```

### VerScout Lake Lokken physical transport trace (2026-09-25)

The tick-928 failure in the VerScout fixture was a missing emulation global,
not a native/World mover difference. Retail startup constructs a global
path-search service with `0x415f80` and stores it at `GS+0x19e70`; destination
refresh through `0x4e54e0` calls `0x415f30` on that service. The probe created
the per-search object but had omitted this separate startup singleton, leaving
the refresh call's `this` pointer null. The map-backed mover fixture now
constructs the singleton as retail does and supports the VerScout WATER3 cost
profile.

On Lake Lokken, the VerScout/Araarch route matches World after 867 native grade
queries, and all 1,000 native/World physical mover steps match. The existing
Vertrans/Araarch trace still matches for 1,550 steps. The probes use TNT
terrain grades; live feature-body passability and changing unit occupancy
remain outside their coverage. Reproduce with:

```sh
PYTHONPATH=tools/re python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier verscout --passenger araarch \
  --native-map-grades --native-map-mover-steps 1000
PYTHONPATH=tools/re python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier vertrans --passenger araarch \
  --native-map-grades --native-map-mover-steps 1550
```

### Assigned weapon target across a visibility edge (2026-09-24)

The target-loss probe now starts with a live assigned target in a visible cell,
then clears the current visibility grid and calls retail's `0x51a9a0` again.
The native lookup retains the same target pointer and does not dispatch
`TargetCleared`; `0x52ae90` continues to admit the live in-range target behind
the hidden grid. This only covers an existing target reference. It does not
test selecting or acquiring a new target through fog, nor the full mission
caller or a rendered attack timeline. World’s explicit-target path follows the
same rule. This trace therefore supports no production change by itself.

Reproduce with:

```sh
PYTHONPATH=tools/re python3 tools/re/probe_weapon_gate_loss.py
```

### Retail waypoint-marker cursor timing (2026-09-25)

Retail `0x4d5930` chooses each order-end marker from the global simulation tick:
`tick / (2 × first-frame GAF delay) % frame count`. `drawOrderTrails()` used
`tick / 3` for every cursor. The renderer now uses the native interval: four
ticks for Move, Attack, Patrol, Defend and Repair, and six ticks for Load,
Unload and Reclaim. The cursor test checks the native four- and six-tick
boundaries and the shipped sequence lengths and first-frame delays. The live
mouse-pointer clock/restart behavior remains a separate open comparison. No
retail GUI run was needed.

Reproduce with:

```sh
build-o2/cursor_test assets/extracted/all
ctest --test-dir build-o2 -R cursor_roster --output-on-failure
```

### Native cursor registration and action routes (2026-09-25)

`probe_cursor_legacy_start_phase.py` reads retail's actual cursor registration
stores and GAF timing metadata, then runs the table setter and live-pointer
updater. Native registration puts `cursorload` at slot 12 and `cursorunload` at
slot 13. The UI command parser maps LOAD to selector mode 6, which returns slot
12 for a valid passenger target; UNLOAD maps to mode 5, which returns slot 13
for a selected transport. The earlier mode-5-as-Load probe label was wrong; the
product's Load/Unload mappings were already correct. A C++ cursor regression
now checks the armed Unload command explicitly.

The selector has one direct gameplay caller, and Capture, Pickup and Teleport
have no evidenced gameplay route through it. Their shipped GAF sequences each
contain one frame, which the native updater preserves. Thus they have no visible
start phase to reproduce; live input-to-pointer pixels remain un-compared. The
probe uses synthetic manager records and does not simulate the complete cursor
manager initialization.

```sh
PYTHONPATH=tools/re python3 tools/re/probe_cursor_legacy_start_phase.py
PYTHONPATH=tools/re python3 tools/re/probe_cursor_action_modes.py
```

### Zhon Monarch root-anchor join (2026-09-25)

`probe_zhon_construction_pose_join.py` joins retail's native `41ef00`/`4dc800`
placed-build movement and callback requests to the shipped `zonhunt.cob`
running in retail's native display VM. At ticks 1, 25, 63, 82, and 101, the
root piece `Zon_gpoly` has authored origin `(0,0,0)` and all six COB root
setter channels remain zero. The display VM is active at each sample with
gates `(6,9,10)=(1,1,1)`. Native altitude and orbital ground position put the
root anchor 90.50 pixels north of the site at tick 1 and 107.98 pixels north
at tick 101 at zoom 1. This explains the reported northward placement for the
captured movement profile; it does not identify a transform mismatch.

The native projection and GameView altitude-split equations, evaluated from
the same fixture coordinates, differ by at most 0.5 pixels from half-height
integer rounding. This is not an independent framebuffer comparison. The
fixture does not interpolate child-piece timed turns or capture a live camera;
child poses and the full rendered animation remain open. No production change
was justified.

Reproduce with:

```sh
PYTHONPATH=tools/re python3 tools/re/probe_zhon_construction_pose_join.py
```

### Lake Lokken VerScout unload through shore release (2026-09-25)

The map-backed `native-live-unload` checker now also covers VerScout/Araarch on
Lake Lokken. Retail's native search reconstructs the one-waypoint shore route
from 867 grade queries over 652 cells. The native unload mission, mover,
terrain scans, placement checks and passenger remain joined through release at
physical step 1400. All 1,400 native/World mover rows match; 691 terrain-scan
deadlines and all 16 map-backed placement checks match, with the endpoint
inside the authored 266px unload circle. The paired path uses the route
captured from World's completed search; it does not exercise every carrier or
map. The separate World end-to-end fixture also passes placement and a 60-tick
coast, though it starts at order issuance and is not a release-tick comparison.
No production change or retail GUI run was needed.

Reproduce the paired trace with:

```sh
python3 tools/re/check_surface_unload_map_route.py build-o2/transport_test \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier verscout --passenger araarch \
  --native-map-grades --native-map-mover-steps 2000 --native-live-unload \
  --terrain-scan-after 1
```

### Native map-backed sea-pickup route request (2026-09-25)

`probe_surface_pickup_native_map_route.py` joins the native
`GROUND_PICKUP` dispatcher, its circle controller, the initialized route job,
and the native `0x416430` worker. On Lake Lokken, retail creates the Vertrans
goal at cell `(238,348)` with a 284px radius. `0x4e54e0` queues the goal through
`0x4e4f50`; the first `0x416430` worker tick delivers the stable 11-waypoint
route from `(3840,1920)` to `(3824,5360)` using TNT-backed `0x508cd0` grades.

This route-phase trace covers only the 6,275 cells in a narrow corridor; every
outside cell is blocked and feature definitions are zero-filled. It ends before
the mover and boarding, while the integrated mission probe below continues this
route through pickup arrival and attachment. The earlier 70px callback fixture
used synthetic `transportdistance=86`; Vertrans's shipped value is 300, which
produces a 284px native pickup circle. No production change or retail GUI run
was needed.

Reproduce with the locally installed retail data:

```sh
PYTHONPATH=tools/re python3 tools/re/probe_surface_pickup_native_map_route.py \
  --retail-root /home/pocket_geek/tak_data --hpitool build-o2/hpitool
```

### Native Lake Lokken sea pickup through passenger attachment (2026-09-25)

`probe_surface_pickup_native_mission.py` continues the native route-worker
probe through 2,620 physical mover ticks, pickup-circle arrival at tick 2552,
transfer effect creation, Araarch attachment and carrier-order retirement at
tick 2622. The route worker uses retail `0x508cd0` grades over 6,275
TNT-backed corridor cells; outside cells are blocked and feature-definition
bodies are zero-filled. Native movement uses the shipped WATER4 limits and
TNT terrain, while mover-side service requests and the synthetic-heap
attachment sink at `0x51b4f0` are controlled boundaries. The probe therefore
does not establish full-map native pickup or the real per-player entity-array
attachment path. Its native tick count is not compared with World because the
native route corridor and World's full-map route differ. The separate World
load-order roundtrip passes in Release, Debug and optimized builds, boarding
at tick 2178 and completing routed shore unload, placement and a 60-tick coast.

Reproduce the native and optimized World traces with:

```sh
PYTHONPATH=tools/re python3 tools/re/probe_surface_pickup_native_mission.py \
  --retail-root /home/pocket_geek/tak_data --hpitool build-o2/hpitool \
  --world-binary build-o2/transport_test
```

The World roundtrip also passes with `build/transport_test` and
`build-dbg/transport_test` in place of the optimized binary.

### Native mover callback to COB integration (2026-09-25)

`check_native_mover_cob_join.py` captures movement callbacks at retail's
`0x56c640` script-dispatch boundary, filters them against each shipped COB's
method table, then runs the matching callbacks through retail's COB scheduler
and compares complete thread/piece rows with the local VM. Four units cover
all three movement callback names across ground (Tarblack), hover (Tarlich),
water (Tarcship) and flying (Aradrag). Initial callback starts and the next
unchanged tick's edge suppression match in Release, Debug and optimized builds.
The probe replays captured requests instead of attaching a COB to one
uninterrupted native mover call; stop/reversal edges and BeginFlight/
BeginLanding remain outside that original trace.

```sh
python3 tools/re/check_native_mover_cob_join.py \
  --binary build/retail_script_test \
  --helper-binary build/retail_movement_animation_test
python3 tools/re/check_native_mover_cob_join.py \
  --binary build-dbg/retail_script_test \
  --helper-binary build-dbg/retail_movement_animation_test
python3 tools/re/check_native_mover_cob_join.py \
  --binary build-o2/retail_script_test \
  --helper-binary build-o2/retail_movement_animation_test
```

The follow-up `probe_native_mover_cob_transitions.py` attaches each shipped COB
VM to the native unit before calling `0x4dc800`. Retail's own `0x56c640`
dispatcher resolves callback names and starts the method in the attached COB
scheduler. Complete thread/piece state then matches the World VM for Tarblack
(ground), Tarlich (hover), Tarcship (water), and Aradrag (flight), over 8–10
boundaries per unit. Same-target ticks are silent, reversal invokes
`TurnDirection(41)`, stop invokes `TurnDirection(0)` and `MoveRate(0)`, water
resume invokes `MoveRate(1)`, and Aradrag occupancy changes `5→4→5`. Release,
Debug and optimized builds pass. The fixture controls target, speed, and state,
then stops before native position commit `0x51b2a0`; it does not cover
mission-side `BeginFlight`/`BeginLanding` producers or rendered/interpolated
poses.

```sh
python3 tools/re/probe_native_mover_cob_transitions.py \
  --script-binary build/retail_script_test
python3 tools/re/probe_native_mover_cob_transitions.py \
  --script-binary build-dbg/retail_script_test
python3 tools/re/probe_native_mover_cob_transitions.py \
  --script-binary build-o2/retail_script_test
```

The full-roster wrapper discovers FBI records with `canmove` enabled and a
matching shipped COB that declares `TurnDirection`, `MoveRate`, or
`setSFXoccupy`, then runs the same attached-native-COB comparison for each
pair. The shipped assets currently yield 151 pairs: 97 ground, 10 hover, 18
water, and 26 flying. All 1,280 complete VM boundaries match the World script
VM. Each pair sees same-target updates, a reversal, stop, and resume; hover and
flying pairs also cross their controlled occupancy/state modes. The test
executes retail's mover, callback-name dispatcher, COB scheduler, and shipped
COB bytecode. Its host inputs remain synthetic: it rewrites the point
controller target, supplies a controlled speed and unit mode/state, uses a
small uniform sector fixture and profile-specific COB GET values, and stops
before native position commit. Thus this roster closes native mover-to-COB
state comparison for these controlled edges, while actual map grades, mission
producers, physical flight/shore routes, and rendered poses remain separate
coverage.

```sh
python3 tools/re/probe_native_mover_cob_roster.py \
  --script-binary build-o2/retail_script_test
```

### Flight mission callbacks into native COB (2026-09-25)

`probe_native_flight_mission_cob.py` runs retail's `0x416c50` BeginFlight
producer and the accepted-site branch of `0x416cd0/0x417089` for BeginLanding.
An Araarch COB VM is attached at `unit+0xbc`; native `0x56c5c0` resolves the
method names and advances retail's scheduler. After the next VM tick, all 2,170
thread, static, piece, and write-state words match the World-side VM oracle for
both callbacks. The BeginLanding fixture also observes native EndTransport;
Araarch declares no method with that name, so neither scheduler starts one.

Mission allocations, controller/goal creation, first-site acceptance, and
height/velocity services are controlled; this is not a full match, route search,
or transport round trip, and it does not compare rendered model poses.

```sh
PYTHONPATH=tools/re python3 tools/re/probe_native_flight_mission_cob.py \
  --binary build-o2/retail_script_test
```

### Ballistic projectile animation and draw selection (2026-09-25)

`probe_ballistic_sprite_timeline.py` joins retail's native projectile launch,
12 updates/24 admitted substeps, authored animation clocks, current-frame
lookup, and draw dispatch for Arapult cannonballs, Aratre cannonballs, and
Verbal bolts/shadows. All nine weapon/build combinations pass across Release,
Debug and optimized builds. It validates animation timing and exact art/model
selection and projected draw inputs, not GAF/TAF raster pixels, Glide blending,
or a live retail framebuffer. Collision, visibility and raster sinks are
controlled; the separate native impact-body trace below observes `0x529c10`.

```sh
PYTHONPATH=tools/re python3 tools/re/probe_ballistic_sprite_timeline.py \
  build-o2/retail_visual_test --profile arapult
PYTHONPATH=tools/re python3 tools/re/probe_ballistic_sprite_timeline.py \
  build-o2/retail_visual_test --profile aratre
PYTHONPATH=tools/re python3 tools/re/probe_ballistic_sprite_timeline.py \
  build-o2/retail_visual_test --profile verbal
```

### Native ballistic impact damage and splash (2026-09-25)

`probe_ballistic_impact_body.py` runs the native impact body through per-target
damage and splash dispatch. An Arabow arrow with authored damage 476 returns
405 with the CRT roll fixed at zero. With the native unit damage path enabled,
the fixture's HP word changes from 1000 to 595 and its damage fraction becomes
0.405. An Arapult shell dispatches damage to the center enemy, a same-owner
unit and a farther enemy, and reaches feature and effect dispatch plus
projectile retirement. A second run bypasses the unit-mutation hook and verifies
remaining HP of 281 for the center, 673 for the near enemy, 1000 for the
out-of-radius enemy, and 281 for the same-owner unit. With a synthetic feature
damage threshold of 5000, native impacts add 1250 to its map-cell damage word
on each of three calls, leaving 3750 damage and the same feature ID. Native
`0x52a330` uses the CRT `rand()` recurrence with per-thread state seeded during
process setup. World
reproduces that recurrence and spread distribution from match-seeded state;
the retail roll sequence is not promised because the retail CRT state is not a
lockstep input and can have other consumers.

The dispatch-only pass intercepts feature mutation, while the health and
feature-accumulation checks run retail's native mutation paths. A fourth-hit
case crosses the 5000 damage threshold: native `0x4961a0 → 0x494ce0 → 0x494f00
→ 0x496380 → 0x4964b8` tears down the old feature through `0x4ee560`, places
replacement type 1 through `0x495360`, and clones its piece tree via
`0x4ee290/0x4ee760/0x4ee7a0`. The final cell has type 1, zero damage, and flag
`0x08`; projectile retirement also runs. The two piece roots are synthetic
empty leaves, heap frees are shimmed, and the replacement effect pointer is
null. Map/terrain lookup, weapon damage lookup, and impact-environment services
remain controlled; authored feature rendering and dynamic replacement effects
are not covered. Broader collision/effect results also remain open. World applies the verified
asymmetric integer damage spread once per eligible damage recipient, using the
CRT stream separately from `gameRng_` so pathfinding's RNG sequence is
  unchanged. Ordinary area damage also includes same-owner units, as in the
  native Arapult trace; mind-control still excludes allies. Regression coverage
  checks the native spread endpoints and one CRT draw per damaged recipient. Full
CTest passes in Release (52/52), Debug (54/54) and optimized Debug (54/54).

```sh
PYTHONPATH=tools/re python3 tools/re/probe_ballistic_impact_body.py
ctest --test-dir build --output-on-failure
ctest --test-dir build-dbg --output-on-failure
ctest --test-dir build-o2 --output-on-failure
```

### Native live-pointer cursor clock (2026-09-25)

`probe_cursor_native_restart.py` executes the live-pointer setter dispatch
through `0x575e30` into the native sequence selector, then runs `0x58a080`.
Native cursor state keeps a separate frame index per registered sequence and a
shared countdown; switching A→B→A retains A's frame while B consumes the same
countdown. `CursorSet` now shares this clock across software and hardware
rendering and caps delayed catch-up at five 30 Hz updates. The probe stubs only
the native frame-delay lookup to a uniform two ticks and uses synthetic IDs;
authored variable delays and the catch-up limit have separate SDL-free
regressions. No cursor pixels or live pointer input are compared.

```sh
PYTHONPATH=tools/re python3 tools/re/probe_cursor_native_restart.py
cmake --build build-o2 --target cursor_test takclient -j4
build-o2/cursor_test assets/extracted/all
ctest --test-dir build-o2 -R cursor_roster --output-on-failure
```

### Full-map native sea pickup and transfer (2026-09-25)

`probe_surface_pickup_native_fullmap.py` runs Lake Lokken's TNT-backed route
and pickup with retail's native grade routine over the complete 480×480 map.
The native worker installs five waypoints at tick 3. Vertrans enters its
284-pixel pickup circle at tick 1,667 and attaches Araarch at tick 1,731 after
1,728 mover ticks from route delivery. The native transfer functions
`0x51b4f0` and `0x51b5a0` both execute once and resolve IDs through the game's
global entity pool. The probe asserts the transfer payload, reciprocal carrier
and passenger links, four transfer effects, and carrier mission retirement.
The World full-map roundtrip boards at tick 2,178 and completes shore unload,
placement, and coast; those tick counts are not required to match because
native and World search spaces differ.

The entity pool uses two synthetic records and a player row. Feature-definition
bodies are zero-filled, and visibility, grade-body calls, mover services, and
the UI feedback effect are controlled. Araarch is stationary and inserted into
the native sector list; its own ground route is not part of the pickup trace.
After the carrier mission retires, the probe replaces the fixture's synthetic
passenger-order head with a retail code-2 `Move_Seek_Pickup` order, using the
native `0x4d6c40` constructor and `0x4d7750` queue insertion. It dispatches the
already-attached Araarch once through `0x4d8450` and the actual `0x403430`
handler. The existing fixture `0x4d6ad0` replacement is removed for this one
dispatch so retail's `0x4d6ad0`/`0x4d6da0` cleanup and `0x519950` reference unlink
execute; the allocator's no-op free sink remains controlled. Assertions verify
the passenger order is the one removed, both queues and the carrier target
reference are clear afterward, and the carrier/passenger cargo links remain.
This is a post-boarding completion check. It does not test passenger approach,
route search, or movement during pickup; a separate fixture covers pre-boarding
circle-controller setup, not this map's full passenger route.

```sh
PYTHONPATH=tools/re python3 -u tools/re/probe_surface_pickup_native_fullmap.py \
  --hpitool build-o2/hpitool \
  --world-binary build-o2/transport_test
```

The full-map command also reports the staged post-boarding passenger-order
completion and native reference cleanup.

### Native passenger-order dispatch and approach controller (2026-09-25)

`probe_native_passenger_order_dispatch.py` constructs both a code-1 carrier
`GROUND_PICKUP` order and the code-2 passenger `Move_Seek_Pickup` order with
native `0x4d6c40`, then inserts them using native `0x4d7750`. The executable
installs reciprocal reference nodes at carrier `+0xc4` and passenger `+0xc4`.
One native `0x4d8450(passenger)` dispatch reaches the real `0x403430` handler,
which snapshots the carrier's position into the passenger mission and calls
real `0x4d4da0`. Retail allocates and initializes its `0x5f28d8` circle
controller through `0x4e2500`, stores it at passenger-order `+0x6e`, and passes
it to the passenger navigator's SetController slot. The observed one-cell
fixture gives circle origin `(6, 6)`, radius `134`, event mask `0x789`, and
deadline `130` at game tick `100`.

Mission-object memory is supplied by the fixture; malloc/free, mission-name
lookup, passenger eligibility, deterministic RNG, and the navigator
SetController slot are controlled boundaries. Both order constructors,
reference linking, queue insertion, dispatcher, handler, approach routine,
circle-controller constructor, and mission-side controller binding execute
from `KINGDOMS.icd`. This test stops after controller installation: no map
search, route movement, sector/body service, passenger mover tick, or full-map
passenger-order continuation is claimed. The Bink/retail GUI remains closed.

```sh
PYTHONPATH=tools/re python3 tools/re/probe_native_passenger_order_dispatch.py
```

### Native full-map VTOL pickup through BeCarried completion (2026-09-25)

`probe_air_pickup_native_fullmap.py` creates a ZONROC `VTOL_Pickup` order and
an Araarch `Move_Seek_Pickup` order on Lake Lokken using their actual sorted
registry IDs (62 and 30). Retail's three descriptor registration wrappers
populate all 76 native 25-byte rows, including `BeCarried` at code 11 with
handler `0x4024a0`; native `0x4d4bf0` resolves the initial pickup names. Retail
`0x50e740` builds the height-sector plane from the shipped 480×480 TNT data,
and the probe independently validates every 60×60 sector record. The real
`0x4d8450` dispatcher selects the passenger and installs native flight
pursuit; 1,043 updates of `0x4dc800` plus `0x51b2a0` move the carrier to it.

On dispatcher tick 1,044, the native attachment creates reciprocal cargo
links, looks up `BECARRIED` through the retail sorted registry at string
`0x615984`, and dispatches handler `0x4024a0` on the Araarch's code-11
`BeCarried` order. The original passenger code-30 order is absent from both
current and queued lists. On that same tick, native `0x4d6ad0` unlinks the
carrier's code-62 order; afterward both carrier order lists are empty and the
Araarch's current list contains only `BeCarried`. Thus the carrier pickup
order completes during the boarding dispatch at tick 1,044. The Araarch stays
stationary, so this still does not cover its pre-boarding ground route or
mover, unloading, other aircraft, or rendering. Feature-definition bodies,
passenger eligibility, allocation/free, audio/effect/UI, COB VM requests, and
visibility/mover-side services remain controlled fixture boundaries; native
descriptor registration/name lookup, order construction and dispatch, flight
movement, map-sector construction, cargo attachment, order cleanup and
BeCarried dispatch execute from `KINGDOMS.icd`. A 100-tick
`--allow-incomplete` control also passes with active pursuit and measurable
carrier movement before boarding.

The full-map attach also confirms the cargo render gate. Native `VTOL_Pickup`
passes `-1` to `0x51b4f0`; its transfer record carries `0xff`, which
`0x51b5a0` converts to passenger flag `Unit+0x130 & 0x8000`. The shipped
`GROUND_PICKUP` attach call passes the same `-1`. `BeCarried` (`0x4024a0`)
does not clear that bit. In the native draw path, `0x4ee700` refreshes cargo
poses only when the bit is clear, and `0x4ec7b0` recursively submits a cargo
drawable through `0x4ec720` only when the bit is clear and its altitude gate
passes. Thus ordinary attached transport cargo is intentionally hidden by this
gate. The probe did not run the full graphics/raster services; this conclusion
comes from the native transfer trace and disassembled renderer branches.

```sh
PYTHONPATH=tools/re python3 -u tools/re/probe_air_pickup_native_fullmap.py \
  --max-ticks 100 --allow-incomplete
PYTHONPATH=tools/re python3 -u tools/re/probe_air_pickup_native_fullmap.py \
  --max-ticks 5000
```

### Tarhel death-effect owner lifecycle (2026-09-25)

`probe_native_tarhel_effect_lifecycle.py` runs Tarhel's native death builder
through `0x512610`, Killed/Dying dispatch, attached SFX 260/261 callbacks,
SET31, the owner timer, and list teardown. Its five tick-0 callback rows match
the World render-host trace. Native SET31 is set at tick 0; the owner timer
expires on update 34, and update 35 executes `0x512ae0/0x4ee560/0x497380`.
Native `0x497400` then unlinks all five nodes inserted by the controlled
creation sink. The direct VM-only timeline also emits callbacks at ticks
18/36/54/72; those are correctly suppressed by the World render-host owner
gate after SET31.

The fixture controls the unit/model/XYZ and SFX allocation/list-insertion sink;
it does not run native SFX constructors or compare pixels. No retail GUI is
launched.

```sh
PYTHONPATH=tools/re python3 -u tools/re/probe_native_tarhel_effect_lifecycle.py \
  --scripts assets/extracted/all/scripts \
  --world-binary build-o2/animation_roster_test
```

### Additional native air-carrier pickup profiles (2026-09-25)

`probe_air_pickup_native_fullmap.py` now accepts shipped transporter profiles
instead of assuming ZONROC. ZONROC/Araarch remains the control (reciprocal
attachment and `BeCarried` dispatch at tick 1,044). CREAERI/Araarch attaches
and retires its pickup and passenger orders at tick 1,462; TARSHIP/Araarch does
the same at tick 1,817. All three use their parsed retail FBI movement scale,
the actual sorted native mission registry, and the installed `KINGDOMS.icd`.
The TARSHIP COB also matches the native mover's declared callbacks against
World across ten VM boundaries, including direction and speed changes.

These profile checks use a stationary Araarch and a controlled empty COB sink
for the pickup probe; the separate TARSHIP callback check executes its shipped
COB methods. They establish air pickup/attachment completion for these three
carriers, not air unloading, moving-passenger synchronization, multi-cargo
capacity, or final animation rendering. Release and optimized builds pass the
pickup profiles.

```sh
PYTHONPATH=tools/re python3 tools/re/probe_air_pickup_native_fullmap.py \
  --hpitool build-o2/hpitool --carrier creaeri --passenger araarch
PYTHONPATH=tools/re python3 tools/re/probe_air_pickup_native_fullmap.py \
  --hpitool build-o2/hpitool --carrier tarship --passenger araarch
```

### Callback-bearing flying-unit model transforms (2026-09-25)

`check_native_flight_model_pose.py --all` discovers 26 shipped FBI/COB pairs
with flying movement callbacks, then compares captured native COB state and
named 3DO transforms with the World pose path. All 26 pass in Release, Debug,
and optimized Debug. Twenty-three produce a changing matched pose in this
generic mover fixture. TARSHIP, ZONHARP, and ZONHUNT have no matched pose change
at its sample boundary, but their static transform checks pass. LIFBIRD is
outside this callback-bearing roster because its COB declares no mover
callback. The largest measured native/World model-transform difference is
0.00002599.

This closes the declared-callback transform roster for the sampled mover state;
it does not cover every animation producer, ambient playback, construction
script, camera projection, or rendered frame.

```sh
PYTHONPATH=tools/re python3 tools/re/check_native_flight_model_pose.py \
  --all --script-binary build-o2/retail_script_test \
  --model-binary build-o2/model_transform_test
```

### Aratrans joined map-backed shore unload (2026-09-25)

The Lake Lokken `aratrans`/`araarch` route now completes through native unload
mission, live navigator arrival, placement, passenger release, and mover
retirement. Standard and Crusades pass in Release, Debug, and optimized Debug.
Native TNT grading queries 698 cells (906 grade calls); the one-waypoint route
ends inside the authored 385-pixel unload circle. Native and World agree for
3,494 physical mover steps, including one navigator arrival wake and 16
map-backed passenger-placement checks.

The fixture loads the actual base/Crusades FBI override and checks its movement
profile against the World unit definition. It uses one passenger, defers full
terrain rescans during this joined mover comparison, and controls pool/player,
allocator, scheduler request weight, and other host services. It does not test
capacity accounting or live blockers. A 2,500-step limit was too short for this
slow carrier; the probe now allows up to 10,000 and the passing invocation uses
4,000.

```sh
python3 tools/re/check_surface_unload_map_route.py \
  build-o2/transport_test --retail-root /home/pocket_geek/tak_data \
  --map 'Lake Lokken' --start 240 120 --target 240 350 \
  --carrier aratrans --passenger araarch \
  --native-map-mover-steps 4000 --native-live-unload
```

### ZONHUNT construction anchor follow-up (2026-09-25)

The placed-construction trace shows the monarch root piece remains at zero
relative offset, its native/display COB state matches the World VM at the
sampled boundaries, and the calculated site-relative screen anchor agrees with
the native projection to within 0.5 pixels. The native transform for the
shipped 48-piece `zonhunt.3do` also matches World within 0.00001716 world
units. The 90–108 world-unit northward separation seen in this fixture follows
from flight altitude and the monarch's orbit around the construction site; it
does not establish a mismatch in the live view.

The remaining useful observation is synchronized actual-map and camera data:
unit and site positions, flight altitude, camera offsets, and GameView's
projected anchors. The current flat-map fixture does not capture camera or
framebuffer output, so no production correction is justified from it.

### Weapon impact effect-class route (2026-09-25)

`spawnWeaponImpact` centralizes the production land/water effect-class choice
and particle fallback used by weapon hits. The headless Lake Lokken fixture
loads Arapult's authored land and water variants, checks empty-water-class
fallback, impact position/tick, last-frame and expiry timing, and missing-class
particle fallback. It passes in Debug and optimized Debug, with a separate
Release build of the client source.

This fixture calls the production impact helper directly; it does not create a
real projectile collision or compare retail hit-to-render output, model pixels,
or arrow/bolt appearance. The earlier “laser beam” report remains unverified
by this test and is parked for a more specific reproduction.

```sh
python3 tools/re/check_weapon_impact_effect_route.py --binary build-o2/takclient
ctest --test-dir build-dbg -R weapon_impact_effect_route --output-on-failure
ctest --test-dir build-o2 -R weapon_impact_effect_route --output-on-failure
```

### Full shipped surface-carrier unload roster on Lake Lokken (2026-09-25)

The asset-backed joined unload test now covers every shipped water-moving
transporter from a water start at cell `(240,120)` to Araarch at `(240,350)`.
All ten profiles match the native `0x508cd0` grade route and remain paired
through live `GROUND_UNLOAD` arrival, release, and the physical mover. Every
carrier/balance combination passes in Release, Debug, and optimized Debug: 60
joined traces total. Each run observes one navigator arrival wake and 16
map-backed passenger-placement checks. The route is one waypoint and ends
inside the selected FBI unload circle.

| Carrier | Movement class | Shipped capacity | O2 Standard / Crusades release steps |
| --- | --- | ---: | ---: |
| Aratrans | WATER5 | 50 | 3494 / 3494 |
| Arawar | WATER4 | 18 | 1286 / 1286 |
| Creiron | WATER3 | 12 | 1293 / 1386 |
| Crester | WATER3 | 20 | 1861 / 1622 |
| NpcBotl | WATER3 | 1 | 1076 / 1076 |
| NpcRixx | WATER4 | 12 | 1244 / 1244 |
| VerHarp | WATER4 | 16 | 1076 / 1076 |
| VerMan | WATER4 | 20 | 1205 / 1205 |
| VerScout | WATER3 | 4 | 945 / 945 |
| Vertrans | WATER4 | 32 | 1488 / 1488 |

The grade probe resolves the actual base or `unitscb` FBI and checks its
movement inputs against World. Its path-cost assertion now derives turn and
heavy-slope costs from the selected unit profile; this admits authored
per-carrier turning values while still checking the full cost tuple. The 10
carriers × 2 balances × 3 builds yield 60 passing joined traces. Each trace
exercises one Araarch. Separate two-passenger traces check native linked-list
unload sequencing, the production capacity-helper boundary, and live placement
against the first released passenger. Native boarding rejection at capacity
and following the first passenger's PARK movement until the second release
remain open. Live blockers and other maps remain separate gates.

```sh
python3 tools/re/check_surface_unload_map_route.py \
  build-o2/transport_test --hpitool build-o2/hpitool \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier creiron --passenger araarch \
  --native-map-mover-steps 5000 --native-live-unload
```

### Profiled air unload for all three shipped flying transports (2026-09-25)

`probe_transport_air_unload_map_flight.py` now accepts each shipped flying
transporter's retail FBI profile and uses the selected Crusades override when
present. ZONROC, CREAERI, and TARSHIP each match World through a live native
height scan, ridge flight, unload-circle arrival, passenger release, PARK, and
mission retirement. All 18 profile/balance/build combinations pass across
Release, Debug, and optimized Debug.

| Carrier | Standard ticks | Crusades ticks |
| --- | ---: | ---: |
| ZONROC | 288 | 252 |
| CREAERI | 376 | 376 |
| TARSHIP | 412 | 460 |

This uses a controlled 100-to-220 height ridge and a direct unload circle,
not a shipped-map route search. Landing feasibility, effects, detach, and PARK
installation remain host hooks. The flight mover runs retail's live height
scan over its native `0x50e740` sector table, with its selected max velocity,
acceleration, braking, turn rate, cruise altitude, transport distance,
footprint, multipliers, and half-cell interval populated from the FBI.
The sector check permits any sector actually overlapped by the carrier's
grid-origin footprint; this is needed for the large ZONROC footprint and
continues to reject pointers outside the unit's occupied rectangle.

```sh
PYTHONPATH=tools/re python3 tools/re/probe_transport_air_unload_map_flight.py \
  --binary build-o2/transport_test --carrier tarship --crusades --steps 600
```

### LIFBIRD ambient idle and flight animation (2026-09-25)

LIFBIRD's shipped `Create` starts `FlightControl`, `AnimationControl`, and
`StaticAnim`; it has no generic mover callback, so the 26-unit flyer callback
roster could not observe these animations. The new headless check runs both
idle and positive-speed profiles for 500 updates. All 501 native COB scheduler
boundaries match World for each profile. Idle advances the body and head; the
fly loop advances all six named wing pieces. Native and World 3DO transforms
also match at five idle and seven flying sample ticks, with a worst delta of
0.00000811 world units. Release, Debug, and optimized Debug pass, and the
optional `native_lifbird_ambient_pose` CTest is registered when the retail ICD,
extracted bird assets, Python, and Unicorn are available.

The host controls LIFBIRD's speed reads; the test does not connect live map
movement, a flight-state transition, camera projection, or rendered pixels.

```sh
ctest --test-dir build-o2 -R '^native_lifbird_ambient_pose$' --output-on-failure
```

### Moving-target native air pickup on Lake Lokken (2026-09-25)

`probe_air_pickup_native_moving_target.py` starts the real ZONROC VTOL pickup
dispatcher on Lake Lokken's shipped TNT and moves Araarch 112 pixels along a
shoreline strip whose full 2×2 footprint is validated by native `0x507d10`.
Retail follows the passenger's updated position, attaches it, retires the
pickup order, and installs `BeCarried`. Standard attaches on tick 1,044 and
Crusades on tick 1,011; Release, Debug, and optimized Debug pass in both
balances.

Araarch's smooth shoreline trajectory is host-scripted. The trace therefore
establishes that the native flyer tracks a moving passenger, but does not test
Araarch's ground route search/mover or a map-wide flying route search.

```sh
PYTHONPATH=tools/re python3 tools/re/probe_air_pickup_native_moving_target.py
PYTHONPATH=tools/re python3 tools/re/probe_air_pickup_native_moving_target.py --crusades
```

### Multi-passenger surface unload sequencing (2026-09-25)

`check_surface_unload_map_route.py --native-cargo-count 2` seeds two linked
Araarch records, then runs the full Lake Lokken Aratrans GROUND_UNLOAD trace.
Retail advances the same live mission through both cargo entries, detaches them
in sequence, and retires the carrier order. All six Release/Debug/optimized
Debug × Standard/Crusades traces pass. The optimized Standard trace releases
at physical mover steps 3,494 and 3,511 and performs 32 native map-backed
placement checks.

The selected Aratrans FBI limits are count/total-size/largest-passenger
50/200/16 in Standard and 100/400/16 in Crusades. Araarch costs 4, so the
production capacity helper accepts up to those profiles' computed limit and
rejects one more. NpcBotl's count cap of one is also checked against that
helper in both balances and all three builds.

This initial fixture does not maintain released cargo in a live retail entity
occupancy list. Both recorded landing origins are `(240,350)`, and reserving
the first footprint makes the second passenger wait at the fixed unload point.
The later same-Phase occupancy probe below corrects this limitation; following
the first passenger's PARK movement until the second release and testing other
maps remain open.

```sh
python3 tools/re/check_surface_unload_map_route.py \
  build-o2/transport_test --hpitool build-o2/hpitool \
  --retail-root /home/pocket_geek/tak_data --map 'Lake Lokken' \
  --start 240 120 --target 240 350 --carrier aratrans --passenger araarch \
  --native-map-mover-steps 5000 --native-live-unload --native-cargo-count 2
```

### ZONROC BeginFlight model-pose join (2026-09-25)

The optional `native_air_transport_animation_pose` CTest calls retail's
`0x416c50` BeginFlight producer for ZONROC, advances its COB once, and compares
the full 1,742-word native checkpoint with World. All six named wing pieces
advance. Retail `0x4ee620` and the World geometry helper match for all 43 model
pieces and 368 vertices within 0.00001529 world units. Release, Debug, and
optimized Debug pass.

The mission services and GET profile are controlled by the fixture. It does
not run a full carrier order, movement, camera projection, or rendered frame.

```sh
ctest --test-dir build-o2 -R '^native_air_transport_animation_pose$' \
  --output-on-failure
```

### CREAERI flight-to-ground animation boundary (2026-09-25)

CREAERI has no `BeginFlight` or `BeginLanding` script methods, so its distinct
carrier animation edge is the engine-driven flight-to-ground transition. The
native `0x4dc800` mover emits `MoveRate(0)` and `setSFXoccupy(4)` at boundary
18 (COB event tick 17). All 19 native/World COB snapshots match, both
propellers advance across the transition, and native `0x4ee620` matches the
World model-transform helper for 24 pieces and 370 vertices within 0.00001522
world units. Release, Debug, and optimized Debug pass; the optional
`native_creaeri_landing_pose` CTest is registered when the local ICD, extracted
CREAERI assets, Python, and Unicorn are available.

This controlled mover transition does not cover a transport order, map route,
camera projection, rendered frame, or GUI.

```sh
ctest --test-dir build -R '^native_creaeri_landing_pose$' --output-on-failure
ctest --test-dir build-dbg -R '^native_creaeri_landing_pose$' --output-on-failure
ctest --test-dir build-o2 -R '^native_creaeri_landing_pose$' --output-on-failure
```

### Native placement against a detached passenger footprint (2026-09-25)

The two-passenger Lake Lokken unload probe installs the first detached Araarch
in the same live retail entity pool and cell-occupancy plane used by the
continuing `GROUND_UNLOAD` mission. At passenger 2's real native `0x507d10`
call, the fixture verifies the first Araarch is alive at `(240,350)` and all
four footprint cells contain entity id 2. Standard and Crusades O2 traces both
reach this check after 3,496 physical mover steps. Retail returns strict `0`
and allow-moving `1` for passenger 2 at that occupied point; the second
passenger stays linked, and the mission selects no alternate cell. This
supersedes the earlier cross-Phase observation.

To give passenger 1 the native occupancy state needed by the mover, the fixture
sets its ground-mover mode to 1 and calls retail `0x5066f0`, rather than writing
the four occupancy words itself. That routine registers all four cells and the
passenger's sector link. The actual `GROUND_PARK` mover then runs retail's old
footprint cleanup and new footprint registration: its initial update at tick
3,527 clears the four old words at `0x506bc7` and registers the intersecting
cell `(240,350)` at `0x506a3d`. A later write hook records the last candidate
cell clearing at tick 3,548. At that point passenger 1 is at origin `(239,348)`
and the candidate cells `[(240,350),(241,350),(240,351),(241,351)]` all read
`[0,0,0,0]`.

The carrier's real dispatcher retries that now-free candidate at tick 3,550;
native `0x507d10` returns `1`. Passenger 2 stays linked while retail's
transfer counter advances from 1 to 15 on accepted placement calls at ticks
3,550–3,564. At tick 3,565, `GROUND_UNLOAD` makes another native placement call
which returns `1`, reaches its `0x51b4f0` detach call, and retires the mission:
the mission stage changes from 2 to 0, the cargo head and passenger owner both
become null, and the recorded release tick is 3,565.

The default continuation still uses a diagnostic `0x51b4f0` hook, but a
separate bounded O2 Standard variant now runs its retail body for passenger 2.
It places passenger 2 in global entity slot 3 (`0x71000270`) and gives it the
sector state of a carried unit: native `0x506650` inserts the record into the
destination sector, then native `0x5065f0` removes it while preserving its
sector pointer. At tick 3,565, the real `0x51b4f0` call receives
`(passenger2, 0, -1, 1, 1)` and calls `0x51b5a0` with payload
`07 03 00 00 00 ff 01`. The retail body clears passenger 2's owner, removes it
from the carrier cargo head, and re-adds it at the sector-list head. The same
tick's native `0x506a3d` writes entity id 3 into all four candidate cells, and
the mission transitions from stage 2 to stage 0. The resulting flags are
`0x01004001`; all four cells contain id 3.

This validates passenger 2's native detach body given an already-carried
entity record with a real id and sector pointer. The test constructs that
carried state in the fixture instead of replaying the initial boarding order;
passenger 1's earlier detach also remains on the diagnostic hook. The corrected
post-vacancy and native-body traces pass in Release, Debug, and O2 for both
Standard and Crusades balance. All six runs report the same vacancy tick
(3,548), first accepted placement tick (3,550), and native passenger-2 release
tick (3,565), with the same four candidate cells registered to entity id 3.

```sh
for binary in build/transport_test build-dbg/transport_test build-o2/transport_test; do
  for balance in standard crusades; do
    balance_args=()
    if [[ $balance == crusades ]]; then balance_args+=(--crusades); fi
    PYTHONPATH=tools/re python3 tools/re/check_surface_unload_map_route.py "$binary" \
      --retail-root /home/pocket_geek/tak_data \
      --map 'Lake Lokken' --start 240 120 --target 240 350 \
      --carrier aratrans --passenger araarch \
      --native-map-mover-steps 4500 --native-live-unload \
      --native-cargo-count 2 --probe-native-occupancy --native-detach-second \
      "${balance_args[@]}"
  done
done
```

### VTOL pickup order and GROUND2 route boundary (2026-09-25)

The native `0x403430` `Move_Seek_Pickup` handler only creates a passenger
GROUND2 route when the referenced carrier has `GROUND_PICKUP` code 21. When
the carrier has `VTOL_Pickup` code 62, the handler takes the wait/poll path and
does not submit a route request. This is retail order behavior, not a
pathfinder failure.

An earlier probe mistook registry code 28 (`Move_Ground_Formation`) for
ordinary `Move_Ground`; that fixture was discarded. Native descriptor lookup
gives ordinary `Move_Ground` code 27 (handler `0x402b00`). The corrected probe
uses that constructor and verifies the mission code and destination. It then
sets retail's `0x20000` append flag before native `0x4d7750`, placing code 30
at `passenger+0x64` behind code 27 at `passenger+0x60`.

The route worker installs two points; Araarch moves 1.25 pixels on the first
physical update. The corrected O2 trace advances the real per-unit order path:
active dispatcher `0x4d8450`, then queued dispatcher `0x4d85e0`. At tick 4,
ZONROC's code-62 handler returns 8 and retires because the passenger's current
head is still code 27. Retail then dispatches queued code 30; it also returns 8
and is removed. Araarch's controller remains null, no cargo attaches, and the
fixture reports one pending path request whose owner is not isolated. The
trace stops before code 27 completes.

These are O2 Standard Lake Lokken diagnostics for one ZONROC/Araarch pair. The
route worker, visibility, feature bodies, UI/effects, and COB method tables
remain controlled. Separate World regressions now confirm that queued load and
unload orders preserve existing movement and finish boarding/disembarkation
after those routes complete. The native diagnostic above still covers a different
ordering: carrier pickup is already active while a GROUND2 move is ahead of
`Move_Seek_Pickup`. Retail's result for coordinated queued movement on both
units remains unverified.

```sh
PYTHONPATH=tools/re python3 tools/re/probe_air_pickup_native_ground_route.py \
  --hpitool build-o2/hpitool --max-ticks 9000
PYTHONPATH=tools/re python3 tools/re/probe_air_pickup_native_ground_move_seek.py \
  --hpitool build-o2/hpitool --max-ticks 9000
```

### ZONROC accepted-site BeginLanding model-pose join (2026-09-25)

The retail `0x416cd0` accepted-site producer dispatches `EndTransport` then
`BeginLanding` to the attached ZONROC COB. After one native VM tick, all 1,742
COB state words match World, all six named wing pieces advance, and retail's
`0x4ee620` transform matches the World geometry helper for 43 pieces and 368
vertices within 0.00001903 world units. Release, Debug, and optimized Debug
pass. The optional `native_zonroc_landing_pose` CTest is registered when the
local ICD, extracted ZONROC assets, Python, and Unicorn are available.

The accepted site, mission services, and GET profile are controlled by the
fixture. This check does not run a full transport order, movement, camera
projection, rendered frame, or GUI.

```sh
ctest --test-dir build -R '^native_zonroc_landing_pose$' --output-on-failure
ctest --test-dir build-dbg -R '^native_zonroc_landing_pose$' --output-on-failure
ctest --test-dir build-o2 -R '^native_zonroc_landing_pose$' --output-on-failure
```

### Sea Dragon Spine Arawar shore unload (2026-09-25)

The map-backed native-grade and joined `GROUND_UNLOAD` profiles now include
Arawar/Araarch on Sea Dragon Spine. From water cell `(240,95)` to shore point
`(240,120)`, retail and World reconstruct the same three-waypoint route using
183 distinct native grade cells across 226 calls. In Standard and Crusades,
the joined trace keeps the retail mission, navigator, mover, and passenger
placement live through 61 matching terrain-scan deadlines, 16 native
map-backed placement checks, and cargo release at physical step 260. Release,
Debug, and optimized Debug all pass both balances (six traces).

This adds a second carrier profile on Sea Dragon Spine. The case uses one
Araarch, has no traffic blocker, and does not cover other maps for Arawar.

```sh
PYTHONPATH=tools/re python3 tools/re/check_surface_unload_map_route.py \
  build-o2/transport_test --retail-root /home/pocket_geek/tak_data \
  --hpitool build-o2/hpitool --map 'Sea Dragon Spine' \
  --start 240 95 --target 240 120 --carrier arawar --passenger araarch \
  --native-map-grades --native-map-mover-steps 1500 --native-live-unload \
  --terrain-scan-after 1
PYTHONPATH=tools/re python3 tools/re/check_surface_unload_map_route.py \
  build-o2/transport_test --retail-root /home/pocket_geek/tak_data \
  --hpitool build-o2/hpitool --map 'Sea Dragon Spine' \
  --start 240 95 --target 240 120 --carrier arawar --passenger araarch \
  --native-map-grades --native-map-mover-steps 1500 --native-live-unload \
  --terrain-scan-after 1 --crusades
```

### Cairbray Coast Landing live-scan diagnostic (2026-09-25)

Cairbray's `(80,4)` Vertrans start and `(127,8)` Araarch shore target produce a
valid World route, but the joined live-scan profile does not pass. At physical
step 129, native and World both enter scan tick 144 from
`(90895837,3735552,5751650)`, heading 53544, speed 100973. The native fixture's
exploration plane is still all-zero, so retail stops its forward scan after the
first grade-6 probe and selects speed mode 1/deadline 148. World has explored
the first nine forward probe cells during the approach; it reaches the tenth
probe, which is grade -1, and selects speed mode 2/deadline 150. The eight
neighbor grades match exactly.

Replaying retail `0x4dba80` from that same pose and tick with the visibility
plane enabled produces the same ten forward probes, grades, eight neighbor
probes, speed mode 2, and deadline 150 as World. This isolates the failure to a
missing fixture input: it does not propagate World exploration updates into the
native visibility plane while the carrier moves. Cairbray therefore remains
out of the supported native-grade and joined live-unload profile lists until
that per-step visibility state is modeled. Its route-only result is not live
scan parity.

The bounded O2 Standard diagnostic used the following invocation while
Cairbray was temporarily admitted to both profile lists. Temporary Unicorn
hooks captured native `0x4dba80`/`0x4db640` inputs and results, and temporary
World logging captured scan grades and visibility bits; that instrumentation
was removed after collecting the probe rows. The current checker intentionally
rejects Cairbray at its support gate until the missing exploration input is
modeled.

```sh
PYTHONPATH=tools/re python3 tools/re/check_surface_unload_map_route.py \
  build-o2/transport_test --retail-root /home/pocket_geek/tak_data \
  --hpitool build-o2/hpitool --map 'Cairbray Coast Landing' \
  --start 80 4 --target 127 8 --carrier vertrans --passenger araarch \
  --native-map-grades --native-map-mover-steps 1000 --native-live-unload \
  --terrain-scan-after 1
```

### Cairbray VTOL flight-mover diagnostic (2026-09-25)

A separate O2 Standard trace uses Cairbray's actual TNT, shipped ZONROC/Araarch
profiles, retail `0x50e740` height sectors, live `0x4dc800` movement, and
unhooked `0x507d10` placement. The independently checked 24×28 native sector
table is correct, and retail accepts Araarch's flat, feature-free GROUND2 site
at `(97,153)`. The initial synthetic ZONROC `UnitDef` omitted mobile flag
`+0x24a`, which caused `0x507d10` to take its building-placement path. That
false rejection triggered retail's collision-braking branch at tick 232. After
setting the mobile flag in the fixture, all 377 World/native rows match through
the height-sector scans, 11 sector relinks, landing, passenger release, PARK,
and mission retirement. This was a probe-fixture omission; no production mover
change was needed.

```sh
PYTHONPATH=tools/re python3 tools/re/probe_cairbray_zonroc_unload.py \
  --binary build-o2/transport_test --retail-root /home/pocket_geek/tak_data \
  --hpitool build/hpitool --steps 1200
```

### Queued unload range is checked at activation (2026-09-25)

A queued unload previously omitted its approach when the carrier was within
transfer range at command issue time. An earlier queued move could then take
it outside that range, leaving a bare unload to use ordinary point movement
instead of the native transport-distance-minus-34 circle approach. Deferred
unloads now retain the existing approach mission, which checks the carrier's
position when activated. Immediate in-range unloads retain their existing path.

The transport regression checks the circle mission and runs a move away,
unload back near the starting position, and subsequent move through completion.
Release client, server, and transport test rebuilt; transport, shipped roster,
and Standard/Crusades map roundtrip tests all pass. This corrects use of stale
command-time range; it does not establish full native queued-command parity.

### Queue an unload while pickup is running (2026-09-25)

The armed Unload command and World command handler previously rejected an empty
cargo list even when queueing behind a pickup. Both now accept queued unloads
before cargo arrives, and the transport panel keeps Unload available during
pickup. Immediate unload still requires cargo; only transport types can accept
an unload. This lets the existing mission handler inspect cargo when the queued
order actually runs.

New air and surface integration cases failed before the change and now pass:
issue pickup, immediately queue unload through applyCommand, then observe boarding
and delivery at the requested destination. Release client/server rebuilt and all
four transport suites passed, including both map-roundtrip balance modes. The
existing native action-mode probe also passes its cantransport-to-Unload cursor
gate; that probe does not prove the entire native queued-command sequence.

### Reclaim drawing uses the pinned scene snapshot (2026-09-25)

The reclaim sparkle draw path previously tried to lock the simulation mutex
and omitted both worker and target sparkles when the worker owned it. It also
combined a live feature position with an older rendered unit position. UnitR
now captures the live feature's position and footprint during frame publication;
the draw path consumes that copy without accessing the simulation. Publication
clears the optional target when the order ends or the feature disappears.

Release takclient and retail_visual_test rebuilt, and retail_visual passes.
The missing-draw condition is established by the removed try_lock branch;
no controlled contention screenshot was captured. This is a snapshot integration
fix. Reclaim's legacy anchored sprite and model-texture animation timing still
need retail behavior investigation; neither was changed or certified here.

### Model texture sequence classification (2026-09-25)

Production loader now retains every frame of model texture sequences. Native
`4be8e3..4be978` initializes sequences with more than one frame, classifies only
10-frame sequences containing the lowercased substring `logo` as player colours,
and registers all other multi-frame sequences for animation. The substring at
`614698` is `logo`; `5d4670` performs the substring search. The previous loader
retained only one frame unless the sequence contained exactly ten frames, then
animated only selected keywords. Scanning extracted texture banks identifies 30
sequence classifications affected, including five-frame build pads and fire/green
arrows, eight-frame Lokken crystals, and five-frame `tesla2`.
Unpacked model rendering now uses the same animation frame as atlas rendering,
instead of selecting a player-colour frame for animated textures.

Validation: Release takclient and retail_visual_test rebuilt; retail_visual CTest
passed; git diff --check passed. No new fixtures and no retail GUI launch. This
fix establishes sequence selection, not playback timing or rendered visual parity.
The existing four-fps clock still needs replacing after checking native update
cadence and non-looping model-texture rendering semantics. Native `421ea0` updates
registered clocks through `5373d0`; its call site is `526438`.

### Model texture playback clock (2026-09-25)

Replaced the fixed four-fps wall clock with each sequence's authored low-word
frame durations and loop flag, sampled using the existing retailEffectFrame
helper at the captured simulation tick. Atlas rendering, unpacked model rendering,
and cutout shadow masks share the selected per-sequence frame. Atlas repainting
still occurs only when the selection changes or the atlas is rebuilt.
Removed the completed-visible-glow-unit gate: retail's registered texture list is
advanced unconditionally by `421ea0` from simulation tick body `526438` (the tick
boundary is `526351`). This also allows animated projectile textures to advance
without a visible completed glow unit. Paused simulation holds the sampled frame.

All 44 animated sequences in the extracted texture banks loop and have ten-tick
frame durations (three frames per second at normal 30-tick simulation speed).
An ephemeral direct emulation check loaded each shipped sequence's metadata into
`537390`, then called the actual registered-list update `421ea0`: 7,724 samples
across two loops per sequence matched the authored-duration selection. No new
fixture files were added. The existing native clock probe also passed 147,456
updates and matched the shared C++ clock on 1,280 native timelines. The existing
retail_visual CTest passed. No retail GUI or controlled visual capture was used;
these checks prove timing/selection, not complete rendered animation parity.

### Atlas creation uses the selected animated frame (2026-09-25)

The atlas initialization path still chose every texture by player colour, even
for animated sequences. Atlases created after the main per-frame animation
update (for example by a projectile or debris draw) could therefore display a
player-indexed frame until the next update. atlasFor now initializes animated
regions with the same selected per-sequence frame used by the update and unpacked
rendering paths, including leaving an expired non-looping region empty. Static
textures and player logos retain their existing selection. Removed the unused
per-unit usesGlow flag and its model scan, left over from the former visibility
gate. Release client rebuilt; existing retail_visual and four transport CTests
passed. No simulation or pathfinding changes and no new fixtures.
Optimized Debug client also rebuilt. Its existing headless weapon-impact route
check passed with the updated renderer (Arapult land/water variants, fallback,
location and authored expiry). This is a rendering integration smoke check,
not a pixel assertion specifically targeting late-created atlas contents.

### Reclaim fallback follows simulation time (2026-09-25)

The existing reclaim sparkle now samples authored frame durations at the captured
simulation tick instead of advancing at a hardcoded wall-clock 15 fps. It holds
while paused and follows simulation speed. This retains the existing repeating,
anchored fallback and does not claim native emitter/placement parity. Renamed the
helper drawReclaimSparkle and removed ignored footprint arguments so its remaining
scope is explicit. The snapshot currently captures feature targets only; corpse
reclaim has no corresponding visual target. That observation alone does not
establish which native corpse-reclaim emitter should be implemented.

Release client rebuilt; retail_visual passed and the existing native clock probe
passed 147,456 updates plus 1,280 paired C++ timelines. No new fixtures or retail
launch. An interactive paused-reclaim capture was not performed.

### Reclaim target sparkle removed from production (2026-09-25)

Native mission registry row `5eb9fd` maps Reclaim to `406250` (the following
`406670` handler is ReclaimArea). The reclaim handler has one construction-emitter
call, `40658e`: worker drawable `+c0`, falling direction 0, default faction art 0.
It does not emit a matching particle at the feature. The production renderer now
removes that unsupported target sparkle while retaining the worker fallback.
Construction's actual two-ended particle rendering is unchanged.

An ephemeral Unicorn check executed the real `40657c..406593` guard with remaining
values -1,0,1,14,15,16,17,30,300. Exactly the values above 15 emitted one worker
request. No fixtures added. Native reclaim initializes its countdown at `4063bf`
from feature-definition float `+128` plus 15; `406539` subtracts two each dispatch
and schedules the next wake with argument 2 to `4d6a10`. This countdown is not yet
represented by World's energy-based work drain. Therefore this change does not
claim native emission cadence, particle placement, or corpse-reclaim parity; the
next emitter change needs that countdown relationship established first.

Release client rebuilt; retail_visual and git diff --check passed. No retail GUI
launch or visual capture. Earlier notes describing reclaim sparkles at both ends
were an implementation description and are superseded by this native evidence.

### Reclaim worker particle integration (2026-09-25)

Native feature loader `494040..49404a` reads `energy` into definition `+128`.
Reclaim initializes its countdown to trunc(energy + 15), subtracts two per work
dispatch, and emits one falling worker particle while the resulting count exceeds
15. This explains the worker-only emitter and its two-tick wake cadence; zero
energy does not produce that emission.

Production feature reclaim now requests the shared construction particle emitter
at two-active-work-tick intervals, only while positive-energy work remains. The
per-job cadence resets on new work, cancellation and completion and participates
in state hashing. Existing particle motion, owner-relative projection, front/back
partition, faction art and expiry now render reclaim as distributed falling
particles instead of a single looping sprite. Removed the old sprite helper and
the now-unneeded feature-target render snapshot. Existing particles drain after
work ends. This uses the existing sim-to-render particle snapshot and does not
look up live simulation targets from the renderer.

Scope: feature reclaim only. Corpse reclaim is unchanged. Current World reclaim
uses an energy-proportional mana drip and work rate rather than retail's mission
countdown and completion delay. This patch deliberately ties effects to actual
World work and does not claim exact whole-job retail timing or change income,
reclaim duration, pathfinding, or footprint release. Native reclaim mission timing
is therefore still an open limitation, separate from particle representation.

The existing navblock test now checks multiple two-tick worker emissions, live
particles, unchanged feature/footprint removal, and emission cessation plus
particle expiry after completion. No standalone fixture was added. Existing
native particle tests passed 4,096 emitter/motion/expiry cases and 2,048 draw
projection/partition cases. Full Release build/test results follow below.
Full Release rebuild succeeded, including takclient and takserver. All 56 Release
CTests passed (47.78 seconds), including the extended navblock reclaim checks,
transport/map round trips, animation roster, flying combat and native landing
poses. git diff --check passed. No retail launch or rendered reclaim capture.

### Reclaim particle command lifecycle verification (2026-09-25)

Extended the existing navblock regression for the worker-particle fix with actual
Cmd::Reclaim replacement, Cmd::Stop, and zero-energy feature cases. Replacement
leaves the old feature's work unchanged and advances the new target; Stop freezes
remaining work and emission counts while existing particles drain; zero-energy
reclaim completes without particles. The rebuilt navblock CTest passed.

An initial diagnostic called World::reclaim/stop directly and failed: those helper
calls bypass applyCommand's redirect/cancelBuilds contract. Repeating through the
real command path passes, so no production order-switching bug was established
and no speculative engine change was made. These tests verify lifecycle for the
recent emitter integration, not rendered appearance. The planned rendered reclaim
capture is still outstanding. git diff --check passed; no retail launch.

### Reclaim emitter rendered in the engine (2026-09-25)

Added a small TAK_RECLAIM_CAPTURE branch to the existing local testBuild harness
specifically to inspect the new worker-particle integration. It finds an authored
positive-energy map feature and a legal nearby builder position, issues the normal
Cmd::Reclaim, and sets the camera. No additional test framework or CTest was added.
The first 500-energy filter found no suitable site; 250 admits the common trees.

Optimized Debug client captures on Ulasem Arena used Araking and Zonhunt at
(104,184), reclaiming feature 7052 at (200,184). Inspected both images:
`/tmp/reclaim-araking-arena.png` shows distributed gold worker sparkles;
`/tmp/reclaim-zonhunt-arena.png` shows distributed purple worker sparkles. Neither
has the former separate tree-anchor sparkle. This verifies the sim particle
snapshot reaches the normal faction-art renderer. It does not establish native
pixel/timing parity, falling motion from a single still, or airborne reclaim.

Reproduce with SDL_AUDIODRIVER=dummy TAK_RECLAIM_CAPTURE=1
TAK_CONJURE_BUILDER=araking (or zonhunt), TAK_SHOT_MS=500, and
`build-o2/takclient game 'Ulasem Arena' --data assets/game --testbuild --nofog --shot /tmp/reclaim.png`.
No retail GUI was launched. Optimized Debug client rebuilt; Release client
rebuild also performed for the capture-harness source change.

### Repair particle emission wired into World (2026-09-25)

The RepairUnit registry row `5eb9cb` names handler `407190`. Its success branch
`4076d0..4076f0` calls the model emitter once for the worker (direction 0, default
art) and once for the target (direction 1, default art), followed by a one-tick
wake. An ephemeral Unicorn check exercised zero and nonzero repair results:
failed work produces neither request; success produces exactly those two calls.

World::tickRepair restored HP and debited mana but emitted no particles. It now
calls the existing emitter for falling worker and rising target particles only
after affordable, in-range repair work succeeds. Existing particle snapshots,
faction art, front/back rendering and expiry handle display. Repair HP, mana,
range, movement and completion logic are unchanged.

A focused case in the existing production test failed before the engine change
and passes afterward. It checks no emission without funds, paired live particles
during successful repair, and no new emission plus eventual particle expiry when
repair finishes. Release takclient, takserver and affected test targets rebuilt;
all six selected CTests passed: production, retail_visual, transport,
transport_roster and both transport_map_roundtrip balances. git diff --check
passed. No new standalone fixture or retail GUI launch. Repair rendering has not
yet been visually captured; this establishes the missing emission connection.

The existing native queued-ground-move/air-pickup probe also passed in both
balances. It retires pickup at tick four while Move_Ground remains active, as
previously observed, so it does not justify changing carrier waiting behavior.
Corrected its stale output claiming World always clears passenger orders:
World now honors the queue flag. The probe is still explicitly native-only and
does not claim a joined World comparison for that ordering.

### Route every live construction/repair particle through drawUnit (2026-09-25)

The main draw classifier only selected under-construction units and active
build/reclaim workers for interleaved model/effect rendering. A repair target (and
a repair worker without another special draw condition) instead went through the
ordinary geometry batches, which never submit constructionParticles. Completed
or cancelled workers likewise lost the visible remainder of their particles.
The classifier now includes every unit with a nonempty captured particle vector,
so both repair ends and surviving particles reach the existing front/back passes.

Added a repair variant to the existing conjure capture setup, selected with
TAK_CONJURE_REPAIR=1; it spawns a half-health target and issues Cmd::Repair.
Inspected `/tmp/repair-araking.png` from the optimized Debug client on Ulasem Arena:
gold particles are visible around both Araking and Araarch. Invocation used
TAK_CONJURE_TEST=1, TAK_CONJURE_REPAIR=1, TAK_CONJURE_BUILDER=araking,
TAK_CONJURE_TARGET=araarch, TAK_SHOT_MS=500, SDL_AUDIODRIVER=dummy and the usual
`game 'Ulasem Arena' --data assets/game --testbuild --nofog --shot` arguments.
The screenshot uses SDL's headless software backend and verifies the normal draw
classification, not native pixels or an accelerated-driver comparison.

The builder-animation helper already includes repairId; this inspection does not
prove all World/COB callback timing. No speculative pose change was made. Release
and optimized Debug client rebuilds plus existing visual/production/navblock
checks are used to validate the rendering change. No retail GUI was launched.

### Let ground-builder scripts restore their own poses (2026-09-25)

GameView's mobile-builder transition called StopBuilding and then independently
started restore_x (or RestoreAfterDelay) for every ground builder. Removed the
second call. The shipped Araking COB's StopBuilding clears its building latch;
MeleeControl calls restore_x itself only when its movement/attack/building guards
permit it. Starting that routine externally races the controller and bypasses
those guards. Native KINGDOMS contains the StopBuilding entry-point name, but no
restore_x or RestoreAfterDelay lookup name; those are script-internal routines.
The same client stop path serves construction, repair and reclaim.

The ordinary rendered unit tree reads its Anim display VM via pieceFor; captured
pose overrides in the inspected paths serve debris/effect queries, not replacement
of that live unit pose. The display builder helper already includes repairId, so
there was no evidence for adding a second repair start callback in the renderer.

Existing builder-animation tests pass with both the shipped Araking and Zonhunt
COBs. Seven relevant CTests pass (retail_script, movement/builder animation,
door animation, animation roster, gate scripts, native air-transport pose).
Release client rebuilt and git diff --check passed. These checks plus script
inspection support removal of the unsolicited callback; a synchronized before/
after retail restoration capture was not performed. No new fixture or retail
GUI launch.
