# WE Wave 0: instruments and the re-baseline (2026-10-09)

Engine-wide perf (WE; the WE synthesis plan, section 2, Wave 0). E0.1 is the instrument commit
(`src/sim/simprobe.h`, TAK_SIMSTATS / TAK_PMU / the TAK_PHASE prologue= and compact= fields, the
TAK_PACELOG presentation and worker columns). E0.2 is this file: the baseline every later WE step
compares against, and the two go/no-go decisions Wave 0 owes the plan.

Head: main 7c490b2f (protocol 240) plus E0.1, which is hash-identical (legion_identity PASS, 522
SAME). Machine: Core Ultra 9 275HX; P-cores 0-7, E-cores 8-23 in L2 clusters of four.

## Verdicts

| Gate (WE-synthesis section 2) | Measured | Verdict |
|---|---|---|
| A1 (COB sleep skip) proceeds only if vm_skippable >= 25% of vm_ticks at 10k | R-2h 42.7%, L-2h 43.7% at 9.5-10.5k units (34.6% / 33.4% at 14k+, 13.4% in the spawn ticks 0-5; R-bench 33.9%, L-bench 37.1%). Empty VMs are 0.0-0.6%: the "skip empty VMs only" fallback would save nothing. | **GO** for A1 as designed. The 25% bar is met by a wide margin. The fallback is not worth building. |
| Data steps (D1, D2, D3, D5) only on phases with >= 40% memory-stall cycles | P-core (Lion Cove, cycles stalled on L1-miss loads / all cycles, 10k window): scripts 55-56%, sep 52-56% (grid 41-51%, explore 49%), prologue 43-47%, other 41-47%; combat 30-36%, movement 27-39%, nav 12% R / 27% L. Whole tick 39%. E-core (Skymont, contended run): whole tick 52%, scripts 75-77%, sep 67-68%, prologue 60-65%, other 58-59%, combat 43-49%, movement 31% R / 47% L, nav 16% R / 40% L. | **GO** on scripts, the prologue/other passes and sep/grid (both core types). **NO-GO on P-cores** for combat, movement and nav. On E-cores, combat and Legion movement qualify and Legion nav is at the bar. So D2's combat claim and D1's movement claim hold only for the 1x-on-E-cores target, not for the 8x-on-P-cores target. D5 (Legion containers) is at the bar on E-cores only (40%) and below it on P (27%). D3 (script slab) stays behind the E2.7 re-profile, as planned. |
| B1 (threshold compaction) built only if compaction >= 1 ms at 10k | compact= max 2.2 ms at 10k on P (1.9-2.2 on E), 2.7 ms at 14k+. It runs every 30th tick, outside tick=; its per-tick mean is about 0.06-0.07 ms. | Condition **met**. B1 still needs the Retail-golden note at its own step. |
| A2d (nearest-first ring) only if los_calls per scan >= 2 | R-2h at 10k: 736 LoS calls / 673 acquisition scans = 1.09; at 14k+ 2.7; spawn 3.4. L-2h 0.81 at 10k. | **Not at 10k.** It is a 14k+ / spawn effect. Re-check after A2b/c. |

The A2 premise is confirmed by the counters. At 10k, forEachNear tests about 0.9-1.0M cells per tick.
Of those, 99% already fail the per-cell owner mask, and 94-95% lie in an 8x8 block whose mask has no
wanted player. findTarget scans also visit 213-235k cells per tick whose box lies wholly outside the
search disk.

## How it was measured

- **Builds.**
  - build-o2 (Debug + `-O2`) for every replay and the client side of the Ulasem benchmark.
  - Release for crowdbench and for the Ulasem referee: a release `takserver` honours TAK_PHASE through the sim library.
  - A release takclient has no replay mode (`src/client/dev.h`), so there are **no Release replay numbers**. The
    Release-to-o2 ratio comes from crowdbench and the referee instead.
  - Crowdbench: Release within about ±5% of o2.
  - Ulasem: the Release referee is 2-9% faster than the o2 client on the same game.
  - Read the o2 replay figures as at most about 10% above Release.
- **Replays.**
  - R-2h, L-2h, R-bench, L-bench are the pf07 copies (`tools/legion_identity.py patch-replay`, protocol 237 -> 240, TOV1 -> TOV2).
  - R-2h and R-bench replay exactly (1228d1e97936109c, 10d19f7dfd9a1aa9).
  - The L replays diverge at tick 0 (recorded on an older Legion), but the run is deterministic
    (2a67fa5148c6e6f0, e184dc3bef4f164b on every run). They remain a fixed workload.
  - The windows are the PF-07 ones: 9.5-10.5k units, 14k+ and ticks 0-5 (spawn).
- **Pinning.**
  - P-cores run as concurrent pairs on 0-1 and 2-3, swapped in a second half.
  - E-core runs use the 16-19 cluster, one replay at a time, beside the P pairs.
  - The other-cores-busy column is the mean busy % of every core outside the run's own, from /proc/stat.
- **Load.**
  - Lane A was building on 8-17 for much of the session. Its load shows in that column.
  - Rows marked *contended* ran beside the identity harness on cores 0-5 and are kept only to show the effect:
    up to +35% on E-core bench means, little on the 2h means.
  - The quiet E-core runs repeat within 1% (R-2h 19.53 / 19.60, L-2h 25.37 / 25.23 ms at 10k).
  - P-pair runs spread 5-15% from run to run.
  - In the swapped base/cand round, both builds ran 9-13% slower in the second half on both pairs. A move
    that both builds make together is host-side, not code.
  - Use the medians below, and compare a later step against an interleaved pair, never against these absolutes.
- **Instrument cost** (E0.1, every switch off). From swapped base/cand pairs at 10k:
  - R-2h -0.3%, L-2h +0.5%, against 3-4% pair-to-pair noise.
  - TAK_PMU=1 with TAK_PHASE: R-2h 15.28 ms at 10k on P0-1, inside the spread of the runs without it. rdpmc
    dilution of the per-phase shares is therefore small.
- **SIMPHASE quirk, kept for comparability.** `other=` subtracts grid twice, because grid is inside sep.
  SIMPMU's other does not.

## Summary at 9.5-10.5k units (build-o2, ms per tick; medians of the runs listed below)

| Replay | P0-1 mean (runs) | P2-3 mean | E16-19 mean | P0-1 p99 | E p99 | 8x budget 4.17 ms | 1x budget 33.3 ms on E |
|---|---|---|---|---|---|---|---|
| R-2h | 14.88 (14.22-16.32, 4 runs) | 15.77 | 19.56 | 16.3 | 21.4 | 3.6x over | 41% margin |
| L-2h | 19.68 (18.86-21.73, 4 runs) | 19.97 | 25.30 | 24.6 | 30.7 | 4.7x over | 24% margin; p99 30.7 |
| R-2h, 14k+ | 21.6-21.9 (P0-1) | | 28.6-29.0 | | 33.3 | | 4-7 ticks over 33.3 ms |
| L-2h, 14k+ | 28.9-32.0 | | 37.6-38.5 | | 45.6-45.9 | | **fails**: 541-560 of 660 ticks over 33.3 ms |
| Ulasem 8-AI, 3.5k units | R 5.9-6.1, L 8.9-9.5 | | R 7.7, L 11.3 | | | | |

