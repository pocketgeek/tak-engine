# 0.9.4 release validation

Protocol **298** (was 297 in 0.9.3), replay format **15** (unchanged),
generator version **9**. The Crusades campaign payload and database schema of
0.9.3 (4 and 9) no longer exist.

TODO-LEAD: tag line (tag `v0.9.4` -> short and full commit sha, annotated tag
object, "TAK Engine 0.9.4", publication time in UTC, as in the 0.9.3 report).

## Scope and compatibility

0.9.4 follows 0.9.3 (protocol 297; tag `v0.9.3` -> `a255d930`, validation commit
`2e32b9a1`). Two branches were cherry-picked onto main in this order and
integrated at `2651b032`:

- **Crusades balance as in the original game** (`3d3e02eb`, `be1dc213`,
  `1dbfeb0b`). The original game never reads `canbuildcb/`; with Crusades on it
  looks units up in `unitscb/` only (no fallback to `units/`) and builds every
  menu from the standard `canbuild/` tree, kept to roster types and sorted by
  ascending `[Menu] Priority`. `setupRegistry` follows (`monelem`, `monmum`,
  `tarcship`, `tarhel`, `tarnecr2` and `zonspide` no longer exist in a Crusades
  game); `canbuildcb/` leaves the gameplay-data hash; an install without
  `unitscb/` plays the standard roster; new data-backed `balance_roster_test`
  (32 failures before the change). Hashed state changes for Crusades games only.
  Evidence for the rule is in `docs/crusades-balance-reference.md`.
- **Darien Crusades meta game removal** (eight commits, `74bc2fba` to
  `f4a76466`, `d5ec2c17`): shared pieces moved first (`kMaxReplayBytes`,
  `tak::srv::FileLease`, `tools/server_net.py`, new `account_auth_network`);
  then the client screens and `MpClient` campaign API, the server side, the 22
  `Crusades*` messages, event kind 3, the campaign store, SQLite and
  `crusades_admin`; the Boneyards art mount; CI, test costs and packaging; the
  territory research tool; the meta-game docs and research. Hashed state does
  not change for ordinary games; the wire does.
- `2651b032` (integration): `kNetVersion` 297 -> 298, `tools/replay_test.cpp`
  pins 298 and refuses 297; `--crusades-db` and `--crusades-definition` print a
  warning and are ignored (with their value) so a 0.9.3 service drop-in still
  starts; `account_auth_network` checks that; CHANGELOG, `multiplayer-design.md`,
  `public-server.md`, `user-guide.md`.

TODO-LEAD: diff stat `git diff --stat v0.9.3 v0.9.4 -- src` and over the whole
tree, the commit count between the tags, and the list of files outside `src`
(taken at the tag). Measured at `2651b032` for reference: whole tree 171 files
changed, 1,233 insertions(+), 20,764 deletions(-); `src` 44 files changed, 272
insertions(+), 6,098 deletions(-) (`src/client` 10 files, `src/hpi` 2, `src/net`
8, `src/server` 20, `src/sim` 4).

The preparation commit on top changes no code: `CMakeLists.txt` (the version
line), `CHANGELOG.md` (dated, links), `README.md`, the version lines in `docs/`,
the notes, this report, `docs/img/README.md` and `docs/img/title.jpg`.

