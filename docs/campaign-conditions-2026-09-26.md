# Campaign condition audit — 2026-09-26

The mission runner now follows the retail condition manager rather than inferred RTS victory rules. This work changes campaign objectives, not movement/pathfinding. No retail GUI was launched; comparisons execute routines from the locally installed, ignored `assets/game/KINGDOMS.icd` under Unicorn.

## Native evidence and resulting behavior

The parser at `5225f0` constructs these conditions. Numeric zero disables boolean rules; timers require positive values; enemy axis rules accept zero and reject negative values. `ANYTYPE` is the explicit wildcard for radius and typed axis rules. Campaign mode 1 skips the DestroyAllUnits fallback (`522f48`); an empty victory list waits for script outcome. An empty defeat list receives AllUnitsKilled (`522f78`). The mode is independently established by command-line Mission loading (`4a5659`) and the campaign briefing branch (`4b34be`).

The manager at `5230b0` requires **every victory condition**; `523120` accepts **any defeat condition**. Native virtual dispatch was executed for all 16 combinations of four completion bits in both lists. The ordinary outcome caller at `4f6e30` checks victory before defeat. A separate player-state branch at `4f6e9f` checks defeat first; this port retains victory-first campaign resolution and does not claim to reproduce that multiplayer player-state branch.

| Condition | Native routine | Implemented semantics |
| --- | --- | --- |
| KillEnemyCommander | `523670` | Latches on the enemy commander death event, even if another commander survives. |
| DestroyAllUnits | `523770` | Polls whether original enemy player 2 has any living units. |
| KillAllMobileUnits | `523870` | Enemy mobile death latches when no enemy mobiles remain. |
| BuildUnitType | `523a10` | Latches when an owned matching unit has finished construction. |
| CaptureUnitType | `523b20` | Matching enemy ownership-transfer event latches before ownership changes. |
| KillAllOfType | `523c00` | Matching enemy death latches when none of that enemy type remain. |
| KillUnitType | `523d60` | Counts matching enemy deaths; a nonpositive initial quota does not auto-complete. |
| MoveUnitToRadius | `523e70`, `523eb0`, `509ac0` | Ready human unit of matching type/wildcard enters inclusive fixed-point radius. Authored cell coordinates have no added half-cell offset. |
| UnitTypePassesX/Z | `524000`, `524100`, `524180` | Latches within two cells of the authored line; no inferred initial side or crossing direction. |
| VictoryTimerRunsOut / DeathTimerRunsOut | `524270` | Unsigned tick deadline, seconds multiplied by 30. |
| CommanderKilled | `524310` | Human commander death event latches. |
| AllUnitsKilled | `524450` | No ready owned unit remains; buildings count. |
| UnitTypeKilled | `524540` | Counts matching deaths regardless of owner, including zero/negative starting quotas. |
| AllUnitsKilledOfType | `524660` | Matching death latches when neither original human nor original enemy has a surviving match. |
| AnyUnitPassesX/Z | `5247e0`, `5248a0`, `5248e0` | Enemy unit enters inclusive two-cell axis tolerance. |

Death dispatch at `523510` and capture dispatch use event latches rather than checking a current unit count and guessing whether a type previously existed. Latches and remaining quotas are included in the deterministic mission hash, together with the mission clock. A full-roster startup sweep exposed the previously invented fallback victory in takx13_mt (Player1 and Player9 with a scripted exit trigger). Correcting the native mode guard removes that immediate win without changing player mapping. Human/enemy references are mapped through the OTA-to-world slot mapping; an allied or neutral third player is not treated as the original enemy.

Axis coordinates are footprint origins, not center cells: native spawn at `511b68..511b9f` writes `(position - footprint*8 + 8) >> 20` into unit `+74/+76`. The port uses its existing `footprintOrigin`, leaving pathfinding untouched.

Commander identity is the owner's authored `sidedata.tdf` commander name, not the FBI commander flag. This matters for shipped `ZONHURT`, which carries that flag but is not the Zhon side's `ZONHUNT` commander. Mission setup resolves the commander per compact player slot and includes that immutable mapping in the mission hash. Missing authored commander metadata does not invent a substitute.

## Validation

`tools/re/check_campaign_conditions.py` compares the production predicate/event helper against 7,168 original-code cases: 2,048 axis cases, 1,024 complete spatial-radius calls, and 4,096 death-event cases spanning ownership, type matches, commander matches, remaining quotas, existing latches and survivor scans. Another 32 cases execute the native AND/OR manager, three verify empty victory/defeat lists, and two execute the parser fallback tail in modes 0 and 1. All **7,205 native cases pass**.

`tools/campaign_conditions_test.cpp` exercises real MissionScript parsing/dispatch against synthetic unit data and runs without proprietary assets. It covers event-based monarch loss, combined objectives, exact timer deadlines, axis/radius boundaries, disabled rules, building survival, capture and construction readiness, zero enemy axis coordinates, empty scripted objectives and same-owner Capture no-ops. The existing shipped escort regression now moves NPCDERN into the native tolerance instead of assuming that crossing far beyond the line suffices.

Run:

```
build-o2/campaign_conditions_test
python3 tools/re/check_campaign_conditions.py --binary build-o2/campaign_conditions_test
```

## Limits

The native radius/AllUnitsKilled readiness filter additionally checks native authority-ready bit `0x20`, ownership-transfer countdown `unit+104`, and attachment-parent flags. The port maps its available live/completed/unembarked state; it has no equivalent peer ownership-transfer countdown. `unit+104` is set to 150 during network ownership transfer (`514e26`) and decremented at `51deb3`; it is not a paralysis timer. No speculative paralysis substitute was added.

This is a condition and event audit, not a claim that every campaign has been played to completion or that every MAP_COMMAND script action is exact. Campaign audio and presentation have separate reports. Protocol 183 gates these changed authoritative campaign outcomes; the [integration validation](campaign-design.md#integration-validation) covers the full rebuild and regression sweep.
