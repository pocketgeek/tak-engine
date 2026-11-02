# 0.9.16 release validation

Protocol **309** (was 308 in 0.9.15), replay format **15** (unchanged), generator
version **9** (unchanged).

## Scope and compatibility

0.9.16 follows 0.9.15 (tag `v0.9.15` -> `8cb2d102`). One code commit, `60c0684c`,
carries the replay transport and world clone, the watch-only rule, the mobile rally
change, surrender, the result screen's doors, sign-in tokens, the benchmark's pathfinding
choice, the rebindable streaming key and the STARTING button. The work ran on branches
`task-replay-spectate`, `task-rally-exit`, `task-bench-path` and `task-session-token`,
each reviewed; the smaller menu changes were not.

**Compatibility.** `kNetVersion` is **309**. A rallied mobile output's first order is
hashed, so 0.9.16 plays with no 0.9.15 or older client or server and refuses their
replays. The gates below do not move, because the AI never repeats a build queue.

## Local preparation checks

On Linux, cores 4-23, scratch `HOME` and XDG dirs, logs in
`~/tak-tmp/scratch/release-0916/`, on `60c0684c`.

- **Full suite** (`ctest -j 12`, Debug `-O2 -g`, `TAK_TEST_DATA` set, 937 s): 852 of 852
  passed.
- **Cross-compiler golden hash:** `dcef618cd2e4d558`, unchanged ("DETERMINISM OK";
  aarch64 legs skipped, no target libc here).
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`, once on this commit: all seven equal
  0.9.15's, `err=none`, no desync, mismatch or referee-suspect line:

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `8690eaee7b16b4e6` | `269f3da1835a17ea` |
| Legion | `b558575535124afc` | `a860707b7b4113e6` |
| Retail AI (`TAK_AI_LEVEL=6`) | `12003fba552b8696` | not run |
| Crusades, Retail pathfinding | not run | `6f8d58fdcd304fc7` |
| Crusades, Legion | not run | `4db379ba59111b0c` |

- **Owner:** tried the replay and spectating work by hand and called it good. Which of
  the later changes were tried is not recorded.

## Limits

- Not run: a real Game Select click and its fallback, an in-game reconnect after a
  dropped link on a token session, a real Surrender in a game, the benchmark panel on a
  real GPU, and a token session against a real TLS server outside the tests.
- All of 0.9.15's limits carry over (see
  [release-0.9.15-validation.md](release-0.9.15-validation.md#limits)).
