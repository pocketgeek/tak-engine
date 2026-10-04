# 0.7.23 release validation

Protocol **221**, replay format **11**, campaign payload **4**, database schema **9**.

[Version 0.7.23](https://github.com/pocketgeek/tak-engine/releases/tag/v0.7.23)
was published on 2026-10-04 after the checks below. The tag points to
`337c33f8db97310208165492b2784536570f1117`.

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
  The final Release suite passed **193/193** CTests. Debug passed all **201**
  cases after repeating the giant-maze group regression: it exceeded its
  300-second wall-clock limit during the parallel sweep, then passed in 268.41
  seconds with a 600-second allowance. Its strict progress assertions are
  unchanged. The full Clang Debug suite passed **201/201** CTests.
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

## Remote multiplayer sweep

The full 37-scenario Retail table passed on `tak.pgnet.us` and `vpn3.pgnet.us`,
with **52/52 seats** completed. Each scenario allowed ten simulated minutes,
or ended at an actual match result. Human-seated cases compare client/referee
hashes; the two all-AI spectator cases exercise flow control without claiming
hash verification. The planted desync was detected and the shared-override
loaded-content mismatch was rejected. Cases include both balance sets, naval
maps, stress armies, one to four seated humans, live commands, fog, random
starts, 50–500 ms delay, jitter and 1–3% packet loss.

Flowfield covered the same **37 scenarios / 52 seats**, including both negative
validation gates. The initial parallel run completed 34 scenarios; two large
two-player cases and one stress spectator exceeded the default 900-second
real-time deadline while making progress. All three passed with a 3,600-second
allowance, retaining their original seeds (2021, 2023 and 2035), simulated-time
limit and completion/hash requirements. The surviving human seats and spectator
reached 18,000 ticks. Five additional stress seats also passed with alternate
seeds. These results show consensus and completion, not a claim that Flowfield
maintains 4× simulation speed in dense battles.

The remote harness now exposes `TAK_WALL_TIMEOUT` and preserves full-table seeds
when `--only` filters scenarios. Shell syntax and dry-run checks confirmed all
37 assignments, the three original retry seeds and rejection of invalid deadlines.

Remote servers use GCC 13 optimized Debug builds (`-O2 -g`) from the same engine
sources as the GCC 16 optimized local clients. They run as isolated test
processes on separate ports; no public-server binary/service was replaced.

## Pre-tag CI

The engine changes in `a13839a0cd74190340dd47d0513c2db8170c55e3` passed
[determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/37183045511),
[all 16 Linux package jobs](https://github.com/pocketgeek/tak-engine/actions/runs/37183045481),
[both macOS architectures](https://github.com/pocketgeek/tak-engine/actions/runs/37183045469),
and [both Windows architectures](https://github.com/pocketgeek/tak-engine/actions/runs/37183045470).
Signing is intentionally skipped on ordinary main builds and is a separate tag
release gate.

## Tagged CI and packages

The exact tag passed all four workflows:

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/37185976281).
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/37185962102): all 16 native package jobs and release attachment.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/37185962117): x64, ARM64 and the x64 signing/package job.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/37185962104): x64 and ARM64, including native determinism checks.

Redundant main-branch platform builds for this documentation/harness-only
follow-up were cancelled in favor of the same commit's tagged builds. The
preceding engine commit's main builds passed as listed above.

All **29 assets** were downloaded and checked against GitHub's SHA-256 digests
and byte sizes before publication: ten DEBs, four RPMs, one Arch package, one
Slackware TXZ, two Linux Debug archives, eight ZIPs, two DMGs and one Windows
installer. ZIP CRC checks passed. Linux ELF and Windows PE architectures were
checked from their payloads; macOS ZIP Mach-O architectures and app versions
were checked. Systemd-equipped Linux packages contain the service and both
ACME/settings templates. Slackware and plain Debug archives do not install
systemd integration. DMGs were hash-checked, not mounted locally.

Windows CI signed all **16 shipped executables** before ZIP/NSIS packaging,
then signed the completed installer. Native verification confirmed trusted
signatures and timestamps at both stages. Local inspection also confirmed
embedded certificate tables in every Windows executable and installer.

All targets were rebuilt locally in Release, Debug, optimized Debug, Clang
Debug and AddressSanitizer configurations from the clean tag. Client/server
version output reports **0.7.23 (build v0.7.23)**. The final post-tag commit
updates this validation report only; no engine source differs from the tag.

## Screenshots

All thirteen README and Cartographer screenshots were freshly captured from
the 0.7.23 preparation build and visually inspected. The capture notes in
[the image directory](img/README.md) identify development demos and sample
results rather than presenting them as live-game measurements.

## Limits

Local visual inspection covers Linux OpenGL screenshots and the software
Cartographer renderer. Interactive Windows/macOS GPU rendering, physical
high-DPI displays/device loss and a multi-hour combat soak were not repeated
locally. Native CI builds and tests portable code and packages; it cannot
establish appearance or sustained performance on every GPU. The remote time
limits are simulated time, not a nine-hour real-time endurance test.
