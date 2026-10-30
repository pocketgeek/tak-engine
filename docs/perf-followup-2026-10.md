# Performance follow-up (2026-10-30)

Branch `perf-followup`, based on main `61f294ac` (the audit commit; main had not
moved when work started). Exact performance pass: gameplay, state hashes, AI
command streams, RNG use and rendering output must not change (one shadow-atlas
fidelity question is handled separately, see K).

## Rules for this work

- Timing runs only on cores 0-3 through `/home/pocket_geek/tak-tmp/tools/timing.sh`
  (flock); builds, tests and sweeps on 4-23. Optimized Debug builds (`-O2 -g`) for
  harness timing, as `docs/development.md` describes.
- Screening numbers taken during development are marked *screening*. Final claims
  come from the serial measurement phase on the integrated branch (section
  "Final measurements").
- Identity: `tools/legion_identity.sh`, the 7 `--mpai` gates, `tools/check-determinism.sh`
  and the scenario baseline must match the base exactly.

Base identity values (0.9.12, Inner Circle, seed 1, `TAK_SPEED=80`): Retail 60
`8690eaee7b16b4e6`, Retail 300 `d028e465c7676e68`, Legion 60 `b558575535124afc`,
Legion 300 `eab501625cd4381a`, Retail AI 60 `12003fba552b8696`, Crusades Retail
300 `def92d0b7d4a798e`, Crusades Legion 300 `fb33fb2edffd997e`; determinism golden
`dcef618cd2e4d558`.

Hardware: Intel Core Ultra 9 275HX (8 P-cores 0-7 at up to 5.3-5.4 GHz, 16
E-cores 8-23), 125 GB RAM, RTX 5070 8 GB, Fedora 44 (Linux 7.2).

## Plan

1. Measurement changes A (complete-tick timing in `simperf`) and B (interactive
   rendering benchmark cases), each on its own branch.
2. Workstreams A-L, each in its own worktree and branch off `perf-followup`, with
   focused correctness checks and screening measurements, then an adversarial
   review.
3. Integration into `perf-followup` in a fixed order, full identity and test gates.
4. Serial final measurements on a quiet machine, base `61f294ac` vs the branch.
5. Dispositions; any change that does not pay is reverted with its evidence kept here.

## Disposition table

| Item | Disposition | Evidence |
|---|---|---|
| Measurement A: complete-tick timing | pending | |
| Measurement B: interactive render cases | pending | |
| A Feature burning/fading | pending | |
| B AI resurrection corpses | pending | |
| C Cursor picking | pending | |
| D Exploration block summaries | pending | |
| E Build-queue ghosts | pending | |
| F Hidden-unit Fade covers | pending | |
| G View-count deltas | pending | |
| H Legion static refresh | pending | |
| I Legion route invalidation | pending | |
| J Legion dirty-rect clustering | pending | |
| K Shadow atlas capacity | pending | |
| L Auras | pending | |

## Progress log

- 2026-10-30: branch created at `61f294ac`; plan written. Next: launch the
  workstream branches.
