# TAK Engine 0.9.8

0.9.8 follows 0.9.7. Guards now defend what they guard, as the original
game's do; a builder guards (or repairs) a unit of yours on a right-click;
raising the dead respects the unit limit, and a computer player short of mana
stops retrying the same raising. Release builds no longer read the diagnostic
`TAK_*` environment variables, and the living documentation was checked
against the game.

**Compatibility first.** A guard's answer to an attack on what it guards and
the new raising rules join the game state the players compare, so 0.9.8 uses
network protocol **302** (replay format **15**, unchanged). **0.9.8 does not
play with 0.9.7 or any earlier version, and does not play their replays.**

- **Everyone updates together.** A 0.9.8 player cannot join a 0.9.7 server, and
  a 0.9.7 player cannot join a 0.9.8 server: the connection is refused with a
  *protocol version mismatch* message. Update your game and tell the people you
  play with to update theirs.
- **Old replays:** a replay recorded by 0.9.7 or earlier is refused with the
  version message. To keep watching them, keep your 0.9.7 install (0.9.0 to
  0.9.7 stay on the GitHub releases page); versions before 0.9.0 are no longer
  offered for download and can be built from their git tags (see the
  [development guide](development.md)).
- **Players:** nothing to do. Settings, your data folder choice, key bindings,
  saved `.kmp` maps, generated-map recipes and mod packs carry over.
