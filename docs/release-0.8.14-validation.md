# 0.8.14 release validation

Protocol **293** (unchanged from 0.8.13 and 0.8.12), replay format **14**,
generator version **9**, campaign payload **4**, database schema **9**.

Tagged `v0.8.14` at `23ad7a5f` (full `23ad7a5fe2f52683a51f53e35b4d7bab18ca3662`, annotated tag object `072fe304c7747b7674ab15039341cffc078e133e`, "TAK Engine 0.8.14") and published on 2026-10-24 15:01 UTC.

## Scope and compatibility

0.8.14 follows 0.8.13 (protocol 293). After the 0.8.13 validation commit
`369dee1d`, main reached `5fa673d0` in 2 commits, both linear (no merges):
`8d92c6b5` (always-enabled mod folders; no GAME MODS switch in multiplayer) and
`5fa673d0` (its review fixes).
`git diff --stat v0.8.13..5fa673d0 -- src` touches 21 files:
`src/cartographer/` (`aiprofileswindow.cpp`, `dialogs.cpp`, `document.cpp`,
`document.h`, `session.cpp`), `src/client/` (`campaignscreen.cpp`,
`campaignscreen.h`, `gameview.h`, `gameview_impl.cpp`, `gameview_lobby.cpp`,
`gameview_net.cpp`, `main.cpp`, `settings.cpp`, `settings.h`, `sound.h`),
`src/hpi/` (`hpi.cpp`, `hpi.h`), `src/net/` (`client.cpp`, `client.h`,
`protocol.h`) and `src/server/server.cpp`. Nothing under `src/sim/`,
`src/ai/`, `src/campaign/`, `src/tnt/`, `src/server/crusades/`,
`src/net/replayhdr.h` or `src/net/crusades.h` changed.

- `src/net/protocol.h` and `src/server/server.cpp`: **comments only** (the
  `GameOptions::gameMods` comment, and two comments at the server's
  `gameMods` sanitising and `modMsg` host check). The server's behaviour is
  unchanged: it still maps `gameMods` to 0/1, forces 0 for campaign missions
  (`lobbyMsg`) and Crusades battles (room setup), and accepts game-pack uploads
  only from the host of a room with game mods on.
- `src/net/client.cpp` / `client.h`: client mod logic only.
  `MpClient::hostGameModPacks()` (the host's always-enabled plus ticked game
  packs, sorted and unique, at most 64) replaces `gameModPacks_` in the host's
  map package (`createGame`), mod package (`offerMods`) and room VFS
  (`roomDataVfs`); `rescanAlwaysGameMods` and `setGameModsAllowed` (debug
  `--mods`). A guest still mounts the host's package alone.
- The other changes are tests (`tools/mod_packs_test.cpp`,
  `mod_settings_test.cpp`, `mod_transfer_test.cpp`, `ai_profile_test.cpp`,
  `cartographer_aiprofile_test.cpp`, `settings_test.cpp`,
  `server_public_test.py`, the new `tools/no_game_mods_toggle.cmake` with its
  `add_test` line in `CMakeLists.txt`), `.gitignore`, the repository's
  `click.hpi` moved to `mods/cosmetic/always_enabled/click/click.hpi`, and docs
  (`README.md`, `CLAUDE.md`, `docs/user-guide.md`, `docs/cartographer-guide.md`,
  `docs/multiplayer-design.md`, `docs/img/README.md`).

