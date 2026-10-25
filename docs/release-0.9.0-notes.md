# TAK Engine 0.9.0

0.9.0 follows 0.8.14. It brings a new set of guides, with the release history
moved into one [CHANGELOG](../CHANGELOG.md), and a handful of game changes that
bring area clearing, gate doorways, the computer players and build sparkles
closer to the original game.

**Compatibility first.** The area-clear changes alter the game simulation, so
0.9.0 uses network protocol **294** and replay format **15**. **0.9.0 does not
play with 0.8.14 or any earlier version, and does not play their replays.**

- **Everyone updates together.** A 0.9.0 player cannot join a 0.8.x server, and
  a 0.8.x player cannot join a 0.9.0 server: the connection is refused with a
  *protocol version mismatch* message. Update your game and tell the people you
  play with to update theirs.
- **Server operators:** update `takserver` to 0.9.0 when you announce the
  release. Until you do, 0.9.0 players cannot use your server; afterwards, 0.8.x
  players cannot. Crusades battle replays recorded by 0.8.x on your server show
  as *Recorded replay version unsupported* and can no longer be watched in
  0.9.0.
- **Old replays:** a replay recorded by 0.8.14 or earlier needs a version with
  that replay's protocol. Older versions are no longer offered for download;
  to keep watching old replays, keep your current install, or build the
  version from its git tag (see the [development guide](development.md)).
- **Everything else carries over:** settings, your data folder choice, key
  bindings, saved `.kmp` maps, generated-map recipes, mod packs and their
  ticks, and `ModCache/` downloads.

## A new set of guides

- **Release history lives in the [CHANGELOG](../CHANGELOG.md).** Every
  published release, from 0.0.1 to this one, has an entry with its date,
  protocol, replay format, who it plays with and anything you must do when
  updating. Entries from 0.7.11 on link their full release notes in `docs/`,
  and every entry links the release's source (`tree/vX` on GitHub).
- **The README is short and player-first**: what the engine is, the download
  table, installing, getting started, playing, mods and hosting.
