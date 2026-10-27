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

## 2026-10-26: the -N is mana requested

The bulb's two numbers, from the HUD at `0x4b2239..0x4b2324`: `PositiveM` is
`"+%d"` of `0x401330` and `NegativeM` is `"-%d"` of `0x401350`, each the sum of
the oldest 29 of the resource object's 30 per-tick samples times `30/29`
(`1.034482717514038`), plus 0.5 and truncated. `0x401270` rolls the samples
from resource `+0xc` (produced) and `+0x10` (requested). Every writer of
`+0x10` in the binary: construction (`0x429ba8`) and repair (`0x429ddd`) add
`inverseBuildTime x cost x work` -- the full-rate request, before the
allocation scales the work and before the store caps the payment -- and the
mana transfer at `0x4f61c0` adds what the giver sends. Only the economy step
(`0x51d837`) and unconjure refunds add to `+0xc`; reclaim (`0x4ea1b0`) and a
new storage building's start bonus (`0x5123e7`) credit the store without
counting as income.

So the -N is what consumers ask for, not what a short pool could pay: a
starved economy shows a larger -N than +N. Ordinary matches recorded the mana
actually paid. `Player::constructionShare` now records the full-rate cost of
construction, repair and production, the healing aura records its whole pulse,
and every other `debitMana` requests exactly what it pays. (This first landed
as a separate display-only history beside the paid one; see the AI section
below for why there is now one.)

Debug capture: `TAK_CONJURE_TEST=1 TAK_CONJURE_MANA=0 TAK_SHOT_MS=500
takclient game "Ulasem Arena" --data <install> --testbuild --time 4 --shot
out.png` shows `0/10000`, `+19`, `-26` while the Zhon monarch conjures a Hunter.

## 2026-10-26: the AI's shortfall reads the request too

The build manager's priority refresh (`0x410810`) reads the same two
averages as the bulb: at `0x410928` it calls `0x401330` (produced) and at
`0x410938` `0x401350` (requested, +0x10), and takes net = produced -
requested. With fill = stored / capacity (0 when capacity is not above 0.01,
`0x5eb790` = 0.01), it is short when net < -0.01 (`0x5ebbf0`) or when net <
0.01 and stored < 0.01. The formula was already ported and emulation-checked
(`tools/re/check_resource_history.py`); only its input was wrong: ours fed it
the mana paid, which never exceeds what a short pool holds, so a starved
economy whose store kept a little mana read as balanced.

`RetailResourceHistory` is again one history, `samples[i] = {produced,
requested}`, and `Player::requestMana` is what fills the second figure; the
paid amount is no longer recorded. Every reader of the shortfall moves with
it:

- Retail AI (lobby RETAIL, `ClassicBrain::recomputePriorities`): the direct
  port of `0x410810`.
- Our Passive, Easy, Normal and Absurdly Defensive levels
  (`Controller::manaShort`, the store-under-30% lodestone trigger ported from
  the same branch); Hard and Absurd do not use it.
- The restored retail build cache (`World::tick`, used by the replay probes
  and tests, hashed there as before).

The history is still not folded into `stateHash`: only the server's AI reads
it, from its own World, and it is built only from hashed sim arithmetic, so a
seed reproduces it exactly; clients and replays receive the AI's commands, not
its inputs. This is how the paid history was treated.