The preparation commits on top of `5fa673d0`, cherry-picked linearly onto
`origin/main` (a `git fetch` before landing showed nothing else there):
`618021cb` (screenshots), `cf67bacd` (version 0.8.14, release notes, this
report's skeleton), `529b5dca` (this report's screenshots section) and the
commit filling this report's local checks, screenshots and scope, then
`7e50d1c8` (this report's validation and scope) and `23ad7a5f` (the release
`--help` line for `--game-mod` now says that `mods/game/always_enabled` packs
apply as well, and the notes' resolved placeholder comment is removed). No code fix
was needed.

No hashed simulation state changed: no commit touches `src/sim/` or `src/ai/`,
and the `src/net/` and `src/server/` changes do not reach the lockstep, the
referee or any message layout.

**Compatibility.** `kNetVersion` is **293** in `src/net/protocol.h` at
`5fa673d0`, as at `v0.8.13`. `kReplayFormat` is **14** in
`src/net/replayhdr.h`, as at `v0.8.13`. The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion` or whose pure-retail gameplay-data hash differs from the
  referee's; the build id is read and ignored and the release string is never
  sent. The data hash comes from `hpi::mountRetailRoot` with no packs, which
  the always-enabled folders do not enter. So **0.8.14, 0.8.13 and 0.8.12
  clients and servers play together in any mix**; 0.8.11 or earlier (protocol
  290 or lower) cannot join any of them, nor they an older server.
- **Mixed-version rooms (from the source).**
  - The `GameOptions` layout is unchanged; a 0.8.14 host always sends
    `gameMods = 1` (`gameview_lobby.cpp`, CREATE). A 0.8.12/0.8.13 host's
    `gameMods = 0` (GAME MODS: OFF) is accepted as before. In such a room a
    0.8.14 guest mounts no game packs: `roomDataVfs` returns the bare retail
    mount when `!room_.opts.gameMods`, `modVfs` adds no game files, and a guest
    never adds its own always-enabled game packs in any room. Its lobby shows
    **GAME MODS: OFF (HOST)**. Its always-enabled cosmetic packs still load
    (local only).
  - A 0.8.12/0.8.13 guest in a 0.8.14 host's room sees `gameMods = 1`, which
    its lobby shows as **GAME MODS** with **HOST:** and the package's names
    (or **NONE SELECTED**). The package's names are sorted, unique, plain
    folder names, at most 64 (`hostGameModPacks`), so they pass
    `mods::decode`'s checks (`src/net/modpackage.cpp`: at most 64, strictly
    increasing, no separators) unchanged since 0.8.12; the files are merged by
    the host (always-enabled layers first), so the guest needs no knowledge of
    `always_enabled/`.
  - A 0.8.14 guest in a 0.8.12/0.8.13 host's room with GAME MODS on receives
    that host's ticked packs as before.
- **Replays.** `supportedReplayProtocol` accepts a recording when its format is
  1..14 and its protocol equals `kNetVersion`. The header's `gameMods` byte and
  `modDigest` are unchanged in meaning. So 0.8.14, 0.8.13 and 0.8.12 play each
  other's protocol-293 replays; 0.8.11's protocol-290 replays are refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generator-8/9 recipes,
  `mods/` pack folders and `ModCache/*.takmods` carry over. One settings key
  was removed: `gameCreate.gameMods` is no longer written and is skipped on
  load (`src/client/settings.cpp`). A 0.8.13 that reads a settings file saved
  by 0.8.14 finds no such key and keeps its default (off) for its own create
  screen. `.hpi` archives directly in `mods/cosmetic/` are no longer loaded
  (the player moves them; nothing is migrated).

**No mixed-version check was run.** No 0.8.13 or 0.8.12 client joined a
0.8.14 server or host (or the reverse), and no older replay was played on
0.8.14; compatibility rests on the source above.

## Local preparation checks

These ran on Linux, cores 4-23, with an optimized Debug build (`build-o2`, `-O2 -g`, ccache + mold, `TAK_TEST_DATA` = the retail install). The build was made fresh in a detached worktree at `5fa673d0`, all targets; `--version` reports `0.8.13 (build v0.8.13-3-g5fa673d05f08)` (the CMake version is still 0.8.13 at this commit). The Release `build/takclient` and `build/takserver` in the main checkout report the same build id, so they were already at `5fa673d0` and were not rebuilt. By the user's rule (minimal testing), no Release, `-O0` Debug, Clang or ASan suite was run, and no repeat runs.

- **Full suite.** The one full `ctest` run on `-O2` at `5fa673d0` (`-LE nightly -j16`, scratch HOME/XDG_DATA_HOME) passed **609/609** (`rc=0`, 359 s real). 771 tests are defined, the 164 `nightly`-labelled ones excluded. The mods tests passed inside this run: `no_game_mods_toggle` (new: the create screen has no GAME MODS option), `mod_packs`, `mod_settings`, `mod_transfer`, `settings`, `server_public`, `ai_profile`, `ai_mod_profile` (59.5 s; it timed out once in 0.8.13's run and did not here) and the `cartographer_aiprofile*` set. No test failed or needed a rerun. Log: `~/tak-tmp/scratch/release-0814/ctest.out`.
- **Detmath guard** (`tools/check-detmath.sh`): OK. The sim path has no direct libm transcendentals and no `std::atomic_ref`.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): g++ and clang++ at -O0/-O2/-O3 all give `dcef618cd2e4d558`, the same golden as 0.8.13. The aarch64 legs were SKIPPED because there is no target libc or headers locally; the macOS CI leg covers ARM64. Log: `~/tak-tmp/scratch/release-0814/check-determinism.log`.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`, a clean scratch HOME/XDG_DATA_HOME per game, `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI). All five games ended `err=none end=timelimit`, and no log has a desync or referee-suspect line.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `b353410003e98dd5` (4 units) | `a3d953803a064191` (13 units) |
| Legion | `0825aeb9e8139203` (4 units) | `ce5c4ba7562e53b9` (18 units) |
| Retail AI (`TAK_AI_LEVEL=6`) | `14849c00bc6e2a19` (4 units) | not run |

Every hash matches 0.8.13 (and 0.8.12), as expected: `git diff --stat v0.8.13 5fa673d0` lists nothing under `src/sim`, `src/ai`, `src/tnt`, `src/campaign`, `src/server/crusades`, `src/net/replayhdr.h` or `src/net/crusades.h`. The `src/net` and `src/server` changes are client mod-selection logic (`client.cpp`/`client.h`) and comments (`protocol.h`, `server.cpp`). The Retail AI server again logged `game 1: DEFAULT AI profile ai/DEFAULT.txt from the base data`; this is log wording only, as before. Logs: `~/tak-tmp/scratch/release-0814/mpai/`.

- **Compatibility constants** (checked at `5fa673d0`): `kNetVersion` **293** (`src/net/protocol.h`, unchanged from `v0.8.13`) and `kReplayFormat` **14** (`src/net/replayhdr.h`, unchanged). `tools/replay_test.cpp` still pins `kNetVersion == 293` and `kReplayFormat == 14`. Unchanged: map generator `formatVer` 9 (`src/tnt/mapgen.h`), campaign `kVersion` 4 (`src/net/crusades.h`), `kSchemaVersion` 9 (`src/server/crusades/store.cpp`).
- **How versions are compared** (source check for the notes):
  - *Joining a server.* `Server::handshake` (`src/server/server.cpp`) rejects a Hello only when `ver != kNetVersion`, or when the client's pure-retail gameplay-data hash differs from the referee's. The client sends an empty build id (`src/net/client.cpp`, "version-gated only") and the server discards it (`(void)build`). `tak::kVersion` is never compared. So **0.8.14, 0.8.13 and 0.8.12 clients and servers play together in any mix**; 0.8.11 or earlier (protocol 290 or lower) cannot.
  - *Replays.* A replay loads when `supportedReplayProtocol(format, protocol)` holds (`1 <= format <= 14` and `protocol == kNetVersion`). All three builds write format 14 under protocol 293, so **replays play back in both directions** between 0.8.14 and 0.8.13/0.8.12. The replay header is unchanged.
  - *The game-mods byte.* `GameOptions::gameMods` keeps its meaning (0 = no game packs, 1 = the host's package is sent to and mounted by every player). A 0.8.14 host always sends 1 for a non-campaign room. The server still accepts either value from any host (`o.gameMods = o.gameMods ? 1 : 0`, on create and on options updates), and forces 0 for campaign rooms (`!mission.empty()`), campaign battles (`campaignBattleId`) and Crusades rooms (`opts.crusades=1; gameMods=0`).
  - *A 0.8.12/0.8.13 host with GAME MODS: OFF.* The room's byte is 0, so no game pack applies to anyone: the server refuses every `ModOffer` ("game mods are off in this game"), and a 0.8.14 client's `modVfs`/`roomDataVfs` mount the retail data alone. A 0.8.14 guest's own `mods/game/always_enabled/` packs never apply in any room it joins (they are used only through `hostGameModPacks()`, when it is the host), so "off" means off, always-enabled packs included. A 0.8.14 guest shows the column as **GAME MODS: OFF (HOST)**. Each player's own cosmetic packs (always-enabled and ticked) still load, as cosmetic mods always have.
  - *A 0.8.13 joiner in a 0.8.14 host's room.* It sees an ordinary game-mods-on room: the **GAME MODS** heading and **HOST:** with the package's pack names, which are the host's always-enabled and ticked game packs merged into one sorted list (`hostGameModPacks()`), with no mark showing which are always-enabled; **NONE SELECTED** when the host has neither. Package names stay plain (no `/`, `\`, NUL or newline, strictly sorted), so `modpackage.cpp`'s 0.8.12/0.8.13 decode accepts them, and the joiner downloads, verifies and mounts the package like any other. The joiner's own 0.8.13 always-on `mods/cosmetic/*.hpi` archives still load on its own client (cosmetic, never sent).
  - Because the sim is byte-identical, mixed games also play out the same on every build, which the unchanged `--mpai` hashes confirm.

**No mixed-version check was run.** No 0.8.13 or 0.8.12 client joined a 0.8.14 server or host (or the reverse), and no older replay was played on 0.8.14; the points above rest on the source.

- **Final source** (the preparation commits on `5fa673d0`, at `529b5dca`; the
  fill commit after it changes only this report): every `build-o2` target was
  rebuilt (`rc=0`), `takclient --version` reports `0.8.14 (build
  v0.8.13-6-g529b5dcae9b5)`, and `ctest -L quick -LE nightly -j16` (cores
  4-23, scratch HOME/XDG_DATA_HOME) passed **501/501** (`rc=0`, 141 s real).
  Logs: `~/tak-tmp/scratch/release-0814/final-build.log` and `final-quick.out`.

Failures: none.

## Remote multiplayer sweep

**Not run for this release.** Since `v0.8.13` no commit touches `src/sim/` or
`src/ai/`. `src/server/server.cpp` and `src/net/protocol.h` change only in
comments. `src/net/client.cpp` changes only which game packs the host
packages (client mod logic, not the lockstep, referee or message layout). The
protocol stays 293, and all five `--mpai` hashes match 0.8.13 on Linux, as do
all ten on Windows and macOS (see "CI and packages"). The sweep's 37
scenarios run with no mod packs, so they would replay 0.8.13's table (in turn
0.8.10's, `docs/release-0.8.10-validation.md`).

## CI and packages

Main CI on the final source: main CI on `5fa673d0` (the mods change) passed
all four workflows. The runs on `7e50d1c8` were cancelled as superseded by
`23ad7a5f`, and the main runs on `23ad7a5f` were cancelled because the tag
runs build the same commit. The tag runs are the CI check on the final source.

On the exact tag (`23ad7a5f`, annotated tag object `072fe304`), all four
workflows passed on the first attempt, with no reruns and no force-cancels.

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/40101686205):
  started manually on the tag because the workflow is path-filtered.
  `unit-tests` (8/8), `detmath` and `sim-equiv` passed. The GCC and Clang
  x86-64 builds at O0/O2/O3 all agree on golden `dcef618cd2e4d558`, the same as
  0.8.13. This workflow runs no `--mpai` game.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/40101667398):
  all 16 native package jobs and the release attachment, first attempt.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/40101667280):
  x64, ARM64 and the x64 signing/package job, first attempt.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/40101667270):
  both builds plus the **Sign + notarize** job for each architecture, first
  attempt. Apple accepted every submission. The job replaced the unsigned
  macOS ZIPs and DMGs in the draft with signed, notarized and stapled ones
  (14:35 UTC on 2026-10-24).

The release was created as a draft with these notes, and tagged CI attached
the packages. All **29 assets** were downloaded after the macOS signing
finished.
- They are the same set as 0.8.13 with the version swapped. None has size 0 or
  an upload state other than "uploaded". Each one matched GitHub's SHA-256
  digest and byte size before publication.
- Sizes barely moved from 0.8.13. The release assets range from -0.040% to
  +0.042% (-9.3 KB to +8.9 KB), and the debug archives from -0.057% to +0.023%
  (-15.9 KB to +21.9 KB).
- There are no new dependencies. Debian and Ubuntu packages still depend only
  on `libc6`, RPMs require only libc, libm and the loader, and the Windows x64
  import tables of `takclient.exe`, `takserver.exe`, `cartographer.exe` and
  `crusades_admin.exe` are identical to 0.8.13's.
- Each package, installer and ZIP ships the same file list as its 0.8.13
  counterpart (no file added or removed). The DMGs hold the same two apps and
  `Applications` link, and each DMG app's file list equals the same app's
  from the ZIP.

- **Build ids.** The client and server in every Linux package carry
  `v0.8.14`. That covers Debian 12 and 13, Ubuntu 22.04, 24.04 and 26.04,
  Fedora 44 and openSUSE Leap 16.0 (each for both architectures), plus Arch,
  Slackware and both Linux debug tarballs. Installer, DMG and ZIP names carry
  `0.8.14`.
  - On Linux, the x86-64 `takclient` and `takserver` report
    `0.8.14 (build v0.8.14)`. That holds for Debian 12 and 13, Ubuntu 22.04,
    24.04 and 26.04, Fedora 44, openSUSE Leap 16.0, Slackware and the x64
    debug tarball. So does the Arch `takserver`. The Arch `takclient` needs
    glibc 2.44, newer than the checking host's 2.43, so only its build string
    was checked. For the arm64 binaries, only the build string `v0.8.14` was
    checked.
  - On Windows 11, the x64 Windows ZIP's `takclient.exe` and `takserver.exe`
    report `0.8.14 (build v0.8.14)`. For the ARM64 `takclient.exe` and
    `takserver.exe`, the installer's and the debug ZIPs', only the build
    string `v0.8.14` was checked.
  - On the arm64 MacBook, `takclient` and `takserver` report
    `0.8.14 (build v0.8.14)` for both architectures. This holds for the bare
    programs from the ZIPs, for the copies inside the game app from each DMG
    and each ZIP, and for the debug ZIPs' `takclient`. The x64 copies ran
    under Rosetta.
- **Windows signatures.** `Get-AuthenticodeSignature` on oden-win reports
  **Valid** (CN=Curtis Edge, timestamped) for 9 signed files, the same count
  as 0.8.13. They are the setup `.exe`, plus `takclient.exe`, `takserver.exe`,
  `cartographer.exe` and `crusades_admin.exe` in both the x64 and ARM64 ZIPs.
  The debug ZIPs were not checked.
- **macOS notarization.** On the MacBook, both DMGs pass `xcrun stapler
  validate` and `spctl` (open, primary signature) as **accepted,
  source=Notarized Developer ID, origin=Developer ID Application: Curtis Edge
  (L479Y59RN8)**.
  - Both apps (the game and TAK Cartographer) pass both checks the same way,
    from each DMG and from each ZIP, and pass `codesign --verify --strict
    --deep`.
  - Every bare ZIP `takclient`, `takserver`, `cartographer` and
    `crusades_admin` passes `codesign --verify --strict` (TeamIdentifier
    L479Y59RN8). `spctl` says those bare programs do not seem to be an app,
    which is expected for unstapled command-line tools.
- **Packaged server runs the Retail AI.** The Fedora 44 x86-64 RPM's
  `takserver` (extracted, reporting `0.8.14 (build v0.8.14)`, protocol v293)
  was started on the Linux checking host against the retail install. The flags
  were `--data <retail> --local --no-auth --seed 1` on a loopback-only scratch
  port, with a scratch `HOME`, `XDG_DATA_HOME` and `XDG_CONFIG_HOME`.
  - One headless harness client joined it with `TAK_AI_LEVEL=6 --mpai --time
    60` on Inner Circle. The client was the `-O2` Debug build of the tag
    `23ad7a5f`, reporting `0.8.14 (build v0.8.14)`.
  - The server logged `game 1: DEFAULT AI profile ai/DEFAULT.txt from the base
    data` (the accepted old wording), and the game ended
    `hash=14849c00bc6e2a19 units=4 err=none end=timelimit`. That is the Linux
    Retail AI 60 s hash.
  - No desync or referee-suspect line appeared.
- **Packaged client and editor command lines.** These were run from the
  extracted Fedora 44 x86-64 RPM under `env -i` with a fresh scratch `HOME`,
  `XDG_DATA_HOME` and `XDG_CONFIG_HOME` (all deleted afterwards).
  - `takclient --help` exits 0 and lists `--play-map <snapshot.kmp>`,
    `--game-mod <pack>` with the new line `(mods/game/always_enabled packs
    apply as well)`, `--version` and `--data`.
  - `cartographer --version` still prints its usage and exits 2, as in 0.8.13.
  - As a working run, `cartographer "Inner Circle" --data <retail> --bundle
    <scratch>/ic.kmp` under `SDL_VIDEODRIVER=dummy` loaded the map (96 × 96
    blocks, 4 start positions, 215 features, 1261 sections for `zhon`), wrote a
    379,088-byte `.kmp` and exited 0, the same as 0.8.13.
- **Mods folder.** No package, installer, ZIP, DMG or app contains an
  `overrides/` or `mods/` directory (so no `always_enabled/` folder), and none
  contains `click.hpi` or any other `.hpi`. The repository's
  `mods/cosmetic/always_enabled/click/click.hpi` (at `mods/cosmetic/click.hpi`
  in 0.8.13) is for tests and captures only, and neither release shipped it.
- **Streaming local copy in packages.** Every shipped `takclient` contains
  FFmpeg's MP4 muxer: all 29 copies in the Linux packages and tarballs, the
  Windows installer and ZIPs, and the macOS ZIPs and debug ZIPs, plus the game
  apps from both DMGs.
  - Each has the muxer's `video/mp4` MIME string and the `frag_keyframe`
    movflags option, next to the FLV muxer's `video/x-flv`.
  - Functionally, the tag CI's "Streaming encode test" ran `stream_test` on
    all 16 Linux package jobs and on the Windows x64, Windows ARM64, macOS x64
    and macOS arm64 builds.
  - Each run wrote a fragmented local copy (`2 fragments, video 55-76 samples
    2.46-2.59s, audio 2.48-2.58s`). No run printed the `SKIP local copy` line.
- **Opus is static.**
  - No packaged Linux `takclient` or `takserver` has a `NEEDED` entry beyond
    libc, libm and the loader (Slackware also lists libdl and libpthread), so
    none needs libopus.
  - No Windows `.exe` imports an Opus DLL, and the ZIPs ship no DLLs (the
    installer has only its NSIS plugin DLLs).
  - No macOS binary links a libopus dylib (`otool -L`).
  - `OPUS-LICENSE.txt` is in every Debian, RPM, Arch and Slackware package, in
    both Linux debug tarballs, in the Windows installer, in all four release
    ZIPs (inside the game app's Resources on macOS) and in the four debug
    ZIPs.
- **Font licences.** `CHAT-FONT-LICENSE.txt` sits beside
  `EDITOR-FONT-LICENSE.txt` in every place the game ships:
  - every Debian, RPM, Arch and Slackware package (`share/licenses/tak-engine`)
    and both Linux debug tarballs;
  - the Windows installer, both Windows release ZIPs and both Windows debug
    ZIPs;
  - both macOS debug ZIPs;
  - the game app's `Contents/Resources` from both DMGs and both ZIPs.

  As in 0.8.13, TAK Cartographer's app has only `EDITOR-FONT-LICENSE.txt`,
  which covers its Noto Sans Mono editor font.
- **macOS microphone.** The game app's `Info.plist` carries
  `NSMicrophoneUsageDescription` in both DMGs and both ZIPs. The
  `com.apple.security.device.audio-input` entitlement is on the game app's
  `takclient` (DMG and ZIP, both architectures) and on the bare ZIP
  `takclient`. It is absent from `takserver`, `crusades_admin` and
  `cartographer`.
- **macOS Retina.** `NSHighResolutionCapable` is `true` in the `Info.plist` of
  both apps, from both DMGs and both ZIPs.
- As in 0.8.1 to 0.8.13, no packaged Linux `takclient` has a `NEEDED` entry
  for libsecret, GLib, GIO or GObject.
- `docs/release-0.8.14-notes.md`, this report,
  `docs/img/guide/room-mods-power-codes.jpg`, `docs/img/room.jpg` and
  `docs/img/title.jpg` are in the source tree at the tag. `kNetVersion` there
  is 293.
- Every `blob/v0.8.14/docs/...` link in the release body resolves to a file at
  the tag, including its two section anchors (`user-guide.md#mods` and
  `cartographer-guide.md#saving-as-a-game-mod`). The files are
  `cartographer-guide.md`, `release-0.8.14-validation.md` and `user-guide.md`.
  The body's own `#moving-your-always-on-archives` link points to its
  "Moving your always-on archives" heading, which the notes at the tag also
  carry.

`--mpai` on Windows and macOS was run on optimized Debug builds (`-O2`) of the
tag `23ad7a5f`, built on oden-win (MSYS2 MINGW64, GCC) and on the arm64
MacBook (Apple Clang). The settings were Inner Circle, `takserver --local
--no-auth --seed 1`, a scratch profile, `TAK_LEGION=1` for Legion and
`TAK_AI_LEVEL=6` for the Retail AI. All ten games ended `err=none
end=timelimit` and match Linux on both hosts:
- Retail: 60 s `b353410003e98dd5` (4 units), 300 s `a3d953803a064191`
  (13 units).
- Legion: 60 s `0825aeb9e8139203` (4 units), 300 s `ce5c4ba7562e53b9`
  (18 units).
- Retail AI: 60 s `14849c00bc6e2a19` (4 units). Each host's server logged
  `DEFAULT AI profile ai/DEFAULT.txt from the base data`.

Neither host logged a desync or referee-suspect line. The tag's dispatched
Determinism run (40101686205) runs no `--mpai` game. Its cross-build check
agrees on golden `dcef618cd2e4d558`, as did 0.8.13's (40086931128).

Published with `gh release edit v0.8.14 --draft=false --latest` on 2026-10-24 at 15:01 UTC (the release was created as a draft with the notes first, whose links were rewritten to `blob/v0.8.14`). `gh api repos/pocketgeek/tak-engine/releases/latest` returns `v0.8.14` with 29 assets: <https://github.com/pocketgeek/tak-engine/releases/tag/v0.8.14>.

## Screenshots

Six pictures were retaken on branch `task-release-0814-shots` (`8a7a1fb3`,
one commit on `5fa673d0`) with a local `-O2` Debug build whose CMake version
was set to 0.8.14 for the capture and reverted (no capture patch):
`docs/img/title.jpg` (VERSION 0.8.14), and every picture that named the old
always-on archive, which now reads **ALWAYS ON: click**: `room.jpg`,
`guide/room-mods-power-codes.jpg` (now also **ALWAYS ON: House Rules** under
GAME MODS, from a scratch `mods/game/always_enabled/House Rules/`, with Big
Battles ticked and GAME MODS VERIFIED), `campaign.jpg` and
`guide/campaign-briefing.jpg` (*Always-on mods: click*), and
`cartographer/test-lobby.jpg`. The scratch installs' packs are placeholder
folders with one tiny sound file each, plus a copy of the repository's
`click.hpi`; nothing from them is committed. Kept after a look:
`quick/lobby-move-here.jpg` (its install had no packs), and `lobby.jpg`,
`lobby-scenario.jpg` and `guide/random-map-lobby.jpg` (the single-player
create screen, which never had a GAME MODS option). No shipped picture shows
the multiplayer create screen, so none showed the removed option. The README
title alt text (version 0.8.14), gallery footnote ("0.7.25 to 0.8.14 builds"),
the user guide's `room-mods-power-codes.jpg` alt text and `docs/img/README.md`
were updated with them. Scripts and raw captures:
`~/tak-tmp/scratch/release-0814/shots/`.
They landed on main as `618021cb`.

## Limits

- No game mixing 0.8.14 with 0.8.13 or 0.8.12 was attempted;
  compatibility rests on the unchanged protocol, replay format and
  source above.
- New: a `.hpi` left directly in `mods/cosmetic/` is silently not loaded (no
  warning, no migration), by design; the notes and user guide give the move.
- All of 0.8.13's limits carry over: Legion units stand and wait more than
  Retail's; Legion attackers through a narrow gate deal more damage than
  Retail's; power codes are lockstep (retail applied them only on the typer's
  machine); FULL VISION does not switch LOS off at start; UI FONT CLASSIC is not
  pixel-identical with 0.8.7; the Legion bridge lottery; the Legion 10,000
  workloads' congestion and tick budget, mixed-10000's hovering flyers; Retail
  `h-cramped` ends early; the macOS Retina UI Scale is migrated once; a
  stationary auto-targeting shooter keeps refusing a once-blocked target; the
  campaign book's chapter numeral touches "APTER"; a server-side upload stall
  longer than 1 s is charged to every seat it holds up; a reconnecting seat
  that hitches while catching up is charged as a hold; the Retail AI's
  accepted limits ([acceptance ledger](classic-ai-acceptance.md#accepted-limits));
  our six levels' built-in tables are identical, so `ai/takengine.txt` sets the
  same weights and limits for every level; Cartographer **TEST IN GAME**
  (`--game-mod`) has not been tried by hand on Windows or macOS.
- Accepted from 0.8.12 and unchanged: the Retail AI server log still says
  `DEFAULT AI profile ai/DEFAULT.txt`, and the Cartographer's tab still reads
  **DEFAULT AI**.
- `ai_mod_profile` hit its 600 s timeout in the 0.8.11 and 0.8.13 full suites
  and passed alone in 56 s each time (possible intermittent hang, not
  investigated). It did not recur in this release's full suite (passed in
  59.5 s).
- Tag CI: none. Every tag run passed on attempt 1, with no reruns or
  force-cancels. None of the known flakes appeared, and no setup step hung.
