# TAK Engine 1.0.4

TAK Engine is a modern, clean-room recreation of *Total Annihilation: Kingdoms*
(Cavedog, 1999), written in C++20 with SDL2, for Windows, macOS and Linux. It plays the
original game's skirmishes, campaigns and multiplayer from your own installation, and
comes with Cartographer, a map and scenario editor, and `takserver`, a dedicated
multiplayer server. 1.0.4 updates 1.0.3 with stances for new units and clearer
production and lodestone building.

## What's new

- **Choose the stance of new units.** A production building, and a mobile builder on
  infinite production, shows the **Offensive**, **Defensive** and **Passive** buttons. Click one and every unit made from then on starts with that stance; click it
  again to go back to each unit's own. None is lit at first.
- **A repeating builder's units spread out** on a ring round it, as units leaving a
  production building do, instead of each taking one step south.
- **Lodestone areas show every lodestone**: a faded lodestone on every spot the builder
  is going to build on, not just the next one.
- **Fast units in a formation walk properly** under Legion pathfinding, instead of
  sliding along at a slow member's pace with their legs still.
- **Infinite production reads +++** on the **TRAINING** line, as the unit's icon does.

The [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.4/CHANGELOG.md) has the
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
| Windows x64 | `tak-engine-1.0.4-windows-x64-setup.exe` (installer), or `takclient-1.0.4-windows-x64.zip` |
| Windows 11 on ARM | `takclient-1.0.4-windows-arm64.zip` |
| macOS 14 or later, Apple Silicon | `tak-engine-1.0.4-macos-arm64.dmg`, or `takclient-1.0.4-macos-arm64.zip` |
| macOS 14 or later, Intel | `tak-engine-1.0.4-macos-x64.dmg`, or `takclient-1.0.4-macos-x64.zip` |
| Ubuntu 22.04 / 24.04 / 26.04 | `tak-engine-1.0.4-ubuntu<version>-amd64.deb` or `-arm64.deb` |
| Debian 12 / 13 | `tak-engine-1.0.4-debian<version>-amd64.deb` or `-arm64.deb` |
| Fedora 44 / 45 | `tak-engine-1.0.4-fedora<version>-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Leap 16.0 | `tak-engine-1.0.4-opensuse-leap16.0-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Tumbleweed | `tak-engine-1.0.4-opensuse-tumbleweed-x86_64.rpm` or `-aarch64.rpm` |
| RHEL, Rocky Linux, AlmaLinux 9 / 10 | `tak-engine-1.0.4-el<version>-x86_64.rpm` or `-aarch64.rpm` |
| Arch Linux (x64) | `tak-engine-1.0.4-1-x86_64.pkg.tar.zst` |
| Slackware64 15.0 | `tak-engine-1.0.4-x86_64-1_slack15.0.txz` |
| Any other Linux, Steam Deck | `tak-engine-1.0.4-x86_64.AppImage` or `tak-engine-1.0.4-aarch64.AppImage` |

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

1.0.4 uses network protocol **326**, replay format **15** and random map generator
version **10**. The stance and parking changes alter the game, so **it does not play with
1.0.3 or earlier, nor play their replays**. Everyone in a game, and the server, needs
1.0.4.

The [quick start](https://github.com/pocketgeek/tak-engine/blob/v1.0.4/docs/quick-start.md),
the [user guide](https://github.com/pocketgeek/tak-engine/blob/v1.0.4/docs/user-guide.md)
and the [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.4/CHANGELOG.md)
cover the rest.

The tests and checks behind this release are in the
[validation report](release-1.0.4-validation.md).
