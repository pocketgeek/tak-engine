# TAK Engine 0.7.14

Cartographer-authored scenarios now follow more of retail's trigger and placement
behavior, with shared client/server execution and clearer editor validation.

- Enforce authored Use Only construction lists; support neutral placements,
  armor/weapon and veteran defaults, and independent player victory/defeat.
- Correct All Players rule ownership and once-per-game-second evaluation.
- Implement persistent resource limits, income suppression and resource reset;
  preserve native rounding for scripted resource changes.
- Correct scripted creation, destruction, ownership transfer, absolute HP
  heal/damage, movement destinations and wildcard selection. Unfinished building
  destruction no longer leaves a site that can resume construction.
- Match region counting, strict most/least conditions, flag aliases, integer
  timers and the shared deterministic random stream.
- Position authored units using footprint origins in both editor and game.
  Show authored display names with retail's 31-byte cap. Preserve the unused
  vertical field without treating it as an altitude override.
- Wire Display gameclock to the HUD and clarify editor validation messages.
- Refresh the README and all eleven game/editor gallery screenshots.

Protocol **200**: update clients and servers together. Versions using protocols
198 or 199 cannot join the same match. No new dynamic runtime dependencies were
added. Retail game data remains external and is not distributed.

Full retail mission-runtime parity is not claimed. Scripted transfers keep stable
engine unit IDs, authored angles remain an extension, and the engine retains its
chosen allied mana-sharing policy.

See the [validation report](https://github.com/pocketgeek/tak-engine/blob/main/docs/release-0.7.14-validation.md)
and [scenario reference](https://github.com/pocketgeek/tak-engine/blob/main/docs/crt-triggers.md).
