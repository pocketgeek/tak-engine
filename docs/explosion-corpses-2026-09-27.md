# Explosion corpses (2026-09-27)

Historical first fix (protocol 188). The subsequent [native death lifecycle
fix](native-death-lifecycle-2026-09-27.md) replaces the admission heuristic
and four-second timer with live script execution, including the seven ordinary
corpse refusals described below. Current development uses protocol 189.

The 0.7.7 follow-up restores explosion-death corpses for 29 unit types whose
shipped `Killed` handlers explicitly request corpse 1. Previously the simulation
suppressed their corpses whenever the killing blow had damage type 3.

| Roster | Internal unit names |
| --- | --- |
| Aramon | araat, arassh, aratrans, aratre, arawar |
| Creon | cregatl, creiron, crenavy, crepris, crester, cresubm |
| Scenario units | npcbotl, npcrixx |
| Taros | tarcship, tarsh |
| Veruna | verasy, verat, verflag, verharp, verlight, verman, vermer, vermort, verpill, verscout, vertower, vertrans, vertre |
| Zhon | zonglyph |

A bounded script analysis resolves the explicit corpse output from constant
operations and death-type comparisons. Veruna's Sea Fort, Guard Tower and
Lighthouse have conditional effect branches, but still request the corpse.
Other runtime queries, asynchronous work and unknown instructions are rejected;
there is no unit-name whitelist or guessed query result. Corpse admission is
separate from the existing immediate-retirement rule for sixteen buildings.

This change preserves the existing death-animation handoff, ordinary-death
policy, underwater sinking/expiry, and exclusions for unfinished construction
and self-destruction. It does not resolve the separate seven ordinary-death
mismatches found by the audit or claim complete retail death-timing parity.
Protocol 188 prevents mixing this changed simulation with released 0.7.7 peers.

## Verification

- All 29 scripts passed native death dispatch and owner retirement under
  `probe_native_set31_lifecycle.py --native-death-state --death-type 3
  --corpse-request`. The native engine requested corpse 1 in every case.
  This runs isolated retail routines, not the retail GUI. Map stamping and
  unrelated rendering services remain controlled seams of that probe.
- `building_death_test` checks the 29 types in both balance modes, on land and
  water, including the existing animation handoff and water expiry. It executes
  each authored Killed handler independently to verify the analysis, checks
  corpse-less explosion controls, unfinished units and self-destruction, and
  retains the sixteen-building immediate-corpse regressions.
- `TAK_RUIN_TEST=1 TAK_GEOMETRY_VERIFY=1` checks all 45 admitted corpse models,
  including the restored 29, through body/shadow generation and reference-path
  geometry verification in both balance modes.
- Full suites passed after the change: Release 80/80; optimized Debug, Debug,
  and Clang Debug 83/83 each. All targets were rebuilt in all four configurations.
- GCC and Clang agreed on the corpse-scenario state hashes:
  `7d63a5c8d55353a3` (standard) and `695ed4d5d47743d3` (Crusades).
  The deterministic-math sweep retained `dcef618cd2e4d558`; local ARM emulation
  remained unavailable because its target headers are missing.
- Repeated seeded 1,800-tick client/server matches reproduced their hashes with
  both normal and doubled sight/radar, with no desync or referee mismatch.