**Compatibility.** `kNetVersion` is **298** in `src/net/protocol.h` (297 at
`v0.9.3`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** is unchanged.

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared. `Hello`, `Welcome`
  and `Reject` keep their message ids, so an old peer is refused cleanly instead
  of misreading a shifted message. So **0.9.4 plays with no 0.9.3 or older
  client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.4 refuses every
  0.9.3 (protocol 297) replay. `tools/replay_test.cpp` pins `kNetVersion == 298`
  and refuses 297.
- **Game-data check value.** `canbuildcb/` is no longer counted, so the
  gameplay-data hash is `d6000f4fa9095a51` for every install (it changed for
  all of them); it matters only together with the protocol change.
- **Maps, mods and settings.** No map, recipe or package format changed; see the
  settings check below.

## Local preparation checks

All ran on Linux, cores 4-23, with optimized Debug builds (`build-o2`, `-O2 -g`,
ccache + mold), configured with `TAK_TEST_DATA` so the data-backed tests ran,
on the integrated tree at `2651b032`. Extra testing was allowed for this
release.

- **Builds.** Debug `-O2` and Release: every target builds. The Release
  `takserver --help` mentions no Crusades option and `takserver` does not link
  SQLite. Leftover binaries of the removed targets were deleted from the main
  `build/` and `build-dbg/` trees after a rebuild of every target.
- **Full suite.** `ctest -LE nightly -j16`: **600 of 604** passed, 1 skipped
  (`vashim_system_loader`), 3 failed, none a defect of the change:
  - `vashim_unavailable`: the known failure on this host (the local
    `third_party/ffmpeg-bink` predates the libva shim, which CI builds).
  - `voice`: a timing failure under load; passed when run alone.
  - `account_auth_network`: that run used a `takserver` built before the
    warn-and-ignore change; after rebuilding it passed, and so did the 31 tests
    matching `server_`, `disconnect`, `spectat`, `replay`, `acme` and
    `mod_transfer`.
- **Detmath guard** (`tools/check-detmath.sh`): OK.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): "DETERMINISM
  OK", golden `dcef618cd2e4d558` for g++ and clang++ at -O0, -O2 and -O3,
  unchanged from 0.9.3; the aarch64 legs were skipped (toolchain missing).
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`, a
  clean scratch XDG_DATA_HOME per game, `TAK_LEGION=1` for Legion,
  `TAK_AI_LEVEL=6` for the Retail AI, `TAK_SPEED=80`), two runs each. All five
  standard hashes are identical in both runs and unchanged from 0.9.3, and a
  Retail 60 s run at normal speed (1x) gave the same hash as at 80x. Every run
  ended with `err=none` at the time limit.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d375d03a6c472773` | `387df8b0916a182e` |
| Legion | `426ab0842b709c28` | `2d09d13d3ef0d324` |
| Retail AI (`TAK_AI_LEVEL=6`) | `c5846d769e0a7d93` | not run |
| Crusades, Retail pathfinding | not run | `3a380ae964f75b5a` (new: Crusades roster) |
| Crusades, Legion | not run | `772874591d07dbdf` (new: Crusades roster) |

  The two Crusades hashes repeat run to run and match the balance branch before
  integration; they changed on purpose (the roster and menus), and there is no
  0.9.3 Crusades reference to compare them with.

### Compatibility checks

Run against the integrated build with scratch HOME and XDG_DATA_HOME folders.

- **(a) Accounts.** A `takserver` started without `--no-auth`. The client
  registered "Tester" with `--user`/`--pass` and played a 30 s game (tick 900,
  hash `1242304dcfd1abd6`). A wrong password was refused with "that password is
  not right". (That server did not save a replay because its `--replaydir`
  folder did not exist, as on 0.9.3.)
- **(b) Old server setup.** A `takserver` given both removed flags, a
  `campaign.sqlite`, a `crusades-replays/` folder and an existing accounts file
  printed both warnings and started. The existing account logged in and a
  Crusades room played. The store and the `crusades-replays/` folder were left
  unchanged, the replay was written, and `TAK_REPLAY_VERIFY` replayed it to tick
  903 with no checkpoint complaint.
- **(c) Old settings.** A 0.9.3 `settings.ini` with non-default values plus some
  made-up meta-game keys (0.9.3 never wrote any) was loaded and resaved: the
  made-up keys were dropped and every other setting was kept, including
  `gameCreate.crusades = 1` and `campaigndone`. The only other change was
  `accountName = Tester`, from signing in.
- **(d) Old replay and old client.** A 0.9.3 Crusades replay is refused with
  "recorded under protocol v297, this build is v298 -- this replay protocol is
  unsupported" (exit 1, no crash). A 0.9.3 client connecting to the 0.9.4 server
  is refused with "protocol version mismatch (server 298, client 297)".
