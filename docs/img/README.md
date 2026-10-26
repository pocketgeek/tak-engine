# README and user-guide screenshots

**Retaken for 0.9.3** (2026-10-26): `title.jpg` only, by the 0.9.3 build (Debug
with `-O2`, CMake `VERSION 0.9.3` in the release commit), with the 0.9.1 title
capture (`takclient menu --shot`, offscreen video driver, a 1920 × 1080 window,
the 0.9.1 scratch install and settings; script in
`~/tak-tmp/scratch/release-093/shots/`), cropped to the artwork as before. It
reads VERSION 0.9.3 and differs from the 0.9.2 file only in the last digit of
that line. 0.9.3 changes which shots arcing weapons may fire and how macOS
streaming connects; no picture shows either, so no other picture was retaken.

**Earlier: retaken for 0.9.2** (2026-10-25): `title.jpg` only, by the 0.9.2 build (Debug
with `-O2`, CMake `VERSION 0.9.2` in the release commit), with the 0.9.1 title
capture (`takclient menu --shot`, offscreen video driver, a 1920 × 1080 window,
the 0.9.1 scratch install and settings; script in
`~/tak-tmp/scratch/release-092/shots/`), cropped to the artwork as before. It
reads VERSION 0.9.2 and differs from the 0.9.1 file only in the last digit of
that line. 0.9.2 changes how flames and lobbed shots are judged and where the
computer players build; no picture shows any of that, so no other picture was
retaken.

