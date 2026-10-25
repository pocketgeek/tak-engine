# 0.9.0 release validation

Protocol **294** (was 293 in 0.8.12 to 0.8.14), replay format **15** (was 14),
generator version **9**, campaign payload **4**, database schema **9**.

Tagged `v0.9.0` at `2dc5e712` (full `2dc5e712ee3047f0c9d98ce8c616057d1cf04471`, annotated tag object `2d284ac0`, "TAK Engine 0.9.0") and published on 2026-10-25 03:36:57 UTC.

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

**Checked by hand:** besides the emulation of `0x406250` (a write watch on the player and its economy), the user played the real retail program on 2026-10-24: a reclaimed tree pays no mana, and the time to clear one matches the emulated energy + 15 ticks.

Failures: none in the run above. (The earlier `mod_transfer` failure at `aedb827c` was a test-harness config gap after `737653ed`, fixed in the test: its peers set `roomMapRevealed`/`roomLosOff` from the room's FOG OF WAR option exactly as `net::matchConfigFor` does.)

## Remote multiplayer sweep

The full 37-scenario table passed for both pathfinding modes, Retail and Legion, on **2026-10-25** (UTC). Both ran at `29b2a88a83e9`, the `v0.9.0` tag at the time (the final tag `2dc5e712` differs only by the debug-harness rename described under CI and packages) (protocol **294**, replay format **15**, build id `v0.9.0`). Each mode passed in a single pass with no retries. No seat was lost, and no `held the game` or `stopped responding` line appeared in any server log.

- **Retail:** 02:17-02:34 UTC (17 minutes including the gates), single pass.
- **Legion:** 02:34-03:03 UTC (28 minutes including the gates), single pass.
- **Repeats:** afterwards, one Retail and three Legion cases were repeated to check the reference changes (below).

Referees ran on `tak.pgnet.us` (24 runs, 10 at a time) and `vpn3.pgnet.us` (13 runs, 6 at a time), with optimized local clients pinned to CPUs 4-23.

- **Isolation:** each host ran its own referee binary, `~/takserver.sweep090`. Each mode had its own replay and map-cache folders on the remotes. The ports were 8690/8691 and 8700+ for Retail, 8790/8791 and 8800+ for Legion, and 8900+/9000+ for the repeats. The public `takserver` on `tak.pgnet.us` (port 7677) was not touched; it was the same process, 12 hours up, before and after.
- **Duration:** each scenario allowed ten simulated minutes (18,000 ticks) or ended at an actual result for that seat.
- **Coverage:** real seated players provide the client/referee checksum coverage. The two all-AI spectator scenarios per mode (`w-allai-stress`, `w-allai-bench`) test flow control, not hashes.
- **Mode:** chosen through the hosting client's room options (`TAK_LEGION=1` creates a Legion room). `TAK_WALL_TIMEOUT=3600` was set for both modes.
- **Replay headers:** every recorded referee replay header confirms the intended mode, replay format 15, protocol 294 and engine `0.9.0`. There are 37 headers per mode, seeds 2000-2036, each mapped to its case.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 (single pass) | 52/52 | 10,272 across 22 seat pairs, 0 mismatched | 2,636 across 51 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (single pass) | 52/52 | 10,224 across 22 seat pairs, 0 mismatched | 2,716 across 51 seat/referee comparisons, 0 mismatched |

Each run passed all three `--validate` gates before its table started:

- **Build identity:** `both at v0.9.0`.
- **Planted desync:** the referee reported `game 1: client 1 DESYNCED at tick 900`.
- **Game mod transfer:** `verified game mods transferred and incorrect loaded gameplay rejected`.

**Referee binary.** The gate checks only `tak.pgnet.us`; the binary on `vpn3.pgnet.us` is the same file (SHA-256 `addf706b8d31401ff20be4fd17256691f572e3cc324d83cedff85fbba989c154`, `--version` `0.9.0 (build v0.9.0)` on both). The referee was rebuilt from this exact source in the Ubuntu 24.04 container, as Debug `-O2 -g`, and needs at most GLIBC 2.38. The client is a Debug `-O2 -g` build of the same tag.

**No desyncs.** No natural `DESYNCED`, `REFEREE SUSPECT` or client-reported desync appeared in any case log; the only such line in each run is the planted one. Both sweeps ended with "no desyncs reported (52 seats, all completed)", and their lists of runs that did not complete were empty. The repeats ended with "no desyncs reported" (1 and 6 seats, all completed).

**What was compared.** A state hash is compared at every checkpoint, and the position digest as well wherever both sides carry one. Every game wrote a referee replay (37 per mode, matched to cases by the seed in the replay header).

**Harness unchanged.** `tools/desync-hunt-remote.sh` is unchanged since 0.8.8 (SHA-256 `df09e28844be1f399f9466a62e19c9cdf53298e5fdb9eb5dcefb5fca830ad56e`), and so is the checkpoint comparison script (SHA-256 `ebdd8dc6…7eafc4`). As in 0.8.9 and 0.8.10, the harness ran with the short `TMPDIR` `/home/pocket_geek/tk`, because a longer path overflows the ssh ControlPath limit.

**Seat budget.** Unlike 0.8.10, where one Legion `2h-stress` seat was lost on a loaded box, no seat was charged at all. Local load stayed around 5-7 during the sweep. The end-of-game `slot N disconnected -- waiting` lines in every log are the headless clients quitting after the game ended; they are not stalls.

### Reference ticks and hashes (0.9.0)

Hash and units are the seated host's final line.

**Hashes are not comparable with 0.8.10.** `World::stateHash()` folds different reclaim fields in 0.9.0 (`7c0325b6`), so every hash differs from 0.8.10 by construction. The "vs 0.8.10" column therefore compares the end, tick and units with the 0.8.10 table (protocol 289, same harness, hosts and seeds). The last remote sweep was 0.8.10; 0.8.11 to 0.8.14 skipped it.

**Expected noise.** The six `TAK_AUTOPLAY` cases never repeat a result. `h-lake`, `h-stress`, `2h-stress`, `2h-orders-stress` and `4h-gods` are load-sensitive. So "differs" in those rows is expected.

| Case | Retail end | Retail tick | Retail hash | Retail units | vs 0.8.10 | Legion end | Legion tick | Legion hash | Legion units | vs 0.8.10 |
| --- | --- | ---: | --- | ---: | --- | --- | ---: | --- | ---: | --- |
| `h-baseline` | timelimit | 18,000 | `e0bef7ba9d699da3` | 210 | same | timelimit | 18,000 | `8d99968895f3de8d` | 213 | same |
| `h-gods` | concluded | 14,626 | `b03c93e64c951c5d` | 104 | same | concluded | 15,383 | `ad2a20126c9a650c` | 82 | same |
| `h-crusades` | timelimit | 18,000 | `910a9c9075ebd098` | 245 | same | timelimit | 18,000 | `1f5e7bc74fe4d9b1` | 230 | same |
| `h-stress` | concluded | 4,891 | `c63c71c06553ac4f` | 8,596 | differs (tick 4,899 -> 4,891; units 8,593 -> 8,596) | concluded | 5,147 | `4c0430c6535a7052` | 7,305 | differs (tick 5,161 -> 5,147; units 7,301 -> 7,305) |
| `h-absurd` | timelimit | 18,000 | `4c8f928faf2ffe83` | 454 | same | timelimit | 18,000 | `73eb7d3a5f3b47ed` | 471 | differs (units 469 -> 471) |
| `h-fog-explored` | timelimit | 18,000 | `8b3b9f6a264fed60` | 188 | same | timelimit | 18,000 | `ce5cc363a40074fc` | 142 | same |
| `h-fog-full` | timelimit | 18,000 | `68ef706cfd2b09dc` | 216 | same | timelimit | 18,000 | `7095484b072a67e6` | 217 | same |
| `h-unitcap` | concluded | 14,780 | `da14c05dff5ab338` | 204 | same | timelimit | 18,000 | `19ec90a1c208ca5c` | 220 | same |
| `h-cramped` | concluded | 1,307 | `79887ca66a9dec4c` | 10 | same | timelimit | 18,000 | `f1bf0446b082ea29` | 29 | same |
| `h-naval` | timelimit | 18,000 | `a7d11c3181a75470` | 58 | same | timelimit | 18,000 | `1cab043b39e71524` | 88 | same |
| `h-naval-crus` | timelimit | 18,000 | `5858d43a1bed439b` | 70 | same | timelimit | 18,000 | `f14d470489daad23` | 88 | same |
| `h-lake` | concluded | 3,923 | `68b6f1d33e5a0b62` | 4,893 | differs (tick 3,864 -> 3,923; units 4,890 -> 4,893) | concluded | 4,268 | `7ee489a61a749138` | 4,905 | differs (tick 4,538 -> 4,268; units 4,791 -> 4,905) |
| `h-random-starts` | concluded | 15,702 | `2069a674d5d7aa86` | 228 | same | concluded | 12,289 | `897cbdadecd0da87` | 194 | same |
| `h-monarch-exp` | concluded | 12,231 | `2aa1dd887be5b7ac` | 89 | same | concluded | 17,804 | `20a13db0bd669788` | 82 | same |
| `h-mods-all` | timelimit | 18,000 | `a89ebd1a5beb2c72` | 209 | same | timelimit | 18,000 | `c956b37f7c1115dc` | 212 | same |
| `h-everything` | timelimit | 18,000 | `275eeedac3c0bb71` | 4,985 | differs (units 4,237 -> 4,985) | timelimit | 18,000 | `7bef3278f0468a63` | 5,167 | differs (units 4,237 -> 5,167) |
| `h-speed-1x` | timelimit | 18,000 | `e059ba7f5fb2694d` | 236 | same | timelimit | 18,000 | `e0b02aa4c5e1c221` | 246 | same |
| `h-two-castles` | timelimit | 18,000 | `2f03ee4664a94e9b` | 23 | same | timelimit | 18,000 | `b200d4fe20647a7f` | 4 | same |
| `2h-baseline` | timelimit | 18,000 | `d207e5eff496b2bf` | 173 | same | timelimit | 18,000 | `4e78ba06f356ba02` | 172 | same |
| `2h-gods` | concluded | 12,423 | `8f3020d1899948e1` | 104 | same | concluded | 13,059 | `1926c51770f0d008` | 61 | same |
| `2h-crusades` | timelimit | 18,000 | `fc44dc4aefcfb4cd` | 182 | same | timelimit | 18,000 | `0e0e845287ae2915` | 167 | same |
| `2h-stress` | concluded | 5,048 | `14d335ea1266d68f` | 9,102 | differs (tick 5,074 -> 5,048; units 9,096 -> 9,102) | concluded | 5,248 | `43a96c0a627f382c` | 8,427 | same |
| `2h-orders` | timelimit | 18,000 | `e7ecb8e289a2d648` | 181 | differs (units 179 -> 181) (autoplay) | timelimit | 18,000 | `3fa8cf923cde6853` | 192 | differs (units 175 -> 192) (autoplay) |
| `2h-orders-stress` | concluded | 5,063 | `c91664a5f5d82e4d` | 9,008 | differs (tick 13,590 -> 5,063; units 7,562 -> 9,008) (autoplay) | concluded | 5,401 | `64e3d78a20a45767` | 8,083 | differs (timelimit 18,000 -> concluded 5,401; units 5,166 -> 8,083) (autoplay) |
| `4h-orders` | timelimit | 18,000 | `18507a40a7134174` | 161 | differs (units 159 -> 161) (autoplay) | timelimit | 18,000 | `b8ba03c7e4698ba8` | 166 | differs (units 155 -> 166) (autoplay) |
| `3h-baseline` | timelimit | 18,000 | `3a0df12dbd0ec122` | 172 | same | timelimit | 18,000 | `c26b73f03f0b2386` | 148 | same |
| `4h-gods` | timelimit | 18,000 | `b35c1a464a586469` | 149 | same | timelimit | 18,000 | `669188d33e690a77` | 129 | differs (units 142 -> 129; repeat gave 142) |
| `net-rtt50` | timelimit | 18,000 | `9ca5ec4cda2d90f0` | 230 | same | timelimit | 18,000 | `1315731fc6c25ac3` | 227 | same |
| `net-rtt100` | timelimit | 18,000 | `29cc9cb12ee78c14` | 190 | same | timelimit | 18,000 | `c39c70e273424d38` | 204 | same |
| `net-jitter` | timelimit | 18,000 | `e647f5aba352bb1a` | 173 | same | timelimit | 18,000 | `9d73c7d065014fe2` | 196 | same |
| `net-rtt500` | timelimit | 18,000 | `e74521b6ec85fe9d` | 219 | same | timelimit | 18,000 | `ebc7ad06fe800efc` | 200 | same |
| `net-loss1` | timelimit | 18,000 | `5ac2d0be14c79b13` | 192 | same | timelimit | 18,000 | `11433d3b49194918` | 206 | same |
| `net-loss3-orders` | timelimit | 18,000 | `850abaf07436a723` | 188 | differs (units 190 -> 188) (autoplay) | timelimit | 18,000 | `b92bdc02ccd2773a` | 222 | same (autoplay) |
| `net-orders100` | timelimit | 18,000 | `16f76bb5e1caf8ef` | 185 | differs (units 183 -> 185) (autoplay) | timelimit | 18,000 | `aa1b1a76491532f1` | 193 | differs (units 187 -> 193) (autoplay) |
| `net-2h-rtt100` | timelimit | 18,000 | `0ce142f9a44e9a93` | 184 | differs (units 190 -> 184) (autoplay) | timelimit | 18,000 | `d1da47bc0fda2e1f` | 195 | differs (units 168 -> 195) (autoplay) |
| `w-allai-stress` | timelimit | 18,000 | `b11dd4b3cfacc1c5` | 5,392 | same | timelimit | 18,000 | `820ad6d250d9775d` | 3,925 | same |
| `w-allai-bench` | timelimit | 18,000 | `50b971f1800f6c4d` | 312 | same | timelimit | 18,000 | `ebce3e4e22e9cc37` | 344 | same |

### Comparison with 0.8.10

0.9.0 changes hashed simulation state (area clear, installed map features, room vision, reclaim reach and time with no reclaim mana, the AI building the Rolling Tower) and the protocol (294). Every hash differs from 0.8.10 by construction, so cases are compared on end, tick and units.

**Retail: 27 of 37 cases reproduce 0.8.10 exactly (end, tick and units).** The 10 that differ:

- **The six `TAK_AUTOPLAY` cases**, which never repeat.
- **Load-sensitive, the same class as in 0.8.10:** `h-stress` (4,899 -> 4,891, the tick a 0.8.10 repeat gave), `h-lake` (3,864 -> 3,923) and `2h-stress` (5,074 -> 5,048). Units are within 0.1% of 0.8.10.
- **`h-everything` moved for real:** it still reaches the time limit, but with 4,985 units instead of 4,237. A repeat reproduced it exactly (hash `275eeedac3c0bb71`, 4,985 units).

**Legion: 27 of 37 cases reproduce 0.8.10 exactly.** The 10 that differ:

- **Autoplay:** five of the six autoplay cases (`net-loss3-orders` happened to match).
- **Load-sensitive:**
  - `h-stress` (5,161 -> 5,147, which is 0.8.9's tick).
  - `h-lake` (4,538 -> 4,268).
  - `4h-gods` (142 -> 129 units). A repeat gave 142 units and yet another hash, `0542c08192175080`, so it is the same noise as in 0.8.10.
- **`h-everything` moved for real:** 4,237 -> 5,167 units at the time limit. A repeat reproduced it exactly (hash `7bef3278f0468a63`).
- **`h-absurd` moved for real, slightly:** 469 -> 471 units at the time limit. A repeat reproduced it exactly (hash `73eb7d3a5f3b47ed`), so it is stable, not noise.
- **`2h-stress`**, the 0.8.10 retry case, reproduced 0.8.10 exactly in the first pass.

**Why `h-everything` (and Legion `h-absurd`) moved.** `h-everything` is the one case that fields every unit type, and it is where most of the 28 `src/sim`/`src/ai` commits between `v0.8.10` and `v0.9.0` could reach a running battle. Those commits are mainly the Classic/Retail AI, the AI profile rework, the AI building the Rolling Tower, the flyer patrol lap, the reclaim rules and area clear. The move is consistent with those changes, but no bisect was done: say "consistent with", not "proven". Both new references reproduced exactly on a repeat.

There is no desync anywhere: every seat agreed with the referee at every checkpoint in every case, in both modes and in the repeats.

## CI and packages

The first tag attempts never became assets. Tag CI on `c7412bc5` was
cancelled (runs 40147387853, 40147387957, 40147387967, 40147387978). On
`29b2a88a`, [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/40153640718)
failed in both jobs' "Configure + build (debug)" step. A local in the debug
harness `gameview_devharness.cpp` was named `far`, which the Windows headers
define as a macro. Linux (40153640749), macOS (40153640784) and Determinism
(40153640827) passed there. The fix `2dc5e712` renames that local and
changes nothing else. The tag was moved to it before publication.

On the final tag (`2dc5e712`, annotated tag object `2d284ac0`), all four
workflows passed on the first attempt, with no reruns and no force-cancels.

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/40155682111):
  started manually on the tag because the workflow is path-filtered.
  `unit-tests` (8/8), `detmath` and `sim-equiv` passed. The GCC and Clang
  x86-64 builds at O0/O2/O3 all agree on golden `dcef618cd2e4d558`, the same as
  0.8.14. This workflow runs no `--mpai` game.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/40155682386):
  all 16 native package jobs and the release attachment, first attempt.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/40155682404):
  x64, ARM64 and the x64 signing/package job, first attempt.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/40155682378):
  both builds plus the **Sign + notarize** job for each architecture, first
  attempt. Apple accepted every submission. The job replaced the unsigned
  macOS ZIPs and DMGs in the draft with signed, notarized and stapled ones
  (03:15 UTC on 2026-10-25).

