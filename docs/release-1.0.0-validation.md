# 1.0.0 release validation

Protocol **323**, replay format **15**, random map generator version **10**.

Tag `v1.0.0` -> `TODO-LEAD` (the version commit); annotated tag object `TODO-LEAD`.
Published TODO-LEAD UTC as the Latest release.

## Scope and compatibility

1.0.0 is main at `bd0cccd6` plus the release preparation: the version commit `368bb3a7`,
the documentation `8f959e33` (guides rewritten, history files removed), the random map
fix `7e18cb94` (below) and `1bb4cdb6` (README status). Its changes since the last
development version, 0.9.24 (tag `v0.9.24` -> `ec50a785`), are listed in the CHANGELOG's
1.0.0 entry.

**Compatibility.** 1.0.0 plays with no earlier version and plays none of their replays
(`replay_test` refuses another protocol). Games on shipped maps hash exactly as in 0.9.24
in every `--mpai` gate (below), at any simulation thread count.

## Local tests on the cut

Linux, on `368bb3a7` (the code of 1.0.0 before the random map fix), five builds, each with
the full ctest (every label, nightly included; 930 tests, 853 in Release):

| Build | Passed | Failures and their cause |
|---|---|---|
| Release | 852 / 853 | `map_cache_readonly`: a test bug, fixed in `7e18cb94` (it looked for the Test Map copy beside `TAK_SETTINGS_FILE`, which a release client does not read); passes against the Release binaries since |
| Debug `-O2 -g` | 929 / 930 | `room_flow` under load; passed alone |
| Debug `-O0` | 916 / 929 | the random map read (below) in 4 `mapgen_*` tests; 7 nightly Legion w3 checks over their 30-minute limit unoptimised (they pass at `-O2`); `streaming` and `room_flow` under load, passed alone |
| Clang, Debug `-O2` | 929 / 930 | `harness_seat_load` under load; passed alone |
| ASan + UBSan | 916 / 929 | the same random map read in 4 `mapgen_*` tests (the only sanitizer report; UBSan reported nothing); the same 7 Legion checks over time; `voice` and `room_flow` under load, passed alone |

**The random map read.** The trees and rocks placed on plateau tops checked their
footprint without a bound, so a footprint near the bottom of the map read past the end
of the map's arrays: an assertion at `-O0`, a heap-buffer-overflow under ASan, undefined
(and harmless in every run) in the optimised builds. It came with the generator's
version 10 in 0.9.24. Fixed in `7e18cb94`: a footprint that runs off the map is refused;
every `mapgen` test passes at `-O0` and `-O2` with the fix and the generator goldens are
unchanged.

- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80` (`build-o2`): every gate keeps
  0.9.24's hash, run twice, and again with the client's SIMULATION THREADS at 1 and at 8
  (the server at `--sim-threads 1` / `8`); the Ulasem Arena gate (`e8b009af2418e33e`) too.

| Gate | 0.9.24 | 1.0.0 `368bb3a7` |
|---|---|---|
| Retail 60 s | `beb81788610d2a6c` | `beb81788610d2a6c` |
| Legion 60 s | `87efa5106c2d3c0e` | `87efa5106c2d3c0e` |
| Retail AI 60 s (`TAK_AI_LEVEL=6`) | `a3ddc1ba59738936` | `a3ddc1ba59738936` |
| Retail 300 s | `30a3e25d3f414fad` | `30a3e25d3f414fad` |
| Legion 300 s | `bd274b29f05f2c18` | `bd274b29f05f2c18` |
| Crusades, Retail 300 s | `23d5e2016eae1bb8` | `23d5e2016eae1bb8` |
| Crusades, Legion 300 s | `ee9c38ff55448ab3` | `ee9c38ff55448ab3` |

- **Determinism**: `tools/check-determinism.sh` OK, every g++ and clang++ build at
  `-O0`/`-O2`/`-O3` agrees on golden `dcef618cd2e4d558` (the aarch64 leg needs a
  toolchain this host lacks); `tools/check-detmath.sh` OK.
- **Legion baseline** (`legion_check.py check`, every scenario in both modes): the 83
  ctest `legion_check_*` cases pass; the full check reports 133 drifted keys in seven
  Legion fixtures (attackring-gatehouse2-48, -gateyard-36, -deadcorridor-30,
  -wall-mixed-56, -gatehouse-40, mixed2, corridor-corner-40). A 0.9.24 `legion_scenario`
  gives byte-identical output for all seven: the deep firing rings of 0.9.24 moved them and
  the baseline was retaken for other fixtures only. Not a 1.0.0 change; see Known limits.
- **Doc links**: every relative link and anchor in the docs resolves; `legion_doc_links`
  and its self-test pass.
- **Pictures** retaken for 1.0.0: the title screen (VERSION 1.0.0), the streaming panel,
  the Options Graphics and User Interface pages, voice chat and the Connect to Server
  panel with NEW SERVER...; the other front-end and room pictures were rendered again and
  matched.

## Remote sweep

`tools/desync-hunt-remote.sh --validate` with the referee on tak.pgnet.us (24 runs, 10 at
a time) and vpn3.pgnet.us (13 runs, 6 at a time), an isolated container-built Debug
`-O2 -g` takserver beside the public one (never touched), 37 runs of 10 simulated minutes
each with seated human players, over plain, latent, lossy and shaped links, in both
pathfinding modes. Each sweep first plants a desync, which was caught.

- **Retail**: 37 of 37, no desync. Two two-player stress runs first ended early when their
  hosting client, on a host loaded by the local builds, fell behind and the server handed
  its seat to an AI (no desync line); both passed alone. `h-stress` hash
  `68b6f1b880ffd4b2` at tick 18,000; `h-cramped` concluded at tick 7,454.
- **Legion**: 37 of 37 in one pass, no desync (52 seats). `h-stress` hash
  `8d8ee8010baffbbd`; `h-cramped` concluded at tick 10,762.

Every test binary, folder, port and traffic shaping was removed from both hosts afterwards.

## Windows and macOS

On `368bb3a7`, built on each host:

- **Windows** (oden-win, MinGW GCC): Debug `-O2` quick suite 616 / 616, Release full suite
  759 / 759 (`streaming`'s local copy and `credstore` skip there: no MP4 muxer in that
  FFmpeg, no password store over SSH). `cpu_topology_test`: 8 performance and 12 efficiency
  cores, pinning works.
- **macOS** (MacBook Neo, Apple Clang): Debug `-O2` quick suite 617 / 618. The failure,
  `streaming_rtmps`, came with macOS 27.2.1, which installed itself during the run: the
  RTMPS upload stalls with a TLS error, and a test binary built from earlier code on
  9 November fails the same way on that machine now (see Known limits).
  `cpu_topology_test`: 2 performance and 4 efficiency cores, pinning not available.
- **Gates** (Retail and Legion, 60 s and 300 s): both hosts give exactly the Linux hashes.

## CI and packages

TODO-LEAD: tag CI on `v1.0.0` (Determinism (sim), Windows build, Linux packages, macOS
build, both Sign + notarize jobs) with run ids and attempts; the asset count, all assets
downloaded and matched against the release's SHA-256 digests; the build string `v1.0.0`
in a Linux package's `takclient` and `takserver`; the new AppImage, openSUSE Tumbleweed
and EL9 / EL10 packages installed and started.

## Signing

Windows: TODO-LEAD (`Get-AuthenticodeSignature` on the x64 setup program and
`takclient`, `takserver` and `cartographer` in both zips; version string).

macOS: TODO-LEAD (`stapler validate`, `spctl -a -vv` and `codesign --verify --strict
--deep` on both DMGs and every app copy; version string).

## Publication

TODO-LEAD: the draft created with the notes (validation link rewritten to its `blob`
`v1.0.0` URL) before tag CI uploaded into it; publication time; asset count; the notes'
links answering 200. Every earlier GitHub release removed after publication, their tags
kept: TODO-LEAD (`gh release list` afterwards, `git ls-remote --tags` still listing every
`v*` tag).

## Known limits

- **RTMPS streaming on macOS 27.** Streaming to YouTube's secure ingest stalls on
  macOS 27.2.1 (the TLS connection drops after a few frames). It is not a 1.0.0 change:
  earlier builds fail the same way on that system. macOS 14 and 15 are unaffected as far
  as CI shows.
- **Legion baseline drift.** The seven fixtures named above carry 133 keys that moved with
  0.9.24's deep firing rings and were never retaken in `tools/scenarios/baseline.json`;
  the ctest Legion checks pass.
- Seven nightly Legion w3 checks take longer than their 30-minute limit in unoptimised
  and sanitizer builds; they pass at `-O2`.
