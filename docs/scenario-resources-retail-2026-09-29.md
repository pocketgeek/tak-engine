# CRT resource actions: native observations, 2026-09-29

`tools/re/check_scenario_resources.py` executes the installed retail dispatcher
at `0x4cad50` with controlled player/resource records. Its 25 cases cover all five
resource actions, signed values and values beyond exact float integer precision.
No retail executable bytes or assets are included.

| Action | Native behavior |
| --- | --- |
| 16, resource limit | Write the integer operand as a float to resource override `+0x14`. |
| 17, set resources | Replace the stored pool at `+0x00`, rounded to float. |
| 18, add resources | Add to the float pool and double lifetime-produced total at `+0x18`. |
| 19, subtract resources | Subtract from the float pool, without immediately clamping negative values. |
| 20, resources normal | Clear the override; leave the pool and counters alone. |

The corresponding dispatcher branches are `0x4cb593` through `0x4cb68a`.
The override is not a one-time assignment to capacity. The normal economy
recomputes capacity from units, but a **positive override suppresses natural
income** (`0x51d837`) and replaces capacity at tick-end (`0x401270`). Zero or a
negative override restores ordinary income/storage behavior. Scripted resource
changes still work while natural income is disabled.

The existing native economy comparison (`tools/re/check_construction_economy.py`)
covers income suppression and tick-end clamping independently. The engine now
persists the scenario override, hashes it, applies it to its ordinary economy and
restored retail resource objects, and exposes the effective cap to the HUD.
Resource reset restores live-unit income/storage on the following tick. Script
pool writes follow native float rounding and do not invent per-second income or
usage samples; the add action updates lifetime-produced accounting when the
native resource object is present.

The engine retains its chosen allied mana-sharing policy. The ordinary skirmish
minimum capacity and economy remain unchanged when no scenario override is used.
`scenario_resources_test` checks these actions through startup/timed rules,
client/referee parity, low caps in the HUD, reset, negative pools, float rounding
and the native resource-object path.

The same probe also confirms action 12 writes the local UI clock-enable byte
(`0x4cb26a`–`0x4cb272`). The engine now carries that cosmetic request through its
render snapshot and displays game time even when the optional statistics panel
is closed. It does not change simulation state or force the F4 scoreboard open.
