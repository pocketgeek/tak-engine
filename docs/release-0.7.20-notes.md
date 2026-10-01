# TAK Engine 0.7.20

- Fix Creon shipyards queuing ships without starting: authored water production
  pads now respect open yards while still rejecting obstructions.
- Improve coastal production planning for Aramon and Zhon, shipyard placement
  and launch routes for Veruna and Creon, and funding for the first naval force.
- Let small fleets deploy without waiting for a large land-army wave. Defensive
  AI continues building and clears harbour exits without launching attacks.
- Avoid repeatedly building zero-income sacred lodestones on maps without mana
  deposits.
- Validate all five factions in Standard and Crusades balance. Taros uses
  amphibious units and flyers, not skirmish ships. AI transport loading and
  invasion planning are not added by this release.

Protocol is now **212**; update clients and servers together. Earlier tactical
recordings require their compatible engine. Campaign payload **4** and database
schema **9** are unchanged. No new dependencies or retail data are bundled.
Live movement controllers and pathfinding are unchanged.

See the [naval AI audit](https://github.com/pocketgeek/tak-engine/blob/main/docs/naval-ai-2026-10-01.md)
and [validation report](https://github.com/pocketgeek/tak-engine/blob/main/docs/release-0.7.20-validation.md).
