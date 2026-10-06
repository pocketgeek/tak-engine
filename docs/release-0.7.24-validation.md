# 0.7.24 release validation

Protocol **227**, replay format **11**, generator version **8**. Campaign payload
and database schema remain unchanged. This report is being completed during
release preparation; publication and package verification are pending.

## Scope and compatibility

The release includes the accumulated work since 0.7.23: Retail+ and Cooperative,
Flowfield movement refinements, behavior-equivalent Retail search optimizations,
renderer/AA improvements and restored graphics controls, live GUI-art smoothing,
builder input/queued-order display fixes, generated-map mana controls and local
server status queries. See [release notes](release-0.7.24-notes.md).

Retail remains the default; alternatives are experimental. Retail+ applies new
crowd behavior only to ordinary ground Move orders. It is not a general
large-army performance upgrade: some arrival/recovery cases improve and some
saturated bottlenecks regress. The Retail+ report (removed with the mode on 2026-10-06; see git history)
includes all 120 matched timing cases, baseline equivalence, physical outcomes,
memory observations and explicit limits. Its older timing binary is identified
by SHA-256; the version bump does not turn those measurements into new timings.

Live clients/referees must use matching protocol 227 builds. Format-11 replay
playback requires the current simulation protocol; older recordings need their
original engine. Generator-8 recipes replace earlier seed recipes; existing
saved `.kmp` snapshots remain playable. No new dynamic runtime dependencies or
retail assets are added.

## Local preparation

Release, Debug and optimized Debug all-target builds have completed with version
0.7.24. Full CTest sweeps, Clang and sanitizer validation are in progress.

- GCC/Clang x86-64 deterministic-math checks at O0/O2/O3 agree on
  `dcef618cd2e4d558`. ARM emulation checks skip because target headers are absent;
  native ARM64 platform CI is a separate gate.
- Python research suite: 157 tests, one optional corpus test skipped. The
  separate owned-retail archive verifier passes all six tests, including the
  corpus. No retail file was modified.
- Syntax checks: 410 Python files and 12 shell scripts pass.
- Independent review covers the full source delta and native CI target
  availability. Release regressions now run the new asset-independent rendering
  and navigation tests on Linux, Windows and macOS, including ARM64.

## Remote multiplayer

The full 37-scenario table is being exercised for each pathfinding mode with
isolated optimized Debug referees on `tak.pgnet.us` and `vpn3.pgnet.us`, matching
optimized local clients. Tests retain real seated players for checksum coverage;
all-AI spectator scenarios test flow control rather than client/referee hashes.
Public binaries/services are not replaced. Test executables, ports, caches and
recordings are separate from the public service.

## CI and packages

Pre-tag platform CI, tagged builds, Windows signing/timestamp verification and
downloaded release-asset verification are pending. No release has been published
at this stage.

## Screenshots and limits

All thirteen README/Cartographer gallery views are being recaptured from the
0.7.24 preparation build with isolated preferences. [Capture notes](img/README.md)
identify development fixtures and sample results explicitly.

Local renderer checks cover Linux, not hands-on Windows/macOS GPU behavior or
every high-DPI/device-reset combination. Remote simulated-time sweeps do not
substitute for a multi-hour wall-time soak. Native CI coverage and remaining
test gaps will be recorded before publication.
