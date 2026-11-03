# 0.9.19 release validation

Protocol **311** (0.9.18 used 309; 310 was never released on its own), replay format
**15** (unchanged), generator version **9** (unchanged).

Tag `v0.9.19` -> `0a6f52dc9d01c4a53bef265bbec6b2e876d08763` (the preparation
commit); annotated tag object `e1e2ff569d7277fcf7d122ef7e467a778289dcf2`.
Published 2026-11-03 19:57:25 UTC as the Latest release.

## Scope and compatibility

0.9.19 follows 0.9.18 (tag `v0.9.18` -> `5fbfbc9f`, validation commit `dc337c0d`) and is
cut from `e8b3ca5b`. Its commits (`git log v0.9.18..e8b3ca5b`, docs and test commits
grouped):

**Legion steering, protocol 310** (Legion only; every Retail hash is unchanged):

- `41fe26a2`: a per-member mechanism tag (`Member::why`) for the scenario observer, which
  attributes statue ticks, sideways and backward steps, reversals and stopped samples to
  it (`why.*` keys). Observation only, never hashed or read by a decision.
- `a06d3f80`: an unturned step keeps travel speed; the facing cap applies only to a step
  that turns the course (no cap on flow-around shuffles, side-steps, short detour routes
  and presses).
- `740f0bd9`: a flow-around shuffle never steps to a neighbour more than 90 degrees
  from its aim.
- `75925618`: a body blocked by bodies is clamped per axis and presses on to its origin
  cell's edge (Retail's clamp) instead of stopping dead; the hold's ways out run as
  before at the edge.
- `8dcb123f`: the press only on legs that engage no enemies, and as an unturned step;
  the in-reach press keeps its facing cap.
- `c4933aa8`: a plain mover's (not a formation member's) flow-around shuffle keeps its
  old speed rule (groupdetour 64/64 settled, crowdtrap spin 0 kept).
- `2913e751`, `d4c4d610`: `kNetVersion` 310 and its comment.
- `6e8c1dd6`: a press blocked by a member walking through its point's origin waits for
  it (`pressWaits`, bounded by `kPressTicks`); a press that ended in reach is no repeat's
  origin in the observer.
- `3b09f132`: a face step whose course just caught up with the body requests no turn
  (`turnReqBam`, display only, never hashed).
- `566fc41a`: the no-backward shuffle rule applies to formation members only
  (bridges-1000 back to 965/0 at offset 0 from 728/184).
- `58d87d96`: the press is for move legs only, never a mission leg with an exact goal
  (Build, Repair, Reclaim, Load, Unload, Park) or a production exit.
- `e95abe0d`, `ad12c080`, `2a68a223`: exact performance work (the window build relaxes
  an interior cell's neighbours through one resolved tile; a per-build corridor-charge
  memo; `policy()` from a compile-time table). Hashes and observer keys byte-identical on
  the 20 identity fixtures, both modes, serial == workers.
- `d2bf9e58`: merge of `origin/main` into the pathfinding branch.
- Tests and goldens: `9a089398`, `65bfe947`, `cc826274` (re-bounded `legion_mixedformation`,
  `legion_cornermerge`, `legion_landedflyers` for the new steering), `f01aeaac` (one
  proving ctest per steering change), `bd6cfae8` (Legion goldens retaken for 310).
- Docs: `48022dd9` (the 2026-11-02 deep dive), `9b0bb45d`, `15cfec65`, `b4389c5e`,
  `9c526669` (the changes as landed, the bridges-1000 numbers, the `why.*` keys).

**Passive AI** (server-side AI only; protocol and hashes unchanged):

- `793510f3`: a Passive AI's strike board keeps only enemies within 800 px of one of its
  buildings, so it defends its base and never strikes at what it saw elsewhere;
  `ai_behavior_test` covers it.

**Legion allies swap places, protocol 311** (Legion only; Retail bit-identical to 310):

- `8ab9c67e`: two members of allied players on move legs blocking each other head on
  (one covering body, routes descending into each other's origins, courses at least 90
  degrees apart, equal footprints, one plane, neither settled, engaged, trapped,
  approaching, livelocked, pressing in or walking a route) trade origins in one exact
  integer step, tried first in `drive`'s blocked branch; a body that swapped waits 45
  ticks (`kSwapCool`). `swapTicks` are hashed and carried by `World::clone`.
  bridges-1000 at offsets 0, ±1, ±2, ±3: median arrived/stuck 741/182 on main ->
  983/0; maze-1000 950/0 as main; acceptance worlds identical.
- `5e220572`: `ctest:legion_swap` (bridges-1000 at offsets -2, -1, +1, +2; fails on main
  at all four).
