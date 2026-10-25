# TAK Engine 0.9.2

0.9.2 follows 0.9.1. It fixes four bugs seen in an all-AI game: a Fire Wagon
that burned the same standing stone for minutes, Creon Submersibles that lobbed
their shells into the water at their own bow, computer players that trapped new
units behind their own buildings, and Navy Yards built where ships could not get
out.

**Compatibility first.** The flame and lobbing fixes change the game
simulation, so 0.9.2 uses network protocol **296** (replay format **15**,
unchanged). **0.9.2 does not play with 0.9.1 or any earlier version, and does
not play their replays.**

- **Everyone updates together.** A 0.9.2 player cannot join a 0.9.1 server, and
  a 0.9.1 player cannot join a 0.9.2 server: the connection is refused with a
  *protocol version mismatch* message. Update your game and tell the people you
  play with to update theirs.
- **Server operators:** update `takserver` to 0.9.2 when you announce the
  release. Until you do, 0.9.2 players cannot use your server; afterwards, 0.9.1
  players cannot. Crusades battle replays recorded by 0.9.1 on your server show
  as *Recorded replay version unsupported* and can no longer be watched in
  0.9.2.
- **Old replays:** a replay recorded by 0.9.1 or earlier needs a version with
  that replay's protocol. To keep watching 0.9.1 replays, keep your 0.9.1
  install (0.9.1 and 0.9.0 stay on the GitHub releases page); versions before
  0.9.0 are no longer offered for download and can be built from their git tags
  (see the [development guide](development.md)).
- **Everything else carries over:** settings, your data folder choice, key
  bindings, saved `.kmp` maps, generated-map recipes, mod packs and their
  ticks, and `ModCache/` downloads. No setting was added or changed.

## Flames and scenery

A unit whose shots keep hitting something it cannot destroy (a hill, water, a
rock or a standing stone) walks to a spot with a clear shot after three blocked
shots in a row. Flames never counted, so a flame unit never moved. Now they do:

- **Flames count as blocked shots**, like arrows and spears, when they stop on
  indestructible scenery before reaching their target.
- **On the move, even on Hold Position.** A unit that picked its target by
  itself while carrying out a movement order (an attack-move, a patrol or a
  queued move) walks to a clear shot, even on Hold Position. When it finds no
  spot, or has used up its three tries, it drops that target and carries on
  with its orders. The Creon Fire Wagon starts on Hold Position and the computer
  players attack-move it, so this is what lets it get round a stone. With
  nothing queued, a unit on Hold Position still fires on from where it stands.

In the reported game a Fire Wagon burned a standing stone 101 times in a row
without ever reaching the lodestone behind it. In a 20-minute all-AI test game
on 0.9.2, 135 Fire Wagon shots had no such run.