- **(e) Screens.** Looked at in screenshots taken with the debug client (the
  release build has no screenshot option): the main menu shows only the three
  doors, settings and credits; the multiplayer browser shows only Refresh,
  Create and Back; the create screen has CRUSADES: OFF, and clicking it and
  pressing CREATE gives a room showing CRUSADES: ON whose server loaded the
  Crusades data.

No 0.9.3 build ran in a game with a 0.9.4 build (the refusal in (d) is the 0.9.3
client's message as received from the 0.9.4 server; a real 0.9.3 binary did not
join). Not run: macOS and Windows builds of the removal, CPack DEB/RPM, a
public-server-style TLS run and the desync sweeps.

## Screenshots

Only `title.jpg` was retaken, by the prepared tree's build (Debug `-O2`, CMake
`VERSION 0.9.4`), with the 0.9.1 title capture (`takclient menu --shot`,
offscreen driver, 1920 x 1080 window cropped to the 1440 x 1080 artwork; script
`~/tak-tmp/scratch/release-094/shots/title.sh`). It reads VERSION 0.9.4 and shows
the three doors. `docs/img/README.md` has a "Retaken for 0.9.4" note and the
updated row. No picture showed the removed meta game (it was hidden from the
menu and the game browser) or a Crusades-balance build menu, so no other
picture was retaken or deleted.

## Remote multiplayer sweep

Verdict: clean. No desync in either pathfinding mode.

The full 37-scenario table passed for both Retail and Legion on 2026-10-26
(UTC). Both ran at `2651b032`, the 0.9.4 integration head of `main` (protocol
298, replay format 15, build id `v0.9.3-14-g2651b03235a2`). The CMake project
version was still 0.9.3 at that sha, so the swept binaries and replay headers
say engine `0.9.3`. The release-prep commits after `2651b032` change only the
version, documentation and the title image, not simulation, network or server
code. Each mode passed in a single pass with no retries. No seat was lost and
no `stopped responding` or `held the game` line appeared in any case.

- Retail: 06:07-06:25 UTC (17 minutes including the gates).
- Legion: 06:25-06:53 UTC (28 minutes including the gates).
- Repeats: four Retail and four Legion cases were repeated afterwards.

Referees ran on `tak.pgnet.us` (24 runs, 10 at a time) and `vpn3.pgnet.us` (13
runs, 6 at a time), with optimized local clients pinned to CPUs 4-23.

- Isolation: each host ran its own referee binary, `~/takserver.sweep094`, with
  separate replay and map-cache folders per mode. Ports: Retail 8690/8691 and
  8700+; Legion 8790/8791 and 8800+; repeats 8900+ and 9000+. The public
  `takserver` on `tak.pgnet.us` (port 7677) was not touched: it was the same
  process (PID 85437, started 04:41 UTC on Oct 26) before and after. Every file
  the sweep put on either remote was removed afterwards.
- Duration: each scenario allowed ten simulated minutes (18,000 ticks) or ended
  at an actual result for that seat.
- Coverage: real seated players provide the client/referee checksum coverage.
  The two all-AI spectator scenarios per mode (`w-allai-stress`,
  `w-allai-bench`) test flow control, not hashes.
- Mode: chosen through the hosting client's room options (`TAK_LEGION=1`
  creates a Legion room). `TAK_WALL_TIMEOUT=3600` was set for both modes.
- Crusades balance: `--crusades` rooms ran in both modes (`h-crusades`,
  `h-naval-crus`, `h-everything`, `h-two-castles`, `2h-crusades`), so the
  retail-selected `unitscb` roster was exercised against the referee.
  `h-everything` fields every unit type.
