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
- `7c0325b6`, after the preparation commits below: reclaim as retail RECLAIM
  (`0x406250`, emulated): footprint-adjacent reach (`0x4d4e80`'s rectangle,
  no builddistance), `trunc(energy + 15)` ticks whatever the builder, no mana,
  a 34-tick fade with no rubble. Every reclaim path (clicks, area-clear
  children, a build's own clearing reclaims, corpses). **Hashed simulation
  change under the same protocol 294** (0.9.0 is still an unpublished draft;
  whether to bump to 295 is the lead's call);
  `src/sim/` (`legion.cpp`, `matchsetup.cpp`, `retailreclaim.h` new,
  `sim.cpp`, `sim.h`) and `src/client/` (`gameview_devharness.cpp`,
  `gameview_impl.cpp`). New ctest `reclaim`; `tools/navigation_checkpoints.json`
  retaken.

The preparation commits on top of `aedb827c`, all linear (no merges):
`55e3016d` (release notes, this report's skeleton, protocol 294 in every
guide), `6f5714a9` (review fixes: building older versions, overlapping
features, shot status), the three screenshot sets cherry-picked from their
branches (`182b1fd0` and `83d2d10f` Cartographer, `a69d5b8c` front end and
multiplayer, `4c39cd35` in-game), one test fix (`tools/mod_transfer_test.cpp`,
see *Failures* below) and the commit filling this report's local checks,
screenshots and scope, which also deletes `docs/img/SHOTLIST-0.9.0.md`. A
`git fetch` before the fast-forward showed `origin/main` still at `7d49a46a`.

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
  changed: `git diff v0.8.14..aedb827c` touches no `src/client/settings.*`,
  `src/tnt/`, `src/hpi/`, `src/campaign/` or `src/server/` file. Maps are read
  as before; the only map-related change is that the simulation drops the
  earlier of two overlapping TNT features at load (`737653ed`), which the
  Cartographer still shows and saves unchanged.

No mixed-version check was run (see *Local preparation checks*).

## Local preparation checks

These ran on Linux, cores 4-23, with an optimized Debug build (`build-o2`, `-O2 -g`, ccache + mold, `TAK_TEST_DATA` = the retail install), all targets, on the tree committed as `7c0325b6` (on top of `ef1b2b15`; `takclient --version` read `0.9.0 (build v0.9.0-1-gef1b2b15630b-dirty)` before the commit). The earlier checks at `aedb827c` (full suite 610/611 with the `mod_transfer` harness gap, fixed in the test before landing; Release build with vendored static libraries, `ldd` libc/libm/loader only, `--help` listing only `--data`, `--play-map`, `--game-mod` and `--version`) are superseded by the results below; logs in `~/tak-tmp/scratch/release-090/`. By the user's rule (minimal testing), no Release, `-O0` Debug, Clang or ASan suite was run, and no repeat runs.

- **Full suite.** One full `ctest` run (`-LE nightly -j16`, scratch HOME/XDG_DATA_HOME) passed **612/612** (357 s real), including the new `reclaim` test and `reclaimarea`, `clear_build`, `navblock`, `retail_script`, `ai_behavior`, `retailgap`, `reclaim_fanout`, `gate_pick`, `mod_transfer` and the `replay*` tests. Log: `~/tak-tmp/scratch/reclaim-fix/ctest.out`.
- **Detmath guard** (`tools/check-detmath.sh`): OK. The sim path has no direct libm transcendentals and no `std::atomic_ref`.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): g++ and clang++ at -O0/-O2/-O3 all give `dcef618cd2e4d558`, the same golden as 0.8.14 (unchanged; reclaim adds no detmath). The aarch64 legs were SKIPPED because there is no target libc or headers locally; the macOS CI leg covers ARM64.
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`, a clean scratch HOME/XDG_DATA_HOME per game, `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI), each game run **twice with identical hashes**. All ten games ended `err=none end=timelimit`, and no log has a desync or referee-suspect line.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d375d03a6c472773` (4 units) | `b69b4e0842ba4eef` (13 units) |
| Legion | `426ab0842b709c28` (4 units) | `0da23f54df7a1b9d` (18 units) |
| Retail AI (`TAK_AI_LEVEL=6`) | `c5846d769e0a7d93` (4 units) | not run |

They replace the area-clear baselines (`89c5eadaa7290dbb`, `9b58b36177e3a767`, `b1a0afcc56c14990`, `1ddb894f6d73dee5`, `68f228cec489010b`) only because `World::stateHash()` folds different reclaim fields: the AI never reclaims, and with a temporary patch that hashed the old fields all five old baselines and both navigation goldens came back exactly. The Retail AI server again logged `game 1: DEFAULT AI profile ai/DEFAULT.txt from the base data`; this is log wording only, as before. Logs: `~/tak-tmp/scratch/reclaim-fix/land-mpai/`.

- **Compatibility constants** (unchanged by `7c0325b6`): `kNetVersion` **294** (`src/net/protocol.h`, was 293 in `v0.8.14`) and `kReplayFormat` **15** (`src/net/replayhdr.h`, was 14; the header gains the room's FOG OF WAR byte). `tools/replay_test.cpp` pins `kNetVersion == 294` and `kReplayFormat == 15`, and checks that format 14 under protocol 293 is refused. Unchanged since `v0.8.14`: map generator `formatVer` 9 (`src/tnt/mapgen.h`), campaign `kVersion` 4 (`src/net/crusades.h`), `kSchemaVersion` 9 (`src/server/crusades/store.cpp`). The reclaim change alters hashed state without a protocol bump: a build from before `7c0325b6` that also says 294 would desync against 0.9.0, which is harmless because no 294 build was ever released (the first 0.9.0 tag, on `c7412bc5`, was an unpublished draft and was moved), so the protocol stays 294. The reclaim rules were also checked by hand in real retail (the user, 2026-10-24): a reclaimed tree pays no mana, and a 250-energy tree takes about the emulated energy + 15 ticks.
- **How versions are compared** (source check for the notes):
  - *Joining a server.* `Server::handshake` (`src/server/server.cpp`) rejects a Hello when `ver != kNetVersion` ("protocol version mismatch"), or when the client's pure-retail gameplay-data hash differs from the referee's. The client sends an empty build id ("version-gated only"); `tak::kVersion` is never compared. So **0.9.0 does not play with any 0.8.x client or server**: a 0.8.12-0.8.14 (protocol 293) client is refused by a 0.9.0 server and a 0.9.0 client by a 0.8.x server. Public-server operators must update `takserver` with their players.
  - *Replays.* A replay loads only when `supportedReplayProtocol(format, protocol)` holds (`1 <= format <= 15` and `protocol == kNetVersion`). 0.8.12-0.8.14 replays carry protocol 293, so **0.9.0 cannot play 0.8.x replays**, and 0.8.x cannot play 0.9.0's (format 15, protocol 294).

**No mixed-version check was run.** No 0.8.14 client joined a 0.9.0 server (or the reverse), and no 0.8.14 replay was opened on 0.9.0; the points above rest on the source and `replay_test`.

**Not checked by hand:** that retail pays no mana for a reclaim rests on the emulation of `0x406250` (a write watch on the player and its economy), not on a game of the real retail program under Proton.

Failures: none in the run above. (The earlier `mod_transfer` failure at `aedb827c` was a test-harness config gap after `737653ed`, fixed in the test: its peers set `roomMapRevealed`/`roomLosOff` from the room's FOG OF WAR option exactly as `net::matchConfigFor` does.)

## Remote multiplayer sweep

TODO-LEAD. Unlike 0.8.11 to 0.8.14, this release changes hashed simulation
state under `src/sim/` and the protocol (294), so the reason earlier releases
gave for skipping the sweep (no `src/sim/` change, `--mpai` identical) does not
hold. State the plan here: either run the 37-scenario sweep per pathfinding
mode (`tools/desync-hunt-remote.sh`, isolated binaries and ports, never the
public takserver) and record the new table, or record why it was skipped (for
example: the change is confined to area clear, reclaim and map-feature install, covered
by `reclaim_test`, `reclaimarea_test`, `navblock_test`, `reclaim_fanout` and five
run-twice-identical `--mpai` games plus Windows and macOS `--mpai`).

## CI and packages

TODO-LEAD: main CI / tag CI runs (Determinism dispatched on the tag, Linux,
Windows, macOS with signing), attempts, reruns and force-cancels.

TODO-LEAD: assets (count, SHA-256 and sizes against GitHub's digests, size
change against 0.8.14, dependencies, file lists), build ids, Windows
signatures, macOS notarization, packaged server running the Retail AI (expect
`hash=c5846d769e0a7d93` at 60 s, protocol v294), packaged client and editor
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

All 81 pictures on the 0.9.0 shot list (`docs/img/SHOTLIST-0.9.0.md`,
deleted once they landed) were
retaken by the 0.9.0 build (Debug `-O2`, from `aedb827c`, CMake `VERSION
0.9.0`), each shot by exactly one branch, and every `SHOT-0.9.0` marker was
removed:

- `task-090-shots-carto` (`28d7a187`): 34 Cartographer pictures, nine of them
  byte-identical to the old files (`cartographer.png`,
  `guide/cartographer-terrain.jpg`, `empty-window`, `new-map`,
  `terrain-variant`, `preferences`, `ai-mod`, `ai-group-rules`,
  `ai-takengine`). The old `cartographer.png` already had the five-tool
  toolbar; every whole-window picture shows the five tools.
- `task-090-shots-front` (`6a3515ac`): 24 front-end and multiplayer pictures,
  including the three new ones (`mp-connect`, `mp-connect-custom`,
  `guide/game-browser`); five byte-identical (`lobby`, `lobby-scenario`,
  `guide/random-map-lobby`, `campaign`, `guide/campaign-briefing`).
  `title.jpg` reads VERSION 0.9.0. The connect panels come from the menu's
  debug capture (no server contacted); the game browser used a private
  loopback `takserver`.
- `task-090-shots-game` (`153bb70b`): 23 in-game shots (24 captures;
  `quick/build-line.jpg` is a two-capture composite).

Capture-only patches were applied while building and reverted before each
commit. `docs/img/README.md` conflicts between the front and game branches
(rows in the same tables); each row keeps the version of the branch that owns
that picture, and carto's `cartographer.png` row is kept. They landed, cherry-picked
linearly onto the docs branch, as `182b1fd0` and `83d2d10f` (Cartographer, the
second renaming the `random-generator.jpg` and `test-lobby.jpg` rows to the
0.9.0 retake), `a69d5b8c` (front end and multiplayer) and `4c39cd35`
(in-game). After landing, no `SHOT-0.9.0` marker is left, every image a doc
references exists, and every image under `docs/img/` is referenced by a doc.

Small flaws carried over from the old pictures, left as they are:
`cartographer/unit-properties.jpg` has a map label (ARAARCH) drawn over the
dialog's title bar (a Cartographer drawing bug), and `hud.jpg`, `naval.jpg`,
`guide/hud-annotated.jpg` and the `guide/side-*.jpg` pictures have a stray
mouse pointer in the top-left corner.

## Limits

- No game mixing 0.9.0 with 0.8.x is possible: protocol 294 is refused by
  every older server and client. No mixed-version check was run.
- New: the Shift order-trail preview of a queued area clear uses current
  sight only; flying reclaimers use a plain move and keep the old reach; a
  reclaimed object's 34-tick fade is not drawn; the turn and COB build-stance
  time before reclaim work starts were not emulated; a reclaimer follows its
  target by feature id where retail looks it up by cell each wake.
- All of 0.8.14's limits carry over (see
  [release-0.8.14-validation.md](release-0.8.14-validation.md#limits)), except
  the mixed-version notes, which no longer apply.
- Tag CI: TODO-LEAD.