Against PF-07 (b8a4110, P0-1):
- R-2h at 10k was 14.12 ms and is now 14.2-16.3 (median 14.9).
- L-2h at 10k was 18.45 ms and is now 18.9-21.7 (median 19.7). The E-core figures in the four WE proposals were contended; the quiet ones here are lower.
- R-2h 10k: 26.4 -> 19.6 ms.
- L-2h 10k: 31.8 -> 25.3 ms.
- L-2h 14k+: 49.4 -> 38 ms.

The spawn ticks are unchanged at 51-70 ms on P (R 51-63, L 55-70; scripts 9.5-12.6 ms of the 6-tick
mean).

## Work counters (TAK_SIMSTATS, per tick, window means)

| replay | window | vm_ticks | vm_skippable | vm_skippable_pct | vm_empty_pct | vm_debt_flushes | near_scans | near_cells | masked_pct | near_blocks_skipped | block_skippable_pct | near_cells_outside_disk | acq_scans | los_calls | refresh_rects | refresh_grade_evals | refresh_raw_grades | compact_moved | passes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| R-2h | u9500-10500 | 9984.8 | 4262.8 | 42.7 | 0.0 | 5.5 | 730.8 | 900875.4 | 99.1 | 123601.5 | 93.9 | 213276.5 | 673.2 | 736.2 | 35.8 | 1452.0 | 3957.9 | 340.2 | 26.2 |
| R-2h | u14000+ | 14686.3 | 5075.3 | 34.6 | 0.0 | 32.6 | 1092.5 | 1293834.0 | 91.9 | 160709.9 | 85.2 | 306019.9 | 987.1 | 2658.0 | 199.9 | 8155.1 | 17567.2 | 386.8 | 26.6 |
| R-2h | ticks0-5 | 15208.3 | 2035.5 | 13.4 | 0.0 | 56.2 | 2591.0 | 3365335.0 | 98.0 | 440843.7 | 89.8 | 803785.2 | 2493.7 | 8478.3 | 0.0 | 0.0 | 0.0 | 0.0 | 27.2 |
| L-2h | u9500-10500 | 9938.9 | 4340.0 | 43.7 | 0.0 | 10.0 | 814.7 | 989835.6 | 99.3 | 138252.4 | 95.3 | 235180.9 | 758.1 | 612.2 | 0.0 | 0.0 | 0.0 | 332.8 | 26.9 |
| L-2h | u14000+ | 14706.1 | 4916.4 | 33.4 | 0.0 | 54.3 | 1080.4 | 1277718.6 | 93.2 | 161541.6 | 86.4 | 303037.9 | 975.5 | 2683.9 | 0.0 | 0.0 | 0.0 | 401.2 | 26.9 |
| L-2h | ticks0-5 | 15208.3 | 2035.3 | 13.4 | 0.0 | 55.3 | 2585.5 | 3360049.5 | 98.0 | 439986.7 | 89.8 | 802264.0 | 2488.2 | 8482.2 | 0.0 | 0.0 | 0.0 | 0.0 | 27.7 |
| R-bench | all | 814.4 | 276.2 | 33.9 | 0.6 | 2.0 | 135.3 | 152494.9 | 99.5 | 21326.3 | 95.1 | 36019.5 | 116.4 | 102.2 | 34.2 | 1428.2 | 4635.9 | 19.1 | 27.8 |
| L-bench | all | 1189.1 | 441.4 | 37.1 | 0.2 | 1.2 | 151.1 | 159818.5 | 99.4 | 22538.4 | 95.5 | 36855.3 | 129.3 | 49.2 | 0.0 | 0.0 | 0.0 | 20.9 | 28.2 |

Every counter except passes is the A1/A2/A3 probe described in `src/sim/simprobe.h`. passes counts
full sweeps over `units_` per tick in World::tick and the sim.cpp helpers it calls; Legion's own
sweeps in legion.cpp are not included. Counters are deterministic and were taken on E-cores 16-19.

## Memory stalls (TAK_PMU, per phase: stall share / IPC / LLC misses per 1000 instructions)

| run | window | tick | scripts | combat | movement | nav | sep | grid | explore | prologue | other | compact |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| R-2h P0-1 | u9500-10500 | 39% / 1.51 / 10.7 | 55% / 1.34 / 28.6 | 36% / 1.96 / 6.7 | 27% / 1.59 / 5.5 | 12% / 0.47 / 20.8 | 56% / 1.23 / 19.9 | 51% / 1.22 / 20.5 | 49% / 1.84 / 9.6 | 43% / 1.41 / 11.7 | 47% / 1.31 / 12.3 | 16% / 1.16 / 13.0 |
| R-2h P0-1 | u14000+ | 33% / 1.71 / 8.6 | 53% / 1.47 / 24.1 | 25% / 1.92 / 3.9 | 18% / 1.98 / 3.2 | 14% / 0.45 / 26.1 | 58% / 1.12 / 23.9 | 46% / 1.46 / 16.5 | 51% / 1.73 / 10.7 | 42% / 1.48 / 10.0 | 39% / 1.51 / 10.0 | 21% / 1.46 / 12.0 |
| L-2h P2-3 | u9500-10500 | 39% / 1.72 / 7.7 | 56% / 1.31 / 28.8 | 30% / 2.11 / 4.2 | 39% / 1.57 / 5.1 | 27% / 2.48 / 2.2 | 52% / 1.34 / 19.8 | 41% / 1.49 / 16.4 | 49% / 1.83 / 10.2 | 47% / 1.36 / 17.7 | 41% / 1.41 / 13.4 | 20% / 1.48 / 13.6 |
| L-2h P2-3 | u14000+ | 35% / 1.73 / 7.8 | 55% / 1.42 / 22.4 | 28% / 1.81 / 4.4 | 27% / 1.84 / 3.7 | 29% / 2.37 / 2.6 | 59% / 1.04 / 31.9 | 58% / 1.00 / 26.1 | 52% / 1.71 / 11.9 | 43% / 1.43 / 14.2 | 38% / 1.45 / 11.4 | 27% / 1.65 / 14.0 |
| R-2h E16-17 (contended) | u9500-10500 | 52% / 1.25 / 10.8 | 77% / 0.70 / 23.9 | 49% / 1.75 / 7.4 | 31% / 1.56 / 5.1 | 16% / 0.33 / 5.7 | 68% / 1.19 / 23.7 | 67% / 1.04 / 26.7 | 53% / 1.95 / 14.4 | 60% / 1.20 / 17.2 | 58% / 1.08 / 14.0 | 54% / 1.37 / 23.6 |
| R-2h E16-17 (contended) | u14000+ | 51% / 1.31 / 10.0 | 74% / 0.84 / 21.1 | 43% / 1.61 / 5.3 | 26% / 1.78 / 3.0 | 18% / 0.35 / 7.7 | 73% / 0.93 / 34.6 | 71% / 0.90 / 32.4 | 54% / 1.90 / 16.9 | 61% / 1.15 / 19.0 | 57% / 1.11 / 14.8 | 60% / 1.30 / 25.8 |
| L-2h E18-19 (contended) | u9500-10500 | 52% / 1.40 / 7.8 | 75% / 0.75 / 22.9 | 43% / 1.87 / 4.1 | 47% / 1.42 / 5.1 | 40% / 2.04 / 2.5 | 67% / 1.23 / 22.7 | 64% / 1.13 / 23.1 | 53% / 1.96 / 14.5 | 65% / 1.07 / 24.4 | 59% / 1.07 / 15.5 | 53% / 1.39 / 24.0 |
| L-2h E18-19 (contended) | u14000+ | 51% / 1.38 / 8.9 | 72% / 0.93 / 20.0 | 44% / 1.54 / 5.2 | 37% / 1.66 / 3.8 | 46% / 1.85 / 3.4 | 75% / 0.88 / 39.2 | 74% / 0.79 / 40.7 | 54% / 1.89 / 17.5 | 64% / 1.03 / 25.3 | 59% / 1.04 / 16.9 | 59% / 1.25 / 26.5 |

