# TAK Engine 0.7.15

Improve long-match performance by removing fully expired unit records from the
simulation and using compact render snapshots. Previously, accumulated casualties
kept increasing simulation, AI, checksum and snapshot work even when few units
remained alive.

- Preserve active corpses, scripts and outstanding projectile/spell sources.
- Keep unit IDs stable and preserve delayed kill attribution after retirement.
- Maintain correct selection, rendering and interpolation after storage moves.
- Correct benchmark stationary-unit tracking across compaction.

A synthetic test with 300 living units and 60,000 expired records reduced average
tick-and-checksum time from 6.26 ms to 0.022 ms. This excludes rendering and AI;
it is not a promised frame-rate gain. The reported nine-hour Windows spectator
slowdown still needs a long-session retest.

Protocol **201**: update clients and servers together. Version 0.7.14 (protocol
200) cannot join the same match. No new dynamic runtime dependencies were added.
Retail game data remains external and is not distributed.

See the [validation report](https://github.com/pocketgeek/tak-engine/blob/main/docs/release-0.7.15-validation.md)
and [performance measurements](https://github.com/pocketgeek/tak-engine/blob/main/docs/unit-retirement-performance-2026-09-29.md).
