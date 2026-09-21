# Test sweep — 2026-09-20

The continuing large-army optimization work and its current measurements are in
[the performance report](performance-2026-09-20.md). The general 40,000-unit target
is not yet achieved; this historical sweep report must not be read as that claim.

Local validation passed; additional regression validation and remote stress/network validation are still in progress;
this report is not yet a clean-sweep verdict.

## Issues found and addressed

- **Absurd Aramon could repeatedly fail to start its first Keep.** Its builder
  approached the short side of the rectangular footprint and entered the placement
  exclusion radius. AI site selection now checks the snapped site and the resulting
  constructor approach. Ground movement, terrain grades and occupancy are unchanged.
  A real Ulasem Arena opening regression covers both balances. In the 900-second
  Crusades reproduction, Aramon now builds an army and issues attacks instead of
  issuing 675 unsuccessful Build commands.
- **Conjure cancellation lacked coverage.** Added Stop-command checks for queued
  and map-placed Zhon conjuring, at multiple headings/sites and in both balances.
  They allow the flyer's ordinary landing order but require the canceled build and
  its hover controller to remain cleared.
- **Stress setup scanned unused ring interiors.** The placement scan now visits
  only the perimeter, in exactly the previous order. Six complete 15,208-unit
  setups (Ulasem Arena, Lake Lokken and Tarosian Plain, both balances) retain their
  exact initial hashes. On the remote unoptimized build, Ulasem setup decreased
  from 14.455 to 6.015 seconds; the optimized build took 1.527 seconds.
- **Server catch-up could withhold network service.** Heavy tick batches could
  exceed the connection timeout before flushing bundles and keepalives. Catch-up
  now returns to socket servicing after an 8 ms work budget, checked after each
  complete tick. No tick is skipped and no simulation calculation changes.
- **Remote build configuration was unsuitable for stress measurements.** Servers
  now use Ubuntu GCC with Debug hooks retained and `-O2 -g`, rather than unoptimized
  Debug. Plain Release disables harness features such as `TAK_GODS`. The remote
  harness accepts `TAK_CLIENT=./build-o2/takclient` for the matching optimized client.

- **Off-map stress flyers could corrupt cached navigation checksums.** A landed
  flyer beyond the map edge caused an out-of-bounds read while aging its cached
  footprint. The World adapter now bounds cache accesses and skips wholly external
  rectangles; the emulated native grading routine is unchanged. Stress setup also
  keeps flyer footprints within the map. Added off-map cache checksum and stress
  flyer placement regressions. The previously failing remote Tarosian Plain case
  now reaches tick 60 without desync (previous failure: tick 30).

Protocol 170 identifies the cache-boundary and stress-placement fixes; 169 identified
the AI construction-selection change. The six setup hashes above predate the flyer
clamp and establish equivalence of the perimeter-scan optimization only.

## Completed validation

| Check | Result |
| --- | --- |
| All-target Debug and Release builds | Pass |
| All-target optimized Debug build | Pass |
| Debug CTest, including retail data | 36/36 |
| Release CTest, including retail data | 36/36 |
| Optimized Debug CTest, including retail data | 36/36 |
| Python reverse-engineering/tool unit tests | 123/123 |
| Standalone emulated-retail checks | 77/77 scripts |
| AI scenarios: 5 factions × 4 difficulties × 2 balances, 900 seconds each | 40/40 complete; all attack-policy checks pass |
| 450 Hunters, Crusades, Ulasem terrain, 1,800 ticks | All compared movement/mission fields match native execution |
| Six full stress-setup comparisons before/after scan optimization | Exact unit counts and state hashes match |
| Repeated seeded multiplayer with a seated client and five AI factions | Both reach tick 1,800, hash `41f924f6099e83cc`; no referee desync |
| Fixed-math golden across GCC/Clang, O0/O2/O3 on x86-64 | All `8adc4762a852fadd` |
| Conjure rendering smoke test | Completed; `/tmp/tak-sweep-conjure.png` |
| `git diff --check` | Pass |

The ARM cross-build was skipped because target libc/headers are unavailable.
The fixed-math golden is not a whole-simulation cross-compiler test; remote
client/referee comparisons provide that separate coverage. The crowd comparison
excludes combat, strategic AI and rendered animation, and does not require every
unit to reach the destination within 60 simulated seconds.

## Remote runs

Both `tak.pgnet.us` and `vpn3.pgnet.us` received a server built from the current
workspace. Previous server binaries were preserved as
`/home/pocket_geek/takserver.pre-sweep169`.

The optimized sweep runs the complete 37-scenario table with a 45-simulated-minute
limit per scenario (or game conclusion). It covers both balances, naval maps,
multiple seated human clients, commands in flight, latency/jitter/loss, and stress
loads. Spectator-only scenarios measure flow control, not referee hash agreement.
The planted desync was detected at tick 900 and the gameplay-override mismatch
was rejected; the temporarily moved override was restored.