The P rows ran on quiet pairs. The E rows ran beside the identity harness, so their stall shares carry
some extra L3 pressure. The designer's E-core figure was 57-59% for the whole tick (S0 of the WE data-layout design);
the measurement here is 52%. On P-cores the whole tick is 39%. That is the first measured P-core
number, and it is why the data steps clear the 40% bar only per phase.

## Replays in full (build-o2)


**R-2h, window u9500-10500** (build-o2)

| run | ticks | mean | p95 | p99 | max | >4.17 | >33.3 | combat | sep | scripts | movement | nav | other | of which prologue | compact max | other cores busy |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cand P0-1 | 1753 | 14.83 | 15.88 | 16.22 | 16.90 | 1753 | 0 | 1.94 | 1.13 | 1.84 | 5.68 | 0.00 | 4.04 | 1.52 | 2.18 | 20% |
| base P2-3 | 1753 | 15.41 | 17.30 | 18.10 | 19.70 | 1753 | 0 | 1.94 | 1.32 | 1.86 | 5.94 | 0.00 | 4.13 | 0.00 | 0.00 | 21% |
| base P0-1 | 1753 | 16.32 | 17.60 | 18.60 | 20.40 | 1753 | 0 | 2.07 | 1.40 | 1.94 | 6.23 | 0.00 | 4.44 | 0.00 | 0.00 | 19% |
| cand P2-3 | 1753 | 16.85 | 18.49 | 20.55 | 28.11 | 1753 | 0 | 2.25 | 1.54 | 2.06 | 6.24 | 0.00 | 4.51 | 1.67 | 2.23 | 18% |
| base P0-1 (round 1) | 1753 | 14.22 | 15.30 | 15.80 | 16.40 | 1753 | 0 | 1.78 | 1.06 | 1.73 | 5.59 | 0.00 | 3.87 | 0.00 | 0.00 | 11% |
| base P2-3 (round 1) | 1753 | 15.35 | 17.00 | 18.10 | 26.00 | 1753 | 0 | 1.94 | 1.33 | 1.82 | 5.99 | 0.00 | 4.09 | 0.00 | 0.00 | 26% |
| cand P0-1 (round 3) | 1753 | 14.93 | 15.98 | 16.39 | 17.05 | 1753 | 0 | 1.96 | 1.15 | 1.84 | 5.72 | 0.00 | 4.06 | 1.54 | 2.12 | 15% |
| cand P2-3 (round 3) | 1753 | 16.13 | 18.06 | 19.20 | 27.26 | 1753 | 0 | 2.11 | 1.53 | 1.95 | 6.02 | 0.00 | 4.30 | 1.59 | 1.99 | 25% |
| cand E16-19 (quiet) | 1753 | 19.53 | 20.82 | 21.24 | 21.88 | 1753 | 0 | 2.34 | 1.16 | 3.89 | 6.32 | 0.00 | 5.59 | 2.02 | 1.92 | 25% |
| cand E16-19 (round 3) | 1753 | 19.60 | 20.97 | 21.57 | 27.14 | 1753 | 0 | 2.37 | 1.19 | 3.90 | 6.31 | 0.00 | 5.59 | 2.01 | 1.91 | 22% |
| cand E16-19 (contended) | 1753 | 20.66 | 21.95 | 22.52 | 24.03 | 1753 | 0 | 2.48 | 1.25 | 4.11 | 6.51 | 0.00 | 6.06 | 2.20 | 2.24 | 30% |

**R-2h, window u14000+** (build-o2)

| run | ticks | mean | p95 | p99 | max | >4.17 | >33.3 | combat | sep | scripts | movement | nav | other | of which prologue | compact max | other cores busy |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cand P0-1 | 687 | 21.55 | 23.69 | 24.81 | 51.14 | 687 | 1 | 3.37 | 1.53 | 3.41 | 7.18 | 0.00 | 5.78 | 2.50 | 2.70 | 20% |
| base P2-3 | 687 | 21.79 | 25.10 | 26.60 | 53.90 | 687 | 1 | 3.35 | 1.66 | 3.34 | 7.37 | 0.00 | 5.77 | 0.00 | 0.00 | 21% |
| base P0-1 | 687 | 22.59 | 24.80 | 26.00 | 54.80 | 687 | 1 | 3.45 | 1.75 | 3.44 | 7.56 | 0.00 | 6.10 | 0.00 | 0.00 | 19% |
| cand P2-3 | 687 | 23.33 | 25.71 | 27.66 | 63.40 | 687 | 1 | 3.67 | 1.79 | 3.66 | 7.64 | 0.00 | 6.24 | 2.68 | 2.71 | 18% |
| base P0-1 (round 1) | 687 | 20.79 | 22.80 | 23.90 | 51.10 | 687 | 1 | 3.19 | 1.49 | 3.20 | 7.09 | 0.00 | 5.56 | 0.00 | 0.00 | 11% |
| base P2-3 (round 1) | 687 | 23.46 | 25.70 | 27.40 | 58.40 | 687 | 3 | 3.59 | 1.96 | 3.50 | 7.78 | 0.00 | 6.31 | 0.00 | 0.00 | 26% |
| cand P0-1 (round 3) | 687 | 21.85 | 24.02 | 25.08 | 51.47 | 687 | 1 | 3.42 | 1.61 | 3.45 | 7.23 | 0.00 | 5.85 | 2.54 | 2.71 | 15% |
| cand P2-3 (round 3) | 687 | 24.04 | 26.65 | 29.77 | 61.40 | 687 | 5 | 3.78 | 1.95 | 3.71 | 7.92 | 0.00 | 6.36 | 2.74 | 2.68 | 25% |
| cand E16-19 (quiet) | 687 | 28.64 | 31.44 | 32.94 | 62.25 | 687 | 4 | 3.94 | 1.58 | 6.35 | 8.30 | 0.00 | 8.12 | 3.24 | 2.48 | 25% |
| cand E16-19 (round 3) | 687 | 28.99 | 31.92 | 33.32 | 62.22 | 687 | 7 | 4.01 | 1.63 | 6.42 | 8.32 | 0.00 | 8.24 | 3.30 | 2.70 | 22% |
| cand E16-19 (contended) | 687 | 31.03 | 34.81 | 36.58 | 64.55 | 687 | 125 | 4.28 | 1.84 | 6.79 | 8.68 | 0.00 | 9.05 | 3.70 | 3.04 | 30% |

