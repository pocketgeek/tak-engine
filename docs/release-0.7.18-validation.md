# 0.7.18 release validation

Protocol **211**, campaign payload **4**, database schema **9**. Clients and
servers must be updated together. Retail data is external; campaign service
activation and map assignments require server configuration.

## Local checks

All targets were rebuilt with version 0.7.18. Release **155/155**, optimized
GCC Debug **163/163** and Clang Debug **163/163** CTests passed. Unoptimized GCC
Debug and the corrected Clang ASAN/LSAN suite are still running.

The first sanitizer attempt aborted SDL-linked tests because the local cache
mixed GCC's C AddressSanitizer runtime with Clang's C++ runtime. Configuring both
languages with Clang corrects this build issue; it does not require engine changes.
Leak detection is enabled without suppressions, with dummy SDL audio/video and
explicit SDL DBus shutdown.

GCC/Clang O0/O2/O3 deterministic math checks agree on
`dcef618cd2e4d558`; local ARM legs lack target headers and were skipped.
All **28** Crusades research-tool tests passed. Two fresh local client/referee/AI
runs reached tick 150 with identical `af6adf8833b57e3e` hashes and no desync.
Release client, server, Cartographer and Crusades admin link only libc/libm.

Before release preparation, data-backed naval tests passed all five difficulties
with both balance modes on Varro Passage. A regression control failed seven
checks against the old AI. Accelerated 4× AA rendering passed 36 window resizes
and 17 injected target-reset paths; the user's exact live black-window failure
was not reproduced.

## Remote and platform checks

The isolated remote referee was rebuilt with Ubuntu GCC 13, Debug hooks and
`-O2 -g`, from the same source as the local optimized client. Public server
binaries were not replaced. The sweep on tak.pgnet.us and vpn3.pgnet.us passed
source identity, planted desync detection at tick 900 and gameplay-override
rejection gates. The 37 multiplayer scenarios are running.

Release preparation is awaiting Linux, Windows and macOS CI before tagging.
Package publication and integrity checks will be recorded after the tag builds.

## Documentation and captures

README release information is updated. The existing gallery remains explicitly
labeled 0.7.16 until fresh screenshots are captured and inspected after publication.
Retail was not launched for this release sweep. Native Windows/macOS interactive
sessions and a real GPU device loss were not tested locally.
