# 0.7.10 release validation

Local checks performed on 2026-09-28. Network protocol is 194; update clients and
servers together. Earlier releases using protocol 189 cannot join these games.

## Local checks

- All targets built in Release and optimized Debug.
- Release CTest: 94/94 passed; optimized Debug: 97/97 passed.
- Deterministic-math guard and available GCC/Clang optimization comparisons:
  golden `dcef618cd2e4d558`. ARM emulation legs skipped because target headers/
  libraries are unavailable locally; macOS CI supplies native ARM64 coverage.
- SDL UI checks: player-name typing, paste sanitization and save/reload; skirmish
  leave/recreate preserves setup and the lobby speed option is read-only;
  Unit Info hover art, modal clicks, left-only activation and keyboard dismissal.

A fresh local server run with separate host, peer, server, and late-spectator
data roots transferred a differing map copy and completed 300 matching ticks
with hash `a5a69304ffacaa64`. Temporary caches/accounts remained outside the
checkout.

Earlier checks on included changes cover map transfer and persistence, recipient
unit limits and ownership on gifting, and deterministic host/peer/spectator
state after a gift. See [map transfer](map-transfer.md),
[unit gifting](unit-gifting.md), and [performance results](performance-2026-09-28.md).
The remote-server and full live-retail sweeps were not repeated for this release.

## Packaging

Candidate `8aab757` (tagged as `v0.7.10`) passed
[Windows](https://github.com/pocketgeek/tak-engine/actions/runs/36509221615),
[macOS ARM64](https://github.com/pocketgeek/tak-engine/actions/runs/36509221469), and
[all seven Linux package builds](https://github.com/pocketgeek/tak-engine/actions/runs/36509221548).

Earlier macOS CI exposed
a packaging regression: adding FinderInfo metadata to hide `.app` invalidated
strict signature verification. The workflow now leaves extension display to
Finder and verifies the unmodified signed bundle. Native CI confirmed this fix
before tagging. Tag workflows repeat the platform checks and attach packages.

No retail game archives or additional dynamic libraries were added.

## Documentation and screenshots

README controls, setup, gifting, map sharing, player identity, package names and
protocol compatibility were checked against the current implementation. All seven
gallery images were newly captured at 1600 × 1000 and visually inspected, including
ships on water and the updated creation screen. The base/naval scenes are developer
demos and the results use sample data, as labeled. See [capture notes](img/README.md).
README local file/image links and `git diff --check` passed.
