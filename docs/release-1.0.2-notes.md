# TAK Engine 1.0.2

TAK Engine is a modern, clean-room recreation of *Total Annihilation: Kingdoms*
(Cavedog, 1999), written in C++20 with SDL2, for Windows, macOS and Linux. It plays the
original game's skirmishes, campaigns and multiplayer from your own installation, and
comes with Cartographer, a map and scenario editor, and `takserver`, a dedicated
multiplayer server. 1.0.2 is a small update to 1.0.1.

## What's new

- **Random map previews show the map.** The small preview of a random map, in the
  **Random map** tab and in the game room, was a rough sketch with coloured dots for
  trees, rocks and mana spots. It is now a picture made from the map's own terrain,
  like every other map's, and matches the full-size view you get by clicking it.
- **Random maps you played show a preview and a size.** A played random map is saved
  in your Maps folder and listed under **Existing map**, where it used to show *NO
  PREVIEW* and no size. New saves carry the map picture and size, as Cartographer's
  maps do, and ones saved by earlier versions get them when listed.

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
| Windows x64 | `tak-engine-1.0.2-windows-x64-setup.exe` (installer), or `takclient-1.0.2-windows-x64.zip` |
| Windows 11 on ARM | `takclient-1.0.2-windows-arm64.zip` |
| macOS 14 or later, Apple Silicon | `tak-engine-1.0.2-macos-arm64.dmg`, or `takclient-1.0.2-macos-arm64.zip` |
| macOS 14 or later, Intel | `tak-engine-1.0.2-macos-x64.dmg`, or `takclient-1.0.2-macos-x64.zip` |
| Ubuntu 22.04 / 24.04 / 26.04 | `tak-engine-1.0.2-ubuntu<version>-amd64.deb` or `-arm64.deb` |
| Debian 12 / 13 | `tak-engine-1.0.2-debian<version>-amd64.deb` or `-arm64.deb` |
| Fedora 44 / 45 | `tak-engine-1.0.2-fedora<version>-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Leap 16.0 | `tak-engine-1.0.2-opensuse-leap16.0-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Tumbleweed | `tak-engine-1.0.2-opensuse-tumbleweed-x86_64.rpm` or `-aarch64.rpm` |
| RHEL, Rocky Linux, AlmaLinux 9 / 10 | `tak-engine-1.0.2-el<version>-x86_64.rpm` or `-aarch64.rpm` |
| Arch Linux (x64) | `tak-engine-1.0.2-1-x86_64.pkg.tar.zst` |
| Slackware64 15.0 | `tak-engine-1.0.2-x86_64-1_slack15.0.txz` |
| Any other Linux, Steam Deck | `tak-engine-1.0.2-x86_64.AppImage` or `tak-engine-1.0.2-aarch64.AppImage` |

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

1.0.2 uses network protocol **324**, replay format **15** and random map generator
version **10**, like 1.0.1. **It plays with 1.0.1 and plays 1.0.1's replays**: only the
map previews and the saved copy of a played random map changed, not the game. It does
not play with 1.0.0.

The [quick start](https://github.com/pocketgeek/tak-engine/blob/v1.0.2/docs/quick-start.md),
the [user guide](https://github.com/pocketgeek/tak-engine/blob/v1.0.2/docs/user-guide.md)
and the [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.2/CHANGELOG.md)
cover the rest.

The tests and checks behind this release are in the
[validation report](release-1.0.2-validation.md).
