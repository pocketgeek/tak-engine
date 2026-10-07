# 0.7.27 release validation

Protocol **238**, replay format **11**, generator version **8**, campaign
payload **4**, database schema **9**.

[Version 0.7.27](https://github.com/pocketgeek/tak-engine/releases/tag/v0.7.27)
was published on 2026-10-07 after the checks below. The tag points to
`2847e1f`.

## Scope and compatibility

Four optimizations for very large battles. Three Legion bookkeeping changes
(live-field totals, a member index per formation point, recounting only changed
soft-obstacle cells) and Retail's per-unit path-pending flag are
behavior-identical. Two further Legion changes alter Legion games: held units
rest between updates, and far-apart static-map changes are repaired one cluster
at a time. The Legion navigation golden was regenerated for them; the Retail
golden is unchanged. Tactical Dots and the Renderer option are client-only
display changes with no simulation, network or hash effect. The Linux and
Windows CI workflows were changed so packages report their build identity
(confirmed in CI; see below). Options greys out Tactical Dots Zoom while Tactical
Dots is off.

Protocol 238 is required for live play. Replays need the exact simulation
protocol, so protocol-237 (0.7.26) recordings are not playable by this build.
Replay format 11, generator version 8, campaign payload 4 and database schema 9
are unchanged since 0.7.26 (checked in source against `v0.7.26`).

## Local preparation checks

All checks ran on a clean detached worktree of the version commit `9642e70`
(0.7.27, protocol 238) with the owned retail install as game data.

- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and
  AddressSanitizer all-target builds completed. Every client and server reports
  `0.7.27 (build v0.7.26-19-g9642e70e009b)` with no `-dirty` suffix.
- **Full CTest.** Release passed **239/239**. Debug, optimized Debug and Clang
  Debug each passed **252/252**, including the new Tactical Dots and renderer
  driver tests. The intentionally disabled `legion_acceptance_crowdheld_legion`
  is the only skip. No test timed out.
- **AddressSanitizer/leak run.** The 0.7.26 selection plus `tactical_dots` and
  `renderdriver`, **84/84**, passed with leak detection and no suppressions.
- **Retail group port.** `tools/re/check_retail_group.py` passes every section
  against the emulated original routines, with the same counts as 0.7.26.
- **Determinism.** GCC and Clang x86-64 at O0/O2/O3 agree on
  `dcef618cd2e4d558`; aarch64 emulation legs skip for missing headers.
  `tools/check-detmath.sh` passes.
- **Headless AI games.** Inner Circle, seed 1, each reproduced on two runs:
  Legion 300 s `ca02108dfc933123`, Retail 300 s `b240750e5765c02b`, Legion 60 s
  `dadf535a50b404e1`, Retail 60 s `56cfcbf8ef57181e`. Protocol 238's Legion
  changes do not affect this two-player game.
- **Python research suite.** **165 tests** passed, one optional corpus test
  skipped; the owned-retail archive verifier passed **6/6** with archive
  checksums unchanged.
- **Syntax checks.** All 421 Python files compile; all shell scripts pass
  `bash -n`.
- **Final commit.** The Tactical Dots Zoom greying and documentation followed the
  version commit. The final source (`cf98fc9`) was rebuilt and passed the full
  Release (**239/239**) and optimized Debug (**252/252**) suites again.

## Remote multiplayer sweep

The full 37-scenario table passed for both modes on `tak.pgnet.us` and
`vpn3.pgnet.us`, using `9642e70` binaries (the later commits change only the
Options screen and documentation, not the simulation or network code).

| Mode | Scenarios | Seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | --- | --- |
| Retail | 37/37 | 52/52 | 9,628 over 22 pairs, 0 mismatched | 1,081 over 23 comparisons, 0 mismatched |
| Legion | 37/37 | 52/52 | 10,201 over 22 pairs, 0 mismatched | 1,533 over 29 comparisons, 0 mismatched |

- The build-identity check, planted-desync detection (tick 900) and
  override-transfer rejection passed in both modes. No natural desync or
  referee-suspect report appeared. Every replay header shows the intended mode,
  protocol 238 and 0.7.27.
- The three dense Legion stress cases (`2h-stress`, `2h-orders-stress`,
  `w-allai-stress`; seeds 2021, 2023, 2035) again exceeded the default 900-second
  real-time limit while progressing, and passed with a 3,600-second allowance and
  the same seeds in about 930, 930 and 945 seconds (0.7.26: about 1,010, 2,080
  and 1,080). Protocol 238 plays different games from the same seeds, so only the
  all-AI case is a like-for-like comparison, at about 12% faster. The harness runs
  these games at 4x speed; the referees were limited by one CPU core.
- All eight impaired-network cases per mode archived setup, the netem rules and
  an exact game-port filter, and tore down to the interface default. Two leftover
  netem watchdogs whose claim files were already gone were stopped by PID.
- Both hosts were confirmed clean afterwards. The public server binary and
  service were never replaced, stopped or restarted.

SHA-256: referee (Ubuntu 24.04, GCC 13.3, `-O2 -g`)
`73cc48455c8be9ccdcabe5c2ab8edc165f1c9bae34680cfbd0c899ecb3178a01`; client
(GCC 16.2.1, `-O2 -g`)
`486d106c8b3b9f21530fdd2512199fe0a0a1e57974444c1263f42ef93a113a95`; override
transfer test
`e3f9a3e1085768511bc083e3f398a73011c104f1fb3f70d454f624bc77a12384`; harness
(a working copy of `tools/desync-hunt-remote.sh`, unchanged since 0.7.25)
`02d27068863de212b488e19ba8dbe14e9a386b74039dd709defabaabbd6db974`.

## CI and packages

The final source passed the Linux, Windows and macOS workflows on `main`. The
build-identity fix was confirmed first on the version commit `9642e70`: all 16
[Linux package jobs](https://github.com/pocketgeek/tak-engine/actions/runs/37688998787)
and [both Windows builds](https://github.com/pocketgeek/tak-engine/actions/runs/37688998842)
reported the clean id `9642e70e009b`. The exact tag passed all four workflows:

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/37696476743), started manually on the tag because the workflow is path-filtered.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/37696406405): all native package jobs and release attachment.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/37696406434): x64, ARM64 and the x64 signing/package job.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/37696406468): x64 and ARM64.

