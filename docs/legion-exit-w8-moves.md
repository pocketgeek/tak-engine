# W8 exit: keys moved by more than 5% by the passage gate (protocol 247)

Generated from two Windows sweeps (build-o2, `legion_scenario <file> --mode both --offsets gate --data C:/tak/data/game --json`, 11 gate offsets, medians as `legion_check` reads them): the W8 step-0 sim `787fe029` (hash-identical to the W7 exit sim, protocol 246) against the W8 land head (protocol 247). 13 of 131 files change state (Legion only; every Retail line of the 131 is hash-identical). `intended` marks the passage gate's own targets (files abreast, rounded90 / t90 / done, stopped, reversals, stop-go); the rest is incidental and licensed by cause in `baseline.json` (`accepted_regressions`) or inside its band. Keys that were 0 and are now >0 print `new`.


## doorform (49 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|
| back | 26 | 21 | -19.2% | incidental |
| backward | 67 | 88 | +31.3% | incidental |
| churn.bin2 | 43688 | 34477 | -21.1% | incidental |
| crawl_samples | 596 | 495 | -16.9% | incidental |
| engagement_on_flowing | 37 | 33 | -10.8% | incidental |
| flip_rate_per30_permille | 48 | 45 | -6.2% | incidental |
| flips | 238 | 221 | -7.1% | incidental |
| follow_chain_max | 8 | 10 | +25.0% | incidental |
| follow_chain_mean_x100 | 474 | 513 | +8.2% | incidental |
| g.A.complete_dist_max | 11 | 10 | -9.1% | incidental |
| g.A.stops | 154 | 145 | -5.8% | incidental |
| gate.door.files_x100 | 418 | 387 | -7.4% | intended |
| gate.door.samples | 814 | 737 | -9.5% | incidental |
| parked_held | 169 | 116 | -31.4% | incidental |
| reversals | 3 | 8 | +166.7% | intended |
| sideways | 789 | 852 | +8.0% | incidental |
| statue_ticks | 6112 | 5040 | -17.5% | incidental |
| stopped_permille | 41 | 38 | -7.3% | intended |
| transit_far_held_permille | 51 | 46 | -9.8% | incidental |
| transit_held_permille | 39 | 34 | -12.8% | incidental |
| waiting_held | 4174 | 3819 | -8.5% | incidental |
| wall_touch_near_permille | 84 | 77 | -8.3% | incidental |
| work.arrivals.max | 2 | 3 | +50.0% | incidental |
| work.completion_dist_1.max | 1 | 0 | -100.0% | incidental |
| work.completion_dist_1.total | 1 | 0 | -100.0% | incidental |
| work.completion_dist_2.total | 7 | 8 | +14.3% | incidental |
| work.completion_dist_4.max | 2 | 1 | -50.0% | incidental |
| work.completion_dist_4.total | 9 | 8 | -11.1% | incidental |
| work.completion_dist_sum.max | 19 | 15 | -21.1% | incidental |
| work.contact_arrivals.total | 15 | 14 | -6.7% | incidental |
| work.crowd_window_ring_cells.max | 544 | 352 | -35.3% | incidental |
| work.crowd_window_ring_cells.total | 7104 | 5760 | -18.9% | incidental |
| work.detour_cells.max | 1186 | 980 | -17.4% | incidental |
| work.detour_cells.total | 22704 | 19606 | -13.6% | incidental |
| work.detours.total | 42 | 32 | -23.8% | incidental |
| work.held_rechecks.max | 3 | 2 | -33.3% | incidental |
| work.held_rechecks.total | 292 | 211 | -27.7% | incidental |
| work.holds.max | 4 | 5 | +25.0% | incidental |
| work.mission_arrivals_1.max | 2 | 3 | +50.0% | incidental |
| work.move_calls_by_state_2.max | 17 | 14 | -17.6% | incidental |
| work.move_calls_by_state_2.p99 | 12 | 11 | -8.3% | incidental |
| work.move_calls_by_state_2.total | 6065 | 5480 | -9.6% | incidental |
| work.pass_scan_cells.total | 9792 | 15663 | +60.0% | incidental |
| work.pass_scans.total | 408 | 653 | +60.0% | incidental |
| work.rechoice_bfs_cells.max | 1246 | 1107 | -11.2% | incidental |
| work.rechoice_bfs_cells.total | 7295 | 4802 | -34.2% | incidental |
| work.slides.max | 4 | 5 | +25.0% | incidental |
| work.slides.total | 701 | 844 | +20.4% | incidental |
| work.slot_search_cells.total | 10031 | 7538 | -24.9% | incidental |

