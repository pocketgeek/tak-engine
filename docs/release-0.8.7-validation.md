# 0.8.7 release validation

Protocol **284** (0.8.6: 281), replay format **13**, generator version **9**,
campaign payload **4**, database schema **9** (replay, generator, campaign and
schema unchanged since 0.8.6).
<!-- LEAD: re-check kNetVersion right before tagging.
     Version 0.8.7 and protocol 284 are already written in: CMakeLists.txt
     (project VERSION); README.md (badge, New in 0.8.7, random maps x2,
     multiplayer, overrides); docs/user-guide.md (intro, Versions, Replays,
     public server, old replays); docs/public-server.md;
     docs/random-map-generation.md (header line); docs/multiplayer-design.md
     (protocol summary, 282-284 replay history, the required-protocol line);
     these notes and the release notes.
     NOT bumped (they describe the pictures, so they belong to the screenshots
     step): README title alt text ("version 0.8.6"), the gallery footnote
     ("0.7.25 to 0.8.6 builds") and docs/img/README.md. If kNetVersion moves
     past 284, update every one of them. -->

Tagged `v0.8.7` at `TODO-LEAD` (annotated tag object `TODO-LEAD`) and published on TODO-LEAD.

## Scope and compatibility

0.8.7 follows 0.8.6 (protocol 281). After the 0.8.6 validation commit
`bd7f3f6e`, main reached `e6425449` in 45 commits: 33 changes and 12 merges of
the task branches. `git diff --stat bd7f3f6e..e6425449 -- src` touches 7 files:
`src/sim/` (2: `legion.cpp`, `legion.h`), `src/net/` (3: `protocol.h`,
`client.cpp`, `client.h`), `src/server/` (1: `server.cpp`) and `src/client/`
(1: `gameview_lobby.cpp`). Nothing under `src/ai/`, `src/campaign/`,
`src/tnt/`, `src/cartographer/`, `src/terrain/`, `src/cob/`, `src/crt/` or
`src/util/` changed.

<!-- LEAD: if anything lands after e6425449 (fixes, screenshots, these docs),
     list it here and say whether the checks below still stand for the
     release source. -->
After `e6425449` the preparation added TODO-LEAD (screenshots, the
documentation with the CMake version bump to 0.8.7, the quick-start controls
reference, this report).

The changes:

- **282** Legion at ~100 bodies, round 3 (landing `03a1d3a0`, bump `f86cceb2`;
  `4dbfd41c` passage greens scale with the crossing, `a6d4ce09` groups under 8
  queue at narrow passages, `0addb7a8`, `b766b76d` attack rings round still
  targets, `93eac5c7`, `b7f3a3bb` corridor-mouth funnel claims and own-slot
  arrivals; scenario baseline `30813a73`). Legion only.
- **283** Legion at ~100 bodies, round 4 (bump `e7a44c10`; `43e436f1`,
  `6ec272e6`, `3bffab7a` the turn ribbon, with points over 256 members keeping
  the pinwheel and a blocked file let go after 120 ticks; `c4e5ba39` slot
  wedge; `c6bab4da`, `684370a3`, `b6b11439` rings along walls; `7dadea58`,
  `0de09b45` fight and guard rings round still targets; `da132d1c`, `e56aa0c1`
  room at a merge; scenario baseline and navigation golden `e48b1c12`). Legion
  only.
- **284** lobby seat moves (`4ff11cd0` `Msg::MoveSeat` and MOVE HERE,
  `f5f59995` host slot edits that would seat or unseat a human are dropped,
  bump `e6425449`). The simulation is 283's.

**Retail is expected to be bit-identical to 0.8.6** (282 and 283 are Legion
only; 284 changes no sim state). `--mpai` Inner Circle seed 1 from the landing
commits: Retail 60 s `b353410003e98dd5`, 300 s `a3d953803a064191`; Legion 60 s
`0825aeb9e8139203`, 300 s `ce5c4ba7562e53b9`, all equal to 0.8.6, verified for
300 s on Linux, Windows and macOS.