The release was created as a draft with these notes and tagged CI attached the
packages. All **29 assets** were downloaded and matched GitHub's SHA-256
digests and byte sizes before publication. Every ZIP passed its CRC test.
Windows ZIP payloads are x86-64 and ARM64 PE executables; macOS ZIP payloads are
arm64 and x86-64 Mach-O apps with bundle version 0.7.27. **Build identity is now
correct on every platform:** the Ubuntu 24.04 DEB's client and server report
`0.7.27 (build v0.7.27)`, and the Windows and macOS server binaries carry the
`v0.7.27` id with no `-dirty` suffix. Windows CI reported **17** valid
timestamped signatures with no errors, and every Windows executable checked
locally, plus the installer, carries a certificate table.

## Screenshots

Done on 2026-10-07 with the 0.7.27 release-preparation build (`takclient
--version` 0.7.27; Debug with `-O2`, OpenGL; servers report protocol v238) and a
capture-only patch that is not committed. Each new or re-rendered image was
inspected by eye against its caption.

- **Gallery.** Re-rendered: `title.jpg` (shows VERSION 0.7.27) and `hud.jpg`
  (the stats panel now ends with the new RENDERER row, showing GL). The other
  gallery images are unchanged from 0.7.26; none shows the stats panel, the
  Options screen or Tactical Dots.
- **User guide.** New: `guide/tactical-dots.jpg`, the same moment of a
  deterministic Benchmark battle rendered with Tactical Dots off and on, side
  by side, inserted in the Tactical Dots text with alt text and a caption.
  Re-rendered: `guide/options-graphics.jpg`, showing Renderer (AUTO) first in
  Graphics, Tactical Dots (OFF) and Tactical Dots Zoom (20%, greyed out
  because Tactical Dots is off), all at their defaults in a fresh profile. `guide/hud-annotated.jpg` was recaptured for
  checking but kept: at 1600 × 900 the stats panel shows only its first four
  rows, so the RENDERER row is not visible there.
- **Labels.** The development and Benchmark scenes are identified in the
  captions and in [the capture notes](img/README.md).
- **Size.** Total tracked image bytes rose from 14.71 MB to 15.49 MB (+0.78 MB, almost all the new Tactical Dots picture).
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve.
- **Not captured.** The first-launch folder picker is the operating system's
  own dialog. The Renderer fallback notice and the non-OpenGL backends were not
  photographed.

## Limits

Local visual inspection covers Linux OpenGL screenshots and the software
Cartographer renderer. The Renderer option's non-OpenGL backends were exercised
on Linux only (OpenGL ES 2, OpenGL ES, Software); Direct3D and Metal were not
tested by hand. Remote simulated-time sweeps do not substitute for a multi-hour
wall-time soak. Dense Legion stress battles still run below the harness's 4x
speed on the Debug test referees (see above).
