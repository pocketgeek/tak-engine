# 0.9.12 release validation

Protocol **305** (was 304 in 0.9.11), replay format **15** (unchanged),
generator version **9** (unchanged).

Tagged `v0.9.12` at `a6e56964` (full
`a6e56964433479359555681e7029e866d706d050`, annotated tag object `0064630d`) and
published on 2026-10-30 19:40:20 UTC. `a6e56964` is the release-preparation
tree plus one CI-only commit that moves the macOS arm64 job from `macos-14` to
`macos-15` (the deployment target stays 14.0).

## Scope and compatibility

0.9.12 follows 0.9.11 (protocol 304; tag `v0.9.11` -> `fbae4bbd`, validation
commit `287b4006`). Nine branches were merged on main, the streaming and menu
changes and the retail fog display were committed on main itself, and each was
reviewed on its own; the code tested below is main `95ca9163`:

- **Streaming** (client only): `37dae0dc` keep to YouTube's bitrate (the
  recommended figure of YouTube's live encoder table per row, frame rate and
  codec, as the cap; RECOMMENDED; a saved higher value lowered),
  `120be0a1` UPLOAD CAN'T KEEP UP only when the encoder waits to send.
- **AI lodestone search** (`task-perf-aisearch`, merged as `ab8cf71a`):
  `e42d54ed` deposits tried nearest first, the first usable one wins (same
  commands: all 41,359 bundles of a 23-minute Absurd game byte-identical).
- **Sim hot spots** (`task-perf-simhot`, `e64707c4`): `3ca2001c` bucketed
  sacred sites, flat per-line opening records for Legion passages
  (hash-neutral: the seven gates, `navigation_determinism` and the user
  replay's final hash over 39,600 ticks unchanged).
- **Nav splits** (`task-perf-relabel`, `106ea10b`): `364446dc` a possible
  split is settled locally (`resolveSplit`) instead of relabelling the map;
  answers unchanged (root equality only), gates unchanged.
- **State hash** (`task-perf-hash`, `18dc8520`): `c057165d` word-at-a-time
  folds (`src/sim/statefold.h`), planes and scripts folded in fixed chunks on
  the sim pool (every hash value changes; the simulation does not: all seven
  gates send byte-identical bundles and match `posDigest` at every replay
  checkpoint against 0.9.11 in both directions), `ae2b02cb` the server skips
  the referee hash when no human slot reads it.
- **AI sight-only targets** (`task-ai-vision`, `b982ee86`): `e4974aba` our
  levels target only what their side has seen and explore otherwise,
  `20daa9d2` scouts attack-move and a lone ship may scout.
- **Combine** `3ce75ac7`: `kNetVersion` 304 -> 305, `replay_test` pins 305,
  retaken gates, scenario baseline (2,193 `hash@` keys) and navigation
  checkpoints (all 48).
- **AI fixes on main**: `7c2e3fc8` blind before the first view pass, heard is
  not seen; `894d23d4` backs out the found-base start check;
  `4d2fce88` the protocol 305 note in `docs/multiplayer-design.md`.
- **Menu**: `72ecf67d` a BACK button on SETTINGS, BENCHMARK and LOAD REPLAY.
- **AI knowledge** (`task-ai-knowledge`, `ff083b74`): `58d08824` our levels
  know what a player of their side is shown under the room's fog,
  `6da1017f` the think cost kept at main's on big maps.
- **AI scouting** (`task-ai-scouting`, `4b374c14`): `f75d0993` start spots as
  the room shows them, strength from what is seen, scouts that learn from
  losses, `d0a66de1` an explore-log counter (debug).
- **Settings autosave** (merged with main into `task-0912-ai2` as `27eafa28`):
  `d02f3387` Options, Voice Chat and Controls save every change, `767e8d10`
  their pictures retaken, `35879695` DEFAULTS re-opens audio on the default
  device and the Controls hint gets its own band.
- **AI first think** (`task-0912-ai2`): `eec342d6` player 0's tick-0 think
  waits a tick under NOT EXPLORED.
- **AI vacated bases** (`task-ai-vacated`, `4ce336f3`): `aa079d28`,
  `ae3da6c6` a beaten player's base, an enemy's or an ally's, is a start
  again after five minutes out of sight; `e49f8056` an explore-log counter.
- **Retail fog display** (client only, nothing hashed): `9835ee6d` the sim's
  sight plane, LosOn and minimap-only radar, retail's Glide overlay;
  `8ecab736` `World::setVisPlayer` removed; `016fbe71` only the touched
  squares rebuilt; `8d30ca45` minimap radar bucket lookup; `f1dab5c0` docs;
  `b7df5925` the two guide pictures whose fog changed; `9b5ec0dc` an AI
  comment; `95ca9163` `CLAUDE.md`.
- **Shot range** (`task-los-range`, after the preparation commits and
  `94459ddb`, the Windows fog-test fix): no structure footprint pad for a
  weapon whose shot dies at its range (`Weapon::shotEndsAtRange`: LOS bolt,
  lightning, flame, Guided), as retail's `530580`; from the user's 0.9.12
  Crusades replay, where a Death Totem (range 550) fired all game at a
  lodestone 584.5 px off and never hit. Protocol unchanged (305 was never
  released).
- **Range by weapon class** (`task-range-classes`): no structure pad for any
  weapon but melee (`Weapon::centreReach`), after emulating every retail
  weapon class's range test: the Dropped, Remote, Hailstorm, Earthquake, area,
  Wandering and freeze/stone/mind-control Line-of-Sight weapons kept the pad
  in the change above. Protocol unchanged.
- **Combat fixes from the attack sweep** (a dev harness over every armed unit
  type, branch `task-attack-sweep` `50bb6fbb`, not merged), integrated on
  `task-0912-four` from main `6dbdf9d2` with a merge commit each, then merged
  to main; protocol unchanged (305 was never released):
  - **Attack timing** (`task-vermage-aim` `a0cd32eb`, merged as `a0168baf`):
    `8e4625fa` an attack after a walk aims two ticks after the stop
    (`Attack_Chase` `0x403bb0`), `1f83633d` TurnDirection reaches the unit's
    own script, `a588bc72` / `d66d02ec` docs, `601e5e62` / `b7e274ba`
    `classic_ai_test` U13 and raid checks, `a0cd32eb` a comment.
  - **Range by weapon class** (`task-range-classes` `24eaf20f`, merged as
    `3b05b25c`; above).
  - **Blocked shots at long buildings** (`task-impact-footprint` `337c4159`,
    merged as `d87bb0dd`): a non-square body is its footprint rectangle, for
    the blocked-shot count and the clear-line test.
  - **AirToAir** (`task-airtoair` `1d9d7834`, merged as `60489e21`):
    `0a4ae3ad` flyers dogfight flyers (`0x41ca50`, `0x41e3d0`), `b7f8111e` a
    Dropped bomb waits for SET 22 and a hover attacker's facing, `8f6f3737`
    docs, `1d9d7834` CMake order.
  - **Integration**: `7c31c766` keeps `Order` at 320 bytes (the vermage and
    AirToAir fields each fit alone and together made 384; `attackChased`
    moved into the pad beside `chaseLeashed`, every line assertion
    unchanged). The CHANGELOG, sim.h and sim.cpp merge conflicts were
    textual (adjacent edits), resolved keeping both sides.
  - **Slot modes in the air missions** (`task-jugg-cannons` `777a5751`,
    merged on main after `b8d4be5a`): `c2554047` AirToAir stage 1
    (`0x41d5ae`) and `777a5751` AirToGround stages 1 and 2 (`0x41c308`,
    `0x41c31f`) turn the unselected slots automatic, kept on the attack
    order by a new hashed `Order::mixedSlots` (World::setSlotMode). The
    Juggernaut's cannons bomb the ground while its arrows fight a flyer;
    the Juggernaut's and Dirigible's other weapons pick their own targets
    in a ground attack. `mixedSlots` sits in the pad beside
    `reclaimHeading` so `Order` stays 320 bytes; the one sim.h conflict
    (both sides added a field after `airRunCount`) was textual.
- **Retail-check fixes** (eight branches, each fixed and reviewed on its own
  from main `8d671ee0`, integrated on `task-integrate-0912b` with a merge
  commit each, then merged to main; protocol unchanged, 305 was never
  released):
  - **Mission orders** (`task-mission-orders` `1870496b`, merged as
    `27c36e80`): the `.ota` / `SetMission` order strings read clause by
    clause as `0x513fb0`; `o`, `b`, `i`, `g`, `wa`, `w F N`, `v`, `a TYPE`,
    `s`, `d` as the original (`ScriptedOrder`, `World::scriptHold`); the
    Retail AI's `firstMission` reads a scripted head as class 0x13; the
    review added the table compaction (`1870496b`).
  - **Help and repair approach** (`task-repair-approach` `414554f5`, merged
    as `dc5b2a64`): HelpBuild `0x405cf0` and RepairUnit `0x407190` on the
    rectangle controller; the factory-yard step-out is our own addition
    (not in the original), **kept by the owner** as a deliberate non-retail
    improvement: it stops helpers blocking the factory yard (without it 84
    of the 232 door cases left a helper standing in the yard); the review
    fixed an own-site assist stall and a repeating conjurer's rally.
  - **Raise by order** (`task-resurrect-order` `c795530f`, merged as
    `c745a465`): `Cmd::Resurrect` (29), the resolver's resurrect / animate /
    clear per unit, no idle raise or idle corpse clear; our AI levels order
    their idle casters' raises.
  - **Harpy capture** (`task-harpy-capture` `ed38a8f8`, merged as
    `b443fdfb`): no contact capture, a charmed unit keeps its HP and loses
    its veterancy, no charm on an unfinished unit.
  - **VTOL_SeekAttack** (`task-air-seekattack` `c4480aee`, merged as
    `1a94cd76`): an attack flyer whose last order's target is lost circles
    its point (`0x4192f0`).
  - **Remote Effect timing** (`task-spell-timing` `10ab21bf`, merged as
    `db616d3d`): retail's aim point (SweetSpot plus lead) and tick timeline;
    `nimbustime` as the storm launch delay.
  - **Flyer group spread** (`task-air-group-move` `f5226ba8`, merged as
    `ce387184`): retail's `0x5214b0` offsets for flyers on a right-click
    move, MOVE and PATROL; ground units keep our spread (the user's ruling:
    a deliberate deviation, keeping the shape of a big air group).
  - **Three small fixes** (`task-small3` `bf4d7b1e`, merged as `130a954a`):
    a Divine Lodestone over the Standing Stones (S yards test terrain only
    where the sacred-site rule applies), flyers drawn after the shots and
    effects, no hit response from a unit under construction.
  - **Integration** (`2246f550`, `7638e3cb`): `Order` stays 320 bytes
    (the branches' fields each fit alone and together made 384:
    `repairLooks` moved to line 1's one-byte pad before `missionRadius`,
    `seekAttack` to the tail pad beside `aheadOfDefault`, `reviveKind`
    beside `mixedSlots`, `scripted` after `chaseOrigin`; every line
    assertion unchanged). Two code conflicts: the hash folds in
    `World::stateHash` (both kept) and `tickCombat`'s lost-target branches
    (the seek kept, `captureProg` gone). Test fixes for the combination
    (`air_seek_attack_test` lost `canCapture`, `build_controls`' flyer
    expectation, the `classic_ai_duels` cap; below) and the mixed2 retake.
    The rest of the conflicts were in `CHANGELOG.md` and the notes (adjacent
    sections), resolved keeping both sides.

`git diff --stat v0.9.11 95ca9163`: 142 files changed, 8196 insertions(+),
3917 deletions(-); `-- src`: 38 files changed, 3306 insertions(+), 1137
deletions(-) (`src/sim` 7 files, `src/client` 22, `src/ai` 2, `src/net` 1,
`src/server` 3, `src/video` 3; the rest is tests, test baselines, CI
workflows, `CMakeLists.txt`, `CHANGELOG.md`, the documentation and four guide
pictures). 43 commits (34 without the merges).

Final `git diff --stat v0.9.11 v0.9.12`: 189 files changed, 20444 insertions(+),
4984 deletions(-); `-- src`: 52 files changed, 6666 insertions(+), 1939
deletions(-). 114 commits (88 without the merges) since `v0.9.11`.

On top of the preparation commit `3e615f95` (the version, CHANGELOG, README,
notes, report and title picture) landed 70 commits (53 without the 17
merges), in these groups:

- Review and fog figure: the release review's suite and gate results and
  wording fixes (`35c784b8`), the fog cost from the review's re-measurement
  (`748a7eda`), and the shot-range review results and the review's fourth
  gate run (`7c15df6b`, `f75dfa63`).
- Windows and macOS CI fixes: the fog test's `near`/`far` locals, which
  `windows.h` defines as macros (`94459ddb`), and the slow-upload proxy in
  `streaming_rtmps` paced against the clock (`89035d8e`).
- Shot range (Death Totem): no structure pad for a shot that dies at its range
  (`cab59085`, `3d252db3`), for any weapon but melee (`d8b58304`,
  `24eaf20f`), and a long building's body is its footprint (`337c4159`).
- Attack-sweep fixes: an attack after a walk aims two ticks after the stop
  (`8e4625fa`), `TurnDirection` reaches the unit's own script (`1f83633d`,
  `a0cd32eb`), `Attack_Chase` notes and emulation (`d66d02ec`, `a588bc72`),
  `Order` kept at five cache lines (`7c31c766`), the `classic_ai_test` fixes
  (`601e5e62`, `b7e274ba`) and the baseline retake and docs (`eb222a4f`,
  `af93b6f1`).
