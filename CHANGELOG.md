# Changelog

Every published TAK Engine release, newest first.

The current version is always at
[the latest release](https://github.com/pocketgeek/tak-engine/releases/latest).
Releases from 0.9.0 on stay on the GitHub releases page; versions before 0.9.0
are kept only as git tags (each entry links its source): to run one, build it
from source with the [development guide](docs/development.md) of that tag. No release includes retail game assets: every version needs your own
*Total Annihilation: Kingdoms* installation.

**Protocol** decides who can play together. A server and all of its players
must use the same protocol, and a replay plays only in a version with the
protocol that recorded it. Each entry gives its protocol and replay format; an
entry says "plays with" only where that release was checked against its
neighbours. **Upgrading** lines appear only where you have to do something
yourself.

Releases 0.7.11 and later link full release notes, and 0.7.6 and later link
their validation reports; both live in [`docs/`](docs/).

## 0.9.12
*2026-10-29 · protocol 305 · replay format 15 · does not play with 0.9.11 or earlier, nor their replays*

- **Fog of war looks and works as in the original game**: what your units see
  is the same sight the game plays by (further from high ground, less uphill,
  on 32-pixel squares), ground seen before lies under your side's rolling,
  drifting mist in its own colour, unseen ground is black, and radar marks
  enemies on the minimap only, never in the main view. Another player's unit
  shows, and can be clicked, only when a corner of it is in sight. The fog also
  costs less: at 3,000 units its overlay and minimap draw in 0.05 ms a frame
  instead of 0.17, and the separate fog pass and its thread are gone.
- **Our AI levels attack only what they have seen** (not in the original game,
  whose AI weighs unseen units too; the Retail AI is unchanged): their own and
  their allies' sight and radar, units shooting at them, and buildings
  remembered where they were seen; with nothing known they scout and explore
  the map instead. They follow the room's fog of war and start locations as a
  player does (Explored: the map's ground and deposits are known, enemies are
  not; Random hides whose start is whose), pick lodestone deposits only where
  they know the ground, judging one taken by what they have seen there, steer
  scouts away from where scouts died, and send their ships out when the army's
  target is inland. A beaten player's base, an enemy's or an ally's, is looked
  in on again once out of sight five minutes, in case someone moves in (and
  attacked if someone has). A game between AIs may now not end when a last
  unit stays hidden, and Not Explored games run longer (the AI no longer knows
  every deposit).
- **Weapons no longer aim at buildings past their range**, as in the
  original game: every weapon but melee measures a building's distance centre
  to centre, with no allowance for its size. A Death Totem no longer empties
  its lightning into the air at a lodestone just out of range, a Crossbowman,
  Shaman, mage or dragon ordered onto a building walks into range and hits it
  instead of stopping short, and bombers, earthquakes, storms, hail, freeze,
  stone and mind-control spells are no longer cast at a building from past
  their range.
- **Kirenna no longer always stands without firing after walking up to a
  target.** Her aim waits for her walk to settle, and the engine aimed the
  instant she stopped. Now, as in the original game, an attack aims a moment
  after the attacker stops, and a unit's own scripts hear it turn. Like the
  original, she still stalls when she stops mid-stride (until she moves
  again).
- **Flyers dogfight flyers**, as in the original game: a flyer attacking a
  flyer no longer hangs still or hovers beside it but chases it, swings out
  when threatened and now and then breaks off and turns before coming back.
  The Aerial Juggernaut now closes in so its anti-air arrows reach (it stopped
  at its cannons' range, and cannonballs miss flyers), and its cannons pick
  their own targets meanwhile, bombing the ground. Attacking a ground unit,
  the Juggernaut's and the Dirigible's other weapons likewise pick their own
  targets, as in the original. An Iron Beak drops an
  egg only once it faces its target, as in the original; the egg hits a flyer
  below the Beak and falls past one at its own height.
- **A shot stopped in front of a castle counts as blocked.** A castle, keep,
  gate or other long building is hit through the ground it stands on, as in
  the original game, not anywhere within a circle of its length, so shots that
  land on a ridge or scenery in front of its long face count as blocked. An
  archer told to manoeuvre now walks to a clear line and hits it, where it
  used to fire into the crest for ever.
- **The Harpy converts only with its mind-control spell**, as in the original
  game: it no longer takes any enemy it stays beside for three seconds, or at
  once below a quarter of its health, for free. Each charm costs 300 mana and
  can fail, and the Harpy now casts on its own like the Mind Mage. **A charmed
  unit keeps its damage** (it used to come over at half health or more) and,
  as in the original, starts without its veteran stars. **A unit still being
  built cannot be charmed.**
- **Big late games keep their speed.** With 8 Absurd AIs on a 64x64 random map
  at +10, 0.9.11 fell to -2 at about 2,700 units, with 219 ticks over 250 ms;
  0.9.12 held +9 to +10 to 3,160 units with no tick over 100 ms:
  - **The AI's lodestone search** no longer tests every deposit on a large map
    for each site (300-840 ms in one tick for an Absurd AI, which dropped the
    speed below +0); it takes the nearest usable one (the same choice).
  - **A building that might cut off part of the map** is checked around itself
    instead of re-mapping the whole map (~90 ms each, up to 24 times a minute
    on a 64x64 map).
  - **A lodestone's mana bonus and Legion's narrow-passage lookups** no longer
    scan the whole map each tick.
  - **The lockstep state check** costs about 5 ms instead of 33 with 2,500
    units on a 64x64 map, and a game with no human player (AIs you watch)
    skips it on the server.
- **Streaming keeps to YouTube's bitrate**, above which YouTube could connect
  but never start the broadcast: the panel caps it there (a saved higher value
  is lowered, with a note), adds **Recommended**, and warns when the upload
  can't keep up.
- **SETTINGS, BENCHMARK and LOAD REPLAY have a BACK button** beside "Esc - Back".
- **Options, Voice Chat and Controls save every change as you make it**; the
  SAVE button is gone. A slider saves when let go, and **DEFAULTS** asks first
  (and puts the sound device back to the system default at once). The
  Controls hint has a band of its own, so it no longer covers the last row (a
  right-click there used to unbind that key).
- **Protocol 305:** the AI's sight-only targets, the combat fixes and the new
  state check change what players compare, so this release does not play with
  0.9.11 or earlier, nor their replays. A 0.9.11 client or server is turned away with a
  *protocol version mismatch* message, and a 0.9.11 replay is refused with the
  version message.

**Upgrading (server operators):** update `takserver` together with your
players.

[Release notes](docs/release-0.9.12-notes.md) · [Validation report](docs/release-0.9.12-validation.md) · [Source at v0.9.12](https://github.com/pocketgeek/tak-engine/tree/v0.9.12)

## 0.9.11
*2026-10-29 · protocol 304 · replay format 15 · does not play with 0.9.10 or earlier, nor their replays*

- **Hovering flyers no longer fire into scenery for ever** (not in the
  original game). A Thirsha or other hover attacker whose shots keep
  stopping on a standing stone or other indestructible scenery between it
  and its target now moves round the target to a clear line and hits it.
- **Full zoom-out shows the whole map width.** The zoom-out limit now leaves
  out the right-hand panel and the bottom bar, so the map's right edge no
  longer stays hidden under the panel, and the bottom edge scrolls into view
  above the bar.
- **Fade Opacity** (Hidden Units) now defaults to 50% (was 65%). A value in
  your settings file is kept, and 0.9.10 saved 65% there, so set the slider
  to 50% yourself for the new look.
- **A factory's unfinished unit dies with it**, as in the original game: when
  a factory is destroyed or changes hands, the unit it is building dies at
  once (no wreck, no kill for anyone, the owner's loss, no mana back);
  before, it stood unprotected and slowly faded away. Cancelling the unit in
  work does the same: from the build menu when it is the last queued unit of
  its type, by turning repeat off, or by **Stop** (beyond the original, whose
  Stop keeps production running), and a leaving or forfeiting player's
  factories lose theirs too. Cancelling a unit still waiting in the queue
  only removes it. A Zhon builder's unfinished unit and a builder's
  unfinished building still stay behind (the original keeps those).
- **Legion pathfinding:** units sliding round other units and walls face
  the way they actually move instead of their route's direction (units on
  fight, attack, guard and patrol orders keep facing their way to fight);
  sideways and backward walking falls by about two thirds.
- **AI attacks go for everything** (beyond the original game; Retail unchanged):
  our AI levels choose attack and raid targets from every enemy unit and
  building by value, threat and defences (unseen ones count for less, as the
  original AI's do), and move on from one to the next, so a winning AI
  finishes off a player's last lodestone or unit and the game ends.
- **Protocol 304:** the hover-for-shot move, the factory's unfinished unit
  dying, Legion facing and the AI's target choice change the game state the
  players compare, so this release does not play with 0.9.10 or earlier, nor
  their replays. A 0.9.10 client or server is turned away with a *protocol
  version mismatch* message, and a 0.9.10 replay is refused with the version
  message.

**Upgrading (server operators):** update `takserver` together with your
players.

[Release notes](docs/release-0.9.11-notes.md) · [Validation report](docs/release-0.9.11-validation.md) · [Source at v0.9.11](https://github.com/pocketgeek/tak-engine/tree/v0.9.11)

## 0.9.10
*2026-10-28 · protocol 303 · replay format 15 · plays with 0.9.9, and its replays*

- **Hidden Units** (Options → Graphics; not in the original game): units
  behind trees, rocks and buildings stay visible. **Fade**, the default,
  fades the whole tree, rock or building out while a unit is behind it (to
  65% by default; **Fade Opacity** slider, 0-100%); **Dots** puts the
  unit's tactical dot on top; **Off** hides it as before. Only ground units
  you can see anyway are marked.
- **Flat scenery is drawn under units**, as in the original game: features
  whose height is below 10 (stumps, crops, smudges, mana deposits) no longer
  paint over the feet of a unit standing just north of them.
- **macOS: Auto renderer now picks OpenGL**, as Windows and Linux do, so the
  game's OpenGL-only fast drawing paths run on a Mac too (2.6 times as fast as
  Metal at 2,000 units on a MacBook Neo). Apple's software OpenGL is passed
  over; Metal follows if OpenGL cannot start. A saved **Metal** choice is
  kept. If OpenGL misbehaves, choose **Metal** under Options → Graphics →
  Renderer.
- **Metal: no stray drawing from off-screen clips.** A part of the screen
  clipped away entirely (a unit behind a wall at the view's edge, the chat
  box) no longer draws over the view on the Metal renderer, and the Options
  and Hotkeys lists in a window too small for them no longer draw outside
  their frame (that one on every renderer).
- **Protocol 303, unchanged:** every change in 0.9.10 is in the game's
  display and options, and nothing the players compare changed (checked in
  the source against 0.9.9), so 0.9.10 and 0.9.9 clients and servers play
  together in either direction, and each plays the other's replays. Servers
  need no update.

[Release notes](docs/release-0.9.10-notes.md) · [Validation report](docs/release-0.9.10-validation.md) · [Source at v0.9.10](https://github.com/pocketgeek/tak-engine/tree/v0.9.10)

## 0.9.9
*2026-10-28 · protocol 303 · replay format 15 · does not play with 0.9.8 or earlier, nor their replays*

- **The AI upgrades to divine lodestones only when it pays.** Defensive,
  Normal, Hard, Absurd and Absurdly Defensive now put a divine lodestone only
  on one of their own lodestones, once no free deposit near home is left, and
  only where the upgrade pays for itself within 15 minutes (20 for Absurdly
  Defensive). They save up for it first, do one at a time, and train a priest
  for it when they have none. Hard and Absurd no longer start divine lodestones
  on free deposits far from home with an empty store, and a priest no longer
  starts a dragon it cannot pay for. From 15 minutes on Defensive (at most 5),
  10 on Normal (2), 8 on Hard (6), 6 on Absurd and 8 on Absurdly Defensive (no
  limit); Easy never upgrades.
- **Make Poster is Shift+P.** The default key for MAKE POSTER (power codes) is
  now **Shift+P** instead of Print Screen, which many desktops take for their
  own screenshots and Mac keyboards lack. A key you set yourself in CONTROLS is
  kept.
- **Back from a Settings screen returns to Settings.** Options, Controls,
  Voice Chat, Benchmark and YouTube Streaming opened from the title screen's
  SETTINGS menu now go back to it on BACK or Esc, as Load Replay already did,
  instead of to the title screen.
- **Max FPS can be unlimited.** The Options slider runs from 30 to 240 frames
  a second, and its far right end is **Unlimited**. A saved cap above 240 (the
  settings file used to allow up to 480) now reads as Unlimited.
- **Builders only place what they can reach.** When a walking builder can
  never get close enough to a spot to build there, such as a shipyard out in
  open sea or on a lake it has no way to walk to, the placement ghost turns
  red and the click is refused. Before, the order was accepted and the builder
  set off, then gave up. This goes beyond the original game, which accepted
  such sites. Flying builders still place anywhere they could build.
- **Your units get out of the way of a build.** When a builder reaches its
  site and some of your own idle units, or units guarding something (the
  builder itself, say), stand on it, they now step just off the site, and the
  builder starts as soon as it is clear. A guard keeps its Guard order and goes
  back to guarding. You can also place a building on top of such units now;
  before, the site was refused. Units of other players, and your units that are
  moving, fighting or working, still make the builder wait and give up after
  about ten seconds, as in the original game, where a builder only ever waits.
- **A builder guarding a factory no longer blocks its door.** A unit on
  **Guard** used to walk up to the middle of what it guarded and stop about 70
  pixels from it, which for a factory is its yard: the guard stood on the spot
  where the next unit is made, and the factory could not start another one.
  As in the original game, a ground unit now guards from a ring round the unit
  or building it guards: measured from its centre, the guarded unit's full
  longer side plus the guard's own width plus two cells, and three cells deep
  (a Barracks guard waits about 256 to 304 pixels from the Barracks' centre),
  so a guarding builder walks in to help each unit being made and back out
  between them; a ship guarding a shipyard does the same. Guards of soldiers
  keep a little further off than before for the same reason. Protocol 303.
- **The Benchmark always runs the plain game, with no mods.** It used to load
  your ticked cosmetic and game mods and the packs in `always_enabled`, so a
  mod could change the units, the AI or the art being measured, and two
  machines with different mods gave numbers that could not be compared. Now
  the Benchmark leaves every mod pack out, and its private server plays the
  original game data. Your mod choices are unchanged and apply again in your
  next game.
- **The Retail AI no longer upgrades its own Lodestones.** As in the original
  game, the Retail AI level builds a Divine Lodestone only on a free sacred
  site: one where its own Lodestone already stands is passed over for the next
  free site in reach, and with none it builds nothing. Before, it could order a
  Divine Lodestone on top of its own finished Lodestone. You can still upgrade
  a Lodestone in place yourself, and the other AI levels are unchanged.
- **A build icon's tooltip is readable while something is training.** The
  TRAINING line above the build menu and the tooltip of the icon under the
  mouse were drawn in the same place and overlapped; the tooltip now takes the
  line's place while it is shown.
- **Protocol 303:** units stepping off a build site and the guard ring are
  part of the game state the players compare, so 0.9.9 plays only with 0.9.9
  and plays only replays recorded by 0.9.9. A 0.9.8 client or server is turned
  away with a *protocol version mismatch* message, and a 0.9.8 replay is
  refused with the version message.

**Upgrading (server operators):** update `takserver` together with your
players.

[Release notes](docs/release-0.9.9-notes.md) · [Validation report](docs/release-0.9.9-validation.md) · [Source at v0.9.9](https://github.com/pocketgeek/tak-engine/tree/v0.9.9)

## 0.9.8
*2026-10-27 · protocol 302 · replay format 15 · does not play with 0.9.7 or earlier, nor their replays*

- **Guards defend what they guard,** as in the original game. When something
  attacks the unit a guard is guarding, the guard goes after the attacker,
  however far away it is shooting from, and comes back to guarding once it is
  dead. Before, a guard only fought what came within its own reach, so an
  archer or a tower outranging it could shoot the guarded unit unanswered.
  Flying guards do the same. As in the original, the guard answers even on
  Hold Fire or Hold Position, and an unarmed guard (a builder) keeps helping
  and repairing instead.
- **A computer player short of mana no longer tries the same raising over and
  over.** A computer priest gives up a resurrect or animate at its end when
  its side is well short of mana; it then took the same corpse up again at
  once and gave it up again, for as long as the shortage lasted. It now waits
  until the economy recovers before it raises anything.
- **Raising the dead respects the unit limit.** A resurrect or animate that
  would take a side past the game's unit limit (or a unit past its own limit,
  such as a second dragon) now ends without raising anything and leaves the
  corpse, as in the original. Before, it raised the unit anyway. A priest
  already at the limit does not start one.
- **Builders guard on a right-click,** as in the original game. Right-clicking
  one of your finished, undamaged units with a builder selected now tells the
  builder to Guard it, as the G key does; before, a right-click guarded only
  with armed units and a builder did nothing. Each selected unit decides for
  itself: a builder repairs a damaged unit while the soldiers selected with it
  guard it (before, they did nothing), and any unit that can guard does so,
  armed or not. An unfinished unit is still helped to build, an enemy still
  attacked and a transport still boarded. A god, which the original cannot
  tell to guard, no longer guards on a right-click either.
- **Release builds read no diagnostic environment variables.** The remaining
  `TAK_*` logging and tuning hooks in the server, simulation, AI, network code
  and Cartographer now work only in debug builds, as the client's already did.
  A release `takserver` still honours its four test timers (`TAK_TIMEOUT_MS`,
  `TAK_LOAD_TIMEOUT_MS`, `TAK_STALL_MS`, `TAK_DROP_WAIT_MS`).
- **Documentation checked against the game.** The README, user guide, quick
  start, Cartographer guide and the developer, server, multiplayer, pathfinding,
  AI, campaign and research documents were checked against 0.9.7 and brought
  up to date. Among the fixes: the user guide no longer says a guarding
  builder repairs an ally's unit it guards (you cannot guard an ally's unit;
  the game refuses it), and the development guide lists the release client's
  options correctly.
- **Protocol 302:** a guard's answer to an attack on what it guards is part of
  the game state the players compare, and so are the new raising rules, so
  0.9.8 plays only with 0.9.8 and plays only replays recorded by 0.9.8. A 0.9.7
  client or server is turned away with a *protocol version mismatch* message,
  and a 0.9.7 replay is refused with the version message.

**Upgrading (server operators):** update `takserver` together with your
players.

[Release notes](docs/release-0.9.8-notes.md) · [Validation report](docs/release-0.9.8-validation.md) · [Source at v0.9.8](https://github.com/pocketgeek/tak-engine/tree/v0.9.8)

## 0.9.7
*2026-10-27 · protocol 301 · replay format 15 · does not play with 0.9.6 or earlier, nor their replays*

- **Helping to build now works the way the original game does.** A mobile
  builder can help finish any unfinished unit of its own side (not an ally's),
  including a unit a factory or a Zhon builder is still producing. The helpers
  and the producer work on one shared build: the unit finishes sooner, the mana
  is spent faster, and the total cost is still its build cost. Before, a helper
  on a produced unit finished it early while its producer kept its slot and was
  charged the full cost again (a Zhon builder paid 108 mana too much on a unit
  costing 311) and kept healing it for free. Now the producer is freed the
  moment the unit is finished, the unit's finishing orders (a Zhon builder's
  exit step, or a factory's PARK or rally) are given once, and orders you gave
  the unit while it was unfinished are kept. A factory with no helper finishes
  on exactly the tick it did before.
- **Guarding builders lend a hand.** A mobile builder told to Guard a unit
  repairs it when it is damaged (allies too), helps build it when it is
  unfinished, and otherwise helps with what that unit is building, producing,
  helping with or repairing. The help goes ahead of the guard order, which
  resumes afterwards; a guarding builder repairing a unit that walks off follows
  it.
- **Flying builders guard the same way,** as the original's flyers do: a
  guarding Flying Builder, Dark Priest, Darkest Priest or Thirsha repairs and
  helps build, and also helps flying producers (a Thirsha's output), which a
  ground guard does not. Flying builders now fly in to twice their build
  distance before they start helping or repairing, then circle the job at about
  their build distance; before, a flyer helping a site someone else had begun worked on it
  from any distance, and a flying repairer hovered on top of its target. A
  flying builder does not repair a unit under the sea.
- **Flyers on guard circle what they guard,** as the original's do. Any flyer
  told to Guard (armed, unarmed or a builder with nothing to help) now circles
  the guarded unit about 320 pixels out, or its weapon's range plus 160 when it
  is armed, and follows it when it moves; before, it hovered close by. A
  flyer given the guard while off the map flies back onto it first and keeps
  the guard.
- **Who may help is checked by the game, not just the screen.** A builder helps
  only a unit of its own side, only if it can move (buildings never help), and,
  when it may build only what is on its own menu, only a type on that menu. A
  modified client cannot get around this.
- **Builders on a repeat queue can help.** A Zhon builder repeating a unit can
  be told to help with an unfinished unit; its own product waits until it has
  finished helping. Move, Patrol and Attack orders still set its rally.
- **For testers:** a debug build's `TAK_AUTOPLAY` now also builds, trains, puts
  conjurers on a repeat queue, guards busy units and assists unfinished ones,
  and sends idle flying builders to guard damaged or busy units, so the desync
  sweeps cover these orders. `TAK_START_FLYERS=N` (set on every peer) starts
  each player with N flying builders, every other one damaged, and
  `TAK_AUTOPLAY_SIDE` picks the autoplay seat's side. Release builds are
  unchanged.
- **Protocol 301:** the shared build progress, the guard's next look, a flying
  guard's circling and the help walk are part of the game state the players
  compare, so 0.9.7 plays only with 0.9.7 and plays only replays recorded by
  0.9.7. A 0.9.6 client or server
  is turned away with a *protocol version mismatch* message, and a 0.9.6 replay
  is refused with the version message.

**Upgrading (server operators):** update `takserver` together with your
players.

[Release notes](docs/release-0.9.7-notes.md) · [Validation report](docs/release-0.9.7-validation.md) · [Source at v0.9.7](https://github.com/pocketgeek/tak-engine/tree/v0.9.7)

## 0.9.6
*2026-10-27 · protocol 300 · replay format 15 · does not play with 0.9.5 or earlier, nor their replays*

- **Slow loaders are no longer dropped.** The server now allows 30 s of
  silence, up from 15 s, while a player's game is loading, so a slow PC or a
  huge map no longer drops the seat before it finishes. Play and the lobby keep
  15 s. The server's own start-up work (building its referee world) no longer
  counts as player silence either.
- **Double-click selects every unit of that type.** Double-click one of your
  units (or buildings) to select all units of the same type that are finished
  and visible on screen. Hold Shift while double-clicking to add them to your
  selection, or Ctrl to remove them. Double-clicking an enemy or allied unit
  selects just that one. The original game has no double-click select; this
  follows the player request.
- **Rally orders for production.** Production buildings, and builders producing
  on an endless queue (such as the Zhon mobile builders), now accept Move,
  Patrol, Attack and Move-fight as rally orders, and Shift queues them in the
  same way as a unit's own orders. Every unit produced then runs the whole list
  in order; a Shift-queued patrol loops. Stop clears the rally. The original
  game gives produced units only Move and Patrol rallies, and only from
  buildings; Attack and Move-fight rallies, and rallies from mobile builders,
  are additions.
- **The mana bulb shows mana requested.** The red number on the right of the
  mana bulb is now what all your builders and producers are asking for each
  second, as in the original game, instead of only what could be paid. When
  your mana runs short it can be larger than the green income number on the
  left; both are averaged over the last second.
- **Crusades and scenario maps.** A scenario map that gives stats to a custom
  unit type the Crusades balance does not have now simply skips that type, as
  the original does, instead of refusing to start. A scenario that places a unit
  Crusades lacks is still refused, and the message now names the unit and tells
  you to turn CRUSADES off. Test Map accepts Use Only entries from the other
  balance. Cartographer's Check Map warns about units a Crusades game will not
  have.
- **Construction hit points.** An unfinished building's hit points can no longer
  grow past its maximum, and a hit on an unfinished retail-style site now
  rewrites its build progress as the original does.
- **Native Wayland on Linux.** Options, Display has a new NATIVE WAYLAND setting
  (Auto, On, Off; restart required). On a Wayland desktop (GNOME, KDE Plasma,
  Sway, Hyprland and others) the game and Cartographer now open native Wayland
  windows, sharp and at the right size at 150% or 200% scaling; GNOME without
  the libdecor library keeps X11. Switching keeps UI Scale and Cursor Size the
  same size on screen: the game measures whether its old X11 window had real
  pixels or was enlarged by the desktop and converts them only in the second
  case, so on KDE Plasma's default they stay as set. Cartographer follows the
  same setting, converts its UI Scale and window size the same way and
  re-centres its window after the conversion. Set NATIVE WAYLAND to Off to
  return to the X11 window; the sizes are converted back.
- **Command-line tools.** `cartographer --version` and `--help` work, the dev
  tools print their usage when given a missing or bad argument, and `simperf`
  works with the current random-map recipe.
- **Test harness reliability.** Headless runs keep their server seat while the
  simulation is slow, and report the exact tick the result landed. A mod
  transfer test no longer drops a player while loading, and the server log now
  says why a client was dropped.
- **Cloaking spends the unit's own mana.** As in the original game, a cloaked
  unit pays for cloaking from its own mana (its blue bar), not from your mana
  pool, so cloaking no longer shows in the mana bulb. When the unit's mana runs
  out it becomes visible for three seconds and then cloaks again. The Trapdoor
  Spider, which has no separate moving cost, now pays the original game's much
  smaller cost while it moves.
- **Fighting and working reveal a cloaked unit, as in the original game.**
  Each shot keeps the unit visible for 20 seconds. Building, reclaiming or
  raising the dead keeps it visible while it works and for 10 seconds after,
  and repairing for 5 seconds after. Enemies close by no longer make a cloaked
  unit visible (the original game has no such rule), and a cloaked unit pays
  its moving cost for any movement, even turning on the spot.
- **Raising the dead costs no mana.** As in the original game, resurrecting
  and animating are free: they no longer drain your mana pool or show in the
  mana bulb. A short mana supply still slows them, as it slows construction.
  Resurrection now takes three tenths of the unit's build time, and animation
  the creature's whole build time (until now they were the other way round).
  A computer player whose mana is well short abandons a raising at its end, as
  in the original.
- **Computer players judge a mana shortage as the original game does.** The
  Retail AI, and the Passive, Easy, Normal and Absurdly Defensive levels, now
  compare their income with the mana their builders and producers ask for,
  not with what a short pool could pay them, when deciding whether they need
  another lodestone.
- **Protocol 300:** rally plans, the cloak block and the raising-the-dead
  channel are now part of the game state the players compare, so 0.9.6 plays
  only with 0.9.6 and plays only replays recorded by 0.9.6. A 0.9.5 client or
  server is turned away with a *protocol version mismatch* message, and a
  0.9.5 replay is refused with the version message.

**Upgrading (Linux players on a Wayland desktop):** the first start of 0.9.6
opens a native Wayland window instead of an X11 one, on KDE Plasma as well as
GNOME, Sway, Hyprland and the others; GNOME without the `libdecor` library
keeps X11. UI Scale and Cursor Size keep the same size on screen. If the
native window misbehaves on your desktop, set Options, Display, NATIVE WAYLAND
to Off and restart.

**Upgrading (server operators):** update `takserver` together with your
players. A server without `--map-cache-dir` keeps its maps in
`TAKengine/takserver` in its per-user folder (since 0.9.5). A 0.9.6 server
allows a loading seat 30 s of silence and logs why it dropped a client.

[Release notes](docs/release-0.9.6-notes.md) · [Validation report](docs/release-0.9.6-validation.md) · [Source at v0.9.6](https://github.com/pocketgeek/tak-engine/tree/v0.9.6)

## 0.9.5
*2026-10-26 · protocol 299 · replay format 15 · does not play with 0.9.4 or earlier, nor their replays*

- **Dead units stay dead.** A unit that was killed could take more hits in the
  same instant, for example from the rest of a big splash or a volley landing
  together. Each extra hit was still subtracted from its health, and with
  enough of them the health counter ran past its lowest value and wrapped round
  to nearly full: the "dead" unit stood back up with around 30,000 hit points.
  As in the original game, a unit that has just been killed now ignores every
  further hit: the player whose hit killed it keeps the kill, and the way it
  dies, its overkill and its last reaction come from that killing blow alone.
  A single hit stronger than any unit's health (over 32,767 damage, possible
  with armour and veteran bonuses) also no longer wraps round. Health is capped
  at 32,767, the most the original game can hold; no shipped unit has more
  than 29,999. Unfinished buildings under heavy fire, and units standing in
  deadly water, are protected the same way.
- **The game no longer writes into your game data folder.** Until now, every
  map a game room accepted, every map downloaded from another player, every
  downloaded game mod, every random map you played and every Cartographer Test
  Map copy was saved inside the folder holding your *Total Annihilation:
  Kingdoms* install (in its `MapCache`, `ModCache` and `Maps` folders). They now
  go in your own per-user folder, beside `settings.ini`:
  `%APPDATA%\TAKengine\TAKingdoms\` on Windows,
  `~/Library/Application Support/TAKengine/TAKingdoms/` on macOS and
  `~/.local/share/TAKengine/TAKingdoms/` on Linux. The game folder can now be
  read-only, for example a shared or system-wide install. A `MapCache` or
  `ModCache` folder an earlier version left in the game folder is ignored and
  left alone (you can delete it); a map you had downloaded is downloaded again
  the first time a room uses it. Random maps an earlier version saved in the
  game folder's `Maps` still show up as ordinary maps, and Cartographer still
  lists your downloaded maps and Test Map copies. Screenshots and the maps and
  mods you save from Cartographer still go where they did.
- **A game whose map cannot be built says so:** creating a game room on a map
  that cannot be built (for example one whose terrain art is missing) now stays
  on the setup screen and shows *CANNOT CREATE* with the reason, instead of
  silently doing nothing.
- **Windows test fixes and test tooling:** eight developer tests that
  failed on Windows are fixed (they needed symbolic links, which the Windows
  build cannot make, and `hpitool cat` wrote its output in text mode, which
  corrupted extracted maps). `hpitool cat` now writes exact bytes on Windows.
  The developer test harness stops at once with an error when its map cannot
  be built, instead of waiting for its time limit; the desync-hunt script no
  longer mistakes its own folder name for a desync and can use any build
  folder; and an out-of-bounds read in a Cartographer test is fixed.
- **The Legion scenario gate is re-baselined.** The developer gate that replays
  145 Legion pathfinding scenarios and compares them against a stored baseline
  had been left out of release checks since 0.8.11, and its stored game-state
  checksums had gone stale in 0.9.0, when reclaiming changed what the checksum
  covers. Only those checksums are retaken; every movement and timing limit is
  unchanged, and the gate passes again.
- **Server operators:** without `--map-cache-dir`, `takserver` now keeps
  uploaded and random maps in `TAKengine/takserver` in the per-user data folder
  (`$XDG_DATA_HOME` or `~/.local/share` on Linux, `%APPDATA%` on Windows,
  `~/Library/Application Support` on macOS) instead of its data folder, and it
  refuses to start if it has neither a home folder nor `--map-cache-dir`.
- **Protocol 299:** the dead-unit fix changes how fights play out, so 0.9.5
  plays only with 0.9.5 and plays only replays recorded by 0.9.5. A 0.9.4
  client or server is turned away with a *protocol version mismatch* message,
  and a 0.9.4 replay is refused with the version message.

**Upgrading (server operators):** update `takserver` together with your
players. A server started with `--map-cache-dir` behaves as before; one started
without it now caches maps in its per-user folder (above), and its data folder's
old `MapCache/` is no longer read.

[Release notes](docs/release-0.9.5-notes.md) · [Validation report](docs/release-0.9.5-validation.md) · [Source at v0.9.5](https://github.com/pocketgeek/tak-engine/tree/v0.9.5)

## 0.9.4
*2026-10-26 · protocol 298 · replay format 15 · does not play with 0.9.3 or earlier, nor their replays*

- **The Darien Crusades meta game is removed.** Everything built for the online
  campaign is gone from the client and the server: the strategic map of Darien
  and its campaign screens, the online campaigns themselves, allegiance to a
  side, territory matchmaking (searching for and issuing a battle over a
  territory), the issued Crusades battles with their forfeit-on-disconnect rule,
  the server's campaign database and its archive of battle replays, and the
  `crusades_admin` tool that managed it. The meta game was never offered in the
  game browser, so ordinary skirmish, multiplayer and campaign play is
  unchanged: rooms, accounts, spectating, reconnecting, replays, voice chat and
  game mods work as before. The client no longer asks a server for Crusades
  campaigns after signing in, and the lobby, the in-game menu and replay
  playback lose their Crusades-battle labels and rules (no more "ESC: RETURN TO
  CAMPAIGN").
- **What stays: the Crusades balance and accounts.** The **Crusades** option of
  a game room (the alternative unit balance from the final retail patch, and
  `--crusades` in debug launches) is kept, and so are server accounts and
  sign-in, which never depended on the meta game.
- **Smaller downloads and fewer moving parts:** `crusades_admin` is no longer in
  any package, `takserver` no longer links SQLite (the static SQLite build is
  gone from the source tree), and the client no longer mounts the Boneyards
  campaign art it used only for the strategic map. With issued battles gone,
  the server's rules have no exceptions left: every room writes its replay to
  the replay directory (when one is set) and allows spectators by its ALLOW
  SPECTATORS option, and a player whose connection runs out of time is always
  replaced by a server AI.
- **Crusades balance as in the original game.** The original game never reads
  the `canbuildcb/` build menus shipped with the final patch, and it does not
  fall back to the standard unit files for a unit the Crusades set lacks. With
  Crusades on, the units are now only those of the Crusades unit set
  (`unitscb/`), and every build menu is the standard one, limited to those units
  and in the same order. In practice:
  - **Six Iron Plague units do not exist in a Crusades game:** the Trapdoor
    Spider, the Heligrin, the Cult of Lokken Ship, the Sea Serpent, the Mummy
    and the Iron Plague's second Lokken unit (`tarnecr2`; the Taros monarch
    Lokken is unaffected). Until now a Crusades Beast Tamer could train the
    Trapdoor Spider, which the original game never offers.
  - **Gods are built by the monarchs again:** Elsin, Kirenna, Lokken and Thirsha
    build their side's god, as in the standard game, instead of the Acolyte,
    the Priest of Lihr, the Dark Priest and the Shaman.
  - **The Zhon Shaman builds the Giant Orm and the Giant Barracuda**, and the
    **Beast Handler builds the Swamp Beast**, as in the standard game.
  - **The Beast Tamer has its nine standard entries** (Lodestone, Kraken,
    Gryphon, Spirit Wolf, Basilisk, Harpy, Death Totem, Beast Handler, Beast
    Lord) instead of thirteen.
  - **The Ark has no build menu** (it built the Spyhawk, Swordsman and Archer
    in Crusades games).
  - The other menus that had moved are standard again: the Grenadier comes
    from the Acolyte, not the Keep; the Flying Builder no longer builds the
    Rolling Tower; the Flagship no longer builds the Lighthouse; and the
    Ballista comes from the Priest of Lihr, not the Citadel, whose menu is back
    in its standard order.

  Unit costs and statistics, which is what the Crusades balance is really
  about, are unchanged. Standard games are bit-identical to 0.9.3. A menu entry
  without a `[Menu] Priority` now sorts last, as in the original game, and a
  unit without `builder=1` gets no build menu; this matters only for mods,
  since the shipped data has neither. Because `canbuildcb/` is no longer game
  data, the game-data check value every player and server compare changes for
  every install; this only matters together with the protocol change below.
- **Protocol 298:** removing the Crusades messages renumbers the network
  messages after them, and the balance fix changes Crusades games, so 0.9.4
  plays only with 0.9.4 and plays only replays recorded by 0.9.4. A 0.9.3
  client or server is turned away with a *protocol version mismatch* message,
  and a 0.9.3 replay is refused with the version message. A Crusades-balance
  replay containing one of the six removed units could not be played anyway.

**Upgrading (server operators):** update `takserver` together with your
players. `takserver` no longer has `--crusades-db` or `--crusades-definition`;
a server still started with either (and its value) prints a warning that the
option was removed and is ignored, then starts as usual, so remove them from
your service drop-in or script at your convenience. The campaign database (`campaign.sqlite` and its `-journal`, `-wal`,
`-shm` and `.service-lock` files) and the `crusades-replays/` folder are never
read and can be deleted. Accounts, the account file and the replay directory
are untouched. See [public-server.md](docs/public-server.md#versions).

**Upgrading (players):** nothing to do. A client's old `ReplayCache/` folder
beside `settings.ini` (downloaded Crusades battle replays) is no longer used and
can be deleted; your settings, including the remembered Crusades choice of the
game setup screen, are kept.

[Release notes](docs/release-0.9.4-notes.md) · [Validation report](docs/release-0.9.4-validation.md) · [Source at v0.9.4](https://github.com/pocketgeek/tak-engine/tree/v0.9.4)

## 0.9.3
*2026-10-26 · protocol 297 · replay format 15 · does not play with 0.9.2 or earlier, nor their replays*

- **Trebuchets fire again:** a Trebuchet no longer sits idle because a wall,
  hill, tree line or other scenery lies between it and its target. Its
  shell flies in an arc, so it fires, as in the original game, which never
  checks for a clear line before firing a lobbed shell. In a test game
  between two computer players an Aramon Trebuchet held a target for 47
  minutes without a single shot.
- **Every arrow, spear and cannon ball does the same:** the same fix applies
  to 42 unit types that shoot a plain arcing shell: the bows, cannons and
  both Trebuchets, the Arrow Tower and other towers with arrow weapons. They now fire at targets behind scenery instead of waiting
  for a clear line, and a unit whose shots keep landing short walks to a clear
  spot as described under Blocked shots in the user guide. Units with
  direct-fire weapons (flames, magic bolts, beams) still need a clear line,
  and a unit that mixes both kinds (the Cre'on Sage) now tests each weapon on its own.
- **Towers stop holding targets they cannot hit:** a tower or other building,
  or a unit on Hold Position, that has picked a target it has no clear line to
  now drops it and looks for another, instead of holding it without firing
  while an enemy in the open shoots it. In a test game a tower whose own new
  walls blocked its target ignored an enemy in the open for a whole minute.
  Orders you give directly are unchanged.
- **Fewer idle standoffs:** in 16 test duels between two computer players
  (Creon against Aramon on Edmont's Field), the times a unit stood holding a
  target for over a minute without firing fell from 80 to 4, and the one duel
  that ended with no winner within 90 minutes now has one.
- **macOS streaming uses OpenSSL:** streaming to RTMPS services (YouTube,
  Twitch and the like) from a Mac now uses the same OpenSSL as multiplayer,
  instead of Apple's deprecated SecureTransport, which needed a patch in 0.9.1
  to keep uploads alive. Certificates are still checked against the macOS system root
  certificates, and a certificate for the wrong host or from an unknown
  authority is still refused with the TLS CERTIFICATE reason. Windows and
  Linux are unchanged. Certificates you added to the Mac keychain yourself
  (for example a company proxy) are not used.
- **Streaming panel remembers its settings:** the YouTube streaming panel
  now keeps its resolution, frame rate, bitrate and encoder between sessions
  in `settings.ini`. A value that is not one of the panel's choices falls back
  to the default (MAX 3840, 60 FPS, 6,000 Kbps, Automatic), a saved bitrate
  below the saved resolution's minimum is raised to it, and Options' reset to
  defaults leaves them alone, like Remember Key and Save a Local Copy.
- **Stream banner no longer covers the scorecard:** the STREAM LIVE banner in
  the top-left corner hid the F4 scorecard's header. The banner, game clock,
  scorecard, objectives and voice-chat speaker list now stack in that corner
  instead of overlapping, and at large scorecard scales the scorecard stays
  clear of the minimap. The banner is not part of the streamed video, but the
  overlays below it move down by its height there too.
- **Not compatible with 0.9.2:** the firing changes change the game itself,
  so 0.9.3 uses protocol 297 (replay format 15 is unchanged). It does not play
  with 0.9.2 or any earlier version, nor their replays.
- **Upgrading:** a server and all of its players update to 0.9.3 together;
  public-server operators update `takserver` as well. Keep 0.9.2 (it stays on
  the releases page, or build it from its tag) to watch replays recorded
  before 0.9.3.

[Release notes](docs/release-0.9.3-notes.md) · [Validation report](docs/release-0.9.3-validation.md) · [Source at v0.9.3](https://github.com/pocketgeek/tak-engine/tree/v0.9.3)

## 0.9.2
*2026-10-25 · protocol 296 · replay format 15 · does not play with 0.9.1 or earlier, nor their replays*

- **Flames blocked by scenery:** flames that stop on a rock, a standing
  stone or other indestructible scenery now count as blocked shots, like
  arrows and spears. A Fire Wagon or other flame unit that picked its target
  while on the move (an attack-move, a patrol or a queued move) walks to a
  clear shot, even on Hold Position, or drops the target and carries on. In a
  reported game a Fire Wagon burned a standing stone 101 times in a row
  without ever reaching the lodestone behind it.
- **Lobbing weapons stay within their arc:** units that lob their shots (the
  Creon Submersible, catapults and the like) no longer fire at targets their
  arc cannot reach, as in the original game. They close in until they can hit,
  or hold their fire when they cannot get nearer. Before, such a unit lobbed
  the shell flat into the ground or water in front of it: in a reported game
  44% of all Submersible shells landed beside the Submersible itself.
- **Towers no longer flicker between targets:** towers, other buildings and
  Monarchs holding position no longer pick a target they cannot hit, only to
  drop it again a moment later, over and over. They keep firing at what is in
  range. In a test game between two Retail AI players, the Creon Monarch,
  Prismatic Mirrors and Aramon Strongholds spent their time switching
  targets instead of shooting, and the game never ended.
- **AI factory doors:** the computer players (every level except Retail) keep
  the ground in front of their factory doors clear and no longer park units in
  another factory's doorway, so new units are not trapped inside.
- **AI Navy Yards:** the computer players build Navy Yards where ships can sail
  straight out to open water, not at the head of a narrow inlet where new ships
  stayed stuck.
- **Not compatible with 0.9.1:** the flame, lobbing and targeting changes
  change the game itself, so 0.9.2 uses protocol 296 (replay format 15 is
  unchanged). It does not play with 0.9.1 or any earlier version, nor their
  replays.
- **Upgrading:** a server and all of its players update to 0.9.2 together;
  public-server operators update `takserver` as well. Keep 0.9.1 (it stays on
  the releases page, or build it from its tag) to watch replays recorded
  before 0.9.2.

[Release notes](docs/release-0.9.2-notes.md) · [Validation report](docs/release-0.9.2-validation.md) · [Source at v0.9.2](https://github.com/pocketgeek/tak-engine/tree/v0.9.2)

## 0.9.1
*2026-10-25 · protocol 295 · replay format 15 · does not play with 0.9.0 or earlier, nor their replays*

- **Aiming at the body:** units now aim at the middle of the target's body, as
  in the original game. Before, most units aimed too low, some below the
  target's feet, so spears and other shots that fly in a curve hit the ground in
  front of it: Zhon Hunters almost never hit each other (4% of shots in a
  reported game, 89% now). The aim point also turns with the target, as in the
  original.
- **Lightning at the Creon Submersible:** whether the Thirsha's lightning hits
  a parked Submersible now depends on which way the Submersible faces, as in
  the original, and it still misses often, as the original does: the
  Submersible's aim point lies off its centre, outside the cells it stands on.
  In the reported game it missed 47 bolts out of 47; the original hits 17 of
  those same shots, and we now aim the way it does.
- **New units leaving their building:** a new unit with no rally point moves
  off the building's pad to a spot a short way from the building and waits there, as in
  the original. A ship only picks a spot it can sail to, so ships no longer stay
  stuck on the pad of a yard by a small lake. The AI no longer builds a
  shipyard on a pond that does not reach the sea.
- **Flyers leaving the factory:** a new flyer with no rally point flies to a
  random spot on a circle around the factory, a few hundred pixels out and on
  any side of it, as in the original, instead of stopping a short way in front
  of it. A flyer with a rally point flies straight to it, with no stop on the
  way.
- **New units under Legion:** with Legion pathfinding, each new unit with no
  rally point takes a free spot of its own around the building, nearest the
  door first, on alternating sides and never in front of the door, so units
  from a busy Barracks do not queue in a column for one spot and block its
  door. When every spot is taken, they spread further out. Retail pathfinding
  is unchanged.
- **Game speed:** raising the speed to +10 now takes effect at once. A player
  or spectator whose game had paused for a moment (while it loaded, or after
  switching windows) could stay at about +4 for most of a minute.
- **Streaming on Intel and AMD (Linux):** YouTube streaming now uses the
  computer's own VA-API library (`libva`) and graphics driver for hardware
  encoding. The game used to carry an older copy of libva that could not start
  current drivers, such as Intel's on Fedora 44, and did not look in Fedora's
  RPM Fusion driver folders, so streams fell back to the CPU (**LIVE - CPU
  FALLBACK**) even where `vainfo` worked. Without libva the game still streams
  on the CPU as before; libva is not a package dependency. This changes nothing
  about who can play together.
- **YouTube streaming on macOS:** streams from a Mac now reach YouTube. Before,
  every upload stopped after a few frames: the panel went RECONNECTING and then
  FAILED, and only the local copy was saved. A bug in FFmpeg's macOS TLS code
  froze the upload for 5 seconds the first time it checked for a reply from
  YouTube; the built-in FFmpeg is now patched. When streaming fails, the panel
  now says why: the certificate was not trusted, the connection could not be
  made, or it kept dropping. A stream that stayed up for a minute gets three
  fresh retries when the connection drops, instead of sharing three retries
  across the whole stream.
- **Streaming from a large window:** a stream smaller than the game window now
  keeps up. Streaming a 7680x2160 window at MAX 3840 (3840x1080) and 60 FPS on
  Windows encoded only about 18 frames a second: the panel counted the rest as
  DROPPED, and the bitrate stayed near a third of the setting. Under Direct3D
  9, what Windows used to pick by default, capturing the window also cut the
  game itself from 240 FPS to about 30. The stream's resize now runs on several
  threads, Direct3D 9 and 11 shrink the frame on the graphics card as OpenGL
  already did, and the encoder no longer oversleeps on Windows. On the reporter's setup the stream
  now encodes all 60 frames a second at about 6,000 Kbps, and the game stays at
  240 FPS. This changes nothing about who can play together.
- **Hardware cursor while streaming:** streaming no longer switches the cursor
  to the one the game draws itself. The pointer stays the system's hardware
  cursor on your screen, and the stream draws the same cursor into the video:
  the same art, animation frame and size, at the pointer, scaled to the stream,
  clipped at the picture's edges and left out while the pointer is outside the
  window. With Hardware Cursor off nothing changes: the game draws the cursor and
  the stream captures it. This changes nothing about who can play together.
- **Which cursor is in use:** the Options screen now shows beside Hardware Cursor
  what the game gave the system, such as `OS CURSOR 64X100`, or
  `UNAVAILABLE - SOFTWARE` when the system refused that size and the game is
  drawing the cursor itself; the log says the same. On the Windows and macOS
  machines where the cursor was reported to feel like a software cursor, the
  hardware cursor was in use throughout, with nothing drawn by the game, so a
  report can now say which one ran.
- **Windows draws with OpenGL by default:** with the Renderer option on Auto,
  Windows now uses OpenGL, where some of the game's drawing shortcuts work,
  instead of Direct3D 9. Auto passes over Windows' software OpenGL (a machine
  with no graphics driver) and Microsoft's OpenGL compatibility layer and uses
  Direct3D 11 instead, then Direct3D 9. The Renderer row shows the pick, such
  as **Auto (OpenGL)**; choose **Direct3D 11** or **Direct3D 9** there if
  OpenGL misbehaves. OpenGL was faster on the test machine with 2,000 units on
  screen; with 10,000, Direct3D 11 was about 10% faster. Linux and macOS pick
  as before; their Auto row now names the pick too, such as **Auto (Metal)**.
- **Not compatible with 0.9.0:** the aiming and the way new units leave their
  building change the game itself, so 0.9.1 uses protocol 295 (replay format
  15 is unchanged). It does not play with 0.9.0 or any earlier version, nor
  their replays.
- **Upgrading:** a server and all of its players update to 0.9.1 together;
  public-server operators update `takserver` as well. Keep 0.9.0 (it stays on
  the releases page, or build it from its tag) to watch replays recorded
  before 0.9.1.

[Release notes](docs/release-0.9.1-notes.md) · [Validation report](docs/release-0.9.1-validation.md) · [Source at v0.9.1](https://github.com/pocketgeek/tak-engine/tree/v0.9.1)

## 0.9.0
*2026-10-24 · protocol 294 · replay format 15 · does not play with 0.8.14 or earlier, nor their replays*

- **AI and the Rolling Tower:** the shipped AI profiles misspell the Aramon
  Rolling Tower (`araseige`). The original game ignores the name, so the Retail
  AI built Rolling Towers without limit, and our levels (Easy to Absurd) never
  built one. Both now read either spelling as the Rolling Tower: the Retail AI
  keeps to the profile's limit (10 in `ai/default.txt`), and our levels build up
  to 10. Only the server runs the AI, so this changes nothing about who can play
  together.
- **Build sparkles:** the sparkles over a building going up now match the
  original. The ring sits on the edge of the building's footprint and holds the
  original number of sparkles; buildings whose unit file has no `bmcode` line
  were sized like mobile units, with a wider ring and a quarter of the sparkles.
  Rising sparkles on high ground or water also climb as far as they did in the
  original before they fade. The sparkles are display only, so this changes
  nothing about who can play together.
- **Gate doorways:** clicking the doorway of an open gate now works like
  clicking the ground, as in the original: it selects nothing, and a right-click
  moves the selected units through or into the opening instead of guarding,
  repairing or attacking the gate. A closed gate, the gate's posts and units
  standing in the doorway are still selected as before. This is a mouse change
  only, so it changes nothing about who can play together.
- **Area clear (right-drag box) and reclaim:** every selected builder that can
  reclaim now takes the order, each working through the same box, as in the
  original game; before, only the first one did. The same goes for a
  right-click or **CLEAR** click on a single tree or rock.
- **Area clear no longer skips trees:** where two of a map's trees, rocks or
  other objects overlap, the original game removes the earlier one when the map
  loads. We kept it, drawn and blocking but never picked by an area clear. It
  is now gone, as in the original, which can open small gaps in some forests
  (Inner Circle loses one tree; 481 of 1,360 maps checked lose some). Such an
  object no longer makes a sacred site or a lodestone deposit either.
- **Area clear under the fog:** trees and rocks on ground your side has
  explored are cleared even while you cannot see them, as in the original;
  wrecks still have to be in sight. The room's FOG OF WAR option counts:
  **EXPLORED** and **FULL VISION** make the whole map count as explored.
- **Unreachable objects:** a builder now gives up on a tree or rock it cannot
  reach, and an area clear ends when the nearest object left is one it could
  not reach, as in the original. Before, the builder could keep trying forever.
- **Reclaim works as in the original:** a builder now walks right up to a tree,
  rock or wreck and works standing next to it; before, it started from as far
  away as its build distance. Each object takes as long as the original game
  gives it, whatever the builder: about 9 seconds for a big tree (it was about
  2), half a second for a rock or wreck, and builders on the same object no
  longer share the work. Reclaiming gives **no mana**, as in the original; it
  gave the object's value before. The object fades for about a second before
  it is gone and leaves no rubble. Area clears and the clearing a builder does
  before building take longer as a result.
- **Not compatible with 0.8.x:** these changes alter the game itself, so 0.9.0
  uses protocol 294 and replay format 15. It does not play with 0.8.14 or any
  earlier version, nor their replays.
- **Upgrading:** a server and all of its players update to 0.9.0 together;
  public-server operators update `takserver` as well. Keep an older engine
  (built from its tag) to watch replays recorded before 0.9.0.
- **Documentation:** the release history moved out of the README into this
  changelog; the README, quick start, user guide, map editor guide, server
  guide and streaming guide were rewritten and their pictures retaken.

[Release notes](docs/release-0.9.0-notes.md) · [Validation report](docs/release-0.9.0-validation.md) · [Source at v0.9.0](https://github.com/pocketgeek/tak-engine/tree/v0.9.0)

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
