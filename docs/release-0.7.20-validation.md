# 0.7.20 release validation

Protocol **212**, campaign payload **4**, database schema **9**.

## Gameplay validation

The release contains the naval corrections tested at `62f6aef`; release
preparation changes version metadata and documentation only.

- Full GCC Release suite: **156/156**. Optimized GCC Debug: **164/164**, including
  the corrected campaign-history replay fixture rerun.
- Focused GCC Debug and Clang Debug suites: **9/9 each**. Clang ASAN/LSAN: **6/6**.
  The final expanded naval fixture passed **146 checks** in all five builds.
- Factory placement: **108 ship completions**, including consecutive production
  without a rally point, both balances and native birth-heading samples.
- **150** ordinary 20-minute AI openings across five factions, five difficulties,
  both balances and three naval maps; no Defensive attack orders.
- GCC/Clang O0/O2/O3 determinism agrees on `dcef618cd2e4d558`.
  Optional local ARM cross-checks lacked target headers.
- Both balance modes passed seated client/referee checks with seven AIs on
  **tak.pgnet.us** and **vpn3.pgnet.us**, with no desyncs. Standard concluded at
  tick 19603 (`48567e467b3533bf`); Crusades reached tick 27000
  (`5f528b4dfbe4fc79`). Isolated servers were stopped, network shaping was absent,
  and public server processes were unchanged.

The [naval audit](naval-ai-2026-10-01.md) records faction coverage and limitations.
Tests do not imply every opening chooses boats or that AI transport invasions
are implemented. Retail was not launched during this audit.

## Release verification

All local Release, optimized GCC Debug, GCC Debug and Clang Debug targets were
rebuilt as **0.7.20**. The versioned Release suite passed **156/156** again.
Client and server both report build `v0.7.20`.

Tag `v0.7.20` points to `00245d39ffb012a53fce3e898efaf2bcf32c85f7`.
All tagged workflows passed:
[Linux](https://github.com/pocketgeek/tak-engine/actions/runs/36899094767),
[Windows](https://github.com/pocketgeek/tak-engine/actions/runs/36899094497), and
[macOS](https://github.com/pocketgeek/tak-engine/actions/runs/36899094633).
The naval source revision also passed
[determinism CI](https://github.com/pocketgeek/tak-engine/actions/runs/36897034531).
Duplicate main-branch package jobs for the release commit were cancelled in
favour of the identical tagged builds.

All **19** expected assets are published in the non-draft, non-prerelease
[0.7.20 release](https://github.com/pocketgeek/tak-engine/releases/tag/v0.7.20).
All downloaded files match GitHub's SHA-256 digests. Every Windows/macOS Release
and Debug ZIP passed archive integrity, executable inventory, license,
architecture and dependency checks. Windows client/editor use the GUI subsystem;
macOS bundles declare 0.7.20, macOS 14 and their included icons. Native CI checks
signatures and DMG integrity. Debug macOS ZIPs contain standalone executables.
The Ubuntu 24.04 DEB has the correct version, client/editor launchers and all four
binaries; extracted client/server report 0.7.20 and the binaries link only system
libraries. No new dynamic dependencies or retail data are shipped.

Interactive Windows/macOS gameplay was not tested locally; native CI covers
startup and platform regressions.

README release and compatibility guidance are updated. Existing screenshots
remain explicitly identified as 0.7.19 captures; this release changes AI and
production behavior without changing the pictured interface.