**R-2h, window ticks0-5** (build-o2)

| run | ticks | mean | p95 | p99 | max | >4.17 | >33.3 | combat | sep | scripts | movement | nav | other | of which prologue | compact max | other cores busy |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cand P0-1 | 6 | 20.87 | 51.14 | 51.14 | 51.14 | 6 | 1 | 1.16 | 1.42 | 9.29 | 4.74 | 0.00 | 4.03 | 1.79 | 0.00 | 20% |
| base P2-3 | 6 | 22.50 | 53.90 | 53.90 | 53.90 | 6 | 1 | 1.27 | 1.73 | 9.80 | 5.00 | 0.00 | 4.32 | 0.00 | 0.00 | 21% |
| base P0-1 | 6 | 22.32 | 54.80 | 54.80 | 54.80 | 6 | 1 | 1.27 | 1.58 | 9.70 | 5.12 | 0.00 | 4.30 | 0.00 | 0.00 | 19% |
| cand P2-3 | 6 | 25.51 | 63.40 | 63.40 | 63.40 | 6 | 1 | 1.42 | 1.70 | 11.19 | 5.98 | 0.00 | 4.83 | 2.25 | 0.00 | 18% |
| base P0-1 (round 1) | 6 | 21.23 | 51.10 | 51.10 | 51.10 | 6 | 1 | 1.25 | 1.53 | 9.35 | 4.73 | 0.00 | 4.10 | 0.00 | 0.00 | 11% |
| base P2-3 (round 1) | 6 | 24.50 | 58.40 | 58.40 | 58.40 | 6 | 1 | 1.33 | 2.17 | 10.30 | 5.45 | 0.00 | 4.87 | 0.00 | 0.00 | 26% |
| cand P0-1 (round 3) | 6 | 20.95 | 51.47 | 51.47 | 51.47 | 6 | 1 | 1.22 | 1.42 | 9.36 | 4.76 | 0.00 | 3.90 | 1.87 | 0.00 | 15% |
| cand P2-3 (round 3) | 6 | 25.69 | 61.40 | 61.40 | 61.40 | 6 | 1 | 1.41 | 2.43 | 10.95 | 5.76 | 0.00 | 4.71 | 2.13 | 0.00 | 25% |
| cand E16-19 (quiet) | 6 | 28.13 | 62.25 | 62.25 | 62.25 | 6 | 1 | 1.74 | 1.81 | 12.17 | 6.03 | 0.00 | 5.96 | 2.65 | 0.00 | 25% |
| cand E16-19 (round 3) | 6 | 28.06 | 62.22 | 62.22 | 62.22 | 6 | 1 | 1.72 | 1.89 | 11.93 | 6.02 | 0.00 | 6.07 | 2.72 | 0.00 | 22% |
| cand E16-19 (contended) | 6 | 29.70 | 64.55 | 64.55 | 64.55 | 6 | 1 | 1.83 | 2.01 | 12.76 | 6.12 | 0.00 | 6.54 | 2.89 | 0.00 | 30% |

**L-2h, window u9500-10500** (build-o2)

| run | ticks | mean | p95 | p99 | max | >4.17 | >33.3 | combat | sep | scripts | movement | nav | other | of which prologue | compact max | other cores busy |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cand P0-1 | 2357 | 19.34 | 22.32 | 23.79 | 25.75 | 2357 | 0 | 1.25 | 1.14 | 1.89 | 7.68 | 4.00 | 3.23 | 1.47 | 2.29 | 27% |
| base P2-3 | 2357 | 20.13 | 23.70 | 25.50 | 27.80 | 2357 | 0 | 1.28 | 1.31 | 1.86 | 7.98 | 4.20 | 3.33 | 0.00 | 0.00 | 27% |
| base P0-1 | 2357 | 21.73 | 25.70 | 27.40 | 29.80 | 2357 | 0 | 1.39 | 1.33 | 1.95 | 8.52 | 4.66 | 3.64 | 0.00 | 0.00 | 32% |
| cand P2-3 | 2357 | 22.82 | 27.08 | 29.64 | 36.01 | 2357 | 3 | 1.47 | 1.42 | 2.12 | 8.87 | 4.85 | 3.85 | 1.80 | 2.19 | 31% |
| base P0-1 (round 1) | 2357 | 18.86 | 22.40 | 24.00 | 26.80 | 2357 | 0 | 1.21 | 1.10 | 1.78 | 7.48 | 3.98 | 3.16 | 0.00 | 0.00 | 23% |
| base P2-3 (round 1) | 2357 | 19.13 | 22.70 | 24.70 | 26.90 | 2357 | 0 | 1.22 | 1.23 | 1.80 | 7.62 | 3.95 | 3.16 | 0.00 | 0.00 | 15% |
| cand P0-1 (round 3) | 2357 | 20.02 | 23.84 | 25.27 | 26.64 | 2357 | 0 | 1.29 | 1.16 | 1.95 | 7.91 | 4.19 | 3.35 | 1.53 | 2.18 | 25% |
| cand P2-3 (round 3) | 2357 | 19.81 | 23.38 | 24.86 | 29.14 | 2357 | 0 | 1.27 | 1.38 | 1.92 | 7.79 | 4.01 | 3.26 | 1.46 | 1.99 | 20% |
| cand E16-19 (quiet) | 2357 | 25.37 | 29.00 | 30.62 | 32.76 | 2357 | 0 | 1.50 | 1.24 | 3.60 | 9.05 | 5.10 | 4.64 | 2.00 | 1.98 | 29% |
| cand E16-19 (round 3) | 2357 | 25.23 | 28.93 | 30.85 | 34.30 | 2357 | 2 | 1.48 | 1.22 | 3.61 | 8.99 | 5.06 | 4.63 | 1.99 | 1.93 | 26% |
| cand E16-19 (contended) | 2357 | 25.01 | 28.05 | 29.59 | 31.82 | 2357 | 0 | 1.46 | 1.18 | 3.56 | 8.92 | 5.09 | 4.58 | 1.93 | 1.97 | 57% |

**L-2h, window u14000+** (build-o2)

