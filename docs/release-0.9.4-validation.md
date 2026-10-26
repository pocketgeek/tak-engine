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

TODO-LEAD: paste the remote multiplayer sweep result (scenarios, referees,
modes, mismatches, new reference ticks and hashes), or state that it was
skipped.

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
- The remote multiplayer sweep: TODO-LEAD (see above).
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it. `voice` can fail on timing
  when the suite runs at -j16 on a loaded host.
- Operators: the tak.pgnet.us service drop-in may still pass `--crusades-db` or
  `--crusades-definition`; they are harmless (a warning at start) until removed.
- All of 0.9.3's limits carry over (see
  [release-0.9.3-validation.md](release-0.9.3-validation.md#limits)).
- Tag CI: TODO-LEAD.