## doorplug-124 (73 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|
| aim_reversals | 339 | 214 | -36.9% | intended |
| back | 131 | 83 | -36.6% | incidental |
| backward | 73 | 60 | -17.8% | incidental |
| churn.bin2 | 1633537 | 1927765 | +18.0% | incidental |
| churn.bin3 | 653357 | 536368 | -17.9% | incidental |
| churn.bin4 | 51673 | 15299 | -70.4% | incidental |
| crawl_samples | 2649 | 2006 | -24.3% | incidental |
| decision_samples | 23098 | 21173 | -8.3% | incidental |
| flip_rate_per30_permille | 169 | 146 | -13.6% | incidental |
| flips | 1299 | 1040 | -19.9% | incidental |
| follow_chain_max | 15 | 12 | -20.0% | incidental |
| g.B.complete_dist_max | 17 | 15 | -11.8% | incidental |
| g.B.complete_outside_radius | 2 | 0 | -100.0% | incidental |
| g.B.done | -1 | 8042 | +804300.0% | intended |
| g.B.left_behind | 2 | 0 | -100.0% | incidental |
| g.B.stops | 919 | 714 | -22.3% | incidental |
| g.B.t90 | 6557 | 6038 | -7.9% | intended |
| gate.door.crossings | 0 | 8 | new | incidental |
| gate.door.files_x100 | 250 | 317 | +26.8% | intended |
| gate.door.pairs | 2853 | 3216 | +12.7% | incidental |
| gate.door.samples | 2228 | 1871 | -16.0% | incidental |
| gate.door.spread_x100 | 394 | 541 | +37.3% | incidental |
| parked_held | 1564 | 182 | -88.4% | incidental |
| reversals | 26 | 16 | -38.5% | intended |
| side.past.t90 | 4813 | 4508 | -6.3% | intended |
| sideways | 6643 | 7078 | +6.5% | incidental |
| spacing_samples | 23098 | 21173 | -8.3% | incidental |
| statue_ticks | 25238 | 19909 | -21.1% | incidental |
| stop_go | 1916 | 1666 | -13.0% | intended |
| stopped_permille | 149 | 115 | -22.8% | intended |
| transit_far_held_permille | 232 | 147 | -36.6% | incidental |
| transit_far_samples | 8030 | 6957 | -13.4% | incidental |
| transit_held_permille | 149 | 111 | -25.5% | incidental |
| transit_samples | 21508 | 19637 | -8.7% | incidental |
| waiting_held | 20203 | 13436 | -33.5% | incidental |
| wall_near_samples | 5062 | 4616 | -8.8% | incidental |
| wall_touch_near_permille | 167 | 138 | -17.4% | incidental |
| wall_touch_permille | 41 | 33 | -19.5% | incidental |
| work.aware_pairs.total | 125 | 117 | -6.4% | incidental |
| work.completion_dist_1.total | 3 | 4 | +33.3% | incidental |
| work.completion_dist_2.total | 12 | 13 | +8.3% | incidental |
| work.completion_dist_sum.max | 30 | 35 | +16.7% | incidental |
| work.contact_arrivals.total | 53 | 59 | +11.3% | incidental |
| work.crowd_window_ring_cells.max | 800 | 480 | -40.0% | incidental |
| work.crowd_window_ring_cells.p99 | 128 | 64 | -50.0% | incidental |
| work.crowd_window_ring_cells.total | 29248 | 20928 | -28.4% | incidental |
| work.detour_cells.max | 1125 | 1023 | -9.1% | incidental |
| work.detour_cells.p99 | 471 | 379 | -19.5% | incidental |
| work.detour_cells.total | 100845 | 57093 | -43.4% | incidental |
| work.detours.max | 3 | 2 | -33.3% | incidental |
| work.detours.total | 216 | 123 | -43.1% | incidental |
| work.group_loop_iters.total | 464 | 438 | -5.6% | incidental |
| work.held_rechecks.max | 6 | 3 | -50.0% | incidental |
| work.held_rechecks.p99 | 5 | 1 | -80.0% | incidental |
| work.held_rechecks.total | 1660 | 526 | -68.3% | incidental |
| work.holds.max | 5 | 6 | +20.0% | incidental |
| work.holds.total | 1948 | 1703 | -12.6% | incidental |
| work.move_calls_by_state_2.max | 31 | 27 | -12.9% | incidental |
| work.move_calls_by_state_2.p99 | 23 | 19 | -17.4% | incidental |
| work.move_calls_by_state_2.total | 34294 | 24226 | -29.4% | incidental |
| work.moves.total | 230870 | 212003 | -8.2% | incidental |
| work.outside_area_completions.max | 2 | 1 | -50.0% | incidental |
| work.outside_area_completions.total | 6 | 5 | -16.7% | incidental |
| work.pass_scan_cells.max | 192 | 312 | +62.5% | incidental |
| work.pass_scan_cells.p99 | 120 | 216 | +80.0% | incidental |
| work.pass_scan_cells.total | 130344 | 158616 | +21.7% | incidental |
| work.pass_scans.max | 8 | 13 | +62.5% | incidental |
| work.pass_scans.p99 | 5 | 9 | +80.0% | incidental |
| work.pass_scans.total | 5431 | 6609 | +21.7% | incidental |
| work.rechoice_bfs_cells.max | 1801 | 2045 | +13.5% | incidental |
| work.rechoice_bfs_cells.total | 31976 | 27011 | -15.5% | incidental |
| work.slides.total | 2396 | 2611 | +9.0% | incidental |
| work.slot_search_cells.total | 40434 | 35469 | -12.3% | incidental |

