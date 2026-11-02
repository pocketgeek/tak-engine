# 0.9.18 release validation

Protocol **309** (unchanged from 0.9.17), replay format **15** (unchanged), generator
version **9** (unchanged).

## Scope and compatibility

0.9.18 follows 0.9.17 (tag `v0.9.17` -> `13e0a3a6`, validation commit `3c84ba67`). Its
code commits:

- `ca696ca0`, `078b27bc`, `c2e83279`, `1e0c8219` and `74758ccd`: every compiler warning
  on every platform silenced or fixed (missing-field-initializers off; `tak::u8path`
  for the C++20-deprecated `std::filesystem::u8path`; GCC false reports suppressed at
  their sites or on GCC 11/12; small no-behaviour fixes; the TLS context holders under
  a mutex; PBKDF2 refuses a salt within 4 bytes of `SIZE_MAX`).
- `18d97759`: on Windows the local server inherits the client's standard handles and
  runs in a kill-on-close job; `map_cache_readonly` skips its `--play-map` half for a
  Release client on Windows and macOS; `server_spectators` waits for the second seat's
  ready before StartGame; the protocol 307 `legion_check_*` blocks get the `NOT WIN32`
  guard.
- `1489a1fe`: `replaybar_test` keeps its counted allocations live under Clang -O2.
- `a34041f6`: `disconnect_client_test` plays its replay back in the recording player's
  profile, which holds the verified game mods.
- `fcc3df0e`: the room's slot-number buffer fits any `int` (GCC `-O1` warning).
- `729d7801`: `streaming_pacing` is registered only in optimized builds.

The changes in `src/sim`, `src/ai` and `src/tnt` change no behaviour: unused parameters
and an unused count, plain copies in range loops, explicit `int` casts on a branch-free
test, value-initialised copies of empty members, and pragmas round three sorts. The
gates and the golden hash confirm it.

**Compatibility.** `kNetVersion` stays **309**, so 0.9.18 plays with 0.9.17 and 0.9.16
in either direction and plays their replays.

## Full test run

Run against `74758ccd`, the last warning commit, on 2026-11-02 (workflow
`wf_3e78005b-cd0`; logs in `~/tak-tmp/scratch/fullrun-1102/`), except where noted.

- **Local desync sweep** (`tools/desync-hunt.sh --minutes 20 --jobs 8`, Debug `-O2 -g`,
  cores 16-23, 27 min 50 s): 34 cases, 32 comparing real hashes and 2 flow-only
  (`flow-bench-high`, `flow-bench-absurd`); 969,857 ticks; 19 games won, 15 at the
  36,000-tick limit; largest end state 5,315 units (`stress`). "No desyncs in any run",
  nothing unclean or missing, every run `err=none`, and no DESYNCED, REFEREE SUSPECT,
  crash or assert line in the 68 logs.
- **Remote multiplayer sweep** (`tools/desync-hunt-remote.sh --validate --minutes 10`,
  `TAK_WALL_TIMEOUT=3600`, as in 0.9.6). Referees on `tak.pgnet.us` (10 at a time) and
  `vpn3.pgnet.us` (6 at a time) ran a sweep-only `~/takserver.sweep1102`, built in the
  `tak-sweep-builder-0719` Ubuntu 24.04 image (Debug `-O2 -g`, at most GLIBC_2.38,
  SHA-256 `fac125503d9390df0e4e312e30509751cd85345458b277d5afa4574f514239d6`); clients
  were a local Debug `-O2 -g` build on cores 4-15. All three `--validate` gates passed
  in both modes.

| Mode | Cases / seats | Ticks | Client-pair checkpoints | Client-referee checkpoints |
|---|---|---|---|---|
| Retail | 37/37, 52/52, "no desyncs reported" | 628,491 (11 games won) | 11,066 across 22 seat pairs, 0 mismatched | 2,794 across 51 comparisons, 0 mismatched |
| Legion | 37/37, 52/52, "no desyncs reported" | 637,374 (8 games won) | 12,444 across 22 seat pairs, 0 mismatched | 2,889 across 51 comparisons, 0 mismatched |

  The only DESYNCED line is the planted one in `validate.server.log`; no seat was lost
  and no "slot N held the game" line appeared (0.9.6 had some). All 74 referee replay
  headers read format 15, protocol 309, engine 0.9.17 and the right mode. Everything the
  sweep put on either host was removed afterwards. The public `takserver` on
  `tak.pgnet.us` (PID 13783) was the same process before and after and was never
  contacted.
- **Full local suite** (`build-o2`, Debug `-O2 -g`, nightly label included, `-j12`):
  852 of 852 passed in 851 s, `vashim_unavailable` and `vashim_system_loader`
  included. The Python suites ctest does not run (`tools/re/*_test.py`, `acme_ari`,
  `acme_recovery`) passed; `acme_pebble_test.py` was not run (no Pebble here).
