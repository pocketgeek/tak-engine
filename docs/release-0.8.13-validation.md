# 0.8.13 release validation

Protocol **293** (unchanged from 0.8.12), replay format **14**, generator
version **9**, campaign payload **4**, database schema **9**.

Tagged `v0.8.13` at `cfab0cdb` (full `cfab0cdb07bb57b3b11149aa29cc1f2f96a59ab5`, annotated tag object `2a076761b27bb3209e32500f41688c239d3800ec`, "TAK Engine 0.8.13") and published on 2026-10-24 11:32 UTC.

## Scope and compatibility

0.8.13 follows 0.8.12 (protocol 293). After the 0.8.12 validation commit
`1745b3d2`, main reached `c579cab5` in 3 commits, all linear (no merges).
`git diff --stat v0.8.12..c579cab5 -- src` touches 9 files: `src/cartographer/`
(`aiprofileswindow.cpp`, `chrome.cpp`, `dialogs.cpp`, `editorstate.h`,
`main.cpp`, `session.cpp`) and `src/client/` (`gameview.h`,
`gameview_hud.cpp`, `gameview_render.cpp`). Nothing under `src/sim/`,
`src/net/`, `src/ai/`, `src/server/`, `src/campaign/`, `src/tnt/` or
`src/net/replayhdr.h` changed. The other changes are tests
(`tools/cartographer_ui_test.cpp`, `tools/cartographer_placement_test.cpp`, two
new `add_test` lines in `CMakeLists.txt`) and docs (`docs/cartographer-guide.md`,
`docs/user-guide.md`).
After `c579cab5`, main gained `bb22812e` (CI only: the Linux packages
workflow pins the Slackware mirror to mirrors.kernel.org; no source, test or
doc change). The preparation commits sit on top of `bb22812e`, cherry-picked
linearly: `18c53580` (screenshots), `c5b11728` (version 0.8.13, notes, this
report's skeleton), `9feb6625` (doc completeness fixes) and the commit filling
this report. `git fetch` just before landing showed `origin/main` at
`bb22812e`, nothing else.
Later, before the tag, two more commits landed, neither touching source: `69228074`
(README and the 0.8.11 notes: the `#default` links now point to the user
guide's `#retail-ai`) and `cfab0cdb` (macOS workflow: retry `hdiutil verify`
up to 5 times, 10 s apart). `cfab0cdb` is the tagged commit.

The changes:

- In game: `a41beb28` (the armed-order text banner, such as **ATTACK: CLICK
  TARGET**, and the objectives-panel offset it needed are removed; retail shows
  an armed order only by its cursor and pressed order button).
- Cartographer: `90342ab7` (a bare launch opens an empty window, title
  **Cartographer**, **NO MAP OPEN**; `Session::noDocument` and
  `Editor::noMap()`; map commands, tools and canvas input inert; the empty
  window is never dirty; cancelling New/Open or closing an AI mod returns to
  it), `c579cab5` (held Esc no longer quits; the failed TEST IN GAME message is
  worded by what was tested, `playtestAiMod`; a map that fails to open at launch
  ends on the empty window after OPEN FAILED).

No hashed simulation state changed: no commit touches `src/sim/`, `src/net/`,
`src/ai/` or `src/server/`.

**Compatibility.** `kNetVersion` is **293** in `src/net/protocol.h` at
`c579cab5`, as at `v0.8.12`. `kReplayFormat` is **14** in
`src/net/replayhdr.h`, as at `v0.8.12`. The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** `Server::handshake` (`src/server/server.cpp`) rejects a
  Hello whose protocol differs from `kNetVersion` or whose pure-retail
  gameplay-data hash differs from the referee's. The Hello's build id is read
  and ignored (`(void)build`), and the release string `tak::kVersion` is never
  sent in the handshake. So **0.8.13 and 0.8.12 clients and servers play
  together in any mix**; 0.8.11 or earlier (protocol 290 or lower) cannot join
  either, nor either join an older server.
- **Replays.** `supportedReplayProtocol` accepts a recording when its format is
  1..14 and its protocol equals `kNetVersion`; the header's `engineVersion` is
  only displayed. So 0.8.13 plays 0.8.12's protocol-293 replays and 0.8.12
  plays 0.8.13's; 0.8.11's protocol-290 replays are refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generator-8/9 recipes,
  `mods/` packs and `ModCache/*.takmods` carry over. No settings key was added
  or removed.

**No mixed-version check was run.** No 0.8.12 client joined a 0.8.13 server (or
the reverse), and no 0.8.12 replay was played on 0.8.13. Compatibility rests on
the source: the handshake compares only `kNetVersion` and the gameplay-data
hash, and replays need an equal protocol, both unchanged from 0.8.12.

## Local preparation checks

Testing is reduced for this release, by the user's rule (minimal testing). The
checks ran on Linux, cores 4-23, with an optimized Debug build (`build-o2`,
`-O2 -g`, ccache + mold, `TAK_TEST_DATA` = the retail install), made fresh in a
detached worktree at `c579cab5`, all targets; `--version` there reports `0.8.12
(build v0.8.12-4-gc579cab5a86c)` (the CMake version was still 0.8.12 at that
commit). No Release, `-O0` Debug, Clang or ASan suite was run, and no repeat
runs.

- **Full CTest, once, on optimized Debug (`-O2`) at `c579cab5`: 607/608
  passed** (`-LE nightly -j16`, 677 s real, `rc=8`, scratch HOME and
  XDG_DATA_HOME). 770 tests are defined; the 164 `nightly`-labelled ones were
  excluded. The new and changed Cartographer tests passed inside this run:
  `cartographer_blank_launch_ui` and `cartographer_failed_open_ui` (both new),
  `cartographer_ui`, `cartographer_aimod_ui`, `cartographer_aiprofile_ui`,
  `cartographer_recovery` and the `cartographer_place_*` set. The one failure
  was **`ai_mod_profile`, which hit its 600 s timeout** and printed nothing; run
  alone right afterwards, once, it **passed in 56 s**. In 0.8.12's full run
  under the same load it passed in 59 s (0.8.11's final suite had the same
  timeout and also passed alone in 56 s), so ten times longer looks like one of
  its three games hanging intermittently rather than load. No code it covers
  changed since `v0.8.12`; recorded as an intermittent hang, not investigated.
  Logs: `~/tak-tmp/scratch/release-0813/ctest.out` and
  `ctest-rerun-ai_mod_profile.out`.
- **Detmath guard** (`tools/check-detmath.sh`): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): g++ and
  clang++ at -O0/-O2/-O3 all give `dcef618cd2e4d558`, the 0.8.12 golden. The
  aarch64 legs were skipped (no target libc locally); the macOS CI leg covers
  ARM64. Log: `~/tak-tmp/scratch/release-0813/check-determinism.log`.
