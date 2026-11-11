# TAK Engine 1.0.0

TAK Engine is a modern, clean-room recreation of *Total Annihilation: Kingdoms*
(Cavedog, 1999), written in C++20 with SDL2, for Windows, macOS and Linux. It plays the
original game's skirmishes, campaigns and multiplayer from your own installation, and
comes with Cartographer, a map and scenario editor, and `takserver`, a dedicated
multiplayer server. 1.0.0 is its first public release.

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
| Windows x64 | `tak-engine-1.0.0-windows-x64-setup.exe` (installer), or `takclient-1.0.0-windows-x64.zip` |
| Windows 11 on ARM | `takclient-1.0.0-windows-arm64.zip` |
| macOS 14 or later, Apple Silicon | `tak-engine-1.0.0-macos-arm64.dmg`, or `takclient-1.0.0-macos-arm64.zip` |
| macOS 14 or later, Intel | `tak-engine-1.0.0-macos-x64.dmg`, or `takclient-1.0.0-macos-x64.zip` |
| Ubuntu 22.04 / 24.04 / 26.04 | `tak-engine-1.0.0-ubuntu<version>-amd64.deb` or `-arm64.deb` |
| Debian 12 / 13 | `tak-engine-1.0.0-debian<version>-amd64.deb` or `-arm64.deb` |
| Fedora 44 / 45 | `tak-engine-1.0.0-fedora<version>-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Leap 16.0 | `tak-engine-1.0.0-opensuse-leap16.0-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Tumbleweed | `tak-engine-1.0.0-opensuse-tumbleweed-x86_64.rpm` or `-aarch64.rpm` |
| RHEL, Rocky Linux, AlmaLinux 9 / 10 | `tak-engine-1.0.0-el<version>-x86_64.rpm` or `-aarch64.rpm` |
| Arch Linux (x64) | `tak-engine-1.0.0-1-x86_64.pkg.tar.zst` |
| Slackware64 15.0 | `tak-engine-1.0.0-x86_64-1_slack15.0.txz` |
| Any other Linux, Steam Deck | `tak-engine-1.0.0-x86_64.AppImage` or `tak-engine-1.0.0-aarch64.AppImage` |

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

## What 1.0.0 offers

- **Skirmish** on up to eight seats with all five sides, teams, Retail or Crusades
  balance and eight computer-player levels, including **RETAIL**, the original game's
  own AI.
- **Random maps** in five themed layouts and three classic ones, from 8 to 64 sections
  a side, with plateaus, mountains and towns built from the original terrain pieces.
- **Two pathfinding modes:** Retail, as in 1999 (the default), and Legion, for large
  armies.
- **Multiplayer** through a server, with accounts, spectators, voice chat, a disconnect
  wait with computer takeover, and automatic map and mod transfer; or on your own
  network with **LOCAL SERVER**, no accounts needed. The host sets the game up in the
  game room.
- **Campaigns:** *The Book of Darien* (48 missions) and *The Iron Plague* (25, with its
  alternate ending).
- **Replays** of every game you play or watch.
- **Mods** for art and sound, or for units and rules, and custom music for any of the
  original tracks.
- **Cartographer**, a port of the original map editor, with scenario and AI-profile
  editing and a random map generator.
- **YouTube streaming** built in, and a repeatable **Benchmark**; the simulation can use
  as many processor threads as you allow it.

The [quick start](https://github.com/pocketgeek/tak-engine/blob/v1.0.0/docs/quick-start.md)
and the [user guide](https://github.com/pocketgeek/tak-engine/blob/v1.0.0/docs/user-guide.md)
cover the rest.

## Compatibility

1.0.0 uses network protocol **323**, replay format **15** and random map generator
version **10**. **It plays with no earlier version and plays none of their replays.** A
server and all of its players must run 1.0.0.

What changed since the last development version, 0.9.24, is listed in the
[CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.0/CHANGELOG.md).

The tests and checks behind this release are in the
[validation report](release-1.0.0-validation.md).
