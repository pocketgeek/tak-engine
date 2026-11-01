# 0.9.15 release validation

Protocol **308** (unchanged from 0.9.14), replay format **15** (unchanged),
generator version **9** (unchanged).

## Scope and compatibility

0.9.15 follows 0.9.14 (tag `v0.9.14` -> `c92da57d`, validation commit `3779381c`).
One code commit: `88130300` removes the zoomed-out selection marker and
`projectilemodelscale.h`, display only, in `src/client` and
`tools/model_transform_test.cpp`. No file in `src/sim`, `src/net`, `src/ai` or
`src/server` changed. The preparation adds the version, CHANGELOG, README, notes,
this report and the title picture.

## Local preparation checks

On Linux, cores 4-23, scratch `HOME` and XDG dirs, logs in
`~/tak-tmp/scratch/release-0915/`, on `88130300` with the preparation edits.

- **Full suite** (`ctest -j 12`, Debug `-O2 -g`, `TAK_TEST_DATA` set, 850 s): 846
  of 846 passed.
- **Cross-compiler golden hash:** `dcef618cd2e4d558`, unchanged ("DETERMINISM OK";
  aarch64 legs skipped, no target libc here).
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`, twice (43 and 42 s): all seven
  equal 0.9.14's, `err=none`, no desync, mismatch or referee-suspect line:

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `8690eaee7b16b4e6` | `269f3da1835a17ea` |
| Legion | `b558575535124afc` | `a860707b7b4113e6` |
| Retail AI (`TAK_AI_LEVEL=6`) | `12003fba552b8696` | not run |
| Crusades, Retail pathfinding | not run | `6f8d58fdcd304fc7` |
| Crusades, Legion | not run | `4db379ba59111b0c` |

The rendering changes were looked at by the owner on the Linux build before the
cut; they were not rendered on the Neo before the tag.

## Limits

- All of 0.9.14's limits carry over (see
  [release-0.9.14-validation.md](release-0.9.14-validation.md#limits)).