- Juggernaut slot modes (the air missions): AirToAir stage 1 (`c2554047`),
  AirToGround stages 1 and 2 (`777a5751`), dogfighting (`0a4ae3ad`), Dropped
  bombs and hover attackers (`b7f8111e`), the led SweetSpot, docs and
  CHANGELOG (`8f6f3737`, `64fcfa3e`) and the test link fix (`1d9d7834`).
- Mods in the repo: the ManaPost and CUTbugfix community packs kept in the
  repo, not shipped (`6dbdf9d2`).
- Retail-check fixes: lodestone S yards over scenery, flyers over fx and no
  target for a site (`1d92df15`, `bf4d7b1e`), the Harpy charms only with its
  spell (`96e6d335`, `ed38a8f8`), raising the dead by order (`3da26b17`,
  `c795530f`, `4ed7ba16`), Remote Effect spell timing (`10ab21bf`,
  `4a49e9f5`), flyers circle a lost target (`5c422a3b`) and keep retail's
  group spread (`098da05b`, `f5226ba8`), the Classic AI seek (`c4480aee`), the
  campaign mission-order parser (`a4abcf05`, `e297adbc`, `1870496b`), ground
  help and repair walking up to touch (`d165c943`, `414554f5`), and the
  integration and docs (`2246f550`, `9c2465c1`).
- Scenario retake: mixed2 for the flyer group spread (`7638e3cb`), the 22
  fixtures without a ctest (`9f796e34`), and the retake review's wall-touch
  gate median and final-code speed line (`39dd049f`).
- macOS runner: arm64 built on `macos-15` (`a6e56964`).

The code of the release is `39dd049f`; `a6e56964` differs from it only in the
macOS workflow.

The preparation commit on top changes no code: `CMakeLists.txt` (the version
line), `CHANGELOG.md`, `README.md`, the version and protocol lines in `docs/`,
the notes, this report, `docs/img/README.md` and `docs/img/title.jpg`.

