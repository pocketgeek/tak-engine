# 0.9.10 release validation

Protocol **303** (unchanged from 0.9.9), replay format **15** (unchanged),
generator version **9** (unchanged).

TODO-LEAD: tag line (tag `v0.9.10` at which commit, full sha, annotated tag
object, publication time).

## Scope and compatibility

0.9.10 follows 0.9.9 (protocol 303; tag `v0.9.9` -> `c990bbfb`). Main after
0.9.9's validation commit `f2a219e3` has the following, in order of landing;
the code tested below is `4832e58d`:

- `c2f5a24b` Draw flat features (TDF height below 10) under every unit, as
  retail; the `-24` mana-deposit sort bias removed.
- `c2e6cd8d`, `7494602c`, `9b376f27` Options > Graphics: HIDDEN UNITS, its
  policy, settings and renderer tests, and its documentation.
- `eadc0b78` to `8df5569d` (with the merges `c9f19757` and `8a480ded` of
  `task-hidden-units`) and `b381e197`: HIDDEN UNITS display modes developed
  and measured on the branch, with their tests and cost notes; `f2a81d19`
  removed them again before release, and no released setting uses them.
- `94814f4b` macOS: Auto renderer tries OpenGL first, then Metal.
- `4ba5725b` Metal: skip draws whose clip rectangle covers no pixel
  (`src/client/cliprect.h`); `cb9404d9` a harness skip for machines without
  OpenGL.
- `bb163613` Docs: retail does not z-buffer between sprites; the world draw
  order. `9645c6fb` Docs: debug CLI single-player opens its room;
  `TAK_SP_AIS` starts it. `5acb7861` Options picture retaken. Merged as
  `2f4e91bb` (`task-hidden-docs`) and `03976633` (`task-win-lobby`).
- `64bff627` HIDDEN UNITS: FADE, a tree or rock over a unit fades out.
- `a37dfbc3` FADE fades buildings too, FADE OPACITY slider, FADE the default.
- `f2a81d19` OFF, DOTS and FADE only.
- `2ac340d3` Docs: HIDDEN UNITS is Off, Dots and Fade; Options picture
  retaken.
- `4832e58d` HIDDEN UNITS: unused helpers dropped.

`git diff --stat f2a219e3 4832e58d`: 27 files changed, 1731 insertions(+),
157 deletions(-); `-- src`: 16 files changed, 1255 insertions(+), 114
deletions(-), **all in `src/client/`** (the rest is tests, `CMakeLists.txt`,
`tools/render_benchmark.py`, `CHANGELOG.md`, the documentation and the Options
picture). `git diff v0.9.9 4832e58d -- src/sim src/net src/ai src/server` is
empty. 38 commits after `f2a219e3` (34 without the merges).

TODO-LEAD: final `git diff --stat v0.9.9 v0.9.10` (whole tree and `-- src`) and
the commit count, once the tag exists.

The preparation commit on top changes no code: `CMakeLists.txt` (the version
line), `CHANGELOG.md`, `README.md`, the version and protocol lines in `docs/`,
the notes, this report, `docs/img/README.md` and `docs/img/title.jpg`.

**Compatibility.** `kNetVersion` is **303** in `src/net/protocol.h`, as at
`v0.9.9`. `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** is unchanged. The gameplay data hash of the
test install is `d6000f4fa9095a51`, as for 0.9.9.

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared, and the server
  code is unchanged. So **0.9.10 plays with 0.9.9 clients and servers in either
  direction** (checked in source; no mixed-version game was run), and with no
  0.9.8 or older one.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`; both are unchanged,
  so 0.9.10 plays 0.9.9 replays and 0.9.9 plays 0.9.10's. A 0.9.8 (protocol
  302) replay is refused, as in 0.9.9.
- **Hashed state.** No simulation, network, server or AI code changed. Hidden
  Units, the flat-feature draw order, the macOS renderer choice and the Metal
  clip fix are display only and not hashed, so **the gate hashes did not
  change** (below).
- **Maps, mods and settings.** No map, recipe or package format changed.
  Settings gain `hiddenUnits` (`off`, `dots`, `fade`; absent or unknown reads
  as `fade`) and `hiddenFadeOpacity` (0..100, absent = 65); 0.9.9 skips both.

## Local preparation checks

All ran on Linux with optimized Debug builds (`-O2 -g`, ccache + mold,
`TAK_TEST_DATA` set so the data-backed tests ran) and a Release build. Every
takclient and takserver run used scratch `HOME`, `XDG_DATA_HOME` and
`XDG_CONFIG_HOME`.

