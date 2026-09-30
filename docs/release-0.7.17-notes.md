# TAK Engine 0.7.17

- Right-click cycles settings backward in game creation, lobby slots (AI difficulty,
  faction, team, color, and slot type), and streaming setup. Choices wrap around;
  player colors skip colors already in use. Options toggles accept either button.
  Campaign page buttons also support right-clicking in the opposite direction.
- Game-create choices persist between sessions, including map selection/sorting,
  random-map settings and seed, balance, unit cap, sight/radar, fog, start locations,
  monarch rules, speed changes, spectating, overrides, and game name. Game passwords
  are not saved. Resetting Options preserves these remembered choices.
- The stats panel shows a local 24-hour clock directly beneath Units, alongside
  the existing elapsed Real Time and Game Time displays.

Protocol remains **203**. No simulation or network behavior changes, and no new
runtime dependencies. Retail game data remains external and is not distributed.

See the [validation report](https://github.com/pocketgeek/tak-engine/blob/main/docs/release-0.7.17-validation.md).