**Compatibility.** `kNetVersion` is **284** in `src/net/protocol.h` at
`e6425449`, against 281 at `v0.8.6`. `kReplayFormat` is **13** in
`src/net/replayhdr.h`, the map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`), all unchanged.

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion`, so **0.8.7 cannot play with 0.8.6 or earlier in
  either direction**. The server and all players must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.7 does not play 0.8.6 or older replays
  and those builds do not play 0.8.7 replays, although the file format is still
  13. `tools/replay_test.cpp` pins `kNetVersion == 284` and refuses 283, 282
  and 281.
- **Maps and settings.** Saved `.kmp` maps and generator-8/9 recipes carry
  over. A 0.8.6 settings file reads the same in 0.8.7; no settings keys were
  added.

## Local preparation checks

<!-- LEAD: fill from the local-checks run (0.8.6 shape: clean detached
     worktree, toolchains, cores 4-23, owned retail install as --data and
     TAK_TEST_DATA). -->

All checks ran from a clean detached worktree (`TODO-LEAD`) at `main` `TODO-LEAD`
(protocol **284**, replay format **13**, generator recipe **9**, campaign payload 4,
database schema unchanged since 0.8.6). Toolchains: TODO-LEAD.

**Result: TODO-LEAD.**

- **Multiplayer and replays.** `kNetVersion` is **284** in `src/net/protocol.h` (281 in
  0.8.6). Replay format stays 13; the replay loader requires protocol == `kNetVersion`.
- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and AddressSanitizer
  all-target builds: TODO-LEAD (steps, warnings; new warnings since 0.8.6).
