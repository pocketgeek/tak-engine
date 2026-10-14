# README and user-guide screenshots

Refreshed on 2026-10-14 for **0.8.2**. Every image below is actual engine output
from a release-preparation build (Debug with `-O2`, OpenGL renderer) and was
inspected by eye. 0.8.2 changes only the Legion simulation (protocol 257), so the
0.8.2 build re-rendered `title.jpg` (VERSION 0.8.2) and the three pictures of a
Legion army moving past Ulasem Arena's trees, whose positions the change moves:
`flyers.jpg`, `guide/flyers-retail-legion.jpg` (both halves, so the pair comes
from one build) and `guide/replay.jpg` (a fresh recording, since 0.8.2 cannot
play a 0.8.1 replay). The 0.8.1 build re-rendered the pictures that show the
information bar, whose empty portrait box and empty bars it removed: `hud.jpg`,
`naval.jpg`, `diplomacy.jpg`, `guide/hud-annotated.jpg`,
`guide/selection-strip.jpg`, the five `guide/side-*.jpg`,
`guide/shift-queue.jpg`, `guide/fire-on-the-move.jpg` and
`guide/campaign-briefing.jpg`, plus `guide/options-graphics.jpg` for the GPU
Memory Limit row. None of those shows Legion movement. `streaming.jpg` was
rendered after 0.8.0 for Remember Key;
`cartographer.png` and `guide/cartographer-terrain.jpg` are from the 0.8.0 build,
`guide/tactical-dots.jpg` from the 0.7.27 build, and the rest are from the 0.7.26
build (or older, where noted). None of those shows the information bar: they are
spectator views, menus or the Cartographer. Encoding is
the only change after capture: JPEG with full chroma resolution, quality 90 for
the title, 88 for the gallery and 86 for the guide; guide images are scaled from
1600 × 900 to 1280 × 720. The title is cropped to its 4:3 artwork, and
`guide/selection-strip.jpg` to the information bar. The Cartographer images are
the editor's own `--shot` output at its 921 × 691 default window. Nothing was painted into a scene. The guide's HUD picture has
numbered markers drawn on top, and the Retail/Legion and Tactical Dots comparisons
put two captures side by side under text labels.

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

A capture-only patch, not committed, added camera, selection and overlay switches.
It also added the larger naval fleet and the spawned-scene switch (since 0.7.28 with an
optional enemy block; since 0.8.0 a Legion scene also turns on the path service, as
match setup does, without which Legion flyers had no stations). The larger naval
fleet, dropped from the patch after 0.7.26, was added back for 0.8.1. It changes no
simulation code. Every scene runs the normal game rules, AI and renderer.

## Gallery (`docs/img/`)

| Image | Scene |
| --- | --- |
| `title.jpg` | Title menu showing version 0.8.2, 1920 × 1080 window cropped to the 1440 × 1080 artwork |
| `battle.jpg` | **Benchmark** run (Settings → Benchmark, Absurd intensity) on Ulasem Arena, 55 game seconds in. Eight server AIs fight, all five sides. The camera, at 1.3× zoom, shows Aramon (magenta) and Taros (yellow) armies, with Black Dragons, clashing at a sandy arena. A real feature with no fixture; spectator view, so no fog |
| `dragons.jpg` | **Development scene.** Five server AIs, one per side, from a stress-test start (300-unit cap) with gods on, 90 game seconds in. Creon Neo Dragons breathe fire on an Aramon siege column. Spectator view, 1.4× zoom |
| `flyers.jpg` | **Development scene, spawned.** Legion pathfinding, 22 s after one move order east-south-east. A 44-unit Zhon formation: Trolls, Stone Giants and Jungle Orcs, with Gryphons, Harpies and Drakes holding rows of stations over the front of the ground (new in 0.8.0, protocol 254; before, over its middle). `1F`, solid green, marks members of formation 1. Nothing is selected, so the information bar is empty (0.8.1). Fog off, 1.45× zoom. Re-rendered by the 0.8.2 build: the ground units walk past the lone trees (protocol 257) |
| `hud.jpg` | Aramon player in a local-server game, about 10 s in. **Stress-test start**: Normal AI opponent, 250-unit cap. **Scripted orders**: 45 units of eight types (Archers, Swordsmen, Knights, Catapults, Mage Archers, Cannoneers, Horsemen, Rolling Towers) were made formation 1 and selected, so the information bar shows the portrait strip (one portrait per type with its count), FORMATION 1 and 45 SELECTED, and the selected units show `1F` in solid green (new in 0.8.0). The empty right-hand bar is gone (0.8.1). Stats panel on, 1.15× zoom; its VID row reads 87 MB, against 151 MB in the 0.8.0 capture of the same scene. Rendered by the 0.8.1 build |
| `naval.jpg` | **Development scene, spawned.** Cairbray Coast Landing, 10 s in. Veruna Flagship, Men of War, Trebuchet Ships and Harpoon Ships fight Creon Iron Clads, Stern Wheelers and Submersibles. Nothing is selected, so the information bar is empty. 1.6× zoom, fog off. Rendered by the 0.8.1 build |
| `generated.jpg` | Generated Aramon Riverlands map "Silverford" (generator v8, seed 7, 256 × 256, four players). Four Hard AIs at 8× speed, seven game minutes in. Spectator view of a lake shore at 1× |
| `lobby.jpg` | Unchanged from 0.7.25: create-game screen with Ulasem Arena preview and Retail pathfinding |
| `room.jpg` | Unchanged from 0.7.25: local-server lobby with three Normal AIs, example pack selections (empty UI fixtures) and Legion pathfinding |
| `campaign.jpg` | Unchanged from 0.7.25: Book of Darien campaign picker with a fresh profile |
| `crusades.jpg` | Unchanged from 0.7.25: imported Darien Crusades definition on an authenticated local server; no authored battle maps or ownership |
| `streaming.jpg` | Streaming setup with no key or broadcast, showing the **Remember Key** row (off; the Secret Service is available). Rendered after 0.8.0 at 1600 × 1000, with the debug `TAK_CREDSTORE=test` store so no real key could appear |
| `diplomacy.jpg` | Built-in input-test fixture (the debug `TAK_DIPLOMACY_TEST` harness, `--testbuild`) after gifting, 1600 × 1000; Elsin stays selected, so the bar shows her portrait, name and health and mana only. Rendered by the 0.8.1 build; the fixture now shows fog of war, which the 0.7.25 picture did not |
| `results.jpg` | Unchanged from 0.7.25: built-in five-player **sample statistics**, not a finished match |
| `cartographer.png` | Cartographer Units tab on Ulasem Arena, from the 0.8.0 build: the menu bar now has **Symmetry**. A fresh editor profile, `--shot --shot-tool units`, 30% zoom |
| `cartographer-model.png` | Unchanged from 0.7.25: Cartographer F6 model inspector, Aramon Archer |