The release was created as a draft with these notes, and tagged CI attached
the packages. All **29 assets** were downloaded after the macOS signing
finished.
- Every asset was uploaded between 03:09 and 03:15 UTC. That is after every
  `29b2a88a` run had ended, so all of them come from the `2dc5e712` runs.
- They are the same set as 0.8.14 with the version swapped. None has size 0 or
  an upload state other than "uploaded". Each one matched GitHub's SHA-256
  digest and byte size before publication.
- Every asset grew a little from 0.8.14. The release assets grew by +0.004% to
  +0.116% (+0.5 KB to +36.6 KB), and the debug archives by +0.069% to +0.112%
  (+27.5 KB to +97.7 KB).
- There are no new dependencies. Debian and Ubuntu packages still depend only
  on `libc6`. The Fedora and openSUSE RPM requirements and the Arch
  `depend`/`optdepend` lines are identical to 0.8.14's. The Windows x64 import
  tables of `takclient.exe`, `takserver.exe`, `cartographer.exe` and
  `crusades_admin.exe` are identical to 0.8.14's.
- Each package, installer and ZIP ships the same file list as its 0.8.14
  counterpart (no file added or removed). In particular, the new
  `CHANGELOG.md` is not packaged; the only `README.md` shipped is the existing
  `share/doc/tak-engine/systemd/README.md`. The DMGs hold the same two apps and
  `Applications` link, and each DMG app's file list equals the same app's from
  the ZIP (12 files for the game, 6 for TAK Cartographer).

