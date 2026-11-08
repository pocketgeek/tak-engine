# 0.9.22 release validation

Protocol **316** (0.9.21 used 315), replay format **15** (unchanged), generator version
**9** (unchanged).

Tag `v0.9.22` -> `faaeb222` (the preparation commit); annotated tag object
`3990894f`. Published 2026-11-08 03:07:18 UTC as the Latest release.

## Scope and compatibility

0.9.22 follows 0.9.21 (tag `v0.9.21` -> `c1523095`, validation commit `56bee12d`).
`git log v0.9.21..` holds seven commits before the preparation commit, all made
directly on main:

- `8c1ef3d6`: terrain chunk images reuse their textures from per-level free slots
  (every image of a level gets the level's full-size texture); budget 512 MiB.
- `c1b6ee26`: map sections and shadow silhouette pages keep released textures for reuse
  (sections within the 64 MiB idle budget, shadow pages up to kShadowBasePages); freed
  under GPU pressure and on renderer resets.
- `819801fa`: the terrain chunk budget follows the player's GPU MEMORY LIMIT: 40%,
  256 MiB to 2 GiB (UNLIMITED 2 GiB), 512 MiB at the 1.25 GiB default.
- `36effa33`: Voice Chat MICROPHONE on/off switch (`voiceMicEnabled`) and MIC GAIN 0..400 %
  (same key `voiceMicVolume`).
- `3878921b`: the Diplomacy CHAT and VOICE columns start each game at the player's allies
  (occupied slots on its team) instead of everyone.
- `ac1f9dac`: protocol 316. The Diplomacy SPECTATORS row (chat and voice, off by default),
  carried as Chat's trailing spectators byte and VoiceUp's `kVoiceSpectators` flag; a
  spectator's chat and voice are dropped in a running game (server) and not offered
  (client).
- `fc4535ad`: `MpClient::chat(text, mask)` defaults to no spectators; `chat(text)` still
  reaches everyone (found by `map_cache_readonly` on the cut, below).

**Compatibility.** Nothing hashed changed: every `--mpai` gate keeps 0.9.21's hash
(below), so games play as in 0.9.21 in both pathfinding modes. The Chat and VoiceUp
wire formats changed, so the protocol moves to 316 and 0.9.22 plays with neither 0.9.21
nor any earlier version, nor their replays (`replay_test` refuses 315).

## Local tests on the cut

Linux, `build-o2` (Debug `-O2 -g`):

- **Full ctest** (`ctest -j 16`, every label) on `ac1f9dac`: 904 of 905 passed. The one
  failure, `map_cache_readonly` ("private chat escaped recipient mask"), was real:
  `MpClient::chat`'s new spectators argument defaulted to on, so a line sent to one
  player also reached the spectator. Fixed in `fc4535ad`; it passes, with
  `map_transfer`, `voice`, `voice_client`, `voice_uplink`, `server_voice`,
  `server_voice_slow`, `diplomacy_ui`, `chatbox`, the spectator tests and `replay`
  (18 tests).
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`, on `ac1f9dac`: all seven gates keep
  0.9.21's hashes.

| Gate | 0.9.21 | 0.9.22 `ac1f9dac` |
|---|---|---|
| Retail 60 s | `beb81788610d2a6c` | `beb81788610d2a6c` |
| Legion 60 s | `87efa5106c2d3c0e` | `87efa5106c2d3c0e` |
| Retail AI 60 s (`TAK_AI_LEVEL=6`) | `a3ddc1ba59738936` | `a3ddc1ba59738936` |
| Retail 300 s | `30a3e25d3f414fad` | `30a3e25d3f414fad` |
| Legion 300 s | `bd274b29f05f2c18` | `bd274b29f05f2c18` |
| Crusades, Retail 300 s | `23d5e2016eae1bb8` | `23d5e2016eae1bb8` |
| Crusades, Legion 300 s | `ee9c38ff55448ab3` | `ee9c38ff55448ab3` |

- **Video memory** (rendered 60 s menu Benchmark, High at +10, 1920x1080, OpenGL
  offscreen): terrain images 270 uploaded and 270 resident, none destroyed (0.9.21: 85 of
  270 evicted); 122 section uploads into kept textures (176 with the cap at 400 MiB);
  0 MiB tracked after teardown in every run. Render, terrain, shadow and gpuvram tests
  (50) pass.

Not run for this cut (the testing tiers): the remote desync sweeps, ASan, Clang and the
Windows and macOS local suites; tag CI builds and tests all three platforms.

## CI and packages

Tag CI on `v0.9.22`: Determinism (sim) 42417216399 (dispatched), Linux packages
42417215445 and Windows build 42417215418 green at the first attempt. macOS build
42417215425: both builds and the arm64 Sign + notarize passed at once; the x64 Sign +
notarize waited on Apple's notary service for 5 hours until the job limit cancelled it
(as in 0.9.16), and `gh run rerun --failed` finished it in under 3 minutes, replacing
the x64 macOS assets. Main's runs for `faaeb222` and `ac1f9dac` were cancelled in favour
of the tag's (main's Determinism on `ac1f9dac` had passed).

31 assets, as 0.9.21; all 31 downloaded and match the release's SHA-256 digests. The
build string is `v0.9.22` in the Arch package's `takclient` and `takserver`.

## Signing

Windows: on oden-win (scratch `C:/tak/w/rel0922`, removed afterwards; no TAK process
running before or after), `Get-AuthenticodeSignature` reports **Valid** (timestamped,
"Curtis Edge") for the x64 setup program and `takclient`, `takserver` and `cartographer`
in both zips; the x64 `takclient.exe` and `takserver.exe` report `0.9.22 (build v0.9.22)`.

macOS: on the MacBook, both DMGs and all eight app copies pass `stapler validate`, and
`spctl -a -vv` accepts them as "Notarized Developer ID"; every app passes
`codesign --verify --strict --deep` and has version 0.9.22. The arm64 zip's `takclient`
reports `0.9.22 (build v0.9.22)`. The scratch folder was removed.

## Publication

The draft was created with the notes (the validation link rewritten to its `blob`
`v0.9.22` URL) before tag CI uploaded into it; published on 2026-11-08 03:07:18 UTC as
the Latest release, 31 assets; 0.9.21 and earlier releases are kept. The notes'
validation link answers 200.

## Known limits

- Only the Linux suite ran locally (the testing tiers); Windows and macOS were covered by
  tag CI.
- The user guide's Diplomacy and Voice Chat pictures predate the SPECTATORS line and the
  MICROPHONE / MIC GAIN rows (noted in `docs/img/README.md`).
- No mixed-version game was played: the protocol changed, so 0.9.22 plays with no earlier
  release by construction.
