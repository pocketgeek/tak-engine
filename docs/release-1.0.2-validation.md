# 1.0.2 release validation

Protocol **324**, replay format **15**, random map generator version **10** (all as 1.0.1).

Tag `v1.0.2` -> `81e208af` (the release preparation commit); annotated tag object `8e2d7b53`.
Published 2026-11-12 13:34:17 UTC as the Latest release; 1.0.0 and 1.0.1 stay GitHub releases.

## Scope and compatibility

1.0.2 is 1.0.1 (tag `v1.0.1` -> `a830ab1c`) plus three changes, listed in the CHANGELOG's
1.0.2 entry:

| Commit | Change | Game state |
|---|---|---|
| `403db480` | A map file with no radar picture gets a preview drawn from its terrain; the map list reads a saved random map's size from its `.tnt` | display only |
| `a8001c96` | The Cartographer's minimap builder moves into the shared library (`terrain/minimap.{h,cpp}`); `maps::saveGenerated` writes both radar pictures and `size=` into a played random map's saved copy | saved file only |
| `2834c192` | A random map's small preview is its large radar picture built from its tiles (no more height sketch with feature dots); the fallback for maps saved before `a8001c96` builds the same picture | display only |

**Compatibility.** Nothing hashed or sent in a game changed: `kNetVersion` stays 324 and
the simulation sources are untouched, so 1.0.2 plays with 1.0.1 and plays its replays (a
server checks only the protocol and the game-data hash). A random map room sends only the
recipe, which every player rolls; a saved `Generated-*.kmp` picked as an existing map
transfers like any map, with or without radar pictures. No mixed 1.0.1/1.0.2 game was
played.

## Local tests

Linux, Debug `-O2 -g` (`build-o2`):

- **Quick suite** (`ctest -L quick`): 629 / 629 on each of the three commits.
- **The changes' own tests:** `map_transfer` and `map_transfer_data` (a saved random map
  carries a 126 x 126 minimap, an overview and `size=`, and its terrain equals the rolled
  map's) and the 17 `cartographer` tests (its minimap code now in the shared library).
- **Screens, by eye:** the room and CHANGE MAP panel on a random map saved by 1.0.1
  (preview and `2P 16x8` in the list), the RANDOM MAP tab against its full-window view
  (same terrain), and that saved map's preview against its full-window view.
- **Save cost:** `saveGenerated` on a 64 x 64 map takes about 200 ms more than before
  (the tile pass for the pictures), on an 8 x 8 map about 60 ms.
- **`--mpai` Inner Circle, seed 1, Retail 60 s**, `TAK_SPEED=80`: `beb81788610d2a6c`, as
  1.0.0 and 1.0.1. The other gates were not run: no simulation source changed.

Pictures retaken with the 1.0.2 build: `docs/img/title.jpg` (VERSION 1.0.2) and
`docs/img/guide/random-map-lobby.jpg` (the new preview).

## Not run for this release

The full suite, the other `--mpai` gates, `tools/check-determinism.sh`, the desync sweeps
and the Windows and macOS test runs: the changes are confined to map previews, the map
list and the saved copy of a random map. Tag CI runs the determinism workflow.

## Tag CI and packages

Tag CI on `v1.0.2`, all green at the first attempt: Determinism (sim) 43222569522
(dispatched), Linux packages 43222557307, macOS build 43222557182 (both Sign + notarize
jobs included) and Windows build 43222557389. Main's runs on the preparation commit and
the three code commits were cancelled in favour of the tag's.

39 assets, the same set as 1.0.1's. All 39 were downloaded and match the release's
SHA-256 digests. `takclient` and `takserver` from the x86_64 AppImage and the Fedora 44
package report `1.0.2 (build v1.0.2)`.
Windows: on oden-win (scratch `C:/tak/w/rel102sig`, removed afterwards),
`Get-AuthenticodeSignature` reports **Valid** (timestamped, "Curtis Edge") for the x64
setup program and `takclient`, `takserver` and `cartographer` in both zips; the x64
`takclient.exe` and `takserver.exe` report `1.0.2 (build v1.0.2)`.
macOS: **the MacBook was offline**, so the DMGs and apps were not checked on a Mac this
time. The Sign + notarize jobs' own logs show, for each architecture, the DMG and both
apps accepted by `spctl` as "Notarized Developer ID" and "The staple and validate action
worked!"; the downloaded DMGs are those jobs' uploads (digests above). Both zips' apps
carry `_CodeSignature` and version 1.0.2.
The draft was created with the notes (their links rewritten to `blob` `v1.0.2` URLs)
before the tag was pushed, and tag CI uploaded into it. Every link in the notes answers
200.
