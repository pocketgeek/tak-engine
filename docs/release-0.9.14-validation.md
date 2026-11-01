# 0.9.14 release validation

Protocol **308** (was 307 in 0.9.13), replay format **15** (unchanged),
generator version **9** (unchanged).

Tag `v0.9.14` -> `c92da57dc6a736296d8e7cf8d1db408998b43fad` (the preparation
commit); annotated tag object `a76907c780d30b8ffde7e7f8787831216595ef8b`.
Published 2026-11-01 05:53:26 UTC as the Latest release.

## Scope and compatibility

0.9.14 follows 0.9.13 (protocol 307; tag `v0.9.13` -> `4ad86ec6`, validation
commit `64a62b73`). On main since then:

- `c173afe7` the selection ring: retail's root-plate box, centre and turn
  (`4fd0d0`, `546fa0`, `535d50`). Display only.
- `0a7f4681` and its revert `f44c56be`: no net change.
- `53058c27` `CLAUDE.md`: a build dir keeps its generator.
- `36745684` the hardware cursor stays up across a screen change.
- `143557be` AI level 7, Passive, protocol 308; the lobby's level order;
  `Difficulty::Passive` (the lobby's Defensive) renamed `Difficulty::Defensive`.
- This release's preparation: version, CHANGELOG, README, notes, this report,
  title picture, and `tools/replay_test.cpp` pinning protocol 308.

**Compatibility.** `kNetVersion` is **308**, `kReplayFormat` **15**, generator
**9**. So 0.9.14 plays with no 0.9.13 or older client or server, and refuses
every 0.9.13 (protocol 307) replay. **Hashed state:** unchanged: a Passive seat's
sim config is an Easy seat's (`ai_behavior_test`), and the gates below equal
0.9.13's. No map, recipe, package or settings format changed.

## Local preparation checks

On Linux, cores 4-23, scratch `HOME` and XDG dirs, data and logs in
`~/tak-tmp/scratch/release-0914/`, on `143557be` with the preparation edits.

- **Full suite** (`ctest -j 12`, Debug `-O2 -g`, `TAK_TEST_DATA` set, 904 s): 845
  of 846 passed. The one failure was `replay`, which pinned protocol 307; pinned
  to 308, it passes (rerun alone, not the whole suite again).
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): golden
  `dcef618cd2e4d558` from g++ and clang++ at -O0, -O2 and -O3, unchanged
  ("DETERMINISM OK"). The aarch64 legs were skipped (no target libc here).
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`, twice (42 and 44 s for the
  seven in parallel). All seven equal 0.9.13's in both runs, `err=none`, and no
  desync, mismatch or referee-suspect line in any log:

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `8690eaee7b16b4e6` | `269f3da1835a17ea` |
| Legion | `b558575535124afc` | `a860707b7b4113e6` |
| Retail AI (`TAK_AI_LEVEL=6`) | `12003fba552b8696` | not run |
| Crusades, Retail pathfinding | not run | `6f8d58fdcd304fc7` |
| Crusades, Legion | not run | `4db379ba59111b0c` |

- **Passive:** `ai_behavior_test` (no exploring in 26 game minutes with nothing in
  sight, Easy's wave at a seen enemy, deposits near and far), `server_ai_level`
  (levels 0..7 round-trip a real server's lobby, 8 and up store Normal), and a
  600 s `--mpai` game with `TAK_AI_LEVEL=7`, `err=none`.

## Screenshots

`docs/img/title.jpg` retaken, reads VERSION 0.9.14 (looked at); see
`docs/img/README.md`.

## Desync sweeps

None was run, by the project's testing rule; no hashed state changed.

## CI and packages

All four tag workflows ran on `c92da57d` (tag `v0.9.14`) on 2026-11-01 from about
05:19 UTC and all passed on attempt 1: Linux packages 41276454053, Windows
41276454050, macOS 41276454134 and Determinism 41276476783, whose GCC and Clang
builds agree on golden `dcef618cd2e4d558` ("DETERMINISM OK", unchanged). The
`main` runs of the same commit were cancelled as duplicates.

All 29 assets were downloaded fresh and match GitHub's SHA-256 digests and sizes.
The file set is identical to 0.9.13's: the same 29 asset names, and the file lists
of all 28 extracted packages, installer, zips and debug archives match after
replacing the version string (the 2 DMGs had no 0.9.13 extraction to compare). 18
assets grew, by at most 5,722 bytes (+0.02%, openSUSE x86_64 rpm; largest relative
+0.03%, Ubuntu 24.04 arm64 deb); 11 shrank, by at most 2,712 bytes (-0.02%, the
Windows x64 installer).

All 75 release `takclient`, `takserver` and `cartographer` binaries (48 Linux ELF,
9 Windows including the installer's three, 18 macOS in the zips, apps and DMGs)
carry the `v0.9.14` build string and no other `v0.x.y` string. None of the 48 ELF
binaries has a libva, libdrm, libsecret, GLib or libsqlite `NEEDED` entry, and none
of the 75 contains `__vaDriverInit_`, `SQLite format` or `sqlite3_`. The `TAK_`
names are 0.9.13's and none of the forbidden names appears. Licence files are in
every package, zip, installer and app, and all 29 `STREAMING-LICENSES.txt` copies
carry the OpenSSL notice. No `mods/`, `overrides/`, `click.hpi`, ManaPost or
CUTbugfix is shipped. The packaged Linux `takclient --help` lists only `--data`,
`--play-map`, `--game-mod` and `--version`. Every x86-64 package and the Linux x64
debug archive report `0.9.14 (build v0.9.14)`; the Arch `takclient` needs GLIBC
2.44 and cannot run on this Fedora host, as before.

The Fedora 44 x86-64 RPM's `takserver` listened on protocol v308 with retail
gameplay hash `d6000f4fa9095a51` and ran the Retail AI game (Inner Circle, seed 1,
scratch profile dirs, the Debug client of `c92da57d`, `TAK_AI_LEVEL=6 TAK_SPEED=80
--mpai --time 60`): `hash=12003fba552b8696 units=4 err=none end=timelimit`, the
expected value, with no desync or referee-suspect line. The draft's body equals the
prepared one, and every `blob` `v0.9.14` link in it names a file at the tag.

## Signing

Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid** (timestamped,
"Curtis Edge") for the same 7 files as 0.9.13: the x64 installer and `takclient`,
`takserver` and `cartographer` in both the x64 and arm64 zips. The x64
`takclient.exe` and `takserver.exe` report `0.9.14 (build v0.9.14)`, and the arm64
ones carry the build string. They ran from a scratch copy with a scratch profile,
removed afterwards. The real profile's listing before the run was not captured, so
"identical before and after" was not checked this time; the listing after differs
from 0.9.13's only by a replay and a `settings.ini` time from hours earlier.

macOS: on the MacBook (no builds), both DMGs and all eight app copies pass `stapler
validate`, and `spctl -a -vv` accepts them as "Notarized Developer ID" (Curtis Edge,
L479Y59RN8). Each app passes `codesign --verify --strict --deep` and has
`NSHighResolutionCapable` true; the game apps have `NSMicrophoneUsageDescription`,
and only `takclient` has `audio-input`. The bare zip programs pass `codesign
--strict`; `spctl` calls them "not an app", as expected. `takclient` and `takserver`
report `0.9.14 (build v0.9.14)`. `otool -L` shows no `libssl`, `libcrypto`,
`libsqlite` or libopus dylib; both takclients contain `OpenSSL 3.5.8 25 Aug 2026`,
no SecureTransport symbols, and link Security.framework. `vtool -show-build` on all
24 Mach-O binaries reports `minos 14.0 sdk 15.5`, and all 8 apps have
`LSMinimumSystemVersion` 14.0 and version 0.9.14. The Mac's real profile listing
(107 files) was identical before and after, and the scratch folder was removed.

`--mpai` was not run on Windows or macOS, per the release rule.

## Publication

Published on 2026-11-01 05:53:26 UTC as the Latest release, 29 assets; 0.9.13 and
earlier releases are kept.

## Limits

- The cursor change was not exercised on a real display (no display here, and
  SDL's dummy driver has no cursors).
- The lobby's level cycling was not clicked through by hand.
- All of 0.9.13's limits carry over (see
  [release-0.9.13-validation.md](release-0.9.13-validation.md#limits)).
