# 0.7.12 release validation

Checks performed on 2026-09-29. Candidate `7d463ee` sets version 0.7.12;
protocol is **195**. This release contains
[diplomacy controls](unit-gifting.md),
[Hard/Absurd expansion](ai-expansion-2026-09-29.md), and
[memory/resource fixes](memory-audit-2026-09-29.md).
Movement and pathfinding rules are unchanged.

## Local sweep

All targets were rebuilt in Release, optimized Debug, unoptimized Debug, Clang,
and AddressSanitizer configurations. CTest passed in each:

| Configuration | Passed |
| --- | ---: |
| Release | 95/95 |
| Optimized Debug (`-O2 -g`) | 99/99 |
| Unoptimized Debug | 99/99 |
| Clang | 99/99 |
| AddressSanitizer + LeakSanitizer | 95/95 |

The sanitizer suite used dummy SDL video/audio and
`SDL_SHUTDOWN_DBUS_ON_QUIT=1` for complete SDL test-process shutdown, with leak
detection enabled and no suppressions. These are test settings, not production
changes. Sanitizers establish coverage of these exercised paths, not proof that
all possible leaks are absent.

- Python reverse-engineering tools: 123/123 tests passed.
- Retail-data AI policy tests passed for both balances.
- Deterministic-math guard passed. Available GCC/Clang optimization comparisons
  matched golden hash `dcef618cd2e4d558`. Local ARM emulation legs were skipped
  because target headers/libraries were unavailable.
- All 52 fresh 900-second AI cases passed: five factions × four difficulties ×
  two balances on Ulasem Arena, plus Zhon on generated 8×8 seeds 1, 42, 777 at each
  difficulty with Crusades balance. Every case produced an army; the smallest
  final army was 32 units. Passive issued no Attack/AttackMove commands. These
  scenarios use an idle opponent, not a competitive match.

## Offline retail comparisons

No retail GUI was launched. Individual native routines were compared offline:

- 204 scripts × four animation profiles × 1,500 ticks: **816/816 passed**.
- Death sound: 500 priority/age/looping and 512 visibility, selection, class,
  and free-voice routing comparisons passed.
- Model rendering: all 16 piece-flag combinations, both body-cache passes,
  shadow admission, 289 sloped faces, flag callbacks, all 32 indexed shade rows,
  and transparency passed.

An initial animation pass overlapped executable relinking and encountered
permission errors. The entire pass was repeated using a frozen executable;
only that clean run is counted. The model comparison was rerun with its proper
`retail_visual_test` executable after an incorrect harness argument. Neither
failure required an engine change. These comparisons do not establish
pixel-perfect full-frame rendering parity.

## Network and streaming

The live host/peer/referee/late-spectator check used separate map roots and
completed 300 matching ticks, hash `183efa1c94425d9c`. It exercised map transfer,
outgoing mana toggles and restoration, sender stamping, targeted chat,
excluded recipients, empty masks, and broadcast/spectator privacy.
Encrypted upload, reconnect, hostname rejection, and stream-key redaction passed.
No public broadcast was started.

The complete remote sweep passed on **tak.pgnet.us** and **vpn3.pgnet.us**:
37 scenarios, 52 client sessions, no unexpected desyncs and no incomplete runs.
Cases requested five simulated minutes, accepting normal earlier conclusions.
Coverage includes both balances, naval maps, fog/start rules, unit caps, stress,
multiple seated players, live orders, latency/jitter, loss, and all-AI spectators.
Surviving seated players continued after opponents concluded normally.
The spectator stress case reached tick 9,000 with 8,202 surviving units.
Spectators validate flow control, not hash consensus; live-order cases establish
consensus within a match, not repeatable hashes between runs.

Both negative checks passed: planted tick-900 desync detection and gameplay-data
mismatch rejection. The temporary override was restored and test servers stopped.
The isolated remote servers used Ubuntu GCC 13 optimized Debug (`-O2 -g`), built
from the same `7d463ee` source as the optimized client. Production servers were
not replaced. An initial preflight correctly refused a stale client build ID;
the complete sweep ran after rebuilding it. Logs:
`/tmp/tak-0712-remote-final.log` and `/tmp/desync-remote-3134704/`.

## 16,000-unit smoke tests

Each workload completed 900 ticks with the per-player cap at 2,000. AI combat
used eight Absurd AIs and Crusades balance.

| Workload | Ending live units | Observed simulation speed |
| --- | ---: | ---: |
| Synthetic movement | 16,000 | 2.478× |
| Mixed-unit patrol | 16,000 | 0.923× |
| Destination congestion | 16,000 | 0.931× |
| AI combat | 14,605 | 1.194× |

These headless smoke runs overlapped other checks on the same machine. They are
not controlled performance comparisons, rendered frame rates, or sustained-speed
guarantees. In particular, this does not establish 1× speed for every 16,000-unit
workload.

## Packaging

The candidate fixed an Apple Clang structured-binding capture compilation error
in the AI reservation loop. Its portable form preserves the same decisions.
Candidate builds: [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/36576012881),
[macOS ARM64](https://github.com/pocketgeek/tak-engine/actions/runs/36576012764),
[seven Linux packages](https://github.com/pocketgeek/tak-engine/actions/runs/36576012838),
and [determinism](https://github.com/pocketgeek/tak-engine/actions/runs/36576012861).

Tag `v0.7.12` points to `7d463ee`. Its packaging workflows are
[Windows](https://github.com/pocketgeek/tak-engine/actions/runs/36577795939),
[macOS ARM64](https://github.com/pocketgeek/tak-engine/actions/runs/36577795845), and
[Linux](https://github.com/pocketgeek/tak-engine/actions/runs/36577795979).
All three tag workflows passed. All 14 platform packages and debug archives are
attached to [the release](https://github.com/pocketgeek/tak-engine/releases/tag/v0.7.12).

## README and screenshots

The README now describes the released diplomacy, expansion, and memory changes,
protocol 195, and the need to update clients and server together. All nine
images were freshly captured at 1600 × 1000 and visually inspected. The gallery
includes a large Aramon army from the 16,000-unit patrol fixture, ships on water and streaming setup without a key. The new diplomacy
image uses the input-test fixture and labels its sample state. Results also use
sample statistics. The setup image uses a real GameView connected to a temporary
local server. See [capture notes](img/README.md) for scene and rendering details.
Release and optimized Debug client/server targets were rebuilt after tagging;
`build/takclient --version` reports `0.7.12 (build v0.7.12)`.