## gap6 (76 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|
| aim_reversals | 147 | 117 | -20.4% | intended |
| back | 108 | 66 | -38.9% | incidental |
| backward | 15 | 2 | -86.7% | incidental |
| churn.bin0 | 1586958 | 1795203 | +13.1% | incidental |
| churn.bin1 | 428936 | 251726 | -41.3% | incidental |
| churn.bin2 | 12891 | 2400 | -81.4% | incidental |
| contact_own_permille | 688 | 735 | +6.8% | incidental |
| crawl_samples | 1293 | 463 | -64.2% | incidental |
| decision_samples | 10745 | 9417 | -12.4% | incidental |
| engagement_on_flowing | 42 | 47 | +11.9% | incidental |
| flip_rate_per30_permille | 176 | 124 | -29.5% | incidental |
| flips | 629 | 388 | -38.3% | incidental |
| follow_chain_mean_x100 | 319 | 361 | +13.2% | incidental |
| g.A.complete_dist_max | 13 | 14 | +7.7% | incidental |
| g.A.stops | 397 | 370 | -6.8% | incidental |
| g.A.t50 | 2207 | 1937 | -12.2% | incidental |
| g.A.t90 | 3062 | 2395 | -21.8% | intended |
| gate.top.crossings | 0 | 3 | new | incidental |
| gate.top.files_x100 | 149 | 262 | +75.8% | intended |
| gate.top.pairs | 278 | 543 | +95.3% | incidental |
| gate.top.samples | 1504 | 806 | -46.4% | incidental |
| gate.top.spread_x100 | 114 | 338 | +196.5% | incidental |
| lanes.lane_work | 0 | 890849 | new | incidental |
| lanes.pivot_calls | 4041 | 3324 | -17.7% | incidental |
| lanes.pivot_work | 314576 | 273984 | -12.9% | incidental |
| parked_held | 507 | 2 | -99.6% | incidental |
| reversals | 13 | 8 | -38.5% | intended |
| side.east.t90 | 2266 | 1525 | -32.7% | intended |
| sideways | 4893 | 5177 | +5.8% | incidental |
| spacing_samples | 10745 | 9417 | -12.4% | incidental |
| statue_ticks | 11161 | 3781 | -66.1% | incidental |
| stopped_permille | 172 | 95 | -44.8% | intended |
| transit_held_permille | 175 | 92 | -47.4% | incidental |
| transit_samples | 10137 | 8770 | -13.5% | incidental |
| waiting_held | 12439 | 4570 | -63.3% | incidental |
| wall_near_samples | 3825 | 3278 | -14.3% | incidental |
| wall_samples | 9486 | 8966 | -5.5% | incidental |
| wall_touch_near_permille | 211 | 196 | -7.1% | incidental |
| wall_touch_permille | 82 | 73 | -11.0% | incidental |
| work.aware_pairs.total | 72 | 62 | -13.9% | incidental |
| work.completion_dist_1.max | 0 | 1 | new | incidental |
| work.completion_dist_1.total | 0 | 1 | new | incidental |
| work.completion_dist_3.max | 1 | 2 | +100.0% | incidental |
| work.completion_dist_4.total | 22 | 19 | -13.6% | incidental |
| work.completion_dist_sum.max | 32 | 28 | -12.5% | incidental |
| work.completion_dist_sum.total | 357 | 336 | -5.9% | incidental |
| work.crowd_window_ring_cells.p99 | 64 | 0 | -100.0% | incidental |
| work.crowd_window_ring_cells.total | 31712 | 21312 | -32.8% | incidental |
| work.detour_cells.max | 1174 | 562 | -52.1% | incidental |
| work.detour_cells.p99 | 466 | 1 | -99.8% | incidental |
| work.detour_cells.total | 60099 | 24573 | -59.1% | incidental |
| work.detours.max | 3 | 1 | -66.7% | incidental |
| work.detours.p99 | 1 | 0 | -100.0% | incidental |
| work.detours.total | 140 | 51 | -63.6% | incidental |
| work.group_loop_iters.total | 214 | 191 | -10.7% | incidental |
| work.held_rechecks.max | 8 | 3 | -62.5% | incidental |
| work.held_rechecks.p99 | 5 | 1 | -80.0% | incidental |
| work.held_rechecks.total | 1227 | 166 | -86.5% | incidental |
| work.line_sweeps.total | 114517 | 126161 | +10.2% | incidental |
| work.move_calls_by_state_1.total | 90737 | 85380 | -5.9% | incidental |
| work.move_calls_by_state_2.max | 24 | 21 | -12.5% | incidental |
| work.move_calls_by_state_2.p99 | 21 | 15 | -28.6% | incidental |
| work.move_calls_by_state_2.total | 18445 | 8941 | -51.5% | incidental |
| work.moves.total | 106326 | 94265 | -11.3% | incidental |
| work.outside_area_completions.total | 1 | 2 | +100.0% | incidental |
| work.pass_scans_skipped.max | 36 | 42 | +16.7% | incidental |
| work.pass_scans_skipped.p99 | 33 | 39 | +18.2% | incidental |
| work.pass_scans_skipped.total | 52396 | 47324 | -9.7% | incidental |
| work.rechoice_bfs_cells.max | 2148 | 1408 | -34.5% | incidental |
| work.rechoice_bfs_cells.total | 11665 | 9830 | -15.7% | incidental |
| work.slides.p99 | 2 | 3 | +50.0% | incidental |
| work.slides.total | 970 | 1187 | +22.4% | incidental |
| work.slot_search_cells.max | 2148 | 2025 | -5.7% | incidental |
| work.slot_search_cells.total | 14410 | 12575 | -12.7% | incidental |
| work.still_units_processed.total | 6010 | 6445 | +7.2% | incidental |
| work.trace_cells.total | 1093013 | 1225740 | +12.1% | incidental |

