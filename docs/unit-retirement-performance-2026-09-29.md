# Long-match unit retirement — 2026-09-29

## Problem and change

A Windows spectator report described eight AIs on Ulasem Arena: after nine real
hours, roughly 300 units remained, but requested 4× simulation ran at about 3.2×.
Inspection found that the simulation retained every historical unit record and
render snapshots were indexed by lifetime unit ID. Simulation, AI and state
hash scans therefore grew with accumulated casualties, independently of the
number of surviving units.

Ordinary matches now compact fully retired records once per simulation second,
with a one-second grace period after final retirement. Active corpses, corpse
footprints, scripts and sources of outstanding projectiles/spells remain present.
Unit IDs are stable and never reused. A small owner history preserves delayed
kill attribution; its immutable checksum is accumulated rather than rescanned.
Spatial indices are refreshed after compaction. Render snapshots and geometry
indices use compact slots with validated ID lookup, including interpolation.

Small ID lookup tables and owner attribution still grow with historical IDs;
this is not a claim of constant memory usage. Full unit and render records no
longer accumulate forever. Restored native allocation-pool fixtures retain their
existing lifecycle. Protocol 201 prevents joining a protocol-200 peer with a
different record/hash lifecycle. Released 0.7.14 remains protocol 200.

## Focused timing

Linux Release, 300 living synthetic stationary units, three successive history
sizes; 300 timed ticks per sample, including one state hash every 30 ticks.
The old binary was preserved before changing the engine. The revised fixture
warms for 60 ticks instead of 30 to cover the retirement grace period.

| Retired units accumulated | Old records | New records | Old ms/tick | New ms/tick |
| --- | ---: | ---: | ---: | ---: |
| 0 | 300 | 300 | 0.0292 | 0.0267 |
| 30,000 | 30,300 | 300 | 2.5424 | 0.0244 |
| 60,000 | 60,300 | 300 | 6.2554 | 0.0216 |

Run the new workload with `build/unit_retirement_test --benchmark`. These are
synthetic simulation timings, excluding rendering, AI and real-map navigation.
They demonstrate removal of historical-record cost, not a predicted Windows FPS
or simulation-speed gain. Other validation processes ran on the same machine.

## Validation

- Release: 124/124 CTests; optimized Debug: 129/129; Clang
  AddressSanitizer/LeakSanitizer: 125/125, with no leak suppressions. Sanitizer
  tests use dummy SDL video/audio and `SDL_SHUTDOWN_DBUS_ON_QUIT=1` for complete
  SDL test-process teardown (the first run omitted this existing test setting
  and reported SDL DBus allocations). Total: 378 passing tests.
- Regression coverage: paired-world hashes, stable lookup after slot movement,
  active corpse retention, delayed kill credit, delayed spell damage versus a
  retained-source control, replay reset, and stale/high-ID render lookup.
- UI integration: retire 240 temporary records before exercising selection,
  gifting and rendering with high-numbered IDs.
- GCC/Clang O0/O2/O3 deterministic-math checks agree on `dcef618cd2e4d558`.
  ARM cross-build legs were unavailable because target headers were missing.
- Live local host/referee/late-observer scenario: 330 ticks with matching hash
  `6d9f4eb1ff38520c`, including script actions and victory.
- Ulasem Arena: eight Absurd AIs, Crusades, 800 initial mixed units, 2,000 cap
  per player, 9,000 ticks. Finished with 241 alive / 265 retained records;
  13,427 hit events. Five game minutes ran in 11.497 seconds (26.093×), excluding
  rendering. Final hash: `1acfe88dca8c00c7`. The benchmark's stationary-unit
  metric now matches samples by stable ID across compaction. All 300 periodic
  hashes agree in a repeated optimized Debug run and a Release run.

The original nine-hour Windows session has not been reproduced. This fixes a
measured source of long-match overhead; it does not establish that it was the
only cause of that report.