- **`--mpai` golden hashes.** Inner Circle, `takserver --local --no-auth
  --seed 1`, a clean scratch profile per game, optimized Debug binaries at
  `c579cab5`, `TAK_LEGION=1` for Legion and `TAK_AI_LEVEL=6` for the Retail AI.
  All five games ended `err=none end=timelimit` with no desync or
  referee-suspect line, and every hash equals 0.8.12's.

  | Mode | Length | Hash (units) | 0.8.12 |
  | --- | --- | --- | --- |
  | Retail | 60 s | `b353410003e98dd5` (4) | `b353410003e98dd5` |
  | Retail | 300 s | `a3d953803a064191` (13) | `a3d953803a064191` |
  | Legion | 60 s | `0825aeb9e8139203` (4) | `0825aeb9e8139203` |
  | Legion | 300 s | `ce5c4ba7562e53b9` (18) | `ce5c4ba7562e53b9` |
  | Retail AI | 60 s | `14849c00bc6e2a19` (4) | `14849c00bc6e2a19` |

  The Retail AI server again logged `DEFAULT AI profile ai/DEFAULT.txt` (log
  wording only, as in 0.8.12). Logs: `~/tak-tmp/scratch/release-0813/mpai/`.
- **Final source** (the preparation commits on `bb22812e`): every `build-o2`
  target rebuilt at `9feb6625`, `takclient --version` reports `0.8.13 (build
  v0.8.12-8-g9feb6625cb2f)`, and `ctest -L quick` passed 500/500 (139 s). This
  report's fill commit changes only this file. Log:
  `~/tak-tmp/scratch/release-0813/final-quick.out`.

