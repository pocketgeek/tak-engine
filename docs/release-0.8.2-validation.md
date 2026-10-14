# 0.8.2 release validation

Protocol **257** (0.8.0 and 0.8.1: 256), replay format **12**, generator
version **8**, campaign payload **4**, database schema **9** (all but the
protocol unchanged since 0.8.0).

Tag and publication: *Pending.*

## Scope and compatibility

0.8.2 follows 0.8.1 (protocol 256). After the 0.8.1 validation commit
`9f677c87`, three commits change the Legion simulation, its tests, CI and
documentation:

- **Walk-in-place livelock** (`3d4737ad`, protocol 256 -> 257). In the
  pinwheel, a blocked cell is a wall contact only when its blocked run is not
  all blocking features of TDF category trees, plant(s) or rocks inside 7 × 7
  cells (`LegionNavigator`'s `wallOrigin` / `loneCell`,
  `RetailMapFeatureType::treeOrRock`, set from the feature's TDF `category` in
  `src/sim/matchsetup.cpp`). A body blocked again on the cell a flow-around
  took it to does not flow straight back for `kSlideHold` = 60 hold updates
  (hashed `Member::slideCell` / `slideFrom`). `World::updateLegionStill` judges
  stillness by net headway over 8-tick windows (`LegionHeadway`); a window that
  stands although the body walked or turned to and fro latches
  `Unit::legionShuffle` (hashed with its window), which holds the body until it
  is a body length clear or `kLock` = 64 ticks pass. A standing body, and steps
  that do not turn it, request no turn (`turnReqBam` 0), so no `TurnDirection`
  reaches its scripts. New tests `legion_lonetrees`, `legion_slideback`,
  `legion_stillturn` and `legion_still` (`tools/legion_still_test.cpp`, added to
  the Legion CI build in `linux.yml`, `windows.yml` and `macos.yml`); the
  scenario, cost and navigation-checkpoint baselines retaken for Legion.
- **A forest is a wall** (`05855040`, review of `3d4737ad`, no baseline move).
  `loneCell` links blocked cells the plane's body cannot pass between (at most
  `footX` columns and `footZ` rows apart) instead of 8-connected runs, and its
  memo is keyed on a navigator `cellGen` bumped by every static change
  rectangle. New test `legion_forestedge`.
- **User acceptance** (`c100382f`): the six `LL-257` Retail-floor exceptions
  and the lane-crossing rise (6,578 -> 7,117) are recorded as accepted in
  `docs/legion-pathfinding.md` and `tools/scenarios/baseline.json`.

Every measured figure in the [release notes](release-0.8.2-notes.md) is taken
from those commits and the "Livelock: the 0.8.1 walk-in-place" section of
[the Legion notes](legion-pathfinding.md#livelock-the-081-walk-in-place-protocol-257),
with its setup. The commits record Retail as bit-identical: every Retail record
of the full scenario run equals the previous head's, and the Retail `--mpai`
hashes are unchanged.

**Compatibility.** `kNetVersion` is **257** in `src/net/protocol.h` at
`c100382f`, against 256 at `v0.8.1` and `v0.8.0`. `git diff v0.8.1..c100382f --
src` touches only `src/sim/` (`legion.cpp`, `matchsetup.cpp`, `retailmap.h`,
`sim.cpp`, `sim.h`) and `src/net/protocol.h`; nothing under `src/client/`.
`git diff v0.8.0..c100382f` touches nothing under `src/server/`, `src/ai/`,
`src/tnt/` (the map generator, `formatVer` 8), `src/campaign/`,
`src/server/crusades/` or `src/net/replayhdr.h` / `src/net/crusades.h`, so
replay format 12 (`kReplayFormat`), generator version 8, campaign payload 4
(`crusades.h` `kVersion`) and schema 9 (`kSchemaVersion`) are unchanged.

- **Multiplayer.** The server's Hello check (`src/server/server.cpp`) rejects a
  client whose protocol differs from `kNetVersion` ("protocol version
  mismatch"), as well as a different pure-retail gameplay-data hash. So **0.8.2
  cannot play with 0.8.0 or 0.8.1 in either direction**: 0.8.2 clients cannot
  join a 0.8.0/0.8.1 server, and 0.8.0/0.8.1 clients cannot join a 0.8.2
  server. The server and all players must update together.
- **Replays.** `supportedReplayProtocol` (`src/net/replayhdr.h`) accepts a
  recording only when its protocol equals `kNetVersion`, and the client's
  replay loader (`src/client/replayfile.cpp`) and the Crusades replay files
  (`src/server/crusades/replayfiles.cpp`) both use it. So 0.8.2 does not play
  0.8.0 or 0.8.1 replays, and those builds do not play 0.8.2 replays.
  `tools/replay_test.cpp` now pins `kNetVersion == 257`.
- **Maps and settings.** Generated-map recipes (generator 8) and saved `.kmp`
  maps carry over. No setting was added or removed, so a settings file reads
  the same in 0.8.0, 0.8.1 and 0.8.2.

`kNetVersion` must be re-checked immediately before tagging (in 0.8.0 the
protocol moved during preparation).

## Local preparation checks

*Pending.*

## Remote multiplayer sweep

*Pending.* Includes the open follow-up from 0.8.1: Retail `h-stress` (seed
2003) concluded at tick 5,606 (`c93ae6becfcde6ca`) in the 0.7.28 and 0.8.1
sweeps and at 5,634 in the 0.8.0 sweep.

## CI and packages

*Pending.*

## Screenshots

Done on 2026-10-14 with the 0.8.2 release-preparation build (`takclient
--version` 0.8.2, build `v0.8.1-4-gc100382fb06c-dirty` from the version bump and
the capture patch; Debug with `-O2`, OpenGL) and the same capture-only patch as
0.8.1, which is not committed. Each re-rendered image was inspected by eye
against its caption.

- **Why these.** No user-interface code changed since 0.8.1 (no `src/client/`
  change), so only the title's version string and pictures whose scene the
  Legion change moves are stale. A first render of the `flyers.jpg` scene showed
  the Zhon ground units in different places among Ulasem Arena's trees, so the
  three Legion scenes were re-rendered.
- **Gallery.** Re-rendered: `title.jpg` (shows VERSION 0.8.2) and `flyers.jpg`
  (the 44-unit Zhon formation 22 s after its move order; flyers still hold
  stations over the front of the ground).
- **User guide.** Re-rendered: `guide/flyers-retail-legion.jpg` (both halves,
  so the Retail/Legion pair comes from one build) and `guide/replay.jpg` (a
  fresh recording of the same scripted Legion move, played back by the same
  build, 9 s of 16 s; 0.8.2 cannot play the 0.8.1 recording).
- **Kept.** Every other picture: the information-bar pictures re-rendered for
  0.8.1, `guide/fire-on-the-move.jpg` (Retail), the spectator, menu, Options
  and Cartographer pictures. None shows Legion movement or a changed screen.
- **Encoding.** As documented in [the capture notes](img/README.md): JPEG with
  full chroma resolution, quality 90 (title), 88 (gallery) and 86 (guide),
  guide images scaled from 1600 × 900 to 1280 × 720, the title cropped to its
  1440 × 1080 artwork.
- **Size.** Total tracked image bytes 15,769,010 -> 15,765,757 (−3 KB).
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve.
- **Not captured.** A unit standing instead of walking on the spot cannot be
  told from a still picture; Windows and macOS rendering.

## Limits

*Pending.*
