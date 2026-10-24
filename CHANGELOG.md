# Changelog

Every published TAK Engine release, newest first.

The current version is always at
[the latest release](https://github.com/pocketgeek/tak-engine/releases/latest).
Older versions are kept as git tags (each entry links its source); to run one,
build it from source with the [development guide](docs/development.md) of that
tag. No release includes retail game assets: every version needs your own
*Total Annihilation: Kingdoms* installation.

**Protocol** decides who can play together. A server and all of its players
must use the same protocol, and a replay plays only in a version with the
protocol that recorded it. Each entry gives its protocol and replay format; an
entry says "plays with" only where that release was checked against its
neighbours. **Upgrading** lines appear only where you have to do something
yourself.

Releases 0.7.11 and later link full release notes, and 0.7.6 and later link
their validation reports; both live in [`docs/`](docs/).

## 0.9.0
*Unreleased* <!-- VERSION-0.9.0: confirm date, protocol and replay format at cut (main: 293/14), and which versions it plays with -->

<!-- WHATS-NEW-0.9.0: add the 0.9.0 gameplay and engine changes here before the cut. -->
- **Retail AI:** the shipped AI profiles misspell the Aramon Rolling Tower
  (`araseige`), which the original game ignores, so its AI built Rolling Towers
  without limit. The Retail AI now reads the name as the Rolling Tower and keeps
  to the profile's limit (10 in `ai/default.txt`). Only the server runs the AI,
  so this changes nothing about who can play together.
- **Documentation:** the release history moved out of the README into this
  changelog; the README, quick start, user guide, map editor guide, server
  guide and streaming guide were rewritten and their pictures retaken.

## 0.8.14
*2026-10-24 · protocol 293 · replay format 14 · plays with 0.8.12 and 0.8.13, and their replays*

- **Always-enabled mods, in folders:** `mods/cosmetic/always_enabled/<pack>/`
  (loaded in every game, campaigns included, never sent) and
  `mods/game/always_enabled/<pack>/` (applied in every game you host and sent
  like ticked game packs; never in campaigns or Crusades battles). The room
  shows them, locked, on **ALWAYS ON** lines.
- **No GAME MODS switch:** in multiplayer, as in single player, the host ticks
  game packs in the room. A room hosted by 0.8.12 or 0.8.13 with the switch off
  still plays without game packs.
- **Cartographer** edits ordinary game packs only; **TEST IN GAME** and **Test
  Map** name the always-enabled game packs that apply in the test game.
- **Upgrading:** a `.hpi` placed directly in `mods/cosmetic/` (such as
  `mods/cosmetic/click.hpi`) is no longer loaded. Move it into its own folder,
  for example `mods/cosmetic/always_enabled/click/click.hpi`. The saved GAME
  MODS choice is ignored.

[Release notes](docs/release-0.8.14-notes.md) · [Validation report](docs/release-0.8.14-validation.md) · [Source at v0.8.14](https://github.com/pocketgeek/tak-engine/tree/v0.8.14)

## 0.8.13
*2026-10-24 · protocol 293 · replay format 14 · plays with 0.8.12, and its replays*

- **No order text on screen:** an armed order shows only its cursor and its
  pressed command-panel button, as in the original game (no **ATTACK: CLICK
  TARGET** banner).
- **Cartographer starts with no map:** an empty **NO MAP OPEN** window; start
  from **File → New map** (Ctrl+N), **Open map** (Ctrl+O), **New AI mod**
  (Ctrl+Shift+N) or **Open AI mod** (Ctrl+Shift+O).

[Release notes](docs/release-0.8.13-notes.md) · [Validation report](docs/release-0.8.13-validation.md) · [Source at v0.8.13](https://github.com/pocketgeek/tak-engine/tree/v0.8.13)

## 0.8.12
*2026-10-23 · protocol 293 · replay format 14 · does not play with 0.8.11 or earlier*

- **AI profiles in the map editor:** **Scenario → AI Profiles...** (A) edits a
  map's AI profiles for the Retail AI and our six levels, with presets, a text
  view, checks, and saving to or opening from a game pack. Maps keep their
  `ai/` files when opened and saved.
- **AI mods without a map:** **File → New AI mod...** and **Open AI mod...**,
  with **TEST IN GAME...** on a map you pick.
- **One file for our six AI levels:** `ai/takengine.txt`, from a ticked game
  mod or the map, applied on top of each level's built-in profile.
- **Each map's own AI files:** two maps with an AI file of the same name each
  play their own.
- **Campaigns:** game speed can always be changed in a mission.
- **Flyer patrols:** an armed flyer told to move, then Shift-patrol, patrols
  instead of hovering at the point.
- **Upgrading:** the old `ai/d.txt` ... `ai/ad.txt` files are no longer read;
  Cartographer's **MERGE INTO TAK ENGINE...** moves one into
  `ai/takengine.txt`. AI level 6, DEFAULT in 0.8.11, is now shown as **RETAIL**.

[Release notes](docs/release-0.8.12-notes.md) · [Validation report](docs/release-0.8.12-validation.md) · [Source at v0.8.12](https://github.com/pocketgeek/tak-engine/tree/v0.8.12)

## 0.8.11
*2026-10-23 · protocol 290 · replay format 14 · does not play with 0.8.10 or earlier*

- **The original game's own AI** as a seventh level (shown as DEFAULT, renamed
  RETAIL in 0.8.12): the 1999 AI driven by `ai/default.txt`, ported routine by
  routine, quirks included. It plays weaker than Normal. New AI seats still
  start on Normal, and a dropped player is still replaced by Normal.
- **Campaign opponents** always use this AI, with each mission's own profile and
  timed mass attack.
- **Long build menus wrap** into extra rows instead of running under the
  command panel.
- **No blue production bar** over factories and conjurers, as in the original;
  the information panel shows progress.
- The other AI levels and the game itself play as in 0.8.10.

[Release notes](docs/release-0.8.11-notes.md) · [Validation report](docs/release-0.8.11-validation.md) · [Source at v0.8.11](https://github.com/pocketgeek/tak-engine/tree/v0.8.11)

## 0.8.10
*2026-10-22 · protocol 289 · replay format 14 · does not play with 0.8.9 or earlier*

- **Disconnects: the game waits, then a computer takes over.** Everyone sees a
  **WAITING FOR** *name* countdown; each player has 30 seconds for the whole
  game. A player back in time carries on where the game stopped; otherwise a
  Normal AI takes the seat with its units, buildings and mana.
- **Save a Local Copy** of your YouTube stream to
  `videos/TAK-YYYYMMDD-HHMMSS.mp4` (off by default).
- **Campaign seat:** you always play the mission's first player (fixes *Iron
  Plague* missions 22 and 24).
- **Cartographer Test Map on macOS** finds the game from the app, DMG or ZIP.
- **Retail target choice** scales a shooter's turn rate by the terrain under it.
- **Public servers:** Let's Encrypt renewal follows the CA's renewal window
  (ARI) and renews at once after a revocation; clients check revocation lists.

[Release notes](docs/release-0.8.10-notes.md) · [Validation report](docs/release-0.8.10-validation.md) · [Source at v0.8.10](https://github.com/pocketgeek/tak-engine/tree/v0.8.10)

## 0.8.9
*2026-10-21 · protocol 288 · replay format 14 · plays with 0.8.8, and its replays*

- **Tight build lines:** a Shift-drag line packs buildings edge to edge; the
  mouse wheel during the drag adds a gap of up to 12 cells (**GAP** label).
- **Deliberate drag boxes:** reclaim and lodestone boxes need a drag of about
  32 pixels; boxes stay on the ground you pressed when the view moves.
- **Chat box** always sits above the build menu.
- Neater setup and room screens; **GAME MODS VERIFIED** shows only when game
  packs are in play.

[Release notes](docs/release-0.8.9-notes.md) · [Validation report](docs/release-0.8.9-validation.md) · [Source at v0.8.9](https://github.com/pocketgeek/tak-engine/tree/v0.8.9)

## 0.8.8
*2026-10-21 · protocol 288 · replay format 14 · does not play with 0.8.7 or earlier*

- **Mods replace overrides:** packs go in `mods/cosmetic/<pack>/` (art and
  sound, each player's own) or `mods/game/<pack>/` (units and rules, picked by
  the host).
- **Power codes** match option: the original cheat codes (+ATM, +ManaMe,
  +DoubleShot, +HalfShot, +LOS, +Mapping, +NowISee, +Radar, +MakePoster) typed
  in chat. Off in campaigns and Crusades battles.
- **Readable chat:** bold text on a dark panel, **Chat Scale** (50–300%) and
  **Chat Position** (Left or Centered).
- **New interface font,** Noto Sans Bold; **UI Font: Classic** restores the
  earlier look.
- **Legion** rings along walls and through gates hold about as many attackers
  in reach as Retail's.
- Replay format 14 records the Power Codes option.
- **Upgrading:** the old `overrides/` folder is no longer read and nothing in it
  is moved: move your packs into `mods/` yourself and tick them again in the
  room.

[Release notes](docs/release-0.8.8-notes.md) · [Validation report](docs/release-0.8.8-validation.md) · [Source at v0.8.8](https://github.com/pocketgeek/tak-engine/tree/v0.8.8)

## 0.8.7
*2026-10-20 · protocol 284 · replay format 13 · does not play with 0.8.6 or earlier*

- **Pick your seat:** every open lobby slot has a **Move here** button; with
  **Start locations: By slot** it also picks where you start.
- **Legion:** formations keep their lanes through turns (an army of 100 rounds
  a corner clearly sooner than Retail), steadier attack, fight and guard rings,
  and bridges and one-unit corridors taken in turn.

[Release notes](docs/release-0.8.7-notes.md) · [Validation report](docs/release-0.8.7-validation.md) · [Source at v0.8.7](https://github.com/pocketgeek/tak-engine/tree/v0.8.7)

## 0.8.6
*2026-10-20 · protocol 281 · replay format 13 · does not play with 0.8.5 or earlier*

- **Clear shots:** a unit whose shots keep hitting a hill, water or scenery
  walks to a spot with a clear shot (not in the original game).
- **Attack cursor as in the original** over any enemy the weapon can hurt.
- **Voice activation:** voice chat's **Talk Mode** with an **Activation Level**
  slider; Push to Talk stays the default.
- **Legion** with armies of about 100: bridges taken in turn, squads that pass
  each other, full attack rings, formations that keep their files.

[Release notes](docs/release-0.8.6-notes.md) · [Validation report](docs/release-0.8.6-validation.md) · [Source at v0.8.6](https://github.com/pocketgeek/tak-engine/tree/v0.8.6)

## 0.8.5
*2026-10-18 · protocol 278 · replay format 13 · does not play with 0.8.4 or earlier*

- **Scattered Mana** for random maps: neutral spots across open ground, None to
  Max, by map area.
- **Sharp on Retina Macs:** **Native Retina Resolution** (on by default).
- **Tactical Dots** no longer draw fire, smoke and projectiles over the armies.
- **New defaults:** Tactical Dots on (zoom 0%), Smooth GUI Art and Smooth
  Movies on, Texture Filtering Everything; new **Screen Shake** option (off).
  UI Scale and F4 Scorecard Scale go down to 50%.
- Death cries heard as in the original; queued-building outlines leave the site
  once it is being built.
- **Upgrading:** a settings file you already have keeps its values; **Defaults**
  in Options applies the new ones.

[Release notes](docs/release-0.8.5-notes.md) · [Validation report](docs/release-0.8.5-validation.md) · [Source at v0.8.5](https://github.com/pocketgeek/tak-engine/tree/v0.8.5)

## 0.8.4
*2026-10-18 · protocol 277 · replay format 13 · does not play with 0.8.3 or earlier*

- **Voice chat:** hold Space to talk in multiplayer rooms and games; off by
  default, set up under **Settings → Voice Chat**, relayed by the server and
  never recorded.
- **Faster rendering:** a still army of 2,000 drew in 4.5 ms instead of 10.5 ms
  at 7680×2160 (measured on Linux with an NVIDIA GPU).
- **Legion takes much bigger armies:** one click reaches 10,000 units in the
  same tick ([Legion 10,000 report](docs/legion-10000-report.md)).
- **New AI level: Absurdly Defensive;** Defensive keeps building as fast as its
  income allows.
- **Replays are saved however a game ends,** crash or power cut included.
- **Short mana is shared:** every construction slows together, as in the
  original.

[Release notes](docs/release-0.8.4-notes.md) · [Validation report](docs/release-0.8.4-validation.md) · [Source at v0.8.4](https://github.com/pocketgeek/tak-engine/tree/v0.8.4)

## 0.8.3
*2026-10-15 · protocol 266 · replay format 13 · does not play with 0.8.2 or earlier*

- **New match options:** **Allow Spectators**, **Map Scenario** (a gold **S**
  marks maps with a scenario) and **Start Locations: By Slot**; the preview
  numbers every start.
- **Retail self-repair** rates (most units heal fully in about four minutes).
- **Retail air combat:** attack runs, no leading of hovering flyers, area
  damage measured in 3D.
- **Smarter computer players:** shared deposits, a lodestone when short of
  mana, the original per-type limits.
- **Map editor:** a **Blocked** tool, a shared **Game data folder** setting,
  borderless fullscreen and edge scrolling.
- Replay format 13 records the Map Scenario option.

[Release notes](docs/release-0.8.3-notes.md) · [Validation report](docs/release-0.8.3-validation.md) · [Source at v0.8.3](https://github.com/pocketgeek/tak-engine/tree/v0.8.3)

## 0.8.2
*2026-10-14 · protocol 257 · replay format 12 · does not play with 0.8.1 or earlier*

- **Legion: no more walking on the spot** near scattered trees (in the 0.8.1
  recording, walking on the spot fell from 857 to 34 unit-seconds). Retail
  plays as in 0.8.1.

[Release notes](docs/release-0.8.2-notes.md) · [Validation report](docs/release-0.8.2-validation.md) · [Source at v0.8.2](https://github.com/pocketgeek/tak-engine/tree/v0.8.2)

## 0.8.1
*2026-10-13 · protocol 256 · replay format 12 · plays with 0.8.0, and its replays*

- **The information bar shows only what has data.**
- **Less video memory for terrain:** peak use at 3840×2160 fell from 498 to
  309 MiB zoomed out.
- **GPU Memory Limit** option (512 MB to 4 GB or Unlimited; default 1.25 GB).
- **Streaming: Remember Key** in the system password store.
- Options dropdowns open upward when there is no room below.

[Release notes](docs/release-0.8.1-notes.md) · [Validation report](docs/release-0.8.1-validation.md) · [Source at v0.8.1](https://github.com/pocketgeek/tak-engine/tree/v0.8.1)

## 0.8.0
*2026-10-13 · protocol 256 · replay format 12 · does not play with 0.7.28 or earlier*

- **Signed and notarized on macOS:** the app and DMG open on the first launch.
- **The information bar shows the whole selection:** **N SELECTED**, one
  portrait per unit type with its count, **GROUP** / **FORMATION** numbers.
- **Texture Filtering** (Off, Units, Everything) replaces Bilinear Filtering.
- Factories no longer jam their door with large units; flying transports land
  at the drop point.
- **Cartographer:** symmetry tools, terrain copy and paste, a grouped terrain
  browser and a wider built-in font.
- **Upgrading:** a saved Bilinear Filtering choice loads as Off or Units.

[Release notes](docs/release-0.8.0-notes.md) · [Validation report](docs/release-0.8.0-validation.md) · [Source at v0.8.0](https://github.com/pocketgeek/tak-engine/tree/v0.8.0)

## 0.7.28
*2026-10-11 · protocol 253 · replay format 12 · does not play with 0.7.27 or earlier*

- **Units fire as in the original:** the round-robin target sweep, firing on
  the move, armed builders defending themselves, and retaliation within a leash.
- **Original game speed:** −9 (0.1×) to +10 (2×), host-only with Allow speed
  change.
- **Blocked building sites** retried for about ten seconds when only units block
  them.
- **Better AI defence** and a Monarch that fights back.
- **Legion plan W0–W9:** one order per click, attack rings, flyers that stay
  with the army.
- Steadier frame pacing, VSync pacing that fixes macOS stutter, and **Ignore
  Windows Display Scaling** on Windows.

[Release notes](docs/release-0.7.28-notes.md) · [Validation report](docs/release-0.7.28-validation.md) · [Source at v0.7.28](https://github.com/pocketgeek/tak-engine/tree/v0.7.28)

## 0.7.27
*2026-10-07 · protocol 238 · replay format 11 · does not play with 0.7.26 or earlier*

- **Faster large battles:** an order-heavy Legion battle went from about 1.1×
  to 1.6× real time in exploratory replays; Retail movement about 16% cheaper.
- **Tactical Dots** option: far zoomed out, units are drawn as player-coloured
  dots.
- **Renderer** option: Auto or any backend SDL offers, with fallback to Auto.
- Linux and Windows builds report their real build id.

[Release notes](docs/release-0.7.27-notes.md) · [Validation report](docs/release-0.7.27-validation.md) · [Source at v0.7.27](https://github.com/pocketgeek/tak-engine/tree/v0.7.27)

## 0.7.26
*2026-10-07 · protocol 237 · replay format 11 · does not play with 0.7.25 or earlier*

- **Legion flyers stay with their formation,** holding stations over the ground
  units.
- **Retail grouped flyers** follow the original's group checks, including its
  1-in-4 chance to drop orders.

[Release notes](docs/release-0.7.26-notes.md) · [Validation report](docs/release-0.7.26-validation.md) · [Source at v0.7.26](https://github.com/pocketgeek/tak-engine/tree/v0.7.26)

## 0.7.25
*2026-10-06 · protocol 236 · replay format 11 · does not play with 0.7.23 or earlier*

- **Two pathfinding choices:** Retail (the default) and experimental **Legion**,
  which plans one route per army.
- **Closer to retail movement:** flyer landing sites, group pacing and
  route-search budgets.
- **Shift queues every order,** patrols included.
- **One graphics filter, as in retail:** **Bilinear Filtering** (off by default)
  replaces the AA options.
- First retail route search for a new unit shape about 9–14 times faster.
- **Extra Mana Spots** (0–6 per player) and `takserver --status` JSON.
- **Upgrading:** the experimental Retail+, Flowfield and Cooperative modes were
  removed; a saved choice of one falls back to Retail.

[Release notes](docs/release-0.7.25-notes.md) · [Validation report](docs/release-0.7.25-validation.md) · [Source at v0.7.25](https://github.com/pocketgeek/tak-engine/tree/v0.7.25)

## 0.7.24
*Prepared but never published; its surviving changes shipped in 0.7.25.*

[Release notes](docs/release-0.7.24-notes.md) · [Validation report](docs/release-0.7.24-validation.md)

## 0.7.23
*2026-10-04 · protocol 221 · replay format 11 · does not play with 0.7.22 or earlier*

- Experimental **Flowfield** pathfinding beside Retail (removed in 0.7.25).
- **Lodestone area jobs:** drag a lodestone box to build on every eligible mana
  spot; patrolling builders repair nearby allies.
- **Defensive AI** meets threats near its base, then recalls its army.
- **Named generated maps** and themed layouts: Aramon Riverlands, Taros Maze,
  Veruna Ports, Zhon Clearings, Creon Highlands.
- **F4 Scorecard Scale,** team rows in F4, **Shift+Z** retail zoom, live speeds
  up to 8× (replaced by the original's 2× in 0.7.28).
- World sounds fade with distance outside the view; all lodestones cast shadows.
- **Linux ARM64, openSUSE Leap and Slackware64 packages.**

[Release notes](docs/release-0.7.23-notes.md) · [Validation report](docs/release-0.7.23-validation.md) · [Source at v0.7.23](https://github.com/pocketgeek/tak-engine/tree/v0.7.23)

## 0.7.22
*2026-10-02 · protocol 213 (unchanged from 0.7.21) · replay format 10*

- Terrain AA and Model AA options (replaced by one filter in 0.7.25).
- **Server hardening:** bounded map parsing, memory admission, command budgets,
  ACME retry recovery.
- **Linux service setup:** packaged systemd service and configuration
  templates.
- Builders clear several reclaimable obstructions from a site.
- Signed Windows executables and installer.

[Release notes](docs/release-0.7.22-notes.md) · [Validation report](docs/release-0.7.22-validation.md) · [Source at v0.7.22](https://github.com/pocketgeek/tak-engine/tree/v0.7.22)

## 0.7.21
*2026-10-01 · protocol 213 · replay format 10 · does not play with 0.7.20 or earlier (still plays 0.7.20 replays)*

- **Selectable override packs** in `overrides/<pack>/` folders (replaced by
  `mods/` in 0.8.8).
- **Clearer lobby:** game-create rules shown read-only.
- Each running match has its own simulation worker on the server.
- **Public-server hardening:** verified TLS, authenticated accounts, upload
  limits, built-in Let's Encrypt; players can type just a server name.

[Release notes](docs/release-0.7.21-notes.md) · [Validation report](docs/release-0.7.21-validation.md) · [Source at v0.7.21](https://github.com/pocketgeek/tak-engine/tree/v0.7.21)

## 0.7.20
*2026-10-01 · protocol 212 · replay format 9 · does not play with 0.7.19 or earlier*

- **Naval AI** for all five sides in both balances: coastal production, usable
  shipyard launch routes and smaller fleets.

[Release notes](docs/release-0.7.20-notes.md) · [Validation report](docs/release-0.7.20-validation.md) · [Source at v0.7.20](https://github.com/pocketgeek/tak-engine/tree/v0.7.20)

## 0.7.19
*2026-10-01 · protocol 211 (unchanged from 0.7.18) · replay format 9*

- **Native Windows ARM64** portable ZIPs.
- **Native Intel macOS** packages; both macOS builds need macOS 14 or later.

[Release notes](docs/release-0.7.19-notes.md) · [Validation report](docs/release-0.7.19-validation.md) · [Source at v0.7.19](https://github.com/pocketgeek/tak-engine/tree/v0.7.19)

## 0.7.18
*2026-10-01 · protocol 211 · replay format 9 · does not play with 0.7.17 or earlier*

- **Darien Crusades service:** allegiance, territory browsing, matchmaking,
  tactical battles, verified history and replays, on servers that enable it.
- **Crusades balance corrections** from the retail data.
- Veruna establishes fleets; converters reject targets they cannot capture.
- Victory opens results after three seconds.

[Release notes](docs/release-0.7.18-notes.md) · [Validation report](docs/release-0.7.18-validation.md) · [Source at v0.7.18](https://github.com/pocketgeek/tak-engine/tree/v0.7.18)

## 0.7.17
*2026-09-30 · protocol 203 (unchanged from 0.7.16) · replay format 9*

- Right-click cycles settings backward.
- Game-create choices and random-map recipes are remembered (not passwords).
- A local 24-hour clock in the stats panel.

[Release notes](docs/release-0.7.17-notes.md) · [Validation report](docs/release-0.7.17-validation.md) · [Source at v0.7.17](https://github.com/pocketgeek/tak-engine/tree/v0.7.17)

## 0.7.16
*2026-09-30 · protocol 203 · replay format 9 · does not play with 0.7.15 or earlier*

- **Larger generated maps** up to 64×64, with ruins round every mana spot.
- **Gentler Easy AI;** Passive renamed **Defensive**.
- Conversion targeting and production exits fixed; results use the F4 emblems.

[Release notes](docs/release-0.7.16-notes.md) · [Validation report](docs/release-0.7.16-validation.md) · [Source at v0.7.16](https://github.com/pocketgeek/tak-engine/tree/v0.7.16)

## 0.7.15
*2026-09-29 · protocol 201 · replay format 9 · does not play with 0.7.14 or earlier*

- Expired unit records are removed from the simulation, so long games stay
  fast (a synthetic test went from 6.26 ms to 0.022 ms per tick and checksum).

[Release notes](docs/release-0.7.15-notes.md) · [Validation report](docs/release-0.7.15-validation.md) · [Source at v0.7.15](https://github.com/pocketgeek/tak-engine/tree/v0.7.15)

## 0.7.14
*2026-09-29 · protocol 200 · replay format 9 · does not play with 0.7.13 or earlier*

- **Authored scenarios:** Use Only restrictions, neutral placements,
  armour/weapon and veteran defaults, per-player victory and defeat.
- **Retail trigger behaviour** for rule ownership, timing and selectors.
- Authored names in game (31-byte limit); Display gameclock works.
- **Cartographer:** browsers, undo/redo, recovery, background saving, map
  checks, regions and rules, F6 model inspector, F5 Test Map.

[Release notes](docs/release-0.7.14-notes.md) · [Validation report](docs/release-0.7.14-validation.md) · [Source at v0.7.14](https://github.com/pocketgeek/tak-engine/tree/v0.7.14)

## 0.7.13
*2026-09-29 · protocol 198 · replay format 9 · does not play with 0.7.12 or earlier*

- **Cartographer desktop workflow:** launchers, recent maps, searchable
  browsers, background KMP saving, recovery snapshots, map checks, named
  regions, objective templates and an F5 Test Map handoff.
- Diplomacy hides gifting and mana sharing for non-allies.

[Release notes](docs/release-0.7.13-notes.md) · [Validation report](docs/release-0.7.13-validation.md) · [Source at v0.7.13](https://github.com/pocketgeek/tak-engine/tree/v0.7.13)

## 0.7.12
*2026-09-29 · protocol 195 · replay format 9 · does not play with 0.7.11 or earlier*

- **Diplomacy on D:** unit gifting, outgoing mana sharing and chat recipients.
- Hard and Absurd expand earlier.
- Memory and resource fixes.

[Release notes](docs/release-0.7.12-notes.md) · [Validation report](docs/release-0.7.12-validation.md) · [Source at v0.7.12](https://github.com/pocketgeek/tak-engine/tree/v0.7.12)

## 0.7.11
*2026-09-29 · protocol 194 (unchanged from 0.7.10) · replay format 9*

- **Defensive AI** keeps growing near home without attacking.
- **Hard and Absurd** keep expanding past the former ceilings.
- Zhon producers build an opening army.

[Release notes](docs/release-0.7.11-notes.md) · [Validation report](docs/release-0.7.11-validation.md) · [Source at v0.7.11](https://github.com/pocketgeek/tak-engine/tree/v0.7.11)

## 0.7.10
*2026-09-28 · protocol 194 · replay format 9 · does not play with 0.7.9 or earlier*

- **Large-map performance:** terrain and fog reuse, cheaper distant models,
  shadows, pathfinding and collision grid.
- **Random maps** with validated routes and balanced resources, saved on every
  participant.
- **Automatic map sharing** in multiplayer.
- **Allied unit gifting** (Alt+G; moved to the D screen in 0.7.12), surplus
  mana to the emptiest ally first.
- **Options → Player Name;** unit limit and Allow Speed Change at game creation.

[Validation report](docs/release-0.7.10-validation.md) · [Source at v0.7.10](https://github.com/pocketgeek/tak-engine/tree/v0.7.10)

## 0.7.9
*2026-09-27 · protocol 189 (unchanged from 0.7.8) · replay format 8*

- Windows: map-list crash with accented filenames fixed; no console windows.
- NVIDIA statistics through NVML; Windows AMD/Intel GPU statistics.
- The retail F4 score table.
- Double Sight/Radar chosen at game setup.

[Validation report](docs/release-0.7.9-validation.md) · [Source at v0.7.9](https://github.com/pocketgeek/tak-engine/tree/v0.7.9)

## 0.7.8
*2026-09-27 · protocol 189 · replay format 8 · does not play with 0.7.7 or earlier*

- Explosion corpses and scripted death timing restored.
- Building palette shading and scripted shadow flags, as in retail Glide.
- A streaming shutdown race fixed.

[Validation report](docs/release-0.7.8-validation.md) · [Source at v0.7.8](https://github.com/pocketgeek/tak-engine/tree/v0.7.8)

## 0.7.7
*2026-09-27 · protocol 187 · replay format 8 · does not play with 0.7.6 or earlier*

- Builders clear trees and rocks from building sites.
- **In-place lodestone upgrades** for all five sides.
- Building ruins as authored; explored terrain stays mapped under fog;
  **Double Sight/Radar.**
- Defeated players go to results without pausing the others.

[Validation report](docs/release-0.7.7-validation.md) · [Source at v0.7.7](https://github.com/pocketgeek/tak-engine/tree/v0.7.7)

## 0.7.6
*2026-09-26 · protocol 184 (unchanged from 0.7.5) · replay format 7*

- Title and lobby music in YouTube streams.
- The retail monarch-under-attack warning.
- Group recall: one number-key press selects, a second tracks.

[Validation report](docs/release-0.7.6-validation.md) · [Source at v0.7.6](https://github.com/pocketgeek/tak-engine/tree/v0.7.6)

## 0.7.5
*2026-09-26 · protocol 184 · replay format 7 · does not play with 0.7.1 or earlier*

- **Built-in YouTube streaming** (F9): Max 3840 / 60 FPS defaults, hardware
  encoding with CPU fallback, VSync-aware pacing.
- Clearer performance statistics (Real Time, Game Time, client and server CPU,
  GPU use).
- Campaign presentation and objective fixes; Sea Fort placement fixed.

[Source at v0.7.5](https://github.com/pocketgeek/tak-engine/tree/v0.7.5)

## 0.7.1
*2026-09-26 · protocol 179 · replay format 7 · does not play with 0.7.0 or earlier*

- One animated shadow silhouette per unit (960 mixed units: 110 to about 162
  FPS in local tests).
- Flyers no longer land on other aircraft.
- Transport pickup and unload, and animation lifecycles, corrected.

[Source at v0.7.1](https://github.com/pocketgeek/tak-engine/tree/v0.7.1)

## 0.7.0
*2026-09-21 · protocol 177 · replay format 7 · does not play with 0.6.9 or earlier*

- **Naval combat fixes:** ships fire across open water and shorelines; shore
  defenders can hit ships.

[Source at v0.7.0](https://github.com/pocketgeek/tak-engine/tree/v0.7.0)

## 0.6.9
*2026-09-20 · protocol 176 · replay format 7 · does not play with 0.6.8 or earlier*

- Better ground, boat and flyer movement; flyers attack after landing.
- Better AI force building.
- Lobby limit of 2,000 units per player; Benchmark targets up to 16,000 units.
- A multiplayer desync fixed.

[Source at v0.6.9](https://github.com/pocketgeek/tak-engine/tree/v0.6.9)

## 0.6.8
*2026-09-15 · protocol 80 · replay format 6*

- Every build is fully static (no runtime libraries beyond the system's).
- The Monarch no longer starts inside a cliff.
- An honest simulation-speed readout.

[Source at v0.6.8](https://github.com/pocketgeek/tak-engine/tree/v0.6.8)

## 0.6.7
*2026-09-15 · protocol 80 · replay format 6*

- Crowds reach their goals instead of giving up.
- Battle stutter removed: over-budget ticks on an 8-AI benchmark fell from 148
  to 1.
- Software-renderer crash with anti-aliasing fixed.

[Source at v0.6.7](https://github.com/pocketgeek/tak-engine/tree/v0.6.7)

## 0.6.6
*2026-09-14 · protocol 80 · replay format 6*

- Two desyncs fixed (map features for display were blocking the simulation's
  grid; a spectator whose game ended froze the referee).
- Rejoin, spectate and replay fixes.

[Source at v0.6.6](https://github.com/pocketgeek/tak-engine/tree/v0.6.6)

## 0.6.5
*2026-09-13 · protocol 70*

- **Pathfinding rebuilt on the original game's algorithm.**
- **Shadows as the Glide renderer drew them;** Smooth GUI Art and Smooth
  Movies options.
- The fog pass runs off the simulation thread.

[Source at v0.6.5](https://github.com/pocketgeek/tak-engine/tree/v0.6.5)

## 0.6.0
*2026-09-12 · protocol 65*

- **Cartographer,** a clean-room port of the original map editor.
- **Multiplayer accounts** with SCRAM-SHA-256 sign-in.
- **One order queue** for every mobile unit, with the retail order line.
- Weapons, unit footprints and campaign scripting re-derived from the original.

[Source at v0.6.0](https://github.com/pocketgeek/tak-engine/tree/v0.6.0)

## 0.5.8
*2026-09-10 · protocol 35*

- The full retail animation audit: death cries, gaits, debris, turrets.
- Roads, burning forests and the complete retail corpse model.
- Terrain drawn from per-section textures.

[Source at v0.5.8](https://github.com/pocketgeek/tak-engine/tree/v0.5.8)

## 0.5.7
*2026-09-10 · protocol 28*

- Retail melee range by footprint.
- Generated coastlines, ship animation, the macOS app launching without a
  Terminal window.

[Source at v0.5.7](https://github.com/pocketgeek/tak-engine/tree/v0.5.7)

## 0.5.6
*2026-09-09 · protocol 27*

- **Random map generator** with a live preview.
- Water drawn the retail way.
- Per-distribution Linux packages, a Windows installer and a macOS DMG.
- A server list on the multiplayer connect screen.

[Source at v0.5.6](https://github.com/pocketgeek/tak-engine/tree/v0.5.6)

## 0.5.5
*2026-09-09 · protocol 26*

- **Benchmark** (Settings → Benchmark).
- First-run game data folder picker.
- A Settings menu; explored-terrain fog option.

[Source at v0.5.5](https://github.com/pocketgeek/tak-engine/tree/v0.5.5)

## 0.5.0
*2026-09-09 · protocol 22*

- Statically linked libraries on every platform.
- The simulation runs on its own thread, apart from drawing.

[Source at v0.5.0](https://github.com/pocketgeek/tak-engine/tree/v0.5.0)

## 0.4.1
*2026-09-08 · protocol 22*

- Bink door videos decoded by a built-in FFmpeg.
- Sound device choice and live switching.

[Source at v0.4.1](https://github.com/pocketgeek/tak-engine/tree/v0.4.1)

## 0.4.0
*2026-09-08 · protocol 22*

- Servers run several games in parallel.
- Single-player spectating of AI games; **Monarch Expendable** option.
- Rebindable hotkeys.

[Source at v0.4.0](https://github.com/pocketgeek/tak-engine/tree/v0.4.0)

## 0.3.0
*2026-09-07 · protocol 20*

- Five AI levels (Passive, Easy, Normal, Hard, Absurd).
- Control groups and formations.
- Campaign progress tracking, with no locked missions.
- A large performance pass.

[Source at v0.3.0](https://github.com/pocketgeek/tak-engine/tree/v0.3.0)

## 0.2.1
*2026-09-07 · protocol 17*

- Windows build fix; no gameplay changes.

[Source at v0.2.1](https://github.com/pocketgeek/tak-engine/tree/v0.2.1)

## 0.2.0
*2026-09-07 · protocol 17*

- **The campaigns:** *Book of Darien* and *Iron Plague*, with movies,
  briefings, objectives and the alternate ending.

[Source at v0.2.0](https://github.com/pocketgeek/tak-engine/tree/v0.2.0)

## 0.1.0
*2026-09-07 · protocol 16*

- The retail front end, animated cursors and build and reclaim effects.

[Source at v0.1.0](https://github.com/pocketgeek/tak-engine/tree/v0.1.0)

## 0.0.1
*2026-09-06 · protocol 16*

- First versioned build: skirmish against the AI, client-server multiplayer,
  the retail HUD, and Linux, Windows and macOS builds.

[Source at v0.0.1](https://github.com/pocketgeek/tak-engine/tree/v0.0.1)
