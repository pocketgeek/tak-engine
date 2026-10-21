# TAK Engine 0.8.8

0.8.8 follows 0.8.7. Mods replace override packs, with a folder for cosmetic
mods and one for game mods; games can be created with the original game's
**power codes**; the in-game chat box is easy to read and can be moved and
sized; the interface has a new, clearer font; and Legion pathfinding had two
more rounds of tuning for armies of about 100, for attack rings along walls and
through narrow gates. The Legion work alters the game simulation, mods and
power codes change the match options, and replays record the new Power codes
option, so the network protocol moves from 284 to **288** and the replay format
from 13 to **14**: **0.8.8 cannot play with 0.8.7 or earlier, and cannot play
their replays.** A server and all of its players must update to 0.8.8
together. Retail pathfinding plays exactly as in 0.8.7.

## Mods replace overrides

Override packs, and the Off / Cosmetic / Full override mode on the setup
screen, are gone. Packs now live in a `mods/` folder in your install folder,
in one of two kinds:

| Folder | Kind | What it may change |
| --- | --- | --- |
| `mods/cosmetic/<pack>/` | **Cosmetic mods** | Textures, sprites, sounds, music, fonts and interface art; never units, rules or anything else that changes the game. |
| `mods/game/<pack>/` | **Game mods** | Everything, including units, weapons, build menus, maps, scripts and models. |

- **Cosmetic mods are each player's own.** In single player and multiplayer,
  host or not, every player ticks their own cosmetic packs in the room. They
  change only what that player sees and hears, are never sent to the server or
  to other players, and players in one game may use different ones.
- **Always-on cosmetic mods.** A `.hpi` archive placed directly in
  `mods/cosmetic/` (for example `mods/cosmetic/click.hpi`) is loaded in every
  game: skirmish, multiplayer, campaigns, map scenarios and replays. It is not
  listed for ticking; the room, the campaign screen and the mission briefing
  name it on an **ALWAYS ON** line. To turn one off, move it out of the folder.
- **Game mods in single player** are always available: tick them in the room.
- **Game mods in multiplayer** need **Game mods: On** on the create screen
  (off by default). Only the host ticks them; they are sent to the server and
  to every player automatically, and everyone checks they have the same
  package before the game starts, as Full override packs were.
- **Campaigns and Crusades battles** use the original game data plus your
  always-on cosmetic archives only: no game packs and no ticked cosmetic packs.
- **A cosmetic mod never stops a game.** If an always-on archive or a ticked
  cosmetic pack cannot be loaded (a damaged archive, or more than 256 MB of
  mods), or its animation art would change game timing, it is left out of that
  game and the game's chat names it.

### Moving your packs from `overrides/`

The old `overrides/` folder is **no longer read, and nothing in it is moved or
changed**. Move your packs yourself, with the game closed:

1. For a pack you used in **Cosmetic** mode (sounds, textures, interface art),
   move its folder from `overrides/<pack>/` to `mods/cosmetic/<pack>/`.
2. For a pack you used in **Full** mode (new units, weapons, build menus or
   rules), move its folder to `mods/game/<pack>/`. A pack that changes units or
   rules belongs here even if it also has art: in `mods/cosmetic/` its gameplay
   files are filtered out.
3. Keep each pack in its own folder. Files placed directly in `mods/` or
   `mods/game/` are never loaded, and a `.hpi` directly in `mods/cosmetic/` is
   always on.
4. Start the game and tick the packs again in the room: the old pack choices
   and the override mode are not carried over.

