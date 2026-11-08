# TAK Engine 0.9.23

0.9.23 follows 0.9.22 a day later. It adds an Idle Builders column, draws shots and
effects at their place in the scene, and tidies the game setup, room and Connect to
Server screens.

**Compatibility.** 0.9.23 keeps network protocol **316** and replay format **15**, the
same as 0.9.22: **0.9.23 plays with 0.9.22, in either direction, and plays its
replays.** A 0.9.22 server needs no update for 0.9.23 players. The random map generator
(version 9) is unchanged. One change lives on the server: the computer players' flying
builders (below) behave the new way only where the server runs 0.9.23.

## Playing

- **Idle Builders.** A column of portraits at the right of the map shows each kind of
  builder or factory of yours that has nothing to do, with how many of it. Click a
  portrait to select the next idle one and move the view to it; **Shift**+click selects
  them all. The Monarch comes first, then mobile builders and then factories, each from
  basic to advanced. A flying builder that is only landing counts as idle.
  - Options → Interface → **Idle Builders**: **Top**, **Middle** (the default) or
    **Bottom** places the column; **Off** hides it.
  - **Idle Builders Scale** (50–400%, default 50%) sizes the portraits on top of UI
    Scale. **Build Menu Scale** now also goes down to 50%.
- **Shots and effects at their place in the scene.** Arrows, bolts, spells, flames,
  explosions, glows, debris, blood and smoke are drawn in depth order with the units and
  trees on the ground: one behind a tree is covered by it, one in front of it paints over
  it. The original game drew every shot and effect over everything on the ground; this
  is a deliberate change.
- **Computer players' flying builders** no longer wait out their landing after each
  building before taking the next job; the Zhon Monarch lost about 4–5 seconds per
  building. Ground builders were never affected, and the Retail AI already treated the
  landing as idle.

## Menus and screens

- **Game setup screen** (single player and multiplayer): a **Filter** box beside the
  Name / Players / Size sort buttons keeps the maps whose title or name contains the
  text, upper or lower case alike; the side margins are even and the map list is wider.
  Play the Machine's screen is titled as its door and drops the repeated headings and the
  MAP line. In multiplayer, **Back** returns to the game browser and the separate
  Browser button is gone.
- **Cached map copies are hidden.** The map list leaves out a cached copy of a map (one
  received from a server, or a Cartographer Test Map snapshot) while a map of the same
  name is installed. Joining a game that uses the copy still works, and the copies stay
  on disk.
- **The game room.** The map's name heads its preview; the player table sits higher, and
  in single player the names are wider. The mods are a short summary under the map:
  **Always on**, your **Cosmetic** packs and the host's **Game** packs, each wrapping onto
  more lines as needed. **MODS...** opens the mods screen: cosmetic and game packs side by
  side, each with a count against the 64-pack limit, a **Filter** box and a scrolling list
  of tick boxes, the always-on packs locked at the top. Guests see the host's game packs
  without changing them. **Done** or **Esc** returns to the room.
- **Room chat** (multiplayer): the box runs from under the mods down to the text field,
  without the Chat and Say labels, and the mouse wheel scrolls back through older lines.
- **Connect to Server**: the default server, then **Custom...**, then the servers you
  have signed in to, most recent first: up to 64 (was 8), six at a time with a scroll
  bar. Each remembered server has **Delete**. **Back** no longer looks disabled. Typed
  without a prefix, `localhost`, `127.x.x.x` and `::1` connect without TLS, so a local
  test server (`takserver --local`) works as typed; `tls://` still forces TLS, and every
  other address stays TLS-only.
- **Loading screens.** Play the Machine's is titled as its door and starts at
  *Starting...*. Connecting to a server shows a loading screen titled *Play an Opponent*
  (*Connecting to*, then *Signing in to* the server) instead of the bare title screen.
- **Options**: a pick from a list (Idle Builders, Build Menu, Chat Position, Health Bars,
  Hidden Units, Texture Filtering and the rest) takes effect at once in a running game,
  as toggles and sliders already did.

## Under the hood

- No hashed simulation state changed: the `--mpai` gates keep 0.9.22's hashes; see the
  [validation report](release-0.9.23-validation.md).
- The streaming test's simulated local-copy failure now triggers after the second
  keyframe, so it no longer fails on a slow machine.