## gap8 (79 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|
| aim_reversals | 147 | 122 | -17.0% | intended |
| back | 108 | 54 | -50.0% | incidental |
| backward | 15 | 4 | -73.3% | incidental |
| churn.bin0 | 1587420 | 1826076 | +15.0% | incidental |
| churn.bin1 | 429544 | 178340 | -58.5% | incidental |
| churn.bin2 | 12891 | 3437 | -73.3% | incidental |
| contact_own_permille | 688 | 734 | +6.7% | incidental |
| crawl_samples | 1293 | 461 | -64.3% | incidental |
| decision_samples | 10745 | 9311 | -13.3% | incidental |
| engagement_on_flowing | 42 | 45 | +7.1% | incidental |
| flip_rate_per30_permille | 176 | 110 | -37.5% | incidental |
| flips | 629 | 340 | -45.9% | incidental |
| g.A.complete_dist_max | 13 | 12 | -7.7% | incidental |
| g.A.done | 3554 | 3242 | -8.8% | intended |
| g.A.stops | 397 | 331 | -16.6% | incidental |
| g.A.t50 | 2207 | 1937 | -12.2% | incidental |
| g.A.t90 | 3062 | 2316 | -24.4% | intended |
| gate.top.crossings | 0 | 7 | new | incidental |
| gate.top.files_x100 | 149 | 328 | +120.1% | intended |
| gate.top.pairs | 278 | 592 | +112.9% | incidental |
| gate.top.samples | 1504 | 716 | -52.4% | incidental |
| gate.top.spread_x100 | 114 | 483 | +323.7% | incidental |
| lanes.lane_work | 0 | 893026 | new | incidental |
| lanes.pivot_calls | 4041 | 3264 | -19.2% | incidental |
| lanes.pivot_work | 314576 | 270648 | -14.0% | incidental |
| parked_held | 507 | 127 | -75.0% | incidental |
| reversals | 13 | 6 | -53.8% | intended |
| side.east.t90 | 2266 | 1343 | -40.7% | intended |
| sideways | 4893 | 5750 | +17.5% | incidental |
| spacing_samples | 10745 | 9311 | -13.3% | incidental |
| statue_ticks | 11161 | 3714 | -66.7% | incidental |
| stopped_permille | 172 | 76 | -55.8% | intended |
| transit_held_permille | 175 | 74 | -57.7% | incidental |
| transit_samples | 10137 | 8616 | -15.0% | incidental |
| waiting_held | 12439 | 3726 | -70.0% | incidental |
| wall_near_samples | 3825 | 3243 | -15.2% | incidental |
| wall_samples | 9486 | 8986 | -5.3% | incidental |
| wall_touch_near_permille | 211 | 182 | -13.7% | incidental |
| wall_touch_permille | 82 | 62 | -24.4% | incidental |
| work.arrivals.max | 3 | 4 | +33.3% | incidental |
| work.aware_pairs.total | 72 | 65 | -9.7% | incidental |
| work.completion_dist_1.max | 0 | 1 | new | incidental |
| work.completion_dist_1.total | 0 | 1 | new | incidental |
| work.completion_dist_3.max | 1 | 2 | +100.0% | incidental |
| work.completion_dist_3.total | 23 | 27 | +17.4% | incidental |
| work.completion_dist_4.total | 22 | 16 | -27.3% | incidental |
| work.completion_dist_sum.total | 357 | 322 | -9.8% | incidental |
| work.contact_arrivals.max | 3 | 4 | +33.3% | incidental |
| work.contact_arrivals.total | 23 | 21 | -8.7% | incidental |
| work.crowd_window_ring_cells.p99 | 64 | 0 | -100.0% | incidental |
| work.crowd_window_ring_cells.total | 31712 | 20032 | -36.8% | incidental |
| work.detour_cells.max | 1174 | 549 | -53.2% | incidental |
| work.detour_cells.p99 | 475 | 0 | -100.0% | incidental |
| work.detour_cells.total | 60585 | 13524 | -77.7% | incidental |
| work.detours.max | 3 | 1 | -66.7% | incidental |
| work.detours.p99 | 1 | 0 | -100.0% | incidental |
| work.detours.total | 140 | 34 | -75.7% | incidental |
| work.held_rechecks.max | 8 | 2 | -75.0% | incidental |
| work.held_rechecks.p99 | 5 | 1 | -80.0% | incidental |
| work.held_rechecks.total | 1227 | 186 | -84.8% | incidental |
| work.line_sweeps.total | 114517 | 123007 | +7.4% | incidental |
| work.mission_arrivals_1.max | 3 | 4 | +33.3% | incidental |
| work.move_calls_by_state_2.max | 24 | 20 | -16.7% | incidental |
| work.move_calls_by_state_2.p99 | 21 | 13 | -38.1% | incidental |
| work.move_calls_by_state_2.total | 18445 | 6958 | -62.3% | incidental |
| work.moves.total | 106326 | 93237 | -12.3% | incidental |
| work.outside_area_completions.max | 1 | 0 | -100.0% | incidental |
| work.outside_area_completions.total | 1 | 0 | -100.0% | incidental |
| work.pass_scans_skipped.max | 36 | 44 | +22.2% | incidental |
| work.pass_scans_skipped.p99 | 33 | 41 | +24.2% | incidental |
| work.pass_scans_skipped.total | 52396 | 46753 | -10.8% | incidental |
| work.rechoice_bfs_cells.max | 2148 | 1232 | -42.6% | incidental |
| work.rechoice_bfs_cells.total | 11665 | 7011 | -39.9% | incidental |
| work.slides.p99 | 2 | 3 | +50.0% | incidental |
| work.slides.total | 970 | 1226 | +26.4% | incidental |
| work.slot_search_cells.max | 2148 | 2025 | -5.7% | incidental |
| work.slot_search_cells.total | 14410 | 9756 | -32.3% | incidental |
| work.still_units_processed.total | 6010 | 6483 | +7.9% | incidental |
| work.trace_cells.total | 1093013 | 1205315 | +10.3% | incidental |