| run | ticks | mean | p95 | p99 | max | >4.17 | >33.3 | combat | sep | scripts | movement | nav | other | of which prologue | compact max | other cores busy |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cand P0-1 | 660 | 30.00 | 36.33 | 38.28 | 55.04 | 660 | 170 | 3.58 | 1.79 | 4.16 | 10.49 | 4.08 | 5.51 | 2.70 | 3.07 | 27% |
| base P2-3 | 660 | 29.50 | 35.90 | 37.70 | 54.50 | 660 | 122 | 3.51 | 1.94 | 3.98 | 10.36 | 4.03 | 5.30 | 0.00 | 0.00 | 27% |
| base P0-1 | 660 | 28.89 | 32.60 | 34.20 | 59.70 | 660 | 12 | 3.41 | 1.67 | 3.96 | 10.17 | 4.04 | 5.29 | 0.00 | 0.00 | 32% |
| cand P2-3 | 660 | 29.79 | 34.44 | 36.78 | 70.31 | 660 | 67 | 3.50 | 1.77 | 4.20 | 10.37 | 4.11 | 5.46 | 2.69 | 2.86 | 31% |
| base P0-1 (round 1) | 660 | 31.81 | 36.40 | 38.10 | 59.00 | 660 | 315 | 3.77 | 1.94 | 4.15 | 10.94 | 4.64 | 5.94 | 0.00 | 0.00 | 23% |
| base P2-3 (round 1) | 660 | 29.68 | 35.50 | 37.70 | 62.30 | 660 | 127 | 3.53 | 1.71 | 4.02 | 10.56 | 4.20 | 5.29 | 0.00 | 0.00 | 15% |
| cand P0-1 (round 3) | 660 | 32.01 | 36.69 | 38.51 | 60.11 | 660 | 348 | 3.76 | 1.95 | 4.36 | 11.00 | 4.54 | 5.98 | 2.99 | 3.11 | 25% |
| cand P2-3 (round 3) | 660 | 30.23 | 34.72 | 36.75 | 55.06 | 660 | 111 | 3.59 | 1.98 | 4.22 | 10.48 | 4.09 | 5.48 | 2.69 | 2.67 | 20% |
| cand E16-19 (quiet) | 660 | 37.63 | 43.76 | 45.90 | 65.17 | 660 | 541 | 4.11 | 1.81 | 6.74 | 11.80 | 5.10 | 7.65 | 3.63 | 2.77 | 29% |
| cand E16-19 (round 3) | 660 | 38.48 | 43.88 | 45.57 | 66.16 | 660 | 560 | 4.17 | 1.84 | 6.88 | 11.95 | 5.26 | 7.94 | 3.76 | 2.81 | 26% |
| cand E16-19 (contended) | 660 | 35.51 | 40.01 | 41.90 | 65.46 | 660 | 553 | 3.84 | 1.64 | 6.55 | 11.24 | 4.75 | 7.11 | 3.21 | 2.98 | 57% |

**L-2h, window ticks0-5** (build-o2)

| run | ticks | mean | p95 | p99 | max | >4.17 | >33.3 | combat | sep | scripts | movement | nav | other | of which prologue | compact max | other cores busy |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cand P0-1 | 6 | 24.11 | 55.04 | 55.04 | 55.04 | 6 | 1 | 1.29 | 1.52 | 9.60 | 4.98 | 2.13 | 4.27 | 1.96 | 0.00 | 27% |
| base P2-3 | 6 | 23.98 | 54.50 | 54.50 | 54.50 | 6 | 1 | 1.28 | 1.60 | 9.48 | 4.95 | 2.05 | 4.32 | 0.00 | 0.00 | 27% |
| base P0-1 | 6 | 26.50 | 59.70 | 59.70 | 59.70 | 6 | 1 | 1.35 | 1.93 | 10.45 | 5.33 | 2.18 | 4.88 | 0.00 | 0.00 | 32% |
| cand P2-3 | 6 | 30.92 | 70.31 | 70.31 | 70.31 | 6 | 1 | 1.47 | 1.90 | 12.63 | 6.35 | 2.53 | 5.56 | 2.66 | 0.00 | 31% |
| base P0-1 (round 1) | 6 | 25.97 | 59.00 | 59.00 | 59.00 | 6 | 1 | 1.33 | 1.75 | 9.90 | 5.47 | 2.22 | 4.85 | 0.00 | 0.00 | 23% |
| base P2-3 (round 1) | 6 | 26.20 | 62.30 | 62.30 | 62.30 | 6 | 1 | 1.38 | 1.60 | 10.15 | 5.67 | 2.57 | 4.50 | 0.00 | 0.00 | 15% |
| cand P0-1 (round 3) | 6 | 26.52 | 60.11 | 60.11 | 60.11 | 6 | 1 | 1.39 | 1.91 | 10.36 | 5.55 | 2.26 | 4.65 | 2.13 | 0.00 | 25% |
| cand P2-3 (round 3) | 6 | 24.34 | 55.06 | 55.06 | 55.06 | 6 | 1 | 1.28 | 1.73 | 9.76 | 4.94 | 2.00 | 4.25 | 1.93 | 0.00 | 20% |
| cand E16-19 (quiet) | 6 | 30.64 | 65.17 | 65.17 | 65.17 | 6 | 1 | 1.74 | 1.71 | 12.26 | 6.17 | 2.29 | 6.07 | 2.78 | 0.00 | 29% |
| cand E16-19 (round 3) | 6 | 31.68 | 66.16 | 66.16 | 66.16 | 6 | 1 | 1.81 | 2.04 | 12.43 | 6.25 | 2.38 | 6.32 | 2.81 | 0.00 | 26% |
| cand E16-19 (contended) | 6 | 31.02 | 65.46 | 65.46 | 65.46 | 6 | 1 | 1.75 | 1.88 | 12.25 | 6.19 | 2.26 | 6.25 | 2.81 | 0.00 | 57% |

**R-bench, all ticks** (build-o2; peak about 1.7k units)

| run | ticks | mean | p95 | p99 | max | >4.17 | >33.3 | combat | sep | scripts | movement | nav | other | of which prologue | compact max | other cores busy |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cand P0-1 | 18004 | 1.41 | 2.61 | 2.89 | 8.96 | 1 | 0 | 0.26 | 0.06 | 0.18 | 0.52 | 0.00 | 0.39 | 0.09 | 0.17 | 22% |
| cand P2-3 | 18004 | 1.53 | 2.84 | 3.19 | 9.47 | 4 | 0 | 0.28 | 0.07 | 0.20 | 0.56 | 0.00 | 0.42 | 0.10 | 0.19 | 33% |
| cand E16-19 (quiet) | 18004 | 1.76 | 3.66 | 4.04 | 12.47 | 87 | 0 | 0.30 | 0.07 | 0.28 | 0.63 | 0.00 | 0.47 | 0.10 | 0.27 | 28% |
| cand E16-19 (contended) | 18004 | 2.67 | 5.35 | 5.94 | 12.61 | 2814 | 0 | 0.45 | 0.11 | 0.52 | 0.85 | 0.00 | 0.72 | 0.16 | 0.42 | 41% |

**L-bench, all ticks** (build-o2; peak about 1.7k units)

| run | ticks | mean | p95 | p99 | max | >4.17 | >33.3 | combat | sep | scripts | movement | nav | other | of which prologue | compact max | other cores busy |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cand P0-1 | 18001 | 2.18 | 4.32 | 4.87 | 11.13 | 1124 | 0 | 0.16 | 0.08 | 0.21 | 0.53 | 0.89 | 0.29 | 0.13 | 0.19 | 31% |
| cand P2-3 | 18001 | 2.06 | 4.03 | 4.54 | 10.53 | 644 | 0 | 0.15 | 0.07 | 0.19 | 0.50 | 0.87 | 0.27 | 0.12 | 0.16 | 27% |
| cand E16-19 (quiet) | 18001 | 2.25 | 4.49 | 5.32 | 14.40 | 1254 | 0 | 0.15 | 0.08 | 0.21 | 0.52 | 0.99 | 0.29 | 0.11 | 0.21 | 35% |
| cand E16-19 (contended) | 18001 | 3.18 | 6.41 | 7.26 | 15.06 | 4014 | 0 | 0.21 | 0.12 | 0.50 | 0.71 | 1.19 | 0.44 | 0.17 | 0.29 | 48% |


