# 0.9.14 release validation

Protocol **308** (was 307 in 0.9.13), replay format **15** (unchanged),
generator version **9** (unchanged).

## Scope and compatibility

0.9.14 follows 0.9.13 (protocol 307; tag `v0.9.13` -> `4ad86ec6`, validation
commit `64a62b73`). On main since then:

- `c173afe7` the selection ring: retail's root-plate box, centre and turn
  (`4fd0d0`, `546fa0`, `535d50`). Display only.
- `0a7f4681` and its revert `f44c56be`: no net change.
- `53058c27` `CLAUDE.md`: a build dir keeps its generator.
- `36745684` the hardware cursor stays up across a screen change.
- `143557be` AI level 7, Passive, protocol 308; the lobby's level order;
  `Difficulty::Passive` (the lobby's Defensive) renamed `Difficulty::Defensive`.
- This release's preparation: version, CHANGELOG, README, notes, this report,
  title picture, and `tools/replay_test.cpp` pinning protocol 308.

**Compatibility.** `kNetVersion` is **308**, `kReplayFormat` **15**, generator
**9**. So 0.9.14 plays with no 0.9.13 or older client or server, and refuses
every 0.9.13 (protocol 307) replay. **Hashed state:** unchanged: a Passive seat's
sim config is an Easy seat's (`ai_behavior_test`), and the gates below equal
0.9.13's. No map, recipe, package or settings format changed.

## Local preparation checks

On Linux, cores 4-23, scratch `HOME` and XDG dirs, data and logs in
`~/tak-tmp/scratch/release-0914/`, on `143557be` with the preparation edits.

- **Full suite** (`ctest -j 12`, Debug `-O2 -g`, `TAK_TEST_DATA` set, 904 s): 845
  of 846 passed. The one failure was `replay`, which pinned protocol 307; pinned
  to 308, it passes (rerun alone, not the whole suite again).
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): golden
  `dcef618cd2e4d558` from g++ and clang++ at -O0, -O2 and -O3, unchanged
  ("DETERMINISM OK"). The aarch64 legs were skipped (no target libc here).
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`, twice (42 and 44 s for the
  seven in parallel). All seven equal 0.9.13's in both runs, `err=none`, and no
  desync, mismatch or referee-suspect line in any log:

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `8690eaee7b16b4e6` | `269f3da1835a17ea` |
| Legion | `b558575535124afc` | `a860707b7b4113e6` |
| Retail AI (`TAK_AI_LEVEL=6`) | `12003fba552b8696` | not run |
| Crusades, Retail pathfinding | not run | `6f8d58fdcd304fc7` |
| Crusades, Legion | not run | `4db379ba59111b0c` |

- **Passive:** `ai_behavior_test` (no exploring in 26 game minutes with nothing in
  sight, Easy's wave at a seen enemy, deposits near and far), `server_ai_level`
  (levels 0..7 round-trip a real server's lobby, 8 and up store Normal), and a
  600 s `--mpai` game with `TAK_AI_LEVEL=7`, `err=none`.

## Screenshots

`docs/img/title.jpg` retaken, reads VERSION 0.9.14 (looked at); see
`docs/img/README.md`.

## Desync sweeps

None was run, by the project's testing rule; no hashed state changed.

## Limits

- The cursor change was not exercised on a real display (no display here, and
  SDL's dummy driver has no cursors).
- The lobby's level cycling was not clicked through by hand.
- All of 0.9.13's limits carry over (see
  [release-0.9.13-validation.md](release-0.9.13-validation.md#limits)).
