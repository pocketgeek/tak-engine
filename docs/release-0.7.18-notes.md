# TAK Engine 0.7.18

- Add an opt-in Darien Crusades multiplayer campaign service: authenticated
  enrollment/allegiance, strategic territory browsing, opponent matching, bound
  two-player tactical battles, authoritative results, territory history and
  retained replay downloads/playback. Persistent SQLite storage, audited offline
  administration, backup/restore checks and network resource limits support it.
- Correct Crusades balance loading and retail-derived damage, wind, water and
  build-menu handling. The reconstruction and balance audits document evidence
  and distinguish modern service choices from recovered retail behavior.
- Improve naval AI coastal planning, reserve a first viable shipyard and check
  each ship's actual scripted launch footprint. Veruna produces boats on Varro
  Passage at all five difficulties with either balance mode.
- Make capture attackers reject uncapturable targets during explicit orders,
  acquisition and ongoing attacks; preserve subsequent queued orders.
- Open victory results automatically after three real seconds, matching defeat
  handling in skirmish/multiplayer. Campaign progression keeps its existing flow.
- Start each skirmish setup with Spectate off while preserving other preferences.
- Recover supersampled rendering after window resize/target resets, normalize
  backbuffer state and fall back to direct rendering if the AA target cannot bind.

Protocol **211** requires matching updated clients and servers. Campaign payload
version is **4**; the campaign database schema is **9**. SQLite is statically
linked: no new dynamic dependency needs shipping. Retail data remains external.

Darien Crusades is disabled by default. Operators enable it with authenticated
accounts, `--crusades-db` and an authored campaign definition/map assignments.
Tactical duels and verified history work; missing original territory-capture,
rank and campaign-victory formulas are not guessed. Tactical wins therefore do
not change ownership under the default historical policy. See the
[setup and operations guide](https://github.com/pocketgeek/tak-engine/blob/main/docs/research/darien-crusades/campaign-operations.md).

See the [validation report](https://github.com/pocketgeek/tak-engine/blob/main/docs/release-0.7.18-validation.md).
