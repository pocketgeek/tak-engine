# 0.8.3 release validation

Protocol **266** (0.8.2: 257), replay format **13** (0.8.2: 12), generator
version **8**, campaign payload **4**, database schema **9** (the last three
unchanged since 0.8.0).

Not yet tagged.

## Scope and compatibility

0.8.3 follows 0.8.2 (protocol 257). After the 0.8.2 validation commit
`cd1bda91`, 103 commits up to `b5c45401` change the lobby, the simulation in both
pathfinding modes, the server-side AI, the Cartographer, the tests and the
documentation. `git diff --stat v0.8.2..b5c45401 -- src` touches 74 files:
`src/sim/` (15), `src/client/` (20), `src/cartographer/` (21), `src/net/` (8),
`src/ai/` (2), `src/util/` (3), `src/cob/` (2), `src/crt/` (1),
`src/server/server.cpp` and `src/server/crusades/replayfiles.cpp`. Nothing under
`src/tnt/` (the map generator) or `src/campaign/` changed. The protocol steps,
each recorded in `src/net/protocol.h` and `docs/multiplayer-design.md`:

- **258** ALLOW SPECTATORS (`e00637a7`, lobby wire only).
- **259** MAP SCENARIO (`55929d8c`, review fixes `d18fb416`, `352598df`,
  `d1f69041`); replay format 12 -> 13 records the option.
- **260** START LOCATIONS: BY SLOT (`2b7c0bbc`, `4ce08d7c`).
- **261** retail healtime self-repair (`0e438b70`, `5afe9538`), hashed.
- **262** a flyer held in range of its attack target stops its velocity
  (`915eff4d`).
- **263** air combat: every flyer hold, retail impact dispatch and 3D area
  damage, the facing request at the led SweetSpot, AirToGround attack runs
  (`77616793`, `3d2ef9a5`, `59549b12`, `b8ff8565`, `54d89402`).
- **264** the 2026-10-14 Legion review, Legion only (`f7d1b40c`, `b2ecc86e`,
  `a8ed05b3`, `c3dcb932`, `1861aff9`, `9987e4fd`, `e5df18b7`, `af879323`,
  `cecdd0e1`).
- **265** retail path-search fidelity, all modes: a pending re-request survives
  displacement, the ground word keeps its live holder, production entity pools
  (`889f1b60`, `cee6ac55`, `6fcde5ea`, `9bfd090d`).
- **266** entity pools start at retail's maxunits and grow as needed
  (`7b0d9182`, `9ab1fe4c`).

Without a protocol change: the AI series (`3539ba13` to `34425983`: deposit
reservations, allies' home deposits, the nearest-ally rule, the stall timeout,
the short-of-mana lodestone within retail's base radius, Normal's builder
growth, Zhon's producers, the full-store economy, the per-type and difficulty
caps), the client-only spectator health bars (`173e28f7`), death cries
(`5a193bf0`, `e7e20d5d`, `65162d4b`), lobby start markers (`783794dc`), the
Cartographer fixes, BLOCKED tool, scrollbars, edge scrolling, fullscreen and
data-folder setting, and the per-test scratch profiles (`d3468b6b`, `646313e9`,
`04f142b3`). The scenario baseline was retaken for 263 (`650c977e`), 264
(`44c77cbd`, `44263867`), 265 (`a1247035`) and 266 (`b5c45401`, the user's
"accept all" ruling of 2026-10-15); `legion_check check --cumulative` passes
at `b5c45401` (31,753 keys, 0 failed).