- **Full CTest.** TODO-LEAD registered tests (0.8.6: 696 Debug / 658 Release). 20 added
  since 0.8.6: `server_seat_move`, `legion_ribbon`, `legion_slotwedge`,
  `legion_cornermerge`, `legion_passage_green`, `legion_passage_small`,
  `legion_attackflush`, `legion_ringstill`, `legion_ringwait`, `legion_ringclosed`,
  `legion_ringblind`, `legion_ringnosight`, `legion_ringpocket`, `legion_ringshadow`,
  `legion_ringshuffle`, `legion_ringroute`, `legion_ringroutespeed`, `legion_fightstill`,
  `legion_fightshuffle`, `legion_guardstill` (TODO-LEAD: confirm against `ctest -N`).

  | Tree | Selection | Result | Time | Failures |
  | --- | --- | --- | ---: | --- |
  | Release | `-LE nightly` | TODO | TODO | TODO |
  | Release | `-L nightly` | TODO | TODO | TODO |
  | Optimized Debug | `-LE nightly` | TODO | TODO | TODO |
  | Optimized Debug | `-L nightly` | TODO | TODO | TODO |
  | Debug (`-O0`) | `-LE nightly` | TODO | TODO | TODO |
  | Clang Debug | `-LE nightly` | TODO | TODO | TODO |
  | ASan | `-LE nightly` | TODO | TODO | TODO |

  - **The settings-leak guard.** TODO-LEAD (`pref_guard_setup`/`pref_guard_check`; the real
    profile's `settings.ini` SHA-256 and mtime before and after).
- **`--mpai` golden hashes.** Optimized Debug build, Inner Circle, `takserver --local
  --no-auth --seed 1`, an empty `XDG_DATA_HOME`, and `TAK_LEGION=1` for Legion.

  | Mode | Duration | TODO-LEAD (284) | 0.8.6 (281) |
  | --- | ---: | --- | --- |
  | Legion | 300 s | TODO | `ce5c4ba7562e53b9` |
  | Retail | 300 s | TODO | `a3d953803a064191` |
  | Legion | 60 s | TODO | `0825aeb9e8139203` |
  | Retail | 60 s | TODO | `b353410003e98dd5` |

- **Determinism.** `tools/check-determinism.sh`: TODO-LEAD (0.8.6: GCC and Clang x86-64
  O0/O2/O3 agree on `dcef618cd2e4d558`). `tools/check-detmath.sh`: TODO-LEAD.
- **Legion scenario gate.** TODO-LEAD (`legion_scenario --mode both --json --offsets gate`
  over every `tools/scenarios/*.scn`, then `legion_check.py check --cumulative --baseline
  tools/scenarios/baseline.json`). The baseline was retaken for rounds 1-4 (`e48b1c12`),
  so unlike 0.8.6 the gate is expected to pass; 0 Retail keys should fail.
- **Seat move.** TODO-LEAD (`server_seat_move`; an in-client check of MOVE HERE in a
  single-player and a multiplayer room, if done).
- **Research suite and retail archive verifier.** TODO-LEAD.

## Remote multiplayer sweep

<!-- LEAD: fill from tools/desync-hunt-remote.sh, both pathfinding modes;
     referees on tak.pgnet.us + vpn3.pgnet.us with isolated binaries and ports
     (never the public takserver). -->

The full 37-scenario table for both pathfinding modes, Retail and Legion, on
**TODO-LEAD** (UTC), on `main` at `TODO-LEAD` (protocol **284**, replay format 13,
build id `TODO-LEAD`):

- **Retail:** TODO-LEAD.
- **Legion:** TODO-LEAD (`TAK_WALL_TIMEOUT=3600` from the start, as since 0.8.1).

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | TODO/37 | TODO/52 | TODO | TODO |
| Legion | TODO/37 | TODO/52 | TODO | TODO |

Each run's `--validate` gates (build identity, planted desync, override transfer):
TODO-LEAD.

### Reference ticks and hashes (0.8.7 candidate)

TODO-LEAD: the per-case table (Retail and Legion end, tick, hash, units, vs 0.8.6). Retail
is expected to match 0.8.6 case for case except the six `TAK_AUTOPLAY` cases and the
load-sensitive `h-lake`, `h-stress`, `2h-stress`, `2h-gods`, `4h-gods`.

### Comparison with 0.8.6

TODO-LEAD.

## CI and packages

TODO-LEAD: main CI on the final source (Determinism, macOS, Windows, Linux packages);
tag CI run ids (determinism dispatched with `gh workflow run determinism.yml --ref
v0.8.7`); reruns and which known flake; asset count (0.8.6: 29), digests and sizes; build
ids `v0.8.7` in every package; Windows Authenticode (0.8.6: 9 files Valid on oden-win);
macOS notarization and stapling of both DMGs and apps; Opus static and
`OPUS-LICENSE.txt`; microphone entitlement; `NSHighResolutionCapable`; no
libsecret/GLib `NEEDED` on Linux; publication time.

## Screenshots

TODO-LEAD: which gallery and guide pictures were recaptured on the 0.8.7 candidate and how
(0.8.6 capture settings), which were kept, and the captions checked. Candidates:
`title.jpg` (VERSION 0.8.7), `lobby.jpg`/`room.jpg` and the guide's room pictures (the new
MOVE HERE button on open slots), the scenes retaken each release, and the pictures of the
new `docs/quick-start.md`. Then bump the README title alt text, the gallery footnote and
`docs/img/README.md` to 0.8.7.

## Limits

- No game mixing 0.8.6 and 0.8.7 builds was played, and none can be (protocol
  284 against 281, the Hello gate and the replay loader).
- **Legion units still stand and wait more than Retail's**, though they arrive
  sooner (a user ruling: cosmetic).
- **Legion rings along walls and gatehouses** hold fewer attackers within reach
  than Retail's (`attackring-gatehouse-40`, `attackring-wall-64`); next round.
- A few Legion fixtures arrive under 5% later than Retail. TODO-LEAD: name them
  from the scenario gate.
- Carried over: the Legion 10,000 program still does not meet its own success
  criterion for congested workloads, and many-10000, scatter-10000 and
  mixed-10000 still run over the 33.3 ms tick budget in part of their ticks;
  mixed-10000's flyers hover over a slow ground army. Retail `h-cramped` ends
  early because the idle seat starts with only its Monarch. The macOS Retina
  UI Scale is migrated once. A stationary auto-targeting shooter keeps refusing
  a once-blocked target.
- TODO-LEAD: any flake or failure seen in this release's runs.
