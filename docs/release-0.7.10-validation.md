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

Earlier checks on included changes cover map transfer and persistence, recipient
unit limits and ownership on gifting, and deterministic host/peer/spectator
state after a gift. See [map transfer](map-transfer.md),
[unit gifting](unit-gifting.md), and [performance results](performance-2026-09-28.md).
The remote-server and full live-retail sweeps were not repeated for this release.

## Packaging

The pre-release Windows and seven-distribution Linux jobs passed. macOS exposed
a packaging regression: adding FinderInfo metadata to hide `.app` invalidated
strict signature verification. The workflow now leaves extension display to
Finder and verifies the unmodified signed bundle. Native CI verification is
required before the release tag.

No retail game archives or additional dynamic libraries were added.
