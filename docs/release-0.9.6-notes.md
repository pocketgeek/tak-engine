# TAK Engine 0.9.6

0.9.6 follows 0.9.5. It adds double-click select and rally orders for
production, brings native Wayland windows to Linux (KDE Plasma included),
makes cloaking and raising the dead follow the original game, and tidies
Crusades scenario handling, the mana bulb and the command-line tools.

**Compatibility first.** Rally plans, the cloak block and the revive channel
join the game state the players compare, so 0.9.6 uses network protocol
**300** (replay format **15**, unchanged). **0.9.6 does not play with 0.9.5 or
any earlier version, and does not play their replays.**

- **Everyone updates together.** A 0.9.6 player cannot join a 0.9.5 server, and
  a 0.9.5 player cannot join a 0.9.6 server: the connection is refused with a
  *protocol version mismatch* message. Update your game and tell the people you
  play with to update theirs.
- **Old replays:** a replay recorded by 0.9.5 or earlier is refused with the
  version message. To keep watching them, keep your 0.9.5 install (0.9.0 to
  0.9.5 stay on the GitHub releases page); versions before 0.9.0 are no longer
  offered for download and can be built from their git tags (see the
  [development guide](development.md)).
- **Players:** nothing to do. Settings, your data folder choice, key bindings,
  saved `.kmp` maps, generated-map recipes and mod packs carry over.
- **Linux players on a Wayland desktop (including KDE Plasma):** the first
  0.9.6 start opens a **native Wayland window** instead of an X11 one (see
  below). UI Scale and Cursor Size keep the same size on screen. If the native
  window misbehaves on your desktop, set Options, Display, **NATIVE WAYLAND**
  to **Off** and restart: the game returns to X11 and converts the sizes back.
- **Server operators:** update `takserver` to 0.9.6 when you announce the
  release; until you do, 0.9.6 players cannot use your server. Since 0.9.5 a
  server without `--map-cache-dir` keeps uploaded and random maps in
  `TAKengine/takserver` in its per-user data folder (see
  [public-server.md](public-server.md#versions)), and a server with neither a
  home folder nor `--map-cache-dir` refuses to start. A server started by hand
  for testing should get its own `XDG_DATA_HOME` or `--map-cache-dir`. New in
  0.9.6: a seat that is loading a game gets 30 s of silence before it is
  dropped (15 s in the lobby and in play), the server's own start-up work does
  not count as silence, and the log line for a dropped client says why.

## Double-click select

Double-click one of your units or buildings to select every unit of the same
type that is finished and drawn on screen (not under the panels). Shift adds
them to your selection, Ctrl removes them. Double-clicking an enemy or allied
unit selects just that one, and a quick third click on the same unit keeps the
wider selection. The original game has no double-click select. It is local to
your client; nothing is sent to other players.

## Rally orders for production

Production buildings, and builders producing on an endless queue (the Zhon
mobile builders), accept Move, Patrol, Attack and Move-fight as rally orders,
and Shift queues them like a unit's own orders. Every unit produced runs the
whole list in order; a Shift-queued patrol loops. Stop clears the rally, and
so does turning a mobile builder's repeat off. The original game offers only
Move and Patrol rallies, and only from buildings; Attack, Move-fight and
rallies from mobile builders are additions. A non-empty rally is part of the
game state, which is one reason for protocol 300.

## The mana bulb shows mana requested

The red number on the right of the mana bulb is what all your builders and
producers ask for each second, as in the original, instead of only what could
be paid. When mana runs short it can exceed the green income number. Computer
players (the Retail AI, and the Passive, Easy, Normal and Absurdly Defensive
levels) now judge a shortage the same way, comparing income with the mana
requested when deciding they need another lodestone.

## Cloaking and raising the dead

- **Cloaking spends the unit's own mana** (its blue bar), not your mana pool,
  so it no longer shows in the mana bulb. When the unit's mana runs out it
  becomes visible for three seconds and then cloaks again. The Trapdoor Spider,
  which has no separate moving cost, pays the original's much smaller cost
  while moving.
- **Fighting and working reveal a cloaked unit.** Each shot keeps it visible for
  20 seconds. Building, reclaiming or raising the dead keeps it visible while
  it works and for 10 seconds after; repairing for 5 seconds after. Enemies
  standing close no longer make a cloaked unit visible (the original has no such
  rule), and any movement, even turning on the spot, costs the moving rate.
- **Raising the dead costs no mana.** Resurrecting and animating no longer
  drain your pool or show in the bulb; a short mana supply slows them as it
  slows construction. Resurrection takes three tenths of the unit's build time
  and animation the creature's whole build time (they were the other way
  round). A computer player whose mana is well short abandons a raising at its
  end.

These are balance changes: cloakers and necromancers play differently from
0.9.5.

## Crusades and scenario maps

A scenario that gives stats to a custom unit type the Crusades balance lacks
(for example the Trapdoor Spider) now skips that type, as the original does,
instead of refusing to start. A scenario that *places* a unit Crusades lacks is
still refused, with a message naming the unit and telling you to turn CRUSADES
off. Test Map accepts Use Only entries from the other balance (they permit
nothing), and Cartographer's Check Map warns about units a Crusades game will
not have.

## Construction hit points

An unfinished building's hit points can no longer grow past its maximum, and a
hit on an unfinished retail-style site rewrites its build progress as the
original does.

## Native Wayland on Linux

Options, Display has a **NATIVE WAYLAND** setting (Auto, On, Off; restart
required). **Auto** opens a native Wayland window in any Wayland session (GNOME,
KDE Plasma, Sway, Hyprland and others), sharp and at the right size at 150% or
200% scaling. GNOME without the `libdecor` library keeps X11, since it would
have no title bar. **Off** restores the X11 window; **On** forces Wayland where
a Wayland desktop exists, and the game falls back to X11 if Wayland cannot
start. The game and Cartographer both follow the setting. Switching keeps UI
Scale and Cursor Size the same size on screen: the game measures whether its X11
window had real pixels or was enlarged by the desktop and converts them only in
the second case, so on KDE Plasma's default they stay as you set them. The
Wayland launcher icon uses the `tak-client` and `tak-cartographer` desktop
names.

## Command-line tools and test reliability

- `cartographer --version` and `--help` work; `cobtool`, `modeltool`,
  `gaftool`, `tnttool` and `tdftool` print usage on a missing or bad argument;
  `simperf` works with the current random-map recipe.
- Headless test runs keep their server seat while the simulation is slow and
  report the exact tick the result landed. The mod-transfer test no longer drops
  a player while loading, and the AI mod profile test reports where a stuck game
  stopped.

## For modders and mapmakers

- A scenario's custom unit types that the active balance does not have are
  skipped; placed units that do not exist are refused with their name.
- Cartographer's Check Map checks a second, Crusades registry.
- `cloakcostmoving` defaults to the (1/30-scaled) `cloakcost` when a unit file
  omits it, as the original loader does. No map, recipe or package format
  changed.

## For contributors

- Sim (protocol 300): producer rally plans in `stateHash` when set; cloak block
  `Unit::cloakBlockedUntil` (retail field +0xcc) hashed when set and written by
  `World::blockCloak` for shots, builds, reclaim, repair and resurrect;
  `Unit::reviveWork` is retail's 16.16 work counter (hashed while channelling,
  `_ftol` low word kept); `Player::allocation()` is a float, retail's resource
  +8. Revive pays and requests nothing. The AI shortfall reads
  `RetailResourceHistory{produced, requested}`.
