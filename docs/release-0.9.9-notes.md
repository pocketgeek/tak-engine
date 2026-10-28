# TAK Engine 0.9.9

0.9.9 follows 0.9.8. A ground guard now waits on a ring round what it guards,
as the original game's guards do, so a builder guarding a factory no longer
parks in its doorway; your own idle and guarding units step off a site you
build on; a walking builder refuses a site it can never reach. Our AI levels
build divine lodestones only where they pay, and the Retail AI no longer
upgrades its own lodestones. Max FPS can be Unlimited, MAKE POSTER moves to
Shift+P, the title screen's Settings pages go back to SETTINGS, the Benchmark
runs without mods, and a build icon's tooltip no longer overlaps the TRAINING
line.

**Compatibility first.** Units stepping off a build site and the guard ring
join the game state the players compare, so 0.9.9 uses network protocol
**303** (replay format **15**, unchanged). **0.9.9 does not play with 0.9.8 or
any earlier version, and does not play their replays.**

- **Everyone updates together.** A 0.9.9 player cannot join a 0.9.8 server, and
  a 0.9.8 player cannot join a 0.9.9 server: the connection is refused with a
  *protocol version mismatch* message. Update your game and tell the people you
  play with to update theirs.
- **Old replays:** a replay recorded by 0.9.8 or earlier is refused with the
  version message. To keep watching them, keep your 0.9.8 install (0.9.0 to
  0.9.8 stay on the GitHub releases page); versions before 0.9.0 are no longer
  offered for download and can be built from their git tags (see the
  [development guide](development.md)).
- **Players:** nothing to do. Settings, your data folder choice, key bindings,
  saved `.kmp` maps, generated-map recipes and mod packs carry over. A MAX FPS
  saved above 240 now reads as Unlimited, and a MAKE POSTER key you set
  yourself is kept (below).
