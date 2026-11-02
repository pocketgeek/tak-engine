# 0.9.17 release validation

Protocol **309** (unchanged from 0.9.16), replay format **15** (unchanged), generator
version **9** (unchanged).

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

## Limits

- The new layouts were seen at 1024 × 768 and 1920 × 1080 with the modern font only:
  not at 4K, on a high-DPI display, or with the classic font.
- The streaming window was not seen with a long error beside BACK, nor during a live
  stream.
- All of 0.9.16's limits carry over (see
  [release-0.9.16-validation.md](release-0.9.16-validation.md#limits)).
