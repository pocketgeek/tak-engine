# 0.7.17 release validation

The changes in `311602d` affect client UI input, saved setup preferences, and the
stats display. Protocol remains **203**; simulation, AI, and networking code are
unchanged from 0.7.16.

## Local checks

- Release: **125/125** CTests passed.
- Optimized Debug: **130/130** CTests passed.
- GCC Debug: **130/130**; Clang Debug: **130/130**; Clang ASAN/LSAN:
  **126/126**, with leak detection enabled and no suppressions.
- Total: **641/641** across five configurations. All five local configurations
  were rebuilt with the release version and tag build ID.
- Separate focused runs: **11/11** UI, settings, campaign, audio-device and streaming
  checks passed in each of Release and optimized Debug.
- A temporary integration harness sent actual SDL mouse events through GameView
  and Options, connected to a real local server, and checked forward/backward
  wrapping for AI difficulty, faction, team, available colors and slot type.
  It also checked setup selectors, options toggles, and that right-click cannot
  activate ordinary action buttons.
- The harness saved setup preferences to an isolated settings file, reloaded
  them, restored the create screen, and checked that Options defaults preserve
  the saved setup.

## Platform checks

The implementation and release commits passed Linux, Windows, and macOS CI
before tagging. Tag **v0.7.17** points to `6e95708e78cc1e69a827bedc665a035843b3337a`.
All tagged workflows passed:

- [Linux packages](https://github.com/pocketgeek/tak-engine/actions/runs/36729518148)
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/36729518096)
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/36729518132)

All **14** expected assets are published in the non-draft, non-prerelease
[0.7.17 release](https://github.com/pocketgeek/tak-engine/releases/tag/v0.7.17).
Downloaded Windows and macOS ZIPs passed archive-integrity checks; their binary
inventories and the macOS app versions/icons were checked. The Ubuntu 24.04 DEB
contains the client, server and Cartographer, with version 0.7.17 metadata; its
extracted client/server run and report version 0.7.17. These packaged Linux
binaries report the optional build ID as `unknown`; the tagged workflow supplies
source provenance. All three downloaded files match GitHub's SHA-256 digests.

## Scope

The previous release's remote network sweep was not repeated: no simulation or
network source changed. Retail was not launched. No new screenshots were needed
for this release; the README gallery remains explicitly labeled as 0.7.16.
Native Windows/macOS interactive input checks were not run locally.
