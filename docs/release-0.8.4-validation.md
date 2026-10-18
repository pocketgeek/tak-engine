# 0.8.4 release validation

<!-- SKELETON (docs branch task-release-084-docs). Sections marked PENDING are
     filled in by the release process (memory: release-process.md); the shape
     follows docs/release-0.8.3-validation.md. Remove every PENDING marker and
     HTML comment before the final validation commit. -->

Protocol **276** (0.8.3: 266), replay format **13**, generator version **8**,
campaign payload **4**, database schema **9** (the last four unchanged since
0.8.3).
<!-- LEAD: re-check kNetVersion right before tagging (it moved during 0.8.0 prep).
     Version 0.8.4 and protocol 276 are already written in: CMakeLists.txt
     (project VERSION); README.md (badge, random maps x2, multiplayer, overrides,
     New in 0.8.4); docs/user-guide.md (intro, Versions, Replays, public server,
     old replays); docs/public-server.md; docs/random-map-generation.md;
     docs/multiplayer-design.md (pathfinding protocol summary, 268-276 replay
     history, the required-protocol line); these notes and the release notes.
     If kNetVersion moves past 276, update every one of them. -->

PENDING: Tagged `v0.8.4` at `<sha>` and published on `<date>`.

## Scope and compatibility

0.8.4 follows 0.8.3 (protocol 266). After the 0.8.3 validation commit
`c2c25707`, main reached `f11fbc1a` in 225 commits: 121 up to `c551ae6c` change
the simulation in both pathfinding modes, the server-side AI, the client (voice
chat, replays), the network layer and server, the tests and the documentation;
the rendering optimization program adds 103 commits and its merge `a8bbe6ed`
(made on the program's branch, which was based on `c2c25707`). `git diff --stat
v0.8.3..f11fbc1a -- src` touches 91 files: `src/sim/` (12), `src/client/` (59),
`src/net/` (13), `src/server/` (3), `src/ai/` (2) and `src/terrain/` (2). The
rendering program changes 35 client files (6 of them also changed on main) and
both terrain files, and no file under `src/sim/`, `src/net/`, `src/server/` or
`src/ai/`. Nothing under `src/tnt/` (the map generator), `src/campaign/`,
`src/cartographer/`, `src/cob/`, `src/crt/` or `src/util/` changed.

The protocol steps, each recorded in `src/net/protocol.h`:

- **267** explicit selection orders, Legion 10,000 stage 3 (`3312368f`,
  `956d8dce`, `6fbce6bd`); Retail clients keep per-unit commands, so Retail is
  bit-identical.
- **268** fair navigation scheduling and budgets, stage 4 (`d487d65d`).
- **269** route queries, stage 5 (`662574f1`).
- **270** hierarchical routes, stage 6 (`ca157c28`).
- **271** static navigation apart from traffic, stage 7 (`eebef946`).
- **272** chokepoints, subgroups and arrivals, stage 8 (`4f30f19e`,
  `1bb377c3`). Stage 9 (parallel planning, `9064aa35` and before) is exact and
  has no protocol step; the program was merged by `f1374ceb`.
- **273** construction shares short mana, Retail and Legion (`af90da79`),
  hashed.
- **274** a lodestone upgrade finishes under a short share (`57a218d1`).
- **275** AI level 5, Absurdly Defensive (`a5f8b429`).
- **276** voice chat: `VoiceListen` / `VoiceUp` / `VoiceDown`, relayed by the
  server, never in a tick bundle or replay (`60e98711`, `6766c844`,
  `4dc8fa88`). Sim unchanged.

Without a protocol change: the Defensive AI melee answer (`7e1e3798`) and its
grown base, sites and stations (`8588b3df`); replays streamed while playing
(`eadcfd76`, `c2319131`, replay format unchanged); the rest of the voice chat
client (`3d73859d`, `e0714bac`, `8fcd0718`, `b9ab446a`, `e6dc6e30`, `63acb2fd`,
`ae90d023`, `7d04578c`, `c551ae6c`); the CI target fix (`27066ebe`) and the
desync sweep's `h-cramped` Monarch Expendable (`5774601a`).

**Retail is not bit-identical to 0.8.3**: 273 (shared mana) and 274 change
Retail games. 267 to 272 leave Retail bit-identical (the Legion 10,000 report,
section 4), and 275 and 276 leave the default-level simulation unchanged.
The `--mpai` Inner Circle seed 1 hashes recorded by `af90da79` and unchanged
since are Retail 300 s `a3d953803a064191`, 60 s `b353410003e98dd5`; Legion
300 s `ce5c4ba7562e53b9`, 60 s `0825aeb9e8139203` (0.8.3's Retail 300 s was
`7535cb08ee787b75`). They are unchanged on the final tree: an optimized Debug
build of `657aef1b` (`f11fbc1a` plus these docs) gave all four, once each,
against the user's retail install through a read-only scratch folder
(`takserver --local --no-auth --seed 1`, scratch profiles). The rendering
program's own merge check found the same four on its merge `a8bbe6ed`, whose
`src/` equals `f11fbc1a`'s.

Every measured figure in the [release notes](release-0.8.4-notes.md) is taken
from the commit that made the change, with its setup, or from the section of
`docs/legion-10000-report.md`, `docs/legion-pathfinding.md`,
`docs/ai-defensive-2026-10-17.md` or `docs/multiplayer-design.md` it cites. The
Rendering section's figures are Experiment 17 of
`docs/render-optimization-progress.md` (forced-completion table and memory
table, A = `60f2b586`, the 0.8.3 renderer with the program's telemetry, B =
`f97f21c8`, the program's final renderer, on the same simulation).

**Compatibility.** `kNetVersion` is **276** in `src/net/protocol.h` at
`c551ae6c`, against 266 at `v0.8.3`. `kReplayFormat` is **13** in
`src/net/replayhdr.h` (13 at `v0.8.3`); the map generator's `formatVer` is 8
(`src/tnt/mapgen.h`, no change under `src/tnt/`), the campaign payload's
`kVersion` 4 (`src/net/crusades.h`, unchanged) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`, unchanged).

