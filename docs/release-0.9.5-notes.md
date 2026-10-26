# TAK Engine 0.9.5

0.9.5 follows 0.9.4. It fixes killed units that stood back up with nearly full
health, and it stops the game writing downloaded maps and mods into your game
data folder.

**Compatibility first.** The dead-unit fix changes the game simulation, so
0.9.5 uses network protocol **299** (replay format **15**, unchanged).
**0.9.5 does not play with 0.9.4 or any earlier version, and does not play
their replays.**

- **Everyone updates together.** A 0.9.5 player cannot join a 0.9.4 server, and
  a 0.9.4 player cannot join a 0.9.5 server: the connection is refused with a
  *protocol version mismatch* message. Update your game and tell the people you
  play with to update theirs.
- **Old replays:** a replay recorded by 0.9.4 or earlier is refused with the
  version message. To keep watching them, keep your 0.9.4 install (0.9.0 to
  0.9.4 stay on the GitHub releases page); versions before 0.9.0 are no longer
  offered for download and can be built from their git tags (see the
  [development guide](development.md)).
- **Players:** nothing else to do. Settings, your data folder choice, key
  bindings, saved `.kmp` maps, generated-map recipes and mod packs and their
  ticks carry over. Maps and game mods downloaded by 0.9.4 or earlier sit in
  `MapCache/` and `ModCache/` folders inside your game data folder; 0.9.5 does
  not read them and leaves them alone, so you can delete them, and a map you
  had downloaded is downloaded again the first time a room uses it.
