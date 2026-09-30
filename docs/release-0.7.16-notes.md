# TAK Engine 0.7.16

Generate larger maps, play against a gentler Easy AI, and keep production moving.

- Generated maps now support up to **64×64**, with terrain-themed ruins around
  every mana spot and clear lodestone yards and access routes. Existing saved
  recipes keep their original layouts.
- **Easy** builds more slowly, has smaller army/factory targets, and sends at most
  eight units per wave. Attacks begin after four game minutes and are at least
  two minutes apart. **Passive** is renamed **Defensive**; its behavior is unchanged.
- Harpies and other converters stop attacking newly allied targets. Late
  conversion projectiles cannot steal an allied unit.
- Completed factory and mobile-builder units clear the production area, including
  infinite queues and factories without a rally point.
- Results show the same faction emblems and player colors as F4, retaining
  faction identity after elimination.

Protocol **203**: update clients and servers together. Version 0.7.15 uses
protocol 201 and cannot join the same match. No new dynamic runtime dependencies
were added. Retail game data remains external and is not distributed.

See the [validation report](https://github.com/pocketgeek/tak-engine/blob/main/docs/release-0.7.16-validation.md).
