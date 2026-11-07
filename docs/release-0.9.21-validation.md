# 0.9.21 release validation

**DRAFT.** Scope, compatibility and the local test results on the cut are written;
every part marked **TODO-LEAD** is filled in by the release lead from the remaining
CI, signing and publication runs.

Protocol **315** (0.9.20 used 312; 313 and 314 were never released), replay format
**15** (unchanged), generator version **9** (unchanged).

Tag `v0.9.21` -> **TODO-LEAD** (the preparation commit); annotated tag object
**TODO-LEAD**. Published **TODO-LEAD** UTC as the Latest release.

## Scope and compatibility

0.9.21 follows 0.9.20 (tag `v0.9.20` -> `ed4564d2`). `git log v0.9.20..` holds eight
commits before the preparation commit, all made directly on main:

- `d3d6b55a`: with Shift held, a queued area-order box's corners are lifted onto the
  terrain like the rest of the order overlay (display only).
- `c8082b3a`: Legion corner lanes, protocol 313 (hashed Legion behaviour).
- `c2ba49e8`: Legion formation flyers circle over the front, protocol 314 (hashed Legion
  behaviour).
- `b70463be`: a flyer's squad number, health bar and self-destruct countdown follow its
  altitude (display only).
- `c69d446e`: Options SOUND VOICES and EVEN OUT SOUND LEVELS (client audio only).
- `eec60b1b`: the menu Benchmark runs at game speed +10.
- `b95091e7`: the Benchmark on the server's wall clock (`Cmd::BenchClock`, protocol 315),
  its SPEED row (NORMAL, +10, +100; +100 = `kMaxBenchmarkGameSpeed` 110, benchmark rooms
  only), the Benchmark and Replay pickers held on screen until the game loads, speed
  levels past +10 shown as levels, the SIM readout seeded at the requested speed, and the
  embedded Cinzel Bold font with `TITLE-FONT-LICENSE.txt` in every package (Linux
  tarball and packages, Windows zip and installer, macOS bundle).
- `7c9d9202`: `retailgap_orders` drives the benchmark ramp with the server's clock.

**Compatibility.** Every protocol change is hashed behaviour of Legion pathfinding or of
a Benchmark run; Retail and Legion games without a benchmark keep 0.9.20's hashes (the
gates below). The protocol still changes, so 0.9.21 plays with neither 0.9.20 nor any
earlier version, nor their replays. A 0.9.21 client sends no `BenchClock` (the server's
command parser refuses any kind past `BuildManaArea` other than the three named
player kinds), and a strict replay read accepts it only from protocol 315
(`kBenchClockProtocol`). The stress fill keeps its own tick-scheduled executor; the
benchmark ramp is a second list fired by the clock, so a stress-plus-benchmark world no
longer merges the two.

## Local tests on the cut

Linux, `build-o2` (Debug `-O2 -g`) of `b95091e7`, plus `7c9d9202` for the one test it
fixes:

- **Full ctest** (`ctest -j 16`, every label): 904 of 905 passed on `b95091e7`; the one
  failure, `retailgap_orders` ("the benchmark plan actually spawned an army -- 7 units"),
  was the test ticking a benchmark world without the server's clock. Fixed in
  `7c9d9202`; it passes. Earlier on the same code: `ctest -L quick` 612/612, the
  benchmark, settings, menu, replay, placement and production tests.
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`: all seven gates keep 0.9.20's hashes.

| Gate | 0.9.20 | 0.9.21 `b95091e7` |
|---|---|---|
| Retail 60 s | `beb81788610d2a6c` | `beb81788610d2a6c` |
| Legion 60 s | `87efa5106c2d3c0e` | `87efa5106c2d3c0e` |
| Retail AI 60 s (`TAK_AI_LEVEL=6`) | `a3ddc1ba59738936` | `a3ddc1ba59738936` |
| Retail 300 s | `30a3e25d3f414fad` | `30a3e25d3f414fad` |
| Legion 300 s | `bd274b29f05f2c18` | `bd274b29f05f2c18` |
| Crusades, Retail 300 s | `23d5e2016eae1bb8` | `23d5e2016eae1bb8` |
| Crusades, Legion 300 s | `ee9c38ff55448ab3` | `ee9c38ff55448ab3` |

  Retail 60 s also once at real time (no `TAK_SPEED`): `beb81788610d2a6c`.
- **Legion**: `c8082b3a` and `c2ba49e8` each ran `ctest -L legion` (414/414) and the
  scenario set (193 and 192 of 194 scenarios keep their hash; the changes are the
  corridor-corner and mixed-formation scenarios their commits name).
- **Benchmark**: a headless menu run, Low at +100, ended 60 s after its first tick (68 s
  with loading; the same run took 215 s before 315). Peak live units per player measured
  at Extra Absurd +10 (1,620) and Absurd +100 (614) stay under the 2,000 cap, so every
  intensity stays available at every speed.

Not run for this cut (the testing tiers): the remote desync sweeps, ASan, Clang and the
Windows and macOS local suites; tag CI builds and tests all three platforms.

## CI and packages

**TODO-LEAD**: main and tag CI run ids (Determinism, Linux packages, Windows, macOS),
asset count, digests, build ids (`build v0.9.21`), `TITLE-FONT-LICENSE.txt` in each
package.

## Signing

**TODO-LEAD**: Windows Authenticode, macOS notarization and stapling.

## Publication

**TODO-LEAD**: draft, tag, publish time, links checked.

## Known limits

**TODO-LEAD**: anything accepted at the cut.
