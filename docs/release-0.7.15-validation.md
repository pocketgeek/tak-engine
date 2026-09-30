# 0.7.15 release validation

Engine source `098d837` adds deterministic final-retirement cleanup and compact
render snapshots. The release changes the version and documentation; protocol is
**201**. Clients and servers must be updated together.

## Engine checks

All **378** local CTests passed: Release **124/124**, optimized Debug **129/129**,
and Clang AddressSanitizer/LeakSanitizer **125/125**. Sanitizer tests used dummy
SDL video/audio and `SDL_SHUTDOWN_DBUS_ON_QUIT=1`, without leak suppressions.

Focused coverage includes corpse retention, final retirement, stable IDs,
replay reset, delayed kill credit, delayed spell damage against a retained-source
control, peer hashes, and compact snapshots with high IDs. The UI integration
retires 240 temporary records before selection, gifting and rendering checks.

Eight Absurd AIs on Ulasem Arena with Crusades and 800 initial units completed
9,000 ticks, ending with 241 alive / 265 retained records. All 300 periodic hashes
matched across two optimized Debug runs and one Release run; final hash
`1acfe88dca8c00c7`. This is five game minutes, not the reported nine-hour session.

The live local host/referee/late-observer scenario passed 330 matching ticks,
including script actions and victory (`6d9f4eb1ff38520c`). GCC/Clang O0/O2/O3
math checks agreed on `dcef618cd2e4d558`; local ARM cross-build legs lacked target
headers and were skipped.

Full timings, scope and lifecycle details are in the
[performance report](unit-retirement-performance-2026-09-29.md). Remote-server
sweeps and native interactive Windows/macOS sessions were not rerun for this fix.
The existing 0.7.14 screenshots remain representative; this release changes
storage and performance rather than the interface.

## Publication

Platform package builds and final version checks are recorded after tagging.
