# 0.8.12 release validation

Protocol **293** (was 290), replay format **14**, generator version **9**,
campaign payload **4**, database schema **9**.

Tagged `v0.8.12` at `be4af85a` (annotated tag object `dee8eff6`) and published on 2026-10-24 04:55 UTC.

## Scope and compatibility

0.8.12 follows 0.8.11 (protocol 290). After the 0.8.11 validation commit
`828a24ee`, main reached `450a10b6` in 37 commits, all linear (no merges).
`git diff --stat 828a24ee..450a10b6 -- src` touches 38 files: `src/ai/`
(`builtinprofile.cpp`, `builtinprofile.h`); `src/cartographer/`
(`aiprofiles.cpp` and `aiprofiles.h` (new), `aiprofileswindow.cpp` (new),
`canvas.cpp`, `chrome.cpp`, `dialogs.cpp`, `document.cpp`, `document.h`,
`editor.h`, `editorstate.h`, `history.cpp`, `history.h`, `main.cpp`,
`palette.cpp`, `playtest.cpp`, `playtest.h`, `savejob.cpp`, `savejob.h`,
`session.cpp`, `validation.cpp`, `validation.h`); `src/client/`
(`datafolder.h`, `gameview.h`, `gameview_hud.cpp`, `gameview_lobby.cpp`,
`gameview_render.cpp`, `main.cpp`, `settings.cpp`, `settings.h`); `src/hpi/`
(`hpi.cpp`, `hpi.h`); `src/net/` (`mappackage.cpp`, `protocol.h`);
`src/server/server.cpp`; and `src/sim/` (`sim.cpp`, `sim.h`). Nothing under
`src/campaign/`, `src/tnt/`, `src/terrain/`, `src/cob/`, `src/crt/`,
`src/video/` or `src/net/replayhdr.h` changed.
The preparation commits on top of `450a10b6` are `ea38fa99` (title
screenshot, capture notes), `e3b54d08` (version 0.8.12, notes, this report's
skeleton, docs), `a549ed5c` (campaign speed in quick-start/README, notes
wording) and this report's fill. `git fetch` before landing showed
`origin/main` still at `450a10b6`, so nothing else landed.

The changes:

- Cartographer keeps a map's `ai/` files: `baa0d0d1` (open, save, Test Map,
  recovery, undo), `95083284` (an installed map opened by name), `0b901740`
  (docs).
- Map packages (protocol 291): `d9e32a1b` (a map's AI files come from its own
  archive, `Vfs::readFromMapArchive`), `62f68894`, `610e0096` (docs).
