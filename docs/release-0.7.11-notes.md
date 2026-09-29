# TAK Engine 0.7.11

This release improves sustained AI production and fixes Zhon openings that
overcommitted mobile producers to builders and lodestones.

- Passive keeps building its army and defenses near home without sending attacks.
- Hard and Absurd keep expanding income and production and spending on troops
  beyond the old AI policy ceilings. Actual player and unit-type limits apply.
- Zhon prioritizes an opening army, reserves production while builders travel
  to expansion sites, and clears idle troops from mobile production sites.
- Normal retains its force targets. Pathfinding and movement rules are unchanged.

Protocol remains **194**. Use matching client and server builds. Your own
Kingdoms + Iron Plague game data is required.

Validation passed: 385 CTest configuration runs across Release, optimized Debug,
unoptimized Debug and Clang; 123 Python tool tests; 52 faction/map AI cases;
816 native animation timelines; sound/render comparisons; deterministic-math
checks; four 16,000-unit simulation smoke tests; local map transfer and encrypted
streaming checks. The full remote sweep on tak.pgnet.us and vpn3.pgnet.us completed
all 37 scenarios and 52 client sessions without unexpected desyncs. Spectator
cases validate flow control rather than hash consensus.

Native Windows, macOS ARM64 and all seven Linux package builds passed before
tagging. Tag workflows attach the release packages as they finish.

See [the validation report](https://github.com/pocketgeek/tak-engine/blob/main/docs/release-0.7.11-validation.md)
for coverage and limits.