- **The [quick start](quick-start.md), the [user guide](user-guide.md), the
  [map editor guide](cartographer-guide.md), the
  [public server guide](public-server.md) and the
  [streaming guide](streaming.md)** were rewritten: the quick start is task
  first, the user guide gains an [Updating](user-guide.md#updating) section,
  and the map editor guide gains quick how-tos. Their pictures were retaken
  from the 0.9.0 build.
- **Downloads** are always at
  [the latest release](https://github.com/pocketgeek/tak-engine/releases/latest).
  Older versions stay available as source, from their git tags.

## Clearing trees and rocks

- **Every selected builder takes the order.** A right-drag box, a right-click
  on one tree or rock, or a **CLEAR** click now goes to every selected unit that
  can reclaim, each working through the same box closest first, as in the
  original game. Before, only the first builder did. Other selected units keep
  their orders, and **Shift** queues the clear behind each builder's own orders.
- **No more trees an area clear skips.** Where two of a map's trees, rocks or
  other objects overlap, the original game removes the earlier one when the map
  loads. We kept it, drawn and blocking but never picked by an area clear, so a
  clear could leave it standing. It is now gone, as in the original. Some
  forests have small new gaps (Inner Circle loses one tree; 481 of the 1,360
  maps checked lose at least one object), and such an object no longer makes a
  sacred site or a lodestone deposit.
- **Under the fog.** Trees and rocks on ground your side has explored are
  cleared even while you cannot see them, as in the original; wrecks of fallen
  units still have to be in sight. With the room's FOG OF WAR set to
  **EXPLORED** or **FULL VISION**, the whole map counts as explored.
- **Unreachable objects.** A builder gives up on a tree or rock it cannot
  reach, and an area clear ends when the nearest object left is one it already
  failed to reach, as in the original. Before, a builder could keep trying
  forever.

See [Reclaiming](user-guide.md#reclaiming) in the user guide.

## Gates

Clicking the doorway of an **open** gate now works like clicking the ground, as
in the original: it selects nothing, and a right-click moves the selected units
through or into the opening instead of guarding, repairing or attacking the
gate. A closed gate, the gate's posts and units standing in the doorway are
still picked as before. This applies to every shipped gate.

## Computer players and the Rolling Tower

The original game's AI profiles (`ai/default.txt` and eleven others) spell the
Aramon Rolling Tower `araseige`. The original ignores the misspelt name, so its
AI builds Rolling Towers without limit, and our own levels, whose tables copied
the spelling, never built one. Both now read either spelling as the Rolling
Tower: the **Retail AI** keeps to the profile's limit (10 in `ai/default.txt`)
and our levels (**Easy** to **Absurd**) build up to 10. A profile you write may
use either spelling. Only the server runs the computer players.

## Build sparkles

The sparkles over a building going up now match the original. The ring sits on
the edge of the building's footprint and holds the original number of sparkles;
the 49 buildings whose unit file has no `bmcode` line were sized like mobile
units, with a wider ring and a quarter of the sparkles. Rising sparkles on high
ground or over water also climb as high as in the original before they fade.
This is display only.

## For modders and mapmakers

- Where a map's features overlap, the game now removes the earlier one when it
  loads the map, on every computer alike, as in the original. To keep both,
  place them so they do not overlap.
- An AI profile may name the Rolling Tower `arasiege` or `araseige`; both mean
  the same unit. Every other unknown unit name is still ignored, as in the
  original.
- A unit file with no `bmcode` line now reads as `bmcode=0`, as in the
  original (it read as 1 before). Every shipped mobile builder has a `bmcode`
  line, so this changes only buildings' construction sparkles in the shipped
  data; a modded mobile builder without one should say `bmcode=1`.

## For contributors

- Area clear (`737653ed`, protocol 294, replay format 15):
  `World::setMapPlacementFeatures` keeps only the installed plane
  (`World::installedMapFeatures`, `retailInstalledMapFeatures`), from which
  `matchsetup` registers blockers, sacred sites, deposits and tracked features;
  `RetailMapFeatureCell` carries the cell owner byte (hashed when not the world
  owner); `tickReclaimArea` reads the builder owner's bit of the hashed
  `navigationExplored_` plane and the room's FOG OF WAR
  (`MatchConfig::roomMapRevealed` / `roomLosOff`, `World::setRoomVision`), and
  the order ends by retail's exits. Retail evidence is in
  [guessed-area-clear-2026-09-26.md](guessed-area-clear-2026-09-26.md) and
  [retail-engine.md](retail-engine.md).
- Clear fan-out and drawing (`b8762727`, client): `GameView::reclaimers()`
  replaces `firstReclaimer()`, and `GameView::loadFeatures` draws only
  `World::installedMapFeatures()`. New ctest `reclaim_fanout`.
- Gate doorways (`18e66f65`, `41d33c43`, client): `GameView::unitUnderCursor`
  skips an open gate when the height-aware ground point falls in one of its own
  `c`/`C` yard cells (retail `0x520530`). New ctest `gate_pick` covers
  `arangate`, `cregate`, `tarngate` and `verngate`.
- Rolling Tower (`5a3820f5`, `0f81db17`, server-side AI):
  `classicUnitSpelling()` in `src/ai/classicprofile.cpp`, shared by the Classic
  executor and `applyProfileText`; the built-in table says `arasiege`.
- Build sparkles (`00494013`): `buildMovementCode` defaults to 0, and the
  cosmetic emitter's ceiling comes from the owner's world Y. The hashed restore
  path keeps the corner height, so no hashed state changes there.
- `--mpai` Inner Circle seed 1 hashes are new with protocol 294: at 60 s Retail
  `89c5eadaa7290dbb`, Legion `b1a0afcc56c14990`, a Retail AI seat
  (`TAK_AI_LEVEL=6`) `68f228cec489010b`; at 300 s Retail `9b58b36177e3a767`,
  Legion `1ddb894f6d73dee5`. The cross-compiler golden (`check-determinism`)
  stays `dcef618cd2e4d558`.

## Known limits

New with 0.9.0:

- **Reclaiming is still ours in reach and speed.** How close a builder must
  stand and how long a tree or rock takes are this engine's, not the
  original's: the original works about four times slower and reclaims from
  next to the object's footprint.
- **The Shift order-trail preview** of a queued area clear uses what your side
  sees now, not the explored ground the order will use.
- **Flying reclaimers** reach their targets with a plain move.

All of 0.8.14's known limits carry over:

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
- The Retail AI's server log still says `DEFAULT AI profile ai/DEFAULT.txt`, and
  the Cartographer's tab still reads **DEFAULT AI** (log and label wording
  only).
- **Old always-on archives are not moved for you**, and the game does not warn
  about a `.hpi` left directly in `mods/cosmetic/` (see 0.8.14's
  [notes](release-0.8.14-notes.md#moving-your-always-on-archives)).

## Compatibility

Protocol **294** (was 293), replay format **15** (was 14); generator version
**9**, campaign payload **4** and Crusades SQL schema **9** unchanged from
0.8.14. Checked in source against `v0.8.14`: `kNetVersion` in
`src/net/protocol.h`, `kReplayFormat` in `src/net/replayhdr.h`, the map
generator's `formatVer` (`src/tnt/mapgen.h`), the campaign payload's `kVersion`
(`src/net/crusades.h`) and `kSchemaVersion` (`src/server/crusades/store.cpp`).

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.0 clients and servers play only with each other**: no 0.8.x
  client can join a 0.9.0 server, nor a 0.9.0 client an 0.8.x server.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.9.0 plays only 0.9.0 replays. Format 15 adds the room's FOG OF
  WAR option to the header. Crusades battle replays follow the same rule.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes,
  `ModCache/` downloads, pack folders and settings files carry over unchanged.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.9.0-validation.md).