- **Legion scenario gate** (`legion_check.py validate --baseline
  tools/scenarios/baseline.json`): PASS, 0 problems.
- **Detmath guard** and **cross-compiler golden** (`tools/check-detmath.sh`,
  `tools/check-determinism.sh`): OK; "DETERMINISM OK", golden `dcef618cd2e4d558` for
  g++ and clang++ at -O0, -O2 and -O3 (aarch64 legs skipped, no toolchain).
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`, twice, and the Retail 60 s gate
  once at 1x: all equal 0.9.17's.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `8690eaee7b16b4e6` | `269f3da1835a17ea` |
| Legion | `b558575535124afc` | `a860707b7b4113e6` |
| Retail AI (`TAK_AI_LEVEL=6`) | `12003fba552b8696` | not run |
| Crusades, Retail pathfinding | not run | `6f8d58fdcd304fc7` |
| Crusades, Legion | not run | `4db379ba59111b0c` |

- **Mac** (MacBook, arm64, Apple Clang, `build-o2`): `detmath`, `fp_contract`, `fixed`
  and `navigation_determinism` passed, and both 300 s `--mpai` games matched Linux
  (`a860707b7b4113e6`, `269f3da1835a17ea`).
- **Windows** (oden-win, MinGW GCC 16, `tools/wintest.sh`): both 300 s `--mpai` games
  matched Linux. The suites had 17 failures in each build (Release 686/704, `-O2`
  751/768), each repeating when rerun alone. All four causes were in tests or in the
  Windows local-server launch, and are fixed in `18d97759`:
  - 16 `legion_check_*` (the protocol 307 melee and air-stack blocks) were registered
    on Windows, where `legion_scenario`'s gate mode deliberately exits 2;
  - `map_cache_readonly`: on Windows the client started its local server with no
    handles, so the server's "map verified" never reached the client's log; and a
    Release `takclient` off Linux uses the real profile, so the Release variant wrote
    the Windows player's `settings.ini` (its data folder) and `MapCache` (left for the
    owner to fix);
  - `server_spectators` (Release, about one run in three): the host's StartGame could
    reach the server before the second player's ready, which the server ignores
    without a reply;
  - killed clients left their local servers running, which also locked `takserver.exe`
    against the next build.

  With `18d97759` on oden-win: `local_server`, `map_cache_readonly` (its full
  `--play-map` half in `-O2`) and `server_spectators` pass in both builds, the fixed
  `server_spectators` passed 15 of 15 runs on Release, and no server was left behind.
- **Release and Clang builds** (Release; Clang Debug `-O2 -g`): both build with no
  compiler warning. Release `ctest -L quick`: 524 of 524. Clang `ctest -L quick`: 553
  of 554; `replaybar` failed (also at 0.9.17) because Clang removed the test's unused
  allocations; fixed in `1489a1fe` and passing with Clang and GCC.
- **ASan + UBSan** (Debug `-O1 -g -fsanitize=address,undefined`,
  `ASAN_OPTIONS=detect_leaks=0`). A first run of the full suite, nightly label included,
  was stopped at 435 of 848 after 80 minutes: the long Legion nightly scenarios ran into
  their 30-minute limits under the sanitizers (8 timeouts by then), and they had passed
  in the normal full suite. A second run on `1489a1fe` without the nightly label: 668
  of 670 passed in 78 minutes, and no AddressSanitizer or UBSan report appeared in any
  test. The two failures:
  - `disconnect_client`: its replay check played the replay in a fresh profile without
    the recording player's cache of verified game mods, so it failed whenever the data
    folder has game mods (this build used `~/tak_data`, which does; `build-o2` uses
    `assets/game`, which does not). Fixed in `a34041f6`; passes with both folders.
  - `streaming_pacing`: real-time 60 fps encoding of a 7680 x 2160 capture dropped 74 of
    105 frames under the sanitizers; it passes in optimized builds and is no longer
    registered in slow builds (`729d7801`).
  The build's one warning (`-Wformat-truncation` in the room's slot number at `-O1`) is
  fixed in `fcc3df0e`.
- **CI on `1489a1fe`**: Linux packages, Windows and macOS all passed, with no compiler
  warning on any platform.

## On the release commit

On `729d7801` with the 0.9.18 version line (the preparation tree), on Linux: `build-o2`,
Release and Debug all built with no compiler warning; `ctest -L quick` on `build-o2`
passed 558 of 558; the seven `--mpai` gates above repeated exactly; and
`tools/check-determinism.sh` reported "DETERMINISM OK" with golden `dcef618cd2e4d558`.

## Limits

- The full suite, sweeps and Mac and Windows runs were against `74758ccd`; `18d97759`
  and `1489a1fe` (Windows launch code and three tests) were checked by their own tests
  on Windows and Linux, CI, and the ASan run.
- All of 0.9.17's limits carry over (see
  [release-0.9.17-validation.md](release-0.9.17-validation.md#limits)).
