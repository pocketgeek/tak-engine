# 0.8.9 release validation

Protocol **288**, replay format **14**, generator version **9**, campaign
payload **4**, database schema **9**: all unchanged from 0.8.8.
<!-- LEAD: re-check kNetVersion and kReplayFormat right before tagging (288 / 14
     at 71757b75). If either moves, 0.8.9 is no longer compatible with 0.8.8:
     rewrite the compatibility statements in these notes, the release notes,
     README.md ("New in 0.8.9", protocol lines), docs/user-guide.md (Versions,
     Replays, troubleshooting), docs/public-server.md,
     docs/random-map-generation.md and docs/multiplayer-design.md. -->

TODO-LEAD: Tagged `v0.8.9` at `<sha>` (annotated tag object `<obj>`) and published on `<date> <time> UTC`.

## Scope and compatibility

0.8.9 follows 0.8.8 (protocol 288). After the 0.8.8 validation commit
`62fd8588`, main reached `71757b75` in 10 commits, all linear (no merges).
`git diff --stat 62fd8588..71757b75 -- src` touches 12 files: `src/client/`
(11: `areadrag.h` and `buildline.h` (both new), `chatbox.h`, `gameview.h`,
`gameview_devharness.cpp`, `gameview_hud.cpp`, `gameview_impl.cpp`,
`gameview_input.cpp`, `gameview_lobby.cpp`, `gameview_net.cpp`,
`gameview_render.cpp`) and `src/net/client.h` (a header-only display helper,
`MpClient::roomModStatus`, that no simulation, wire or replay code calls).
Nothing under `src/sim/`, `src/server/`, `src/ai/`, `src/campaign/`,
`src/tnt/`, `src/terrain/`, `src/cob/` or `src/crt/` changed. The other
files are tests (`tools/area_drag_test.cpp`, `tools/buildline_test.cpp` (both
new), `tools/chatbox_test.cpp`, `tools/mod_packs_test.cpp`), their CMake
registration, and docs (`docs/retail-engine.md`, `docs/user-guide.md`,
`docs/quick-start.md`).
TODO-LEAD: list any commit that lands on main after `71757b75` before the tag
(and whether it touches `src/sim`, `src/net`, `src/server` or `src/ai`), and the
preparation commits (screenshots, docs with the CMake version bump to 0.8.9,
this report).

The changes, all client only:

- Create screen and room layout: `2f601915` (every create-screen option button
  300 px wide, every caption at one size in both UI fonts), `627698d2` (room
  rules: MAP SCENARIO is a grid line at the others' size, scenario details
  under both columns), `d6377bd9` (GAME MODS VERIFIED only when game packs are
  in play; `mod_packs_test` covers the rule).
- Chat box: `4ecae85c` (always above the build row and its TRAINING line, idle
  and typing), `24f7b991` (a build row that is not drawn never hides chat:
  room for two lines and the composer, `chatbox::minRoom`).
- Area commands: `4f5e9a9d` (reclaim and lodestone boxes need a deliberate
  32 x 32 UI-px box, UI-scale aware; drag boxes anchored to the map),
  `aa572bde` (clicks act at the press; the pointer must move 32 px on either
  axis; edge-scrolled boxes count), `c6d9168c` (Windows `near` macro rename).
  Retail finding (`0x5202c0`, `0x527bf0`) in `docs/retail-engine.md`.