- `9120e251`: `kNetVersion` 311.
- `76679bfb`: docs (round 2 as landed, the walk-in-place correction).
- `65e29265`: Legion goldens retaken for 311.

**Client and CI:**

- `aba8cf78`: the client draws an ally swap as an 8-tick pass, sidestepping each body to
  the right of its jump (`src/client/swapshow.h`, `ctest:swapshow`). Display only:
  `World::legionSwapTick` is a read-only query; nothing hashed.
- `e8b3ca5b`: the Slackware CI job tries slackware.osuosl.org,
  mirrors.ocf.berkeley.edu and mirrors.kernel.org in turn.
- `dc337c0d`: 0.9.18's validation report (docs only).

**Compatibility.** `kNetVersion` is **311**, so 0.9.19 plays with neither 0.9.18 nor any
earlier version, in either direction, nor their replays (309 and 310 replays are
refused). Nothing on the wire changes. Replay format stays **15** and the random map
generator stays version **9** (`src/net/replayhdr.h`, `src/tnt` and
`docs/random-map-generation.md` are unchanged since `v0.9.18`). Retail pathfinding is
bit-identical to 0.9.18: the Retail `--mpai` gates below are expected to equal
0.9.18's, and the Legion ones to change.

## Full test run

Run against `e8b3ca5b` (origin/main, the code of the release) on 2026-11-03; logs in
`~/tak-tmp/scratch/release-0919/{local,sweeps,platforms}/`. All binaries in this run
report `0.9.18 (build v0.9.18-35-ge8b3ca5be42f)`: the right commit, before the version
bump.

- **Local desync sweep** (`tools/desync-hunt.sh --minutes 20 --jobs 8`, Debug `-O2 -g`,
  cores 16-23, 27 min 55 s): 34 cases, 32 comparing real hashes and 2 flow-only
  (`flow-bench-high`, `flow-bench-absurd`); 969,857 ticks; 19 games won, 15 at the
  36,000-tick limit; largest end state 5,315 units (`stress`). "No desyncs in any run",
  nothing unclean or missing, every run `err=none`, and no DESYNCED, REFEREE SUSPECT,
  crash, assert or bind-failure line in the 68 logs. All 34 final hashes are identical
  to 0.9.18's, as expected: the sweep has no Legion case and no Passive AI seat, which is
  where 0.9.19's sim changes are.
