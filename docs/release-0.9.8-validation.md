# 0.9.8 release validation

Protocol **302** (was 301 in 0.9.7), replay format **15** (unchanged),
generator version **9** (unchanged).

TODO-LEAD: tag line (tag `v0.9.8` at which commit, full sha, annotated tag
object, publication time).

## Scope and compatibility

0.9.8 follows 0.9.7 (protocol 301; tag `v0.9.7` -> `3c4cf3b2`). Main after
0.9.7's validation commit `98810658` has the following, in order of landing;
the code tested below is `b23f8cac` (full suite) and `7b99dfff` (the
environment cleanup on top, which changes no game state):

- `4585e9da` Sim: guards answer their guarded unit's attacker; revives wait
  for mana and respect the unit limit (`kNetVersion` 301 -> 302).
- `5ce3c0a1` Docs: guard defends, revive retry and unit limit (CHANGELOG,
  retail notes).
- `99cc8a91` Client: right-click Guard for builders (the original's per-unit
  resolver, `src/client/friendlyorder.h`; `friendly_order_test`), merged from
  `task-rclick-guard` as `e9a3c9fb` with its docs commit `b256fa18`.
- `0dc7cb83`, `4d4244df`, `30559fb4` Documentation audit of about 35 living
  documents (README, user guide and quick start; developer, server and
  multiplayer design docs; Cartographer, pathfinding, retail-engine, AI,
  campaign and research docs), merged from `task-docs-a`, `-b` and `-c` as
  `cc51e368`, `7aae1a73` and `49591f08`.
- `48e004b6`, `b23f8cac` Docs: guard defence, right-click Guard and revive
  limits in the living docs; guarding an ally's unit is not possible.
- `7b99dfff` Release builds read no `TAK_*` diagnostic environment variables
  outside the four `takserver` timers (`src/util/devenv.h`).

`git diff --stat 98810658 7b99dfff`: 52 files changed, 1175 insertions(+),
257 deletions(-); `-- src`: 14 files changed, 290 insertions(+), 108
deletions(-) (`src/sim` 3 files, `src/client` 4, `src/net` 2, `src/server` 2,
`src/ai` 1, `src/cartographer` 1, `src/util` 1; the rest is tests,
`CMakeLists.txt`, `CHANGELOG.md`, `CLAUDE.md`, `tools/we_timing.sh` and the
documentation). 14 commits.

TODO-LEAD: final `git diff --stat v0.9.7 v0.9.8` (whole tree and `-- src`) and
the commit count, once the tag exists.

The preparation commit on top changes no code: `CMakeLists.txt` (the version
line), `CHANGELOG.md`, `README.md`, the version and protocol lines in `docs/`,
the notes, this report, `docs/img/README.md` and `docs/img/title.jpg`.

