# TAK Engine 0.8.9

0.8.9 follows 0.8.8. Shift-drag build lines pack their buildings tightly and
the mouse wheel spaces them out; reclaim and lodestone boxes need a deliberate
drag and stay on the ground you pressed while the view moves; the in-game chat
box never covers the build menu or its TRAINING line; and the game setup screen
and the room's rules are laid out more evenly. Every change is in the client:
the game simulation, the network protocol (**288**) and the replay format
(**14**) are the same as 0.8.8's, so **0.8.9 plays with 0.8.8 and plays its
replays, both ways.** A server and its players may run either version, in any
mix; 0.8.7 or earlier still cannot play with either.

## Build lines

- **Tight by default.** A Shift-drag line of walls, towers or any other
  building now packs its buildings edge to edge, with no spare cell between
  them (corner to corner on a diagonal). No two sites share a cell, and every
  site in the line is built.
- **The mouse wheel sets the gap.** While you hold Shift and the left button,
  turn the wheel to space the line out, one empty cell per notch, up to 12
  (wheel down tightens it again, back to none). The camera does not zoom
  meanwhile. A small **GAP** label by the pointer shows the spacing.
- **The gap is kept.** The spacing you choose holds for every later line, of
  any building, for the rest of the game. Each new game starts tight again; the
  gap is never saved.
- **Builders no longer wall themselves in.** The builder works along the line
  from one side. When the next site's natural approach is blocked by the
  building it just made, or would take it round to the far side, it first
  steps round to its own side (you may see an extra waypoint in its queue), so
  it never stalls between sites.
- **Lines chain.** A line dragged right after another (an L, a U, a closed box)
  plans on from where the builder's queued work leaves it, even before the
  first line's orders have come back from the server. A site that would sit
  half on top of one already queued for that builder is left out, since it
  could never be built.
- A trackpad or high-resolution wheel changes the gap a few cells per swipe
  instead of running it to the end stop.

## Reclaim and lodestone boxes

- **A box has to be meant.** A right-drag with a reclaiming builder (reclaim
  an area) and a lodestone box now need a drag of at least about 32 pixels
  and a box at least 32 pixels wide and 32 tall (at 100% UI Scale; the size
  grows with UI Scale). Anything smaller or thinner is the ordinary click:
  a right-click order, or a single lodestone.
- **A click acts where you pressed.** A small twitch of the mouse while
  clicking no longer moves the order off what you clicked.
- **The preview tells you.** The orange box and the broom cursor appear only
  once releasing would give an area order.
- **Boxes stay on the map.** The reclaim box, the lodestone box and the
  selection box keep their first corner on the ground you pressed when the view
  scrolls (edge scroll, keys, the minimap) or zooms during the drag, and the
  order or selection covers what the box shows. A box stretched by edge
  scrolling counts as a box. The original game also kept its drag boxes on
  the map.

## Chat box

- The in-game chat box always sits above the build menu's icons and the line
  over them (such as **TRAINING**), at every Chat Position, Chat Scale, UI
  Scale and Build Menu Scale, whether you are reading or typing.
- With no builder selected the box stays where it would be with one, so it
  does not jump when you select a builder.
- In a small window at a large UI Scale it keeps room for at least two lines
  and the line you are typing.

## Game setup and room

- **Setup screen.** The match option buttons are all one width (300 pixels)
  and every caption is drawn at one size, in both UI fonts, so none sticks out
  of the column or changes size as you cycle it.
- **Room rules.** **MAP SCENARIO: ON / OFF** is now a rules line like the
  others, at the same size, at the bottom of the right column (under SPECTATE
  in single player, ALLOW SPECTATORS in multiplayer). What a scenario changes
  and which seats it needs follow under both columns of rules.
- **Game mods status.** **GAME MODS VERIFIED** appears only when game mod packs
  are in play. A room with game mods on but no game pack ticked, such as a
  single-player room in an install without game packs, no longer shows it.
  Transfer progress and errors show as before.

## For contributors

- New tests: `area_drag` (the box rules), `area_drag_input` (real mouse input
  through the client, with `TAK_TEST_DATA`, Debug only), `buildline` (line
  geometry, 7 footprints in 72 directions, gaps 0-4, wheel steps),
  `buildline_harness` and the `buildline_chain_{u,box,tower_v,double_back}`
  chained-line cases (real mouse and wheel input; every site built; with
  `TAK_TEST_DATA`, Debug only). `chatbox` and `mod_packs` gained cases.
