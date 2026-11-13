# Documentation pictures

The pictures used by the README, the [user guide](../user-guide.md), the
[quick start](../quick-start.md), the [Cartographer guide](../cartographer-guide.md),
[streaming.md](../streaming.md) and [public-server.md](../public-server.md). Every
picture is real engine or editor output from a Debug build with `-O2` (OpenGL
renderer, version 1.0.0; `title.jpg` version 1.0.5, `guide/random-map-lobby.jpg` version 1.0.2), inspected by eye. Nothing is painted into a scene; the only
additions are the numbered markers on `guide/hud-annotated.jpg` and
`cartographer/window-annotated.jpg` and the text labels over the side-by-side
composites, which the captions name.

This file says what each picture shows and how to take it again.

## Capture settings

- **Build.** A Debug build configured with `-DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-O2`
  (the capture switches below are debug-only; a release build has none of them).
- **Video.** `SDL_VIDEODRIVER=offscreen`, `SDL_RENDER_DRIVER=opengl`,
  `SDL_AUDIODRIVER=dummy`; the window size is given with `--winsize W H` and the
  picture written with `--shot <png>`.
- **Profiles.** Every run has its own scratch `HOME`, `XDG_DATA_HOME`,
  `XDG_CONFIG_HOME` and `TAK_SETTINGS_FILE`, never a user profile. The base profile is
  `base-settings.ini` (below): windowed, VSync off, the default graphics (Texture
  Filtering Everything, Smooth GUI Art and Smooth Movies on, Tactical Dots on at 0%
  zoom, Screen Shake off, shadows and tree sway on), stats panel off, and the hardware
  cursor on, so no pointer is drawn. The Options pictures use a fresh profile whose
  only setting is `fullscreen = 0`.
- **Data folder.** A scratch install that links the retail archives, `Maps`, `Movies`
  and `Music` and has its own empty caches, so nothing is written into a real install.
  Its `mods/` holds the always-enabled cosmetic pack *click*
  (`mods/cosmetic/always_enabled/click/click.hpi`), the cosmetic packs *Alternate
  Cursors* and *Classic Sounds* and the game pack *Big Battles* (placeholder folders
  with one tiny file each, for the pack lists). Variants: one that also has the
  always-enabled game pack *House Rules* (`mods/game/always_enabled/House Rules/`),
  one with only *click*, and one with no packs.
- **Servers.** Server-backed scenes run a private loopback
  `takserver --local --no-auth --seed 42` (the game browser: `--local` with a scratch
  account file), never the public server.
- **Timing.** Headless scenes render the synchronized frame after the given number of
  game seconds. Interactive scenes (the HUD, side and replay pictures) render live and
  are captured after a wall-clock wait, so their FPS readings are real. Screenshot
  timing is not a performance measurement.
- **Encoding.** JPEG with full chroma resolution (`subsampling=0`): quality 90 for
  `title.jpg`, 88 for the other root pictures, 86 for `guide/`, `quick/` and
  `cartographer/` (98 for three AI Profiles pictures, as noted). Only cropping and
  scaling are applied after capture.

### Where the capture scripts are

