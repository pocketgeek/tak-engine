# Screenshot list for 0.9.0

Compiled from every `<!-- SHOT-0.9.0: <id> -- <what it must show> -->` marker in the
docs. Take every shot from the final 0.9.0 build, then remove the markers (or this
file) when the pictures land. Capture notes and sizes for the existing pictures are
in [README.md](README.md) in this folder; record the new captures there too.

**81 shots** (89 markers; an id used in several files is one picture):

- **NEW** (3): the file does not exist yet; the docs already link to it, so it must be added before the docs merge.
- **MUST** (12): the current picture is wrong for 0.9.0; the reason is given.
- **refresh** (66): the current picture is still correct; retake so the set matches the 0.9.0 build.

Line numbers are the marker lines at the commit that added this file.

## New pictures

| id | image | used at | must show |
|---|---|---|---|
| `mp-connect-custom` | `mp-connect-custom.jpg` (new) | `docs/public-server.md:91` | the Multiplayer door's CONNECT TO SERVER panel with CUSTOM... selected and a hostname such as tak.example.org typed in the address box |
| `mp-connect` | `mp-connect.jpg` (new) | `docs/quick-start.md:65`<br>`docs/user-guide.md:1793` | the Multiplayer door's CONNECT TO SERVER panel, server list with DEFAULT, account name typed, password as pips, CONNECT and BACK |
| `guide-game-browser` | `guide/game-browser.jpg` (new) | `docs/user-guide.md:1811` | the online game browser with several games, one LOCK, one LIVE with WATCH, REFRESH and CREATE, PASSWORD (FOR LOCKED GAMES) field |

## Must retake

| id | image | used at | must show | why |
|---|---|---|---|---|
| `title` | `title.jpg` | `README.md:16`<br>`docs/quick-start.md:40`<br>`docs/user-guide.md:222` | title menu, three doors, VERSION 0.9.0 | title must read VERSION 0.9.0 |
| `cartographer-main` | `cartographer.png` | `README.md:94`<br>`docs/user-guide.md:2410` | README.md: Cartographer on Ulasem Arena with the five-tool toolbar, browser and minimap<br>user-guide.md: editor on Ulasem Arena, five tools | the current picture shows the old four-tool toolbar; the editor now has five |
| `carto-window-annotated` | `cartographer/window-annotated.jpg` | `docs/cartographer-guide.md:307` | Ulasem Arena, Terrain tool, view on the arena at 35%; whole window | the current picture shows the old four-tool toolbar; the editor now has five; redraw numbered markers 1-13 for the five tools |
| `carto-menus` | `cartographer/menus.jpg` | `docs/cartographer-guide.md:382` | composite of the FILE, EDIT, VIEW, SCENARIO, SYMMETRY and HELP menus, each cropped to the menu | missing File > New AI mod / Open AI mod and Scenario > AI Profiles |
| `carto-terrain-groups` | `cartographer/terrain-groups.jpg` | `docs/cartographer-guide.md:482` | Ulasem Arena, terrain group list open, five tool buttons; whole window | the current picture shows the old four-tool toolbar; the editor now has five |
| `carto-overlay-movement` | `cartographer/overlay-movement.jpg` | `docs/cartographer-guide.md:616` | Ulasem Arena at 40%, Horseman chosen, View > Overlay: movement; whole window | the current picture shows the old four-tool toolbar; the editor now has five |
| `carto-features` | `cartographer/features.jpg` | `docs/cartographer-guide.md:636` | Ulasem Arena, Features tool, trees category, five palm trees placed and a sixth previewed; whole window | the current picture shows the old four-tool toolbar; the editor now has five |
| `carto-units` | `cartographer/units.jpg` | `docs/cartographer-guide.md:785` | King of the Hill, Units tool filtered to GROUND and ARAMON, Player 1 units box-selected at 75%; whole window | the current picture shows the old four-tool toolbar; the editor now has five |
| `carto-starts` | `cartographer/starts.jpg` | `docs/cartographer-guide.md:915` | Ulasem Arena, Starts tool, View > Fit map; whole window | the current picture shows the old four-tool toolbar; the editor now has five |
| `carto-regions` | `cartographer/regions.jpg` | `docs/cartographer-guide.md:1256` | King of the Hill at Fit map, REGIONS on, The Hill selected with handles; whole window | the current picture shows the old four-tool toolbar; the editor now has five |
| `carto-test-map` | `cartographer/test-map.jpg` | `docs/cartographer-guide.md:1873` | King of the Hill after F5: TEST MAP message, TEST RUNNING on the toolbar; whole window | the current picture shows the old four-tool toolbar; the editor now has five |
| `guide-cartographer-terrain` | `guide/cartographer-terrain.jpg` | `docs/user-guide.md:2446` | grouped terrain browser on Ulasem Arena with variant badges on the pieces | current picture is from the 0.8.3 build |

