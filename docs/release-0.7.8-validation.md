# 0.7.8 release validation

Checks performed on 2026-09-27. Simulation, rendering and native-script changes
are in `60ba3e8`; candidate `8ea71a1` sets version 0.7.8. Streaming
shutdown correction `1581a1a` was rebuilt and retested in all four configurations.
Network protocol is 189.

## Builds and tests

| Configuration | Result |
| --- | --- |
| Release, all targets | 81/81 CTest tests passed |
| Debug with `-O2 -g`, all targets | 84/84 CTest tests passed |
| Debug, all targets | 84/84 CTest tests passed |
| Clang Debug, all targets | 84/84 CTest tests passed |

`tools/check-determinism.sh` passed GCC/Clang and optimization comparisons with
hash `dcef618cd2e4d558`. The local ARM emulation leg lacked target headers; macOS
CI separately exercises ARM64 deterministic math and driver equivalence.

## Offline retail comparisons

No retail GUI was launched. Checks use locally installed retail data and native
routines in controlled emulation:

- 295 death, corpse, cursor, origin and shading cases passed, including all 204
  shipped scripts, seven corpse-refusal types in three death modes, 29 explosive
  corpse requests and 32 immediate building deaths.
- All 816 animation timelines passed: 204 scripts × four profiles, 1,500 ticks
  each. One executable-launch failure during relinking passed on retry using an
  isolated binary; there was no VM state mismatch.
- SET26/SET31 owner teardown and world/render callback comparisons passed.
- 846 simulation lifecycle cases passed. All 202 ordinary-death timing
  comparisons agreed with native, using Create-initialized state for Giant Spider.
- All 16 piece-flag combinations, both body passes, shadow admission, 289 sloped
  face normals and all 32 indexed shade rows/transparency passed.

See the [death lifecycle audit](native-death-lifecycle-2026-09-27.md) and
[piece rendering audit](piece-render-flags-2026-09-27.md) for probe boundaries.
Synthetic host records and controlled allocation/render/map seams do not prove
live GUI behavior, actual map stamping, or pixel-for-pixel equivalence.

## Remote multiplayer

The full 37-scenario table in `tools/desync-hunt-remote.sh` passed across
`tak.pgnet.us` and `vpn3.pgnet.us`: **52 seats completed, no desyncs, no incomplete
runs**. Cases requested five simulated minutes, accepting legitimate earlier
match conclusions. Coverage included both balances, naval maps, fog/start rules,
unit caps, stress, live orders, multiple human seats, latency/jitter, packet loss,
and two all-AI spectator runs.

Spectator cases check flow/completion only, not hash consensus. The spectator
stress case reached tick 9,000 with 8,234 surviving units. Live-order cases prove
consensus within the match, not reproducibility of hashes across runs.
The harness detected a planted desync at tick 900 and rejected an intentional
gameplay-data mismatch; the temporarily moved remote override was restored.
Temporary sweep servers were stopped after the runs.

Servers use isolated GCC 13 optimized Debug (`-O2 -g`) binaries from `8ea71a1`,
reporting version 0.7.8. Production server binaries were not replaced. The later
streaming shutdown change does not affect simulation or the network protocol.

## 16,000-unit simulation smoke tests

All four `simperf` workloads completed 900 ticks (30 simulated seconds), run
serially after local CTest. Match used Crusades balance, mixed armies and eight
Absurd AIs; other modes used the default balance.

| Workload | Ending units | Observed simulation speed |
| --- | ---: | ---: |
| Synthetic movement | 16,000 | 2.988× |
| Mixed-unit patrol | 16,000 | 1.076× |
| Destination congestion | 16,000 | 1.185× |
| AI combat | 14,707 | 0.903× |

Host: Core Ultra 9 275HX. Remote-sweep clients and other release work were active
on the host. These are headless generated-map smoke tests, not isolated
benchmarks, rendered frame rates, sustained performance guarantees, or controlled
comparisons to previous releases. The combat workload did not sustain 1.0× in
this run.

## Streaming

- Local encrypted upload, reconnect, wrong-hostname rejection and key-redaction
  checks passed with `tools/check-stream-network.py`.
- Accelerated offscreen OpenGL capture converted a 7680 × 2160 source through the
  4K/60 preset: 225 frames encoded, zero dropped, average capture 3.149 ms. The
  requested swap interval remained 1. H.264 output was 3840 × 2160 at 60 fps with
  stereo AAC. This was local encoding, not a live YouTube broadcast or proof of
  monitor pacing with an offscreen driver.
- Candidate macOS CI found a shutdown race: stopping during audio catch-up could
  still submit a newer video timestamp. Cancellation checks now stop that video
  submission. A deterministic paused-worker regression fails with the old code
  and passes with the correction; the existing 100 ms A/V tolerance is unchanged.

## Platform packaging and documentation

Final candidate `1581a1a` passed Linux packaging, Windows x64 and macOS ARM64 CI,
including their streaming checks and platform dependency checks. Windows/macOS
also passed their deterministic simulation gates. The earlier macOS failure is
recorded above; it was corrected before release.

All seven README scenes were freshly captured from tagged `v0.7.8` and visually
inspected after the release tag. Identical deterministic scenes may produce
identical image files. See [capture details](img/README.md) for provenance and
scene descriptions. README local links and `git diff --check` passed.

These checks cover the paths and workloads described above, not exhaustive
visual equivalence with retail or performance on every GPU.