- Replay headers: every recorded referee replay header confirms the intended
  mode, replay format 15, protocol 298 and engine `0.9.3`. There are 37 headers
  per mode, seeds 2000-2036, each mapped to its case.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 (single pass) | 52/52 | 11,848 across 22 seat pairs, 0 mismatched | 2,706 across 51 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (single pass) | 52/52 | 10,216 across 22 seat pairs, 0 mismatched | 2,686 across 51 seat/referee comparisons, 0 mismatched |

Each run passed all three `--validate` gates before its table started:

- Build identity: `both at v0.9.3-14-g2651b03235a2`.
- Planted desync: the referee reported `game 1: client 1 DESYNCED at tick 900`.
- Game mod transfer: `verified game mods transferred and incorrect loaded
  gameplay rejected`.

Referee binary: the gate checks only `tak.pgnet.us`. The binary on
`vpn3.pgnet.us` is the same file: SHA-256
`ae796308dffba7f3f6c9b40ac18ed8153abac97a8878269fbe2ee25d314955a1`, `--version`
`0.9.3 (build v0.9.3-14-g2651b03235a2)` on both. It was built from this exact
source in the Ubuntu 24.04 container as Debug `-O2 -g` and needs at most GLIBC
2.38. The client is a Debug `-O2 -g` build of the same sha.

No desyncs: no natural `DESYNCED`, `REFEREE SUSPECT` or client-reported desync
appeared in any case log; the only such line in each run is the planted one in
`validate.server.log`. Every seated client ended `err=none` (37 per mode). Both
sweeps ended with "no desyncs reported (52 seats, all completed)". The repeats
ended with "(4 seats, all completed)" for Retail and "(5 seats, all completed)"
for Legion.

What was compared: a state hash at every checkpoint, plus the position digest
wherever both sides carry one. Every game wrote a referee replay (37 per mode).
The harness (`tools/desync-hunt-remote.sh` and the checkpoint comparison
script) is the 0.9.1 version, unchanged. It ran with the short `TMPDIR`
`/home/pocket_geek/tk`, because a longer path overflows the ssh ControlPath
limit.

Seat budget: no seat was lost in either mode and neither mode charged any wait
time. The end-of-game `slot N disconnected -- waiting` lines are the headless
clients quitting after the game ended; they are not stalls.

### Reference ticks and hashes (0.9.4)

Hash and units are the seated host's final line.

Hashes and outcomes are not comparable with 0.9.1. 0.9.2 and 0.9.3 changed
hashed simulation state, and 0.9.4 adds the retail Crusades roster, build menus
and gods. Only 2 of 74 end/tick/units results match 0.9.1 (both Legion:
`h-fog-explored` and `h-two-castles`), and only Legion `h-two-castles` kept its
hash. The "vs 0.9.1" column is for reference only. Whether the new numbers are
stable is answered by the repeats below.

Expected noise: the six `TAK_AUTOPLAY` cases never repeat a result. `h-lake`,
`h-stress`, `2h-stress`, `2h-orders-stress` and `4h-gods` are load-sensitive.

