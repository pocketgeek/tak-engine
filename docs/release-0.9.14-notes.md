# TAK Engine 0.9.14

0.9.14 follows 0.9.13. It adds a new AI level, Passive, puts the lobby's AI
levels in a new order, lays the selection ring at a unit's feet as in the
original, and stops the system arrow showing through loading screens when
Hardware Cursor is on.

**Compatibility first.** A Passive seat changes nothing in the game state the
players compare, but an older server would store the new level as Normal, so
0.9.14 uses network protocol **308** (replay format **15**, unchanged).
**0.9.14 does not play with 0.9.13 or any earlier version, and does not play
their replays.**

- **Everyone updates together.** A 0.9.14 player cannot join a 0.9.13 server,
  and a 0.9.13 player cannot join a 0.9.14 server: the connection is refused
  with a *protocol version mismatch* message. Update your game and tell the
  people you play with to update theirs.
- **Old replays:** a replay recorded by 0.9.13 or earlier is refused with the
  version message. To keep watching them, keep your 0.9.13 install (0.9.0 to
  0.9.13 stay on the GitHub releases page); versions before 0.9.0 are no
  longer offered for download and can be built from their git tags (see the
  [development guide](development.md)).
- **Players:** nothing to do. Settings, your data folder choice, key bindings,
  saved `.kmp` maps, generated-map recipes and mod packs carry over. An AI seat
  saved at any level keeps that level.
- **Server operators:** update `takserver` to 0.9.14 when you announce the
  release; until you do, 0.9.14 players cannot use your server. Nothing else
  about running a server changed (see
  [public-server.md](public-server.md#versions)).

## New AI level: Passive

Passive plays exactly like Easy: the same build pace, the same build profile,
the same waves of at most eight units on the same timing. Two things differ.

- **Nothing of its goes looking for you.** It sends no scouts, and its army and
  fleet never explore or head for a start position, so it attacks only what it
  has seen: enemies that come within its sight or its allies'. If nothing does,
  it sits at home building.
- **Its builders never leave home.** It takes mana deposits and builds only
  within 900 pixels of its base, where Easy's builders walk out to 1,600.

Like Easy it has no income bonus, upgrades no lodestones and keeps two builders.
A game pack's or map's `ai/takengine.txt` applies to it as to the other levels.

## The AI levels' new order

The lobby's level button now cycles Passive, Easy, Normal, Hard, Absurd,
Defensive, Absurdly Defensive, Retail, then back to Passive; right-click goes
the other way. A new AI seat still starts on Normal. Cartographer's preview
level follows the same order, without Passive (it plays Easy's table) and
Retail (its own tab).

## Selection ring

The ring floated at the visible model's mid height and took its size from every
piece except the root, so a tall unit such as the Death Totem wore it round its
crown. As in the original (`KINGDOMS.icd` `4fd0d0`), it now reads the root
piece, the unit's ground plate, alone: it lies at the unit's feet at the plate's
size, centred on that box, and turns with the unit. This is display only.

## Loading screens and the cursor

With Hardware Cursor on, each screen put the system's default cursor back as it
closed, and the arrow stayed up through the load until the next screen's first
frame. The game's cursor now stays up until the next screen sets its own. You
still see the system arrow at launch, before the cursor art has loaded.

## Known limits

- The cursor change could not be checked on a real display before the release
  (no display in the test setup; it compiles, and the path it changes is short).
- Attack waves a Passive AI sends at something it saw do not walk home when
  they finish: they wait where they stopped for the next order.
- A defensive AI's idle fighters at the front stay where they stopped until
  sent, so a few melee units can stand near a target another group is killing.
  This is 0.9.13's behaviour, unchanged.
- All of 0.9.13's limits carry over (see
  [release-0.9.13-notes.md](release-0.9.13-notes.md#known-limits)).

Validation: [release-0.9.14-validation.md](release-0.9.14-validation.md).
