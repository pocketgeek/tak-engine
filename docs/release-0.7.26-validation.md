# 0.7.26 release validation

Protocol **237**, replay format **11**, generator version **8**, campaign
payload **4**, database schema **9**.

[Version 0.7.26](https://github.com/pocketgeek/tak-engine/releases/tag/v0.7.26)
was published on 2026-10-07 after the checks below. The tag points to
`32e9310`. Its scope is everything since [0.7.25](release-0.7.25-validation.md); see the
[release notes](release-0.7.26-notes.md).

## Scope and compatibility

Grouped flyer movement changes in both pathfinding modes. Retail ports the
original game's VTOL_Move group checks and VTOL_Move_Formation, including the
1-in-4 order drop for a flyer found ahead of its group a second time. Legion
flyers in a formation with ground members hold stations over the ground
members and keep to their pace. AI games issue no groups, so the headless AI
game hashes are unchanged. The release also fixes Creon's in-game panel art
(Iron Plague names it differently), which removed a leftover text readout
from Creon players' screens; that is interface-only.

Protocol 237 is required for live play. Replays need the exact simulation
protocol, so protocol-236 (0.7.25) recordings are not playable by this build.
Replay format 11, generator version 8, campaign payload 4 and database schema 9
are unchanged since 0.7.25 (checked in source against `v0.7.25`).

## Local preparation checks

All checks ran on a clean detached worktree of the version commit `a2842fe`
(0.7.26, protocol 237) with the owned retail install as game data.

- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and
  AddressSanitizer all-target builds completed. Every client and server reports
  `0.7.26 (build v0.7.25-6-ga2842fefbdd2)` with no `-dirty` suffix.
- **Full CTest.** Release passed **238/238**. Debug, optimized Debug and Clang
  Debug each passed **250/250**, including the new mixed-formation test. The
  intentionally disabled `legion_acceptance_crowdheld_legion` is the only skip.
  No test timed out.
- **AddressSanitizer/leak run.** The 0.7.25 selection plus the mixed-formation
  test, **82/82**, passed with leak detection and no suppressions.
- **Retail flyer port.** `tools/re/check_retail_group.py` compares the port with
  the emulated original routines; every section passes, including 2,000 runs
  each of the new flyer group checks and flyer formation return.
- **Determinism.** GCC and Clang x86-64 at O0/O2/O3 agree on
  `dcef618cd2e4d558`; aarch64 emulation legs skip for missing headers.
  `tools/check-detmath.sh` passes.
- **Headless AI games.** Inner Circle, seed 1: Legion 300 s
  `ca02108dfc933123`, Retail 300 s `b240750e5765c02b`, Legion 60 s
  `dadf535a50b404e1`, Retail 60 s `56cfcbf8ef57181e`, all unchanged.
- **Python research suite.** **165 tests** passed, one optional corpus test
  skipped; the owned-retail archive verifier passed **6/6** with archive
  checksums unchanged.
- **Syntax checks.** All 421 Python files compile; all shell scripts pass
  `bash -n`.
- **Final commit.** The Creon panel fix and documentation followed the version
  commit. The final source (`44e834b`) was rebuilt and passed the full Release
  (**238/238**) and Debug (**250/250**) suites again.

## Remote multiplayer sweep

The full 37-scenario table passed for both modes on `tak.pgnet.us` and
`vpn3.pgnet.us`, using `a2842fe` binaries (the later commits change only the
Creon interface art and documentation, not the simulation or network code).

| Mode | Scenarios | Seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | --- | --- |
| Retail | 37/37 | 52/52 | 10,169 over 22 pairs, 0 mismatched | 859 over 17 comparisons, 0 mismatched |
| Legion | 37/37 | 52/52 | 10,990 over 22 pairs, 0 mismatched | 1,829 over 34 comparisons, 0 mismatched |

- The build-identity check, planted-desync detection (tick 900) and
  override-transfer rejection passed in both modes. No natural desync appeared.
- As in 0.7.25, three dense Legion stress cases (`2h-stress`,
  `2h-orders-stress`, `w-allai-stress`; seeds 2021, 2023, 2035) exceeded the
  default 900-second real-time limit while progressing, and passed with a
  3,600-second allowance and the same seeds (about 1,010, 2,080 and 1,080
  seconds). Their referees ran at about one full core: these battles run below
  real time on the Debug test referees. Profiling them is follow-up work.
- The first Legion pass was discarded after the local temporary filesystem
  quota filled and one referee log came out empty; the whole Legion table was
  rerun with logs on the home disk and passed as shown. Retail finished before
  the quota filled.
- All eight impaired-network cases per mode archived setup, the netem rules and
  an exact game-port filter, and tore down to the interface default.
- Both hosts were confirmed clean afterwards. The public server binary and
  service were never replaced, stopped or restarted.

SHA-256: referee (Ubuntu 24.04, GCC 13.3, `-O2 -g`)
`af3c70a6af066b7868c7ddda6593a86917dc8f643ec0b463f7173f1710864718`; client
(GCC 16.2.1, `-O2 -g`)
`f3fb5ca110e17621b007c6b1dcff6f48da4dac70b51a7d0ac1a89234e6230354`; override
transfer test
`d2ea76b21abc0a5dceb34459b66413f3b9bfaed8e6af5eb850f2adb56105f0c0`; harness
(a working copy of `tools/desync-hunt-remote.sh`, the same file as for 0.7.25)
`02d27068863de212b488e19ba8dbe14e9a386b74039dd709defabaabbd6db974`.

## CI and packages

The final source (`44e834b`) passed the Linux, Windows and macOS workflows on
`main`, and the version commit `a2842fe` passed them before it. The exact tag
passed all four workflows:

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/37645691778), started manually on the tag because the workflow is path-filtered.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/37645600274): all native package jobs and release attachment.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/37645600271): x64, ARM64 and the x64 signing/package job.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/37645600281): x64 and ARM64.