- **Build ids.** All 58 `takclient` and `takserver` binaries in the 29 assets
  carry `v0.9.0`, and none carries an older version string. That covers every
  Linux package for both architectures, Arch, Slackware, both Linux debug
  tarballs, the Windows installer, all Windows and macOS ZIPs and debug ZIPs,
  and the copies inside the game apps. Installer, DMG and ZIP names carry
  `0.9.0`.
  - On Linux, the x86-64 `takclient` and `takserver` report
    `0.9.0 (build v0.9.0)`. That holds for Debian 12 and 13, Ubuntu 22.04,
    24.04 and 26.04, Fedora 44, openSUSE Leap 16.0, Slackware and the x64
    debug tarball. So does the Arch `takserver`. The Arch `takclient` needs
    glibc 2.44, newer than the checking host's 2.43, so only its build string
    was checked, as in 0.8.14. For the arm64 binaries, only the build string
    `v0.9.0` was checked.
  - On Windows 11, the x64 Windows ZIP's `takclient.exe` and `takserver.exe`
    report `0.9.0 (build v0.9.0)`. For the ARM64 ZIP's `takclient.exe` and
    `takserver.exe` (checked on oden-win), and for the installer's and the
    debug ZIPs' copies, only the build string `v0.9.0` was checked.
  - On the arm64 MacBook, `takclient` and `takserver` report
    `0.9.0 (build v0.9.0)` for both architectures. This holds for the bare
    programs from the ZIPs, for the copies inside the game app from each DMG
    and each ZIP, and for the debug ZIPs' `takclient`. The x64 copies ran
    under Rosetta.
