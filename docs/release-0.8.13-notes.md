# TAK Engine 0.8.13

0.8.13 follows 0.8.12. It is a small release: arming an order no longer puts a
line of text on screen (the original game never did), and the Cartographer
starts on an empty window with no map, with everything starting from its
**File** menu.

**Compatibility first.** Every change is in the game's interface or in the
Cartographer: the game simulation, the network protocol (**293**) and the replay
format (**14**) are the same as 0.8.12's, so **0.8.13 plays with 0.8.12 and
plays its replays, both ways.** A server and its players may run either version,
in any mix; 0.8.11 or earlier still cannot play with either.

## In game

- **No order text on screen.** Pressing an order key or button (Attack,
  Fight-move, Patrol, Guard, Reclaim, Repair, Load, Unload, Move) used to show
  a line such as **ATTACK: CLICK TARGET** or **FIGHT-MOVE: CLICK DESTINATION**
  near the top left of the view. It is gone. As in the original game, an armed order
  shows only as its cursor (the attack cursor, the patrol cursor and so on) and
  its pressed button in the command panel, which the game already drew. In a
  campaign mission nothing now sits under the objectives panel either (0.8.12
  had moved the line there). See [Orders](user-guide.md#orders).

## Cartographer

- **An empty window to start.** Started from its shortcut with no map named,
  the Cartographer opens an empty window: the title bar reads just
  **Cartographer**, the status bar **NO MAP OPEN**, and the middle a short hint.
  It used to open the New Map dialog over a blank 8 × 8 map that already
  counted as unsaved.
- **Everything starts from the File menu:** **New map** (**Ctrl+N**), **Open
  map** (**Ctrl+O**), **New AI mod...** (**Ctrl+Shift+N**), **Open AI mod...**
  (**Ctrl+Shift+O**) and **Exit**. The UI size, **Preferences** and **Help**
  work too; every map command, tool and click on the map does nothing until a
  map is open.
- **Nothing to save.** The empty window never counts as unsaved: closing it
  asks nothing, **New map** does not ask to discard anything, and no recovery
  copy is written.
- **Back to the empty window.** Cancelling New map or Open map, or closing an AI
  mod opened from the empty window, returns to it.
- **A map that fails to open at start** (missing from the install, or a damaged
  `.kmp` or `.tnt`) shows **OPEN FAILED** and then the empty window. It used to
  leave an editable, unsaved Untitled map behind.
- **Holding Esc no longer quits.** Esc still closes the empty window, but an
  Esc held down past closing a dialog or message no longer goes on to quit the
  editor.
- **TEST IN GAME's error message.** If the game exits with an error after you
  closed the AI mod that was being tested, the message no longer claims the AI
  mod is still open.

A map named on the command line, `--new`, crash recovery and the command-line
`--save` / `--bundle` modes behave as before. See
[The first screen](cartographer-guide.md#the-first-screen) in the Cartographer
guide.

## For contributors

- The armed-order banner and the objectives-panel offset it needed are removed
  from `src/client/gameview_render.cpp` (`a41beb28`).
- The Cartographer's empty window is `Session::noDocument`, with
  `Editor::noMap() = aiModOnly || noDocument` generalising the AI-mod no-map
  mode (`90342ab7`); review fixes for held Esc, the failed-test message
  (`playtestAiMod`) and a failed launch open (`c579cab5`). New tests:
  `cartographer_blank_launch_ui` and `cartographer_failed_open_ui`;
  `cartographer_aimod_ui`, `cartographer_starts_ui` and
  `cartographer_place_blocked` now create their map with Ctrl+N.
- Nothing under `src/sim/`, `src/net/`, `src/ai/` or `src/server/` changed. The
  `--mpai` Inner Circle seed 1 hashes are those of 0.8.12: at 60 s Retail
  `b353410003e98dd5`, Legion `0825aeb9e8139203`; at 300 s Retail
  `a3d953803a064191`, Legion `ce5c4ba7562e53b9`; a Retail AI seat
  (`TAK_AI_LEVEL=6`) `14849c00bc6e2a19` at 60 s.

## Known limits

All of 0.8.12's known limits carry over:

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

## Compatibility

Protocol **293**, replay format **14**, generator version **9**, campaign
payload **4** and Crusades SQL schema **9**: all unchanged from 0.8.12. Checked
in source against `v0.8.12`: `kNetVersion` in `src/net/protocol.h`,
`kReplayFormat` in `src/net/replayhdr.h`, the map generator's `formatVer`
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` (`src/net/crusades.h`)
and `kSchemaVersion` (`src/server/crusades/store.cpp`).

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number and build
  id are only logged. So **0.8.13 and 0.8.12 clients and servers play together
  in any mix**. 0.8.11 or older clients still cannot join a 0.8.13 or 0.8.12
  server, nor can 0.8.13 clients join an older one.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.8.13 plays 0.8.12's replays and 0.8.12 plays 0.8.13's. Replays
  from 0.8.11 or earlier still need the version that recorded them. Crusades
  battle replays follow the same rule.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes,
  `mods/` packs, `ModCache/` downloads and 0.8.12 settings files carry over
  unchanged. There are no new settings.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.8.13-validation.md).
