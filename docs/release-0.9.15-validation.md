# 0.9.15 release validation

Protocol **308** (unchanged from 0.9.14), replay format **15** (unchanged),
generator version **9** (unchanged).

Tag `v0.9.15` -> `8cb2d1022297651415207751d12af4c406d85bdb` (the preparation
commit); annotated tag object `28f349eeea74e8f18d4d635d37f84ba4b7b24de9`.
Published 2026-11-01 09:19:58 UTC as the Latest release.

## Scope and compatibility

0.9.15 follows 0.9.14 (tag `v0.9.14` -> `c92da57d`, validation commit `3779381c`).
One code commit: `88130300` removes the zoomed-out selection marker and
`projectilemodelscale.h`, display only, in `src/client` and
`tools/model_transform_test.cpp`. No file in `src/sim`, `src/net`, `src/ai` or
`src/server` changed. The preparation adds the version, CHANGELOG, README, notes,
this report and the title picture.

## Local preparation checks

On Linux, cores 4-23, scratch `HOME` and XDG dirs, logs in
`~/tak-tmp/scratch/release-0915/`, on `88130300` with the preparation edits.

- **Full suite** (`ctest -j 12`, Debug `-O2 -g`, `TAK_TEST_DATA` set, 850 s): 846
  of 846 passed.
- **Cross-compiler golden hash:** `dcef618cd2e4d558`, unchanged ("DETERMINISM OK";
  aarch64 legs skipped, no target libc here).
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`, twice (43 and 42 s): all seven
  equal 0.9.14's, `err=none`, no desync, mismatch or referee-suspect line:

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `8690eaee7b16b4e6` | `269f3da1835a17ea` |
| Legion | `b558575535124afc` | `a860707b7b4113e6` |
| Retail AI (`TAK_AI_LEVEL=6`) | `12003fba552b8696` | not run |
| Crusades, Retail pathfinding | not run | `6f8d58fdcd304fc7` |
| Crusades, Legion | not run | `4db379ba59111b0c` |

The owner tried the rendering changes and asked for the release; which machine was
not recorded, and neither change was rendered on the Neo's external monitor by this
preparation. (The copy of this report at the tag says "on the Linux build", which was
not known and is corrected here.)

## CI and packages

All four tag workflows ran on `8cb2d102` (tag `v0.9.15`) on 2026-11-01: macOS
41283380391, Windows 41283380390 and Determinism 41283409020 (golden
`dcef618cd2e4d558`, "DETERMINISM OK", unchanged) on attempt 1, and Linux packages
41283380370 on attempt 2. Attempt 1 failed in Slackware 15.0 x64's dependency step:
the package mirror's `dbus`, `extra-cmake-modules` and `flex` files failed their md5
checks, before anything was compiled. Only the failed jobs were rerun, on the same
commit, and they passed. The tag was not moved.

All 29 assets were downloaded fresh and match GitHub's SHA-256 digests and sizes. The
file set is identical to 0.9.14's: the same 29 asset names, and the file lists of all
extracted packages, installer, zips, debug archives and both DMGs match after
replacing the version string. 2 assets grew, by at most 790 bytes (+0.003%, macOS
arm64 debug zip); 27 shrank, by at most 8,147 bytes (-0.03%, openSUSE x86_64 rpm).

All 75 release binaries (48 Linux ELF, 9 Windows, 18 macOS) carry the `v0.9.15` build
string and no other `v0.x.y` string. None of the 48 ELF binaries has a libva, libdrm,
libsecret, GLib or libsqlite `NEEDED` entry, and none of the 75 contains
`__vaDriverInit_`, `SQLite format` or `sqlite3_`. The `TAK_` names and `NEEDED` lists
are identical to 0.9.14's for every binary, and no forbidden name appears. Licence
files are in every package, zip, installer and app, all 29 `STREAMING-LICENSES.txt`
copies carry the OpenSSL notice, and no `mods/`, `overrides/`, `click.hpi`, ManaPost
or CUTbugfix is shipped. The packaged Linux `takclient --help` lists only `--data`,
`--play-map`, `--game-mod` and `--version`. Every x86-64 package and the Linux x64
debug archive report `0.9.15 (build v0.9.15)`; the Arch `takclient` needs GLIBC 2.44
and cannot run on this Fedora host, as before.

The Fedora 44 x86-64 RPM's `takserver` listened on protocol v308 with retail gameplay
hash `d6000f4fa9095a51` and ran the Retail AI game (Inner Circle, seed 1, scratch
profile dirs, the Debug client of `8cb2d102`, `TAK_AI_LEVEL=6 TAK_SPEED=80 --mpai
--time 60`): `hash=12003fba552b8696 units=4 err=none end=timelimit`, the expected
value, with no desync or referee-suspect line.

**Plays with 0.9.14.** The 0.9.14 Fedora 44 RPM's `takserver` accepted the same 0.9.15
client with no protocol mismatch and ended on the same hash, `err=none`. The other
direction (a 0.9.14 client on a 0.9.15 server) and replay exchange were not run; both
rest on the protocol and format being equal and on the handshake comparing only those.

## Signing

Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid** (timestamped,
"Curtis Edge") for the same 7 files as 0.9.14. The x64 `takclient.exe` and
`takserver.exe` report `0.9.15 (build v0.9.15)`, and the arm64 ones carry the build
string. They ran from a scratch copy with a scratch profile, removed afterwards. The
real profile's listing (374 lines) was captured before the run and is identical after.

macOS: on the MacBook (no builds; macOS 26.6.2), both DMGs and all eight app copies
pass `stapler validate`, and `spctl -a -vv` accepts them as "Notarized Developer ID"
(Curtis Edge, L479Y59RN8). Each app passes `codesign --verify --strict --deep` and has
`NSHighResolutionCapable` true; the game apps have `NSMicrophoneUsageDescription`, and
only `takclient` has `audio-input`. The bare zip programs pass `codesign --strict`;
`spctl` calls them "not an app", as expected. `takclient` and `takserver` report
`0.9.15 (build v0.9.15)`. `otool -L` shows no `libssl`, `libcrypto`, `libsqlite` or
libopus dylib; both takclients contain `OpenSSL 3.5.8 25 Aug 2026`, no SecureTransport
symbols, and link Security.framework. `vtool -show-build` on all 24 Mach-O binaries
reports `minos 14.0 sdk 15.5`, and all 8 apps have `LSMinimumSystemVersion` 14.0 and
version 0.9.15. The Mac's real profile listing (115 files) was identical before and
after, and the scratch folder was removed.

`--mpai` was not run on Windows or macOS, per the release rule.

## Publication

Published on 2026-11-01 09:19:58 UTC as the Latest release, 29 assets; 0.9.14 and
earlier releases are kept. Every `blob` `v0.9.15` link in the notes names a file at
the tag.

## Limits

- All of 0.9.14's limits carry over (see
  [release-0.9.14-validation.md](release-0.9.14-validation.md#limits)).
