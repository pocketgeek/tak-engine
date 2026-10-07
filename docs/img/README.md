# README and user-guide screenshots

Refreshed on 2026-10-07 for **0.7.26**. Every image below is actual engine output
from the 0.7.26 release-preparation build (Debug with `-O2`, OpenGL renderer) and
was inspected by eye. Encoding is the only change after capture: JPEG with full
chroma resolution, quality 90 for the title, 88 for the gallery and 86 for the
guide; guide images are scaled from 1600 × 900 to 1280 × 720. The title is cropped
to its 4:3 artwork. Nothing was painted into a scene. The guide's HUD picture has
numbered markers drawn on top, and the Retail/Legion comparison puts two captures
side by side under text labels.

Several scenes were set up with debug-build harnesses. Release builds do not have
these switches. Captions say when a picture is a **development scene**:

- **Stress-test start.** Each AI or player begins with about 95% of the unit cap
  in combat units, in a grid. This is the debug `TAK_STRESS` fixture.
- **Gods on.** Gods appear at the start. This is the debug `TAK_GODS` switch.
- **Spawned scene.** The local harness places units and gives the move order
  itself. There is no server, and it is not a real match.
- **Scripted orders.** The debug harness gives the player's orders through the
  game's own selection and right-click order code, as a click would.

A capture-only patch, not committed, added camera, selection and overlay switches.
It also added the larger naval fleet and the spawned-scene switch. It changes no
simulation code. Every scene runs the normal game rules, AI and renderer.

## Gallery (`docs/img/`)

| Image | Scene |
| --- | --- |
| `title.jpg` | Title menu showing version 0.7.26, 1920 × 1080 window cropped to the 1440 × 1080 artwork |
| `battle.jpg` | **Benchmark** run (Settings → Benchmark, Absurd intensity) on Ulasem Arena, 55 game seconds in. Eight server AIs fight, all five sides. The camera, at 1.3× zoom, shows Aramon (magenta) and Taros (yellow) armies, with Black Dragons, clashing at a sandy arena. A real feature with no fixture; spectator view, so no fog |
| `dragons.jpg` | **Development scene.** Five server AIs, one per side, from a stress-test start (300-unit cap) with gods on, 90 game seconds in. Creon Neo Dragons breathe fire on an Aramon siege column. Spectator view, 1.4× zoom |
| `flyers.jpg` | **Development scene, spawned.** Legion pathfinding, 22 s after one move order. A 44-unit Zhon formation: Trolls, Stone Giants and Jungle Orcs, with Gryphons, Harpies and Drakes holding stations over them (new in 0.7.26). `1F` marks members of formation 1. Fog off, 1.8× zoom |
| `hud.jpg` | Aramon player in a local-server game, about 12 s in. **Stress-test start**: Normal AI opponent, 250-unit cap. Elsin is selected, so her build menu and portrait show. Stats panel on, 1.15× zoom |
| `naval.jpg` | **Development scene, spawned.** Cairbray Coast Landing, 10 s in. Veruna Flagship, Men of War, Trebuchet Ships and Harpoon Ships fight Creon Iron Clads, Stern Wheelers and Submersibles. 1.6× zoom, fog off |
| `generated.jpg` | Generated Aramon Riverlands map "Silverford" (generator v8, seed 7, 256 × 256, four players). Four Hard AIs at 8× speed, seven game minutes in. Spectator view of a lake shore at 1× |
| `lobby.jpg` | Unchanged from 0.7.25: create-game screen with Ulasem Arena preview and Retail pathfinding |
| `room.jpg` | Unchanged from 0.7.25: local-server lobby with three Normal AIs, example pack selections (empty UI fixtures) and Legion pathfinding |
| `campaign.jpg` | Unchanged from 0.7.25: Book of Darien campaign picker with a fresh profile |
| `crusades.jpg` | Unchanged from 0.7.25: imported Darien Crusades definition on an authenticated local server; no authored battle maps or ownership |
| `streaming.jpg` | Unchanged from 0.7.25: streaming setup with no key or broadcast |
| `diplomacy.jpg` | Unchanged from 0.7.25: built-in input-test fixture after gifting |
| `results.jpg` | Unchanged from 0.7.25: built-in five-player **sample statistics**, not a finished match |
| `cartographer.png` | Unchanged from 0.7.25: Cartographer Units tab on Ulasem Arena |
| `cartographer-model.png` | Unchanged from 0.7.25: Cartographer F6 model inspector, Aramon Archer |