## Remote multiplayer sweep

**Not run for this release.**
Confirmed: no commit since `v0.8.12` touches `src/sim/`, `src/net/`, `src/ai/`
or `src/server/` (only the client HUD, the Cartographer, tests, docs and CI
changed), the protocol stays 293, and all five `--mpai` hashes match 0.8.12 on
Linux, Windows and macOS. The sweep's 37 scenarios would replay 0.8.12's table
(in turn 0.8.10's, `docs/release-0.8.10-validation.md`).

## CI and packages

Main CI on the final source, with no reruns:

- On `c579cab5`: Windows, Determinism and macOS arm64 passed. macOS x64 failed
  once on the known `crusades_client_network_test` "complete replay not
  published" flake (1 failure in the last 40 macOS runs). Linux packages failed
  on Slackware 15.0: the mirrors.slackware.com redirector sent it to
  mirror.techrich.hk, whose certificate chain the runner could not verify. That
  is fixed by `bb22812e`.
- The runs on `bb22812e` and `1886cc7f` were cancelled as superseded.
- On `69228074`: Windows (40084595912) and Linux packages (40084595978, Slackware
  included) passed. macOS arm64 (40084595986) failed in the App bundle + DMG
  step, after all tests passed: `hdiutil verify` said "Resource temporarily
  unavailable" right after `create`. `cfab0cdb`'s retry fixes it.
- The main runs on `cfab0cdb` (Windows 40086909456, macOS 40086909467, Linux
  40086909494) were cancelled, because the tag runs build the same commit.

On the exact tag (`cfab0cdb`, annotated tag object `2a076761`), all four
workflows passed on the first attempt, with no reruns and no force-cancels.

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/40086931128):
  started manually on the tag because the workflow is path-filtered.
  `unit-tests` (8/8), `detmath` and `sim-equiv` passed. The GCC and Clang
  x86-64 builds at O0/O2/O3 all agree on golden `dcef618cd2e4d558`, the same as
  0.8.12. This workflow runs no `--mpai` game.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/40086915661):
  all 16 native package jobs and the release attachment, first attempt.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/40086915668):
  x64, ARM64 and the x64 signing/package job, first attempt.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/40086915695):
  both builds plus the **Sign + notarize** job for each architecture, first
  attempt. Apple accepted every submission. The job replaced the unsigned
  macOS ZIPs and DMGs in the draft with signed, notarized and stapled ones
  (11:02 UTC on 2026-10-24).

The release was created as a draft with these notes, and tagged CI attached
the packages. All **29 assets** were downloaded after the macOS signing
finished.
- They are the same set as 0.8.12 with the version swapped. None has size 0 or
  an upload state other than "uploaded". Each one matched GitHub's SHA-256
  digest and byte size before publication.
- Sizes barely moved from 0.8.12. The release assets range from -0.011% to
  +0.060% (-2.7 KB to +6.9 KB), and the debug archives from -0.010% to +0.010%
  (-3.0 KB to +3.4 KB).
- There are no new dependencies. Debian and Ubuntu packages still depend only
  on `libc6`, RPMs require only libc, libm and the loader, and the Windows x64
  import tables of `takclient.exe`, `takserver.exe`, `cartographer.exe` and
  `crusades_admin.exe` are identical to 0.8.12's.
- Each package, installer and ZIP ships the same file list as its 0.8.12
  counterpart (no file added or removed). The DMGs hold the same two apps and
  `Applications` link, and each DMG app's file list equals the same app's
  from the ZIP.

