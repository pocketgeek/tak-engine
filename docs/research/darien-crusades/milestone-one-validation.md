# Milestone 1 validation

Validated 2026-09-30 against the working tree committed with this report.
No original game, historical service, or production campaign service was launched.
Original assets remain outside Git. The native checks execute isolated routines
from the fingerprinted retail binary.

## Builds and regression tests

- Full `cmake --build build` and `cmake --build build-dbg` succeeded after the
  production corrections. Both client and server were rebuilt; development
  protocol is 204. No release tag was cut.
- Release CTest: **127/127 passed**, including the new synthetic
  `crusades_damage` test, cursor, combat, movement, transport, campaign, rendering
  and existing simulation regressions. Full run: 29.08 seconds.
- Final expanded `crusades_registry_test` checks: **126,413 passed** on the
  original Iron Plague CD view, **126,413 passed** on the GOG view and
  **93,337 passed** on the official 3.0 view. Scope counts and individual field
  classifications are in the [balance reference](../../crusades-balance-reference.md).
- Python research discovery: **151 tests passed**.
- `tools/check-determinism.sh`: available GCC/Clang optimization variants agree
  on detmath golden hash `dcef618cd2e4d558`; ARM64 legs were skipped because the
  required target build dependencies were unavailable. This is the deterministic
  math check, not a claim of running the entire game on every platform.
- Official native damage probe: actual CRT initializer selects 53-bit precision;
  **14 arithmetic and four category-lookup cases passed**.
- Native wind probe: **16 flag pairs and 96 callback/heading cases passed**.
- Existing GUI parser: **20/20 original inputs passed**, all input hashes
  rechecked, **374 gadgets**. Research wrapper compiled with
  `-Wall -Wextra -Werror`.

## Client/server smoke checks

Four independent loopback runs used rebuilt Debug client/server, seed 1,
`Frey River Plain`, no overrides, one server-run AI and 4× speed. Two runs used
standard balance and two used Crusades Balance. Every run reached tick 1800
with four living units, `err=none`, and a normal time-limit exit.

All four final hashes were `0e0f5c8f6d8c82fd`; each same-mode pair therefore
reproduced exactly. This is a short networking/determinism smoke check, not a
long combat benchmark or proof that both balance modes must generally have
identical state. The source/registry and native damage tests establish the
balance differences separately.

## Research artifact integrity

All nine checked-in source-difference, archive/member-manifest and effective-
value reports were regenerated from their three documented source roots and
compared byte for byte. Original-CD and GOG source/effective differences agree.
The coverage table accounts for all **177 unique changed field paths** and
exact per-scope occurrence counts; none is marked missing coverage.

Every inventory JSON parses. All local Markdown link targets were checked,
research/source claims reconciled, and `git diff --check` passed. Asset parse
results retain real qualifications (including OTA text round-trip differences)
instead of relabeling them as failures or hiding them.

The original servers' calculation/validation/persistence rules, graph, complete
map assignment, historical update order and documented source discrepancies
remain explicit unknowns. Their boundaries are in the
[acceptance audit](milestone-one-status.md); no guessed production behavior was
introduced to fill them.
