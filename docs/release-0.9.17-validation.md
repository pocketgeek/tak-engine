# 0.9.17 release validation

Protocol **309** (unchanged from 0.9.16), replay format **15** (unchanged), generator
version **9** (unchanged).

Tag `v0.9.17` -> `13e0a3a65ddbd473c11b04d3d174ae5fc4f86bb9` (the preparation
commit); annotated tag object `62d4926f1808a56a589e37f0bec576e0f7638122`.
Published 2026-11-02 14:56:18 UTC as the Latest release.

## Scope and compatibility

0.9.17 follows 0.9.16 (tag `v0.9.16` -> `ede3d06b`, validation commit `a286ed3a`). One
code commit, `7268d897`, changes the settings screens only: five Options rows become
dropdowns, UI Font becomes a Modern UI Font switch (same `uiFont` values), the Options
VOICE CHAT row goes, DEFAULTS and BACK move to the footer's edges on Options, Voice Chat
and Controls, and YouTube Streaming, Settings, Benchmark and Load Replay are laid out in
the Options unit with an Options-sized BACK. Nothing in `src/sim`, `src/net`, `src/ai`
or `src/server` changed. The owner saw screenshots of the changes, said they were all
good and asked for the release; which of them the owner tried in the game is not
recorded. The work was not reviewed.

**Compatibility.** `kNetVersion` stays **309**, so 0.9.17 plays with 0.9.16 in either
direction and plays its replays.

## Local preparation checks

On Linux, cores 4-23, scratch `HOME` and XDG dirs, logs in
`~/tak-tmp/scratch/release-0917/`, on `7268d897` with the 0.9.17 version line.

- **Full suite** (`ctest -j 12`, Debug `-O2 -g`, 876 s): 852 of 852 passed.
- **Cross-compiler golden hash:** `dcef618cd2e4d558`, unchanged ("DETERMINISM OK").
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`, once: all seven equal 0.9.16's,
  `err=none`, no desync, mismatch or referee-suspect line:

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `8690eaee7b16b4e6` | `269f3da1835a17ea` |
| Legion | `b558575535124afc` | `a860707b7b4113e6` |
| Retail AI (`TAK_AI_LEVEL=6`) | `12003fba552b8696` | not run |
| Crusades, Retail pathfinding | not run | `6f8d58fdcd304fc7` |
| Crusades, Legion | not run | `4db379ba59111b0c` |

- **Plays with 0.9.16.** The published 0.9.16 Fedora 44 RPM's `takserver` (protocol
  v309, retail gameplay hash `d6000f4fa9095a51`) ran the Retail AI game with the 0.9.17
  Debug client (`TAK_AI_LEVEL=6 TAK_SPEED=80 --mpai --time 60`): `hash=12003fba552b8696
  units=4 err=none`, the expected value, with no desync, mismatch or referee-suspect
  line. The other direction (a 0.9.16 client on a 0.9.17 server) and replay exchange
  were not run; both rest on the protocol and format being equal.
- **Screenshots:** `title.jpg`, `streaming.jpg`, `guide/options-graphics.jpg`,
  `guide/options-interface.jpg` and `guide/voice-chat.jpg` were retaken (see
  `docs/img/README.md`) and looked at.
- **Targeted tests during the work:** `options_autosave`, `voice`, `stream_key`,
  `stream_bitrate` and `benchmark_pathfinding` passed after each change.

## CI and packages

All four tag workflows ran on `13e0a3a6` (tag `v0.9.17`) on 2026-11-02: Determinism
41451343164 (golden `dcef618cd2e4d558`, "DETERMINISM OK", unchanged), Windows
41451329474 and macOS 41451329483 (both signing jobs about two minutes) passed on
attempt 1. Linux packages 41451329453 passed on attempt 4. Attempts 1 to 3 failed only
in Slackware 15.0 x64's dependency step: the runners could not connect to
`mirrors.kernel.org` (connection timed out), before anything was compiled; the mirror
answered normally from outside GitHub. Only the failed jobs were rerun, on the same
commit, and the tag was not moved.

All 29 assets were downloaded fresh and match GitHub's SHA-256 digests, and the asset
names are 0.9.16's with the version changed. Every extracted x86-64 Linux binary carries
the `v0.9.17` build string and no other `v0.x.y` string; none has a libva, libdrm,
libsecret, GLib or libsqlite `NEEDED` entry or embedded VA or SQLite code, and no
release binary contains a forbidden `TAK_` name. Each one that runs here reports
`0.9.17 (build v0.9.17)` from scratch profile dirs and leaves no files; the Arch
`takclient` needs GLIBC 2.44 and cannot run on this Fedora host, as before.

The Fedora 44 x86-64 RPM's `takserver` listened on protocol v309 with retail gameplay
hash `d6000f4fa9095a51` and ran the Retail AI game with the 0.9.17 Debug client
(`TAK_AI_LEVEL=6 TAK_SPEED=80 --mpai --time 60`): `hash=12003fba552b8696 units=4
err=none end=timelimit`, with no desync, mismatch or referee-suspect line.

## Signing

Windows: on oden-win, `Get-AuthenticodeSignature` reports **Valid** (timestamped,
"Curtis Edge") for the same 7 files as 0.9.16. The x64 `takclient.exe` and
`takserver.exe` report `0.9.17 (build v0.9.17)`, and the arm64 ones carry the build
string. The real profile's listing (374 lines) is identical before and after, and the
scratch copy was removed.

macOS: on the MacBook, both DMGs and all eight app copies pass `stapler validate`, and
`spctl -a -vv` accepts them as "Notarized Developer ID" (Curtis Edge, L479Y59RN8). Each
app passes `codesign --verify --strict --deep` and has version 0.9.17 and
`LSMinimumSystemVersion` 14.0; `takclient` and `takserver` report `0.9.17 (build
v0.9.17)`, and every Mach-O binary reports `minos 14.0`. The bare zip programs pass
`codesign`; `spctl` calls them "not an app", as expected. The Mac's real profile listing
(117 lines) was identical before and after, and the scratch folder was removed.

`--mpai` was not run on Windows or macOS, per the release rule.

## Publication

Published on 2026-11-02 14:56:18 UTC as the Latest release, 29 assets; 0.9.16 and
earlier releases are kept. Every `blob` `v0.9.17` link in the notes names a file at
the tag.

## Limits

- The new layouts were seen at 1024 × 768 and 1920 × 1080 with the modern font only:
  not at 4K, on a high-DPI display, or with the classic font.
- The streaming window was not seen with a long error beside BACK, nor during a live
  stream.
- All of 0.9.16's limits carry over (see
  [release-0.9.16-validation.md](release-0.9.16-validation.md#limits)).
