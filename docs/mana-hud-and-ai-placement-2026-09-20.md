# Mana HUD and AI placement

The HUD's expenditure estimate walked only mobile `buildSiteId` and `repairId`
references. Factory production uses `productionSiteId`, so an actively producing
factory could display zero spending. The estimate also used nominal build-time
arithmetic instead of the simulation's actual transactions.

Players now maintain a display-only resource history. Ordinary matches record
`creditMana`/`debitMana`; restored retail resource objects supply their existing
produced/requested counters. The HUD captures these into the immutable render
snapshot and averages the oldest 29 samples at retail's per-second scale.
The counters are not consulted by gameplay and are excluded from `stateHash`.

The bulb uses the applicable simulation storage cap, including retail capacities
below 100, and truncates its frame index instead of rounding upward. The numeric
stored-mana readout and bulb fraction remain based on current stored mana;
retail additionally smooths the pool through its stored-mana history. The income
and expenditure window matches retail, but this change does not claim full HUD
timing parity.

Retail observations: the HUD at `0x4b223f`/`0x4b2256` calls resource rate functions
`0x401350`/`0x401330`. The bulb selection at `0x4b221e` multiplies by frame count
minus one and truncates. A scratch emulation comparison passed 512 rate and
steady-pool frame-selection cases (`/tmp/tak-check-mana-retail.py`).

AI structure placement now reserves each mana spot for the largest same-faction
lodestone footprint, including upgrades. It checks cell overlap, the lodestone's
unit-center exclusion, and the working space the AI itself requires around
factories. Mobile units and lodestones retain their existing placement policy;
player building rules and pathfinding are unchanged.

Validation:

- Placement fixtures cover all five factions, both balances, square and
  rectangular structures, factory clearance, and deposits at/near the original
  candidate. After the AI chooses and spawns its building, actual `canPlace`
  checks still allow each faction's lodestones and upgrades on the deposit.
- Production regression confirms factory payments reach the HUD history and
  changing only that history leaves the lockstep hash unchanged.
- A controlled client factory fixture at tick 121 reported 587.3066 stored of
  1000, income 10/sec and expenditure 20.8/sec. The captured HUD showed
  `587/1000`, `+10`, `-21`, rather than zero spending. This uses a synthetic
  production queue and income for a stable display check, not a balance test.
- All 38 CTest tests passed in each of Release, Debug and optimized Debug
  (114 passes). All targets, including clients and servers, were rebuilt.
- Determinism golden remains `8adc4762a852fadd`; native compiler legs passed,
  ARM toolchain legs were unavailable. Two local optimized-debug Crusades
  multiplayer runs reached tick 600 with hash `b879f25ebe4532bf` and no errors.
- No remote binaries were deployed.
