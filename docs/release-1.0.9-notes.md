# TAK Engine 1.0.8

TAK Engine is a modern, clean-room recreation of *Total Annihilation: Kingdoms*
(Cavedog, 1999), written in C++20 with SDL2, for Windows, macOS and Linux. It plays the
original game's skirmishes, campaigns and multiplayer from your own installation, and
comes with Cartographer, a map and scenario editor, and `takserver`, a dedicated
multiplayer server. 1.0.8 updates 1.0.7: with Legion pathfinding, an order to thousands
of units at once no longer makes the game stutter, and very large armies take less
processor time.

## What's new

- **Legion pathfinding: an order to a very large army no longer makes the game
  stutter.** With 8,500 units under one order, 21 game ticks used to run late while the
  route was worked out, the slowest taking 161 ms where a tick has 33 ms at normal
  speed. Now the slowest takes 25 ms and none runs late.
- **Legion pathfinding: marches of thousands of units take less processor time.** In our
  measurements the average tick with 10,000 units marching over open ground falls from
  25.2 ms to 20.5 ms, with 5,000 units from 10.9 ms to 9.2 ms, and with 1,000 units it
  is 13% shorter through a maze. These shortcuts by themselves change nothing in how
  units move. In a game of the usual size, up to a few thousand units, you will not
  notice a difference.
- **Legion pathfinding: three corrections**, none of them visible in play: a unit could
  fail to step aside for one tick after two allies swapped places just ahead of it, a
  unit before a narrow passage could take its lane up to a second late, and the room a
  crowd counts on at its destination was not measured again when a building, a wreck or
  a corpse appeared or was cleared there. In our tests of crowds of 1,000 units and more
  that meet or cross each other, the first of them changes how some turn out, sometimes
  better and sometimes worse, so 1.0.8 has a new protocol number.

The [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.8/CHANGELOG.md) has the
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
| Windows x64 | `tak-engine-1.0.8-windows-x64-setup.exe` (installer), or `takclient-1.0.8-windows-x64.zip` |
| Windows 11 on ARM | `takclient-1.0.8-windows-arm64.zip` |
| macOS 14 or later, Apple Silicon | `tak-engine-1.0.8-macos-arm64.dmg`, or `takclient-1.0.8-macos-arm64.zip` |
| macOS 14 or later, Intel | `tak-engine-1.0.8-macos-x64.dmg`, or `takclient-1.0.8-macos-x64.zip` |
| Ubuntu 22.04 / 24.04 / 26.04 | `tak-engine-1.0.8-ubuntu<version>-amd64.deb` or `-arm64.deb` |
| Debian 12 / 13 | `tak-engine-1.0.8-debian<version>-amd64.deb` or `-arm64.deb` |
| Fedora 44 / 45 | `tak-engine-1.0.8-fedora<version>-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Leap 16.0 | `tak-engine-1.0.8-opensuse-leap16.0-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Tumbleweed | `tak-engine-1.0.8-opensuse-tumbleweed-x86_64.rpm` or `-aarch64.rpm` |
| RHEL, Rocky Linux, AlmaLinux 9 / 10 | `tak-engine-1.0.8-el<version>-x86_64.rpm` or `-aarch64.rpm` |
| Arch Linux (x64) | `tak-engine-1.0.8-1-x86_64.pkg.tar.zst` |
| Slackware64 15.0 | `tak-engine-1.0.8-x86_64-1_slack15.0.txz` |
| Any other Linux, Steam Deck | `tak-engine-1.0.8-x86_64.AppImage` or `tak-engine-1.0.8-aarch64.AppImage` |

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
  order still take a whole-map route on a 2048x2048 map.
- The original game turns a flyer on a move order back onto the map from beyond its
  edge (`VTOL_Move`); that is not ported.

## Compatibility

1.0.8 uses network protocol **335**, replay format **15** and random map generator
version **10**. The three Legion corrections can change how a game turns out, so **it
does not play with 1.0.7 or earlier, nor play their replays**. Random maps and saved map
files are unchanged. Everyone in a game, and the server, needs 1.0.8.

The [quick start](https://github.com/pocketgeek/tak-engine/blob/v1.0.8/docs/quick-start.md),
the [user guide](https://github.com/pocketgeek/tak-engine/blob/v1.0.8/docs/user-guide.md)
and the [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.8/CHANGELOG.md)
cover the rest.

The tests and checks behind this release are in the
[validation report](release-1.0.8-validation.md).
