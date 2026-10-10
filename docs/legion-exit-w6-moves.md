# W6 land: every key that moved by more than 5% against the W4 land head

Base: the W4 land sweep (`6333e38d`, protocol 242: all 91 scenario files, both modes, gate offsets; Windows). Candidate:
`task-w6-land` (protocol 243), the same sweep. Seven Legion rows have a different state hash; the other 175 result lines
(all Retail rows included) are identical in every key. Values are the runner's gated medians. A key that did not exist in
the base shows `None`. *intended* = the step the row exists for (step 4 merged rejoin for the dead-end squads; steps 2, 5, 6 for mixed2);
*incidental (within band / licensed)* = a follow-on of the same behaviour change, inside its band or carried by an `accepted_regressions` reason.

| key | old | new | kind |
|---|---|---|---|
| deadend-w4-n120-closed-squad/legion/churn.bin2 | 150450 | 132737 | incidental (within band / licensed) |
| deadend-w4-n120-closed-squad/legion/churn.bin3 | 24574 | 6000 | incidental (within band / licensed) |
| deadend-w4-n120-closed-squad/legion/churn.bin4 | 24509 | 6000 | incidental (within band / licensed) |
| deadend-w4-n120-closed-squad/legion/churn.bin5 | 8149 | 2000 | incidental (within band / licensed) |
| deadend-w4-n120-closed-squad/legion/follow_chain_mean_x100 | 1351 | 1419 | incidental (within band / licensed) |
| deadend-w4-n120-closed-squad/legion/g.A.arrived | 8 | 10 | intended |
| deadend-w4-n120-closed-squad/legion/open_still900 | 2 | 0 | intended |
| deadend-w4-n120-closed-squad/legion/parked_held | 21005 | 14183 | intended |
| deadend-w4-n120-closed-squad/legion/region.goal.inside | 1 | 3 | intended |
| deadend-w4-n120-closed-squad/legion/still900_ever | 2 | 0 | intended |
| deadend-w4-n120-closed-squad/legion/work.crowd_window_ring_cells.total | 83008 | 75904 | incidental (within band / licensed) |
| deadend-w4-n120-closed-squad/legion/work.field_work.total | 80160 | 74640 | incidental (within band / licensed) |
| deadend-w4-n120-closed-squad/legion/work.field_work_first_slot.total | 80160 | 74640 | incidental (within band / licensed) |
| deadend-w4-n120-closed-squad/legion/work.fields_built.total | 2 | 1 | intended |
| deadend-w4-n120-closed-squad/legion/work.fields_started_by_kind_1.total | 2 | 1 | intended |
| deadend-w4-n120-closed-squad/legion/work.group_loop_iters.total | 492 | 362 | incidental (within band / licensed) |
| deadend-w4-n120-closed-squad/legion/work.groups.total | 2 | 1 | intended |
| deadend-w4-n120-closed-squad/legion/work.held_rechecks.total | 28566 | 19953 | intended |
| deadend-w4-n120-closed-squad/legion/work.move_calls_by_state_2.total | 98998 | 93659 | intended |
| deadend-w4-n120-closed-squad/legion/work.sched_group_visits.total | 3 | 1 | intended |
| deadend-w4-n120-closed-squad/legion/work.slot_search_cells.p99 | 2 | 1 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/backward | 131 | 123 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/churn.bin2 | 105053 | 65099 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/churn.bin3 | 34196 | 6000 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/churn.bin4 | 34435 | 6000 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/churn.bin5 | 11400 | 2000 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/decision_samples | 28419 | 26560 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/detour_permille | 124 | 115 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/flip_rate_per30_permille | 268 | 284 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/g.A.arrived | 19 | 24 | intended |
| deadend-w4-n120-room-squad/legion/open_still900 | 4 | 0 | intended |
| deadend-w4-n120-room-squad/legion/parked_held | 18840 | 2494 | intended |
| deadend-w4-n120-room-squad/legion/region.goal.inside | 6 | 9 | intended |
| deadend-w4-n120-room-squad/legion/spacing_samples | 28419 | 26560 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/still900_ever | 4 | 0 | intended |
| deadend-w4-n120-room-squad/legion/stopped_permille | 316 | 268 | intended |
| deadend-w4-n120-room-squad/legion/work.crowd_window_ring_cells.total | 79040 | 65312 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/work.fields_built.total | 2 | 1 | intended |
| deadend-w4-n120-room-squad/legion/work.fields_started_by_kind_1.total | 2 | 1 | intended |
| deadend-w4-n120-room-squad/legion/work.group_loop_iters.total | 496 | 356 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/work.groups.total | 2 | 1 | intended |
| deadend-w4-n120-room-squad/legion/work.held_rechecks.total | 21942 | 5312 | intended |
| deadend-w4-n120-room-squad/legion/work.move_calls_by_state_2.total | 89232 | 73281 | intended |
| deadend-w4-n120-room-squad/legion/work.pass_scans_skipped.total | 166623 | 156431 | incidental (within band / licensed) |
| deadend-w4-n120-room-squad/legion/work.sched_group_visits.total | 3 | 1 | intended |
| deadend-w4-n120-room-squad/legion/work.slot_search_cells.p99 | 8 | 6 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/aim_reversals | 16 | 15 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/churn.bin2 | 15862 | 2401 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/churn.bin3 | 11856 | 2000 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/churn.bin4 | 11923 | 2000 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/churn.bin5 | 3955 | 670 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/crawl_samples | 360 | 337 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/decision_samples | 9690 | 8534 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/detour_permille | 37 | 33 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/flip_rate_per30_permille | 79 | 87 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/follow_chain_mean_x100 | 1641 | 1806 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/g.A.arrived | 4 | 6 | intended |
| deadend-w4-n40-closed-squad/legion/g.A.left_behind | 36 | 34 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/open_still900 | 2 | 0 | intended |
| deadend-w4-n40-closed-squad/legion/parked_held | 10805 | 1470 | intended |
| deadend-w4-n40-closed-squad/legion/region.goal.inside | 3 | 4 | intended |
| deadend-w4-n40-closed-squad/legion/spacing_samples | 9690 | 8534 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/statue_ticks | 2895 | 2678 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/still900_ever | 2 | 0 | intended |
| deadend-w4-n40-closed-squad/legion/stopped_permille | 217 | 129 | intended |
| deadend-w4-n40-closed-squad/legion/work.crowd_window_ring_cells.p99 | 64 | 0 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/work.crowd_window_ring_cells.total | 14336 | 7232 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/work.detour_cells.p99 | 8 | 3 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/work.fields_built.total | 2 | 1 | intended |
| deadend-w4-n40-closed-squad/legion/work.fields_started_by_kind_1.total | 2 | 1 | intended |
| deadend-w4-n40-closed-squad/legion/work.group_loop_iters.total | 332 | 163 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/work.groups.total | 2 | 1 | intended |
| deadend-w4-n40-closed-squad/legion/work.held_rechecks.total | 11595 | 2138 | intended |
| deadend-w4-n40-closed-squad/legion/work.move_calls_by_state_2.total | 20765 | 10733 | intended |
| deadend-w4-n40-closed-squad/legion/work.moves.total | 91421 | 84329 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/work.pass_scans_skipped.total | 73317 | 66337 | incidental (within band / licensed) |
| deadend-w4-n40-closed-squad/legion/work.sched_group_visits.total | 3 | 1 | intended |
| deadend-w4-n40-closed-squad/legion/work.still_units_processed.total | 7442 | 7830 | intended |
| deadend-w6-n120-closed-squad/legion/back | 37 | 34 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/backward | 83 | 77 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/churn.bin2 | 44407 | 18526 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/churn.bin3 | 24983 | 6000 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/churn.bin4 | 25079 | 6000 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/churn.bin5 | 8329 | 2000 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/detour_permille | 52 | 47 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/flip_rate_per30_permille | 157 | 167 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/follow_chain_mean_x100 | 1536 | 1707 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/g.A.arrived | 17 | 22 | intended |
| deadend-w6-n120-closed-squad/legion/open_still900 | 3 | 0 | intended |
| deadend-w6-n120-closed-squad/legion/parked_held | 12341 | 562 | intended |
| deadend-w6-n120-closed-squad/legion/region.goal.inside | 3 | 6 | intended |
| deadend-w6-n120-closed-squad/legion/statue_ticks | 14452 | 13445 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/still900_ever | 3 | 0 | intended |
| deadend-w6-n120-closed-squad/legion/stopped_permille | 206 | 160 | intended |
| deadend-w6-n120-closed-squad/legion/work.crowd_window_ring_cells.p99 | 128 | 64 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/work.crowd_window_ring_cells.total | 36000 | 23328 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/work.fields_built.total | 2 | 1 | intended |
| deadend-w6-n120-closed-squad/legion/work.fields_started_by_kind_1.total | 2 | 1 | intended |
| deadend-w6-n120-closed-squad/legion/work.group_loop_iters.total | 494 | 344 | incidental (within band / licensed) |
| deadend-w6-n120-closed-squad/legion/work.groups.total | 2 | 1 | intended |
| deadend-w6-n120-closed-squad/legion/work.held_rechecks.p99 | 5 | 4 | intended |
| deadend-w6-n120-closed-squad/legion/work.held_rechecks.total | 14669 | 2265 | intended |
| deadend-w6-n120-closed-squad/legion/work.move_calls_by_state_2.total | 55213 | 39178 | intended |
| deadend-w6-n120-closed-squad/legion/work.sched_group_visits.total | 3 | 1 | intended |
| deadend-w6-n120-room-squad/legion/churn.bin2 | 31773 | 21360 | incidental (within band / licensed) |
| deadend-w6-n120-room-squad/legion/churn.bin3 | 8631 | 6000 | incidental (within band / licensed) |
| deadend-w6-n120-room-squad/legion/churn.bin4 | 8663 | 6000 | incidental (within band / licensed) |
| deadend-w6-n120-room-squad/legion/churn.bin5 | 2877 | 2000 | incidental (within band / licensed) |
| deadend-w6-n120-room-squad/legion/detour_permille | 72 | 67 | incidental (within band / licensed) |
| deadend-w6-n120-room-squad/legion/g.A.arrived | 31 | 34 | intended |
| deadend-w6-n120-room-squad/legion/open_still900 | 1 | 0 | intended |
| deadend-w6-n120-room-squad/legion/parked_held | 4876 | 1944 | intended |
| deadend-w6-n120-room-squad/legion/region.goal.inside | 12 | 14 | intended |
| deadend-w6-n120-room-squad/legion/still900_ever | 1 | 0 | intended |
| deadend-w6-n120-room-squad/legion/stopped_permille | 227 | 205 | intended |
| deadend-w6-n120-room-squad/legion/work.crowd_window_ring_cells.total | 48832 | 43360 | incidental (within band / licensed) |
| deadend-w6-n120-room-squad/legion/work.field_work.total | 81144 | 76992 | incidental (within band / licensed) |
| deadend-w6-n120-room-squad/legion/work.fields_built.total | 2 | 1 | intended |
| deadend-w6-n120-room-squad/legion/work.fields_started_by_kind_1.total | 2 | 1 | intended |
| deadend-w6-n120-room-squad/legion/work.group_loop_iters.total | 489 | 342 | incidental (within band / licensed) |
| deadend-w6-n120-room-squad/legion/work.groups.total | 2 | 1 | intended |
| deadend-w6-n120-room-squad/legion/work.held_rechecks.total | 7329 | 4540 | intended |
| deadend-w6-n120-room-squad/legion/work.move_calls_by_state_2.total | 56507 | 50041 | intended |
| deadend-w6-n120-room-squad/legion/work.pass_scan_cells.total | 182795 | 169910 | incidental (within band / licensed) |
| deadend-w6-n120-room-squad/legion/work.pass_scans.total | 7622 | 7086 | incidental (within band / licensed) |
| deadend-w6-n120-room-squad/legion/work.sched_group_visits.total | 3 | 1 | intended |
| mixed2/legion/back | 116 | 126 | incidental (within band / licensed) |
| mixed2/legion/churn.bin4 | 117318 | 105487 | incidental (within band / licensed) |
| mixed2/legion/flip_rate_per30_permille | 64 | 68 | incidental (within band / licensed) |
| mixed2/legion/flips | 646 | 698 | incidental (within band / licensed) |
| mixed2/legion/g.F.hover_max | 56 | 62 | intended |
| mixed2/legion/g.F.illegal_overlap_ticks | 3746 | 0 | intended |
| mixed2/legion/g.F.relifts | 16 | 17 | intended |
| mixed2/legion/g.F.relifts_max | 1 | 2 | intended |
| mixed2/legion/g.F.takeoffs_max | 2 | 3 | intended |
| mixed2/legion/gate.mid.crossings | 24 | 35 | incidental (within band / licensed) |
| mixed2/legion/gate.mid.files_x100 | 291 | 307 | incidental (within band / licensed) |
| mixed2/legion/spins | 101 | 0 | intended |
| mixed2/legion/statue_ticks | 17929 | 16782 | incidental (within band / licensed) |
| mixed2/legion/waiting_held | 9112 | 9720 | incidental (within band / licensed) |
| mixed2/legion/waiting_no_progress | 633 | 665 | incidental (within band / licensed) |
| mixed2/legion/work.completion_dist_1.total | 1 | 2 | incidental (within band / licensed) |
| mixed2/legion/work.completion_dist_2.total | 11 | 10 | incidental (within band / licensed) |
| mixed2/legion/work.completion_dist_3.max | 2 | 3 | incidental (within band / licensed) |
| mixed2/legion/work.completion_dist_4.max | 3 | 4 | incidental (within band / licensed) |
| mixed2/legion/work.completion_dist_5.total | 3 | 5 | incidental (within band / licensed) |
| mixed2/legion/work.completion_dist_sum.max | 45 | 40 | incidental (within band / licensed) |
| mixed2/legion/work.crowd_window_ring_cells.max | 640 | 736 | incidental (within band / licensed) |
| mixed2/legion/work.crowd_window_ring_cells.p99 | 63 | 39 | incidental (within band / licensed) |
| mixed2/legion/work.detours.p99 | 0 | 1 | incidental (within band / licensed) |
| mixed2/legion/work.detours.total | 85 | 93 | incidental (within band / licensed) |
| mixed2/legion/work.formation_ring_cells.max | 2479 | 2351 | incidental (within band / licensed) |
| mixed2/legion/work.held_rechecks.max | 3 | 4 | incidental (within band / licensed) |
| mixed2/legion/work.held_rechecks.total | 466 | 576 | incidental (within band / licensed) |
| mixed2/legion/work.lift_members_skipped.p99 | 3 | 4 | intended |
| mixed2/legion/work.lift_members_skipped.total | 922 | 1226 | intended |
| mixed2/legion/work.lift_members_walked.total | 3270 | 3615 | intended |
| mixed2/legion/work.lift_target_polls.max | None | 1 | intended |
| mixed2/legion/work.lift_target_polls.total | None | 22 | intended |
| mixed2/legion/work.lifts.max | None | 5 | intended |
| mixed2/legion/work.lifts.total | None | 30 | intended |
| mixed2/legion/work.move_calls_by_state_2.p99 | 15 | 16 | incidental (within band / licensed) |
| mixed2/legion/work.move_calls_by_state_2.total | 14740 | 15817 | incidental (within band / licensed) |
| mixed2/legion/work.outside_area_completions.total | 10 | 9 | incidental (within band / licensed) |
| mixed2/legion/work.slot_search_cells.max | 2487 | 2351 | incidental (within band / licensed) |

165 keys; no Retail key and no key of a hash-identical row.
