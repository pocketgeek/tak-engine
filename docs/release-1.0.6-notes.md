# TAK Engine 1.0.6

TAK Engine is a modern, clean-room recreation of *Total Annihilation: Kingdoms*
(Cavedog, 1999), written in C++20 with SDL2, for Windows, macOS and Linux. It plays the
original game's skirmishes, campaigns and multiplayer from your own installation, and
comes with Cartographer, a map and scenario editor, and `takserver`, a dedicated
multiplayer server. 1.0.6 updates 1.0.5 with fixes for what an audit of an 18-hour game
of eight Absurd computer players found.

## What's new

- **Computer players keep their economy running**: Normal, Hard and Absurd keep one
  factory for every 60 mana of income instead of more than they can feed, and still
  build lodestones when short. Hard and Absurd armies at 20 minutes are typically two to
  four times larger, Normal's about a fifth larger. Hard and Absurd also keep lodestones
  to a full store and a quarter of the unit cap, keep builders away from mana deposits
  beside enemy troops, and stop piling up unused scouts and Rocs.
- **Computer players' stuck units are sent again**: a unit standing for a minute on its
  way to an attack is stopped and re-ordered, minimum-range units are no longer
  re-ordered while they step back to fire, and charmers and Basilisks are no longer
  sent at targets they cannot take.
- **Queued orders work**: a queued attack (Retail pathfinding) heads straight for its
  next target instead of the top-left corner of the map, and a queued load is picked up
  when its turn comes.
- **A unit that changes owner is a new unit for everyone**, as in the original game:
  attack, guard and rally orders on it end.
- **Conjuring**: an accepted conjure order is always carried out; the conjurer and your
  idle units make room, and the ghost is red where a unit will not move.
- **Minimum-range units back off from every enemy that gets too close**, not just the
  first three.
- **No freeze when a factory makes its first unit of a new kind** on large maps.
- **The largest maps**: flyers no longer wrap to the far side at the east and south
  edges, and group moves, shots and builders behave there.
- **Watched games end when they are decided**, on the winner's result screen, and their
  replays end there too.
- **Long replays rewind and jump much faster**; the replay bar shows hours, and **Load
  Replay** lists only replays this version can play, with their length.
- **Legion pathfinding**: units no longer stand for good holding an order behind their
  own side's settled crowds or idle units, nor wait for a route on the largest maps.

The [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.6/CHANGELOG.md) has the
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
| Windows x64 | `tak-engine-1.0.6-windows-x64-setup.exe` (installer), or `takclient-1.0.6-windows-x64.zip` |
| Windows 11 on ARM | `takclient-1.0.6-windows-arm64.zip` |
| macOS 14 or later, Apple Silicon | `tak-engine-1.0.6-macos-arm64.dmg`, or `takclient-1.0.6-macos-arm64.zip` |
| macOS 14 or later, Intel | `tak-engine-1.0.6-macos-x64.dmg`, or `takclient-1.0.6-macos-x64.zip` |
| Ubuntu 22.04 / 24.04 / 26.04 | `tak-engine-1.0.6-ubuntu<version>-amd64.deb` or `-arm64.deb` |
| Debian 12 / 13 | `tak-engine-1.0.6-debian<version>-amd64.deb` or `-arm64.deb` |
| Fedora 44 / 45 | `tak-engine-1.0.6-fedora<version>-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Leap 16.0 | `tak-engine-1.0.6-opensuse-leap16.0-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Tumbleweed | `tak-engine-1.0.6-opensuse-tumbleweed-x86_64.rpm` or `-aarch64.rpm` |
| RHEL, Rocky Linux, AlmaLinux 9 / 10 | `tak-engine-1.0.6-el<version>-x86_64.rpm` or `-aarch64.rpm` |
| Arch Linux (x64) | `tak-engine-1.0.6-1-x86_64.pkg.tar.zst` |
| Slackware64 15.0 | `tak-engine-1.0.6-x86_64-1_slack15.0.txz` |
| Any other Linux, Steam Deck | `tak-engine-1.0.6-x86_64.AppImage` or `tak-engine-1.0.6-aarch64.AppImage` |

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

1.0.6 uses network protocol **330**, replay format **15** and random map generator
version **10**. Computer players and Legion pathfinding play differently, so **it does
not play with 1.0.5 or earlier, nor play their replays**. Random maps and saved map
files are unchanged. Everyone in a game, and the server, needs 1.0.6.

The [quick start](https://github.com/pocketgeek/tak-engine/blob/v1.0.6/docs/quick-start.md),
the [user guide](https://github.com/pocketgeek/tak-engine/blob/v1.0.6/docs/user-guide.md)
and the [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.6/CHANGELOG.md)
cover the rest.

The tests and checks behind this release are in the
[validation report](release-1.0.6-validation.md).