- **Windows signatures.** `Get-AuthenticodeSignature` on oden-win reports
  **Valid** (CN=Curtis Edge, timestamped) for 9 signed files, the same count
  as 0.8.14. They are the setup `.exe`, plus `takclient.exe`, `takserver.exe`,
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
  `takserver` (extracted, reporting `0.9.0`, `build v0.9.0`, protocol v294)
  was started on the Linux checking host against the retail install. The
  flags were `--data <retail> --local --no-auth --seed 1` on a loopback-only
  scratch port, under `env -i` with a scratch `HOME`, `XDG_DATA_HOME` and
  `XDG_CONFIG_HOME`.
  - One headless harness client joined it with `TAK_AI_LEVEL=6 --mpai --time
    60` on Inner Circle. The client was the `-O2` Debug build of the tag
    `2dc5e712`, reporting `0.9.0 (build v0.9.0)`.
  - The server logged `game 1: DEFAULT AI profile ai/DEFAULT.txt from the base
    data` (the accepted old wording), and the game ended
    `hash=c5846d769e0a7d93 units=4 err=none end=timelimit`. That is the Linux
    Retail AI 60 s hash in the table above.
  - No desync or referee-suspect line appeared.
- **Packaged client and editor command lines.** These were run from the
  extracted Fedora 44 x86-64 RPM under `env -i` with a fresh scratch `HOME`,
  `XDG_DATA_HOME` and `XDG_CONFIG_HOME` (all deleted afterwards).
  - `takclient --help` exits 0 and lists only `--data`,
    `--play-map <snapshot.kmp>`, `--game-mod <pack>` (with its
    `(mods/game/always_enabled packs apply as well)` line) and `--version`.
    Its text is byte-identical to 0.8.14's.
  - `cartographer --version` still prints its usage and exits 2, as in 0.8.14.
  - As a working run, `cartographer "Inner Circle" --data <retail> --bundle
    <scratch>/ic.kmp` under `SDL_VIDEODRIVER=dummy` loaded the map (96 × 96
    blocks, 4 start positions, 215 features, 1261 sections for `zhon`), wrote a
    379,088-byte `.kmp` and exited 0, the same as 0.8.14.
