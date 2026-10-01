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

Tagged platform builds and published-package verification are pending.
README release and compatibility guidance are updated. Existing screenshots
remain explicitly identified as 0.7.19 captures; this release changes AI and
production behavior without changing the pictured interface.
