# TAK Engine 0.9.7

0.9.7 follows 0.9.6. It makes builders help the way the original game's
builders do: they can finish a unit that a factory or a Zhon builder is still
producing, a guarding builder repairs and helps what it guards, a flyer on
guard circles what it guards, and the game itself decides who may help.

**Compatibility first.** The shared build progress, the guard's next look, a
flying guard's circling and the help walk join the game state the players
compare, so 0.9.7 uses network protocol **301** (replay format **15**,
unchanged). **0.9.7 does not play with
0.9.6 or any earlier version, and does not play their replays.**

- **Everyone updates together.** A 0.9.7 player cannot join a 0.9.6 server, and
  a 0.9.6 player cannot join a 0.9.7 server: the connection is refused with a
  *protocol version mismatch* message. Update your game and tell the people you
  play with to update theirs.
- **Old replays:** a replay recorded by 0.9.6 or earlier is refused with the
  version message. To keep watching them, keep your 0.9.6 install (0.9.0 to
  0.9.6 stay on the GitHub releases page); versions before 0.9.0 are no longer
  offered for download and can be built from their git tags (see the
  [development guide](development.md)).
- **Players:** nothing to do. Settings, your data folder choice, key bindings,
  saved `.kmp` maps, generated-map recipes and mod packs carry over.