| Folder | What it holds |
| --- | --- |
| `~/tak-tmp/rel100/pictures/` | Front-end and room pictures: `menu.py` (one job per picture), `jobs.json` (the room jobs), `base-settings.ini`, `encode.py` (each picture's crop, scale and quality) and `capture-connC.diff` (the one capture-only hook, for `mp-connect-custom.jpg`) |
| `~/tak-tmp/scratch/release-091/shots/inst` | The scratch install with *click*, the two cosmetic packs and *Big Battles* |
| `~/tak-tmp/scratch/release-090/front/shots/` | The install variants `instM` (adds *House Rules*), `instP` (*click* only) and `instP0` (no packs); `kmp/Cartographer Test.kmp` for the Test Map room; `dw.sh` for the disconnect banner |
| `~/tak-tmp/scratch/release-090/game/` | In-game pictures: `jobs.json` (scene, seed, moment, window and crop per picture), `drv.py`, `run1.py`, `encode.py` and `capture-090.patch`, the capture-only patch (camera, selection, overlay and spawned-scene switches; not committed) |
| `~/tak-tmp/scratch/retailfog/shots/brief.sh` | The campaign briefing picture |
| `~/tak-tmp/scratch/release-090/carto/` | Cartographer pictures: the capture driver `capdrv.cpp` with `build.sh`, `run.sh`, `scenes.inc` and `finish.py` (markers and composites) |

The in-game patch is written against an older source tree and has to be ported to
the current one before it applies. Three of the in-game pictures also used two small
hooks that are not in it: `TAK_CAP_BENCHSEC` (the Benchmark badge's seconds, which a
headless run does not advance) and `TAK_CAP_NONAME` (no player name on the Monarch's
panel).

### Front-end and room recipes

Front-end pictures are `takclient menu --shot` with one capture switch each; `menu.py`
has the exact environment, window and profile per job:

- `TAK_SHOT_SERVER=4` the CONNECT TO SERVER panel with two sample remembered servers
  (192.168.1.50:7677, example.dyndns.org), account *aldric* and a typed password;
  `=2` the same panel with NEW SERVER... selected (with `capture-connC.diff`,
  `TAK_CAP_SERVERTEXT` and `TAK_CAP_LOGINUSER` fill the address and the name).
  Nothing connects to any server.
- `TAK_SHOT_STREAM=1` (with `TAK_CREDSTORE=test`, so no real key can appear) the
  streaming window; `TAK_SHOT_CAMPAIGN=0` the campaign book; `TAK_SHOT_RESULT=<png>`
  the result screen with built-in sample statistics.
- `TAK_SHOT_OPTIONS=graphics|interface|voice` an Options page.

Room pictures launch the menu with `TAK_MENU_PICK=singleplayer` and `TAK_NODOORVID=1`,
which opens the single-player room at once from the profile's remembered settings:
`spRoom.map` names the map (`spRoom.generator` the RANDOM MAP tab's recipe), the other
`spRoom.*` keys set the rules, and `mods.cosmeticPack` and `spRoom.gamePack` (hex
pack names) tick packs. `TAK_ROOM_SCRIPT` seats the AIs and sets rules
(`ai:1;ai:2`, `opt:randomStarts=2`), `TAK_SHOT_CLICKS` clicks the rest (the AIs'
sides, CHANGE MAP..., a panel tab, MAP SCENARIO, MODS...) with waits between and
then moves the pointer to a corner, and `TAK_SHOT_SIZES` captures the live window.
The AI names are the room's own random picks, so they differ from run to run.

## Development scenes

Several scenes are set up with debug-build harnesses. Captions say when a picture is
a **development scene**:

- **Stress-test start.** Each AI or player begins with about 95% of the unit cap in
  combat units, in a grid (the debug `TAK_STRESS` fixture).
- **Gods on.** Gods appear at the start (the debug `TAK_GODS` switch).
- **Spawned scene.** The local harness places units and gives the move order itself.
  There is no server, and it is not a real match.
- **Scripted orders.** The harness gives the player's orders through the game's own
  selection and right-click order code, as a click would.
- **Two-player spawned scene.** As a spawned scene, but a second block of units is
  placed for an enemy player (player 1), which has no AI and gives no orders.
- **Selection still life.** The debug `TAK_SELPORTRAITS` fixture (with `--testbuild`)
  spawns a mixed roster at the map centre and selects it; `TAK_SELPORTRAITS_HURT`
  lowers some units' health and mana and `TAK_SELPORTRAITS_SQUAD` puts them in one
  group.

Every scene runs the normal game rules, AI and renderer; the capture patch changes no
simulation code.

## Gallery (`docs/img/`)

| Image | Scene |
| --- | --- |
| `title.jpg` | The title menu with its three doors, reading VERSION 1.0.5. `menu` job `title`: 1920 × 1080, cropped to the 1440 × 1080 artwork at (240, 0) |
| `mp-connect.jpg` | The Multiplayer door's CONNECT TO SERVER panel: tak.pgnet.us (DEFAULT) selected, LOCAL SERVER (HOST), NEW SERVER..., the two sample remembered servers each with DELETE, ACCOUNT NAME *aldric* and PASSWORD as pips side by side, A NEW NAME IS REGISTERED WHEN YOU SIGN IN, and BACK, SPECTATE and CONNECT. `TAK_SHOT_SERVER=4`, 1600 × 900, the panel's contents (360, 244, 1240, 656) scaled to 760 × 356 |
| `mp-connect-custom.jpg` | The same panel with NEW SERVER... selected: the address box under it holds *tak.example.org*, the account name *aldric*, the password empty. `TAK_SHOT_SERVER=2` with `capture-connC.diff`, 1600 × 900, (360, 216, 1240, 684) scaled to 760 × 404 |
| `battle.jpg` | **Benchmark** run (Absurd) on Ulasem Arena, 55 game seconds in: eight server AIs of all five sides; at 1.3× zoom Aramon (magenta) and Taros (yellow) armies with Black Dragons clash in a sandy arena, damaged units of every side showing health bars. Spectator view, no fog. `TAK_CAP_BENCHSEC=55`, so the badge reads 0:05; 1920 × 1080 |
| `dragons.jpg` | **Development scene.** Five server AIs, one per side, from a stress-test start (300-unit cap) with gods on, 90 game seconds in: Creon Neo Dragons breathe fire on an Aramon siege column. Spectator view, 1.4× zoom, 1920 × 1080 |
| `hud.jpg` | Aramon player in a local-server game, about 10 s in. **Stress-test start** (Normal AI opponent, 250-unit cap) and **scripted orders**: 45 units of eight types made formation 1 and selected, so the information bar shows one portrait per type with its count, FORMATION 1 and 45 SELECTED, and the units show `1F`. Stats panel on (its CLOCK row is the capture machine's clock), Idle Builders portraits, 1.15× zoom, 1920 × 1080 |
| `naval.jpg` | **Development scene, spawned.** Cairbray Coast Landing, 10 s in: a Veruna Flagship, Men of War, Trebuchet Ships and Harpoon Ships fight Creon Iron Clads, Stern Wheelers and Submersibles. Nothing selected, 1.6× zoom, fog off, 1920 × 1080 |
| `generated.jpg` | The generated Aramon Riverlands map *Silverford* (seed 13, 512 × 512 cells, four players, Scattered Mana Medium, Plateaus 96, Mountains 48, Towns 64: `~gen1~0a000d00000000000000000002000204aa6e808296050260304053696c766572666f7264`). Local server with `--seed 42` and four level-3 AIs at `TAK_SPEED=80`; spectator view at (2912, 6672), 1×, 660 game seconds in: the lake shore, an AI's lodestone among standing stones on a neutral scattered mana spot, and a plateau rim. 1920 × 1080 |
| `lobby.jpg` | The CHANGE MAP panel over a single-player room whose remembered map is Ulasem Arena: the EXISTING MAP tab, the list sorted by name with Ulasem Arena selected, the preview numbering the eight start positions above 20 X 20 8 PLAYER, MAP SCENARIO: OFF greyed (THIS MAP HAS NO SCENARIO), CANCEL and PICK; the room dimmed at the left. Room job `lob`, 1600 × 900 |
| `lobby-scenario.jpg` | The CHANGE MAP panel on King of the Hill with its **MAP SCENARIO** clicked ON: the gold **S** marks maps with a playable scenario, the lines under the button say what the scenario changes (UNITS PLACED FOR 1-4, NEEDS SEATS 1-4, BUILDS 6 UNIT TYPES, ENDS BY SCENARIO RULES), and the preview numbers the four starts. Room job `scen`, 1600 × 900 scaled to 1280 × 720. Used by the user guide |
| `room.jpg` | Single-player room on Ulasem Arena from remembered settings with Crusades, Double Sight and Legion pathfinding on and *Alternate Cursors* and *Classic Sounds* ticked; three Normal AIs seated by `TAK_ROOM_SCRIPT=ai:1;ai:2;ai:3`, their sides clicked to Taros, Veruna and Zhon. The host's row has WATCH, the open rows 5 to 8 have MOVE HERE greyed (START LOCATIONS: FIXED), the rules are buttons under START and LEAVE, the map preview marks starts 1 to 4 in the players' colours, and under it are CHANGE MAP... and the MODS line (click, Alternate Cursors +1) with MODS.... Room job `room`, 1600 × 900 |
| `campaign.jpg` | The Book of Darien campaign book with a fresh profile, chapter 1: retail CHAPTER lettering, the chapter title and the always-on mods note (click) in the UI font. `TAK_SHOT_CAMPAIGN=0`, 1600 × 1000 |
| `streaming.jpg` | The YouTube Streaming window with no key or broadcast: KEY NOT SET with PASTE, REMEMBER KEY (off; SECRET SERVICE), resolution, frame rate 60, bitrate 6000 Kbps with RECOMMENDED, encoder Automatic, SAVE A LOCAL COPY (off), START STREAMING, the status OFF with its counters, and BACK; VERSION 1.0.0 shows below the panel. `TAK_SHOT_STREAM=1`, 1600 × 1000, not cropped |
| `diplomacy.jpg` | The built-in input-test fixture (`TAK_DIPLOMACY_TEST`, `--testbuild`) after gifting, 1600 × 1000, in a profile with voice chat on. The capture patch keeps a voice engine on in the fixture, so the VOLUME sliders draw live, and sets the CHAT and VOICE columns as a game starts: you and ALLY ticked (your own VOICE box greyed), ENEMY, DEFEATED and SPECTATORS off. VOLUME has sliders for the two other humans (ALLY 50%, DEFEATED 25%) and none for the computer player |
| `results.jpg` | The result screen with built-in five-player **sample statistics** (`TAK_SHOT_RESULT`), not a finished match. 1600 × 1000 |
| `cartographer.png` | Cartographer's Units tab on Ulasem Arena: the five-tool toolbar (TERRAIN, FEATURES, UNITS, STARTS, BLOCKED), the browser with its scrollbar, and the minimap. The editor's own `--shot --shot-tool units`, fresh editor profile, 921 × 691, 30% zoom |

## User guide (`docs/img/guide/`)

Game pictures here are captured at 1600 × 900 and scaled to 1280 × 720 unless the row
says otherwise.

| Image | Scene |
| --- | --- |
| `hud-annotated.jpg` | The `hud.jpg` setup with Elsin alone selected, so her build menu shows, at 1× zoom. Markers: 1 minimap, 2 stats panel, 3 orders, 4 weapons and stances, 5 mana, 6 build menu, 7 information bar, 8 selected Monarch. At this window height the stats panel shows its first four rows (FPS, PING, SIM, UNITS) |
| `selection-strip.jpg` | **Selection still life.** Ulasem Arena, `TAK_SELPORTRAITS=arasword:9,araarch:6,arabow:4,arapal:3,arapult:2` with `TAK_SELPORTRAITS_HURT` and `TAK_SELPORTRAITS_SQUAD=3`, 1920 × 1080 at UI Scale 150%, cropped to the left of the information bar (1160 × 110): five type portraits with counts, GROUP 3, the group's health and mana totals and 24 SELECTED |
| `side-aramon.jpg` … `side-creon.jpg` | One per side: a local-server game with a **stress-test start** (250-unit cap, Normal AI opponent), about 10 s in, the player's Monarch selected (Elsin, Lokken, Kirenna, Thirsha or the Sage), showing that side's interface and build menu |
| `shift-queue.jpg` | Elsin with a move and three Shift-queued moves (**scripted orders**), Shift held, about 3 s after the orders; the status reads MOVING. 1.7× zoom |
| `flyers-retail-legion.jpg` | **Development scene, spawned.** A Zhon formation of ground units and flyers given one formation move order, 22 s in, run once with Retail pathfinding (flyers far ahead) and once with Legion (flyers over the leading ground units), the same 1.05× camera, side by side under labels (1610 × 450) |
| `options-graphics.jpg` | The GRAPHICS page: Display (Fullscreen OFF, VSync ON, Max FPS 60 greyed, Native Wayland Auto) and Graphics (Renderer Auto (OpenGL), Texture Filtering Everything, Shadows, Smooth GUI Art, Smooth Movies and Trees Sway in Wind ON, Hidden Units Fade with Fade Opacity 50%, Smooth Motion ON, GPU Memory Limit 1.25 GB (default)), DEFAULTS and BACK. `TAK_SHOT_OPTIONS=graphics`, fresh profile, 1600 × 1100, cropped to the panel (840 × 844 at (380, 126)). A Linux capture, so the Native Wayland row shows and the Windows-only display-scaling row does not |
| `fire-on-the-move.jpg` | **Development scene, two-player spawned.** Ulasem Arena, Retail pathfinding, 14 s after one plain move order east (**scripted orders**): 14 Aramon Archers and 2 Mage Builders in a 4-wide grid walk past 6 Taros Zombies and 6 Skeleton Archers (enemy, no AI). Shift is held, so the move line shows; the column keeps walking and shooting, three of the sixteen have fallen, and the bar reads 13 SELECTED. 1.4× zoom, fog off |
| `cartographer-terrain.jpg` | Cartographer's Terrain tab on Ulasem Arena (`--shot --shot-tool terrain`, fresh editor profile, 921 × 691): the grouped section browser, All sections (555 pieces, 1261 sections), piece names, variant badges and the scrollbar |
| `tactical-dots.jpg` | **Benchmark** run (Absurd) on Ulasem Arena, 50 game seconds in, spectator view at zoom 0.10 (the zoom-out floor), rendered twice at the same tick and camera with Tactical Dots off and on. The same 800 × 900 region (the east of the map) is cropped from each 1920 × 1080 capture at 1:1 and placed side by side under labels (1610 × 900) |
| `campaign-briefing.jpg` | *Book of Darien* chapter 1 briefing over the paused battlefield, from a direct mission launch (`--campaign takmission01_mt`, `TAK_SHOT_BRIEFING`; `brief.sh`): the retail PAUSED heading, the chapter, title and objectives in the UI font, *Always-on mods: click*, ground never seen black and the squad's sight clear. Nothing selected |
| `replay.jpg` | Replay playback of a recorded local-server game, 9 s of 16 s, with the time bar: a **stress-test start**, an Aramon army given one Legion move order (**scripted orders**). 1024 × 768, not scaled |
| `benchmark.jpg` | A Benchmark run (Absurd), 55 s in, the BENCHMARKING badge reading 0:05 (`TAK_CAP_BENCHSEC=55`): armies meet at the edge of a sandy clearing, with health bars for every side. 1920 × 1080, scaled |
| `scorecard.jpg` | The F4 scorecard over a Benchmark battle (Absurd), 55 s in, at 1.1× zoom: eight AI players with kills, losses and score. 1920 × 1080 (so the badge clears the Score column), scaled |
| `voice-chat.jpg` | The VOICE CHAT page with voice chat on and every other voice setting at its default: GENERAL (VOICE CHAT ON), MICROPHONE (MICROPHONE ON, TALK MODE Push to Talk, PUSH TO TALK on SPACE, MIC DEVICE System Default, MIC GAIN 100%, an empty MIC LEVEL, ACTIVATION LEVEL 15 greyed), OUTPUT (Same as Game Output, volume 100), DEFAULTS and BACK. `TAK_SHOT_OPTIONS=voice`, a profile with `voiceEnabled = 1`, 1600 × 900, cropped to the panel (832 × 696 at (384, 101)). No voice engine runs under `--shot`, so the level meter is empty |
| `random-map-lobby.jpg` | The CHANGE MAP panel's **RANDOM MAP** tab over a single-player room, opened on the remembered recipe `~gen1~0a00ea0700000000000000000280010480608060800003603040` (Aramon, Mainland, 4 players, 16 × 12, Scattered Mana High, seed 2026): MAP NAME (OPTIONAL) empty, the sliders in two columns, RE-ROLL SEED, the preview (the map's radar picture, made from its terrain) with four numbered starts and 16 x 12 4 PLAYER, CANCEL and PICK. Room job `rnd`, 1600 × 900, not cropped or scaled |
| `chat-box.jpg` | The in-game chat box at its defaults (Chat Position Centered, Chat Scale 100%) above the build row and its TRAINING Horseman (10) line, on Ulasem Arena. **Selection still life** (`TAK_SELPORTRAITS=arakeep:1`): a Barracks selected, with a Horseman queue of ten and four chat lines (Ludwin, Aldric, Kirenna, Pat in their player colours) put in by the capture patch, and *on my way* being typed after SAY:. Cropped to the box, the row and the information bar (1240 × 340, not scaled) |
| `options-interface.jpg` | The USER INTERFACE page's Interface section at its defaults: MODERN UI FONT ON, F4 SCORECARD SCALE and UI SCALE 100%, HEALTH BARS Damaged, STATS PANEL ON, BUILD MENU Center at 100%, IDLE BUILDERS Middle at 50%, CHAT POSITION Centered and CHAT SCALE 100%. `TAK_SHOT_OPTIONS=interface`, fresh profile, 1600 × 1100, cropped to the section (824 × 530 at (388, 108)) |
| `room-mods-power-codes.jpg` | Single-player room on Ulasem Arena in the install with *House Rules*: POWER CODES: ON from the remembered settings, two Normal AIs (Taros, Veruna); the MODS line names click and House Rules (always enabled, in green), then Classic Sounds and +1 (Big Battles), with MODS... and GAME MODS VERIFIED under it. Room job `roomPC`, 1600 × 900 |
| `mods-screen.jpg` | The MODS screen opened from that room with MODS...: COSMETIC MODS and GAME MODS side by side, each 2 / 64 ON with a FILTER box; click and House Rules ALWAYS ON at the top, Classic Sounds and Big Battles ticked, Alternate Cursors not. Room job `mods`, 1600 × 900, cropped to (0, 1, 1600, 371) |
| `build-line.jpg` | A Shift-drag wall line in progress: the debug `TAK_BUILDLINE_SHOT=560,0,2` fixture (`--testbuild`, Ulasem Arena) selects Elsin, chooses the Aramon wall, holds Shift, drags 560 px east and turns the wheel up two notches, so the preview shows nine wall sites two cells apart and the **GAP 2** label by the pointer. 1.5× zoom, fog off |
| `disconnect-wait.jpg` | Two humans on Inner Circle through a local `takserver --local --no-auth --seed 1` at the default 8 s stall threshold; the second client (Brannoc, headless) is stopped with `SIGSTOP` 40 s in, and the first (Aldric) is captured about 4 s after the banner appears: the game is paused and reads WAITING FOR Brannoc 18, and the ground outside Elsin's sight lies under Aramon's white mist. Nothing selected. `dw.sh` |
| `game-browser.jpg` | The online game browser of a client signed in as *aldric* to a private loopback `takserver --local` with a scratch account file. Headless debug clients signed in with `TAK_MENU_PICK=connect:<name>:<password>@<address>` make the games with `TAK_ROOM_SCRIPT`, each from its own remembered `mpRoom.map`: `create:Friday Night Skirmish` (Ulasem Arena, 1/8), `create:Clan Practice:oak` (King of the Hill, 1/4, locked), `create:Evening Ladder;wait:humans=2;ready;wait:ready=2;start` with a second client's `join:Evening Ladder;ready` (Inner Circle, 2/4 LIVE with WATCH) and `create:Newcomers Welcome` (Ladron's Tarn, 1/8). NAME, MAP and PLAYERS columns, REFRESH and CREATE at the top, BACK at the bottom. The viewer runs with `--shot` and `TAK_SHOT_CLICKS` only waits and moves the pointer to a corner |
| `build-menu-wrap.jpg` | A Zhon Beast Tamer's nine-icon conjure menu wrapped into two rows (five above four) left of the command panel and the mana orb. **Selection still life** (`--testbuild --side zon`, `TAK_SELPORTRAITS=zontrain:1`, local server), Ulasem Arena, 1024 × 768 at Build Menu Scale 150% (the menu fits one row at 100%); not scaled |

The guide also uses `title.jpg`, `mp-connect.jpg`, `lobby.jpg`, `lobby-scenario.jpg`,
`room.jpg`, `campaign.jpg`, `diplomacy.jpg`, `generated.jpg` and `cartographer.png`.
The first-launch folder picker is the operating system's own dialog
(`kdialog`/`zenity`, Finder or Explorer), so it has no picture.

## Cartographer guide (`docs/img/cartographer/`)

Pictures for the [Cartographer user guide](../cartographer-guide.md), taken in three
ways:

- **The capture driver** (`capdrv.cpp`, kept outside Git) links the editor objects
  that the `cartographer_ui_test` target builds and drives the editor through its own
  SDL input path, as a player's clicks and keys would: menu and toolbar clicks,
  browser picks, drags, key presses and a warped pointer for hover states. It waits
  until thumbnails, portraits and feature sprites have loaded and the terrain is
  composed before each capture.
- **The `cartographer_ui_test` workflows' capture mode** (`TAK_EDITOR_TEST_CAPTURE`
  names a file prefix) for the AI Profiles pictures and the empty window.