**Compatibility.** `kNetVersion` is **302** in `src/net/protocol.h` (301 at
`v0.9.7`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** is unchanged. The gameplay data hash of the
test install is `d6000f4fa9095a51`.

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared. So **0.9.8 plays
  with no 0.9.7 or older client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.8 refuses every
  0.9.7 (protocol 301) replay. `tools/replay_test.cpp` pins 302.
- **Hashed state.** It gains `Unit::guardHeard` (folded in with a tag when
  set); `Unit::attackedTick` is not hashed. A guard's answer and the raising
  limits change the game only where a guard's charge is hit, or a raising
  meets a limit or a short computer caster. No gate game does, so **the gate
  hashes did not change** (below).
- **Maps, mods and settings.** No map, recipe, package or settings format
  changed.

## Local preparation checks

All ran on Linux with optimized Debug builds (`build-o2`, `-O2 -g`, ccache +
mold, `TAK_TEST_DATA` set so the data-backed tests ran) and a Release build.
Every takclient and takserver run used scratch `HOME`, `XDG_DATA_HOME` and
`XDG_CONFIG_HOME`.

- **Builds.** At `7b99dfff` the binaries print `0.9.7 (build ...)`, because
  the version number changes in the preparation commit; the prepared tree's
  own `takclient --version` prints `0.9.8 (build v0.9.7-15-g7b99dfffce6a-dirty)`
  from a fresh Release build and from `build-o2` (the tree was dirty only with
  the preparation edits).
- **New tests.** `helpbuild_test` `guardDefends` (with its same-hash
  determinism check), `production_test` `abandonedReviveWaits` and
  `reviveRespectsUnitLimit`, and `friendly_order` / `friendly_order_data` all
  fail on the old code and pass on the new.
- **Full suite** (`ctest`, `build-o2`, 777 tests) on the merged code at
  `b23f8cac`: **776 of 777** passed; the one failure is `vashim_unavailable`,
  the known failure on this host (the local `third_party/ffmpeg-bink` predates
  the libva shim, which CI builds).
- **Quick suite** (`ctest -L quick`) after the environment cleanup at
  `7b99dfff`: **512 of 513** passed; the one failure is `vashim_unavailable`
  (as above).
- **Detmath guard** (`tools/check-detmath.sh`, on the prepared tree): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): golden
  `dcef618cd2e4d558`, unchanged from 0.9.7.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `--crusades`
  for the Crusades games, `TAK_SPEED=80`, `build-o2`). The gates ran on the
  guard and revive change; the Retail AI 60 s gate was run again after the
  environment cleanup and gave the same hash.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d0da4ccc71d2067c` | `e820d37865085bd9` |
| Legion | `c3f6a3a13f0c167f` | `0783cb05f7f128cf` |
| Retail AI (`TAK_AI_LEVEL=6`) | `4952493f51b9a568` | not run |
| Crusades, Retail pathfinding | not run | `f2f51dc1703fc209` |
| Crusades, Legion | not run | `c4189ae189dd68dc` |

**The gate hashes are the 0.9.6 and 0.9.7 values.** The AIs never give Guard
orders, so no guard answers an alarm and `guardHeard` stays zero, and no gate
game raises past a limit or has a computer caster retry a raising. The
cross-compiler golden is unchanged for the same reason.

## Screenshots

One picture was retaken, by the prepared tree's build (Debug `-O2`, CMake
`VERSION 0.9.8`): `docs/img/title.jpg`, with the 0.9.1 title capture
(`takclient menu --shot`, offscreen driver, 1920 x 1080 window cropped to the
1440 x 1080 artwork). It reads VERSION 0.9.8 and shows the three doors (looked
at). Nothing else in 0.9.8 changes a picture, so none was retaken or deleted.
`docs/img/README.md` lists the details.

## Desync sweeps

No local or remote desync sweep (`tools/desync-hunt.sh`,
`tools/desync-hunt-remote.sh`) was run for 0.9.8, and no two-client game, so
no multiplayer run had a guard answering an alarm (the AIs never guard); the
answer is covered by `helpbuild_test` (including its same-hash check).

TODO-LEAD: say whether a remote multiplayer sweep is wanted before the tag.

## CI and packages

TODO-LEAD: tag workflows and their results, package verification (assets,
digests, build strings, `--version` of each package).

## Signing

TODO-LEAD: Windows Authenticode and macOS notarisation results.

## Publication

TODO-LEAD: publication time, asset count, Latest flag.

## Limits

- No game mixing 0.9.8 with 0.9.7 or older is possible: protocol 302 is refused
  by every older server and client. No mixed-version game was run with a real
  0.9.7 binary.
- No Windows or macOS `--mpai` for 0.9.8; their tag CI determinism tests cover
  them.
- No desync sweep and no multiplayer or remote run had a guard answering an
  alarm; the AI never guards. The guard-defence and right-click limits are in
  the [release notes](release-0.9.8-notes.md#known-limits).
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it.
- TODO-LEAD: tag CI limits (failures, reruns, stalls).
- All of 0.9.7's limits carry over (see
  [release-0.9.7-validation.md](release-0.9.7-validation.md#limits)).