- Client: `doubleclick.h` and its test; rally markers and cursors; the Wayland
  window kind is recorded in `settings.ini` (`displayMode`, last native scale,
  X11 desktop width) so conversions happen only when the unit changes.
- Server: 30 s loading allowance (`TAK_TIMEOUT_MS` and `TAK_LOAD_TIMEOUT_MS`
  scale both for tests), silence clocks shifted by the start-up stall.
- Harness: `harness_seat_load` (fails before), `cli_tools`, `hp_overflow`
  lethal-hit case, `server_load_timeout`.
- `--mpai` Inner Circle seed 1, `TAK_SPEED=80`: **the gate hashes change**, and
  only because the cloak block is now hashed. New values: Retail 60 s
  `d0da4ccc71d2067c`, 300 s `e820d37865085bd9`; Legion 60 s `c3f6a3a13f0c167f`,
  300 s `0783cb05f7f128cf`; Retail AI 60 s `4952493f51b9a568`; Crusades 300 s
  Retail `f2f51dc1703fc209`, Legion `c4189ae189dd68dc`. The cross-compiler golden
  (`check-determinism`) is unchanged, `dcef618cd2e4d558`. See the
  [validation report](release-0.9.6-validation.md).

## Known limits

New with 0.9.6:

- **Wayland was tested in a nested KWin 6.7.5 and by hand** on the maintainer's
  KDE laptop and 57-inch monitor; GNOME, Sway and Hyprland native windows were
  not run on real desktops, and the GNOME libdecor case is by code reading.
- **Rally, cloak, resurrect and animate orders were not exercised against a
  remote referee**: the AIs and the test autoplay never give them. Covered by
  unit tests and the hashed cloak-block timers every game runs.
- **Producer rallies:** Attack and Move-fight rallies from unarmed outputs are
  dropped, and a mobile builder holds a rally only while it repeats.

Everything listed under Known limits in the
[0.9.5 notes](release-0.9.5-notes.md#known-limits) still applies, except the
`mod_transfer` flake (fixed) and the construction-health overflow (fixed).

## Compatibility

Protocol **300** (was 299), replay format **15** and generator version **9**,
all unchanged except the protocol. Checked in source against `v0.9.5`:
`kNetVersion` in `src/net/protocol.h` and `kReplayFormat` in
`src/net/replayhdr.h`.

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.6 clients and servers play only with each other**.
- **Replays.** Playback requires the recording's protocol to equal the engine's,
  so 0.9.6 plays only 0.9.6 replays; a 0.9.5 replay (format 15, protocol 299) is
  refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes, pack
  folders and settings files carry over. `settings.ini` gains Wayland window
  records that 0.9.5 ignores.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.9.6-validation.md).
