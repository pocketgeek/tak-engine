# 0.9.6 release validation

Protocol **300** (was 299 in 0.9.5), replay format **15** (unchanged),
generator version **9** (unchanged).

TODO-LEAD: tag line. Tagged `v0.9.6` at `<short sha>` (full `<sha>`, annotated tag object `<obj>`) and published on `<date time UTC>`.

## Scope and compatibility

0.9.6 follows 0.9.5 (protocol 299; tag `v0.9.5` -> `35ee5c27`). Thirty-nine
commits landed linearly on `35ee5c27` (no merges); the code tested below is
`1e1df882`. In order of landing, by what they change:

- **Player features (client only).** `cf9892e1`, `884de7e3`: double-click select
  (Shift adds, Ctrl removes; a triple click expands again; a consumed press
  breaks the chain). No sim or protocol change by themselves.
- **Producer rally plans (hashed).** `bd0bf5c0`, `e317b3c0`, `a7d8ef47`: Attack
  and Move-fight rallies, queued patrol loops, Stop and ending a builder's repeat
  clear the rally; a non-empty rally is folded into `stateHash`. `a7d8ef47`
  bumped `kNetVersion` 299 -> 300.
- **Mana bulb and AI shortfall.** `5b2bf4a8`, `601de583`, `22642bed`, `78c889e3`:
  the bulb's -N is mana requested; the Retail AI and the store-under-30%
  lodestone trigger read requested, not paid (`RetailResourceHistory` is one
  history `{produced, requested}` again). Not hashed.
- **Crusades scenarios.** `a08c0530`, `3c60d300`: a custom type no unit answers
  to is skipped as in retail (checked by `tools/re/check_scenario_missing_type.py`),
  a missing placed unit is refused by name, Test Map permits nothing for an
  other-balance Use Only entry, Cartographer's Check Map warns.
- **Construction health.** `b485ace8`, `9569f25e`: growth capped at `maxdamage` in
  64 bits and a hit rewrites a retail site's progress (`51a140`); the site's hp
  word is read signed. Only retail sites in the restored-save probe are affected.
- **Harness and tools.** `4601146e`, `9d82c1a0`: the headless harness keeps its
  seat under load and stops at the tick the result landed (`result=` on the
  done line; new `harness_seat_load`, fails before); `429e5c07`, `115054f5`: CLI
  polish (new `cli_tools`); `5b486619`, `7fadb9b6`: `mod_transfer` flake fixed,
  `ai_mod_profile` reports a stuck game.
- **Server silence.** `bee05edf`, `d35eb02c`, `cd5b8b30`, `48014de5`, `a246791c`:
  30 s allowance for a loading seat in a running room, 15 s in the lobby and in
  play; the server's own start-up stall shifts the silence clocks instead of
  letting them run (a reset would keep a dead client alive); the drop reason
  says why. New `server_load_timeout`.
- **Native Wayland.** `2fb7c6ce`, `f7abe39f`, `d0b0e3d5`, `ec414711`, `6da4c811`,
  `b836abf2`: NATIVE WAYLAND (Auto/On/Off) in Options, Display; Auto = any
  Wayland session including KDE Plasma, except GNOME without libdecor; the
  previous X11 window kind is measured (not read from `kwinrc`) so UI Scale and
  Cursor Size are converted only when their unit changes; the native scale is
  read once settled; a ratio that neither kind gives is "not measured"; a
  guessed kind is remembered; Cartographer re-centres after a conversion.
- **Cloak and revive (hashed).** `ec8a190d`: cloaking spends the unit's own mana,
  a pool that cannot pay decloaks for 90 ticks, `cloakcostmoving` defaults to the
  scaled `cloakcost`. `cff8ef46`, `31bba640`, `1e1df882`: revive costs no mana
  and requests none; the allocation paces it; `Unit::reviveWork` is retail's
  16.16 counter (`_ftol` low word); resurrect 0.3 of build time, animate full;
  a computer player under 0.7 allocation abandons it. `cadf3b29`, `1e1df882`:
  shots (600 ticks), builds, reclaim and resurrect (300) and repair (150) block
  cloaking (`World::blockCloak`), the enemy-proximity decloak is removed, and a
  restored build stage sets the block as well. `Unit::cloakBlockedUntil` is
  hashed when set.
- **Docs.** `35343c2b`, `8170b309`, and the 0.9.5 validation commits `36671415`
  and `d1546055`.