- **Remote multiplayer sweep** (`tools/desync-hunt-remote.sh --validate --minutes 10`,
  `TAK_WALL_TIMEOUT=3600`). Referees on `tak.pgnet.us` (10 at a time) and
  `vpn3.pgnet.us` (6 at a time) ran a sweep-only `~/takserver.sweep1103`, built in the
  `tak-sweep-builder-0719` Ubuntu 24.04 image (Debug `-O2 -g`, at most GLIBC_2.38,
  SHA-256 `8a18c5f29998d9ea0e42f8c2509bb45b7bafb6d60a72cfbd0d4c6dfa570c3f55`); clients
  were a local Debug `-O2 -g` build on cores 16-23. All three `--validate` gates passed
  in both modes ("both at v0.9.18-35-ge8b3ca5be42f", the planted "client 1 DESYNCED at
  tick 900" detected, game mods transferred and the wrong loaded gameplay rejected).

| Mode | Cases / seats | Ticks | Client-pair checkpoints | Client-referee checkpoints |
|---|---|---|---|---|
| Retail | 37/37, 52/52, "no desyncs" (2 cases rerun, below) | 631,384 (10 games won) | 11,129 across 22 seat pairs, 0 mismatched | 2,806 across 51 comparisons, 0 mismatched |
| Legion | 37/37, 52/52, "no desyncs" (2 cases rerun, below) | 637,602 (9 games won) | 12,233 across 22 seat pairs, 0 mismatched | 2,867 across 51 comparisons, 0 mismatched |

  In the first pass, `2h-stress` and `2h-orders-stress` (Ulasem Arena, `TAK_STRESS`, two
  humans) ended in both modes with "the wait ran out: an AI took over your seat": the
  local host seat, at about 13,400-14,100 units, held the game 15-23 times and used up
  its 30 s wait. The second seat and the referee played on to 18,000 ticks and all their
  checkpoints, the host's ticks before the takeover included, agreed. The cause was local
  CPU starvation (8 client cores instead of 0.9.18's 12, with another ctest run on cores
  4-15; load about 27). Rerun alone (`--only 2h-stress,2h-orders-stress`, each mode), all
  4 cases and 8 seats ran to 18,000 ticks with no desync, no "held the game" line and 0
  mismatched checkpoints; the Retail `2h-stress` hash `6c93e34b295be6a4` is identical to
  0.9.18's. The table counts the 35 first-pass cases plus the 2 rerun ones per mode.

  The only DESYNCED line is the planted one in `validate.server.log`, and the only
  mismatch line is the expected game-mods reject. All 78 referee replay headers (74
  first-pass, 4 rerun) read format 15, protocol 311, engine 0.9.18 and the right mode.
  Everything the sweep put on either host was removed afterwards. The public `takserver`
  on `tak.pgnet.us` (PID 17919) was the same process before and after and was never
  contacted.
- **Full local suite** (Debug `-O2 -g`, GCC, nightly label included, `-j12`): 857 of 857
  passed in 948 s; no compiler warning. The Python suites ctest does not run (all 23
  `tools/re/*_test.py`, `acme_ari`, `acme_recovery`) passed; `acme_pebble_test.py` was
  not run (no Pebble here).
- **Legion scenario gate** (`legion_check.py validate --baseline
  tools/scenarios/baseline.json`): PASS, 0 problems.
- **Detmath guard** and **cross-compiler golden** (`tools/check-detmath.sh`,
  `tools/check-determinism.sh`): OK; "DETERMINISM OK", golden `dcef618cd2e4d558` for
  g++ and clang++ at -O0, -O2 and -O3 (aarch64 legs skipped, no toolchain).
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`, twice, and the Retail 60 s gate
  once at 1x: all equal the expected values. The Retail ones equal 0.9.18's; the Legion
  300 s ones changed with protocols 310 and 311.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `8690eaee7b16b4e6` | `269f3da1835a17ea` |
| Legion | `b558575535124afc` | `f8931be77508f851` |
| Retail AI (`TAK_AI_LEVEL=6`) | `12003fba552b8696` | not run |
| Crusades, Retail pathfinding | not run | `6f8d58fdcd304fc7` |
| Crusades, Legion | not run | `c036ee3dcc43e84e` |

- **Mac** (MacBook, arm64, Apple Clang, `build-o2`): `detmath`, `fp_contract`, `fixed`
  and `navigation_determinism` passed, and both 300 s `--mpai` games matched Linux
  (`269f3da1835a17ea`, `f8931be77508f851`).
- **Windows** (oden-win, MinGW GCC 16, `tools/wintest.sh`): both 300 s `--mpai` games
  matched Linux. Both builds passed their suites with no failures (Release 693 of 693,
  `-O2` 757 of 757, 2 skipped in each). None of 0.9.18's Windows failures fixed in
  `18d97759` came back, and no server was left behind.
- **Release and Clang builds** (Release; Clang Debug `-O2 -g`): both build with no
  compiler warning. Release `ctest -L quick`: 533 of 533. Clang `ctest -L quick`: 563
  of 563.
- **ASan + UBSan** (Debug `-O1 -g -fsanitize=address,undefined`,
  `ASAN_OPTIONS=detect_leaks=0`, full suite without the nightly label): 678 of 678 passed
  in 55.5 minutes, and no AddressSanitizer report appeared. The build's 10 warnings are
  `-Wmaybe-uninitialized` inside libstdc++'s `<regex>`/`std::function` headers in one
  test file (`tools/mod_transfer_test.cpp`), as at 0.9.18; no engine source warns.
  `LastTest.log` holds 8 UBSan "signed integer overflow" reports, all in
  `src/sim/retailflight.h` (`retailFlightNavigation` lines 196-198,
  `retailFlightVelocity` lines 233-234), reached through `World::tickAirToAir` in
  `air_to_air` (6) and `retailgap` (2): int32 subtractions of 16.16 positions near
  ±2.1e9, apparently a far-off intercept destination. Both tests pass, and the reports
  reproduce exactly when rerun alone. None of that code changed since 0.9.18 (0.9.18's
  report read only the ctest output, which shows failing tests). See Limits.

## On the release commit

On `db6457a8` (the preparation tree, 0.9.19 version line), on Linux: Debug `-O2 -g` and
Release both built with no compiler warning and report `0.9.19`; `ctest -L quick` on
the `-O2` build passed 563 of 563; the seven `--mpai` gates above repeated exactly; and
`tools/check-determinism.sh` reported "DETERMINISM OK" with golden `dcef618cd2e4d558`.

## CI and packages

Main CI on `0a6f52dc` (pushed as a fast-forward of `e8b3ca5b`) passed on attempt 1:
Determinism 41697487708, Linux packages 41697487766, Windows 41697487601 and macOS
41697487802.

All four tag workflows ran on `0a6f52dc` (tag `v0.9.19`) on 2026-11-03 and passed on
attempt 1: Determinism 41702270178 (dispatched; golden `dcef618cd2e4d558`, "DETERMINISM
OK"), Linux packages 41702258339, Windows 41702258204 and macOS 41702258437 (both signing
jobs passed; no notary stall). Compiler warnings: the Windows job's 8 `'NOMINMAX'
redefined` lines (`src/cartographer/document.cpp`, `playtest.cpp`) are the same as in
0.9.18's tag run, and the macOS job has no compiler warning (its `ld` "no platform load
command" lines for libjpeg's assembly objects are as in 0.9.18). The Linux job has one
new warning, on GCC 13 and 14 only (Debian 13 and Ubuntu 24.04, x64 and arm64):
`-Walloc-size-larger-than=` at `src/sim/legion.cpp:6122`, the `new uint8_t[cells]` of
the corridor-charge memo added in `ad12c080` (see Limits).

All 29 assets were downloaded fresh and match GitHub's SHA-256 digests and sizes, and
the asset names are 0.9.18's with the version changed. All 30 extracted x86-64 Linux
binaries carry the `v0.9.19` build string and no other `v0.x.y` string; none has a
libva, libdrm, libsecret, GLib or libsqlite `NEEDED` entry or embedded VA or SQLite
code; the 27 release binaries hold no forbidden `TAK_` name, and every binary's `TAK_`
name set equals 0.9.18's. Each reports `0.9.19 (build v0.9.19)` from scratch profile
dirs and leaves no files, except the Arch `takclient`, which needs GLIBC 2.44 and
cannot run on this Fedora host, as before.

The Fedora 44 x86-64 RPM's `takserver` (protocol v311, retail gameplay hash
`d6000f4fa9095a51`) ran the Retail AI game with the 0.9.19 Debug client (the local
Debug `-O2 -g` build of `db6457a8`, the preparation tree, with the 0.9.19 version line;
`TAK_AI_LEVEL=6 TAK_SPEED=80 --mpai --time 60`): `hash=12003fba552b8696 units=4
err=none`, with no desync, mismatch or referee-suspect line. **Does not play with
0.9.18:** the 0.9.18 Fedora 44 RPM's `takserver` (protocol v309) refused the same client
with "protocol version mismatch (server 309, client 311) -- update your build".

## Signing

Windows: on oden-win, no `takserver.exe` or `takclient.exe` was running before or after
the check, which ran in `C:/tak/w/rel0919`. `Get-AuthenticodeSignature` reports
**Valid** (timestamped, "Curtis Edge") for the same 7 files as 0.9.18 (the x64 setup
program and `takclient`, `takserver` and `cartographer` in both zips); the x64
`takclient.exe` and `takserver.exe` report `0.9.19 (build v0.9.19)` and the arm64 ones
carry the build string. The real profile's listing (384 lines) is identical before and
after, and the scratch copy was removed.

macOS: on the MacBook, both DMGs and all eight app copies pass `stapler validate` and
`spctl -a -vv` accepts them as "Notarized Developer ID" (Curtis Edge, L479Y59RN8); each
app passes `codesign --verify --strict --deep` and has version 0.9.19 and
`LSMinimumSystemVersion` 14.0; `takclient` and `takserver` report 0.9.19 and every
Mach-O binary reports `minos 14.0`. The bare zip programs pass `codesign`; `spctl`
calls them "not an app", as expected. The real profile listing (121 lines) was identical
before and after, and the scratch folder was removed.

## Publication

Published on 2026-11-03 19:57:25 UTC as the Latest release, 29 assets; 0.9.18 and
earlier releases are kept. Every `blob` `v0.9.19` link in the notes names a file at
the tag.

## Limits

- The full suite, sweeps and Mac and Windows runs were against `e8b3ca5b`; the
  preparation commit changes only the version line and docs, and was checked by the
  builds, `ctest -L quick`, the gates and the golden above, and by CI.
- The remote sweep's two 2-human stress cases needed one `--only` rerun in each mode
  because the local host seat was starved of CPU (see Full test run); not a desync.
- UBSan's signed-overflow reports in `src/sim/retailflight.h` (pre-existing, unchanged
  since 0.9.18) are undefined behaviour in hashed sim code that in practice wraps; the
  golden and gates hold on g++ and clang++. Follow-up: do those subtractions in int64 or
  as wrapping uint32.
- GCC 13/14's `-Walloc-size-larger-than=` at `src/sim/legion.cpp:6122` (Linux package
  CI only) is new in 0.9.19; it was not seen locally (GCC 16) or on the other CI
  compilers. Follow-up: give `cells` a type or bound GCC can see.
- All of 0.9.18's limits carry over (see
  [release-0.9.18-validation.md](release-0.9.18-validation.md#limits)).
