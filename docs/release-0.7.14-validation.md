# 0.7.14 release validation

The release includes the scenario work in `54b37e3` and `2fa44e8`, followed by
version, documentation and screenshot updates. Protocol **200** requires matching
clients and servers; 0.7.13 used protocol 198.

## Engine validation

The completed scenario sweep passed all **373** CTest checks: Release 123/123,
optimized Debug 127/127, and AddressSanitizer/LeakSanitizer 123/123. Leak detection
was enabled without suppressions, with SDL dummy video/audio and explicit DBus
shutdown. The earlier restricted-environment leak run was not counted; the
unrestricted rerun passed.

All **229** focused native comparisons passed: 99 unit-action cases, 75 selector
cases, 29 placement/name cases and 26 resource/clock cases. The existing native
construction-economy comparison passed 8,192 cases. Retail routines were emulated
offline; no retail game window was launched. GCC/Clang O0/O2/O3 builds agreed on
ordinary-simulation golden hash `dcef618cd2e4d558`; local ARM checks lacked target
headers and were skipped.

The solo-authored scenario passed 330 matching ticks across host, referee and
late observer, including scripted creation, raw HP damage, movement, resource
suppression/reset and victory. Companion transfer passed 300 matching ticks with
host, peer, server and late spectator caching the verified map independently.
Directed chat/privacy checks and Release `--play-map` private-server launch,
snapshot verification and trigger-log creation passed.

Full details and fidelity limits are in the
[scenario validation report](scenario-actions-validation-2026-09-29.md).
The previous release's remote-server and 16,000-unit stress results remain
historical evidence; they are not represented as fresh 0.7.14 runs.

## Release checks

Linux, Windows, macOS and determinism CI all passed for engine source `2fa44e8`.
The release version is set by CMake; protocol remains 200. Local Release,
optimized Debug and sanitizer targets are rebuilt with the release version.
The version-stamped suites passed again: Release **123/123**, optimized Debug
**127/127**, and AddressSanitizer/LeakSanitizer **123/123**. Package publication
and final release checks are recorded below.

## Screenshots

All eleven gallery images are refreshed from the 0.7.14 build and inspected.
Actual engine/editor rendering is used; sample results and development scenes
are identified in captions and [capture notes](img/README.md). Retail assets,
stream keys and temporary capture harnesses are not distributed. The unrelated
untracked weapon probe remains outside this release.

## Published packages

Tag `v0.7.14` points to `4c58e5e`. All tag workflows passed:
[Linux](https://github.com/pocketgeek/tak-engine/actions/runs/36653682604),
[Windows](https://github.com/pocketgeek/tak-engine/actions/runs/36653682574), and
[macOS ARM64](https://github.com/pocketgeek/tak-engine/actions/runs/36653682602).
All **14** platform packages and debug archives are attached to the release.

Downloaded macOS and Windows ZIPs and the Ubuntu 24.04 package were inspected
for client, server, Cartographer and licenses. Linux desktop launchers and macOS
bundle versions were checked; the app bundles identify version 0.7.14. The first
Linux inspection guessed the desktop filenames incorrectly; the corrected check
verified `tak-client.desktop` and `tak-cartographer.desktop` successfully.
Native interactive Windows/macOS sessions were not run on this Linux host.

All targets in local Release, optimized Debug and sanitizer builds were refreshed
after tagging. Client/server versions report `0.7.14 (build v0.7.14)`.
