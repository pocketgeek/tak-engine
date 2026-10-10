# W7 exit: every key moved by more than 5% against the step-0 sim

Base = `task-w7-s0` (`7dbc218e`): the W7 step-0 sim (instruments and fixtures only, protocol 245, sim = `2d671644`); new = `task-w7-land`
(the W7 steps 1-3, 5-7 chain on `origin/main` `9564266f`, protocol 246). Both swept on Windows (optimized Debug), all 122 files, both modes,
the gate offsets (keys as the run lines report them), serial == workers. Only 11 Legion scenarios change state at all: `aware-cross`,
`aware-cross-behind`, `aware-headon`, `aware-seen`, `aware-attack`, `crosslong`, `densehead`, `ringcross`, `unreach-200`, `mazeapproach` and
`battle-assault`; every other scenario (`aware-unseen`, the awareness-off control, included) and every Retail line is hash-identical to the
base. *intended*: the route-behind, awareness and contact keys of the aware fixtures, `crosslong`, and the `unreach-200` / `densehead` theme
keys; *incidental (within band)*: everything else, which is the cost of the intended change (docs/legion-exit-tables.md, "W7 exit").

| key | old | new | change | kind |
|---|---|---|---|---|
| aware-attack/legion/aim_reversals | 42 | 46 | +9.5% | incidental (within band) |
| aware-attack/legion/back | 5 | 20 | +300.0% | incidental (within band) |
| aware-attack/legion/backward | 105 | 78 | -25.7% | incidental (within band) |
| aware-attack/legion/churn.bin0 | 1481428 | 1657932 | +11.9% | incidental (within band) |
| aware-attack/legion/churn.bin1 | 53561 | 56514 | +5.5% | incidental (within band) |
| aware-attack/legion/click.a00.done | 2117 | 2387 | +12.8% | intended |
| aware-attack/legion/click.b00.done | 2150 | 2274 | +5.8% | intended |
| aware-attack/legion/click.b00.t90 | 2117 | 1952 | -7.8% | intended |
| aware-attack/legion/contact_other_permille | 548 | 508 | -7.3% | intended |
| aware-attack/legion/crawl_samples | 510 | 626 | +22.7% | incidental (within band) |
| aware-attack/legion/detour_permille | 27 | 32 | +18.5% | incidental (within band) |
| aware-attack/legion/engagement_on_flowing | 47 | 44 | -6.4% | incidental (within band) |
| aware-attack/legion/flip_rate_per30_permille | 97 | 69 | -28.9% | incidental (within band) |
| aware-attack/legion/flips | 271 | 188 | -30.6% | incidental (within band) |
| aware-attack/legion/g.a00.done | 2035 | 1922 | -5.6% | incidental (within band) |
| aware-attack/legion/g.a00.t50 | 2035 | 1922 | -5.6% | incidental (within band) |
| aware-attack/legion/g.a01.done | 1996 | 1780 | -10.8% | incidental (within band) |
| aware-attack/legion/g.a01.t50 | 1996 | 1780 | -10.8% | incidental (within band) |
| aware-attack/legion/g.a01.t90 | 1990 | 1830 | -8.0% | incidental (within band) |
| aware-attack/legion/g.a02.t90 | 2072 | 1895 | -8.5% | incidental (within band) |
| aware-attack/legion/g.a03.done | 2018 | 1722 | -14.7% | incidental (within band) |
| aware-attack/legion/g.a03.t50 | 2018 | 1722 | -14.7% | incidental (within band) |
| aware-attack/legion/g.a04.done | 1802 | 1917 | +6.4% | incidental (within band) |
| aware-attack/legion/g.a04.t50 | 1802 | 1917 | +6.4% | incidental (within band) |
| aware-attack/legion/g.a08.done | 1720 | 1888 | +9.8% | incidental (within band) |
| aware-attack/legion/g.a08.t50 | 1720 | 1888 | +9.8% | incidental (within band) |
| aware-attack/legion/g.a08.t90 | 1783 | 1909 | +7.1% | incidental (within band) |
| aware-attack/legion/g.a12.complete_dist_max | 6 | 5 | -16.7% | incidental (within band) |
| aware-attack/legion/g.a12.complete_dist_median | 6 | 5 | -16.7% | incidental (within band) |
| aware-attack/legion/g.a12.done | 1802 | 1709 | -5.2% | incidental (within band) |
| aware-attack/legion/g.a12.t50 | 1802 | 1709 | -5.2% | incidental (within band) |
| aware-attack/legion/g.a13.complete_dist_max | 4 | 3 | -25.0% | incidental (within band) |
| aware-attack/legion/g.a13.complete_dist_median | 4 | 3 | -25.0% | incidental (within band) |
| aware-attack/legion/g.a13.done | 1757 | 1982 | +12.8% | incidental (within band) |
| aware-attack/legion/g.a13.t50 | 1757 | 1982 | +12.8% | incidental (within band) |
| aware-attack/legion/g.a13.t90 | 1767 | 1933 | +9.4% | incidental (within band) |
| aware-attack/legion/g.b09.t90 | 1723 | 1847 | +7.2% | incidental (within band) |
| aware-attack/legion/g.b11.t90 | 1800 | 1938 | +7.7% | incidental (within band) |
| aware-attack/legion/g.b13.done | 1595 | 1689 | +5.9% | incidental (within band) |
| aware-attack/legion/g.b13.t50 | 1595 | 1689 | +5.9% | incidental (within band) |
| aware-attack/legion/g.b13.t90 | 1504 | 1622 | +7.8% | incidental (within band) |
| aware-attack/legion/g.b14.complete_dist_max | 2 | 1 | -50.0% | incidental (within band) |
| aware-attack/legion/g.b14.complete_dist_median | 2 | 1 | -50.0% | incidental (within band) |
| aware-attack/legion/g.b18.done | 1594 | 1729 | +8.5% | incidental (within band) |
| aware-attack/legion/g.b18.t50 | 1594 | 1729 | +8.5% | incidental (within band) |
| aware-attack/legion/g.b19.done | 1963 | 1676 | -14.6% | incidental (within band) |
| aware-attack/legion/g.b19.t50 | 1963 | 1676 | -14.6% | incidental (within band) |
| aware-attack/legion/g.b20.t90 | 1723 | 1853 | +7.5% | incidental (within band) |
| aware-attack/legion/g.b21.complete_dist_max | 5 | 3 | -40.0% | incidental (within band) |
| aware-attack/legion/g.b21.complete_dist_median | 5 | 3 | -40.0% | incidental (within band) |
| aware-attack/legion/g.b22.done | 1963 | 1818 | -7.4% | incidental (within band) |
| aware-attack/legion/g.b22.t50 | 1963 | 1818 | -7.4% | incidental (within band) |
| aware-attack/legion/pair.contacts.contacts | 514 | 388 | -24.5% | intended |
| aware-attack/legion/pair.contacts.permille_x100 | 550 | 410 | -25.5% | intended |
| aware-attack/legion/parked_held | 6 | 51 | +750.0% | incidental (within band) |
| aware-attack/legion/reversals | 5 | 4 | -20.0% | intended |
| aware-attack/legion/sideways | 1189 | 1065 | -10.4% | incidental (within band) |
| aware-attack/legion/statue_ticks | 5255 | 6526 | +24.2% | incidental (within band) |
| aware-attack/legion/stop_go | 241 | 211 | -12.4% | incidental (within band) |
| aware-attack/legion/stopped_permille | 68 | 56 | -17.6% | intended |
| aware-attack/legion/waiting_held | 3745 | 3205 | -14.4% | incidental (within band) |
| aware-attack/legion/wall_near_samples | 99 | 108 | +9.1% | incidental (within band) |
| aware-attack/legion/work.arrivals.max | 3 | 2 | -33.3% | incidental (within band) |
| aware-attack/legion/work.completion_dist_1.total | 7 | 6 | -14.3% | incidental (within band) |
| aware-attack/legion/work.completion_dist_2.max | 1 | 2 | +100.0% | incidental (within band) |
| aware-attack/legion/work.completion_dist_4.max | 1 | 0 | -100.0% | incidental (within band) |
| aware-attack/legion/work.completion_dist_4.total | 2 | 0 | -100.0% | incidental (within band) |
| aware-attack/legion/work.completion_dist_sum.max | 15 | 10 | -33.3% | incidental (within band) |
| aware-attack/legion/work.contact_arrivals.max | 3 | 2 | -33.3% | incidental (within band) |
| aware-attack/legion/work.contact_arrivals.total | 10 | 6 | -40.0% | incidental (within band) |
| aware-attack/legion/work.crowd_window_ring_cells.max | 704 | 768 | +9.1% | incidental (within band) |
| aware-attack/legion/work.crowd_window_ring_cells.p99 | 0 | 32 | new | incidental (within band) |
| aware-attack/legion/work.crowd_window_ring_cells.total | 6112 | 7136 | +16.8% | incidental (within band) |
| aware-attack/legion/work.detour_cells.max | 1143 | 1074 | -6.0% | incidental (within band) |
| aware-attack/legion/work.detour_cells.p99 | 419 | 99 | -76.4% | incidental (within band) |
| aware-attack/legion/work.detour_cells.total | 17938 | 16888 | -5.9% | incidental (within band) |
| aware-attack/legion/work.held_rechecks.p99 | 1 | 2 | +100.0% | incidental (within band) |
| aware-attack/legion/work.held_rechecks.total | 127 | 182 | +43.3% | incidental (within band) |
| aware-attack/legion/work.holds.max | 5 | 3 | -40.0% | incidental (within band) |
| aware-attack/legion/work.holds.p99 | 2 | 1 | -50.0% | incidental (within band) |
| aware-attack/legion/work.holds.total | 249 | 213 | -14.5% | incidental (within band) |
| aware-attack/legion/work.legion_total.p99 | 1726 | 2046 | +18.5% | incidental (within band) |
| aware-attack/legion/work.legion_total.total | 1536335 | 1714446 | +11.6% | incidental (within band) |
| aware-attack/legion/work.line_sweeps.max | 89 | 94 | +5.6% | incidental (within band) |
| aware-attack/legion/work.line_sweeps.total | 40412 | 68828 | +70.3% | incidental (within band) |
| aware-attack/legion/work.mission_arrivals_1.max | 1 | 2 | +100.0% | incidental (within band) |
| aware-attack/legion/work.move_calls_by_state_2.max | 24 | 21 | -12.5% | incidental (within band) |
| aware-attack/legion/work.move_calls_by_state_2.p99 | 20 | 18 | -10.0% | incidental (within band) |
| aware-attack/legion/work.move_calls_by_state_2.total | 5447 | 4773 | -12.4% | incidental (within band) |
| aware-attack/legion/work.pass_scan_cells.max | 556 | 591 | +6.3% | incidental (within band) |
| aware-attack/legion/work.pass_scan_cells.p99 | 482 | 507 | +5.2% | incidental (within band) |
| aware-attack/legion/work.rechoice_bfs_cells.max | 1038 | 765 | -26.3% | incidental (within band) |
| aware-attack/legion/work.rechoice_bfs_cells.total | 4294 | 2542 | -40.8% | incidental (within band) |
| aware-attack/legion/work.slides.total | 408 | 386 | -5.4% | incidental (within band) |
| aware-attack/legion/work.slot_search_cells.max | 1353 | 1258 | -7.0% | incidental (within band) |
| aware-attack/legion/work.slot_search_cells.total | 8115 | 6363 | -21.6% | incidental (within band) |
| aware-attack/legion/work.trace_cells.p99 | 1614 | 1986 | +23.0% | incidental (within band) |
| aware-attack/legion/work.trace_cells.total | 879663 | 1070631 | +21.7% | incidental (within band) |
| aware-cross/legion/aim_reversals | 55 | 43 | -21.8% | incidental (within band) |
| aware-cross/legion/back | 10 | 13 | +30.0% | incidental (within band) |
| aware-cross/legion/backward | 20 | 14 | -30.0% | incidental (within band) |
| aware-cross/legion/churn.bin0 | 1455152 | 1982137 | +36.2% | incidental (within band) |
| aware-cross/legion/churn.bin1 | 36303 | 5276 | -85.5% | incidental (within band) |
| aware-cross/legion/click.a00.done | 2252 | 1530 | -32.1% | intended |
| aware-cross/legion/click.a00.t50 | 1736 | 1455 | -16.2% | intended |
| aware-cross/legion/click.a00.t90 | 2027 | 1517 | -25.2% | intended |
| aware-cross/legion/click.b00.done | 1540 | 1812 | +17.7% | intended |
| aware-cross/legion/click.b00.t50 | 1128 | 1367 | +21.2% | intended |
| aware-cross/legion/click.b00.t90 | 1332 | 1542 | +15.8% | intended |
| aware-cross/legion/crawl_samples | 502 | 165 | -67.1% | incidental (within band) |
| aware-cross/legion/detour_permille | 16 | 79 | +393.8% | incidental (within band) |
| aware-cross/legion/engagement_on_flowing | 30 | 14 | -53.3% | incidental (within band) |
| aware-cross/legion/flip_rate_per30_permille | 81 | 12 | -85.2% | incidental (within band) |
| aware-cross/legion/flips | 183 | 27 | -85.2% | incidental (within band) |
| aware-cross/legion/g.a00.done | 1766 | 1520 | -13.9% | incidental (within band) |
| aware-cross/legion/g.a00.t50 | 1766 | 1520 | -13.9% | incidental (within band) |
| aware-cross/legion/g.a00.t90 | 1744 | 1520 | -12.8% | incidental (within band) |
| aware-cross/legion/g.a01.done | 1807 | 1510 | -16.4% | incidental (within band) |
| aware-cross/legion/g.a01.t50 | 1807 | 1510 | -16.4% | incidental (within band) |
| aware-cross/legion/g.a01.t90 | 1753 | 1510 | -13.9% | incidental (within band) |
| aware-cross/legion/g.a02.done | 1795 | 1500 | -16.4% | incidental (within band) |
| aware-cross/legion/g.a02.t50 | 1795 | 1500 | -16.4% | incidental (within band) |
| aware-cross/legion/g.a02.t90 | 1641 | 1500 | -8.6% | incidental (within band) |
| aware-cross/legion/g.a03.done | 1703 | 1490 | -12.5% | incidental (within band) |
| aware-cross/legion/g.a03.t50 | 1703 | 1490 | -12.5% | incidental (within band) |
| aware-cross/legion/g.a03.t90 | 1632 | 1490 | -8.7% | incidental (within band) |
| aware-cross/legion/g.a04.done | 1638 | 1482 | -9.5% | incidental (within band) |
| aware-cross/legion/g.a04.t50 | 1638 | 1482 | -9.5% | incidental (within band) |
| aware-cross/legion/g.a04.t90 | 1600 | 1482 | -7.4% | incidental (within band) |
| aware-cross/legion/g.a05.done | 1549 | 1437 | -7.2% | incidental (within band) |
| aware-cross/legion/g.a05.t50 | 1549 | 1437 | -7.2% | incidental (within band) |
| aware-cross/legion/g.a05.t90 | 1555 | 1437 | -7.6% | incidental (within band) |
| aware-cross/legion/g.a06.done | 1860 | 1463 | -21.3% | incidental (within band) |
| aware-cross/legion/g.a06.t50 | 1860 | 1463 | -21.3% | incidental (within band) |
| aware-cross/legion/g.a06.t90 | 1860 | 1463 | -21.3% | incidental (within band) |
| aware-cross/legion/g.a07.done | 1763 | 1455 | -17.5% | incidental (within band) |
| aware-cross/legion/g.a07.t50 | 1763 | 1455 | -17.5% | incidental (within band) |
| aware-cross/legion/g.a07.t90 | 1712 | 1455 | -15.0% | incidental (within band) |
| aware-cross/legion/g.a08.done | 1787 | 1447 | -19.0% | incidental (within band) |
| aware-cross/legion/g.a08.t50 | 1787 | 1447 | -19.0% | incidental (within band) |
| aware-cross/legion/g.a08.t90 | 1787 | 1447 | -19.0% | incidental (within band) |
| aware-cross/legion/g.a09.done | 1694 | 1379 | -18.6% | incidental (within band) |
| aware-cross/legion/g.a09.t50 | 1694 | 1379 | -18.6% | incidental (within band) |
| aware-cross/legion/g.a09.t90 | 1689 | 1379 | -18.4% | incidental (within band) |
| aware-cross/legion/g.a10.done | 1604 | 1370 | -14.6% | incidental (within band) |
| aware-cross/legion/g.a10.t50 | 1604 | 1370 | -14.6% | incidental (within band) |
| aware-cross/legion/g.a10.t90 | 1604 | 1370 | -14.6% | incidental (within band) |
| aware-cross/legion/g.a11.done | 1476 | 1361 | -7.8% | incidental (within band) |
| aware-cross/legion/g.a11.t50 | 1476 | 1361 | -7.8% | incidental (within band) |
| aware-cross/legion/g.a11.t90 | 1495 | 1361 | -9.0% | incidental (within band) |
| aware-cross/legion/g.a12.complete_dist_max | 7 | 5 | -28.6% | incidental (within band) |
| aware-cross/legion/g.a12.complete_dist_median | 7 | 5 | -28.6% | incidental (within band) |
| aware-cross/legion/g.a12.done | 1937 | 1517 | -21.7% | incidental (within band) |
| aware-cross/legion/g.a12.t50 | 1937 | 1517 | -21.7% | incidental (within band) |
| aware-cross/legion/g.a12.t90 | 1937 | 1517 | -21.7% | incidental (within band) |
| aware-cross/legion/g.a13.complete_dist_max | 4 | 3 | -25.0% | incidental (within band) |
| aware-cross/legion/g.a13.complete_dist_median | 4 | 3 | -25.0% | incidental (within band) |
| aware-cross/legion/g.a13.done | 1837 | 1507 | -18.0% | incidental (within band) |
| aware-cross/legion/g.a13.t50 | 1837 | 1507 | -18.0% | incidental (within band) |
| aware-cross/legion/g.a13.t90 | 1837 | 1507 | -18.0% | incidental (within band) |
| aware-cross/legion/g.a14.complete_dist_max | 2 | 1 | -50.0% | incidental (within band) |
| aware-cross/legion/g.a14.complete_dist_median | 2 | 1 | -50.0% | incidental (within band) |
| aware-cross/legion/g.a14.done | 1847 | 1498 | -18.9% | incidental (within band) |
| aware-cross/legion/g.a14.t50 | 1847 | 1498 | -18.9% | incidental (within band) |
| aware-cross/legion/g.a14.t90 | 1817 | 1498 | -17.6% | incidental (within band) |
| aware-cross/legion/g.a15.done | 1814 | 1467 | -19.1% | incidental (within band) |
| aware-cross/legion/g.a15.t50 | 1814 | 1467 | -19.1% | incidental (within band) |
| aware-cross/legion/g.a15.t90 | 1814 | 1467 | -19.1% | incidental (within band) |
| aware-cross/legion/g.a16.done | 1731 | 1442 | -16.7% | incidental (within band) |
| aware-cross/legion/g.a16.t50 | 1731 | 1442 | -16.7% | incidental (within band) |
| aware-cross/legion/g.a16.t90 | 1731 | 1442 | -16.7% | incidental (within band) |
| aware-cross/legion/g.a17.done | 1497 | 1282 | -14.4% | incidental (within band) |
| aware-cross/legion/g.a17.t50 | 1497 | 1282 | -14.4% | incidental (within band) |
| aware-cross/legion/g.a17.t90 | 1409 | 1282 | -9.0% | incidental (within band) |
| aware-cross/legion/g.a18.done | 1622 | 1476 | -9.0% | incidental (within band) |
| aware-cross/legion/g.a18.t50 | 1622 | 1476 | -9.0% | incidental (within band) |
| aware-cross/legion/g.a18.t90 | 1791 | 1476 | -17.6% | incidental (within band) |
| aware-cross/legion/g.a19.done | 1987 | 1469 | -26.1% | incidental (within band) |
| aware-cross/legion/g.a19.t50 | 1987 | 1469 | -26.1% | incidental (within band) |
| aware-cross/legion/g.a19.t90 | 1847 | 1469 | -20.5% | incidental (within band) |
| aware-cross/legion/g.a20.done | 1637 | 1445 | -11.7% | incidental (within band) |
| aware-cross/legion/g.a20.t50 | 1637 | 1445 | -11.7% | incidental (within band) |
| aware-cross/legion/g.a20.t90 | 1637 | 1445 | -11.7% | incidental (within band) |
| aware-cross/legion/g.a21.done | 1745 | 1437 | -17.7% | incidental (within band) |
| aware-cross/legion/g.a21.t50 | 1745 | 1437 | -17.7% | incidental (within band) |
| aware-cross/legion/g.a21.t90 | 1601 | 1437 | -10.2% | incidental (within band) |
| aware-cross/legion/g.a22.done | 1588 | 1428 | -10.1% | incidental (within band) |
| aware-cross/legion/g.a22.t50 | 1588 | 1428 | -10.1% | incidental (within band) |
| aware-cross/legion/g.a22.t90 | 1598 | 1428 | -10.6% | incidental (within band) |
| aware-cross/legion/g.a23.done | 1802 | 1420 | -21.2% | incidental (within band) |
| aware-cross/legion/g.a23.t50 | 1802 | 1420 | -21.2% | incidental (within band) |
| aware-cross/legion/g.a23.t90 | 1757 | 1420 | -19.2% | incidental (within band) |
| aware-cross/legion/g.b00.done | 1048 | 1131 | +7.9% | incidental (within band) |
| aware-cross/legion/g.b00.t50 | 1048 | 1131 | +7.9% | incidental (within band) |
| aware-cross/legion/g.b00.t90 | 1048 | 1131 | +7.9% | incidental (within band) |
| aware-cross/legion/g.b01.done | 1122 | 1206 | +7.5% | incidental (within band) |
| aware-cross/legion/g.b01.t50 | 1122 | 1206 | +7.5% | incidental (within band) |
| aware-cross/legion/g.b01.t90 | 1106 | 1201 | +8.6% | incidental (within band) |
| aware-cross/legion/g.b02.done | 1124 | 1336 | +18.9% | incidental (within band) |
| aware-cross/legion/g.b02.t50 | 1124 | 1336 | +18.9% | incidental (within band) |
| aware-cross/legion/g.b02.t90 | 1124 | 1341 | +19.3% | incidental (within band) |
| aware-cross/legion/g.b03.complete_dist_max | 3 | 2 | -33.3% | incidental (within band) |
| aware-cross/legion/g.b03.complete_dist_median | 3 | 2 | -33.3% | incidental (within band) |
| aware-cross/legion/g.b03.done | 1129 | 1452 | +28.6% | incidental (within band) |
| aware-cross/legion/g.b03.t50 | 1129 | 1452 | +28.6% | incidental (within band) |
| aware-cross/legion/g.b03.t90 | 1129 | 1452 | +28.6% | incidental (within band) |
| aware-cross/legion/g.b04.complete_dist_max | 4 | 2 | -50.0% | incidental (within band) |
| aware-cross/legion/g.b04.complete_dist_median | 4 | 2 | -50.0% | incidental (within band) |
| aware-cross/legion/g.b04.done | 1092 | 1452 | +33.0% | incidental (within band) |
| aware-cross/legion/g.b04.t50 | 1092 | 1452 | +33.0% | incidental (within band) |
| aware-cross/legion/g.b04.t90 | 1092 | 1412 | +29.3% | incidental (within band) |
| aware-cross/legion/g.b05.done | 971 | 1272 | +31.0% | incidental (within band) |
| aware-cross/legion/g.b05.t50 | 971 | 1272 | +31.0% | incidental (within band) |
| aware-cross/legion/g.b05.t90 | 971 | 1274 | +31.2% | incidental (within band) |
| aware-cross/legion/g.b06.done | 1285 | 1203 | -6.4% | incidental (within band) |
| aware-cross/legion/g.b06.t50 | 1285 | 1203 | -6.4% | incidental (within band) |
| aware-cross/legion/g.b07.done | 1249 | 1343 | +7.5% | incidental (within band) |
| aware-cross/legion/g.b07.t50 | 1249 | 1343 | +7.5% | incidental (within band) |
| aware-cross/legion/g.b07.t90 | 1249 | 1342 | +7.4% | incidental (within band) |
| aware-cross/legion/g.b08.done | 1128 | 1387 | +23.0% | incidental (within band) |
| aware-cross/legion/g.b08.t50 | 1128 | 1387 | +23.0% | incidental (within band) |
| aware-cross/legion/g.b08.t90 | 1128 | 1367 | +21.2% | incidental (within band) |
| aware-cross/legion/g.b09.done | 1138 | 1497 | +31.5% | incidental (within band) |
| aware-cross/legion/g.b09.t50 | 1138 | 1497 | +31.5% | incidental (within band) |
| aware-cross/legion/g.b09.t90 | 1138 | 1497 | +31.5% | incidental (within band) |
| aware-cross/legion/g.b10.done | 1099 | 1311 | +19.3% | incidental (within band) |
| aware-cross/legion/g.b10.t50 | 1099 | 1311 | +19.3% | incidental (within band) |
| aware-cross/legion/g.b10.t90 | 1099 | 1314 | +19.6% | incidental (within band) |
| aware-cross/legion/g.b11.done | 1047 | 1644 | +57.0% | incidental (within band) |
| aware-cross/legion/g.b11.t50 | 1047 | 1644 | +57.0% | incidental (within band) |
| aware-cross/legion/g.b11.t90 | 1047 | 1407 | +34.4% | incidental (within band) |
| aware-cross/legion/g.b12.done | 1312 | 1394 | +6.2% | incidental (within band) |
| aware-cross/legion/g.b12.t50 | 1312 | 1394 | +6.2% | incidental (within band) |
| aware-cross/legion/g.b12.t90 | 1324 | 1411 | +6.6% | incidental (within band) |
| aware-cross/legion/g.b13.done | 1250 | 1369 | +9.5% | incidental (within band) |
| aware-cross/legion/g.b13.t50 | 1250 | 1369 | +9.5% | incidental (within band) |
| aware-cross/legion/g.b13.t90 | 1250 | 1369 | +9.5% | incidental (within band) |
| aware-cross/legion/g.b14.done | 1199 | 1497 | +24.9% | incidental (within band) |
| aware-cross/legion/g.b14.t50 | 1199 | 1497 | +24.9% | incidental (within band) |
| aware-cross/legion/g.b14.t90 | 1199 | 1542 | +28.6% | incidental (within band) |
| aware-cross/legion/g.b15.done | 1146 | 1454 | +26.9% | incidental (within band) |
| aware-cross/legion/g.b15.t50 | 1146 | 1454 | +26.9% | incidental (within band) |
| aware-cross/legion/g.b15.t90 | 1146 | 1415 | +23.5% | incidental (within band) |
| aware-cross/legion/g.b16.done | 1107 | 1362 | +23.0% | incidental (within band) |
| aware-cross/legion/g.b16.t50 | 1107 | 1362 | +23.0% | incidental (within band) |
| aware-cross/legion/g.b16.t90 | 1107 | 1363 | +23.1% | incidental (within band) |
| aware-cross/legion/g.b17.done | 1055 | 1258 | +19.2% | incidental (within band) |
| aware-cross/legion/g.b17.t50 | 1055 | 1258 | +19.2% | incidental (within band) |
| aware-cross/legion/g.b17.t90 | 1055 | 1258 | +19.2% | incidental (within band) |
| aware-cross/legion/g.b19.complete_dist_max | 4 | 6 | +50.0% | incidental (within band) |
| aware-cross/legion/g.b19.complete_dist_median | 4 | 6 | +50.0% | incidental (within band) |
| aware-cross/legion/g.b19.done | 1263 | 1677 | +32.8% | incidental (within band) |
| aware-cross/legion/g.b19.t50 | 1263 | 1677 | +32.8% | incidental (within band) |
| aware-cross/legion/g.b19.t90 | 1263 | 1476 | +16.9% | incidental (within band) |
| aware-cross/legion/g.b20.complete_dist_max | 3 | 4 | +33.3% | incidental (within band) |
| aware-cross/legion/g.b20.complete_dist_median | 3 | 4 | +33.3% | incidental (within band) |
| aware-cross/legion/g.b20.done | 1200 | 1455 | +21.2% | incidental (within band) |
| aware-cross/legion/g.b20.t50 | 1200 | 1455 | +21.2% | incidental (within band) |
| aware-cross/legion/g.b20.t90 | 1200 | 1455 | +21.2% | incidental (within band) |
| aware-cross/legion/g.b21.done | 1155 | 1419 | +22.9% | incidental (within band) |
| aware-cross/legion/g.b21.t50 | 1155 | 1419 | +22.9% | incidental (within band) |
| aware-cross/legion/g.b21.t90 | 1155 | 1415 | +22.5% | incidental (within band) |
| aware-cross/legion/g.b22.done | 1115 | 1282 | +15.0% | incidental (within band) |
| aware-cross/legion/g.b22.t50 | 1115 | 1282 | +15.0% | incidental (within band) |
| aware-cross/legion/g.b22.t90 | 1115 | 1362 | +22.2% | incidental (within band) |
| aware-cross/legion/g.b23.done | 1062 | 1247 | +17.4% | incidental (within band) |
| aware-cross/legion/g.b23.t50 | 1062 | 1247 | +17.4% | incidental (within band) |
| aware-cross/legion/g.b23.t90 | 1062 | 1277 | +20.2% | incidental (within band) |
| aware-cross/legion/gate.cross.files_x100 | 664 | 385 | -42.0% | incidental (within band) |
| aware-cross/legion/gate.cross.samples | 874 | 596 | -31.8% | incidental (within band) |
| aware-cross/legion/gate.cross.spread_x100 | 1758 | 1007 | -42.7% | incidental (within band) |
| aware-cross/legion/pair.contacts.contacts | 341 | 0 | -100.0% | intended |
| aware-cross/legion/pair.contacts.pairs | 62184 | 71971 | +15.7% | intended |
| aware-cross/legion/pair.contacts.permille_x100 | 548 | 0 | -100.0% | intended |
| aware-cross/legion/parked_held | 67 | 0 | -100.0% | incidental (within band) |
| aware-cross/legion/reversals | 0 | 5 | new | intended |
| aware-cross/legion/sideways | 250 | 972 | +288.8% | incidental (within band) |
| aware-cross/legion/statue_ticks | 5181 | 1921 | -62.9% | incidental (within band) |
| aware-cross/legion/stop_go | 156 | 122 | -21.8% | incidental (within band) |
| aware-cross/legion/stopped_permille | 74 | 17 | -77.0% | intended |
| aware-cross/legion/waiting_held | 3759 | 460 | -87.8% | incidental (within band) |
| aware-cross/legion/wall_near_samples | 42 | 52 | +23.8% | incidental (within band) |
| aware-cross/legion/work.arrivals.max | 2 | 3 | +50.0% | incidental (within band) |
| aware-cross/legion/work.aware_pairs.p99 | 2 | 4 | +100.0% | incidental (within band) |
| aware-cross/legion/work.aware_pairs.total | 151 | 178 | +17.9% | incidental (within band) |
| aware-cross/legion/work.completion_dist_2.total | 15 | 16 | +6.7% | incidental (within band) |
| aware-cross/legion/work.completion_dist_sum.max | 11 | 10 | -9.1% | incidental (within band) |
| aware-cross/legion/work.contact_arrivals.max | 2 | 3 | +50.0% | incidental (within band) |
| aware-cross/legion/work.contact_arrivals.total | 9 | 8 | -11.1% | incidental (within band) |
| aware-cross/legion/work.crowd_window_ring_cells.max | 576 | 160 | -72.2% | incidental (within band) |
| aware-cross/legion/work.crowd_window_ring_cells.p99 | 32 | 0 | -100.0% | incidental (within band) |
| aware-cross/legion/work.crowd_window_ring_cells.total | 5792 | 1536 | -73.5% | incidental (within band) |
| aware-cross/legion/work.detour_cells.max | 1106 | 504 | -54.4% | incidental (within band) |
| aware-cross/legion/work.detour_cells.p99 | 505 | 0 | -100.0% | incidental (within band) |
| aware-cross/legion/work.detour_cells.total | 22426 | 4133 | -81.6% | incidental (within band) |
| aware-cross/legion/work.detours.max | 2 | 1 | -50.0% | incidental (within band) |
| aware-cross/legion/work.detours.p99 | 1 | 0 | -100.0% | incidental (within band) |
| aware-cross/legion/work.detours.total | 34 | 6 | -82.4% | incidental (within band) |
| aware-cross/legion/work.field_work.total | 788320 | 995280 | +26.3% | incidental (within band) |
| aware-cross/legion/work.field_work_refresh_moving.total | 388960 | 592800 | +52.4% | incidental (within band) |
| aware-cross/legion/work.fields_built.total | 4 | 5 | +25.0% | incidental (within band) |
| aware-cross/legion/work.fields_started_by_kind_1.total | 4 | 5 | +25.0% | incidental (within band) |
| aware-cross/legion/work.held_rechecks.max | 3 | 1 | -66.7% | incidental (within band) |
| aware-cross/legion/work.held_rechecks.p99 | 1 | 0 | -100.0% | incidental (within band) |
| aware-cross/legion/work.held_rechecks.total | 121 | 16 | -86.8% | incidental (within band) |
| aware-cross/legion/work.holds.total | 162 | 130 | -19.8% | incidental (within band) |
| aware-cross/legion/work.legion_total.p99 | 1446 | 2096 | +45.0% | incidental (within band) |
| aware-cross/legion/work.legion_total.total | 1464645 | 1987858 | +35.7% | incidental (within band) |
| aware-cross/legion/work.line_sweeps.max | 24 | 97 | +304.2% | incidental (within band) |
| aware-cross/legion/work.line_sweeps.p99 | 14 | 81 | +478.6% | incidental (within band) |
| aware-cross/legion/work.line_sweeps.total | 9111 | 40037 | +339.4% | incidental (within band) |
| aware-cross/legion/work.mission_arrivals_1.max | 2 | 3 | +50.0% | incidental (within band) |
| aware-cross/legion/work.move_calls_by_state_2.max | 19 | 6 | -68.4% | incidental (within band) |
| aware-cross/legion/work.move_calls_by_state_2.p99 | 16 | 4 | -75.0% | incidental (within band) |
| aware-cross/legion/work.move_calls_by_state_2.total | 5152 | 1082 | -79.0% | incidental (within band) |
| aware-cross/legion/work.pass_scan_cells.max | 480 | 576 | +20.0% | incidental (within band) |
| aware-cross/legion/work.pass_scan_cells.p99 | 432 | 480 | +11.1% | incidental (within band) |
| aware-cross/legion/work.pass_scan_cells.total | 119160 | 157176 | +31.9% | incidental (within band) |
| aware-cross/legion/work.pass_scans.max | 20 | 24 | +20.0% | incidental (within band) |
| aware-cross/legion/work.pass_scans.p99 | 18 | 20 | +11.1% | incidental (within band) |
| aware-cross/legion/work.pass_scans.total | 4965 | 6555 | +32.0% | incidental (within band) |
| aware-cross/legion/work.rechoice_bfs_cells.max | 884 | 239 | -73.0% | incidental (within band) |
| aware-cross/legion/work.rechoice_bfs_cells.total | 2494 | 1113 | -55.4% | incidental (within band) |
| aware-cross/legion/work.refresh_completed.total | 2 | 3 | +50.0% | incidental (within band) |
| aware-cross/legion/work.sched_group_visits.total | 12 | 17 | +41.7% | incidental (within band) |
| aware-cross/legion/work.share_scan_iters.total | 4 | 6 | +50.0% | incidental (within band) |
| aware-cross/legion/work.slides.max | 3 | 4 | +33.3% | incidental (within band) |
| aware-cross/legion/work.slides.p99 | 1 | 2 | +100.0% | incidental (within band) |
| aware-cross/legion/work.slides.total | 174 | 301 | +73.0% | incidental (within band) |
| aware-cross/legion/work.slot_search_cells.total | 5745 | 5454 | -5.1% | incidental (within band) |
| aware-cross/legion/work.trace_cells.p99 | 1300 | 1700 | +30.8% | incidental (within band) |
| aware-cross/legion/work.trace_cells.total | 562082 | 818446 | +45.6% | incidental (within band) |
| aware-cross-behind/legion/aim_reversals | 55 | 43 | -21.8% | incidental (within band) |
| aware-cross-behind/legion/aware.builds | 1 | 2 | +100.0% | intended |
| aware-cross-behind/legion/aware.latency_n | 2 | 3 | +50.0% | intended |
| aware-cross-behind/legion/aware.latency_sum | 4 | 6 | +50.0% | intended |
| aware-cross-behind/legion/aware.replans | 2 | 3 | +50.0% | intended |
| aware-cross-behind/legion/aware.work | 4221 | 7437 | +76.2% | intended |
| aware-cross-behind/legion/aware.work_max | 115 | 212 | +84.3% | intended |
| aware-cross-behind/legion/back | 10 | 13 | +30.0% | incidental (within band) |
| aware-cross-behind/legion/backward | 20 | 14 | -30.0% | incidental (within band) |
| aware-cross-behind/legion/behind.wait.detour_permille | 5 | 151 | +2920.0% | intended |
| aware-cross-behind/legion/behind.wait.extra_cells | 1 | 16 | +1500.0% | intended |
| aware-cross-behind/legion/behind.wait.path_cells | 108 | 123 | +13.9% | intended |
| aware-cross-behind/legion/behind.wait.wait_member_ticks | 1471 | 0 | -100.0% | intended |
| aware-cross-behind/legion/behind.wait.wait_run_max | 47 | 0 | -100.0% | intended |
| aware-cross-behind/legion/behind.wait.wait_ticks | 87 | 0 | -100.0% | intended |
| aware-cross-behind/legion/behind.wait.waits | 1 | 0 | -100.0% | intended |
| aware-cross-behind/legion/churn.bin0 | 1455152 | 1982137 | +36.2% | incidental (within band) |
| aware-cross-behind/legion/churn.bin1 | 36303 | 5276 | -85.5% | incidental (within band) |
| aware-cross-behind/legion/click.a00.done | 2252 | 1530 | -32.1% | intended |
| aware-cross-behind/legion/click.a00.t50 | 1736 | 1455 | -16.2% | intended |
| aware-cross-behind/legion/click.a00.t90 | 2027 | 1517 | -25.2% | intended |
| aware-cross-behind/legion/click.b00.done | 1540 | 1812 | +17.7% | intended |
| aware-cross-behind/legion/click.b00.t50 | 1128 | 1367 | +21.2% | intended |
| aware-cross-behind/legion/click.b00.t90 | 1332 | 1542 | +15.8% | intended |
| aware-cross-behind/legion/crawl_samples | 502 | 165 | -67.1% | incidental (within band) |
| aware-cross-behind/legion/detour_permille | 16 | 79 | +393.8% | incidental (within band) |
| aware-cross-behind/legion/engagement_on_flowing | 30 | 14 | -53.3% | incidental (within band) |
| aware-cross-behind/legion/flip_rate_per30_permille | 81 | 12 | -85.2% | incidental (within band) |
| aware-cross-behind/legion/flips | 183 | 27 | -85.2% | incidental (within band) |
| aware-cross-behind/legion/g.a00.done | 1766 | 1520 | -13.9% | incidental (within band) |
| aware-cross-behind/legion/g.a00.t50 | 1766 | 1520 | -13.9% | incidental (within band) |
| aware-cross-behind/legion/g.a00.t90 | 1744 | 1520 | -12.8% | incidental (within band) |
| aware-cross-behind/legion/g.a01.done | 1807 | 1510 | -16.4% | incidental (within band) |
| aware-cross-behind/legion/g.a01.t50 | 1807 | 1510 | -16.4% | incidental (within band) |
| aware-cross-behind/legion/g.a01.t90 | 1753 | 1510 | -13.9% | incidental (within band) |
| aware-cross-behind/legion/g.a02.done | 1795 | 1500 | -16.4% | incidental (within band) |
| aware-cross-behind/legion/g.a02.t50 | 1795 | 1500 | -16.4% | incidental (within band) |
| aware-cross-behind/legion/g.a02.t90 | 1641 | 1500 | -8.6% | incidental (within band) |
| aware-cross-behind/legion/g.a03.done | 1703 | 1490 | -12.5% | incidental (within band) |
| aware-cross-behind/legion/g.a03.t50 | 1703 | 1490 | -12.5% | incidental (within band) |
| aware-cross-behind/legion/g.a03.t90 | 1632 | 1490 | -8.7% | incidental (within band) |
| aware-cross-behind/legion/g.a04.done | 1638 | 1482 | -9.5% | incidental (within band) |
| aware-cross-behind/legion/g.a04.t50 | 1638 | 1482 | -9.5% | incidental (within band) |
| aware-cross-behind/legion/g.a04.t90 | 1600 | 1482 | -7.4% | incidental (within band) |
| aware-cross-behind/legion/g.a05.done | 1549 | 1437 | -7.2% | incidental (within band) |
| aware-cross-behind/legion/g.a05.t50 | 1549 | 1437 | -7.2% | incidental (within band) |
| aware-cross-behind/legion/g.a05.t90 | 1555 | 1437 | -7.6% | incidental (within band) |
| aware-cross-behind/legion/g.a06.done | 1860 | 1463 | -21.3% | incidental (within band) |
| aware-cross-behind/legion/g.a06.t50 | 1860 | 1463 | -21.3% | incidental (within band) |
| aware-cross-behind/legion/g.a06.t90 | 1860 | 1463 | -21.3% | incidental (within band) |
| aware-cross-behind/legion/g.a07.done | 1763 | 1455 | -17.5% | incidental (within band) |
| aware-cross-behind/legion/g.a07.t50 | 1763 | 1455 | -17.5% | incidental (within band) |
| aware-cross-behind/legion/g.a07.t90 | 1712 | 1455 | -15.0% | incidental (within band) |
| aware-cross-behind/legion/g.a08.done | 1787 | 1447 | -19.0% | incidental (within band) |
| aware-cross-behind/legion/g.a08.t50 | 1787 | 1447 | -19.0% | incidental (within band) |
| aware-cross-behind/legion/g.a08.t90 | 1787 | 1447 | -19.0% | incidental (within band) |
| aware-cross-behind/legion/g.a09.done | 1694 | 1379 | -18.6% | incidental (within band) |
| aware-cross-behind/legion/g.a09.t50 | 1694 | 1379 | -18.6% | incidental (within band) |
| aware-cross-behind/legion/g.a09.t90 | 1689 | 1379 | -18.4% | incidental (within band) |
| aware-cross-behind/legion/g.a10.done | 1604 | 1370 | -14.6% | incidental (within band) |
| aware-cross-behind/legion/g.a10.t50 | 1604 | 1370 | -14.6% | incidental (within band) |
| aware-cross-behind/legion/g.a10.t90 | 1604 | 1370 | -14.6% | incidental (within band) |
| aware-cross-behind/legion/g.a11.done | 1476 | 1361 | -7.8% | incidental (within band) |
| aware-cross-behind/legion/g.a11.t50 | 1476 | 1361 | -7.8% | incidental (within band) |
| aware-cross-behind/legion/g.a11.t90 | 1495 | 1361 | -9.0% | incidental (within band) |
| aware-cross-behind/legion/g.a12.complete_dist_max | 7 | 5 | -28.6% | incidental (within band) |
| aware-cross-behind/legion/g.a12.complete_dist_median | 7 | 5 | -28.6% | incidental (within band) |
| aware-cross-behind/legion/g.a12.done | 1937 | 1517 | -21.7% | incidental (within band) |
| aware-cross-behind/legion/g.a12.t50 | 1937 | 1517 | -21.7% | incidental (within band) |
| aware-cross-behind/legion/g.a12.t90 | 1937 | 1517 | -21.7% | incidental (within band) |
| aware-cross-behind/legion/g.a13.complete_dist_max | 4 | 3 | -25.0% | incidental (within band) |
| aware-cross-behind/legion/g.a13.complete_dist_median | 4 | 3 | -25.0% | incidental (within band) |
| aware-cross-behind/legion/g.a13.done | 1837 | 1507 | -18.0% | incidental (within band) |
| aware-cross-behind/legion/g.a13.t50 | 1837 | 1507 | -18.0% | incidental (within band) |
| aware-cross-behind/legion/g.a13.t90 | 1837 | 1507 | -18.0% | incidental (within band) |
| aware-cross-behind/legion/g.a14.complete_dist_max | 2 | 1 | -50.0% | incidental (within band) |
| aware-cross-behind/legion/g.a14.complete_dist_median | 2 | 1 | -50.0% | incidental (within band) |
| aware-cross-behind/legion/g.a14.done | 1847 | 1498 | -18.9% | incidental (within band) |
| aware-cross-behind/legion/g.a14.t50 | 1847 | 1498 | -18.9% | incidental (within band) |
| aware-cross-behind/legion/g.a14.t90 | 1817 | 1498 | -17.6% | incidental (within band) |
| aware-cross-behind/legion/g.a15.done | 1814 | 1467 | -19.1% | incidental (within band) |
| aware-cross-behind/legion/g.a15.t50 | 1814 | 1467 | -19.1% | incidental (within band) |
| aware-cross-behind/legion/g.a15.t90 | 1814 | 1467 | -19.1% | incidental (within band) |
| aware-cross-behind/legion/g.a16.done | 1731 | 1442 | -16.7% | incidental (within band) |
| aware-cross-behind/legion/g.a16.t50 | 1731 | 1442 | -16.7% | incidental (within band) |
| aware-cross-behind/legion/g.a16.t90 | 1731 | 1442 | -16.7% | incidental (within band) |
| aware-cross-behind/legion/g.a17.done | 1497 | 1282 | -14.4% | incidental (within band) |
| aware-cross-behind/legion/g.a17.t50 | 1497 | 1282 | -14.4% | incidental (within band) |
| aware-cross-behind/legion/g.a17.t90 | 1409 | 1282 | -9.0% | incidental (within band) |
| aware-cross-behind/legion/g.a18.done | 1622 | 1476 | -9.0% | incidental (within band) |
| aware-cross-behind/legion/g.a18.t50 | 1622 | 1476 | -9.0% | incidental (within band) |
| aware-cross-behind/legion/g.a18.t90 | 1791 | 1476 | -17.6% | incidental (within band) |
| aware-cross-behind/legion/g.a19.done | 1987 | 1469 | -26.1% | incidental (within band) |
| aware-cross-behind/legion/g.a19.t50 | 1987 | 1469 | -26.1% | incidental (within band) |
| aware-cross-behind/legion/g.a19.t90 | 1847 | 1469 | -20.5% | incidental (within band) |
| aware-cross-behind/legion/g.a20.done | 1637 | 1445 | -11.7% | incidental (within band) |
| aware-cross-behind/legion/g.a20.t50 | 1637 | 1445 | -11.7% | incidental (within band) |
| aware-cross-behind/legion/g.a20.t90 | 1637 | 1445 | -11.7% | incidental (within band) |
| aware-cross-behind/legion/g.a21.done | 1745 | 1437 | -17.7% | incidental (within band) |
| aware-cross-behind/legion/g.a21.t50 | 1745 | 1437 | -17.7% | incidental (within band) |
| aware-cross-behind/legion/g.a21.t90 | 1601 | 1437 | -10.2% | incidental (within band) |
| aware-cross-behind/legion/g.a22.done | 1588 | 1428 | -10.1% | incidental (within band) |
| aware-cross-behind/legion/g.a22.t50 | 1588 | 1428 | -10.1% | incidental (within band) |
| aware-cross-behind/legion/g.a22.t90 | 1598 | 1428 | -10.6% | incidental (within band) |
| aware-cross-behind/legion/g.a23.done | 1802 | 1420 | -21.2% | incidental (within band) |
| aware-cross-behind/legion/g.a23.t50 | 1802 | 1420 | -21.2% | incidental (within band) |
| aware-cross-behind/legion/g.a23.t90 | 1757 | 1420 | -19.2% | incidental (within band) |
| aware-cross-behind/legion/g.b00.done | 1048 | 1131 | +7.9% | incidental (within band) |
| aware-cross-behind/legion/g.b00.t50 | 1048 | 1131 | +7.9% | incidental (within band) |
| aware-cross-behind/legion/g.b00.t90 | 1048 | 1131 | +7.9% | incidental (within band) |
| aware-cross-behind/legion/g.b01.done | 1122 | 1206 | +7.5% | incidental (within band) |
| aware-cross-behind/legion/g.b01.t50 | 1122 | 1206 | +7.5% | incidental (within band) |
| aware-cross-behind/legion/g.b01.t90 | 1106 | 1201 | +8.6% | incidental (within band) |
| aware-cross-behind/legion/g.b02.done | 1124 | 1336 | +18.9% | incidental (within band) |
| aware-cross-behind/legion/g.b02.t50 | 1124 | 1336 | +18.9% | incidental (within band) |
| aware-cross-behind/legion/g.b02.t90 | 1124 | 1341 | +19.3% | incidental (within band) |
| aware-cross-behind/legion/g.b03.complete_dist_max | 3 | 2 | -33.3% | incidental (within band) |
| aware-cross-behind/legion/g.b03.complete_dist_median | 3 | 2 | -33.3% | incidental (within band) |
| aware-cross-behind/legion/g.b03.done | 1129 | 1452 | +28.6% | incidental (within band) |
| aware-cross-behind/legion/g.b03.t50 | 1129 | 1452 | +28.6% | incidental (within band) |
| aware-cross-behind/legion/g.b03.t90 | 1129 | 1452 | +28.6% | incidental (within band) |
| aware-cross-behind/legion/g.b04.complete_dist_max | 4 | 2 | -50.0% | incidental (within band) |
| aware-cross-behind/legion/g.b04.complete_dist_median | 4 | 2 | -50.0% | incidental (within band) |
| aware-cross-behind/legion/g.b04.done | 1092 | 1452 | +33.0% | incidental (within band) |
| aware-cross-behind/legion/g.b04.t50 | 1092 | 1452 | +33.0% | incidental (within band) |
| aware-cross-behind/legion/g.b04.t90 | 1092 | 1412 | +29.3% | incidental (within band) |
| aware-cross-behind/legion/g.b05.done | 971 | 1272 | +31.0% | incidental (within band) |
| aware-cross-behind/legion/g.b05.t50 | 971 | 1272 | +31.0% | incidental (within band) |
| aware-cross-behind/legion/g.b05.t90 | 971 | 1274 | +31.2% | incidental (within band) |
| aware-cross-behind/legion/g.b06.done | 1285 | 1203 | -6.4% | incidental (within band) |
| aware-cross-behind/legion/g.b06.t50 | 1285 | 1203 | -6.4% | incidental (within band) |
| aware-cross-behind/legion/g.b07.done | 1249 | 1343 | +7.5% | incidental (within band) |
| aware-cross-behind/legion/g.b07.t50 | 1249 | 1343 | +7.5% | incidental (within band) |
| aware-cross-behind/legion/g.b07.t90 | 1249 | 1342 | +7.4% | incidental (within band) |
| aware-cross-behind/legion/g.b08.done | 1128 | 1387 | +23.0% | incidental (within band) |
| aware-cross-behind/legion/g.b08.t50 | 1128 | 1387 | +23.0% | incidental (within band) |
| aware-cross-behind/legion/g.b08.t90 | 1128 | 1367 | +21.2% | incidental (within band) |
| aware-cross-behind/legion/g.b09.done | 1138 | 1497 | +31.5% | incidental (within band) |
| aware-cross-behind/legion/g.b09.t50 | 1138 | 1497 | +31.5% | incidental (within band) |
| aware-cross-behind/legion/g.b09.t90 | 1138 | 1497 | +31.5% | incidental (within band) |
| aware-cross-behind/legion/g.b10.done | 1099 | 1311 | +19.3% | incidental (within band) |
| aware-cross-behind/legion/g.b10.t50 | 1099 | 1311 | +19.3% | incidental (within band) |
| aware-cross-behind/legion/g.b10.t90 | 1099 | 1314 | +19.6% | incidental (within band) |
| aware-cross-behind/legion/g.b11.done | 1047 | 1644 | +57.0% | incidental (within band) |
| aware-cross-behind/legion/g.b11.t50 | 1047 | 1644 | +57.0% | incidental (within band) |
| aware-cross-behind/legion/g.b11.t90 | 1047 | 1407 | +34.4% | incidental (within band) |
| aware-cross-behind/legion/g.b12.done | 1312 | 1394 | +6.2% | incidental (within band) |
| aware-cross-behind/legion/g.b12.t50 | 1312 | 1394 | +6.2% | incidental (within band) |
| aware-cross-behind/legion/g.b12.t90 | 1324 | 1411 | +6.6% | incidental (within band) |
| aware-cross-behind/legion/g.b13.done | 1250 | 1369 | +9.5% | incidental (within band) |
| aware-cross-behind/legion/g.b13.t50 | 1250 | 1369 | +9.5% | incidental (within band) |
| aware-cross-behind/legion/g.b13.t90 | 1250 | 1369 | +9.5% | incidental (within band) |
| aware-cross-behind/legion/g.b14.done | 1199 | 1497 | +24.9% | incidental (within band) |
| aware-cross-behind/legion/g.b14.t50 | 1199 | 1497 | +24.9% | incidental (within band) |
| aware-cross-behind/legion/g.b14.t90 | 1199 | 1542 | +28.6% | incidental (within band) |
| aware-cross-behind/legion/g.b15.done | 1146 | 1454 | +26.9% | incidental (within band) |
| aware-cross-behind/legion/g.b15.t50 | 1146 | 1454 | +26.9% | incidental (within band) |
| aware-cross-behind/legion/g.b15.t90 | 1146 | 1415 | +23.5% | incidental (within band) |
| aware-cross-behind/legion/g.b16.done | 1107 | 1362 | +23.0% | incidental (within band) |
| aware-cross-behind/legion/g.b16.t50 | 1107 | 1362 | +23.0% | incidental (within band) |
| aware-cross-behind/legion/g.b16.t90 | 1107 | 1363 | +23.1% | incidental (within band) |
| aware-cross-behind/legion/g.b17.done | 1055 | 1258 | +19.2% | incidental (within band) |
| aware-cross-behind/legion/g.b17.t50 | 1055 | 1258 | +19.2% | incidental (within band) |
| aware-cross-behind/legion/g.b17.t90 | 1055 | 1258 | +19.2% | incidental (within band) |
| aware-cross-behind/legion/g.b19.complete_dist_max | 4 | 6 | +50.0% | incidental (within band) |
| aware-cross-behind/legion/g.b19.complete_dist_median | 4 | 6 | +50.0% | incidental (within band) |
| aware-cross-behind/legion/g.b19.done | 1263 | 1677 | +32.8% | incidental (within band) |
| aware-cross-behind/legion/g.b19.t50 | 1263 | 1677 | +32.8% | incidental (within band) |
| aware-cross-behind/legion/g.b19.t90 | 1263 | 1476 | +16.9% | incidental (within band) |
| aware-cross-behind/legion/g.b20.complete_dist_max | 3 | 4 | +33.3% | incidental (within band) |
| aware-cross-behind/legion/g.b20.complete_dist_median | 3 | 4 | +33.3% | incidental (within band) |
| aware-cross-behind/legion/g.b20.done | 1200 | 1455 | +21.2% | incidental (within band) |
| aware-cross-behind/legion/g.b20.t50 | 1200 | 1455 | +21.2% | incidental (within band) |
| aware-cross-behind/legion/g.b20.t90 | 1200 | 1455 | +21.2% | incidental (within band) |
| aware-cross-behind/legion/g.b21.done | 1155 | 1419 | +22.9% | incidental (within band) |
| aware-cross-behind/legion/g.b21.t50 | 1155 | 1419 | +22.9% | incidental (within band) |
| aware-cross-behind/legion/g.b21.t90 | 1155 | 1415 | +22.5% | incidental (within band) |
| aware-cross-behind/legion/g.b22.done | 1115 | 1282 | +15.0% | incidental (within band) |
| aware-cross-behind/legion/g.b22.t50 | 1115 | 1282 | +15.0% | incidental (within band) |
| aware-cross-behind/legion/g.b22.t90 | 1115 | 1362 | +22.2% | incidental (within band) |
| aware-cross-behind/legion/g.b23.done | 1062 | 1247 | +17.4% | incidental (within band) |
| aware-cross-behind/legion/g.b23.t50 | 1062 | 1247 | +17.4% | incidental (within band) |
| aware-cross-behind/legion/g.b23.t90 | 1062 | 1277 | +20.2% | incidental (within band) |
| aware-cross-behind/legion/gate.cross.files_x100 | 664 | 385 | -42.0% | incidental (within band) |
| aware-cross-behind/legion/gate.cross.samples | 874 | 596 | -31.8% | incidental (within band) |
| aware-cross-behind/legion/gate.cross.spread_x100 | 1758 | 1007 | -42.7% | incidental (within band) |
| aware-cross-behind/legion/pair.contacts.contacts | 341 | 0 | -100.0% | intended |
| aware-cross-behind/legion/pair.contacts.pairs | 62184 | 71971 | +15.7% | intended |
| aware-cross-behind/legion/pair.contacts.permille_x100 | 548 | 0 | -100.0% | intended |
| aware-cross-behind/legion/parked_held | 67 | 0 | -100.0% | incidental (within band) |
| aware-cross-behind/legion/reversals | 0 | 5 | new | intended |
| aware-cross-behind/legion/sideways | 250 | 972 | +288.8% | incidental (within band) |
| aware-cross-behind/legion/statue_ticks | 5181 | 1921 | -62.9% | incidental (within band) |
| aware-cross-behind/legion/stop_go | 156 | 122 | -21.8% | incidental (within band) |
| aware-cross-behind/legion/stopped_permille | 74 | 17 | -77.0% | intended |
| aware-cross-behind/legion/waiting_held | 3759 | 460 | -87.8% | incidental (within band) |
| aware-cross-behind/legion/wall_near_samples | 42 | 52 | +23.8% | incidental (within band) |
| aware-cross-behind/legion/work.arrivals.max | 2 | 3 | +50.0% | incidental (within band) |
| aware-cross-behind/legion/work.aware_pairs.p99 | 2 | 4 | +100.0% | incidental (within band) |
| aware-cross-behind/legion/work.aware_pairs.total | 151 | 178 | +17.9% | incidental (within band) |
| aware-cross-behind/legion/work.completion_dist_2.total | 15 | 16 | +6.7% | incidental (within band) |
| aware-cross-behind/legion/work.completion_dist_sum.max | 11 | 10 | -9.1% | incidental (within band) |
| aware-cross-behind/legion/work.contact_arrivals.max | 2 | 3 | +50.0% | incidental (within band) |
| aware-cross-behind/legion/work.contact_arrivals.total | 9 | 8 | -11.1% | incidental (within band) |
| aware-cross-behind/legion/work.crowd_window_ring_cells.max | 576 | 160 | -72.2% | incidental (within band) |
| aware-cross-behind/legion/work.crowd_window_ring_cells.p99 | 32 | 0 | -100.0% | incidental (within band) |
| aware-cross-behind/legion/work.crowd_window_ring_cells.total | 5792 | 1536 | -73.5% | incidental (within band) |
| aware-cross-behind/legion/work.detour_cells.max | 1106 | 504 | -54.4% | incidental (within band) |
| aware-cross-behind/legion/work.detour_cells.p99 | 505 | 0 | -100.0% | incidental (within band) |
| aware-cross-behind/legion/work.detour_cells.total | 22426 | 4133 | -81.6% | incidental (within band) |
| aware-cross-behind/legion/work.detours.max | 2 | 1 | -50.0% | incidental (within band) |
| aware-cross-behind/legion/work.detours.p99 | 1 | 0 | -100.0% | incidental (within band) |
| aware-cross-behind/legion/work.detours.total | 34 | 6 | -82.4% | incidental (within band) |
| aware-cross-behind/legion/work.field_work.total | 788320 | 995280 | +26.3% | incidental (within band) |
| aware-cross-behind/legion/work.field_work_refresh_moving.total | 388960 | 592800 | +52.4% | incidental (within band) |
| aware-cross-behind/legion/work.fields_built.total | 4 | 5 | +25.0% | incidental (within band) |
| aware-cross-behind/legion/work.fields_started_by_kind_1.total | 4 | 5 | +25.0% | incidental (within band) |
| aware-cross-behind/legion/work.held_rechecks.max | 3 | 1 | -66.7% | incidental (within band) |
| aware-cross-behind/legion/work.held_rechecks.p99 | 1 | 0 | -100.0% | incidental (within band) |
| aware-cross-behind/legion/work.held_rechecks.total | 121 | 16 | -86.8% | incidental (within band) |
| aware-cross-behind/legion/work.holds.total | 162 | 130 | -19.8% | incidental (within band) |
| aware-cross-behind/legion/work.legion_total.p99 | 1446 | 2096 | +45.0% | incidental (within band) |
| aware-cross-behind/legion/work.legion_total.total | 1464645 | 1987858 | +35.7% | incidental (within band) |
| aware-cross-behind/legion/work.line_sweeps.max | 24 | 97 | +304.2% | incidental (within band) |
| aware-cross-behind/legion/work.line_sweeps.p99 | 14 | 81 | +478.6% | incidental (within band) |
| aware-cross-behind/legion/work.line_sweeps.total | 9111 | 40037 | +339.4% | incidental (within band) |
| aware-cross-behind/legion/work.mission_arrivals_1.max | 2 | 3 | +50.0% | incidental (within band) |
| aware-cross-behind/legion/work.move_calls_by_state_2.max | 19 | 6 | -68.4% | incidental (within band) |
| aware-cross-behind/legion/work.move_calls_by_state_2.p99 | 16 | 4 | -75.0% | incidental (within band) |
| aware-cross-behind/legion/work.move_calls_by_state_2.total | 5152 | 1082 | -79.0% | incidental (within band) |
| aware-cross-behind/legion/work.pass_scan_cells.max | 480 | 576 | +20.0% | incidental (within band) |
| aware-cross-behind/legion/work.pass_scan_cells.p99 | 432 | 480 | +11.1% | incidental (within band) |
| aware-cross-behind/legion/work.pass_scan_cells.total | 119160 | 157176 | +31.9% | incidental (within band) |
| aware-cross-behind/legion/work.pass_scans.max | 20 | 24 | +20.0% | incidental (within band) |
| aware-cross-behind/legion/work.pass_scans.p99 | 18 | 20 | +11.1% | incidental (within band) |
| aware-cross-behind/legion/work.pass_scans.total | 4965 | 6555 | +32.0% | incidental (within band) |
| aware-cross-behind/legion/work.rechoice_bfs_cells.max | 884 | 239 | -73.0% | incidental (within band) |
| aware-cross-behind/legion/work.rechoice_bfs_cells.total | 2494 | 1113 | -55.4% | incidental (within band) |
| aware-cross-behind/legion/work.refresh_completed.total | 2 | 3 | +50.0% | incidental (within band) |
| aware-cross-behind/legion/work.sched_group_visits.total | 12 | 17 | +41.7% | incidental (within band) |
| aware-cross-behind/legion/work.share_scan_iters.total | 4 | 6 | +50.0% | incidental (within band) |
| aware-cross-behind/legion/work.slides.max | 3 | 4 | +33.3% | incidental (within band) |
| aware-cross-behind/legion/work.slides.p99 | 1 | 2 | +100.0% | incidental (within band) |
| aware-cross-behind/legion/work.slides.total | 174 | 301 | +73.0% | incidental (within band) |
| aware-cross-behind/legion/work.slot_search_cells.total | 5745 | 5454 | -5.1% | incidental (within band) |
| aware-cross-behind/legion/work.trace_cells.p99 | 1300 | 1700 | +30.8% | incidental (within band) |
| aware-cross-behind/legion/work.trace_cells.total | 562082 | 818446 | +45.6% | incidental (within band) |
| aware-headon/legion/aim_reversals | 63 | 59 | -6.3% | incidental (within band) |
| aware-headon/legion/back | 10 | 12 | +20.0% | incidental (within band) |
| aware-headon/legion/churn.bin0 | 1687878 | 2006316 | +18.9% | incidental (within band) |
| aware-headon/legion/churn.bin1 | 71734 | 45206 | -37.0% | incidental (within band) |
| aware-headon/legion/click.a00.done | 2411 | 2099 | -12.9% | intended |
| aware-headon/legion/click.a00.t90 | 2086 | 1847 | -11.5% | intended |
| aware-headon/legion/click.b00.done | 2239 | 2442 | +9.1% | intended |
| aware-headon/legion/click.b00.t90 | 2037 | 1906 | -6.4% | intended |
| aware-headon/legion/crawl_samples | 657 | 435 | -33.8% | incidental (within band) |
| aware-headon/legion/detour_permille | 29 | 25 | -13.8% | incidental (within band) |
| aware-headon/legion/engagement_on_flowing | 45 | 49 | +8.9% | incidental (within band) |
| aware-headon/legion/flip_rate_per30_permille | 81 | 60 | -25.9% | incidental (within band) |
| aware-headon/legion/flips | 226 | 160 | -29.2% | incidental (within band) |
| aware-headon/legion/g.a00.t90 | 1928 | 2026 | +5.1% | incidental (within band) |
| aware-headon/legion/g.a01.done | 2031 | 1822 | -10.3% | incidental (within band) |
| aware-headon/legion/g.a01.t50 | 2031 | 1822 | -10.3% | incidental (within band) |
| aware-headon/legion/g.a01.t90 | 2027 | 1817 | -10.4% | incidental (within band) |
| aware-headon/legion/g.a02.done | 1914 | 1757 | -8.2% | incidental (within band) |
| aware-headon/legion/g.a02.t50 | 1914 | 1757 | -8.2% | incidental (within band) |
| aware-headon/legion/g.a02.t90 | 1872 | 1775 | -5.2% | incidental (within band) |
| aware-headon/legion/g.a06.done | 1993 | 1813 | -9.0% | incidental (within band) |
| aware-headon/legion/g.a06.t50 | 1993 | 1813 | -9.0% | incidental (within band) |
| aware-headon/legion/g.a06.t90 | 1909 | 1797 | -5.9% | incidental (within band) |
| aware-headon/legion/g.a07.t90 | 1900 | 1802 | -5.2% | incidental (within band) |
| aware-headon/legion/g.a08.complete_dist_max | 4 | 1 | -75.0% | incidental (within band) |
| aware-headon/legion/g.a08.complete_dist_median | 4 | 1 | -75.0% | incidental (within band) |
| aware-headon/legion/g.a08.done | 1982 | 1807 | -8.8% | incidental (within band) |
| aware-headon/legion/g.a08.t50 | 1982 | 1807 | -8.8% | incidental (within band) |
| aware-headon/legion/g.a09.done | 1877 | 1775 | -5.4% | incidental (within band) |
| aware-headon/legion/g.a09.t50 | 1877 | 1775 | -5.4% | incidental (within band) |
| aware-headon/legion/g.a12.complete_dist_max | 5 | 6 | +20.0% | incidental (within band) |
| aware-headon/legion/g.a12.complete_dist_median | 5 | 6 | +20.0% | incidental (within band) |
| aware-headon/legion/g.a14.complete_dist_max | 2 | 1 | -50.0% | incidental (within band) |
| aware-headon/legion/g.a14.complete_dist_median | 2 | 1 | -50.0% | incidental (within band) |
| aware-headon/legion/g.b10.done | 1852 | 1753 | -5.3% | incidental (within band) |
| aware-headon/legion/g.b10.t50 | 1852 | 1753 | -5.3% | incidental (within band) |
| aware-headon/legion/g.b11.done | 1901 | 1776 | -6.6% | incidental (within band) |
| aware-headon/legion/g.b11.t50 | 1901 | 1776 | -6.6% | incidental (within band) |
| aware-headon/legion/g.b14.complete_dist_max | 8 | 1 | -87.5% | incidental (within band) |
| aware-headon/legion/g.b14.complete_dist_median | 8 | 1 | -87.5% | incidental (within band) |
| aware-headon/legion/g.b14.done | 2127 | 1857 | -12.7% | incidental (within band) |
| aware-headon/legion/g.b14.t50 | 2127 | 1857 | -12.7% | incidental (within band) |
| aware-headon/legion/g.b14.t90 | 2021 | 1902 | -5.9% | incidental (within band) |
| aware-headon/legion/g.b15.complete_dist_max | 5 | 1 | -80.0% | incidental (within band) |
| aware-headon/legion/g.b15.complete_dist_median | 5 | 1 | -80.0% | incidental (within band) |
| aware-headon/legion/g.b15.done | 2037 | 1853 | -9.0% | incidental (within band) |
| aware-headon/legion/g.b15.t50 | 2037 | 1853 | -9.0% | incidental (within band) |
| aware-headon/legion/g.b17.complete_dist_max | 6 | 5 | -16.7% | incidental (within band) |
| aware-headon/legion/g.b17.complete_dist_median | 6 | 5 | -16.7% | incidental (within band) |
| aware-headon/legion/g.b17.done | 2037 | 1906 | -6.4% | incidental (within band) |
| aware-headon/legion/g.b17.t50 | 2037 | 1906 | -6.4% | incidental (within band) |
| aware-headon/legion/g.b17.t90 | 2037 | 1927 | -5.4% | incidental (within band) |
| aware-headon/legion/g.b19.done | 2037 | 1677 | -17.7% | incidental (within band) |
| aware-headon/legion/g.b19.t50 | 2037 | 1677 | -17.7% | incidental (within band) |
| aware-headon/legion/g.b19.t90 | 1829 | 1722 | -5.9% | incidental (within band) |
| aware-headon/legion/g.b21.done | 1946 | 1812 | -6.9% | incidental (within band) |
| aware-headon/legion/g.b21.t50 | 1946 | 1812 | -6.9% | incidental (within band) |
| aware-headon/legion/g.b21.t90 | 1945 | 1812 | -6.8% | incidental (within band) |
| aware-headon/legion/g.b23.done | 1954 | 1776 | -9.1% | incidental (within band) |
| aware-headon/legion/g.b23.t50 | 1954 | 1776 | -9.1% | incidental (within band) |
| aware-headon/legion/gate.mid.samples | 626 | 507 | -19.0% | incidental (within band) |
| aware-headon/legion/pair.contacts.contacts | 390 | 251 | -35.6% | intended |
| aware-headon/legion/pair.contacts.permille_x100 | 431 | 287 | -33.4% | intended |
| aware-headon/legion/parked_held | 33 | 0 | -100.0% | incidental (within band) |
| aware-headon/legion/reversals | 3 | 4 | +33.3% | intended |
| aware-headon/legion/sideways | 914 | 823 | -10.0% | incidental (within band) |
| aware-headon/legion/statue_ticks | 7210 | 5101 | -29.3% | incidental (within band) |
| aware-headon/legion/stop_go | 222 | 176 | -20.7% | incidental (within band) |
| aware-headon/legion/stopped_permille | 55 | 42 | -23.6% | intended |
| aware-headon/legion/waiting_held | 3089 | 2044 | -33.8% | incidental (within band) |
| aware-headon/legion/work.completion_dist_1.total | 5 | 7 | +40.0% | incidental (within band) |
| aware-headon/legion/work.completion_dist_2.total | 14 | 16 | +14.3% | incidental (within band) |
| aware-headon/legion/work.completion_dist_3.max | 2 | 1 | -50.0% | incidental (within band) |
| aware-headon/legion/work.completion_dist_4.max | 1 | 0 | -100.0% | incidental (within band) |
| aware-headon/legion/work.completion_dist_4.total | 2 | 0 | -100.0% | incidental (within band) |
| aware-headon/legion/work.completion_dist_sum.max | 13 | 10 | -23.1% | incidental (within band) |
| aware-headon/legion/work.completion_dist_sum.total | 199 | 177 | -11.1% | incidental (within band) |
| aware-headon/legion/work.contact_arrivals.total | 10 | 9 | -10.0% | incidental (within band) |
| aware-headon/legion/work.crowd_window_ring_cells.max | 416 | 384 | -7.7% | incidental (within band) |
| aware-headon/legion/work.crowd_window_ring_cells.p99 | 64 | 32 | -50.0% | incidental (within band) |
| aware-headon/legion/work.crowd_window_ring_cells.total | 5632 | 4192 | -25.6% | incidental (within band) |
| aware-headon/legion/work.detour_cells.max | 921 | 823 | -10.6% | incidental (within band) |
| aware-headon/legion/work.detour_cells.total | 12248 | 7011 | -42.8% | incidental (within band) |
| aware-headon/legion/work.detours.total | 24 | 10 | -58.3% | incidental (within band) |
| aware-headon/legion/work.held_rechecks.p99 | 1 | 0 | -100.0% | incidental (within band) |
| aware-headon/legion/work.held_rechecks.total | 48 | 26 | -45.8% | incidental (within band) |
| aware-headon/legion/work.holds.total | 226 | 180 | -20.4% | incidental (within band) |
| aware-headon/legion/work.legion_total.p99 | 1989 | 2510 | +26.2% | incidental (within band) |
| aware-headon/legion/work.legion_total.total | 1756995 | 2048666 | +16.6% | incidental (within band) |
| aware-headon/legion/work.line_sweeps.total | 70603 | 111866 | +58.4% | incidental (within band) |
| aware-headon/legion/work.move_calls_by_state_2.max | 20 | 16 | -20.0% | incidental (within band) |
| aware-headon/legion/work.move_calls_by_state_2.p99 | 16 | 14 | -12.5% | incidental (within band) |
| aware-headon/legion/work.move_calls_by_state_2.total | 4643 | 3249 | -30.0% | incidental (within band) |
| aware-headon/legion/work.pass_scans.p99 | 16 | 17 | +6.2% | incidental (within band) |
| aware-headon/legion/work.rechoice_bfs_cells.max | 963 | 651 | -32.4% | incidental (within band) |
| aware-headon/legion/work.rechoice_bfs_cells.total | 4485 | 3602 | -19.7% | incidental (within band) |
| aware-headon/legion/work.sched_group_visits.total | 10 | 8 | -20.0% | incidental (within band) |
| aware-headon/legion/work.slides.max | 4 | 5 | +25.0% | incidental (within band) |
| aware-headon/legion/work.slides.total | 433 | 392 | -9.5% | incidental (within band) |
| aware-headon/legion/work.slot_search_cells.total | 9832 | 8888 | -9.6% | incidental (within band) |
| aware-headon/legion/work.trace_cells.p99 | 1864 | 2451 | +31.5% | incidental (within band) |
| aware-headon/legion/work.trace_cells.total | 1058957 | 1376229 | +30.0% | incidental (within band) |
| aware-seen/legion/aim_reversals | 46 | 50 | +8.7% | incidental (within band) |
| aware-seen/legion/back | 13 | 16 | +23.1% | incidental (within band) |
| aware-seen/legion/backward | 82 | 104 | +26.8% | incidental (within band) |
| aware-seen/legion/churn.bin0 | 1664098 | 1996176 | +20.0% | incidental (within band) |
| aware-seen/legion/churn.bin1 | 52814 | 47531 | -10.0% | incidental (within band) |
| aware-seen/legion/click.a00.done | 2250 | 2608 | +15.9% | intended |
| aware-seen/legion/click.b00.done | 2702 | 2205 | -18.4% | intended |
| aware-seen/legion/click.b00.t90 | 2027 | 1871 | -7.7% | intended |
| aware-seen/legion/detour_permille | 31 | 27 | -12.9% | incidental (within band) |
| aware-seen/legion/engagement_on_flowing | 42 | 47 | +11.9% | incidental (within band) |
| aware-seen/legion/flip_rate_per30_permille | 67 | 57 | -14.9% | incidental (within band) |
| aware-seen/legion/flips | 188 | 157 | -16.5% | incidental (within band) |
| aware-seen/legion/g.a00.complete_dist_max | 5 | 6 | +20.0% | incidental (within band) |
| aware-seen/legion/g.a00.complete_dist_median | 5 | 6 | +20.0% | incidental (within band) |
| aware-seen/legion/g.a01.done | 2162 | 1888 | -12.7% | incidental (within band) |
| aware-seen/legion/g.a01.t50 | 2162 | 1888 | -12.7% | incidental (within band) |
| aware-seen/legion/g.a02.done | 1991 | 2108 | +5.9% | incidental (within band) |
| aware-seen/legion/g.a02.t50 | 1991 | 2108 | +5.9% | incidental (within band) |
| aware-seen/legion/g.a02.t90 | 1982 | 2108 | +6.4% | incidental (within band) |
| aware-seen/legion/g.a03.done | 1942 | 1738 | -10.5% | incidental (within band) |
| aware-seen/legion/g.a03.t50 | 1942 | 1738 | -10.5% | incidental (within band) |
| aware-seen/legion/g.a03.t90 | 1885 | 1768 | -6.2% | incidental (within band) |
| aware-seen/legion/g.a06.t90 | 1981 | 1821 | -8.1% | incidental (within band) |
| aware-seen/legion/g.a07.t90 | 1847 | 1982 | +7.3% | incidental (within band) |
| aware-seen/legion/g.a08.complete_dist_max | 1 | 2 | +100.0% | incidental (within band) |
| aware-seen/legion/g.a08.complete_dist_median | 1 | 2 | +100.0% | incidental (within band) |
| aware-seen/legion/g.a10.t90 | 1658 | 1564 | -5.7% | incidental (within band) |
| aware-seen/legion/g.a16.done | 1770 | 1636 | -7.6% | incidental (within band) |
| aware-seen/legion/g.a16.t50 | 1770 | 1636 | -7.6% | incidental (within band) |
| aware-seen/legion/g.b09.done | 1701 | 1606 | -5.6% | incidental (within band) |
| aware-seen/legion/g.b09.t50 | 1701 | 1606 | -5.6% | incidental (within band) |
| aware-seen/legion/g.b10.done | 1728 | 1614 | -6.6% | incidental (within band) |
| aware-seen/legion/g.b10.t50 | 1728 | 1614 | -6.6% | incidental (within band) |
| aware-seen/legion/g.b11.done | 1782 | 1660 | -6.8% | incidental (within band) |
| aware-seen/legion/g.b11.t50 | 1782 | 1660 | -6.8% | incidental (within band) |
| aware-seen/legion/g.b13.done | 1618 | 1509 | -6.7% | incidental (within band) |
| aware-seen/legion/g.b13.t50 | 1618 | 1509 | -6.7% | incidental (within band) |
| aware-seen/legion/g.b14.complete_dist_max | 3 | 1 | -66.7% | incidental (within band) |
| aware-seen/legion/g.b14.complete_dist_median | 3 | 1 | -66.7% | incidental (within band) |
| aware-seen/legion/g.b14.done | 1847 | 1702 | -7.9% | incidental (within band) |
| aware-seen/legion/g.b14.t50 | 1847 | 1702 | -7.9% | incidental (within band) |
| aware-seen/legion/g.b16.complete_dist_max | 6 | 3 | -50.0% | incidental (within band) |
| aware-seen/legion/g.b16.complete_dist_median | 6 | 3 | -50.0% | incidental (within band) |
| aware-seen/legion/g.b16.done | 2027 | 1745 | -13.9% | incidental (within band) |
| aware-seen/legion/g.b16.t50 | 2027 | 1745 | -13.9% | incidental (within band) |
| aware-seen/legion/g.b18.done | 1590 | 1712 | +7.7% | incidental (within band) |
| aware-seen/legion/g.b18.t50 | 1590 | 1712 | +7.7% | incidental (within band) |
| aware-seen/legion/g.b19.done | 1712 | 1624 | -5.1% | incidental (within band) |
| aware-seen/legion/g.b19.t50 | 1712 | 1624 | -5.1% | incidental (within band) |
| aware-seen/legion/g.b21.complete_dist_max | 3 | 5 | +66.7% | incidental (within band) |
| aware-seen/legion/g.b21.complete_dist_median | 3 | 5 | +66.7% | incidental (within band) |
| aware-seen/legion/g.b21.done | 1914 | 2027 | +5.9% | incidental (within band) |
| aware-seen/legion/g.b21.t50 | 1914 | 2027 | +5.9% | incidental (within band) |
| aware-seen/legion/g.b22.t90 | 1855 | 1972 | +6.3% | incidental (within band) |
| aware-seen/legion/g.b23.done | 1742 | 1847 | +6.0% | incidental (within band) |
| aware-seen/legion/g.b23.t50 | 1742 | 1847 | +6.0% | incidental (within band) |
| aware-seen/legion/gate.mid.files_x100 | 496 | 471 | -5.0% | incidental (within band) |
| aware-seen/legion/gate.mid.samples | 700 | 591 | -15.6% | incidental (within band) |
| aware-seen/legion/pair.contacts.contacts | 358 | 304 | -15.1% | intended |
| aware-seen/legion/pair.contacts.permille_x100 | 390 | 336 | -13.8% | intended |
| aware-seen/legion/sideways | 952 | 874 | -8.2% | incidental (within band) |
| aware-seen/legion/stop_go | 200 | 162 | -19.0% | incidental (within band) |
| aware-seen/legion/stopped_permille | 49 | 41 | -16.3% | intended |
| aware-seen/legion/waiting_held | 2703 | 2214 | -18.1% | incidental (within band) |
| aware-seen/legion/wall_near_samples | 98 | 110 | +12.2% | incidental (within band) |
| aware-seen/legion/work.completion_dist_1.total | 6 | 7 | +16.7% | incidental (within band) |
| aware-seen/legion/work.completion_dist_sum.max | 11 | 12 | +9.1% | incidental (within band) |
| aware-seen/legion/work.contact_arrivals.max | 2 | 3 | +50.0% | incidental (within band) |
| aware-seen/legion/work.contact_arrivals.total | 10 | 9 | -10.0% | incidental (within band) |
| aware-seen/legion/work.detour_cells.max | 1060 | 1118 | +5.5% | incidental (within band) |
| aware-seen/legion/work.detour_cells.total | 10292 | 9318 | -9.5% | incidental (within band) |
| aware-seen/legion/work.detours.total | 19 | 20 | +5.3% | incidental (within band) |
| aware-seen/legion/work.held_rechecks.p99 | 1 | 0 | -100.0% | incidental (within band) |
| aware-seen/legion/work.held_rechecks.total | 59 | 30 | -49.2% | incidental (within band) |
| aware-seen/legion/work.holds.total | 202 | 166 | -17.8% | incidental (within band) |
| aware-seen/legion/work.legion_total.p99 | 2068 | 2612 | +26.3% | incidental (within band) |
| aware-seen/legion/work.legion_total.total | 1711803 | 2039915 | +19.2% | incidental (within band) |
| aware-seen/legion/work.line_sweeps.total | 69635 | 119641 | +71.8% | incidental (within band) |
| aware-seen/legion/work.move_calls_by_state_2.max | 16 | 17 | +6.2% | incidental (within band) |
| aware-seen/legion/work.move_calls_by_state_2.p99 | 13 | 14 | +7.7% | incidental (within band) |
| aware-seen/legion/work.move_calls_by_state_2.total | 4126 | 3400 | -17.6% | incidental (within band) |
| aware-seen/legion/work.pass_scan_cells.max | 466 | 504 | +8.2% | incidental (within band) |
| aware-seen/legion/work.pass_scan_cells.p99 | 384 | 408 | +6.2% | incidental (within band) |
| aware-seen/legion/work.pass_scan_cells.total | 128055 | 105618 | -17.5% | incidental (within band) |
| aware-seen/legion/work.pass_scans.p99 | 16 | 17 | +6.2% | incidental (within band) |
| aware-seen/legion/work.pass_scans.total | 5386 | 4447 | -17.4% | incidental (within band) |
| aware-seen/legion/work.rechoice_bfs_cells.max | 726 | 862 | +18.7% | incidental (within band) |
| aware-seen/legion/work.rechoice_bfs_cells.total | 3860 | 3385 | -12.3% | incidental (within band) |
| aware-seen/legion/work.sched_group_visits.total | 10 | 8 | -20.0% | incidental (within band) |
| aware-seen/legion/work.slides.total | 406 | 384 | -5.4% | incidental (within band) |
| aware-seen/legion/work.slot_search_cells.max | 2220 | 1258 | -43.3% | incidental (within band) |
| aware-seen/legion/work.slot_search_cells.total | 9146 | 8671 | -5.2% | incidental (within band) |
| aware-seen/legion/work.trace_cells.p99 | 1949 | 2563 | +31.5% | incidental (within band) |
| aware-seen/legion/work.trace_cells.total | 1041926 | 1372232 | +31.7% | incidental (within band) |
| battle-assault/legion/back | 2905 | 2745 | -5.5% | incidental (within band) |
| battle-assault/legion/churn.bin0 | 98903826 | 83633444 | -15.4% | incidental (within band) |
| battle-assault/legion/churn.bin1 | 86787366 | 79202626 | -8.7% | incidental (within band) |
| battle-assault/legion/g.A3.left_behind | 3 | 4 | +33.3% | incidental (within band) |
| battle-assault/legion/g.A4.dead | 25 | 22 | -12.0% | incidental (within band) |
| battle-assault/legion/g.A4.left_behind | 30 | 33 | +10.0% | incidental (within band) |
| battle-assault/legion/g.A5.left_behind | 3 | 2 | -33.3% | incidental (within band) |
| battle-assault/legion/g.A6.dead | 47 | 44 | -6.4% | incidental (within band) |
| battle-assault/legion/g.A6.left_behind | 8 | 11 | +37.5% | incidental (within band) |
| battle-assault/legion/g.A9.left_behind | 3 | 5 | +66.7% | incidental (within band) |
| battle-assault/legion/g.B0.left_behind | 0 | 1 | new | incidental (within band) |
| battle-assault/legion/g.B1.left_behind | 7 | 8 | +14.3% | incidental (within band) |
| battle-assault/legion/g.B2.left_behind | 7 | 5 | -28.6% | incidental (within band) |
| battle-assault/legion/g.B4.left_behind | 7 | 6 | -14.3% | incidental (within band) |
| battle-assault/legion/g.B6.dead | 18 | 21 | +16.7% | incidental (within band) |
| battle-assault/legion/g.B6.left_behind | 37 | 34 | -8.1% | incidental (within band) |
| battle-assault/legion/g.B7.dead | 26 | 28 | +7.7% | incidental (within band) |
| battle-assault/legion/g.B7.left_behind | 29 | 27 | -6.9% | incidental (within band) |
| battle-assault/legion/g.B8.left_behind | 0 | 1 | new | incidental (within band) |
| battle-assault/legion/g.B9.dead | 22 | 28 | +27.3% | incidental (within band) |
| battle-assault/legion/g.B9.left_behind | 33 | 27 | -18.2% | incidental (within band) |
| battle-assault/legion/parked_held | 119179 | 111107 | -6.8% | incidental (within band) |
| battle-assault/legion/parked_no_progress | 168 | 210 | +25.0% | incidental (within band) |
| battle-assault/legion/reversals | 1628 | 1760 | +8.1% | incidental (within band) |
| battle-assault/legion/still900_ever | 2 | 1 | -50.0% | incidental (within band) |
| battle-assault/legion/wall_touch_near_permille | 73 | 79 | +8.2% | incidental (within band) |
| battle-assault/legion/work.blocked_rerequests.total | 13 | 7 | -46.2% | incidental (within band) |
| battle-assault/legion/work.detour_cells.max | 3207 | 3682 | +14.8% | incidental (within band) |
| battle-assault/legion/work.field_work.total | 177071800 | 155462176 | -12.2% | incidental (within band) |
| battle-assault/legion/work.field_work_first_slot.total | 32793168 | 31151864 | -5.0% | incidental (within band) |
| battle-assault/legion/work.field_work_refresh_idle.p99 | 70928 | 56488 | -20.4% | incidental (within band) |
| battle-assault/legion/work.field_work_refresh_idle.total | 7582592 | 7008792 | -7.6% | incidental (within band) |
| battle-assault/legion/work.field_work_refresh_moving.p99 | 96000 | 40680 | -57.6% | incidental (within band) |
| battle-assault/legion/work.field_work_refresh_moving.total | 22336592 | 4059784 | -81.8% | incidental (within band) |
| battle-assault/legion/work.fields_paused.total | 95 | 101 | +6.3% | incidental (within band) |
| battle-assault/legion/work.fields_shared.total | 19 | 21 | +10.5% | incidental (within band) |
| battle-assault/legion/work.fields_started_by_kind_2.total | 238 | 215 | -9.7% | incidental (within band) |
| battle-assault/legion/work.legion_total.total | 233653403 | 212031355 | -9.3% | incidental (within band) |
| battle-assault/legion/work.mission_legs_4.max | 12 | 11 | -8.3% | incidental (within band) |
| battle-assault/legion/work.paused_resumes.total | 4 | 7 | +75.0% | incidental (within band) |
| battle-assault/legion/work.refresh_completed.total | 334 | 297 | -11.1% | incidental (within band) |
| battle-assault/legion/work.refresh_deferred.max | 5 | 3 | -40.0% | incidental (within band) |
| battle-assault/legion/work.refresh_deferred.p99 | 2 | 0 | -100.0% | incidental (within band) |
| battle-assault/legion/work.refresh_deferred.total | 176 | 17 | -90.3% | incidental (within band) |
| battle-assault/legion/work.sched_group_visits.max | 11 | 10 | -9.1% | incidental (within band) |
| battle-assault/legion/work.sched_group_visits.total | 5534 | 5049 | -8.8% | incidental (within band) |
| battle-assault/legion/work.share_scan_iters.max | 9 | 8 | -11.1% | incidental (within band) |
| battle-assault/legion/work.share_scan_iters.p99 | 4 | 2 | -50.0% | incidental (within band) |
| battle-assault/legion/work.share_scan_iters.total | 431 | 174 | -59.6% | incidental (within band) |
| crosslong/legion/aim_reversals | 395 | 297 | -24.8% | intended |
| crosslong/legion/aware.builds | 1 | 2 | +100.0% | intended |
| crosslong/legion/aware.latency_n | 2 | 3 | +50.0% | intended |
| crosslong/legion/aware.latency_sum | 8 | 12 | +50.0% | intended |
| crosslong/legion/aware.replans | 2 | 3 | +50.0% | intended |
| crosslong/legion/aware.work | 8957 | 26733 | +198.5% | intended |
| crosslong/legion/aware.work_max | 116 | 388 | +234.5% | intended |
| crosslong/legion/back | 166 | 130 | -21.7% | intended |
| crosslong/legion/backward | 191 | 106 | -44.5% | intended |
| crosslong/legion/behind.wait.detour_permille | 35 | 175 | +400.0% | intended |
| crosslong/legion/behind.wait.extra_cells | 6 | 30 | +400.0% | intended |
| crosslong/legion/behind.wait.path_cells | 181 | 203 | +12.2% | intended |
| crosslong/legion/behind.wait.wait_member_ticks | 13609 | 0 | -100.0% | intended |
| crosslong/legion/behind.wait.wait_run_max | 603 | 0 | -100.0% | intended |
| crosslong/legion/behind.wait.wait_ticks | 1035 | 0 | -100.0% | intended |
| crosslong/legion/behind.wait.waits | 3 | 0 | -100.0% | intended |
| crosslong/legion/churn.bin0 | 9101061 | 10359839 | +13.8% | intended |
| crosslong/legion/churn.bin1 | 2409561 | 1465187 | -39.2% | intended |
| crosslong/legion/churn.bin2 | 97594 | 26291 | -73.1% | intended |
| crosslong/legion/contact_other_permille | 41 | 0 | -100.0% | intended |
| crosslong/legion/crawl_samples | 4324 | 2410 | -44.3% | intended |
| crosslong/legion/decision_samples | 58214 | 54917 | -5.7% | intended |
| crosslong/legion/detour_permille | 17 | 28 | +64.7% | intended |
| crosslong/legion/flip_rate_per30_permille | 81 | 38 | -53.1% | intended |
| crosslong/legion/flips | 1574 | 699 | -55.6% | intended |
| crosslong/legion/follow_chain_mean_x100 | 658 | 718 | +9.1% | intended |
| crosslong/legion/g.A.complete_outside_radius | 0 | 1 | new | intended |
| crosslong/legion/g.A.done | 5358 | -1 | -100.0% | intended |
| crosslong/legion/g.A.left_behind | 0 | 1 | new | intended |
| crosslong/legion/g.A.t90 | 3378 | 2881 | -14.7% | intended |
| crosslong/legion/g.B.complete_dist_max | 5 | 6 | +20.0% | intended |
| crosslong/legion/g.B.done | 3686 | 2831 | -23.2% | intended |
| crosslong/legion/g.B.t90 | 3306 | 2786 | -15.7% | intended |
| crosslong/legion/parked_held | 772 | 670 | -13.2% | intended |
| crosslong/legion/reversals | 22 | 16 | -27.3% | intended |
| crosslong/legion/spacing_samples | 58214 | 54917 | -5.7% | intended |
| crosslong/legion/statue_ticks | 43363 | 24476 | -43.6% | intended |
| crosslong/legion/stop_go | 1284 | 818 | -36.3% | intended |
| crosslong/legion/stopped_permille | 75 | 42 | -44.0% | intended |
| crosslong/legion/waiting_held | 32912 | 15864 | -51.8% | intended |
| crosslong/legion/wall_near_samples | 366 | 336 | -8.2% | intended |
| crosslong/legion/work.arrivals.max | 9 | 10 | +11.1% | intended |
| crosslong/legion/work.aware_pairs.p99 | 2 | 4 | +100.0% | intended |
| crosslong/legion/work.aware_pairs.total | 310 | 289 | -6.8% | intended |
| crosslong/legion/work.completion_dist_2.total | 14 | 13 | -7.1% | intended |
| crosslong/legion/work.completion_dist_4.max | 5 | 7 | +40.0% | intended |
| crosslong/legion/work.completion_dist_4.p99 | 1 | 0 | -100.0% | intended |
| crosslong/legion/work.completion_dist_sum.max | 108 | 146 | +35.2% | intended |
| crosslong/legion/work.completion_dist_sum.p99 | 17 | 13 | -23.5% | intended |
| crosslong/legion/work.contact_arrivals.max | 9 | 10 | +11.1% | intended |
| crosslong/legion/work.contact_arrivals.total | 155 | 165 | +6.5% | intended |
| crosslong/legion/work.crowd_window_ring_cells.max | 864 | 800 | -7.4% | intended |
| crosslong/legion/work.crowd_window_ring_cells.p99 | 320 | 160 | -50.0% | intended |
| crosslong/legion/work.crowd_window_ring_cells.total | 55680 | 29280 | -47.4% | intended |
| crosslong/legion/work.detour_cells.max | 1710 | 1081 | -36.8% | intended |
| crosslong/legion/work.detour_cells.p99 | 564 | 408 | -27.7% | intended |
| crosslong/legion/work.detour_cells.total | 175934 | 66160 | -62.4% | intended |
| crosslong/legion/work.detours.total | 371 | 161 | -56.6% | intended |
| crosslong/legion/work.field_work.total | 1627920 | 2020080 | +24.1% | intended |
| crosslong/legion/work.field_work_refresh_moving.total | 784320 | 1176480 | +50.0% | intended |
| crosslong/legion/work.fields_built.total | 4 | 5 | +25.0% | intended |
| crosslong/legion/work.fields_started_by_kind_1.total | 4 | 5 | +25.0% | intended |
| crosslong/legion/work.held_rechecks.max | 12 | 17 | +41.7% | intended |
| crosslong/legion/work.held_rechecks.p99 | 8 | 10 | +25.0% | intended |
| crosslong/legion/work.held_rechecks.total | 2495 | 2862 | +14.7% | intended |
| crosslong/legion/work.holds.p99 | 3 | 2 | -33.3% | intended |
| crosslong/legion/work.holds.total | 1380 | 920 | -33.3% | intended |
| crosslong/legion/work.line_sweeps.p99 | 68 | 107 | +57.4% | intended |
| crosslong/legion/work.line_sweeps.total | 90898 | 132508 | +45.8% | intended |
| crosslong/legion/work.mission_arrivals_1.max | 9 | 10 | +11.1% | intended |
| crosslong/legion/work.move_calls_by_state_2.max | 46 | 62 | +34.8% | intended |
| crosslong/legion/work.move_calls_by_state_2.p99 | 34 | 47 | +38.2% | intended |
| crosslong/legion/work.move_calls_by_state_2.total | 44069 | 22886 | -48.1% | intended |
| crosslong/legion/work.moves.total | 580555 | 547774 | -5.6% | intended |
| crosslong/legion/work.pass_scan_cells.p99 | 624 | 504 | -19.2% | intended |
| crosslong/legion/work.pass_scan_cells.total | 468956 | 296928 | -36.7% | intended |
| crosslong/legion/work.pass_scans.p99 | 26 | 21 | -19.2% | intended |
| crosslong/legion/work.pass_scans.total | 19540 | 12372 | -36.7% | intended |
| crosslong/legion/work.rechoice_bfs_cells.p99 | 174 | 74 | -57.5% | intended |
| crosslong/legion/work.rechoice_bfs_cells.total | 34499 | 27722 | -19.6% | intended |
| crosslong/legion/work.refresh_completed.total | 2 | 3 | +50.0% | intended |
| crosslong/legion/work.sched_group_visits.total | 23 | 32 | +39.1% | intended |
| crosslong/legion/work.share_scan_iters.total | 4 | 6 | +50.0% | intended |
| crosslong/legion/work.slides.max | 7 | 8 | +14.3% | intended |
| crosslong/legion/work.slides.p99 | 4 | 5 | +25.0% | intended |
| crosslong/legion/work.slides.total | 3038 | 3303 | +8.7% | intended |
| crosslong/legion/work.slot_search_cells.p99 | 248 | 197 | -20.6% | intended |
| crosslong/legion/work.slot_search_cells.total | 74657 | 68589 | -8.1% | intended |
| densehead/legion/back | 1023 | 915 | -10.6% | incidental (within band) |
| densehead/legion/churn.bin0 | 12703883 | 15481426 | +21.9% | incidental (within band) |
| densehead/legion/contact_other_permille | 140 | 199 | +42.1% | intended |
| densehead/legion/engagement_on_flowing | 340 | 414 | +21.8% | incidental (within band) |
| densehead/legion/flip_rate_per30_permille | 246 | 231 | -6.1% | incidental (within band) |
| densehead/legion/flips | 11711 | 11125 | -5.0% | incidental (within band) |
| densehead/legion/follow_chain_max | 11 | 13 | +18.2% | incidental (within band) |
| densehead/legion/g.A.complete_dist_max | 23 | 21 | -8.7% | incidental (within band) |
| densehead/legion/g.B.complete_dist_max | 24 | 21 | -12.5% | incidental (within band) |
| densehead/legion/g.B.complete_dist_median | 15 | 14 | -6.7% | incidental (within band) |
| densehead/legion/gate.mid.crossings | 523 | 447 | -14.5% | intended |
| densehead/legion/gate.mid.pairs | 6500 | 7430 | +14.3% | intended |
| densehead/legion/open_still900 | 1 | 5 | +400.0% | incidental (within band) |
| densehead/legion/parked_held | 147161 | 179980 | +22.3% | incidental (within band) |
| densehead/legion/region.goalB.inside | 79 | 87 | +10.1% | incidental (within band) |
| densehead/legion/reversals | 73 | 61 | -16.4% | incidental (within band) |
| densehead/legion/side.farB.low | 88 | 95 | +8.0% | incidental (within band) |
| densehead/legion/side.farB.t90 | 443 | 475 | +7.2% | incidental (within band) |
| densehead/legion/sideways | 25312 | 23446 | -7.4% | incidental (within band) |
| densehead/legion/still900_ever | 33 | 43 | +30.3% | incidental (within band) |
| densehead/legion/stop_go | 7793 | 7397 | -5.1% | incidental (within band) |
| densehead/legion/work.arrivals.max | 4 | 3 | -25.0% | incidental (within band) |
| densehead/legion/work.completion_dist_1.max | 1 | 0 | -100.0% | incidental (within band) |
| densehead/legion/work.completion_dist_1.total | 1 | 0 | -100.0% | incidental (within band) |
| densehead/legion/work.completion_dist_3.total | 15 | 14 | -6.7% | incidental (within band) |
| densehead/legion/work.completion_dist_4.total | 60 | 64 | +6.7% | incidental (within band) |
| densehead/legion/work.completion_dist_5.total | 67 | 60 | -10.4% | incidental (within band) |
| densehead/legion/work.completion_dist_sum.max | 56 | 61 | +8.9% | incidental (within band) |
| densehead/legion/work.contact_arrivals.max | 4 | 3 | -25.0% | incidental (within band) |
| densehead/legion/work.contact_arrivals.total | 46 | 40 | -13.0% | incidental (within band) |
| densehead/legion/work.detour_cells.max | 1906 | 2121 | +11.3% | incidental (within band) |
| densehead/legion/work.held_rechecks.max | 124 | 133 | +7.3% | incidental (within band) |
| densehead/legion/work.held_rechecks.p99 | 116 | 128 | +10.3% | incidental (within band) |
| densehead/legion/work.held_rechecks.total | 217524 | 253571 | +16.6% | incidental (within band) |
| densehead/legion/work.holds.max | 17 | 20 | +17.6% | incidental (within band) |
| densehead/legion/work.holds.p99 | 8 | 7 | -12.5% | incidental (within band) |
| densehead/legion/work.holds.total | 7927 | 7517 | -5.2% | incidental (within band) |
| densehead/legion/work.legion_total.p99 | 15612 | 19886 | +27.4% | incidental (within band) |
| densehead/legion/work.legion_total.total | 22841945 | 25725392 | +12.6% | incidental (within band) |
| densehead/legion/work.line_sweeps.max | 1060 | 1124 | +6.0% | incidental (within band) |
| densehead/legion/work.line_sweeps.p99 | 907 | 1035 | +14.1% | incidental (within band) |
| densehead/legion/work.line_sweeps.total | 423694 | 710465 | +67.7% | incidental (within band) |
| densehead/legion/work.mission_arrivals_1.max | 4 | 3 | -25.0% | incidental (within band) |
| densehead/legion/work.outside_area_completions.total | 8 | 2 | -75.0% | incidental (within band) |
| densehead/legion/work.pass_scan_cells.p99 | 1153 | 1344 | +16.6% | incidental (within band) |
| densehead/legion/work.pass_scan_cells.total | 2160052 | 2420223 | +12.0% | incidental (within band) |
| densehead/legion/work.pass_scans.p99 | 49 | 57 | +16.3% | incidental (within band) |
| densehead/legion/work.pass_scans.total | 92534 | 103138 | +11.5% | incidental (within band) |
| densehead/legion/work.rechoice_bfs_cells.max | 2496 | 2898 | +16.1% | incidental (within band) |
| densehead/legion/work.rechoice_bfs_cells.p99 | 540 | 569 | +5.4% | incidental (within band) |
| densehead/legion/work.slot_search_cells.p99 | 817 | 729 | -10.8% | incidental (within band) |
| densehead/legion/work.trace_cells.max | 21854 | 26957 | +23.4% | incidental (within band) |
| densehead/legion/work.trace_cells.p99 | 13970 | 18078 | +29.4% | incidental (within band) |
| densehead/legion/work.trace_cells.total | 17119926 | 19875581 | +16.1% | incidental (within band) |
| mazeapproach/legion/approach.stopwait_max | 8 | 14 | +75.0% | incidental (within band) |
| mazeapproach/legion/approach.stopwait_p50 | 5 | 4 | -20.0% | incidental (within band) |
| mazeapproach/legion/approach.stopwait_p90 | 8 | 14 | +75.0% | incidental (within band) |
| mazeapproach/legion/g.A.complete_dist_median | 11 | 12 | +9.1% | incidental (within band) |
| mazeapproach/legion/statue_ticks | 0 | 2 | new | incidental (within band) |
| mazeapproach/legion/stopped_permille | 1 | 0 | -100.0% | incidental (within band) |
| mazeapproach/legion/work.move_calls_by_state_2.total | 7 | 6 | -14.3% | incidental (within band) |
| ringcross/legion/churn.bin1 | 2214458 | 2435988 | +10.0% | incidental (within band) |
| ringcross/legion/churn.bin2 | 290395 | 511618 | +76.2% | incidental (within band) |
| ringcross/legion/work.field_work.total | 529192 | 970776 | +83.4% | incidental (within band) |
| ringcross/legion/work.fields_built.total | 2 | 4 | +100.0% | incidental (within band) |
| ringcross/legion/work.fields_started_by_kind_4.total | 1 | 3 | +200.0% | incidental (within band) |
| ringcross/legion/work.legion_total.total | 2943661 | 3386399 | +15.0% | incidental (within band) |
| ringcross/legion/work.sched_group_visits.max | 1 | 2 | +100.0% | incidental (within band) |
| ringcross/legion/work.sched_group_visits.total | 2 | 12 | +500.0% | incidental (within band) |
| unreach-200/legion/aim_reversals | 3 | 4 | +33.3% | incidental (within band) |
| unreach-200/legion/approach.stopwait_max | 157 | 103 | -34.4% | incidental (within band) |
| unreach-200/legion/approach.stopwait_p90 | 43 | 33 | -23.3% | incidental (within band) |
| unreach-200/legion/clearance_p10_x100 | 300 | 400 | +33.3% | incidental (within band) |
| unreach-200/legion/flips | 323 | 345 | +6.8% | incidental (within band) |
| unreach-200/legion/follow_chain_mean_x100 | 941 | 879 | -6.6% | incidental (within band) |
| unreach-200/legion/statue_ticks | 2049 | 1324 | -35.4% | intended |
| unreach-200/legion/stopped_permille | 114 | 101 | -11.4% | incidental (within band) |
| unreach-200/legion/waiting_held | 6557 | 5256 | -19.8% | incidental (within band) |
| unreach-200/legion/wall_near_samples | 175 | 126 | -28.0% | incidental (within band) |
| unreach-200/legion/wall_touch_near_permille | 25 | 0 | -100.0% | intended |
| unreach-200/legion/wall_touch_permille | 1 | 0 | -100.0% | intended |
| unreach-200/legion/work.crowd_window_ring_cells.max | 802 | 720 | -10.2% | incidental (within band) |
| unreach-200/legion/work.crowd_window_ring_cells.p99 | 432 | 324 | -25.0% | incidental (within band) |
| unreach-200/legion/work.crowd_window_ring_cells.total | 88427 | 75782 | -14.3% | incidental (within band) |
| unreach-200/legion/work.detour_cells.total | 11381 | 14952 | +31.4% | incidental (within band) |
| unreach-200/legion/work.detours.total | 34 | 46 | +35.3% | incidental (within band) |
| unreach-200/legion/work.held_rechecks.max | 12 | 10 | -16.7% | incidental (within band) |
| unreach-200/legion/work.held_rechecks.p99 | 4 | 2 | -50.0% | incidental (within band) |
| unreach-200/legion/work.held_rechecks.total | 888 | 617 | -30.5% | incidental (within band) |
| unreach-200/legion/work.holds.max | 10 | 9 | -10.0% | incidental (within band) |
| unreach-200/legion/work.line_sweeps.max | 128 | 105 | -18.0% | incidental (within band) |
| unreach-200/legion/work.move_calls_by_state_2.max | 81 | 73 | -9.9% | incidental (within band) |
| unreach-200/legion/work.move_calls_by_state_2.p99 | 55 | 44 | -20.0% | incidental (within band) |
| unreach-200/legion/work.move_calls_by_state_2.total | 10821 | 9809 | -9.4% | incidental (within band) |
| unreach-200/legion/work.move_calls_by_state_5.max | 8 | 9 | +12.5% | incidental (within band) |
| unreach-200/legion/work.pass_scan_cells.max | 144 | 192 | +33.3% | incidental (within band) |
| unreach-200/legion/work.pass_scan_cells.p99 | 24 | 0 | -100.0% | incidental (within band) |
| unreach-200/legion/work.pass_scan_cells.total | 4800 | 5736 | +19.5% | incidental (within band) |
| unreach-200/legion/work.pass_scans.max | 6 | 8 | +33.3% | incidental (within band) |
| unreach-200/legion/work.pass_scans.p99 | 1 | 0 | -100.0% | incidental (within band) |
| unreach-200/legion/work.pass_scans.total | 200 | 239 | +19.5% | incidental (within band) |
| unreach-200/legion/work.slides.max | 19 | 21 | +10.5% | incidental (within band) |
| unreach-200/legion/work.slides.p99 | 7 | 8 | +14.3% | incidental (within band) |
| unreach-200/legion/work.slides.total | 1933 | 2119 | +9.6% | incidental (within band) |
| unreach-200/legion/work.trapped.max | 8 | 9 | +12.5% | incidental (within band) |
1017 keys moved by more than 5%; 151 intended, 866 incidental