## Refresh

| id | image | used at | must show |
|---|---|---|---|
| `readme-battle` | `battle.jpg` | `README.md:72` | Benchmark battle on Ulasem Arena, Aramon and Taros armies clashing with fire and dragons |
| `readme-hud` | `hud.jpg` | `README.md:76` | mixed selection: portrait strip with counts, FORMATION n and N SELECTED in the information bar, stats panel visible |
| `readme-naval` | `naval.jpg` | `README.md:78` | Veruna fleet trading fire with Creon ships on open water |
| `readme-dragons` | `dragons.jpg` | `README.md:82` | Creon Neo Dragons breathing fire on an Aramon siege column |
| `generated` | `generated.jpg` | `README.md:84`<br>`docs/user-guide.md:467` | README.md: generated themed map (e.g. Aramon Riverlands) with a lodestone on a scattered mana spot<br>user-guide.md: generated themed map with scattered mana: a lodestone on a neutral scattered spot |
| `room` | `room.jpg` | `README.md:88`<br>`docs/user-guide.md:1816` | README.md: room with AI slots, MOVE HERE, coloured start markers, mod lists with ALWAYS ON lines, read-only match rules<br>user-guide.md: room with AI slots, MOVE HERE, start markers in the players' colours, mod lists with ALWAYS ON lines, read-only match rules |
| `campaign` | `campaign.jpg` | `README.md:90`<br>`docs/quick-start.md:83`<br>`docs/user-guide.md:2168` | README.md: Book of Darien campaign picker<br>quick-start.md: Book of Darien picker<br>user-guide.md: Book of Darien picker |
| `readme-results` | `results.jpg` | `README.md:96` | post-game statistics screen with faction emblems and player colours |
| `carto-empty-window` | `cartographer/empty-window.jpg` | `docs/cartographer-guide.md:142` | bare launch, fresh profile: empty window, NO MAP OPEN hint naming File > New map, Open map and the AI mod commands; whole window |
| `carto-new-map` | `cartographer/new-map.jpg` | `docs/cartographer-guide.md:149` | File > New map over the empty window: New Map dialog (Untitled, 8 x 8, aramon), NO MAP OPEN in the status bar; whole window |
| `carto-terrain-variant` | `cartographer/terrain-variant.jpg` | `docs/cartographer-guide.md:506` | Ulasem Arena, Cliff W clicked twice (variant 2/3), status bar shows its full name; crop of the left of the window |
| `carto-terrain-clipboard` | `cartographer/terrain-clipboard.jpg` | `docs/cartographer-guide.md:549` | composite: Terrain + Select 15 x 15-block rectangle, then the clipboard brush after Ctrl+C / Ctrl+V |
| `carto-features-select` | `cartographer/features-select.jpg` | `docs/cartographer-guide.md:663` | composite: selection box dragged round palm trees in Select mode, then the selected anchor cells |
| `carto-blocked` | `cartographer/blocked.jpg` | `docs/cartographer-guide.md:709` | Ladron's Tarn at 30%, BLOCKED tool: red wall cells, blue road cells, brush sizes listed; whole window |
| `carto-unit-properties` | `cartographer/unit-properties.jpg` | `docs/cartographer-guide.md:863` | King of the Hill, Unit Properties of a Player 1 Archer with a display name typed; dialog crop |
| `carto-unit-model` | `cartographer/unit-model.jpg` | `docs/cartographer-guide.md:903` | Ulasem Arena, Units browser searched for knight, F6 3D model window on the Aramon Knight |
| `carto-symmetry-stamp` | `cartographer/symmetry-stamp.jpg` | `docs/cartographer-guide.md:1041` | composite: live symmetry on Ulasem Arena, preview before the click, then the stamped section and its mirror twin |
| `carto-symmetrize` | `cartographer/symmetrize.jpg` | `docs/cartographer-guide.md:1070` | Ulasem Arena, Shift+M Symmetrize dialog; dialog crop |
| `carto-random-generator` | `cartographer/random-generator.jpg` | `docs/cartographer-guide.md:1121` | composite: Random Map Settings (seed, players, themed layout, Scattered Mana) and the Generated Map Preview it produced |
| `carto-scripting` | `cartographer/scripting.jpg` | `docs/cartographer-guide.md:1298` | King of the Hill, Scenario Scripting with Rule 1 of All Players selected and FULL TEXT; crop of the window |
| `carto-rule-editor` | `cartographer/rule-editor.jpg` | `docs/cartographer-guide.md:1329` | the same rule with its condition open and the LOCATION list opened with Alt+Down |
| `carto-templates` | `cartographer/templates.jpg` | `docs/cartographer-guide.md:1352` | King of the Hill, OBJECTIVE TEMPLATES list with the six templates; dialog crop |
| `carto-ai-profiles` | `cartographer/ai-profiles.jpg` | `docs/cartographer-guide.md:1480` | Scenario > AI Profiles on a map with its own profile, DEFAULT AI tab after the NO FLYERS preset; crop of the window |
| `carto-ai-takengine` | `cartographer/ai-takengine.jpg` | `docs/cartographer-guide.md:1558` | AI Profiles, TAK ENGINE AI tab on a map carrying old level files, right after MERGE INTO TAK ENGINE; crop of the window |
| `carto-ai-group-rules` | `cartographer/ai-group-rules.jpg` | `docs/cartographer-guide.md:1607` | Ulasem Arena, AI Profiles, DEFAULT AI tab, GROUP RULES view with one weight rule; crop of the window |
| `carto-ai-mod` | `cartographer/ai-mod.jpg` | `docs/cartographer-guide.md:1747` | File > New AI mod Harder AI, no map open: AI MOD (NO MAP) window, TAK ENGINE AI tab, one edit, UNSAVED; whole window |
| `carto-check-map` | `cartographer/check-map.jpg` | `docs/cartographer-guide.md:1808` | Check Map results on a practice map (one start, no mana, a War Galley on dry land); dialog crop |
| `carto-test-lobby` | `cartographer/test-lobby.jpg` | `docs/cartographer-guide.md:1880` | the game's private test lobby for a King of the Hill snapshot: NEEDED BY SCENARIO seats with MOVE HERE, ALWAYS ON pack line, MAP SCENARIO: ON (TEST MAP); cropped to the lobby panel |
| `carto-save-as` | `cartographer/save-as.jpg` | `docs/cartographer-guide.md:1947` | Save As for Twin Rivers into the Maps folder; dialog crop |
| `carto-open-map` | `cartographer/open-map.jpg` | `docs/cartographer-guide.md:1986` | Open Map with two Recent rows; dialog crop |
| `carto-preferences` | `cartographer/preferences.jpg` | `docs/cartographer-guide.md:2115` | View > Preferences with default settings; dialog crop |
| `quick-box-select` | `quick/box-select.jpg` | `docs/quick-start.md:139` | left-drag selection box around about seventeen Aramon units |
| `quick-selected` | `quick/selected.jpg` | `docs/quick-start.md:144` | the same army selected: rings, type portraits with counts, N SELECTED, command panel |
| `quick-attack-cursor` | `quick/attack-cursor.jpg` | `docs/quick-start.md:163` | attack cursor (crossed swords) over an enemy with an army selected, Cursor Size 2x |
| `quick-command-panel` | `quick/command-panel.jpg` | `docs/quick-start.md:194` | a builder's command panel: MOVE, PATROL, ATTACK, GUARD, HEAL, CLEAR, STOP |
| `quick-build-placement` | `quick/build-placement.jpg` | `docs/quick-start.md:215` | Mage Builder selected, Barracks icon chosen, Barracks ghost under the pointer |
| `quick-build-line` | `quick/build-line.jpg` | `docs/quick-start.md:221` | two Shift-drag wall lines: a tight diagonal and a straight line with the GAP 3 label |
| `quick-lobby-move-here` | `quick/lobby-move-here.jpg` | `docs/quick-start.md:313` | single-player room: you in slot 1, a Normal AI in slot 2, MOVE HERE on open slots, map with numbered starts |
| `streaming` | `streaming.jpg` | `docs/streaming.md:6` | panel with Remember Key and Save a Local Copy rows, no key |
| `lobby` | `lobby.jpg` | `docs/user-guide.md:289` | single-player game setup screen: map list, preview with numbered starts and player count, match options |
| `guide-random-map-lobby` | `guide/random-map-lobby.jpg` | `docs/user-guide.md:461` | Generate Random Map open in the single-player setup: Type/Layout/Size/Players row, the sliders with Scattered Mana at High, Re-roll Seed, preview with MANA and SCATTERED totals |
| `guide-hud-annotated` | `guide/hud-annotated.jpg` | `docs/user-guide.md:478` | in-game screen with numbered markers 1-8 (minimap, stats panel, orders, weapons and stances, mana, build menu, information bar, the selected Monarch) |
| `guide-lobby-scenario` | `lobby-scenario.jpg` | `docs/user-guide.md:516` | single-player setup with King of the Hill and MAP SCENARIO: ON, the lines under it, gold S in the list, Monarch Expendable and Start Locations greyed |
| `guide-build-menu-wrap` | `guide/build-menu-wrap.jpg` | `docs/user-guide.md:583` | a Beast Tamer menu wrapped into two balanced rows at Build Menu Scale 150% in a 1024x768 window |
| `guide-build-line` | `guide/build-line.jpg` | `docs/user-guide.md:605` | Shift-drag wall line being laid, sites two cells apart, GAP 2 label by the pointer |
| `guide-fire-on-the-move` | `guide/fire-on-the-move.jpg` | `docs/user-guide.md:705` | archers on a move order (Shift held, dotted line) shooting enemies at the back of the column while walking |
| `guide-flyers-retail-legion` | `guide/flyers-retail-legion.jpg` | `docs/user-guide.md:776` | side by side, same formation of ground units and flyers: Retail flyers far ahead, Legion flyers over the leading ground units |
| `guide-scorecard` | `guide/scorecard.jpg` | `docs/user-guide.md:822` | F4 scorecard over a battle: players with kills, losses and score |
| `guide-side-aramon` | `guide/side-aramon.jpg` | `docs/user-guide.md:852` | Aramon Monarch selected beside an Aramon army, with the side's interface and build menu |
| `guide-side-taros` | `guide/side-taros.jpg` | `docs/user-guide.md:873` | Taros Monarch selected beside a Taros army, with the side's interface and build menu |
| `guide-side-veruna` | `guide/side-veruna.jpg` | `docs/user-guide.md:895` | Veruna Monarch selected beside a Veruna army, with the side's interface and build menu |
| `guide-side-zhon` | `guide/side-zhon.jpg` | `docs/user-guide.md:919` | Zhon Monarch selected beside a Zhon army, with the side's interface and build menu |
| `guide-side-creon` | `guide/side-creon.jpg` | `docs/user-guide.md:943` | Creon Monarch selected beside a Creon army, with the side's interface and build menu |
| `guide-selection-strip` | `guide/selection-strip.jpg` | `docs/user-guide.md:1097` | information bar for a whole selected group: type portraits with counts, GROUP n label, health and mana bars, N SELECTED |
| `guide-shift-queue` | `guide/shift-queue.jpg` | `docs/user-guide.md:1249` | Shift held: dotted line with waypoint markers through a unit's queued moves |
| `guide-options-graphics` | `guide/options-graphics.jpg` | `docs/user-guide.md:1558` | Options scrolled to Display and Graphics at the default settings (Max FPS greyed by VSync) |
| `guide-tactical-dots` | `guide/tactical-dots.jpg` | `docs/user-guide.md:1653` | same moment of a big zoomed-out battle, MODELS left and TACTICAL DOTS right |
| `guide-options-interface` | `guide/options-interface.jpg` | `docs/user-guide.md:1686` | Options, Interface section at the defaults |
| `guide-chat-box` | `guide/chat-box.jpg` | `docs/user-guide.md:1747` | in-game chat box centred above a build row and TRAINING line: messages with coloured names and a line being typed after SAY: |
| `diplomacy` | `diplomacy.jpg` | `docs/user-guide.md:1861` | Diplomacy screen: players with Share Mana, Chat and Voice checkboxes, Volume sliders for other humans, Give Selected Units |
| `guide-disconnect-wait` | `guide/disconnect-wait.jpg` | `docs/user-guide.md:1920` | game paused with the WAITING FOR <name> <seconds> banner |
| `guide-voice-chat` | `guide/voice-chat.jpg` | `docs/user-guide.md:2023` | Voice Chat page with voice chat on, Push to Talk on Space, microphone, level meter, Activation level greyed, output Same as Game Output |
| `guide-campaign-briefing` | `guide/campaign-briefing.jpg` | `docs/user-guide.md:2178` | Book of Darien chapter 1 briefing over the darkened paused battlefield |
| `guide-replay` | `guide/replay.jpg` | `docs/user-guide.md:2224` | replay playback with the time bar showing elapsed and total time |
| `guide-room-mods-power-codes` | `guide/room-mods-power-codes.jpg` | `docs/user-guide.md:2318` | single-player room with Power Codes: On in the rules, COSMETIC MODS with an ALWAYS ON line and a ticked pack, GAME MODS with an ALWAYS ON line, a ticked pack and GAME MODS VERIFIED |
| `guide-benchmark` | `guide/benchmark.jpg` | `docs/user-guide.md:2552` | benchmark battle at Absurd intensity with the BENCHMARKING countdown at the top |

## Pictures no longer used

The rewritten README, quick start and guides no longer show these files. Nothing
links to them outside `docs/img/README.md` and the frozen release validation
reports, so they need no retake; delete them or put them back in a guide when the
pictures land.

- `cartographer-model.png`
- `crusades.jpg`
- `flyers.jpg`

`lobby.jpg`, `streaming.jpg` and `diplomacy.jpg` left the README but are still
used by the user guide and `docs/streaming.md`; they are listed above.