The 0.7.25 `army.jpg` (an Aramon army standing still, with no combat) and
`gameplay.jpg` (a single barracks) were removed. `battle.jpg` and `hud.jpg`
replace them. Since 0.7.26, `battle.jpg`, `dragons.jpg` and `generated.jpg` are
unchanged: they are spectator views, so none of them shows the information bar, the
stats panel, the Options screen, Tactical Dots, a squad marker or a selection, and
none shows a build-menu portrait
with Smooth GUI Art on (it is off in every capture profile). Texture Filtering is
Off in every profile, which draws exactly as Bilinear Filtering off did. The
unchanged views contain no version text.

## User guide (`docs/img/guide/`)

| Image | Scene |
| --- | --- |
| `hud-annotated.jpg` | From the 0.8.1 build. The 0.7.28 `hud.jpg` setup (Elsin alone selected, so her build menu shows), captured at 1600 × 900 with 1× zoom. Markers: 1 minimap, 2 stats panel, 3 orders, 4 weapons and stances, 5 mana, 6 build menu, 7 information bar, 8 selected Monarch. The bar shows only her portrait, name, health and mana and STANDBY; the right-hand bars do not draw, since she is not building anything (0.8.1). At this window height the stats panel has room for its first four rows only (FPS, PING, SIM, UNITS) |
| `selection-strip.jpg` | **Selection still life.** Ulasem Arena, `--testbuild` with `TAK_SELPORTRAITS=arasword:9,araarch:6,arabow:4,arapal:3,arapult:2`, `TAK_SELPORTRAITS_HURT` and `TAK_SELPORTRAITS_SQUAD=3`, 1920 × 1080 at UI Scale 150%, cropped to the left of the information bar (1160 × 110, not scaled). Five full-size type portraits with counts, running out over the end-cap scroll, GROUP 3, the group's partial health and mana totals and 24 SELECTED. Re-rendered by the 0.8.1 build |
| `side-aramon.jpg` … `side-creon.jpg` | One per side, re-rendered by the 0.8.1 build (the right-hand bars no longer draw). A local-server game with a **stress-test start** (250-unit cap, Normal AI opponent), about 10 s in. The player's Monarch is selected: Elsin, Lokken, Kirenna, Thirsha or the Sage. Each shows that side's interface and build menu |
| `shift-queue.jpg` | From the 0.8.1 build. Elsin with a move and three Shift-queued moves (**scripted orders**), Shift held, about 3 s after the orders; the status reads MOVING. 1.7× zoom |
| `flyers-retail-legion.jpg` | **Development scene, spawned.** From the 0.8.2 build (both halves). The `flyers.jpg` formation and order, 22 s in, run once in Retail (flyers far ahead) and once in Legion (flyers over the leading ground units). The same 1.05× camera is used for both, and the two captures are placed side by side with labels |
| `options-graphics.jpg` | 0.8.1. Options screen scrolled until the whole Graphics section shows (Display's heading is just off the top), cropped to the panel: Fullscreen (OFF), VSync (ON), Max FPS (60, greyed out because VSync is on), Renderer (AUTO), the Texture Filtering slider at OFF, Shadows through Trees Sway in Wind, Tactical Dots (OFF), Tactical Dots Zoom (20%, greyed out), Hardware Cursor, Smooth Motion and the new GPU Memory Limit dropdown at AUTO (1.25 GB). A fresh profile whose only setting is `fullscreen = 0`, so the window takes the capture size; everything else, VSync included, is at its default. Linux build, so the Windows-only Ignore Windows Display Scaling row is absent |
| `fire-on-the-move.jpg` | **From the 0.8.1 build. Development scene, two-player spawned.** Ulasem Arena, Retail pathfinding, 14 s after one plain move order east (**scripted orders**). 14 Aramon Archers and 2 Mage Builders were spawned in a 4-wide grid; 6 Taros Zombies and 6 Skeleton Archers (enemy, no AI) stand south of their path. Shift is held, so the move line shows. Zombies have reached the back of the column, which keeps walking and shooting; the flashes are hits, and five of the sixteen have fallen, so the information bar shows two portraits (2 Mage Builders, 9 Archers) and 11 SELECTED. 1.4× zoom, fog off |
| `cartographer-terrain.jpg` | **New in 0.8.0.** Cartographer Terrain tab on Ulasem Arena (`--shot --shot-tool terrain`, fresh editor profile, 921 × 691): the grouped section browser, All sections (555 pieces, 1261 sections), with readable piece names and variant badges |
| `tactical-dots.jpg` | 0.7.27. **Benchmark** run (Absurd) on Ulasem Arena, 50 game seconds in, spectator view at zoom 0.23 in a 1920 × 1080 window, inside the range where the default Tactical Dots Zoom of 20% shows dots. The deterministic run was rendered twice at the same tick and camera, with Tactical Dots off and on in otherwise identical profiles. The same 800 × 900 region is cropped from each at 1:1 and placed side by side under labels |
| `campaign-briefing.jpg` | *Book of Darien* chapter 1 briefing over the paused battlefield, from a direct mission launch. Re-rendered by the 0.8.1 build: nothing is selected, so the information bar is empty |
| `replay.jpg` | Replay playback of a recorded local-server game, 9 s of 16 s, with the time bar. **Stress-test start**: an Aramon army given one Legion move order (**scripted orders**). Recorded and played back by the 0.8.2 build |
| `benchmark.jpg` | Benchmark run (Absurd), 55 s in; the BENCHMARKING badge counts down the remaining time (0:05 of 60 s): Zhon and Creon armies, with Creon Neo Dragons breathing fire; scaled from 1920 × 1080 |
| `scorecard.jpg` | F4 scorecard over a benchmark battle (Absurd), 55 s in, at 1.1× zoom |

The guide also uses `title.jpg`, `lobby.jpg`, `room.jpg`, `campaign.jpg`,
`diplomacy.jpg`, `generated.jpg` and `cartographer.png` from the gallery.

The other guide images (benchmark and scorecard) are from the 0.7.26 build and
show nothing that 0.7.27 to 0.8.2 changed: they are spectator views with no
information bar, their profiles had the stats panel off and Smooth GUI Art off,
none shows the Options screen, and none has a selection or a squad marker.

The first-launch folder picker is the operating system's own dialog
(`kdialog`/`zenity`, Finder or Explorer), so it has no in-engine screenshot.

## Capture settings

Captures use isolated SDL preference directories (`XDG_DATA_HOME` and
`XDG_CONFIG_HOME`), never a user profile. The window is 1920 × 1080 for the
gallery and 1600 × 900 for the guide, windowed, with VSync off. Shadows and tree
sway are on. Texture Filtering (Off), Smooth GUI Art and Smooth Movies are off, as
by default. The hardware cursor is off for capture. The stats panel is off unless a
caption says it is on.

Game images use `SDL_VIDEODRIVER=offscreen` with `SDL_RENDER_DRIVER=opengl`.
Server-backed scenes run a temporary `takserver --local --no-auth --seed 42`.
Headless scenes (`--mphost`) render the final synchronized frame after the given
number of game seconds. Interactive scenes (the HUD, side shots and replay) render
live and capture after a wall-clock wait, so their FPS reading is real. Screenshot
timing is not a performance measurement.

Capture scripts, job lists and the encoder are in the release-preparation
scratch directories, `$SCR/shared/release-0.7.26/shots`,
`$SCR/shared/release-0.7.27/shots`, `$SCR/shared/release-0.7.28/shots`,
`$SCR/shared/release-0.8.0/shots`, `$SCR/shared/release-0.8.1/shots` and
`$SCR/shared/release-0.8.2/shots`. Raw PNGs, logs, profiles
and the SHA-256 manifest are kept locally in the worktree's ignored
`build-o2/capture` folder. Keep retail archives, extracted assets and temporary
capture files out of Git. When refreshing, rerender and inspect every view, then
update these notes and the captions.