| Case | Retail end | Retail tick | Retail hash | Retail units | vs 0.9.1 | Legion end | Legion tick | Legion hash | Legion units | vs 0.9.1 |
| --- | --- | ---: | --- | ---: | --- | --- | ---: | --- | ---: | --- |
| `h-baseline` | timelimit | 18,000 | `ff349c72410fcdb9` | 182 | differs (units 188 -> 182) | timelimit | 18,000 | `e43ccd5d2f5a1dad` | 226 | differs (units 195 -> 226) |
| `h-gods` | concluded | 11,985 | `868811731f906612` | 69 | differs (tick 14,197 -> 11,985; units 75 -> 69) | concluded | 14,002 | `12b116d3cc37ad49` | 63 | differs (tick 13,445 -> 14,002; units 59 -> 63) |
| `h-crusades` | timelimit | 18,000 | `67aed96eddbaaa25` | 218 | differs (units 216 -> 218) | timelimit | 18,000 | `e51b6d0f02416ccb` | 207 | differs (units 211 -> 207) |
| `h-stress` | concluded | 4,967 | `3fba65f4cf37c726` | 8,227 | differs (tick 5,200 -> 4,967; units 8,789 -> 8,227) | concluded | 4,787 | `f5889e7f369324e8` | 7,596 | differs (tick 4,719 -> 4,787; units 7,910 -> 7,596) |
| `h-absurd` | timelimit | 18,000 | `b72a2b851da22fe9` | 430 | differs (units 463 -> 430) | timelimit | 18,000 | `2432537c303503d8` | 372 | differs (units 421 -> 372) |
| `h-fog-explored` | timelimit | 18,000 | `d898dea2339c33fd` | 173 | differs (units 193 -> 173) | timelimit | 18,000 | `c862b143fbedb07c` | 180 | same |
| `h-fog-full` | timelimit | 18,000 | `f5f114e8c58a0faa` | 202 | differs (units 226 -> 202) | timelimit | 18,000 | `f2310af1927e27bd` | 227 | differs (units 226 -> 227) |
| `h-unitcap` | timelimit | 18,000 | `7450b3d416d61583` | 204 | differs (units 226 -> 204) | timelimit | 18,000 | `7232f0a3761d4f6a` | 232 | differs (units 221 -> 232) |
| `h-cramped` | concluded | 1,217 | `e91c922bd5c90c7f` | 11 | differs (tick 1,359 -> 1,217; units 9 -> 11) | timelimit | 18,000 | `373b9a600f369aa3` | 31 | differs (units 37 -> 31) |
| `h-naval` | timelimit | 18,000 | `6b5970439ed5adfa` | 83 | differs (units 92 -> 83) | timelimit | 18,000 | `289e275165865ba0` | 50 | differs (tick 15,169 -> 18,000; units 73 -> 50; concluded -> timelimit) |
| `h-naval-crus` | timelimit | 18,000 | `07d14b34cce76969` | 88 | differs (units 74 -> 88) | concluded | 16,891 | `d4b488fbaee9d0e5` | 94 | differs (tick 16,487 -> 16,891; units 93 -> 94) |
| `h-lake` | concluded | 3,720 | `f7c99a5e2a4021e0` | 4,953 | differs (tick 4,242 -> 3,720; units 4,600 -> 4,953) | concluded | 3,726 | `d3457f198bdbaf0f` | 4,979 | differs (tick 4,378 -> 3,726; units 4,653 -> 4,979) |
| `h-random-starts` | timelimit | 18,000 | `95d0599caf7c1eb9` | 265 | differs (tick 13,068 -> 18,000; units 189 -> 265; concluded -> timelimit) | concluded | 14,142 | `9afb3238138446df` | 253 | differs (tick 13,051 -> 14,142; units 215 -> 253) |
| `h-monarch-exp` | timelimit | 18,000 | `1bc57c56d69e99ad` | 137 | differs (tick 11,761 -> 18,000; units 88 -> 137; concluded -> timelimit) | concluded | 11,642 | `6f2f55551c1ba786` | 59 | differs (tick 11,753 -> 11,642; units 60 -> 59) |
| `h-mods-all` | timelimit | 18,000 | `83905dc1a645a576` | 169 | differs (units 172 -> 169) | timelimit | 18,000 | `2230d5d72e16f25d` | 189 | differs (units 199 -> 189) |
| `h-everything` | timelimit | 18,000 | `ac37e38430583e2b` | 4,889 | differs (units 5,246 -> 4,889) | timelimit | 18,000 | `72e46fdc0c743374` | 3,653 | differs (units 4,997 -> 3,653) |
| `h-speed-1x` | timelimit | 18,000 | `1d7c9371a7604ed5` | 225 | differs (units 202 -> 225) | timelimit | 18,000 | `6306720044eb2666` | 210 | differs (units 245 -> 210) |
| `h-two-castles` | timelimit | 18,000 | `1e78ebf79bab3dc9` | 25 | differs (units 27 -> 25) | timelimit | 18,000 | `b200d4fe20647a7f` | 4 | same |
| `2h-baseline` | timelimit | 18,000 | `6353e3b7ba4b13f7` | 177 | differs (units 146 -> 177) | timelimit | 18,000 | `a8256a110ef3b633` | 193 | differs (units 181 -> 193) |
| `2h-gods` | concluded | 11,588 | `1e8c155d56f9cd2a` | 73 | differs (tick 12,434 -> 11,588; units 60 -> 73) | concluded | 11,696 | `865ed3976d4c1f26` | 69 | differs (tick 16,301 -> 11,696; units 81 -> 69) |
| `2h-crusades` | timelimit | 18,000 | `618ebb96d114c871` | 190 | differs (units 195 -> 190) | timelimit | 18,000 | `a5f408f4304e2c6d` | 170 | differs (units 193 -> 170) |
| `2h-stress` | concluded | 5,359 | `b9c7f7e46f962d76` | 9,055 | differs (tick 5,109 -> 5,359; units 9,285 -> 9,055) | concluded | 5,131 | `b97a54031f1ce4f4` | 8,013 | differs (tick 5,197 -> 5,131; units 8,564 -> 8,013) |
| `2h-orders` | timelimit | 18,000 | `40edd801aa4acf8c` | 164 | differs (units 195 -> 164) (autoplay) | timelimit | 18,000 | `ce6bc036d7b92a87` | 194 | differs (units 202 -> 194) (autoplay) |
| `2h-orders-stress` | concluded | 5,076 | `6b1c3d8b9ccdd420` | 9,035 | differs (units 9,386 -> 9,035) (autoplay) | concluded | 4,949 | `394c9bbcebfc194e` | 8,088 | differs (tick 18,000 -> 4,949; units 5,237 -> 8,088; timelimit -> concluded) (autoplay) |
| `4h-orders` | timelimit | 18,000 | `3dda119cf6a4b17c` | 152 | differs (units 161 -> 152) (autoplay) | timelimit | 18,000 | `389898f3d33b348c` | 161 | differs (units 148 -> 161) (autoplay) |
| `3h-baseline` | timelimit | 18,000 | `7536f76ba1649578` | 181 | differs (units 179 -> 181) | timelimit | 18,000 | `09a8f631dfbf6ff7` | 161 | differs (units 162 -> 161) |
| `4h-gods` | timelimit | 18,000 | `039e47c00ecca544` | 161 | differs (units 119 -> 161) | timelimit | 18,000 | `79281b9d8bbfa0d9` | 155 | differs (units 144 -> 155) |
| `net-rtt50` | timelimit | 18,000 | `c5af6ec8774a316f` | 231 | differs (units 219 -> 231) | timelimit | 18,000 | `6e39f20abcf6b34c` | 259 | differs (units 238 -> 259) |
| `net-rtt100` | timelimit | 18,000 | `b123e50112f6e6a6` | 206 | differs (units 173 -> 206) | timelimit | 18,000 | `0723697c99799ada` | 185 | differs (units 180 -> 185) |
| `net-jitter` | timelimit | 18,000 | `0dd5c41786cbbb76` | 211 | differs (units 185 -> 211) | timelimit | 18,000 | `6b97f48ec8d767fa` | 193 | differs (units 207 -> 193) |
| `net-rtt500` | timelimit | 18,000 | `b25f4f8b415cb2bd` | 222 | differs (units 224 -> 222) | timelimit | 18,000 | `4567deba99204f13` | 223 | differs (units 185 -> 223) |
| `net-loss1` | timelimit | 18,000 | `d4305992538ec269` | 209 | differs (units 217 -> 209) | timelimit | 18,000 | `d2219171c4d90fa1` | 178 | differs (units 224 -> 178) |
| `net-loss3-orders` | timelimit | 18,000 | `710edbdb1a138c54` | 181 | differs (units 208 -> 181) (autoplay) | timelimit | 18,000 | `5ab9bbe82168526f` | 193 | differs (units 205 -> 193) (autoplay) |
| `net-orders100` | timelimit | 18,000 | `7b1641f7347fc5ef` | 202 | differs (units 210 -> 202) (autoplay) | timelimit | 18,000 | `53809d7da8db90e6` | 171 | differs (units 179 -> 171) (autoplay) |
| `net-2h-rtt100` | timelimit | 18,000 | `c5617723ea9298a7` | 149 | differs (units 161 -> 149) (autoplay) | timelimit | 18,000 | `4b188d9c85c0775f` | 186 | differs (units 155 -> 186) (autoplay) |
| `w-allai-stress` | timelimit | 18,000 | `0edbf0b319453687` | 5,723 | differs (units 6,139 -> 5,723) | timelimit | 18,000 | `a0e8793d7b611f0d` | 3,475 | differs (units 3,370 -> 3,475) |
| `w-allai-bench` | timelimit | 18,000 | `dd6df1af87368b48` | 323 | differs (units 368 -> 323) | timelimit | 18,000 | `63d703c4c68071b4` | 397 | differs (units 419 -> 397) |

