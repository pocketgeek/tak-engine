# 1.0.3 release validation

Protocol **325**, replay format **15**, random map generator version **10**.

Tag `v1.0.3` -> `0a9221d7` (the release preparation commit); annotated tag object `bbd63176`.
Published 2026-11-12 20:25:49 UTC as the Latest release; 1.0.0, 1.0.1 and 1.0.2 stay GitHub
releases.

## Scope and compatibility

1.0.3 is 1.0.2 (tag `v1.0.2` -> `81e208af`) plus six changes, listed in the CHANGELOG's
1.0.3 entry and described in `docs/retail-engine.md` ("Ours, not retail: no endless
shots that cannot land" and "a landed flyer on Hold Position lifts to fight"):

| Commit | Change | Game state |
|---|---|---|
| `6cf40d4a` | Switchers refuse swimmers; storms over water run on the surface; a SweetSpot outside the footprint is moved over the body's centre; a shot that leaves a still target unhurt or runs out counts as blocked, and a mover with nowhere better gives the target up | simulation |
| `7ac23beb` | A switcher takes an order only on its selected weapon; stone and freeze refuse targets their callback refuses; charm refuses buildings | simulation |
| `6ef229ac` | A computer's weapon switch picks a weapon that serves its target; a landed flyer on Hold Position that cannot fight from the ground lifts over its post | simulation |
| `b865e008` | That lift also when the flyer's ground shots are written off (Iron Beak) | simulation |
| `d2866473` | Custom music plays only in games; the title screen and lobby keep the original | client only |
| `12d26205` | Units back out of their minimum range; a boat's clear-shot chord starts at its own height; the lift after 45 ticks without a release; the Legion lift counts reloads down | simulation |

**Compatibility.** The simulation changed, so `kNetVersion` is 325: 1.0.3 does not play
with 1.0.2 or earlier, nor play their replays.

## Local tests

Linux, Debug `-O2 -g` (`build-o2`):

- **Quick suite** (`ctest -L quick`): 629 / 629.
- **Legion suite** (`ctest -L legion`): 414 tests; `legion_check_attackring-river-64`'s
  final hashes moved with these attack changes and were retaken
  (`tools/legion_check.py baseline --hashes`), and its 23 improved metrics ratcheted
  (bodies that reach the ring 16 -> 21, trapped 1 -> 0). It then passes.
- **The changes' own tests:** `naval_combat` and `legion_naval_combat` (190 cases each),
  `hold_flyer`, `flyer_combat`, `seadragon_hold`, `shot_range`, `legion_shot_range`,
  `los_status_flight_roster`, `sweetspot`, `music_decode`.
- **`--mpai` gates**, Inner Circle, seed 1, `TAK_SPEED=80`, all unchanged from 1.0.2:

  | Gate | Hash |
  |---|---|
  | Retail 60 s | `beb81788610d2a6c` |
  | Legion 60 s | `87efa5106c2d3c0e` |
  | Retail AI 60 s | `a3ddc1ba59738936` |
  | Retail 300 s | `30a3e25d3f414fad` |
  | Legion 300 s | `bd274b29f05f2c18` |
  | Crusades Retail 300 s | `23d5e2016eae1bb8` |
  | Crusades Legion 300 s | `ee9c38ff55448ab3` |
  | Ulasem | `e8b009af2418e33e` |

- **`tools/check-determinism.sh`**: every build agrees on golden `dcef618cd2e4d558`
  (gcc and clang, -O0 to -O3; the aarch64 legs skipped, no toolchain here).
- **Unit sweeps** (scratch programs, not in the repository): every ranged attacker
  against every unit that can be in water, in deep water, shallows and on a shore,
  both balances (about 17,700 pairs), 40 s each: no attacker holds an attack for the
  whole run without hurting its target. Every armed flyer landed on Hold Position
  against an enemy at eight bearings; every ranged ship against shore targets on
  three slopes; every unit with a minimum range ordered onto a target inside it.

Picture retaken with the 1.0.3 build: `docs/img/title.jpg` (VERSION 1.0.3).

## Not run for this release

The full suite, the desync sweeps and the Windows and macOS test runs. Tag CI runs the
determinism workflow and builds every package.

## Tag CI and packages

Main's CI on the last code commit (`12d26205`) was green before tagging. Tag CI on
`v1.0.3`: Determinism (sim) 43289879097 (dispatched), macOS build 43289821344 (both Sign +
notarize jobs included) and Windows build 43289821198 green at the first attempt; Linux
packages 43289821496 green after one rerun of its EL10 x64 job, whose first attempt failed
on an AlmaLinux mirror (the CRB repository's `repomd.xml` did not parse), not on the build.
Main's runs on the preparation commit were cancelled in favour of the tag's.

39 assets, the same set as 1.0.2's. All 39 were downloaded and match the release's
SHA-256 digests. `takclient` and `takserver` from the x86_64 AppImage and the Fedora 44
package report `1.0.3 (build v1.0.3)`.
Windows: on oden-win (scratch `C:/tak/w/rel103sig`, removed afterwards),
`Get-AuthenticodeSignature` reports **Valid** (timestamped, "Curtis Edge") for the x64
setup program and `takclient`, `takserver` and `cartographer` in both zips; the x64
`takclient.exe` and `takserver.exe` report `1.0.3 (build v1.0.3)`.
macOS: **the MacBook was offline** when the packages were ready, so the DMGs and apps were
not checked on a Mac. The Sign + notarize jobs' own logs show, for each architecture, the
DMG and both apps accepted by `spctl` as "Notarized Developer ID" and "The staple and
validate action worked!"; the downloaded DMGs are those jobs' uploads (digests above).
Both zips' apps carry `_CodeSignature` and version 1.0.3.
The draft was created with the notes (their links rewritten to `blob` `v1.0.3` URLs)
before the tag was pushed, and tag CI uploaded into it. Every link in the notes answers
200.
