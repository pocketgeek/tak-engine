# 0.9.23 release validation

Protocol **316** (unchanged from 0.9.22), replay format **15** (unchanged), generator
version **9** (unchanged).

Tag `v0.9.23` -> the preparation commit (filled in below once tagged).

## Scope and compatibility

0.9.23 follows 0.9.22 (tag `v0.9.22` -> `faaeb222`, validation commit `48f08a7c`).
`git log v0.9.22..` holds 28 commits before the preparation commit, all made directly on
main. Besides two documentation commits (`8e69d3b8`, the 0.9.20 validation report;
`d9a6e777`, the Diplomacy and Voice Chat pictures) and the 0.9.22 validation commit:

- `c551f75c`: `stream_test` fails the local copy after its second keyframe (it was flaky
  on a loaded machine).
- `ea09701e`: shots and effects are painter items sorted with the ground units and tall
  features (a deliberate departure from retail, at the owner's request).
- `3b72c4ce`, `7274fc88`, `08c41cb5`: the IDLE BUILDERS column, its scale (50–400 %,
  default 50 %) and order; BUILD MENU SCALE down to 50 %.
- `1cda56dd`: AI: a flying builder that is only landing is idle (server-side only).
- `bb6242a9`: an Options dropdown pick applies at once.
- `5de49e4f`, `d2fc173b`, `59d53d3b`, `493f03ab`: loading screens titled as their doors,
  *Starting...*, and a loading screen for a multiplayer connect.
- `ca697a65`, `431f83c0`, `935a010c`: the create screen (Play the Machine title, no
  repeated headings, map FILTER, even margins, wider list).
- `9269cd88`, `d8fb7d96`: the room's map name, slot table and names; the map list hides
  MapCache copies of installed maps.
- `ea7950cd`, `d8215976`: the room's mods summary and the MODS screen (FILTER per list).
- `0c739df6`, `564f9bad`, `e794db17`: CONNECT: loopback addresses default to plain TCP,
  DELETE on remembered servers, up to 64 of them scrolling under the default and CUSTOM;
  BACK drawn like CONNECT.
- `e70851e8`, `a5108ef6`, `3d1f8d70`: the room chat box (no CHAT or SAY labels, a scroll
  bar, under the mods status).
- `72ebf352`: the multiplayer create screen's BACK returns to the browser.

**Compatibility.** Nothing hashed changed and the wire formats did not change: every
`--mpai` gate keeps 0.9.22's hash, and mixed games of 0.9.22 and 0.9.23 binaries agree
with it in both directions (below). 0.9.23 plays with 0.9.22 and its replays. The AI fix
runs on the server, so it applies only where the server is 0.9.23.

## Local tests on the cut

Linux, `build-o2` (Debug `-O2 -g`), on `72ebf352`:

- **Full ctest** (`ctest -j 16`, every label): 908 of 908 passed.
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`: all seven gates keep 0.9.22's
  hashes.

| Gate | 0.9.22 | 0.9.23 `72ebf352` |
|---|---|---|
| Retail 60 s | `beb81788610d2a6c` | `beb81788610d2a6c` |
| Legion 60 s | `87efa5106c2d3c0e` | `87efa5106c2d3c0e` |
| Retail AI 60 s (`TAK_AI_LEVEL=6`) | `a3ddc1ba59738936` | `a3ddc1ba59738936` |
| Retail 300 s | `30a3e25d3f414fad` | `30a3e25d3f414fad` |
| Legion 300 s | `bd274b29f05f2c18` | `bd274b29f05f2c18` |
| Crusades, Retail 300 s | `23d5e2016eae1bb8` | `23d5e2016eae1bb8` |
| Crusades, Legion 300 s | `ee9c38ff55448ab3` | `ee9c38ff55448ab3` |

- **Mixed versions**, the same `--mpai` run at 300 s with the published 0.9.22 Linux
  debug binaries (`tak-engine-linux-x64-debug.tar.gz`, build `v0.9.22`) and the 0.9.23
  `build-o2` ones: a 0.9.22 server with a 0.9.23 client and a 0.9.23 server with a 0.9.22
  client, in Retail and in Legion. All four finish with the gate hash (`30a3e25d3f414fad`
  Retail, `bd274b29f05f2c18` Legion), and no server or client log reports a mismatch, a
  desync or a referee suspect.
- **Replay**: the replay the 0.9.22 server recorded of the Retail game (9003 ticks, 31
  hash checkpoints, "recorded by 0.9.22") played in the 0.9.23 client without a
  divergence report.
- **Pictures** retaken for the changed screens (`docs/img/README.md`).

Not run for this cut (the testing tiers): the remote desync sweeps, ASan, Clang and the
Windows and macOS local suites; tag CI builds and tests all three platforms.

## CI and packages

(Filled in after tag CI.)

## Signing

(Filled in after tag CI.)

## Publication

(Filled in at publication.)

## Known limits

- Only the Linux suite ran locally (the testing tiers); Windows and macOS are covered by
  tag CI.
- The in-game pictures predate the Idle Builders column and the depth-ordered shots and
  effects (noted in `docs/img/README.md`).
- The mixed-version check ran headless `--mpai` games only; no hand-played mixed game.
