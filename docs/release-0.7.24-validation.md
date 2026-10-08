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

Release, Debug, optimized Debug, Clang Debug and AddressSanitizer all-target
builds have completed with version 0.7.24. They were rebuilt after the portability
fix and report `v0.7.23-3-g86673b413a47-dirty`, identifying preparation builds
rather than a release tag. The Release sweep passed all **230** cases after correcting
one test fixture: its `--port 0` was rejected by the stricter CLI before the
intended malformed-campaign check. The fixture now selects a valid ephemeral
port; the focused rerun passed without changing server validation. The full
Debug and Clang Debug suites each passed **242/242** tests.

Focused AddressSanitizer/leak validation passed **43/43** cases, including the
new navigation modes, renderer caches/AA, builder input, live GUI smoothing,
movement, production, transport, replay, parser limits, server commands and
ACME. Six UI/rendering fixtures initially reported SDL/DBus global allocations
retained at shutdown. They now request SDL's explicit DBus cleanup, as the
existing editor fixtures do; the complete 43-case rerun passes with leak
detection enabled and no suppression.

Accelerated Linux/X11/OpenGL validation passes all **15** Terrain/Model AA
combinations, including painter order, transparency, native-resolution UI,
resource fallback, tile sampling and resize recovery. All **2,688** accelerated
geometry pixel/state comparisons pass on the NVIDIA RTX 5070 Laptop GPU. The
synthetic resize test uses X11 because SDL's offscreen drawable does not resize
correctly here, as already documented in the [AA review](antialiasing.md).
DBus shutdown cleanup is limited to isolated dummy-video fixtures; forcing it
on the accelerated X11 process triggered a shared-library DBus shutdown
assertion. The normal accelerated run without that override passes.

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

The local evidence snapshot at **2026-10-05 19:02 UTC** records:

| Mode | Completed scenarios | Completed seats |
| --- | ---: | ---: |
| Retail | 37/37 | 52/52 |
| Retail+ | 31/37 | 45/52 |
| Cooperative | 13/37 | 13/52 |
| Flowfield | Not started in available evidence | — |

Completion requires 18,000 ticks or an actual concluded result for that seat;
one player's conclusion does not imply the whole multiplayer game has ended.
The Retail table includes two spectator scenarios without client/referee hash
comparison. All three started tables passed planted-desync detection and
override-transfer/loaded-hash rejection. No natural desync appears in the
available case logs. Recorded human clients agree at 9,104 shared checkpoints
across 22 Retail seat pairs and 8,587 across 21 Retail+ pairs.

The harness now requires a successful network-shaping setup, archived qdisc and
exact game-port filter evidence, and positively confirmed cleanup before
releasing ownership. Cleanup also requires its lock and matching owner token.
Failed or missing per-case statuses make the whole sweep fail, including setup
errors before a client log exists. Independent local mocks cover early setup
failures, wrong filters, failed cleanup queries, remaining shaping, changed
owners, failed locks and missing status. These checks pass. The original Retail
and Retail+ runs lack complete per-case shaping evidence; their eight impaired
scenarios each must be repeated under the corrected harness before claiming
verified latency/loss coverage.

Referees use Ubuntu GCC 13.3 optimized Debug (`-O2 -g`); local clients use GCC 16
with the same optimization mode. They were frozen from the release-preparation
worktree before its commit and report a dirty build identity. Binary SHA-256:

- Client: `e11a7acbb0f0f7499f9f9efbc281e87b8ecff129f528452e2c647952f116885e`.
- Referee: `c9bf0765a2c827c79a985d3abc792b6cedf9f82f6fe352c90a8fed3e6190ef68`.

These are preparation-build checks, not tests of published tagged binaries.
Remaining mode completions, impairment confirmations, remote cleanup and final
source/package verification are still pending.

## CI and packages

Pre-tag Windows CI found a renderer variable named `far` that conflicts with a
legacy Windows header macro on x64 and ARM64. Renaming it to `positiveInfinity`
changes no calculations. The corrected engine commit
`86673b413a4747e2b19a128777c2223656cd0681` passes
[all 16 Linux package jobs](https://github.com/pocketgeek/tak-engine/actions/runs/37356772509),
[both Windows architectures](https://github.com/pocketgeek/tak-engine/actions/runs/37356772526),
and [both macOS architectures](https://github.com/pocketgeek/tak-engine/actions/runs/37356772477).
The unchanged simulation code also passes
[determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/37355539882)
on preparation commit `3255400007b3bda040c9b8aebe23a841bf5a5c63`.
Local renderer regressions and both automated commit reviews pass.
Tagged builds, Windows signing/timestamp verification and
downloaded release-asset verification are pending. No release has been published
at this stage.

The session subsequently changed to restricted network access and read-only
Git metadata. GitHub requests fail and Git cannot create `.git/index.lock`.
Local review/builds can continue, but the remaining changes cannot be committed,
tagged or published until that access is restored. This is an incomplete release
report, not a publication announcement.
The 43-case sanitizer pass predates that restriction. LeakSanitizer now fails
its process-inspection step in this sandbox; leak checking was disabled only
for reading the rebuilt sanitizer binaries' `--version` output, not for the
reported regression run.

## Screenshots and limits

All thirteen README/Cartographer gallery views were freshly captured and visually
inspected from the 0.7.24 preparation build with isolated preferences. [Capture notes](img/README.md)
identify development fixtures and sample results explicitly.

Local renderer checks cover Linux, not hands-on Windows/macOS GPU behavior or
every high-DPI/device-reset combination. Remote simulated-time sweeps do not
substitute for a multi-hour wall-time soak. Native CI coverage and remaining
test gaps will be recorded before publication.