## gapsweep (72 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|
| aim_reversals | 619 | 517 | -16.5% | intended |
| back | 359 | 266 | -25.9% | incidental |
| backward | 102 | 70 | -31.4% | incidental |
| churn.bin1 | 1163537 | 781736 | -32.8% | incidental |
| churn.bin2 | 27925 | 12871 | -53.9% | incidental |
| crawl_samples | 3859 | 1942 | -49.7% | incidental |
| decision_samples | 48209 | 44964 | -6.7% | incidental |
| engagement_on_flowing | 218 | 232 | +6.4% | incidental |
| flip_rate_per30_permille | 112 | 87 | -22.3% | incidental |
| flips | 1789 | 1315 | -26.5% | incidental |
| follow_chain_max | 12 | 14 | +16.7% | incidental |
| g.G6.complete_dist_max | 13 | 14 | +7.7% | incidental |
| g.G6.stops | 397 | 370 | -6.8% | incidental |
| g.G6.t50 | 2207 | 1937 | -12.2% | incidental |
| g.G6.t90 | 3062 | 2395 | -21.8% | intended |
| g.G8.complete_dist_median | 8 | 7 | -12.5% | incidental |
| g.G8.done | 3604 | 2884 | -20.0% | intended |
| g.G8.stops | 410 | 300 | -26.8% | incidental |
| g.G8.t50 | 2164 | 1894 | -12.5% | incidental |
| g.G8.t90 | 3019 | 2317 | -23.3% | intended |
| gate.g6.crossings | 0 | 3 | new | incidental |
| gate.g6.files_x100 | 149 | 262 | +75.8% | intended |
| gate.g6.pairs | 278 | 543 | +95.3% | incidental |
| gate.g6.samples | 1504 | 806 | -46.4% | incidental |
| gate.g6.spread_x100 | 114 | 338 | +196.5% | incidental |
| gate.g8.crossings | 0 | 10 | new | incidental |
| gate.g8.files_x100 | 171 | 337 | +97.1% | intended |
| gate.g8.pairs | 233 | 659 | +182.8% | incidental |
| gate.g8.samples | 1233 | 780 | -36.7% | incidental |
| gate.g8.spread_x100 | 163 | 505 | +209.8% | incidental |
| lanes.lane_work | 0 | 5013620 | new | incidental |
| lanes.pivot_calls | 20358 | 18873 | -7.3% | incidental |
| parked_held | 1467 | 126 | -91.4% | incidental |
| reversals | 49 | 32 | -34.7% | intended |
| spacing_samples | 48209 | 44964 | -6.7% | incidental |
| statue_ticks | 33392 | 14750 | -55.8% | incidental |
| stopped_permille | 101 | 70 | -30.7% | intended |
| transit_held_permille | 97 | 63 | -35.1% | incidental |
| transit_samples | 44339 | 41467 | -6.5% | incidental |
| waiting_held | 30086 | 15463 | -48.6% | incidental |
| wall_near_samples | 15632 | 14061 | -10.0% | incidental |
| wall_touch_near_permille | 170 | 155 | -8.8% | incidental |
| wall_touch_permille | 58 | 50 | -13.8% | incidental |
| work.aware_pairs.p99 | 15 | 20 | +33.3% | incidental |
| work.completion_dist_2.total | 24 | 28 | +16.7% | incidental |
| work.completion_dist_4.max | 3 | 4 | +33.3% | incidental |
| work.completion_dist_4.total | 78 | 73 | -6.4% | incidental |
| work.crowd_window_ring_cells.p99 | 960 | 832 | -13.3% | incidental |
| work.crowd_window_ring_cells.total | 125504 | 99168 | -21.0% | incidental |
| work.detour_cells.max | 1494 | 1101 | -26.3% | incidental |
| work.detour_cells.p99 | 526 | 497 | -5.5% | incidental |
| work.detour_cells.total | 177908 | 84816 | -52.3% | incidental |
| work.detours.max | 3 | 2 | -33.3% | incidental |
| work.detours.total | 381 | 167 | -56.2% | incidental |
| work.held_rechecks.max | 16 | 4 | -75.0% | incidental |
| work.held_rechecks.p99 | 11 | 2 | -81.8% | incidental |
| work.held_rechecks.total | 3820 | 596 | -84.4% | incidental |
| work.holds.max | 11 | 12 | +9.1% | incidental |
| work.move_calls_by_state_2.max | 82 | 70 | -14.6% | incidental |
| work.move_calls_by_state_2.p99 | 70 | 58 | -17.1% | incidental |
| work.move_calls_by_state_2.total | 47911 | 31137 | -35.0% | incidental |
| work.moves.total | 481183 | 449627 | -6.6% | incidental |
| work.pass_scans_skipped.max | 190 | 206 | +8.4% | incidental |
| work.pass_scans_skipped.p99 | 185 | 201 | +8.6% | incidental |
| work.rechoice_bfs_cells.max | 2148 | 1674 | -22.1% | incidental |
| work.rechoice_bfs_cells.p99 | 330 | 285 | -13.6% | incidental |
| work.rechoice_bfs_cells.total | 41775 | 33425 | -20.0% | incidental |
| work.slides.max | 14 | 13 | -7.1% | incidental |
| work.slides.total | 4693 | 5197 | +10.7% | incidental |
| work.slot_search_cells.max | 2148 | 2025 | -5.7% | incidental |
| work.slot_search_cells.p99 | 421 | 360 | -14.5% | incidental |
| work.slot_search_cells.total | 55500 | 47150 | -15.0% | incidental |

## gen1-route-45941 (0 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|

## gen1-route-54772 (12 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|
| g.g_zongob.done | 9947 | 9272 | -6.8% | intended |
| g.g_zontroll.complete_dist_median | 17 | 15 | -11.8% | incidental |
| g.g_zontroll.complete_outside_radius | 0 | 1 | new | incidental |
| g.g_zontroll.left_behind | 1 | 2 | +100.0% | incidental |
| reversals | 48 | 56 | +16.7% | intended |
| work.completion_dist_2.total | 5 | 4 | -20.0% | incidental |
| work.completion_dist_3.total | 17 | 19 | +11.8% | incidental |
| work.completion_dist_4.max | 2 | 3 | +50.0% | incidental |
| work.completion_dist_5.max | 3 | 2 | -33.3% | incidental |
| work.completion_dist_sum.max | 55 | 52 | -5.5% | incidental |
| work.rechoice_bfs_cells.p99 | 20 | 12 | -40.0% | incidental |
| work.slot_search_cells.p99 | 31 | 18 | -41.9% | incidental |