- Cartographer AI Profiles window: `654657ef` (model checked against the
  engine's parsers), `fdf825e7` (window), `1e8fcb50` (UI tests), `5862d171`
  (game-mod writer: plan, backups, atomic install), `9de87966` (Save as / Open
  from game mod), `9f51b0e6` (tests), `e37deb2f`, `065bd4c3` (group rules, pack
  roster, Ctrl+Z, limits in play), `129358fb` (extras test), `f24224d2` (end to
  end), `12eb7a86` (guides), `5f8273d6` (case-only rename), `da0f2de1` (a
  Retail profile saved as `ai/default.txt` names that file), `a2374397` (pack
  list scrolls), `80083f65` (move a group rule down), `96471330` (copy to other
  levels; replaced by `ca9b612d`), `3ed1b8b3`, `fcc6d27b`; ledger `95673f1c`,
  `6eee1837`, `8776dbad`, `c1aa2990`, `d9840695`.
- Campaign: `4de33a79` (order hint below the objectives panel), `c51dc03a`
  (missions always allow speed changes; `src/server/server.cpp` sets
  `speedUnlock` for a mission room).
- Lobby: `2c6b89a9` (AI level 6 shown as RETAIL; `aitool retail`).
- Flyers (protocol 292): `1e9d7df3` (a Shift-queued patrol of an armed or
  builder flyer laps; `World::openFlightPatrol`, `movement_orders`
  `queuedFlyerPatrols`).
- AI files (protocol 293): `40958301` (our six levels read one
  `ai/takengine.txt` over their own tables), `ca9b612d` (one TAK ENGINE AI tab,
  MERGE INTO TAK ENGINE), `7a040a0f` (AI mod without a map, TEST IN GAME,
  release-safe `--game-mod`), `450a10b6` (footer wording, `ai-mod.jpg`).

The only hashed simulation change is `1e9d7df3` (queued flyer patrols; its own
test failed 6 checks on the old code). Protocols 291 and 293 change map and
game-mod packages, not the simulation; `c51dc03a` only sets a campaign
mission room's speed-change option on the server. With no `ai/takengine.txt` in play our
levels play their built-in tables, as in 0.8.11. The commits themselves checked
`--mpai` Inner Circle seed 1 at 60 s only (Retail, Legion and a Retail AI seat
unchanged from 0.8.11). The release's local checks confirm 60 s and 300 s
in both modes and the Retail AI seat at 60 s unchanged (below).

`--mpai` on Windows and macOS was run on optimized Debug builds (`-O2`) of the
tag `be4af85a`, built on oden-win (MSYS2 MINGW64, GCC) and on the arm64
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
Determinism run (40060866101) runs no `--mpai` game. Its cross-build check
agrees on golden `dcef618cd2e4d558`.

**Compatibility.** `kNetVersion` is **293** in `src/net/protocol.h` at
`450a10b6` (290 at `v0.8.11`). `kReplayFormat` is **14** in
`src/net/replayhdr.h`, as at `v0.8.11`. The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion` (or whose gameplay-data hash differs). So **0.8.12
  cannot play with 0.8.11 or earlier in either direction**; the server and
  every player must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.12 refuses 0.8.11's protocol-290
  replays (format 14 as well) and 0.8.11 refuses 0.8.12's.
  `tools/replay_test.cpp` pins `kNetVersion == 293` and `kReplayFormat == 14`
  and checks that `supportedReplayProtocol(14, 292)` is refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generator-8/9 recipes,
  `mods/` packs and `ModCache/*.takmods` carry over. No settings key was added
  or removed (`--game-mod`'s pack selection is for one run and never saved over
  the player's ticks). A game pack or `.kmp` map made for 0.8.11 that carries
  `ai/d.txt` ... `ai/ad.txt` no longer adjusts our levels (the files are not
  read; a map package naming one is refused); `ai/takengine.txt` replaces them.

## Local preparation checks

Testing is reduced for this release, by the user's rule (minimal testing):

- **Full CTest, once, on optimized Debug (`-O2`) at `450a10b6`: 606/606
  passed** (`-LE nightly -j16`, 353 s real, `rc=0`, scratch HOME and
  XDG_DATA_HOME). 768 tests are defined; the 164 `nightly`-labelled ones were
  excluded. No test timed out and nothing was rerun. The new and changed AI and
  Cartographer tests passed inside this run: `cartographer_aiprofile`,
  `cartographer_aiprofile_ui`, `_mod_ui`, `_extras_ui`, `_packs_ui`,
  `_merge_ui`, `cartographer_aimod_ui`, `cartographer_ai_profile_e2e`,
  `ai_profile`, `ai_profile_data`, `ai_mod_profile`, `classic_ai_map_profile`,
  `server_ai_level`, `flyer_combat`, `replay`, `replay_file`, `replay_writer`
  and `replay_always`. The build was fresh, all targets, in a detached worktree
  at `450a10b6` (ccache + mold, `TAK_TEST_DATA` = the retail install). Log:
  `~/tak-tmp/scratch/release-0812/ctest.out`.
- No Release, `-O0` Debug, Clang or ASan suite was run, and no repeat runs.
- **Detmath guard** (`tools/check-detmath.sh`): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): g++ and
  clang++ at -O0/-O2/-O3 all give `dcef618cd2e4d558`, the same golden as
  0.8.11. The aarch64 legs were skipped (no target libc locally); the macOS CI
  leg covers ARM64 parity. Log:
  `~/tak-tmp/scratch/release-0812/check-determinism.log`.
- **`--mpai` golden hashes.** Inner Circle, `takserver --local
  --no-auth --seed 1`, a clean scratch profile per game, optimized Debug
  binaries at `450a10b6`, `TAK_LEGION=1` for Legion and `TAK_AI_LEVEL=6` for
  the Retail AI. Every game ended `err=none end=timelimit`, with no desync or
  referee-suspect line.

  | Mode | Length | Hash (units) | 0.8.11 |
  | --- | --- | --- | --- |
  | Retail | 60 s | `b353410003e98dd5` (4) | `b353410003e98dd5` |
  | Retail | 300 s | `a3d953803a064191` (13) | `a3d953803a064191` |
  | Legion | 60 s | `0825aeb9e8139203` (4) | `0825aeb9e8139203` |
  | Legion | 300 s | `ce5c4ba7562e53b9` (18) | `ce5c4ba7562e53b9` |
  | Retail AI | 60 s | `14849c00bc6e2a19` (4) | `14849c00bc6e2a19` |

  Every hash matches 0.8.11: 291 and 293 change only map- and game-mod package
  contents, and 292's flyer-patrol fix does not come up in these games. The
  Retail AI server still logs `DEFAULT AI profile ai/DEFAULT.txt from the base
  data` (the 0.8.11 name; log wording only). Logs:
  `~/tak-tmp/scratch/release-0812/mpai/`.
- Landing checks recorded in the commits: `40958301` and `1e9d7df3` each
  report `--mpai` Inner Circle seed 1 60 s unchanged (Retail
  `b353410003e98dd5`, Legion `0825aeb9e8139203`, Retail AI
  `14849c00bc6e2a19`); `ca9b612d` reports `ctest -L quick` 497/497 and
  `cartographer_ai_profile_e2e` passing on `build-o2`.
- **Final source** (the preparation commits on `450a10b6`): every `build-o2`
  target rebuilt, `takclient --version` reports `0.8.12 (build
  v0.8.11-41-ga549ed5c5650)`, and `ctest -L quick` passed 498/498 (139 s).
  Log: `~/tak-tmp/scratch/release-0812/land-quick.out`.

## Remote multiplayer sweep

**Not run for this release.**
Confirmed by the lead: the `--mpai` gates are unchanged from
0.8.11 (Retail and Legion at 60 s and 300 s, the Retail AI at 60 s), and the
only simulation change is the Shift-queued armed/builder flyer patrol fix
(`1e9d7df3`, protocol 292), which has its own test (`movement_orders`
`queuedFlyerPatrols`, failing 6 checks on the old code, passing in Retail and
Legion). Protocols 291 and 293 change only which AI files map and game-mod
packages carry. The sweep's 37 scenarios would otherwise replay 0.8.11's
(0.8.10's) table (`docs/release-0.8.10-validation.md`).

## CI and packages

Main CI on the final source (`be4af85a`): Windows build 40059047954, macOS
build 40059047967, Linux packages 40059047987 and Determinism (sim)
40059047955 all passed on the first attempt.

On the exact tag (`be4af85a`, annotated tag object `dee8eff6`), all four
workflows passed on the first attempt, with no reruns and no force-cancels.

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/40060866101):
  started manually on the tag because the workflow is path-filtered.
  `unit-tests` (8/8), `detmath` and `sim-equiv` passed. The GCC and Clang
  x86-64 builds at O0/O2/O3 all agree on golden `dcef618cd2e4d558`, the same as
  0.8.11. This workflow runs no `--mpai` game.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/40060843204):
  all 16 native package jobs and the release attachment, first attempt.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/40060843176):
  x64, ARM64 and the x64 signing/package job, first attempt.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/40060843178):
  both builds plus the **Sign + notarize** job for each architecture, first
  attempt. Apple accepted every submission. The job replaced the unsigned
  macOS ZIPs and DMGs in the draft with signed, notarized and stapled ones
  (04:20 UTC on 2026-10-24).

The release was created as a draft with these notes, and tagged CI attached
the packages. All **29 assets** were downloaded after the macOS signing
finished.
- They are the same set as 0.8.11 with the version swapped. None has size 0 or
  an upload state other than "uploaded". Each one matched GitHub's SHA-256
  digest and byte size before publication.
- Every release asset is 1.07% to 1.88% larger than its 0.8.11 counterpart
  (+190 KB to +648 KB). The debug archives grew 1.71% to 3.08% (+490 KB to
  +3.9 MB).
- There are no new dependencies. Debian and Ubuntu packages still depend only
  on `libc6`, RPMs require only libc, libm and the loader, and the Windows x64
  import tables of `takclient.exe`, `takserver.exe`, `cartographer.exe` and
  `crusades_admin.exe` are identical to 0.8.11's.
- Each package, installer and ZIP ships the same file list as its 0.8.11
  counterpart (no file added or removed), and the DMGs hold the same two apps
  and `Applications` link.

- **Build ids.** The client and server in every Linux package carry
  `v0.8.12`. That covers Debian 12 and 13, Ubuntu 22.04, 24.04 and 26.04,
  Fedora 44 and openSUSE Leap 16.0 (each for both architectures), plus Arch,
  Slackware and both Linux debug tarballs. Installer, DMG and ZIP names carry
  `0.8.12`.
  - On Linux, the x86-64 `takclient` and `takserver` report
    `0.8.12 (build v0.8.12)`. That holds for Debian 12 and 13, Ubuntu 22.04,
    24.04 and 26.04, Fedora 44, openSUSE Leap 16.0, Slackware and the x64
    debug tarball. So does the Arch `takserver`. The Arch `takclient` needs
    glibc 2.44, newer than the checking host's 2.43, so only its build string
    was checked. For the arm64 binaries, only the build string `v0.8.12` was
    checked.
  - On Windows 11, the x64 Windows ZIP's `takclient.exe` and `takserver.exe`
    report `0.8.12 (build v0.8.12)`. For the ARM64 `takclient.exe` and
    `takserver.exe`, the installer's and the debug ZIPs', only the build
    string `v0.8.12` was checked.
  - On the arm64 MacBook, `takclient` and `takserver` report
    `0.8.12 (build v0.8.12)` for both architectures. This holds for the bare
    programs from the ZIPs, for the copies inside the game app from each DMG
    and each ZIP, and for the debug ZIPs' `takclient`. The x64 copies ran
    under Rosetta.
- **Windows signatures.** `Get-AuthenticodeSignature` on oden-win reports
  **Valid** (CN=Curtis Edge, timestamped) for 9 signed files, the same count
  as 0.8.11. They are the setup `.exe`, plus `takclient.exe`, `takserver.exe`,
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
  `takserver` (extracted, reporting `0.8.12 (build v0.8.12)`, protocol v293)
  was started on the Linux checking host against the retail install. The flags
  were `--data <retail> --local --no-auth --seed 1` on a loopback-only scratch
  port, with a scratch `HOME` and `XDG_DATA_HOME`.
  - One headless harness client from the `-O2` Debug build of the tag source
    joined it with `TAK_AI_LEVEL=6 --mpai --time 60` on Inner Circle.
  - The server logged `game 1: DEFAULT AI profile ai/DEFAULT.txt from the base
    data` (the accepted old wording), and the game ended
    `hash=14849c00bc6e2a19 units=4 err=none end=timelimit`. That is the Linux
    Retail AI 60 s hash.
  - No desync or referee-suspect line appeared.
- **Packaged client and editor command lines (new).** These were run from the
  extracted Fedora 44 x86-64 RPM under `env -i` with a fresh scratch `HOME`,
  `XDG_DATA_HOME` and `XDG_CONFIG_HOME` (all deleted afterwards) and
  `SDL_VIDEODRIVER=dummy`.
  - `takclient --help` exits 0 and lists `--game-mod <pack>` ("with
    --play-map: tick only this mods/game pack in that room") beside
    `--play-map`, `--version` and `--data`.
  - `cartographer` has no `--version` flag. `cartographer --version` prints
    its usage and exits 2, the same as any unrecognised argument. Every
    packaged x86-64 `cartographer` (Debian, Ubuntu, Fedora, openSUSE, Arch,
    Slackware and the x64 debug tarball) starts and prints that usage.
  - As a working run, the RPM's `cartographer "Inner Circle" --data <retail>
    --bundle <scratch>/ic.kmp` loaded the map (96 × 96 blocks, 4 start
    positions, 215 features, 1261 sections for `zhon`), wrote a 379,088-byte
    `.kmp` and exited 0.
- **Streaming local copy in packages.** Every shipped `takclient` contains
  FFmpeg's MP4 muxer: all 29 copies in the Linux packages and tarballs, the
  Windows installer and ZIPs, and the macOS ZIPs and debug ZIPs, plus the game
  apps from both DMGs.
  - Each has the muxer's `video/mp4` MIME string and the `frag_keyframe`
    movflags option, next to the FLV muxer's `video/x-flv`.
  - Functionally, the tag CI's "Streaming encode test" ran `stream_test` on
    all 16 Linux package jobs and on the Windows x64, Windows ARM64, macOS x64
    and macOS arm64 builds.
  - Each run wrote a fragmented local copy (`2 fragments, video 51-76 samples
    2.52-2.56s, audio 2.51-2.60s`). No run printed the `SKIP local copy` line.
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

  As in 0.8.11, TAK Cartographer's app has only `EDITOR-FONT-LICENSE.txt`,
  which covers its Noto Sans Mono editor font.
- **Mods folder.** No package, installer, ZIP, DMG or app contains an
  `overrides/` or `mods/` directory, and none contains `click.hpi`.
- **macOS microphone.** The game app's `Info.plist` carries
  `NSMicrophoneUsageDescription` in both DMGs and both ZIPs. The
  `com.apple.security.device.audio-input` entitlement is on the game app's
  `takclient` (DMG and ZIP, both architectures) and on the bare ZIP
  `takclient`. It is absent from `takserver`, `crusades_admin` and
  `cartographer`.
- **macOS Retina.** `NSHighResolutionCapable` is `true` in the `Info.plist` of
  both apps, from both DMGs and both ZIPs.
- As in 0.8.1 to 0.8.11, no packaged Linux `takclient` has a `NEEDED` entry
  for libsecret, GLib, GIO or GObject.
- `docs/release-0.8.12-notes.md`, this report, `docs/cartographer-guide.md`,
  `docs/img/cartographer/ai-mod.jpg` and `docs/img/title.jpg` are in the
  source tree at the tag. `kNetVersion` there is 293.
- Every `blob/v0.8.12/docs/...` link in the release body resolves to a file at
  the tag, including the seven section anchors (two in
  `cartographer-guide.md`, five in `user-guide.md`). The files are
  `cartographer-guide.md`, `cartographer-improvements-progress.md`,
  `multiplayer-design.md`, `release-0.8.12-validation.md` and `user-guide.md`.

Published with `gh release edit v0.8.12 --draft=false --latest` on 2026-10-24 04:55 UTC; the release is Latest with 29 assets.

## Screenshots

Only `docs/img/title.jpg` was retaken (`ea38fa99`, from branch
`task-release-0812-shots`): an optimized Debug build with the version set to 0.8.12 locally,
1920 × 1080 offscreen, cropped to 1440 × 1080, JPEG quality 90; it reads
VERSION 0.8.12. The README title alt text, the gallery footnote ("0.7.25 to
0.8.12 builds") and `docs/img/README.md` follow. Checked and kept: the lobby and
room pictures (no AI seat at level 6, so neither DEFAULT nor RETAIL appears);
`guide/campaign-briefing.jpg` and `campaign.jpg` (no order armed in a mission,
no speed change shown); the Cartographer AI window captures
(`docs/img/cartographer/ai-profiles.jpg`, `ai-group-rules.jpg`,
`ai-takengine.jpg`, `ai-mod.jpg`), made by the `cartographer_aiprofile*_ui`
tests' capture mode as the features landed (see
`docs/cartographer-improvements-progress.md`) and matching the window text at
`450a10b6` (one TAK ENGINE AI tab; the AI mod footer saying the install never
supplies `ai/takengine.txt`). No picture shows the flyer patrol fix. The shots commit landed on main as
`ea38fa99` unchanged (`title.jpg`, the README alt text and footnote,
`docs/img/README.md`).

## Limits

- No game mixing 0.8.12 with 0.8.11 or older was attempted: the protocols
  differ, so the server refuses it.
- All of 0.8.11's limits carry over: Legion units stand and wait more than
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
  accepted limits ([acceptance ledger](classic-ai-acceptance.md#accepted-limits)).
- New: our six levels' built-in tables are identical
  (`src/ai/builtinprofile.cpp`: every level's change list is empty), so
  `ai/takengine.txt` sets the same unit weights and limits for every level;
  they still differ in `ai::paramsFor` (Easy `limitScale` 50, Hard and Absurd
  150, the Defensive levels unbounded except a limit of 0) and behaviour.
- New: Cartographer **TEST IN GAME** (`--game-mod`) has not been tried by hand
  on Windows or macOS.
- Tag CI: no flakes. All four workflows passed on the first attempt with no
  reruns or force-cancels. None of the known flakes appeared, and no setup
  step hung.