- **Server operators:** update `takserver` to 0.9.7 when you announce the
  release; until you do, 0.9.7 players cannot use your server. Nothing else
  about running a server changed (see [public-server.md](public-server.md#versions)).

## Helping to build

A builder helps with **HelpBuild**: the Assist right-click on an unfinished
unit, and what a guarding builder does (below). 0.9.7 follows the original's
rules for it, found by running the retail routines (the engine copies none of
the original's code or data):

- **Who may help.** The unfinished unit must be alive, not the helper itself and
  owned by the helper's own side: an ally's unfinished unit does not qualify
  (allies' damaged units can still be repaired). The helper must be a builder
  that can move, so buildings never help. A builder that may build only what is
  on its own menu helps only with a type on that menu. Nothing else is asked:
  not what the helper is doing, not a repeat queue. The sim now enforces all of
  this when it receives the order (`World::canHelpBuild`, used by
  `World::assist` and the command handler), so it holds whatever the client
  sent; before, only the client checked the menu.
- **One shared progress.** The work on a unit that a factory or a conjurer is
  producing (a Zhon mobile builder produces at a placed site) is a single
  progress, the producer's. Every helper adds its own share, scaled by its work
  rate; mana is paid per unit of progress, so the total is the unit's build
  cost however many help, and more helpers finish it sooner and spend the mana
  faster. The unit finishes once, and its producer is freed at that moment (the
  next queued unit can start the same tick). A building site that has no
  producer grows from its own hit points, as before.
- **What was wrong before.** Helping with a produced unit marked it finished
  early while the producer kept its slot until its own timer ran out, charged
  the full cost again on top of the helper (108 mana too much on a unit that
  costs 311, from a Zhon builder), kept adding hit points to the finished unit
  and then, on its own completion, gave the unit its exit or rally orders again.
- **Orders at completion.** The finished unit gets its exit step (a mobile
  producer's) or PARK and rally (a factory's) exactly once. If you gave the
  unfinished unit orders, they stand and no exit step, PARK or rally is added.
- **A factory with no helper is unchanged.** Its unit finishes on exactly the
  tick it did before; `helpbuild_test` checks the 180-tick case and its cost.
- **Repeat queues.** A Zhon builder on a repeat queue can be told to assist; the
  Assist right-click is no longer withheld from it (a Move, Patrol, Attack or
  fight-move order still sets its rally, as in 0.9.6). Its own product waits,
  held and not decaying, while it helps elsewhere, and resumes afterwards.

## Guarding builders

A mobile builder with a Guard order looks at what it guards (a new guard looks at
once; one that found nothing looks again 15 to 29 game ticks, half a second
to a second, later; one that started a job looks again on the first tick after the
job ends). In the original's
order:

1. The guarded unit is damaged and repairable (allied units count): the builder
   repairs it. A builder that cannot walk to it does not take the repair on.
2. The guarded unit is itself unfinished and eligible: the builder helps build
   it.
3. Otherwise it follows the guarded unit's job: help with the site or product
   the guarded unit builds, produces or helps (a builder still walking to its
   site is just followed; a flying producer is not helped from the ground), or
   repair the unit the guarded unit repairs.

The job is put **ahead of** the guard order, which resumes when the job ends.
Distance is never checked. A repair started by a guard follows its target: while
the target is out of reach, the approach is re-aimed once a second at where the
target now stands, so a repaired unit that walks off is followed and repaired to
full (the test walks it 900 px; the guard never fell more than 250 px behind).
A conjurer on a repeat queue and a builder at work on its own job do not look.

**Flying builders** (the original's `VTOL_Follow`, emulated) run the same three
checks in the same order, with two differences in what they help: a flying
guard also helps a flying producer (a Thirsha's output or site) and joins a
flyer's repair, which a ground guard does not; and a flying guard counts every
target as reachable except one whose top is under the sea, which it treats as
not repairable at all (so it goes on to help with what that unit is doing, as
the original does). Its cadence also differs: a look that finds nothing looks
again after exactly 15 ticks, or on the next tick once the flyer reaches its
point on the circle (below), and a look that starts a job sleeps 15 plus 0 to
29 ticks. Its jobs are the
original's flyer jobs: it flies to the target until it is inside twice its
build distance, starts work there and then circles the target at about its
build distance (re-picking its point now and then, as the original does). Help
work is never re-measured once started; a repair pauses and flies back in
whenever the target is more than twice the build distance away. The same
approach and circling now apply to a flying builder told to Assist or Repair
directly, and a flyer refuses to repair a unit under the sea.

## Flyers on guard

Every flyer's Guard order runs the original's `VTOL_Follow`: the original
picks it for a guard by the unit's ability to fly alone, so armed flyers,
unarmed flyers and flying builders all use it. When a flying builder finds
nothing to help or repair (the checks above come first), and always for any
other flyer, the guard **circles the guarded unit**:

- The circle's radius is 320 px, or the active weapon's range plus 160 when the
  flyer is armed (a weapon switcher's selected weapon, otherwise its first).
  The original reads "armed" from a unit flag whose setter was not found; the
  engine takes it from the type's weapons, as its AI port does.
- The guard starts at a random angle on the circle (0 to 65,535). It flies to
  the point at that angle from the guarded unit's current position and counts
  as there within 128 px. Each arrival steps the angle back by a quarter turn
  plus 0 to an eighth of a turn and wakes the look on the next tick; otherwise
  the look comes again after a fixed 15 ticks with no random draw, and the point
  follows the guarded unit from look to look.
- A flyer given the guard while off the map first heads 800 px back toward it,
  as the original does, and keeps its guard. The look's deadline belongs to the
  guard order itself, so a queued guard starts at once.
- Before, a flyer on guard hovered within 70 px of the guarded unit, braking
  there; that hold is gone for flyers on a running guard.

## For modders and mapmakers

No file format changed. A builder's menu now comes with its type
(`UnitType::buildMenu`), so the sim can apply the menu rule without the client.

## For contributors

- Sim (protocol 301): `Unit::guardHelpTick` (hashed when set; a ground
  guard's, while a flying guard keeps its next look on its order's mission),
  `Order::guardOrbitAngle` (hashed with the guard's mission once it runs),
  `Order::helpSite` (hashed when set), `Order::guardRepair` (hashed when set);
  `Unit::productionOwner` is a hint and is not hashed. New functions
  `World::canHelpBuild`, `canRepairUnit`, `productionOwnerOf`,
  `workProduction`, `finishProduction`, `tickGuardHelp`, `helpApproach`,
  `tickFlyingRepair`, `payRepair`, `tickFlyingGuard` (with the shared
  `guardHelpPick` and `guardInsertJob`); `tickConjureHover` takes the repair
  re-pick rule; `tickFlightMovement` flies a running flying guard's own
  controller. A flyer's help admission is `conjureHoverTarget` (already hashed), so
  a flying repairer's orbit uses the same hashed fields as flying construction. The producer's own
  step is a full tick (65,536 sub-ticks) as before, so its solo timing is
  unchanged.
- Net: `kNetVersion` 300 -> 301, no new command, replay format 15.
  `World::assist`'s command handler accepts Assist from a repeating conjurer.
- Client: the Assist right-click no longer rejects a repeat builder for an
  unfinished target; nothing else about selection or cursors changed.
- Harness (debug builds only, `#ifndef NDEBUG`): `TAK_AUTOPLAY` also builds,
  trains, toggles repeat on conjurers, has builders guard busy units and assist
  unfinished ones, and gives idle flying builders their own guard turn
  (`GameView::autoplayHelpStep`), sending idle or idly circling flying
  builders to guard damaged units (busy ones if none are damaged). It prints
  `AUTOPLAY_HELP` and `AUTOPLAY_FLYGUARD` lines to stderr. `TAK_START_FLYERS=N`
  (at most 16, set on every peer, as `TAK_GODS`) starts each player with N
  finished flying builders of its side, every other one at half health (Aramon
  and Taros; the Zhon flying builder is its Monarch, so Zhon gets none), and
  `TAK_AUTOPLAY_SIDE` picks the autoplay seat's side. Release builds read none
  of these.
- Tests: new `helpbuild` (shared production, eligibility, the repeat
  conjurer, guard-help, a moving repair, flying guards and flying help/repair
  reach and orbit, a flying guard's circle around the guarded unit, an
  off-map and a queued flying guard, determinism); `flyer_combat_test` now
  checks that a guarding Zongryp or Creaeri circles its charge instead of
  holding over it; `replay_test` now pins
  301 and refuses 300.
- `--mpai` Inner Circle seed 1, `TAK_SPEED=80`: **the gate hashes did not
  change** (the AIs never guard or assist, and no helper ever works on their
  production; the flying-guard port and its circling left them unchanged too): Retail 60 s `d0da4ccc71d2067c`, 300 s `e820d37865085bd9`; Legion
  60 s `c3f6a3a13f0c167f`, 300 s `0783cb05f7f128cf`; Retail AI 60 s
  `4952493f51b9a568`; Crusades 300 s Retail `f2f51dc1703fc209`, Legion
  `c4189ae189dd68dc`. The cross-compiler golden (`check-determinism`) is
  unchanged, `dcef618cd2e4d558`. See the
  [validation report](release-0.9.7-validation.md).

## Known limits

New with 0.9.7:

- **Parts of the original's flying guard are not ported:** its reaction when
  the guarded unit is attacked, its step-out after landing and its own attack
  scan; the engine's target acquisition does a guard's fighting instead. A
  flying builder at work on its own job, or on a repeat queue, does not run its
  guard and still hovers near the guarded unit until it does. A flying repair's
  optional leash (from where the order was given) is not ported.
- **A flyer's re-pick costs no ticks here.** When a flying helper or repairer
  picks a new point to circle to, the original appears to spend two ticks
  without work before it works again; the engine re-picks and works in the same
  tick, as its flying construction already did, so the random draws fall on
  different ticks.
- **A flying repairer chases a moving target continuously;** the original flies
  to a fixed point, arrives and starts the repair over. Neither gets stuck, but
  the paths differ.
- **Guard re-checks use the sim's random generator** (15 plus 0 to 14 ticks for
  a ground guard that found nothing, 15 plus 0 to 29 for a flying guard that
  started a job, and a flying guard's circle angle and steps), the same ranges
  as the original's; retail's exact random
  stream is not reproduced.
- **A guard's repair re-aims once a second,** not on retail's 7 to 21 tick
  schedule, to avoid extra random draws. While chasing a unit that moves at its
  own speed the guard trails it by about the gap it started with (140 to 200 px
  in the test).
- **A ground guard skips a repair it cannot walk to** (a damaged flyer over
  water, say) and just follows; the original instead tests the target's height
  against the sea. The reachability test falls
  back to the nearest walkable cell within 24 cells, so a unit over water near a
  shore still counts as reachable and the builder waits on the shore. No test
  covers the unreachable case.
- **A conjurer's own product waits while it helps elsewhere** (one job at a time,
  as the original's single mission); orders you gave an unfinished unit stand
  at completion; a monarch can assist its own product.
- **Right-click Guard is still offered only to armed units** (as before); a
  builder gets the order from the G key or the command panel.
- **No sweep showed a guard repairing an ally** (the AIs do not damage their
  allies): all 14 guard repairs counted in the first desync sweep were of the
  guarded unit itself. Ally repair is covered by `helpbuild_test`.
- **Zhon flying guards were driven only by scratch harness changes.** The Zhon
  flying builder is its Monarch, which the committed autoplay never puts on
  guard; Aramon and Taros flying guards helped and repaired in the multiplayer
  checks (see the [validation report](release-0.9.7-validation.md)).
- **0.9.6's limits still apply:** see the [0.9.6 notes](release-0.9.6-notes.md#known-limits) (Wayland testing,
  rally and cloak orders against a remote referee, producer rallies) and, through it, the [0.9.5 notes](release-0.9.5-notes.md#known-limits).

## Compatibility

Protocol **301** (was 300), replay format **15** and generator version **9**,
all unchanged except the protocol. Checked in source against `v0.9.6`:
`kNetVersion` in `src/net/protocol.h` and `kReplayFormat` in
`src/net/replayhdr.h`.

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.7 clients and servers play only with each other**.
- **Replays.** Playback requires the recording's protocol to equal the engine's,
  so 0.9.7 plays only 0.9.7 replays; a 0.9.6 replay (format 15, protocol 300) is
  refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes, pack
  folders and settings files carry over unchanged.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.9.7-validation.md).