### Repeats: the new references reproduce

Eight cases with no autoplay and no load sensitivity were run a second time on
the same hosts. Each gave exactly the same end, tick, hash and units as in the
first pass:

- Retail: `h-baseline` (`ff349c72410fcdb9`, 182 units), `h-crusades`
  (`67aed96eddbaaa25`, 218 units), `h-naval-crus` (`07d14b34cce76969`, 88
  units), `h-everything` (`ac37e38430583e2b`, 4,889 units).
- Legion: `h-baseline` (`e43ccd5d2f5a1dad`, 226 units), `h-crusades`
  (`e51b6d0f02416ccb`, 207 units), `2h-crusades` (`a5f408f4304e2c6d`, 170
  units), `h-everything` (`72e46fdc0c743374`, 3,653 units).

The repeats include the Crusades-balance cases in both modes, so the retail
roster and build-menu selection is deterministic across hosts and compilers.

There is no desync anywhere: every seat agreed with the referee at every
checkpoint in every case, in both modes and in the repeats.

## CI and packages

TODO-LEAD: tag CI runs (Determinism, Linux, Windows, macOS) with links and
outcomes; the asset list, digests and file sets compared with 0.9.3 (the
Linux, Windows and macOS packages should list `crusades_admin` nowhere); build
strings `v0.9.4`; no libsecret/glib `NEEDED`; licence files; the packaged
`takserver --help` without Crusades options; a packaged `takserver` Retail AI
`--mpai` run.

