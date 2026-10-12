# TAK Engine 0.7.28

0.7.28 follows 0.7.27. Units now fire the way they did in the original game:
on the move, on their way to a building site, and back at whoever hits them
while they stand idle. Game speed uses the original's levels, blocked building
sites no longer hold a builder up, the Defensive AI and the AI Monarch defend
themselves better, and Legion pathfinding gets the ten workstreams of its
2026-10 plan. Engine-wide performance work makes simulation and display cheaper
without changing any game.

- **Fire at will, as in the original.** Each player now runs the original
  game's round-robin target sweep over its units, and every weapon has its own
  target and an **automatic** or **manual** mode:
  - **Units fire while they move.** A unit on a plain move or a patrol shoots
    at enemies in range on the way. It no longer has to stop,
    brake or turn its body to fire. Only hover-attack flyers (and the few ground
    units that must turn in place to aim) still face their target. A ground unit
    standing with no orders still turns towards what it shoots.
  - **Builders defend themselves en route.** An armed builder walking to a
    building site fires at enemies it passes. While it places, builds, repairs,
    reclaims or resurrects, its weapons are manual and stay quiet, as in the
    original; they return to automatic when that job ends. An attack order also
    makes the weapons manual for as long as it lasts.
  - **Idle units hit back.** An idle or patrolling armed unit that is hit
    attacks its attacker if it can reach it. On **Offensive**, and on patrol, it
    chases no further than its leash from where it was hit, then walks back and
    resumes what it was doing. On **Defensive** it fires back from where it
    stands, and **Passive** units do not respond.
  - **Targets come from what each player sees.** Vision for targeting is now
    part of the simulation: every peer, the server's referee included, keeps a
    per-player record of which units each player can see, so automatic targets
    are chosen identically everywhere. The visible lists are refreshed every
    seven updates, so a newly seen target can take up to a fifth of a second to
    be picked, as in the original. The fog you see on screen is unchanged.
  - **Computer players switch weapons.** AI-owned units with several weapons
    pick one on each sweep visit by the original rules: splash weapons when
    enemies bunch and allies are few, the primary weapon when nothing better
    applies. Your own units keep the weapon you choose with **W**.
  - **A few objects are never auto-targeted by players' units.** As in the
    original, a human player's units do not pick unit types without the
    `shootme` flag (in the shipped data: the Creon Gate, the mission Flag and an
    Aramon placeholder) as automatic targets. Computer players' units do, and an
    explicit attack order works on them either way (protocol 253).

  See [the retail-engine notes](retail-engine.md#fire-at-will-sweep-and-weapon-slots-icd-ported-2026-10-11-protocol-252)
  for the routines ported and the deviations kept.
- **Game speed matches the original.** **+** and **−** step one level from **−9**
  (0.1×) through **NORMAL** to **+10** (2×), 0.1× per level, and the banner says
  **GAME SPEED NORMAL** or **GAME SPEED +5** as in the original. The old range of
  0.5× to 8× is gone. Only the host can change speed, and only when **Allow speed
  change** was on, as before. The stats panel's SIM row and the Benchmark's SIM
  column now show the measured speed as the nearest level (NORMAL, +5, −3)
  instead of a multiplier. Debug builds keep up to 8× for the test harnesses
  (`TAK_FAST_SPEED`); every release server clamps to 2×.
- **Blocked building sites.** Placement at a site now follows the original:
  - A site blocked **only by mobile units** is retried every third of a second
    while the builder waits where it stands (a flying builder hovers), and the
    order is dropped after 31 failed tries, so the builder moves on to its next
    order after about ten seconds.
  - A site blocked by **anything else**, such as a building, a feature that
    cannot be reclaimed, the terrain or a unit cap, is dropped at once.
    Reclaimable trees and rocks are still cleared first, as before.
  - **Legion:** a builder held in front of another player's unit that never
    moves used to wait for ever. It now stops as close as it can get and then
    places the building if the site is within reach, or drops the order.
- **The AI defends itself better.**
  - A **Defensive** AI now answers attackers it cannot see, such as archers
    shooting its units from beyond their sight, as long as the attacker is
    within 1,500 of its base. Its melee units attack such an attacker directly
    and are walked home once it leaves.
  - The **AI Monarch fights back.** Whether it is building, moving or idle, a
    computer player's Monarch attacks whoever last hit it, or an armed enemy
    within its reach, instead of standing at a stalled build. A hit also makes
    it drop the build it is working on.
  - The Defensive AI no longer picks building sites within 400 of a spotted
    armed enemy, so its builders are not sent into a fight.
- **Legion: the 2026-10 plan (protocols 239–248).** Legion, the optional
  group pathfinding mode, was reworked in ten workstreams, W0 to W9, each
  judged against Retail on a set of recorded scenarios:
  - **One order per click** (W3). A right-click is one order however large the
    selection, with one destination area and one formation; quick clicks to the
    same spot join it. The last units of a large group, pressed against their
    own crowd, settle where they stand instead of edging round it for minutes.
  - **Unreachable orders end** where the unit stops, and units held behind
    their own stopped army end their orders there (W2). Buildings and
    unfinished units no longer slow an Alt+number formation to a standstill. A
    patrolling builder's repair detour uses Legion's own route.
  - **Attackers surround their target** (W5): two or more units attacking one
    target spread round it to a ring of places at weapon reach, and units
    that are firing stand still while the others walk round them.
  - **Flyers stay with the army** (W6): flyers in a Ctrl+number group, as
    well as an Alt+number formation, hold stations over the ground units of the
    same order, and land at their destination when the ground stops. A flyer
    that lifted off to let an army pass comes down after at most a minute and
    fights while it is up.
  - **Crossing groups take the longer route** (W7) behind the other stream's
    tail instead of waiting at its edge, and units stuck in a clot no longer
    block other groups' routes.
  - **Gaps are taken several abreast** (W8): a large group splits across a
    short gap that fits two or three units instead of queueing in one file.
  - **Columns meeting head on keep right** (W9): two large groups on the same
    route narrow slightly before they meet and pass each on its own right.
  - Earlier in the cycle (protocol 239): groups with the same goal share one
    route field, a blocked unit stands instead of walking in place, idle allied
    landed flyers lift off to let ground groups pass, formation flyers cruise
    with the group and land on arrival, and moving groups plan round each
    other as wholes.
  - **Cheaper upkeep** (W4): Legion's per-unit records are cleared when orders
    change instead of by a walk over every unit each update, route fields are
    refreshed only for groups that are using them, and a group's first field
    pauses once it covers the group.

  **Accepted limits.** At the close of the plan, eleven harder targets resisted
  every local fix and were accepted as known limitations, to be revisited
  later. Compared with Retail on the same scenarios: units in a group swap
  places more often at corners, in long straits and after wide gaps (because
  Legion takes gaps several abreast); bodies in an attack ring against a wall,
  or queued at a blocked door, touch the wall about twice as often; a group
  crossing its own firing ring arrives about 11% later; from some starting
  positions one unit of a door-plug or crossing scenario never finishes; and a
  lifted flyer detours 5 cells where Retail's detours 2. The 61-of-64
  acceptance check for packed separate goals is still a ratchet, not a pass,
  and with thousands of units Legion still costs more processor time than
  Retail. See [Legion's known weaknesses](legion-pathfinding.md#known-weaknesses)
  and [the user guide](user-guide.md#legion).
- **Faster simulation and smoother display (engine-wide work).** Every
  simulation change below is exact: the recorded stress replays, the
  navigation goldens and the headless AI games give the same state hashes
  before and after.
  - **Simulation.** Unit scripts that are only sleeping are skipped and caught
    up when touched; target searches skip whole 8×8-cell blocks with none of the
    wanted players and test their cheap conditions first; about a third of the
    per-update walks over every unit were merged into shared walks; unit and
    order records were packed into fewer cache lines; Legion's group records
    became flat tables; and navigation exploration and some per-update passes
    run on a small worker pool. In replay A/B pairs on recordings of
    10,000-unit games, the combat phase cost 19–26% less after the block skip
    alone and 3–36% less after the reordered target tests, and the per-update
    prologue 9–15% less after the merged walks (each measured against the step
    before it on an optimized debug build, not as one end-to-end figure).
  - **Display.** Live games now show a steady presentation clock over a ring of
    twelve snapshots instead of the newest snapshot. In the harness at 8× (a
    debug speed), the share of frozen frames fell from 49–100% to about 0.1% at
    60–240 Hz, and in an overloaded 8-AI Benchmark from about 88% to under
    0.5%. The jitter buffer now measures lateness at the current game speed.
  - **Slow clients.** Clients report the update they have processed every four
    updates, and the server leads each client by an amount scaled to its ping
    instead of a flat 120 updates. In the harness, a deliberately slowed client
    kept 21–31 updates of backlog instead of about 100 (protocol 249).
  - **Server pacing** is exact: normal speed runs at 30.0 updates a second (it
    was 30.3). On Windows servers, a speed change no longer leaves a deficit
    that is paid back in bursts afterwards.
  - **Stress starts** spread their units over the first second instead of
    spawning them all in the first update: the opening updates of a 15,000-unit
    stress start fell from about 24–54 ms to 3–8 ms.

  The figures are from [the WE baseline](we-baseline-2026-10-09.md) and the
  [timing-round validation](development.md#we-timing-rounds-toolswe_timingsh);
  they were measured on a shared development machine and are exploratory, not
  benchmarks.
- **Interface and platform.**
  - **Build-menu portraits follow Smooth GUI Art**: with the option on they are
    smoothed like the rest of the interface art, and toggling it applies at
    once.
  - **Max FPS is greyed out while VSync is on**, since the frame cap is not
    used then.
  - **VSync paces presentation.** Whenever VSync is on, the game waits for the
    display refresh itself if the driver returns early. This fixes a Benchmark
    running past the refresh rate and stuttering under Metal on macOS.
  - **Windows: Ignore Windows Display Scaling** (Options → Graphics, on by
    default, restart required). The game is no longer stretched blurrily by
    Windows display scaling; on the first run UI Scale and Cursor Size are
    raised to your Windows scale so the interface keeps its size.
- **Other changes and fixes.**
  - **Lodestone areas are visited nearest first**: after each mana spot the
    builder picks the closest one left, preferring spots it can walk to, and
    builders given overlapping areas split the spots.
  - **Full-mode override packs carry whole files.** Files over 4 KiB used to
    arrive truncated (for example a 68 KB model), so such packs failed to load.
    The package format is now TOV2; older packages are rejected rather than
    misread.
  - **Spectated games are recorded**, including all-AI watch games; such
    replays have no checkpoint hashes.
  - Spectators and replay viewers see every factory's build queue.
  - Class-based weapon impact sounds (sword, arrow, rock, cannon, fire and so
    on, including the Aramon catapult's) were silent and now play.
  - A game hosted on a macOS server now plays out exactly as on Linux or
    Windows: one compiler setting that let Apple Clang fuse floating-point
    operations in the server-side AI is now applied to every target.
  - On Windows, a corrupt JPEG no longer corrupts memory while it is decoded.
  - Replay checkpoints also carry a position digest (replay format 12), so a
    divergence report can tell a changed hash layout from a changed game.

**Compatibility:** protocol **253**, replay format **12**, generator version
**8**, campaign payload **4** and Crusades SQL schema **9** (generator, campaign
payload and schema unchanged since 0.7.27, checked in source against
`v0.7.27`). Update clients and servers together; 0.7.27 clients cannot join.
Replay playback requires the exact simulation protocol, so 0.7.27 recordings
(protocol 238) need 0.7.27. Generated-map recipes and saved `.kmp`
maps from 0.7.27 remain usable. Saved settings carry over; on Windows the new
display-scaling option starts on.

No new dynamic runtime dependencies or retail assets are included. See the
[release validation report](release-0.7.28-validation.md).
