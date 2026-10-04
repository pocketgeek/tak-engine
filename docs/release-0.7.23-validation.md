# 0.7.23 release validation

Protocol **221**, replay format **11**, campaign payload **4**, database schema **9**.

Release preparation is in progress. Final remote, CI and package results will be
recorded here before publication.

## Scope and compatibility

Retail remains the default pathfinder and campaigns retain it. The Retail search
implementation files match their pre-Flowfield baseline. Flowfield is optional
and experimental; congestion and optimistic exploration on giant maps can still
produce long routes or waits. Its reserved storage ceiling is per World, rather
than eagerly allocated memory or a process-wide RSS limit.

New area-build commands and automatic builder patrol repairs require protocol
221 for live play. Protocol-220/format-11 and protocol-219/format-10 replay tests
retain their original pathfinder and disable the new automatic repair behavior.

## Preparation checks

- Full local Release and Debug sweeps exposed an outdated fake Crusades lobby
  message missing the pathfinding byte. The test fixture was corrected.
  The final Release suite passed **193/193** CTests. Debug passed 200 tests;
  its giant-maze group regression exceeded its 300-second wall-clock limit
  during the parallel sweep. Its strict progress assertions are unchanged;
  its time allowance is now 600 seconds and a repeat is in progress.
  The SDL UI fixture also enables DBus shutdown cleanup for sanitizer runs,
  releasing allocations otherwise retained by the SDL/DBus process-global cache.
- Python research-tool suite: **157 tests**, one optional corpus test skipped.
  The separate owned-retail archive verifier run passed all **6 tests**, including
  in-memory modified-signature/header/directory negatives; no retail file changed.
- The preceding builder automation sweep passed 40 targeted CTests in each
  build, plus four private 300-tick host/guest/referee/late-spectator scenarios
  covering Retail/Flowfield and both balance sets.
- GCC/Clang x86_64 O0/O2/O3 determinism runs agreed on
  `dcef618cd2e4d558`; ARM emulation legs were unavailable due to missing target
  headers. Native ARM64 CI remains a separate release gate.

Focused AddressSanitizer/leak checks passed **23/23** CTests covering builders,
movement, Flowfield fields/snapshots/admission/World, transport, production, AI,
command parsing, parser limits, validation workers, replay, ACME and Crusades UI.
The UI test releases SDL/DBus global allocations at shutdown.

No public server deployment or public certificate-authority probing is part of
this release validation. Remote game tests use isolated executables and ports.