- **Build ids.** The client and server in every Linux package carry
  `v0.8.13`. That covers Debian 12 and 13, Ubuntu 22.04, 24.04 and 26.04,
  Fedora 44 and openSUSE Leap 16.0 (each for both architectures), plus Arch,
  Slackware and both Linux debug tarballs. Installer, DMG and ZIP names carry
  `0.8.13`.
  - On Linux, the x86-64 `takclient` and `takserver` report
    `0.8.13 (build v0.8.13)`. That holds for Debian 12 and 13, Ubuntu 22.04,
    24.04 and 26.04, Fedora 44, openSUSE Leap 16.0, Slackware and the x64
    debug tarball. So does the Arch `takserver`. The Arch `takclient` needs
    glibc 2.44, newer than the checking host's 2.43, so only its build string
    was checked. For the arm64 binaries, only the build string `v0.8.13` was
    checked.
  - On Windows 11, the x64 Windows ZIP's `takclient.exe` and `takserver.exe`
    report `0.8.13 (build v0.8.13)`. For the ARM64 `takclient.exe` and
    `takserver.exe`, the installer's and the debug ZIPs', only the build
    string `v0.8.13` was checked.
  - On the arm64 MacBook, `takclient` and `takserver` report
    `0.8.13 (build v0.8.13)` for both architectures. This holds for the bare
    programs from the ZIPs, for the copies inside the game app from each DMG
    and each ZIP, and for the debug ZIPs' `takclient`. The x64 copies ran
    under Rosetta.
- **Windows signatures.** `Get-AuthenticodeSignature` on oden-win reports
  **Valid** (CN=Curtis Edge, timestamped) for 9 signed files, the same count
  as 0.8.12. They are the setup `.exe`, plus `takclient.exe`, `takserver.exe`,
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
  `takserver` (extracted, reporting `0.8.13 (build v0.8.13)`, protocol v293)
  was started on the Linux checking host against the retail install. The flags
  were `--data <retail> --local --no-auth --seed 1` on a loopback-only scratch
  port, with a scratch `HOME`, `XDG_DATA_HOME` and `XDG_CONFIG_HOME`.
  - One headless harness client joined it with `TAK_AI_LEVEL=6 --mpai --time
    60` on Inner Circle. The client was the `-O2` Debug build at `9feb6625`,
    whose source is identical to the tag (only docs and CI changed after it).
  - The server logged `game 1: DEFAULT AI profile ai/DEFAULT.txt from the base
    data` (the accepted old wording), and the game ended
    `hash=14849c00bc6e2a19 units=4 err=none end=timelimit`. That is the Linux
    Retail AI 60 s hash.
  - No desync or referee-suspect line appeared.
- **Packaged client and editor command lines.** These were run from the
  extracted Fedora 44 x86-64 RPM under `env -i` with a fresh scratch `HOME`,
  `XDG_DATA_HOME` and `XDG_CONFIG_HOME` (all deleted afterwards).
  - `takclient --help` exits 0 and lists `--play-map <snapshot.kmp>`,
    `--game-mod <pack>`, `--version` and `--data`.
  - `cartographer --version` still prints its usage and exits 2, as in 0.8.12.
  - As a working run, `cartographer "Inner Circle" --data <retail> --bundle
    <scratch>/ic.kmp` under `SDL_VIDEODRIVER=dummy` loaded the map (96 × 96
    blocks, 4 start positions, 215 features, 1261 sections for `zhon`), wrote a
    379,088-byte `.kmp` and exited 0, the same as 0.8.12.
  - **Bare launch (new).** `cartographer --data <retail>` with no map was run
    for 6 s under `SDL_VIDEODRIVER=offscreen`, and once more under `dummy`.
    Both times it was still running at 6 s.
    - Its stderr shows only the hidden placeholder (`editing 'Untitled' (),
      128x128 blocks`, 0.8.12 prints the same line), so the empty window
      itself was not seen. No screenshot was possible: there is no X server
      on the checking host, and a release build has no capture hook.
    - Stopping it by PID with SIGTERM, which SDL turns into a window close,
      ended it at once with exit 0. It wrote no recovery file, as expected
      for an empty window that is never dirty.
    - The 0.8.12 RPM's `cartographer`, launched the same way, kept running
      after SIGTERM on its unsaved-map prompt and had written a recovery lock,
      so it had to be killed.
    - The empty window's contents are covered by `cartographer_blank_launch_ui`
      (local checks above).