## mixed2 (44 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|
| back | 145 | 100 | -31.0% | incidental |
| churn.bin4 | 105528 | 118824 | +12.6% | incidental |
| clearance_p10_x100 | 0 | 100 | new | incidental |
| contact_own_permille | 661 | 697 | +5.4% | incidental |
| engagement_on_flowing | 112 | 120 | +7.1% | incidental |
| follow_chain_max | 15 | 16 | +6.7% | incidental |
| g.F.hover_max | 65 | 99 | +52.3% | incidental |
| gate.mid.crossings | 35 | 37 | +5.7% | incidental |
| gate.mid.pairs | 317 | 348 | +9.8% | incidental |
| gate.mid.samples | 2018 | 1907 | -5.5% | incidental |
| parked_held | 211 | 229 | +8.5% | incidental |
| side.east.t90 | 2436 | 2308 | -5.3% | intended |
| sideways | 6902 | 7321 | +6.1% | incidental |
| transit_far_held_permille | 74 | 63 | -14.9% | incidental |
| transit_far_samples | 11325 | 10757 | -5.0% | incidental |
| waiting_held | 9720 | 8704 | -10.5% | incidental |
| waiting_no_progress | 648 | 609 | -6.0% | incidental |
| wall_touch_permille | 22 | 19 | -13.6% | incidental |
| work.completion_dist_1.total | 1 | 2 | +100.0% | incidental |
| work.completion_dist_2.total | 13 | 8 | -38.5% | incidental |
| work.completion_dist_3.total | 42 | 38 | -9.5% | incidental |
| work.completion_dist_4.max | 4 | 3 | -25.0% | incidental |
| work.completion_dist_4.total | 37 | 44 | +18.9% | incidental |
| work.completion_dist_5.total | 5 | 2 | -60.0% | incidental |
| work.contact_arrivals.total | 44 | 51 | +15.9% | incidental |
| work.crowd_window_ring_cells.max | 800 | 608 | -24.0% | incidental |
| work.crowd_window_ring_cells.p99 | 64 | 32 | -50.0% | incidental |
| work.detour_cells.max | 1002 | 934 | -6.8% | incidental |
| work.detour_cells.p99 | 349 | 287 | -17.8% | incidental |
| work.detour_cells.total | 51881 | 47122 | -9.2% | incidental |
| work.formation_ring_cells.max | 2351 | 3247 | +38.1% | incidental |
| work.formation_ring_cells.total | 4151 | 5047 | +21.6% | incidental |
| work.lift_members_skipped.total | 1275 | 1104 | -13.4% | incidental |
| work.lift_members_walked.p99 | 5 | 6 | +20.0% | incidental |
| work.lift_members_walked.total | 3487 | 4087 | +17.2% | incidental |
| work.lift_target_polls.total | 22 | 12 | -45.5% | incidental |
| work.lifts.max | 3 | 2 | -33.3% | incidental |
| work.lifts.total | 68 | 6 | -91.2% | incidental |
| work.move_calls_by_state_2.max | 22 | 20 | -9.1% | incidental |
| work.outside_area_completions.total | 9 | 13 | +44.4% | incidental |
| work.rechoice_bfs_cells.max | 1349 | 1252 | -7.2% | incidental |
| work.rechoice_bfs_cells.total | 12043 | 14391 | +19.5% | incidental |
| work.slot_search_cells.max | 2351 | 3247 | +38.1% | incidental |
| work.slot_search_cells.total | 20244 | 22288 | +10.1% | incidental |