- **Multiplayer.** The server's Hello check (`src/server/server.cpp`) rejects a
  client whose protocol differs from `kNetVersion` ("protocol version
  mismatch"), as well as a different pure-retail gameplay-data hash. So **0.8.4
  cannot play with 0.8.3 or earlier in either direction**. The server and all
  players must update together.
- **Replays.** `supportedReplayProtocol` (`src/net/replayhdr.h`) accepts a
  recording only when its protocol equals `kNetVersion`, so 0.8.4 does not play
  0.8.3 or older replays and those builds do not play 0.8.4 replays, although
  the file format is still 13. `tools/replay_test.cpp` pins
  `kNetVersion == 276`. The streamed writer's finished `.takrep` is the format-13
  layout byte for byte (`replay_writer`).
- **Maps and settings.** Generated-map recipes (generator 8) and saved `.kmp`
  maps carry over. A 0.8.3 settings file reads the same in 0.8.4. 0.8.4 adds
  `voiceEnabled`, `voiceMicDevice`, `voiceOutputSameAsGame`,
  `voiceOutputDevice`, `voiceMicVolume`, `voiceOutputVolume` and the
  `hotkey.voice.ptt` override; 0.8.3 ignores the voice keys (and drops them when
  it saves) and keeps any `hotkey.*` entry it does not know.

## Local preparation checks

PENDING. As in 0.8.3, on a clean detached worktree of `origin/main` after the
rendering merge, with an `assets` link (for `cobanim` and `gaitpace_shipped`)
and cores 4–23 (or 4–15 while the rendering benchmark holds 16–23):

- **Builds**: Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and
  AddressSanitizer, all targets; build ids; `takserver` announces protocol 276.
- **Full CTest** per tree (Release and optimized Debug full; Debug and Clang
  Debug `-LE nightly`); the new tests since 0.8.3; the settings-leak guard;
  `credstore`; all `legion_check_*` gates against `tools/scenarios/baseline.json`;
  battle cost and naval tests; slowest tests.
- **Voice tests**, which need SDL's disk audio driver or a real server:
  `voice`, `voice_client`, `voice_uplink`, `server_voice`, `server_voice_slow`,
  `server_voice_cpu`, `diplomacy_ui`, `conn_test`.
- **Replay tests**: `replay_writer`, `replay_always` (including the campaign
  mission cases).
- **Focused AddressSanitizer/leak run** over the tests new since 0.8.3.
- **Retail group emulation check** (`tools/re/check_retail_group.py`).
- **Determinism**: `tools/check-determinism.sh` (0.8.3: `dcef618cd2e4d558`) and
  `tools/check-detmath.sh`.
- **`--mpai` golden hashes**, once each, both modes, 60 s and 300 s, against the
  values under Scope; optimized Debug binary SHA-256.
- **Python research suite**, the owned-retail archive verifier, retail archive
  SHA-256 before and after.
- **Syntax checks** (`py_compile`, `bash -n`) and **warnings** against 0.8.3's
  725.

## Remote multiplayer sweep

PENDING. The full 37-scenario table for Retail and Legion with
`tools/desync-hunt-remote.sh` (unmodified; record its SHA-256), referees on
`tak.pgnet.us` and `vpn3.pgnet.us` as an isolated test binary on test ports,
never the public service; `--validate` gates (planted desync, override
transfer, build identity); `TAK_WALL_TIMEOUT=3600` for Legion; replay headers
(mode, format 13, protocol 276); client-pair and client-referee checkpoint
counts with the bit-flip check of the comparison script; impaired networks;
isolation and cleanup.

- `h-cramped` now runs with Monarch Expendable (`5774601a`), so 0.8.3's Retail
  reference (tick 1,303) no longer applies; record the new end tick and hash.
- Retail `h-stress` (seed 2003): 0.8.3 recorded tick 4,789
  (`8e7c0acbf9752f10`); 273 changes Retail, so a new value is expected.
- Voice is presentation only and the sweep's clients do not talk; note whether
  any case exercised it.

## CI and packages

PENDING. Determinism (dispatched on the tag: `gh workflow run determinism.yml
--ref v0.8.4`), Linux, Windows and macOS on `main` and on the tag (macOS x64
`stream_test` is a known timing flake: one rerun allowed). All assets
downloaded and checked against GitHub's SHA-256 digests and sizes; ZIP CRCs;
build ids `v0.8.4` with no `-dirty`; Windows signatures (`Get-AuthenticodeSignature`
on oden-win); macOS notarization and stapling on the MacBook.

New checks for 0.8.4:

- **Opus is static.** No packaged `takclient` has a `NEEDED` (Linux), import
  (Windows) or `otool -L` (macOS) entry for libopus, and no package declares
  one. `OPUS-LICENSE.txt` is present in every package beside the other licence
  files.
- **macOS microphone.** `Info.plist` in both DMG and ZIP apps carries
  `NSMicrophoneUsageDescription`, and `codesign -d --entitlements -` on the
  game app, its `takclient` and the bare `takclient` shows
  `com.apple.security.device.audio-input` (the other programs carry no
  entitlement).
- As in 0.8.1 to 0.8.3: no packaged Linux `takclient` has a `NEEDED` entry for
  libsecret, GLib, GIO or GObject.

## Screenshots

Recaptured on 2026-10-18 per [the capture notes](img/README.md), with an
optimized Debug build (`-O2`, the debug capture harnesses) of `657aef1b` (main
`f11fbc1a` plus these docs and the version bump) and the capture-only patch.
The retail data came from the user's install, read-only, through a scratch
folder with its own caches. All scratch profiles were used. Every new picture
was inspected.

- Interface: `title.jpg` (VERSION 0.8.4), `diplomacy.jpg` (VOICE and VOLUME
  columns, with live sliders at 50% and 25% for the two other humans and none
  for the computer player), and the new `guide/voice-chat.jpg`, now shown in
  the user guide's Voice Chat section. The guide no longer says that the
  diplomacy picture predates the Voice and Volume columns.
- Simulation-dependent: `battle.jpg`, `dragons.jpg`, `hud.jpg`, `naval.jpg`,
  `guide/benchmark.jpg`, `guide/scorecard.jpg`, `guide/tactical-dots.jpg` and
  `guide/fire-on-the-move.jpg`. Each is the 0.8.3 scene, with units moved a
  little; the captions were updated where a count changed. The HUD's stats panel
  now reads VID 94 MB (0.8.3 capture: 87 MB), and three of sixteen units
  have fallen in the fire picture (0.8.3: four).
- Rendering: the program draws the same picture as 0.8.3 apart from 1–14
  shadow-edge pixels a frame and the slower terrain fill after a long camera
  jump at 7680×2160 (the capture sizes are 1920×1080 and 1600×900). So no
  picture was retaken for the rendering alone, and none shows a difference.
- Not retaken: `lobby.jpg`, `lobby-scenario.jpg` and `room.jpg` (their screens
  did not change; Absurdly Defensive appears only in a slot's level list),
  `generated.jpg`, the menus and the editor pictures.
- **`flyers.jpg` and `guide/flyers-retail-legion.jpg` kept from 0.8.3:** on this
  build, the Legion flyers of a formation no longer stay over the front of the
  ground units. They fly ahead to the destination, as Retail's do. This shows in
  the gallery scene (44 Zhon, one formation move, 22 s) and in the committed
  `TAK_FLYER_FRONT` fixture (`--testbuild`, 30 Swordsmen and 8 Gryphons, 14 s).
  A `v0.8.3` build with the same capture patch keeps the flyers over the front
  in both scenes. The user guide's [Air units](user-guide.md#air-units) section
  still describes 0.8.3's behaviour. New pictures wait for a fix or a change to
  that text.
  <!-- LEAD: reported to the lead on 2026-10-18; resolve before tagging. -->

## Limits

PENDING. Draft items:

- No game mixing 0.8.3 and 0.8.4 builds was played, and none can be (protocol
  276 against 266, the Hello gate and the replay loader).
- Voice chat was not exercised by the remote sweep; its coverage is the voice
  tests above and the user's own multiplayer sessions. Windows before 10
  version 1703 cannot send voice (no `SIO_TCP_INFO`), by design.
- The Legion 10,000 program does not meet its own success criterion for
  congested workloads (the report's sections 13 and 14): crowds at corners and
  narrow passages, the queue for many unrelated destinations, and three of the
  ten 10,000-unit rows over the 33.3 ms tick budget: many-10000 / scatter-10000
  in 13% / 2% of ticks (more bodies now move), and mixed-10000, whose overrun is
  inherited from its flyers' exploration phase outside Legion.
- Local visual inspection covers Linux OpenGL only; Windows and macOS packages
  are checked from CI and by signature, notarization and build-id checks.