`git diff --stat v0.9.5 1e1df882 -- src`: 39 files changed, 1,313 insertions(+),
175 deletions(-) (`src/client` 18 files, `src/cartographer` 9, `src/sim` 8,
`src/ai` 2, `src/net` 1, `src/server` 1). Over the whole tree: 80 files
changed, 3,661 insertions(+), 284 deletions(-).

TODO-LEAD: final diff stat `v0.9.5..v0.9.6` (commits, files, src stat) once the preparation commit is landed.

The preparation commit on top changes no code: `CMakeLists.txt` (the version
line), `CHANGELOG.md` (dated, links), `README.md`, the version and protocol
lines in `docs/`, the notes, this report, `docs/img/README.md`,
`docs/img/title.jpg` and `docs/img/guide/options-graphics.jpg`.

**Compatibility.** `kNetVersion` is **300** in `src/net/protocol.h` (299 at
`v0.9.5`). `kReplayFormat` is **15** in `src/net/replayhdr.h`, unchanged. The
map generator's `formatVer` **9** is unchanged.

- **Multiplayer.** `Server::handshake` rejects a Hello whose protocol differs
  from `kNetVersion`; the release string is never compared. So **0.9.6 plays
  with no 0.9.5 or older client or server**.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  format is 1..15 and its protocol equals `kNetVersion`, so 0.9.6 refuses every
  0.9.5 (protocol 299) replay. `tools/replay_test.cpp` pins 300 and refuses 299.
- **Hashed state.** It changes for producer rally plans, the cloak block and
  the revive channel counter. Because the cloak block is hashed, **the gate
  hashes change** (below).
- **Maps, mods and settings.** No map, recipe or package format changed.
  `settings.ini` gains Wayland window records, which 0.9.5 ignores.

## Local preparation checks

All ran on Linux with optimized Debug builds (`build-o2`, `-O2 -g`, ccache +
mold, `TAK_TEST_DATA` set so the data-backed tests ran) and a Release build,
all at `1e1df882` in a detached worktree. Every takclient and takserver run used
scratch `HOME`, `XDG_DATA_HOME` and `XDG_CONFIG_HOME`.

- **Builds.** All targets built in `build-o2` and in Release: rc 0. At that
  commit the binaries print `0.9.5 (build v0.9.5-39-g1e1df8828733)`, because the
  version number changes in the preparation commit; the prepared tree's own
  build prints `0.9.6`.
- **Full suite**, nightly label included (`ctest -j12`, one run, 2,015 s wall
  time): **773 of 774** passed; the one failure is `vashim_unavailable` ("no
  display on a render node without libva"), the known failure on this host (the
  local `third_party/ffmpeg-bink` predates the libva shim, which CI builds); it
  was not rerun. `vashim_system_loader` skipped (known). No timeout-looking
  failures, so no solo reruns. All 67 `legion_check_*` tests ran and passed.
- **Legion scenario gate.** `legion_check.py validate --baseline
  tools/scenarios/baseline.json`: PASS, 0 problems; the check gate ran inside the
  67 `legion_check_*` tests and passed. The cloak-block hash change moved none of
  the `hash@` keys, so the baseline needed no retake.
- **Detmath guard** (`tools/check-detmath.sh`): OK (no direct libm
  transcendentals, no `std::atomic_ref`).
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): "DETERMINISM
  OK", golden `dcef618cd2e4d558` for g++ and clang++ at -O0, -O2 and -O3,
  unchanged from 0.9.5; the aarch64 legs were skipped (toolchain missing).
- **`--mpai` Inner Circle, seed 1** (`takserver --local --no-auth --seed 1`,
  `TAK_LEGION=1` for Legion, `TAK_AI_LEVEL=6` for the Retail AI, `--crusades` for
  the Crusades games, `TAK_SPEED=80`). Two full rounds gave identical hashes, and
  a Retail 60 s run at normal speed (1x) gave the same hash as at 80x.

| Mode | 60 s | 300 s |
|---|---|---|
| Retail pathfinding (Normal AI) | `d0da4ccc71d2067c` | `e820d37865085bd9` |
| Legion | `c3f6a3a13f0c167f` | `0783cb05f7f128cf` |
| Retail AI (`TAK_AI_LEVEL=6`) | `4952493f51b9a568` | not run |
| Crusades, Retail pathfinding | not run | `f2f51dc1703fc209` |
| Crusades, Legion | not run | `c4189ae189dd68dc` |

