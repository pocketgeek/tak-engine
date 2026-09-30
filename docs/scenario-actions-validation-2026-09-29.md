# Authored scenario actions and placement validation — 2026-09-29

Scope: Cartographer-authored trigger resources/reset, ownership, create/destroy,
heal/damage, movement, wildcard selection, display names and vertical placement.
This is current checkout protocol **200**, not a new release. Rebuild clients
and servers together.

## Evidence by requirement

| Requirement | Implementation and checks |
| --- | --- |
| Resource limit/reset | Persistent hashed override, income suppression, capacity/HUD, reset and native pool rounding; `scenario_resources_test`; 25 native dispatcher cases plus clock flag; existing 8192-case native construction-economy comparison. |
| Ownership/create/destroy | Dedicated scenario APIs preserve HP, update ownership/counts/orders, obey player unit caps, handle unfinished sites and native primary-grid creation placement; `scenario_actions_test`; native action probe. |
| Heal/damage and movement | Absolute HP, native word wrapping, unfinished-site progress, native midpoint with ordinary order/pathfinding submission; action regressions and native receiver/dispatcher checks. |
| Wildcards and conditions | Exact region lookup, footprint-origin bounds, strict most/least, first-inserted death counters, first-byte flags, integer timers and shared CRT random stream; `scenario_selectors_test`; 75 native comparisons. |
| Display names | Native 31-byte cap, cosmetic HUD labels, editor duplicate/truncation rules; placement and validation tests; five native rename cases. |
| Vertical placement | Native conversion never reads the serialized Y field; preserve it in editor round trips without inventing altitude; 24 native conversions with memory-read monitoring and placement/hash tests. |
| Footprint placement | Matching editor/runtime footprint-origin conversion, save/reload and dragging; placement tests and native conversion checks. |
| Determinism and ordinary games | Full regression suites, shared client/referee scenario hashes, unchanged GCC/Clang O0/O2/O3 golden `dcef618cd2e4d558`; no pathfinding algorithm changes. |

Native probe totals: actions **99**, selectors **75**, placement **29**, resources
**26** including the clock flag: **229** observations. All passed. These probes
execute installed retail routines in Unicorn; no retail GUI was launched and no
retail executable bytes or assets are distributed.

All targets rebuilt in Release, optimized Debug and AddressSanitizer directories.
Release CTest: **123/123 passed**. Optimized Debug CTest: **127/127 passed**.
AddressSanitizer/LeakSanitizer CTest: **123/123 passed** with
`detect_leaks=1:allow_addr2line=1`, SDL dummy video/audio and
`SDL_SHUTDOWN_DBUS_ON_QUIT=1`; no leak suppressions. Total: **373** passing
CTest cases across the three builds. The unrestricted rerun completed the leak
checks that the temporary restricted environment had prevented.

The Release solo-authored network fixture passed **330 ticks** with host,
headless referee and late observer agreeing on hash `6d9f4eb1ff38520c`.
It verifies names, neutral placement, stats/restrictions, scripted creation,
absolute damage, movement, resource suppression/reset and victory. Verified map
caches were written independently for host, server and late observer.

The companion-transfer network fixture also passed **300 matching ticks**, hash
`ac6884d589d58b1`, with host, peer, server and late spectator independently
verifying and caching authored map companions. Directed chat recipient/privacy
checks passed. The Release `--play-map` smoke test launched its local server,
opened a private lobby, verified the snapshot and created the opt-in trigger log.

## Deliberate limits

Ownership keeps stable engine unit IDs rather than reproducing retail allocation
identity. Authored angles remain an engine extension. The engine retains its
chosen allied mana-sharing policy. Native actions use ordinary engine mission
orders/pathfinding; this work does not claim complete native mission-allocation,
network-object or every surrounding campaign subsystem parity.

Details: [actions](scenario-actions-retail-2026-09-29.md),
[selectors](scenario-selectors-retail-2026-09-29.md),
[resources](scenario-resources-retail-2026-09-29.md),
[placement](scenario-placement-retail-2026-09-29.md).
