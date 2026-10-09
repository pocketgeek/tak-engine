# Legion scenario keys: audit definitions

The `.scn` fixtures in `tools/scenarios/` measure Legion with the shared observer
(`tools/legion_observe.h`). Several audit numbers came from throwaway probes with
their own definitions (the parked lanes2 `laneRun` and the `aware` world test).
This page says which `.scn` key reproduces which audit number, and which keys are
report-only because their definition differs. Grammar: `tools/scenarios/README.scn-format.txt`.

All values below are one run at start offset 0 in Legion mode on optimized Debug,
the same single run the audit quoted. The committed baseline gates the median of
five offsets (0, +1, -1, +2, -2), which can differ a lot from offset 0 on keys with a
large offset spread.

## Keys that reproduce an audit number

| Fixture | Key | Definition | Audit | `.scn` | Verdict |
|---|---|---|---|---|---|
| island-1x48 | `lane.cross.swaps` / `.pairs` | first entry into x=70 and x=130 (lateral z), pairs entering x=70 within 300 ticks of each other, swapped by >= 2 cells at both lines | 33 / 978 | 33 / 978 | exact |
| island-1x48 | `gate.strip.files_x100` | distinct z/2 files among members in x 95-105, mean over 10-tick samples with >= 3 inside | 3.02 | 3.01 | exact within rounding |
| wall-1x48 | `lane.cross.swaps` / `.pairs` | first entry into z=60 west of the wall (lateral 98-x) and east of it (lateral x-102), 300 ticks, >= 2 cells | 217 / 1123 | 208 / 1123 | -4%, comparable |
| wall-1x48 | `gate.strip.files_x100` | the same files measure over x 97-102, z < 48 | 4.48 | 4.47 | exact within rounding |
| wall-4x50 | `lane.cross.swaps` / `.pairs` | as wall-1x48 with `across=all`: pairs of members of different groups count (the probe ran all 200 bodies as one set) | 1223 / 6821 | 1308 / 7010 | +7%, comparable |
| wall-4x50 | `gate.strip.files_x100` | as wall-1x48 | 4.97 | 4.96 | exact within rounding |
| aware-headon | `pair.contacts.permille_x100` | live ordered a x b pairs sampled every 10 ticks whose centre cells are within 2 on both axes, per mille x 100 | 3.15 | 3.13 | -0.6% |
| aware-unseen | `pair.contacts.permille_x100` | as above | 7.45 | 7.54 | +1.2% |
| refreshchurn | `work.refresh_completed.total` | field refreshes installed in 900 ticks under a 2x2 feature placed in a fresh cell every tick | 11 on main (1 before b565029) | 10 | comparable; the control without churn gives 0 |

The three remaining aware fixtures (cross, seen, attack) have no audit number for
contacts; their `pair.contacts.permille_x100` (12.5, 2.8, 6.7 per mille at offset 0)
are new keys, not comparisons.

Why the wall crossings are not identical: the probe sampled the cell before and after
each tick and recorded a directional crossing of the line; the observer records the
first sample inside a one-cell strip (and a wall line is restricted by x to the side
being measured, so the return pass is not confused with the outbound one). Bodies
move less than one cell per tick, so both see almost the same entries; the 4-7%
difference is the sampling point. These keys are gated as ordinary bands on the
median of five offsets.

## Report-only keys

These keys exist and are in the baseline where listed, but they do not measure the
audit's quantity, so do not compare them with an audit number:

- `gate.NAME.crossings` on every gate (`gate.top`, `gate.lane`, `gate.abreast`,
  `gate.strip`, `gate.mid...`). This is the observer's `pw.py` strip count: two
  same-group members that traversed the strip the same way within `pairWindow`
  ticks flip lateral order at both end windows by at least one body width. The
  audit's crossings are the `lane.*` swaps above. On island-1x48 the gate counts
  29 / 1128 where the audit has 33 / 978; on wall-1x48 36 and on wall-4x50 30
  against 217 and 1223.
- `gate.abreast.files_x100` on island-1x48 (an 8-cell strip, 2.51 against 3.02):
  use `gate.strip.files_x100`.
- `stop_go` everywhere: the observer counts a start after a stop per tick, the
  audit counted moving to still transitions between 10-tick samples inside a
  region (`flips` is the nearest comparable key).
- `stopped_permille` on wall-4x50 (24.0% against 16.5%): the observer counts speed
  zero at a decision sample, the audit counted an unchanged position, which a
  shuffling body defeats.
- island-1x150 and strait-3x80, which depend on how a selection of more than 64
  bodies reaches the server: `.scn` orders go through the client's
  64-per-tick uplink (`issueSelection`), the probes issued every order in one
  tick. They are known-failing or report-only until W3 changes the order merge.
- gapsweep gaps 8, 14 and 20 (five groups share one navigator) and every fixture
  listed as KNOWN-FAILING in its header.

## Grammar added for these keys

- `gate NAME x0 z0 x1 z1 [lateral=x|z] [band=N] [mincount=N] [edge=N] [pairwindow=N] [flip=N]`
- `lane NAME BX0 BZ0 BX1 BZ1 AX0 AZ0 AX1 AZ1 [bsign=-1|1] [asign=-1|1] [window=N] [mincells=N] [across=all]`
- `pair NAME LISTA LISTB [cells=N]` (comma lists of groups, a trailing `*` is a prefix)
- `churn X Z W H every=N [walk=DX,DZ,ROW] [toggle] [from=T] [until=T]`

`churn` is the one directive that edits the world mid-run: `OrderFeed::apply`
places the feature before the tick's orders, so every consumer of the feed (the
runner, the tests) sees the same schedule.

## Tier-2 counter cost (W0 follow-up A2)

Release, crowdbench `--mode legion --units 2000 --players 1 --moving-percent 100 --ticks 600 --seed 1`,
cores 18-21 (no SMT siblings), 14 interleaved A/B pairs (alternating order), A = counters always on,
B = tier-2 counters (traceCells, passScanCells, groupLoopIters, shareScanIters, awarePairs) summed in
debug builds only. Identical hash for A and B in both scenarios (no sim state touched).

| scenario   | median A ms/tick | median B ms/tick | paired B/A median | 95% CI (bootstrap) |
|------------|------------------|------------------|-------------------|--------------------|
| open       | 1.652            | 1.652            | 0.990             | [0.974, 1.008]     |
| sharedgoal | 1.149            | 1.164            | 1.016             | [1.006, 1.026]     |

Gating the counters off did not make a tick cheaper (open is within noise, sharedgoal reads slightly
slower), so the always-on counters stay: the "> 1% and CI excludes 1%" rule for moving them behind
NDEBUG is not met.
