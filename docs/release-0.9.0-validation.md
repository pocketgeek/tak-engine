# 0.9.0 release validation

Protocol **294** (was 293 in 0.8.12 to 0.8.14), replay format **15** (was 14),
generator version **9**, campaign payload **4**, database schema **9**.

TODO-LEAD: Tagged `v0.9.0` at `<sha>` (full `<full sha>`, annotated tag object `<obj>`, "TAK Engine 0.9.0") and published on `<date>` `<time>` UTC.

## Scope and compatibility

0.9.0 follows 0.8.14 (protocol 293). After the 0.8.14 validation commit
`7d49a46a`, main had not moved; the release branch `task-090-docs` starts at
`7d49a46a` and fast-forwards onto main. It carries the documentation overhaul
(three sets `4441306b`, `d971c4c2`, `cf58ac29`, each branched from `7d49a46a`
and merged as `56e86ce4`, `04f77b36`, `e53b4e82`; then `30007217`, `563a3a40`,
`e1f6073f`) and, linearly after it, these code commits:

- `5a3820f5` and `0f81db17`: the Retail AI and our AI levels read the shipped
  profiles' `araseige` as the Rolling Tower (`arasiege`):
  `classicUnitSpelling()` in `src/ai/classicprofile.cpp`, the built-in table in
  `src/ai/builtinprofile.cpp`. Server-side AI only.
- `00494013`: build sparkles. `buildMovementCode` defaults to 0 as retail's FBI
  parser does (`src/sim/sim.cpp`), and the cosmetic emitter's ceiling is the
  owner's world Y. The hashed restore path keeps the corner height. No shipped
  mobile builder lacks a `bmcode` line, so the HUD's repair-cursor test
  (`gameview_hud.cpp`, `buildMovementCode && !isStructure()`) sees the same
  values on shipped data.
- `18e66f65` and `41d33c43`: an open gate's doorway picks the ground
  (`GameView::unitUnderCursor`, retail `0x520530`); ctest `gate_pick` over
  `arangate`, `cregate`, `tarngate`, `verngate`. Client only.
- `737653ed`: area clear as retail (overlapped TNT features removed at load,
  fogged but explored features clearable, retail exits). **Hashed simulation
  change: protocol 294, replay format 15.**
- `b8762727`: clear orders to every selected reclaimer; removed features not
  drawn. Client only.
- `de4a383e` (docs) and `aedb827c` (CMake `VERSION 0.9.0`).

TODO-LEAD: the preparation commits on `aedb827c` (this report, the notes, the
screenshot branches and any fix), and whether `origin/main` moved before the
fast-forward.

`git diff --stat v0.8.14..aedb827c -- src` touches 18 files: `src/ai/`
(`builtinprofile.cpp`, `classicprofile.cpp`, `classicprofile.h`),
`src/client/` (`gameview.h`, `gameview_devharness.cpp`, `gameview_hud.cpp`,
`gameview_impl.cpp`, `gameview_render.cpp`, `renderframe.h`), `src/net/`
(`matchconfig.cpp`, `protocol.h`, `replayhdr.h`) and `src/sim/`
(`matchsetup.cpp`, `matchsetup.h`, `retailmap.h`, `retailreclaimarea.h`,
`sim.cpp`, `sim.h`). Nothing under `src/server/`, `src/tnt/`, `src/campaign/`
or `src/net/crusades.h` changed.

**Compatibility.** `kNetVersion` is **294** in `src/net/protocol.h` (293 at
`v0.8.14`). `kReplayFormat` is **15** in `src/net/replayhdr.h` (14 at
`v0.8.14`); format 15 appends the room's FOG OF WAR byte
(`ReplayHeader::fogExplored`). The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** `Server::handshake` (`src/server/server.cpp`) rejects a
  Hello whose protocol differs from `kNetVersion` with "protocol version
  mismatch (server 294, client 293) -- update your build"; the release string
  is never compared. So **0.9.0 plays with no 0.8.x client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.0 refuses every
  0.8.x (protocol 293 or lower) replay; Crusades battle replays recorded by an
  0.8.x server are shown as *Recorded replay version unsupported*
  (`src/client/crusadesscreen.cpp`). `tools/replay_test.cpp` pins
  `kNetVersion == 294` and `kReplayFormat == 15`.
- **Maps, mods and settings.** No settings key, map, recipe or package format
  changed. TODO-LEAD: confirm.

TODO-LEAD: whether any mixed-version check was run (expected: none; a 0.8.14
client against a 0.9.0 server should be refused at the handshake).

