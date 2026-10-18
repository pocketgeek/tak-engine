# 0.8.4 release validation

<!-- SKELETON (docs branch task-release-084-docs). Sections marked PENDING are
     filled in by the release process (memory: release-process.md); the shape
     follows docs/release-0.8.3-validation.md. Remove every PENDING marker and
     HTML comment before the final validation commit. -->

Protocol **276** (0.8.3: 266), replay format **13**, generator version **8**,
campaign payload **4**, database schema **9** (the last four unchanged since
0.8.3).
<!-- LEAD: re-check kNetVersion right before tagging (it moved during 0.8.0 prep). -->

PENDING: Tagged `v0.8.4` at `<sha>` and published on `<date>`.

## Scope and compatibility

0.8.4 follows 0.8.3 (protocol 266). After the 0.8.3 validation commit
`c2c25707`, 121 commits up to `c551ae6c` (origin/main when this skeleton was
written) change the simulation in both pathfinding modes, the server-side AI,
the client (voice chat, replays), the network layer and server, the tests and
the documentation. `git diff --stat v0.8.3..c551ae6c -- src` touches 60 files:
`src/sim/` (12), `src/client/` (30), `src/net/` (13), `src/server/` (3) and
`src/ai/` (2). Nothing under `src/tnt/` (the map generator), `src/campaign/`,
`src/cartographer/`, `src/cob/`, `src/crt/` or `src/util/` changed.
<!-- RENDER-OPT PLACEHOLDER: add the rendering program's merge, its commit range
     and its src/ file counts (and recount everything above) once
     task-render-opt is merged onto main. -->

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
`7535cb08ee787b75`).
<!-- LEAD: confirm after the rendering merge (Local preparation checks). -->

Every measured figure in the [release notes](release-0.8.4-notes.md) is taken
from the commit that made the change, with its setup, or from the section of
`docs/legion-10000-report.md`, `docs/legion-pathfinding.md`,
`docs/ai-defensive-2026-10-17.md` or `docs/multiplayer-design.md` it cites.

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
  app's `takclient` (and the bare `takclient`) shows
  `com.apple.security.device.audio-input`.
- As in 0.8.1 to 0.8.3: no packaged Linux `takclient` has a `NEEDED` entry for
  libsecret, GLib, GIO or GObject.

## Screenshots

PENDING. Candidates, following [the capture notes](img/README.md):

- `title.jpg` (shows VERSION 0.8.4).
- `diplomacy.jpg`: the diplomacy screen now has **Voice** and **Volume**
  columns. The user guide's caption says the current picture predates them;
  drop that sentence when it is re-rendered.
- A new user-guide picture of the **Voice Chat** page, if wanted.
- Any lobby picture showing an AI difficulty list (Absurdly Defensive, violet).
- The scenes the rendering program changes.
  <!-- RENDER-OPT PLACEHOLDER -->
- Simulation-dependent pictures (Retail moved with 273; Legion with 267–273).

## Limits

PENDING. Draft items:

- No game mixing 0.8.3 and 0.8.4 builds was played, and none can be (protocol
  276 against 266, the Hello gate and the replay loader).
- Voice chat was not exercised by the remote sweep; its coverage is the voice
  tests above and the user's own multiplayer sessions. Windows before 10
  version 1703 cannot send voice (no `SIO_TCP_INFO`), by design.
- The Legion 10,000 program does not meet its own success criterion for
  congested workloads (the report's sections 13 and 14): crowds at corners and
  narrow passages, the queue for many unrelated destinations, and many-10000 /
  scatter-10000 over the tick budget in 13% / 2% of ticks.
- Local visual inspection covers Linux OpenGL only; Windows and macOS packages
  are checked from CI and by signature, notarization and build-id checks.