- **The editor's own `--shot`** for `blocked.jpg`, `../cartographer.png` and
  `../guide/cartographer-terrain.jpg`.

Each run uses the SDL `offscreen` driver, which gives the editor's default
921 × 691 window, and scratch `XDG_CONFIG_HOME`, `XDG_DATA_HOME`,
`TAK_CARTOGRAPHER_PROFILE` and `TAK_SETTINGS_FILE`. The data folder links the retail
archives into a scratch install with an empty `Maps` folder, seen through a private
mount namespace (`unshare`) at `/home/player/Games/TA Kingdoms`, so the paths in
Save As, Open Map and Preferences are neutral. *Twin Rivers*, *Silverford* and
*Practice* are maps made in the editor during the captures; *Ulasem Arena*, *King of
the Hill* and *Ladron's Tarn* are shipped maps, never saved over. Pictures are the
whole window or a crop of it; JPEG quality 86 unless noted.

| Image | Scene |
| --- | --- |
| `window-annotated.jpg` | Ulasem Arena, Terrain tool, view centred on the arena at 35%, whole window. Markers 1–13 drawn on a copy with Pillow: menu bar, tools, modes, REGIONS, TEST MAP, search, group button, counter line, variant badge, scrollbar, canvas, minimap, status bar |
| `empty-window.jpg` | A bare launch with a fresh profile, the first run's welcome closed: title *Cartographer*, menu bar only, an empty toolbar, the centred NO MAP OPEN hint naming File → New map (Ctrl+N), Open map (Ctrl+O) and the AI mod commands, and NO MAP OPEN in the status bar. The `blank-launch` workflow, with a capture-only line that writes `<prefix>-empty.png` once the welcome is closed; whole window |
| `new-map.jpg` | The same bare launch after File → New map: the New Map dialog (Untitled, 8 × 8, aramon) over the dimmed empty window (`<prefix>-newmap.png` from the same capture-only line); whole window |
| `random-generator.jpg` | Composite. Left: Random Map Settings (seed 2026, 4 players, Aramon Riverlands, 16 × 16, SCATTERED MANA (BY MAP AREA) Medium, plateau, mountain and town fields at their defaults). Right: the Generated Map Preview it produced. Dialog crops (1103 × 508) |
| `terrain-groups.jpg` | Ulasem Arena, the terrain group list open, whole window |
| `terrain-variant.jpg` | Ulasem Arena, Cliff W clicked twice (variant 2/3) with the pointer on it, so the status bar shows its full name. The left 470 px |
| `symmetry-stamp.jpg` | Composite of two crops. Ulasem Arena, live symmetry (vertical axis), Low Sandy to High Sandy *Transition NW outer* chosen. Left: the preview before the click; right: the stamped section and its twin |
| `terrain-clipboard.jpg` | Composite of two crops. Ulasem Arena, Terrain + Select: a 15 × 15-block rectangle (left); after Ctrl+C and Ctrl+V, the clipboard brush under the pointer (right) |
| `symmetrize.jpg` | Ulasem Arena, Shift+M, the Symmetrize dialog. Dialog crop |
| `features.jpg` | Ulasem Arena, Features tool, category *trees*: five palm trees placed by clicking and the preview of a sixth under the pointer, whole window |
| `features-select.jpg` | Composite of two crops of the same trees: the selection box being dragged in Select mode, then the selected anchor cells |
| `units.jpg` | King of the Hill, Units tool filtered to GROUND and ARAMON; eight of Player 1's units box-selected in Select mode at 75%, whole window |
| `unit-properties.jpg` | King of the Hill, Unit Properties of one of Player 1's Archers (Shift+Enter in Placed units) with *Captain Aldric* typed as the display name, not applied. Dialog crop |
| `unit-model.jpg` | Ulasem Arena, the Units browser searched for *knight*, F6 on the Aramon Knight. Crop of the browser and the model window |
| `starts.jpg` | Ulasem Arena, Starts tool, View → Fit map (7%), whole window |
| `regions.jpg` | King of the Hill at Fit map, REGIONS on in Select mode with The Hill selected, whole window |
| `scripting.jpg` | King of the Hill, Scenario Scripting with Rule 1 of All Players selected. Crop of the scripting window |
| `rule-editor.jpg` | The same, with Rule 1's condition open for editing and its LOCATION list opened with Alt+Down. Crop of the scripting window |
| `templates.jpg` | King of the Hill, Objective Templates. Dialog crop |
| `check-map.jpg` | *Practice*: a new 8 × 8 Aramon map with one start, a Horseman, and a War Galley placed on dry land; then Check Map. Dialog crop |
| `overlay-movement.jpg` | Ulasem Arena at 40%, Horseman chosen in the Units browser, View → Overlay: movement, whole window |
| `preferences.jpg` | View → Preferences with default settings. Dialog crop |
| `open-map.jpg` | Open Map after opening King of the Hill and saving *Twin Rivers*, so two Recent rows lead the list (Twin Rivers first). Dialog crop |
| `save-as.jpg` | Save As for the new map *Twin Rivers*. Dialog crop |
| `test-map.jpg` | King of the Hill after F5: the TEST MAP message, with TEST RUNNING on the toolbar, whole window. A stand-in `takclient` beside the driver keeps a copy of the snapshot and waits, so no game opens |
| `test-lobby.jpg` | The game's single-player room for that King of the Hill snapshot (`Cartographer Test.kmp`): `takclient --play-map <snapshot> --shot` in the install with only *click* (room job `test`, `TAK_SHOT_CLICKS` waits 8 s), 1024 × 768 cropped to the room (1024 × 576 at (0, 96)). PLAY THE MACHINE, the map name cut with ... to fit its column, seat 1 with WATCH, seats 2 to 4 open and tagged NEEDED BY SCENARIO, START greyed beside QUIT, the rules with START LOCATIONS: FIXED, MONARCH EXPENDABLE: SCENARIO greyed and MAP SCENARIO: ON (TEST MAP) with the scenario details under both columns, the preview numbering four starts, and MODS: click |
| `menus.jpg` | Composite: the FILE, EDIT, VIEW, SCENARIO, SYMMETRY and HELP menus, each opened over Ulasem Arena and cropped to the menu with its own title |
| `ai-profiles.jpg` | *AiTest* (Ulasem Arena's terrain), Scenario → AI Profiles, DEFAULT AI tab: the map's own profile *aitest* started Empty, then NO FLYERS, so every flying unit reads BUILD OFF and LIMIT 0; TAK ENGINE AI (`ai/takengine.txt`) is tagged `map *`, and PREVIEW LEVEL reads HARD. From the `aiprofile` workflow, cropped to the window (802 × 534 at (60, 78)), quality 98 |
| `ai-group-rules.jpg` | Ulasem Arena, AI Profiles, DEFAULT AI tab in the GROUP RULES view: own profile *ulasem arena*, one rule WEIGHT ARA 50 % covering 32 units, the Archer set individually (80 %) and shown as skipped. From the `aiprofile-extras` workflow, cropped to the window (802 × 534) |
| `ai-takengine.jpg` | *AiTest*, AI Profiles, TAK ENGINE AI tab previewing NORMAL with FIND *araarch*: the Archer's weight set to 0, so it reads OFF. The `aiprofile` workflow's `-level.png`, cropped to the window (802 × 534 at (60, 78)), quality 98 |
| `ai-mod.jpg` | File → New AI mod *Harder AI* (a scratch pack, no map open): the AI MOD (NO MAP) window on the TAK ENGINE AI tab previewing NORMAL after one edit (the Archer's WEIGHT 0), so the pack reads UNSAVED; the toolbar names the pack and the editor behind it is empty. The `aimod-nomap` workflow's `-aimod.png`, whole window, quality 98 |
| `blocked.jpg` | `cartographer "Ladron's Tarn" --shot <png> --shot-tool blocked`, fresh profile, whole window at 30%: the map's blocked cells along its castle walls tinted red and its road cells blue; the browser lists the five brush sizes |

The native dialogs (the first-start folder picker, *Not a game folder*, *Recover map*
and *Restart failed*) belong to the operating system and are not pictured.

## Quick start (`docs/img/quick/`)

For [quick-start.md](../quick-start.md). The game pictures are development scenes on
Ulasem Arena, local server, 1600 × 900 at 1× zoom, cropped and scaled; the drag uses
press, move and release entries in `TAK_SHOT_CLICKS`.

| Image | Scene |
| --- | --- |
| `box-select.jpg` | **Spawned.** 17 Aramon units (Swordsmen, Archers, Knights, Catapults); a left-drag box (`D160,190;M400,330;M570,430`) drawn round them, before release. Crop, not scaled |
| `selected.jpg` | The same army after release: green selection rings, four type portraits with counts 2, 3, 6 and 6, 17 SELECTED and the command panel; Taros units stand to the east. 75% |
| `attack-cursor.jpg` | The army selected, the pointer over a Taros Skeleton Archer: retail's hover cursor over an enemy (an arrow with a red dot), at Cursor Size 2×. 80% |
| `command-panel.jpg` | A Mage Builder's command panel at 200%, labelled MOVE (M), PATROL (P), ATTACK (A), GUARD (G), HEAL (H), CLEAR and STOP (S) |
| `build-placement.jpg` | **Spawned** Mage Builder selected, its build menu along the bottom, the Barracks icon chosen and the Barracks ghost with its label under the pointer. 80% |
| `build-line.jpg` | Two Shift-drag wall lines by Elsin side by side: left, a tight diagonal of ten walls corner to corner (`TAK_BUILDLINE_SHOT=300,220,0`); right, five walls three cells apart with the **GAP 3** label (`TAK_BUILDLINE_SHOT=360,0,3`). `--testbuild`, 1280 × 960; the same crop of each, joined and scaled to 85% |
| `lobby-move-here.jpg` | Single-player room opened from the menu with the default map (Athri Cay) in the install with no packs: the player in slot 1 with WATCH, a Normal AI in slot 2, START LOCATIONS: BY SLOT (so MOVE HERE is live) on open slots 3 to 5, slots 6 to 8 closed, the rules, the preview numbering starts 1 to 5, CHANGE MAP... and MODS: NONE. Room job `quick` (`TAK_ROOM_SCRIPT=ai:1;opt:randomStarts=2`), 1600 × 900 cropped to (40, 80, 1580, 830) and scaled to 72% |

## Retaking

Rerender every view a change touches and look at each one before keeping it; keep a
picture's file name, size and crop unless the screen's layout changed, and update its
row here and the alt text where it is used. Keep retail archives, extracted assets and
raw captures out of Git.
