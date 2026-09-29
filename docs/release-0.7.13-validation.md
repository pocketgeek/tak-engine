# 0.7.13 release validation

Checks performed on 2026-09-29 against source `af7f653`, followed by release
metadata/documentation changes. Protocol **198** includes explicit authored
Cartographer playtest setup. Ordinary skirmishes retain their existing setup.

## Local tests

All targets were rebuilt in all five configurations; all 592 CTest checks passed.

| Configuration | Passed |
| --- | ---: |
| Release | 116/116 |
| Optimized Debug (`-O2 -g`) | 120/120 |
| Unoptimized Debug | 120/120 |
| Clang Debug | 120/120 |
| AddressSanitizer + LeakSanitizer | 116/116 |

The sanitizer sweep used dummy SDL video/audio, `SDL_SHUTDOWN_DBUS_ON_QUIT=1`
and leak detection with no suppressions. This covers the exercised paths, not
proof that every possible leak is absent.

Python reverse-engineering tools: 123/123. Deterministic-math guard passed.
Available native GCC/Clang optimization comparisons agreed on golden hash
`dcef618cd2e4d558`; local ARM legs lack target headers/libraries and were skipped.

All 52 fresh 900-second AI cases completed: five factions × four difficulties ×
two balances on Ulasem Arena, plus Zhon on generated 8×8 maps with seeds 1, 42
and 777 at each difficulty with Crusades balance. Every case produced an army;
Passive issued no Attack/AttackMove commands. The opposing player was idle.

## Offline retail comparisons

No retail game window was launched. All 204 shipped scripts × four controlled
animation profiles × 1,500 ticks passed (816/816). Death-sound comparisons passed
500 priority/age/looping cases and 512 visibility/selection/class/free-voice routes.
Model comparisons passed all 16 piece-flag combinations, both body-cache passes,
shadow admission, 289 sloped faces, callbacks, all 32 indexed shade rows and
transparency. These tests do not claim full-frame visual parity.

The first model-harness invocation used an unsupported `--binary` flag and did
not run the comparison. The corrected positional invocation passed; no engine
change was needed.

## Network and streaming

Host, peer, referee and late spectator completed 300 matching ticks with authored
scenario companions transferred and cached separately, hash `504864d2961d486a`.
The check also covered directed mana/chat routing and spectator privacy.
A Release `--play-map` client launched a private local server, verified the map
snapshot and created its explicitly requested persistent trigger log.

Local encrypted streaming upload, reconnect, hostname rejection and key redaction
passed. No public broadcast was started.

## 16,000-unit smoke workloads

Each workload completed 900 ticks with a per-player cap of 2,000. Match combat
used eight Absurd AIs and Crusades balance.

| Workload | Ending live units | Observed simulation speed |
| --- | ---: | ---: |
| Synthetic movement | 16,000 | 3.438× |
| Mixed-unit patrol | 16,000 | 1.067× |
| Destination congestion | 16,000 | 1.366× |
| AI combat | 14,605 | 1.211× |

These headless runs overlapped builds and other checks. They are smoke results,
not controlled performance comparisons, rendered FPS or sustained-speed promises.

## Remote multiplayer sweep

All 37 scenarios and 52 client sessions completed on **tak.pgnet.us** and
**vpn3.pgnet.us**, with no unexpected desyncs or incomplete runs. Each case
requested five simulated minutes, accepting normal earlier match conclusions.
Coverage includes both balances, naval maps, fog/start rules, unit caps, stress,
multiple seated players, live orders, latency/jitter, packet loss and spectators.
The all-AI spectator stress run reached tick 9,000 with 8,202 surviving units.
Spectator cases cover flow control, not hash consensus; live-order cases establish
within-match consensus, not repeatable hashes across runs.

Both negative tests passed: planted tick-900 desync detection and gameplay-data
mismatch rejection. The temporary override was restored and the sweep servers
were stopped. Isolated remote servers used Ubuntu GCC 13 optimized Debug
(`-O2 -g`), matching client source `af7f653`; production servers were not replaced.
The test binaries retained the 0.7.12 version stamp during this pre-tag sweep.
Logs: `/tmp/tak-0713-remote.log` and `/tmp/desync-remote-1595394/`.

## Documentation and screenshots

The README describes the 0.7.13 editor changes and protocol 198, with two freshly
captured and visually inspected Cartographer images: unit browser/map canvas and
F6 model inspector. Existing game-gallery images remain labeled 0.7.12.
See [capture notes](img/README.md). The unrelated untracked retail-weapon probe
was left untouched and is not part of the release.

## Packaging

Linux, Windows, macOS and determinism CI passed for source `af7f653` before
release preparation. The 0.7.13 tag starts fresh packaging workflows; their final
publication results will be recorded here after they complete. Native interactive
Windows/macOS testing was not performed on this Linux host.
