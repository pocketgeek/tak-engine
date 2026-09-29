# TAK Engine 0.7.12

This release adds per-player diplomacy controls, improves Hard/Absurd expansion,
and fixes resource leaks and a terrain reload race.

- **D opens Diplomacy:** all players are listed. Give eligible selected units to
  living teammates, choose which teammates receive your surplus mana, and choose
  who receives your chat. Mana sharing defaults on for teammates; chat defaults
  on for everyone. Giving units keeps the dialog open and updates eligibility.
- **Hard and Absurd expand across more mana deposits:** earlier expansion
  builders, reservations that prevent several builders choosing the same site,
  and new deposits prioritized over nearby lodestone upgrades. Normal and Passive
  policy, movement, and pathfinding are unchanged.
- **Memory and resource fixes:** loading fonts, model/editor teardown, stopped
  stream buffers, and terrain worker synchronization during reload/remount.
- Fix Apple Clang compatibility in AI mana-site reservation checks.

**Protocol 195:** update clients and servers together. Version 0.7.11 uses
protocol 194 and cannot join these matches. Your own Kingdoms + Iron Plague game
data is required. No new dynamic game-library dependencies were added.

Validation: 487 CTest runs across Release, optimized Debug, unoptimized Debug,
Clang, and AddressSanitizer/LeakSanitizer; 123 Python tests; 52 faction/map AI
cases; 816 animation timelines; native sound/model comparisons; deterministic
math checks; four 16,000-unit simulation smoke tests; local diplomacy/map
transfer and encrypted streaming checks.

The full remote sweep on tak.pgnet.us and vpn3.pgnet.us passed all 37 scenarios
and 52 client sessions without unexpected desyncs or incomplete runs. Spectator
cases validate flow control rather than hash consensus. Native Windows, macOS
ARM64, and all seven Linux package builds passed before tagging.

See [the validation report](https://github.com/pocketgeek/tak-engine/blob/main/docs/release-0.7.12-validation.md)
for full coverage and limits.
