# Legacy mission guesses — 2026-09-26

The audit found an obsolete, separately maintained mission interpreter in
`GameView`, reachable through Debug's `game <map> --mission`. Normal campaign
play already used `setupMission` and the server-driven `MissionScript`.

The obsolete path interpreted MAP_COMMAND's inline integer as a numeric verb,
guessed reinforcements from already placed unit types, issued activation orders
toward an army centroid, and substituted a tower roster index for unrelated
queries. Shipped COB v6 instead indexes a string/name table. `MissionScript`
already resolves that string and dispatches the authored command and arguments;
see the original evidence and implementation history in
[campaign-design.md](campaign-design.md).

Removed the duplicate constructor setup, client-side VM, guessed command
handlers, private trigger sweep and legacy briefing overlay. The old CLI spelling
now normalizes its mission stem and joins the same campaign launch path as
`--campaign <stem>`, including authoritative setup, script events, AI, mission
briefing and progression. Missing arguments fail with an explicit usage error.
No simulation or pathfinding rules were changed for this cleanup.

This eliminates the identified guessed interpreter; it is not a new claim that
every operation in the existing campaign implementation has been independently
compared with retail. This change needs no retail GUI launch: the operand meaning
is established by the shipped COB name table and the existing campaign tests.

Validation: both alias error cases (missing stem and a conflicting standalone
scenario flag) return a usage error before SDL initialization. Fresh local
multiplayer runs of `--campaign takmission01_mt` and
`missions/TAKMISSION01_MT.tnt --mission` reached the same state hash,
`a5032e7b986d7a1c`, after the ten-second headless mission run. The full campaign
regression suite also passed in Release, optimized Debug and regular Debug.