**Compatibility.** `kNetVersion` is **305** in `src/net/protocol.h` (304 at
`v0.9.11`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** is unchanged. The gameplay data hash of the
test install is `d6000f4fa9095a51`, as for 0.9.11 (server logs of the
measurements below).

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared. So **0.9.12 plays
  with no 0.9.11 or older client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.12 refuses every
  0.9.11 (protocol 304) replay. `tools/replay_test.cpp` pins 305 and checks
  that 304 is refused.
- **Hashed state.** The state hash's new mixing changes every hash value
  (and so every gate) without changing the simulation. Our AI levels' new
  knowledge, targets and scouting change their commands, which moved the
  Legion 300 s and both Crusades gates beyond the layout (below). The
  lodestone search, nav split, sacred-site and passage changes are
  hash-neutral. The fog display, the streaming cap, the autosave and the BACK
  buttons are client-only and hash nothing. The combat fixes change the
  simulation (and add tagged hash folds for the new attack and AirToAir
  state), which moved all seven gates again.
- **Maps, mods and settings.** No map, recipe, package or settings format
  changed; a streaming bitrate above YouTube's figure for the stream is
  lowered and saved.

## Local preparation checks

All ran on Linux. Every takclient and takserver run used scratch `HOME`,
`XDG_DATA_HOME` and `XDG_CONFIG_HOME`.

- **Builds.** At `95ca9163` the binaries print `0.9.11 (build ...)`, because
  the version number changes in the preparation commit; the prepared tree's
  own `takclient --version` and `takserver --version` print
  `0.9.12 (build v0.9.11-43-g95ca91637cef-dirty)` from a Release build
  (`build-0912-rel`; the tree was dirty only with the preparation edits), and
  the title picture's Debug `-O2` build reads VERSION 0.9.12.
- **New tests.** `state_hash` (pinned fold values, serial equals pooled; added
  to the Linux, Windows and macOS CI workflows), `options_autosave` (22 of 38
  checks fail on the old code), `stream_bitrate`, the `retail_fog` harness
  test, the throttled-upload run in `streaming_rtmps`; new cases in
  `ai_behavior_test` (the nearest-first tie, fog knowledge, unseen
  attackers, scouting), `navincr_test` (open and sealed corridor and tee plugs;
  each split path proven to fire), `room_worker_test` (the hash skip; 9
  failures on the old room tick) and `fogupload_test`.
- **Quick suite** (`ctest -L quick`, during the combine `3ce75ac7`): 520 of
  521 passed; `vashim_unavailable` is the known local failure (a render node
  without libva).
- **Shot range (`task-los-range`).** New `shot_range` / `legion_shot_range`
  (`tools/shot_range_test.cpp`): on `94459ddb` (with only the new
  `Weapon` helpers added, so the test builds) 5 of 11 checks fail in each
  mode: the Death Totem holds the lodestone 584.5 px off for all 900 ticks,
  with bolts in the air on 266 ticks and no hit; a Crossbowman, Shaman and
  Fire Demon ordered onto a 7x12 Barracks stop 119-120 px past their range
  with no hit, and a Fire Wagon 113 px past it (its flame's splash still
  reached). With the fix all pass: they stand at the range itself (407-431,
  338 and 899 px) and hit. The Drake (a flyer, which closes in anyway), the
  Archer and the Swordsman read the same distances and hits before and
  after. `structure_combat_reach` now places
  its target 20 px inside the range for a tower whose shot dies at its range
  (Prismatic Mirror, Stronghold; it sat in the old pad). Related tests
  (`fire_sweep*`, combat, `blocked_shot`, `ballistic_line`, `factory_death`,
  `ai_behavior*`, `classic_ai*`, `retail_ai`, Crusades: 77) pass; quick suite
  528 of 529 (`vashim_unavailable`, the known local failure).
  User replay (`totem-0912.takrep`, the replay tracer on the fixed sim): it
  departs from the recording at tick 27,390, as expected; at 21:00 (tick
  37,800) the totem (id 190, complete) and the lodestone (id 158) stand
  584.493 px apart as recorded, and the totem has no order and fired no bolt
  from tick 35,000 to 38,500 (the recording: on target, bolts every reload).
  Review (scratch harness, not in the suite): every mobile unit type whose
  reach weapon's shot ends at its range, both rosters, ordered onto seven
  buildings (a 2x2 lodestone up to the 8x20 Cabal and 10x14 Temple) from two
  sides, 80 s each, on `94459ddb` and on the fix, Retail and Legion: none
  stands past its range; 268 Retail cases hit only with the fix, and the
  shortest such ranges (Fallen Angel 140, Fire Mage 200) still reach the
  biggest buildings' centres. Lost: the Fallen Angel (a flyer) on a lodestone
  (1-3 hits before, none now), and Kirenna on the Temple from one side (18
  hits before; she fires after a walk only by her script's timing, and on
  neither build does she fire after walking to any other target: see the
  notes' known limits). The Fire Mage's flame missed a lodestone on both
  builds. Naval units were not covered (dry map).
- **Range by weapon class (`task-range-classes`).**
  `tools/re/check_inrange_classes.py` calls each of the 17 handler vtables'
  +0xc natively (238 answers, range 300, a 7x12 target at 299-420 px along x
  and on the diagonal): 16 classes are in range exactly at centre distance
  <= 300 (`530580`; Ballistic `52bc60` with an arc that reaches; Dropped
  `532290`, a bare call of `530580`), none inside the old 120 px pad; melee
  (`52b980`) answers by its boxes. `shot_range_test` now has a case per
  class (31 checks): on `259d26a5` with only the new helper, 13 fail in each
  mode (the class survey in both rosters; a Lokken's Fire Wave, Fire Mage's
  Fire Storm, Acolyte's Earthquake and Weather Witch's Tornado on Hold
  Position target and damage a Barracks half the old pad past their range;
  ordered onto it, those four, a Mechanic's Freeze Beam and a Basilisk's
  Turn To Stone stop 119-120 px past their range, and a Lich, range 60,
  damages it from 179 px). With the fix all pass in both modes: they stand
  at 119-446 px, within their range, and hit. An Iron Beak (a flyer) hits on
  both builds. `structure_combat_reach` adds a Fire Mage (Fire Storm) and
  an Acolyte (Earthquake) on Hold Position: pass. Quick suite 527 of 529:
  `vashim_unavailable` (the known local failure) and `streaming_pacing`
  (frames dropped encoding 7680x2160 at a load average of 20-25 from other
  work on the machine; no video code changed). Attack sweep (`task-attack-sweep`
  harness on the fix, the two building targets, both rosters, 5,354 cases,
  against main `259d26a5`'s run): futile cases 50 -> 50; cases that fired
  from beyond every one of the attacker's weapon ranges 98 -> 0 (90 on the
  weapons this change moves, 8 on other weapons of the same units); every
  ordered attack that damaged its building still does (1,208 orders from
  100% and far: 1,141 -> 1,141). 226 cases changed: band cases on Hold
  Position that fired 141 -> 125, Dirigibles and Juggernauts now drop their
  bombs from within 200/500 px, and a Weather Witch or Sea Dragon on Hold
  Position no longer engages a building past the range of the weapon that
  set it off (the Tornado's 150, the Tsunami's 450), as it already did not
  engage a unit there.
- **Attack timing (`task-vermage-aim`).** `tools/re/probe_attack_chase_stop.py`
  runs the original's weapon update, `vermage.cob` in its own script VM, the
  mission dispatcher with `Attack_Chase` and the mover: every engagement
  stops on tick A, gives the slot its target on A+2 and runs AimWeapon on
  A+3 (also with the brake rate cut so she slides 44 ticks: A is the first
  tick in reach), and Kirenna fires only when her walk script has taken
  `restore_x` by then; the original fires in 36 of a 144-case grid with its
  arrival forced in range. New `attack_stop` / `legion_attack_stop`: on
  `259d26a5` 5 checks fail (Kirenna and the Archer aim on the stop tick,
  Kirenna never fires, the in-reach pivot starts on the order tick, the
  script never hears TurnDirection); with the fix all pass, and Kirenna
  ordered from 664 px stops at tick 180, aims at 182 and fires at 221 from
  399 px. Her own grid: fires in 58 of 144 (12 on `259d26a5`).
  `flyer_combat_test` skips the held ticks in its facing check;
  `classic_ai_test` U13 no longer fails when the whole column dies (main
  loses it in 8 of 12 start shifts) and the raid check asks for 2 soldiers
  sent, not 4 (an instrumented AI shows the original's 49-in-50 keep rule
  holds 4 of 6 defenders; main sends 2-5 as the raiders shift 0-24 px);
  `tools/legion_cost_baseline.json` battle-field-2x60 mopup
  `pass_scan_cells.total` 2,616 -> 3,402 retaken (reason recorded). Review:
  the addresses re-read, the probe rerun, the test shown to fail on
  `259d26a5`; one stale comment fixed.
- **Blocked shots at long buildings (`task-impact-footprint`).**
  `tools/re/probe_long_footprint_blocked.py` runs the original's launch,
  ballistic update, projectile collision and cell lookup (`0x52bdf0`,
  `0x52be80`, `0x52bf90`, `0x52a4d0`, `0x50e660`, `0x51f340`) at a 4x12-cell
  body 128 px away: open ground hits it at x=497.1; a ridge two or one cells
  before its near face stops the shot on terrain with no unit 75 and 40 px
  from its centre; a ridge under its own near column still hits. New castle
  case in `blocked_shot` / `legion_blocked_shot` (a Mage Archer at a Taros
  Castle's long face behind a ridge): Hold Position fires 11 short shots
  without moving; Manoeuvre walks 41 px (Legion 48) and hits 7 times for
  3,083. On `259d26a5` the Manoeuvre archer fires 11 shots into the crest and
  never moves; with either half of the fix reverted the check fails too
  (review). A Taros-gate variant of the sweep (narrow end on) changed 0 of
  1,974 cases.
- **AirToAir (`task-airtoair`).** `tools/re/check_air_to_air.py` compares the
  port with the original code: the facing test `0x41d780` and the moving
  controller (20,000 cases each), AirToAir `0x41ca50` and VTOL_Evade
  `0x41e3d0` (12,000 each), the Dropped aim and gate `0x52fe30` / `0x52fff0`
  (6,000): all equal, and twelve deliberately broken copies of the port all
  fail. New `air_to_air` (quick, 24 checks in both balances): all 19 armed
  flyers dogfight a flyer, Juggernaut arrows hit 27-31 times a minute (0
  before), Beak eggs hit a holding flyer below (14 of 14 on a Fallen Angel)
  and pass under a Roc at its own height. With AirToAir switched off in
  `tickCombat` it reports 9 failures (review). `retailgap` passes.
- **Attack sweep on the integrated code** (`task-attack-sweep` harness built
  against `7c31c766`, `--data` the test install, both rosters, 19,501 cases,
  `~/tak-tmp/attack-sweep/four-merged/`, against main `259d26a5`'s run;
  rows matched one to one): futile cases (4+ shots, no damage) **1,110 ->
  1,091**, the sum of the four branches' own changes (attack timing -4, AirToAir
  -13, long buildings -2, range 0). 1,802 rows changed; damage up in 322,
  down in 345.
  - Kirenna's orders fire in 126 of 156 (102), from far 44 of 66 (27), after
    walking more than 40 px (subs excluded) 26 of 52 (9).
  - Buildings (castle and lodestone, 4,844 cases with a ranged weapon):
    cases firing from past every one of the attacker's weapon ranges 98 -> 0;
    Hold Position band cases that fired 141 -> 125; ordered attacks from 100%
    and far that damaged their building 1,141 -> 1,141 of 1,208.
  - Castle: futile 21 -> 19 (the Taros Beak's two Hold Position cases at
    100%); the G5 cliff targets (square) unchanged in futile cases (72, 26).
  - Flyers against flyers (720 cases): futile 51 -> 38, shots 3,129 -> 2,830,
    hits 1,740 -> 1,798, damage +3.6%; Aerial Juggernaut futile 13 -> 0
    (hits 81 -> 120), Iron Beak 20 -> 18, Dirigible unchanged at 0. The three
    Dropped-bomb units on every target: futile 49 -> 32, shots 3,740 ->
    3,321.
  - 35 cases lost all their damage and 46 gained some. Lost: 32 Hold
    Position cases at a building in the old pad band (Acolyte 16, Weather
    Witch 11, Sea Dragon 5; the range change, as in the original) and three
    single-shot cases at the circling flyer (a Stone Giant on fire at will,
    a Centaur and a Weather Witch ordered from far). Gained: Kirenna 24,
    Aerial Juggernaut 13, others 9.
- **Full suite on `95ca9163`** (Debug `-O2`, all targets built, `ctest -j12`,
  scratch `HOME` and XDG folders, test data `assets/game`, which has no mods):
  789 of 791 passed, one skipped (`vashim_system_loader`, which skips itself).
  One failed: `vashim_unavailable` ("no display on a render node without
  libva"), the known local failure; CI builds FFmpeg with the shim and runs it
  there. Wall time 1,343 s.
- **Full suite on the integrated combat fixes** (`7c31c766`, Debug `-O2`, all
  targets built, `ctest -j12`, scratch `HOME`, XDG folders and `TMPDIR`, test
  data `assets/game`): 778 of 796 passed, one skipped
  (`vashim_system_loader`, which skips itself), 17 failed: `vashim_unavailable`
  (the known local failure, as above) and 16 `legion_check_*` scenario gates
  (below), which all pass after the baseline retake (rerun: 20 of 20, with
  the reasons and baseline checks). Wall time 1,309 s. The four branches'
  own tests (`attack_stop`, `legion_attack_stop`, `shot_range`,
  `legion_shot_range`, `structure_combat`, `structure_combat_reach`,
  `blocked_shot`, `legion_blocked_shot`, `air_to_air`, `flyer_combat`, the
  `classic_ai_*` tests, `retailgap`) pass on the merged code.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): golden
  `dcef618cd2e4d558` from g++ and clang++ at -O0, -O2 and -O3, unchanged from
  0.9.11. The aarch64 leg was skipped because no target libc or headers are
  installed here. Rerun on `task-los-range` and on the integrated combat
  fixes (`7c31c766`; `src/sim/detmath*` untouched): the same golden.
- **Gates on the final code** (`95ca9163`, the set-up below, ports
  24301-24307): all seven hashes equal the table below in three runs. All
  seven servers loaded retail gameplay hash `d6000f4fa9095a51` in every run,
  and no desync, mismatch or referee-suspect line appeared in any client or
  server log of the three runs. On `task-los-range` (ports 24311-24317,
  `~/tak-tmp/gates-los/`), four runs (the third on the final, comment-only
  tidied build, the fourth by the review on the same build): all seven
  hashes unchanged from `95ca9163`'s and identical in every run; no desync,
  mismatch or referee-suspect line. On the integrated combat fixes
  (`7c31c766`, ports 24451-24457, `~/tak-tmp/gates-0912-four/`), two runs:
  all seven hashes are the table below and identical in both runs (wall 42
  and 43 s), every server loaded `d6000f4fa9095a51`, and no desync, mismatch
  or referee-suspect line appeared in any client or server log.
  With `task-jugg-cannons` merged (ports 24611-24617,
  `~/tak-tmp/gates-0912-jugg/`), two runs: all seven hashes unchanged and
  identical in both runs (wall 42 s each), every server loaded
  `d6000f4fa9095a51`, no desync, mismatch or referee-suspect line
  (`mixedSlots` is folded only when set, and no gate's game reaches it).
  With the retail-check branches merged (`7638e3cb`'s sim, ports
  24711-24717, `~/tak-tmp/gates-0912-b/`), two runs: six hashes unchanged,
  Crusades Retail 300 s `01c551c99113dbb9` -> `def92d0b7d4a798e`, identical
  in both runs; every server loaded `d6000f4fa9095a51`; no
  desync, mismatch or referee-suspect line. The Crusades Retail move comes
  from the raise order alone (that branch's build gives `def92d0b7d4a798e`,
  the other seven branches' builds `01c551c99113dbb9`): bodies are no
  longer cleared by idle builders and our AI orders its casters' raises.
- **On the merged retail-check branches** (`7638e3cb`; Debug `-O2` and
  Release, all targets built; scratch `HOME`, XDG folders and `TMPDIR`; test
  data `assets/game`):
  - **Detmath and golden:** `check-detmath.sh` OK; `check-determinism.sh`
    golden `dcef618cd2e4d558` unchanged (g++ and clang++, -O0/-O2/-O3;
    aarch64 skipped, no target libc), `src/sim/detmath*` untouched.
  - **Full suite** (`ctest -j12`, 803 tests, wall 1,297 s): 799 passed,
    `vashim_system_loader` skipped, 3 failed. `vashim_unavailable` is the
    known local failure. `build_controls` (not labelled quick, so no branch
    ran it): its mixed builder-and-flyer right-click expected the old
    +-60 clamp (2340); the flyer now keeps its unclamped place, offset -75
    from the centre within sqrt(3000 x 2), 2325 (expectation updated).
    `classic_ai_duels`: Aramon v Veruna on the default seed ended with no
    winner within 90 game-minutes (an even war, both sides launching waves to
    the end; Aramon wins at 91.9). Each branch alone passes it. Over eight
    seeds (`TAK_DUEL_PAIRS`): main 8 of 8 end, mean 49.9 minutes, Aramon
    5 / Veruna 3; merged 7 of 8 within 90, mean 31.9, Aramon all 7. The
    shift comes from the help/repair approach alone (that branch: Aramon
    wins all 8, mean 35.4); the spell timing and small fixes move single
    seeds. The cap is now 120 minutes (`2246f550`), with the reason in the
    test; both tests pass on a rerun. **Accepted by the owner:** the cap
    stays at 120 minutes. The Retail AI's Aramon got stronger because its
    builders now walk up to what they repair and keep trying, as the
    original's do, so its repairs land.
  - **Branch tests on the merged code:** `mission_orders`,
    `mission_orders_shipped`, `ambush`, `revive_order`, `capture_combat*`,
    `air_seek_attack`, `helpbuild`, `remote_timing`, `sacred_yard`,
    `fire_sweep_retaliation`, `issue_selection*`, `selection_order`,
    `convoy`, `production*`, `ai_behavior*`, `featgen`,
    `builder_automation`, `retail_script`, `unit_retirement`,
    `flyer_combat`, `air_to_air`, the campaign and transport tests,
    `doorway`, `legion_patrolrepair`, `retailgap`: 46 of 46 pass.
  - **Scenario baseline:** all 67 `legion_check_*` tests and
    `navigation_determinism` pass unchanged. All 145 scenarios at the gate
    offsets (both modes; `--data` for the 11 that need it): only **mixed2**
    moved (the flyer group spread): its five Retail `hash@` keys retaken and
    one licence, Legion `work.completion_dist_3.total` 35 -> 55 (54 on main
    at offset 0); Legion outcome keys within a few ticks (ground group done
    6,621 -> 6,711). In Retail mode its ground group no longer completes
    (48 -> 46 arrived, done 8,342 -> never; Retail keys there are the floor
    reference, not gated): the spread flyers land among the arriving ground.
    `check` on mixed2 and `validate` PASS. **Not retaken:** 22 fixtures
    with no `legion_check` ctest (`attackring-deadcorridor-30`,
    `-fight-diag-60`, `-gatehouse-40`, `-gatehouse2-48`, `-gateyard-36`,
    `-melee-30`, `-wall-mixed-56`, `battle-assault`, `battle-field-2x60`,
    `-2x250`, `-2x500`, `gen1-route-*` x7, `motion-cross`, `-open`,
    `-wall`, `ringbend-pocket`) fail 296 keys on main `8d671ee0` already
    (`hash@` keys, and Legion work counters and contact keys in the four
    battles, a Retail offset spread in motion-open and a Retail-floor key in
    motion-cross), with results identical before and after these merges:
    the combat fixes' and air missions' changes, which `eb222a4f` retook only
    for the fixtures with a ctest. Retaken afterwards on `cc71d630` (see
    **Scenario retake for the fixtures without a ctest** below).
  - **Campaign smoke** (takserver with the referee and Retail AI, headless
    client, `--mpmission`, `TAK_SPEED=80`): takx04_ph, takx07_ph and
    takmission23_ph 120 s, takx26_dh 60 s: no crash, no desync, mismatch or
    referee-suspect line; takx04_ph, takx07_ph and takmission23_ph give the
    same hashes as the mission branch alone (`09c1a8465de44dd1`,
    `d65dcd362c9a17f8`, `97302177f370ac1d`); takx04_ph ends in defeat at
    75 s with nobody playing, as on the mission branch.
- **Detmath guard** (`tools/check-detmath.sh`, on the prepared tree, on
  `7c31c766` and with `task-jugg-cannons` merged): OK.
- **With `task-jugg-cannons` merged** (Debug `-O2` and Release, all targets
  built; scratch `HOME`, XDG folders and `TMPDIR`): golden
  `dcef618cd2e4d558` unchanged (g++ and clang++, -O0/-O2/-O3); quick suite
  526 of 528 passed (`cobanim` and `gaitpace_shipped` on a rerun once the
  worktree had the extracted assets), `vashim_system_loader` skipped,
  `vashim_unavailable` the known local failure; `air_to_air` and `flyer_combat` pass; all 67
  `legion_check_*` scenario gates and `navigation_determinism` pass against
  the unchanged `baseline.json` (nothing retaken).
- **Scenario baseline on the integrated combat fixes** (`7c31c766`). The
  four branches ran the quick suite only, which leaves out the nightly
  `legion_check_*` gates; on the merged code 16 of the 67 failed: 352
  `hash@` keys of 16 attack and chase fixtures (`attackring-*`, `chase-1`,
  `chase-50`, `chase-line`, `fight-retarget`, `ringcross`; the attack-timing
  fold and the centre-to-centre building reach), and 39 band keys of two
  fixtures. **attackring-struct-60** (60 gunners, range 96, at a 4x4 house)
  was built around the old structure allowance (reach 152 px): with the
  original's centre-to-centre reach (`task-range-classes`) Retail still gets
  every attacker a shot but no longer kills the house by tick 3,000 (damage
  30,000 -> 25,338, so the group never completes), and Legion gets 60 of 60
  a shot (42 before) and more damage (23,846 -> 27,906) with more shuffling
  in the tighter ring (sideways 36 -> 2,848, backward 0 -> 976, reversals
  0 -> 79, churn, held rechecks); measured on `task-range-classes` alone,
  the same 39 keys fail. **fight-retarget** (Legion): reach slots built
  24 -> 28 and follow chain mean 4.35 -> 5.35 from the attack-timing hold
  (the same on `task-vermage-aim` alone), outcome keys unchanged. Retake
  (`legion_check.py baseline --hashes --keys 'hash@*'` over the 16 fixtures,
  `--mode both --offsets gate`): only those 352 `hash@` values changed;
  new band entries for `work.ring_swaps.max/total` (attackring-struct-60)
  and `work.field_work_refresh_moving.max/p99/total` (fight-retarget), which
  first move there; 39 `accepted_regressions` with the measured values and
  the reasons above; a history line. After: `check` and `check --cumulative`
  **PASS** on the 16 fixtures (4,894 keys, 0 failed, 541 ratchets, 720
  licensed), `validate` **PASS** (0 problems). `navigation_determinism`
  passes unchanged (no checkpoint retaken). The Legion shuffling in
  attackring-struct-60 is licensed as the cost of the original's reach, not
  fixed; a later Legion round could settle the tighter ring.
- **Scenario retake for the fixtures without a ctest** (main `cc71d630`,
  Debug `-O2 -g`, `legion_scenario --mode both --json --offsets gate --data
  assets/game`, scratch `HOME`, XDG folders and `TMPDIR`; data in
  `~/tak-tmp/scnretake/`). The 22 fixtures the retail-check integration left
  (above) fail the same 296 keys on `cc71d630` as on `8d671ee0`: 246
  `hash@` keys and 50 measured keys. **Attribution:** the 22 run on
  `259d26a5` (before the combat fixes) PASS against the old baseline (0
  failed); on the three other combat branch heads (`task-range-classes`
  `24eaf20f`, `task-impact-footprint` `337c4159`, `task-airtoair`
  `1d9d7834`) they PASS unchanged; on `task-vermage-aim` alone (`a0cd32eb`)
  the same 296 keys fail, with results byte-identical to `b8d4be5a`,
  `8d671ee0` and `cc71d630` (so neither the other combat fixes nor the air
  missions' slot modes nor the retail-check branches touch them). Within
  that branch (`601e5e62`, before its TurnDirection commit):
  - **The attack hold after a walk** (`8e4625fa`) moves
    attackring-deadcorridor-30, -fight-diag-60, -gatehouse-40,
    -gatehouse2-48, -gateyard-36, -wall-mixed-56, ringbend-pocket and the
    four battles: a ground attacker that walked to its target now brakes
    and stands two ticks before its weapons take the target and turns to
    face only after that, as the original does, so every engagement after
    a walk starts up to three ticks later and each fight takes another
    course. The attackring fixtures and ringbend-pocket move only in
    `hash@` (ring damage within a few points). In the battles (Legion,
    means over all 11 gate offsets, `259d26a5` -> `cc71d630`; the gate
    reads the median of the core five, which moved more, and for the
    small-count wall-touch keys the median of all 11):
    **battle-assault** contact with other groups 417 -> 430 permille, held
    rechecks 433k -> 445k, still units processed 16.6k -> 17.0k (the
    braked bodies), blocked re-requests 0.098 -> 0.106 per unit, wall
    touch 26.5 both (median of the 11: 26 -> 27, near the wall 107 ->
    111), field refresh 29.9M -> 29.7M; **battle-field-2x60**
    reversals 106 -> 109 (core five 103 -> 129: the offsets that reverse
    more moved into the core), passage scans 1,815 -> 2,161 and their
    cells 78.8k -> 91.7k (bodies braked at the front are scanned again),
    reach slots 6.86 -> 6.59 per unit (core median 6.04 -> 6.83);
    **battle-field-2x250** the even fight now tips toward side B (dead A
    225 -> 238, B 238 -> 219 of 250; Retail likewise), and B's survivors
    finish their fight move, which the arrival and completion counters
    count (arrivals 24.5 -> 28.6, core median 25 -> 39; completion
    distance sum 268 -> 397); **battle-field-2x500** fewer deaths inside
    the run and more survivors at their goal (core median arrived A 41 ->
    53, B 14 -> 30), still units processed 6,213 -> 6,956 (+12%, the held
    bodies), reach slots 7.10 -> 8.00 per unit, ring and slot-search cells
    and holds +4%.
  - **TurnDirection to the unit's own script** (`1f83633d`) moves
    attackring-melee-30 (a melee attacker is not held), the seven
    gen1-route fixtures (`hash@` only, both modes) and motion-cross, -open
    and -wall (Legion `hash@` only; Retail takes another course). The
    turn call-in runs the unit's gameplay script at once, so a script's
    RAND (crechie's ChiefDancer in the motion fixtures) draws the shared
    game random a tick earlier or at another point in the tick; Retail's
    mover and missions draw the same random, so their sleeps and routes
    change (motion-cross offset 0: the draw order differs from tick 3, the
    first unit's route at tick 387, with every position equal until then).
    motion-cross's Retail `click.u00.arrived` median 16 -> 18 (Legion 16
    unchanged) now fails the Retail floor, and motion-open's Retail
    `click.u00.t50` offset spread 2,300..2,534 is just over the band
    (median 2,470, the base).

  Nothing looks like a bug: both changes are the original's behaviour
  (emulated, reviewed on their branch), the battles' outcome keys move both
  ways, and the work counters move 0-15% over all offsets. **Retake:**
  the 246 `hash@` values (nothing else changed by the retake); 43
  `accepted_regressions` for the battles' Legion band keys with the measured
  values and the reasons above; five raised bounds (battle-assault
  `wall_touch_near_permille` 108 -> 111, `wall_touch_permille` 26 -> 27,
  `work.blocked_rerequests.total` 0.0937 -> 0.1046 per unit;
  `work.reach_slots_built.total` battle-field-2x500 7.169 -> 7.718,
  battle-field-2x60 6.6417 -> 6.8334 per unit); a Retail offset-spread
  exception (motion-open `click.u00.t50`) and a Retail-floor exception
  (motion-cross `click.u00.arrived`, Retail moved, Legion did not), cluster
  MV-12; a history line. After: `check` and `check --cumulative` **PASS** on
  the 22 (5,721 keys, 0 failed, 504 ratchets, 678 licensed, 33 floor
  exceptions), `validate` **PASS** (0 problems).
- **Scenario baseline and navigation checkpoints on `task-los-range`:** all
  67 `legion_check_*` tests (every key, `hash@` included, against
  `baseline.json`) and `navigation_determinism` pass unchanged; nothing was
  retaken.
- **Scenario baseline** (`tools/scenarios/baseline.json`, at the combine): all
  145 scenarios in both modes at the gate offsets; only the values of the
  2,193 existing `hash@` keys retaken (1,229 Retail, 964 Legion: every one
  moved with the hash layout); `check`: 36,566 keys, 0 failed; `validate`: 0
  problems.
- **Navigation checkpoints** (`tools/navigation_checkpoints.json`, at the
  combine): all 48 retaken (both modes; serial equals workers).
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `--crusades`
  for the Crusades games, `TAK_SPEED=80`), on the integrated combat fixes
  `7c31c766`, two runs (`~/tak-tmp/gates-0912-four/`), unchanged with
  `task-jugg-cannons` merged (two runs, `~/tak-tmp/gates-0912-jugg/`), and
  with the retail-check branches merged (two runs, `~/tak-tmp/gates-0912-b/`;
  only Crusades Retail moved).
  "Before the combat fixes" is main `95ca9163` (three runs, `~/tak-tmp/gates-0912-suite/`, and
  the retail fog review's runs; unchanged by the shot-range fix); "0.9.11" is
  `v0.9.11`:

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | **`8690eaee7b16b4e6`** (before the combat fixes `779b760554ee2b85`; 0.9.11 `d0da4ccc71d2067c`) | **`d028e465c7676e68`** (`83b33de237aaa0a7`; `7a0be4440cd154b7`) |
| Legion | **`b558575535124afc`** (`95f426566c4440da`; `7a403f097f3506b3`) | **`eab501625cd4381a`** (`e1161d1937792d5b`; `dc2652648568ad54`) |
| Retail AI (`TAK_AI_LEVEL=6`) | **`12003fba552b8696`** (`468f6d3dbbf68923`; `4952493f51b9a568`) | not run |
| Crusades, Retail pathfinding | not run | **`def92d0b7d4a798e`** (before the retail-check branches `01c551c99113dbb9`; before the combat fixes `eb3107aa3d2bbb93`; 0.9.11 `3e8c0ad3b4f1b859`) |
| Crusades, Legion | not run | **`fb33fb2edffd997e`** (`c58249d598fd55af`; `b0c9748d62aafeb6`) |

**All seven changed, as expected.** Against 0.9.11, Retail 60 s, Retail
300 s, Legion 60 s and the Retail AI first changed by the hash layout alone
(equal to `task-perf-hash`'s own runs at the combine). Legion 300 s and the
two Crusades games changed with the AI as well: at the combine they read
`4ceb517086c9aa75`, `56aae72cb72e8880` and `545ca39f878ff37a`, and moved
again with the AI commits after the combine (`7c2e3fc8` left all seven
unchanged; the tick-0 wait `eec342d6` too). The fog display and the shot-range
fix changed none of them. The combat fixes then moved all seven: every gate
has walkers that attack (the new attack-timing fold and TurnDirection in the
hashed script VM) and flyers or buildings under fire. Of the retail-check
branches only the raise order moved a gate (Crusades Retail 300 s); the
others leave these small Inner Circle games (4 units at 60 s, 18-19 at
300 s) unchanged.

## The reported game: late-game speed

The user's case, replayed as a measurement: 8 Absurd AIs (sides 1, 3, 3, 0,
0, 2, 4, 1) on the 64x64 random map
`~gen1~09000100000000000000030008000808ffffff00ff0004`, Legion, Crusades,
random starts, Monarch expendable, double sight, power codes,
`takserver --seed 1754662010`, game speed +10 (`TAK_SPEED=20`), 30 game
minutes, watched by one headless spectating client. Debug `-O2` builds with
the same uncommitted timing hooks (server tick and pacing logs): 0.9.11 is
`v0.9.11`, 0.9.12 is `4b374c14` (before the fog display, which is client-only,
and the vacated-base rule). Intel Core Ultra 9 275HX (24 threads), machine
otherwise quiet (load average about 2.5). Data in
`~/tak-tmp/perf-0912-final/` (`r0911`, `f0912b`).

| Game minute | 0.9.11: units / sim ms / AI max ms / ticks > 100 ms / slowest shown | 0.9.12: units / sim ms / AI max ms / ticks > 100 ms / slowest shown |
|---|---|---|
| 10 | 872 / 3.73 / 302 / 11 / +7 | 689 / 2.45 / 3.0 / 0 / +10 |
| 15 | 1,830 / 9.85 / 358 / 26 / +6 | 1,455 / 5.63 / 2.9 / 0 / +10 |
| 20 | 2,546 / 14.02 / 659 / 58 / +0 | 2,255 / 9.42 / 2.9 / 0 / +10 |
| 24 | 2,661 / 15.67 / 745 / 55 / -2 | 2,917 / 12.80 / 4.7 / 0 / +9 |
| 29 | 2,621 / 13.91 / 344 / 56 / +1 | 3,162 / 13.36 / 4.8 / 0 / +9 |

(sim ms: the server's mean simulation time per tick in that minute; slowest
shown: the client's lowest SIM speed readout.) Whole game: ticks over 100 ms
861 -> 0, over 250 ms 219 -> 0; longest AI think after the first minute 929.4
-> 7.0 ms; the server's referee hash 24-36 ms per check in 0.9.11, not computed
in 0.9.12 (no human slot); wall time for the 30 game minutes (900 s at +10)
1,085 -> 909 s. The two games part after the opening (the AI changed), so the
unit counts differ.

A first 0.9.12 run of the same game (`f0912`) overlapped parallel compiles on
the machine (the load average climbed from about 2 to 17-29 in its second
half) and fell below +0 from minute 24 (-2 from minute 27); it is not
counted, and the repeat on a quiet machine is the run above.

**On the final code** (main `cc71d630`: the combat fixes, the air missions'
slot modes and the retail-check fixes on top of `4b374c14`; the same game,
hooks and machine, quiet; `cc71-ana.txt` against `f0912b-ana.txt`): the
whole 30-minute game at +10 in 909 s wall, as before; 0 seconds below 1x
(was 1, in the opening); simulation time per tick at equal unit counts
(500-unit bands) lower in five of the seven, for example 11.7 against 13.0
ms at 2,500 to 3,000 units, equal below 500 and higher at 1,000 to 1,500
(5.9 against 5.2 ms; one run each, so partly the game's course); AI cost up about 0.3 ms a tick (0.5 to 0.7 ms against 0.2 to 0.35);
the client's SIM readout never below +8, its lowest 1.80x near 3,100 units
(1.91x before).

Per-change costs from the commits: the lodestone search took 300-840 ms in
one tick on that map; a full nav relabel 90-150 ms, 16 to 24 times a minute
(164 of 200 in the user's replay no split at all); sacred sites 9-12% and
Legion passages 7-8% of a late tick; the state hash 33 ms per check at 2,500
units, about 5 ms after. The AI knowledge layer's think cost on the same kind
of game (8 Absurd, Legion, double sight, Crusades, 25 game minutes): 722 ms of
AI work per game minute against main's 715 at minute 24 (`6da1017f`).

## AI behaviour

All-AI games, `TAK_SPEED=80`, a 60-game-minute limit, `takserver --seed` 1 to 6
as listed, the room's default fog (EXPLORED; `TAK_FOG=0` for NOT EXPLORED),
Debug `-O2` builds with an uncommitted measuring patch that changes no
decision. "Sight only" is main `894d23d4` (sight-only targets, before the
knowledge and scouting branches); "0.9.12" is `4b374c14` (knowledge and
scouting merged; before the tick-0 wait under NOT EXPLORED and the
vacated-base rule). Data in `~/tak-tmp/aiv2/runs/` (`m-*` and `n-*`).

| Game (Inner Circle unless named) | Sight only | 0.9.12 |
|---|---|---|
| Absurd against Absurd, seeds 1-6 | all end, mean 21.2 min (29.6, 24.4, 18.6, 7.8, 24.8, 22.3) | all end, mean 17.7 (16.9, 9.8, 12.3, 25.9, 28.9, 12.6) |
| ... scouts lost | 320 | 112 |
| Hard against Normal, seeds 1-6 | all end, mean 14.0; Hard wins 5 | all end, mean 13.0; Hard wins 5 |
| ... scouts lost | 98 | 37 |
| Easy against Easy, seeds 1-6 | all end, mean 12.5 | all end, mean 15.0 |
| Defensive against Normal, seeds 1-3 | 2 of 3 end (12.6, 9.5), Defensive wins | 2 of 3 end (15.1, 19.3), Defensive wins |
| 8 Absurd, the reported 64x64 map, seeds 1-3 | 1 of 3 ends (56.8) | 2 of 3 end (31.5, 27.6) |
| ... scouts lost | 1,006 | 394 |
| Absurd against Absurd, NOT EXPLORED, seeds 1-3 | (ignores the room's fog) | all end, mean 21.6 (EXPLORED, same seeds: 13.0) |

From the commits (Absurd against Absurd, Inner Circle, seeds 1-6, mean
length): 0.9.11 27.2 min; the combine 18.4 (with the tick-0 leak that `7c2e3fc8`
closed); with the found-base start check 29.1, without it 21.3 (`894d23d4`,
which backed it out); scouting on its own branch 19.3 with 128 scouts lost
against main's 21.3 and 320 (`f75d0993`). In the 0.9.12 runs no strike was
ordered by id on a target only heard or not known (the measuring patch's
audit), and no lodestone was placed on ground the AI did not know.

Not measured: the vacated-base rule's effect on game length (`ae3da6c6`
reports that before it no vacated base on the 64x64 eight-Absurd map was ever
looked at again) and the tick-0 wait (`eec342d6`: player 0's first lodestone
order, about 68 s, or 4 minutes with random starts, before the fix; EXPLORED
and FULL VISION rooms and every gate unchanged).

## Fog display cost

The figure in the CHANGELOG and the notes is the fog review's own
re-measurement (`~/tak-tmp/scratch/retailfog-review/old.log` and `new.log`:
the 3,000-unit replay seated as player 0 at tick 48,640, offscreen OpenGL,
cores 4-15, static camera): fog draw 0.048 -> 0.025 ms a frame, minimap
0.117 -> 0.028 ms a frame, the old fog pass 0.065 ms a tick on the sim thread
plus 0.085 ms on its worker -> none, the new plane build 0.17 ms a tick inside
capture, whole capture 0.95 -> 0.77 ms a tick. An earlier summary of
"1.22 -> 0.37 ms a frame" from the fog work's builder could not be
reproduced from its logs and is not used.

Its profile logs (`~/tak-tmp/scratch/retailfog/perf/`, `TAK_PROFILE_FOG`
lines, every 10 s): the replay `game3k.takrep` seeked to tick 48,600, seated as
player 0 with a camera pan for 60 s and then spectating, 1920 x 1080
offscreen OpenGL; old is main before the fog change, new the fog branch.
Medians of the ten seated samples after the seek (about 3,280 units):

| | Old | New |
|---|---|---|
| Fog overlay draw, ms per frame | 0.410 | 0.024 |
| Minimap, ms per frame | 0.116 | 0.028 |
| Capture, ms per tick (new: of which the fog plane 0.171) | 0.881 | 0.654 |
| Display fog pass, ms per tick (sim thread + worker) | 0.064 + 0.060 | gone |

During the seek's catch-up the old pass cost 2.37 + 6.84 ms per tick. At about
8,600 units (the 10,000-unit shadow benchmark as a battle on Ulasem Arena):
overlay 0.445 -> 0.044 ms per frame, minimap 0.334 -> 0.353, the old worker
pass 1.67 ms per tick gone. `8d30ca45` later cut the minimap radar lookup
(about 0.3 ms a capture with a Zhon Roc army at 10,000 units).

## Screenshots

`docs/img/guide/campaign-briefing.jpg` and `docs/img/guide/disconnect-wait.jpg`
were retaken for the retail fog (`b7df5925`), and
`docs/img/guide/options-graphics.jpg` and `docs/img/guide/voice-chat.jpg`
for the autosave footer (`767e8d10`), before this commit. In the preparation,
`docs/img/title.jpg` was retaken by the prepared tree's build (Debug `-O2`,
CMake `VERSION 0.9.12`) with the 0.9.1 title capture (`takclient menu
--shot`, offscreen driver, 1920 x 1080 window cropped to the 1440 x 1080
artwork). It reads VERSION 0.9.12 and shows the three doors (looked at). The
AI, speed, streaming and BACK-button changes change no picture in the folder,
so none other was retaken or deleted. `docs/img/README.md` lists the details.

## Desync sweeps

No local or remote desync sweep (`tools/desync-hunt.sh`,
`tools/desync-hunt-remote.sh`) was run for 0.9.12, and no two-client game. The
new hash mixing is checked by `state_hash_test` (serial equals pooled) and the
gates (byte-identical bundles and matching `posDigest` against 0.9.11); the
AI changes run on the server only and reach the game as commands.

None is planned before the tag: by the project's testing rule a release runs
no remote sweep, and the tag CI determinism tests are the cross-platform check.

## CI and packages

All four tag workflows ran on `a6e56964` (tag `v0.9.12`), started 2026-10-30
18:41 UTC: [Linux packages
41108989726](https://github.com/pocketgeek/tak-engine/actions/runs/41108989726)
(18 jobs, all success on attempt 1, finished 19:06 UTC), [Windows
41108989757](https://github.com/pocketgeek/tak-engine/actions/runs/41108989757)
(4 jobs, attempt 1, finished 19:08 UTC), [macOS
41108989771](https://github.com/pocketgeek/tak-engine/actions/runs/41108989771)
(4 jobs, passed on attempt 2, finished 19:30 UTC) and [Determinism
41108988983](https://github.com/pocketgeek/tak-engine/actions/runs/41108988983)
(attempt 1, about 7 minutes): detmath, sim-equiv and unit-tests all
succeeded, and GCC and Clang x86-64 at -O0/-O2/-O3 all agree on golden
`dcef618cd2e4d558` ("DETERMINISM OK", unchanged). macOS attempt 1 built both
architectures but its arm64 job failed at the `hdiutil verify` step ("Resource
temporarily unavailable", five retries) after a full successful build, which
skipped Sign + notarize; the failed-job rerun passed, including signing and
notarisation.

All 29 assets were downloaded fresh and match GitHub's SHA-256 digests and
sizes. The file set is identical to 0.9.11's: the same 29 asset names, and the
file lists of all packages, installer, zips and debug archives match 0.9.11's
exactly (after replacing the version string). 26 assets grew, from +158,046
bytes (+0.59%, openSUSE Leap 16.0 aarch64 RPM) to +1,003,285 bytes (+0.81%,
Windows x64 debug zip, the largest absolute growth; Windows arm64 debug zip
+1.16% is the largest relative growth). Three assets shrank, all macOS arm64,
the first built on `macos-15`: the zip -452,803 bytes (-1.37%), the DMG
-278,671 (-1.53%) and the debug zip -787,476 (-2.84%). All 69 release
`takclient`, `takserver` and `cartographer` binaries (48 Linux ELF, 9 Windows
including the installer's three, 12 macOS in the zips and apps) carry the
`v0.9.12` build string and no older `v0.9.x` string. None of the 48 Linux ELF
binaries has a libva, libdrm, libsecret, GLib or libsqlite `NEEDED` entry, and
none contains `__vaDriverInit_`, `SQLite format` or `sqlite3_`. Each binary's
`NEEDED` set equals its 0.9.11 counterpart's.

`strings` (ASCII and UTF-16) on all 69 release executables finds the `TAK_`
names only as expected, identical to 0.9.11: the 23 `takserver` binaries hold
exactly their four timers (`TAK_TIMEOUT_MS`, `TAK_LOAD_TIMEOUT_MS`,
`TAK_STALL_MS`, `TAK_DROP_WAIT_MS`); the 23 `takclient` binaries hold only
`TAK_MARK_OUTSIDE`; the 23 `cartographer` binaries hold only
`TAK_CARTOGRAPHER_RECOVERY` and `TAK_EDITOR_RULE_NAMES` plus their split
pieces; none contains `AUTOPLAY`, `TAK_START_FLYERS`, `TAK_NET_JITTER_MS`,
`TAK_FOG_EYE`, `TAK_CARTOGRAPHER_PROFILE`, `TAK_HIDDEN_UNITS`, `TAK_SHOT_MENU`
or `TAK_VERIFYPROF`. The licence files are in every package, zip, installer
and app (the Cartographer app carries `EDITOR-FONT-LICENSE.txt` only, as
before), and all `STREAMING-LICENSES.txt` copies carry the OpenSSL notice. No
`mods/`, `overrides/`, `click.hpi`, ManaPost or CUTbugfix is shipped, in the
packages or the debug archives. The packaged Linux `takclient --help` (Fedora
44 and Slackware, and the 8 x86-64 packages that run here, identical text)
lists only `--data`, `--play-map`, `--game-mod` and `--version` (plus
`--help`). The `takclient`, `takserver` and `cartographer` of every x86-64
package report `0.9.12 (build v0.9.12)` (the Arch `takclient` needs GLIBC 2.44
and cannot run on this Fedora host, as in 0.9.11).

The Fedora 44 x86-64 RPM's `takserver` printed "listening ... (protocol v305)"
with retail gameplay hash `d6000f4fa9095a51`. It ran the Retail AI game on
Inner Circle with `--local --no-auth --seed 1` and scratch HOME,
`XDG_DATA_HOME` and `XDG_CONFIG_HOME`. The client was the Debug build of
`39dd049f` (`build-dbg`, the code of v0.9.12) with `TAK_AI_LEVEL=6
TAK_SPEED=80 --mpai --time 60`. The game ended `hash=12003fba552b8696 units=4
err=none end=timelimit` (tick 1800), the expected value. The server's data
hash accepted the client, and no desync or referee-suspect line appeared; the
server was then killed by PID. The same RPM's `cartographer "Inner Circle"
--bundle <file>` (headless, scratch HOME) loaded the map, wrote a 379,088-byte
`.kmp`, exited 0 and left nothing in the scratch HOME. The packaged release
`takclient --data <read-only link farm> --play-map <that .kmp>` reached the
single-player lobby (client joined, game created, map verified). The retail
data folder was a `chmod -R a-w` symlink copy of `assets/game` (77 entries plus
the root). No entry in that copy changed in name or mtime, and the profile
files landed under the scratch `XDG_DATA_HOME`. The draft's live body equals the
prepared `body.md` apart from one trailing newline. Every `blob` `v0.9.12`
link in the release notes (4 distinct URLs: `docs/development.md`,
`docs/public-server.md#versions`, `docs/release-0.9.11-notes.md#known-limits`
and `docs/release-0.9.12-validation.md`) resolves to a file present at the tag,
and both anchors exist.

## Signing

Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid**
(timestamped, "Curtis Edge") for all 7 signed files: the x64 installer and
`takclient`, `takserver` and `cartographer` in both the x64 and arm64 zips,
the same 7 as 0.9.11. The x64 `takclient.exe` and `takserver.exe` report
`0.9.12 (build v0.9.12)`, and the arm64 `takclient.exe` and `takserver.exe`
carry the `v0.9.12` build string. `--mpai` was not run on Windows or macOS,
per the release rule; the Windows and macOS tag CI determinism tests cover
them. The files were run from a scratch copy with a scratch profile; the real
profile listing on oden-win was identical before and after (352 file entries),
and the scratch folder was removed.

macOS: on the MacBook (no builds), both DMGs and all eight app copies (the game
and TAK Cartographer, from the DMGs and from the zips, arm64 and x64) pass
`stapler validate`, and `spctl -a -vv` accepts them as "Notarized Developer ID"
(Curtis Edge, L479Y59RN8). Both DMGs themselves are also stapled and accepted
as Notarized Developer ID. Each app copy passes `codesign --verify --strict
--deep` and has `NSHighResolutionCapable` true; the game apps have
`NSMicrophoneUsageDescription`, and the `takclient` has the `audio-input`
entitlement (the other binaries do not). The bare zip programs pass `codesign
--strict` with the same team id; `spctl` calls them "not an app", as expected.
The macOS `takclient` and `takserver` report `0.9.12 (build v0.9.12)` (arm64
and x64, apps, bare programs and the debug zips' takclient) and contain the MP4
muxer strings. No licence file is missing, and no `mods/`, `overrides/` or
`click.hpi` is present. `otool -L` of the shipped binaries shows no `libssl`,
`libcrypto`, `libsqlite` or libopus dylib. Both takclients contain `OpenSSL
3.5.8 25 Aug 2026` and no SecureTransport TLS symbols, and Security.framework
is still linked for the system root anchors.

macOS arm64 was built on `macos-15` for the first time. `vtool -show-build` on
all 24 Mach-O `takclient`, `takserver` and `cartographer` binaries (the zips,
the apps in the zips and DMGs, and the debug zips, arm64 and x86_64) reports
`LC_BUILD_VERSION platform MACOS minos 14.0 sdk 15.5`, linker LD 1167.5, and
all 8 apps have `LSMinimumSystemVersion` 14.0 and version 0.9.12. The Mac's
real `TAKengine`, Preferences and Caches listing (106 files) was identical
before and after, and the scratch folder was removed.

## Publication

Published on 2026-10-30 19:40:20 UTC as the Latest release, 29 assets
(v0.9.12 -> `a6e56964`, tag object `0064630d`); 0.9.11 and earlier releases
are kept.

## Limits

- No game mixing 0.9.12 with 0.9.11 or older is possible: protocol 305 is
  refused by every older server and client. No mixed-version game was run
  with a real 0.9.11 binary.
- No Windows or macOS `--mpai` for 0.9.12; their tag CI determinism tests
  cover them (`state_hash_test` runs in all three workflows).
- The speed and fog measurements ran on an Intel Core Ultra 9 275HX, not on
  the reporting player's Ryzen; the speed run is `4b374c14`, and the AI
  measurements predate the vacated-base rule and the tick-0 wait.
- +10 is still limited by the plain simulation cost past an estimated 3,500
  to 4,000 units, and a spectating client on a slow core can lag first.
- The AI, fog and streaming limits are in the
  [release notes](release-0.9.12-notes.md#known-limits).
- Tag CI: Linux packages, Windows and Determinism passed on attempt 1; macOS
  needed one rerun. Its attempt 1 built both architectures, but the arm64 job
  failed at `hdiutil verify` ("Resource temporarily unavailable", five
  retries) after a full successful build, which skipped signing; the failed-job
  rerun (attempt 2) passed, including signing and notarisation. Earlier tag
  attempts were deleted with their drafts and re-tagged before anything was
  published: one at `748a7eda` (a Windows build failure from the `near`/`far`
  macros in the fog test, and a macOS slow-upload test failure), and one at
  `39dd049f` whose macOS arm64 job was cancelled instantly with 0 steps by
  GitHub's `macos-14` brownouts, on the run and on its rerun. `a6e56964` moves the arm64 job to `macos-15`
  (deployment target 14.0); the three arm64 macOS assets are 1.4 to 2.8% smaller
  than 0.9.11's and still report minos 14.0.
- All of 0.9.11's limits carry over (see
  [release-0.9.11-validation.md](release-0.9.11-validation.md#limits)).