- **Mods folder.** No package, installer, ZIP, DMG or app contains an
  `overrides/`, `mods/` or `always_enabled/` directory, and none contains
  `click.hpi` or any other `.hpi`. As in 0.8.14, the repository's
  `mods/cosmetic/always_enabled/click/click.hpi` is not shipped.
- **Streaming local copy in packages.** Every shipped `takclient` contains
  FFmpeg's MP4 muxer: all 29 copies in the Linux packages and tarballs, the
  Windows installer and ZIPs, and the macOS ZIPs and debug ZIPs, plus the game
  apps from both DMGs.
  - Each has the muxer's `video/mp4` MIME string and the `frag_keyframe`
    movflags option, next to the FLV muxer's `video/x-flv`.
  - Functionally, the tag CI's "Streaming encode test" ran `stream_test` on
    all 16 Linux package jobs and on the Windows x64, Windows ARM64, macOS x64
    and macOS arm64 builds.
  - Each run wrote a fragmented local copy (`2 fragments, video 53-76 samples
    2.52-2.66s, audio 2.51-2.65s`). No run printed the `SKIP local copy` line.
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

  As in 0.8.14, TAK Cartographer's app has only `EDITOR-FONT-LICENSE.txt`,
  which covers its Noto Sans Mono editor font.
