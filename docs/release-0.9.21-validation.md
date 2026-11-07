# 0.9.21 release validation

Protocol **315** (0.9.20 used 312; 313 and 314 were never released), replay format
**15** (unchanged), generator version **9** (unchanged).

Tag `v0.9.21` -> `c1523095` (the preparation commit); annotated tag object
`df764b6a`. Published 2026-11-07 09:43:14 UTC as the Latest release.

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

Tag CI on `v0.9.21`, all green at the first attempt: Determinism (sim) 42357645107
(dispatched), Linux packages 42357637762, Windows build 42357637730, macOS build
42357637741 (with its sign and notarize jobs). Main's runs for `c1523095` were cancelled
in favour of the tag's. The last main Windows run before the cut (`eec60b1b`,
42349680590) failed only at the arm64 MSYS2 package download ("Operation too slow"
from the mirror); its macOS and Linux runs passed, as did every workflow on
`c69d446e`.

31 assets, as 0.9.20. All 31 downloaded and match the release's SHA-256 digests.
`TITLE-FONT-LICENSE.txt` is in every package: the ten .debs, six RPMs, the Arch and
Slackware packages, both Linux debug tarballs and the four takclient zips (Windows x64
and arm64, macOS x64 and arm64). The build string is `v0.9.21` in the Arch package's
`takclient` and `takserver` and in the Windows x64 `takclient.exe` (the Arch binaries
need glibc 2.44, newer than the Fedora 44 checking host, so they were read rather than
run).

## Signing

Windows: on oden-win, no `takserver.exe` or `takclient.exe` was running before or after
the check, which ran in `C:/tak/w/rel0921` (removed afterwards). `Get-AuthenticodeSignature`
reports **Valid** (timestamped, "Curtis Edge") for the same 7 files as 0.9.20: the x64
setup program and `takclient`, `takserver` and `cartographer` in both zips. The x64
`takclient.exe` and `takserver.exe` report `0.9.21 (build v0.9.21)`.

macOS: on the MacBook, both DMGs and all eight app copies (two in each DMG, two in each
zip) pass `stapler validate`, and `spctl -a -vv` accepts them as "Notarized Developer
ID"; every app passes `codesign --verify --strict --deep` and has version 0.9.21. The
arm64 zip's `takclient` reports `0.9.21 (build v0.9.21)`. The scratch folder was
removed.

## Publication

The draft was created with the notes (the validation link rewritten to its `blob`
`v0.9.21` URL) before tag CI uploaded into it; published on 2026-11-07 09:43:14 UTC as
the Latest release, 31 assets; 0.9.20 and earlier releases are kept. The notes'
validation link answers 200.

## Known limits

- Only the Linux suite ran locally (the testing tiers); Windows and macOS were covered by
  tag CI.
- No mixed-version game was played: 0.9.21 changes the protocol, so it plays with no
  earlier release by construction (the server's Hello refuses a protocol mismatch).