**Why every gate hash changed.** All seven differ from 0.9.5
(`d375d03a6c472773`, `387df8b0916a182e`, `426ab0842b709c28`,
`2d09d13d3ef0d324`, `c5846d769e0a7d93`, `3a380ae964f75b5a`,
`772874591d07dbdf`), and the change is attributed to one thing: the cloak block
(`cloakBlockedUntil`) became hashed state on cloak-capable units, and the AIs
cloak nothing, so no game behaves differently. At the integration commit
`a7d8ef47` (rally plans only) the five gates at `TAK_SPEED=80` were still
the 0.9.5 values; the three cloak commits changed them. With the block left out of the
hash, every gate kept its old value (`cadf3b29`). Because the gate games do not
change behaviour, the new values are the same games hashed with one more field.
The cross-compiler golden is unchanged because that harness has no cloaker.

- **Rally, cloak, resurrect and animate** are covered by their own proving tests
  (`doubleclick`, the rally and cloak tests, `reviveWorkWrapsLikeFtol`) in the full
  suite, not by an order-driven game.

## Screenshots

Two pictures were retaken, by the prepared tree's build (Debug `-O2`, CMake
`VERSION 0.9.6`):

- `docs/img/title.jpg`, with the 0.9.1 title capture (`takclient menu --shot`,
  offscreen driver, 1920 x 1080 window cropped to the 1440 x 1080 artwork;
  script `~/tak-tmp/scratch/release-096/shots/title.sh`). It reads VERSION 0.9.6
  and shows the three doors.
- `docs/img/guide/options-graphics.jpg`, which now shows the **NATIVE WAYLAND**
  row (Auto) in Options, Display. It was captured with the 0.9.1 capture-only
  patch applied to the working tree and then removed again (not committed).

No picture shows double-click select, rally markers, cloaking or the mana bulb
in a way that changed visibly, so none of those was retaken or deleted.
`docs/img/README.md` lists the details.

## Desync sweeps

Both sweeps ran against `1e1df882` (protocol 300, replay format 15; the binaries
report `0.9.5 (build v0.9.5-39-g1e1df8828733)` because the version line changes
in the preparation commit), and both are clean.

**Local.** `BUILD=build-o2 tools/desync-hunt.sh --minutes 20 --jobs 8` (Debug
`-O2 -g`, cores 16-23, scratch `HOME` and XDG folders) ran all 34 cases in 35
minutes (03:29 to 04:04 UTC on 2026-10-27). 32 cases compared real hashes and 2
benchmark cases (`flow-bench-high`, `flow-bench-absurd`) were flow-only (they
seat a spectator and compare nothing). They covered 957,579 ticks; 17 games
reached the 36,000-tick limit and 17 ended with a winner; the largest end state
was 7,031 units (`stress`). The summary reads "no desyncs in any run", the "did
not finish cleanly" list is empty, no case is missing (no NEVER RAN), every run
ended `err=none`, and a search of all 68 logs found no `DESYNCED`, `REFEREE
SUSPECT`, client desync, crash or assert line.

**Remote multiplayer.** `tools/desync-hunt-remote.sh --validate --minutes 10`,
`TAK_WALL_TIMEOUT=3600`, run as in 0.9.4. Referees on `tak.pgnet.us` (10 at a
time) and `vpn3.pgnet.us` (6 at a time) ran a sweep-only binary,
`~/takserver.sweep096`, built in the `tak-sweep-builder-0719` Ubuntu 24.04
container (Debug `-O2 -g`, needs at most GLIBC_2.38, SHA-256
`45b590923ce67316e9c8157978257c0a14d4982f48b9634d976abadb9195078b`). Each mode
had its own replay and map-cache folders and ports. The clients were the local
`build-o2` takclient on cores 4-15. Retail ran 03:30:40 to 03:49:11 UTC and
Legion 03:49:11 to 04:17:28 UTC, both single passes with no retries.

All three `--validate` gates passed in both modes: "both at
v0.9.5-39-g1e1df8828733", the planted desync reported as "game 1: client 1
DESYNCED at tick 900", and game mods transferred with the incorrect loaded
gameplay rejected.

| Mode | Cases / seats | Ticks | Client-pair checkpoints | Client-referee checkpoints |
|---|---|---|---|---|
| Retail | 37/37, 52/52, "no desyncs reported" | 594,938 (7 games ended with a winner) | 12,215 across 22 seat pairs, 0 mismatched | 2,752 across 51 comparisons, 0 mismatched |
| Legion | 37/37, 52/52, "no desyncs reported" | 592,278 (10 ended with a winner) | 10,243 across 22 seat pairs, 0 mismatched | 2,691 across 51 comparisons, 0 mismatched |

