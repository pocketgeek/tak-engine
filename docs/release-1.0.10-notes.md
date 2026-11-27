# TAK Engine 1.0.10

TAK Engine is a modern, clean-room recreation of *Total Annihilation: Kingdoms*
(Cavedog, 1999), written in C++20 with SDL2, for Windows, macOS and Linux. It plays the
original game's skirmishes, campaigns and multiplayer from your own installation, and
comes with Cartographer, a map and scenario editor, and `takserver`, a dedicated
multiplayer server. 1.0.10 updates 1.0.9: Legion pathfinding does far less work placing
units that surround a target, and two guards of a factory no longer hold each other in its
doorway.

## What's new

- **Legion pathfinding: much less work placing units that surround a target.** Handing
  out spots to many units ringing one target, long-range attackers above all, repeated
  the same searches hundreds of times. Every unit still gets the spot it got before, so
  games play out exactly as in 1.0.9; big fights with archers and other long-range units
  should hitch less. (ta-engine, a sister project on the same code, measured its
  Benchmark's worst tick falling from 1.3 seconds to 47 ms.)
- **Two guards of a factory no longer hold each other in its doorway.** Guards stepping
  aside for a new unit could each be sent behind the other and walk out and back for the
  rest of the game. Now a guard held up while stepping aside stops once the door is clear
  and goes back to guarding.
- **Every game is built from a clean start**, as the server builds it, so nothing from
  the menu's map can carry over into a match.

The [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.10/CHANGELOG.md) has the
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
| Windows x64 | `tak-engine-1.0.10-windows-x64-setup.exe` (installer), or `takclient-1.0.10-windows-x64.zip` |
| Windows 11 on ARM | `takclient-1.0.10-windows-arm64.zip` |
| macOS 14 or later, Apple Silicon | `tak-engine-1.0.10-macos-arm64.dmg`, or `takclient-1.0.10-macos-arm64.zip` |
| macOS 14 or later, Intel | `tak-engine-1.0.10-macos-x64.dmg`, or `takclient-1.0.10-macos-x64.zip` |
| Ubuntu 22.04 / 24.04 / 26.04 | `tak-engine-1.0.10-ubuntu<version>-amd64.deb` or `-arm64.deb` |
| Debian 12 / 13 | `tak-engine-1.0.10-debian<version>-amd64.deb` or `-arm64.deb` |
| Fedora 44 / 45 | `tak-engine-1.0.10-fedora<version>-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Leap 16.0 | `tak-engine-1.0.10-opensuse-leap16.0-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Tumbleweed | `tak-engine-1.0.10-opensuse-tumbleweed-x86_64.rpm` or `-aarch64.rpm` |
| RHEL, Rocky Linux, AlmaLinux 9 / 10 | `tak-engine-1.0.10-el<version>-x86_64.rpm` or `-aarch64.rpm` |
| Arch Linux (x64) | `tak-engine-1.0.10-1-x86_64.pkg.tar.zst` |
| Slackware64 15.0 | `tak-engine-1.0.10-x86_64-1_slack15.0.txz` |
| Any other Linux, Steam Deck | `tak-engine-1.0.10-x86_64.AppImage` or `tak-engine-1.0.10-aarch64.AppImage` |

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

1.0.10 uses network protocol **337**, replay format **15** and random map generator
version **10**. The factory guard change can change how a game turns out, so **it does
not play with 1.0.9 or earlier, nor play their replays**. Random maps and saved map files
are unchanged. Everyone in a game, and the server, needs 1.0.10.

The [quick start](https://github.com/pocketgeek/tak-engine/blob/v1.0.10/docs/quick-start.md),
the [user guide](https://github.com/pocketgeek/tak-engine/blob/v1.0.10/docs/user-guide.md)
and the [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.10/CHANGELOG.md)
cover the rest.

The tests and checks behind this release are in the
[validation report](release-1.0.10-validation.md).
