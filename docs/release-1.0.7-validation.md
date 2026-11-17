# 1.0.7 release validation

Protocol **334**, replay format **15**, random map generator version **10**.

[LANDER: tag `v1.0.7` -> `<version commit>`; annotated tag object `<id>`.
Published `<date time>` UTC as the Latest release.]

## Scope and compatibility

1.0.7 is 1.0.6 (tag `v1.0.6` -> `dc29fb6a`, validation commit `184c0a4e`) plus the
changes below (`24a10c47` to `b700ec15`): two combat fixes found checking the owner's
1.0.6 replay (Harpies charming buildings in the way, Gryphon spears landing wide), and
Legion pathfinding's overtaking, arrival layout for mixed-speed orders and destination
markers. They are listed in the CHANGELOG's 1.0.7 entry and described in
`docs/legion-pathfinding.md` ("Allies overtake", "Speed groups", "Arrival layout",
Known weaknesses), `docs/retail-engine.md` ("Attack runs") and the user guide:

| Change | Game state |
|---|---|
| A flyer without hover attack re-solves its aim at a scripted release (Gryphon, Fallen Angel, Dirigible and Juggernaut anti-air) | hashed sim |
| A mind-control, stone or freeze shot striking a body it cannot take counts as blocked; a hovering flyer on such a weapon drops the target after three | hashed sim |
| Legion: a fast body held behind one slower allied body walking the same way takes a local route past it (`overtakeRoute`, protocol 332) | hashed sim (Legion only) |
| Legion: passing within one click of up to 64 members, by speed groups (a top speed at least 4/3 of another's), strung out (protocol 333) | hashed sim (Legion only) |
| Legion: a non-Alt+N click of two or more speed groups and 2 to 48 members gets an arrival layout, fast rows in front, a lane through the slower rows; give-way, pace and wait on spots; fall-back to the formation's own shape past a gate or bridge and for three speeds by terrain (protocol 334) | hashed sim (Legion only) |
| Legion: each unit's own spot outlined after a move, fight-move or patrol order and under Shift (`World::legionDestination`, read at frame capture, own units only) | client only (display) |

**Compatibility.** The simulation changed, so `kNetVersion` is 334 (331 for the combat
fixes, 332 to 334 for Legion): 1.0.7 does not play with 1.0.6 or earlier, nor play
their replays. The replay format stays 15 and the random map recipe version 10, so
random maps and saved map files are unchanged.

## Local tests

Linux, Debug `-O2 -g` (`build-o2`). On main, as each part landed (from the commit
bodies):

- **Combat** (`870296cf`, protocol 331): quick suite 687 of 687 passed; golden
  `dcef618cd2e4d558`; all eight `--mpai` gates unchanged from 1.0.6 (none fields these
  flyers or casters within its time). `flyer_combat_test`: Gryphon 4 -> 12 hits of 12
  spears, Fallen Angel 0 -> 11 of 11; `blocked_shot_test` covers the Blue Flame hop.
- **Overtake** (`2103fb83`, protocol 332, and `f3a4c16e`, protocol 333): `ctest -L
  legion` 467 of 467 and quick suite 688 of 688; new test `legion_overtake`, failing on
  the parent; `bridges-1000` and `bridges-1000-fight` at or above the parent, none
  stuck; Inner Circle Retail and Legion 60 s gates unchanged.
- **Arrival layout** (`7416eec4`, protocol 334): `legion_overtake` rewritten to the
  owner's bar over 28 one-click fixtures (13 offsets each, 7 or 4 for the columns and
  the blob), with the owner's sign-off of 2026-11-16 encoded (`4efca247`: field and road
  at most 1% later, per-fixture bars for knights starting behind slower rows);
  `navigation_determinism`'s Legion goldens retaken, Retail's unchanged;
  `bridges-1000` hash-identical to main; `--mpai` gates unchanged.
- **Destination markers** (`ccd6c15d`, `b700ec15`): `legion_world_test` destinations and
  `unit_spot_markers` (data).

On the version commit's tree (`b700ec15` plus the version bump), built in a fresh build
directory:

- **Quick suite**: 690 tests, 689 passed. The one failure, `depth_fx` ("FADE: the tree in
  front of an effect does not fade"), is the load flake 1.0.6 had; it passed alone.
- **`tools/check-determinism.sh`**: the six x86_64 builds (g++ and clang++ at `-O0`,
  `-O2`, `-O3`) agree on golden `dcef618cd2e4d558` (the aarch64 legs skip here).
- **`--mpai` gates**, Inner Circle (Ulasem Arena for the last), seed 1, `TAK_SPEED=80`:

  | Gate | 1.0.6 | 1.0.7 |
  |---|---|---|
  | Retail 60 s | `beb81788610d2a6c` | `beb81788610d2a6c` |
  | Legion 60 s | `87efa5106c2d3c0e` | `87efa5106c2d3c0e` |
  | Retail AI 60 s | `a3ddc1ba59738936` | `a3ddc1ba59738936` |
  | Retail 300 s | `e058fbd245a7be8f` | `e058fbd245a7be8f` |
  | Legion 300 s | `5ac9f4c0ad44dafc` | `5ac9f4c0ad44dafc` |
  | Crusades Retail 300 s | `7415b4dbd309b753` | `7415b4dbd309b753` |
  | Crusades Legion 300 s | `12c69fd491f4f258` | `12c69fd491f4f258` |
  | Ulasem | `29ae726896b1338f` | `29ae726896b1338f` |

  All eight gave the 1.0.6 hashes.

Picture retaken with the 1.0.7 build: `docs/img/title.jpg` (VERSION 1.0.7).

## Not run for this release

The full suite, the desync sweeps and the Windows and macOS test runs. Tag CI runs the
determinism workflow and builds every package.

## Tag CI and packages

[LANDER: main's CI on the version commit; tag CI run ids (Determinism, Linux packages,
Windows build, macOS build with Sign + notarize); asset count and SHA-256 digest check;
AppImage and Fedora package `--version`; Windows Authenticode on oden-win; macOS
stapler, spctl and codesign on the MacBook; draft created before the tag; link check.]