The 0.7.25 `army.jpg` (an Aramon army standing still, with no combat) and
`gameplay.jpg` (a single barracks) were removed. `battle.jpg` and `hud.jpg`
replace them. The unchanged views contain no version text and still match 0.7.26.

## User guide (`docs/img/guide/`)

| Image | Scene |
| --- | --- |
| `hud-annotated.jpg` | The same setup as `hud.jpg`, captured at 1600 × 900 with 1× zoom. Markers: 1 minimap, 2 stats panel, 3 orders, 4 weapons and stances, 5 mana, 6 build menu, 7 information bar, 8 selected Monarch |
| `side-aramon.jpg` … `side-creon.jpg` | One per side. A local-server game with a **stress-test start** (250-unit cap, Normal AI opponent), about 10 s in. The player's Monarch is selected: Elsin, Lokken, Kirenna, Thirsha or the Sage. Each shows that side's interface and build menu. `side-creon.jpg` was recaptured after 0393b54 (Creon's Iron Plague panel art); the other four were unchanged by that fix, verified by pixel comparison of the panels |
| `shift-queue.jpg` | Elsin with a move and three Shift-queued moves (**scripted orders**), Shift held, about 3 s after the orders. 1.7× zoom |
| `flyers-retail-legion.jpg` | **Development scene, spawned.** The `flyers.jpg` formation and order, 22 s in, run once in Retail and once in Legion. The same 1.05× camera is used for both, and the two captures are placed side by side with labels |
| `options-graphics.jpg` | Options screen scrolled to Graphics, cropped to the panel. Every Graphics setting is at its default; Display shows the windowed capture profile |
| `campaign-briefing.jpg` | *Book of Darien* chapter 1 briefing over the paused battlefield, from a direct mission launch |
| `replay.jpg` | Replay playback of a recorded local-server game, 9 s of 16 s, with the time bar. **Stress-test start**: an Aramon army given one Legion move order (**scripted orders**) |
| `benchmark.jpg` | Benchmark run (Absurd), 55 s in; the BENCHMARKING badge counts down the remaining time (0:05 of 60 s): Zhon and Creon armies, with Creon Neo Dragons breathing fire; scaled from 1920 × 1080 |
| `scorecard.jpg` | F4 scorecard over a benchmark battle (Absurd), 55 s in, at 1.1× zoom |

The guide also uses `title.jpg`, `lobby.jpg`, `room.jpg`, `campaign.jpg`,
`diplomacy.jpg`, `generated.jpg` and `cartographer.png` from the gallery.

The first-launch folder picker is the operating system's own dialog
(`kdialog`/`zenity`, Finder or Explorer), so it has no in-engine screenshot.

## Capture settings

Captures use isolated SDL preference directories (`XDG_DATA_HOME` and
`XDG_CONFIG_HOME`), never a user profile. The window is 1920 × 1080 for the
gallery and 1600 × 900 for the guide, windowed, with VSync off. Shadows and tree
sway are on. Bilinear Filtering, Smooth GUI Art and Smooth Movies are off, as by
default. The hardware cursor is off for capture. The stats panel is off unless a
caption says it is on.

Game images use `SDL_VIDEODRIVER=offscreen` with `SDL_RENDER_DRIVER=opengl`.
Server-backed scenes run a temporary `takserver --local --no-auth --seed 42`.
Headless scenes (`--mphost`) render the final synchronized frame after the given
number of game seconds. Interactive scenes (the HUD, side shots and replay) render
live and capture after a wall-clock wait, so their FPS reading is real. Screenshot
timing is not a performance measurement.

Capture scripts, job lists and the encoder are in the release-preparation
scratch directory, `$SCR/shared/release-0.7.26/shots`. Raw PNGs, logs, profiles
and the SHA-256 manifest are kept locally in the worktree's ignored
`build-o2/capture` folder. Keep retail archives, extracted assets and temporary
capture files out of Git. When refreshing, rerender and inspect every view, then
update these notes and the captions.