## strait (55 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|
| back | 88 | 71 | -19.3% | incidental |
| backward | 253 | 184 | -27.3% | incidental |
| churn.bin2 | 47664 | 61258 | +28.5% | incidental |
| crawl_samples | 2381 | 2109 | -11.4% | incidental |
| decision_samples | 14107 | 13119 | -7.0% | incidental |
| detour_permille | 257 | 226 | -12.1% | incidental |
| engagement_on_flowing | 16 | 11 | -31.2% | incidental |
| flips | 1266 | 1143 | -9.7% | incidental |
| follow_chain_mean_x100 | 261 | 275 | +5.4% | incidental |
| g.B.arrived | 31 | 34 | +9.7% | intended |
| g.B.complete_dist_max | 52 | 49 | -5.8% | incidental |
| g.B.complete_dist_median | 15 | 14 | -6.7% | incidental |
| g.B.complete_outside_radius | 17 | 14 | -17.6% | incidental |
| g.B.left_behind | 17 | 14 | -17.6% | incidental |
| g.B.stops | 938 | 1003 | +6.9% | incidental |
| gate.strait.files_x100 | 243 | 205 | -15.6% | intended |
| gate.strait.pairs | 254 | 287 | +13.0% | incidental |
| gate.strait.samples | 283 | 5960 | +2006.0% | incidental |
| gate.strait.spread_x100 | 346 | 401 | +15.9% | incidental |
| parked_held | 3623 | 2228 | -38.5% | incidental |
| region.east.inside | 32 | 35 | +9.4% | incidental |
| reversals | 16 | 12 | -25.0% | intended |
| side.barrier.high | 32 | 35 | +9.4% | incidental |
| side.barrier.low | 16 | 13 | -18.8% | incidental |
| spacing_samples | 14107 | 13119 | -7.0% | incidental |
| statue_ticks | 22948 | 20548 | -10.5% | incidental |
| still900_ever | 1 | 0 | -100.0% | incidental |
| stop_go | 1446 | 1543 | +6.7% | intended |
| stopped_permille | 285 | 248 | -13.0% | intended |
| transit_held_permille | 289 | 252 | -12.8% | incidental |
| transit_samples | 13780 | 12885 | -6.5% | incidental |
| waiting_held | 21956 | 19511 | -11.1% | incidental |
| work.arrivals.max | 4 | 5 | +25.0% | incidental |
| work.completion_dist_2.total | 2 | 3 | +50.0% | incidental |
| work.completion_dist_3.total | 5 | 3 | -40.0% | incidental |
| work.completion_dist_4.max | 1 | 2 | +100.0% | incidental |
| work.completion_dist_4.total | 17 | 18 | +5.9% | incidental |
| work.completion_dist_5.max | 1 | 2 | +100.0% | incidental |
| work.completion_dist_6.total | 15 | 10 | -33.3% | incidental |
| work.completion_dist_sum.total | 1043 | 918 | -12.0% | incidental |
| work.contact_arrivals.max | 4 | 5 | +25.0% | incidental |
| work.crowd_window_ring_cells.total | 76752 | 67680 | -11.8% | incidental |
| work.detour_cells.p99 | 303 | 284 | -6.3% | incidental |
| work.detour_cells.total | 57836 | 48043 | -16.9% | incidental |
| work.detours.max | 2 | 3 | +50.0% | incidental |
| work.detours.total | 176 | 143 | -18.8% | incidental |
| work.held_rechecks.p99 | 5 | 4 | -20.0% | incidental |
| work.held_rechecks.total | 3619 | 2773 | -23.4% | incidental |
| work.holds.total | 1464 | 1563 | +6.8% | incidental |
| work.mission_arrivals_1.max | 4 | 5 | +25.0% | incidental |
| work.move_calls_by_state_2.p99 | 19 | 18 | -5.3% | incidental |
| work.move_calls_by_state_2.total | 35804 | 32083 | -10.4% | incidental |
| work.moves.total | 139683 | 130455 | -6.6% | incidental |
| work.outside_area_completions.total | 16 | 12 | -25.0% | incidental |
| work.trace_cells.total | 1000181 | 1056972 | +5.7% | incidental |

## strait-2x150 (0 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|

## strait-3x48 (28 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|
| back | 55 | 72 | +30.9% | incidental |
| backward | 112 | 86 | -23.2% | incidental |
| churn.bin1 | 451662 | 502690 | +11.3% | incidental |
| churn.bin2 | 29153 | 20671 | -29.1% | incidental |
| crawl_samples | 2068 | 1961 | -5.2% | incidental |
| detour_permille | 154 | 163 | +5.8% | incidental |
| flips | 767 | 829 | +8.1% | incidental |
| g.B.complete_dist_median | 10 | 9 | -10.0% | incidental |
| g.B.t90 | 3692 | 3422 | -7.3% | intended |
| parked_held | 1493 | 1033 | -30.8% | incidental |
| reversals | 90 | 47 | -47.8% | intended |
| statue_ticks | 21979 | 20324 | -7.5% | incidental |
| work.aware_pairs.total | 71 | 75 | +5.6% | incidental |
| work.completion_dist_1.max | 1 | 0 | -100.0% | incidental |
| work.completion_dist_1.total | 1 | 0 | -100.0% | incidental |
| work.completion_dist_2.total | 2 | 3 | +50.0% | incidental |
| work.completion_dist_3.total | 12 | 13 | +8.3% | incidental |
| work.completion_dist_sum.max | 26 | 38 | +46.2% | incidental |
| work.contact_arrivals.total | 19 | 21 | +10.5% | incidental |
| work.crowd_window_ring_cells.p99 | 40 | 0 | -100.0% | incidental |
| work.crowd_window_ring_cells.total | 37760 | 35320 | -6.5% | incidental |
| work.held_rechecks.total | 2273 | 2082 | -8.4% | incidental |
| work.legion_total.p99 | 1085 | 1154 | +6.4% | incidental |
| work.pass_scan_cells.max | 540 | 594 | +10.0% | incidental |
| work.pass_scan_cells.p99 | 324 | 378 | +16.7% | incidental |
| work.pass_scans.max | 10 | 11 | +10.0% | incidental |
| work.pass_scans.p99 | 6 | 7 | +16.7% | incidental |
| work.rechoice_bfs_cells.max | 1348 | 1026 | -23.9% | incidental |