## Ulasem 8-AI benchmark (TAK_BENCH=4, 240 s at 1x, seed 1)

The client is build-o2 and the referee on the same game is Release. The client is pinned to the pair and
the referee to core 4 or 5; for the E rows, client 16-17 and referee 18-19. The first 30 ticks (load) are
dropped. The game is deterministic: 3537 / 3469 units at peak on every run.

| run | side | ticks | units max | mean | p95 | p99 | max | >4.17 | >33.3 | combat | scripts | movement | nav | other |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ul-R-c01-s4 | client build-o2 | 7170 | 3537 | 6.10 | 8.00 | 8.41 | 15.50 | 6374 | 0 | 1.05 | 0.79 | 2.29 | 0.00 | 1.68 |
| ul-R-c01-s4 | referee Release | 7221 | 3537 | 5.78 | 7.63 | 8.03 | 17.80 | 6356 | 0 | 1.00 | 0.75 | 2.22 | 0.00 | 1.52 |
| ul-R-c23-s5 | client build-o2 | 7170 | 3537 | 5.92 | 8.08 | 9.23 | 12.70 | 6363 | 0 | 1.02 | 0.76 | 2.24 | 0.00 | 1.63 |
| ul-R-c23-s5 | referee Release | 7221 | 3537 | 5.80 | 7.78 | 8.25 | 9.99 | 6384 | 0 | 1.01 | 0.76 | 2.23 | 0.00 | 1.52 |
| ul-R-e16 | client build-o2 | 7170 | 3537 | 7.65 | 9.96 | 10.53 | 11.40 | 6592 | 0 | 1.24 | 1.40 | 2.61 | 0.00 | 2.09 |
| ul-R-e16 | referee Release | 7221 | 3537 | 7.49 | 9.76 | 10.29 | 11.57 | 6582 | 0 | 1.26 | 1.24 | 2.65 | 0.00 | 1.97 |
| ul-L-c01-s4 | client build-o2 | 7170 | 3469 | 8.88 | 11.41 | 12.07 | 18.43 | 6842 | 0 | 0.71 | 0.77 | 3.22 | 2.70 | 1.19 |
| ul-L-c01-s4 | referee Release | 7221 | 3469 | 8.27 | 10.56 | 11.20 | 12.47 | 6827 | 0 | 0.67 | 0.76 | 2.96 | 2.54 | 1.10 |
| ul-L-c23-s5 | client build-o2 | 7170 | 3469 | 9.48 | 12.54 | 14.68 | 17.89 | 6824 | 0 | 0.77 | 0.82 | 3.46 | 2.86 | 1.27 |
| ul-L-c23-s5 | referee Release | 7221 | 3469 | 8.62 | 11.16 | 12.40 | 26.73 | 6818 | 0 | 0.70 | 0.78 | 3.09 | 2.63 | 1.14 |
| ul-L-e16 | client build-o2 | 7170 | 3469 | 11.27 | 14.42 | 15.19 | 16.51 | 6905 | 0 | 0.87 | 1.30 | 3.78 | 3.44 | 1.56 |
| ul-L-e16 | referee Release | 7222 | 3469 | 10.60 | 13.59 | 14.36 | 16.18 | 6908 | 0 | 0.85 | 1.29 | 3.57 | 3.06 | 1.50 |


## crowdbench 2000x1 (3000 ticks, seed 0, `tick_ms_mean`, median of 2 rounds)

Hashes agree across builds and cores. Release and build-o2 are within about 5% of each other in either
direction. The "E16 busy" column ran beside the identity harness, and its Legion rows show how much
contention costs (up to 2.5x).

| mode | scenario | P0 o2 | P0 Release | P2 o2 | P2 Release | E16 o2 | E16 Release | E16 busy o2 | E16 busy Release | hashes |
|---|---|---|---|---|---|---|---|---|---|---|
| retail | open | 0.802 | 0.853 | 0.873 | 0.925 | 0.908 | 0.972 | 0.901 | 0.952 | same |
| retail | doors | 1.121 | 1.149 | 1.206 | 1.194 | 1.309 | 1.313 | 1.260 | 1.272 | same |
| retail | maze | 0.940 | 0.928 | 0.994 | 0.987 | 1.091 | 1.087 | 1.108 | 1.097 | same |
| retail | sharedgoal | 1.316 | 1.356 | 1.399 | 1.391 | 1.552 | 1.516 | 1.467 | 1.454 | same |
| retail | opposingcolumns | 1.005 | 0.998 | 0.998 | 1.035 | 1.114 | 1.153 | 1.145 | 1.178 | same |
| retail | mixedfootprints | 1.509 | 1.571 | 1.603 | 1.575 | 1.861 | 1.767 | 1.832 | 1.796 | same |
| legion | open | 1.080 | 1.078 | 1.099 | 1.098 | 1.267 | 1.202 | 1.444 | 2.006 | same |
| legion | doors | 1.100 | 1.088 | 1.126 | 1.118 | 1.288 | 1.266 | 1.905 | 1.342 | same |
| legion | maze | 1.043 | 1.031 | 1.071 | 1.054 | 1.234 | 1.181 | 1.848 | 2.300 | same |
| legion | sharedgoal | 0.825 | 0.871 | 0.848 | 0.888 | 0.962 | 0.982 | 1.896 | 1.899 | same |
| legion | opposingcolumns | 1.145 | 1.133 | 1.174 | 1.158 | 1.335 | 1.293 | 2.823 | 2.857 | same |
| legion | mixedfootprints | 0.995 | 1.051 | 1.009 | 1.076 | 1.171 | 1.181 | 2.501 | 2.573 | same |


## E3.2: presentation clock (S4), 2026-10-10

The render no longer shows the newest snapshot with a wall-clock interpolation alpha. It keeps a display
time Td (ticks) that advances at the game speed every frame, servoed to stay lagTarget ticks behind the
newest snapshot, and shows ring slot ceil(Td) interpolated by Td - (ceil(Td) - 1). Client only: no sim
or wire change.

- **Ring.** `frameBuf_` is a ring of 12 Frames (the design named 8).
  - The writer fills the oldest slot that is neither published, pinned by the render this frame, nor
    the one shown last frame.
  - 8 was measured too small on the Mac's real 60 Hz display. A frame that misses a vsync (27 ms) takes
    5 ticks at 8x, on top of a lag of 4, so the tick the next frame wanted was overwritten in 50 of 551
    frames. With 12: 5 of 432, all at the start catch-up.
- **lagTarget.** The p98 of the last 128 publish gaps plus 0.25, clamped to 1..4 ticks:
  - 1.25 at 1x;
  - 2.3-2.7 at 8x at 144-240 Hz;
  - 4 at 60 Hz.
- **Base rate.** The game speed, or the measured publish rate (0.25 s EMA) when the game cannot hold
  that speed. So an overloaded game slows smoothly instead of freezing between snapshots.
  - The design's 0.85-1.25 servo band alone left 64-99% freeze frames on the overloaded bench.