- Shift-drag build lines: `8b1d5631` (tight default spacing, wheel gap 0..12
  with a GAP label and no camera zoom, gap kept for the game and reset each
  game, never saved; builders kept on one side with short Moves), `71757b75`
  (chained lines plan from the builder's queue; wheel notch handling).

Build lines are queued as the existing Build and Move commands, so **Retail and
Legion are expected to be bit-identical to 0.8.8.** `--mpai` Inner Circle seed 1
must equal 0.8.8: Retail 60 s `b353410003e98dd5`, 300 s `a3d953803a064191`;
Legion 60 s `0825aeb9e8139203`, 300 s `ce5c4ba7562e53b9`.
TODO-LEAD: Linux results from the local checks; `--mpai` on Windows and macOS
(optimized Debug builds of the tag on oden-win and the arm64 MacBook, all eight
games, `err=none end=timelimit`, no desync or referee-suspect line).

**Compatibility.** `kNetVersion` is **288** in `src/net/protocol.h` at
`71757b75`, as at `v0.8.8`. `kReplayFormat` is **14** in
`src/net/replayhdr.h`, as at `v0.8.8`. The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** The server's Hello check rejects only a client whose
  protocol differs from `kNetVersion` (or whose gameplay-data hash differs); the
  build id is logged, not enforced. So **0.8.9 and 0.8.8 play together in
  either direction**, and a server may host a mix of both. 0.8.7 or earlier
  still cannot join either.
- **Replays.** `supportedReplayProtocol` accepts a recording whose protocol
  equals `kNetVersion`, so 0.8.9 plays 0.8.8 replays and 0.8.8 plays 0.8.9
  replays (both write format 14). `tools/replay_test.cpp` still pins
  `kNetVersion == 288` and `kReplayFormat == 14`.
- **Maps, mods and settings.** Saved `.kmp` maps, generator-8/9 recipes,
  `mods/` packs and `ModCache/*.takmods` carry over. No settings key was added
  or removed (the build-line gap is per game and never saved).
- TODO-LEAD: was a mixed 0.8.8 / 0.8.9 game played? If not, say so (as 0.8.1
  did): compatibility rests on the unchanged protocol, replay format and
  simulation sources.

## Local preparation checks

TODO-LEAD: from the local-checks agent. Shape as in 0.8.8: clean detached
worktree (`~/tak-tmp/wt/release-089-local`) at `71757b75`, toolchains, cores
4-23, owned retail install as `--data` and `TAK_TEST_DATA`; binaries call
themselves `0.8.8 (build v0.8.8-11-g71757b75...)` until the version bump lands.

**Result:** TODO-LEAD.

- **Multiplayer and replays.** TODO-LEAD (288 / 14, unchanged from 0.8.8).
- **Builds.** TODO-LEAD: Release, Debug, optimized Debug (`-O2`), Clang Debug
  and ASan all-target builds; step counts against 0.8.8 (886 Release / 887
  others); warning-line count and kinds against 0.8.8 (950).
- **Full CTest.** TODO-LEAD: registered tests against 0.8.8 (734 Debug / 696
  Release). Expected additions: `area_drag`, `buildline` (all trees), and in
  the Debug trees with `TAK_TEST_DATA` `area_drag_input`, `buildline_harness`,
  `buildline_chain_u`, `buildline_chain_box`, `buildline_chain_tower_v`,
  `buildline_chain_double_back`.

  | Tree | Selection | Result | Time | Failures |
  | --- | --- | --- | ---: | --- |
  | Release | `-LE nightly`, TODO | TODO | TODO | TODO |
  | Release | `-L nightly`, TODO | TODO | TODO | TODO |
  | Optimized Debug | `-LE nightly`, TODO | TODO | TODO | TODO |
  | Optimized Debug | `-L nightly`, TODO | TODO | TODO | TODO |
  | Debug (`-O0`) | `-LE nightly`, TODO | TODO | TODO | TODO |
  | Clang Debug | `-LE nightly`, TODO | TODO | TODO | TODO |
  | ASan | `-LE nightly`, TODO | TODO | TODO | TODO (`ASAN_OPTIONS=detect_leaks=0` expected, SDL D-Bus) |

  TODO-LEAD: notes on any failure (known flakes: `voice` at `-O2`,
  `crusades_client_network` at `-O0`); the unoptimized Legion tests against
  their limits; the settings-leak guard (`pref_guard_*`, real `settings.ini`
  SHA-256 and mtime before and after).
- **`--mpai` golden hashes.** TODO-LEAD: one run each on the optimized Debug
  build, Inner Circle, `takserver --local --no-auth --seed 1`, an empty
  `XDG_DATA_HOME`, `TAK_LEGION=1` for Legion.

  | Mode | Duration | `71757b75` (288) | 0.8.8 (288) |
  | --- | ---: | --- | --- |
  | Legion | 300 s | TODO | `ce5c4ba7562e53b9` |
  | Retail | 300 s | TODO | `a3d953803a064191` |
  | Legion | 60 s | TODO | `0825aeb9e8139203` |
  | Retail | 60 s | TODO | `b353410003e98dd5` |

- **Determinism.** TODO-LEAD: `tools/check-determinism.sh` golden (0.8.8:
  `dcef618cd2e4d558`); `tools/check-detmath.sh`.
- **Legion scenario gate.** TODO-LEAD: 145 fixtures (unchanged since 0.8.8),
  `legion_check.py check` and `validate` results.
- **Retail group emulation check (formation-air).** TODO-LEAD.
- **Python research suite.** TODO-LEAD (0.8.8: 165 tests; verifier 6 tests;
  retail archive SHA-256 before and after).
- **Syntax checks.** TODO-LEAD (tracked Python files compile; `bash -n` on the
  shell scripts).

Logs: TODO-LEAD (`~/tak-tmp/scratch/release-089/`).

**Final source.** TODO-LEAD: after the preparation commits, the optimized
Debug tree rebuilt every target (version `0.8.9`) and passed `ctest -L quick`
TODO/TODO.

## Remote multiplayer sweep

TODO-LEAD: the 37-scenario table for Retail and Legion (`tools/desync-hunt-remote.sh`,
referees on `tak.pgnet.us` and `vpn3.pgnet.us` with isolated binaries and ports, never
the public takserver), date and UTC times, build id, the three `--validate` gates
(build identity, planted desync, game mod transfer), referee binary SHA-256, replay
headers (protocol 288, format 14, seeds 2000-2036), and any retries
(`TAK_WALL_TIMEOUT=3600`).

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | TODO | TODO | TODO | TODO |
| Legion | TODO | TODO | TODO | TODO |

### Reference ticks and hashes (0.8.9 candidate)

TODO-LEAD: the per-case table in the 0.8.8 shape (end, tick, hash, units, "vs 0.8.8"
for each mode). With no simulation change every deterministic case should be
bit-identical to 0.8.8; the six `TAK_AUTOPLAY` cases never repeat a hash, and
`h-lake`, `h-stress`, `2h-stress`, `2h-gods`, `4h-gods` and `h-everything` are
load-sensitive.

### Comparison with 0.8.8

TODO-LEAD: which cases are bit-identical to 0.8.8 in each mode and why any
others differ (expected: only the autoplay and load-sensitive cases).

## CI and packages

TODO-LEAD: main CI on the final source (Windows, Linux packages, macOS); tag CI
(Determinism dispatched with `gh workflow run determinism.yml --ref v0.8.9`,
Linux, Windows, macOS incl. Sign + notarize) with run links and any reruns
(known flakes: macOS `stream_test` timing, "Upload CI artifact: ENOTFOUND",
macOS x64 `hdiutil` "Resource busy", Slackware mirror md5sum error).

TODO-LEAD: the 29 assets (expected the same set as 0.8.8 with the version
swapped): SHA-256 digests and sizes, build ids `0.8.9 (build v0.8.9)` on Linux,
Windows and macOS, 9 Authenticode Valid files on oden-win, both DMGs and apps
notarized and stapled, `NSHighResolutionCapable`, the microphone entitlement,
Opus static with `OPUS-LICENSE.txt`, `CHAT-FONT-LICENSE.txt` everywhere, no
libsecret/GLib `NEEDED`, no `mods/` or `click.hpi` in any package, and
`docs/release-0.8.9-notes.md` plus this report in the source tree at the tag.

TODO-LEAD: publication time and "latest release" check.

## Screenshots

TODO-LEAD: from the screenshots agent. Expected retakes for this release's
changes (images are not edited by the docs commit):

- `guide/chat-box.jpg` (the box above the build row / TRAINING line);
- the create screen and room pictures whose layout changed: `lobby.jpg`,
  `lobby-scenario.jpg`, `room.jpg`, `guide/random-map-lobby.jpg`,
  `guide/room-mods-power-codes.jpg` (and `cartographer/test-lobby.jpg`,
  `quick/lobby-move-here.jpg` if their rules or mods lines changed);
- check `guide/options-interface.jpg` (no Options change expected);
- `title.jpg` (VERSION 0.8.9), then the README title alt text and gallery
  footnote, which still say 0.8.8;
- new `guide/build-line.jpg` (a tight Shift-drag line with the GAP label),
  placed in `docs/user-guide.md` under *Building*;
- the per-release scenes if retaken (`battle.jpg`, `dragons.jpg`, `flyers.jpg`,
  `hud.jpg`, `naval.jpg`), and `docs/img/README.md` rows and header updated to
  match.

## Limits

- TODO-LEAD: whether a game mixing 0.8.8 and 0.8.9 builds was played (it can
  be: same protocol and replay format).
- All of 0.8.8's limits carry over unchanged (the simulation is 0.8.8's):
  Legion units stand and wait more than Retail's; Legion attackers through a
  narrow gate deal more damage than Retail's; power codes are lockstep (retail
  applied them only on the typer's machine); FULL VISION does not switch LOS
  off at start; UI FONT CLASSIC is not pixel-identical with 0.8.7; the Legion
  bridge lottery; the Legion 10,000 workloads' congestion and tick budget,
  mixed-10000's hovering flyers; Retail `h-cramped` ends early; the macOS
  Retina UI Scale is migrated once; a stationary auto-targeting shooter keeps
  refusing a once-blocked target.
- The campaign book's chapter numeral still touches the bottom of "APTER"
  (retail-equivalent: the retail lettering boxes overlap).
- The area-command test deliberately differs from retail's (`0x527bf0`): 32 px
  on both axes of the box and a real pointer movement, no held-time rule.
- TODO-LEAD: flakes seen in this release's runs.