Unlike 0.8.2, **Retail is not bit-identical to the previous release**: 261,
262, 263, 265 and 266 change Retail games. The `--mpai` Inner Circle seed 1
hashes recorded by `9ab1fe4c` (build-o2, `takserver --local --no-auth --seed 1`,
scratch XDG directories) are Retail 300 s `7535cb08ee787b75`, 60 s
`2c39430636a3dbda`; Legion 300 s `c707e1a8063bc0c7`, 60 s `532539f51d70840b`
(0.8.2's Retail 300 s was `95b05c8290b55317`).

Every measured figure in the [release notes](release-0.8.3-notes.md) is taken
from the commit that made the change, with its setup, or from the section of
`docs/legion-pathfinding.md`, `docs/pathfinding-port.md`,
`docs/ai-expansion-2026-09-29.md` or `docs/retail-engine.md` it cites.

**Compatibility.** `kNetVersion` is **266** in `src/net/protocol.h` at
`b5c45401`, against 257 at `v0.8.2`. `kReplayFormat` is **13** in
`src/net/replayhdr.h` (12 at `v0.8.2`); the map generator's `formatVer` is 8
(`src/tnt/mapgen.h`, no change under `src/tnt/`), the campaign payload's
`kVersion` 4 (`src/net/crusades.h`, unchanged) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`, unchanged).

- **Multiplayer.** The server's Hello check (`src/server/server.cpp`) rejects a
  client whose protocol differs from `kNetVersion` ("protocol version
  mismatch"), as well as a different pure-retail gameplay-data hash. So **0.8.3
  cannot play with 0.8.2 or earlier in either direction**: 0.8.3 clients cannot
  join a 0.8.2 or older server, and older clients cannot join a 0.8.3 server.
  The server and all players must update together.
- **Replays.** `supportedReplayProtocol` (`src/net/replayhdr.h`) accepts a
  recording only when its protocol equals `kNetVersion`; the client's replay
  loader (`src/client/replayfile.cpp`), the campaign replay path
  (`src/net/client.cpp`) and the Crusades replay files
  (`src/server/crusades/replayfiles.cpp`, which also refuses a recording with
  MAP SCENARIO on) all use it. So 0.8.3 does not play 0.8.2 or older replays,
  and those builds do not play 0.8.3 replays (format 13 is also newer than they
  read). `tools/replay_test.cpp` pins `kNetVersion == 266`.
- **Maps and settings.** Generated-map recipes (generator 8) and saved `.kmp`
  maps carry over. A 0.8.2 settings file reads the same in 0.8.3. 0.8.3 adds
  `gameCreate.allowSpectators` and lets `gameCreate.randomStarts` hold 2 (By
  Slot); 0.8.2 reads only `1`, `true` or `on` as Random, so it reads a saved By
  Slot as Fixed. Cartographer's `preferences.txt` gains its display, edge
  scrolling, UI scale and `blocked` keys.

`kNetVersion` must be re-checked immediately before tagging (in 0.8.0 the
protocol moved during preparation).

## Local preparation checks

Pending.

## Remote multiplayer sweep

Pending. Open follow-up to record: the Retail `h-stress` scenario (seed 2003)
ended at 5,606 in every sweep since 0.8.1 against 0.8.0's 5,634; the simulation
changed in this release, so a new value is expected.

## CI and packages

Pending.

## Screenshots

Done on 2026-10-15 with the 0.8.3 release-preparation build (`takclient
--version` 0.8.3, build `v0.8.2-104-gb5c45401929b-dirty` from the version bump
and the capture patch; Debug with `-O2`, OpenGL) and the capture-only patch of
0.8.2, merged onto 0.8.3's own Options capture hook and not committed. Each
re-rendered image was inspected by eye against its caption.

- **Why these.** The lobby gained MAP SCENARIO, BY SLOT and numbered start
  markers (the create screen's preview and the room's map); ALLOW SPECTATORS is
  multiplayer only and does not appear on the single-player screens pictured.
  The Cartographer gained the BLOCKED tool and browser scrollbars. Spectators
  now see every side's health bars, which changes every spectator view of a
  fight. The simulation changed in both modes, which moves the spawned and
  recorded scenes.
- **Gallery.** Re-rendered: `title.jpg` (shows VERSION 0.8.3), `battle.jpg` and
  `dragons.jpg` (health bars on every side), `flyers.jpg`, `lobby.jpg`,
  `lobby-scenario.jpg`, `room.jpg` (start markers in the four seated players'
  colours) and `cartographer.png` (five tools).
- **User guide.** Re-rendered: `guide/flyers-retail-legion.jpg` (both
  halves), `guide/replay.jpg` (a fresh recording played back by the same build;
  0.8.3 cannot play the 0.8.2 recording), `guide/fire-on-the-move.jpg` (now four
  of sixteen fallen at 14 s, against five in the 0.8.1 picture),
  `guide/benchmark.jpg`, `guide/scorecard.jpg`, `guide/tactical-dots.jpg` (both
  halves from one deterministic run) and `guide/cartographer-terrain.jpg`.
- **Kept.** The 0.8.1 information-bar pictures (`hud.jpg`, `naval.jpg`,
  `diplomacy.jpg`, the guide's HUD, selection strip, five side pictures, Shift
  queue, campaign briefing and Options pictures): their screens did not change,
  though the 0.8.3 simulation would place some units a little differently a few
  seconds in. `generated.jpg` (a spectator view with no unit in sight), the
  menus, the results and Crusades screens, `streaming.jpg`,
  `cartographer-model.png` and the 27 Cartographer guide pictures, whose
  whole-window shots still show four tool buttons (noted in
  [the capture notes](img/README.md)).
- **Encoding.** As documented in [the capture notes](img/README.md): JPEG with
  full chroma resolution, quality 90 (title), 88 (gallery) and 86 (guide),
  guide images scaled from 1600 × 900 to 1280 × 720, the title cropped to its
  1440 × 1080 artwork, `lobby-scenario.jpg` cropped to the lobby panel;
  `cartographer.png` is the editor's `--shot` PNG, stored as RGB.
- **Size.** Total tracked image bytes 18,875,806 -> 18,826,503 (−49 KB).
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve (a scripted check of 378 links in the nine changed files),
  and `tools/legion_doc_links.py` reports 61 citations resolved, 0 problems.
- **Not captured.** Multiplayer screens (ALLOW SPECTATORS, the NO WATCH
  button), BY SLOT in the room, the BLOCKED tool in the Cartographer guide's
  other pictures, motion (attack runs, self-repair), Windows and macOS
  rendering.

## Limits

Pending.