- **Corrected servo sign.** The servo is `1 + (lag - lagTarget) / (rate * 0.25 s)`, clamped to 0.85..1.25.
  WE-client.md had the sign the other way round.
- **Display-time queues.** `cosmeticStep` runs on ΔTd. Impacts (`hitQueue_`) are now tick-stamped and
  released by the displayed tick, like the other queues.
- **Start gate.** Td holds the first tick until 15 ticks at 8x are published (scaled to the speed, at
  least 3).
- **TAK_PRESENT=0** (debug) restores the old presentation for A/B runs.

Measured with `tools/pace_check.sh --vsync HZ` (TAK_VSYNC_HZ, a headless deadline loop) at 8x. Setup:
build-o2, `TAK_SIM_THREAD=1`, on a busy machine (cores 22-23 for Inner Circle, 20-23 for the bench).

- **cv** is the coefficient of variation of the displayed game time per frame (the design's metric).
  It also carries the harness's own frame-time jitter: nominal CV is about 12% at 240 Hz here.
- **rate_cv** is the CV of displayed time over wall time (dtd / nominal), which is what the clock
  controls.
- **freeze** is Td held at the newest snapshot.

| run | Hz | cv | rate_cv | freeze | over 1.3x | snaps | hash |
|---|---|---|---|---|---|---|---|
| Inner Circle Legion --mpai 300 s, before | 240 / 144 / 60 | 39.4 / 29.7 / 12.8% | 428 / 40 / 16% | 49.3 / 93.6 / 100% | 2340 / 1078 / 69 | - | b2261dc31dc84cdf |
| Inner Circle Legion --mpai 300 s, S4 | 240 / 144 / 60 | 11.0 / 7.6 / 3.7% | 3.0 / 3.3 / 2.9% | 0.09 / 0.12 / 0.09% | 0 / 0 / 0 | 0 / 0 / 0 | b2261dc31dc84cdf |
| Ulasem 8-AI bench (VERY HIGH) 120 s, TAK_PRESENT=0 | 240 / 144 / 60 | 218 / 184 / 99% | 1374 / 186 / 98% | 88.2 / 89.2 / 88.0% | 25 / 6 / 0 | - | 45c5f6b47c6a0506 |
| Ulasem 8-AI bench (VERY HIGH) 120 s, S4 | 240 / 144 / 60 | 59 / 46 / 71% | 52 / 41 / 73% | 0.22 / 0.42 / 0.50% | 0 / 0 / 0 | 0 / 0 / 0 | 45c5f6b47c6a0506 |

The bench runs heavily overloaded on this box. The client's World::tick takes 34-37 ms against a
4.17 ms budget, with wall/CPU at 2.0-2.2 (preempted). Throughput falls about tenfold as the armies grow,
so the whole-run CV is the slowdown itself.

Over a one-second rolling window, the bench's local rate CV is 13.1 / 12.9 / 14.4% with S4. With
TAK_PRESENT=0 it is 339 / 196 / 99%.

**Real displays** (`TAK_QUIT_TICK=1500`, Ulasem benchmark watch at 8x against an explicit local
takserver; `TAK_PRESENT=1` vs `0` on the same binary):

| host | run | game rate | frame ms p50 / p99 | rate_cv on / off | freeze on / off |
|---|---|---|---|---|---|
| Windows, RTX 5070, D3D11 (scheduled-task desktop session; vsync not engaged) | LOW | 2.3x (server-paced; client world 0.9 ms/tick) | 1 / 17 | 6.1% / 162% | 0.02% / 69.4% |
| Windows, RTX 5070, D3D11 | VERY HIGH | 2.2x | 6 / 23 | 6.3% / 101% | 0.07% / 66.7% |
| MacBook Neo, Metal | LOW | 5.6x | 17 / 30 (60 Hz with missed vsyncs) | 5.2% / 117% | 0.69% / 74.1% (432 frames) |

Hashes are unchanged in every run. Both --mpai Inner Circle 300 s games were inline and seed 1, base
8fcc03d6 vs S4: Legion b2261dc31dc84cdf and Retail b240750e5765c02b on both, with no DESYNCED or
suspect lines.

**Not yet done:** the manual 240 Hz session on the user's display, at 1x and 8x (WE synthesis section 3).

## E3.3: progress acks and an RTT-scaled lead (S3), 2026-10-10

A client-bound game no longer queues 100+ ticks. Net protocol only: no sim, hash or golden change.
It needs a kNetVersion bump; `protocol.h` carries a placeholder note, and the number is assigned when
it lands with the next Legion bump.

- **Msg::Progress** (C->S: `u32 tick`, `u32 rttMs`). A seated or spectating client sends the tick it
  has PROCESSED (the worker's `simProcessedTick_`, never the push position) whenever it has moved
  `kProgressPeriod` = 4 ticks since the last report, at most once per frame. The server moves the
  client's ack tick (same "not closed yet -> ignore" check as StateHash). Progress is never
  hash-checked and never recorded.
- **Lead.** `flowLeadTicks(speed, rttMs) = clamp(4 + 2 * rttTicks + 16, 16, 120)`, per client, where
  rttTicks is the client's smoothed ping at the room's speed. It replaces the flat `kMaxLeadTicks = 120`
  past a hash sent every 30 ticks. A client that has not sent Progress yet (or an older one) keeps
  120. The ping includes one rendered frame of polling delay, so a 60 Hz client gets about 30.
  - The first cut let the client add its jitter-buffer depth to the reserve. Under flow control the
    bundles arrive in bursts of a few ticks, the measured jitter grows, and the lead fed back into
    itself (backlog crept from 27 to 40 over two minutes at 2x). The reserve is fixed at 16.
- **Worker inbox.** A seated player's sim worker is fed only while its inbox holds 2 ticks or fewer at
  the start of the frame, and the 512-tick fast-forward is off for it. Undelivered bundles wait in
  `mp_`, where the jitter servo, the 900-tick guard and the server's flow control see them. Rejoin
  catch-up (`cmdCatchUp_`) and spectators keep the 512 path. The inbox can hold 2 plus one frame's
  budget right after a push (inbox_p99 5-8 at 60 Hz, 16 at 20 fps).
- **Server.** `ackTick` and the lead are reset when a client starts, rejoins or spectates a game (a
  lobby client that played an earlier game used to carry its old ack tick into the next one).
- **TAK_SIM_SLOW_US=N** (debug) sleeps N us per simulated tick to play a slow client;
  `tools/pace_check.sh --slow-us N`. The PACE line gains `backlog_p99` / `backlog_max`: ticks delivered
  but not yet simulated (jitter buffer + worker queue), sampled after each frame's drain.

Measured with `tools/pace_check.sh --seconds 120` (Inner Circle Legion, 8x, `TAK_SIM_THREAD=1`,
AUTOPLAY_FORMATION, cores 22-23 on a busy machine; base 57f506c6 with the same slow-client hook
added locally). The base has no backlog column, so its row is inbox + jitter-buffer depth per frame.

| run | backlog p99 / max, base | backlog p99 / max, S3 | game rate, base / S3 |
|---|---|---|---|
| no slowdown, 60 Hz | 18 / 23 | 13 / 25 | 8.00x / 7.98x |
| slow 6 ms/tick, 60 Hz | 100 / 101 | 25 / 25 | 5.27x / 5.28x |
| slow 6 ms/tick, 240 Hz | 102 / 105 | 21 / 22 | 4.96x / 5.33x |
| slow 6 ms/tick, 20 fps (50 ms frame) | 99 / 102 | 31 / 34 | 5.04x / 4.72x |
| slow 15 ms/tick, 60 Hz | 113 / 115 | 27 / 29 | 2.12x / 2.10x |

- Every row ends on the same hash (da66d89b2ed187e3 at tick 3600; the formation orders are timed by
  the client, and they land on the same ticks in both builds).
- At 8x a backlog of 25 ticks is 0.1 s of command latency (was about 0.4 s).
- A slowed client settles at what it can play (6 ms/tick allows at most 5.3x), and the server follows
  it in small steps instead of 30-tick bursts.
- **Cost at 20 fps:** 6% throughput (4.72x vs 5.04x). With a 50 ms frame the worker runs dry between
  frames once the 2-tick inbox is spent. At 60 Hz and above there is no loss.

Hashes: --mpai Inner Circle 300 s, seed 1, clean XDG: Legion b2261dc31dc84cdf, Retail b240750e5765c02b,
Legion with `TAK_SIM_THREAD=1` b2261dc31dc84cdf (all equal to E3.2), no DESYNCED / suspect. The two
recordings replay with `TAK_REPLAY_VERIFY=1` without divergence, to the same final hashes as
the base binary (Legion f2e4e1be92069a1a, Retail ff78a0babb848c38 at tick 9002). Rejoin and spectate: Ulasem Arena, host +
joiner + 2 AIs at 4x, joiner killed after its game had run 15 s and rejoined with `--mprejoin`
(replaying 1881 and 1885 ticks in two runs), plus a `--mpspectate` client joining mid-game: host, rejoiner and spectator
all end on 78015ff081df2d52 at tick 4500, no DESYNCED / suspect.

## E4.5: staged stress fill (B2), 2026-10-11

Stress runs (`TAK_STRESS`, room option `stressTest`) no longer open with one huge tick. Bench/stress
harness hygiene only: it is not a game fix, and normal games do not change (identity below).

- **Staging.** `setupMatch` still plans the whole fill at setup (same roster, round robin, lattice and
  `snapSpawn` claims, so every body gets the same type and position as before), but puts it into the
  staged spawn plan instead of spawning it: unit k of a player's army of `target` appears at tick
  `1 + k * 30 / target` (`kStressStageTicks` = 30, about 507 bodies a tick for 8 x 1900). Each army
  grows evenly. `World::setStagedSpawns` installs the plan without making the run a benchmark (no end
  tick, Monarchs stay mortal). With a benchmark on too, the two plans are merged in tick order.
- **Old stress replays stay exact.** The room option is now 0 = off, 1 = `kStressImmediate` (the old
  all-at-setup fill), 2 = `kStressStaged` (what `TAK_STRESS` asks for). Replays recorded before this
  carry 1 and replay with the immediate fill, so L-2h / R-2h (stress recordings) and their harvested
  timing situations are unchanged. Fixtures that inspect or order the army right after setup
  (simperf, the patrol perf fixture, placement_test, retailgap_test) set `MatchConfig::stressImmediate`.
  The server keeps the value (it used to fold it to 0/1). A client from before this sees 2 as "on" and
  would fill immediately: stress games only, and no kNetVersion bump was taken (it rides the next one).
- **What the opening tick was.** Staging alone moved most of the cost but left two in-tick costs:
  (1) `retailRotationCoefficients` builds a process-wide 65536-entry Q60 table on first use (20-54 ms);
  its first user is the first non-upright ground-support spawn, which used to be at setup and was now
  tick 1. `setupMatch` builds it up front (pure and immutable, so hash-neutral; this also removes the
  same one-off from the first such unit a normal game produces). (2) `units_` reallocating its 1.3 KB
  records during the fill (up to 6 ms once ~15k are copied, and an exact reservation overflowed on the
  last stage because the AIs produce meanwhile): `setStagedSpawns` reserves the plan plus 25%.
- **Check.** placement_test `staged stress` (both balances, with and without a benchmark): the opening
  holds only the 8 Monarchs, 3800 bodies are staged over ticks 1..30 (peak 128 a tick at cap 500), and
  after tick 30 the set of (player, type, x, z) equals the immediate fill's. A seated 8-player staged
  stress game (7 AIs + host, 3038 ticks at 4x) raised no desync, and its replay verifies (100 hash
  checkpoints).

Opening ticks, client sim, Ulasem 8 x 1900 (`TAK_STRESS=1 TAK_UNITCAP=2000`, 8 AIs, watch), build-o2,
`TAK_PHASE`, under `timing.sh` (cores 0-3) on a busy box; three runs each:

| | ticks 1-3 (ms) | worst of ticks 1-40 | mean of ticks 61-100 |
|---|---|---|---|
| base (a7a7902a) | 29.8 26.8 51.8 / 23.5 27.9 54.1 / 34.8 25.6 53.6 | 51.8 / 54.1 / 53.6 (tick 3) | 20.2 / 21.3 / 19.3 |
| E4.5 | 7.1 6.1 6.2 / 6.1 3.4 4.4 / 7.1 6.8 7.7 | 19.5 / 19.9 / 24.7 | 19.1 / 19.1 / 22.6 |

The base's spawn hitch is 32-35 ms above its steady tick (tick 3: scripts 37 ms + first movement 24 ms
on this run); with E4.5 no opening tick is more than about 2 ms above the steady tick, and ticks 1-3
run 3-8 ms.

## Reproducing

```sh
# counters (deterministic; any core), one SIMSTATS line per tick
TAK_SIMSTATS=1 TAK_PHASE=1 TAK_PHASE_MS=0 TAK_REPLAY_VERIFY=1 TAK_HEADLESS=1 SDL_VIDEODRIVER=dummy \
  taskset -c 16-17 build-o2/takclient replay R-2h.takrep --data <install>
# PMU split (pin to ONE core type), one SIMPMU line per tick
TAK_PMU=1 TAK_PHASE=1 TAK_PHASE_MS=0 ... taskset -c 0-1 build-o2/takclient replay R-2h.takrep --data <install>
# presentation / worker columns
tools/pace_check.sh build-o2 --cpus 4-5     # PACE + PACEWORK lines, pace.txt per frame
tools/pace_check.sh build-o2 --vsync 240 --seconds 300            # + PRESENT (E3.2) at a 240 Hz fake vsync
tools/pace_check.sh build-o2 --vsync 60 --bench 4 --no-formation  # the 8-AI Ulasem bench instead
tools/pace_check.sh build-o2 --vsync 60 --slow-us 6000 --seconds 120  # a slow client: backlog_p99 (E3.3)
# a real display (debug build): TAK_MPAUTO=1 TAK_BENCH=1 TAK_SPEED=80 TAK_QUIT_TICK=1500 TAK_PACELOG=pace.txt \
#   takclient game "Ulasem Arena" --data <install> --server 127.0.0.1 --serverport <port of a local takserver>
```