See [Blocked shots](user-guide.md#blocked-shots) in the user guide.

## Lobbing weapons

A weapon that lobs its shot in an arc (the Creon Submersible, catapults, mortars
and the like) is now in range only where its arc can reach the target, as in
the original game. Before, the engine counted the target's footprint towards
the range, so a Submersible 822 to 848 pixels from an 800-range lodestone
thought it was in range, found no arc, and lobbed every shell flat into the
water at its bow. In the reported game 44% of all Submersible shells landed
beside the Submersible itself.

- **Closing in.** A unit ordered to attack a target just past its arc's reach
  closes in until it can hit.
- **Holding.** One that cannot get nearer (a boat whose target stands inland,
  past the shore) holds without firing.
- **No more flat shells.** A loaded, aimed weapon whose arc cannot reach from
  where its muzzle is spends its reload as a blocked shot instead of firing.
  After three, the unit gives up on the target from that spot: it picks
  another, or, on Hold Position or as a building, drops it.
- **No churn.** Buildings and units on Hold Position no longer pick a target
  their arc cannot reach only to drop it again at once.

In the 20-minute test game, 1 of 197 Submersible shells landed within 300
pixels of the Submersible (290 pixels out, on high ground), against 44% in the
reported game.

## Computer players: factory doors

Every level except Retail now keeps the ground in front of its factory doors
clear:

- it does not build in front of a door, nor put a new factory where its own door
  would face a building or a planned one;
- its builders do not stand in a door's path or in a factory's open yard while
  they build;
- it does not send units to wait in a building or in another factory's doorway;
- a conjurer standing in a factory's open yard waits for the yard to be cleared
  before conjuring.

In the reported game an Academy built just in front of a Smithy's door held a
Fire Wagon inside for two and a half minutes. In three 15-minute test games no
two factory doors faced each other closer than 80 pixels, and every new land
unit left its factory within 10 seconds (on average in under 2).

## Computer players: Navy Yards

Every level except Retail now prefers a Navy Yard site where a ship leaving the
door has water straight ahead out to open water. It looks a little further than
before for such a site, then for any site whose door is clear, and only then
takes the first usable site as before. On the reported map the yard at the head
of a narrow inlet, where new ships stayed stuck, is no longer chosen.

The Retail AI places yards as the original did.

## For modders and mapmakers

- A Ballistic weapon's range now follows the original's arc solve: from the
  firing unit's origin to the target's origin under the map's gravity, with no
  allowance for the target's footprint. A modded lobbing unit whose
  `range` was only just enough against large buildings may now need to come
  closer, and a modded boat with a lobbing weapon may hold without firing at
  targets inland.
- A flame weapon (one that fires a flame stream) now counts its blocked shots
  like any other weapon.

## For contributors

- Sim (`6697b952`, `cc5e966e`, protocol 296): Ballistic weapons use retail's
  BallisticWeapon inRange (`52bc60`) in `slotInRange`, the attack order's fire
  gate and approach, and `repositionForShot`; `queryWeaponAim` flags a no-arc
  solve unreachable and `World::noteNoArc` counts it as a blocked shot.
  `FlameShot::targetId` (hashed) feeds `noteShotImpact`. `findTarget` skips
  targets past the origin arc for units that may not leave their post, and
  targets written off from the same spot. `approachSlot()` is shared by
  `tickCombat` and `findTarget`. New checks in `naval_combat_test` and
  `blocked_shot_test`; `tools/re/check_ballistic_inrange.py` matches 6,000
  emulated `52bc60` answers. Evidence in [retail-engine.md](retail-engine.md).
- AI (`1a2f1333`, `064cb448`, `src/ai/ai.cpp`, our levels only; the Retail
  AI's ClassicBrain is untouched): factory door strips (yardmap edges with open
  `c`/`C` cells; span plus 16 px each side, 80-112 px deep, capped at retail's
  7-cell street; none for producers whose outputs all fly) checked by
  `placeSite`, door clearing and `stationArmy`; `navalDoorSteps` door
  clearance for Navy Yard siting, cached for one think. New checks in
  `ai_behavior_test`. Notes in
  [ai-defensive-2026-10-17.md](ai-defensive-2026-10-17.md) and
  [naval-ai-2026-10-01.md](naval-ai-2026-10-01.md).
- `--mpai` Inner Circle seed 1: at 60 s unchanged from 0.9.1 (Retail
  `d375d03a6c472773`, Legion `426ab0842b709c28`, Retail AI
  `c5846d769e0a7d93`); at 300 s Retail `00499a875aa5f0db` (was
  `a470242be92a0e14`), Legion `15097d3fe06e195b` (was `a65a48d7a63825ae`),
  moved by the AI change only. The cross-compiler golden
  (`check-determinism`) stays `dcef618cd2e4d558`.

## Known limits

New with 0.9.2:

- **A boat with an unreachable target waits.** A Submersible (or other boat
  with a lobbing weapon) whose only target stands inland past the shore keeps
  that target and holds without firing, as the original does. The computer
  players do not yet give it other work.
- **No arc from the muzzle.** Where the arc from the unit's origin reaches but
  the arc from its muzzle does not, the original fires a flat shell; 0.9.2
  instead spends reloads without firing and drops the target after three.
- **Ships leave some Navy Yards slowly.** At a yard whose door opens partly onto
  the shore, ships took a median of about 7 seconds to get clear in the test
  games; a crowd of Iron Clads can still block a yard's door.
- **Door strips on several sides.** The Aramon Keep and Castle have open
  cells on their west and east edges as well as the south, so the computer
  players keep all three sides clear. Whether that is too cautious is a
  judgement call; it was left as it is.
- **A conjurer in a factory yard** waits for the yard to be cleared; if no spot
  to clear it to is free within 600 pixels it could wait for ever. This was not
  seen in testing.

Still true from 0.9.1:

- **Legion factory spots.** In a test of 100 Catapults built at 20× speed, one
  stayed held behind the Keep; a unit's spot does not avoid the lane of a
  neighbouring building's door; and once all 16 layers of spots are full, a new
  unit falls back to leaving clear of the building.
- **Ships and yards.** The size of pond our AI refuses (256 cells) is ours; the
  original's AI has no such test, so the Retail AI can still put a yard on a
  pond. When a ship's ring has no reachable spot, the farthest reachable spot is
  our own fallback, not the original's.
- **Aiming cost.** 0.9.1's aim point costs more to work out than 0.9.0's, and no
  large-battle benchmark compared the two.
- **The hardware cursor.** No engine cause was found for the reports that it
  feels like a software cursor; Options and the log say which cursor runs.
- **Streaming capture.** Direct3D 9 still waits for the graphics card on each
  readback, and Metal and the software renderer still read back the whole
  window on the render thread.
- **The Windows renderer** was measured on one NVIDIA machine. On other
  drivers OpenGL may be slower than Direct3D 11; switch in Options.
- **macOS streaming** still uses the system's SecureTransport (patched); moving
  it to OpenSSL is planned, not done.

All of 0.9.0's known limits carry over:

- **The Shift order-trail preview** of a queued area clear uses what your side
  sees now, not the explored ground the order will use.
- **Flying reclaimers** reach their targets with a plain move and still
  start from their old distance; they work for the original's time.
- **The fade** of a reclaimed object is not drawn: it stays as it was for
  about a second, then disappears.
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

Protocol **296** (was 295), replay format **15**, generator version **9**,
campaign payload **4** and Crusades SQL schema **9**, all but the protocol
unchanged from 0.9.1. Checked in source against `v0.9.1`: `kNetVersion` in
`src/net/protocol.h`, `kReplayFormat` in `src/net/replayhdr.h`, the map
generator's `formatVer` (`src/tnt/mapgen.h`), the campaign payload's `kVersion`
(`src/net/crusades.h`) and `kSchemaVersion` (`src/server/crusades/store.cpp`).

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.2 clients and servers play only with each other**: no 0.9.1
  or older client can join a 0.9.2 server, nor a 0.9.2 client an older server.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.9.2 plays only 0.9.2 replays; a 0.9.1 replay (format 15,
  protocol 295) is refused. Crusades battle replays follow the same rule.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes,
  `ModCache/` downloads, pack folders and settings files carry over unchanged;
  no settings key was added or renamed.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.9.2-validation.md).
