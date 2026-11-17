# TAK Engine 1.0.7

TAK Engine is a modern, clean-room recreation of *Total Annihilation: Kingdoms*
(Cavedog, 1999), written in C++20 with SDL2, for Windows, macOS and Linux. It plays the
original game's skirmishes, campaigns and multiplayer from your own installation, and
comes with Cartographer, a map and scenario editor, and `takserver`, a dedicated
multiplayer server. 1.0.7 updates 1.0.6: flyers hit with their attack runs, status
shots stop wasting mana on the unit in the way, and with Legion pathfinding fast units
pass slow ones and arrive first.

## What's new

- **Flyers on attack runs aim each shot when they release it**, not half a second
  before: in testing a Gryphon hit with 12 of 12 spears instead of 4, and a Fallen Angel
  with 11 of 11 instead of none. The Dirigible's and Juggernaut's arrows at flyers are
  aimed the same way. The original game aimed early; this is TAK Engine's own.
- **Charms, Turn to Stone and freeze shots no longer waste mana on a unit in the way**:
  a shot that strikes a building, a Monarch, an immune unit or your own or an ally's unit
  counts as blocked, and after three a Harpy or a Creon dragon on Freezing Breath picks
  another target (an ordered attack is dropped); a ground caster such as the Mind Mage
  moves to another spot first, then gives up.
- **Legion pathfinding: fast units overtake slow ones** of yours or an ally's walking
  the same way when there is room beside, instead of stopping and starting behind them.
  In a recorded game the Aramon Knights' stops on the march fell by over 80%.
- **Legion pathfinding: an order of fast and slow units arrives fast units first.** An
  order of up to 48 units of clearly different speeds, not an Alt+N formation, is laid
  out with the fast units in rows at the place you clicked and the slow units behind
  them, each unit on its own spot; the slow units let the fast ones by on the way. On
  most orders the fast units get there clearly earlier than before.
- **Legion pathfinding: you see where each unit will stand.** A move, fight-move or
  patrol order briefly outlines each of your units' places in your colour; hold Shift to
  keep them shown until the units arrive.

The [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.7/CHANGELOG.md) has the
details.

## What you need

**Your own copy of Total Annihilation: Kingdoms.** No game assets are included: the
engine reads the maps, models, scripts, interface art, sound and music straight from
your installation folder, nothing extracted. An installation of *Kingdoms + The Iron
Plague*, such as the GOG edition, supplies everything. Without *The Iron Plague* you
have the four original sides and the base campaign: the Creon side and its campaign
need the `IP*.hpi` archives. At first start the game asks for the folder that holds
`data.hpi`, `terrain.hpi`, `sections.hpi` and `maps.hpi`.

## Downloads

Pick the file for your system below. Each package holds the game, Cartographer and
`takserver`.

| System | File |
| --- | --- |
| Windows x64 | `tak-engine-1.0.7-windows-x64-setup.exe` (installer), or `takclient-1.0.7-windows-x64.zip` |
| Windows 11 on ARM | `takclient-1.0.7-windows-arm64.zip` |
| macOS 14 or later, Apple Silicon | `tak-engine-1.0.7-macos-arm64.dmg`, or `takclient-1.0.7-macos-arm64.zip` |
| macOS 14 or later, Intel | `tak-engine-1.0.7-macos-x64.dmg`, or `takclient-1.0.7-macos-x64.zip` |
| Ubuntu 22.04 / 24.04 / 26.04 | `tak-engine-1.0.7-ubuntu<version>-amd64.deb` or `-arm64.deb` |
| Debian 12 / 13 | `tak-engine-1.0.7-debian<version>-amd64.deb` or `-arm64.deb` |
| Fedora 44 / 45 | `tak-engine-1.0.7-fedora<version>-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Leap 16.0 | `tak-engine-1.0.7-opensuse-leap16.0-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Tumbleweed | `tak-engine-1.0.7-opensuse-tumbleweed-x86_64.rpm` or `-aarch64.rpm` |
| RHEL, Rocky Linux, AlmaLinux 9 / 10 | `tak-engine-1.0.7-el<version>-x86_64.rpm` or `-aarch64.rpm` |
| Arch Linux (x64) | `tak-engine-1.0.7-1-x86_64.pkg.tar.zst` |
| Slackware64 15.0 | `tak-engine-1.0.7-x86_64-1_slack15.0.txz` |
| Any other Linux, Steam Deck | `tak-engine-1.0.7-x86_64.AppImage` or `tak-engine-1.0.7-aarch64.AppImage` |

