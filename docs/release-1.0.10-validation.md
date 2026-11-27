# 1.0.10 release validation

Protocol **337**, replay format **15**, random map generator version **10**.

Tag `v1.0.10` -> `0f4e2a11` (the version commit after `870cd097`); annotated tag object
`bd781aeb`. Published 2026-11-27 21:52 UTC as the Latest release; 1.0.0 to 1.0.9 stay GitHub releases.

## Scope and compatibility

1.0.10 is 1.0.9 (tag `v1.0.9` -> `a2641214`, validation commit `9d390d20`) plus three changes
ported on 2026-11-27 from ta-engine, a sister project that began as a copy of this code on
2026-11-23 (no shared git history; the Legion files differ only by renamed identifiers). They
are listed in the CHANGELOG's 1.0.10 entry:

| Change | Game state |
|---|---|
| Legion: a deep ring's spot hand-out shares its walks' cell classes and sight tests, tries candidates nearest first and stops a walk once settled; ring builds and scans skip work whose answer they do not read; sight and shot tests are memoised across a ring's members and the groups ringing one still target (`9c95aed4`, ta-engine `0e29ce57` + `97f43f16`) | none: the same answers |
| A factory guard's step aside that is held (Retail refusal streak >= 2, Legion `kHeldStill`) ends once no output is leaving the door and no site is refused (`f0d332e3`, ta-engine `e0818895`) | hashed sim (both modes) |
| `World::resetForMatch`: the client builds every match and replay in a fresh World, keeping only its thread setup (`870cd097`, ta-engine `00e6b0f`; test `match_rebuild`) | none for a correct setup |

ta-engine's third ring change (`5af304a5`, each ring group's upkeep scan on its own tick) was
not ported: it changes results, and on this engine's fixtures it worsened 29 outcome keys (for
example `attackring-struct-60` `open_still900` 37 -> 60).

**Compatibility.** `kNetVersion` is 337 (`f0d332e3`): 1.0.10 does not play with 1.0.9 or
earlier, nor play their replays. The replay format stays 15 and the random map recipe version
10, so random maps and saved map files are unchanged.

## Measurements

- **Ring speedups.** Exact on this engine: with them, all 22 `legion_check_` baseline tests of
  the ring, chase, fight, goalblock, gate and wall fixtures pass with every hash unchanged at
  every gate offset, and the gates below are unchanged. ta-engine measured its Benchmark
  (High, Legion, +100): worst sim tick 1,325 -> 47 ms, ticks over 15 ms 433 -> 10. This
  release has no headless per-tick timing of its own, so that speedup was not measured here.
- **Factory guards.** `landdoor_exit` passes; the gates below are unchanged.
- **Fresh World.** `match_rebuild`: a World set up on Athri Cay (the lobby's launch map) and
  reset builds the same Two Castles match as a fresh one, with the same state hash after setup
  and after ten seconds. No leak of launch-map state is known in this engine; the test guards
  against one.

## Local tests

Linux, Debug `-O2 -g` (`build-o2`):

- **Quick suite** on `870cd097`: 692 of 692 passed.
- **`tools/check-determinism.sh`**: the six x86_64 builds agree on golden `dcef618cd2e4d558`
  (the aarch64 legs skip here).
- **`--mpai` gates** on the version commit's code, Inner Circle (Ulasem Arena for the last),
  seed 1, `TAK_SPEED=80`, all eight the same as 1.0.9's:

  | Gate | 1.0.9 | 1.0.10 |
  |---|---|---|
  | Retail 60 s | `beb81788610d2a6c` | `beb81788610d2a6c` |
  | Legion 60 s | `87efa5106c2d3c0e` | `87efa5106c2d3c0e` |
  | Retail AI 60 s | `a3ddc1ba59738936` | `a3ddc1ba59738936` |
  | Retail 300 s | `6ef0b1fbbaa61815` | `6ef0b1fbbaa61815` |
  | Legion 300 s | `8ac4b5887eefea82` | `8ac4b5887eefea82` |
  | Crusades Retail 300 s | `4da74d1650f8c6df` | `4da74d1650f8c6df` |
  | Crusades Legion 300 s | `7fb55681ed620dee` | `7fb55681ed620dee` |
  | Ulasem | `0742b016af71e1ba` | `0742b016af71e1ba` |

Picture retaken with the 1.0.10 build: `docs/img/title.jpg` (VERSION 1.0.10).

## Not run for this release

The full suite, the desync sweeps and the Windows and macOS test runs. Tag CI runs the
determinism workflow and builds every package.

## Tag CI and packages

Main's CI runs for `870cd097` and `0f4e2a11` were cancelled: tag CI builds the same code.
Tag CI on `v1.0.10` (`0f4e2a11`), all green at the first attempt: Determinism (sim)
46066487032 (dispatched), Linux packages 46066477638, Windows build 46066477589, macOS build
46066477580.

39 assets, the same set as 1.0.9's. All 39 were downloaded and match the release's SHA-256
digests and sizes. `takclient` and `takserver` from the Ubuntu 24.04 x64 package report
`1.0.10 (build v1.0.10)`, its `takclient` links no libsecret or GLib, and the x86_64 AppImage
reports the same version.
Windows: oden-win was not reachable under its name, and the machine answering at its usual
address showed a different SSH host key, which was not overridden, so `Get-AuthenticodeSignature`
was not run. Tag CI's signing step signed every executable and the installer and verified 13
signatures with 0 errors, and each `.exe` in both zips and the x64 setup program carries a
15,616-byte certificate table.
macOS: on the MacBook, both DMGs and both apps in each pass `stapler validate`, and `spctl`
accepts all of them as Notarized Developer ID; the arm64 `takclient` reports
`1.0.10 (build v1.0.10)`.
Every link in the notes answers 200.
