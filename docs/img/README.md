# README and user-guide screenshots

Refreshed on 2026-10-21 for **0.8.8**. Every image below is actual engine output
from a release-preparation build (Debug with `-O2`, OpenGL renderer) and was
inspected by eye. 0.8.8 draws all plain interface text in Noto Sans Bold
(Options → Interface → **UI Font**, default Modern), so every picture with
interface text was re-rendered by the 0.8.8 build, with the default UI Font:

- menus and rooms: `title.jpg` (VERSION 0.8.8), `lobby.jpg`,
  `lobby-scenario.jpg`, `room.jpg`, `campaign.jpg`, `crusades.jpg`, `streaming.jpg`,
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
to the Voice Chat panel (832 × 651, not scaled), `guide/chat-box.jpg` to the chat box (1240 × 340 from 1600 × 900) and `guide/options-interface.jpg` to the Interface section (824 × 455 from 1600 × 1100). `cartographer.png` and
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
`guide/chat-box.jpg`. It changes no
simulation code. Every scene runs the normal game rules, AI and renderer.

## Gallery (`docs/img/`)

| Image | Scene |
| --- | --- |
| `title.jpg` | Title menu showing version 0.8.8, 1920 × 1080 window cropped to the 1440 × 1080 artwork |
| `battle.jpg` | **Benchmark** run (Settings → Benchmark, Absurd intensity) on Ulasem Arena, 55 game seconds in. Eight server AIs fight, all five sides. The camera, at 1.3× zoom, shows Aramon (magenta) and Taros (yellow) armies, with Black Dragons, clashing at a sandy arena; damaged units of every side show health bars. A real feature with no fixture; spectator view, so no fog. Rendered by the 0.8.8 build |
| `dragons.jpg` | **Development scene.** Five server AIs, one per side, from a stress-test start (300-unit cap) with gods on, 90 game seconds in. Creon Neo Dragons breathe fire on an Aramon siege column; health bars show for every side. Spectator view, 1.4× zoom. Rendered by the 0.8.8 build |
| `flyers.jpg` | **Development scene, spawned.** Legion pathfinding, 22 s after one move order east-south-east. A 44-unit Zhon formation: Trolls, Stone Giants and Jungle Orcs, with Gryphons, Harpies and Drakes holding rows of stations over the front of the ground (new in 0.8.0, protocol 254; before, over its middle). `1F`, solid green, marks members of formation 1. Nothing is selected, so the information bar is empty (0.8.1). Fog off, 1.45× zoom. Rendered by the 0.8.8 build (protocol 288) |
| `hud.jpg` | Aramon player in a local-server game, about 10 s in. **Stress-test start**: Normal AI opponent, 250-unit cap. **Scripted orders**: 45 units of eight types (Archers, Swordsmen, Knights, Catapults, Mage Archers, Cannoneers, Horsemen, Rolling Towers) were made formation 1 and selected, so the information bar shows the portrait strip (one portrait per type with its count), FORMATION 1 and 45 SELECTED, and the selected units show `1F` in solid green (new in 0.8.0). The empty right-hand bar is gone (0.8.1). Stats panel on, 1.15× zoom; its VID row reads 117 MB with the new graphics defaults (Smooth GUI Art's upscaled interface art uses more video memory) (0.8.7: 113 MB, 0.8.4: 94 MB, 0.8.1: 87 MB, 0.8.0: 151 MB). Rendered by the 0.8.8 build |
| `naval.jpg` | **Development scene, spawned.** Cairbray Coast Landing, 10 s in. Veruna Flagship, Men of War, Trebuchet Ships and Harpoon Ships fight Creon Iron Clads, Stern Wheelers and Submersibles. Nothing is selected, so the information bar is empty. 1.6× zoom, fog off. Rendered by the 0.8.8 build |
| `generated.jpg` | Generated Aramon Riverlands map "Silverford" (generator v9, seed 7, 256 × 256, four players, Scattered Mana Medium: 53 mana spots). Four Hard AIs at 8× speed, seven game minutes in. Spectator view of the lake shore at 1×, with a scattered neutral mana spot among standing stones in the clearing. Rendered by the 0.8.8 build |
| `lobby.jpg` | Single-player create screen with Ulasem Arena selected (the title from the map's translate table, in the list and on the MAP line): the preview numbers the map's eight start positions, MAP SCENARIO under it is greyed (THIS MAP HAS NO SCENARIO), and POWER CODES: OFF is among the options. Retail pathfinding, other options at their defaults. Rendered by the 0.8.8 build, 1600 × 1000 (see *Lobby captures* below) |
| `lobby-scenario.jpg` | Single-player create screen on King of the Hill with **MAP SCENARIO** turned on: the gold **S** marks the maps with a playable scenario in the list, the lines under the button say what the scenario changes, Monarch Expendable and Start Locations are greyed, and the preview numbers the four start positions. 1024 × 768, cropped to the lobby panel; the map row and the button were clicked with `TAK_SHOT_CLICKS`, then the pointer moved off the panel. Rendered by the 0.8.8 build. Used only by the user guide |
| `room.jpg` | Single-player room on Ulasem Arena with three Normal AIs (Taros, Veruna, Zhon) added by clicks, Crusades and Double Sight on, Legion pathfinding and both example cosmetic packs ticked (empty folders in the scratch install); under the COSMETIC MODS heading ALWAYS ON: click.hpi, then the pack names at the column's text size, the GAME MODS list below. The open rows 5 to 8 each have a **MOVE HERE** button. The map at the top of the right column marks starts 1 to 4 in the colours of the four seated players. The AI names are the lobby's own random picks. Rendered by the 0.8.8 build, 1600 × 1000; the CREATE screen's toggles, slot types, sides and pack boxes were clicked with `TAK_SHOT_CLICKS`, then the pointer moved off the panel |
| `campaign.jpg` | Book of Darien campaign picker with a fresh profile, chapter 1. The CHAPTER heading and numeral are retail lettering in both UI fonts; the chapter title and the always-on mods note (click.hpi, in dark ink on the parchment) are in the UI font. Rendered by the 0.8.8 build, 1600 × 1000, with `TAK_SHOT_CAMPAIGN=0` |
| `crusades.jpg` | Imported Darien Crusades definition (`crusades_import` from the retail `Darien.def`) on an authenticated local server, account *Gallery*; Torcairn selected in the territory list after joining Honor, so the territory panel lists its details and the map marks it. No authored battle maps or ownership. Rendered by the 0.8.8 build, 1600 × 1000 |
| `streaming.jpg` | Streaming setup with no key or broadcast, showing the **Remember Key** row (off; the Secret Service is available). Rendered by the 0.8.8 build at 1600 × 1000, with the debug `TAK_CREDSTORE=test` store so no real key could appear |
| `diplomacy.jpg` | Built-in input-test fixture (the debug `TAK_DIPLOMACY_TEST` harness, `--testbuild`) after gifting, 1600 × 1000, in a profile with voice chat on. The VOICE column ticks every other player (your own box is shown, greyed); VOLUME has sliders for the two other humans, left by the fixture at 50% (ALLY) and 25% (DEFEATED), and none for the computer player (ENEMY). Elsin stays selected, so the bar shows her portrait, name and health and mana only. Rendered by the 0.8.8 build |
| `results.jpg` | Built-in five-player **sample statistics** (`TAK_SHOT_RESULT`), not a finished match. Rendered by the 0.8.8 build |
| `cartographer.png` | Cartographer Units tab on Ulasem Arena, from the 0.8.3 build: the toolbar has five tools (BLOCKED is new) and the browser a scrollbar. A fresh editor profile, `--shot --shot-tool units`, 30% zoom |
| `cartographer-model.png` | Unchanged from 0.7.25: Cartographer F6 model inspector, Aramon Archer |

The 0.7.25 `army.jpg` (an Aramon army standing still, with no combat) and
`gameplay.jpg` (a single barracks) were removed. `battle.jpg` and `hud.jpg`
replace them. The unchanged views contain no version text.

## User guide (`docs/img/guide/`)

| Image | Scene |
| --- | --- |
| `hud-annotated.jpg` | From the 0.8.8 build. The 0.7.28 `hud.jpg` setup (Elsin alone selected, so her build menu shows), captured at 1600 × 900 with 1× zoom. Markers (redrawn for the new layout): 1 minimap, 2 stats panel, 3 orders, 4 weapons and stances, 5 mana, 6 build menu, 7 information bar, 8 selected Monarch. The bar shows only her portrait, name, health and mana and STANDBY. At this window height the stats panel has room for its first four rows only (FPS, PING, SIM, UNITS) |
| `selection-strip.jpg` | **Selection still life.** Ulasem Arena, `--testbuild` with `TAK_SELPORTRAITS=arasword:9,araarch:6,arabow:4,arapal:3,arapult:2`, `TAK_SELPORTRAITS_HURT` and `TAK_SELPORTRAITS_SQUAD=3`, 1920 × 1080 at UI Scale 150%, cropped to the left of the information bar (1160 × 110, not scaled). Five full-size type portraits with counts, GROUP 3, the group's partial health and mana totals and 24 SELECTED. Rendered by the 0.8.8 build |
| `side-aramon.jpg` … `side-creon.jpg` | One per side, rendered by the 0.8.8 build. A local-server game with a **stress-test start** (250-unit cap, Normal AI opponent), about 10 s in. The player's Monarch is selected: Elsin, Lokken, Kirenna, Thirsha or the Sage. Each shows that side's interface and build menu |
| `shift-queue.jpg` | From the 0.8.8 build. Elsin with a move and three Shift-queued moves (**scripted orders**), Shift held, about 3 s after the orders; the status reads MOVING. 1.7× zoom |
| `flyers-retail-legion.jpg` | **Development scene, spawned.** From the 0.8.8 build (both halves, protocol 288). The `flyers.jpg` formation and order, 22 s in, run once in Retail (flyers far ahead) and once in Legion (flyers over the leading ground units). The same 1.05× camera is used for both, and the two captures are placed side by side with labels |
| `options-graphics.jpg` | 0.8.8. Options screen in a 1600 × 1100 window, scrolled so that Display and the whole Graphics section show (the last Audio rows above, the Interface heading and UI FONT below), cropped to the panel: Fullscreen (OFF), VSync (ON), Max FPS (60, greyed out because VSync is on), Renderer (AUTO), Texture Filtering at EVERYTHING, Shadows, Smooth GUI Art, Smooth Movies, Trees Sway in Wind and Tactical Dots (ON), Tactical Dots Zoom (0%), Screen Shake (OFF), Hardware Cursor and Smooth Motion (ON) and GPU Memory Limit at 1.25 GB (default). A fresh profile whose only setting is `fullscreen = 0`. Linux build, so the Windows-only Ignore Windows Display Scaling row is absent |
| `fire-on-the-move.jpg` | **From the 0.8.8 build. Development scene, two-player spawned.** Ulasem Arena, Retail pathfinding, 14 s after one plain move order east (**scripted orders**). 14 Aramon Archers and 2 Mage Builders were spawned in a 4-wide grid; 6 Taros Zombies and 6 Skeleton Archers (enemy, no AI) stand south of their path. Shift is held, so the move line shows. Zombies have reached the back of the column, which keeps walking and shooting; the flashes are hits, and three of the sixteen have fallen, so the information bar shows two portraits (2 Mage Builders, 11 Archers) and 13 SELECTED. 1.4× zoom, fog off |
| `cartographer-terrain.jpg` | Cartographer Terrain tab on Ulasem Arena (`--shot --shot-tool terrain`, fresh editor profile, 921 × 691), from the 0.8.3 build: the grouped section browser, All sections (555 pieces, 1261 sections), with readable piece names, variant badges and the scrollbar, and five tools on the toolbar |
| `tactical-dots.jpg` | From the 0.8.5 build. **Benchmark** run (Absurd) on Ulasem Arena, 50 game seconds in, spectator view fully zoomed out (zoom 0.1875, the floor for this map in a 1920 × 1080 window), where the 0.8.5 default Tactical Dots Zoom of 0% shows dots. The deterministic run was rendered twice at the same tick and camera, with Tactical Dots off and on in otherwise identical profiles. The same 800 × 900 region (the east of the map: the blue and red armies) is cropped from each at 1:1 and placed side by side under labels. With models, fire and spell effects show among the armies; with dots none are drawn (new in 0.8.5) |
| `campaign-briefing.jpg` | *Book of Darien* chapter 1 briefing over the paused battlefield, from a direct mission launch, by the 0.8.8 build: the PAUSED heading is retail art, the chapter, title and objectives are in the UI font, and the last line names the always-on mod (click.hpi). Nothing is selected, so the information bar is empty |
| `replay.jpg` | Replay playback of a recorded local-server game, 9 s of 16 s, with the time bar. **Stress-test start**: an Aramon army given one Legion move order (**scripted orders**). Recorded and played back by the 0.8.8 build |
| `benchmark.jpg` | From the 0.8.8 build. Benchmark run (Absurd), 55 s in; the BENCHMARKING badge counts down the remaining time (0:05 of 60 s): armies meet at the edge of a sandy clearing, with health bars for every side; scaled from 1920 × 1080 |
| `scorecard.jpg` | From the 0.8.8 build. F4 scorecard over a benchmark battle (Absurd), 55 s in, at 1.1× zoom; captured at 1920 × 1080 (so the BENCHMARKING badge clears the Score column) and scaled |
| `voice-chat.jpg` | Recaptured by the 0.8.8 build. The Voice Chat page opened from the menu (capture-only patch), 1600 × 900, cropped to the panel. The capture profile with `voiceEnabled = 1`; every other voice setting is at its default: TALK MODE PUSH TO TALK, PUSH TO TALK on SPACE, SYSTEM DEFAULT microphone at 100, ACTIVATION LEVEL 15 (greyed out under Push to Talk), SAME AS GAME OUTPUT at 100. No voice engine runs under `--shot`, so MIC LEVEL is empty |
| `random-map-lobby.jpg` | Single-player create screen with **Generate Random Map** clicked, then SIZE clicked once (12 × 12), PLAYERS twice (4) and the SCATTERED MANA slider set to HIGH (`TAK_SHOT_CLICKS`, see *Lobby captures*). The preview reads MANA: 57 and SCATTERED: 33 NEUTRAL. Captured live at 1600 × 1000 with `TAK_SHOT_SIZES` by the 0.8.8 build, cropped to 1500 × 900, not scaled. The seed is the lobby's own random one |
| `chat-box.jpg` | New in 0.8.8. The in-game chat box at its defaults (Chat Position Centered, Chat Scale 100%) in a local-server game on Ulasem Arena, 1600 × 900, cropped to the box (1240 × 340, not scaled). Four chat lines put into the box by the capture patch with fixed names (Ludwin, Aldric, Kirenna, Pat) in their player colours, and *on my way* being typed after SAY: |
| `options-interface.jpg` | New in 0.8.8. Options screen in a 1600 × 1100 window scrolled to the Interface section, cropped to it: UI FONT MODERN, F4 SCORECARD SCALE and UI SCALE 100%, HEALTH BARS DAMAGED, STATS PANEL ON, BUILD MENU CENTER, BUILD MENU SCALE 100%, CHAT POSITION CENTERED and CHAT SCALE 100%; a fresh profile, all defaults |
| `room-mods-power-codes.jpg` | New in 0.8.8. Single-player room on Ulasem Arena, 1600 × 1000: POWER CODES clicked on in the create screen, two Normal AIs; under the COSMETIC MODS heading ALWAYS ON: click.hpi, then the packs at the column's text size (Classic Sounds ticked, Alternate Cursors not) and the GAME MODS list (Big Battles ticked); the match rules read POWER CODES: ON. Clicked with `TAK_SHOT_CLICKS` |

The guide also uses `title.jpg`, `lobby.jpg`, `room.jpg`, `campaign.jpg`,
`diplomacy.jpg`, `generated.jpg` and `cartographer.png` from the gallery, and
`lobby-scenario.jpg` from the table above.


The first-launch folder picker is the operating system's own dialog
(`kdialog`/`zenity`, Finder or Explorer), so it has no in-engine screenshot.

## Cartographer guide (`docs/img/cartographer/`)

Added on 2026-10-14 for the [Cartographer user guide](../cartographer-guide.md),
from a build of `main` after 0.8.2 (Debug with `-O2`). Every picture is real
editor output, inspected by eye; the Test Map lobby is the game's.

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
  the COSMETIC MODS heading).
- **Markers and composites.** `window-annotated.jpg` has numbered markers drawn
  on a copy of the capture with Pillow. `random-generator.jpg`,
  `symmetry-stamp.jpg`, `terrain-clipboard.jpg` and `features-select.jpg` put two
  crops side by side under text labels, and `menus.jpg` puts six crops of the
  opened menus together (each with only its own menu title).
- **Encoding.** JPEG, full chroma resolution, quality 86, not scaled. The 27
  files total about 2.9 MB.
- **Toolbar.** Every whole-window picture except `blocked.jpg` predates the
  BLOCKED tool, so its toolbar shows four tool buttons where the editor now has
  five.

| Image | Scene |
| --- | --- |
| `window-annotated.jpg` | Ulasem Arena, Terrain tool, view centred on the arena at 35%, whole window. Markers 1–13: menu bar, tools, modes, REGIONS, TEST MAP, search, group button, counter line, variant badge, scrollbar, canvas, minimap, status bar |
| `new-map.jpg` | Bare launch with a fresh profile: the New Map dialog over the blank Untitled 8 × 8 Aramon map, whole window |
| `random-generator.jpg` | Recaptured for 0.8.5 by the same driver, linked against the 0.8.5 editor. Composite. Left: Random Map Settings (seed 2026, 4 players, Aramon Riverlands, 16 × 16, SCATTERED MANA (BY MAP AREA) set to Medium, other fields default). Right: the Generated Map Preview it produced. Dialog crops; the settings dialog is taller than before (twelve fields) |
| `terrain-groups.jpg` | Ulasem Arena, the terrain group list open, whole window |
| `terrain-variant.jpg` | Ulasem Arena, Cliff W clicked twice (variant 2/3) with the pointer on it, so the status bar shows its full name. Crop of the left 470 px |
| `symmetry-stamp.jpg` | Composite of two crops. Ulasem Arena, live symmetry (vertical axis), Low Sandy to High Sandy *Transition NW outer* chosen. Left: the preview before the click; right: the stamped section and its twin |
| `terrain-clipboard.jpg` | Composite of two crops. Ulasem Arena, Terrain + Select: a 15 × 15-block rectangle (left); after Ctrl+C and Ctrl+V, the clipboard brush under the pointer (right) |
| `symmetrize.jpg` | Ulasem Arena, Shift+M, the Symmetrize dialog. Dialog crop |
| `features.jpg` | Ulasem Arena, Features tool, category *trees*: five palm trees placed by clicking, the preview of a sixth under the pointer, whole window |
| `features-select.jpg` | Composite of two crops of the same trees: the selection box being dragged in Select mode, then the selected anchor cells |
| `units.jpg` | King of the Hill, Units tool filtered to GROUND and ARAMON; Player 1's units box-selected in Select mode at 75%, whole window |
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
| `open-map.jpg` | Open Map after opening King of the Hill and saving *Twin Rivers*, so two Recent rows lead the list. Dialog crop |
| `save-as.jpg` | Save As for the new map *Twin Rivers*. Dialog crop |
| `test-map.jpg` | King of the Hill after F5: the TEST MAP message, with TEST RUNNING on the toolbar, whole window |
| `test-lobby.jpg` | The game's private lobby for that King of the Hill snapshot (recaptured by the 0.8.8 client for the UI font and the mods lists): seats 2 to 4 open, tagged NEEDED BY SCENARIO and each with a MOVE HERE button, the map preview numbering the four starts, ALWAYS ON: click.hpi under the COSMETIC MODS heading with no packs in either the COSMETIC MODS or the GAME MODS list, START greyed, and the MAP SCENARIO: ON (TEST MAP) lines under the match options; 1024 × 768 cropped to the lobby panel |
| `menus.jpg` | Composite: the FILE, EDIT, VIEW, SCENARIO, SYMMETRY and HELP menus, each opened over Ulasem Arena or King of the Hill and cropped to the menu |
| `blocked.jpg` | Added with the BLOCKED tool, not by the capture driver: the editor's own `--shot` output (`cartographer "Ladron's Tarn" --shot … --shot-tool blocked`, SDL `offscreen`, fresh scratch profile), whole 921 × 691 window at 30%. The shipped map's blocked cells along its castle walls are tinted red and its road cells blue; the browser lists the five brush sizes |

The native dialogs (the first-start folder picker, *Not a game folder*,
*Recover map* and *Restart failed*) belong to the operating system and are not
pictured.

## Quick start (`docs/img/quick/`)

New in 0.8.7, for [quick-start.md](../quick-start.md). Recaptured for 0.8.8
(UI Font Modern) by the 0.8.8 build, with the 0.8.7 capture patch plus press,
move and release entries for `TAK_SHOT_CLICKS` (for the drag);
`command-panel.jpg` has no engine text and came out byte-identical, so it is
the 0.8.7 file. The game pictures are development scenes on
Ulasem Arena, local server, 1600 × 900 at 1× zoom, cropped and scaled.

| Image | Scene |
| --- | --- |
| `box-select.jpg` | **Spawned.** 17 Aramon units (Swordsmen, Archers, Knights, Catapults); a left-drag box drawn round them, before release. Crop, not scaled |
| `selected.jpg` | The same army after release: green selection rings, four type portraits with counts 2, 3, 6 and 6, 17 SELECTED and the command panel. Taros units stand to the east. 75% |
| `attack-cursor.jpg` | The army selected, the pointer over a Taros unit: the crossed-swords attack cursor, at Cursor Size 2×. 80% |
| `command-panel.jpg` | A Mage Builder's command panel at 200%, labelled MOVE (M), PATROL (P), ATTACK (A), GUARD (G), HEAL (H), CLEAR and STOP (S). Which icon is which was checked by arming each key and seeing which button darkened |
| `build-placement.jpg` | **Spawned** Mage Builder selected, its build menu along the bottom, the Barracks icon chosen and the Barracks ghost with its label under the pointer. 80% |
| `lobby-move-here.jpg` | Single-player room opened from the menu with the default map, 1600 × 900: the player in slot 1, a Normal AI in slot 2, MOVE HERE on open slots 3 to 5, slots 6 to 8 closed, the preview numbering starts 1 to 5, and the COSMETIC MODS and GAME MODS lists (no packs in the scratch install). 72% |

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

**Lobby captures** (`lobby.jpg`, `lobby-scenario.jpg`, `room.jpg`,
`guide/random-map-lobby.jpg` and `guide/room-mods-power-codes.jpg`, all from 0.8.8)
launch the debug client's menu with `TAK_MENU_PICK=singleplayer` (the
single-player door, with `TAK_NODOORVID=1` skipping the door video), the profile's
`lastMap` naming the map, and the hardware cursor on so no pointer is drawn.
`TAK_SHOT_CLICKS` clicks through the screens (CREATE, slot types and sides, pack
boxes, the MAP SCENARIO button) and `TAK_SHOT_SIZES` captures the live window. The
data folder is a scratch copy of the retail install (links to its archives and
`Maps`, an empty `MapCache`) whose `mods/` holds the always-on `click.hpi`,
the empty cosmetic packs *Alternate Cursors* and *Classic Sounds* and the empty
game pack *Big Battles*, for the pack lists.

Capture scripts, job lists and the encoder are in the release-preparation
scratch directories, `$SCR/shared/release-0.7.26/shots`,
`$SCR/shared/release-0.7.27/shots`, `$SCR/shared/release-0.7.28/shots`,
`$SCR/shared/release-0.8.0/shots`, `$SCR/shared/release-0.8.1/shots`,
`$SCR/shared/release-0.8.2/shots`, `$SCR/shared/release-0.8.3/shots`, `$SCR/shared/release-0.8.4/shots`, `~/tak-tmp/scratch/release-085/shots`, `~/tak-tmp/scratch/release-086/shots`, `~/tak-tmp/scratch/release-087/shots` and `~/tak-tmp/scratch/release-088/shots`. Raw PNGs, logs, profiles
and the SHA-256 manifest are kept locally in the worktree's ignored
`build-o2/capture` folder. Keep retail archives, extracted assets and temporary
capture files out of Git. When refreshing, rerender and inspect every view, then
update these notes and the captions.