- **Server operators:** update `takserver` to 0.9.8 when you announce the
  release; until you do, 0.9.8 players cannot use your server. A release
  `takserver` now reads only its four test timers from the environment (below);
  nothing else about running a server changed (see
  [public-server.md](public-server.md#versions)).

## Guards defend what they guard

In the original, a hit on a unit alarms everything listening to it, and a
guard (the original's `Follow_Ground`, or `VTOL_Follow` for a flyer) is one of
those listeners. 0.9.8 ports that answer, found by running the retail routines
(the engine copies none of the original's code or data):

- **The alarm.** Every hit that runs the hit response stamps the victim
  (`Unit::attackedTick`, the tick after the hit; not hashed). A guard whose
  guarded unit carries a newer stamp than the one it last answered
  (`Unit::guardHeard`) hears the alarm at once (the alarm wakes its look)
  and records it.
- **The answer.** The guard takes the guarded unit's last attacker
  (`lastHitBy`). If it is alive, not allied and not in the guard's
  `notargetcategory`, the guard attacks it with a plain attack order put ahead
  of the guard order: no leash, no return move, and it goes ahead on **Hold
  Fire** and **Hold Position** too, as the original's does. The guard order
  resumes once the attack ends. Failing that, a guard not on Hold Fire hands
  the attacker to its weapons' own targeting.
- **Ground and flying guards** both answer. A flying guard keeps a current
  target inside its weapon's minimum range when it retargets, as the
  original's flyer does.
- **Unarmed guards** (builders) do not attack: they keep helping and repairing
  as in 0.9.7.
- **What was wrong before.** A guard fought only what came within its own
  reach, so an archer or a tower outranging it could shoot the guarded unit
  unanswered.

## Builders guard on a right-click

Right-clicking a finished unit of yours now decides **per selected unit**, as
the original's default-order resolver does with the right-click interface
(`friendlyUnitOrder`, `src/client/friendlyorder.h`):

1. A mobile builder repeating production does nothing (the game refuses its
   Guard, and it did not repair before either).
2. A mobile builder repairs the unit when it is damaged.
3. Otherwise any unit whose type can guard (`canguard`) guards it, armed or
   not. The original's test does not look at weapons, so a builder guards a
   healthy unit just as a soldier does.
4. Otherwise that unit does nothing.

So a builder now guards an undamaged unit on a right-click (before, it did
nothing; the G key and the command panel were the only way), and a builder's
repair no longer stops the soldiers selected with it from guarding. An
unfinished unit is still a help-build site, an enemy is still attacked and a
transport still boarded. A god, which the original cannot tell to guard, no
longer guards on a right-click. This is a client change only.

## Raising the dead

- **Unit limit.** A resurrect or animate that would take a side past the
  game's unit limit, or a unit type past its own `totalallowed` (a second
  dragon, say), now ends without raising anything and leaves the corpse, as
  the original's does. Before, it raised the unit anyway. An idle priest
  already at either limit does not start one.
- **Computer players short of mana.** A computer caster gives up a raising at
  its end when its side is well short of mana; it then took the same corpse up
  again at once (every tick) and gave it up again, for as long as the shortage
  lasted. An idle computer caster now takes up no raising while its side's
  allocation is under 0.7, so it waits until the economy recovers. The idle
  caster's search is the engine's own; the original has no such rule to copy.

## Release builds and the environment

The client's debug-only environment hooks (`devEnv`, `devFlag`) moved to
`src/util/devenv.h`, so the simulation, AI, network code, server and
Cartographer use them too. Under `NDEBUG` they return nothing, so a release
build reads none of these: `TAK_AI_PICK`, `TAK_AI_STATION`,
`TAK_NET_JITTER_MS`, `TAK_NET_BASE_MS`, `TAK_NET_LOSS_PCT`, `TAK_AIPHASE`
(`_MS`), `TAK_SRV_DELAY`, `TAK_SRV_PACELOG`, `TAK_PATHDBG`, `TAK_PHASE`
(`_MS`), `TAK_PATHLOG`, `TAK_BURNLOG`, `TAK_SPLASHLOG`, `TAK_FOG_MARGIN`,
`TAK_FOG_EYE` and `TAK_SITUATION_CMDS` / `_SHAPES`. Their release defaults
are unchanged (fog margin 16, eye 40). A release `takserver` still honours
`TAK_TIMEOUT_MS`, `TAK_LOAD_TIMEOUT_MS`, `TAK_STALL_MS` and
`TAK_DROP_WAIT_MS`. `TAK_CARTOGRAPHER_PROFILE` stays in debug editors and in
the editor's test executables of every build type.

## Documentation

About 35 living documents were checked against the game and brought up to
date: the README, user guide, quick start and Cartographer guide; the
development, server, multiplayer design, map transfer, streaming and
single-player equivalence documents; and the pathfinding, retail-engine, AI,
campaign, height-and-fog, map generation and research notes. Among the fixes:
the user guide said a guarding builder repairs an ally's unit it guards, but
an ally's unit cannot be guarded (the game refuses the order); the development
guide's list of release client options was out of date. Release notes and
validation reports of earlier versions are history and were not changed.

## For modders and mapmakers

No file format changed. A unit type's `canguard` now also decides whether it
guards on a right-click, and `totalallowed` now also limits raising the dead.

## For contributors

- Sim (protocol 302): `Unit::attackedTick` (not hashed) and `Unit::guardHeard`
  (hashed with a tag when set). New `World::guardAlarm`, `guardDefend`,
  `guardStandAside` and `guardSlotRetarget`; `retailIssueAutoAttack` gains a
  `forced` form (the original's explicit attack: neither the Hold Position nor
  the Hold Fire gate, no leash or return move, not an auto target). The
  raising end refuses past the unit limit and `totalallowed`; the idle
  caster's search skips what the end would refuse.
- Net: `kNetVersion` 301 -> 302, no new command, replay format 15.
  `tools/replay_test.cpp` pins 302.
- Client: `friendlyUnitOrder` (`src/client/friendlyorder.h`) holds the
  right-click decision for a finished friendly unit.
- Environment: `src/util/devenv.h` (`devEnv`, `devFlag`); use it instead of
  `std::getenv` for any `TAK_*` knob. `src/client/dev.h` includes it.
- Tests: `helpbuild_test` gains `guardDefends` (and its hash check);
  `production_test` gains `abandonedReviveWaits` and `reviveRespectsUnitLimit`;
  new `friendly_order_test` (synthetic cases always, every shipped mobile
  builder when `TAK_TEST_DATA` is set; registered as `friendly_order` and
  `friendly_order_data`). All of them fail on 0.9.7's code.
- `--mpai` Inner Circle seed 1, `TAK_SPEED=80`: **the gate hashes did not
  change** (the AIs never guard, so no guard ever answers an alarm, and no
  gate game raises past a limit or retries a raising): Retail 60 s
  `d0da4ccc71d2067c`, 300 s `e820d37865085bd9`; Legion 60 s
  `c3f6a3a13f0c167f`, 300 s `0783cb05f7f128cf`; Retail AI 60 s
  `4952493f51b9a568`; Crusades 300 s Retail `f2f51dc1703fc209`, Legion
  `c4189ae189dd68dc`. The cross-compiler golden (`check-determinism`) is
  unchanged, `dcef618cd2e4d558`. See the
  [validation report](release-0.9.8-validation.md).

## Known limits

New with 0.9.8:

- **After it answers an attack, a ground guard looks again on the next tick,**
  without the original's random 0 to 29 tick draw, so its next look can fall
  on a different tick from the original's.
- **One alarm per guarded unit per tick.** Several hits on the guarded unit in
  one tick make one alarm; the guard answers the attacker recorded last
  (`lastHitBy`).
- **A queued guard hears only newer alarms.** A guard order queued behind
  other orders does not listen while it waits; once it is current, it hears
  only an alarm newer than the last one that unit answered.
- **Raising and AI profiles.** The raising end checks the game's unit limit and
  the type's `totalallowed`, not an AI profile's per-type limit.
- **No "no more units" message.** A raising refused at the limit ends quietly;
  the original's NOMOREUNITS message is not shown.
- **The don't-start rules are ours.** The idle caster's refusal to start a
  raising the end would refuse (at a limit, or a computer caster short of
  mana) is the engine's own search, not a port.
- **Guard defence in multiplayer.** No multiplayer or remote run had a guard
  answering an alarm, and the AI never guards; the answer is covered by
  `helpbuild_test` and the local checks only.
- **Right-click differences from the original:**
  - A selected unit that can neither repair nor guard does nothing on a
    right-click on a friendly unit; the original moves it to the point.
  - A non-builder right-clicking an unfinished unit does nothing; the original
    has it guard.
  - A right-click Guard on an ally's unit is refused by the game
    (`World::guard`), as the G key's is.
  - There is no guard cursor, as in the original.
- **0.9.7's limits still apply:** see the
  [0.9.7 notes](release-0.9.7-notes.md#known-limits) (flying guard parts not
  ported, guard re-check timing, the unreachable-repair skip), except that a
  flying guard now answers an attack on what it guards (its landed step-out
  and own attack scan are still not ported) and right-click Guard is no longer
  limited to armed units; and, through it, the 0.9.6 and 0.9.5 notes.

## Compatibility

Protocol **302** (was 301), replay format **15** and generator version **9**,
all unchanged except the protocol. Checked in source against `v0.9.7`:
`kNetVersion` in `src/net/protocol.h` and `kReplayFormat` in
`src/net/replayhdr.h`.

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.8 clients and servers play only with each other**.
- **Replays.** Playback requires the recording's protocol to equal the engine's,
  so 0.9.8 plays only 0.9.8 replays; a 0.9.7 replay (format 15, protocol 301) is
  refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes, pack
  folders and settings files carry over unchanged.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.9.8-validation.md).