Initial unoptimized runs and optimized runs before the pacing fix exposed stress
timeouts. Those failures are preserved and are not counted as passes. The five
affected seated stress scenarios are being rerun with the pacing and cache fixes.
The first pacing-only rerun exposed the real cache desync and was stopped after
diagnosis. Protocol 170 uses isolated `takserver.sweep170` binaries on both hosts,
so remaining protocol-169 ordinary tests can finish without a version mismatch.
The full remote sweep is not yet a pass.

## Logs and reproduction

### Large-army performance follow-up

The user's target is **40,000 units at sustained 1.0× simulation speed** (30 Hz,
about 33.3 ms per simulation tick), with retail pathfinding preserved. This target
has not been achieved. Current profiling runs on an Intel Core Ultra 9 275HX.

An eight-AI Absurd-intensity spawn-ramp fixture identified `mobilePlacement` as
another hotspot: its per-cell callback scanned the full army for every footprint
cell. It now reuses a single rectangular body snapshot for the placement query,
including identical yard-map filtering and last-body-wins cell ownership. The
native placement routine is unchanged. Out-of-map queries still return before
constructing a snapshot. Both all-target builds and both 36-test suites pass.
The comparison harness and logs are `/tmp/tak-bench-ai-profile.cpp` and
`/tmp/tak-bench-ai-{before,after}.log`. All 1,800 per-tick hashes match; the fixture
finishes with 7,697 allocated unit records (including dead units). Average measured
tick times by allocated count were 36.21 → 23.02 ms at 2–4k, 83.95 → 47.05 ms at
4–6k, and 155.03 → 76.13 ms at 6–8k. These are local fixture measurements with
profiling/system load, not a guarantee for every map or a 40k-live-unit result.
All 15 rerun native search/movement check scripts passed as well.
Whole-army body lookups remain a scaling problem beyond this optimization.

A live stress-client stack sampled `searchBodyRect` inside the eight-neighbor
ground scan: each probe scanned the full unit list. Ground scans now snapshot the
union of those footprints once, preserving cell overwrite order, yard handling,
and the ascending occupant-ID checks. Repeated corner checks share that snapshot;
boat look-ahead retains its existing query path. No protocol change is required.

After the user confirmed camera/menu freezes too, the render-side feature sync
and reclaim sparkle reads were changed to nonblocking mutex acquisition. Feature
sync retains its previous visual state when the simulation is busy and copies
existing and new features together on success, so deferred corpse discovery is
not accidentally marked complete. Reclaim sparkles can skip a busy frame. These
are visual changes only; feature, conjure and COB animation tests pass.
A rendered seven-AI spectator smoke run completed and captured
`/tmp/tak-render-wait.png`; this checks the threaded rendering path, not a claim
that every large-army frame stall has been eliminated.

A synthetic 3,000-unit, 120-tick movement comparison produced identical state
hashes at every tick before and after. With both simulation objects compiled at
`-O2 -g`, a sequential repeat took 2.893 seconds before and 1.014 seconds after.
This measures the movement fixture, not the complete rendered Absurd benchmark.
All-target Debug/Release builds and both 36-test suites passed. Logs and the
fixture are `/tmp/tak-movement-perf.cpp`, `/tmp/tak-movement-*.hashes`,
`/tmp/tak-movement-*.time`, and `/tmp/tak-perf-*-tests.log`.

The original optimized remote table finished with 30 seated passes, one spectator
flow pass, five seated stress timeouts, and one spectator stress non-completion.
The protocol-170 combined stress rerun subsequently passed at tick 4,073 by game
conclusion; the other heavy reruns remain pending. These running remote binaries
predate the ground-scan performance follow-up.

- Final local CTest logs: `/tmp/tak-sweep-ring-debug-tests.log`,
  `/tmp/tak-sweep-ring-release-tests.log`, `/tmp/tak-sweep-o2-tests.log`.
- Native routine results: `/tmp/tak-sweep-oracles/results.json` and
  `/tmp/tak-sweep-search/results.json`.
- AI scenario results: `/tmp/tak-sweep-ai-results.json`.
- Crowd comparison: `/tmp/tak-sweep-crowd.log` and `/tmp/tak-sweep-crowd/`.
- Setup equivalence: `/tmp/tak-sweep-setup-equivalence.log`.
- Seated multiplayer repetition: `/tmp/tak-sweep-final-mp.log`.
- Full optimized remote sweep: `/tmp/tak-sweep-remote-optimized.log`,
  `/tmp/desync-remote-1020781/`.
- Pacing-fix stress reruns: `/tmp/tak-sweep-remote-paced-stress.log`,
  `/tmp/desync-remote-1053671/`.
- Protocol-170 stress reruns: `/tmp/tak-sweep-remote-170-stress.log`,
  `/tmp/desync-remote-1304990/`.
- Protocol-170 local validation: `/tmp/tak-sweep-170-debug-tests.log`,
  `/tmp/tak-sweep-170-release-tests.log`, `/tmp/tak-sweep-170-search.log`.

```sh
ctest --test-dir build-dbg --output-on-failure -j 4
ctest --test-dir build --output-on-failure -j 4
ctest --test-dir build-o2 --output-on-failure -j 4
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/re -p '*_test.py'
bash tools/check-determinism.sh
TAK_CLIENT=./build-o2/takclient tools/desync-hunt-remote.sh --validate --minutes 45
```
