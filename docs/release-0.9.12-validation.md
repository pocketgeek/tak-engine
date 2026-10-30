# 0.9.12 release validation

Protocol **305** (was 304 in 0.9.11), replay format **15** (unchanged),
generator version **9** (unchanged).

TODO-LEAD: tag line (tag `v0.9.12` at which commit, full sha, annotated tag
object, publication time).

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

`git diff --stat v0.9.11 95ca9163`: 142 files changed, 8196 insertions(+),
3917 deletions(-); `-- src`: 38 files changed, 3306 insertions(+), 1137
deletions(-) (`src/sim` 7 files, `src/client` 22, `src/ai` 2, `src/net` 1,
`src/server` 3, `src/video` 3; the rest is tests, test baselines, CI
workflows, `CMakeLists.txt`, `CHANGELOG.md`, the documentation and four guide
pictures). 43 commits (34 without the merges).

TODO-LEAD: final `git diff --stat v0.9.11 v0.9.12` (whole tree and `-- src`) and
the commit count, once the tag exists.

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
  buttons are client-only and hash nothing.
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
- **Full suite on `95ca9163`** (Debug `-O2`, all targets built, `ctest -j12`,
  scratch `HOME` and XDG folders, test data `assets/game`, which has no mods):
  789 of 791 passed, one skipped (`vashim_system_loader`, which skips itself).
  One failed: `vashim_unavailable` ("no display on a render node without
  libva"), the known local failure; CI builds FFmpeg with the shim and runs it
  there. Wall time 1,343 s.
  TODO-LEAD: rerun the full suite on the final code (the shot-range fix
  changed `src/sim` after this run).
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): golden
  `dcef618cd2e4d558` from g++ and clang++ at -O0, -O2 and -O3, unchanged from
  0.9.11. The aarch64 leg was skipped because no target libc or headers are
  installed here. Rerun on `task-los-range`: the same golden.
- **Gates on the final code** (`95ca9163`, the set-up below, ports
  24301-24307): all seven hashes equal the table below in three runs. All
  seven servers loaded retail gameplay hash `d6000f4fa9095a51` in every run,
  and no desync, mismatch or referee-suspect line appeared in any client or
  server log of the three runs. On `task-los-range` (ports 24311-24317,
  `~/tak-tmp/gates-los/`), four runs (the third on the final, comment-only
  tidied build, the fourth by the review on the same build): all seven
  hashes unchanged from the table below and identical in every run; no desync, mismatch or referee-suspect line.
- **Detmath guard** (`tools/check-detmath.sh`, on the prepared tree): OK.
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
  for the Crusades games, `TAK_SPEED=80`), on main `95ca9163`, three runs
  (`~/tak-tmp/gates-0912-suite/`); the retail fog review's runs of the same
  seven (`~/tak-tmp/scratch/retailfog-review/gates/`) give the same hashes:

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | **`779b760554ee2b85`** (was `d0da4ccc71d2067c`) | **`83b33de237aaa0a7`** (was `7a0be4440cd154b7`) |
| Legion | **`95f426566c4440da`** (was `7a403f097f3506b3`) | **`e1161d1937792d5b`** (was `dc2652648568ad54`) |
| Retail AI (`TAK_AI_LEVEL=6`) | **`468f6d3dbbf68923`** (was `4952493f51b9a568`) | not run |
| Crusades, Retail pathfinding | not run | **`eb3107aa3d2bbb93`** (was `3e8c0ad3b4f1b859`) |
| Crusades, Legion | not run | **`c58249d598fd55af`** (was `b0c9748d62aafeb6`) |

**All seven changed, as expected.** Retail 60 s, Retail 300 s, Legion 60 s
and the Retail AI changed by the hash layout alone (equal to
`task-perf-hash`'s own runs at the combine). Legion 300 s and the two Crusades
games changed with the AI as well: at the combine they read
`4ceb517086c9aa75`, `56aae72cb72e8880` and `545ca39f878ff37a`, and moved
again with the AI commits after the combine (`7c2e3fc8` left all seven
unchanged; the tick-0 wait `eec342d6` too). The fog display
changed none of them (it hashes nothing).

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

TODO-LEAD: tag workflows and their results, package verification (assets,
digests, build strings, `--version` of each package).

## Signing

TODO-LEAD: Windows Authenticode and macOS notarisation results.

## Publication

TODO-LEAD: publication time, asset count, Latest flag.

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
- TODO-LEAD: tag CI limits (failures, reruns, stalls).
- All of 0.9.11's limits carry over (see
  [release-0.9.11-validation.md](release-0.9.11-validation.md#limits)).
