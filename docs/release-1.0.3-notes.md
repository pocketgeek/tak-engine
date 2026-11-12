# TAK Engine 1.0.3

TAK Engine is a modern, clean-room recreation of *Total Annihilation: Kingdoms*
(Cavedog, 1999), written in C++20 with SDL2, for Windows, macOS and Linux. It plays the
original game's skirmishes, campaigns and multiplayer from your own installation, and
comes with Cartographer, a map and scenario editor, and `takserver`, a dedicated
multiplayer server. 1.0.3 updates 1.0.2: units no longer keep attacking what they
cannot hurt.

## What's new

Units no longer keep attacking something they can never hurt. Most of these changes
differ from the original game, which kept firing.

- **Shots that keep landing short end the attack, or the unit moves.** A unit whose
  shots stop in a bank, a wall's foot or a slope it cannot see past walks or sails to
  a spot with a clear shot, or gives the target up and picks another. Ships no longer
  empty their quivers into the bank under a unit on a raised shore.
- **Units back away from targets inside their minimum range**, such as a Catapult or a
  Trebuchet Ship with an enemy too close to shoot, unless they are on Hold Position.
- **Weapon-switching units only take attacks their selected weapon can make**, and the
  computer picks weapons that work: no more Mage Archers switching weapons without
  firing, and a computer's Fallen Angel uses its Fire Bomb on ships.
- **Stone, freeze and mind control are not wasted** on units and buildings they cannot
  affect.
- **Flyers on Hold Position take off to fight.** A landed Thirsha, Iron Beak or Taros
  priest rises over its spot, turns to the enemy and fires, without chasing. Passive
  flyers stay down.
- **The Wrath of Tammuz's storm reaches boats**, and **the Creon Submersible can be hit
  from any side**.
- **Custom music no longer changes the title screen**: it plays only in games.

The [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.3/CHANGELOG.md) has the
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
| Windows x64 | `tak-engine-1.0.3-windows-x64-setup.exe` (installer), or `takclient-1.0.3-windows-x64.zip` |
| Windows 11 on ARM | `takclient-1.0.3-windows-arm64.zip` |
| macOS 14 or later, Apple Silicon | `tak-engine-1.0.3-macos-arm64.dmg`, or `takclient-1.0.3-macos-arm64.zip` |
| macOS 14 or later, Intel | `tak-engine-1.0.3-macos-x64.dmg`, or `takclient-1.0.3-macos-x64.zip` |
| Ubuntu 22.04 / 24.04 / 26.04 | `tak-engine-1.0.3-ubuntu<version>-amd64.deb` or `-arm64.deb` |
| Debian 12 / 13 | `tak-engine-1.0.3-debian<version>-amd64.deb` or `-arm64.deb` |
| Fedora 44 / 45 | `tak-engine-1.0.3-fedora<version>-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Leap 16.0 | `tak-engine-1.0.3-opensuse-leap16.0-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Tumbleweed | `tak-engine-1.0.3-opensuse-tumbleweed-x86_64.rpm` or `-aarch64.rpm` |
| RHEL, Rocky Linux, AlmaLinux 9 / 10 | `tak-engine-1.0.3-el<version>-x86_64.rpm` or `-aarch64.rpm` |
| Arch Linux (x64) | `tak-engine-1.0.3-1-x86_64.pkg.tar.zst` |
| Slackware64 15.0 | `tak-engine-1.0.3-x86_64-1_slack15.0.txz` |
| Any other Linux, Steam Deck | `tak-engine-1.0.3-x86_64.AppImage` or `tak-engine-1.0.3-aarch64.AppImage` |

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

## Compatibility

1.0.3 uses network protocol **325**, replay format **15** and random map generator
version **10**. The changes alter how units fight, so **it does not play with 1.0.2 or
earlier, nor play their replays**. Everyone in a game, and the server, needs 1.0.3.

The [quick start](https://github.com/pocketgeek/tak-engine/blob/v1.0.3/docs/quick-start.md),
the [user guide](https://github.com/pocketgeek/tak-engine/blob/v1.0.3/docs/user-guide.md)
and the [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.3/CHANGELOG.md)
cover the rest.

The tests and checks behind this release are in the
[validation report](release-1.0.3-validation.md).
