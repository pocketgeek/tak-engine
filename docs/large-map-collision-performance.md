# Large-map collision-grid cost (2026-09-27)

Reported workload: spectating eight Absurd AIs with Crusades balance on Ultima
Online B1, requesting 4x speed, slowing down after armies grow to a few hundred
units. This is separate from the wide-map terrain/fog submission cost documented
in [distant rendering performance](distant-rendering-performance.md).

Server stack samples identified time spent clearing the ground occupancy grid,
clearing the combat spatial grid, and creating/converting the aircraft projectile
collision grid. These operations walked large mostly empty arrays every tick.
The map has 2016 x 2016 simulation cells, despite the small initial population.

The three grids now retain their allocations and track touched cells. Rebuilding
clears only those cells before stamping the current units. Aircraft collision
resolution retains the same player/entity traversal, candidate order, overlap
limit and RNG calls; only temporary cell encoding and cleanup change. Ground
movement and construction completion register their occupancy writes for the next
rebuild. Spatial-grid origin/dimension changes still reset the previous indices.
No path search, movement, collision eligibility or tick cadence was changed.

## Developing-match measurement

Core Ultra 9 275HX, optimized Debug (`-O2 -g`), Linux, local retail data:

```
build-o2/simperf --mode match --units 8 --unit-cap 2000 --ticks 18000 \
  --map 'Ultima Online B1' --data /home/pocket_geek/tak_data --crusades
```

`--unit-cap` is new: the older harness tied the cap to the initial unit count,
so `--units 8` could not reproduce natural army growth. This run starts with eight
monarchs, lets all eight Absurd AIs develop normally, and ends with 425 living
units after ten game minutes. Rendering is excluded; periodic state hashes are
included. The run also logged simulation phases above 20 ms.

| Measurement | Before | After |
|---|---:|---:|
| Wall time for 600 game seconds | 85.471 s | 28.302 s |
| Mean simulation throughput | 7.020x | 21.200x |
| Median tick | 3.319 ms | 0.191 ms |
| p95 tick | 11.458 ms | 6.459 ms |
| p99 tick | 19.340 ms | 13.234 ms |

All 600 checkpoint hashes are identical, including final `c0d9a9de2be7eb5b`.
This preserves the match's complete deterministic state and random sequence.
The remaining long ticks include path-search work; this change does not promise
that every map/match will sustain 4x indefinitely.

## Rendered spectator check

An eight-Absurd-AI, Crusades, local server/client run used the same seed (2002),
map and settings, requesting 4x, in a 7680 x 2160 offscreen OpenGL window. Both
runs reached approximately 18,000 ticks and 400 living units. The camera remained
at its normal spectator zoom: the player-only initial-camera profiling override
does not apply to spectators. No claim about extreme-zoom throughput is made by
this comparison (the separate terrain/fog measurements cover that).

Over each run's final 30 seconds, old paths delivered 1.83x with 409 units; the
optimized paths delivered 4.17x with 407 units. Actual rendering throughput was
approximately 427 FPS in both. Thus this reproduces a simulation-speed drop
without a low frame rate. The old run had brief stack sampling earlier in the
match; the final measurement window had none. The before run disabled the new
rendering optimizations and used the pre-grid-change client/server. The after
run used the rebuilt client/server with all changes enabled.

A headless eight-Absurd-AI server/client control completed ten game minutes in
147.7 wall seconds before this change, reaching 406 units without errors. This
is why simulation-only throughput cannot establish the interactive bottleneck.

## Validation

- 2,000 generated aircraft-grid cases compare every cell, RNG seed and draw count
  against the pre-change executable: identical.
- The aircraft regression also compares reused and fresh grids over 200 frames,
  including movement, removal, empty grids, overlap overflow and equal-area map
  shape changes.
- Release CTest 86/86; optimized Debug CTest 89/89.
- Deterministic-math guard and GCC/Clang O0/O2/O3 golden checks pass
  (`dcef618cd2e4d558`). The local ARM cross-build lacks target headers and is skipped.
- A dense 16,000-unit patrol, 300 ticks, was unchanged: 7.894 s before and
  7.899 s after. All ten checkpoints match (`ebef90c26bde9148` final), providing
  a check that sparse cleanup does not regress the dense workload.
- All Release and optimized Debug targets rebuilt, including client and server.