The release was created as a draft with these notes and tagged CI attached the
packages. All **29 assets** were downloaded and matched GitHub's SHA-256
digests and byte sizes before publication. Every ZIP passed its CRC test.
Windows ZIP payloads are x86-64 and ARM64 PE executables; macOS ZIP payloads are
arm64 and x86-64 Mach-O apps with bundle version 0.7.26; the Ubuntu 24.04 DEB's
client and server report 0.7.26 and ship the systemd service. Windows CI
reported **17** valid timestamped signatures with no errors, and every Windows
executable checked locally, plus the installer, carries a certificate table.

Release packages report their build identity inconsistently: Linux packages
report `build unknown` (Git refuses the container checkout once the checkout
step's temporary safe-directory setting is gone), and Windows reports the tag
with a spurious `-dirty` suffix; macOS reports the tag correctly. The version
number and protocol are unaffected. This is a CI-only issue, to be fixed for the
next release.

## Screenshots

Done on 2026-10-07 with the 0.7.26 release-preparation build (`takclient
--version` 0.7.26; Debug with `-O2`; servers report protocol v237). Every image
was rendered fresh, except the nine UI views carried over from 0.7.25 (listed
in the notes), and each was inspected by eye against its caption.

- **Gallery.** New: `battle.jpg`, `dragons.jpg`, `flyers.jpg`, `hud.jpg` and
  `generated.jpg`. Re-rendered: `title.jpg` (shows VERSION 0.7.26) and
  `naval.jpg`. Removed: `army.jpg` and `gameplay.jpg`.
- **User guide.** Thirteen new images in `docs/img/guide/`, plus seven reused
  gallery images. All 20 are referenced from `user-guide.md` with alt text and
  a caption.
- **Labels.** Development scenes, debug fixtures and sample data are identified
  in the captions and in [the capture notes](img/README.md).
- **Size.** Total tracked image bytes rose from 7.06 MB to 14.7 MB.
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve.
- **Not captured.** The first-launch folder picker is the operating system's
  own dialog.

## Limits

Local visual inspection covers Linux OpenGL screenshots and the software
Cartographer renderer. Interactive Windows/macOS GPU rendering was not tested
by hand. Remote simulated-time sweeps do not substitute for a multi-hour
wall-time soak. Dense Legion stress battles run below real time on the Debug
test referees (see above).