When `overrides/` still exists, the game prints a line saying it is no longer
read to the terminal it was started from. See [Mods](user-guide.md#mods) in
the user guide.

## Power codes

- **A new match option, Power codes**, on the create screen in single player
  and multiplayer, off by default. It is always off in campaigns and Crusades
  battles, and it cannot be changed in the room.
- **Type a code in the chat** (**Enter**, the code, **Enter**). Capital letters
  do not matter. The nine codes are **+ATM** (fill your mana pool),
  **+ManaMe** (fill your selected units' own mana), **+DoubleShot** and
  **+HalfShot** (double or halve all damage in the game), **+LOS** (line of
  sight off or on for you, or **+LOS Off** / **+LOS On**), **+Mapping** (the map
  revealed or hidden for you), **+NowISee** (line of sight and the map both
  off: you see everything), **+Radar** (your minimap shows every unit) and
  **+MakePoster**.
- **Posters.** **+MakePoster**, or **MAKE POSTER** in **Controls** (Print
  Screen by default, only in a game with power codes on), saves a picture of
  the whole map at full size, as you see it, as `BIGSHOT0001.png` and so on in
  a `screenshots` folder in your game data folder. The game keeps running while
  it is made, and a chat line says where it was saved. Where the desktop takes
  Print Screen for itself, or the keyboard has none, set another key or type
  the code.
- A code line is sent as chat to every player, as in the original game, so
  everyone sees what was typed. With power codes off a code line is ordinary
  chat. Codes are recorded in replays.

See [Power codes](user-guide.md#power-codes) in the user guide, including how
they differ from the original game.

## A chat box you can read

- In-game chat is now clear bold text (Noto Sans Bold) drawn at your screen's
  real resolution, on a dark panel that reads over bright sand and dark forest
  alike.
- Each message starts with the sender's name in their player colour; long
  lines wrap instead of running off the screen or under the command panel.
  The box sits above the information bar and moves up over a builder's build
  icons when they are in the way.
- **Options → Interface** has **Chat Scale** (50–300% in 10% steps, on top of UI
  Scale; default 100%) and **Chat Position** (Left or Centered; default
  Centered). Both take effect at once, also during a game.
- Single player shows what you type in the chat box too, such as power codes.

## A new interface font

- Plain interface text, in the menus, rooms, Options, the HUD's readouts, the
  scoreboard, briefings and Unit Info, is now drawn in Noto Sans Bold, sharp at
  every UI Scale. Names keep their own capitals (players, maps, mod packs,
  units); labels such as **START** stay in capitals.
- The original game's decorative lettering is kept: big messages, PAUSED,
  briefing headings, the Unit Info labels, menu art and buttons.
- **Options → Interface → UI Font** chooses **Modern** (the new font, the
  default) or **Classic** (the look the engine had before). It takes effect at
  once.
- Text in the rooms, Options and menus is now measured to fit its box in
  either font, so names, labels and buttons no longer run into each other.

## Legion pathfinding: armies of about 100, rounds 5 and 6

As in 0.8.6 and 0.8.7, the target was about 100 units at a time, in the places
Legion was known to handle worse than Retail. Retail pathfinding is unchanged
by all of it.

- **Pressing into a wall's shadow.** Attackers held just outside reach of a
  target that a wall hides from part of the ground step on into that ground
  until they can fire, and stay there. Rings along a plain wall now hold about
  as many attackers in reach as Retail's.
- **Fight-move columns round a still target.** Units on a fight (attack-move)
  order that come upon a target standing still surround it as an attack order
  does, instead of queueing on the near side. In the test fixture the damage
  dealt rose from 2,372 to 2,867 (Retail 2,814), with as many units in reach
  as Retail.
- **The reach edge.** A unit stopped right at the edge of its weapon's reach
  counts as in reach, as it does when firing. Melee units still need contact.
- **Gatehouse corridors.** Units attacking through a narrow gate, such as the
  corridor round a gatehouse, fill it from its far end, as in Retail: the first
  units walk on past the first place they could fire from instead of stopping
  in the gate and shutting it. In the gatehouse fixture the damage dealt rose
  from 4,998 to 11,719 (Retail 7,172), with more units in reach than Retail.

See [Legion](user-guide.md#legion) in the user guide, and
[legion-pathfinding.md](legion-pathfinding.md) for the measurements.

## For contributors

- New tests: `legion_ringshadowpress`, `legion_ringshadowmixed`,
  `legion_fightring`, `legion_ringedge`, `legion_meleepress` (registered only
  with `TAK_TEST_DATA`), `legion_gatewalkon`, `legion_gateyard`,
  `legion_walkononward`, `legion_ringdeep`, `legion_ringdeepchurn`,
  `power_codes`, `server_power_codes`, `poster`, `chatbox` and `uifont`.
  Renamed: `override_packs`, `override_settings` and `override_transfer` are
  now `mod_packs`, `mod_settings` and `mod_transfer`. `replay_test` pins
  protocol 288 and replay format 14 and refuses 287, 286 and 285.
- New fixtures: `tools/scenarios/attackring-melee-30.scn`,
  `attackring-wall-mixed-56.scn`, `attackring-gateyard-36.scn`,
  `attackring-gatehouse2-48.scn` and `attackring-deadcorridor-30.scn`. The
  Legion scenario baseline (`tools/scenarios/baseline.json`) was retaken for
  rounds 5 and 6, and now also covers the fixtures 0.8.7 left without entries
  (`attackring-gatehouse-40`, `attackring-fight-diag-60`, `taperbridge-100`,
  `taperbridge-small`, `ringbend-pocket`).
- Wire: `GameOptions` byte 3 is `gameMods` (0/1) instead of the 0/1/2 override
  tier, the `Override*` messages are `Mod*` with the same ids, and the replay
  header's byte and digest are `gameMods` and `modDigest` (protocol 287).
  `GameOptions` gains a `powerCodes` byte, `Cmd::PowerCode` is 28, and replay
  format 14 appends the POWER CODES byte (protocol 288). See
  [multiplayer-design.md](multiplayer-design.md).
- The mod cache is `ModCache/*.takmods` (was `OverrideCache/*.takoverrides`).
  The debug `--overrides none|cosmetic|full` flag is now
  `--mods none|cosmetic|game|all`. The repository's order-click archive moved
  from `overrides/click.hpi` to `mods/cosmetic/click.hpi`.
- The interface font is a Latin-1 subset of Noto Sans Bold (SIL OFL 1.1),
  embedded in `takclient` and drawn with the vendored `stb_truetype`
  (`src/vendor/stb`); `src/client/uitext.h` is the one text API. Their
  licences are in the new `CHAT-FONT-LICENSE.txt`, which ships beside the
  binaries in every package.
- The `--mpai` Inner Circle seed 1 hashes are unchanged from 0.8.7: Retail
  60 s `b353410003e98dd5`, 300 s `a3d953803a064191`; Legion 60 s
  `0825aeb9e8139203`, 300 s `ce5c4ba7562e53b9` (the Legion changes are not met
  in that game, and power codes are off there).
  <!-- LEAD: confirm on the final source and on Linux, Windows and macOS. -->

## Known limits

- **Legion units still stand and wait more than Retail's**, in crowds, at
  corners and in rings, though they usually arrive sooner. This is cosmetic.
- **Legion attackers through a narrow gate now deal more damage than
  Retail's**: in the test layouts from about 1.2 to 2.2 times as much, with
  more units in reach.
- **Power codes go through the game simulation like orders**, so their effects
  are part of every player's game (and of replays). The original applied a
  code only on the computer of the player who typed it. The codes that act on
  the typer (+ATM, +ManaMe, +LOS, +Mapping, +NowISee) still act only for that
  player; +DoubleShot and +HalfShot change everyone's damage.
- **Fog of war: Full Vision does not switch line of sight off at the start.**
  You already see the whole map and every unit, but your units do not attack
  what they cannot see until **+LOS** or **+NowISee** is typed.
- **UI Font: Classic is close to, but not pixel-identical with, 0.8.7's
  interface**: spaces are narrower in places (4 px), and some layouts moved to
  fit text by its measured width.
- Carried over from 0.8.7: **Legion bridges are still a lottery at about 100
  units.** Two armies meeting at a narrow, tapering bridge can still jam in its
  mouth and take far longer than Retail to cross. In a few test fixtures a
  Legion group still arrives a little later than in Retail, and a group
  crossing a firing ring arrives a little later than in Retail.
- Carried over from 0.8.6: a stationary unit choosing its own targets can keep
  ignoring an enemy it once found blocked. The Legion 10,000-unit workloads
  still struggle with congestion and the largest run over the tick budget; in
  mixed-10000 the flyers hover over a slow ground army. Moving the game window
  between a Retina and an ordinary display does not rescale UI Scale. In the
  remote test sweep, Retail `h-cramped` ends early because the idle seat starts
  with only its Monarch.

## Compatibility

Protocol **288** (0.8.7: 284), replay format **14** (0.8.7: 13), generator
version **9**, campaign payload **4** and Crusades SQL schema **9** (the last
three unchanged). Checked in source against `v0.8.7`: `kNetVersion` in
`src/net/protocol.h`, `kReplayFormat` in `src/net/replayhdr.h`, the map
generator's `formatVer` (`src/tnt/mapgen.h`), the campaign payload's
`kVersion` (`src/net/crusades.h`) and `kSchemaVersion`
(`src/server/crusades/store.cpp`).

- **Multiplayer.** A server rejects a client whose protocol differs from its
  own, so **0.8.8 clients cannot join 0.8.7 or older servers, and 0.8.7 or
  older clients cannot join a 0.8.8 server**. Update the server and every
  player together. Protocols 285, 286 and 287 were never released.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.8.8 does not play replays recorded by 0.8.7 or earlier, and
  they do not play 0.8.8 replays. 0.8.8 writes replay format 14, which adds the
  Power codes option. Keep an old copy of the engine to watch old recordings.
  Crusades battle replays follow the same rule.
- **Maps.** Saved `.kmp` maps and generated-map recipes (generator version 8
  or 9) carry over unchanged.
- **Mods.** Packs in `overrides/` are no longer loaded and are not moved: see
  [Moving your packs](#moving-your-packs-from-overrides). Downloaded override
  packages in `OverrideCache/` are not reused; game mods download again into
  `ModCache/`.
- **Settings.** A 0.8.7 settings file reads in 0.8.8 and keeps its values,
  except the override choices (`gameCreate.overrides`, `overrides.hostPack`,
  `overrides.cosmeticPack`), which are ignored and dropped on the next save.
  New settings: `uiFont` (default `modern`), `chatScale` (1.0), `chatAlign`
  (1, centred), `gameCreate.powerCodes` and `gameCreate.gameMods` (both off),
  `mods.gamePack` and `mods.cosmeticPack` (the ticked packs), and a new
  rebindable **MAKE POSTER** key (Print Screen by default).

The interface font's licence, `CHAT-FONT-LICENSE.txt`, is new in every
package. No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.8.8-validation.md).
