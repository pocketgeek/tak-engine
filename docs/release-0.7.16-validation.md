# 0.7.16 release validation

Engine source `a1c9e50` includes conversion-target and production-exit fixes,
results emblems, version-4 generated maps, and Easy/Defensive AI changes.
The release uses **protocol 203**; update clients and servers together.

## Local checks

All **641** CTests passed: Release **125/125**, optimized Debug **130/130**,
unoptimized GCC Debug **130/130**, Clang Debug **130/130**, and Clang ASAN/LSAN
**126/126**. Sanitizer checks use dummy SDL video/audio, explicit SDL DBus shutdown,
and leak detection without suppressions.

All **65** fresh 900-second AI cases completed: five factions × five difficulties
× two balances on Ulasem Arena, plus Zhon on three generated-map seeds at every
difficulty with Crusades. Every case produced an army; Defensive sent no attack
commands. The opposing player was idle.

The generator's **4,725-case** sweep and all **15** 64×64/eight-player world/layout
cases passed on this engine source. All 105 roster hashes match GCC Release and
Clang ASAN; the version-3 synthetic golden remains unchanged. A 64×64 network
match passed 300 matching ticks and saved the map on host, peer, and server.
These generation checks preceded the release sweep without intervening engine changes.

Python reverse-engineering tests: **123/123**. GCC/Clang O0/O2/O3 deterministic
math checks agree on `dcef618cd2e4d558`; local ARM legs lacked target headers and
were skipped. Native death-audio comparisons passed 500 admission and 512 routing
cases. Model transforms passed 8,192 native vertex comparisons. All 204 shipped scripts
passed four controlled animation profiles at 1,500 ticks each (**816/816**).
The first animation-roster command used the wrong local extraction directory;
the corrected invocation supplied the full script roster.

## Remote checks

The isolated remote referee was built with Ubuntu GCC 13, Debug test hooks and
`-O2 -g`, from the same source as the optimized local client. Existing public
server binaries were not replaced.

The first attempt stopped at the data-mismatch gate because the client used a
retail root without the remote fixture's gameplay overrides. Repeating with the
matching `assets/game` fixture passed source identity, deliberately planted
desync detection at tick 900, and gameplay-override rejection checks.

The full sweep on **tak.pgnet.us** and **vpn3.pgnet.us** passed all **37**
scenarios and all **52** seated clients. Thirty-five scenarios compare player
hashes with the referee; two spectator-only cases verify flow control without
hash comparison. Cases cover Crusades, gods, fog, naval maps, unit caps, random
starts, two to four human seats, live orders, crowded armies, artificial RTT,
jitter and packet loss. Runs target 9,000 ticks (five game minutes); three
crowded matches ended legitimately before that target. The all-AI stress case
completed 9,006 ticks with 8,425 living units and no stall/error.

## Scope

No retail game window was launched. Native Windows/macOS interactive sessions and
the original nine-hour Windows spectator slowdown were not rerun. Cross-platform
CI checks and packaged-artifact checks are reported separately below.

## Screenshots and documentation

All eleven images were freshly captured and inspected from the 0.7.16 build.
They include ships on water, the 16,000-unit army fixture, updated results emblems,
menus, and both Cartographer views. Sample statistics and development scenes are
identified in [capture notes](img/README.md); no game imagery was fabricated.
The README now describes this release, protocol 203, generated sizes, AI behavior,
and multiplayer bandwidth with explicit protocol-calculation assumptions.

Tag `v0.7.16` points to `433046e`. Tag workflows build Linux, Windows and macOS
packages. Duplicate branch package runs for the same commit were canceled.

## Published packages

All tag workflows passed:
[Linux](https://github.com/pocketgeek/tak-engine/actions/runs/36710898722),
[Windows](https://github.com/pocketgeek/tak-engine/actions/runs/36710898718), and
[macOS ARM64](https://github.com/pocketgeek/tak-engine/actions/runs/36710898675).
All **14** packages/debug archives are attached to the
[release](https://github.com/pocketgeek/tak-engine/releases/tag/v0.7.16).

The first macOS attempt compiled and signed both applications, but immediate DMG
verification reported `Resource temporarily unavailable`. Rerunning the same tag
passed disk-image verification, tests and uploads; no source or tag change was needed.

Downloaded Windows/macOS ZIPs and the Ubuntu 24.04 package matched GitHub's SHA-256
digests. ZIP integrity, client/server/Cartographer executables, license files,
macOS bundle versions/icons, Linux package version and desktop launchers passed
inspection. Extracted Ubuntu client/server executables run and report 0.7.16.
Their optional source-build identifier is `unknown`; package provenance is supplied
by the successful tag workflow rather than that runtime field.

All targets were rebuilt locally in Release, optimized Debug, unoptimized GCC
Debug, Clang Debug and ASAN configurations with version 0.7.16. The final follow-up
commits change documentation/screenshots only. All 46 local README link targets exist.
