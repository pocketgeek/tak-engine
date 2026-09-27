# Geometry and shadow reuse — 2026-09-26

Follow-up to [geometry submission optimization](render-optimization-2026-09-26.md).
This pass addresses all five requested opportunities and adds local-server CPU
usage to the in-game stats panel.

## Implementation

1. **Shared piece transforms.** A render worker prepares the animated model tree
   once, preserving hidden-parent transforms, then supplies the same coordinates
   to body and shadow collectors. Ghosts, portraits and radial-extent queries
   retain their original transform path.
2. **Unique vertex transforms.** Each visible piece's model vertices are
   transformed and rotated once, then reused across its polygon faces and both
   collectors. Fan-position UVs, triangle order, depth and culling are unchanged.
   Worker scratch retains child storage across different model shapes; repeatedly
   destroying/reallocating that tree was not useful in the first experiment.
3. **Unchanged geometry reuse.** Existing geometry slots retain their vertex
   buffers when an exact input key matches. The key includes unit/type identity,
   atlas/color slot, pose, heading, camera/terrain anchor, zoom, altitude, body
   attitude, occlusion, construction/birth effects, corpse mode, tint, veterancy,
   shadow toggle and animated-texture tick. It compares actual values, not hashes.
   Animated textures conservatively invalidate each simulation tick. Target
   invalidation also clears geometry keys. No simulation updates are skipped.
4. **Script/state lookup overhead.** Yard-state snapshot queries, weapon-piece
   queries, weapon callbacks and movement/work notifications use the existing
   maintained ID index. Null entries are authoritative misses, avoiding a second
   map lookup for removed scripts. The map still owns scripts and determines the
   unchanged state-hash traversal. Callback order, timing, RNG and pathfinding
   remain unchanged. Snapshot vector copying was examined but not replaced: the
   prior profile attributed 3.1% cumulative process CPU to the entire snapshot
   function, including yard lookup. No evidence justified complicating its
   triple-buffer ownership to remove remaining copies in this pass.
5. **Retained shadow tiles.** Unchanged atlas tiles retain their coverage.
   Changed/reassigned tiles clear their full padded rectangle and redraw; an
   entirely dirty page uses one clear. A tile key includes geometry-slot identity,
   owner/revision, layout, pixel origin and output scale. Slot identity matters:
   revisions alone can coincide after units change depth/visibility slots. Page
   recreation discards tile records, and failures invalidate them before fallback.
   Masked silhouettes and per-unit compositing order are unchanged.

The caches stay within the existing visible-geometry pool and bounded four-page
shadow atlas. They do not allocate an atlas per unit or introduce a per-unit cache that grows
with every spawned/dead unit. Camera motion and animated poses reduce the reuse rate; these
optimizations do not promise a fixed frame rate for every battle.

## Measurement

Intel Core Ultra 9 275HX, RTX 5070 Laptop, NVIDIA 610.57.04, optimized Debug
`-O2`, accelerated offscreen OpenGL, 1280×960, 4× AA, shadows enabled, no VSync,
480 FPS cap. The same mixed stationary fixture requests 1,998 units plus two
initial units: 2,000 total, 1,788 visible. Before is `dd4130a`. Each run lasts
30 seconds, excluding the first 15; no builds, verification or native profiler
run alongside the measured comparisons. ABBA ordering checks drift.

| Run | Frame work | Approx. FPS | Geometry preparation | Shadow time |
|---|---:|---:|---:|---:|
| Before A | 9.07 ms | 110.3 | 1.25 ms | 2.58 ms |
| After A | 6.65 ms | 150.4 | 0.70 ms | 0.78 ms |
| After B | 6.54 ms | 152.9 | 0.71 ms | 0.75 ms |
| Before B | 9.11 ms | 109.8 | 1.26 ms | 2.61 ms |

Average frame work falls about 27%, equivalent to approximately 38% higher FPS;
shadow time falls about 70%. Main-thread CPU drops from roughly 86.6% to 75.4%
of one core. GPU remains around 19–21%. FPS is reciprocal mean instrumented
frame work, not monitor presentation cadence. These are stationary-crowd gains;
moving scenes with frequent pose/layout changes will reuse less.

A separate pair disabled both caches to isolate shared piece/vertex preparation:
reference 9.42 ms/frame, 1.36 ms preparation; shared 9.19 ms/frame, 1.19 ms
preparation. This smaller single-pair gain is less robust than the full ABBA
comparison. The first allocation-heavy version showed no gain and was refined
to retain worker scratch across model shapes before this comparison.

The script-index change is behavior-preserving and removes repeated map searches,
but this combined rendering benchmark does not establish an isolated simulation
speedup. The prior network profile measured yard queries at 1.3% flat process CPU;
the new snapshot query no longer searches the map on index misses.

## Correctness and builds

- Production body vertices compare exactly against the reference collector with
  `TAK_GEOMETRY_VERIFY=1`; cache hits are rebuilt and compared too.
- `TAK_SHADOW_VERIFY=1` compares opaque/masked vertices and order against the
  reference collector. `TAK_SHADOW_CACHE_VERIFY=1` compares every active padded
  atlas tile against a freshly cleared/rebuilt page using pixel readback.
- A stationary 2,000-unit fixture passes all three checks. Roughly 1,192 of 1,788
  visible units reused geometry in a sampled frame.
- A separate-server combat scene passes the checks with up to 15,209 living
  units, including moving flyers and changing atlas layouts, with no desync.
  Verification runs are deliberately expensive and are not timing benchmarks.
- 216 full CTest executions passed: 70 Release, 73 optimized Debug, 73 Debug.
  Additional focused CPU-metric, shadow, geometry-submission and stats-fit checks
  passed after the HUD addition.
- Cross-compiler deterministic-math checks agree on `dcef618cd2e4d558`; unavailable
  ARM cross-build legs were skipped by the existing harness.
- Two 1,800-tick network smoke runs and the pre-change client against the rebuilt
  server agree on `669cbfb1ecb7164f`. These small smoke matches supplement the
  combat/transport/script tests and the larger network rendering check.
- Both pre-change and optimized clients verify all 22 checkpoints of the same
  653-tick large combat replay, ending with 14,000 living units and hash
  `205ee7766315ce26` (15,209 total unit records).
- Combined geometry/shadow/pixel verification also passes live target resizing
  through 1280×960, 1600×960 and 1920×1080 in a 200-unit scene.
- All targets, including servers, rebuilt after the shared simulation change;
  clients refreshed again after final rendering changes. Protocol stays 184.

Development comparison switches: `TAK_GEOMETRY_REFERENCE=1` uses original body/
shadow transforms and disables geometry reuse; `TAK_GEOMETRY_NOCACHE=1` and
`TAK_SHADOW_NOCACHE=1` independently disable caches; `TAK_CACHE_STATS=1` reports
reuse. These switches and expensive verifiers are unavailable in Release.
Local evidence and scripts live under `/tmp/tak-reuse/`.

## Local-server CPU display

Single-player/campaign/benchmark launches pass the owned server PID to the HUD,
not just the benchmark recorder. `SERVER CPU` samples that process once per
second and divides CPU time by elapsed time and online logical CPU count:
0–100% of total machine capacity. First/unavailable samples show `N/A`; remote
servers have no row. It does not include the client's simulation worker.
The existing cross-platform sampler supports Linux, Windows and macOS; actual
launch/visual validation was on Linux. CPU normalization/reset cases pass the
metric test, and a 1920×1080 single-player screenshot confirms the row fits.
