# TAK Engine 1.0.5

TAK Engine is a modern, clean-room recreation of *Total Annihilation: Kingdoms*
(Cavedog, 1999), written in C++20 with SDL2, for Windows, macOS and Linux. It plays the
original game's skirmishes, campaigns and multiplayer from your own installation, and
comes with Cartographer, a map and scenario editor, and `takserver`, a dedicated
multiplayer server. 1.0.5 updates 1.0.4 with Volcano random maps, mountain peaks on
smaller maps and CCD pinning.

## What's new

- **The Volcano world for random maps**: *The Iron Plague*'s lava world, with two layouts
  of its own. **Lava Flows** runs rivers of lava out between the homes; **Industrial
  Zone** is lava everywhere but the plateaus, joined by a maze of walkways, with a mana
  spot at every dead end. Its **Lava pads** slider sets how many square pads stand cut
  off in the lava, each with a mana spot only flying builders can reach. Cartographer
  builds them too.
- **Mountain peaks on smaller random maps**: a map too small for a whole range gets a
  single compact peak, and where even that does not fit the recipe reads
  **MOUNTAINS: NO ROOM**.
- **Random map sliders that cannot change the map read NONE**, such as Forests and
  Hills on Volcano.
- **Pin to CCDs** (**Settings > Game > Performance**): on a processor with more than one
  CCD, such as many AMD Ryzen chips, the game and its local server each run on a CCD you
  choose, with each CCD's cores, caches and clock shown. `takserver` gets `--cpus`.
- **Spectators and replay viewers see kill counts** of the selected unit.

The [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.5/CHANGELOG.md) has the
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
| Windows x64 | `tak-engine-1.0.5-windows-x64-setup.exe` (installer), or `takclient-1.0.5-windows-x64.zip` |
| Windows 11 on ARM | `takclient-1.0.5-windows-arm64.zip` |
| macOS 14 or later, Apple Silicon | `tak-engine-1.0.5-macos-arm64.dmg`, or `takclient-1.0.5-macos-arm64.zip` |
| macOS 14 or later, Intel | `tak-engine-1.0.5-macos-x64.dmg`, or `takclient-1.0.5-macos-x64.zip` |
| Ubuntu 22.04 / 24.04 / 26.04 | `tak-engine-1.0.5-ubuntu<version>-amd64.deb` or `-arm64.deb` |
| Debian 12 / 13 | `tak-engine-1.0.5-debian<version>-amd64.deb` or `-arm64.deb` |
| Fedora 44 / 45 | `tak-engine-1.0.5-fedora<version>-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Leap 16.0 | `tak-engine-1.0.5-opensuse-leap16.0-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Tumbleweed | `tak-engine-1.0.5-opensuse-tumbleweed-x86_64.rpm` or `-aarch64.rpm` |
| RHEL, Rocky Linux, AlmaLinux 9 / 10 | `tak-engine-1.0.5-el<version>-x86_64.rpm` or `-aarch64.rpm` |
| Arch Linux (x64) | `tak-engine-1.0.5-1-x86_64.pkg.tar.zst` |
| Slackware64 15.0 | `tak-engine-1.0.5-x86_64-1_slack15.0.txz` |
| Any other Linux, Steam Deck | `tak-engine-1.0.5-x86_64.AppImage` or `tak-engine-1.0.5-aarch64.AppImage` |

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

1.0.5 uses network protocol **327**, replay format **15** and random map generator
version **10**. Random maps are built differently (peaks on smaller maps, the
Volcano layouts), so **it does not play with 1.0.4 or earlier, nor play their
replays**, and a random map recipe from 1.0.4 can build a different map in 1.0.5. Maps
already saved as map files are unchanged. Everyone in a game, and the server, needs
1.0.5.

The [quick start](https://github.com/pocketgeek/tak-engine/blob/v1.0.5/docs/quick-start.md),
the [user guide](https://github.com/pocketgeek/tak-engine/blob/v1.0.5/docs/user-guide.md)
and the [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.5/CHANGELOG.md)
cover the rest.

The tests and checks behind this release are in the
[validation report](release-1.0.5-validation.md).
