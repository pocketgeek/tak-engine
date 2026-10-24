# TAK Engine 0.8.14

0.8.14 follows 0.8.13. It is a small release about mods: packs you always want
now live in their own **always-enabled** folders, and multiplayer has no
separate GAME MODS switch any more: the host ticks game packs in the room, as
in single player.

**Compatibility first.** The game simulation, the network protocol (**293**)
and the replay format (**14**) are the same as 0.8.13's and 0.8.12's, so
**0.8.14 plays with 0.8.13 and 0.8.12 and plays their replays, both ways.** A
server and its players may run any of the three, in any mix; 0.8.11 or earlier
still cannot play with any of them. In a mixed game:

- A room hosted by **0.8.12 or 0.8.13 with GAME MODS: OFF** still runs **no
  game packs for anyone**. A 0.8.14 player in it sees **GAME MODS: OFF
  (HOST)**, and their own always-enabled game packs are not used either.
- A **0.8.12 or 0.8.13 player joining a 0.8.14 host's room** sees an ordinary
  room with game mods on: **HOST:** names the host's packs (always-enabled ones
  included), or **NONE SELECTED**, and their game receives and checks the
  host's package as usual.
- Cosmetic mods stay each player's own: an older player's
  `mods/cosmetic/*.hpi` archives still load in their own older game.