- **Server operators:** update `takserver` to 0.9.9 when you announce the
  release; until you do, 0.9.9 players cannot use your server. Nothing else
  about running a server changed (see
  [public-server.md](public-server.md#versions)).

## Guards wait on a ring round what they guard

Reported: a builder guarding a factory, or helping it build, sometimes stood in
its doorway, and nothing more got built.

A unit on **Guard** used to walk to the middle of what it guarded and stop
within 70 pixels of its centre. For a factory that is its yard (a Barracks'
pad is 65 pixels from its centre): the guard stood where the next unit is
made, and the factory, which never pushes a body off its pad (nor does the
original's), could not start another one.

The original's ground guard (`Follow_Ground`) does not do that. Found by
running the retail routines (the engine copies none of the original's code or
data), it keeps to a ring round the unit it guards:

- **The ring.** Measured from the guarded unit's centre, its inner edge is
  (the guarded unit's longer footprint side + the guard's footprint width + 2)
  cells of 16 pixels, and it is three cells deep. A guard of a Barracks waits
  about 256 to 304 pixels from its centre.
- **Holding and walking.** A ground guard anywhere in that band stays put;
  outside it, the guard walks to the ring's point on its own bearing from the
  guarded unit. The band is its only arrival, so a large guard (a siege engine,
  a Flagship) walks into it rather than stopping a footprint short and walking
  in place there.
- **Helping.** A guarding builder still walks in to help or repair each unit
  being made and goes back out to the ring between them; a ship guarding a
  shipyard does the same.
- **Guards of soldiers** keep a little further off than before, for the same
  reason.
- **Flyers** are unchanged: they keep the original's `VTOL_Follow` orbit and
  70 px hold.
- **Legion pathfinding** plans a guard's leg to the same ring point.

Measured on the doorway test (below): before the change 77 of its 216 Guard
and Assist cases stalled production or left the guard in the yard; none do
now. In the 64-guard crowding fixture, walking in place fell from 1060 (0.9.8)
to 867.

## Your units step off a build site

Reported: units in the way of a build should move, even when they are guarding
the builder.

In the original, a builder whose site is taken only waits: it retries the
placement every 10 ticks and drops the order after 31 tries, and the build
cursor refuses a site any visible body stands on. 0.9.9 goes beyond that for
the player's own units:

- **Who steps off.** Your own idle units and your own units on Guard, mobile
  and able to move. On each of the builder's retries they are sent just off
  the site: to its nearest side (a guard to the side nearest the unit it
  guards; the builder's side last), each to its own spot along that side, so
  a crowd of guards does not walk into one another. Flyers in the air over a
  mobile unit's site count too.
- **Guards go back.** A guard's Guard order stays queued under the move and
  resumes once it is off the site.
- **Placing.** The build cursor, build lines and the clear-then-build
  placement accept a site such units stand on; before, it was refused.
- **Who still blocks.** Units of other players (allies included) and your own
  units that are moving, fighting or working still make the builder wait, and
  it gives up after about ten seconds, as in the original.

## Builders only place what they can reach

A walking builder could place a shipyard, a ship or a water creature out in
open sea or on a lake it has no way to reach; the order was accepted, and the
builder walked to the shore and gave up. The original does the same (its
placement test looks only at the type, the cell and the player, and the
builder later reports it cannot reach the construction).

0.9.9 refuses such a site in the client: the placement ghost turns red and the
click is refused, for single placement, clear-then-build and build lines. A
site is reachable when some cell the builder can walk to from where it stands
lies within its build reach of the site. Flying builders, structures and
builders inside a transport always pass. The simulation still accepts such an
order exactly as before, so this changes no game state.

## The AI and divine lodestones

**Our six AI levels** (not the Retail AI) now upgrade only where it pays:

- **Only an own lodestone, only once the base is full.** A divine lodestone
  goes only on one of the AI's own finished lodestones, never on a free
  deposit, and only when no free deposit within 1600 pixels of home is left
  for a plain lodestone (which pays for itself in seconds, so it always comes
  first). The lodestone must be at full health, within 1600 pixels of home,
  with no spotted armed enemy within 600 pixels.
- **Payback.** The upgrade's cost, the old lodestone's income lost while it is
  built and, if needed, a priest's cost must be paid back by the extra mana
  within 15 minutes (20 for Absurdly Defensive). Richest deposit first, then
  nearest; one at a time.
- **Saving up.** Before buying, the AI saves at least 60% of the next purchase
  (the priest, then the divine lodestone), holding other new builds back but
  not a lodestone it needs because it is short of mana. A threat within 900 pixels
  of home stops the saving; a stage not bought within 3 minutes is given up
  and tried again 5 minutes later. Nothing is planned where the store cannot
  hold the reserve, or past the type's limit.
- **A priest.** With no priest (shaman, engineer) a factory trains exactly
  one. A priest starts a dragon or other troops only with mana banked (half
  its cost, or 90% of the store's capacity if that is less).
- **Per level:**

| Level | Earliest | At most | Payback horizon |
| --- | --- | --- | --- |
| Defensive | 15 min | 5 | 15 min |
| Easy | never | none | |
| Normal | 10 min | 2 | 15 min |
| Hard | 8 min | 6 | 15 min |
| Absurd | 6 min | no limit | 15 min |
| Absurdly Defensive | 8 min | no limit | 20 min |

Before, Hard and Absurd started divine lodestones on free deposits far from
home with an empty store, and a priest started a dragon it could not pay for.
In all-AI games on real data, Inner Circle Normal upgraded at 13.8 minutes
(finished at 17.5) and Absurdly Defensive at 14.0 (17.0); on Ulasem Arena Hard
made four upgrades between 11.8 and 22.3 minutes. None went on a free deposit,
and no dragon was started with an empty store.

**The Retail AI** no longer upgrades a lodestone in place. The original's
placement test refuses a site any live unit stands on and has no in-place
upgrade, so its AI builds a divine lodestone only on a free sacred site. Ours
admitted a divine lodestone over the AI's own finished lodestone; the Retail
AI's deposit search now passes such a site over for the next free one in
reach, or builds nothing.

**You** can still upgrade a lodestone in place yourself (an engine addition
since 0.7.7).

## Smaller changes

- **Max FPS.** The Options slider runs from 30 to 240, and its far right end
  is **Unlimited** (saved as 0). A saved cap above 240 (the settings file used
  to allow up to 480) now loads as Unlimited.
- **Back to Settings.** Options, Controls, Voice Chat, Benchmark and YouTube
  Streaming opened from the title screen's SETTINGS menu go back to it on BACK
  or Esc, as Load Replay already did, instead of to the title screen.
- **MAKE POSTER is Shift+P.** The default key for the power code is now
  Shift+P instead of Print Screen, which many desktops take for their own
  screenshots and Mac keyboards lack. A key you set in CONTROLS is kept.
- **The Benchmark runs the plain game.** It no longer loads your ticked
  cosmetic or game mods, nor the `always_enabled` packs, so a mod cannot change
  the units, AI or art being measured, and two machines' numbers compare. Its
  room asks for no game mods, and the server keeps a Benchmark room at the
  original game data whatever it is sent. Your mod choices are untouched and
  apply again in your next game.
- **TRAINING line and tooltips.** The TRAINING line above the build menu and
  the tooltip of the build icon under the mouse were drawn in the same place;
  the tooltip now takes the line's place while it is shown.

## For modders and mapmakers

No file format changed. A unit type's footprint now also sets how far its
guards wait from it (and how far it waits as a guard). The Benchmark ignores
every mod pack; test a game mod in an ordinary skirmish.

## For contributors

- Sim (protocol 303): `World::canPlaceOrder` (build orders and the cursor:
  `canPlace` passing over units `yieldsToSite` accepts), `clearBuildSite` on
  each placement retry (exit points slide a body at a time past taken spots;
  the circle test uses raw 16.16 coordinates and counts airborne flyers), and
  `World::guardRing` (Follow_Ground's ring; `tickCombat` and the ground mover
  hold a guard in its band, and the band is its only arrival). Legion's Guard
  mission plans to the ring point. No hashed field was added.
- `World::buildSiteReachable` (not hashed; client placement only) and
  `World::lodestoneUpgradeBase` (a pure refactor of
  `lodestoneUpgradeSource`'s faction-pair lookup).
- AI: `Controller::planUpgrade` and `upgradeLevelFor` (`src/ai/ai.cpp`);
  `ClassicBrain::siteOpen` / `depositSite` refuse an own-lodestone site.
- Client: `src/client/benchmarkmods.h` (`applyBenchmarkMods`); the server
  forces `gameMods` 0 for a Benchmark room. The placement preview cache is
  keyed on the builder.
- Net: `kNetVersion` 302 -> 303, no new command, replay format 15.
  `tools/replay_test.cpp` pins 303.
- Tests, all failing on the old code: new `doorway_test` (data-backed: every
  shipped ground factory makes four units with a side builder on Guard or
  Assist from three start points, both pathfinding modes and balances, plus
  ring-rest and naval-door cases; 232 cases), new `naval_reach_test` (Retail
  and Legion), `pathblock_test` site-clear cases (an idle unit, a guard of the
  builder, a Hold Position unit, a unit walking on, three guards, a guard back
  on its ring), new `benchmark_mods_test` and a Benchmark step in
  `mod_transfer_test`, `settings_test` Max FPS, `poster_test` Shift+P,
  `classic_ai_test` lodestone sites and `ai_behavior_test` divine upgrades
  (13 checks fail on the old code). Guard distances in `movement_orders_test`,
  `pathblock_test` and `legion_world_test` moved to the ring;
  `attackring-guard-64` counts guards within 144 px and its baseline and the
  navigation checkpoints after its guard order were retaken.
- `--mpai` Inner Circle seed 1, `TAK_SPEED=80`: **the gate hashes did not
  change** (the AIs never guard, no gate game builds on a site its own idle
  units stand on, and the divine-lodestone rules change no pick in them):
  Retail 60 s `d0da4ccc71d2067c`, 300 s `e820d37865085bd9`; Legion 60 s
  `c3f6a3a13f0c167f`, 300 s `0783cb05f7f128cf`; Retail AI 60 s
  `4952493f51b9a568`; Crusades 300 s Retail `f2f51dc1703fc209`, Legion
  `c4189ae189dd68dc`. The cross-compiler golden (`check-determinism`) is
  unchanged, `dcef618cd2e4d558`. See the
  [validation report](release-0.9.9-validation.md).

## Known limits

New with 0.9.9:

- **Placement reach is checked by the client only.** A modified client can
  still send an order for a site its builder cannot reach; the game accepts
  it and the builder gives up, as in the original. The check is made from
  where the builder stands now (so a Shift-queued build is judged from there,
  not from the end of the queue), and only for the first selected builder.
- **The guard ring is re-aimed every tick,** where the original re-aims it at
  each of the guard's looks.
- **Allies' units on a site still block.** Only your own idle and guarding
  units step off; an ally's make your builder wait and give up, as in the
  original.
- **Benchmark and loose maps.** A loose file in the install's `Maps/` folder
  named like Ulasem Arena is not a mod pack, so the Benchmark would use it.
- **Inner Circle's double home deposit** (at 497 pixels) cannot take a Taros
  Divine Lodestone, because a feature overlaps the site.
- **The Retail AI change** can only matter where an in-place upgrade would
  have been its choice.
- **0.9.8's limits still apply:** see the
  [0.9.8 notes](release-0.9.8-notes.md#known-limits), and through them the
  0.9.7, 0.9.6 and 0.9.5 notes.

## Compatibility

Protocol **303** (was 302), replay format **15** and generator version **9**,
all unchanged except the protocol. Checked in source against `v0.9.8`:
`kNetVersion` in `src/net/protocol.h` and `kReplayFormat` in
`src/net/replayhdr.h`.

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.9 clients and servers play only with each other**.
- **Replays.** Playback requires the recording's protocol to equal the engine's,
  so 0.9.9 plays only 0.9.9 replays; a 0.9.8 replay (format 15, protocol 302) is
  refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes, pack
  folders and settings files carry over unchanged (a Max FPS above 240 reads
  as Unlimited).

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.9.9-validation.md).
