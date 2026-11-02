# TAK Engine 0.9.16

0.9.16 follows 0.9.15. Replays get real controls, watching a game becomes properly
read-only with each player's own interface, an infinite-queue builder's units start
their rally orders at once, and you can surrender. Game Select signs you back in with a
temporary token, and the Benchmark can run either pathfinder.

**Compatibility first.** A rallied unit's first order is part of the state the players
compare, so 0.9.16 uses network protocol **309** (replay format **15**, unchanged).
**0.9.16 does not play with 0.9.15 or any earlier version, and does not play their
replays.**

- **Everyone updates together.** A 0.9.16 player cannot join a 0.9.15 server, and a
  0.9.15 player cannot join a 0.9.16 server: the connection is refused with a *protocol
  version mismatch* message.
- **Old replays:** a replay recorded by 0.9.15 or earlier is refused with the version
  message. To keep watching them, keep your 0.9.15 install (0.9.0 to 0.9.15 stay on the
  GitHub releases page).
- **Players:** nothing to do. Settings, key bindings, maps and mod packs carry over.
- **Server operators:** update `takserver` to 0.9.16 when you announce the release (see
  [public-server.md](public-server.md#versions)).

## Replays

A new bar across the top has a timeline you can click or drag, rewind, play/pause, fast
forward, and speeds from 0.25x to 256x. The speed label also shows the real speed when a
machine cannot keep up, as in `32X (27X)`. **<<** and **>>** jump 30 seconds and repeat
while held. Space pauses, **+** and **-** change speed, **[** and **]** jump 30 seconds,
and Shift with them jumps 5 minutes. A jump lands on exactly the recorded game state.

## Watching a game

In replays and when spectating, you can select any unit, with no sound and no
acknowledgement, and no order can be given. The right side shows the selected unit's
player, with mana, buttons and build queues. T tracks the selection, and Ctrl and Alt
number groups are your own. With units from several players in a box, the owner with the
most units in it is kept. Nothing leaves the client, with one exception: the host of a
private single-player game watching the AIs keeps Pause and speed.

## Rally orders from infinite-queue builders

Units from a mobile builder on an infinite queue used to walk about 188-316 pixels south
first, around 12 seconds when the rally lay the other way. They now start their first
rally step at once, for every kind of rally. Production buildings already did this, as in
the original. A rally on a mobile builder is ours, not the original's.

When such a builder has no room for its next unit, it now moves one idle unit out of the
way, every half second, so production does not stop. This can move your own parked units
near it. An attack rally step a unit cannot do is now skipped, for factories too.

## Surrender and the result screen

**Surrender** in the game menu (click twice) ends your part in a game and shows the
defeat screen. Your units go the way the room's forfeit setting says, and everyone else
plays on. While you play, it replaces **Main Menu** and **Quit**; replays, spectating and
a finished game have **Main Menu** instead. Close the window to leave without a result
screen.

On the result screen the left door says **Title Screen**. The right door says **New
Game** in a single-player game, **Game Select** in multiplayer, and **Next Mission** or
**Retry** in a campaign. It does nothing after a campaign's last win.

## Sign-in tokens

Game Select takes you straight to the game list. After a password login over an encrypted
connection, the server gives the client a temporary token, held in memory only. It lasts
at most 12 hours from the password login, changes with each use, and is revoked when you
quit. The server stores only a hash, in memory. If it is missing or refused, you get the
normal password prompt. Plaintext servers never use tokens, so they keep the prompt.

## Also

- The Benchmark screen can run **Retail** or **Legion**, remembers your choice and shows
  the mode on its results. The results panel now fits small windows.
- The streaming panel's key can be rebound in **Controls** (it was fixed on F9).
- **START** says **STARTING...** as soon as you click it.

## Known limits

- Rewind points on very large Legion games take about 350 MB each and cost an estimated
  100-150 ms stall, more often at high speed, so far rewinds take longer.
- A unit caught mid-death when a jump lands replays its fall from the start. The game
  state is exact.
- A very short patrol rally (about 64 pixels) can still stop a mobile builder early.
- These were tried by simulated input only, or not run at all: a real Game Select click
  and its fallback, an in-game reconnect after a dropped link on a token session, a real
  Surrender in play, and the benchmark panel on a real GPU.
- All of 0.9.15's limits carry over (see
  [release-0.9.15-notes.md](release-0.9.15-notes.md#known-limits)).

Validation: [release-0.9.16-validation.md](release-0.9.16-validation.md).