- **macOS microphone.** The game app's `Info.plist` carries
  `NSMicrophoneUsageDescription` in both DMGs and both ZIPs. The
  `com.apple.security.device.audio-input` entitlement is on the game app's
  `takclient` (DMG and ZIP, both architectures) and on the bare ZIP
  `takclient`. It is absent from `takserver`, `crusades_admin` and
  `cartographer`.
- **macOS Retina.** `NSHighResolutionCapable` is `true` in the `Info.plist` of
  both apps, from both DMGs and both ZIPs.
- As in 0.8.1 to 0.8.14, no packaged Linux `takclient` has a `NEEDED` entry
  for libsecret, GLib, GIO or GObject.
- `docs/release-0.9.0-notes.md`, this report, `CHANGELOG.md`,
  `docs/img/title.jpg` and `docs/img/mp-connect.jpg` are in the source tree at
  the tag. `kNetVersion` there is 294 and `kReplayFormat` 15.
- Every `blob/v0.9.0/...` link in the release body resolves to a file at the
  tag, including its four section anchors
  (`release-0.8.14-notes.md#moving-your-always-on-archives`,
  `retail-engine.md#reclaim-icd-0x406250-emulated-2026-10-24`,
  `user-guide.md#reclaiming` and `user-guide.md#updating`). The files are
  `CHANGELOG.md` and, under `docs/`, `cartographer-guide.md`,
  `development.md`, `guessed-area-clear-2026-09-26.md`, `public-server.md`,
  `quick-start.md`, `release-0.8.14-notes.md`, `release-0.9.0-validation.md`,
  `retail-engine.md`, `streaming.md` and `user-guide.md`. The only other link
  is `releases/latest`.