## strait-3x80 (50 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|
| back | 176 | 135 | -23.3% | incidental |
| backward | 294 | 235 | -20.1% | incidental |
| churn.bin1 | 1027376 | 1121529 | +9.2% | incidental |
| churn.bin2 | 190028 | 138347 | -27.2% | incidental |
| churn.bin3 | 4849 | 4409 | -9.1% | incidental |
| crawl_samples | 4729 | 4102 | -13.3% | incidental |
| flips | 2442 | 2227 | -8.8% | incidental |
| follow_chain_max | 9 | 10 | +11.1% | incidental |
| g.B.arrived | 58 | 54 | -6.9% | intended |
| g.B.complete_dist_median | 14 | 15 | +7.1% | incidental |
| g.B.complete_outside_radius | 22 | 26 | +18.2% | incidental |
| g.B.left_behind | 22 | 26 | +18.2% | incidental |
| g.B.stops | 1761 | 1671 | -5.1% | incidental |
| gate.strait.files_x100 | 216 | 296 | +37.0% | intended |
| gate.strait.samples | 6537 | 6921 | +5.9% | incidental |
| gate.strait.spread_x100 | 399 | 480 | +20.3% | incidental |
| parked_held | 3564 | 3320 | -6.8% | incidental |
| region.east.inside | 59 | 54 | -8.5% | incidental |
| reversals | 301 | 248 | -17.6% | intended |
| side.barrier.high | 59 | 54 | -8.5% | incidental |
| side.barrier.low | 21 | 26 | +23.8% | incidental |
| statue_ticks | 44914 | 39429 | -12.2% | incidental |
| stopped_permille | 288 | 271 | -5.9% | intended |
| transit_held_permille | 289 | 273 | -5.5% | incidental |
| waiting_held | 45016 | 38597 | -14.3% | incidental |
| wall_samples | 19054 | 18081 | -5.1% | incidental |
| work.completion_dist_3.total | 12 | 10 | -16.7% | incidental |
| work.completion_dist_5.total | 18 | 17 | -5.6% | incidental |
| work.completion_dist_6.max | 2 | 3 | +50.0% | incidental |
| work.completion_dist_6.total | 18 | 22 | +22.2% | incidental |
| work.completion_dist_sum.max | 97 | 147 | +51.5% | incidental |
| work.completion_dist_sum.total | 1500 | 1651 | +10.1% | incidental |
| work.contact_arrivals.total | 52 | 57 | +9.6% | incidental |
| work.crowd_window_ring_cells.max | 1560 | 1480 | -5.1% | incidental |
| work.crowd_window_ring_cells.total | 104240 | 90560 | -13.1% | incidental |
| work.detour_cells.total | 120348 | 103854 | -13.7% | incidental |
| work.detours.total | 360 | 296 | -17.8% | incidental |
| work.held_rechecks.p99 | 7 | 10 | +42.9% | incidental |
| work.held_rechecks.total | 6625 | 7063 | +6.6% | incidental |
| work.move_calls_by_state_1.total | 173917 | 163611 | -5.9% | incidental |
| work.move_calls_by_state_2.total | 67713 | 59899 | -11.5% | incidental |
| work.outside_area_completions.max | 2 | 4 | +100.0% | incidental |
| work.outside_area_completions.total | 23 | 28 | +21.7% | incidental |
| work.pass_scan_cells.max | 810 | 864 | +6.7% | incidental |
| work.pass_scan_cells.p99 | 540 | 594 | +10.0% | incidental |
| work.pass_scans.max | 15 | 16 | +6.7% | incidental |
| work.pass_scans.p99 | 10 | 11 | +10.0% | incidental |
| work.pass_scans_skipped.total | 112177 | 105757 | -5.7% | incidental |
| work.rechoice_bfs_cells.total | 26368 | 28343 | +7.5% | incidental |
| work.slot_search_cells.total | 32515 | 34490 | +6.1% | incidental |

## strait-4x24 (36 keys)

| key | step-0 sim | exit | change | kind |
|---|---|---|---|---|
| back | 16 | 19 | +18.8% | incidental |
| backward | 48 | 24 | -50.0% | incidental |
| churn.bin1 | 89554 | 100970 | +12.7% | incidental |
| churn.bin2 | 2601 | 4635 | +78.2% | incidental |
| crawl_samples | 507 | 561 | +10.7% | incidental |
| engagement_on_flowing | 9 | 7 | -22.2% | incidental |
| flip_rate_per30_permille | 120 | 111 | -7.5% | incidental |
| flips | 197 | 177 | -10.2% | incidental |
| follow_chain_max | 7 | 8 | +14.3% | incidental |
| follow_chain_mean_x100 | 249 | 225 | -9.6% | incidental |
| g.B.complete_dist_max | 19 | 18 | -5.3% | incidental |
| g.B.complete_dist_median | 10 | 8 | -20.0% | incidental |
| g.B.complete_outside_radius | 2 | 1 | -50.0% | incidental |
| g.B.left_behind | 2 | 1 | -50.0% | incidental |
| g.B.stops | 189 | 176 | -6.9% | incidental |
| parked_held | 122 | 136 | +11.5% | incidental |
| sideways | 3161 | 3394 | +7.4% | incidental |
| statue_ticks | 5488 | 5947 | +8.4% | incidental |
| stopped_permille | 106 | 100 | -5.7% | intended |
| transit_held_permille | 111 | 101 | -9.0% | incidental |
| work.arrivals.max | 2 | 1 | -50.0% | incidental |
| work.completion_dist_2.total | 2 | 3 | +50.0% | incidental |
| work.completion_dist_sum.max | 22 | 18 | -18.2% | incidental |
| work.contact_arrivals.max | 2 | 1 | -50.0% | incidental |
| work.contact_arrivals.total | 9 | 6 | -33.3% | incidental |
| work.crowd_window_ring_cells.total | 12144 | 13008 | +7.1% | incidental |
| work.detour_cells.max | 447 | 549 | +22.8% | incidental |
| work.detours.total | 27 | 29 | +7.4% | incidental |
| work.group_loop_iters.total | 155 | 166 | +7.1% | incidental |
| work.held_rechecks.max | 2 | 1 | -50.0% | incidental |
| work.held_rechecks.p99 | 0 | 1 | new | incidental |
| work.held_rechecks.total | 42 | 141 | +235.7% | incidental |
| work.mission_arrivals_1.max | 2 | 1 | -50.0% | incidental |
| work.rechoice_bfs_cells.total | 13008 | 10754 | -17.3% | incidental |
| work.slides.total | 796 | 753 | -5.4% | incidental |
| work.slot_search_cells.total | 17331 | 15077 | -13.0% | incidental |

574 keys moved by more than 5% in 13 files.
