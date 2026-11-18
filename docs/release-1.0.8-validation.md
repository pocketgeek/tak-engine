# 1.0.8 release validation

Protocol **335**, replay format **15**, random map generator version **10**.

[LANDER: tag `v1.0.8` -> `<version commit>`; annotated tag object `<id>`.
Published `<date time>` UTC as the Latest release.]

## Scope and compatibility

1.0.8 is 1.0.7 (tag `v1.0.7` -> `be0a81c2`, validation commit `5347adcc`) plus the
changes below (`1eaa96ae` to `a0d96369`): four exact shortcuts on Legion's move path,
which take processor time off very large orders and marches without changing any result,
and corrections to three older Legion shortcuts found while the first four were being
checked: two that Legion's self-checking mode (`TAK_LEGION_VERIFY`) reported, and the
complete-route bound cache, which the review of the first shortcut found and a scratch
cross-check confirmed in play. They are listed in the CHANGELOG's 1.0.8 entry and
described in `docs/legion-pathfinding.md` ("Exact shortcuts on the move path", "Allies
swap places", "Oncoming and opposing traffic", and the gate and corner memo paragraphs):

| Change | Game state |
|---|---|
| Legion: the destination-area bound of a route still building is kept for the rest of the tick's unit loop (`boundMemo`, `1eaa96ae`) | derived, never hashed; results identical |
| Legion: the awareness scan samples a group's way once per group and tests boxes without dividing (`5b953f95`) | derived, never hashed; results identical |
| Legion: the direct-line probe walks a line once instead of three times and tests its last blocker first (`lineClear`, `d4937696`) | derived, never hashed; results identical |
| Legion: each member keeps its descent chain while it stays on one cell (`Member::Descent`, `e8bc9674`) | derived, never hashed; results identical |
| Legion: a swap marks its partner in `passAhead`'s oncoming mask (`f790f158`) | hashed sim (Legion only) |
| Legion: the gate and corner memos key on the route's work count and the plane's generation too (`9719fdd0`) | hashed sim (Legion only) |
| Legion: the destination-area bound of a complete route is keyed on the route's work count too and dropped on any static change that reaches the group (`3587d591`) | hashed sim (Legion only; no hash changed in any run) |

**Compatibility.** The three corrections can change what the simulation computes, so
`kNetVersion` is 335 (`d8d67da1`): 1.0.8 does not play with 1.0.7 or earlier, nor play
their replays. The replay format stays 15 and the random map recipe version 10, so
random maps and saved map files are unchanged.

## Local tests

Linux, Debug `-O2 -g` (`build-o2`). On main, as each part landed:

- **Exact shortcuts** (`1eaa96ae` to `3f6fa9a4`, protocol 334 unchanged):
  `tools/legion_identity.sh` over the whole suite gave 1,054 of 1,055 rows the same as
  the parent. The one other row was `legion_check_fight-retarget`, whose offset-spread
  band on the never-hashed work sum `churn.bin0` tripped with the state hash identical
  at every offset; it is now judged on the median of five offsets (`3f6fa9a4`), as 18
  other fixtures are. `ctest -L legion -LE nightly` 324 of 324; all eight `--mpai` gates
  unchanged. Against the parent's referee, a forced stress battle (eight Absurd computer
  players on Ulasem Arena, unit cap 1,000 a player, 14,400 ticks) ended on
  `fb9ec3b9ab10f38c` and a natural eight-player game on `35eb2de619f7e77e` at tick
  71,694, both the parent's hashes. Each shortcut keeps the form it replaces under
  `TAK_LEGION_VERIFY`, and each cross-check trips on a deliberately broken shortcut
  (`docs/legion-pathfinding.md` names the ticks). Tick times, back-to-back pairs on four
  idle cores:

  | Scenario | Before | With the four shortcuts |
  |---|---|---|
  | `maze-10000`, one order to 8,500 units: slowest tick | 161 ms | 24 to 25 ms |
  | the same: ticks over 33.3 ms (their summed time) | 21 (1,933 ms) | 0 |
  | `bridges-4000` (generated for the timing, not in the tree), two armies of 2,000 crossing: slowest tick | 41 ms | 13 ms |
  | `many-10000`, mean tick | 25.2 ms | 20.5 ms |
  | the same: ticks over 33.3 ms, of 2,700 | 196 to 206 | 1 to 2 |
  | `many-5000`, mean tick | 10.9 ms | 9.2 ms |
  | `many-2500`, mean tick | 4.7 ms | 4.1 ms |
  | `maze-1000`, mean tick | 0.75 ms | 0.65 ms |
  | `bridges-1000`, mean tick | 1.09 ms | 1.02 ms |
  | `attack-10000`, mean tick | 12.8 ms | 12.4 ms |

