# TAK Engine 0.8.4

0.8.4 follows 0.8.3. It adds push-to-talk voice chat in multiplayer, the
Legion 10,000 program (a click reaches every unit of a 10,000-unit selection in
one tick, long routes, shared passages), a new AI difficulty, Absurdly
Defensive, beside an uncapped Defensive, replays that are kept however a game
ends, the original game's sharing of short mana across construction, and
<!-- RENDER-OPT PLACEHOLDER: name the rendering work in this list. -->
[the rendering work: placeholder]. The simulation changed in both pathfinding
modes, so the network protocol moves from 266 to **276**: **0.8.4 cannot play
with 0.8.3 or earlier, and cannot play their replays.** A server and all of its
players must update to 0.8.4 together. As in 0.8.3, Retail pathfinding does
**not** play exactly as before: the shared mana changes Retail games too,
although the Legion work leaves Retail untouched.

## Voice chat

Players in a multiplayer room or game can talk to each other (protocol 276).
See [Voice chat](user-guide.md#voice-chat) in the user guide.

- **Off by default.** Turn it on on the new **Voice Chat** page: **Settings →
  Voice Chat** on the main menu, **Esc → Voice Chat** in a game, or the new
  **Voice chat** row in the Audio section of Options. While voice chat is off,
  the page's other rows are greyed out and nothing is opened: no microphone, no
  output, and the server sends the client no voice.
- **Devices and volumes.** **Mic device** (System Default or any input) with
  **Mic volume** 0–100 and a live **Mic level** meter; **Output device** (Same
  as Game Output, the default, which follows the game's Sound device; System
  Default; or any output) with one **Output volume** 0–100 for every speaker. A
  missing device falls back to the system default with a note
  (**NOT FOUND - USING DEFAULT**, **DEVICE LOST - USING DEFAULT**,
  **UNAVAILABLE**), and the game returns to the chosen device when it comes
  back. The page has its own **Defaults**, which touches only voice settings.
- **Push to talk** on **Space**, rebindable as **PUSH TO TALK** under **VOICE**
  in Controls (Enter, the number keys and the arrows are refused). A key pressed
  while typing in a text box is typing, never talking, and a transmission ends
  at once when the key is released, the window loses focus, a text field takes
  the keyboard or the player leaves the room.
- **Who is talking** is shown at the top left: **YOU** while sending, then each
  player heard in their colour.
- **Diplomacy.** The diplomacy screen (**D**) gains a **Voice** column, which
  chooses who hears you like the Chat column (everyone at the start of each
  game), and a **Volume** column with a 0–100% slider for each other human
  player. The sliders are local: never sent, recorded or hashed, reset to 100%
  each game and greyed out while voice chat is off. Before the game starts,
  everyone in the room hears you; spectators talk to everyone.
- **Relayed by the server, never stored.** Voice is Opus at 48 kHz mono, about
  3 KB/s per talker, on the same connection as the game (so encrypted on TLS
  servers). It never enters a tick bundle, a replay, the referee or the state
  hash.
- **The game comes first.** Voice waits behind every game message at both ends,
  and a link that cannot carry both loses voice, not game speed: the server
  stops sending voice to a listener whose link lags the running game, and a
  talker whose own upload backs up stops sending, each for 5 s doubling to 60 s,
  then resumes by itself. In the slow-listener test (two listeners read at
  1,500 B/s while a third player talks) the room keeps about 27 of its 30 ticks
  a second; without these rules it ran at 2–8. Over a 2,500 B/s upload the
  talker's own tick acknowledgements wait at most about 0.4 s behind its voice,
  where without the hold Linux queued them up to 2.4 s and Windows 22 s after a
  minute.
- **Server limits.** Each talker is relayed at most 60 frames and 16 KB a
  second, the whole server 4,000 frames a second; 300 frames a second, a
  malformed frame or a client sending server-to-client voice fails that
  connection. See [public server deployment](public-server.md#resource-controls).
- **macOS** asks for microphone permission the first time the microphone opens
  (the app carries the microphone usage text and the signed programs the
  audio-input entitlement, without which a notarized build would record
  silence). **Windows** needs Windows 10 version 1703 or later to send voice:
  where the system cannot report how much it still has queued, voice is
  dropped rather than allowed to delay the game.

## Rendering

<!-- RENDER-OPT PLACEHOLDER: the lead fills this section from
     docs/render-optimization-progress.md when task-render-opt merges: what got
     faster, the measured before/after, any option or default that changed,
     any visible difference, and the protocol (none expected; rendering is
     client-only). Also update the intro paragraph, README "New in 0.8.4" and
     the validation report's Scope and Screenshots sections. -->

**Placeholder: the rendering optimization program is still on its branch.
This section is filled in when it merges.**

## Game

- **Short mana is shared across construction** (protocol 273, Retail and
  Legion). When income cannot keep up, every factory, conjuring, building site
  and repair now does the same share of its work each tick, as the original
  game does (the share is the stored mana over the total demand), instead of
  each paying its whole cost or doing nothing, in unit order. Before, the
  cheaper producers took every trickle and the dearest stood still: in a
  playtest a Beast Lord's drake sat at 5% for 6,543 ticks (3.6 minutes) beside
  six other producers and a builder. Unstarved games behave as before. The
  user guide always said that "all construction slows down together"; now it
  does.
- **A divine lodestone upgrade finishes under a short share** (protocol 274).
  With the share at or below one half, the upgrade could stop one rounding step
  short of done, at full health, until the player freed mana by stopping another
  builder; in the playtest that cost 3 minutes 29 seconds. It now completes on
  the shared schedule, as in the original.

## Computer players

- **Defensive keeps building as fast as its income allows** (AI only). It has
  no limit of its own on factories, builders, towers or troops, and the
  original game's AI file no longer caps it; only the room's unit cap and the
  game's rules stop it. Its builders other than the Monarch claim deposits out
  to 1,600 pixels from home, its base grows outward with its buildings (the original AI's base
  radius) but stays on its own side of the map, idle troops stand on rings
  around the base instead of on factory pads, and a factory whose unit finds no
  room drops it and trains something else. It still never attacks or scouts and
  keeps its Monarch home. In 0.8.3 its income never left its first deposit
  (27–34 mana a second at 30 minutes on every map and side tested); now it
  reaches 113–205 on the maps with deposits to spare, and in one-AI tests at
  60 minutes on three maps the mana store never sat 90% full in 29 of 30 side
  and level combinations (the exception had reached the 2,000-unit cap). See
  [the Defensive notes](ai-defensive-2026-10-17.md).
- **New difficulty: Absurdly Defensive** (protocol 275). It plays as Defensive
  with Absurd's double mana income from every source (recurring income, reclaim
  and, through income, allied sharing), and like Defensive its units take the
  Defensive stance. The lobby lists it after Absurd, in violet. Every peer derives the income
  multiplier from the level, so older builds cannot take part.
- **Defensive melee troops answer the unit hitting them.** A melee defender now
  attacks its own attacker first, otherwise the nearest threat it can reach,
  and keeps chasing a target while it stays near, instead of being retargeted
  about every second. Replaying a playtest with a live Defensive AI, its melee
  attack orders fell from 1,472 to 257, and a traced Swordsman held each target
  until it died.

Easy, Normal, Hard and Absurd play as before.

## Replays

- **A replay is kept however the game ends.** The recording is written to disk
  while the game is played (`<name>.takrep.part` until it ends), so a win, a
  loss, quitting, a lost connection, a crash, the game being killed or the
  computer losing power all leave a replay, running to the moment the player
  left, including anything watched after the result. A recording a crash or a
  kill left unfinished is completed at the next start, losing at most about the
  last second. The finished file is format 13, byte for byte as before.
- **Campaign missions are recorded again.**
- A failed write (a full disk, an unwritable folder) no longer stops the
  recording: the game keeps it in memory and writes it as soon as it can.
- `nohup takclient` keeps running when its terminal closes.
- **Servers.** A `takserver --replaydir` stopped by SIGTERM (`systemctl stop`)
  or Ctrl-C first writes the replays of the games still running, and every
  server replay is written under a temporary name and renamed when complete.

## Legion

The Legion 10,000 program (protocols 267 to 272, Legion only) extends Legion to
about 10,000 individually simulated units moving at once. Retail is
bit-identical to 0.8.3 through these steps. The
[Legion 10,000 report](legion-10000-report.md) has every measurement; the
design is in the [Legion notes](legion-pathfinding.md).

- **One click is one order** (protocol 267). A Legion client sends a click as
  one explicit order for the whole selection, and the server puts it into a
  single tick. A 10,000-unit click used to take 308 ticks (10.3 s) to arrive and
  became up to 20 groups; on the real client and server path, an 18,000-unit
  order took 345 ticks and the next order could replace it before it landed.
  Now it lands whole in one tick, and in the 10,000-unit single-click test the
  last unit starts moving after 92 ticks instead of 652.
- **Fair, budgeted navigation work** (268): planning is shared fairly between
  orders inside a fixed budget each tick, so one big order no longer makes a
  long tick. **Route queries** (269): a unit asks for the part of a route it
  needs, and waits for it correctly while the route is still being built.
- **Hierarchical routes** (270): long routes are planned over sectors and
  regions and share tiles between orders.
- **Static changes apart from traffic** (271): a closed way re-plans at once, an
  opened one later, and standing jams count as traffic to other orders.
- **Chokepoints, subgroups and arrivals** (272): bridges and doors carry their
  queues, groups choose and split between passages by travel time plus queue,
  a narrow passage admits one direction at a time, a second order to the same
  point joins the first one's destination area, and each group fills a room
  from its own entrance.
- **Parallel planning** (no protocol change): independent planning work runs on
  the simulation's worker threads, with every hash identical on any number of
  workers.

128 of the program's 212 frozen targets are met (99 at the start). Not done:
very large crowds still jam at static corners, at the mouths of narrow passages
where two armies meet head on, and in the route queue when many groups head for
many different places; two of the ten 10,000-unit workloads now run over the
33 ms tick budget in 2–13% of ticks, because four to eight times as many units
actually move. See the report's sections 13 and 14.

## For contributors

- **Opus** 1.5.2, voice chat's codec, is built from its pinned, SHA-256-checked
  source release (`cmake/Opus.cmake`) and linked statically into `takclient`
  only, so packages gain no runtime library. Its BSD licence ships as
  `OPUS-LICENSE.txt` beside the binaries. Offline builds set
  `-DFETCHCONTENT_SOURCE_DIR_TAK_OPUS`; see [development](development.md).
- New tests include `voice` (codec, jitter buffer, push to talk, the engine on
  SDL's disk audio driver, the Voice Chat page and the per-player mix),
  `voice_client`, `voice_uplink`, `server_voice`, `server_voice_slow`,
  `server_voice_cpu`, `diplomacy_ui`'s volume sliders and `conn_test`'s
  low-priority queue; `replay_writer` and `replay_always` (self-kill, outside
  kill, play on after defeat, SIGTERM, a dead server, campaign missions);
  `production_test`'s shared-mana and lodestone-upgrade cases;
  `ai_behavior_test`'s Defensive, Absurdly Defensive and melee-answer checks;
  and the Legion tests `selection_order`, `server_orders`,
  `server_orders_network`, `navbudget_test`, `legion_hier_test`,
  `legion_route_test`, `legion_route_paused`, `legion_parallel`,
  `legion_sched_*`, `legion_planepin`, `legion_traffic`, `legion_choke`,
  `legion_arrival`, `legion_arrival_press`, `legion_entrances`,
  `legion_sharedarea` and the `legion_scale_*` suite.
- The CI's per-commit Legion step builds `legion_hier_test` and
  `legion_route_test` (`27066ebe`; before, both were reported Not Run and the
  step failed on every platform).
- The remote desync sweep's `h-cramped` case runs with Monarch Expendable
  (`5774601a`): since 0.8.3's air combat, the Zhon god's storms killed the idle
  seat's Monarch at tick 1,303, leaving the case checking almost nothing.
- The `--mpai` Inner Circle seed 1 hashes are now: Retail 300 s
  `a3d953803a064191`, 60 s `b353410003e98dd5`; Legion 300 s
  `ce5c4ba7562e53b9`, 60 s `0825aeb9e8139203`.
  <!-- LEAD: confirm on the final main after the rendering merge (the
       validation report's --mpai table); update if anything moved. -->

## Compatibility

Protocol **276** (0.8.3: 266), replay format **13** (unchanged), generator
version **8**, campaign payload **4** and Crusades SQL schema **9**. Checked in
source against `v0.8.3`: `kNetVersion` in `src/net/protocol.h`, `kReplayFormat`
in `src/net/replayhdr.h`, the map generator's `formatVer` (`src/tnt/mapgen.h`,
no change under `src/tnt/`), the campaign payload's `kVersion`
(`src/net/crusades.h`, unchanged) and `kSchemaVersion`
(`src/server/crusades/store.cpp`, unchanged).
<!-- LEAD: re-check kNetVersion right before tagging; update 276 everywhere if it moved. -->

- **Multiplayer.** A server rejects a client whose protocol differs from its
  own, so **0.8.4 clients cannot join 0.8.3 or older servers, and 0.8.3 or
  older clients cannot join a 0.8.4 server**. Update the server and every
  player together.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.8.4 does not play replays recorded by 0.8.3 or earlier, and
  they do not play 0.8.4 replays, although the file format is unchanged. Keep
  an old copy of the engine to watch old recordings. Crusades battle replays
  follow the same rule.
- **Maps and settings.** Generated-map recipes and saved `.kmp` maps carry over.
  A 0.8.3 settings file reads the same in 0.8.4. New keys: `voiceEnabled`,
  `voiceMicDevice`, `voiceOutputSameAsGame`, `voiceOutputDevice`,
  `voiceMicVolume` and `voiceOutputVolume`, and `hotkey.voice.ptt` once push to
  talk is rebound. 0.8.3 ignores the voice keys and drops them the next time it
  saves its settings. Anyone who had bound **Space** to another action in 0.8.3
  keeps that binding, and push to talk does nothing until it is given another
  key in Controls.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.8.4-validation.md).
