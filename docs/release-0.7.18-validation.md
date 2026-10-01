# 0.7.18 release validation

Protocol **211**, campaign payload **4**, database schema **9**. Clients and
servers must be updated together. Retail data is external; campaign service
activation and map assignments require server configuration.

## Local checks

All targets were rebuilt with version 0.7.18. All **807/807** CTests passed:
Release **155/155**, optimized GCC Debug **163/163**, unoptimized GCC Debug
**163/163**, Clang Debug **163/163** and Clang ASAN/LSAN **163/163**.

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
rejection gates. All **37** multiplayer scenarios and all **52** client seats
completed without desync, referee-suspect errors or stalls. Thirty-five scenarios
compare seated player hashes with the referee; two spectator-only scenarios
exercise flow control without hash comparison. Coverage includes Crusades balance,
naval maps, fog, unit caps, random starts, two to four human seats, live orders,
RTT, jitter and packet loss. Three crowded matches ended legitimately before
9,000 ticks; the all-AI stress scenario reached 9,000 ticks with 7,915 living units.

Release preparation passed [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/36855466760),
[Windows](https://github.com/pocketgeek/tak-engine/actions/runs/36855466805) and
[macOS](https://github.com/pocketgeek/tak-engine/actions/runs/36855466820) CI before
tagging. Tag `v0.7.18` points to `012d2e3d94102ed347a606c21499c4e0d7d0635f`.
All tagged workflows passed: [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/36856934084),
[Windows](https://github.com/pocketgeek/tak-engine/actions/runs/36856934013) and
[macOS](https://github.com/pocketgeek/tak-engine/actions/runs/36856934034).
All **14** expected assets are published in the non-draft, non-prerelease
[0.7.18 release](https://github.com/pocketgeek/tak-engine/releases/tag/v0.7.18).

Downloaded Windows/macOS ZIPs and Ubuntu 24.04 DEB match GitHub's SHA-256 digests.
Archive integrity, executable inventories (including `crusades_admin`), licenses,
macOS bundle versions/icons and Linux package version/launchers passed inspection.
Windows client/editor have the GUI subsystem; all four Windows executables pass
checks against imported non-system media, SQLite and compiler-runtime DLLs.
Extracted Ubuntu client/server run and report 0.7.18; all four packaged Linux
executables link only libc/libm. Their optional runtime build ID is `unknown`;
the successful tag workflow supplies package-source provenance.

## Documentation and captures

README release information is updated. All eleven existing gallery views have
been freshly captured and inspected from the 0.7.18 build, plus a new live local
Darien Crusades territory-browser view. Capture notes identify development scenes,
sample results and the imported campaign definition without authored maps/owners.
README corrections and all twelve inspected images are included in the
documentation update following release publication.
Retail was not launched for this release sweep. Native Windows/macOS interactive
sessions and a real GPU device loss were not tested locally.