35 cases per mode seated players and 2 were flow-only (`w-allai-stress`,
`w-allai-bench`). The only DESYNCED line in either mode is the planted one in
`validate.server.log`. All 37 referee replay headers per mode read format 15,
protocol 300, engine `0.9.5` and the right mode. No seat was lost.

**One change from 0.9.4's sweep:** the server logged "slot N held the game"
lines, all in the two-player stress cases (Retail `2h-stress` 3 lines,
`2h-orders-stress` 26; Legion 15 and 13), each lasting 1.0 to 7.9 s; one Legion
slot got down to "0s of its wait left". 0.9.4 had none. The local machine's load
was 28 to 34 on 24 cores while both sweeps ran at once, so the most likely cause
is local load slowing the clients; this was not tested.

**What the sweeps did not drive.** The AIs never give rally, cloak, resurrect or
animate orders, `TAK_AUTOPLAY` issues only random AttackMove and SetSquad
commands, and no unit starts cloaked. So actual cloaking, revive, animate and
producer rally orders were never sent between a client and a referee. Every
game did exercise the hashed cloak-block timer, the AI's mana-requested reading,
the construction hit-point cap, the 30 s loading allowance and the start-up
silence shift. Covering the orders would need new harness hooks.

**Cleanup.** `~/takserver.sweep096`, the four `tak_replay.sweep096-*` and
`tak_cache.sweep096-*` folders and this sweep's `/tmp/tak-*` logs were removed
from both remote hosts, and no sweep process remained. The public `takserver` on
`tak.pgnet.us` was not touched (same process before and after).

## Native Wayland testing

Wayland was tested only in a nested KWin 6.7.5 (at scale 1.45, 1.5 and 2,
windowed and fullscreen, with a 0.9.5 settings file, a stretched X11 display
and the way back), by the unit-level `settings_test` cases for each upgrade
history (KDE default, GNOME stretched, docked and undocked monitors, a history
with a scale but no width), and by hand by the maintainer on a KDE laptop and a
57-inch monitor. GNOME, Sway and Hyprland native windows, and GNOME without
libdecor, were not run on real desktops. Windows and macOS code paths are
unchanged.

The maintainer's by-hand result, in their words: "wayland on my laptop here kde seems to be just fine"; on the 57-inch monitor, "Edge scrolling still worked after alt-tabbing I tested on the 57, seems fine."

## CI and packages

TODO-LEAD: tag CI (Linux packages, Windows, macOS, Determinism run ids, results, reruns), the 29 assets and their SHA-256 checks, version strings in every takclient and takserver, licence files, `--help`, the RPM server and `--play-map` smoke, and every `blob` `v0.9.6` link in the release notes.

TODO-LEAD: Authenticode on oden-win (7 signed files) and macOS notarisation and stapling on the MacBook (DMGs and apps).

TODO-LEAD: publication line (`gh release edit v0.9.6 --draft=false --latest`, date, asset count; 0.9.0 to 0.9.5 stay on the releases page).

## Limits

- No game mixing 0.9.6 with 0.9.5 or older is possible: protocol 300 is refused
  by every older server and client. No mixed-version game was run with a real
  0.9.5 binary.
- No Windows or macOS `--mpai` for 0.9.6; their tag CI determinism tests cover
  them.
- Rally, cloak, resurrect and animate orders were not exercised between a client
  and a referee (see the sweeps above).
- Wayland: see the section above; no real GNOME, Sway or Hyprland run.
- The sweeps' "held the game" lines in two-player stress cases are unexplained
  beyond local load.
- Local only: `vashim_unavailable` fails against a `third_party/ffmpeg-bink`
  built before the shim; CI builds FFmpeg with it.
- Attack and Move-fight rally steps are dropped for unarmed outputs, and a mobile
  builder holds a rally only while it repeats.
- Screenshots and Cartographer saves still go into the data folder (decided in
  0.9.5). The Windows `linkOrCopy` fallback skips directories.
- TODO-LEAD: Limits from tag CI (failures, reruns, slow jobs).
- All of 0.9.5's limits carry over except the `mod_transfer` flake and the
  construction-health overflow, both fixed (see
  [release-0.9.5-validation.md](release-0.9.5-validation.md#limits)).