**Retaken for 0.9.1** (2026-10-25): three pictures, by the 0.9.1 build (Debug
with `-O2`, CMake `VERSION 0.9.1` set locally ahead of the release commit), with
the 0.9.0 front-end capture scripts (copied to
`~/tak-tmp/scratch/release-091/shots/`). `title.jpg` reads VERSION 0.9.1.
`guide/options-graphics.jpg` shows the RENDERER row as **Auto (OpenGL)** (Auto
now names the backend it is running) and the new note beside HARDWARE CURSOR,
**OS CURSOR 16X25** (the size the game gave the system at Cursor Size 1×). The
menu's still capture runs before the menu loop, which is where the cursor is
first handed to the system, so the capture-only patch makes that same call
before drawing the Options screen; the offscreen video driver accepts it.
`streaming.jpg` was retaken too: an idle panel still reads OFF, so it differs
from 0.9.0 only by the VERSION 0.9.1 line faintly visible behind the panel.
0.9.1's new failure and reconnect reasons (for example `FAILED - CANNOT CONNECT -
CHECK KEY, NETWORK`) appear only after a real start, and the panel always
connects to YouTube, so no picture shows them. No other picture's scene or
caption changed in 0.9.1 (its other changes are aiming, factory exits, streaming
speed and the Windows renderer choice, none of them shown in a picture).

**Retaken for 0.9.0** (2026-10-24): all 81 pictures on the 0.9.0 shot list
(`SHOTLIST-0.9.0.md`, deleted once they landed) were retaken by the 0.9.0 build (Debug
with `-O2`, CMake `VERSION 0.9.0`), on three branches: the Cartographer
pictures, the front-end and multiplayer pictures, and the in-game pictures.
`title.jpg` reads VERSION 0.9.0, and three pictures are new: `mp-connect.jpg`,
`mp-connect-custom.jpg` (both from the menu's debug server-panel capture; no
server was contacted) and `guide/game-browser.jpg` (a private loopback
`takserver`, never the public one). Fourteen retakes came out byte-identical to
the files they replace, because nothing in those scenes changed. Each picture's
row below says how it was made; the sections that follow still describe the
earlier rounds.

Refreshed on 2026-10-24 for **0.8.14**. The always-on cosmetic mods moved
from `.hpi` archives directly in `mods/cosmetic/` (no longer loaded) to pack
folders in `mods/cosmetic/always_enabled/<pack>/`, `mods/game/always_enabled/<pack>/`
was added, and the multiplayer create screen lost its GAME MODS: ON/OFF option.
The 0.8.14 build (version set to 0.8.14 locally, ahead of the release commit; no
capture patch) retook `title.jpg` (VERSION 0.8.14) and every picture that named
the old always-on archive: `room.jpg`, `guide/room-mods-power-codes.jpg`,
`campaign.jpg`, `guide/campaign-briefing.jpg` and `cartographer/test-lobby.jpg`,
which now read **ALWAYS ON: click** (and *Always-on mods: click*). The scratch
install holds the click archive at `mods/cosmetic/always_enabled/click/click.hpi`
and the ordinary packs *Alternate Cursors*, *Classic Sounds* (cosmetic) and *Big
Battles* (game), each a folder with one tiny placeholder sound file; for
`guide/room-mods-power-codes.jpg` a second scratch install also has the
always-enabled game pack *House Rules* (`mods/game/always_enabled/House
Rules/`, one placeholder file), so both kinds show an ALWAYS ON line;
`cartographer/test-lobby.jpg` used an install with only the click pack. No
retail or game files were added to any pack, and none is committed. Checked by
eye and kept: `quick/lobby-move-here.jpg` (its install had no packs, so it shows
no ALWAYS ON line and nothing in it changed) and `lobby.jpg`,
`lobby-scenario.jpg` and `guide/random-map-lobby.jpg` (the single-player create
screen, which never had a GAME MODS option). No shipped picture shows the
multiplayer create screen, so none showed its old GAME MODS option.

**0.8.13** (2026-10-24): the 0.8.13 build (version set to
0.8.13 locally, ahead of the release commit; no capture patch was needed for the
game) retook `title.jpg` (VERSION 0.8.13, since retaken for 0.8.14) the same way as for 0.8.12; only the
version digits differ from the 0.8.12 file. Cartographer now opens to an empty
window, so `cartographer/new-map.jpg` was retaken (the New Map dialog opened with
File → New map over that window, where it used to sit over a placeholder map) and
`cartographer/empty-window.jpg` was added; see the Cartographer section below.
0.8.13 also drops the in-game text banner shown while an order is armed (ATTACK:
CLICK TARGET and the like); no picture showed it (every game, guide and quick-start
picture was checked by eye where the banner drew, under the top-left corner), so
none was retaken for it.

**0.8.12** retook `title.jpg` (VERSION 0.8.12, since retaken for 0.8.13) the same
way as for 0.8.11. No other picture showed what 0.8.12 changed. The lobby and room pictures show no AI seat
at level 6, so the RETAIL name does not appear in them (see below). The campaign
order hint (ATTACK: CLICK TARGET...) then sat below the objectives panel (0.8.13
removed the hint), but no picture showed an order armed in a mission: `guide/campaign-briefing.jpg` shows
the paused briefing with the objectives button only. The Shift-queued flyer
patrol has no picture. The Cartographer AI Profiles pictures
(`cartographer/ai-profiles.jpg`, `ai-group-rules.jpg`, `ai-takengine.jpg` and
`ai-mod.jpg`) were taken on `main` before the release with the final window
text (one TAK ENGINE AI tab, the AI mod footer that says the install never
supplies `ai/takengine.txt`) and were checked by eye again; they are kept.

**0.8.11** added the **Opponent AI** line
to the campaign book's left page, wraps a build menu too long for one row into
more rows left of the command panel, and drops the blue production bar over a
factory or conjurer. The 0.8.11 build (version set to 0.8.11 locally, ahead of
the release commit; no capture patch was needed) retook `title.jpg` (VERSION
0.8.11) and `campaign.jpg`, and added `guide/build-menu-wrap.jpg`. When campaigns
were switched to default to Classic opponents (still before the 0.8.11 tag),
`campaign.jpg` was retaken the same way and now reads OPPONENT AI: CLASSIC (A),
the new default; nothing else in the picture changed (a Normal capture of the same
build matched the earlier NORMAL picture pixel for pixel). When the line was removed
again (campaign opponents are always Classic, still before the tag), `campaign.jpg`
was retaken without it (`takclient menu --shot`, offscreen video driver, a scratch
install with `mods/cosmetic/click.hpi`, a fresh profile with only `fullscreen = 0`). No picture showed the blue production bar (the
game pictures with AI or player bases were checked by eye; the spawned scenes
produce nothing), and the lobby and room pictures show each AI seat's level but
not the list of levels, so CLASSIC does not appear in them; they keep their
earlier captures. When level 6's lobby name became DEFAULT (still before the
tag), the lobby and room pictures were checked again by eye (`lobby.jpg`,
`lobby-scenario.jpg`, `room.jpg`, `guide/random-map-lobby.jpg`,
`guide/room-mods-power-codes.jpg`, `cartographer/test-lobby.jpg`, and the
in-game `diplomacy.jpg` and `guide/scorecard.jpg`): every AI seat shown reads
NORMAL and none shows level 6 under either name, so none was retaken
(`options-interface.jpg`'s CLASSIC is the UI font, not the AI).
When it was renamed RETAIL after 0.8.11, the same pictures still show no level-6
seat, so none was retaken.
`guide/chat-box.jpg` shows a Barracks training Horsemen, but
its crop leaves out the Barracks; its information bar's Horseman gauge now
follows the new unit's health instead of build progress, a small early fill
either way, so it is kept too (retaking it needs the 0.8.9 capture patch).

0.8.10 added the **SAVE A LOCAL COPY**
row to the YouTube Streaming panel and changed the disconnect wait (each seat has
one 30 s budget per game, counted from the player's last contact, so a first stop
shows 22 when the banner appears). The 0.8.10 build (version set to 0.8.10
locally, ahead of the release commit; no capture patch was needed) retook
`title.jpg` (VERSION 0.8.10), `streaming.jpg` and `guide/disconnect-wait.jpg`.
No other picture shows what 0.8.10 changed: the campaign pictures show chapter 1,
whose human player was already Player1, and the Cartographer Test Map fix is in
the macOS app bundle only. Every other picture keeps the release named below.

0.8.9 changed the create screen's option
column (one button width and one label size), the room's rules (MAP SCENARIO is
a normal rules line; GAME MODS VERIFIED shows only when game packs are in play)
and the in-game chat box (always above the build row and its TRAINING or status
line), and added Shift-drag build lines with a wheel-set gap. The 0.8.9 build
(version set to 0.8.9 locally, ahead of the release commit) retook every picture
that shows them: `title.jpg` (since retaken for 0.8.10), `lobby.jpg`, `lobby-scenario.jpg`,
`room.jpg`, `cartographer/test-lobby.jpg`, `guide/random-map-lobby.jpg`,
`guide/room-mods-power-codes.jpg` and `guide/chat-box.jpg`, and added
`guide/build-line.jpg`. `diplomacy.jpg`, `guide/options-interface.jpg` and
`guide/options-graphics.jpg` were re-rendered and compared with their 0.8.8
captures: nothing they show changed, so they stay the 0.8.8 files.

Every image below is actual engine output from a release-preparation build
(Debug with `-O2`, OpenGL renderer) and was inspected by eye. 0.8.8 draws all plain interface text in Noto Sans Bold
(Options → Interface → **UI Font**, default Modern), so every picture with
interface text was re-rendered by the 0.8.8 build, with the default UI Font:

- menus and rooms: `title.jpg` (VERSION 0.8.8), `lobby.jpg`,
  `lobby-scenario.jpg`, `room.jpg`, `campaign.jpg`, `streaming.jpg`,
  `results.jpg`, `cartographer/test-lobby.jpg`, `guide/random-map-lobby.jpg`,
  `guide/options-graphics.jpg` and `guide/voice-chat.jpg`;
- the game pictures: `battle.jpg`, `dragons.jpg`, `flyers.jpg`, `hud.jpg`,
  `naval.jpg`, `generated.jpg`, `diplomacy.jpg` and, in `guide/`,
  `hud-annotated.jpg` (markers redrawn), `selection-strip.jpg`, the five
  `side-*.jpg`, `shift-queue.jpg`, `flyers-retail-legion.jpg`,
  `fire-on-the-move.jpg`, `campaign-briefing.jpg`, `replay.jpg`,
  `benchmark.jpg` and `scorecard.jpg`. Their scenes are the same runs as
  before; the simulation places units a little differently than in the older
  pictures;
- new: `guide/chat-box.jpg`, `guide/options-interface.jpg` and
  `guide/room-mods-power-codes.jpg`.

Kept: `guide/tactical-dots.jpg` (0.8.5;
no interface text), `cartographer.png`, `cartographer-model.png`,
`guide/cartographer-terrain.jpg` and the other `cartographer/` pictures (the
editor does not use the UI font), and `quick/` (its own section).

The 0.8.5 to 0.8.8 captures read the user's own retail install, read-only, through a
scratch folder that links its archives, `Maps`, `Movies` and `Music` and has
its own empty `MapCache` and `ModCache`. For 0.8.8 its `mods/` holds the shipped
always-on `mods/cosmetic/click.hpi`, two empty cosmetic packs (*Alternate
Cursors*, *Classic Sounds*) and one empty game pack (*Big Battles*) for the
room's pack lists, so nothing was written into the install. Earlier captures
used a copy of the same retail archives. Encoding is
the only change after capture: JPEG with full chroma resolution, quality 90 for
the title, 88 for the gallery and 86 for the guide; guide images are scaled from
1600 × 900 to 1280 × 720 (`guide/scorecard.jpg` from 1920 × 1080). `guide/options-graphics.jpg` (from a 1600 × 1100
window, 840 × 1024) and `guide/random-map-lobby.jpg` (from 1600 × 1000,
1500 × 900) are cropped, not scaled. The title is cropped to its 4:3 artwork,
`guide/selection-strip.jpg` to the information bar, and `guide/voice-chat.jpg`
to the Voice Chat panel (832 × 651, not scaled), `guide/chat-box.jpg` to the chat box, build row and information bar (1240 × 340 from 1600 × 900) and `guide/options-interface.jpg` to the Interface section (824 × 455 from 1600 × 1100). `cartographer.png` and
`guide/cartographer-terrain.jpg` are the editor's own `--shot` output at its
921 × 691 default window; the pictures in `cartographer/` come from the capture
driver described below. Nothing was painted into a scene. The guide's HUD picture has
numbered markers drawn on top, and the Retail/Legion and Tactical Dots comparisons
put two captures side by side under text labels. The Cartographer guide's pictures
are described in [their own section](#cartographer-guide-docsimgcartographer).

Several scenes were set up with debug-build harnesses. Release builds do not have
these switches. Captions say when a picture is a **development scene**:

- **Stress-test start.** Each AI or player begins with about 95% of the unit cap
  in combat units, in a grid. This is the debug `TAK_STRESS` fixture.
- **Gods on.** Gods appear at the start. This is the debug `TAK_GODS` switch.
- **Spawned scene.** The local harness places units and gives the move order
  itself. There is no server, and it is not a real match.
- **Scripted orders.** The debug harness gives the player's orders through the
  game's own selection and right-click order code, as a click would.
- **Two-player spawned scene.** As a spawned scene, but a second block of units
  is placed for an enemy player (player 1), which has no AI and gives no orders.
- **Selection still life.** The committed debug `TAK_SELPORTRAITS` fixture
  (with `--testbuild`) spawns a mixed roster at the map centre and selects it;
  `TAK_SELPORTRAITS_HURT` lowers some units' health and mana and
  `TAK_SELPORTRAITS_SQUAD` puts them in one group.

A capture-only patch, not committed, added camera, selection and overlay
switches. It also added the larger naval fleet and the spawned-scene switch
(since 0.7.28 with an optional enemy block; since 0.8.0 a Legion scene also
turns on the path service, as match setup does, without which Legion flyers had
no stations). The larger naval fleet, dropped from the patch after 0.7.26, was
added back for 0.8.1. For 0.8.4 it also opens the Voice Chat page for a menu
capture (`--shot` builds no voice engine, so no microphone is opened and the
level meter is empty) and leaves the diplomacy fixture with a voice engine that
is on, so the VOLUME sliders draw live instead of greyed. For 0.8.5 the same
patch was applied unchanged; for 0.8.6 and 0.8.7 too, less its version bump
(the 0.8.7 capture build set its version to 0.8.7 locally, ahead of the release
commit). For 0.8.8 (version set to 0.8.8 locally) it also puts chat lines into
the in-game chat box with fixed player names and a typed SAY line, for
`guide/chat-box.jpg`. For 0.8.9 (version set to 0.8.9 locally) it also starts a
Horseman training queue on the selected Barracks of the selection still life,
puts the same named chat lines in from there, and lets the chat box draw in that
serverless fixture (the game draws it only with a server). It changes no
simulation code. Every scene runs the normal game rules, AI and renderer.

## Gallery (`docs/img/`)

| Image | Scene |
| --- | --- |
| `title.jpg` | Title menu showing version 0.9.3 (0.9.3 build), 1920 × 1080 window cropped to the 1440 × 1080 artwork |
| `mp-connect.jpg` | New in 0.9.0. The Multiplayer door's CONNECT TO SERVER panel over the title menu: the official TAK Engine server, tak.pgnet.us (marked DEFAULT), selected at the top of the list, two sample remembered servers the debug `TAK_SHOT_SERVER` capture seeds into an empty list (192.168.1.50:7677 and example.dyndns.org), CUSTOM..., the account name *aldric* typed and a ten-character password shown as pips, the A NEW NAME IS REGISTERED WHEN YOU SIGN IN note, CONNECT and BACK. `TAK_SHOT_SERVER=4` with the name and password set by the capture-only patch; nothing connected to any server. 1600 × 900, cropped to the panel (600 × 494, not scaled). Rendered by the 0.9.0 build |
| `mp-connect-custom.jpg` | New in 0.9.0. The same panel with CUSTOM... selected (`TAK_SHOT_SERVER=2`): the address box below the list holds *tak.example.org* (typed by the capture-only patch), the account name *aldric*, the password empty. Nothing connected. 1600 × 900, cropped to the panel (600 × 550, not scaled). Rendered by the 0.9.0 build |
| `battle.jpg` | **Benchmark** run (Settings → Benchmark, Absurd intensity) on Ulasem Arena, 55 game seconds in. Eight server AIs fight, all five sides. The camera, at 1.3× zoom, shows Aramon (magenta) and Taros (yellow) armies, with Black Dragons, clashing at a sandy arena; damaged units of every side show health bars. A real feature with no fixture; spectator view, so no fog. Rendered by the 0.9.0 build |
| `dragons.jpg` | **Development scene.** Five server AIs, one per side, from a stress-test start (300-unit cap) with gods on, 90 game seconds in. Creon Neo Dragons breathe fire on an Aramon siege column; health bars show for every side. Spectator view, 1.4× zoom. Rendered by the 0.9.0 build |
| `hud.jpg` | Aramon player in a local-server game, about 10 s in. **Stress-test start**: Normal AI opponent, 250-unit cap. **Scripted orders**: 45 units of eight types (Archers, Swordsmen, Knights, Catapults, Mage Archers, Cannoneers, Horsemen, Rolling Towers) were made formation 1 and selected, so the information bar shows the portrait strip (one portrait per type with its count), FORMATION 1 and 45 SELECTED, and the selected units show `1F` in solid green. Stats panel on, 1.15× zoom; its VID row reads 117 MB with the default graphics settings, and its CLOCK row is the capture machine's wall clock. Rendered by the 0.9.0 build |
| `naval.jpg` | **Development scene, spawned.** Cairbray Coast Landing, 10 s in. Veruna Flagship, Men of War, Trebuchet Ships and Harpoon Ships fight Creon Iron Clads, Stern Wheelers and Submersibles. Nothing is selected, so the information bar is empty. 1.6× zoom, fog off. Rendered by the 0.9.0 build |
| `generated.jpg` | Generated Aramon Riverlands map "Silverford" (seed 7, 256 × 256, four players, Scattered Mana Medium). Four Hard AIs at 8× speed, seven game minutes in. Spectator view of the lake shore at 1×, with an AI's lodestone on a neutral scattered mana spot among standing stones in the clearing. Rendered by the 0.9.0 build |
| `lobby.jpg` | Single-player create screen with Ulasem Arena selected (the title from the map's translate table, in the list and on the MAP line): the preview numbers the map's eight start positions and reads 20 X 20 8 PLAYER, MAP SCENARIO under it is greyed (THIS MAP HAS NO SCENARIO), and POWER CODES: OFF is among the options. Retail pathfinding, other options at their defaults. Rendered by the 0.9.0 build, 1600 × 1000 (see *Lobby captures* below) |
| `lobby-scenario.jpg` | Single-player create screen on King of the Hill with **MAP SCENARIO** turned on: the gold **S** marks the maps with a playable scenario in the list, the lines under the button say what the scenario changes, Monarch Expendable and Start Locations are greyed, and the preview numbers the four start positions. 1024 × 768, cropped to the lobby panel; the map row and the button were clicked with `TAK_SHOT_CLICKS`, then the pointer moved off the panel. Rendered by the 0.9.0 build. Used only by the user guide |
| `room.jpg` | Single-player room on Ulasem Arena with three Normal AIs (Taros, Veruna, Zhon) added by clicks, Crusades and Double Sight on, Legion pathfinding and both example cosmetic packs ticked (placeholder folders in the scratch install); under the COSMETIC MODS heading ALWAYS ON: click (the always-enabled pack folder), then the pack names, the GAME MODS list below (no game pack ticked and no always-enabled game pack, so no GAME MODS VERIFIED line). The open rows 5 to 8 each have a **MOVE HERE** button. The match rules end with MAP SCENARIO: OFF. The map at the top of the right column marks starts 1 to 4 in the colours of the four seated players. The AI names are the lobby's own random picks. Rendered by the 0.9.0 build, 1600 × 1000; the CREATE screen's toggles, slot types, sides and pack boxes were clicked with `TAK_SHOT_CLICKS`, then the pointer moved off the panel |
| `campaign.jpg` | Book of Darien campaign picker with a fresh profile, chapter 1. The CHAPTER heading and numeral are retail lettering; the chapter title and the always-on mods note (click, the always-enabled pack folder, in dark ink on the parchment) are in the UI font. No Opponent AI line: missions always play the Retail AI. Rendered by the 0.9.0 build, 1600 × 1000, with `TAK_SHOT_CAMPAIGN=0` |
| `streaming.jpg` | Streaming setup with no key or broadcast, showing the **Remember Key** row (off; the Secret Service is available) and the **Save a Local Copy** row (off, the default; MP4 IN THE VIDEOS FOLDER); the status reads OFF. Rendered by the 0.9.1 build at 1600 × 1000, with the debug `TAK_CREDSTORE=test` store so no real key could appear |
| `diplomacy.jpg` | Built-in input-test fixture (the debug `TAK_DIPLOMACY_TEST` harness, `--testbuild`) after gifting, 1600 × 1000, in a profile with voice chat on (capture-only patch: the fixture keeps a voice engine that is on, so the sliders draw live). The VOICE column ticks every other player (your own box is shown, greyed); VOLUME has sliders for the two other humans, left by the fixture at 50% (ALLY) and 25% (DEFEATED), and none for the computer player (ENEMY). Elsin stays selected, so the bar shows her portrait, name and health and mana only. Rendered by the 0.9.0 build |
| `results.jpg` | Built-in five-player **sample statistics** (`TAK_SHOT_RESULT`), not a finished match. Hardware cursor on, so no pointer is drawn. Rendered by the 0.9.0 build |
| `cartographer.png` | Cartographer Units tab on Ulasem Arena: the five-tool toolbar (TERRAIN, FEATURES, UNITS, STARTS, BLOCKED), the browser with its scrollbar, and the minimap. A fresh editor profile, `--shot --shot-tool units`, 30% zoom. Retaken by the 0.9.0 build; the output is byte-identical to the 0.8.3 picture, which already had the five tools |

The 0.7.25 `army.jpg` (an Aramon army standing still, with no combat) and
`gameplay.jpg` (a single barracks) were removed. `battle.jpg` and `hud.jpg`
replace them. The unchanged views contain no version text.

## User guide (`docs/img/guide/`)

| Image | Scene |
| --- | --- |
| `hud-annotated.jpg` | From the 0.9.0 build. The `hud.jpg` setup with Elsin alone selected, so her build menu shows, captured at 1600 × 900 with 1× zoom and scaled. Markers (redrawn): 1 minimap, 2 stats panel, 3 orders, 4 weapons and stances, 5 mana, 6 build menu, 7 information bar, 8 selected Monarch. The bar shows only her portrait, name, health and mana and STANDBY. At this window height the stats panel has room for its first four rows only (FPS, PING, SIM, UNITS) |
| `selection-strip.jpg` | **Selection still life.** Ulasem Arena, `--testbuild` with `TAK_SELPORTRAITS=arasword:9,araarch:6,arabow:4,arapal:3,arapult:2`, `TAK_SELPORTRAITS_HURT` and `TAK_SELPORTRAITS_SQUAD=3`, 1920 × 1080 at UI Scale 150%, cropped to the left of the information bar (1160 × 110, not scaled). Five full-size type portraits with counts, GROUP 3, the group's partial health and mana totals and 24 SELECTED. Rendered by the 0.9.0 build |
| `side-aramon.jpg` … `side-creon.jpg` | One per side, rendered by the 0.9.0 build. A local-server game with a **stress-test start** (250-unit cap, Normal AI opponent), about 10 s in, 1600 × 900, scaled. The player's Monarch is selected: Elsin, Lokken, Kirenna, Thirsha or the Sage. Each shows that side's interface and build menu |
| `shift-queue.jpg` | From the 0.9.0 build. Elsin with a move and three Shift-queued moves (**scripted orders**), Shift held, about 3 s after the orders; the status reads MOVING. 1.7× zoom |
| `flyers-retail-legion.jpg` | **Development scene, spawned.** From the 0.9.0 build (both halves). A spawned Zhon formation of ground units and flyers (`TAK_CAP_SCENE`) given one formation move order, 22 s in, run once in Retail (flyers far ahead) and once in Legion (flyers over the leading ground units). The same 1.05× camera is used for both, and the two captures are placed side by side with labels |
| `options-graphics.jpg` | 0.9.1 build. Options screen in a 1600 × 1100 window, scrolled so that Display and the whole Graphics section show (the last Audio rows above, the Interface heading and UI FONT below), cropped to the panel: Fullscreen (OFF), VSync (ON), Max FPS (60, greyed out because VSync is on), Renderer (Auto (OpenGL): Auto, and the backend it started), Texture Filtering at EVERYTHING, Shadows, Smooth GUI Art, Smooth Movies, Trees Sway in Wind and Tactical Dots (ON), Tactical Dots Zoom (0%), Screen Shake (OFF), Hardware Cursor (ON, with the note OS CURSOR 16X25 beside it) and Smooth Motion (ON) and GPU Memory Limit at 1.25 GB (default). A fresh profile whose only setting is `fullscreen = 0`. The capture-only patch hands the cursor to the system before drawing, as the menu loop would, so the note shows. Linux build, so the Windows-only Ignore Windows Display Scaling row is absent |
| `fire-on-the-move.jpg` | **From the 0.9.0 build. Development scene, two-player spawned.** Ulasem Arena, Retail pathfinding, 14 s after one plain move order east (**scripted orders**). 14 Aramon Archers and 2 Mage Builders were spawned in a 4-wide grid; 6 Taros Zombies and 6 Skeleton Archers (enemy, no AI) stand south of their path. Shift is held, so the move line shows. Zombies have reached the back of the column, which keeps walking and shooting; the flashes are hits, and three of the sixteen have fallen, so the information bar shows two portraits (2 Mage Builders, 11 Archers) and 13 SELECTED. 1.4× zoom, fog off |
| `cartographer-terrain.jpg` | Cartographer Terrain tab on Ulasem Arena (`--shot --shot-tool terrain`, fresh editor profile, 921 × 691), retaken by the 0.9.0 build (byte-identical to the 0.8.3 picture): the grouped section browser, All sections (555 pieces, 1261 sections), with readable piece names, variant badges and the scrollbar, and five tools on the toolbar |
| `tactical-dots.jpg` | From the 0.9.0 build. **Benchmark** run (Absurd) on Ulasem Arena, 50 game seconds in, spectator view fully zoomed out (zoom 0.1875, the floor for this map in a 1920 × 1080 window), where the default Tactical Dots Zoom of 0% shows dots. The deterministic run was rendered twice at the same tick and camera, with Tactical Dots off and on in otherwise identical profiles. The same 800 × 900 region (the east of the map: the blue and red armies) is cropped from each at 1:1 and placed side by side under labels. With models, fire and spell effects show among the armies; with dots none are drawn |
| `campaign-briefing.jpg` | *Book of Darien* chapter 1 briefing over the paused battlefield, from a direct mission launch (`TAK_SHOT_BRIEFING`, 1600 × 900, scaled), by the 0.9.0 build: the PAUSED heading is retail art, the chapter, title and objectives are in the UI font, and the last line names the always-enabled cosmetic pack (Always-on mods: click). Nothing is selected, so the information bar is empty |
| `replay.jpg` | Replay playback of a recorded local-server game, 9 s of 16 s, with the time bar. **Stress-test start**: an Aramon army given one Legion move order (**scripted orders**). Recorded and played back by the 0.9.0 build |
| `benchmark.jpg` | From the 0.9.0 build. Benchmark run (Absurd), 55 s in; the BENCHMARKING badge counts down the remaining time (0:05 of 60 s): armies meet at the edge of a sandy clearing, with health bars for every side; scaled from 1920 × 1080 |
| `scorecard.jpg` | From the 0.9.0 build. F4 scorecard over a benchmark battle (Absurd), 55 s in, at 1.1× zoom: eight AI players with kills, losses and score; captured at 1920 × 1080 (so the BENCHMARKING badge clears the Score column) and scaled |
| `voice-chat.jpg` | Recaptured by the 0.9.0 build. The Voice Chat page opened from the menu (capture-only patch), 1600 × 900, cropped to the panel. The capture profile with `voiceEnabled = 1`; every other voice setting is at its default: TALK MODE PUSH TO TALK, PUSH TO TALK on SPACE, SYSTEM DEFAULT microphone at 100, ACTIVATION LEVEL 15 (greyed out under Push to Talk), SAME AS GAME OUTPUT at 100. No voice engine runs under `--shot`, so MIC LEVEL is empty |
| `random-map-lobby.jpg` | Single-player create screen with **Generate Random Map** clicked, then SIZE clicked once (12 × 12), PLAYERS twice (4) and the SCATTERED MANA slider set to HIGH (`TAK_SHOT_CLICKS`, see *Lobby captures*). The preview reads MANA: 57 and SCATTERED: 33 NEUTRAL. Captured live at 1600 × 1000 with `TAK_SHOT_SIZES` by the 0.9.0 build, cropped to 1500 × 900 (from x 100), not scaled. The seed is the lobby's own random one |
| `chat-box.jpg` | Recaptured by the 0.9.0 build. The in-game chat box at its defaults (Chat Position Centered, Chat Scale 100%) sitting above the build row and its TRAINING Horseman (10) line, on Ulasem Arena, 1600 × 900, cropped to the box, the row and the information bar (1240 × 340, not scaled). **Selection still life** (`--testbuild`, `TAK_SELPORTRAITS=arakeep:1`): a Barracks selected, with a Horseman queue of ten started by the capture patch; the information bar's Horseman gauge follows the new unit's health. Four chat lines put into the box by the capture patch with fixed names (Ludwin, Aldric, Kirenna, Pat) in their player colours, and *on my way* being typed after SAY: |
| `options-interface.jpg` | 0.9.0 build. Options screen in a 1600 × 1100 window scrolled to the Interface section, cropped to it (824 × 455): UI FONT MODERN, F4 SCORECARD SCALE and UI SCALE 100%, HEALTH BARS DAMAGED, STATS PANEL ON, BUILD MENU CENTER, BUILD MENU SCALE 100%, CHAT POSITION CENTERED and CHAT SCALE 100%; a fresh profile, all defaults |
| `room-mods-power-codes.jpg` | Recaptured by the 0.9.0 build. Single-player room on Ulasem Arena, 1600 × 1000: POWER CODES clicked on in the create screen, two Normal AIs; under the COSMETIC MODS heading ALWAYS ON: click, then the packs (Classic Sounds ticked, Alternate Cursors not); under the GAME MODS heading ALWAYS ON: House Rules (an always-enabled game pack in the scratch install), then Big Battles ticked, so GAME MODS VERIFIED shows; the match rules read POWER CODES: ON, and MAP SCENARIO: OFF is a rules line. Clicked with `TAK_SHOT_CLICKS` |
| `build-line.jpg` | New in 0.8.9, retaken by the 0.9.0 build. A Shift-drag wall line in progress: the committed debug `TAK_BUILDLINE_SHOT=560,0,2` fixture (`--testbuild`, Ulasem Arena) selects Elsin, chooses the Aramon wall, holds Shift, presses the left button, drags 560 px east and turns the mouse wheel up two notches, so the preview shows nine wall sites two cells apart and the **GAP 2** label by the pointer. 1.5× zoom, fog off; 1600 × 900, scaled |
| `disconnect-wait.jpg` | Recaptured by the 0.9.0 build. Two humans on Inner Circle through a local `takserver --local --no-auth --seed 1` at the default 8 s stall threshold (data: a copy of the retail archives); the second client (Brannoc, headless) was stopped with `SIGSTOP` 40 s in, and the first (Aldric, 1600 × 900, scaled) was captured about 4 s after the banner appeared: the game is paused and reads WAITING FOR Brannoc 18 (a first stop's countdown starts at 22, as the 8 silent seconds are already spent). Nothing is selected. Capture script `~/tak-tmp/scratch/release-090/front/shots/dw.sh` |
| `game-browser.jpg` | New in 0.9.0. The online game browser of a client signed in as *aldric* to a private loopback `takserver --local` with a scratch account file (every name registered on first sign-in), never the public server. Four headless debug clients on the same server made the games (their names and the password set by the capture-only patch): *Friday Night Skirmish* (Ulasem Arena, 1/8, open), *Clan Practice* (King of the Hill, 1/4, password, so LOCK), *Evening Ladder* (Inner Circle, a host against one server AI, started, so 2/4 LIVE with WATCH) and *Newcomers Welcome* (Ladron's Tarn, 1/8). REFRESH and CREATE at the top, the empty PASSWORD (FOR LOCKED GAMES) field and BACK at the bottom; hardware cursor on, so no pointer is drawn. 1600 × 900, scaled. Capture script `~/tak-tmp/scratch/release-090/front/shots/browser.sh`. Rendered by the 0.9.0 build |
| `build-menu-wrap.jpg` | New in 0.8.11, retaken by the 0.9.0 build. A Zhon Beast Tamer's nine-icon conjure menu wrapped into two rows (five above four, centred, the container over both) left of the command panel and the mana orb. **Selection still life** (`--testbuild --side zon`, `TAK_SELPORTRAITS=zontrain:1`, local server), Ulasem Arena, a 1024 × 768 window at Build Menu Scale 150% (the menu fits one row at 100%), hardware cursor on so no pointer is drawn; not scaled |

The guide also uses `title.jpg`, `lobby.jpg`, `room.jpg`, `campaign.jpg`,
`diplomacy.jpg`, `generated.jpg` and `cartographer.png` from the gallery, and
`lobby-scenario.jpg` from the table above.


The first-launch folder picker is the operating system's own dialog
(`kdialog`/`zenity`, Finder or Explorer), so it has no in-engine screenshot.

## Cartographer guide (`docs/img/cartographer/`)

Added on 2026-10-14 for the [Cartographer user guide](../cartographer-guide.md),
from a build of `main` after 0.8.2 (Debug with `-O2`). Every picture is real
editor output, inspected by eye; the Test Map lobby is the game's.

**Retaken for 0.9.0** (2026-10-24): all 32 pictures, `cartographer.png` and
`guide/cartographer-terrain.jpg`, by the 0.9.0 build (Debug with `-O2`), the same
ways as described below: a new scratch capture driver (outside Git, linked
against the 0.9.0 editor objects like `cartographer_ui_test`) for the
driver-made pictures, the editor's own `--shot` for `blocked.jpg`, the
`cartographer_ui_test` workflows' capture mode for the AI Profiles pictures and
(with the same capture-only patch as for 0.8.13, not committed) for
`empty-window.jpg` and `new-map.jpg`, and the debug game client for
`test-lobby.jpg`. Every whole-window picture now shows the five-tool toolbar.
`window-annotated.jpg` has its markers redrawn for the five tools, and
`menus.jpg` shows the current menus, including File → New AI mod and Open AI
mod and Scenario → AI Profiles. Where the scene had not changed, the output came
out byte-identical to the earlier file (`empty-window.jpg`, `new-map.jpg`,
`terrain-variant.jpg`, `preferences.jpg`, `ai-mod.jpg`, `ai-group-rules.jpg`,
`ai-takengine.jpg`, `cartographer.png` and `guide/cartographer-terrain.jpg`).
Eight of Player 1's units are box-selected in `units.jpg` (seven before), and
the guide's caption says so.

**How they were captured.** A scratch capture driver, kept outside Git, links the
editor objects that the `cartographer_ui_test` target builds (the release editor
code; the test define only removes `main`) and drives the editor through its own
SDL input path from the frame hook, as a player's clicks and keys would: menu and
toolbar clicks, browser picks, drags, key presses and a warped pointer for hover
states such as previews and the status-bar name. Nothing was drawn into the
editor. Before each capture the driver waited until terrain thumbnails, unit
portraits and feature sprites had stopped loading and the terrain was fully
composed, so no browser cell or tree is a placeholder.

- **Window.** The SDL `offscreen` driver gives the editor's default window,
  921 × 691, as for `cartographer.png` (UI scale about 102%). Pictures are either
  that whole window or a crop of it, as listed.
- **Profiles.** Each run used scratch `XDG_CONFIG_HOME`, `XDG_DATA_HOME`,
  `TAK_CARTOGRAPHER_PROFILE` and `TAK_SETTINGS_FILE`, never a user profile.
- **Data folder.** The retail `.hpi` archives were linked into a scratch install
  with an empty `Maps` folder. The capture runs saw that install at
  `/home/player/Games/TA Kingdoms` through a private mount namespace
  (`unshare`), so the paths shown in Save As, Open Map and Preferences are
  neutral. *Twin Rivers*, *Silverford* and *Practice* are maps made in the
  editor during the captures; *Ulasem Arena* and *King of the Hill* are shipped
  maps. Edits made for a picture were not saved over them.
- **Test Map.** `test-map.jpg` was taken with a stand-in `takclient` beside the
  driver (it kept a copy of the snapshot and waited), so no game opened. That
  snapshot was then opened with the debug game client
  (`takclient --data … --play-map <snapshot> --shot`, `TAK_SHOT_MS=9000`,
  `SDL_RENDER_DRIVER=opengl`, offscreen at 1024 × 768) for `test-lobby.jpg`;
  the release client opens the same lobby but has no `--shot`. On 2026-10-14
  `test-lobby.jpg` was recaptured the same way for MAP SCENARIO (protocol 259),
  from a snapshot of the shipped King of the Hill files packed under the same
  *Test* name, as F5 would have written it, and again after the room's
  scenario lines moved up clear of the panel edge. On 2026-10-20 it was
  recaptured by the 0.8.7 client for the MOVE HERE buttons on the open seats
  (protocol 284), from a snapshot of the same files under the same *Test* name
  (packed with `cartographer --bundle` and `hpitool pack`) and a scratch
  install with no override packs; the room now also shows the map preview. On 2026-10-21 it was
  recaptured by the 0.8.8 client for the UI font and the room's mods lists
  (protocol 288), the same way, with only the always-on `mods/cosmetic/click.hpi`
  in the scratch install, and once more after the 0.8.8 polish (ALWAYS ON under
  the COSMETIC MODS heading). On 2026-10-21 it was recaptured by the 0.8.9
  client the same way, for the room's rules (MAP SCENARIO as a normal rules
  line, GAME MODS VERIFIED only with game packs in play). On 2026-10-24 it was
  recaptured by the 0.8.14 client the same way, with the click archive at
  `mods/cosmetic/always_enabled/click/click.hpi` in the scratch install, so the
  room reads ALWAYS ON: click. On 2026-10-24 it was recaptured by the 0.9.0
  client the same way (protocol 294), from the snapshot that F5 on King of the
  Hill wrote for the stand-in client (*Cartographer Test*), with only the
  click pack in the scratch install.
- **Markers and composites.** `window-annotated.jpg` has numbered markers drawn
  on a copy of the capture with Pillow. `random-generator.jpg`,
  `symmetry-stamp.jpg`, `terrain-clipboard.jpg` and `features-select.jpg` put two
  crops side by side under text labels, and `menus.jpg` puts six crops of the
  opened menus together (each with only its own menu title).
- **Encoding.** JPEG, full chroma resolution, quality 86, not scaled (some AI
  Profiles pictures at 98, below). The 32 files total about 3.7 MB (0.9.0).
- **AI Profiles.** `ai-profiles.jpg` and `ai-group-rules.jpg` were added on
  2026-10-23 with the AI Profiles window (branch `task-carto-ai`, after
  0.8.11), not by the capture driver: they are the window captures that the
  `cartographer_ui_test` workflows `aiprofile` and `aiprofile-extras` write
  when `TAK_EDITOR_TEST_CAPTURE` names a prefix, run on the build-o2 test
  binary with SDL `offscreen` (921 × 691) and a scratch profile, and cropped to
  the window (802 × 534). The workflows edit through the editor's own input
  path; their documents (*AiTest*, a copy of Ulasem Arena's terrain, and
  Ulasem Arena itself) are never saved over a shipped map. The scratch data
  root of `aiprofile-extras` holds a synthetic game pack, not shown.
  `ai-profiles.jpg` was recaptured the same way later on 2026-10-23 and
  encoded at quality 98 (full chroma) instead of 86, because at 86 the tab
  label *DEFAULT AI* could read as *DEFAULT AT*; the scene is unchanged.
  Both were recaptured again the same way when the levels' six tabs became one
  TAK ENGINE AI tab (`ai/takengine.txt`), and `ai-takengine.jpg` was added from
  the `aiprofile-merge` workflow (crop 802 × 534 at 60,78, quality 98, full
  chroma).
  `ai-mod.jpg` was added with the AI mod without a map, from the `aimod-nomap`
  workflow's `-aimod.png` (SDL `offscreen`, 921 × 691, scratch data root and
  profile), not cropped, so the empty editor behind the window shows; quality
  98, full chroma.
- **Empty window.** `empty-window.jpg` and `new-map.jpg` were taken for 0.8.13
  (on 2026-10-24, by the 0.8.13 build) from the `cartographer_ui_test`
  workflow `blank-launch` (a bare launch, fresh scratch profile, SDL
  `offscreen`, 921 × 691, scratch data root of linked retail archives), with
  `TAK_EDITOR_TEST_CAPTURE` naming a prefix and a capture-only patch, not
  committed, that also writes `<prefix>-empty.png` right after the first run's
  welcome is closed (before the workflow's inert clicks and keys) and
  `<prefix>-newmap.png` when File → New map has opened the dialog. Whole window,
  quality 86, full chroma; nothing was edited or saved.
- **Toolbar.** From 0.9.0 every whole-window picture shows the five tool
  buttons (TERRAIN, FEATURES, UNITS, STARTS, BLOCKED). Before that, every one
  except `blocked.jpg` predated the BLOCKED tool and showed four.

| Image | Scene |
| --- | --- |
| `window-annotated.jpg` | Ulasem Arena, Terrain tool, view centred on the arena at 35%, whole window, by the 0.9.0 build with the five-tool toolbar. Markers 1–13 (redrawn for 0.9.0): menu bar, tools, modes, REGIONS, TEST MAP, search, group button, counter line, variant badge, scrollbar, canvas, minimap, status bar |
| `empty-window.jpg` | New in 0.8.13. Bare launch with a fresh profile, the first run's welcome closed: the empty window, title *Cartographer*, menu bar only, an empty toolbar, the centred NO MAP OPEN hint naming File → New map (Ctrl+N), Open map (Ctrl+O) and the AI mod commands, and NO MAP OPEN in the status bar. Whole window |
| `new-map.jpg` | Retaken for 0.8.13. The same bare launch after File → New map: the New Map dialog (Untitled, 8 × 8, aramon) over the dimmed empty window, with NO MAP OPEN in the status bar. Whole window. Before 0.8.13 a bare launch opened this dialog by itself over an Untitled placeholder map |
| `random-generator.jpg` | Retaken for 0.9.0 by the new capture driver, linked against the 0.9.0 editor (first recaptured for 0.8.5). Composite. Left: Random Map Settings (seed 2026, 4 players, Aramon Riverlands, 16 × 16, SCATTERED MANA (BY MAP AREA) set to Medium, other fields default). Right: the Generated Map Preview it produced. Dialog crops; the settings dialog is taller than before (twelve fields) |
| `terrain-groups.jpg` | Ulasem Arena, the terrain group list open, whole window |
| `terrain-variant.jpg` | Ulasem Arena, Cliff W clicked twice (variant 2/3) with the pointer on it, so the status bar shows its full name. Crop of the left 470 px |
| `symmetry-stamp.jpg` | Composite of two crops. Ulasem Arena, live symmetry (vertical axis), Low Sandy to High Sandy *Transition NW outer* chosen. Left: the preview before the click; right: the stamped section and its twin |
| `terrain-clipboard.jpg` | Composite of two crops. Ulasem Arena, Terrain + Select: a 15 × 15-block rectangle (left); after Ctrl+C and Ctrl+V, the clipboard brush under the pointer (right) |
| `symmetrize.jpg` | Ulasem Arena, Shift+M, the Symmetrize dialog. Dialog crop |
| `features.jpg` | Ulasem Arena, Features tool, category *trees*: five palm trees placed by clicking, the preview of a sixth under the pointer, whole window |
| `features-select.jpg` | Composite of two crops of the same trees: the selection box being dragged in Select mode, then the selected anchor cells |
| `units.jpg` | King of the Hill, Units tool filtered to GROUND and ARAMON; eight of Player 1's units box-selected in Select mode at 75%, whole window |
| `unit-properties.jpg` | King of the Hill, Unit Properties of one of Player 1's Archers (opened with Shift+Enter in Placed units) with *Captain Aldric* typed as the display name, not applied. Dialog crop |
| `unit-model.jpg` | Ulasem Arena, Units browser searched for *knight*, F6 on the Aramon Knight. Crop of the browser and the model window |
| `starts.jpg` | Ulasem Arena, Starts tool, View → Fit map (7%), whole window |
| `regions.jpg` | King of the Hill at Fit map, REGIONS on in Select mode with The Hill selected, whole window |
| `scripting.jpg` | King of the Hill, Scenario Scripting with Rule 1 of All Players selected. Crop of the scripting window |
| `rule-editor.jpg` | The same, with Rule 1's condition open for editing and its LOCATION list opened with Alt+Down. Crop of the scripting window |
| `templates.jpg` | King of the Hill, Objective Templates. Dialog crop |
| `check-map.jpg` | *Practice*: a new 8 × 8 Aramon map with one start, a Horseman, and a War Galley placed on dry land; then Check Map. Dialog crop |
| `overlay-movement.jpg` | Ulasem Arena at 40%, Horseman chosen in the Units browser, View → Overlay: movement, whole window |
| `preferences.jpg` | View → Preferences with default settings. Dialog crop |
| `open-map.jpg` | Open Map after opening King of the Hill and saving *Twin Rivers*, so two Recent rows lead the list (Twin Rivers first, the latest). Dialog crop |
| `save-as.jpg` | Save As for the new map *Twin Rivers*. Dialog crop |
| `test-map.jpg` | King of the Hill after F5: the TEST MAP message, with TEST RUNNING on the toolbar, whole window |
| `test-lobby.jpg` | The game's private lobby for that King of the Hill snapshot (retaken by the 0.9.0 debug client; before that by the 0.8.14 client for the always-enabled pack folders, by the 0.8.9 client for the room's rules and the 0.8.8 client for the UI font and the mods lists): seats 2 to 4 open, tagged NEEDED BY SCENARIO and each with a MOVE HERE button, the map preview numbering the four starts, ALWAYS ON: click under the COSMETIC MODS heading with no packs in either the COSMETIC MODS or the GAME MODS list (so no GAME MODS VERIFIED line), START greyed, and MAP SCENARIO: ON (TEST MAP) as a rules line at the others' size with the scenario details under both columns; 1024 × 768 cropped to the lobby panel |
| `menus.jpg` | Composite: the FILE, EDIT, VIEW, SCENARIO, SYMMETRY and HELP menus, each opened over Ulasem Arena and cropped to the menu (with only its own title). From 0.9.0 FILE has New AI mod... and Open AI mod..., VIEW ends with Show blocked cells (B) and SCENARIO with AI Profiles... (A) |
| `ai-profiles.jpg` | *AiTest* (Ulasem Arena's terrain), Scenario → AI Profiles, DEFAULT AI tab: the map's own profile *aitest* started Empty, then NO FLYERS after its confirmation, so every flying unit reads BUILD OFF and LIMIT 0; TAK ENGINE AI (`ai/takengine.txt`) is tagged `map *` from an earlier edit, and PREVIEW LEVEL reads HARD. Crop of the window |
| `ai-group-rules.jpg` | Ulasem Arena, AI Profiles, DEFAULT AI tab in the GROUP RULES view: own profile *ulasem arena*, one rule WEIGHT ARA 50 % covering 32 units, the Archer set individually (80 %) and shown as skipped. Crop of the window |
| `ai-takengine.jpg` | *AiMerge* (Ulasem Arena's terrain) carrying the old level files `ai/e.txt`, `ai/n.txt` and `ai/h.txt`: AI Profiles, TAK ENGINE AI tab previewing NORMAL right after MERGE INTO TAK ENGINE took Normal's file; the old-file notice is still shown. From the `aiprofile-merge` workflow's `-merged.png`. Crop of the window |
| `ai-mod.jpg` | File → New AI mod *Harder AI* (a scratch pack, no map open): the AI MOD (NO MAP) window on the TAK ENGINE AI tab previewing NORMAL after one edit, the Archer's WEIGHT 0, so the pack reads UNSAVED; the toolbar names the pack and the editor behind the window is empty. Whole window |
| `blocked.jpg` | Added with the BLOCKED tool, not by the capture driver: the editor's own `--shot` output (`cartographer "Ladron's Tarn" --shot … --shot-tool blocked`, SDL `offscreen`, fresh scratch profile), whole 921 × 691 window at 30%. The shipped map's blocked cells along its castle walls are tinted red and its road cells blue; the browser lists the five brush sizes |

The native dialogs (the first-start folder picker, *Not a game folder*,
*Recover map* and *Restart failed*) belong to the operating system and are not
pictured.

## Quick start (`docs/img/quick/`)

New in 0.8.7, for [quick-start.md](../quick-start.md). Recaptured for 0.9.0
by the 0.9.0 build, with the capture patch plus press, move and release entries
for `TAK_SHOT_CLICKS` (for the drag); `build-line.jpg` (new in 0.8.9) uses the
committed `TAK_BUILDLINE_SHOT` fixture. The game pictures are development scenes on
Ulasem Arena, local server, 1600 × 900 at 1× zoom, cropped and scaled.

| Image | Scene |
| --- | --- |
| `box-select.jpg` | **Spawned.** 17 Aramon units (Swordsmen, Archers, Knights, Catapults); a left-drag box drawn round them, before release. Crop, not scaled |
| `selected.jpg` | The same army after release: green selection rings, four type portraits with counts 2, 3, 6 and 6, 17 SELECTED and the command panel. Taros units stand to the east. 75% |
| `attack-cursor.jpg` | The army selected, the pointer over a Taros Skeleton Archer: the crossed-swords attack cursor, at Cursor Size 2×. 80% |
| `command-panel.jpg` | A Mage Builder's command panel at 200%, labelled MOVE (M), PATROL (P), ATTACK (A), GUARD (G), HEAL (H), CLEAR and STOP (S). Which icon is which was checked by arming each key and seeing which button darkened (0.8.7) |
| `build-placement.jpg` | **Spawned** Mage Builder selected, its build menu along the bottom, the Barracks icon chosen and the Barracks ghost with its label under the pointer. 80% |
| `build-line.jpg` | Two Shift-drag wall lines by Elsin, side by side: left, a tight diagonal of ten walls corner to corner (`TAK_BUILDLINE_SHOT=300,220,0`); right, a straight line of five walls three cells apart with the **GAP 3** label (`TAK_BUILDLINE_SHOT=360,0,3`). `--testbuild`, 1280 × 960, hardware cursor on; the same crop of each, joined and scaled to 85% |
| `lobby-move-here.jpg` | Single-player room opened from the menu with the default map (Athri Cay), 1600 × 900, by the 0.9.0 build: the player in slot 1, a Normal AI in slot 2, MOVE HERE on open slots 3 to 5, slots 6 to 8 closed, the preview numbering starts 1 to 5, and the COSMETIC MODS and GAME MODS lists (no packs in the scratch install). 72% |

## Capture settings

Captures use isolated SDL preference directories (`XDG_DATA_HOME` and
`XDG_CONFIG_HOME`), never a user profile. The window is 1920 × 1080 for the
gallery and 1600 × 900 for the guide, windowed, with VSync off. From 0.8.5 the
profiles use the 0.8.5 defaults: Texture Filtering Everything, Smooth GUI Art and
Smooth Movies on, Tactical Dots on at 0% zoom, Screen Shake off, shadows and tree
sway on. (Until 0.8.4 the defaults, and so the profiles, had Texture Filtering Off
and Smooth GUI Art and Smooth Movies off.) The hardware cursor is off for capture.
The stats panel is off unless a caption says it is on.

Game images use `SDL_VIDEODRIVER=offscreen` with `SDL_RENDER_DRIVER=opengl`.
Server-backed scenes run a temporary `takserver --local --no-auth --seed 42`.
Headless scenes (`--mphost`) render the final synchronized frame after the given
number of game seconds. Interactive scenes (the HUD, side shots and replay) render
live and capture after a wall-clock wait, so their FPS reading is real. Screenshot
timing is not a performance measurement.

**Lobby captures** (`lobby.jpg`, `lobby-scenario.jpg` and
`guide/random-map-lobby.jpg` from 0.8.9; `room.jpg` and
`guide/room-mods-power-codes.jpg` from 0.8.14)
launch the debug client's menu with `TAK_MENU_PICK=singleplayer` (the
single-player door, with `TAK_NODOORVID=1` skipping the door video), the profile's
`lastMap` naming the map, and the hardware cursor on so no pointer is drawn.
`TAK_SHOT_CLICKS` clicks through the screens (CREATE, slot types and sides, pack
boxes, the MAP SCENARIO button) and `TAK_SHOT_SIZES` captures the live window. The
data folder is a scratch copy of the retail install (links to its archives and
`Maps`, an empty `MapCache`) whose `mods/` holds the always-enabled cosmetic
pack *click* (`mods/cosmetic/always_enabled/click/click.hpi`; up to 0.8.13 the
archive sat directly in `mods/cosmetic/`), the cosmetic packs *Alternate
Cursors* and *Classic Sounds* and the game pack *Big Battles* (placeholder
folders), for the pack lists; for `guide/room-mods-power-codes.jpg` also the
always-enabled game pack *House Rules* (`mods/game/always_enabled/House Rules/`).

Capture scripts, job lists and the encoder are in the release-preparation
scratch directories, `$SCR/shared/release-0.7.26/shots`,
`$SCR/shared/release-0.7.27/shots`, `$SCR/shared/release-0.7.28/shots`,
`$SCR/shared/release-0.8.0/shots`, `$SCR/shared/release-0.8.1/shots`,
`$SCR/shared/release-0.8.2/shots`, `$SCR/shared/release-0.8.3/shots`, `$SCR/shared/release-0.8.4/shots`, `~/tak-tmp/scratch/release-085/shots`, `~/tak-tmp/scratch/release-086/shots`, `~/tak-tmp/scratch/release-087/shots`, `~/tak-tmp/scratch/release-088/shots`, `~/tak-tmp/scratch/release-089/shots`, `~/tak-tmp/scratch/release-0810/shots`, `~/tak-tmp/scratch/release-0811/shots`, `~/tak-tmp/scratch/release-0812/shots`, `~/tak-tmp/scratch/release-0813/shots`, `~/tak-tmp/scratch/release-0814/shots`, and for 0.9.0 `~/tak-tmp/scratch/release-090/carto`, `~/tak-tmp/scratch/release-090/front/shots` and `~/tak-tmp/scratch/release-090/game`. Raw PNGs, logs, profiles
and the SHA-256 manifest are kept locally in the worktree's ignored
`build-o2/capture` folder. Keep retail archives, extracted assets and temporary
capture files out of Git. When refreshing, rerender and inspect every view, then
update these notes and the captions.
