# TAK Engine 1.0.1

TAK Engine is a modern, clean-room recreation of *Total Annihilation: Kingdoms*
(Cavedog, 1999), written in C++20 with SDL2, for Windows, macOS and Linux. It plays the
original game's skirmishes, campaigns and multiplayer from your own installation, and
comes with Cartographer, a map and scenario editor, and `takserver`, a dedicated
multiplayer server. 1.0.1 is a small update to 1.0.0.

## What's new

- **Ships and archers no longer shoot at what they cannot hit.** A Mer Warrior swimming
  under the surface used to draw endless volleys that all splashed short. A ranged
  weapon that cannot reach under water now leaves such a unit alone, and an attack
  order on it is refused; on a shore or in shallows, where it stands above the
  surface, it is shot as before.
- **Wrecks and bodies in water decay like those on land**, instead of staying for the
  whole game.
- **Bodies sink and fade as they go**: a decaying body settles into the ground and fades
  over its last two seconds instead of vanishing at once.
- **No frozen sparkles on bodies:** a unit killed while being built or conjured no
  longer leaves its construction sparkles hanging around its body.

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
| Windows x64 | `tak-engine-1.0.1-windows-x64-setup.exe` (installer), or `takclient-1.0.1-windows-x64.zip` |
| Windows 11 on ARM | `takclient-1.0.1-windows-arm64.zip` |
| macOS 14 or later, Apple Silicon | `tak-engine-1.0.1-macos-arm64.dmg`, or `takclient-1.0.1-macos-arm64.zip` |
| macOS 14 or later, Intel | `tak-engine-1.0.1-macos-x64.dmg`, or `takclient-1.0.1-macos-x64.zip` |
| Ubuntu 22.04 / 24.04 / 26.04 | `tak-engine-1.0.1-ubuntu<version>-amd64.deb` or `-arm64.deb` |
| Debian 12 / 13 | `tak-engine-1.0.1-debian<version>-amd64.deb` or `-arm64.deb` |
| Fedora 44 / 45 | `tak-engine-1.0.1-fedora<version>-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Leap 16.0 | `tak-engine-1.0.1-opensuse-leap16.0-x86_64.rpm` or `-aarch64.rpm` |
| openSUSE Tumbleweed | `tak-engine-1.0.1-opensuse-tumbleweed-x86_64.rpm` or `-aarch64.rpm` |
| RHEL, Rocky Linux, AlmaLinux 9 / 10 | `tak-engine-1.0.1-el<version>-x86_64.rpm` or `-aarch64.rpm` |
| Arch Linux (x64) | `tak-engine-1.0.1-1-x86_64.pkg.tar.zst` |
| Slackware64 15.0 | `tak-engine-1.0.1-x86_64-1_slack15.0.txz` |
| Any other Linux, Steam Deck | `tak-engine-1.0.1-x86_64.AppImage` or `tak-engine-1.0.1-aarch64.AppImage` |

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

1.0.1 uses network protocol **324**, replay format **15** and random map generator
version **10**. **It does not play with 1.0.0 and does not play 1.0.0's replays**: the
water and targeting changes alter the game. A server and all of its players must run
1.0.1.

The [quick start](https://github.com/pocketgeek/tak-engine/blob/v1.0.1/docs/quick-start.md),
the [user guide](https://github.com/pocketgeek/tak-engine/blob/v1.0.1/docs/user-guide.md)
and the [CHANGELOG](https://github.com/pocketgeek/tak-engine/blob/v1.0.1/CHANGELOG.md)
cover the rest.

The tests and checks behind this release are in the
[validation report](release-1.0.1-validation.md).
