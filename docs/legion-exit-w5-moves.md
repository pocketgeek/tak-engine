# W5 exit: every key moved by more than 5% against the step-0 sim

Base = the W5 step-0 sim (`task-w5-s0` `2b2dc14c`, the W4 land `6333e38d` plus instrument-only commits; every Legion and
Retail hash equals W4's), new = `task-w5-land` (protocol 243). Both swept on Windows (optimized Debug), all 120 files, both
modes, the gate offsets (keys as the run lines report them: small-count keys on the gate-offset median, the rest on the core
five), serial == workers on all 240 lines. Only 26 Legion scenarios change state at all: the 21 W5 fixtures, the four battle
scenarios and `unreach-200` (hash only, no key moves by more than 5%); every other scenario, and every Retail line, is
hash-identical to the base. *intended*: the W5 theme keys (reach, damage, chase registrations and fields, goal-block and
late-block arrivals, the new counters); *incidental (within band)*: everything else, which for the 21 fixtures is the cost of
the intended change (an army that used to string out now stands round its target and fires: spins, reversals, statue / hold
ticks) and for the four battles ring planning and its bodies standing in the way (docs/legion-exit-tables.md, "W5 exit").
No Retail key moved. The `deadend-*` and `lateblock-before-16`, `liftstream`, `staticblock-*` rows are MV-05's
`group_loop_iters` (one row each).

| key | old | new | change | kind |
|---|---|---|---|---|
| attackring-f1-16/legion/aim_reversals | 0 | 7 | new | incidental (within band) |
| attackring-f1-16/legion/back | 0 | 4 | new | incidental (within band) |
| attackring-f1-16/legion/backward | 0 | 1 | new | incidental (within band) |
| attackring-f1-16/legion/churn.bin0 | 552218 | 401980 | -27.2% | incidental (within band) |
| attackring-f1-16/legion/churn.bin1 | 34480 | 900 | -97.4% | incidental (within band) |
| attackring-f1-16/legion/contact_own_permille | 940 | 768 | -18.3% | incidental (within band) |
| attackring-f1-16/legion/crawl_samples | 2 | 73 | +3550.0% | incidental (within band) |
| attackring-f1-16/legion/detour_permille | 1 | 5 | +400.0% | incidental (within band) |
| attackring-f1-16/legion/engaged.member_ticks | 2244 | 32251 | +1337.2% | intended |
| attackring-f1-16/legion/engagement_on_flowing | 12 | 9 | -25.0% | incidental (within band) |
| attackring-f1-16/legion/flip_rate_per30_permille | 10 | 19 | +90.0% | incidental (within band) |
| attackring-f1-16/legion/flips | 16 | 30 | +87.5% | incidental (within band) |
| attackring-f1-16/legion/follow_chain_max | 13 | 5 | -61.5% | incidental (within band) |
| attackring-f1-16/legion/follow_chain_mean_x100 | 969 | 320 | -67.0% | incidental (within band) |
| attackring-f1-16/legion/gauge.engaged_max | 1 | 16 | +1500.0% | intended |
| attackring-f1-16/legion/parked_no_progress | 2094 | 0 | -100.0% | incidental (within band) |
| attackring-f1-16/legion/reach.ring.damage_end | 75 | 1082 | +1342.7% | intended |
| attackring-f1-16/legion/reach.ring.damage_t1200 | 15 | 131 | +773.3% | intended |
| attackring-f1-16/legion/reach.ring.damage_t2400 | 55 | 762 | +1285.5% | intended |
| attackring-f1-16/legion/reach.ring.ever_end | 1 | 16 | +1500.0% | intended |
| attackring-f1-16/legion/reach.ring.ever_t1200 | 1 | 13 | +1200.0% | intended |
| attackring-f1-16/legion/reach.ring.ever_t2400 | 1 | 16 | +1500.0% | intended |
| attackring-f1-16/legion/reach.ring.farthest_held | 20 | -1 | -105.0% | intended |
| attackring-f1-16/legion/reach.ring.hold_out_max | 2202 | 83 | -96.2% | intended |
| attackring-f1-16/legion/reach.ring.now_end | 1 | 16 | +1500.0% | intended |
| attackring-f1-16/legion/reach.ring.out600_end | 15 | 0 | -100.0% | intended |
| attackring-f1-16/legion/reach.ring.out600_peak | 15 | 0 | -100.0% | intended |
| attackring-f1-16/legion/reach.ring.t50 | -1 | 927 | +92800.0% | intended |
| attackring-f1-16/legion/reversals | 0 | 1 | new | incidental (within band) |
| attackring-f1-16/legion/sideways | 16 | 33 | +106.2% | incidental (within band) |
| attackring-f1-16/legion/spins | 1 | 53 | +5200.0% | incidental (within band) |
| attackring-f1-16/legion/statue_ticks | 0 | 683 | new | incidental (within band) |
| attackring-f1-16/legion/stop_go | 29 | 18 | -37.9% | incidental (within band) |
| attackring-f1-16/legion/waiting_held | 2116 | 2589 | +22.4% | incidental (within band) |
| attackring-f1-16/legion/waiting_no_progress | 141 | 0 | -100.0% | incidental (within band) |
| attackring-f1-16/legion/wall_samples | 1392 | 1547 | +11.1% | incidental (within band) |
| attackring-f1-16/legion/work.held_rechecks.max | 15 | 1 | -93.3% | incidental (within band) |
| attackring-f1-16/legion/work.held_rechecks.p99 | 15 | 1 | -93.3% | incidental (within band) |
| attackring-f1-16/legion/work.held_rechecks.total | 31005 | 47 | -99.8% | incidental (within band) |
| attackring-f1-16/legion/work.holds.max | 3 | 1 | -66.7% | incidental (within band) |
| attackring-f1-16/legion/work.holds.p99 | 1 | 0 | -100.0% | incidental (within band) |
| attackring-f1-16/legion/work.holds.total | 44 | 18 | -59.1% | incidental (within band) |
| attackring-f1-16/legion/work.legion_total.p99 | 840 | 730 | -13.1% | incidental (within band) |
| attackring-f1-16/legion/work.legion_total.total | 586698 | 402880 | -31.3% | incidental (within band) |
| attackring-f1-16/legion/work.line_sweeps.max | 64 | 18 | -71.9% | incidental (within band) |
| attackring-f1-16/legion/work.line_sweeps.p99 | 56 | 10 | -82.1% | incidental (within band) |
| attackring-f1-16/legion/work.line_sweeps.total | 37483 | 3104 | -91.7% | incidental (within band) |
| attackring-f1-16/legion/work.move_calls_by_state_1.total | 13777 | 15243 | +10.6% | incidental (within band) |
| attackring-f1-16/legion/work.move_calls_by_state_2.max | 15 | 4 | -73.3% | incidental (within band) |
| attackring-f1-16/legion/work.move_calls_by_state_2.p99 | 15 | 3 | -80.0% | incidental (within band) |
| attackring-f1-16/legion/work.move_calls_by_state_2.total | 31947 | 474 | -98.5% | incidental (within band) |
| attackring-f1-16/legion/work.moves.total | 30590 | 15714 | -48.6% | incidental (within band) |
| attackring-f1-16/legion/work.pass_scans_skipped.total | 22674 | 13012 | -42.6% | incidental (within band) |
| attackring-f1-16/legion/work.slides.max | 7 | 1 | -85.7% | incidental (within band) |
| attackring-f1-16/legion/work.slides.p99 | 2 | 0 | -100.0% | incidental (within band) |
| attackring-f1-16/legion/work.slides.total | 113 | 8 | -92.9% | incidental (within band) |
| attackring-f1-16/legion/work.still_units_processed.total | 101 | 1173 | +1061.4% | incidental (within band) |
| attackring-f1-16/legion/work.trace_cells.p99 | 840 | 730 | -13.1% | incidental (within band) |
| attackring-f1-16/legion/work.trace_cells.total | 299684 | 160233 | -46.5% | incidental (within band) |
| attackring-f1-64/legion/aim_reversals | 1 | 297 | +29600.0% | incidental (within band) |
| attackring-f1-64/legion/back | 0 | 269 | new | incidental (within band) |
| attackring-f1-64/legion/backward | 0 | 30 | new | incidental (within band) |
| attackring-f1-64/legion/churn.bin0 | 1335655 | 1031691 | -22.8% | incidental (within band) |
| attackring-f1-64/legion/churn.bin1 | 139912 | 99586 | -28.8% | incidental (within band) |
| attackring-f1-64/legion/contact_own_permille | 972 | 914 | -6.0% | incidental (within band) |
| attackring-f1-64/legion/crawl_samples | 154 | 3174 | +1961.0% | incidental (within band) |
| attackring-f1-64/legion/detour_permille | 12 | 88 | +633.3% | incidental (within band) |
| attackring-f1-64/legion/engaged.member_ticks | 6462 | 69227 | +971.3% | intended |
| attackring-f1-64/legion/engagement_on_flowing | 15 | 49 | +226.7% | incidental (within band) |
| attackring-f1-64/legion/flip_rate_per30_permille | 71 | 119 | +67.6% | incidental (within band) |
| attackring-f1-64/legion/flips | 450 | 756 | +68.0% | incidental (within band) |
| attackring-f1-64/legion/follow_chain_max | 40 | 5 | -87.5% | incidental (within band) |
| attackring-f1-64/legion/follow_chain_mean_x100 | 1169 | 252 | -78.4% | incidental (within band) |
| attackring-f1-64/legion/gauge.engaged_max | 3 | 42 | +1300.0% | intended |
| attackring-f1-64/legion/open_still900 | 64 | 40 | -37.5% | incidental (within band) |
| attackring-f1-64/legion/parked_held | 119398 | 70731 | -40.8% | incidental (within band) |
| attackring-f1-64/legion/parked_no_progress | 6012 | 0 | -100.0% | incidental (within band) |
| attackring-f1-64/legion/reach.ring.damage_end | 217 | 2326 | +971.9% | intended |
| attackring-f1-64/legion/reach.ring.damage_t1200 | 37 | 193 | +421.6% | intended |
| attackring-f1-64/legion/reach.ring.damage_t2400 | 157 | 1492 | +850.3% | intended |
| attackring-f1-64/legion/reach.ring.ever_end | 3 | 39 | +1200.0% | intended |
| attackring-f1-64/legion/reach.ring.ever_t1200 | 3 | 21 | +600.0% | intended |
| attackring-f1-64/legion/reach.ring.ever_t2400 | 3 | 37 | +1133.3% | intended |
| attackring-f1-64/legion/reach.ring.farthest_held | 58 | 6 | -89.7% | intended |
| attackring-f1-64/legion/reach.ring.hold_out_max | 2192 | 1044 | -52.4% | intended |
| attackring-f1-64/legion/reach.ring.now_end | 3 | 39 | +1200.0% | intended |
| attackring-f1-64/legion/reach.ring.out600_end | 61 | 8 | -86.9% | intended |
| attackring-f1-64/legion/reach.ring.out600_peak | 61 | 8 | -86.9% | intended |
| attackring-f1-64/legion/reach.ring.t50 | -1 | 1671 | +167200.0% | intended |
| attackring-f1-64/legion/reversals | 0 | 52 | new | incidental (within band) |
| attackring-f1-64/legion/sideways | 156 | 1600 | +925.6% | incidental (within band) |
| attackring-f1-64/legion/spins | 3 | 191 | +6266.7% | incidental (within band) |
| attackring-f1-64/legion/statue_ticks | 290 | 29502 | +10073.1% | incidental (within band) |
| attackring-f1-64/legion/still900_ever | 64 | 40 | -37.5% | incidental (within band) |
| attackring-f1-64/legion/stop_go | 968 | 650 | -32.9% | incidental (within band) |
| attackring-f1-64/legion/stopped_permille | 753 | 513 | -31.9% | incidental (within band) |
| attackring-f1-64/legion/waiting_held | 13439 | 21795 | +62.2% | incidental (within band) |
| attackring-f1-64/legion/waiting_no_progress | 423 | 0 | -100.0% | incidental (within band) |
| attackring-f1-64/legion/wall_samples | 5239 | 9905 | +89.1% | incidental (within band) |
| attackring-f1-64/legion/work.held_rechecks.max | 61 | 17 | -72.1% | incidental (within band) |
| attackring-f1-64/legion/work.held_rechecks.p99 | 61 | 17 | -72.1% | incidental (within band) |
| attackring-f1-64/legion/work.held_rechecks.total | 125405 | 11926 | -90.5% | incidental (within band) |
| attackring-f1-64/legion/work.holds.max | 9 | 3 | -66.7% | incidental (within band) |
| attackring-f1-64/legion/work.holds.p99 | 5 | 2 | -60.0% | incidental (within band) |
| attackring-f1-64/legion/work.holds.total | 1029 | 669 | -35.0% | incidental (within band) |
| attackring-f1-64/legion/work.legion_total.p99 | 2707 | 2121 | -21.6% | incidental (within band) |
| attackring-f1-64/legion/work.legion_total.total | 1475567 | 1131277 | -23.3% | incidental (within band) |
| attackring-f1-64/legion/work.line_sweeps.max | 228 | 64 | -71.9% | incidental (within band) |
| attackring-f1-64/legion/work.line_sweeps.p99 | 196 | 36 | -81.6% | incidental (within band) |
| attackring-f1-64/legion/work.line_sweeps.total | 103526 | 22147 | -78.6% | incidental (within band) |
| attackring-f1-64/legion/work.move_calls_by_state_1.total | 47527 | 93555 | +96.8% | incidental (within band) |
| attackring-f1-64/legion/work.move_calls_by_state_2.max | 61 | 31 | -49.2% | incidental (within band) |
| attackring-f1-64/legion/work.move_calls_by_state_2.p99 | 61 | 26 | -57.4% | incidental (within band) |
| attackring-f1-64/legion/work.move_calls_by_state_2.total | 137883 | 29090 | -78.9% | incidental (within band) |
| attackring-f1-64/legion/work.moves.total | 124366 | 117418 | -5.6% | incidental (within band) |
| attackring-f1-64/legion/work.pass_scan_cells.max | 276 | 24 | -91.3% | incidental (within band) |
| attackring-f1-64/legion/work.pass_scan_cells.p99 | 174 | 6 | -96.6% | incidental (within band) |
| attackring-f1-64/legion/work.pass_scan_cells.total | 12228 | 834 | -93.2% | incidental (within band) |
| attackring-f1-64/legion/work.pass_scans.max | 46 | 4 | -91.3% | incidental (within band) |
| attackring-f1-64/legion/work.pass_scans.p99 | 29 | 1 | -96.6% | incidental (within band) |
| attackring-f1-64/legion/work.pass_scans.total | 2038 | 139 | -93.2% | incidental (within band) |
| attackring-f1-64/legion/work.pass_scans_skipped.total | 94469 | 53527 | -43.3% | incidental (within band) |
| attackring-f1-64/legion/work.slides.max | 8 | 2 | -75.0% | incidental (within band) |
| attackring-f1-64/legion/work.slides.p99 | 5 | 1 | -80.0% | incidental (within band) |
| attackring-f1-64/legion/work.slides.total | 1362 | 364 | -73.3% | incidental (within band) |
| attackring-f1-64/legion/work.still_units_processed.p99 | 1 | 3 | +200.0% | incidental (within band) |
| attackring-f1-64/legion/work.still_units_processed.total | 103 | 2452 | +2280.6% | incidental (within band) |
| attackring-f1-64/legion/work.trace_cells.p99 | 2568 | 2121 | -17.4% | incidental (within band) |
| attackring-f1-64/legion/work.trace_cells.total | 1030587 | 722402 | -29.9% | incidental (within band) |
| attackring-f2-64/legion/aim_reversals | 14 | 185 | +1221.4% | incidental (within band) |
| attackring-f2-64/legion/back | 0 | 161 | new | incidental (within band) |
| attackring-f2-64/legion/backward | 0 | 11 | new | incidental (within band) |
| attackring-f2-64/legion/churn.bin0 | 1212263 | 983925 | -18.8% | incidental (within band) |
| attackring-f2-64/legion/crawl_samples | 148 | 2774 | +1774.3% | incidental (within band) |
| attackring-f2-64/legion/detour_permille | 20 | 88 | +340.0% | incidental (within band) |
| attackring-f2-64/legion/engaged.member_ticks | 8643 | 18020 | +108.5% | intended |
| attackring-f2-64/legion/engagement_on_flowing | 16 | 49 | +206.2% | incidental (within band) |
| attackring-f2-64/legion/flip_rate_per30_permille | 68 | 110 | +61.8% | incidental (within band) |
| attackring-f2-64/legion/flips | 434 | 700 | +61.3% | incidental (within band) |
| attackring-f2-64/legion/follow_chain_max | 17 | 9 | -47.1% | incidental (within band) |
| attackring-f2-64/legion/follow_chain_mean_x100 | 591 | 370 | -37.4% | incidental (within band) |
| attackring-f2-64/legion/gauge.engaged_max | 5 | 9 | +80.0% | intended |
| attackring-f2-64/legion/open_still900 | 64 | 45 | -29.7% | incidental (within band) |
| attackring-f2-64/legion/parked_held | 102509 | 75835 | -26.0% | incidental (within band) |
| attackring-f2-64/legion/parked_no_progress | 7893 | 0 | -100.0% | incidental (within band) |
| attackring-f2-64/legion/reach.ring.damage_end | 291 | 605 | +107.9% | intended |
| attackring-f2-64/legion/reach.ring.damage_t1200 | 18 | 100 | +455.6% | intended |
| attackring-f2-64/legion/reach.ring.damage_t2400 | 191 | 425 | +122.5% | intended |
| attackring-f2-64/legion/reach.ring.ever_end | 5 | 9 | +80.0% | intended |
| attackring-f2-64/legion/reach.ring.ever_t1200 | 1 | 7 | +600.0% | intended |
| attackring-f2-64/legion/reach.ring.ever_t2400 | 5 | 9 | +80.0% | intended |
| attackring-f2-64/legion/reach.ring.farthest_held | 69 | 11 | -84.1% | intended |
| attackring-f2-64/legion/reach.ring.hold_out_max | 2266 | 2142 | -5.5% | intended |
| attackring-f2-64/legion/reach.ring.now_end | 5 | 9 | +80.0% | intended |
| attackring-f2-64/legion/reach.ring.out600_end | 59 | 43 | -27.1% | intended |
| attackring-f2-64/legion/reach.ring.out600_peak | 59 | 43 | -27.1% | intended |
| attackring-f2-64/legion/reversals | 0 | 25 | new | incidental (within band) |
| attackring-f2-64/legion/sideways | 6424 | 1431 | -77.7% | incidental (within band) |
| attackring-f2-64/legion/spins | 5 | 24 | +380.0% | incidental (within band) |
| attackring-f2-64/legion/statue_ticks | 110 | 26778 | +24243.6% | incidental (within band) |
| attackring-f2-64/legion/still900_ever | 64 | 45 | -29.7% | incidental (within band) |
| attackring-f2-64/legion/stop_go | 1175 | 589 | -49.9% | incidental (within band) |
| attackring-f2-64/legion/stopped_permille | 676 | 541 | -20.0% | incidental (within band) |
| attackring-f2-64/legion/waiting_held | 11567 | 22708 | +96.3% | incidental (within band) |
| attackring-f2-64/legion/waiting_no_progress | 705 | 0 | -100.0% | incidental (within band) |
| attackring-f2-64/legion/wall_samples | 6897 | 9308 | +35.0% | incidental (within band) |
| attackring-f2-64/legion/work.held_rechecks.max | 59 | 51 | -13.6% | incidental (within band) |
| attackring-f2-64/legion/work.held_rechecks.p99 | 59 | 51 | -13.6% | incidental (within band) |
| attackring-f2-64/legion/work.held_rechecks.total | 107960 | 67802 | -37.2% | incidental (within band) |
| attackring-f2-64/legion/work.holds.max | 7 | 4 | -42.9% | incidental (within band) |
| attackring-f2-64/legion/work.holds.p99 | 4 | 2 | -50.0% | incidental (within band) |
| attackring-f2-64/legion/work.holds.total | 1234 | 640 | -48.1% | incidental (within band) |
| attackring-f2-64/legion/work.legion_total.p99 | 2756 | 2348 | -14.8% | incidental (within band) |
| attackring-f2-64/legion/work.legion_total.total | 1364325 | 1128359 | -17.3% | incidental (within band) |
| attackring-f2-64/legion/work.line_sweeps.max | 238 | 64 | -73.1% | incidental (within band) |
| attackring-f2-64/legion/work.line_sweeps.p99 | 138 | 34 | -75.4% | incidental (within band) |
| attackring-f2-64/legion/work.line_sweeps.total | 64707 | 18890 | -70.8% | incidental (within band) |
| attackring-f2-64/legion/work.move_calls_by_state_1.total | 62400 | 88346 | +41.6% | incidental (within band) |
| attackring-f2-64/legion/work.move_calls_by_state_2.max | 59 | 52 | -11.9% | incidental (within band) |
| attackring-f2-64/legion/work.move_calls_by_state_2.p99 | 59 | 51 | -13.6% | incidental (within band) |
| attackring-f2-64/legion/work.move_calls_by_state_2.total | 120829 | 85506 | -29.2% | incidental (within band) |
| attackring-f2-64/legion/work.moves.total | 131359 | 143025 | +8.9% | incidental (within band) |
| attackring-f2-64/legion/work.pass_scan_cells.max | 960 | 48 | -95.0% | incidental (within band) |
| attackring-f2-64/legion/work.pass_scan_cells.p99 | 312 | 24 | -92.3% | incidental (within band) |
| attackring-f2-64/legion/work.pass_scan_cells.total | 63312 | 10080 | -84.1% | incidental (within band) |
| attackring-f2-64/legion/work.pass_scans.max | 40 | 2 | -95.0% | incidental (within band) |
| attackring-f2-64/legion/work.pass_scans.p99 | 13 | 1 | -92.3% | incidental (within band) |
| attackring-f2-64/legion/work.pass_scans.total | 2638 | 420 | -84.1% | incidental (within band) |
| attackring-f2-64/legion/work.pass_scans_skipped.total | 76592 | 48632 | -36.5% | incidental (within band) |
| attackring-f2-64/legion/work.slides.max | 11 | 10 | -9.1% | incidental (within band) |
| attackring-f2-64/legion/work.slides.total | 2741 | 1414 | -48.4% | incidental (within band) |
| attackring-f2-64/legion/work.still_units_processed.p99 | 1 | 2 | +100.0% | incidental (within band) |
| attackring-f2-64/legion/work.still_units_processed.total | 103 | 1380 | +1239.8% | incidental (within band) |
| attackring-f2-64/legion/work.trace_cells.total | 877634 | 678243 | -22.7% | incidental (within band) |
| attackring-fight-60/legion/aim_reversals | 52 | 325 | +525.0% | incidental (within band) |
| attackring-fight-60/legion/back | 13 | 245 | +1784.6% | incidental (within band) |
| attackring-fight-60/legion/backward | 14 | 33 | +135.7% | incidental (within band) |
| attackring-fight-60/legion/churn.bin0 | 2007127 | 1352713 | -32.6% | incidental (within band) |
| attackring-fight-60/legion/churn.bin1 | 370246 | 173805 | -53.1% | incidental (within band) |
| attackring-fight-60/legion/crawl_samples | 546 | 3744 | +585.7% | incidental (within band) |
| attackring-fight-60/legion/detour_permille | 15 | 114 | +660.0% | incidental (within band) |
| attackring-fight-60/legion/engaged.member_ticks | 21933 | 43940 | +100.3% | intended |
| attackring-fight-60/legion/flip_rate_per30_permille | 37 | 180 | +386.5% | incidental (within band) |
| attackring-fight-60/legion/flips | 224 | 1074 | +379.5% | incidental (within band) |
| attackring-fight-60/legion/follow_chain_mean_x100 | 326 | 349 | +7.1% | incidental (within band) |
| attackring-fight-60/legion/gauge.engaged_max | 11 | 35 | +218.2% | intended |
| attackring-fight-60/legion/gauge.still_per_residue_max | 1 | 2 | +100.0% | incidental (within band) |
| attackring-fight-60/legion/legion_groups_peak | 32 | 2 | -93.8% | incidental (within band) |
| attackring-fight-60/legion/open_still900 | 59 | 25 | -57.6% | incidental (within band) |
| attackring-fight-60/legion/parked_held | 89281 | 50749 | -43.2% | incidental (within band) |
| attackring-fight-60/legion/parked_no_progress | 20283 | 0 | -100.0% | incidental (within band) |
| attackring-fight-60/legion/reach.ring.damage_end | 737 | 1480 | +100.8% | intended |
| attackring-fight-60/legion/reach.ring.damage_t1200 | 87 | 98 | +12.6% | intended |
| attackring-fight-60/legion/reach.ring.damage_t2400 | 508 | 845 | +66.3% | intended |
| attackring-fight-60/legion/reach.ring.ever_end | 11 | 34 | +209.1% | intended |
| attackring-fight-60/legion/reach.ring.ever_t1200 | 8 | 10 | +25.0% | intended |
| attackring-fight-60/legion/reach.ring.ever_t2400 | 11 | 27 | +145.5% | intended |
| attackring-fight-60/legion/reach.ring.farthest_held | 11 | 6 | -45.5% | intended |
| attackring-fight-60/legion/reach.ring.hold_out_max | 2184 | 1839 | -15.8% | intended |
| attackring-fight-60/legion/reach.ring.now_end | 11 | 34 | +209.1% | intended |
| attackring-fight-60/legion/reach.ring.out600_end | 49 | 3 | -93.9% | intended |
| attackring-fight-60/legion/reach.ring.out600_peak | 49 | 10 | -79.6% | intended |
| attackring-fight-60/legion/reach.ring.t50 | -1 | 2594 | +259500.0% | intended |
| attackring-fight-60/legion/reversals | 0 | 34 | new | incidental (within band) |
| attackring-fight-60/legion/sideways | 249 | 2329 | +835.3% | incidental (within band) |
| attackring-fight-60/legion/spins | 25 | 122 | +388.0% | incidental (within band) |
| attackring-fight-60/legion/statue_ticks | 5271 | 33879 | +542.7% | incidental (within band) |
| attackring-fight-60/legion/still900_ever | 59 | 28 | -52.5% | incidental (within band) |
| attackring-fight-60/legion/stop_go | 139 | 799 | +474.8% | incidental (within band) |
| attackring-fight-60/legion/stopped_permille | 676 | 470 | -30.5% | incidental (within band) |
| attackring-fight-60/legion/waiting_held | 10443 | 28280 | +170.8% | incidental (within band) |
| attackring-fight-60/legion/waiting_no_progress | 1551 | 0 | -100.0% | incidental (within band) |
| attackring-fight-60/legion/wall_samples | 5941 | 10251 | +72.5% | incidental (within band) |
| attackring-fight-60/legion/work.aware_pairs.max | 1 | 2 | +100.0% | incidental (within band) |
| attackring-fight-60/legion/work.aware_pairs.total | 30 | 35 | +16.7% | incidental (within band) |
| attackring-fight-60/legion/work.crowd_window_ring_cells.max | 336 | 288 | -14.3% | incidental (within band) |
| attackring-fight-60/legion/work.crowd_window_ring_cells.p99 | 72 | 0 | -100.0% | incidental (within band) |
| attackring-fight-60/legion/work.crowd_window_ring_cells.total | 53400 | 1704 | -96.8% | incidental (within band) |
| attackring-fight-60/legion/work.detour_cells.max | 1154 | 1766 | +53.0% | incidental (within band) |
| attackring-fight-60/legion/work.detour_cells.p99 | 477 | 1156 | +142.3% | incidental (within band) |
| attackring-fight-60/legion/work.detour_cells.total | 21130 | 212659 | +906.4% | incidental (within band) |
| attackring-fight-60/legion/work.detours.max | 2 | 3 | +50.0% | incidental (within band) |
| attackring-fight-60/legion/work.detours.p99 | 1 | 2 | +100.0% | incidental (within band) |
| attackring-fight-60/legion/work.detours.total | 41 | 365 | +790.2% | incidental (within band) |
| attackring-fight-60/legion/work.field_work.total | 1482424 | 519024 | -65.0% | incidental (within band) |
| attackring-fight-60/legion/work.field_work_first_solo.total | 1004056 | 40656 | -96.0% | incidental (within band) |
| attackring-fight-60/legion/work.fields_built.total | 26 | 3 | -88.5% | incidental (within band) |
| attackring-fight-60/legion/work.fields_started_by_kind_4.total | 24 | 1 | -95.8% | incidental (within band) |
| attackring-fight-60/legion/work.group_loop_iters.p99 | 32 | 2 | -93.8% | incidental (within band) |
| attackring-fight-60/legion/work.group_loop_iters.total | 3111 | 425 | -86.3% | incidental (within band) |
| attackring-fight-60/legion/work.groups.p99 | 1 | 0 | -100.0% | incidental (within band) |
| attackring-fight-60/legion/work.groups.total | 34 | 3 | -91.2% | incidental (within band) |
| attackring-fight-60/legion/work.held_rechecks.max | 49 | 17 | -65.3% | incidental (within band) |
| attackring-fight-60/legion/work.held_rechecks.p99 | 49 | 14 | -71.4% | incidental (within band) |
| attackring-fight-60/legion/work.held_rechecks.total | 93802 | 15170 | -83.8% | incidental (within band) |
| attackring-fight-60/legion/work.holds.total | 189 | 805 | +325.9% | incidental (within band) |
| attackring-fight-60/legion/work.join_iterations.p99 | 12 | 2 | -83.3% | incidental (within band) |
| attackring-fight-60/legion/work.join_iterations.total | 997 | 295 | -70.4% | incidental (within band) |
| attackring-fight-60/legion/work.legion_total.p99 | 3068 | 2341 | -23.7% | incidental (within band) |
| attackring-fight-60/legion/work.legion_total.total | 2376956 | 1533564 | -35.5% | incidental (within band) |
| attackring-fight-60/legion/work.line_sweeps.total | 41253 | 50887 | +23.4% | incidental (within band) |
| attackring-fight-60/legion/work.move_calls_by_state_1.total | 58282 | 95378 | +63.6% | incidental (within band) |
| attackring-fight-60/legion/work.move_calls_by_state_2.max | 50 | 39 | -22.0% | incidental (within band) |
| attackring-fight-60/legion/work.move_calls_by_state_2.p99 | 49 | 36 | -26.5% | incidental (within band) |
| attackring-fight-60/legion/work.move_calls_by_state_2.total | 100406 | 40502 | -59.7% | incidental (within band) |
| attackring-fight-60/legion/work.moves.total | 113884 | 128933 | +13.2% | incidental (within band) |
| attackring-fight-60/legion/work.pass_scan_cells.total | 12 | 6 | -50.0% | incidental (within band) |
| attackring-fight-60/legion/work.pass_scans.total | 2 | 1 | -50.0% | incidental (within band) |
| attackring-fight-60/legion/work.sched_group_visits.p99 | 1 | 0 | -100.0% | incidental (within band) |
| attackring-fight-60/legion/work.sched_group_visits.total | 66 | 4 | -93.9% | incidental (within band) |
| attackring-fight-60/legion/work.share_scan_iters.max | 62 | 0 | -100.0% | incidental (within band) |
| attackring-fight-60/legion/work.share_scan_iters.p99 | 2 | 0 | -100.0% | incidental (within band) |
| attackring-fight-60/legion/work.share_scan_iters.total | 912 | 0 | -100.0% | incidental (within band) |
| attackring-fight-60/legion/work.slides.p99 | 1 | 2 | +100.0% | incidental (within band) |
| attackring-fight-60/legion/work.slides.total | 177 | 336 | +89.8% | incidental (within band) |
| attackring-fight-60/legion/work.still_units_processed.max | 1 | 2 | +100.0% | incidental (within band) |
| attackring-fight-60/legion/work.still_units_processed.p99 | 1 | 2 | +100.0% | incidental (within band) |
| attackring-fight-60/legion/work.still_units_processed.total | 99 | 1565 | +1480.8% | incidental (within band) |
| attackring-fight-60/legion/work.trace_cells.total | 713536 | 766299 | +7.4% | incidental (within band) |
| attackring-guard-64/legion/aim_reversals | 52 | 339 | +551.9% | incidental (within band) |
| attackring-guard-64/legion/back | 0 | 304 | new | incidental (within band) |
| attackring-guard-64/legion/backward | 28 | 81 | +189.3% | incidental (within band) |
| attackring-guard-64/legion/churn.bin1 | 132112 | 228930 | +73.3% | incidental (within band) |
| attackring-guard-64/legion/crawl_samples | 909 | 4484 | +393.3% | incidental (within band) |
| attackring-guard-64/legion/detour_permille | 39 | 121 | +210.3% | incidental (within band) |
| attackring-guard-64/legion/engaged.member_ticks | 14216 | 40363 | +183.9% | intended |
| attackring-guard-64/legion/engagement_on_flowing | 13 | 14 | +7.7% | incidental (within band) |
| attackring-guard-64/legion/flip_rate_per30_permille | 84 | 250 | +197.6% | incidental (within band) |
| attackring-guard-64/legion/flips | 534 | 1596 | +198.9% | incidental (within band) |
| attackring-guard-64/legion/follow_chain_max | 40 | 24 | -40.0% | incidental (within band) |
| attackring-guard-64/legion/follow_chain_mean_x100 | 1022 | 591 | -42.2% | incidental (within band) |
| attackring-guard-64/legion/gauge.engaged_max | 7 | 32 | +357.1% | intended |
| attackring-guard-64/legion/legion_groups_peak | 2 | 1 | -50.0% | incidental (within band) |
| attackring-guard-64/legion/open_still900 | 64 | 22 | -65.6% | incidental (within band) |
| attackring-guard-64/legion/parked_held | 92500 | 40422 | -56.3% | incidental (within band) |
| attackring-guard-64/legion/parked_no_progress | 12806 | 0 | -100.0% | incidental (within band) |
| attackring-guard-64/legion/reach.ring.ever_end | 15 | 61 | +306.7% | intended |
| attackring-guard-64/legion/reach.ring.ever_t2400 | 15 | 49 | +226.7% | intended |
| attackring-guard-64/legion/reach.ring.farthest_held | 33 | -1 | -103.0% | intended |
| attackring-guard-64/legion/reach.ring.hold_out_max | 1957 | 436 | -77.7% | intended |
| attackring-guard-64/legion/reach.ring.now_end | 15 | 57 | +280.0% | intended |
| attackring-guard-64/legion/reach.ring.out600_end | 49 | 0 | -100.0% | intended |
| attackring-guard-64/legion/reach.ring.out600_peak | 49 | 1 | -98.0% | intended |
| attackring-guard-64/legion/reach.ring.t50 | -1 | 1837 | +183800.0% | intended |
| attackring-guard-64/legion/reversals | 0 | 39 | new | incidental (within band) |
| attackring-guard-64/legion/sideways | 573 | 2784 | +385.9% | incidental (within band) |
| attackring-guard-64/legion/statue_ticks | 9513 | 42133 | +342.9% | incidental (within band) |
| attackring-guard-64/legion/still900_ever | 64 | 22 | -65.6% | incidental (within band) |
| attackring-guard-64/legion/stop_go | 1144 | 1836 | +60.5% | incidental (within band) |
| attackring-guard-64/legion/stopped_permille | 658 | 432 | -34.3% | incidental (within band) |
| attackring-guard-64/legion/waiting_held | 13583 | 29921 | +120.3% | incidental (within band) |
| attackring-guard-64/legion/waiting_no_progress | 1276 | 0 | -100.0% | incidental (within band) |
| attackring-guard-64/legion/wall_samples | 7147 | 12146 | +69.9% | incidental (within band) |
| attackring-guard-64/legion/work.field_work.total | 641968 | 717552 | +11.8% | incidental (within band) |
| attackring-guard-64/legion/work.field_work_first_solo.total | 641968 | 239184 | -62.7% | incidental (within band) |
| attackring-guard-64/legion/work.group_loop_iters.max | 126 | 63 | -50.0% | incidental (within band) |
| attackring-guard-64/legion/work.group_loop_iters.total | 420 | 174 | -58.6% | incidental (within band) |
| attackring-guard-64/legion/work.groups.total | 3 | 1 | -66.7% | incidental (within band) |
| attackring-guard-64/legion/work.held_rechecks.max | 57 | 10 | -82.5% | incidental (within band) |
| attackring-guard-64/legion/work.held_rechecks.p99 | 57 | 9 | -84.2% | incidental (within band) |
| attackring-guard-64/legion/work.held_rechecks.total | 98196 | 9151 | -90.7% | incidental (within band) |
| attackring-guard-64/legion/work.holds.max | 11 | 6 | -45.5% | incidental (within band) |
| attackring-guard-64/legion/work.holds.p99 | 5 | 4 | -20.0% | incidental (within band) |
| attackring-guard-64/legion/work.holds.total | 1185 | 1826 | +54.1% | incidental (within band) |
| attackring-guard-64/legion/work.join_iterations.max | 126 | 63 | -50.0% | incidental (within band) |
| attackring-guard-64/legion/work.join_iterations.total | 315 | 63 | -80.0% | incidental (within band) |
| attackring-guard-64/legion/work.legion_total.p99 | 2814 | 3009 | +6.9% | incidental (within band) |
| attackring-guard-64/legion/work.legion_total.total | 1970989 | 2132167 | +8.2% | incidental (within band) |
| attackring-guard-64/legion/work.move_calls_by_state_1.total | 65430 | 108901 | +66.4% | incidental (within band) |
| attackring-guard-64/legion/work.move_calls_by_state_2.max | 57 | 35 | -38.6% | incidental (within band) |
| attackring-guard-64/legion/work.move_calls_by_state_2.p99 | 57 | 31 | -45.6% | incidental (within band) |
| attackring-guard-64/legion/work.move_calls_by_state_2.total | 112088 | 42584 | -62.0% | incidental (within band) |
| attackring-guard-64/legion/work.move_calls_by_state_3.total | 202 | 64 | -68.3% | incidental (within band) |
| attackring-guard-64/legion/work.moves.total | 129836 | 147436 | +13.6% | incidental (within band) |
| attackring-guard-64/legion/work.pass_scans_skipped.total | 80180 | 52390 | -34.7% | incidental (within band) |
| attackring-guard-64/legion/work.registrations.total | 192 | 64 | -66.7% | incidental (within band) |
| attackring-guard-64/legion/work.sched_group_visits.max | 1 | 2 | +100.0% | incidental (within band) |
| attackring-guard-64/legion/work.sched_group_visits.total | 4 | 11 | +175.0% | incidental (within band) |
| attackring-guard-64/legion/work.slides.max | 14 | 10 | -28.6% | incidental (within band) |
| attackring-guard-64/legion/work.slides.p99 | 5 | 4 | -20.0% | incidental (within band) |
| attackring-guard-64/legion/work.still_units_processed.p99 | 1 | 2 | +100.0% | incidental (within band) |
| attackring-guard-64/legion/work.still_units_processed.total | 103 | 1446 | +1303.9% | incidental (within band) |
| attackring-mob-100/legion/aim_reversals | 1 | 419 | +41800.0% | incidental (within band) |
| attackring-mob-100/legion/back | 0 | 421 | new | incidental (within band) |
| attackring-mob-100/legion/backward | 0 | 33 | new | incidental (within band) |
| attackring-mob-100/legion/churn.bin0 | 1729170 | 1458579 | -15.6% | incidental (within band) |
| attackring-mob-100/legion/contact_own_permille | 975 | 903 | -7.4% | incidental (within band) |
| attackring-mob-100/legion/crawl_samples | 215 | 5291 | +2360.9% | incidental (within band) |
| attackring-mob-100/legion/detour_permille | 18 | 96 | +433.3% | incidental (within band) |
| attackring-mob-100/legion/engaged.member_ticks | 6622 | 55151 | +732.8% | intended |
| attackring-mob-100/legion/engagement_on_flowing | 30 | 76 | +153.3% | incidental (within band) |
| attackring-mob-100/legion/flip_rate_per30_permille | 81 | 141 | +74.1% | incidental (within band) |
| attackring-mob-100/legion/flips | 804 | 1403 | +74.5% | incidental (within band) |
| attackring-mob-100/legion/follow_chain_max | 46 | 7 | -84.8% | incidental (within band) |
| attackring-mob-100/legion/follow_chain_mean_x100 | 1332 | 290 | -78.2% | incidental (within band) |
| attackring-mob-100/legion/gauge.engaged_max | 3 | 33 | +1000.0% | intended |
| attackring-mob-100/legion/open_still900 | 100 | 50 | -50.0% | incidental (within band) |
| attackring-mob-100/legion/parked_held | 193509 | 102685 | -46.9% | incidental (within band) |
| attackring-mob-100/legion/parked_no_progress | 6172 | 0 | -100.0% | incidental (within band) |
| attackring-mob-100/legion/reach.ring.damage_end | 223 | 1852 | +730.5% | intended |
| attackring-mob-100/legion/reach.ring.damage_t1200 | 43 | 203 | +372.1% | intended |
| attackring-mob-100/legion/reach.ring.damage_t2400 | 163 | 1217 | +646.6% | intended |
| attackring-mob-100/legion/reach.ring.ever_end | 3 | 33 | +1000.0% | intended |
| attackring-mob-100/legion/reach.ring.ever_t1200 | 3 | 18 | +500.0% | intended |
| attackring-mob-100/legion/reach.ring.ever_t2400 | 3 | 30 | +900.0% | intended |
| attackring-mob-100/legion/reach.ring.farthest_held | 80 | 7 | -91.2% | intended |
| attackring-mob-100/legion/reach.ring.hold_out_max | 2218 | 2059 | -7.2% | intended |
| attackring-mob-100/legion/reach.ring.now_end | 3 | 33 | +1000.0% | intended |
| attackring-mob-100/legion/reach.ring.out600_end | 97 | 26 | -73.2% | intended |
| attackring-mob-100/legion/reach.ring.out600_peak | 97 | 29 | -70.1% | intended |
| attackring-mob-100/legion/reversals | 0 | 51 | new | incidental (within band) |
| attackring-mob-100/legion/sideways | 289 | 2142 | +641.2% | incidental (within band) |
| attackring-mob-100/legion/spins | 3 | 122 | +3966.7% | incidental (within band) |
| attackring-mob-100/legion/statue_ticks | 146 | 50082 | +34202.7% | incidental (within band) |
| attackring-mob-100/legion/still900_ever | 100 | 56 | -44.0% | incidental (within band) |
| attackring-mob-100/legion/stop_go | 1542 | 1182 | -23.3% | incidental (within band) |
| attackring-mob-100/legion/stopped_permille | 780 | 506 | -35.1% | incidental (within band) |
| attackring-mob-100/legion/waiting_held | 24185 | 38689 | +60.0% | incidental (within band) |
| attackring-mob-100/legion/waiting_no_progress | 423 | 0 | -100.0% | incidental (within band) |
| attackring-mob-100/legion/wall_samples | 7521 | 15781 | +109.8% | incidental (within band) |
| attackring-mob-100/legion/work.held_rechecks.max | 97 | 42 | -56.7% | incidental (within band) |
| attackring-mob-100/legion/work.held_rechecks.p99 | 97 | 41 | -57.7% | incidental (within band) |
| attackring-mob-100/legion/work.held_rechecks.total | 204393 | 63704 | -68.8% | incidental (within band) |
| attackring-mob-100/legion/work.holds.max | 12 | 6 | -50.0% | incidental (within band) |
| attackring-mob-100/legion/work.holds.p99 | 7 | 3 | -57.1% | incidental (within band) |
| attackring-mob-100/legion/work.holds.total | 1639 | 1228 | -25.1% | incidental (within band) |
| attackring-mob-100/legion/work.legion_total.p99 | 4390 | 2806 | -36.1% | incidental (within band) |
| attackring-mob-100/legion/work.legion_total.total | 1951594 | 1675569 | -14.1% | incidental (within band) |
| attackring-mob-100/legion/work.line_sweeps.max | 348 | 100 | -71.3% | incidental (within band) |
| attackring-mob-100/legion/work.line_sweeps.p99 | 312 | 45 | -85.6% | incidental (within band) |
| attackring-mob-100/legion/work.line_sweeps.total | 121381 | 31095 | -74.4% | incidental (within band) |
| attackring-mob-100/legion/work.move_calls_by_state_1.total | 66090 | 148341 | +124.5% | incidental (within band) |
| attackring-mob-100/legion/work.move_calls_by_state_2.max | 97 | 54 | -44.3% | incidental (within band) |
| attackring-mob-100/legion/work.move_calls_by_state_2.p99 | 97 | 53 | -45.4% | incidental (within band) |
| attackring-mob-100/legion/work.move_calls_by_state_2.total | 227052 | 96272 | -57.6% | incidental (within band) |
| attackring-mob-100/legion/work.moves.total | 194197 | 216109 | +11.3% | incidental (within band) |
| attackring-mob-100/legion/work.pass_scan_cells.max | 402 | 12 | -97.0% | incidental (within band) |
| attackring-mob-100/legion/work.pass_scan_cells.p99 | 324 | 12 | -96.3% | incidental (within band) |
| attackring-mob-100/legion/work.pass_scan_cells.total | 22182 | 816 | -96.3% | incidental (within band) |
| attackring-mob-100/legion/work.pass_scans.max | 67 | 2 | -97.0% | incidental (within band) |
| attackring-mob-100/legion/work.pass_scans.p99 | 54 | 2 | -96.3% | incidental (within band) |
| attackring-mob-100/legion/work.pass_scans.total | 3697 | 136 | -96.3% | incidental (within band) |
| attackring-mob-100/legion/work.pass_scans_skipped.total | 146484 | 82157 | -43.9% | incidental (within band) |
| attackring-mob-100/legion/work.slides.max | 12 | 5 | -58.3% | incidental (within band) |
| attackring-mob-100/legion/work.slides.p99 | 7 | 3 | -57.1% | incidental (within band) |
| attackring-mob-100/legion/work.slides.total | 2645 | 818 | -69.1% | incidental (within band) |
| attackring-mob-100/legion/work.still_units_processed.p99 | 1 | 4 | +300.0% | incidental (within band) |
| attackring-mob-100/legion/work.still_units_processed.total | 105 | 2943 | +2702.9% | incidental (within band) |
| attackring-mob-100/legion/work.trace_cells.max | 6820 | 10557 | +54.8% | incidental (within band) |
| attackring-mob-100/legion/work.trace_cells.p99 | 4062 | 2806 | -30.9% | incidental (within band) |
| attackring-mob-100/legion/work.trace_cells.total | 1374242 | 1119616 | -18.5% | incidental (within band) |
| attackring-mob-40/legion/aim_reversals | 0 | 154 | new | incidental (within band) |
| attackring-mob-40/legion/back | 0 | 194 | new | incidental (within band) |
| attackring-mob-40/legion/backward | 0 | 24 | new | incidental (within band) |
| attackring-mob-40/legion/churn.bin0 | 973907 | 705228 | -27.6% | incidental (within band) |
| attackring-mob-40/legion/churn.bin1 | 87196 | 28072 | -67.8% | incidental (within band) |
| attackring-mob-40/legion/contact_own_permille | 973 | 864 | -11.2% | incidental (within band) |
| attackring-mob-40/legion/crawl_samples | 43 | 1671 | +3786.0% | incidental (within band) |
| attackring-mob-40/legion/detour_permille | 5 | 67 | +1240.0% | incidental (within band) |
| attackring-mob-40/legion/engaged.member_ticks | 4243 | 58405 | +1276.5% | intended |
| attackring-mob-40/legion/engagement_on_flowing | 16 | 33 | +106.2% | incidental (within band) |
| attackring-mob-40/legion/flip_rate_per30_permille | 49 | 76 | +55.1% | incidental (within band) |
| attackring-mob-40/legion/flips | 194 | 302 | +55.7% | incidental (within band) |
| attackring-mob-40/legion/follow_chain_max | 29 | 5 | -82.8% | incidental (within band) |
| attackring-mob-40/legion/follow_chain_mean_x100 | 1421 | 226 | -84.1% | incidental (within band) |
| attackring-mob-40/legion/gauge.engaged_max | 2 | 36 | +1700.0% | intended |
| attackring-mob-40/legion/open_still900 | 40 | 31 | -22.5% | incidental (within band) |
| attackring-mob-40/legion/parked_held | 75049 | 53010 | -29.4% | incidental (within band) |
| attackring-mob-40/legion/parked_no_progress | 3943 | 0 | -100.0% | incidental (within band) |
| attackring-mob-40/legion/reach.ring.damage_end | 142 | 1961 | +1281.0% | intended |
| attackring-mob-40/legion/reach.ring.damage_t1200 | 22 | 168 | +663.6% | intended |
| attackring-mob-40/legion/reach.ring.damage_t2400 | 102 | 1260 | +1135.3% | intended |
| attackring-mob-40/legion/reach.ring.ever_end | 2 | 34 | +1600.0% | intended |
| attackring-mob-40/legion/reach.ring.ever_t1200 | 2 | 17 | +750.0% | intended |
| attackring-mob-40/legion/reach.ring.ever_t2400 | 2 | 30 | +1400.0% | intended |
| attackring-mob-40/legion/reach.ring.farthest_held | 42 | 6 | -85.7% | intended |
| attackring-mob-40/legion/reach.ring.hold_out_max | 2190 | 152 | -93.1% | intended |
| attackring-mob-40/legion/reach.ring.now_end | 2 | 34 | +1600.0% | intended |
| attackring-mob-40/legion/reach.ring.out600_end | 38 | 2 | -94.7% | intended |
| attackring-mob-40/legion/reach.ring.out600_peak | 38 | 2 | -94.7% | intended |
| attackring-mob-40/legion/reach.ring.t50 | -1 | 1321 | +132200.0% | intended |
| attackring-mob-40/legion/reversals | 0 | 44 | new | incidental (within band) |
| attackring-mob-40/legion/sideways | 62 | 1012 | +1532.3% | incidental (within band) |
| attackring-mob-40/legion/spins | 2 | 183 | +9050.0% | incidental (within band) |
| attackring-mob-40/legion/statue_ticks | 7 | 16048 | +229157.1% | incidental (within band) |
| attackring-mob-40/legion/still900_ever | 40 | 31 | -22.5% | incidental (within band) |
| attackring-mob-40/legion/stop_go | 562 | 247 | -56.0% | incidental (within band) |
| attackring-mob-40/legion/stopped_permille | 742 | 540 | -27.2% | incidental (within band) |
| attackring-mob-40/legion/waiting_held | 6783 | 9241 | +36.2% | incidental (within band) |
| attackring-mob-40/legion/waiting_no_progress | 282 | 0 | -100.0% | incidental (within band) |
| attackring-mob-40/legion/wall_samples | 3382 | 5741 | +69.8% | incidental (within band) |
| attackring-mob-40/legion/work.held_rechecks.max | 38 | 4 | -89.5% | incidental (within band) |
| attackring-mob-40/legion/work.held_rechecks.p99 | 38 | 3 | -92.1% | incidental (within band) |
| attackring-mob-40/legion/work.held_rechecks.total | 78482 | 570 | -99.3% | incidental (within band) |
| attackring-mob-40/legion/work.holds.max | 5 | 3 | -40.0% | incidental (within band) |
| attackring-mob-40/legion/work.holds.p99 | 3 | 1 | -66.7% | incidental (within band) |
| attackring-mob-40/legion/work.holds.total | 600 | 247 | -58.8% | incidental (within band) |
| attackring-mob-40/legion/work.legion_total.p99 | 1678 | 1392 | -17.0% | incidental (within band) |
| attackring-mob-40/legion/work.legion_total.total | 1061103 | 733300 | -30.9% | incidental (within band) |
| attackring-mob-40/legion/work.line_sweeps.max | 144 | 40 | -72.2% | incidental (within band) |
| attackring-mob-40/legion/work.line_sweeps.p99 | 127 | 22 | -82.7% | incidental (within band) |
| attackring-mob-40/legion/work.line_sweeps.total | 79578 | 12671 | -84.1% | incidental (within band) |
| attackring-mob-40/legion/work.move_calls_by_state_1.total | 31030 | 55266 | +78.1% | incidental (within band) |
| attackring-mob-40/legion/work.move_calls_by_state_2.max | 38 | 17 | -55.3% | incidental (within band) |
| attackring-mob-40/legion/work.move_calls_by_state_2.p99 | 38 | 14 | -63.2% | incidental (within band) |
| attackring-mob-40/legion/work.move_calls_by_state_2.total | 84647 | 6249 | -92.6% | incidental (within band) |
| attackring-mob-40/legion/work.moves.total | 77421 | 61311 | -20.8% | incidental (within band) |
| attackring-mob-40/legion/work.pass_scan_cells.max | 144 | 6 | -95.8% | incidental (within band) |
| attackring-mob-40/legion/work.pass_scan_cells.p99 | 36 | 0 | -100.0% | incidental (within band) |
| attackring-mob-40/legion/work.pass_scan_cells.total | 3954 | 60 | -98.5% | incidental (within band) |
| attackring-mob-40/legion/work.pass_scans.max | 24 | 1 | -95.8% | incidental (within band) |
| attackring-mob-40/legion/work.pass_scans.p99 | 6 | 0 | -100.0% | incidental (within band) |
| attackring-mob-40/legion/work.pass_scans.total | 659 | 10 | -98.5% | incidental (within band) |
| attackring-mob-40/legion/work.pass_scans_skipped.total | 64877 | 31938 | -50.8% | incidental (within band) |
| attackring-mob-40/legion/work.slides.max | 7 | 2 | -71.4% | incidental (within band) |
| attackring-mob-40/legion/work.slides.p99 | 3 | 1 | -66.7% | incidental (within band) |
| attackring-mob-40/legion/work.slides.total | 460 | 243 | -47.2% | incidental (within band) |
| attackring-mob-40/legion/work.still_units_processed.p99 | 1 | 2 | +100.0% | incidental (within band) |
| attackring-mob-40/legion/work.still_units_processed.total | 102 | 2044 | +1903.9% | incidental (within band) |
| attackring-mob-40/legion/work.trace_cells.p99 | 1648 | 1392 | -15.5% | incidental (within band) |
| attackring-mob-40/legion/work.trace_cells.total | 697289 | 428336 | -38.6% | incidental (within band) |
| attackring-r200-64/legion/aim_reversals | 0 | 134 | new | incidental (within band) |
| attackring-r200-64/legion/back | 0 | 145 | new | incidental (within band) |
| attackring-r200-64/legion/backward | 0 | 12 | new | incidental (within band) |
| attackring-r200-64/legion/churn.bin0 | 1269095 | 1011672 | -20.3% | incidental (within band) |
| attackring-r200-64/legion/churn.bin1 | 139912 | 55516 | -60.3% | incidental (within band) |
| attackring-r200-64/legion/contact_own_permille | 973 | 866 | -11.0% | incidental (within band) |
| attackring-r200-64/legion/crawl_samples | 158 | 2027 | +1182.9% | incidental (within band) |
| attackring-r200-64/legion/detour_permille | 14 | 51 | +264.3% | incidental (within band) |
| attackring-r200-64/legion/engaged.member_ticks | 6641 | 102719 | +1446.7% | intended |
| attackring-r200-64/legion/engagement_on_flowing | 15 | 50 | +233.3% | incidental (within band) |
| attackring-r200-64/legion/flip_rate_per30_permille | 71 | 54 | -23.9% | incidental (within band) |
| attackring-r200-64/legion/flips | 450 | 342 | -24.0% | incidental (within band) |
| attackring-r200-64/legion/follow_chain_max | 40 | 6 | -85.0% | incidental (within band) |
| attackring-r200-64/legion/follow_chain_mean_x100 | 1108 | 186 | -83.2% | incidental (within band) |
| attackring-r200-64/legion/gauge.engaged_max | 3 | 58 | +1833.3% | intended |
| attackring-r200-64/legion/open_still900 | 64 | 56 | -12.5% | incidental (within band) |
| attackring-r200-64/legion/parked_held | 123551 | 97278 | -21.3% | incidental (within band) |
| attackring-r200-64/legion/parked_no_progress | 6191 | 0 | -100.0% | incidental (within band) |
| attackring-r200-64/legion/reach.ring.damage_end | 223 | 3448 | +1446.2% | intended |
| attackring-r200-64/legion/reach.ring.damage_t1200 | 43 | 366 | +751.2% | intended |
| attackring-r200-64/legion/reach.ring.damage_t2400 | 163 | 2319 | +1322.7% | intended |
| attackring-r200-64/legion/reach.ring.ever_end | 3 | 57 | +1800.0% | intended |
| attackring-r200-64/legion/reach.ring.ever_t1200 | 3 | 35 | +1066.7% | intended |
| attackring-r200-64/legion/reach.ring.ever_t2400 | 3 | 54 | +1700.0% | intended |
| attackring-r200-64/legion/reach.ring.farthest_held | 65 | 13 | -80.0% | intended |
| attackring-r200-64/legion/reach.ring.first | 684 | 726 | +6.1% | intended |
| attackring-r200-64/legion/reach.ring.hold_out_max | 2254 | 1235 | -45.2% | intended |
| attackring-r200-64/legion/reach.ring.now_end | 3 | 57 | +1800.0% | intended |
| attackring-r200-64/legion/reach.ring.out600_end | 61 | 4 | -93.4% | intended |
| attackring-r200-64/legion/reach.ring.out600_peak | 61 | 4 | -93.4% | intended |
| attackring-r200-64/legion/reach.ring.t50 | -1 | 1079 | +108000.0% | intended |
| attackring-r200-64/legion/reversals | 0 | 45 | new | incidental (within band) |
| attackring-r200-64/legion/sideways | 156 | 990 | +534.6% | incidental (within band) |
| attackring-r200-64/legion/spins | 3 | 251 | +8266.7% | incidental (within band) |
| attackring-r200-64/legion/statue_ticks | 290 | 19585 | +6653.4% | incidental (within band) |
| attackring-r200-64/legion/still900_ever | 64 | 56 | -12.5% | incidental (within band) |
| attackring-r200-64/legion/stop_go | 922 | 331 | -64.1% | incidental (within band) |
| attackring-r200-64/legion/stopped_permille | 775 | 591 | -23.7% | incidental (within band) |
| attackring-r200-64/legion/waiting_no_progress | 423 | 0 | -100.0% | incidental (within band) |
| attackring-r200-64/legion/wall_samples | 4808 | 8143 | +69.4% | incidental (within band) |
| attackring-r200-64/legion/work.held_rechecks.max | 61 | 4 | -93.4% | incidental (within band) |
| attackring-r200-64/legion/work.held_rechecks.p99 | 61 | 4 | -93.4% | incidental (within band) |
| attackring-r200-64/legion/work.held_rechecks.total | 129558 | 3815 | -97.1% | incidental (within band) |
| attackring-r200-64/legion/work.holds.max | 9 | 3 | -66.7% | incidental (within band) |
| attackring-r200-64/legion/work.holds.p99 | 5 | 2 | -60.0% | incidental (within band) |
| attackring-r200-64/legion/work.holds.total | 983 | 336 | -65.8% | incidental (within band) |
| attackring-r200-64/legion/work.legion_total.p99 | 2707 | 2454 | -9.3% | incidental (within band) |
| attackring-r200-64/legion/work.legion_total.total | 1409007 | 1067188 | -24.3% | incidental (within band) |
| attackring-r200-64/legion/work.line_sweeps.max | 228 | 64 | -71.9% | incidental (within band) |
| attackring-r200-64/legion/work.line_sweeps.p99 | 196 | 28 | -85.7% | incidental (within band) |
| attackring-r200-64/legion/work.line_sweeps.total | 91522 | 12851 | -86.0% | incidental (within band) |
| attackring-r200-64/legion/work.move_calls_by_state_1.total | 43241 | 78388 | +81.3% | incidental (within band) |
| attackring-r200-64/legion/work.move_calls_by_state_2.max | 61 | 15 | -75.4% | incidental (within band) |
| attackring-r200-64/legion/work.move_calls_by_state_2.p99 | 61 | 11 | -82.0% | incidental (within band) |
| attackring-r200-64/legion/work.move_calls_by_state_2.total | 141990 | 10765 | -92.4% | incidental (within band) |
| attackring-r200-64/legion/work.moves.total | 122168 | 87555 | -28.3% | incidental (within band) |
| attackring-r200-64/legion/work.pass_scan_cells.max | 276 | 42 | -84.8% | incidental (within band) |
| attackring-r200-64/legion/work.pass_scan_cells.p99 | 174 | 36 | -79.3% | incidental (within band) |
| attackring-r200-64/legion/work.pass_scan_cells.total | 12228 | 7788 | -36.3% | incidental (within band) |
| attackring-r200-64/legion/work.pass_scans.max | 46 | 7 | -84.8% | incidental (within band) |
| attackring-r200-64/legion/work.pass_scans.p99 | 29 | 6 | -79.3% | incidental (within band) |
| attackring-r200-64/legion/work.pass_scans.total | 2038 | 1298 | -36.3% | incidental (within band) |
| attackring-r200-64/legion/work.pass_scans_skipped.total | 108309 | 51224 | -52.7% | incidental (within band) |
| attackring-r200-64/legion/work.slides.max | 8 | 5 | -37.5% | incidental (within band) |
| attackring-r200-64/legion/work.slides.p99 | 5 | 2 | -60.0% | incidental (within band) |
| attackring-r200-64/legion/work.slides.total | 1293 | 370 | -71.4% | incidental (within band) |
| attackring-r200-64/legion/work.still_units_processed.p99 | 1 | 3 | +200.0% | incidental (within band) |
| attackring-r200-64/legion/work.still_units_processed.total | 103 | 3527 | +3324.3% | incidental (within band) |
| attackring-r200-64/legion/work.trace_cells.total | 957426 | 724525 | -24.3% | incidental (within band) |
| attackring-river-64/legion/aim_reversals | 0 | 143 | new | incidental (within band) |
| attackring-river-64/legion/back | 0 | 75 | new | incidental (within band) |
| attackring-river-64/legion/backward | 0 | 11 | new | incidental (within band) |
| attackring-river-64/legion/churn.bin0 | 638757 | 755831 | +18.3% | incidental (within band) |
| attackring-river-64/legion/churn.bin1 | 307756 | 357687 | +16.2% | incidental (within band) |
| attackring-river-64/legion/crawl_samples | 135 | 1776 | +1215.6% | incidental (within band) |
| attackring-river-64/legion/detour_permille | 5 | 59 | +1080.0% | incidental (within band) |
| attackring-river-64/legion/flip_rate_per30_permille | 32 | 116 | +262.5% | incidental (within band) |
| attackring-river-64/legion/flips | 199 | 733 | +268.3% | incidental (within band) |
| attackring-river-64/legion/follow_chain_max | 14 | 13 | -7.1% | incidental (within band) |
| attackring-river-64/legion/follow_chain_mean_x100 | 581 | 305 | -47.5% | incidental (within band) |
| attackring-river-64/legion/open_still900 | 63 | 58 | -7.9% | incidental (within band) |
| attackring-river-64/legion/parked_held | 139219 | 103325 | -25.8% | incidental (within band) |
| attackring-river-64/legion/reach.ring.ever_end | 6 | 12 | +100.0% | intended |
| attackring-river-64/legion/reach.ring.ever_t1200 | 6 | 11 | +83.3% | intended |
| attackring-river-64/legion/reach.ring.ever_t2400 | 6 | 12 | +100.0% | intended |
| attackring-river-64/legion/reach.ring.farthest_held | 41 | 18 | -56.1% | intended |
| attackring-river-64/legion/reach.ring.now_end | 6 | 12 | +100.0% | intended |
| attackring-river-64/legion/reach.ring.out600_end | 58 | 49 | -15.5% | intended |
| attackring-river-64/legion/reach.ring.out600_peak | 58 | 49 | -15.5% | intended |
| attackring-river-64/legion/reversals | 0 | 11 | new | incidental (within band) |
| attackring-river-64/legion/sideways | 94 | 696 | +640.4% | incidental (within band) |
| attackring-river-64/legion/statue_ticks | 1025 | 15658 | +1427.6% | incidental (within band) |
| attackring-river-64/legion/still900_ever | 63 | 58 | -7.9% | incidental (within band) |
| attackring-river-64/legion/stop_go | 301 | 699 | +132.2% | incidental (within band) |
| attackring-river-64/legion/stopped_permille | 802 | 690 | -14.0% | incidental (within band) |
| attackring-river-64/legion/waiting_held | 10167 | 20653 | +103.1% | incidental (within band) |
| attackring-river-64/legion/wall_samples | 3975 | 6457 | +62.4% | incidental (within band) |
| attackring-river-64/legion/work.detour_cells.max | 349 | 2328 | +567.0% | incidental (within band) |
| attackring-river-64/legion/work.detour_cells.p99 | 0 | 869 | new | incidental (within band) |
| attackring-river-64/legion/work.detour_cells.total | 2488 | 183808 | +7287.8% | incidental (within band) |
| attackring-river-64/legion/work.held_rechecks.total | 144953 | 110885 | -23.5% | incidental (within band) |
| attackring-river-64/legion/work.holds.total | 364 | 762 | +109.3% | incidental (within band) |
| attackring-river-64/legion/work.legion_total.p99 | 1638 | 1782 | +8.8% | incidental (within band) |
| attackring-river-64/legion/work.legion_total.total | 946042 | 1118314 | +18.2% | incidental (within band) |
| attackring-river-64/legion/work.line_sweeps.total | 9861 | 15367 | +55.8% | incidental (within band) |
| attackring-river-64/legion/work.move_calls_by_state_1.total | 37707 | 58892 | +56.2% | incidental (within band) |
| attackring-river-64/legion/work.move_calls_by_state_2.total | 151696 | 130519 | -14.0% | incidental (within band) |
| attackring-river-64/legion/work.moves.total | 118910 | 135890 | +14.3% | incidental (within band) |
| attackring-river-64/legion/work.pass_scan_cells.total | 42 | 36 | -14.3% | incidental (within band) |
| attackring-river-64/legion/work.pass_scans.total | 7 | 6 | -14.3% | incidental (within band) |
| attackring-river-64/legion/work.pass_scans_skipped.total | 65610 | 37356 | -43.1% | incidental (within band) |
| attackring-river-64/legion/work.slides.max | 5 | 4 | -20.0% | incidental (within band) |
| attackring-river-64/legion/work.trace_cells.total | 337814 | 367969 | +8.9% | incidental (within band) |
| attackring-squad-64/legion/aim_reversals | 1 | 297 | +29600.0% | incidental (within band) |
| attackring-squad-64/legion/back | 0 | 269 | new | incidental (within band) |
| attackring-squad-64/legion/backward | 0 | 30 | new | incidental (within band) |
| attackring-squad-64/legion/churn.bin0 | 1335655 | 1031691 | -22.8% | incidental (within band) |
| attackring-squad-64/legion/churn.bin1 | 139912 | 99586 | -28.8% | incidental (within band) |
| attackring-squad-64/legion/contact_own_permille | 972 | 914 | -6.0% | incidental (within band) |
| attackring-squad-64/legion/crawl_samples | 154 | 3174 | +1961.0% | incidental (within band) |
| attackring-squad-64/legion/detour_permille | 12 | 88 | +633.3% | incidental (within band) |
| attackring-squad-64/legion/engaged.member_ticks | 6462 | 69227 | +971.3% | intended |
| attackring-squad-64/legion/engagement_on_flowing | 15 | 49 | +226.7% | incidental (within band) |
| attackring-squad-64/legion/flip_rate_per30_permille | 71 | 119 | +67.6% | incidental (within band) |
| attackring-squad-64/legion/flips | 450 | 756 | +68.0% | incidental (within band) |
| attackring-squad-64/legion/follow_chain_max | 40 | 5 | -87.5% | incidental (within band) |
| attackring-squad-64/legion/follow_chain_mean_x100 | 1169 | 252 | -78.4% | incidental (within band) |
| attackring-squad-64/legion/gauge.engaged_max | 3 | 42 | +1300.0% | intended |
| attackring-squad-64/legion/open_still900 | 64 | 40 | -37.5% | incidental (within band) |
| attackring-squad-64/legion/parked_held | 119398 | 70731 | -40.8% | incidental (within band) |
| attackring-squad-64/legion/parked_no_progress | 6012 | 0 | -100.0% | incidental (within band) |
| attackring-squad-64/legion/reach.ring.damage_end | 217 | 2326 | +971.9% | intended |
| attackring-squad-64/legion/reach.ring.damage_t1200 | 37 | 193 | +421.6% | intended |
| attackring-squad-64/legion/reach.ring.damage_t2400 | 157 | 1492 | +850.3% | intended |
| attackring-squad-64/legion/reach.ring.ever_end | 3 | 39 | +1200.0% | intended |
| attackring-squad-64/legion/reach.ring.ever_t1200 | 3 | 21 | +600.0% | intended |
| attackring-squad-64/legion/reach.ring.ever_t2400 | 3 | 37 | +1133.3% | intended |
| attackring-squad-64/legion/reach.ring.farthest_held | 58 | 6 | -89.7% | intended |
| attackring-squad-64/legion/reach.ring.hold_out_max | 2192 | 1044 | -52.4% | intended |
| attackring-squad-64/legion/reach.ring.now_end | 3 | 39 | +1200.0% | intended |
| attackring-squad-64/legion/reach.ring.out600_end | 61 | 8 | -86.9% | intended |
| attackring-squad-64/legion/reach.ring.out600_peak | 61 | 8 | -86.9% | intended |
| attackring-squad-64/legion/reach.ring.t50 | -1 | 1671 | +167200.0% | intended |
| attackring-squad-64/legion/reversals | 0 | 52 | new | incidental (within band) |
| attackring-squad-64/legion/sideways | 156 | 1600 | +925.6% | incidental (within band) |
| attackring-squad-64/legion/spins | 3 | 191 | +6266.7% | incidental (within band) |
| attackring-squad-64/legion/statue_ticks | 290 | 29502 | +10073.1% | incidental (within band) |
| attackring-squad-64/legion/still900_ever | 64 | 40 | -37.5% | incidental (within band) |
| attackring-squad-64/legion/stop_go | 968 | 650 | -32.9% | incidental (within band) |
| attackring-squad-64/legion/stopped_permille | 753 | 513 | -31.9% | incidental (within band) |
| attackring-squad-64/legion/waiting_held | 13439 | 21795 | +62.2% | incidental (within band) |
| attackring-squad-64/legion/waiting_no_progress | 423 | 0 | -100.0% | incidental (within band) |
| attackring-squad-64/legion/wall_samples | 5239 | 9905 | +89.1% | incidental (within band) |
| attackring-squad-64/legion/work.held_rechecks.max | 61 | 17 | -72.1% | incidental (within band) |
| attackring-squad-64/legion/work.held_rechecks.p99 | 61 | 17 | -72.1% | incidental (within band) |
| attackring-squad-64/legion/work.held_rechecks.total | 125405 | 11926 | -90.5% | incidental (within band) |
| attackring-squad-64/legion/work.holds.max | 9 | 3 | -66.7% | incidental (within band) |
| attackring-squad-64/legion/work.holds.p99 | 5 | 2 | -60.0% | incidental (within band) |
| attackring-squad-64/legion/work.holds.total | 1029 | 669 | -35.0% | incidental (within band) |
| attackring-squad-64/legion/work.legion_total.p99 | 2707 | 2121 | -21.6% | incidental (within band) |
| attackring-squad-64/legion/work.legion_total.total | 1475567 | 1131277 | -23.3% | incidental (within band) |
| attackring-squad-64/legion/work.line_sweeps.max | 228 | 64 | -71.9% | incidental (within band) |
| attackring-squad-64/legion/work.line_sweeps.p99 | 196 | 36 | -81.6% | incidental (within band) |
| attackring-squad-64/legion/work.line_sweeps.total | 103526 | 22147 | -78.6% | incidental (within band) |
| attackring-squad-64/legion/work.move_calls_by_state_1.total | 47527 | 93555 | +96.8% | incidental (within band) |
| attackring-squad-64/legion/work.move_calls_by_state_2.max | 61 | 31 | -49.2% | incidental (within band) |
| attackring-squad-64/legion/work.move_calls_by_state_2.p99 | 61 | 26 | -57.4% | incidental (within band) |
| attackring-squad-64/legion/work.move_calls_by_state_2.total | 137883 | 29090 | -78.9% | incidental (within band) |
| attackring-squad-64/legion/work.moves.total | 124366 | 117418 | -5.6% | incidental (within band) |
| attackring-squad-64/legion/work.pass_scan_cells.max | 276 | 24 | -91.3% | incidental (within band) |
| attackring-squad-64/legion/work.pass_scan_cells.p99 | 174 | 6 | -96.6% | incidental (within band) |
| attackring-squad-64/legion/work.pass_scan_cells.total | 12228 | 834 | -93.2% | incidental (within band) |
| attackring-squad-64/legion/work.pass_scans.max | 46 | 4 | -91.3% | incidental (within band) |
| attackring-squad-64/legion/work.pass_scans.p99 | 29 | 1 | -96.6% | incidental (within band) |
| attackring-squad-64/legion/work.pass_scans.total | 2038 | 139 | -93.2% | incidental (within band) |
| attackring-squad-64/legion/work.pass_scans_skipped.total | 94469 | 53527 | -43.3% | incidental (within band) |
| attackring-squad-64/legion/work.slides.max | 8 | 2 | -75.0% | incidental (within band) |
| attackring-squad-64/legion/work.slides.p99 | 5 | 1 | -80.0% | incidental (within band) |
| attackring-squad-64/legion/work.slides.total | 1362 | 364 | -73.3% | incidental (within band) |
| attackring-squad-64/legion/work.still_units_processed.p99 | 1 | 3 | +200.0% | incidental (within band) |
| attackring-squad-64/legion/work.still_units_processed.total | 103 | 2452 | +2280.6% | incidental (within band) |
| attackring-squad-64/legion/work.trace_cells.p99 | 2568 | 2121 | -17.4% | incidental (within band) |
| attackring-squad-64/legion/work.trace_cells.total | 1030587 | 722402 | -29.9% | incidental (within band) |
| attackring-struct-60/legion/aim_reversals | 10 | 252 | +2420.0% | incidental (within band) |
| attackring-struct-60/legion/back | 0 | 288 | new | incidental (within band) |
| attackring-struct-60/legion/backward | 0 | 17 | new | incidental (within band) |
| attackring-struct-60/legion/churn.bin0 | 1173019 | 668195 | -43.0% | incidental (within band) |
| attackring-struct-60/legion/churn.bin1 | 123818 | 80091 | -35.3% | incidental (within band) |
| attackring-struct-60/legion/contact_own_permille | 943 | 870 | -7.7% | incidental (within band) |
| attackring-struct-60/legion/crawl_samples | 70 | 3058 | +4268.6% | incidental (within band) |
| attackring-struct-60/legion/detour_permille | 4 | 99 | +2375.0% | incidental (within band) |
| attackring-struct-60/legion/engaged.member_ticks | 13244 | 74741 | +464.3% | intended |
| attackring-struct-60/legion/engagement_on_flowing | 42 | 51 | +21.4% | incidental (within band) |
| attackring-struct-60/legion/flip_rate_per30_permille | 10 | 97 | +870.0% | incidental (within band) |
| attackring-struct-60/legion/flips | 60 | 581 | +868.3% | incidental (within band) |
| attackring-struct-60/legion/follow_chain_max | 17 | 5 | -70.6% | incidental (within band) |
| attackring-struct-60/legion/follow_chain_mean_x100 | 1277 | 208 | -83.7% | incidental (within band) |
| attackring-struct-60/legion/gauge.engaged_max | 6 | 44 | +633.3% | intended |
| attackring-struct-60/legion/open_still900 | 60 | 37 | -38.3% | incidental (within band) |
| attackring-struct-60/legion/parked_held | 108755 | 72680 | -33.2% | incidental (within band) |
| attackring-struct-60/legion/parked_no_progress | 12344 | 0 | -100.0% | incidental (within band) |
| attackring-struct-60/legion/reach.ring.damage_end | 4237 | 23846 | +462.8% | intended |
| attackring-struct-60/legion/reach.ring.damage_t1200 | 806 | 2743 | +240.3% | intended |
| attackring-struct-60/legion/reach.ring.damage_t2400 | 3093 | 15584 | +403.8% | intended |
| attackring-struct-60/legion/reach.ring.ever_end | 6 | 42 | +600.0% | intended |
| attackring-struct-60/legion/reach.ring.ever_t1200 | 6 | 25 | +316.7% | intended |
| attackring-struct-60/legion/reach.ring.ever_t2400 | 6 | 38 | +533.3% | intended |
| attackring-struct-60/legion/reach.ring.farthest_held | 28 | 10 | -64.3% | intended |
| attackring-struct-60/legion/reach.ring.hold_out_max | 2256 | 975 | -56.8% | intended |
| attackring-struct-60/legion/reach.ring.now_end | 6 | 42 | +600.0% | intended |
| attackring-struct-60/legion/reach.ring.out600_end | 54 | 6 | -88.9% | intended |
| attackring-struct-60/legion/reach.ring.out600_peak | 54 | 6 | -88.9% | intended |
| attackring-struct-60/legion/reach.ring.t50 | -1 | 1476 | +147700.0% | intended |
| attackring-struct-60/legion/reversals | 0 | 40 | new | incidental (within band) |
| attackring-struct-60/legion/sideways | 36 | 1467 | +3975.0% | incidental (within band) |
| attackring-struct-60/legion/spins | 8 | 178 | +2125.0% | incidental (within band) |
| attackring-struct-60/legion/statue_ticks | 821 | 29115 | +3446.3% | incidental (within band) |
| attackring-struct-60/legion/still900_ever | 60 | 37 | -38.3% | incidental (within band) |
| attackring-struct-60/legion/stop_go | 184 | 513 | +178.8% | incidental (within band) |
| attackring-struct-60/legion/stopped_permille | 725 | 524 | -27.7% | incidental (within band) |
| attackring-struct-60/legion/waiting_held | 7615 | 16758 | +120.1% | incidental (within band) |
| attackring-struct-60/legion/waiting_no_progress | 846 | 0 | -100.0% | incidental (within band) |
| attackring-struct-60/legion/wall_samples | 5010 | 9018 | +80.0% | incidental (within band) |
| attackring-struct-60/legion/work.held_rechecks.max | 54 | 9 | -83.3% | incidental (within band) |
| attackring-struct-60/legion/work.held_rechecks.p99 | 54 | 9 | -83.3% | incidental (within band) |
| attackring-struct-60/legion/work.held_rechecks.total | 113615 | 7061 | -93.8% | incidental (within band) |
| attackring-struct-60/legion/work.holds.max | 5 | 3 | -40.0% | incidental (within band) |
| attackring-struct-60/legion/work.holds.total | 238 | 522 | +119.3% | incidental (within band) |
| attackring-struct-60/legion/work.legion_total.p99 | 1721 | 1156 | -32.8% | incidental (within band) |
| attackring-struct-60/legion/work.legion_total.total | 1296837 | 748286 | -42.3% | incidental (within band) |
| attackring-struct-60/legion/work.line_sweeps.max | 206 | 120 | -41.7% | incidental (within band) |
| attackring-struct-60/legion/work.line_sweeps.p99 | 191 | 23 | -88.0% | incidental (within band) |
| attackring-struct-60/legion/work.line_sweeps.total | 146838 | 16287 | -88.9% | incidental (within band) |
| attackring-struct-60/legion/work.move_calls_by_state_1.total | 49560 | 85739 | +73.0% | incidental (within band) |
| attackring-struct-60/legion/work.move_calls_by_state_2.max | 54 | 22 | -59.3% | incidental (within band) |
| attackring-struct-60/legion/work.move_calls_by_state_2.p99 | 54 | 19 | -64.8% | incidental (within band) |
| attackring-struct-60/legion/work.move_calls_by_state_2.total | 117076 | 19400 | -83.4% | incidental (within band) |
| attackring-struct-60/legion/work.moves.total | 111218 | 101991 | -8.3% | incidental (within band) |
| attackring-struct-60/legion/work.pass_scan_cells.max | 60 | 12 | -80.0% | incidental (within band) |
| attackring-struct-60/legion/work.pass_scan_cells.p99 | 12 | 0 | -100.0% | incidental (within band) |
| attackring-struct-60/legion/work.pass_scan_cells.total | 1602 | 246 | -84.6% | incidental (within band) |
| attackring-struct-60/legion/work.pass_scans.max | 10 | 2 | -80.0% | incidental (within band) |
| attackring-struct-60/legion/work.pass_scans.p99 | 2 | 0 | -100.0% | incidental (within band) |
| attackring-struct-60/legion/work.pass_scans.total | 267 | 41 | -84.6% | incidental (within band) |
| attackring-struct-60/legion/work.pass_scans_skipped.total | 108503 | 47624 | -56.1% | incidental (within band) |
| attackring-struct-60/legion/work.slides.max | 6 | 3 | -50.0% | incidental (within band) |
| attackring-struct-60/legion/work.slides.p99 | 3 | 1 | -66.7% | incidental (within band) |
| attackring-struct-60/legion/work.slides.total | 208 | 341 | +63.9% | incidental (within band) |
| attackring-struct-60/legion/work.still_units_processed.p99 | 0 | 2 | new | incidental (within band) |
| attackring-struct-60/legion/work.still_units_processed.total | 2 | 2490 | +124400.0% | incidental (within band) |
| attackring-struct-60/legion/work.trace_cells.p99 | 1720 | 1053 | -38.8% | incidental (within band) |
| attackring-struct-60/legion/work.trace_cells.total | 881273 | 374485 | -57.5% | incidental (within band) |
| attackring-wall-64/legion/aim_reversals | 12 | 324 | +2600.0% | incidental (within band) |
| attackring-wall-64/legion/back | 0 | 192 | new | incidental (within band) |
| attackring-wall-64/legion/backward | 0 | 20 | new | incidental (within band) |
| attackring-wall-64/legion/churn.bin1 | 306890 | 429716 | +40.0% | incidental (within band) |
| attackring-wall-64/legion/clearance_mean_x100 | 544 | 318 | -41.5% | incidental (within band) |
| attackring-wall-64/legion/clearance_p10_x100 | 100 | 0 | -100.0% | incidental (within band) |
| attackring-wall-64/legion/crawl_samples | 170 | 3461 | +1935.9% | incidental (within band) |
| attackring-wall-64/legion/detour_permille | 7 | 96 | +1271.4% | incidental (within band) |
| attackring-wall-64/legion/engagement_on_flowing | 43 | 38 | -11.6% | incidental (within band) |
| attackring-wall-64/legion/flip_rate_per30_permille | 21 | 210 | +900.0% | incidental (within band) |
| attackring-wall-64/legion/flips | 129 | 1306 | +912.4% | incidental (within band) |
| attackring-wall-64/legion/follow_chain_mean_x100 | 1134 | 562 | -50.4% | incidental (within band) |
| attackring-wall-64/legion/open_still900 | 62 | 32 | -48.4% | incidental (within band) |
| attackring-wall-64/legion/parked_held | 118543 | 52471 | -55.7% | incidental (within band) |
| attackring-wall-64/legion/reach.ring.ever_end | 19 | 53 | +178.9% | intended |
| attackring-wall-64/legion/reach.ring.ever_t1200 | 19 | 23 | +21.1% | intended |
| attackring-wall-64/legion/reach.ring.ever_t2400 | 19 | 50 | +163.2% | intended |
| attackring-wall-64/legion/reach.ring.farthest_held | 33 | 8 | -75.8% | intended |
| attackring-wall-64/legion/reach.ring.hold_out_max | 2111 | 1482 | -29.8% | intended |
| attackring-wall-64/legion/reach.ring.now_end | 19 | 30 | +57.9% | intended |
| attackring-wall-64/legion/reach.ring.out600_end | 45 | 17 | -62.2% | intended |
| attackring-wall-64/legion/reach.ring.out600_peak | 45 | 17 | -62.2% | intended |
| attackring-wall-64/legion/reach.ring.t50 | -1 | 1488 | +148900.0% | intended |
| attackring-wall-64/legion/reversals | 0 | 32 | new | incidental (within band) |
| attackring-wall-64/legion/sideways | 121 | 1535 | +1168.6% | incidental (within band) |
| attackring-wall-64/legion/statue_ticks | 1660 | 30845 | +1758.1% | incidental (within band) |
| attackring-wall-64/legion/still900_ever | 62 | 33 | -46.8% | incidental (within band) |
| attackring-wall-64/legion/stop_go | 286 | 1114 | +289.5% | incidental (within band) |
| attackring-wall-64/legion/stopped_permille | 700 | 491 | -29.9% | incidental (within band) |
| attackring-wall-64/legion/waiting_held | 9427 | 30360 | +222.1% | incidental (within band) |
| attackring-wall-64/legion/wall_near_samples | 342 | 4456 | +1202.9% | incidental (within band) |
| attackring-wall-64/legion/wall_samples | 5857 | 10425 | +78.0% | incidental (within band) |
| attackring-wall-64/legion/wall_touch_near_permille | 81 | 112 | +38.3% | incidental (within band) |
| attackring-wall-64/legion/wall_touch_permille | 4 | 58 | +1350.0% | incidental (within band) |
| attackring-wall-64/legion/work.crowd_window_ring_cells.total | 296064 | 266940 | -9.8% | incidental (within band) |
| attackring-wall-64/legion/work.detour_cells.max | 560 | 2130 | +280.4% | incidental (within band) |
| attackring-wall-64/legion/work.detour_cells.p99 | 0 | 1054 | new | incidental (within band) |
| attackring-wall-64/legion/work.detour_cells.total | 5393 | 240905 | +4367.0% | incidental (within band) |
| attackring-wall-64/legion/work.held_rechecks.max | 62 | 52 | -16.1% | incidental (within band) |
| attackring-wall-64/legion/work.held_rechecks.p99 | 62 | 52 | -16.1% | incidental (within band) |
| attackring-wall-64/legion/work.held_rechecks.total | 124123 | 61542 | -50.4% | incidental (within band) |
| attackring-wall-64/legion/work.holds.p99 | 2 | 3 | +50.0% | incidental (within band) |
| attackring-wall-64/legion/work.holds.total | 348 | 1167 | +235.3% | incidental (within band) |
| attackring-wall-64/legion/work.legion_total.total | 1660581 | 1748067 | +5.3% | incidental (within band) |
| attackring-wall-64/legion/work.line_sweeps.total | 163556 | 175796 | +7.5% | incidental (within band) |
| attackring-wall-64/legion/work.move_calls_by_state_1.total | 56471 | 95443 | +69.0% | incidental (within band) |
| attackring-wall-64/legion/work.move_calls_by_state_2.max | 62 | 57 | -8.1% | incidental (within band) |
| attackring-wall-64/legion/work.move_calls_by_state_2.p99 | 62 | 56 | -9.7% | incidental (within band) |
| attackring-wall-64/legion/work.move_calls_by_state_2.total | 131196 | 92050 | -29.8% | incidental (within band) |
| attackring-wall-64/legion/work.moves.total | 127745 | 155516 | +21.7% | incidental (within band) |
| attackring-wall-64/legion/work.pass_scan_cells.total | 1464 | 1806 | +23.4% | incidental (within band) |
| attackring-wall-64/legion/work.pass_scans.total | 244 | 301 | +23.4% | incidental (within band) |
| attackring-wall-64/legion/work.pass_scans_skipped.total | 84408 | 55439 | -34.3% | incidental (within band) |
| attackring-wall-64/legion/work.slides.max | 5 | 6 | +20.0% | incidental (within band) |
| attackring-wall-64/legion/work.slides.total | 469 | 570 | +21.5% | incidental (within band) |
| attackring-wall-64/legion/work.trace_cells.total | 983138 | 1038485 | +5.6% | incidental (within band) |
| battle-assault/legion/aim_reversals | 6608 | 7139 | +8.0% | incidental (within band) |
| battle-assault/legion/back | 2615 | 2856 | +9.2% | incidental (within band) |
| battle-assault/legion/churn.bin0 | 107968771 | 98903826 | -8.4% | incidental (within band) |
| battle-assault/legion/churn.bin1 | 100567623 | 91005119 | -9.5% | incidental (within band) |
| battle-assault/legion/churn.bin2 | 64475231 | 58126599 | -9.8% | incidental (within band) |
| battle-assault/legion/g.A1.dead | 21 | 23 | +9.5% | incidental (within band) |
| battle-assault/legion/g.A1.left_behind | 34 | 32 | -5.9% | incidental (within band) |
| battle-assault/legion/g.A3.left_behind | 5 | 3 | -40.0% | incidental (within band) |
| battle-assault/legion/g.A6.left_behind | 8 | 7 | -12.5% | incidental (within band) |
| battle-assault/legion/g.A7.left_behind | 7 | 5 | -28.6% | incidental (within band) |
| battle-assault/legion/g.B1.dead | 45 | 48 | +6.7% | incidental (within band) |
| battle-assault/legion/g.B1.left_behind | 10 | 7 | -30.0% | incidental (within band) |
| battle-assault/legion/g.B2.left_behind | 3 | 5 | +66.7% | incidental (within band) |
| battle-assault/legion/g.B4.left_behind | 6 | 8 | +33.3% | incidental (within band) |
| battle-assault/legion/g.B5.dead | 32 | 30 | -6.2% | incidental (within band) |
| battle-assault/legion/g.B5.left_behind | 23 | 25 | +8.7% | incidental (within band) |
| battle-assault/legion/g.B6.dead | 18 | 20 | +11.1% | incidental (within band) |
| battle-assault/legion/g.B6.left_behind | 37 | 35 | -5.4% | incidental (within band) |
| battle-assault/legion/g.B7.dead | 27 | 31 | +14.8% | incidental (within band) |
| battle-assault/legion/g.B7.left_behind | 28 | 24 | -14.3% | incidental (within band) |
| battle-assault/legion/g.B8.left_behind | 0 | 1 | new | incidental (within band) |
| battle-assault/legion/legion_groups_peak | 125 | 109 | -12.8% | incidental (within band) |
| battle-assault/legion/parked_no_progress | 309 | 151 | -51.1% | incidental (within band) |
| battle-assault/legion/reversals | 1582 | 1665 | +5.2% | incidental (within band) |
| battle-assault/legion/waiting_no_progress | 62625 | 34708 | -44.6% | incidental (within band) |
| battle-assault/legion/wall_near_samples | 31586 | 33299 | +5.4% | incidental (within band) |
| battle-assault/legion/wall_touch_near_permille | 77 | 73 | -5.2% | incidental (within band) |
| battle-assault/legion/work.aware_pairs.p99 | 224 | 195 | -12.9% | incidental (within band) |
| battle-assault/legion/work.blocked_rerequests.total | 6 | 11 | +83.3% | incidental (within band) |
| battle-assault/legion/work.detour_cells.max | 4154 | 3207 | -22.8% | incidental (within band) |
| battle-assault/legion/work.detour_cells.total | 2164398 | 2336456 | +7.9% | incidental (within band) |
| battle-assault/legion/work.detours.max | 10 | 9 | -10.0% | incidental (within band) |
| battle-assault/legion/work.field_work.total | 215110160 | 182877648 | -15.0% | incidental (within band) |
| battle-assault/legion/work.field_work_first_slot.total | 32998304 | 31182640 | -5.5% | incidental (within band) |
| battle-assault/legion/work.field_work_first_solo.p99 | 271048 | 253896 | -6.3% | incidental (within band) |
| battle-assault/legion/work.field_work_first_solo.total | 163758784 | 123113192 | -24.8% | incidental (within band) |
| battle-assault/legion/work.field_work_refresh_moving.total | 19476416 | 21525544 | +10.5% | incidental (within band) |
| battle-assault/legion/work.fields_built.total | 3862 | 2987 | -22.7% | incidental (within band) |
| battle-assault/legion/work.fields_shared.p99 | 1 | 0 | -100.0% | incidental (within band) |
| battle-assault/legion/work.fields_shared.total | 117 | 23 | -80.3% | incidental (within band) |
| battle-assault/legion/work.fields_started_by_kind_2.max | 3 | 2 | -33.3% | incidental (within band) |
| battle-assault/legion/work.fields_started_by_kind_2.total | 257 | 273 | +6.2% | incidental (within band) |
| battle-assault/legion/work.fields_started_by_kind_4.total | 3600 | 2736 | -24.0% | incidental (within band) |
| battle-assault/legion/work.formation_ring_cells.p99 | 76113 | 83921 | +10.3% | incidental (within band) |
| battle-assault/legion/work.formation_ring_cells.total | 26496264 | 28880338 | +9.0% | incidental (within band) |
| battle-assault/legion/work.group_loop_iters.p99 | 893 | 745 | -16.6% | incidental (within band) |
| battle-assault/legion/work.group_loop_iters.total | 825376 | 675355 | -18.2% | incidental (within band) |
| battle-assault/legion/work.groups.p99 | 5 | 4 | -20.0% | incidental (within band) |
| battle-assault/legion/work.groups.total | 4027 | 2776 | -31.1% | incidental (within band) |
| battle-assault/legion/work.join_iterations.p99 | 878 | 736 | -16.2% | incidental (within band) |
| battle-assault/legion/work.join_iterations.total | 806458 | 659073 | -18.3% | incidental (within band) |
| battle-assault/legion/work.legion_total.max | 511216 | 457355 | -10.5% | incidental (within band) |
| battle-assault/legion/work.legion_total.total | 271904335 | 239964275 | -11.7% | incidental (within band) |
| battle-assault/legion/work.mission_legs_2.p99 | 16 | 17 | +6.2% | incidental (within band) |
| battle-assault/legion/work.move_calls_by_state_3.total | 16287 | 15232 | -6.5% | incidental (within band) |
| battle-assault/legion/work.pivot_part_ids.max | 220 | 243 | +10.5% | incidental (within band) |
| battle-assault/legion/work.pivot_part_ids.total | 48203 | 50653 | +5.1% | incidental (within band) |
| battle-assault/legion/work.refresh_completed.max | 1 | 4 | +300.0% | incidental (within band) |
| battle-assault/legion/work.refresh_completed.p99 | 1 | 2 | +100.0% | incidental (within band) |
| battle-assault/legion/work.refresh_completed.total | 42 | 324 | +671.4% | incidental (within band) |
| battle-assault/legion/work.refresh_deferred.max | 3 | 5 | +66.7% | incidental (within band) |
| battle-assault/legion/work.refresh_deferred.p99 | 1 | 2 | +100.0% | incidental (within band) |
| battle-assault/legion/work.refresh_deferred.total | 112 | 200 | +78.6% | incidental (within band) |
| battle-assault/legion/work.sched_group_visits.max | 10 | 11 | +10.0% | incidental (within band) |
| battle-assault/legion/work.sched_group_visits.p99 | 7 | 6 | -14.3% | incidental (within band) |
| battle-assault/legion/work.sched_group_visits.total | 6928 | 5495 | -20.7% | incidental (within band) |
| battle-assault/legion/work.share_scan_iters.max | 11 | 8 | -27.3% | incidental (within band) |
| battle-assault/legion/work.share_scan_iters.p99 | 6 | 4 | -33.3% | incidental (within band) |
| battle-assault/legion/work.share_scan_iters.total | 2613 | 467 | -82.1% | incidental (within band) |
| battle-assault/legion/work.slides.p99 | 15 | 16 | +6.7% | incidental (within band) |
| battle-assault/legion/work.slot_search_cells.p99 | 76113 | 83921 | +10.3% | incidental (within band) |
| battle-assault/legion/work.slot_search_cells.total | 26663660 | 29158705 | +9.4% | incidental (within band) |
| battle-assault/legion/work.still_units_processed.p99 | 3 | 4 | +33.3% | incidental (within band) |
| battle-assault/legion/work.still_units_processed.total | 1794 | 3218 | +79.4% | incidental (within band) |
| battle-assault/legion/work.waiting_member_ticks.max | 7 | 5 | -28.6% | incidental (within band) |
| battle-assault/legion/work.waiting_member_ticks.p99 | 3 | 2 | -33.3% | incidental (within band) |
| battle-assault/legion/work.waiting_member_ticks.total | 1724 | 1202 | -30.3% | incidental (within band) |
| battle-field-2x250/legion/aim_reversals | 1699 | 1941 | +14.2% | incidental (within band) |
| battle-field-2x250/legion/back | 300 | 478 | +59.3% | incidental (within band) |
| battle-field-2x250/legion/backward | 16839 | 19122 | +13.6% | incidental (within band) |
| battle-field-2x250/legion/churn.bin0 | 62527101 | 53326473 | -14.7% | incidental (within band) |
| battle-field-2x250/legion/churn.bin2 | 124904 | 149095 | +19.4% | incidental (within band) |
| battle-field-2x250/legion/contact_own_permille | 432 | 460 | +6.5% | incidental (within band) |
| battle-field-2x250/legion/crawl_samples | 9830 | 11007 | +12.0% | incidental (within band) |
| battle-field-2x250/legion/detour_permille | 35 | 40 | +14.3% | incidental (within band) |
| battle-field-2x250/legion/g.A.arrived | 5 | 19 | +280.0% | incidental (within band) |
| battle-field-2x250/legion/g.A.complete_dist_max | 36 | 24 | -33.3% | incidental (within band) |
| battle-field-2x250/legion/g.A.complete_dist_median | 3 | 9 | +200.0% | incidental (within band) |
| battle-field-2x250/legion/g.A.complete_n | 5 | 19 | +280.0% | incidental (within band) |
| battle-field-2x250/legion/g.A.dead | 241 | 217 | -10.0% | incidental (within band) |
| battle-field-2x250/legion/g.A.left_behind | 5 | 10 | +100.0% | incidental (within band) |
| battle-field-2x250/legion/g.B.arrived | 3 | 0 | -100.0% | incidental (within band) |
| battle-field-2x250/legion/g.B.complete_dist_max | 9 | -1 | -111.1% | incidental (within band) |
| battle-field-2x250/legion/g.B.complete_dist_median | 3 | -1 | -133.3% | incidental (within band) |
| battle-field-2x250/legion/g.B.complete_n | 3 | 0 | -100.0% | incidental (within band) |
| battle-field-2x250/legion/g.B.left_behind | 6 | 1 | -83.3% | incidental (within band) |
| battle-field-2x250/legion/gauge.engaged_max | 48 | 43 | -10.4% | intended |
| battle-field-2x250/legion/legion_groups_peak | 75 | 59 | -21.3% | incidental (within band) |
| battle-field-2x250/legion/parked_held | 493 | 430 | -12.8% | incidental (within band) |
| battle-field-2x250/legion/reversals | 189 | 241 | +27.5% | incidental (within band) |
| battle-field-2x250/legion/sideways | 53408 | 56350 | +5.5% | incidental (within band) |
| battle-field-2x250/legion/spins | 4298 | 4640 | +8.0% | incidental (within band) |
| battle-field-2x250/legion/statue_ticks | 98011 | 108210 | +10.4% | incidental (within band) |
| battle-field-2x250/legion/stop_go | 4536 | 4772 | +5.2% | incidental (within band) |
| battle-field-2x250/legion/waiting_held | 59053 | 82311 | +39.4% | incidental (within band) |
| battle-field-2x250/legion/waiting_no_progress | 39394 | 15761 | -60.0% | incidental (within band) |
| battle-field-2x250/legion/work.arrivals.max | 1 | 2 | +100.0% | incidental (within band) |
| battle-field-2x250/legion/work.arrivals.total | 13 | 19 | +46.2% | incidental (within band) |
| battle-field-2x250/legion/work.completion_dist_0.total | 1 | 2 | +100.0% | incidental (within band) |
| battle-field-2x250/legion/work.completion_dist_2.total | 5 | 4 | -20.0% | incidental (within band) |
| battle-field-2x250/legion/work.completion_dist_4.total | 1 | 5 | +400.0% | incidental (within band) |
| battle-field-2x250/legion/work.completion_dist_5.total | 1 | 5 | +400.0% | incidental (within band) |
| battle-field-2x250/legion/work.completion_dist_6.max | 1 | 0 | -100.0% | incidental (within band) |
| battle-field-2x250/legion/work.completion_dist_6.total | 3 | 0 | -100.0% | incidental (within band) |
| battle-field-2x250/legion/work.completion_dist_sum.max | 36 | 24 | -33.3% | incidental (within band) |
| battle-field-2x250/legion/work.completion_dist_sum.total | 145 | 179 | +23.4% | incidental (within band) |
| battle-field-2x250/legion/work.contact_arrivals.max | 1 | 2 | +100.0% | incidental (within band) |
| battle-field-2x250/legion/work.contact_arrivals.total | 6 | 17 | +183.3% | incidental (within band) |
| battle-field-2x250/legion/work.crowd_settle_visits.max | 190 | 33 | -82.6% | incidental (within band) |
| battle-field-2x250/legion/work.crowd_settle_visits.total | 190 | 33 | -82.6% | incidental (within band) |
| battle-field-2x250/legion/work.crowd_window_ring_cells.max | 808 | 928 | +14.9% | incidental (within band) |
| battle-field-2x250/legion/work.detour_cells.max | 2972 | 2441 | -17.9% | incidental (within band) |
| battle-field-2x250/legion/work.detour_cells.p99 | 1061 | 1173 | +10.6% | incidental (within band) |
| battle-field-2x250/legion/work.detour_cells.total | 277125 | 420106 | +51.6% | incidental (within band) |
| battle-field-2x250/legion/work.detours.p99 | 2 | 3 | +50.0% | incidental (within band) |
| battle-field-2x250/legion/work.detours.total | 624 | 956 | +53.2% | incidental (within band) |
| battle-field-2x250/legion/work.field_work.p99 | 150360 | 141976 | -5.6% | incidental (within band) |
| battle-field-2x250/legion/work.field_work.total | 63553600 | 50246600 | -20.9% | incidental (within band) |
| battle-field-2x250/legion/work.field_work_first_slot.total | 4592408 | 4223128 | -8.0% | incidental (within band) |
| battle-field-2x250/legion/work.field_work_first_solo.max | 291072 | 328976 | +13.0% | incidental (within band) |
| battle-field-2x250/legion/work.field_work_first_solo.total | 58961192 | 43246848 | -26.7% | incidental (within band) |
| battle-field-2x250/legion/work.fields_built.max | 5 | 6 | +20.0% | incidental (within band) |
| battle-field-2x250/legion/work.fields_built.total | 1443 | 1105 | -23.4% | incidental (within band) |
| battle-field-2x250/legion/work.fields_shared.p99 | 1 | 0 | -100.0% | incidental (within band) |
| battle-field-2x250/legion/work.fields_shared.total | 48 | 7 | -85.4% | incidental (within band) |
| battle-field-2x250/legion/work.fields_started_by_kind_2.max | 2 | 1 | -50.0% | incidental (within band) |
| battle-field-2x250/legion/work.fields_started_by_kind_2.total | 17 | 15 | -11.8% | incidental (within band) |
| battle-field-2x250/legion/work.fields_started_by_kind_4.max | 5 | 6 | +20.0% | incidental (within band) |
| battle-field-2x250/legion/work.fields_started_by_kind_4.total | 1417 | 1085 | -23.4% | incidental (within band) |
| battle-field-2x250/legion/work.formation_ring_cells.max | 172773 | 161244 | -6.7% | incidental (within band) |
| battle-field-2x250/legion/work.group_loop_iters.p99 | 319 | 246 | -22.9% | incidental (within band) |
| battle-field-2x250/legion/work.group_loop_iters.total | 144026 | 105918 | -26.5% | incidental (within band) |
| battle-field-2x250/legion/work.groups.max | 5 | 6 | +20.0% | incidental (within band) |
| battle-field-2x250/legion/work.groups.total | 1506 | 1065 | -29.3% | incidental (within band) |
| battle-field-2x250/legion/work.held_rechecks.max | 10 | 9 | -10.0% | incidental (within band) |
| battle-field-2x250/legion/work.held_rechecks.p99 | 6 | 5 | -16.7% | incidental (within band) |
| battle-field-2x250/legion/work.held_rechecks.total | 2227 | 1860 | -16.5% | incidental (within band) |
| battle-field-2x250/legion/work.join_iterations.p99 | 315 | 245 | -22.2% | incidental (within band) |
| battle-field-2x250/legion/work.join_iterations.total | 139507 | 102067 | -26.8% | incidental (within band) |
| battle-field-2x250/legion/work.legion_total.total | 98481163 | 88083594 | -10.6% | incidental (within band) |
| battle-field-2x250/legion/work.line_sweeps.p99 | 101 | 252 | +149.5% | incidental (within band) |
| battle-field-2x250/legion/work.line_sweeps.total | 106973 | 149494 | +39.7% | incidental (within band) |
| battle-field-2x250/legion/work.mission_arrivals_2.max | 1 | 2 | +100.0% | incidental (within band) |
| battle-field-2x250/legion/work.mission_arrivals_2.total | 13 | 19 | +46.2% | incidental (within band) |
| battle-field-2x250/legion/work.move_calls_by_state_3.p99 | 12 | 13 | +8.3% | incidental (within band) |
| battle-field-2x250/legion/work.pass_scan_cells.p99 | 324 | 450 | +38.9% | incidental (within band) |
| battle-field-2x250/legion/work.pass_scans.p99 | 8 | 10 | +25.0% | incidental (within band) |
| battle-field-2x250/legion/work.rechoice_bfs_cells.max | 750 | 1810 | +141.3% | incidental (within band) |
| battle-field-2x250/legion/work.rechoice_bfs_cells.total | 4713 | 19111 | +305.5% | incidental (within band) |
| battle-field-2x250/legion/work.sched_group_visits.max | 6 | 7 | +16.7% | incidental (within band) |
| battle-field-2x250/legion/work.sched_group_visits.total | 1516 | 1196 | -21.1% | incidental (within band) |
| battle-field-2x250/legion/work.share_scan_iters.max | 10 | 4 | -60.0% | incidental (within band) |
| battle-field-2x250/legion/work.share_scan_iters.p99 | 4 | 0 | -100.0% | incidental (within band) |
| battle-field-2x250/legion/work.share_scan_iters.total | 910 | 18 | -98.0% | incidental (within band) |
| battle-field-2x250/legion/work.slides.max | 12 | 11 | -8.3% | incidental (within band) |
| battle-field-2x250/legion/work.slot_search_cells.max | 172773 | 161244 | -6.7% | incidental (within band) |
| battle-field-2x250/legion/work.still_units_processed.p99 | 2 | 3 | +50.0% | incidental (within band) |
| battle-field-2x250/legion/work.still_units_processed.total | 872 | 1898 | +117.7% | incidental (within band) |
| battle-field-2x250/legion/work.trace_cells.max | 25974 | 28329 | +9.1% | incidental (within band) |
| battle-field-2x250/legion/work.trace_cells.p99 | 17518 | 21047 | +20.1% | incidental (within band) |
| battle-field-2x250/legion/work.trace_cells.total | 13404174 | 15788135 | +17.8% | incidental (within band) |
| battle-field-2x500/legion/aim_reversals | 5203 | 5848 | +12.4% | incidental (within band) |
| battle-field-2x500/legion/back | 811 | 1159 | +42.9% | incidental (within band) |
| battle-field-2x500/legion/backward | 44593 | 47650 | +6.9% | incidental (within band) |
| battle-field-2x500/legion/churn.bin1 | 123285739 | 107273537 | -13.0% | incidental (within band) |
| battle-field-2x500/legion/churn.bin2 | 13281658 | 11407905 | -14.1% | incidental (within band) |
| battle-field-2x500/legion/churn.bin3 | 128617 | 103506 | -19.5% | incidental (within band) |
| battle-field-2x500/legion/g.A.arrived | 70 | 36 | -48.6% | incidental (within band) |
| battle-field-2x500/legion/g.A.complete_dist_max | 49 | 44 | -10.2% | incidental (within band) |
| battle-field-2x500/legion/g.A.complete_dist_median | 22 | 13 | -40.9% | incidental (within band) |
| battle-field-2x500/legion/g.A.complete_n | 70 | 36 | -48.6% | incidental (within band) |
| battle-field-2x500/legion/g.A.dead | 416 | 460 | +10.6% | incidental (within band) |
| battle-field-2x500/legion/g.A.left_behind | 12 | 5 | -58.3% | incidental (within band) |
| battle-field-2x500/legion/g.B.arrived | 4 | 35 | +775.0% | incidental (within band) |
| battle-field-2x500/legion/g.B.complete_dist_max | 6 | 42 | +600.0% | incidental (within band) |
| battle-field-2x500/legion/g.B.complete_dist_median | 1 | 14 | +1300.0% | incidental (within band) |
| battle-field-2x500/legion/g.B.complete_n | 4 | 35 | +775.0% | incidental (within band) |
| battle-field-2x500/legion/g.B.dead | 496 | 464 | -6.5% | incidental (within band) |
| battle-field-2x500/legion/g.B.left_behind | 0 | 1 | new | incidental (within band) |
| battle-field-2x500/legion/legion_groups_peak | 102 | 87 | -14.7% | incidental (within band) |
| battle-field-2x500/legion/parked_held | 3081 | 2426 | -21.3% | incidental (within band) |
| battle-field-2x500/legion/reversals | 423 | 593 | +40.2% | incidental (within band) |
| battle-field-2x500/legion/spins | 9147 | 9897 | +8.2% | incidental (within band) |
| battle-field-2x500/legion/waiting_held | 205629 | 237007 | +15.3% | incidental (within band) |
| battle-field-2x500/legion/waiting_no_progress | 72301 | 27770 | -61.6% | incidental (within band) |
| battle-field-2x500/legion/work.arrivals.max | 2 | 3 | +50.0% | incidental (within band) |
| battle-field-2x500/legion/work.arrivals.total | 74 | 70 | -5.4% | incidental (within band) |
| battle-field-2x500/legion/work.aware_pairs.p99 | 6 | 8 | +33.3% | incidental (within band) |
| battle-field-2x500/legion/work.aware_pairs.total | 694 | 937 | +35.0% | incidental (within band) |
| battle-field-2x500/legion/work.completion_dist_2.total | 5 | 7 | +40.0% | incidental (within band) |
| battle-field-2x500/legion/work.completion_dist_5.max | 2 | 1 | -50.0% | incidental (within band) |
| battle-field-2x500/legion/work.completion_dist_5.total | 16 | 17 | +6.2% | incidental (within band) |
| battle-field-2x500/legion/work.completion_dist_6.total | 19 | 14 | -26.3% | incidental (within band) |
| battle-field-2x500/legion/work.completion_dist_sum.max | 59 | 44 | -25.4% | incidental (within band) |
| battle-field-2x500/legion/work.completion_dist_sum.p99 | 6 | 3 | -50.0% | incidental (within band) |
| battle-field-2x500/legion/work.completion_dist_sum.total | 1523 | 1227 | -19.4% | incidental (within band) |
| battle-field-2x500/legion/work.contact_arrivals.max | 2 | 3 | +50.0% | incidental (within band) |
| battle-field-2x500/legion/work.crowd_settle_visits.max | 552 | 75 | -86.4% | incidental (within band) |
| battle-field-2x500/legion/work.crowd_settle_visits.total | 552 | 75 | -86.4% | incidental (within band) |
| battle-field-2x500/legion/work.crowd_window_ring_cells.total | 300015 | 273128 | -9.0% | incidental (within band) |
| battle-field-2x500/legion/work.detour_cells.max | 4049 | 3458 | -14.6% | incidental (within band) |
| battle-field-2x500/legion/work.detour_cells.p99 | 1448 | 1614 | +11.5% | incidental (within band) |
| battle-field-2x500/legion/work.detour_cells.total | 809813 | 1027079 | +26.8% | incidental (within band) |
| battle-field-2x500/legion/work.detours.total | 2022 | 2586 | +27.9% | incidental (within band) |
| battle-field-2x500/legion/work.field_work.p99 | 180816 | 169048 | -6.5% | incidental (within band) |
| battle-field-2x500/legion/work.field_work.total | 135580000 | 106152288 | -21.7% | incidental (within band) |
| battle-field-2x500/legion/work.field_work_first_slot.total | 6468584 | 5369392 | -17.0% | incidental (within band) |
| battle-field-2x500/legion/work.field_work_first_solo.max | 364800 | 302224 | -17.2% | incidental (within band) |
| battle-field-2x500/legion/work.field_work_first_solo.p99 | 171536 | 155504 | -9.3% | incidental (within band) |
| battle-field-2x500/legion/work.field_work_first_solo.total | 128654176 | 95793072 | -25.5% | incidental (within band) |
| battle-field-2x500/legion/work.fields_built.total | 3052 | 2368 | -22.4% | incidental (within band) |
| battle-field-2x500/legion/work.fields_shared.p99 | 1 | 0 | -100.0% | incidental (within band) |
| battle-field-2x500/legion/work.fields_shared.total | 109 | 16 | -85.3% | incidental (within band) |
| battle-field-2x500/legion/work.fields_started_by_kind_2.total | 12 | 4 | -66.7% | incidental (within band) |
| battle-field-2x500/legion/work.fields_started_by_kind_4.total | 3034 | 2362 | -22.1% | incidental (within band) |
| battle-field-2x500/legion/work.group_loop_iters.max | 953 | 741 | -22.2% | incidental (within band) |
| battle-field-2x500/legion/work.group_loop_iters.p99 | 570 | 440 | -22.8% | incidental (within band) |
| battle-field-2x500/legion/work.group_loop_iters.total | 422424 | 325169 | -23.0% | incidental (within band) |
| battle-field-2x500/legion/work.groups.total | 3180 | 2291 | -28.0% | incidental (within band) |
| battle-field-2x500/legion/work.held_rechecks.max | 19 | 17 | -10.5% | incidental (within band) |
| battle-field-2x500/legion/work.held_rechecks.p99 | 12 | 10 | -16.7% | incidental (within band) |
| battle-field-2x500/legion/work.held_rechecks.total | 7974 | 6644 | -16.7% | incidental (within band) |
| battle-field-2x500/legion/work.holds.max | 18 | 20 | +11.1% | incidental (within band) |
| battle-field-2x500/legion/work.holds.p99 | 12 | 13 | +8.3% | incidental (within band) |
| battle-field-2x500/legion/work.join_iterations.max | 950 | 740 | -22.1% | incidental (within band) |
| battle-field-2x500/legion/work.join_iterations.p99 | 562 | 438 | -22.1% | incidental (within band) |
| battle-field-2x500/legion/work.join_iterations.total | 412979 | 317281 | -23.2% | incidental (within band) |
| battle-field-2x500/legion/work.legion_total.max | 452070 | 405958 | -10.2% | incidental (within band) |
| battle-field-2x500/legion/work.legion_total.total | 235851962 | 214789993 | -8.9% | incidental (within band) |
| battle-field-2x500/legion/work.line_sweeps.p99 | 213 | 420 | +97.2% | incidental (within band) |
| battle-field-2x500/legion/work.line_sweeps.total | 311726 | 401831 | +28.9% | incidental (within band) |
| battle-field-2x500/legion/work.mission_arrivals_2.max | 2 | 3 | +50.0% | incidental (within band) |
| battle-field-2x500/legion/work.mission_arrivals_2.total | 74 | 70 | -5.4% | incidental (within band) |
| battle-field-2x500/legion/work.mission_legs_4.max | 11 | 12 | +9.1% | incidental (within band) |
| battle-field-2x500/legion/work.pass_scan_cells.p99 | 702 | 930 | +32.5% | incidental (within band) |
| battle-field-2x500/legion/work.pass_scan_cells.total | 788624 | 859017 | +8.9% | incidental (within band) |
| battle-field-2x500/legion/work.pass_scans.p99 | 18 | 20 | +11.1% | incidental (within band) |
| battle-field-2x500/legion/work.pass_scans.total | 19275 | 20868 | +8.3% | incidental (within band) |
| battle-field-2x500/legion/work.rechoice_bfs_cells.max | 2042 | 2239 | +9.6% | incidental (within band) |
| battle-field-2x500/legion/work.rechoice_bfs_cells.p99 | 276 | 359 | +30.1% | incidental (within band) |
| battle-field-2x500/legion/work.rechoice_bfs_cells.total | 41559 | 52584 | +26.5% | incidental (within band) |
| battle-field-2x500/legion/work.sched_group_visits.max | 7 | 8 | +14.3% | incidental (within band) |
| battle-field-2x500/legion/work.sched_group_visits.total | 3214 | 2510 | -21.9% | incidental (within band) |
| battle-field-2x500/legion/work.share_scan_iters.max | 10 | 3 | -70.0% | incidental (within band) |
| battle-field-2x500/legion/work.share_scan_iters.p99 | 5 | 0 | -100.0% | incidental (within band) |
| battle-field-2x500/legion/work.share_scan_iters.total | 1866 | 54 | -97.1% | incidental (within band) |
| battle-field-2x500/legion/work.slides.p99 | 14 | 13 | -7.1% | incidental (within band) |
| battle-field-2x500/legion/work.still_units_processed.p99 | 4 | 5 | +25.0% | incidental (within band) |
| battle-field-2x500/legion/work.still_units_processed.total | 3658 | 5156 | +41.0% | incidental (within band) |
| battle-field-2x500/legion/work.trace_cells.max | 54590 | 63352 | +16.1% | incidental (within band) |
| battle-field-2x500/legion/work.trace_cells.p99 | 42673 | 47421 | +11.1% | incidental (within band) |
| battle-field-2x500/legion/work.trace_cells.total | 45031998 | 50975145 | +13.2% | incidental (within band) |
| battle-field-2x60/legion/aim_reversals | 207 | 247 | +19.3% | incidental (within band) |
| battle-field-2x60/legion/back | 50 | 90 | +80.0% | incidental (within band) |
| battle-field-2x60/legion/backward | 3513 | 3953 | +12.5% | incidental (within band) |
| battle-field-2x60/legion/churn.bin0 | 22653923 | 19663646 | -13.2% | incidental (within band) |
| battle-field-2x60/legion/churn.bin1 | 320760 | 182795 | -43.0% | incidental (within band) |
| battle-field-2x60/legion/crawl_samples | 1285 | 1399 | +8.9% | incidental (within band) |
| battle-field-2x60/legion/detour_permille | 23 | 25 | +8.7% | incidental (within band) |
| battle-field-2x60/legion/engaged.member_ticks | 10145 | 9557 | -5.8% | intended |
| battle-field-2x60/legion/engagement_on_flowing | 98 | 103 | +5.1% | incidental (within band) |
| battle-field-2x60/legion/flip_rate_per30_permille | 193 | 167 | -13.5% | incidental (within band) |
| battle-field-2x60/legion/flips | 986 | 871 | -11.7% | incidental (within band) |
| battle-field-2x60/legion/follow_chain_max | 6 | 7 | +16.7% | incidental (within band) |
| battle-field-2x60/legion/follow_chain_mean_x100 | 320 | 342 | +6.9% | incidental (within band) |
| battle-field-2x60/legion/g.A.arrived | 7 | 11 | +57.1% | incidental (within band) |
| battle-field-2x60/legion/g.A.complete_dist_max | 10 | 12 | +20.0% | incidental (within band) |
| battle-field-2x60/legion/g.A.complete_dist_median | 2 | 4 | +100.0% | incidental (within band) |
| battle-field-2x60/legion/g.A.complete_n | 7 | 11 | +57.1% | incidental (within band) |
| battle-field-2x60/legion/g.A.left_behind | 16 | 15 | -6.2% | incidental (within band) |
| battle-field-2x60/legion/gauge.engaged_max | 18 | 20 | +11.1% | intended |
| battle-field-2x60/legion/gauge.still_per_residue_max | 2 | 3 | +50.0% | incidental (within band) |
| battle-field-2x60/legion/legion_groups_peak | 33 | 30 | -9.1% | incidental (within band) |
| battle-field-2x60/legion/parked_held | 3 | 10 | +233.3% | incidental (within band) |
| battle-field-2x60/legion/reversals | 40 | 61 | +52.5% | incidental (within band) |
| battle-field-2x60/legion/sideways | 8360 | 9033 | +8.1% | incidental (within band) |
| battle-field-2x60/legion/spins | 914 | 1066 | +16.6% | incidental (within band) |
| battle-field-2x60/legion/statue_ticks | 12028 | 13003 | +8.1% | incidental (within band) |
| battle-field-2x60/legion/stopped_permille | 149 | 133 | -10.7% | incidental (within band) |
| battle-field-2x60/legion/waiting_held | 9176 | 11064 | +20.6% | incidental (within band) |
| battle-field-2x60/legion/waiting_no_progress | 7440 | 2444 | -67.2% | incidental (within band) |
| battle-field-2x60/legion/work.arrivals.max | 1 | 2 | +100.0% | incidental (within band) |
| battle-field-2x60/legion/work.arrivals.total | 7 | 11 | +57.1% | incidental (within band) |
| battle-field-2x60/legion/work.completion_dist_2.total | 2 | 3 | +50.0% | incidental (within band) |
| battle-field-2x60/legion/work.completion_dist_3.total | 2 | 3 | +50.0% | incidental (within band) |
| battle-field-2x60/legion/work.completion_dist_4.total | 1 | 3 | +200.0% | incidental (within band) |
| battle-field-2x60/legion/work.completion_dist_sum.total | 28 | 59 | +110.7% | incidental (within band) |
| battle-field-2x60/legion/work.contact_arrivals.max | 1 | 2 | +100.0% | incidental (within band) |
| battle-field-2x60/legion/work.contact_arrivals.total | 3 | 6 | +100.0% | incidental (within band) |
| battle-field-2x60/legion/work.crowd_window_ring_cells.max | 328 | 304 | -7.3% | incidental (within band) |
| battle-field-2x60/legion/work.crowd_window_ring_cells.total | 6496 | 5528 | -14.9% | incidental (within band) |
| battle-field-2x60/legion/work.detour_cells.max | 1314 | 1465 | +11.5% | incidental (within band) |
| battle-field-2x60/legion/work.detour_cells.total | 49261 | 84628 | +71.8% | incidental (within band) |
| battle-field-2x60/legion/work.detours.total | 103 | 182 | +76.7% | incidental (within band) |
| battle-field-2x60/legion/work.field_work.total | 14548672 | 11138000 | -23.4% | incidental (within band) |
| battle-field-2x60/legion/work.field_work_first_slot.total | 1886600 | 1387328 | -26.5% | incidental (within band) |
| battle-field-2x60/legion/work.field_work_first_solo.max | 124504 | 176720 | +41.9% | incidental (within band) |
| battle-field-2x60/legion/work.field_work_first_solo.p99 | 73040 | 69264 | -5.2% | incidental (within band) |
| battle-field-2x60/legion/work.field_work_first_solo.total | 12553680 | 8877720 | -29.3% | incidental (within band) |
| battle-field-2x60/legion/work.fields_built.max | 3 | 5 | +66.7% | incidental (within band) |
| battle-field-2x60/legion/work.fields_built.total | 374 | 281 | -24.9% | incidental (within band) |
| battle-field-2x60/legion/work.fields_shared.max | 1 | 0 | -100.0% | incidental (within band) |
| battle-field-2x60/legion/work.fields_shared.total | 19 | 0 | -100.0% | incidental (within band) |
| battle-field-2x60/legion/work.fields_started_by_kind_2.total | 13 | 6 | -53.8% | incidental (within band) |
| battle-field-2x60/legion/work.fields_started_by_kind_4.max | 3 | 5 | +66.7% | incidental (within band) |
| battle-field-2x60/legion/work.fields_started_by_kind_4.total | 357 | 269 | -24.6% | incidental (within band) |
| battle-field-2x60/legion/work.formation_ring_cells.max | 131342 | 122317 | -6.9% | incidental (within band) |
| battle-field-2x60/legion/work.group_loop_iters.p99 | 83 | 76 | -8.4% | incidental (within band) |
| battle-field-2x60/legion/work.group_loop_iters.total | 17152 | 13944 | -18.7% | incidental (within band) |
| battle-field-2x60/legion/work.groups.total | 396 | 261 | -34.1% | incidental (within band) |
| battle-field-2x60/legion/work.held_rechecks.max | 6 | 3 | -50.0% | incidental (within band) |
| battle-field-2x60/legion/work.held_rechecks.p99 | 4 | 1 | -75.0% | incidental (within band) |
| battle-field-2x60/legion/work.held_rechecks.total | 432 | 213 | -50.7% | incidental (within band) |
| battle-field-2x60/legion/work.join_iterations.p99 | 81 | 74 | -8.6% | incidental (within band) |
| battle-field-2x60/legion/work.join_iterations.total | 15945 | 12951 | -18.8% | incidental (within band) |
| battle-field-2x60/legion/work.legion_total.total | 23244252 | 19846520 | -14.6% | incidental (within band) |
| battle-field-2x60/legion/work.line_sweeps.p99 | 25 | 38 | +52.0% | incidental (within band) |
| battle-field-2x60/legion/work.line_sweeps.total | 19161 | 21376 | +11.6% | incidental (within band) |
| battle-field-2x60/legion/work.mission_arrivals_2.max | 1 | 2 | +100.0% | incidental (within band) |
| battle-field-2x60/legion/work.mission_arrivals_2.total | 7 | 11 | +57.1% | incidental (within band) |
| battle-field-2x60/legion/work.mission_legs_4.max | 6 | 7 | +16.7% | incidental (within band) |
| battle-field-2x60/legion/work.mission_legs_4.p99 | 3 | 4 | +33.3% | incidental (within band) |
| battle-field-2x60/legion/work.move_calls_by_state_2.max | 34 | 28 | -17.6% | incidental (within band) |
| battle-field-2x60/legion/work.move_calls_by_state_2.p99 | 29 | 25 | -13.8% | incidental (within band) |
| battle-field-2x60/legion/work.move_calls_by_state_2.total | 13438 | 10669 | -20.6% | incidental (within band) |
| battle-field-2x60/legion/work.pass_scan_cells.total | 70536 | 64407 | -8.7% | incidental (within band) |
| battle-field-2x60/legion/work.pass_scans.p99 | 6 | 7 | +16.7% | incidental (within band) |
| battle-field-2x60/legion/work.pass_scans.total | 1763 | 1540 | -12.6% | incidental (within band) |
| battle-field-2x60/legion/work.rechoice_bfs_cells.max | 701 | 1310 | +86.9% | incidental (within band) |
| battle-field-2x60/legion/work.rechoice_bfs_cells.total | 3269 | 5728 | +75.2% | incidental (within band) |
| battle-field-2x60/legion/work.sched_group_visits.max | 4 | 5 | +25.0% | incidental (within band) |
| battle-field-2x60/legion/work.sched_group_visits.total | 401 | 295 | -26.4% | incidental (within band) |
| battle-field-2x60/legion/work.share_scan_iters.max | 6 | 2 | -66.7% | incidental (within band) |
| battle-field-2x60/legion/work.share_scan_iters.p99 | 2 | 0 | -100.0% | incidental (within band) |
| battle-field-2x60/legion/work.share_scan_iters.total | 201 | 5 | -97.5% | incidental (within band) |
| battle-field-2x60/legion/work.slides.max | 6 | 5 | -16.7% | incidental (within band) |
| battle-field-2x60/legion/work.slides.total | 910 | 839 | -7.8% | incidental (within band) |
| battle-field-2x60/legion/work.slot_search_cells.max | 131342 | 122317 | -6.9% | incidental (within band) |
| battle-field-2x60/legion/work.still_units_processed.max | 2 | 3 | +50.0% | incidental (within band) |
| battle-field-2x60/legion/work.still_units_processed.p99 | 1 | 2 | +100.0% | incidental (within band) |
| battle-field-2x60/legion/work.still_units_processed.total | 182 | 397 | +118.1% | incidental (within band) |
| chase-1/legion/churn.bin0 | 3523907 | 3031469 | -14.0% | incidental (within band) |
| chase-1/legion/work.field_work.p99 | 55440 | 96000 | +73.2% | incidental (within band) |
| chase-1/legion/work.field_work.total | 3506720 | 3015800 | -14.0% | incidental (within band) |
| chase-1/legion/work.field_work_first_solo.p99 | 55440 | 0 | -100.0% | incidental (within band) |
| chase-1/legion/work.field_work_first_solo.total | 3506720 | 751760 | -78.6% | incidental (within band) |
| chase-1/legion/work.fields_built.total | 53 | 21 | -60.4% | intended |
| chase-1/legion/work.fields_started_by_kind_4.total | 51 | 19 | -62.7% | intended |
| chase-1/legion/work.group_loop_iters.max | 3 | 2 | -33.3% | incidental (within band) |
| chase-1/legion/work.group_loop_iters.total | 165 | 111 | -32.7% | incidental (within band) |
| chase-1/legion/work.groups.p99 | 1 | 0 | -100.0% | incidental (within band) |
| chase-1/legion/work.groups.total | 53 | 3 | -94.3% | incidental (within band) |
| chase-1/legion/work.join_iterations.p99 | 1 | 0 | -100.0% | intended |
| chase-1/legion/work.join_iterations.total | 51 | 1 | -98.0% | intended |
| chase-1/legion/work.legion_total.p99 | 55453 | 96013 | +73.1% | incidental (within band) |
| chase-1/legion/work.legion_total.total | 3523907 | 3031469 | -14.0% | incidental (within band) |
| chase-1/legion/work.move_calls_by_state_3.p99 | 1 | 0 | -100.0% | incidental (within band) |
| chase-1/legion/work.move_calls_by_state_3.total | 52 | 2 | -96.2% | incidental (within band) |
| chase-1/legion/work.registrations.p99 | 1 | 0 | -100.0% | intended |
| chase-1/legion/work.registrations.total | 53 | 3 | -94.3% | intended |
| chase-1/legion/work.sched_group_visits.p99 | 1 | 2 | +100.0% | incidental (within band) |
| chase-1/legion/work.sched_group_visits.total | 54 | 50 | -7.4% | incidental (within band) |
| chase-1/legion/work.trace_cells.total | 17016 | 15554 | -8.6% | incidental (within band) |
| chase-50/legion/churn.bin0 | 8675217 | 9272372 | +6.9% | incidental (within band) |
| chase-50/legion/contact_own_permille | 929 | 802 | -13.7% | incidental (within band) |
| chase-50/legion/crawl_samples | 5 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/detour_permille | 1 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/engagement_on_flowing | 2 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/flip_rate_per30_permille | 1 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/flips | 2 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/follow_chain_max | 17 | 11 | -35.3% | incidental (within band) |
| chase-50/legion/follow_chain_mean_x100 | 1160 | 1007 | -13.2% | incidental (within band) |
| chase-50/legion/reach.chase.hold_out_max | 14 | 6 | -57.1% | incidental (within band) |
| chase-50/legion/reversals | 0 | 9 | new | incidental (within band) |
| chase-50/legion/sideways | 11 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/statue_ticks | 92 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/stop_go | 1799 | 30 | -98.3% | incidental (within band) |
| chase-50/legion/stopped_permille | 54 | 1 | -98.1% | incidental (within band) |
| chase-50/legion/waiting_held | 9 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/work.field_work.p99 | 162800 | 96000 | -41.0% | incidental (within band) |
| chase-50/legion/work.field_work.total | 8389040 | 9055200 | +7.9% | incidental (within band) |
| chase-50/legion/work.field_work_first_solo.p99 | 162800 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/work.field_work_first_solo.total | 8389040 | 820160 | -90.2% | incidental (within band) |
| chase-50/legion/work.fields_built.total | 53 | 48 | -9.4% | intended |
| chase-50/legion/work.fields_started_by_kind_4.total | 51 | 46 | -9.8% | intended |
| chase-50/legion/work.group_loop_iters.max | 150 | 100 | -33.3% | incidental (within band) |
| chase-50/legion/work.group_loop_iters.p99 | 148 | 2 | -98.6% | incidental (within band) |
| chase-50/legion/work.group_loop_iters.total | 7613 | 336 | -95.6% | incidental (within band) |
| chase-50/legion/work.groups.p99 | 1 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/work.groups.total | 53 | 3 | -94.3% | incidental (within band) |
| chase-50/legion/work.holds.max | 46 | 1 | -97.8% | incidental (within band) |
| chase-50/legion/work.holds.p99 | 39 | 1 | -97.4% | incidental (within band) |
| chase-50/legion/work.holds.total | 1893 | 30 | -98.4% | incidental (within band) |
| chase-50/legion/work.join_iterations.max | 148 | 99 | -33.1% | intended |
| chase-50/legion/work.join_iterations.p99 | 148 | 0 | -100.0% | intended |
| chase-50/legion/work.join_iterations.total | 7499 | 99 | -98.7% | intended |
| chase-50/legion/work.legion_total.p99 | 162863 | 96522 | -40.7% | incidental (within band) |
| chase-50/legion/work.legion_total.total | 8675217 | 9272372 | +6.9% | incidental (within band) |
| chase-50/legion/work.line_sweeps.p99 | 53 | 39 | -26.4% | incidental (within band) |
| chase-50/legion/work.line_sweeps.total | 9749 | 8743 | -10.3% | incidental (within band) |
| chase-50/legion/work.move_calls_by_state_1.total | 40998 | 45704 | +11.5% | incidental (within band) |
| chase-50/legion/work.move_calls_by_state_2.max | 46 | 2 | -95.7% | incidental (within band) |
| chase-50/legion/work.move_calls_by_state_2.p99 | 39 | 1 | -97.4% | incidental (within band) |
| chase-50/legion/work.move_calls_by_state_2.total | 2250 | 44 | -98.0% | incidental (within band) |
| chase-50/legion/work.move_calls_by_state_3.p99 | 50 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/work.move_calls_by_state_3.total | 2551 | 51 | -98.0% | incidental (within band) |
| chase-50/legion/work.pass_scan_cells.max | 72 | 24 | -66.7% | incidental (within band) |
| chase-50/legion/work.pass_scan_cells.p99 | 72 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/work.pass_scan_cells.total | 1248 | 24 | -98.1% | incidental (within band) |
| chase-50/legion/work.pass_scans.max | 3 | 1 | -66.7% | incidental (within band) |
| chase-50/legion/work.pass_scans.p99 | 3 | 0 | -100.0% | incidental (within band) |
| chase-50/legion/work.pass_scans.total | 52 | 1 | -98.1% | incidental (within band) |
| chase-50/legion/work.pass_scans_skipped.total | 24958 | 34154 | +36.8% | incidental (within band) |
| chase-50/legion/work.registrations.p99 | 50 | 0 | -100.0% | intended |
| chase-50/legion/work.registrations.total | 2552 | 52 | -98.0% | intended |
| chase-50/legion/work.sched_group_visits.p99 | 1 | 2 | +100.0% | incidental (within band) |
| chase-50/legion/work.sched_group_visits.total | 54 | 177 | +227.8% | incidental (within band) |
| chase-50/legion/work.slides.max | 43 | 8 | -81.4% | incidental (within band) |
| chase-50/legion/work.slides.p99 | 38 | 6 | -84.2% | incidental (within band) |
| chase-50/legion/work.slides.total | 4148 | 1496 | -63.9% | incidental (within band) |
| chase-50/legion/work.trace_cells.max | 2110 | 1904 | -9.8% | incidental (within band) |
| chase-50/legion/work.trace_cells.p99 | 2072 | 1272 | -38.6% | incidental (within band) |
| chase-50/legion/work.trace_cells.total | 285951 | 209842 | -26.6% | incidental (within band) |
| chase-line/legion/churn.bin0 | 3538548 | 3037554 | -14.2% | incidental (within band) |
| chase-line/legion/work.field_work.p99 | 55968 | 96000 | +71.5% | incidental (within band) |
| chase-line/legion/work.field_work.total | 3515224 | 3015800 | -14.2% | incidental (within band) |
| chase-line/legion/work.field_work_first_solo.p99 | 55968 | 0 | -100.0% | incidental (within band) |
| chase-line/legion/work.field_work_first_solo.total | 3515224 | 751760 | -78.6% | incidental (within band) |
| chase-line/legion/work.fields_built.total | 53 | 21 | -60.4% | intended |
| chase-line/legion/work.fields_started_by_kind_4.total | 51 | 19 | -62.7% | intended |
| chase-line/legion/work.group_loop_iters.max | 3 | 2 | -33.3% | incidental (within band) |
| chase-line/legion/work.group_loop_iters.total | 172 | 111 | -35.5% | incidental (within band) |
| chase-line/legion/work.groups.p99 | 1 | 0 | -100.0% | incidental (within band) |
| chase-line/legion/work.groups.total | 53 | 3 | -94.3% | incidental (within band) |
| chase-line/legion/work.join_iterations.p99 | 1 | 0 | -100.0% | intended |
| chase-line/legion/work.join_iterations.total | 51 | 1 | -98.0% | intended |
| chase-line/legion/work.legion_total.p99 | 56065 | 96013 | +71.3% | incidental (within band) |
| chase-line/legion/work.legion_total.total | 3538548 | 3037554 | -14.2% | incidental (within band) |
| chase-line/legion/work.move_calls_by_state_3.p99 | 1 | 0 | -100.0% | incidental (within band) |
| chase-line/legion/work.move_calls_by_state_3.total | 52 | 2 | -96.2% | incidental (within band) |
| chase-line/legion/work.registrations.p99 | 1 | 0 | -100.0% | intended |
| chase-line/legion/work.registrations.total | 53 | 3 | -94.3% | intended |
| chase-line/legion/work.sched_group_visits.p99 | 1 | 2 | +100.0% | incidental (within band) |
| chase-line/legion/work.sched_group_visits.total | 61 | 50 | -18.0% | incidental (within band) |
| chase-line/legion/work.trace_cells.total | 22797 | 21189 | -7.1% | incidental (within band) |
| deadend-w2-n120-closed-squad/legion/work.group_loop_iters.total | 487 | 530 | +8.8% | incidental (within band) |
| deadend-w2-n120-room-squad/legion/work.group_loop_iters.total | 487 | 530 | +8.8% | incidental (within band) |
| deadend-w4-n120-closed-squad/legion/work.group_loop_iters.total | 494 | 537 | +8.7% | incidental (within band) |
| deadend-w4-n120-room-squad/legion/work.group_loop_iters.total | 496 | 539 | +8.7% | incidental (within band) |
| deadend-w6-n120-closed-squad/legion/work.group_loop_iters.total | 494 | 537 | +8.7% | incidental (within band) |
| deadend-w6-n120-room-squad/legion/work.group_loop_iters.total | 348 | 391 | +12.4% | incidental (within band) |
| fight-retarget/legion/aim_reversals | 53 | 64 | +20.8% | incidental (within band) |
| fight-retarget/legion/back | 30 | 35 | +16.7% | incidental (within band) |
| fight-retarget/legion/backward | 14 | 11 | -21.4% | incidental (within band) |
| fight-retarget/legion/churn.bin0 | 2620944 | 2214212 | -15.5% | incidental (within band) |
| fight-retarget/legion/churn.bin2 | 800 | 2238 | +179.8% | incidental (within band) |
| fight-retarget/legion/crawl_samples | 481 | 582 | +21.0% | incidental (within band) |
| fight-retarget/legion/detour_permille | 18 | 24 | +33.3% | incidental (within band) |
| fight-retarget/legion/flip_rate_per30_permille | 44 | 50 | +13.6% | incidental (within band) |
| fight-retarget/legion/flips | 126 | 145 | +15.1% | incidental (within band) |
| fight-retarget/legion/follow_chain_max | 9 | 8 | -11.1% | incidental (within band) |
| fight-retarget/legion/follow_chain_mean_x100 | 435 | 402 | -7.6% | incidental (within band) |
| fight-retarget/legion/g.F.complete_dist_max | 15 | 13 | -13.3% | intended |
| fight-retarget/legion/g.F.complete_outside_radius | 3 | 2 | -33.3% | intended |
| fight-retarget/legion/g.F.left_behind | 3 | 2 | -33.3% | intended |
| fight-retarget/legion/gauge.engaged_max | 5 | 4 | -20.0% | incidental (within band) |
| fight-retarget/legion/legion_groups_peak | 16 | 4 | -75.0% | incidental (within band) |
| fight-retarget/legion/parked_held | 15 | 16 | +6.7% | incidental (within band) |
| fight-retarget/legion/reach.E.ever_end | 5 | 6 | +20.0% | intended |
| fight-retarget/legion/reach.E.ever_t1200 | 5 | 6 | +20.0% | intended |
| fight-retarget/legion/reversals | 9 | 7 | -22.2% | incidental (within band) |
| fight-retarget/legion/sideways | 282 | 403 | +42.9% | incidental (within band) |
| fight-retarget/legion/statue_ticks | 5109 | 5831 | +14.1% | incidental (within band) |
| fight-retarget/legion/stop_go | 208 | 225 | +8.2% | incidental (within band) |
| fight-retarget/legion/stopped_permille | 47 | 54 | +14.9% | incidental (within band) |
| fight-retarget/legion/waiting_held | 2530 | 3142 | +24.2% | incidental (within band) |
| fight-retarget/legion/waiting_no_progress | 426 | 16 | -96.2% | incidental (within band) |
| fight-retarget/legion/work.completion_dist_2.total | 4 | 6 | +50.0% | incidental (within band) |
| fight-retarget/legion/work.completion_dist_3.total | 16 | 17 | +6.2% | incidental (within band) |
| fight-retarget/legion/work.completion_dist_4.total | 17 | 14 | -17.6% | incidental (within band) |
| fight-retarget/legion/work.completion_dist_sum.max | 26 | 24 | -7.7% | incidental (within band) |
| fight-retarget/legion/work.completion_dist_sum.total | 290 | 254 | -12.4% | incidental (within band) |
| fight-retarget/legion/work.crowd_window_ring_cells.max | 320 | 384 | +20.0% | incidental (within band) |
| fight-retarget/legion/work.crowd_window_ring_cells.p99 | 32 | 64 | +100.0% | incidental (within band) |
| fight-retarget/legion/work.crowd_window_ring_cells.total | 4736 | 5696 | +20.3% | incidental (within band) |
| fight-retarget/legion/work.detour_cells.p99 | 0 | 453 | new | incidental (within band) |
| fight-retarget/legion/work.detour_cells.total | 15831 | 21517 | +35.9% | incidental (within band) |
| fight-retarget/legion/work.detours.p99 | 0 | 1 | new | incidental (within band) |
| fight-retarget/legion/work.detours.total | 29 | 39 | +34.5% | incidental (within band) |
| fight-retarget/legion/work.field_work.total | 984736 | 573840 | -41.7% | incidental (within band) |
| fight-retarget/legion/work.field_work_first_solo.total | 532416 | 121520 | -77.2% | incidental (within band) |
| fight-retarget/legion/work.fields_built.total | 15 | 5 | -66.7% | incidental (within band) |
| fight-retarget/legion/work.fields_started_by_kind_4.total | 13 | 3 | -76.9% | incidental (within band) |
| fight-retarget/legion/work.formation_ring_cells.max | 73322 | 88540 | +20.8% | incidental (within band) |
| fight-retarget/legion/work.formation_ring_cells.total | 220249 | 234571 | +6.5% | incidental (within band) |
| fight-retarget/legion/work.group_loop_iters.total | 608 | 372 | -38.8% | incidental (within band) |
| fight-retarget/legion/work.groups.total | 20 | 5 | -75.0% | incidental (within band) |
| fight-retarget/legion/work.held_rechecks.max | 5 | 3 | -40.0% | incidental (within band) |
| fight-retarget/legion/work.held_rechecks.p99 | 3 | 1 | -66.7% | incidental (within band) |
| fight-retarget/legion/work.held_rechecks.total | 315 | 98 | -68.9% | incidental (within band) |
| fight-retarget/legion/work.holds.max | 3 | 4 | +33.3% | incidental (within band) |
| fight-retarget/legion/work.holds.total | 222 | 239 | +7.7% | incidental (within band) |
| fight-retarget/legion/work.join_iterations.total | 416 | 232 | -44.2% | incidental (within band) |
| fight-retarget/legion/work.legion_total.p99 | 3957 | 3654 | -7.7% | incidental (within band) |
| fight-retarget/legion/work.legion_total.total | 2755771 | 2350364 | -14.7% | incidental (within band) |
| fight-retarget/legion/work.move_calls_by_state_2.max | 13 | 17 | +30.8% | incidental (within band) |
| fight-retarget/legion/work.move_calls_by_state_2.p99 | 9 | 12 | +33.3% | incidental (within band) |
| fight-retarget/legion/work.move_calls_by_state_2.total | 3579 | 4201 | +17.4% | incidental (within band) |
| fight-retarget/legion/work.outside_area_completions.max | 2 | 1 | -50.0% | incidental (within band) |
| fight-retarget/legion/work.outside_area_completions.total | 3 | 2 | -33.3% | incidental (within band) |
| fight-retarget/legion/work.pass_scan_cells.total | 456 | 408 | -10.5% | incidental (within band) |
| fight-retarget/legion/work.pass_scans.total | 19 | 17 | -10.5% | incidental (within band) |
| fight-retarget/legion/work.rechoice_bfs_cells.max | 2090 | 1884 | -9.9% | incidental (within band) |
| fight-retarget/legion/work.rechoice_bfs_cells.total | 13337 | 12302 | -7.8% | incidental (within band) |
| fight-retarget/legion/work.sched_group_visits.max | 2 | 1 | -50.0% | incidental (within band) |
| fight-retarget/legion/work.sched_group_visits.p99 | 1 | 0 | -100.0% | incidental (within band) |
| fight-retarget/legion/work.sched_group_visits.total | 38 | 9 | -76.3% | incidental (within band) |
| fight-retarget/legion/work.slides.total | 765 | 843 | +10.2% | incidental (within band) |
| fight-retarget/legion/work.slot_search_cells.max | 73322 | 88540 | +20.8% | incidental (within band) |
| goalblock-200/legion/aim_reversals | 256 | 238 | -7.0% | incidental (within band) |
| goalblock-200/legion/churn.bin0 | 3058030 | 2857577 | -6.6% | incidental (within band) |
| goalblock-200/legion/crawl_samples | 4071 | 3662 | -10.0% | incidental (within band) |
| goalblock-200/legion/detour_permille | 25 | 23 | -8.0% | incidental (within band) |
| goalblock-200/legion/flip_rate_per30_permille | 120 | 105 | -12.5% | incidental (within band) |
| goalblock-200/legion/flips | 1745 | 1479 | -15.2% | incidental (within band) |
| goalblock-200/legion/follow_chain_max | 18 | 17 | -5.6% | incidental (within band) |
| goalblock-200/legion/legion_groups_peak | 2 | 1 | -50.0% | incidental (within band) |
| goalblock-200/legion/open_still900 | 35 | 30 | -14.3% | incidental (within band) |
| goalblock-200/legion/parked_held | 63113 | 58954 | -6.6% | incidental (within band) |
| goalblock-200/legion/reversals | 16 | 12 | -25.0% | incidental (within band) |
| goalblock-200/legion/sideways | 1156 | 1088 | -5.9% | incidental (within band) |
| goalblock-200/legion/statue_ticks | 37206 | 33748 | -9.3% | incidental (within band) |
| goalblock-200/legion/still900_ever | 35 | 30 | -14.3% | incidental (within band) |
| goalblock-200/legion/stop_go | 1655 | 1527 | -7.7% | incidental (within band) |
| goalblock-200/legion/waiting_held | 45737 | 40131 | -12.3% | incidental (within band) |
| goalblock-200/legion/work.completion_dist_5.total | 33 | 31 | -6.1% | incidental (within band) |
| goalblock-200/legion/work.detour_cells.total | 251965 | 229270 | -9.0% | incidental (within band) |
| goalblock-200/legion/work.detours.total | 520 | 474 | -8.8% | incidental (within band) |
| goalblock-200/legion/work.field_work.total | 466336 | 261368 | -44.0% | incidental (within band) |
| goalblock-200/legion/work.field_work_first_slot.total | 466328 | 261360 | -44.0% | incidental (within band) |
| goalblock-200/legion/work.fields_built.total | 3 | 2 | -33.3% | intended |
| goalblock-200/legion/work.fields_started_by_kind_1.total | 3 | 2 | -33.3% | incidental (within band) |
| goalblock-200/legion/work.formation_ring_cells.max | 4552 | 18818 | +313.4% | incidental (within band) |
| goalblock-200/legion/work.formation_ring_cells.total | 18778 | 37538 | +99.9% | incidental (within band) |
| goalblock-200/legion/work.group_loop_iters.p99 | 2 | 1 | -50.0% | incidental (within band) |
| goalblock-200/legion/work.group_loop_iters.total | 578 | 502 | -13.1% | incidental (within band) |
| goalblock-200/legion/work.groups.total | 2 | 1 | -50.0% | incidental (within band) |
| goalblock-200/legion/work.held_rechecks.max | 44 | 41 | -6.8% | incidental (within band) |
| goalblock-200/legion/work.held_rechecks.p99 | 43 | 39 | -9.3% | incidental (within band) |
| goalblock-200/legion/work.holds.total | 1713 | 1603 | -6.4% | incidental (within band) |
| goalblock-200/legion/work.line_sweeps.p99 | 49 | 46 | -6.1% | incidental (within band) |
| goalblock-200/legion/work.move_calls_by_state_2.max | 56 | 61 | +8.9% | incidental (within band) |
| goalblock-200/legion/work.sched_group_visits.total | 3 | 2 | -33.3% | incidental (within band) |
| goalblock-200/legion/work.slides.total | 133 | 122 | -8.3% | incidental (within band) |
| goalblock-200/legion/work.slot_search_cells.max | 4552 | 18818 | +313.4% | incidental (within band) |
| goalblock-200/legion/work.slot_search_cells.total | 19677 | 37538 | +90.8% | incidental (within band) |
| goalblock-fight-200/legion/aim_reversals | 262 | 237 | -9.5% | incidental (within band) |
| goalblock-fight-200/legion/back | 94 | 121 | +28.7% | incidental (within band) |
| goalblock-fight-200/legion/backward | 1 | 0 | -100.0% | incidental (within band) |
| goalblock-fight-200/legion/churn.bin0 | 3312006 | 3118650 | -5.8% | incidental (within band) |
| goalblock-fight-200/legion/churn.bin1 | 476788 | 507533 | +6.4% | incidental (within band) |
| goalblock-fight-200/legion/crawl_samples | 3659 | 3891 | +6.3% | incidental (within band) |
| goalblock-fight-200/legion/detour_permille | 23 | 25 | +8.7% | incidental (within band) |
| goalblock-fight-200/legion/follow_chain_max | 17 | 16 | -5.9% | incidental (within band) |
| goalblock-fight-200/legion/g.A.arrived | 130 | 123 | -5.4% | intended |
| goalblock-fight-200/legion/g.A.complete_n | 130 | 123 | -5.4% | intended |
| goalblock-fight-200/legion/g.A.left_behind | 70 | 77 | +10.0% | intended |
| goalblock-fight-200/legion/legion_groups_peak | 2 | 1 | -50.0% | incidental (within band) |
| goalblock-fight-200/legion/open_still900 | 31 | 24 | -22.6% | incidental (within band) |
| goalblock-fight-200/legion/parked_held | 63320 | 55392 | -12.5% | incidental (within band) |
| goalblock-fight-200/legion/reversals | 12 | 14 | +16.7% | incidental (within band) |
| goalblock-fight-200/legion/statue_ticks | 33238 | 35282 | +6.1% | incidental (within band) |
| goalblock-fight-200/legion/still900_ever | 31 | 24 | -22.6% | incidental (within band) |
| goalblock-fight-200/legion/work.arrivals.total | 130 | 123 | -5.4% | incidental (within band) |
| goalblock-fight-200/legion/work.completion_dist_2.total | 3 | 2 | -33.3% | incidental (within band) |
| goalblock-fight-200/legion/work.completion_dist_5.total | 35 | 33 | -5.7% | incidental (within band) |
| goalblock-fight-200/legion/work.detour_cells.max | 1524 | 1637 | +7.4% | incidental (within band) |
| goalblock-fight-200/legion/work.detour_cells.p99 | 886 | 948 | +7.0% | incidental (within band) |
| goalblock-fight-200/legion/work.field_work.total | 727696 | 522728 | -28.2% | incidental (within band) |
| goalblock-fight-200/legion/work.field_work_first_slot.total | 727688 | 522720 | -28.2% | incidental (within band) |
| goalblock-fight-200/legion/work.fields_built.total | 4 | 3 | -25.0% | intended |
| goalblock-fight-200/legion/work.fields_started_by_kind_2.total | 3 | 2 | -33.3% | incidental (within band) |
| goalblock-fight-200/legion/work.formation_ring_cells.max | 4576 | 18818 | +311.2% | incidental (within band) |
| goalblock-fight-200/legion/work.formation_ring_cells.total | 18426 | 37210 | +101.9% | incidental (within band) |
| goalblock-fight-200/legion/work.group_loop_iters.p99 | 2 | 1 | -50.0% | incidental (within band) |
| goalblock-fight-200/legion/work.group_loop_iters.total | 778 | 702 | -9.8% | incidental (within band) |
| goalblock-fight-200/legion/work.groups.total | 6 | 5 | -16.7% | incidental (within band) |
| goalblock-fight-200/legion/work.held_rechecks.total | 33968 | 32052 | -5.6% | incidental (within band) |
| goalblock-fight-200/legion/work.line_sweeps.total | 47264 | 44236 | -6.4% | incidental (within band) |
| goalblock-fight-200/legion/work.mission_arrivals_2.total | 130 | 123 | -5.4% | incidental (within band) |
| goalblock-fight-200/legion/work.move_calls_by_state_2.p99 | 55 | 51 | -7.3% | incidental (within band) |
| goalblock-fight-200/legion/work.sched_group_visits.total | 8 | 7 | -12.5% | incidental (within band) |
| goalblock-fight-200/legion/work.slides.total | 127 | 97 | -23.6% | incidental (within band) |
| goalblock-fight-200/legion/work.slot_search_cells.max | 4576 | 18818 | +311.2% | incidental (within band) |
| goalblock-fight-200/legion/work.slot_search_cells.total | 19505 | 37210 | +90.8% | incidental (within band) |
| goalblock-near-200/legion/backward | 77 | 69 | -10.4% | incidental (within band) |
| goalblock-near-200/legion/churn.bin1 | 174843 | 184988 | +5.8% | incidental (within band) |
| goalblock-near-200/legion/crawl_samples | 2900 | 3079 | +6.2% | incidental (within band) |
| goalblock-near-200/legion/detour_permille | 22 | 24 | +9.1% | incidental (within band) |
| goalblock-near-200/legion/g.A.complete_outside_radius | 6 | 8 | +33.3% | intended |
| goalblock-near-200/legion/g.A.left_behind | 7 | 9 | +28.6% | intended |
| goalblock-near-200/legion/parked_held | 590 | 440 | -25.4% | incidental (within band) |
| goalblock-near-200/legion/sideways | 1355 | 1495 | +10.3% | incidental (within band) |
| goalblock-near-200/legion/statue_ticks | 29818 | 31906 | +7.0% | incidental (within band) |
| goalblock-near-200/legion/work.arrivals.max | 10 | 8 | -20.0% | incidental (within band) |
| goalblock-near-200/legion/work.aware_pairs.total | 53 | 56 | +5.7% | incidental (within band) |
| goalblock-near-200/legion/work.completion_dist_2.total | 6 | 5 | -16.7% | incidental (within band) |
| goalblock-near-200/legion/work.completion_dist_sum.max | 105 | 132 | +25.7% | incidental (within band) |
| goalblock-near-200/legion/work.contact_arrivals.max | 10 | 8 | -20.0% | incidental (within band) |
| goalblock-near-200/legion/work.crowd_window_ring_cells.p99 | 384 | 416 | +8.3% | incidental (within band) |
| goalblock-near-200/legion/work.detour_cells.max | 1651 | 1425 | -13.7% | incidental (within band) |
| goalblock-near-200/legion/work.held_rechecks.max | 10 | 11 | +10.0% | incidental (within band) |
| goalblock-near-200/legion/work.held_rechecks.total | 2226 | 1900 | -14.6% | incidental (within band) |
| goalblock-near-200/legion/work.holds.max | 5 | 6 | +20.0% | incidental (within band) |
| goalblock-near-200/legion/work.mission_arrivals_1.max | 10 | 8 | -20.0% | incidental (within band) |
| goalblock-near-200/legion/work.outside_area_completions.max | 4 | 5 | +25.0% | incidental (within band) |
| goalblock-near-200/legion/work.outside_area_completions.p99 | 1 | 0 | -100.0% | incidental (within band) |
| goalblock-near-200/legion/work.outside_area_completions.total | 51 | 46 | -9.8% | incidental (within band) |
| goalblock-near-200/legion/work.rechoice_bfs_cells.max | 1723 | 1997 | +15.9% | incidental (within band) |
| lateblock-before-16/legion/work.group_loop_iters.total | 196 | 255 | +30.1% | incidental (within band) |
| lateblock-late-16/legion/aim_reversals | 538 | 87 | -83.8% | incidental (within band) |
| lateblock-late-16/legion/back | 395 | 19 | -95.2% | incidental (within band) |
| lateblock-late-16/legion/backward | 62 | 93 | +50.0% | incidental (within band) |
| lateblock-late-16/legion/churn.bin0 | 1572496 | 2070657 | +31.7% | incidental (within band) |
| lateblock-late-16/legion/churn.bin2 | 338693 | 18101 | -94.7% | incidental (within band) |
| lateblock-late-16/legion/churn.bin3 | 242287 | 14800 | -93.9% | incidental (within band) |
| lateblock-late-16/legion/churn.bin4 | 165673 | 14800 | -91.1% | incidental (within band) |
| lateblock-late-16/legion/churn.bin5 | 128341 | 14800 | -88.5% | incidental (within band) |
| lateblock-late-16/legion/contact_own_permille | 776 | 658 | -15.2% | incidental (within band) |
| lateblock-late-16/legion/contact_settled_permille | 23643 | 1953 | -91.7% | incidental (within band) |
| lateblock-late-16/legion/crawl_samples | 7697 | 797 | -89.6% | incidental (within band) |
| lateblock-late-16/legion/detour_permille | 213 | 46 | -78.4% | incidental (within band) |
| lateblock-late-16/legion/engagement_on_flowing | 45 | 42 | -6.7% | incidental (within band) |
| lateblock-late-16/legion/flip_rate_per30_permille | 183 | 108 | -41.0% | incidental (within band) |
| lateblock-late-16/legion/flips | 1552 | 347 | -77.6% | incidental (within band) |
| lateblock-late-16/legion/follow_chain_max | 8 | 13 | +62.5% | incidental (within band) |
| lateblock-late-16/legion/follow_chain_mean_x100 | 253 | 447 | +76.7% | incidental (within band) |
| lateblock-late-16/legion/g.A.arrived | 30 | 40 | +33.3% | intended |
| lateblock-late-16/legion/g.A.complete_dist_max | 12 | 10 | -16.7% | intended |
| lateblock-late-16/legion/g.A.complete_dist_median | 6 | 5 | -16.7% | intended |
| lateblock-late-16/legion/g.A.complete_n | 30 | 40 | +33.3% | intended |
| lateblock-late-16/legion/g.A.done | -1 | 3440 | +344100.0% | intended |
| lateblock-late-16/legion/g.A.left_behind | 10 | 0 | -100.0% | intended |
| lateblock-late-16/legion/g.A.t50 | 5691 | 2423 | -57.4% | intended |
| lateblock-late-16/legion/g.A.t90 | -1 | 2927 | +292800.0% | intended |
| lateblock-late-16/legion/parked_held | 30530 | 72 | -99.8% | incidental (within band) |
| lateblock-late-16/legion/region.goal.inside | 31 | 40 | +29.0% | incidental (within band) |
| lateblock-late-16/legion/reversals | 46 | 4 | -91.3% | incidental (within band) |
| lateblock-late-16/legion/sideways | 6940 | 2092 | -69.9% | incidental (within band) |
| lateblock-late-16/legion/spacing_samples | 24193 | 9647 | -60.1% | incidental (within band) |
| lateblock-late-16/legion/statue_ticks | 72722 | 8765 | -87.9% | incidental (within band) |
| lateblock-late-16/legion/still900_ever | 9 | 0 | -100.0% | incidental (within band) |
| lateblock-late-16/legion/stop_go | 954 | 427 | -55.2% | incidental (within band) |
| lateblock-late-16/legion/stopped_permille | 341 | 81 | -76.2% | incidental (within band) |
| lateblock-late-16/legion/waiting_held | 40571 | 4746 | -88.3% | incidental (within band) |
| lateblock-late-16/legion/wall_samples | 16225 | 9206 | -43.3% | incidental (within band) |
| lateblock-late-16/legion/work.arrivals.max | 1 | 2 | +100.0% | incidental (within band) |
| lateblock-late-16/legion/work.arrivals.total | 30 | 40 | +33.3% | incidental (within band) |
| lateblock-late-16/legion/work.aware_pairs.total | 80 | 85 | +6.2% | incidental (within band) |
| lateblock-late-16/legion/work.completion_dist_0.max | 0 | 1 | new | incidental (within band) |
| lateblock-late-16/legion/work.completion_dist_0.total | 0 | 1 | new | incidental (within band) |
| lateblock-late-16/legion/work.completion_dist_1.max | 0 | 1 | new | incidental (within band) |
| lateblock-late-16/legion/work.completion_dist_1.total | 0 | 1 | new | incidental (within band) |
| lateblock-late-16/legion/work.completion_dist_2.total | 4 | 7 | +75.0% | incidental (within band) |
| lateblock-late-16/legion/work.completion_dist_3.total | 16 | 24 | +50.0% | incidental (within band) |
| lateblock-late-16/legion/work.completion_dist_4.total | 9 | 7 | -22.2% | incidental (within band) |
| lateblock-late-16/legion/work.completion_dist_sum.total | 185 | 208 | +12.4% | incidental (within band) |
| lateblock-late-16/legion/work.contact_arrivals.total | 10 | 9 | -10.0% | incidental (within band) |
| lateblock-late-16/legion/work.crowd_window_ring_cells.max | 1124 | 576 | -48.8% | incidental (within band) |
| lateblock-late-16/legion/work.crowd_window_ring_cells.p99 | 409 | 0 | -100.0% | incidental (within band) |
| lateblock-late-16/legion/work.crowd_window_ring_cells.total | 85399 | 9472 | -88.9% | incidental (within band) |
| lateblock-late-16/legion/work.detour_cells.max | 1239 | 912 | -26.4% | incidental (within band) |
| lateblock-late-16/legion/work.detour_cells.p99 | 473 | 0 | -100.0% | incidental (within band) |
| lateblock-late-16/legion/work.detour_cells.total | 254898 | 15391 | -94.0% | incidental (within band) |
| lateblock-late-16/legion/work.detours.max | 3 | 2 | -33.3% | incidental (within band) |
| lateblock-late-16/legion/work.detours.p99 | 1 | 0 | -100.0% | incidental (within band) |
| lateblock-late-16/legion/work.detours.total | 593 | 31 | -94.8% | incidental (within band) |
| lateblock-late-16/legion/work.field_work.total | 226160 | 452320 | +100.0% | incidental (within band) |
| lateblock-late-16/legion/work.fields_built.total | 1 | 2 | +100.0% | incidental (within band) |
| lateblock-late-16/legion/work.fields_started_by_kind_1.total | 1 | 2 | +100.0% | incidental (within band) |
| lateblock-late-16/legion/work.group_loop_iters.total | 380 | 261 | -31.3% | incidental (within band) |
| lateblock-late-16/legion/work.held_rechecks.max | 17 | 3 | -82.4% | incidental (within band) |
| lateblock-late-16/legion/work.held_rechecks.p99 | 15 | 1 | -93.3% | incidental (within band) |
| lateblock-late-16/legion/work.held_rechecks.total | 36163 | 226 | -99.4% | incidental (within band) |
| lateblock-late-16/legion/work.holds.max | 3 | 4 | +33.3% | incidental (within band) |
| lateblock-late-16/legion/work.holds.total | 956 | 430 | -55.0% | incidental (within band) |
| lateblock-late-16/legion/work.legion_total.total | 2862106 | 2562100 | -10.5% | incidental (within band) |
| lateblock-late-16/legion/work.line_sweeps.total | 130864 | 140256 | +7.2% | incidental (within band) |
| lateblock-late-16/legion/work.mission_arrivals_1.max | 1 | 2 | +100.0% | incidental (within band) |
| lateblock-late-16/legion/work.mission_arrivals_1.total | 30 | 40 | +33.3% | incidental (within band) |
| lateblock-late-16/legion/work.move_calls_by_state_1.total | 153838 | 89349 | -41.9% | incidental (within band) |
| lateblock-late-16/legion/work.move_calls_by_state_2.max | 29 | 17 | -41.4% | incidental (within band) |
| lateblock-late-16/legion/work.move_calls_by_state_2.p99 | 24 | 12 | -50.0% | incidental (within band) |
| lateblock-late-16/legion/work.move_calls_by_state_2.total | 82564 | 7665 | -90.7% | incidental (within band) |
| lateblock-late-16/legion/work.moves.total | 225889 | 96621 | -57.2% | incidental (within band) |
| lateblock-late-16/legion/work.pass_scan_cells.p99 | 24 | 168 | +600.0% | incidental (within band) |
| lateblock-late-16/legion/work.pass_scan_cells.total | 27960 | 68808 | +146.1% | incidental (within band) |
| lateblock-late-16/legion/work.pass_scans.p99 | 1 | 7 | +600.0% | incidental (within band) |
| lateblock-late-16/legion/work.pass_scans.total | 1165 | 2867 | +146.1% | incidental (within band) |
| lateblock-late-16/legion/work.pass_scans_skipped.total | 107445 | 75250 | -30.0% | incidental (within band) |
| lateblock-late-16/legion/work.rechoice_bfs_cells.max | 2466 | 1039 | -57.9% | incidental (within band) |
| lateblock-late-16/legion/work.rechoice_bfs_cells.total | 17911 | 3243 | -81.9% | incidental (within band) |
| lateblock-late-16/legion/work.sched_group_visits.max | 1 | 2 | +100.0% | incidental (within band) |
| lateblock-late-16/legion/work.sched_group_visits.total | 1 | 6 | +500.0% | incidental (within band) |
| lateblock-late-16/legion/work.slides.max | 3 | 4 | +33.3% | incidental (within band) |
| lateblock-late-16/legion/work.slides.total | 1499 | 638 | -57.4% | incidental (within band) |
| lateblock-late-16/legion/work.slot_search_cells.max | 2466 | 2025 | -17.9% | incidental (within band) |
| lateblock-late-16/legion/work.slot_search_cells.total | 21208 | 8565 | -59.6% | incidental (within band) |
| lateblock-late-16/legion/work.still_units_processed.total | 80718 | 85565 | +6.0% | incidental (within band) |
| lateblock-late-16/legion/work.trace_cells.total | 2135202 | 1924809 | -9.9% | incidental (within band) |
| liftstream/legion/work.group_loop_iters.total | 383 | 443 | +15.7% | incidental (within band) |
| ringcross/legion/aim_reversals | 6 | 333 | +5450.0% | incidental (within band) |
| ringcross/legion/back | 0 | 284 | new | incidental (within band) |
| ringcross/legion/backward | 0 | 79 | new | incidental (within band) |
| ringcross/legion/churn.bin0 | 537449 | 437106 | -18.7% | incidental (within band) |
| ringcross/legion/churn.bin2 | 239377 | 290395 | +21.3% | incidental (within band) |
| ringcross/legion/churn.bin3 | 36943 | 12747 | -65.5% | incidental (within band) |
| ringcross/legion/contact_other_permille | 27 | 68 | +151.9% | incidental (within band) |
| ringcross/legion/contact_settled_permille | 1488 | 0 | -100.0% | incidental (within band) |
| ringcross/legion/crawl_samples | 158 | 3602 | +2179.7% | incidental (within band) |
| ringcross/legion/detour_permille | 25 | 136 | +444.0% | incidental (within band) |
| ringcross/legion/engaged.member_ticks | 9370 | 158617 | +1592.8% | intended |
| ringcross/legion/engagement_on_flowing | 17 | 51 | +200.0% | incidental (within band) |
| ringcross/legion/flip_rate_per30_permille | 14 | 67 | +378.6% | incidental (within band) |
| ringcross/legion/flips | 150 | 739 | +392.7% | incidental (within band) |
| ringcross/legion/follow_chain_max | 14 | 9 | -35.7% | incidental (within band) |
| ringcross/legion/follow_chain_mean_x100 | 626 | 411 | -34.3% | incidental (within band) |
| ringcross/legion/g.M.complete_dist_max | 9 | 11 | +22.2% | incidental (within band) |
| ringcross/legion/g.M.done | 3796 | 4876 | +28.5% | incidental (within band) |
| ringcross/legion/g.M.t90 | 3616 | 4291 | +18.7% | incidental (within band) |
| ringcross/legion/gauge.engaged_max | 2 | 40 | +1900.0% | intended |
| ringcross/legion/parked_held | 206892 | 166745 | -19.4% | incidental (within band) |
| ringcross/legion/parked_no_progress | 9070 | 0 | -100.0% | incidental (within band) |
| ringcross/legion/reach.ring.damage_end | 313 | 5306 | +1595.2% | intended |
| ringcross/legion/reach.ring.damage_t1500 | 80 | 826 | +932.5% | intended |
| ringcross/legion/reach.ring.ever_end | 2 | 40 | +1900.0% | intended |
| ringcross/legion/reach.ring.ever_t1500 | 2 | 30 | +1400.0% | intended |
| ringcross/legion/reach.ring.farthest_held | 40 | 7 | -82.5% | intended |
| ringcross/legion/reach.ring.hold_out_max | 4711 | 1893 | -59.8% | intended |
| ringcross/legion/reach.ring.now_end | 2 | 40 | +1900.0% | intended |
| ringcross/legion/reach.ring.out600_end | 46 | 8 | -82.6% | intended |
| ringcross/legion/reach.ring.out600_peak | 46 | 8 | -82.6% | intended |
| ringcross/legion/reach.ring.t50 | -1 | 1028 | +102900.0% | intended |
| ringcross/legion/reversals | 0 | 79 | new | incidental (within band) |
| ringcross/legion/sideways | 387 | 1892 | +388.9% | incidental (within band) |
| ringcross/legion/spins | 2 | 159 | +7850.0% | incidental (within band) |
| ringcross/legion/statue_ticks | 866 | 34060 | +3833.0% | incidental (within band) |
| ringcross/legion/stop_go | 220 | 682 | +210.0% | incidental (within band) |
| ringcross/legion/stopped_permille | 703 | 582 | -17.2% | incidental (within band) |
| ringcross/legion/waiting_held | 7387 | 21052 | +185.0% | incidental (within band) |
| ringcross/legion/waiting_no_progress | 282 | 0 | -100.0% | incidental (within band) |
| ringcross/legion/wall_samples | 9709 | 14437 | +48.7% | incidental (within band) |
| ringcross/legion/work.aware_pairs.total | 134 | 164 | +22.4% | incidental (within band) |
| ringcross/legion/work.completion_dist_0.max | 1 | 0 | -100.0% | incidental (within band) |
| ringcross/legion/work.completion_dist_0.total | 1 | 0 | -100.0% | incidental (within band) |
| ringcross/legion/work.completion_dist_1.max | 0 | 1 | new | incidental (within band) |
| ringcross/legion/work.completion_dist_1.total | 0 | 1 | new | incidental (within band) |
| ringcross/legion/work.completion_dist_2.total | 8 | 6 | -25.0% | incidental (within band) |
| ringcross/legion/work.completion_dist_3.max | 2 | 1 | -50.0% | incidental (within band) |
| ringcross/legion/work.completion_dist_4.total | 5 | 6 | +20.0% | incidental (within band) |
| ringcross/legion/work.completion_dist_sum.total | 197 | 212 | +7.6% | incidental (within band) |
| ringcross/legion/work.contact_arrivals.total | 2 | 7 | +250.0% | incidental (within band) |
| ringcross/legion/work.crowd_window_ring_cells.max | 1104 | 544 | -50.7% | incidental (within band) |
| ringcross/legion/work.crowd_window_ring_cells.p99 | 1104 | 0 | -100.0% | incidental (within band) |
| ringcross/legion/work.crowd_window_ring_cells.total | 114544 | 6624 | -94.2% | incidental (within band) |
| ringcross/legion/work.detour_cells.max | 473 | 1360 | +187.5% | incidental (within band) |
| ringcross/legion/work.detour_cells.p99 | 0 | 583 | new | incidental (within band) |
| ringcross/legion/work.detour_cells.total | 569 | 150183 | +26294.2% | incidental (within band) |
| ringcross/legion/work.detours.max | 1 | 3 | +200.0% | incidental (within band) |
| ringcross/legion/work.detours.p99 | 0 | 1 | new | incidental (within band) |
| ringcross/legion/work.detours.total | 4 | 282 | +6950.0% | incidental (within band) |
| ringcross/legion/work.field_work_first_solo.max | 210168 | 220792 | +5.1% | incidental (within band) |
| ringcross/legion/work.field_work_first_solo.total | 210168 | 220792 | +5.1% | incidental (within band) |
| ringcross/legion/work.group_loop_iters.total | 452 | 488 | +8.0% | incidental (within band) |
| ringcross/legion/work.held_rechecks.max | 47 | 12 | -74.5% | incidental (within band) |
| ringcross/legion/work.held_rechecks.p99 | 46 | 9 | -80.4% | incidental (within band) |
| ringcross/legion/work.held_rechecks.total | 211033 | 17093 | -91.9% | incidental (within band) |
| ringcross/legion/work.holds.max | 6 | 4 | -33.3% | incidental (within band) |
| ringcross/legion/work.holds.total | 271 | 693 | +155.7% | incidental (within band) |
| ringcross/legion/work.legion_total.p99 | 3758 | 3457 | -8.0% | incidental (within band) |
| ringcross/legion/work.line_sweeps.p99 | 149 | 118 | -20.8% | incidental (within band) |
| ringcross/legion/work.line_sweeps.total | 70498 | 137789 | +95.5% | incidental (within band) |
| ringcross/legion/work.move_calls_by_state_1.max | 48 | 56 | +16.7% | incidental (within band) |
| ringcross/legion/work.move_calls_by_state_1.p99 | 48 | 54 | +12.5% | incidental (within band) |
| ringcross/legion/work.move_calls_by_state_1.total | 95328 | 138484 | +45.3% | incidental (within band) |
| ringcross/legion/work.move_calls_by_state_2.max | 50 | 24 | -52.0% | incidental (within band) |
| ringcross/legion/work.move_calls_by_state_2.p99 | 48 | 20 | -58.3% | incidental (within band) |
| ringcross/legion/work.move_calls_by_state_2.total | 215790 | 33463 | -84.5% | incidental (within band) |
| ringcross/legion/work.moves.max | 86 | 58 | -32.6% | incidental (within band) |
| ringcross/legion/work.moves.p99 | 69 | 57 | -17.4% | incidental (within band) |
| ringcross/legion/work.moves.total | 209340 | 166055 | -20.7% | incidental (within band) |
| ringcross/legion/work.pass_scan_cells.max | 0 | 240 | new | incidental (within band) |
| ringcross/legion/work.pass_scan_cells.p99 | 0 | 72 | new | incidental (within band) |
| ringcross/legion/work.pass_scan_cells.total | 0 | 37896 | new | incidental (within band) |
| ringcross/legion/work.pass_scans.max | 0 | 10 | new | incidental (within band) |
| ringcross/legion/work.pass_scans.p99 | 0 | 3 | new | incidental (within band) |
| ringcross/legion/work.pass_scans.total | 0 | 1579 | new | incidental (within band) |
| ringcross/legion/work.pass_scans_skipped.max | 74 | 48 | -35.1% | incidental (within band) |
| ringcross/legion/work.pass_scans_skipped.p99 | 61 | 48 | -21.3% | incidental (within band) |
| ringcross/legion/work.pass_scans_skipped.total | 169291 | 90216 | -46.7% | incidental (within band) |
| ringcross/legion/work.rechoice_bfs_cells.max | 694 | 737 | +6.2% | incidental (within band) |
| ringcross/legion/work.rechoice_bfs_cells.total | 1343 | 3823 | +184.7% | incidental (within band) |
| ringcross/legion/work.slot_search_cells.total | 4680 | 8035 | +71.7% | incidental (within band) |
| ringcross/legion/work.still_units_processed.p99 | 2 | 3 | +50.0% | incidental (within band) |
| ringcross/legion/work.still_units_processed.total | 4121 | 9069 | +120.1% | incidental (within band) |
| ringcross/legion/work.trace_cells.max | 16250 | 14642 | -9.9% | incidental (within band) |
| ringcross/legion/work.trace_cells.p99 | 3712 | 3438 | -7.4% | incidental (within band) |
| staticblock-other/legion/work.group_loop_iters.total | 161 | 221 | +37.3% | incidental (within band) |
| staticblock-own/legion/work.group_loop_iters.total | 161 | 221 | +37.3% | incidental (within band) |
| tail-wave/legion/work.group_loop_iters.total | 1218 | 1279 | +5.0% | incidental (within band) |
1609 keys moved by more than 5%; 205 intended, 1404 incidental
