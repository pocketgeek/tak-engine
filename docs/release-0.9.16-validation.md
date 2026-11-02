# 0.9.16 release validation

Protocol **309** (was 308 in 0.9.15), replay format **15** (unchanged), generator
version **9** (unchanged).

Tag `v0.9.16` -> `ede3d06bf60564eaf69ce68450c50aa37f895788` (the preparation
commit); annotated tag object `7f10eb89a0c0bdab0cb4d5e806ac31739240c227`.
Published 2026-11-02 09:19:19 UTC as the Latest release.

## Scope and compatibility

0.9.16 follows 0.9.15 (tag `v0.9.15` -> `8cb2d102`). One code commit, `60c0684c`,
carries the replay transport and world clone, the watch-only rule, the mobile rally
change, surrender, the result screen's doors, sign-in tokens, the benchmark's pathfinding
choice, the rebindable streaming key and the STARTING button. The work ran on branches
`task-replay-spectate`, `task-rally-exit`, `task-bench-path` and `task-session-token`,
each reviewed; the smaller menu changes were not.

**Compatibility.** `kNetVersion` is **309**. A rallied mobile output's first order is
hashed, so 0.9.16 plays with no 0.9.15 or older client or server and refuses their
replays. The gates below do not move, because the AI never repeats a build queue.

## Local preparation checks

On Linux, cores 4-23, scratch `HOME` and XDG dirs, logs in
`~/tak-tmp/scratch/release-0916/`, on `60c0684c`.

- **Full suite** (`ctest -j 12`, Debug `-O2 -g`, `TAK_TEST_DATA` set, 937 s): 852 of 852
  passed.
- **Cross-compiler golden hash:** `dcef618cd2e4d558`, unchanged ("DETERMINISM OK";
  aarch64 legs skipped, no target libc here).
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`, once on this commit: all seven equal
  0.9.15's, `err=none`, no desync, mismatch or referee-suspect line:

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `8690eaee7b16b4e6` | `269f3da1835a17ea` |
| Legion | `b558575535124afc` | `a860707b7b4113e6` |
| Retail AI (`TAK_AI_LEVEL=6`) | `12003fba552b8696` | not run |
| Crusades, Retail pathfinding | not run | `6f8d58fdcd304fc7` |
| Crusades, Legion | not run | `4db379ba59111b0c` |

- **Owner:** tried the replay and spectating work by hand and called it good. Which of
  the later changes were tried is not recorded.

## CI and packages

All four tag workflows ran on `ede3d06b` (tag `v0.9.16`) on 2026-11-02: Linux packages
41384825429, Windows 41384825251 and Determinism 41384860828 (golden
`dcef618cd2e4d558`, "DETERMINISM OK", unchanged) passed on attempt 1. macOS 41384825207
passed on attempt 2. On attempt 1, "Sign + notarize macOS arm64" notarized and stapled
both apps by 04:10 UTC, then waited on Apple's notary service for the arm64 DMG until
the job's 300-minute limit cancelled it at 09:10. The draft at that point held the
unsigned arm64 macOS files from the build job. Only that job was rerun, on the same
commit; it finished in two minutes and replaced the arm64 DMG and both arm64 macOS
zips. The tag was not moved.

All 29 assets were downloaded fresh after the rerun and match GitHub's SHA-256 digests.
The 30 Linux x86-64 binaries extracted (Debian 12/13, Ubuntu 22.04/24.04/26.04, Fedora
44, openSUSE Leap 16.0, Arch, Slackware, and the debug archive) carry the `v0.9.16`
build string and no other `v0.x.y` string. None has a libva, libdrm, libsecret, GLib or
libsqlite `NEEDED` entry or embedded VA or SQLite code, and no forbidden `TAK_` name
appears in a release binary. Every one that runs here reports `0.9.16 (build v0.9.16)`
from scratch profile dirs and leaves no files; the Arch `takclient` needs GLIBC 2.44 and
cannot run on this Fedora host, as before. The arm64 Linux packages were checked by
digest only.

The packaged `takserver` was not run against an AI game this time, and there is no
cross-version check: 0.9.16 plays with no earlier release.

## Signing

Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid** (timestamped,
"Curtis Edge") for the same 7 files as 0.9.15. The x64 `takclient.exe` and
`takserver.exe` report `0.9.16 (build v0.9.16)`, and the arm64 ones carry the build
string. The real profile's listing (374 lines) is identical before and after, and the
scratch copy was removed.

macOS: on the MacBook, both DMGs and all eight app copies (in the DMGs and in the
`takclient-0.9.16-macos-*.zip` files) pass `stapler validate`, and `spctl -a -vv`
accepts them as "Notarized Developer ID" (Curtis Edge, L479Y59RN8); this includes the
arm64 files from the rerun. Each app passes `codesign --verify --strict --deep`, has
`NSHighResolutionCapable` true, version 0.9.16 and `LSMinimumSystemVersion` 14.0; the
game apps have `NSMicrophoneUsageDescription`, and only `takclient` has `audio-input`.
Both takclients contain `OpenSSL 3.5.8 25 Aug 2026`. The bare zip programs pass
`codesign`; `spctl` calls them "not an app", as expected. `takclient` and `takserver`
report `0.9.16 (build v0.9.16)`, and every Mach-O binary reports `minos 14.0 sdk
15.5`. The Mac's real profile listing was identical before and after, and the scratch
folder was removed.

`--mpai` was not run on Windows or macOS, per the release rule.

## Publication

Published on 2026-11-02 09:19:19 UTC as the Latest release, 29 assets; 0.9.15 and
earlier releases are kept. Every `blob` `v0.9.16` link in the notes names a file at
the tag.

## Limits

- Not run: a real Game Select click and its fallback, an in-game reconnect after a
  dropped link on a token session, a real Surrender in a game, the benchmark panel on a
  real GPU, and a token session against a real TLS server outside the tests.
- All of 0.9.15's limits carry over (see
  [release-0.9.15-validation.md](release-0.9.15-validation.md#limits)).