- **Corrections** (`f790f158`, `9719fdd0`, `3587d591`), on the three together before the
  protocol number changed: `ctest -L legion -LE nightly` 325 of 325 (the new
  `legion_verify_seedlift` among them), the 85 `legion_check_` fixture guards and the
  quick suite, 690 of 690. All eight `--mpai` gates, the stress battle and the natural
  game ended on the hashes above. Under `TAK_LEGION_VERIFY` the stress battle now runs
  all 14,400 ticks (before the corrections it stops at tick 6,155), `swap-2500` runs to
  its end (before: tick 3,475), `taperbridge-100` at offsets 0, +1 and -1 (tick 3,739),
  `many-10000` for 3,000 ticks (tick 592) and `many-5000` for 3,000 ticks, and
  `legion_swap` and `legion_passage_taper` pass; `tools/legion_identity.sh --quick
  --verify` shows no verify failure in 375 rows (two rows fail for other reasons:
  `legion_cost` on a verify-only counter with no baseline, as in 1.0.7, and
  `legion_verify_seedlift` missing from the parent). A deliberately weakened rule trips
  the new cross-checks: the bound's at tick 1,069 of `seedlift-40`, the corner memo's at
  tick 372 of `corridor-corner-40`.

  What changes, against the parent: the 212 fixtures at the 11 gate offsets each
  supports (2,232 runs; ten spawn off the map at the larger offsets) and the 38 scale
  scenarios at offsets 0, +1 and -1 (`swap-1000` and `swap-2500` at all 11) were run on
  both. One fixture run ends on another hash (`taperbridge-small` at offset -1:
  arrivals, t90, crossings and wall touches the same) and 22 scale runs do (`swap-1000`,
  `-2500`, `-5000` and `-10000`, `many-2500`, `-5000` and `-10000`, `bridges-1000`,
  `one-5000`), every one of them from the swap's mark alone. Summed over the 22:
  arrivals 67,084 -> 67,056, bodies stuck at the end 15,457 -> 15,890, reversals 608,479
  -> 624,486; single runs move either way (`swap-2500` at offset 0: 2,306 -> 2,379
  arrived; `swap-5000` at -1: 2,963 -> 2,730). The memo correction changes the state
  inside two windows of the stress battle (two standing members commit to their gate
  lane 26 and 21 ticks sooner) and the battle ends on the same hash; the bound's
  correction changed no hash anywhere. Tick times with the three in are within noise of
  the parent's (`many-2500` 4.28 -> 4.31 ms, `many-5000` 9.11 -> 9.13 ms).

On the version commit's tree (`a0d96369` plus the version change):

- **Quick suite**: 690 tests. All passed on the first build of the tree; on the final
  build (after a comment-only change) 689 passed and `depth_fx` failed, the load flake
  1.0.6 and 1.0.7 had, and passed alone.
- **`tools/check-determinism.sh`**: the six x86_64 builds (g++ and clang++ at `-O0`,
  `-O2`, `-O3`) agree on golden `dcef618cd2e4d558` (the aarch64 legs skip here).
- **`--mpai` gates**, Inner Circle (Ulasem Arena for the last), seed 1, `TAK_SPEED=80`:

  | Gate | 1.0.7 | 1.0.8 |
  |---|---|---|
  | Retail 60 s | `beb81788610d2a6c` | `beb81788610d2a6c` |
  | Legion 60 s | `87efa5106c2d3c0e` | `87efa5106c2d3c0e` |
  | Retail AI 60 s | `a3ddc1ba59738936` | `a3ddc1ba59738936` |
  | Retail 300 s | `e058fbd245a7be8f` | `e058fbd245a7be8f` |
  | Legion 300 s | `5ac9f4c0ad44dafc` | `5ac9f4c0ad44dafc` |
  | Crusades Retail 300 s | `7415b4dbd309b753` | `7415b4dbd309b753` |
  | Crusades Legion 300 s | `12c69fd491f4f258` | `12c69fd491f4f258` |
  | Ulasem | `29ae726896b1338f` | `29ae726896b1338f` |

  All eight gave the 1.0.7 hashes.

Picture retaken with the 1.0.8 build: `docs/img/title.jpg` (VERSION 1.0.8).

## Not run for this release

The full suite, the desync sweeps and the Windows and macOS test runs. Tag CI runs the
determinism workflow and builds every package.

## Tag CI and packages

[LANDER: main's CI on the version commit; tag CI run ids (Determinism, Linux packages,
Windows build, macOS build with Sign + notarize); asset count and SHA-256 digest check;
AppImage and Fedora package `--version`; Windows Authenticode on oden-win; macOS
stapler, spctl and codesign on the MacBook; draft created before the tag; link check.]
