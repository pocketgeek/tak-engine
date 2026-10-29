# TAK Engine 0.9.11

0.9.11 follows 0.9.10. Our AI levels now go after every enemy unit and
building, not only what they can see, so an AI that is winning hunts down a
player's last lodestone or unit and the game ends. A factory's unfinished unit
dies with the factory, as in the original game, and cancelling the unit in
work destroys it too. A hovering flyer whose shots keep stopping on a standing
stone moves round to a clear line. Full zoom-out now shows the whole map
beside the right-hand panel, Legion units face the way they slide, and Hidden
Units' Fade Opacity defaults to 50%.

**Compatibility first.** The hover move, the factory's unfinished unit dying,
Legion facing and the AI's target choice all join the game state the players
compare, so 0.9.11 uses network protocol **304** (replay format **15**,
unchanged). **0.9.11 does not play with 0.9.10 or any earlier version, and
does not play their replays.**

- **Everyone updates together.** A 0.9.11 player cannot join a 0.9.10 server,
  and a 0.9.10 player cannot join a 0.9.11 server: the connection is refused
  with a *protocol version mismatch* message. Update your game and tell the
  people you play with to update theirs.
- **Old replays:** a replay recorded by 0.9.10 or earlier is refused with the
  version message. To keep watching them, keep your 0.9.10 install (0.9.0 to
  0.9.10 stay on the GitHub releases page); versions before 0.9.0 are no
  longer offered for download and can be built from their git tags (see the
  [development guide](development.md)).
- **Players:** nothing to do. Settings, your data folder choice, key bindings,
  saved `.kmp` maps, generated-map recipes and mod packs carry over. A Fade
  Opacity already in your settings file is kept; 0.9.10 saved 65% there, so
  set the slider to 50% yourself if you want the new default (below).