- New pure headers: `src/client/areadrag.h` (map-anchored drag boxes and the
  32-pixel test) and `src/client/buildline.h` (line sites and wheel steps).
  `chatbox::hudFloor` / `minRoom` place the chat box over the bottom HUD band,
  and `MpClient::roomModStatus` (`src/net/client.h`, header only) chooses the
  room's game-mods line.
- Retail finding: drag boxes are world-anchored and the box test
  (`KINGDOMS.icd` `0x527bf0`) is 32 world pixels on either axis or a press
  held 25 game-clock units; see
  [retail-engine.md](retail-engine.md#drag-boxes-world-anchored-corners-and-the-32-pixel-test-icd-2026-10-21).
  This engine asks for 32 pixels on both axes and drops the held-time rule.
- Debug hooks: `TAK_AREA_DRAG_TEST`, `TAK_BUILDLINE_TEST`,
  `TAK_BUILDLINE_CHAIN`, `TAK_BUILDLINE_PROBE`, `TAK_BUILDLINE_SHOT`,
  `TAK_BUILDLINE_NET`.
- Build lines are made of the existing Build and Move orders. Nothing under
  `src/sim/`, `src/server/` or `src/ai/` changed, and `src/net/` only by the
  display helper above.
- The `--mpai` Inner Circle seed 1 hashes are unchanged from 0.8.8: Retail
  60 s `b353410003e98dd5`, 300 s `a3d953803a064191`; Legion 60 s
  `0825aeb9e8139203`, 300 s `ce5c4ba7562e53b9`.
  <!-- LEAD: confirm on the tagged source on Linux, Windows and macOS. -->

## Known limits

All of 0.8.8's known limits carry over unchanged, since the simulation is
0.8.8's:

- **Legion units still stand and wait more than Retail's**, in crowds, at
  corners and in rings, though they usually arrive sooner. This is cosmetic.
- **Legion attackers through a narrow gate deal more damage than Retail's**:
  in the test layouts from about 1.2 to 2.2 times as much, with more units in
  reach.
- **Power codes go through the game simulation like orders**, so their effects
  are part of every player's game (and of replays). The original applied a
  code only on the computer of the player who typed it. The codes that act on
  the typer (+ATM, +ManaMe, +LOS, +Mapping, +NowISee) still act only for that
  player; +DoubleShot and +HalfShot change everyone's damage.
- **Fog of war: Full Vision does not switch line of sight off at the start.**
  Your units do not attack what they cannot see until **+LOS** or **+NowISee**
  is typed.
- **UI Font: Classic is close to, but not pixel-identical with, 0.8.7's
  interface.**
- **The campaign book's chapter numeral** still touches the bottom of
  "APTER" in CHAPTER, in both UI fonts, as in the original game (its lettering
  boxes overlap).
- **Legion bridges are still a lottery at about 100 units**: two armies meeting
  at a narrow, tapering bridge can jam in its mouth and take far longer than
  Retail to cross.
- A stationary unit choosing its own targets can keep ignoring an enemy it once
  found blocked. The Legion 10,000-unit workloads still struggle with
  congestion and the largest run over the tick budget; in mixed-10000 the
  flyers hover over a slow ground army. Moving the game window between a
  Retina and an ordinary display does not rescale UI Scale. In the remote test
  sweep, Retail `h-cramped` ends early because the idle seat starts with only
  its Monarch.

## Compatibility

Protocol **288**, replay format **14**, generator version **9**, campaign
payload **4** and Crusades SQL schema **9**: all unchanged from 0.8.8. Checked
in source against `v0.8.8`: `kNetVersion` in `src/net/protocol.h`,
`kReplayFormat` in `src/net/replayhdr.h`, the map generator's `formatVer`
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` (`src/net/crusades.h`)
and `kSchemaVersion` (`src/server/crusades/store.cpp`).

- **Multiplayer.** A server accepts a client whose protocol equals its own, so
  **0.8.9 and 0.8.8 clients and servers play together in any mix**. 0.8.7 or
  older clients still cannot join a 0.8.9 or 0.8.8 server, nor can 0.8.9
  clients join an older one.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.8.9 plays 0.8.8's replays and 0.8.8 plays 0.8.9's. Replays
  from 0.8.7 or earlier still need the version that recorded them. Crusades
  battle replays follow the same rule.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes,
  `mods/` packs, `ModCache/` downloads and 0.8.8 settings files carry over
  unchanged. There are no new settings: the build-line gap is never saved.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.8.9-validation.md).
