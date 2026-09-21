# Rebuild and regression sweep — 2026-09-20

Requested after fixing multiplayer credentials leaking into a later single-player launch.
Protocol remains 174. Local all-target builds completed in `build` (Release),
`build-dbg` (Debug), and `build-o2` (Debug with `-O2 -g`).

## Completed local checks

- CTest: 39/39 in each configuration (117 total), including retail-data tests.
- Python tool tests: 123/123.
- Retail comparisons: 62 routine checks plus 15 search/movement checks.
- Shadow raster comparison passed.
- Bink colors: all 16 sampled retail-DLL frames passed under Proton.
- AI: all 40 faction/difficulty/balance scenarios completed 900 simulated seconds.
  Passive issued no attack commands in all ten cases; the other 30 issued attacks.
- Repeated seated multiplayer: both runs reached tick 600 with hash
  `b879f25ebe4532bf`, without network/simulation errors.
- Menu regression: a debug launch carrying a previous multiplayer username reached
  the single-player Create screen and connected with the local player identity.
- GCC/Clang native fixed-math golden: `8adc4762a852fadd` at O0/O2/O3.
  ARM cross-build checks were skipped because target headers/libraries are missing.

The first visual-quality oracle run exposed a test-output bug: flyer projection
self-tests printed a PASS line into `--quality` numeric output. Restricted those
self-tests to the no-argument mode. The corrected oracle passed all 8,192 native
comparisons, and the visual CTest passed again in all three builds. Simulation/pathfinding code was not changed during this sweep.

## Remote and crowd results

All 37 remote scenarios completed: 35 seated-player scenarios (52 human seats)
reported no unexpected desyncs, and both spectator flow-control scenarios reached
5,400 ticks. The spectator stress run ended with 7,509 living units. Five seated
scenarios ended earlier by game conclusion; those are not full-duration passes.
The 450-Hunter crowd comparison matched every compared movement field through
all 1,800 ticks, including arrival and blockage state.

The two multiplayer stress scenarios exposed an avoidable reconnect pause:
when a defeated player disconnected, the server held their slot and paused
survivors until grace expired. `Server::dropClient` now routes referee-confirmed
defeated players through ordinary room departure; active players still receive
reconnect grace. All-target builds and all 117 CTests passed again after this
change. The repeated local network hash also remained unchanged.
Both affected remote cases passed again with original seeds 2021 and 2023:
all four client seats completed without desync. Server logs confirm that defeated
client 1 left immediately without a reconnect pause; both surviving clients
reached tick 5,400. The order-issuing case legitimately differs in hashes/timing
because commands are assigned to their arrival ticks.

The initial sweep completed before the departure fix. Its results are retained;
the targeted reruns validate the server change separately.

Remote tests use a freshly rebuilt Ubuntu-compatible optimized Debug server at
`/home/pocket_geek/takserver.sweep174` (initial) and
`/home/pocket_geek/takserver.sweep174-fixed` (departure fix) on tak.pgnet.us and vpn3.pgnet.us, leaving
production server binaries alone. Each scenario requests three simulated minutes
(5,400 ticks), or ends earlier upon game conclusion. This is a full scenario-table
regression run, not a 45-minute-per-scenario soak. Unit cap remains 2,000 per player.
The deliberate desync at tick 900 and gameplay-override mismatch were detected.
Spectator cases check progress/flow control, not lockstep hash consensus.

## Logs

- `/tmp/full-sweep-*-build.log`, `/tmp/full-sweep-*-tests.log`
- `/tmp/full-sweep-python.log`, `/tmp/full-sweep-determinism.log`
- `/tmp/full-sweep-oracles.log`, `/tmp/tak-sweep-oracles/`
- `/tmp/full-sweep-search.log`, `/tmp/tak-sweep-search/`
- `/tmp/full-sweep-visual-quality.log`, `/tmp/full-sweep-shadow.log`
- `/tmp/full-sweep-bink.log`, `/tmp/full-sweep-ai.log`
- `/tmp/full-sweep-network.log`, `/tmp/full-sweep-menu.log`
- `/tmp/full-sweep-remote.log`, `/tmp/desync-remote-2732417/`
- `/tmp/full-sweep-crowd.log`, `/tmp/full-sweep-crowd/`
- Departure regression: `/tmp/full-sweep-defeat-rerun.log`, `/tmp/desync-remote-1155729/`