- **Server operators:** update `takserver` to 0.9.5 when you announce the
  release; until you do, 0.9.5 players cannot use your server. A server started
  with `--map-cache-dir` behaves as before. Without it, `takserver` now keeps
  uploaded and random maps in `TAKengine/takserver` in its per-user data folder
  (see [public-server.md](public-server.md#versions)); its data folder's old
  `MapCache/` is no longer read, and a server with neither a home folder nor
  `--map-cache-dir` refuses to start. A server started by hand for testing
  should get its own `XDG_DATA_HOME` or `--map-cache-dir`, or it writes to the
  real per-user folder.

## Dead units stay dead

A unit that was killed could take more hits in the same instant, for example
from the rest of a big splash or a volley landing together. Each extra hit was
still subtracted from its health, and with enough of them the health counter ran
past its lowest value and wrapped round to nearly full: the "dead" unit stood
back up with around 30,000 hit points.

As in the original game, a unit that has just been killed now ignores every
further hit. The player whose hit killed it keeps the kill, and the way it dies,
its overkill and its last reaction come from that killing blow alone. A single
hit stronger than any unit's health (over 32,767 damage, possible with armour
and veteran bonuses) no longer wraps round either, and health is capped at
32,767, the most the original game can hold; no shipped unit has more than
29,999. Unfinished buildings under heavy fire and units standing in deadly water
are protected the same way.

Games where no unit is hit again in the tick it dies are unchanged, which is
why the standard test games produce the same checksums as 0.9.4; the protocol
changes because the games that do hit a dying unit again now play out
differently.

## The game data folder is only read

Until now, every map a game room accepted, every map downloaded from another
player, every downloaded game mod, every random map you played and every
Cartographer Test Map copy was saved inside the folder holding your *Total
Annihilation: Kingdoms* install (in its `MapCache`, `ModCache` and `Maps`
folders). They now go in your own per-user folder, beside `settings.ini`:
`%APPDATA%\TAKengine\TAKingdoms\` on Windows,
`~/Library/Application Support/TAKengine/TAKingdoms/` on macOS and
`~/.local/share/TAKengine/TAKingdoms/` on Linux. The game folder can now be
read-only, for example a shared or system-wide install.

- A `MapCache` or `ModCache` folder an earlier version left in the game folder
  is ignored and left alone.
- Random maps an earlier version saved in the game folder's `Maps` still show
  up as ordinary maps, and Cartographer still lists your downloaded maps and
  Test Map copies.
- Screenshots, and the maps and mods you save from Cartographer, still go where
  they did.

## A game whose map cannot be built says so

Creating a game room on a map that cannot be built (for example one whose
terrain art is missing) now stays on the setup screen and shows *CANNOT CREATE*
with the reason, instead of silently doing nothing.

## For modders and mapmakers

- A unit type's `maxdamage` is capped at 32,767 on load, and a non-finite or
  hugely negative value no longer reaches an undefined float-to-integer
  conversion. The shipped data is unaffected.
- No map, recipe or package format changed.

## For contributors

- Sim (`90eb60b4`, `b01077c9`, protocol 299): `hurt()` returns, after the
  spread draw, for a unit already at zero health or below (a retail
  construction site excepted), as the original does; the HP subtractions in
  `hurt()`, in the unattended-construction decay and in deadly water are done in
  64 bits and clamped. New `hp_overflow` test (fails before: signed overflow in
  `fixed.h` under UBSan). Notes in [retail-engine.md](retail-engine.md).
- Caches (`f3518c51`): `hpi::setUserMapRoot` points the verified map downloads,
  Test Map copies, mod downloads and saved generated maps at the per-user
  folder; `mountRetailRoot` lists that folder's `MapCache/` and `Maps/` instead
  of the install's `MapCache/`. New `map_cache_readonly` test (a host-only map
  through a read-only server and players, and `--play-map` on a read-only
  install; fails before). See [map-transfer.md](map-transfer.md).
- Test tooling (`d06ba6d0`, `1ef443cb`): the AI profile and Cartographer UI
  tests fall back from a symbolic link to a hard link, then a copy, because
  MinGW's `create_symlink` always fails; `hpitool cat` writes binary on Windows;
  a failed `createGame` ends the `--mpai`/`--mphost` harness with
  `create game failed: ...` and exit 1 instead of spinning (new
  `harness_createfail` test), and no auto mode retries it; `desync-hunt.sh`
  matches the desync messages, not its own folder name, and takes `BUILD`.
- Legion scenario gate (`19b50831`): `tools/scenarios/baseline.json` has the
  values of its 2,193 stale `hash@` keys retaken; nothing else in it moves.
- `--mpai` Inner Circle seed 1: all seven gate hashes are unchanged from 0.9.4
  (Retail 60 s `d375d03a6c472773`, 300 s `387df8b0916a182e`; Legion 60 s
  `426ab0842b709c28`, 300 s `2d09d13d3ef0d324`; Retail AI 60 s
  `c5846d769e0a7d93`; Crusades 300 s Retail `3a380ae964f75b5a`, Legion
  `772874591d07dbdf`); the cross-compiler golden (`check-determinism`) stays
  `dcef618cd2e4d558`.

## Known limits

New with 0.9.5:

- **Screenshots and Cartographer saves still go into the data folder** first
  (`<data>/screenshots`, and `Maps/` and `mods/` for Cartographer); only the
  caches and generated maps moved.
- **A construction site's progress is not updated when it is hit** (as the
  original), and `tickConstruction` adds to health with no upper limit, so a
  mod with an absurdly short build time could still overflow it. Neither is new.
- **Windows:** the symbolic-link fallback skips directories.
- **The `mod_transfer` test flaked once** in the 0.9.4 test pass ("Not a
  directory ... ModCache"); the cause was not investigated.
- **Not run interactively:** the *CANNOT CREATE* message in the lobby, and the
  Benchmark and single-player paths when a map fails to build, were compiled and
  covered by the harness test only.

Everything listed under Known limits in the
[0.9.4 notes](release-0.9.4-notes.md#known-limits) still applies.

## Compatibility

Protocol **299** (was 298), replay format **15** and generator version **9**,
all unchanged except the protocol. Checked in source against `v0.9.4`:
`kNetVersion` in `src/net/protocol.h` and `kReplayFormat` in
`src/net/replayhdr.h`.

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.5 clients and servers play only with each other**.
- **Replays.** Playback requires the recording's protocol to equal the engine's,
  so 0.9.5 plays only 0.9.5 replays; a 0.9.4 replay (format 15, protocol 298) is
  refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes, pack
  folders and settings files carry over; downloaded maps and mods are fetched
  again into the new per-user folder.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.9.5-validation.md).