**One step to take when you update:** a `.hpi` placed directly in
`mods/cosmetic/` (the old always-on archives) is **no longer loaded**. Move it
into its own folder under `mods/cosmetic/always_enabled/`; see
[Moving your always-on archives](#moving-your-always-on-archives).

## Mods

- **Always-enabled packs, in folders.** Each kind of mod has an
  `always_enabled` folder for packs you want in every game:
  - `mods/cosmetic/always_enabled/<pack>/` loads in every game you play:
    skirmish, multiplayer, campaigns, map scenarios and replays. Like any
    cosmetic pack it is yours alone and is never sent anywhere.
  - `mods/game/always_enabled/<pack>/` applies in every game you **host**,
    single player and multiplayer. It is sent to every player with your ticked
    game packs, exactly like them. When you join someone else's room, the
    host's packs are the game's packs and yours are not used. Game packs never
    apply in campaigns or Crusades battles.

  A pack there is an ordinary pack folder (loose files or `.hpi`, `.ufo` and
  `.kmp` archives). It is never ticked: the game room lists it, locked, on an
  **ALWAYS ON** line under its kind's heading, and the campaign screen and
  mission briefing name the cosmetic ones. To turn one off, move its folder
  out of `always_enabled/`.
- **No GAME MODS switch.** The multiplayer setup screen's **GAME MODS: ON/OFF**
  option is gone. Multiplayer works like single player: the host of any room
  except a campaign ticks game packs in the room, and they are sent to every
  player. A saved GAME MODS choice is ignored.
- **One name, one pack.** An always-enabled pack and an ordinary pack of the
  same kind with the same name (ignoring upper and lower case) count as one:
  the always-enabled one is used and the ordinary folder is hidden from the
  room. A saved tick of that name is kept and applies again once the
  always-enabled pack leaves `always_enabled/`.
- **Up to 64 packs of each kind**, always-enabled ones included. Inside each
  kind the always-enabled packs load first, then the ticked ones, each in
  alphabetical order; later packs win when they change the same file.

See [Mods](user-guide.md#mods) in the user guide.

### Moving your always-on archives

Up to 0.8.13, every `.hpi` placed directly in `mods/cosmetic/` was always on.
0.8.14 no longer reads them, and does not move them for you. For each one:

1. Make a folder for it in `mods/cosmetic/always_enabled/`, for example
   `mods/cosmetic/always_enabled/click/`.
2. Move the archive into it: `mods/cosmetic/always_enabled/click/click.hpi`.
3. Open a game room: the folder's name shows on the **ALWAYS ON** line under
   **COSMETIC MODS**.

Files placed loose in `mods/cosmetic/`, `mods/game/` or an `always_enabled/`
folder are never loaded; each pack needs its own folder. Your ticked packs and
other settings carry over.

## Cartographer

- The Cartographer lists and edits **ordinary game packs only** (`mods/game/`):
  **SAVE AS GAME MOD...**, **OPEN FROM GAME MOD...**, **File → Open AI mod...**
  and **PREVIEW WITH GAME MOD** leave `mods/game/always_enabled/` out. A new
  pack cannot be named `always_enabled`, nor take the name of an
  always-enabled pack. To edit an always-enabled pack, move its folder out of
  `always_enabled/` first.
- **TEST IN GAME** and **Test Map** open a room you host, so your
  always-enabled game packs apply there too; their messages name them.

See [Saving as a game mod](cartographer-guide.md#saving-as-a-game-mod).

## For modders

- A pack's format is unchanged: the same folder of loose files or archives
  works as an ordinary pack (`mods/<kind>/<pack>/`, ticked in the room) or as
  an always-enabled one (`mods/<kind>/always_enabled/<pack>/`). Moving it
  between the two changes only whether it is ticked.
- Packs you ship to others are still ordinary folders; tell players which kind
  (`cosmetic` or `game`) it belongs in. A pack that should be on in every game
  a player hosts can go in `mods/game/always_enabled/`.
- In multiplayer a game pack reaches the other players only through the
  host's package. An always-enabled game pack on a joining player's install
  changes nothing in someone else's room, so a joiner never runs a different
  game from the host.
- An always-enabled game pack changes the game's data in every game you host,
  so it also changes the map scenarios on offer (a scenario that needs a unit
  type only a pack supplies is offered when your install has any game pack)
  and the AI files (`ai/takengine.txt`, `ai/default.txt`) of every game you
  host.
- The debug `--mods none|cosmetic|game|all` switch leaves out the
  always-enabled packs of a kind it excludes, too. `--game-mod <pack>` (with
  `--play-map`, as TEST IN GAME uses it) may name an always-enabled pack: that
  pack applies anyway, the test room ticks no other pack, and your saved ticks
  stay saved.

## For contributors

- Pack discovery is `hpi::alwaysEnabledPacks` and `hpi::modPacks`
  (`src/hpi/hpi.cpp`); `hpi::kAlwaysEnabledDir` is matched ignoring case, and
  `modPacks` drops it and any name an always-enabled pack holds. `modFiles`
  layers always-enabled packs first. The always-on `alwaysOnMods` /
  `alwaysOnModFiles` archives are removed (`8d92c6b5`).
- `MpClient::hostGameModPacks` (always-enabled plus ticked, sorted, at most 64)
  feeds the host's map package, mod package and room VFS. The create screen
  always sends `GameOptions::gameMods = 1`; the server still sanitises it and
  forces 0 for campaigns and Crusades battles. `gameCreate.gameMods` is dropped
  from settings (skipped on load). The repository's `click.hpi` moves to
  `mods/cosmetic/always_enabled/click/click.hpi`.
- Review fixes (`5fa673d0`): a click on the room's GAME list keeps saved ticks
  hidden by a same-named always-enabled pack; the 64-pack limit counts
  always-enabled packs; `--game-mod` naming an always-enabled pack is an empty
  launch-only selection (`setLaunchGameModPacks` takes an optional). New test
  `no_game_mods_toggle`; `mod_packs`, `mod_settings`, `mod_transfer`,
  `ai_profile`, `cartographer_aiprofile` and `server_public` tests updated.
- Nothing under `src/sim/` or `src/ai/` changed. Under `src/net/` and
  `src/server/` the changes are client mod logic and comments only; the
  server's behaviour is unchanged. The `--mpai` Inner Circle seed 1 hashes are
  those of 0.8.13: at 60 s Retail `b353410003e98dd5`, Legion
  `0825aeb9e8139203`; at 300 s Retail `a3d953803a064191`, Legion
  `ce5c4ba7562e53b9`; a Retail AI seat (`TAK_AI_LEVEL=6`) `14849c00bc6e2a19`
  at 60 s.

## Known limits

All of 0.8.13's known limits carry over:

- **Legion units still stand and wait more than Retail's**, in crowds, at
  corners and in rings, though they usually arrive sooner. This is cosmetic.
- **Legion attackers through a narrow gate deal more damage than Retail's**:
  in the test layouts from about 1.2 to 2.2 times as much.
- **Power codes go through the game simulation like orders**, so their effects
  are part of every player's game (and of replays); the original applied a code
  only on the typer's computer. +DoubleShot and +HalfShot change everyone's
  damage.
- **Fog of war: Full Vision does not switch line of sight off at the start.**
  Your units do not attack what they cannot see until **+LOS** or **+NowISee**
  is typed.
- **UI Font: Classic is close to, but not pixel-identical with, 0.8.7's
  interface.**
- **The campaign book's chapter numeral** still touches the bottom of "APTER"
  in CHAPTER, as in the original game.
- **Legion bridges are still a lottery at about 100 units.**
- **A stall on the server's side is charged to the players**, and a
  reconnecting player who hitches while catching up is charged as any other
  hold.
- A stationary unit choosing its own targets can keep ignoring an enemy it once
  found blocked. The Legion 10,000-unit workloads still struggle with
  congestion and the largest run over the tick budget; in mixed-10000 the
  flyers hover over a slow ground army. Moving the game window between a
  Retina and an ordinary display does not rescale UI Scale. In the remote test
  sweep, Retail `h-cramped` ends early because the idle seat starts with only
  its Monarch.
- **The Retail AI** is not a replay of a 1999 game (this engine's world, orders
  a tick later, its own random numbers), weighs targets against this game's
  unit cap and has not been tested at it, is weaker than Normal (a Taros Retail
  AI can fall far behind), and in campaigns takes its start positions from the
  mission map's start markers (inferred).
- **Our six levels share identical built-in tables**, so `ai/takengine.txt`
  sets the same unit weights and limits for all of them (they still differ in
  behaviour and limit scaling).
- **TEST IN GAME has not yet been tried by hand on Windows or macOS.**

New with 0.8.14:

- **Old always-on archives are not moved for you**, and the game does not warn
  about a `.hpi` left directly in `mods/cosmetic/`: it is simply not loaded.
<!-- TODO-LEAD: confirm no further 0.8.14 limit (for example from the
mixed-version or screenshot checks) and drop this comment. -->

## Compatibility

Protocol **293**, replay format **14**, generator version **9**, campaign
payload **4** and Crusades SQL schema **9**: all unchanged from 0.8.13 and
0.8.12. Checked in source against `v0.8.13`: `kNetVersion` in
`src/net/protocol.h`, `kReplayFormat` in `src/net/replayhdr.h`, the map
generator's `formatVer` (`src/tnt/mapgen.h`), the campaign payload's `kVersion`
(`src/net/crusades.h`) and `kSchemaVersion` (`src/server/crusades/store.cpp`).

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent, and the build id is sent but ignored. So **0.8.14, 0.8.13 and 0.8.12
  clients and servers play together in any mix**. The `GameOptions` game-mods
  byte and the game-mod package format are unchanged: a 0.8.14 host always
  asks for game mods, an older host's GAME MODS: OFF is still honoured, and a
  0.8.14 host's package decodes on 0.8.12 and 0.8.13. 0.8.11 or older clients
  still cannot join a 0.8.12 to 0.8.14 server, nor those clients an older one.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.8.14, 0.8.13 and 0.8.12 play each other's replays. Replays
  from 0.8.11 or earlier still need the version that recorded them. Crusades
  battle replays follow the same rule.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes,
  `ModCache/` downloads, pack folders and settings files carry over; the saved
  GAME MODS choice (`gameCreate.gameMods`) is ignored and no longer written. A
  `.hpi` directly in `mods/cosmetic/` must be moved (see above).

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.8.14-validation.md).
