# 0.7.19 release validation

Protocol **211**, campaign payload **4**, database schema **9**. The checks used
matching engine revisions and compatible retail data. Connection enforcement
checks protocol and gameplay data; the remote harness separately checks build
identity. Retail data remains external.

## Local checks

All targets were rebuilt with version 0.7.19. All **807/807** CTests passed:
GCC Release **155/155**, optimized GCC Debug **163/163**, unoptimized GCC Debug
**163/163**, Clang Debug **163/163**, and a fresh Clang ASAN/LSAN build **163/163**.
Both sanitizer-build languages use Clang. Leak detection is enabled without
suppressions, with dummy SDL audio/video and explicit SDL DBus shutdown; no
address-sanitizer, leak-sanitizer or suppression reports appeared.

GCC/Clang O0/O2/O3 deterministic math checks agree on
`dcef618cd2e4d558`. Local ARM legs lack target headers and were skipped; tagged
Windows/macOS ARM64 jobs run their native math and driver-equivalence checks.
All **28** Crusades research-tool tests passed. Two fresh local client/referee/AI
runs reached tick 150 with identical `af6adf8833b57e3e` hashes and no desync.
Release client, server, Cartographer and Crusades admin link only libc/libm.

Before release preparation, the cleanup passed 481 Release/Debug checks and
preserved the data-backed simulation hash `882315af95c09584` before/after in GCC
Release, GCC Debug and Clang Debug. The live footprint inspection tool still
reports the actual navigation-grid connectivity after its obsolete block was
removed. Live movement, pathfinding, combat and animation logic was not changed.

## Remote checks

The isolated remote referee was rebuilt with Ubuntu GCC 13, Debug hooks and
`-O2 -g` from the same source as the optimized local client. Public server
binaries were not replaced. The sweep on tak.pgnet.us and vpn3.pgnet.us passed
build-identity, planted-desync detection at tick 900 and gameplay-override
rejection gates. All **37** scenarios and **52** client seats completed without
desync, referee-suspect errors or stalls.

Thirty-five scenarios compare seated player hashes with the referee; two
spectator-only scenarios exercise flow control without comparing hashes.
Coverage includes both balance modes, naval maps, fog, unit caps, random starts,
two to four human seats, live orders, RTT, jitter and packet loss. Three crowded
matches ended normally before 9,000 ticks. The all-AI stress scenario reached
9,000 ticks with 7,915 living units. Remote network shaping was confirmed removed
from both hosts after the sweep.

## Platform and package checks

Release preparation passed [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/36882095723),
[Windows](https://github.com/pocketgeek/tak-engine/actions/runs/36882095657) and
[macOS](https://github.com/pocketgeek/tak-engine/actions/runs/36882095853) CI.
Tag `v0.7.19` points to `c0bd879b5ca03d0e870188a920519108b632f3a9`.
All tagged workflows passed: [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/36885109153),
[Windows](https://github.com/pocketgeek/tak-engine/actions/runs/36885108791) and
[macOS](https://github.com/pocketgeek/tak-engine/actions/runs/36885108718).
All **19** expected assets are published in the non-draft, non-prerelease
[0.7.19 release](https://github.com/pocketgeek/tak-engine/releases/tag/v0.7.19).

All 19 downloaded files match GitHub's SHA-256 digests. Every Windows/macOS
Release and Debug ZIP passed archive integrity, executable-inventory and license
checks. All four executable types have the expected native x64/ARM64 architecture
and no non-system dynamic imports. Windows client/editor use the GUI subsystem.
macOS executables target macOS 14; Release app bundles declare version 0.7.19,
macOS 14 and their included icons. Native CI also verifies app signatures and
DMG integrity. Debug macOS ZIPs contain standalone executables, not app bundles.

The Ubuntu 24.04 DEB has the correct package version, client/editor launchers and
all four binaries. Extracted client/server report 0.7.19; all four executables
link only libc/libm. Native Windows ARM64 packages are portable ZIPs; NSIS remains
x64 only. No new dynamic dependencies or retail data are shipped.

## Documentation and captures

README release information, downloads, protocol guidance and build documentation
were reviewed and updated. All twelve gallery views were freshly rendered and
visually inspected from the 0.7.19 preparation build, including the large army,
ships on water, campaign picker, Crusades browser and both Cartographer views.
Capture notes distinguish development scenes, sample results and the imported
campaign definition without authored battle maps or inferred ownership.
Five static views rendered identically to their previous files; all twelve were
recaptured. No units, effects or UI were added after capture.

Retail was not launched for this sweep. Interactive Windows/macOS gameplay and
live hardware encoding on Windows ARM64 were not tested locally; native CI runs
startup, streaming and platform regression checks.