Windows: TODO-LEAD Authenticode results (0.9.3 had 9 signed files: the x64 installer plus `takclient`, `takserver`, `cartographer` and `crusades_admin` in the x64 and arm64 zips; with `crusades_admin` gone the expected set is 7).

macOS: TODO-LEAD notarisation and stapling results.

Publication: TODO-LEAD `gh release edit v0.9.4 --draft=false --latest` time and
asset count; 0.9.0 to 0.9.3 stay on the releases page.

## Limits

- No game mixing 0.9.4 with 0.9.3 or older is possible: protocol 298 is refused
  by every older server and client. No mixed-version game was run with a real
  0.9.3 binary.
- A Crusades-balance replay that contains one of the six removed units could
  not be played in 0.9.4; every 0.9.3 replay is refused anyway.
- The macOS and Windows builds of the removal and CPack DEB/RPM were not run
  locally; TODO-LEAD tag CI result.
- The remote multiplayer sweep ran against `2651b032` (engine string `0.9.3`, see above); the release-prep commits do not touch simulation, network or server code.
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it. `voice` can fail on timing
  when the suite runs at -j16 on a loaded host.
- Operators: the tak.pgnet.us service drop-in may still pass `--crusades-db` or
  `--crusades-definition`; they are harmless (a warning at start) until removed.
- All of 0.9.3's limits carry over (see
  [release-0.9.3-validation.md](release-0.9.3-validation.md#limits)).
- Tag CI: TODO-LEAD.
