# crowdbench turn rate: 10000 -> 2500 (2026-10-08)

A one-time record for the Legion plan's turn-rate decision (LEGION-PLAN section 0,
decision 10; 3.0 "Audit numbers measured at crowdbench turnRate 10000"; C26). The
decision: synthetic crowdbench movers turn at 2500 BAM/tick instead of an instant
10000, so the benchmark pays facing costs like real units.

## What actually changed

The premise behind the conversion step was only half true, and the record below
shows it:

- **The matrix movers were already at 2500.** `crowdbench_matrix.h` `mover()` has
  set `turnRate = turnInPlaceRate = 2500` since before the audited main
  (`b8a4110`). Every `--mode` run, the screen, the scoreboard and every audit
  number (MV-03, MV-06, MV-11, T3's step-0 hashes) ran at 2500. The audit hashes
  reproduce bit for bit at the default (table below).
- **The 10000 was the legacy scenario soldier** (`tools/crowdbench.cpp` `soldier()`,
  used only by `crowdbench [columns|choke|blob|open|maze]`, the pre-matrix
  report). That line is the one this commit changes. Nothing gates on the legacy
  report.
- **So no crowdbench baseline moves.** The matrix rows are byte-identical before
  and after this commit (the 2500 screen of the previous commit's binary and of
  this one: `cmp` equal). No game simulation changes.
- `--turn-rate N` (matrix only) keeps 10000 reachable; the legacy report has no
  flag, so its 10000 numbers are kept in the table below.

Because the audit numbers were taken at 2500, the plan's "convert to the new base"
step is the identity: every audit target keeps its stated number, and the
Retail-parity targets read the same Retail values on this binary. The 10000
columns show what the numbers would have been with an instant turn, for anyone
comparing against an older note that assumed 10000.

The first committed screen base is `tools/scenarios/crowdbench_screen_baseline.jsonl`
(86 cases, 2500, seed 0, 6000 ticks, deterministic keys only, including every
`legion_*` counter from `LegionNavigator::forEachStat`). Two screen runs of this
binary at 2500 are byte-identical.

## Audit numbers at both rates (same binary)

Release `crowdbench`, this commit. "Audit" is the number recorded in
`audit/result.json` on `b8a4110`.

| Item | Command | Audit | 2500 (default) | 10000 | Audit reproduced |
|---|---|---|---|---|---|
| MV-11 / T3 step 0 | legion opposingcolumns 250x8@50 6000 s0 | 886 crossed / 584 arrived, spin 1123, hash 2e4614be25d8a4ec | 886 / 584, spin 1123, 2e4614be25d8a4ec | 880 / 586, spin 53, 6741cd1458518b2b | exact at 2500 |
| MV-11 | legion same, s1 | (= s0) | 886 / 584 (= s0) | 880 / 586 (= s0) | exact |
| MV-11 | legion same, s2 | 843 / 544, spin 710, hash c4fe637e90a1eba2 | 843 / 544, spin 710, c4fe637e90a1eba2 | 885 / 612, spin 810 | exact at 2500 |
| MV-11 | 3-seed mean crossed (target base) | 872 | 871.7 | 881.7 | exact |
| MV-11 | retail same, s0 | 1000 / 493, spin 8583, hash 66e1eac3fd30fccd | 1000 / 493, 8583, 66e1eac3fd30fccd | 1000 / 491, 9181 | exact at 2500 |
| MV-11 | retail same, s2 | 1000 / 488, spin 9007, hash 5315719ec9bcb0be | 1000 / 488, 9007, 5315719ec9bcb0be | 1000 / 511, 9171 | exact at 2500 |
| MV-11 parity | opposingcolumns 250x1@50 6000 s0, crossed legion vs retail | 110 vs 125 | 110 vs 125 | 115 vs 125 | exact at 2500 |
| MV-03 / T3 step 0 | legion opposingcolumns 2000x1@100 12000 s0 | 1194 crossed / 603 arrived, hash d566118a6a050478 | 1194 / 603, d566118a6a050478, last cross 10307 | 1203 / 634, a1126dd464c68fd6, last cross 8443 | exact at 2500 |
| MV-03 | legion same, 24000 ticks | 1194 / 618, last cross 10307, hash 27709d4e3e2481cb | 1194 / 618, 10307, 27709d4e3e2481cb | 1203 / 636, 8443 | exact at 2500 |
| MV-03 | retail same, 12000 / 24000, crossed | 968 / 1178 | 968 / 1178 | 975 / 1157 | exact at 2500 |
| MV-06 | legion doors 200x1 6000 s0, share of Moving samples at cap/8 | 8.8% | 7.6% (hash 870635d6842980a2) | 7.7% | same run, different counter (below) |
| MV-06 | legion doors 2000x1 6000 s0, same share | 20.4% | 18.3% | 17.9% | different counter (below); no audit dump of this run to compare hashes |

MV-06's 8.8% came from the audit's LDUMP probe (`audit/mv06-reproduce/doors200.gz`:
6327 of 71604 Moving samples with `speed>>10 <= 15`, i.e. under 0.25 px/tick). The
run is the same simulation: the probe run's hash, 870635d6842980a2, is the
screen row's hash. The crowdbench key `cap8_moving_samples / moving_state_samples`
uses a tighter threshold, speed at most cap/8 plus 1/64 px (raw 15769, so 0.2406
px/tick). Most of the gap is the probe's `speed>>10 == 15` bucket (1915 samples), of
which the key counts about 1050 (its sample set differs by 15 of 71604). So the
8.8% / 20.4% targets ("cut by >= 60%") carry over to the key as **7.6% -> <= 3.0%**
and **18.3% -> <= 7.3%**. They do not need a turn-rate conversion.

MV-03's `cross_gap_max` at 2500 is 1693 at 12000 ticks and 13693 at 24000 ticks.
(At 10000 it is 3557 and 15557.)

## Legacy scenario report (`crowdbench` with no `--mode`), old -> new

The only output this commit changes. Release, deterministic, identical on a re-run.

| Scenario | spot 10000 -> 2500 | area | t50 s | travel | work |
|---|---|---|---|---|---|
| opposing columns | 20/32 -> 22/32 | 23/32 -> 24/32 | 47.7 -> 46.5 | x1.23 -> x1.19 | 4515776 -> 4004482 |
| chokepoint (1 door) | 10/24 -> 5/24 | 10/24 -> 5/24 | -- | x1.30 -> x1.27 | 1700205 -> 2153300 |
| group order, one point | 28/32 -> 25/32 | 20/32 -> 18/32 | 41.2 -> 40.7 | x1.12 -> x1.14 | 3972837 -> 3815428 |
| open field | 11/24 -> 10/24 | 17/24 -> 18/24 | -- | x1.05 -> x1.07 | 3637217 -> 4032552 |
| serpentine maze | 11/12 -> 11/12 | 11/12 -> 10/12 | 268 -> 247 | x5.82 -> x5.90 | 20934690 -> 20909433 |

## Screen at both rates (matrix, same binary): what an instant turn would change

`crowdbench_screen.py` with and without `--turn-rate 10000`: 86 cases, seed 0,
6000 ticks, 100% moving. A cell shows `10000 -> 2500`, or one value when both
are equal. The 2500 column is the committed baseline. Every row's hash differs
between the rates (both modes read the movers' turn rate). Cap/8 share is
`cap8_moving_samples / moving_state_samples`. Field work is `legion_field_work`.

| case | arrived | crossed | complete | arr p50 | stalled | spin | crowd held | terrain stuck | cap/8 share | field work | hash |
|---|---|---|---|---|---|---|---|---|---|---|---|
| open/200x1/retail | 200 | 200 | 200 | 3278 -> 3280 | 16716 -> 16710 | 0 | 0 | 0 | 0.0% | - | changed |
| open/200x1/legion | 200 | 200 | 200 | 3312 -> 3314 | 200 | 0 | 0 | 0 | 0.0% | 420080 | changed |
| open/500x4/retail | 2000 | 2000 | 2000 | 3685 -> 3687 | 211685 -> 211535 | 0 | 0 | 0 | 0.0% | - | changed |
| open/500x4/legion | 2000 | 2000 | 2000 | 3733 -> 3735 | 2000 | 0 | 0 | 0 | 0.0% | 12138632 | changed |
| doors/200x1/retail | 54 -> 38 | 200 | 200 | -1 | 85809 -> 85814 | 14509 -> 14980 | 145265 -> 125751 | 194 -> 109 | 0.0% | - | changed |
| doors/200x1/legion | 167 -> 171 | 200 | 172 -> 177 | 4240 -> 4377 | 160034 -> 155829 | 0 | 119869 -> 114919 | 0 | 7.7% -> 7.6% | 204360 | changed |
| doors/500x4/retail | 294 -> 266 | 856 -> 887 | 588 -> 543 | -1 | 5853976 -> 5841905 | 14944 -> 12842 | 4935214 -> 4917367 | 15418 -> 16467 | 0.0% | - | changed |
| doors/500x4/legion | 868 -> 889 | 1819 -> 1830 | 880 -> 895 | -1 | 3231424 -> 3178877 | 0 | 2710387 -> 2626075 | 0 -> 15 | 11.0% | 2646592 | changed |
| doors/2000x1/retail | 84 -> 87 | 218 -> 246 | 122 -> 135 | -1 | 7887391 -> 7753377 | 4439 -> 4197 | 7294318 -> 7130494 | 3680 -> 3241 | 0.0% | - | changed |
| doors/2000x1/legion | 220 -> 249 | 506 -> 498 | 229 -> 253 | -1 | 5758883 -> 5761651 | 0 | 5284635 -> 5279761 | 1 -> 8 | 17.9% -> 18.3% | 5987904 | changed |
| bridges/200x1/retail | 62 -> 34 | 200 -> 190 | 200 | -1 | 96041 -> 114249 | 10386 -> 12540 | 116687 -> 168681 | 126 -> 5349 | 0.0% | - | changed |
| bridges/200x1/legion | 173 -> 172 | 200 | 176 -> 174 | 4472 -> 4542 | 165353 -> 158837 | 0 | 112418 -> 103182 | 0 | 8.0% -> 7.3% | 178920 | changed |
| bridges/500x4/retail | 351 -> 325 | 768 -> 843 | 482 -> 455 | -1 | 6464007 -> 6353380 | 12016 -> 12656 | 5569644 -> 5336317 | 19348 -> 21737 | 0.0% | - | changed |
| bridges/500x4/legion | 899 -> 888 | 1772 -> 1731 | 911 -> 903 | -1 | 3287361 -> 3337513 | 25 -> 8 | 2582824 -> 2620264 | 67 -> 44 | 11.5% -> 11.7% | 2350912 | changed |
| maze/200x1/retail | 49 -> 43 | 112 -> 107 | 67 -> 62 | -1 | 421196 -> 444843 | 6475 -> 6320 | 244879 -> 263169 | 2250 -> 2353 | 0.0% | - | changed |
| maze/200x1/legion | 78 -> 87 | 145 -> 150 | 79 -> 88 | -1 | 363795 -> 349880 | 0 | 273433 -> 269854 | 4 -> 0 | 7.8% -> 8.5% | 198960 | changed |
| maze/500x4/retail | 95 -> 92 | 285 -> 295 | 96 -> 97 | -1 | 9186578 -> 9253291 | 4347 -> 2982 | 8451643 -> 8552217 | 18105 -> 17389 | 0.0% | - | changed |
| maze/500x4/legion | 196 -> 182 | 481 -> 473 | 197 -> 186 | -1 | 6891688 -> 6906832 | 0 | 6261193 -> 6285419 | 0 | 18.2% -> 18.7% | 2580352 | changed |
| maze/2000x1/retail | 0 | 24 -> 26 | 0 | -1 | 10423066 -> 10501154 | 2031 -> 834 | 10011288 -> 10104948 | 3624 -> 4304 | 0.0% | - | changed |
| maze/2000x1/legion | 0 | 49 -> 48 | 0 | -1 | 8800466 -> 8773151 | 0 | 8246260 -> 8194328 | 0 | 28.1% -> 28.2% | 5858304 | changed |
| opposingcolumns/200x1/retail | 79 -> 72 | 200 | 200 | -1 | 57841 -> 60996 | 11562 -> 14759 | 82129 -> 83969 | 0 | 0.0% | - | changed |
| opposingcolumns/200x1/legion | 199 -> 194 | 200 | 200 -> 198 | 3458 -> 3717 | 25549 -> 77671 | 9 -> 177 | 3843 -> 46643 | 0 | 1.2% -> 3.1% | 840160 | changed |
| opposingcolumns/500x4/retail | 44 -> 22 | 1049 -> 1065 | 132 -> 77 | -1 | 7375004 -> 7424307 | 12861 -> 10714 | 6278029 -> 6307845 | 0 | 0.0% | - | changed |
| opposingcolumns/500x4/legion | 1058 -> 1029 | 1884 -> 1838 | 1076 -> 1052 | 5863 -> 5933 | 3846456 -> 4071833 | 1954 -> 1353 | 3274833 -> 3523845 | 0 | 1.5% -> 1.7% | 12126416 | changed |
| opposingcolumns/2000x1/retail | 5 -> 0 | 885 -> 870 | 5 -> 1 | -1 | 7882278 -> 7850426 | 9192 -> 6904 | 7190290 -> 7090786 | 0 | 0.0% | - | changed |
| opposingcolumns/2000x1/legion | 301 -> 214 | 1100 -> 1050 | 305 -> 217 | -1 | 6617225 -> 6910878 | 571 -> 1220 | 6146549 -> 6479937 | 0 | 0.7% | 10028928 | changed |
| sharedgoal/200x1/retail | 131 -> 119 | 200 | 140 -> 120 | 4998 -> 5237 | 200140 -> 192661 | 9436 -> 15364 | 59853 -> 33192 | 0 | 0.0% | - | changed |
| sharedgoal/200x1/legion | 199 -> 194 | 200 | 199 | 3278 -> 3277 | 16762 -> 15796 | 0 | 9340 -> 15623 | 0 | 3.8% | 420080 | changed |
| sharedgoal/500x4/retail | 56 -> 57 | 973 -> 801 | 63 -> 57 | -1 | 7248600 -> 7552584 | 15885 -> 15902 | 5192528 -> 5560326 | 0 | 0.0% | - | changed |
| sharedgoal/500x4/legion | 1464 -> 1416 | 2000 | 1899 -> 1884 | 3639 -> 3658 | 661620 -> 726019 | 54 -> 0 | 821015 -> 896995 | 0 | 4.8% -> 5.5% | 6466336 | changed |
| sharedgoal/2000x1/retail | 8 -> 5 | 365 -> 424 | 8 -> 5 | -1 | 8862120 -> 8829335 | 6693 -> 7255 | 7419027 -> 7343527 | 0 | 0.0% | - | changed |
| sharedgoal/2000x1/legion | 1007 -> 1001 | 2000 | 1825 -> 1727 | 5612 -> 5943 | 1144708 -> 1162144 | 0 -> 16 | 1486569 -> 1465012 | 0 | 6.0% -> 6.4% | 1253616 | changed |
| mixedfootprints/200x1/retail | 99 -> 91 | 200 | 100 -> 97 | -1 | 212778 -> 234802 | 18618 -> 18257 | 32565 -> 35001 | 0 | 0.0% | - | changed |
| mixedfootprints/200x1/legion | 199 -> 200 | 200 | 199 -> 200 | 3579 -> 3581 | 11950 -> 8685 | 0 | 1256 -> 416 | 0 | 2.4% -> 2.2% | 2206768 | changed |
| mixedfootprints/500x4/retail | 41 -> 44 | 721 -> 686 | 41 -> 44 | -1 | 7732528 -> 7793295 | 11348 -> 10545 | 5731550 -> 5804319 | 0 | 0.0% | - | changed |
| mixedfootprints/500x4/legion | 1883 -> 1870 | 2000 | 1953 -> 1961 | 3767 -> 3778 | 264258 -> 282128 | 115 -> 56 | 147842 -> 178704 | 0 | 2.8% -> 2.7% | 27401120 | changed |
| exploration/200x1/retail | 4 -> 3 | 66 -> 57 | 20 -> 16 | -1 | 481661 -> 504628 | 14061 -> 12611 | 215221 -> 241458 | 3659 -> 3992 | 0.0% | - | changed |
| exploration/200x1/legion | 78 -> 87 | 145 -> 150 | 79 -> 88 | -1 | 363795 -> 349880 | 0 | 273433 -> 269854 | 4 -> 0 | 7.8% -> 8.5% | 198960 | changed |
| exploration/500x4/retail | 0 | 0 | 0 | -1 | 10389555 -> 10291685 | 8479 -> 8209 | 9502344 -> 9398432 | 75069 -> 78783 | 0.0% | - | changed |
| exploration/500x4/legion | 196 -> 182 | 481 -> 473 | 197 -> 186 | -1 | 6891688 -> 6906832 | 0 | 6261193 -> 6285419 | 0 | 18.2% -> 18.7% | 2580352 | changed |
| dynamicobstacle/200x1/retail | 155 -> 159 | 200 | 200 | 3342 -> 3323 | 22495 -> 23424 | 1569 -> 2808 | 63517 -> 53649 | 0 -> 7 | 0.0% | - | changed |
| dynamicobstacle/200x1/legion | 199 -> 197 | 200 | 199 | 3321 -> 3322 | 9312 -> 16174 | 0 | 2555 -> 6302 | 0 | 1.6% -> 1.7% | 1259600 | changed |
| dynamicobstacle/500x4/retail | 1368 -> 1393 | 2000 | 1576 -> 1674 | 3749 -> 3748 | 1456443 -> 1325737 | 10097 -> 8965 | 976184 -> 923330 | 4100 -> 3547 | 0.0% | - | changed |
| dynamicobstacle/500x4/legion | 1764 -> 1762 | 2000 | 1773 -> 1774 | 3779 -> 3782 | 787374 -> 789092 | 0 | 627688 -> 615073 | 282 -> 63 | 3.3% -> 3.4% | 36388056 | changed |
| rapidreplacement/200x1/retail | 187 -> 180 | 0 | 200 | 3188 -> 3170 | 5831 -> 3233 | 38915 -> 34900 | 8353 -> 5505 | 0 | 0.0% | - | changed |
| rapidreplacement/200x1/legion | 200 | 0 | 200 | 3150 -> 3161 | 1056 -> 679 | 0 | 0 | 0 | 6.6% -> 7.1% | 6777136 -> 6774592 | changed |
| rapidreplacement/500x4/retail | 1790 -> 1833 | 0 | 2000 | 3189 -> 3172 | 87403 -> 55745 | 437990 -> 414784 | 234469 -> 131830 | 0 | 0.0% | - | changed |
| rapidreplacement/500x4/legion | 2000 | 0 | 2000 | 3150 -> 3162 | 10250 -> 5525 | 0 | 0 | 0 | 6.7% -> 7.2% | 170214920 -> 170210696 | changed |
| unreachable/200x1/retail | 0 | 0 | 200 | -1 | 146948 -> 146648 | 0 | 0 | 0 | 0.0% | - | changed |
| unreachable/200x1/legion | 0 | 0 | 0 | -1 | 890541 -> 890290 | 0 | 0 | 0 | 3.0% | 208624 | changed |
| unreachable/500x4/retail | 0 | 0 | 0 | -1 | 8892465 -> 8888054 | 0 | 0 | 0 | 0.0% | - | changed |
| unreachable/500x4/legion | 0 | 0 | 0 | -1 | 8672122 -> 8668247 | 0 | 0 | 0 | 1.6% | 2449040 | changed |
| recovery/200x1/retail | 7 -> 15 | 200 | 200 | -1 | 122605 -> 121787 | 0 | 108526 -> 125420 | 0 | 0.0% | - | changed |
| recovery/200x1/legion | 200 | 200 | 200 | 3802 -> 3805 | 101559 -> 101259 | 0 | 452 -> 462 | 0 | 0.7% | 627088 | changed |
| recovery/500x4/retail | 1442 -> 1481 | 2000 | 2000 | 4115 -> 4114 | 1028946 -> 1024498 | 0 | 174428 -> 155543 | 0 | 0.0% | - | changed |
| recovery/500x4/legion | 1999 | 2000 | 1999 | 4090 -> 4089 | 772654 -> 770944 | 0 | 3342 -> 2708 | 0 | 0.3% | 9844640 | changed |
| recovery-passive/200x1/retail | 0 | 105 | 200 | -1 | 114756 -> 113890 | 0 | 582956 -> 522975 | 0 | 0.0% | - | changed |
| recovery-passive/200x1/legion | 200 | 200 | 200 | 3802 -> 3805 | 101559 -> 101259 | 0 | 452 -> 462 | 0 | 0.7% | 644464 | changed |
| recovery-passive/500x4/retail | 0 | 2000 | 2000 | -1 | 1023812 -> 1019250 | 357 -> 537 | 3222675 -> 3159720 | 0 | 0.0% | - | changed |
| recovery-passive/500x4/legion | 1999 | 2000 | 1999 | 4090 -> 4089 | 772654 -> 770944 | 0 | 3366 -> 2730 | 0 | 0.3% | 9748640 | changed |
| jagged/200x1/retail | 137 -> 145 | 200 | 200 -> 198 | 3997 -> 3953 | 46961 -> 47996 | 5154 -> 5456 | 37371 -> 33949 | 29 -> 70 | 0.0% | - | changed |
| jagged/200x1/legion | 175 | 200 | 194 -> 190 | 4076 -> 4143 | 62548 -> 69612 | 19 -> 0 | 35772 -> 44140 | 11 -> 0 | 5.6% -> 5.9% | 1976600 | changed |
| jagged/500x4/retail | 882 -> 818 | 1417 -> 1323 | 969 -> 885 | -1 | 3915529 -> 4268732 | 13641 -> 12949 | 2797623 -> 3129789 | 47737 -> 45033 | 0.0% | - | changed |
| jagged/500x4/legion | 1323 -> 1364 | 1982 -> 1987 | 1431 -> 1457 | 4797 -> 4730 | 1622513 -> 1562774 | 57 -> 61 | 856306 -> 838539 | 140 -> 146 | 8.9% -> 8.8% | 9194712 | changed |
| jagged/2000x1/retail | 265 -> 263 | 886 -> 922 | 273 -> 264 | -1 | 5729097 -> 5568058 | 4591 -> 8166 | 2664159 -> 2643278 | 10931 -> 11391 | 0.0% | - | changed |
| jagged/2000x1/legion | 670 -> 708 | 1236 -> 1242 | 730 -> 782 | -1 | 3122077 -> 3050910 | 825 -> 53 | 1804101 -> 1749667 | 73 -> 27 | 8.3% -> 8.5% | 20761480 | changed |
| trapped/200x1/retail | 150 -> 130 | 175 | 200 | 3373 -> 3404 | 57380 -> 60802 | 742 -> 1919 | 26445 -> 39553 | 0 -> 78 | 0.0% | - | changed |
| trapped/200x1/legion | 175 | 175 | 175 | 3444 -> 3406 | 170346 -> 173905 | 0 | 4394 -> 9493 | 51 -> 15 | 1.3% -> 1.5% | 927016 | changed |
| trapped/500x4/retail | 1337 -> 1315 | 1752 | 1628 -> 1613 | 3959 -> 4127 | 2443899 -> 2556438 | 5697 -> 7277 | 595336 -> 681269 | 17098 -> 27663 | 0.0% | - | changed |
| trapped/500x4/legion | 1720 -> 1727 | 1752 | 1727 -> 1730 | 3818 -> 3827 | 1690344 -> 1689562 | 0 | 92614 -> 79371 | 24 -> 414 | 1.5% -> 1.6% | 6991584 | changed |
| crowdtrap/200x1/retail | 59 -> 55 | 96 | 200 | -1 | 115008 -> 114257 | 2687 -> 2875 | 340996 -> 340550 | 11 -> 0 | 0.0% | - | changed |
| crowdtrap/200x1/legion | 102 | 200 | 102 -> 103 | 5913 -> 5915 | 341083 -> 341340 | 0 | 17424 -> 17192 | 1 -> 0 | 3.1% -> 3.4% | 609704 | changed |
| crowdtrap/500x4/retail | 537 -> 571 | 1591 -> 1712 | 1100 -> 1177 | -1 | 3954084 -> 3579733 | 15488 -> 14316 | 2854279 -> 2459833 | 16755 -> 11103 | 0.0% | - | changed |
| crowdtrap/500x4/legion | 1287 -> 1323 | 1910 -> 1907 | 1297 -> 1335 | 5118 -> 5025 | 2247909 -> 2230841 | 0 | 1171347 -> 1141263 | 0 -> 2 | 7.0% -> 6.9% | 6166992 | changed |
| singleunit/200x1/retail | 200 | 91 | 200 | 537 -> 541 | 466 -> 355 | 552 -> 392 | 0 | 0 | 0.0% | - | changed |
| singleunit/200x1/legion | 200 | 91 | 200 | 479 -> 483 | 620 | 0 | 0 | 0 | 0.0% -> 0.3% | 3238520 | changed |
| singleunit/500x4/retail | 2000 | 1000 | 2000 | 921 -> 930 | 554900 -> 543937 | 12997 -> 1901 | 0 | 185949 -> 189860 | 0.0% | - | changed |
| singleunit/500x4/legion | 2000 | 1000 | 2000 | 513 -> 519 | 301722 -> 307604 | 0 | 0 | 97 -> 136 | 0.2% -> 0.7% | 27285720 -> 27772464 | changed |
| singleunit/2000x1/retail | 2000 | 1000 | 2000 | 921 -> 930 | 553291 -> 542021 | 12962 -> 1881 | 0 | 196610 -> 198380 | 0.0% | - | changed |
| singleunit/2000x1/legion | 2000 | 1000 | 2000 | 513 -> 519 | 301722 -> 307604 | 0 | 0 | 97 -> 136 | 0.2% -> 0.7% | 27285720 -> 27772464 | changed |
| groupdetour/200x1/retail | 95 -> 89 | 200 | 191 -> 192 | -1 | 61837 -> 66520 | 9722 -> 11189 | 80603 -> 92844 | 94 -> 63 | 0.0% | - | changed |
| groupdetour/200x1/legion | 180 -> 170 | 200 | 183 -> 170 | 3892 -> 4071 | 112724 -> 135000 | 0 | 74328 -> 88127 | 13 -> 0 | 7.2% -> 6.4% | 203048 | changed |
| groupdetour/500x4/retail | 819 -> 859 | 1646 -> 1588 | 1122 -> 1084 | -1 | 3583852 -> 3629617 | 17310 -> 15014 | 2750851 -> 2804121 | 13743 -> 9017 | 0.0% | - | changed |
| groupdetour/500x4/legion | 1314 -> 1328 | 2000 | 1322 -> 1344 | 4999 -> 4948 | 1946836 -> 1884936 | 38 -> 32 | 1561085 -> 1493219 | 150 -> 16 | 7.8% -> 7.7% | 2520704 | changed |
| churn/2000x1/retail | 0 -> 1 | 54 -> 60 | 0 -> 2 | -1 | 7408919 -> 7293545 | 200569 -> 182695 | 1607540 -> 1546055 | 0 | 0.0% | - | changed |
| churn/2000x1/legion | 0 | 87 -> 84 | 0 | -1 | 8632499 -> 8514305 | 51423 -> 39947 | 1379958 -> 1347146 | 0 | 1.8% -> 4.9% | 2304000000 | changed |

## Reproduce

```sh
# the committed base (and a check against it)
tools/crowdbench_screen.py --binary build-rel/crowdbench --output screen.jsonl \
    --baseline tools/scenarios/crowdbench_screen_baseline.jsonl --cpus 16-18
# the 10000 column (its case ids carry /tr10000)
tools/crowdbench_screen.py --binary build-rel/crowdbench --output screen-10000.jsonl \
    --turn-rate 10000 --cpus 16-18
# one audit row, e.g. MV-11 seed 0
build-rel/crowdbench --mode legion --scenario opposingcolumns --units 250 --players 8 \
    --moving-percent 50 --ticks 6000 --seed 0 [--turn-rate 10000]
```