`--mpai` on Windows and macOS was run on optimized Debug builds (`-O2`) of the
tag `2dc5e712`, built on oden-win (MSYS2 MINGW64, GCC) and on the arm64
MacBook (Apple Clang). The settings were Inner Circle, `takserver --local
--no-auth --seed 1`, a scratch profile, `TAK_LEGION=1` for Legion and
`TAK_AI_LEVEL=6` for the Retail AI. All ten games ended `err=none
end=timelimit` and match the Linux table above on both hosts:
- Retail: 60 s `d375d03a6c472773` (4 units), 300 s `b69b4e0842ba4eef`
  (13 units).
- Legion: 60 s `426ab0842b709c28` (4 units), 300 s `0da23f54df7a1b9d`
  (18 units).
- Retail AI: 60 s `c5846d769e0a7d93` (4 units). Each host's server logged
  `DEFAULT AI profile ai/DEFAULT.txt from the base data`.

The Linux table was taken at `7c0325b6`. Since then `src/` changed only by
the debug-harness rename in `2dc5e712`. Neither host logged a desync or
referee-suspect line. The tag's dispatched Determinism run (40155682111) runs
no `--mpai` game. Its cross-build check agrees on golden `dcef618cd2e4d558`,
as did 0.8.14's (40101686205).

Published with `gh release edit v0.9.0 --draft=false --latest` on 2026-10-25 at 03:36:57 UTC, with 29 assets, as Latest.

After publication every older GitHub release was deleted, as planned: 63 releases (58 published, 0.0.1 to 0.8.14, plus 5 stale drafts with no tag: v0.5.0 twice, v0.6.9, v0.7.14, v0.7.19), each by its release id through the API, which never deletes a git tag. Only 0.9.0 remains. `git ls-remote --tags origin` listed 115 lines (59 tags, with peel lines) before and after, byte-identical. Old versions stay available from their tags (`tree/vX`), and their notes stay in `CHANGELOG.md` and `docs/release-X-notes.md`; links to old release pages in frozen validation reports no longer resolve.

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
- Tag CI: no flakes. On the final tag `2dc5e712`, every run passed on
  attempt 1, with no reruns or force-cancels, and no setup step hung. The two
  earlier tag attempts were not flakes. On `c7412bc5` the runs were cancelled
  when the tag moved. On `29b2a88a`, Windows failed on a real build error: the
  Windows `far` macro clashed with a local in the debug harness, fixed in
  `2dc5e712`.