- **Streaming local copy in packages.** Every shipped `takclient` contains
  FFmpeg's MP4 muxer: all 29 copies in the Linux packages and tarballs, the
  Windows installer and ZIPs, and the macOS ZIPs and debug ZIPs, plus the game
  apps from both DMGs.
  - Each has the muxer's `video/mp4` MIME string and the `frag_keyframe`
    movflags option, next to the FLV muxer's `video/x-flv`.
  - Functionally, the tag CI's "Streaming encode test" ran `stream_test` on
    all 16 Linux package jobs and on the Windows x64, Windows ARM64, macOS x64
    and macOS arm64 builds.
  - Each run wrote a fragmented local copy (`2 fragments, video 47-76 samples
    2.52-2.59s, audio 2.51-2.60s`). No run printed the `SKIP local copy` line.
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

  As in 0.8.12, TAK Cartographer's app has only `EDITOR-FONT-LICENSE.txt`,
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
- As in 0.8.1 to 0.8.12, no packaged Linux `takclient` has a `NEEDED` entry
  for libsecret, GLib, GIO or GObject.
- `docs/release-0.8.13-notes.md`, this report,
  `docs/img/cartographer/empty-window.jpg`, `docs/img/cartographer/new-map.jpg`
  and `docs/img/title.jpg` are in the source tree at the tag. `kNetVersion`
  there is 293.
- Every `blob/v0.8.13/docs/...` link in the release body resolves to a file at
  the tag, including its two section anchors
  (`cartographer-guide.md#the-first-screen` and `user-guide.md#orders`). The
  files are `cartographer-guide.md`, `release-0.8.13-validation.md` and
  `user-guide.md`.

`--mpai` on Windows and macOS was run on optimized Debug builds (`-O2`) of the
tag `cfab0cdb`, built on oden-win (MSYS2 MINGW64, GCC) and on the arm64
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
Determinism run (40086931128) runs no `--mpai` game. Its cross-build check
agrees on golden `dcef618cd2e4d558`.

Published with `gh release edit v0.8.13 --draft=false --latest` on 2026-10-24 at 11:32 UTC (the release was created as a draft with the notes first, whose links were rewritten to `blob/v0.8.13`). `gh api repos/pocketgeek/tak-engine/releases/latest` returns `v0.8.13` with 29 assets: <https://github.com/pocketgeek/tak-engine/releases/tag/v0.8.13>.

## Screenshots

Retaken on branch `task-release-0813-shots` (`9e42f24c`, landed as `18c53580`):

- `docs/img/title.jpg`: VERSION 0.8.13, from an `-O2` build with the version
  set to 0.8.13 locally (`~/tak-tmp/scratch/release-0813/shots/title.py`); a
  pixel diff against the 0.8.12 file shows only the version digits changed.
  The README title alt text and gallery footnote ("0.7.25 to 0.8.13 builds")
  follow it.
- `docs/img/cartographer/new-map.jpg`: retaken; the New Map dialog opened with
  **File → New map** over the dimmed empty window (NO MAP OPEN in the status
  bar), whole 921 × 691 window.
- `docs/img/cartographer/empty-window.jpg`: new; the empty window of a bare
  launch, added to the guide's "The first screen".

Both Cartographer pictures come from the `cartographer_ui_test` workflow
`blank-launch` (passed) with `TAK_EDITOR_TEST_CAPTURE`, SDL `offscreen` and a
scratch profile, plus a capture-only patch, not committed
(`~/tak-tmp/scratch/release-0813/shots/carto-capture.patch`).
`docs/img/README.md` records both. No picture showed the removed armed-order
banner (the top-left area of every gallery, guide and quick-start picture was
checked), so none was retaken for it; every other picture is kept.

## Limits

- No game mixing 0.8.13 with 0.8.12 was attempted before landing;
  compatibility rests on the unchanged protocol, replay format and source
  above (see Scope; no mixed-version check was run at all, including after
  publication).
- All of 0.8.12's limits carry over: Legion units stand and wait more than
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
- Tag CI: no flakes. All four workflows passed on the first attempt with no
  reruns or force-cancels. None of the known flakes appeared, and no setup
  step hung.
