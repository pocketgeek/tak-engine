# 0.7.17 release validation

The changes in `311602d` affect client UI input, saved setup preferences, and the
stats display. Protocol remains **203**; simulation, AI, and networking code are
unchanged from 0.7.16.

## Local checks

- Release: **125/125** CTests passed.
- Optimized Debug: **130/130** CTests passed.
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

The implementation commit passed Linux, Windows, and macOS platform CI. Release
commit and tagged-package results are recorded after those workflows finish.

## Scope

The previous release's remote network sweep was not repeated: no simulation or
network source changed. Retail was not launched. No new screenshots were needed
for this release; the README gallery remains explicitly labeled as 0.7.16.
Native Windows/macOS interactive input checks were not run locally.