The `-debug` downloads (`takclient-windows-<arch>-debug.zip`,
`takclient-macos-<arch>-debug.zip`, `tak-engine-linux-<arch>-debug.tar.gz`) are for
diagnosing problems, not for playing. The Windows installer and executables are signed;
the macOS apps and DMGs are signed and notarized by Apple. Libraries the game needs are
bundled: your system provides only graphics, sound and windowing.

- **Windows:** run the installer, which adds **Total Annihilation - Kingdoms** and **TAK
  Cartographer** to the Start menu, or unpack the ZIP anywhere and run `takclient.exe`.
- **macOS:** open the DMG and drag **Total Annihilation - Kingdoms** (and **TAK
  Cartographer**) to Applications. From the ZIP, launch the `.app`.
- **Linux:** install the package as administrator (`apt install ./….deb`,
  `dnf install ./….rpm`, `zypper install ./….rpm`, `pacman -U ./….pkg.tar.zst` or
  `upgradepkg --install-new ./….txz`), or make the AppImage executable and run it; given
  `cartographer` or `takserver` as its first argument, it starts those.

## Known limits

- Legion pathfinding, orders of fast and slow units: on a short move, fast units that
  start behind or among slow ones walk round them to the front rows and arrive later
  than before, and the slow units, waiting for them, may finish later too. Knights
  starting behind slower rows sometimes stop short of their spots. On a long single-file
  column of knights and swordsmen, on a field or a road, the layout itself gains nothing:
  the knights already pass on the way and arrive when they would without it. Mid-speed units sent with knights and swordsmen may arrive later. Clicked
  just past a gate or a bridge, and for three speeds against a wall or in a gap, the
  order keeps its ordinary formation and its fast units gain nothing from the layout there. Orders of
  more than 48 units are not laid out, and in orders of more than 64 fast units still
  follow slow ones of the same order.
- A flyer that re-aims at its release is not limited against its own heading: past its
  target it can throw sideways or backwards.
- Hard and Absurd computer players have no base-defence routine, and their 32-factory
  ceiling can leave income unspent. A Normal player at its limit of seven lodestones
  can still run its store empty and wait on it.
- A game with no human player paces itself to its slowest watcher, and a watched game
  that cannot be decided runs until the watchers leave.
- Playing a replay straight through stops taking rewind points once one no longer fits
  beside the start (only with replay memory budgets under about 420 MB).
- After a unit is charmed, players at war with it pick it up again only once they see
  it. A unit given to an ally arrives without its experience and with an empty mana
  pool.
- The first route search in a part of a map still costs a one-off tick of 21 to 50 ms
  now and then.
- Legion pathfinding: units behind others that are attacking or guarding in a gap can
  still wait there for good; a unit cut off from its goal ends its order if the ground
  around it changes before the way opens again; armies of more than 48 units in one
  order still take a whole-map route on a 2048x2048 map; Legion movement costs about 4
  to 5% more simulation time.
- The original game turns a flyer on a move order back onto the map from beyond its
  edge (`VTOL_Move`); that is not ported.

## Compatibility

1.0.7 uses network protocol **334**, replay format **15** and random map generator
version **10**. Flyers, status shots and Legion pathfinding play differently, so **it
does not play with 1.0.6 or earlier, nor play their replays**. Random maps and saved map
files are unchanged. Everyone in a game, and the server, needs 1.0.7.

The [quick start](https://github.com/pocketgeek/tak-engine/blob/v1.0.7/docs/quick-start.md),
the [user guide](https://github.com/pocketgeek/tak-engine/blob/v1.0.7/docs/user-guide.md)
and the [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.7/CHANGELOG.md)
cover the rest.

The tests and checks behind this release are in the
[validation report](release-1.0.7-validation.md).