- **Server operators:** update `takserver` to 0.9.11 when you announce the
  release; until you do, 0.9.11 players cannot use your server. Nothing else
  about running a server changed (see
  [public-server.md](public-server.md#versions)).

## The AI goes after everything (beyond the original game)

Reported from a 0.9.10 replay: eight Absurd AIs on a generated map with
Monarch-expendable. From minute 15 to the end at minute 38 the last loser's two
lodestones and four units stood in a far corner, while the winner, 468 units
strong, sent its army about 3,400 times to a dead player's start position.
Our levels aimed every wave at the nearest enemy they could see, else at the
nearest enemy **start**, the same one every time, dead or not.

The original game's AI scores **every** enemy unit for its strikes, seen or
not (its strike picker, `KINGDOMS.icd` `0x40a200`, read and run in emulation;
the engine copies none of the original's code or data): unseen units count
about ten times less, and it prefers lodestones and mana buildings. Our six
levels (Defensive to Absurdly Defensive; **not** the Retail AI) now do the same
in their own way:

- **Every enemy unit and building, seen or not.** Enemy units are gathered
  into 400-pixel sites. A site is worth what its units cost, with lodestones
  and mana buildings counted three times, builders and production twice, the
  Monarch heavily when losing it loses the game, and a bonus that grows as a
  player's last units dwindle; armed units add threat, more so near our base.
  A site nobody of ours sees counts for about a third (Easy: 0.15). So these
  levels now ignore fog **for choosing targets**, as the original's AI does;
  they still see and fight under fog as before.
- **Weighed against practicality.** Distance, the defences in and around the
  site against the force being sent (with a per-level margin), and whether the
  force can get there at all (per kind of mover: ground, water, hover, air).
  A raid picks off an exposed lodestone rather than walking into towers.
- **Keep going.** Units already sent go straight on to the next site when they
  run out of targets, instead of going home to wait for the next attack. They
  come home when badly outmatched (the site's defence more than three times
  their strength, unless the AI is far ahead), and give up for 2 minutes on a
  target they keep failing to hurt (out of reach on a cliff, say).
- **Per level.** Easy goes for the nearest site, seen ones first. Hard and
  Absurd split a large force over up to four sites. Defensive and Absurdly
  Defensive still never raid, but once five times stronger than all their
  enemies together they send half their army out to finish the game. (For
  the other levels three times stronger counts as far ahead: their units
  then no longer turn back from a well-defended target.)
- **Melee** walks to a building's edge instead of its centre.
- **The Retail AI** (level 6) is unchanged; it already matches the original
  (no mismatch over 4,000 checked picks and thinks).

Time to a winner (all-AI games, game minutes):

| Game | 0.9.10 | 0.9.11 |
| --- | --- | --- |
| The reported map, 8 Absurd, seed 1 | no winner in 120 min | 82 min |
| The reported map, 8 Absurd, seed 2 | 80.8 min | 36.2 min |
| Ulasem Arena, 4 levels, two seeds | no winner | 34.8 and 54.7 min |
| Inner Circle, Absurd vs Absurd, two seeds | 25.6 and 11.1 min | 33.8 and 25.8 min |

The even Inner Circle duels got slower (the numbers are noisy): armies that
used to meet head-on at a start now go for lodestones and builders first.

## A factory's unfinished unit dies with it

In 0.9.10 a destroyed factory left the unit it was building standing, un-built
and unprotected, fading away at its build rate without counting as anyone's
loss. In the original game, a factory's death aborts its production mission
(`BuildingBuild`, `0x401c20`, run in emulation), which deals the unfinished
unit 30,000 damage of type 11 from the factory: it dies at once, with no
corpse, no explosion, no death weapon and no kill credited to anyone, and
counts as the owner's loss. 0.9.11 does the same.

- **When the factory dies** (destroyed, self-destructed, removed by a
  mission script), its unfinished unit dies with it.
- **When the factory changes hands** (a gift, a script transfer, a capture),
  the unfinished unit dies: the original makes a new object for the new owner
  and kills the old one, so its production is aborted. The capture path is
  inferred from the code, not run (see Known limits).
- **Cancelling the unit in work** destroys it at once (the user's rule), in
  line with the original's per-type count (`0x429810`, `0x4d7ca0`): cancel
  from the build menu when it is the **last** queued unit of its type, or turn
  **repeat** off. Cancelling a unit still waiting in the queue only removes
  it, as before.
- **Stop** clears the factory's queue, so it destroys the unit in work too.
  **Beyond the original:** the original's Stop keeps production running.
- **A player who leaves or forfeits** has every unit stopped, so their
  factories lose their unfinished units.
- **No mana back.** The original charges mana tick by tick while building and
  refunds nothing on an abort; so does 0.9.11.
- **Unchanged:** a Zhon builder's unfinished unit (Zhon produce with mobile
  builders) and a builder's unfinished building stay behind and decay, as in
  the original (its mobile and help-build missions only stop the animation).

## Hovering flyers find a clear shot (beyond the original game)

Reported from a 0.9.10 game: a Thirsha hover-attacking a Zhon Hunter just past
a `ZonHenge11` standing stone (indestructible, height 94) fired Lightning into
the stone for minutes: every bolt stopped on its top, about 148 pixels up.
Another did the same at a lodestone behind a `ZonHenge09`. The original does
the same: its bolts stop at a feature's top (`0x52a4d0`), and its hover
mission keeps the flyer's bearing from the target at every wake
(`0x41de5d`..`0x41df64`), so it never re-aims.

Since 0.9.2, a ground unit whose shots keep stopping on scenery it cannot
destroy walks to a clear shot. 0.9.11 extends that to hover attackers: after
three of its shots in a row stop on such scenery, the flyer moves round its
hover ring to the nearest bearing (in steps of 1/32 of a turn, either way)
from which the line to the target is clear, and fires from there. Flyers on
attack runs, buildings and melee are unchanged. In the reported geometry,
before: 14 bolts on the stone and no damage; now: 4 on the stone, one move,
10 hits.

## Full zoom-out shows the whole map

The zoom-out limit was worked out for the whole window, but the map is seen
only left of the right-hand panel and above the bottom bar. Fully zoomed out,
the map's right edge stayed hidden under the panel (on Ulasem Arena about 11%
of the map's width at 1920 x 1080, 2.6% at 7680 x 2160), and the bottom strip
could not be scrolled out from under the bar. The limit and the scroll limits
now use the area the map is actually seen in (a spectator has no bottom bar,
so a spectator's area is taller). Tactical Dots' zoom threshold and the
Benchmark's camera follow.

## Legion: units face the way they slide

Reported: "when a unit is kinda sliding around something that's fine, but make
them face in the direction they are actually going." With **Legion**
pathfinding, a unit turned only toward its route, so a unit slipping round a
wall end, passing through a crowd or moving over at a merge walked sideways or
backward. The original's mover always steps along its facing, so a Retail unit
always faces its motion.

Now a Legion unit stepping at a quarter of its travel speed or more turns, at
its turn rate, toward the way it has actually been moving over the last few
updates. Its route and pacing are unchanged: arrivals, timings and decisions
in the 31 measured scenarios are identical. Sideways steps round a wall end
fell from 35 to 9 per 1,000, round a standing block from 69 to 16; over the 31
scenarios, sideways steps fell by about two thirds (395,217 to 127,299) and
backward ones likewise (86,133 to 28,674).

Still unturned: slow creeps and side-steps in a jam (turning there read as
spinning), units yielding, units aiming, and units on **fight, attack, guard
or patrol** orders, whose facing serves their weapons (turning them raised the
work of a large battle by 12 to 18%). **Retail** pathfinding is unchanged.

## Fade Opacity defaults to 50%

Hidden Units' **Fade Opacity** (Options → Graphics) now starts at 50% instead
of 65%, so a tree, rock or building over a unit fades a little further. A value
already in `settings.ini` is kept, and 0.9.10 wrote 65% there on its first
save, so if you played 0.9.10, move the slider to 50% for the new look. The
Options picture in the user guide was retaken.

## For modders and mapmakers

No file format changed. Mission scripts that destroy or transfer a factory
now take its unfinished unit with it. A feature's `height` decides where a
hovering flyer's shots stop, as before; a tall indestructible feature between
a hover attacker and its target now makes the flyer move round it.

## For contributors

- Sim (protocol 304): `World::hoverForShot` and `chordClear` (the ring bearing
  with a clear chord to the target's SweetSpot; the blocked-shot counters are
  the ground mover's). `World::killProductionOutput` for a producer that
  `isStructure()`: the death sweep (type 11, the factory as `lastHitBy`) and,
  at once through `scenarioDestroy(output, 11)`, `scenarioDestroy` of the
  factory, `dequeue` of the unit in work (last copy), `setRepeat` off, `stop`,
  `giveUnit`, `scenarioTransfer` and `captureUnit`. Legion: `Member::course`
  (the route heading, which keeps the facing cap, leader and opposing tests),
  `faceMotion` in `legion.cpp`, `courseTurnCorrection` in
  `World::updateLegionStill`, `LegionNavigator::course`; the new member fields
  are folded into the Legion checksum. `docs/retail-engine.md` has the
  original's factory-abort and hover findings; `docs/legion-pathfinding.md`
  "Facing follows motion" the Legion design and the variants dropped.
- AI: `Controller::scanTargets` / `StrikeBoard`, `pickSite` (per kind of mover,
  reachability probed for the best 48 sites), `siteNeed`, `strikeOrder`,
  `sitePad`, `strikers_` and `pursuits_` (`src/ai/ai.cpp`, `ai.h`). The Retail
  AI (`classicwaves.h`) is untouched.
- Client: `MapView::minZoom` and the scroll clamp take the world view
  (`GameView::mapViewW` x `mapViewH`); `MapView::draw` takes the seen height.
  `Settings::hiddenFadeOpacity` defaults to 50.
- Net: `kNetVersion` 303 -> 304, no new command, replay format 15, generator
  9. `tools/replay_test.cpp` pins 304 and refuses 303.
- Tests: new `factory_death_test` (8 checks fail on the old code, 12 more on
  the factory-death commit before the cancel rule), the `blocked_shot_test`
  hover case (the reported geometry), facing cases in `legion_world_test` (a
  wall end and a standing block, sideways and backward steps per 1,000 with
  arrivals unchanged), `ai_behavior_test` strike cases (an unseen last
  lodestone, going on to the next site, Hard's split), `zoom_floor` harness cases (1920 x 1080 and 7680 x 2160, UI scale 100%
  and 200%, spectating), `settings_test` Fade Opacity 50.
- Baselines retaken: the Legion `hash@` keys in
  `tools/scenarios/baseline.json` (797 keys in 76 scenarios; no Retail hash
  moved) with six offset-spread exceptions for Legion facing, and the 24
  Legion lines of `tools/navigation_checkpoints.json` (Retail identical).
- `--mpai` Inner Circle seed 1, `TAK_SPEED=80`: Retail 60 s
  `d0da4ccc71d2067c`, Legion 60 s `c3f6a3a13f0c167f` and Retail AI 60 s
  `4952493f51b9a568` unchanged; **new** Retail 300 s `7a0be4440cd154b7`,
  Legion 300 s `51989ae7896320d8`, Crusades Retail 300 s `3e8c0ad3b4f1b859`,
  Crusades Legion 300 s `0e2d8ac5fdbae8ca` (the AI's target choice). The
  cross-compiler golden (`check-determinism`) is unchanged,
  `dcef618cd2e4d558`. See the [validation report](release-0.9.11-validation.md).

## Known limits

New with 0.9.11:

- **Legion units on fight, attack, guard or patrol orders still slide
  unturned**, as do slow creeps and side-steps in a jam, yielding units and
  units aiming; only travel-speed sliding faces its motion.
- **AI time to win** is slower in some even matchups (Inner Circle Absurd
  against Absurd: 25.6 to 33.8 and 11.1 to 25.8 minutes over two seeds), as
  armies go for the economy first instead of meeting at a start.
- **A cloaked or undamageable last unit** cannot be finished off by the AI, so
  such a game does not end by itself (the original's AI cannot either).
- **Stop destroys the unit in work** in a factory (beyond the original, whose
  Stop keeps production going). Use Stop only when you mean to cancel it.
- **Leaving or forfeiting players' factories** lose their unfinished units at
  once (their units are stopped).
- **The original's capture path is inferred** from its code (the ownership
  change makes a new object and kills the old one), not run in emulation; the
  factory death and cancel paths were.
- **The hover move** happens only after three shots in a row stop on scenery,
  and only for `hoverattack` flyers; a flyer on an attack run never re-aims.
- **0.9.10's limits still apply:** see the
  [0.9.10 notes](release-0.9.10-notes.md#known-limits), and through them the
  earlier notes.

## Compatibility

Protocol **304** (was 303), replay format **15** and generator version **9**,
all unchanged except the protocol. Checked in source against `v0.9.10`:
`kNetVersion` in `src/net/protocol.h` and `kReplayFormat` in
`src/net/replayhdr.h`.

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.11 clients and servers play only with each other**.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.9.11 plays only 0.9.11 replays; a 0.9.10 replay (format 15,
  protocol 303) is refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes, pack
  folders and settings files carry over unchanged (a saved Fade Opacity is
  kept).

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.9.11-validation.md).
