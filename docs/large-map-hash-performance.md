# Large-map lockstep hash cost (2026-09-27)

A report of Ultima Online B1 (63×63) alternating between roughly 4× and 1×
prompted a timing check. The slowdown has not yet been reproduced in a matching
interactive workload; player count and army size remain unknown.

One confirmed cost was the placement-cell portion of the periodic state hash.
Each cell has a 16-bit feature index, serialized as eight bytes by the existing
FNV-1a mixer. Combining the six trailing zero-byte multiplications reduces CPU
work without changing serialized bytes, checkpoints, game behavior, or protocol.
Footprint back-references still use the original mixer. Empty cells remain hashed.

Optimized-debug `simperf`, same local data and deterministic setup:

```
build-o2/simperf --mode match --units 8 --ticks 1800 \
  --map 'Ultima Online B1' --data /home/pocket_geek/tak_data
```

| Measurement | Before | After |
|---|---:|---:|
| Total hashing over 60 checkpoints | 1524.431 ms | 437.177 ms |
| Mean time per hash | 25.41 ms | 7.29 ms |
| Simulation wall time, excluding setup | 4.848 s | 3.685 s |

All 60 checkpoint hashes match, including final `84d02d234af1123f`.
This is a deliberately small eight-unit workload that isolates map-size cost,
not a claim about sustained speed with large armies. State hashes run every
30 simulation ticks, so their wall-clock frequency increases at 4× speed.

An initial 256-unit, eight-AI run before the change sustained 5.246× on average,
with 11.451 ms p95 ticks and 3349.620 ms spent hashing over 120 checkpoints.
There are other intermittent costs: the small baseline still has a startup tick
around 270 ms. This change addresses measured periodic hashing overhead; it does
not establish the cause of the reported repeated drops to 1×.

The 256-unit follow-up also reproduced all 120 pre-change checkpoint hashes.
Release CTest passed 85/85; optimized-debug CTest passed 88/88. Both full builds
completed, including the client and server.