- **Builds.** At `4832e58d` the binaries print `0.9.9 (build ...)`, because
  the version number changes in the preparation commit; the prepared tree's
  own `takclient --version` and `takserver --version` print
  `0.9.10 (build v0.9.9-39-g4832e58df30e-dirty)` from a Release build (the
  tree was dirty only with the preparation edits), and the title picture's
  Debug `-O2` build reads VERSION 0.9.10.
- **New tests.** `hidden_units_policy` (`tools/hidden_units_test.cpp`), the
  data-backed `hidden_units` harness (Ulasem Arena, `--testbuild`, 1280 x
  960: flat features under every unit, Dots' cover rules and click target,
  Fade's covers and that 100% equals Off), `renderdriver_test` (macOS Auto
  order, Apple's software renderer refused, Apple A18 Pro kept) and
  `settings_test` (Hidden Units and Fade Opacity: default, clamp, round trip,
  reset).
- TODO-LEAD: full suite (`ctest`, `build-0910-suite`) on `4832e58d` and the
  gates on the final code (result of the run in progress).
- **Detmath guard** (`tools/check-detmath.sh`, on the prepared tree): OK (no
  file under `src/sim` changed since 0.9.9).
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): golden
  `dcef618cd2e4d558`, expected unchanged from 0.9.9 (no simulation code
  changed); the tag CI Determinism workflow checks it.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `--crusades`
  for the Crusades games, `TAK_SPEED=80`). The Retail AI 60 s gate ran
  repeatedly on the hidden-units branch and gave `4952493f51b9a568` each
  time.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d0da4ccc71d2067c` | `e820d37865085bd9` |
| Legion | `c3f6a3a13f0c167f` | `0783cb05f7f128cf` |
| Retail AI (`TAK_AI_LEVEL=6`) | `4952493f51b9a568` | not run |
| Crusades, Retail pathfinding | not run | `f2f51dc1703fc209` |
| Crusades, Legion | not run | `c4189ae189dd68dc` |

**The gate hashes are the 0.9.6 to 0.9.9 values**: nothing the hash folds in
changed.

- **Hidden Units cost** (render benchmark, 1280 x 960, single runs): Fade
  about +0.07 ms a frame in a forest and +0.57 ms with a 1,800-unit army in
  view; Off at 10,000 units equal to main within noise.
- **Other platforms** (during development, before the final mode set): Windows
  on `oden-win` with OpenGL, Direct3D 11 and Direct3D 9; macOS on the MacBook
  Neo with OpenGL and Metal. On 2026-10-28 the Neo's Auto chose OpenGL
  ("Apple A18 Pro").

## Screenshots

`docs/img/guide/options-graphics.jpg` was retaken on main before this commit
for the Hidden Units and Fade Opacity rows (`docs/img/README.md`). In the
preparation, `docs/img/title.jpg` was retaken by the prepared tree's build
(Debug `-O2`, CMake `VERSION 0.9.10`) with the 0.9.1 title capture
(`takclient menu --shot`, offscreen driver, 1920 x 1080 window cropped to the
1440 x 1080 artwork). It reads VERSION 0.9.10 and shows the three doors
(looked at). Pictures taken before 0.9.10 that show a unit behind a tree or a
building show it as **Off** does; they were not retaken for Hidden Units, and
none was deleted. `docs/img/README.md` lists the details.

## Desync sweeps

No local or remote desync sweep (`tools/desync-hunt.sh`,
`tools/desync-hunt-remote.sh`) was run for 0.9.10, and no two-client game: no
simulation, network or server code changed, and Hidden Units is local display.

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

- No game mixing 0.9.10 with 0.9.9 was run; that they play together is
  checked in source (same protocol, replay format and server code).
- No Windows or macOS `--mpai` for 0.9.10; their tag CI determinism tests
  cover them. The final Off / Dots / Fade build was not rerun on Windows or
  macOS after the other modes were removed.
- Retina sharpness with OpenGL on a Mac's built-in panel has not been looked at
  by a person. The Fade harness does not cover swaying-tree or distant-image
  building fades. These and the other Hidden Units limits are in the
  [release notes](release-0.9.10-notes.md#known-limits).
- TODO-LEAD: tag CI limits (failures, reruns, stalls).
- All of 0.9.9's limits carry over (see
  [release-0.9.9-validation.md](release-0.9.9-validation.md#limits)).
