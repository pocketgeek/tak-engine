# 0.7.11 release validation

Checks performed on 2026-09-29, starting from `874d127`. Protocol remains 194.
This release contains the sustained AI production changes documented in
[AI growth](ai-growth-2026-09-28.md); no pathfinding or movement rules changed.

## Local sweep

- Rebuilt all Release and optimized Debug targets.
- Release CTest: 94/94 passed; optimized Debug CTest: 97/97 passed.
  Both suites also passed again after rebuilding with version 0.7.11.
- Unoptimized Debug and Clang: 97/97 CTest tests passed in each configuration.
- Python reverse-engineering tool tests: 123/123 passed.
- Retail-data AI behavior tests passed for both balance modes.
- Deterministic-math guard and available GCC/Clang optimization comparisons
  matched golden hash `dcef618cd2e4d558`. ARM emulation legs were skipped because
  the required target headers/libraries are not installed locally.
- Ran 52 fresh, 15-minute simulated AI cases: five factions × four difficulties
  (Passive, Normal, Hard, Absurd) × both balance modes on Ulasem Arena, plus Zhon
  on generated 8×8 maps with seeds 1, 42, and 777 at each difficulty. All cases
  completed successfully and produced armies. Passive issued no Attack or
  AttackMove commands. These use an idle opponent, not a competitive match.

## Offline retail comparisons

No retail GUI was launched. These checks execute individual native routines
with controlled inputs and compare the engine's behavior:

- All 816 animation timelines passed: 204 scripts × four profiles, 1,500 ticks.
- Death sound: 500 priority/age/looping comparisons and 512 visibility,
  selection, class, and free-voice routing comparisons passed.
- Model pieces: all 16 flag combinations, both body-cache passes, shading and
  shadow admission, 289 sloped faces, all 32 indexed shade rows and transparency
  passed. These do not constitute a pixel-perfect live-rendering comparison.

## Network and streaming

The local host/peer/late-spectator map-transfer check completed 300 matching ticks
with hash `a5a69304ffacaa64`, using separate data roots and a differing map copy.
Encrypted streaming upload, reconnect, hostname rejection, and key redaction
passed with `tools/check-stream-network.py`. No public broadcast was started.

The complete 37-scenario remote sweep passed on `tak.pgnet.us` and
`vpn3.pgnet.us`: **52 client sessions completed, no unexpected desyncs, no
incomplete runs**. Each case requested five simulated minutes, accepting normal
earlier game conclusions. Coverage includes both balances, naval maps, fog/start
rules, unit caps, stress, live orders, multiple seated players, latency/jitter,
packet loss, and two all-AI spectator cases. Surviving seated players continued
after their opponents' normal early conclusions.

The spectator stress case reached tick 9,000 with 8,202 surviving units.
Spectators validate progress/flow control, not hash consensus; live-order tests
validate consensus within each match, not repeatability across runs.
The harness detected a planted tick-900 desync and rejected a deliberate
mismatch in gameplay data. The temporary override change was restored and test
servers were stopped afterward.

Servers used isolated Ubuntu GCC 13 optimized Debug (`-O2 -g`) binaries built
from the same source as the client, including the 0.7.11 version change before
its commit. Production server binaries were not replaced. An initial preflight
used the ordinary installation instead of the matching `assets/game` test fixture
and correctly rejected differing overrides before play; the complete sweep used
the matching fixture. Logs: `/tmp/tak-0711-remote.log` and
`/tmp/desync-remote-617733/`.

## 16,000-unit simulation smoke tests

All four workloads completed 900 ticks, with the per-player cap set to 2,000.
The combat case used Crusades balance and eight Absurd AIs.

| Workload | Ending live units | Observed simulation speed |
| --- | ---: | ---: |
| Synthetic movement | 16,000 | 3.187× |
| Mixed-unit patrol | 16,000 | 1.107× |
| Destination congestion | 16,000 | 1.268× |
| AI combat | 14,605 | 1.136× |

These were sequential headless smoke tests while remote clients and other
release checks were active on the same host. They are not controlled comparisons
against earlier versions, rendered frame rates, or sustained-speed guarantees.

## Packaging

Candidate `eea2b30` sets version 0.7.11 and passed
[Windows](https://github.com/pocketgeek/tak-engine/actions/runs/36562916545),
[macOS ARM64](https://github.com/pocketgeek/tak-engine/actions/runs/36562916563), and
[all seven Linux package builds](https://github.com/pocketgeek/tak-engine/actions/runs/36562916547).
Tag `v0.7.11` points to `eea2b30`. Tag workflows repeat platform checks and
attach packages to [the release](https://github.com/pocketgeek/tak-engine/releases/tag/v0.7.11).

## README and screenshots

The README distinguishes the new AI behavior from earlier features, retains
Normal's existing force targets, and documents unchanged protocol 194. All seven
gallery images were freshly captured at 1600 × 1000 using the OpenGL renderer
and visually inspected, including ships on water and the streaming panel without
a stream key. The setup image uses a real GameView connected to a temporary local server. Screenshots
and their scene details are recorded in
[the capture notes](img/README.md). Retail archives and temporary capture files
remain outside Git.