## Local preparation checks

TODO-LEAD: build (`build-o2`, `-O2 -g`, cores 4-23, `TAK_TEST_DATA`), commit,
and `--version` string.

- **Full suite.** TODO-LEAD: the one full `ctest` run on `-O2`
  (`-LE nightly`), passed/total, `rc`, time, log path. Note the new tests
  `gate_pick` and `reclaim_fanout` (Debug + `TAK_TEST_DATA`) and the updated
  `reclaimarea` and `navblock`.
- **Detmath guard** (`tools/check-detmath.sh`): TODO-LEAD.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): TODO-LEAD
  (expected `dcef618cd2e4d558`, unchanged from 0.8.14).
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`, a
  clean scratch HOME/XDG_DATA_HOME per game, `TAK_LEGION=1` for Legion,
  `TAK_AI_LEVEL=6` for the Retail AI). TODO-LEAD: results. Expected (new with
  protocol 294, measured at `de4a383e`):

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `89c5eadaa7290dbb` | `9b58b36177e3a767` |
| Legion | `b1a0afcc56c14990` | `1ddb894f6d73dee5` |
| Retail AI (`TAK_AI_LEVEL=6`) | `68f228cec489010b` | not run |

  These differ from 0.8.14's (Retail `b353410003e98dd5` / `a3d953803a064191`,
  Legion `0825aeb9e8139203` / `ce5c4ba7562e53b9`, Retail AI
  `14849c00bc6e2a19`) because of `737653ed` (the removed Inner Circle
  `zontree202`, the hashed cell owner byte, the AI's reclaim exits); the AI
  spelling and sparkle commits left all five unchanged.

- **Final source**: TODO-LEAD (rebuild, `--version`, `ctest -L quick`).

Failures: TODO-LEAD.

## Remote multiplayer sweep

TODO-LEAD. Unlike 0.8.11 to 0.8.14, this release changes hashed simulation
state under `src/sim/` and the protocol (294), so the reason earlier releases
gave for skipping the sweep (no `src/sim/` change, `--mpai` identical) does not
hold. State the plan here: either run the 37-scenario sweep per pathfinding
mode (`tools/desync-hunt-remote.sh`, isolated binaries and ports, never the
public takserver) and record the new table, or record why it was skipped (for
example: the change is confined to area clear and map-feature install, covered
by `reclaimarea_test`, `navblock_test`, `reclaim_fanout` and five
run-twice-identical `--mpai` games plus Windows and macOS `--mpai`).

## CI and packages

TODO-LEAD: main CI / tag CI runs (Determinism dispatched on the tag, Linux,
Windows, macOS with signing), attempts, reruns and force-cancels.

TODO-LEAD: assets (count, SHA-256 and sizes against GitHub's digests, size
change against 0.8.14, dependencies, file lists), build ids, Windows
signatures, macOS notarization, packaged server running the Retail AI (expect
`hash=68f228cec489010b` at 60 s, protocol v294), packaged client and editor
command lines, mods folder, streaming local copy, Opus static, font licences,
macOS microphone and Retina, and that every `blob/v0.9.0/docs/...` link in the
release body resolves at the tag.

TODO-LEAD: `--mpai` on Windows (oden-win) and macOS (arm64 MacBook) on `-O2`
Debug builds of the tag; all ten games against the table above.

TODO-LEAD: publication line (`gh release edit v0.9.0 --draft=false --latest`,
date and time, asset count). After publication the older GitHub releases are
deleted with their tags kept; record the tag count before and after
(`git ls-remote --tags origin`).

## Screenshots

TODO-LEAD: the shot branches, the build they were taken from, which of the 81
pictures in [`docs/img/SHOTLIST-0.9.0.md`](img/SHOTLIST-0.9.0.md) were retaken,
kept or added (the three new ones: `mp-connect`, `mp-connect-custom`,
`guide/game-browser`), and the commits they landed as.

## Limits

- No game mixing 0.9.0 with 0.8.x is possible: protocol 294 is refused by
  every older server and client. TODO-LEAD: confirm no check was run.
- New: reclaim reach and work time are still this engine's (retail works about
  four times slower and reclaims from footprint adjacency); the Shift
  order-trail preview of a queued area clear uses current sight only; flying
  reclaimers use a plain move.
- All of 0.8.14's limits carry over (see
  [release-0.8.14-validation.md](release-0.8.14-validation.md#limits)), except
  the mixed-version notes, which no longer apply.
- Tag CI: TODO-LEAD.
