# TAK Engine 0.7.23

- Add experimental **Flowfield** pathfinding alongside the default **Retail**
  option in skirmish and multiplayer creation. The lobby reports the host's
  choice, and campaigns retain Retail. Shared immutable tiles, bounded caches,
  deterministic work admission and a shared worker pool prepare routes without
  allocating the worst-case memory limit at startup. Retail's search files are
  unchanged. See [the implementation and measurements](pathfinding-port.md).
- Improve maze movement, mixed-footprint traffic, group point arrivals,
  fight-move/patrol continuation, factory exits and inherited rally orders.
  Air and sea transport routes have additional integration and replay coverage.
  Flowfield remains experimental: giant unexplored maps and competing movement
  profiles can still produce long routes and waits.
- Let builders drag a basic or Divine lodestone placement box to visit eligible
  mana spots, scout unseen deposits and use normal clearing/building/upgrading
  rules. Shift appends the job; Stop cancels it. Patrolling builders repair
  nearby damaged allies and resume their original patrol.
- Defensive AI sends its army to meet spotted threats near its base and
  protected structures, then recalls it. It continues building without sending
  offensive raids.
- Add named generated maps with recipe identifiers that distinguish duplicate
  titles. Regenerate previews when a slider is released. Add Aramon Riverlands,
  Taros Maze, Veruna Ports, Zhon Clearings and Creon Highlands, improve biome
  transitions, and leave lake/maze borders open. Veruna Ports checks connected
  deep-water production sites. Older recipes keep their generated terrain.
- Correct damage/destruction for authored trees, rocks and other destructible
  scenery, including generated maze obstacles, building wrecks and corpses.
- Add a remembered, independent F4 Scorecard Scale control. Show team numbers
  and group rows by team when a team contains multiple players.
- Add Shift+Z retail zoom without requiring a selection; allow live speeds up
  to 8×. Disco/headbang affect selected mobile units, use the living monarch
  only when nothing is selected, and last ten real seconds. Buildings do not
  dance.
- Fade world sounds with distance outside the camera view while preserving
  on-screen volume and global/UI/music behavior. Make all basic and Divine
  lodestones cast shadows, intentionally extending retail behavior.
- Tile the bottom HUD art instead of stretching it; cover the spectator's
  full-screen minimap completely and show loading progress during local setup.
  Remove the game name from single-player lobbies.
- Fix AA atlas resource reuse and improve bounded target allocation, while
  keeping application AA, bilinear filtering, smooth GUI art and smooth movies
  **fixed off**. Their old preferences are ignored and controls removed; engine
  support remains. Trees Sway is configurable and defaults on. Fullscreen,
  VSync, shadows, statistics, hardware cursor and smooth motion default on;
  saved choices for these controls still apply.
- Ship native Linux x64/ARM64 packages for Ubuntu, Debian, Fedora and openSUSE
  Leap, plus Arch x64 and Slackware64 15.0. Windows and macOS retain native
  x64/ARM64 packages. Windows executables/installers are signed and timestamped.
- Add a standalone read-only official-HPI directory-signature research verifier
  and [document the findings](research/official-archive-verification.md).
  Retail binaries, archives and key material are not shipped; engine archive
  acceptance is unchanged.

**Compatibility:** protocol **221**, replay format **11**, campaign payload **4**,
SQL schema **9**. Update live clients and servers together. Protocol-220/format-11
and protocol-219/format-10 recordings are supported with their original pathfinder
and patrol behavior; older recordings need their original engine. Ordinary
commands retain their existing wire layout; the new area-build command carries
its additional corner coordinates.

No new dynamic runtime dependencies or retail assets are included. README and
Cartographer screenshots are refreshed from this version. See the
[validation report](release-0.7.23-validation.md) for test coverage and limits.
