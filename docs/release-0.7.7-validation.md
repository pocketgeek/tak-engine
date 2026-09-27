# 0.7.7 release validation

Checks performed on 2026-09-27. The simulation and initial fixes were tested at
`99d9199`; final rendering corrections are in `901aa8b`, the release commit.
The final correction changes client ruin geometry only, not simulation or the
network protocol.

## Builds and tests

| Configuration | Result |
| --- | --- |
| Release, all targets | 80/80 CTest tests passed |
| Debug with `-O2 -g`, all targets | 83/83 CTest tests passed |
| Debug, all targets | 83/83 CTest tests passed |
| Clang Debug, all targets | 83/83 CTest tests passed |

All four suites were rerun after the final ruin-rendering correction.
`tools/check-determinism.sh` passed GCC/Clang and optimization comparisons with
hash `dcef618cd2e4d558`. The local ARM emulation leg lacked its headers; the macOS
CI workflow separately exercises ARM64 deterministic math and driver equivalence.
The new construction-clearing and building-death tests also agreed across GCC
and Clang using the installed retail assets.

Actual-data construction tests cover both balance modes, ground/flying builders,
queued movement, cancellation, complete clearing/construction, and invalid
sites. The earlier lodestone-upgrade checks cover all five factions and both
balance modes. The cursor/result harness checks hover and click behavior,
restricted/unrestricted assist, repair, incapable commands, screen-space hit
bounds, and sparse participant rosters.

Seven offline native cursor probes, 32 native building-death cases, and the
native corpse-position probe passed without launching the retail game. The
sixteen-building rendering check passed with cached/reference geometry
verification. The Keep's death-to-ruin transition was also captured and visually
inspected in the accelerated engine renderer. See the
[construction/cursor/ruin audit](construction-cursors-2026-09-27.md) for probe
boundaries and remaining cursor placeholders.

## Remote multiplayer

The full 37-scenario table in `tools/desync-hunt-remote.sh` passed across
`tak.pgnet.us` and `vpn3.pgnet.us`: **52 seats completed, no desyncs, no incomplete
runs**. Cases requested five simulated minutes, accepting legitimate earlier
match conclusions. Coverage included both balances, naval maps, fog/start rules,
unit caps, stress, live orders, multiple human seats, latency/jitter, packet loss,
and two all-AI spectator runs.

Spectator cases check flow/completion only, not hash consensus. The spectator
stress case reached tick 9,000 with 8,137 surviving units. Live-order cases prove
consensus within the match, not reproducibility of hashes across runs.
The harness correctly detected a planted desync at tick 900 and rejected an
intentional gameplay-data mismatch; the temporarily moved remote override was
restored.

Servers used isolated GCC 13 optimized Debug (`-O2 -g`) binaries built from
`99d9199`, reporting version 0.7.7. Production server binaries were not replaced.
The subsequent client-only ruin-rendering correction does not affect these
multiplayer results. Repeated seeded 60-second matches also reproduced hashes:
`669cbfb1ecb7164f` for normal vision and `c03102628e9c7a1c` for doubled vision.

## 16,000-unit simulation smoke tests

All four `simperf` workloads completed 900 ticks (30 simulated seconds), run
serially after the remote sweep. Match used Crusades balance, mixed armies, and
eight Absurd AIs; other modes used the default balance.

| Workload | Ending units | Observed simulation speed |
| --- | ---: | ---: |
| Synthetic movement | 16,000 | 3.717× |
| Mixed-unit patrol | 16,000 | 1.322× |
| Destination congestion | 16,000 | 1.458× |
| AI combat | 14,799 | 1.321× |

Host: Core Ultra 9 275HX. These are headless generated-map smoke tests, not
rendered frame rates, sustained performance guarantees, or controlled comparisons
to the previous release. Other release review work was active on the host.

## Streaming and documentation

- Local encrypted-upload, reconnect, wrong-hostname rejection, and key-redaction
  checks passed with `tools/check-stream-network.py`.
- Accelerated offscreen OpenGL capture converted a 7680 × 2160 source through the
  4K/60 preset: 223 frames encoded, zero dropped, average capture 4.84 ms. The
  requested swap interval remained 1. This was local encoding, not a live YouTube
  broadcast or proof of monitor pacing with an offscreen driver.
- All seven README scenes were freshly captured and inspected from the tagged
  engine. Identical scenes may yield identical files. See [capture details](img/README.md).
- README local links and `git diff --check` passed.

These checks cover the paths and workloads described above, not exhaustive
visual equivalence with retail or performance on every GPU.
